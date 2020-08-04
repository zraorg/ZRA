#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef ZRA_EXPORT_HEADER
#include "zra_export.h"
#else
#ifdef WIN32
#define ZRA_EXPORT __declspec(dllimport)
#else
#define ZRA_EXPORT
#endif
#endif

namespace zra {
  using u64 = uint64_t;  //!< Unsigned 64-bit integer
  using u32 = uint32_t;  //!< Unsigned 32-bit integer
  using u16 = uint16_t;  //!< Unsigned 16-bit integer
  using u8 = uint8_t;    //!< Unsigned 8-bit integer
  using i64 = int64_t;   //!< Signed 64-bit integer
  using i32 = int32_t;   //!< Signed 32-bit integer
  using i16 = int16_t;   //!< Signed 16-bit integer
  using i8 = int8_t;     //!< Signed 8-bit integer

  using Buffer = std::vector<u8>;  //!< Byte-array for buffers

  /**
   * @brief This class provides a view into a buffer with it's address and size
   */
  struct BufferView {
    u8* data{nullptr};  //!< A pointer to the data within the buffer
    size_t size{};      //!< The size of the buffer in bytes

    BufferView() = default;

    /**
     * @param data The pointer to the data in the buffer
     * @param size The size of the buffer in bytes
     */
    constexpr inline BufferView(u8* data, size_t size) : data(data), size(size) {}

    /**
     * @param buffer The Buffer object that this BufferView should represent
     */
    inline BufferView(const Buffer& buffer) : data(const_cast<u8*>(buffer.data())), size(buffer.size()) {}
  };

  /**
   * @brief This enumerates all of status codes that ZRA can have
   * @note This enumeration is meant to match up with ZraStatusCode in zra.h
   */
  enum class StatusCode {
    Success,                 //!< The operation was successful
    ZStdError,               //!< An error was returned by ZStandard
    ZraVersionLow,           //!< The version of ZRA is too low to decompress this archive
    HeaderInvalid,           //!< The header in the supplied buffer was invalid
    HeaderIncomplete,        //!< The header hasn't been fully written before getting retrieved
    OutOfBoundsAccess,       //!< The specified offset and size are past the data contained within the buffer
    OutputBufferTooSmall,    //!< The output buffer is too small to contain the output (Supply null output buffer to get size)
    CompressedSizeTooLarge,  //!< The compressed output's size exceeds the maximum limit
    InputFrameSizeMismatch,  //!< The input size is not divisible by the frame size nor is it the final frame
  };

  /**
   * @brief This class is used to deliver an exception when it occurs
   */
  struct ZRA_EXPORT Exception : std::exception {
    StatusCode code;  //!< The error code associated with this exception
    int zstdCode;     //!< The error code issued by ZSTD, if any

    Exception(StatusCode code, i32 zstdCode = {});

    /**
     * @param code The error code to describe
     * @return A string describing the supplied error code
     */
    static std::string_view GetExceptionString(StatusCode code);

    /**
     * @return A string describing the contents of this exception
     */
    const char* what() const noexcept override;
  };

  /**
   * @return The highest version of ZRA the library linked to this supports
   */
  ZRA_EXPORT u16 GetVersion();

  constexpr u64 maxCompressedSize = 1ULL << 40;  //!< The maximum size of a compressed ZSTD file

  /**
   * @brief This structure holds the header of a ZRA file
   */
  class Header {
   private:
    std::function<void(size_t, size_t, void*)> readFunction;  //!< This function is used to read data from the compressed file while supplying the offset and the size, the output should be into the buffer

   public:
    u16 version;           //!< The version of ZRA the archive was compressed with
    u32 size;              //!< The size of the entire header
    u64 uncompressedSize;  //!< The size of the original uncompressed file
    u32 frameSize;         //!< The size of the frames except for the final frame
    u32 seekTableOffset;   //!< The offset of the seek table from the start of the file
    u32 seekTableSize;     //!< The size of the seek table

    Header(const std::function<void(size_t offset, size_t size, void* buffer)>& readFunction);

