// SPDX-License-Identifier: BSD-3-Clause
// Copyright © 2020 ZRA Contributors (https://github.com/zraorg)

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
#include <stdbool.h>
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
typedef enum ZraStatusCode {
  Success,                 //!< The operation was successful
  ZStdError,               //!< An error was returned by ZStandard
  ZraVersionLow,           //!< The version of ZRA is too low to decompress this archive
  HeaderInvalid,           //!< The header in the supplied buffer was invalid
  HeaderIncomplete,        //!< The header hasn't been fully written before getting retrieved
  OutOfBoundsAccess,       //!< The specified offset and size are past the data contained within the buffer
  OutputBufferTooSmall,    //!< The output buffer is too small to contain the output (Supply null output buffer to get size)
  CompressedSizeTooLarge,  //!< The compressed output's size exceeds the maximum limit
  InputFrameSizeMismatch,  //!< The input size is not divisible by the frame size nor is it the final frame
} ZraStatusCode;

/**
 * @brief This structure is used to hold the result of an operation
 */
typedef struct ZraStatus {
  ZraStatusCode zra;  //!< The status code from ZRA
  int zstd;           //!< The status code from ZSTD
} ZraStatus;

// ------Library Functions------

/**
 * @return The highest version of ZRA the library linked to this supports
 */
ZRA_EXPORT uint16_t ZraGetVersion();

/**
 * @param status The status structure that should be described
 * @return A pointer to a string describing the error corresponding to the code supplied
 */
ZRA_EXPORT const char* ZraGetErrorString(ZraStatus status);

// ------Header------
typedef struct ZraHeader ZraHeader;

/**
 * @brief Creates a ZraHeader object from a file
 * @param header A pointer to a pointer to store the ZraHeader pointer in
 * @param readFunction This function is used to read data from the compressed file while supplying the offset and the size, the output should be into the buffer
 */
ZRA_EXPORT ZraStatus ZraCreateHeader(ZraHeader** header, void(readFunction)(size_t offset, size_t size, void* buffer));

/**
 * @brief Creates a ZraHeader object from a file
 * @param header A pointer to a pointer to store the ZraHeader pointer in
 * @param buffer A pointer to a buffer containing the entire file
 * @param size The size of the entire file in bytes
 */
ZRA_EXPORT ZraStatus ZraCreateHeader2(ZraHeader** header, void* buffer, size_t size);

/**
 * @brief Deletes a ZraHeader object
 */
ZRA_EXPORT void ZraDeleteHeader(ZraHeader* header);

/**
 * @return The version of ZRA which the file was compressed with
 */
ZRA_EXPORT size_t ZraGetVersionWithHeader(ZraHeader* header);

/**
 * @return The size of the entire header in bytes
 */
ZRA_EXPORT size_t ZraGetHeaderSizeWithHeader(ZraHeader* header);

/**
 * @return The size of the original uncompressed data in bytes
 */
ZRA_EXPORT size_t ZraGetUncompressedSizeWithHeader(ZraHeader* header);

/**
 * @return The size of a single frame in bytes
 */
ZRA_EXPORT size_t ZraGetFrameSizeWithHeader(ZraHeader* header);

/*
 * @return The size of the metadata section in bytes
 */
ZRA_EXPORT size_t ZraGetMetadataSize(ZraHeader* header);

/**
 * @param buffer The buffer into which the metadata section is written into, should be at least ZraGetMetadataSize bytes long
 */
ZRA_EXPORT void ZraGetMetadata(ZraHeader* header, void* buffer);

// ------In-Memory Functions------

/**
 * @param inputSize The size of the input being compressed
 * @param frameSize The size of a single frame
 * @return The worst-case size of the compressed output
 */
ZRA_EXPORT size_t ZraGetCompressedOutputBufferSize(size_t inputSize, size_t frameSize);

/**
 * @brief Compresses the supplied buffer with specified parameters in-memory into the specified buffer
 * @param inputBuffer A pointer to the uncompressed source data
 * @param inputSize The size of the uncompressed source data
 * @param outputBuffer A pointer to the buffer to write compressed data into (Size should be at least ZraGetCompressedOutputBufferSize bytes)
 * @param[out] outputSize The size of the compressed output
 * @param compressionLevel The ZSTD compression level to compress the buffer with
 * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
 * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
 * @param metaBuffer A pointer to the metadata
 * @param metaSize The size of the metadata
 */
ZRA_EXPORT ZraStatus ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize, int8_t compressionLevel, uint32_t frameSize, bool checksum, void* metaBuffer, size_t metaSize);

