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
  using u64 = uint64_t;         //!< Unsigned 64-bit integer
  using u32 = uint32_t;         //!< Unsigned 32-bit integer
  using u16 = uint16_t;         //!< Unsigned 16-bit integer
  using u8 = uint8_t;           //!< Unsigned 8-bit integer
  using i64 = int64_t;          //!< Signed 64-bit integer
  using i32 = int32_t;          //!< Signed 32-bit integer
  using i16 = int16_t;          //!< Signed 16-bit integer
  using i8 = int8_t;            //!< Signed 8-bit integer
  using uint = unsigned int;    //!< Unsigned integer
  using ulong = unsigned long;  //!< Unsigned long

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
   * @brief This enumerates all of the possible errors thrown by the application
   */
  enum class ErrorCode {
    Success,                 //!< The operation was successful
    ZStdError,               //!< An error was returned by ZStandard
    HeaderInvalid,           //!< The header in the supplied buffer was invalid or the header hasn't been fully written
    OutOfBoundsAccess,       //!< The specified offset and size are past the data contained within the buffer
    OutputBufferTooSmall,    //!< The output buffer is too small to contain the output (Supply null output buffer to get size)
    CompressedSizeTooLarge,  //!< The compressed output's size exceeds the maximum limit
    InputFrameSizeMismatch,  //!< The input size is not divisble by the frame size and it isn't the final frame
  };

  /**
   * @brief This class is used to deliver an exception when it occurs
   */
  struct ZRA_EXPORT Exception : std::exception {
    ErrorCode code;  //!< The error code associated with this exception
    i32 zstdCode;    //!< The error code issued by ZSTD, if any

    Exception(ErrorCode code, i32 zstdCode = {});

    /**
     * @param code The error code to describe
     * @return A string describing the supplied error code
     */
    static std::string_view GetExceptionString(ErrorCode code);

    /**
     * @return A string describing the contents of this exception
     */
    const char* what() const noexcept override;
  };

  /**
   * @return The version of ZRA the library linked to this uses
   */
  ZRA_EXPORT u16 GetVersion();

  constexpr u64 maxCompressedSize = 1ULL << 40;  //!< The maximum size of a compressed ZSTD file

  // clang-format off
#pragma pack(push, 1)
#pragma scalar_storage_order little-endian;
  // clang-format on

  /**
   * @brief This structure holds a single entry in the seek table
   */
  struct Entry {
    u64 offset : 40;  //!< The offset of the frame in the compressed segment
    u32 size;         //!< The size of the compressed frame
  };

  /**
   * @brief This structure holds the header of a ZRA file
   * @note The members marked with * are immutable across versions
   */
  struct Header {
    u32 frameId{0x184D2A50};            //!< The frame ID for a ZSTD skippable frame *
    [[maybe_unused]] u32 headerSize{};  //!< The size of the header after this in bytes *
    u32 magic{0x3041525A};              //!< The magic for the ZRA format "ZRA0" *
    u16 version{GetVersion()};          //!< The version of ZRA it was compressed with *
    u32 hash{};                         //!< The CRC-32 hash of the seek-table
    u64 inputSize{};                    //!< The size of the entire input, this is used for bounds-checking and buffer pre-allocation
    u32 tableSize{};                    //!< The amount of entries present in the seek table
    u32 frameSize{};                    //!< The size of frames except for the final frame

    inline Header() = default;

    inline Header(u64 inputSize, u32 tableSize, u32 frameSize) : inputSize(inputSize), tableSize(tableSize), frameSize(frameSize) {
      headerSize = Size() - (offsetof(Header, frameId) + sizeof(frameId));
    }

   public:
    /**
     * @return The size of the entire header including the seek table
     */
    u32 Size() const {
      return sizeof(Header) + (tableSize * sizeof(Entry));
    }

    /**
     * @param seekTable A pointer to the start of the seektable to hash
     * @return If this Header object is valid or not
     */
    bool Valid(u8* seekTable = nullptr) const {
      return magic == 0x3041525A && version == GetVersion() && seekTable ? hash == CalculateHash(seekTable) : true;
    }

    /**
     * @param seekTable A pointer to the start of the seektable to hash
     * @return A CRC-32 hash of the header + the seektable
     */
    u32 CalculateHash(const u8* seekTable) const;
  };

  // clang-format off