    /**
     * @param buffer A buffer with the entire compressed file so that it can be safely used to extract the header
     */
    Header(const BufferView& buffer);

    Buffer GetSeekTable() const;
  };

  /**
   * @param inputSize The size of the input being compressed
   * @param frameSize The size of a single frame
   * @return The worst-case size of the compressed output
   */
  ZRA_EXPORT size_t GetOutputBufferSize(size_t inputSize, size_t frameSize);

  /**
   * @brief This compresses the supplied buffer with specified parameters in-memory into a BufferView
   * @param input A BufferView with the uncompressed source data
   * @param output A BufferView to write the compressed contents into (Can be empty to retrieve size)
   * @param compressionLevel The ZSTD compression level to compress the buffer with
   * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return The size of the data after being compressed
   */
  ZRA_EXPORT size_t CompressBuffer(const BufferView& input, const BufferView& output, i8 compressionLevel = 0, u32 frameSize = 16384, bool checksum = false);

  /**
   * @brief This compresses the supplied buffer with specified parameters in-memory into a Buffer
   * @param buffer A BufferView with the uncompressed source data
   * @param compressionLevel The ZSTD compression level to compress the buffer with
   * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return A Buffer with the compressed contents of the input buffer
   */
  ZRA_EXPORT Buffer CompressBuffer(const BufferView& buffer, i8 compressionLevel = 0, u32 frameSize = 16384, bool checksum = false);

  /**
   * @brief This decompresses the supplied compressed buffer in-memory into a BufferView
   * @param input A BufferView with the compressed data
   * @param output A BufferView to write the uncompressed contents into (Size should be at least Header::UncompressedSize bytes)
   */
  ZRA_EXPORT void DecompressBuffer(const BufferView& input, const BufferView& output);

  /**
   * @brief This decompresses the supplied compressed buffer in-memory into a Buffer
   * @param buffer A BufferView with the compressed data
   * @return A BufferView with the corresponding decompressed contents
   */
  ZRA_EXPORT Buffer DecompressBuffer(const BufferView& buffer);

  /**
   * @brief This decompresses a specific region of the supplied compressed buffer in-memory into a BufferView
   * @param input A BufferView with the compressed data
   * @param output A BufferView to write the uncompressed contents into (Size should be adequate)
   * @param offset The corresponding offset in the uncompressed buffer
   * @param size The amount of bytes to decompress from the offset
   */
  ZRA_EXPORT void DecompressRA(const BufferView& input, const BufferView& output, size_t offset, size_t size);

  /**
   * @brief This decompresses a specific region of the supplied compressed buffer in-memory into a Buffer
   * @param buffer A BufferView with the compressed data
   * @param offset The corresponding offset in the uncompressed buffer
   * @param size The amount of bytes to decompress from the offset
   * @return A Buffer with the corresponding decompressed contents
   */
  ZRA_EXPORT Buffer DecompressRA(const BufferView& buffer, size_t offset, size_t size);

  class ZCCtx;
  struct Entry;

  /**
   * @brief This class is used to implement streaming ZRA compression
   */
  class ZRA_EXPORT Compressor {
   private:
    std::shared_ptr<ZCCtx> ctx;  //!< A shared pointer to the incomplete ZCCtx class
    u32 frameSize;               //!< The size of a single compressed frame
    u32 tableSize;               //!< The size of the frame table in entries
    Buffer header;               //!< A Buffer containing the header of the file
    Entry* entry;                //!< The current frame entry in the seek table
    size_t outputOffset{};       //!< The offset of the output file

   public:
    /**
     * @param size The exact size of the overall stream
     * @param compressionLevel The level of ZSTD compression to use
     * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
     * @param frameSize The size of a single frame which can be decompressed individually
     */
    Compressor(size_t size, i8 compressionLevel = 0, u32 frameSize = 16384, bool checksum = false);

    /**
     * @param inputSize The size of the input being compressed
     * @return The worst-case size of the output buffer
     * @note This is not the same as GetOutputBufferSize
     */
    size_t GetOutputBufferSize(size_t inputSize) const;

