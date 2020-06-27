#pragma once

#include <memory>
#include <string>
#include <vector>

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
   * @brief This compresses the supplied buffer with specified parameters in-memory
   * @param compressionLevel The ZSTD compression level to compress the buffer with
   * @param frameSize The size of a single frame which can be decompressed individually (This does not always equate to a single ZSTD frame)
   * @return A Buffer with the compressed contents of the input buffer
   */
  Buffer CompressBuffer(const Buffer& buffer, i8 compressionLevel = 0, u16 frameSize = 16384);

  /**
   * @brief This decompresses the supplied compressed buffer in-memory
   * @return A Buffer with the corresponding decompressed contents
   */
  Buffer DecompressBuffer(const Buffer& buffer);

  /**
   * @brief This decompresses a specific region of the supplied compressed buffer in-memory
   * @param buffer The entire compressed buffer as returned by CompressBuffer
   * @param offset The corresponding offset in the uncompressed buffer
   * @param size The amount of bytes to decompress from the offset
   * @return A Buffer with the corresponding decompressed contents
   */
  Buffer DecompressRA(const Buffer& buffer, size_t offset, size_t size);

  /**
   * @brief This structure holds a single entry in the seek table
   */
  struct Entry {
    u64 offset : 40;  //!< The offset of the frame in the compressed segment
    u16 size;         //!< The size of the compressed frame
  } __attribute__((packed));

  /**
   * @brief This structure holds the header of a ZRA file
   */
  struct Header {
    u64 inputSize;  //!< The size of the entire input, this is used for bounds-checking and buffer pre-allocation
    u32 tableSize;  //!< The amount of entries present in the seek table
    u16 frameSize;  //!< The size of frames except for the final frame

    /**
     * @return The size of the entire header including the seek table
     */
    [[nodiscard]] size_t Size() const {
      return sizeof(Header) + (tableSize * sizeof(Entry));
    }
  };

  class ZCCtx;

  /**
   * @brief This class is used to implement streaming ZRA compression
   */
  class Compressor {
   private:
    std::shared_ptr<ZCCtx> ctx;  //!< A shared pointer to the incomplete ZCCtx class
    u16 frameSize;               //!< The size of a single compressed frame
    u32 tableSize;               //!< The size of the frame table in entries
    size_t entryOffset;          //!< The offset of the current frame entry in the table

   public:
    Buffer header;  //!< A Buffer containing the header of the file (Will only be "complete" after everything has been compressed)

    /**
     * @param size The exact size of the overall stream
     * @param compressionLevel The level of ZSTD compression to use
     * @param frameSize The size of a single frame which can be decompressed individually
     */
    Compressor(size_t size, i8 compressionLevel = 0, u16 frameSize = 16384);

    /**
     * @brief This compresses a partial stream of data
     * @param input The buffer containing the uncompressed contents, it's size must be divisible by the frame size unless it's the last frame
     * @param output The output buffer which can be reused from previous iterations, compressed data will be written in here
     */
    void Compress(const Buffer& input, Buffer& output);
  };

  class ZDCtx;

  /**
   * @brief This class is used to implement streaming ZRA decompression
   */
  class Decompressor {
   private:
    std::shared_ptr<ZDCtx> ctx;  //!< A shared pointer to the incomplete ZDCtx class
    Buffer header;               //!< A Buffer containing the header of the file
    Buffer frame;                //!< A Buffer containing a partial frame from decompressing
    size_t frameSize;            //!< The size of a single frame (Except the last frame, which can be less than this)
    size_t entryOffset;          //!< The offset of the current frame entry in the table

   public:
    /**
     * @param header A buffer containing the header of the file which should correspond Compressor::header, it's full size can be inferred by using Header::Size
     */
    Decompressor(Buffer header);

    /**
     * @brief This decompresses a partial stream of data (all chunks supplied to this are expected to be contiguous)
     * @param input The buffer containing the compressed contents, the data here might be modified during decompression
     * @param output The output buffer which can be reused from previous iterations, uncompressed data will be written in here
     */
    void Decompress(Buffer& input, Buffer& output);
  };

  /**
   * @brief This class is used to implement streaming random-access ZRA decompression
   */
  class RADecompressor {
   private:
    std::shared_ptr<ZDCtx> ctx;                                 //!< A shared pointer to the incomplete ZDCtx class
    Buffer header;                                              //!< A Buffer containing the header of the file
    size_t frameSize;                                           //!< The size of a single frame (Except the last frame, which can be less than this)
    size_t inputSize;                                           //!< The size of the entire input data, this is used for bounds-checking
    Buffer cache;                                               //!< A Buffer to read compressed data from the file into, it is reused to prevent constant reallocation
    size_t maxCacheSize;                                        //!< The maximum size of the cache, if the uncompressed segment read goes above this then it'll be read into it's own vector
    std::function<void(size_t, size_t, Buffer&)> readFunction;  //!< This function is used to read in data from the compressed file

   public:
    /**
     * @param header A buffer containing the header of the file which should correspond Compressor::header, it's full size can be inferred by using Header::Size
     * @param readFunction This function is used to read data from the compressed file while supplying the offset (Not including the header) and the size, the output should be into the buffer
     * @param cacheSize The maximum size of the cache, if the uncompressed segment read goes above this then it'll be read into it's own vector
     */
    RADecompressor(Buffer header, std::function<void(size_t offset, size_t size, Buffer& buffer)> readFunction, size_t maxCacheSize = 1024 * 1024 * 20);

    /**
     * @brief This decompresses data from a slice of corresponding to the original uncompressed file
     * @param offset The offset of the data to decompress in the original file
     * @param size The size of the data to decompress in the original file
     * @param output The output buffer which can be reused from previous calls, uncompressed data will be written in here
     */
    void Decompress(size_t offset, size_t size, Buffer& output);
  };
}  // namespace zra