/**
 * @brief Decompresses the entirety of the supplied compressed buffer in-memory into the specified buffer
 * @param inputBuffer A pointer to the compressed data
 * @param inputSize The size of the compressed data
 * @param outputBuffer A pointer to the buffer to write uncompressed data into (Size should be at least ZraGetUncompressedSizeWithHeader bytes)
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
typedef struct ZraCompressor ZraCompressor;

/**
 * @brief Creates a ZraCompressor object with the specified parameters
 * @param compressor A pointer to a pointer to store the ZraCompressor pointer in
 * @param size The exact size of the overall stream
 * @param compressionLevel The level of ZSTD compression to use
 * @param frameSize The size of a single frame which can be decompressed individually
 * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
 * @param metaBuffer A pointer to the metadata
 * @param metaSize The size of the metadata
 */
ZRA_EXPORT ZraStatus ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel, uint32_t frameSize, bool checksum, void* metaBuffer, size_t metaSize);

/**
 * @brief Deletes a ZraCompressor object
 */
ZRA_EXPORT void ZraDeleteCompressor(ZraCompressor* compressor);

/**
 * @param inputSize The size of the input being compressed
 * @return The worst-case size of the compressed output
 * @note This is not the same as ZraGetCompressedOutputBufferSize
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
typedef struct ZraDecompressor ZraDecompressor;

/**
 * @brief Creates a ZraDecompressor object with the specified parameters
 * @param decompressor A pointer to a pointer to store the ZraDecompressor pointer in
 * @param readFunction This function is used to read data from the compressed file while supplying the offset and the size, the output should be into the buffer
 * @param maxCacheSize The maximum size of the file cache, if the uncompressed segment read goes above this then it'll be read into it's own buffer
 * @note The cache is to preallocate buffers that are passed into readFunction, so that there isn't constant reallocation
 */
ZRA_EXPORT ZraStatus ZraCreateDecompressor(ZraDecompressor** decompressor, void(readFunction)(size_t offset, size_t size, void* buffer), size_t maxCacheSize);

/**
 * @brief Deletes a ZraDecompressor object
 */
ZRA_EXPORT void ZraDeleteDecompressor(ZraDecompressor* decompressor);

/**
 * @return The header object created by the decompressor internally, so that it won't have to be constructed redundantly
 * @note The lifetime of the object is directly tied to that of the Decompressor, do not manually delete it
 */
ZRA_EXPORT ZraHeader* ZraGetHeaderWithDecompressor(ZraDecompressor* decompressor);

/**
 * @brief Decompresses data from a slice of corresponding to the original uncompressed file into the specified buffer
 * @param offset The offset of the data to decompress in the original file
 * @param size The size of the data to decompress in the original file
 * @param outputBuffer A pointer to the buffer to write the decompressed output into (Size should be adequate)
 */
ZRA_EXPORT ZraStatus ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer);

// ------Full Decompressor------
typedef struct ZraFullDecompressor ZraFullDecompressor;

/**
 * @brief Creates a ZraFullDecompressor object with the specified parameters
 * @param decompressor A pointer to a pointer to store the ZraFullDecompressor pointer in
 * @param readFunction This function is used to read data from the compressed file while supplying the offset and the size, the output should be into the buffer
 */
ZRA_EXPORT ZraStatus ZraCreateFullDecompressor(ZraFullDecompressor** decompressor, void(readFunction)(size_t offset, size_t size, void* buffer), size_t maxCacheSize);

/**
 * @brief Deletes a ZraFullDecompressor object
 */
ZRA_EXPORT void ZraDeleteFullDecompressor(ZraFullDecompressor* decompressor);

/**
 * @return The header object created by the decompressor internally, so that it won't have to be constructed redundantly
 * @note The lifetime of the object is directly tied to that of the FullDecompressor, do not manually delete it
 */
ZRA_EXPORT ZraHeader* ZraGetHeaderWithFullDecompressor(ZraFullDecompressor* decompressor);

/**
 * @brief Decompresses as much data as possible into the supplied output buffer
 * @param outputBuffer A pointer to the buffer to write the decompressed output into
 * @param outputCapacity The size of the output buffer, it should be at least ZraGetFrameSizeWithHeader bytes
 * @param outputSize The size of the uncompressed data that has been written into the buffer, this will be 0 at the end of compression
 */
ZRA_EXPORT ZraStatus ZraDecompressWithFullDecompressor(ZraFullDecompressor* decompressor, void* outputBuffer, size_t outputCapacity, size_t* outputSize);

#ifdef __cplusplus
}
#endif

#endif  //ZRA_ZRA_H