    /**
     * @brief This compresses a partial stream of contiguous data into a BufferView
     * @param input The BufferView containing the uncompressed contents, it's size must be divisible by the frame size unless it's the last frame
     * @param output The output BufferView which can be reused from previous iterations, compressed data will be written in here (Size should be at least GetOutputBufferSize bytes long)
     * @return The size of the data after being compressed
     */
    size_t Compress(const BufferView& input, const BufferView& output);

    /**
     * @brief This compresses a partial stream of contiguous data into a Buffer
     * @param input The Buffer containing the uncompressed contents, it's size must be divisible by the frame size unless it's the last frame
     * @param output The output Buffer which can be reused from previous iterations, compressed data will be written in here
     */
    void Compress(const BufferView& input, Buffer& output);

    /**
     * @return A const-reference to a Buffer containing the entire header (including the seek table)
     * @throws Exception with StatusCode::HeaderInvalid, if the header hasn't been fully written yet
     * @note This refers to a class member and will be destroyed when the class is, it is the user's job to manage this
     */
    const Buffer& GetHeader();

    /**
     * @return The size of the entire header including the seek table
     */
    size_t GetHeaderSize();
  };

  class ZDCtx;

  /**
   * @brief This class is used to implement streaming random-access ZRA decompression
   */
  class ZRA_EXPORT Decompressor {
   private:
    std::shared_ptr<ZDCtx> ctx;                               //!< A shared pointer to the incomplete ZDCtx class
    std::function<void(size_t, size_t, void*)> readFunction;  //!< This function is used to read data from the compressed file while supplying the offset and the size, the output should be into the buffer
    Header header;                                            //!< The Header of the file/buffer that is being decompressed
    Buffer seekTable;                                         //!< The seek-table is required for random-access throughout the file
    Buffer cache;                                             //!< A Buffer to read compressed data from the file into, it is reused to prevent constant reallocation
    size_t maxCacheSize;                                      //!< The maximum size of the cache, if the uncompressed segment read goes above this then it'll be read into it's own vector

   public:
    Decompressor(const std::function<void(size_t offset, size_t size, void* buffer)>& readFunction, size_t maxCacheSize = 1024 * 1024 * 20);

    /**
     * @brief This decompresses data from a slice of corresponding to the original uncompressed file into a BufferView
     * @param offset The offset of the data to decompress in the original file
     * @param size The size of the data to decompress in the original file
     * @param output The output BufferView which can be reused from previous calls, uncompressed data will be written in here
     */
    void Decompress(size_t offset, size_t size, const BufferView& output = {});

    /**
     * @brief This decompresses data from a slice of corresponding to the original uncompressed file into a Buffer
     * @param offset The offset of the data to decompress in the original file
     * @param size The size of the data to decompress in the original file
     * @param output The output Buffer which can be reused from previous calls, uncompressed data will be written in here
     */
    void Decompress(size_t offset, size_t size, Buffer& output);
  };

  /**
   * @brief This class is used to implement streaming ZRA decompression which is optimized for decompressing the entire buffer
   * @note This isn't exposed in the C-API or any of the bindings to it due to the normal decompressor being adequate for the most part
   */
  class ZRA_EXPORT FullDecompressor {
   private:
    std::shared_ptr<ZDCtx> ctx;  //!< A shared pointer to the incomplete ZDCtx class
    Header header;               //!< The Header of the file/buffer that is being decompressed
    Buffer seekTable;            //!< The seek-table is required for random-access throughout the file
    Buffer frame;                //!< A Buffer containing a partial frame from decompressing
    Entry* entry;                //!< The current frame entry in the seek table

   public:
    FullDecompressor(const Header& header);

    /**
     * @brief This decompresses a partial stream of data (All chunks supplied to this are expected to be contiguous)
     * @param input The Buffer containing the compressed contents, the data here might be modified during decompression
     * @param output The output Buffer which can be reused from previous iterations, uncompressed data will be written in here
     */
    void Decompress(Buffer& input, Buffer& output);
  };
}  // namespace zra