#ifndef ZRA_ZRA_H
#define ZRA_ZRA_H

#ifdef ZRA_EXPORT_HEADER
#include "zra_export.h"
#else
#ifdef WIN32
#define ZRA_EXPORT __declspec(dllimport)
#else
#define ZRA_EXPORT
#endif
#endif

#ifndef __cplusplus
#include <stddef.h>
#include <stdint.h>
#else
#include <cstddef>
#include <cstdint>

extern "C" {
#endif

/**
 * @brief This enumerates all of the errors codes provided by ZRA
 */
enum ZraErrorCode {
  Success,                 //!< The operation was successful
  ZStdError,               //!< An error was returned by ZStandard
  HeaderInvalid,           //!< The header in the supplied buffer was invalid or the header hasn't been fully written
  OutOfBoundsAccess,       //!< The specified offset and size are past the data contained within the buffer
  OutputBufferTooSmall,    //!< The output buffer is too small to contain the output (Supply null output buffer to get size)
  CompressedSizeTooLarge,  //!< The compressed output's size exceeds the maximum limit
  InputFrameSizeMismatch,  //!< The input size is not divisble by the frame size and it isn't the final frame
};

/**
 * @brief A structure that represents an error code recieved from a function
 * @note The structure might be returned in it's negated form from functions
 */
union ZraError {
  struct {
    int32_t zra;   //!< The error code from ZRA
    int32_t zstd;  //!< The error code from ZSTD
  };
  int64_t raw;  //!< The overall error code when combining both the ZRA and ZSTD error code
};

/**
 * @return The version of ZRA the library linked to this uses
 */
ZRA_EXPORT uint16_t ZraGetVersion();

/**
 * @param code The error code that should be described
 * @return A pointer to a string describing the error corresponding to the code supplied
 */
ZRA_EXPORT const char* ZraGetErrorString(ZraError code);

/**
 * @brief This compresses the supplied buffer with specified parameters in-memory into the specified buffer
 * @param inputBuffer A pointer to the uncompressed source data
 * @param inputSize The size of the uncompressed source data
 * @param outputBuffer A pointer to the buffer to write compressed data into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write compressed data into
 * @param compressionLevel The ZSTD compression level to compress the buffer with
 * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
 * @return If positive, it's the size of the data read or if outputBuffer is nullptr then the minimum capacity of the output buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity, int8_t compressionLevel = 0, uint64_t frameSize = 16384, bool checksum = false);

/**
 * @brief This decompresses the entirety of the supplied compressed buffer in-memory into the specified buffer
 * @param inputBuffer A pointer to the compressed data
 * @param inputSize The size of the compressed data
 * @param outputBuffer A pointer to the buffer to write uncompressed data into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write uncompressed data into
 * @return If positive, it's the size of the data read or if outputBuffer is nullptr then the minimum capacity of the output buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraDecompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity);

/**
 * @brief This decompresses a specific region of the supplied compressed buffer in-memory into the specified buffer
 * @param inputBuffer A pointer to the compressed data
 * @param inputSize The size of the compressed data
 * @param outputBuffer A pointer to the buffer to write uncompressed data into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write uncompressed data into
 * @param offset The corresponding offset in the uncompressed buffer
 * @param size The amount of bytes to decompress from the offset
 * @return If positive, it's the size of the data read or if outputBuffer is nullptr then the minimum capacity of the output buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraDecompressRA(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity, size_t offset, size_t size);

struct ZraCompressor;

/**
 * @brief Creates a ZraCompressor object with the specified parameters
 * @param compressor A pointer to a ZraCompressor pointer to store the object pointer in
 * @param size The exact size of the overall stream
 * @param compressionLevel The level of ZSTD compression to use
 * @param frameSize The size of a single frame which can be decompressed individually
 * @return A ZraError with the result from the operation
 */
ZRA_EXPORT ZraError ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel = 0, uint64_t frameSize = 16384, bool checksum = false);

