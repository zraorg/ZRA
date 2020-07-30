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
enum ZraStatusCode {
  Success,                 //!< The operation was successful
  ZStdError,               //!< An error was returned by ZStandard
  HeaderInvalid,           //!< The header in the supplied buffer was invalid
  HeaderIncomplete,        //!< The header hasn't been fully written before getting retrieved
  OutOfBoundsAccess,       //!< The specified offset and size are past the data contained within the buffer
  OutputBufferTooSmall,    //!< The output buffer is too small to contain the output (Supply null output buffer to get size)
  CompressedSizeTooLarge,  //!< The compressed output's size exceeds the maximum limit
  InputFrameSizeMismatch,  //!< The input size is not divisble by the frame size and it isn't the final frame
};

/**
 * @brief This structure is used to hold the result of an operation
 */
struct ZraStatus {
  ZraStatusCode zra;  //!< The status code from ZRA
  int zstd;           //!< The status code from ZSTD
};

// ------Library Functions------

/**
 * @return The version of ZRA the library linked to this uses
 */
ZRA_EXPORT uint16_t ZraGetVersion();

/**
 * @param status The status structure that should be described
 * @return A pointer to a string describing the error corresponding to the code supplied
 */
ZRA_EXPORT const char* ZraGetErrorString(ZraStatus status);

// ------Header Functions------

/**
 * @return The size of the fixed-format header in bytes
 */
ZRA_EXPORT size_t ZraGetHeaderSize();

/**
 * @param header A pointer to the buffer containing the header (This is assumed to be at least ZraGetHeaderSize long)
 * @param fullHeader If the entire header should be validated not just the fixed format parts (Size should be at least ZraGetFullHeaderSize bytes)
 * @note This is done by the decompressors, so it's redundant to do a full header validation before calling them
 */
ZRA_EXPORT ZraStatus ZraValidateHeader(void* header, bool fullHeader = false);

/**
 * @param headerBuffer A pointer to the buffer containing the header (This is assumed to be a valid header that is at least ZraGetHeaderSize bytes)
 * @return fullHeaderSize The size of the buffer the full header (Includes fixed-format header + Seek table)
 */
ZRA_EXPORT size_t ZraGetFullHeaderSize(void* header);

/**
 * @param header A pointer to the buffer containing the header (This is assumed to be a valid header that is at least ZraGetHeaderSize bytes long)
 * @return uncompressedSize The size of the original uncompressed data
 */
ZRA_EXPORT size_t ZraGetUncompressedSize(void* header);

// ------In-Memory Functions------

/**
 * @param inputSize The size of the input being compressed
 * @param frameSize The size of a single frame
 * @return The worst-case size of the compressed output
 */
ZRA_EXPORT size_t ZraGetOutputBufferSize(size_t inputSize, size_t frameSize);

/**
 * @brief Compresses the supplied buffer with specified parameters in-memory into the specified buffer
 * @param inputBuffer A pointer to the uncompressed source data
 * @param inputSize The size of the uncompressed source data
 * @param outputBuffer A pointer to the buffer to write compressed data into (Size should be at least ZraGetOutputBufferSize bytes)
 * @param[out] outputSize The size of the compressed output
 * @param compressionLevel The ZSTD compression level to compress the buffer with
 * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
 * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
 */
ZRA_EXPORT ZraStatus ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize, int8_t compressionLevel = 0, uint32_t frameSize = 16384, bool checksum = false);

/**
 * @brief Decompresses the entirety of the supplied compressed buffer in-memory into the specified buffer
 * @param inputBuffer A pointer to the compressed data
 * @param inputSize The size of the compressed data
 * @param outputBuffer A pointer to the buffer to write uncompressed data into (Size should be at least ZraGetUncompressedSize bytes)
 */
ZRA_EXPORT ZraStatus ZraDecompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer);