#pragma scalar_storage_order default
#pragma pack(pop)
  // clang-format on

  /**
   * @brief This compresses the supplied buffer with specified parameters in-memory into a BufferView
   * @param input A BufferView with the uncompressed source data
   * @param output A BufferView to write the compressed contents into (Can be empty to retrieve size)
   * @param compressionLevel The ZSTD compression level to compress the buffer with
   * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return If positive then it is the size of the data read, otherwise it's the required minimum size of the output buffer
   */
  ZRA_EXPORT ptrdiff_t CompressBuffer(const BufferView& input, const BufferView& output, i8 compressionLevel = 0, u32 frameSize = 16384, bool checksum = false);

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
   * @param output A BufferView to write the uncompressed contents into (Can be empty to retrieve size)
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return If positive then it is the size of the data read, otherwise it's the required minimum size of the output buffer
   * @throws std::runtime_error if the supplied buffer is invalid
   */
  ZRA_EXPORT ptrdiff_t DecompressBuffer(const BufferView& input, const BufferView& output);

  /**
   * @brief This decompresses the supplied compressed buffer in-memory into a Buffer
   * @param buffer A BufferView with the compressed data
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return A BufferView with the corresponding decompressed contents
   * @throws std::runtime_error if the supplied buffer is invalid
   */
  ZRA_EXPORT Buffer DecompressBuffer(const BufferView& buffer);

  /**
   * @brief This decompresses a specific region of the supplied compressed buffer in-memory into a BufferView
   * @param input A BufferView with the compressed data (Use Streaming APIs for partially)
   * @param output A BufferView to write the uncompressed contents into (Can be empty to retrieve size)
   * @param offset The corresponding offset in the uncompressed buffer
   * @param size The amount of bytes to decompress from the offset
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return If positive then it is the size of the data read (should be equal to size parameter), otherwise it's the required minimum size of the output buffer (this may be larger than the size parameter)
   * @throws std::runtime_error if the supplied buffer is invalid
   */
  ZRA_EXPORT ptrdiff_t DecompressRA(const BufferView& input, const BufferView& output, size_t offset, size_t size);

  /**
   * @brief This decompresses a specific region of the supplied compressed buffer in-memory into a Buffer
   * @param buffer A BufferView with the compressed data
   * @param offset The corresponding offset in the uncompressed buffer
   * @param size The amount of bytes to decompress from the offset
   * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
   * @return A Buffer with the corresponding decompressed contents
   * @throws std::runtime_error if the supplied buffer is invalid
   */
  ZRA_EXPORT Buffer DecompressRA(const BufferView& buffer, size_t offset, size_t size);

  class ZCCtx;

  /**
   * @brief This class is used to implement streaming ZRA compression
   */
  class ZRA_EXPORT Compressor {
   private:
    std::shared_ptr<ZCCtx> ctx;  //!< A shared pointer to the incomplete ZCCtx class
    u32 frameSize;               //!< The size of a single compressed frame
    u32 tableSize;               //!< The size of the frame table in entries
    size_t entryOffset;          //!< The offset of the current frame entry in the table
    size_t outputOffset{};       //!< The offset of the output file
    Buffer header;               //!< A Buffer containing the header of the file

   public:
    /**
     * @param size The exact size of the overall stream
     * @param compressionLevel The level of ZSTD compression to use
     * @param checksum If ZSTD should add a checksum over all blocks of data that'll be compressed
     * @param frameSize The size of a single frame which can be decompressed individually
     */
    Compressor(size_t size, i8 compressionLevel = 0, u32 frameSize = 16384, bool checksum = false);

    /**
     * @brief This compresses a partial stream of contiguous data into a BufferView
     * @param input The BufferView containing the uncompressed contents, it's size must be divisible by the frame size unless it's the last frame
     * @param output The output BufferView which can be reused from previous iterations, compressed data will be written in here (Can be default to retrieve size)
     * @return If positive then it is the size of the data read, otherwise it's the required minimum size of the output buffer
     */
    ptrdiff_t Compress(const BufferView& input, const BufferView& output);

    /**
     * @brief This compresses a partial stream of contiguous data into a Buffer
     * @param input The Buffer containing the uncompressed contents, it's size must be divisible by the frame size unless it's the last frame
     * @param output The output Buffer which can be reused from previous iterations, compressed data will be written in here
     */
    void Compress(const BufferView& input, Buffer& output);

    /**
     * @return A const-reference to a Buffer containing the entire header (including the seek table)
     * @throws Exception with ErrorCode::HeaderInvalid, if the header hasn't been fully written yet
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
    std::shared_ptr<ZDCtx> ctx;                                    //!< A shared pointer to the incomplete ZDCtx class
    Buffer header;                                                 //!< A Buffer containing the header of the file
    u32 frameSize{};                                               //!< The size of a single frame (Except the last frame, which can be less than this)
    size_t inputSize{};                                            //!< The size of the entire input data, this is used for bounds-checking
    std::function<void(size_t, size_t, BufferView)> readFunction;  //!< This function is used to read in data from the compressed file
    Buffer cache;                                                  //!< A Buffer to read compressed data from the file into, it is reused to prevent constant reallocation
    size_t maxCacheSize;                                           //!< The maximum size of the cache, if the uncompressed segment read goes above this then it'll be read into it's own vector

   public:
    /**
     * @param header A buffer containing the header of the file which should correspond Compressor::header, it's full size can be inferred by using Header::Size
     * @param readFunction This function is used to read data from the compressed file while supplying the offset (Not including the header) and the size, the output should be into the buffer
     * @param cacheSize The maximum size of the cache, if the uncompressed segment read goes above this then it'll be read into it's own vector
     * @throws std::runtime_error if the supplied header is invalid
     */
    Decompressor(const Buffer& header, const std::function<void(size_t, size_t, BufferView)>& readFunction, size_t maxCacheSize = 1024 * 1024 * 20);

    /**
     * @brief This decompresses data from a slice of corresponding to the original uncompressed file into a BufferView
     * @param offset The offset of the data to decompress in the original file
     * @param size The size of the data to decompress in the original file
     * @param output The output BufferView which can be reused from previous calls, uncompressed data will be written in here (Can be empty to retrieve size)
     * @return If positive then it is the size of the data read, otherwise it's the minimum size of the output buffer
     */
    ptrdiff_t Decompress(size_t offset, size_t size, const BufferView& output = {});

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
   */
  class ZRA_EXPORT FullDecompressor {
   private:
    std::shared_ptr<ZDCtx> ctx;  //!< A shared pointer to the incomplete ZDCtx class
    Buffer header;               //!< A Buffer containing the header of the file
    Buffer frame;                //!< A Buffer containing a partial frame from decompressing
    size_t entryOffset;          //!< The offset of the current frame entry in the table
    u32 frameSize;               //!< The size of a single frame (Except the last frame, which can be less than this)

   public:
    /**
     * @param header A Buffer containing the header of the file which should correspond Compressor::header, it's full size can be inferred by using Header::Size
     * @throws std::runtime_error if the supplied header is invalid
     */
    FullDecompressor(Buffer header);

    /**
     * @brief This decompresses a partial stream of data (all chunks supplied to this are expected to be contiguous)
     * @param input The Buffer containing the compressed contents, the data here might be modified during decompression
     * @param output The output Buffer which can be reused from previous iterations, uncompressed data will be written in here
     */
    void Decompress(Buffer& input, Buffer& output);
  };
}  // namespace zra