/**
 * @brief Deletes a ZraCompressor object
 * @param compressor A pointer to the ZraCompressor object to delete
 */
ZRA_EXPORT void ZraDeleteCompressor(ZraCompressor* compressor);

/**
 * @brief This compresses a partial stream of contiguous data into the specified buffer
 * @param compressor A pointer to the ZraCompressor object to use
 * @param inputBuffer A pointer to partial compressed data
 * @param inputSize The size of the partial compressed data
 * @param outputBuffer A pointer to the buffer to write the corresponding uncompressed data into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write uncompressed data into
 * @return If positive, it's the size of the data read or if outputBuffer is nullptr then the minimum capacity of the output buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraCompressWithCompressor(ZraCompressor* compressor, void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity);

/**
 * @brief This writes the header of the ZRA file into the specified buffer, this should only be read in after compression has been completed
 * @param compressor A pointer to the ZraCompressor object to use
 * @param outputBuffer A pointer to the buffer to write the header into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write header into
 * @return If positive, it's the size of the entire header
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraGetHeaderWithCompressor(ZraCompressor* compressor, void* outputBuffer, size_t outputCapacity);

/**
 * @brief This is used to retrieve the size of the entire header buffer
 * @param headerBuffer A pointer to the buffer containing the header (Can be nullptr to retrieve size)
 * @param headerSize The size of the buffer containing the header
 * @return If positive, it's the size of the entire header or if headerBuffer is nullptr the minimum data required to deduce the full size of the header
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraGetHeaderSize(void* headerBuffer, size_t headerSize);

/**
 * @brief This is used to retrieve the size of the uncompressed buffer
 * @param headerBuffer A pointer to the buffer containing the header (Can be nullptr to retrieve size)
 * @param headerSize The size of the buffer containing the header
 * @return If positive, it's the size of the uncompressed buffer or if headerBuffer is nullptr the minimum data required to deduce the size of the uncompressed buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraGetUncompressedSize(void* headerBuffer, size_t headerSize);

struct ZraDecompressor;

/**
 * @brief Creates a ZraDecompressor object with the specified parameters
 * @param decompressor A pointer to a ZraDecompressor pointer to store the object pointer in
 * @param header A pointer to a buffer containing the header, it's full size can be inferred by using ZraGetHeaderSize
 * @param headerSize The size of the header, should be equivalent to the value returned by ZraGetHeaderSize
 * @param readFunction This function is used to read data from the compressed file while supplying the offset (Not including the header) and the size, the output should be into the buffer
 * @param cacheSize The maximum size of the file cache, if the uncompressed segment read goes above this then it'll be read into it's own buffer
 * @return A ZraError with the result from the operation
 */
ZRA_EXPORT ZraError ZraCreateDecompressor(ZraDecompressor** decompressor, void* header, size_t headerSize, void(readFunction)(size_t offset, size_t size, void* buffer), size_t maxCacheSize = 1024 * 1024 * 20);

/**
 * @brief Deletes a ZraDecompressor object
 * @param decompressor A pointer to the ZraDecompressor object to delete
 */
ZRA_EXPORT void ZraDeleteDecompressor(ZraDecompressor* decompressor);

/**
 * @brief This decompresses data from a slice of corresponding to the original uncompressed file into the specified buffer
 * @param decompressor A pointer to the ZraDecompressor object to use
 * @param offset The offset of the data to decompress in the original file
 * @param size The size of the data to decompress in the original file
 * @param outputBuffer A pointer to the buffer to write the decompressed output into (Can be nullptr to retrieve size)
 * @param outputCapacity The capacity of the buffer used to write the decompressed output into
 * @return If positive, it's the size of the data read or if outputBuffer is nullptr then the minimum capacity of the output buffer
 *         If negative, it's a negated ZraError describing the result of the operation
 */
ZRA_EXPORT ptrdiff_t ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer, size_t outputCapacity);

#ifdef __cplusplus
}
#endif

#endif  //ZRA_ZRA_H