/**
 * @brief Decompresses a specific region of the supplied compressed buffer in-memory into the specified buffer
 * @param inputBuffer A pointer to the compressed data
 * @param inputSize The size of the compressed data
 * @param outputBuffer A pointer to the buffer to write uncompressed data into (Size should be adequate)
 * @param offset The corresponding offset in the uncompressed buffer
 * @param size The amount of bytes to decompress from the supplied offset
 */
ZRA_EXPORT ZraStatus ZraDecompressRA(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t offset, size_t size);

// ------Compressor------
struct ZraCompressor;

/**
 * @brief Creates a ZraCompressor object with the specified parameters
 * @param compressor A pointer to a ZraCompressor pointer to store the object pointer in
 * @param size The exact size of the overall stream
 * @param compressionLevel The level of ZSTD compression to use
 * @param frameSize The size of a single frame which can be decompressed individually
 * @return A ZraStatus with the result from the operation
 */
ZRA_EXPORT ZraStatus ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel = 0, uint32_t frameSize = 16384, bool checksum = false);

/**
 * @brief Deletes a ZraCompressor object
 */
ZRA_EXPORT void ZraDeleteCompressor(ZraCompressor* compressor);

/**
 * @param inputSize The size of the input being compressed
 * @return The worst-case size of the compressed output
 * @note This is not the same as ZraGetOutputBufferSize
 */
ZRA_EXPORT size_t ZraGetOutputBufferSizeWithCompressor(ZraCompressor* compressor, size_t inputSize);

/**
 * @brief Compresses a partial stream of contiguous data into the specified buffer
 * @param inputBuffer A pointer to the contiguous partial compressed data
 * @param inputSize The size of the partial compressed data
 * @param outputBuffer A pointer to the buffer to write the corresponding uncompressed data into (Size should be at least ZraGetOutputBufferSizeWithCompressor bytes)
 * @param[out] outputSize The size of the compressed output
 */
ZRA_EXPORT ZraStatus ZraCompressWithCompressor(ZraCompressor* compressor, void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize);

/**
 * @return The size of the full header from the compressor in bytes
 */
ZRA_EXPORT size_t ZraGetHeaderSizeWithCompressor(ZraCompressor* compressor);

/**
 * @brief Writes the header of the ZRA file into the specified buffer, this should only be read in after compression has been completed
 * @param outputBuffer A pointer to the buffer to write the header into (Size should be at least ZraGetHeaderSizeWithCompressor bytes)
 */
ZRA_EXPORT ZraStatus ZraGetHeaderWithCompressor(ZraCompressor* compressor, void* outputBuffer);

// ------Decompressor------
struct ZraDecompressor;

/**
 * @brief Creates a ZraDecompressor object with the specified parameters
 * @param decompressor A pointer to a ZraDecompressor pointer to store the object pointer in
 * @param fullHeader A pointer to a buffer containing the full header, it should be at least ZraGetFullHeaderSize bytes long
 * @param readFunction This function is used to read data from the compressed file while supplying the offset (Not including the header) and the size, the output should be into the buffer
 * @param cacheSize The maximum size of the file cache, if the uncompressed segment read goes above this then it'll be read into it's own buffer
 * @note The cache is to preallocate buffers that are passed into readFunction, so that there isn't constant reallocation
 */
ZRA_EXPORT ZraStatus ZraCreateDecompressor(ZraDecompressor** decompressor, void* fullHeader, void(readFunction)(size_t offset, size_t size, void* buffer), size_t maxCacheSize = 1024 * 1024 * 20);

/**
 * @brief Deletes a ZraDecompressor object
 */
ZRA_EXPORT void ZraDeleteDecompressor(ZraDecompressor* decompressor);

/**
 * @brief Decompresses data from a slice of corresponding to the original uncompressed file into the specified buffer
 * @param offset The offset of the data to decompress in the original file
 * @param size The size of the data to decompress in the original file
 * @param outputBuffer A pointer to the buffer to write the decompressed output into (Size should be adequate)
 */
ZRA_EXPORT ZraStatus ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer);

#ifdef __cplusplus
}
#endif

#endif  //ZRA_ZRA_H
