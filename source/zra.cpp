// SPDX-License-Identifier: BSD-3-Clause
// Copyright © 2020 ZRA Contributors (https://github.com/zraorg)

#include "zra.h"

#define CRCPP_USE_CPP11 1
#include <CRC.h>
#include <zstd.h>
#include <zstd_errors.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

#include "zra.hpp"

namespace zra {
  template <typename Type>
  constexpr Type ZStdCheck(Type object) {
    if (ZSTD_isError(object)) throw Exception(StatusCode::ZStdError, ZSTD_getErrorCode(object));
    return object;
  }

  template <typename Type, Type*(create)(), size_t(destroy)(Type*)>
  class ZCtx {
   public:
    Type* context;

    ZCtx() {
      context = create();
    }

    ~ZCtx() {
      destroy(context);
    }

    operator Type*() {
      return context;
    }
  };

  class ZCCtx : public ZCtx<ZSTD_CCtx, ZSTD_createCCtx, ZSTD_freeCCtx> {};
  class ZDCtx : public ZCtx<ZSTD_DCtx, ZSTD_createDCtx, ZSTD_freeDCtx> {};

  Exception::Exception(StatusCode code, int zstdCode) : code(code), zstdCode(zstdCode) {}

  const char* Exception::GetExceptionString(StatusCode code) {
    switch (code) {
      case StatusCode::Success:
        return "The operation was successful";
      case StatusCode::ZStdError:
        return "An error was returned by ZStandard";
      case StatusCode::ZraVersionLow:
        return "This archive was compressed using a newer version of ZRA";
      case StatusCode::HeaderInvalid:
        return "The header in the supplied buffer was invalid";
      case StatusCode::HeaderIncomplete:
        return "The header hasn't been fully written before being accessed";
      case StatusCode::OutOfBoundsAccess:
        return "The specified offset and size are past the data contained within the buffer";
      case StatusCode::OutputBufferTooSmall:
        return "The output buffer is too small to contain the output";
      case StatusCode::CompressedSizeTooLarge:
        return "The compressed output's size exceeds the maximum limit";
      case StatusCode::InputFrameSizeMismatch:
        return "The input size is not divisible by the frame size and nor is it the final frame";
    }

    return "An unknown error has occurred";
  }

  const char* Exception::what() const noexcept {
    if (code == StatusCode::ZStdError) {
      static std::string reason;
      reason = GetExceptionString(code);
      reason = (reason + ": ").append(ZSTD_getErrorString(static_cast<ZSTD_ErrorCode>(zstdCode)));
      return reason.c_str();
    } else {
      return GetExceptionString(code);
    }
  }

  u16 GetVersion() {
    return 1;
  }

  // clang-format off
#pragma pack(push, 1)
#pragma scalar_storage_order little-endian;
  // clang-format on

  /**
   * @brief This structure holds a single entry in the seek table
   */
  struct Entry {
    u64 offset : 40;  //!< The offset of the frame in the compressed segment

    operator u64() const {
      return offset;
    }

    Entry& operator=(const u64& off) {
      this->offset = off;
      return *this;
    }
  };

  constexpr u64 maxCompressedSize = 1ULL << 40;  //!< The maximum size of a compressed ZSTD file

  struct FixedHeader {
    u32 frameId{0x184D2A50};    //!< The frame ID for a ZSTD skippable frame
    u32 headerSize{};           //!< The size of the header after this in bytes
    u32 magic{0x3041525A};      //!< The magic for the ZRA format "ZRA0"
    u16 version{GetVersion()};  //!< The version of ZRA the archive was compressed with
    u32 hash{};                 //!< The CRC-32 hash of the header
    u64 uncompressedSize{};     //!< The size of the original data, this is used for bounds-checking and buffer pre-allocation
    u32 tableSize{};            //!< The amount of entries present in the seek table
    u32 frameSize{};            //!< The size of frames except for the final frame
    u32 metaSize{};             //!< The size of the metadata section

    inline FixedHeader() = default;

    inline FixedHeader(u64 origSize, u32 tableSize, u32 frameSize, u32 metaSize) : uncompressedSize(origSize), tableSize(tableSize), frameSize(frameSize), metaSize(metaSize) {
      headerSize = sizeof(FixedHeader) + metaSize + (tableSize * sizeof(Entry)) - (offsetof(FixedHeader, headerSize) + sizeof(headerSize));
    }

    u32 CalculateHash(const u8* rest) const {
      auto crc = CRC::Calculate(this, offsetof(FixedHeader, hash), CRC::CRC_32());
      auto hashOffset = offsetof(FixedHeader, hash) + sizeof(hash);
      crc = CRC::Calculate(reinterpret_cast<const u8*>(this) + hashOffset, sizeof(FixedHeader) - hashOffset, CRC::CRC_32(), crc);
      return CRC::Calculate(rest, headerSize - sizeof(FixedHeader) + offsetof(FixedHeader, headerSize) + sizeof(headerSize), CRC::CRC_32(), crc);
    }
  };

  // clang-format off
#pragma scalar_storage_order default
#pragma pack(pop)
  // clang-format on

  Header::Header(const std::function<void(size_t, size_t, void*)>& readFunction) : readFunction(readFunction) {
    FixedHeader fixed;
    readFunction(0, sizeof(fixed), &fixed);
    if (fixed.magic != 0x3041525A || fixed.version > GetVersion())
      throw Exception(StatusCode::HeaderInvalid);

    version = fixed.version;
    size = fixed.headerSize + offsetof(FixedHeader, headerSize) + sizeof(fixed.headerSize);
    uncompressedSize = fixed.uncompressedSize;
    frameSize = fixed.frameSize;
    metaOffset = sizeof(FixedHeader);
    metaSize = fixed.metaSize;
    seekTableOffset = metaOffset + metaSize;
    seekTableSize = fixed.tableSize * sizeof(Entry);

    switch (fixed.version) {
      case 1:
        break;

      default:
        throw Exception(StatusCode::ZraVersionLow);
    }
  }

  Header::Header(const BufferView& buffer) : Header([&buffer](size_t offset, size_t readSize, void* outBuffer) {
                                               if ((offset + readSize) >= buffer.size) throw Exception(StatusCode::OutOfBoundsAccess);
                                               memcpy(outBuffer, buffer.data + offset, readSize);
                                             }) {
    if (buffer.size < size)
      throw Exception(StatusCode::OutOfBoundsAccess);
  }

  Buffer Header::GetSeekTable() const {
    zra::Buffer seekTable(seekTableSize);
    readFunction(seekTableOffset, seekTableSize, seekTable.data());
    return seekTable;
  }

  void Header::GetMetadata(const BufferView& buffer) const {
    readFunction(metaOffset, metaSize, buffer.data);
  }

  Buffer Header::GetMetadata() const {
    zra::Buffer meta(metaSize);
    GetMetadata(meta);
    return meta;
  }

  size_t GetOutputBufferSize(size_t inputSize, u32 frameSize, u32 metaSize) {
    u32 tableSize = (inputSize / frameSize) + ((inputSize % frameSize) ? 2 : 1);
    return sizeof(FixedHeader) + metaSize + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * (tableSize - 1));
  }

  size_t CompressBuffer(const BufferView& input, const BufferView& output, i8 compressionLevel, u32 frameSize, bool checksum, const BufferView& meta) {
    u32 tableSize = (input.size / frameSize) + ((input.size % frameSize) ? 2 : 1);
    auto outputSize = sizeof(FixedHeader) + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * (tableSize - 1));
    if (output.size < outputSize)
      throw Exception(StatusCode::OutputBufferTooSmall);

    size_t outputOffset{};
    auto header = reinterpret_cast<FixedHeader*>(output.data);
    *header = FixedHeader(input.size, tableSize, frameSize, meta.size);
    outputOffset += sizeof(FixedHeader);

    auto entry = reinterpret_cast<Entry*>(output.data + outputOffset);
    outputOffset += tableSize * sizeof(Entry);
    auto compressedOffset = outputOffset;

    ZCCtx ctx;
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_contentSizeFlag, false);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_checksumFlag, checksum);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_dictIDFlag, false);

    auto remaining = input.size;
    while (remaining) {
      frameSize = std::min(static_cast<size_t>(frameSize), remaining);

      auto compressedSize = ZStdCheck(ZSTD_compress2(ctx, output.data + outputOffset, output.size - outputOffset, input.data + (input.size - remaining), frameSize));

      entry++->offset = outputOffset - compressedOffset;

      outputOffset += compressedSize;
      remaining -= frameSize;
    }

    if (outputOffset >= maxCompressedSize)
      throw Exception(StatusCode::CompressedSizeTooLarge);

    entry->offset = outputOffset - compressedOffset;
    header->hash = header->CalculateHash(output.data + sizeof(FixedHeader));

    return outputOffset;
  }

  Buffer CompressBuffer(const BufferView& buffer, i8 compressionLevel, u32 frameSize, bool checksum, const BufferView& meta) {
    Buffer output(GetOutputBufferSize(buffer.size, frameSize));
    output.resize(CompressBuffer(buffer, output, compressionLevel, frameSize, checksum, meta));
    output.shrink_to_fit();
    return output;
  }

  void DecompressBuffer(const BufferView& input, const BufferView& output) {
    Header header(input);
    if (output.size < header.uncompressedSize)
      throw Exception(StatusCode::OutputBufferTooSmall);

    ZDCtx ctx;
    ZStdCheck(ZSTD_decompressDCtx(ctx, output.data, output.size, input.data + header.size, input.size - header.size));
  }

  Buffer DecompressBuffer(const BufferView& buffer) {
    Buffer output(reinterpret_cast<const FixedHeader*>(buffer.data)->uncompressedSize);
    DecompressBuffer(buffer, output);
    return output;
  }

  void DecompressRA(const BufferView& input, const BufferView& output, size_t offset, size_t size) {
    Header header(input);
    if (offset + size >= header.uncompressedSize)
      throw Exception(StatusCode::OutOfBoundsAccess);
    if (output.size < size)
      throw Exception(StatusCode::OutputBufferTooSmall);

    auto frameOffset = std::lldiv(offset, header.frameSize);
    auto frameSize = std::lldiv(static_cast<size_t>(frameOffset.rem + size), header.frameSize);
    auto firstFrame = reinterpret_cast<const Entry*>(input.data + header.seekTableOffset) + frameOffset.quot;
    auto lastFrame = firstFrame + (frameSize.quot + (frameSize.rem ? 1 : 0));
    size_t compressedSize{*lastFrame - *firstFrame};

    ZDCtx ctx;

    std::optional<Buffer> frameBuffer;
    if (frameOffset.rem || frameSize.rem)
      frameBuffer.emplace(header.frameSize);

    u8* contents = input.data + header.size;
    size_t outputOffset{};
    if (frameOffset.rem) {
      ZStdCheck(ZSTD_decompressDCtx(ctx, frameBuffer->data(), frameBuffer->size(), contents + *firstFrame, *(firstFrame + 1) - *firstFrame));
      auto minSize = std::min(size, static_cast<size_t>(static_cast<size_t>(header.frameSize) - frameOffset.rem));
      std::memcpy(output.data, frameBuffer->data() + frameOffset.rem, minSize);
      outputOffset += minSize;
      firstFrame++;
    }

    if (outputOffset < size) {
      compressedSize = *(frameSize.rem ? lastFrame - 1 : lastFrame) - *firstFrame;
      outputOffset += ZStdCheck(ZSTD_decompressDCtx(ctx, output.data + outputOffset, output.size - outputOffset, contents + *firstFrame, compressedSize));
    }

    if (outputOffset < size && frameSize.rem) {
      ZStdCheck(ZSTD_decompressDCtx(ctx, frameBuffer->data(), frameBuffer->size(), contents + *(lastFrame - 1), *lastFrame - *(lastFrame - 1)));
      std::memcpy(output.data + outputOffset, frameBuffer->data(), size - outputOffset);
    }
  }

  Buffer DecompressRA(const BufferView& buffer, size_t offset, size_t size) {
    Buffer output(size);
    DecompressRA(buffer, output, offset, size);
    return output;
  }

  Compressor::Compressor(size_t size, i8 compressionLevel, u32 frameSize, bool checksum, const BufferView& meta) : ctx(std::make_shared<ZCCtx>()), frameSize(frameSize), tableSize(static_cast<u32>((size / frameSize) + ((size % frameSize) ? 2 : 1))), header(sizeof(FixedHeader) + meta.size + (sizeof(Entry) * tableSize)), entry(reinterpret_cast<Entry*>(header.data() + sizeof(FixedHeader) + meta.size)) {
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_contentSizeFlag, false);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_checksumFlag, checksum);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_dictIDFlag, false);

    *reinterpret_cast<FixedHeader*>(header.data()) = FixedHeader(size, tableSize, frameSize, meta.size);
    if(meta.data) memcpy(header.data() + sizeof(FixedHeader), meta.data, meta.size);
  }

  size_t Compressor::GetOutputBufferSize(size_t inputSize) const {
    return ZSTD_compressBound(frameSize) * ((inputSize / frameSize) + ((inputSize % frameSize) ? 1 : 0));
  }

  size_t Compressor::Compress(const BufferView& input, const BufferView& output) {
    auto outputSize = GetOutputBufferSize(input.size);
    if (output.size < outputSize)
      throw Exception(StatusCode::OutputBufferTooSmall);

    auto entryIndex = entry - reinterpret_cast<Entry*>(header.data() + sizeof(FixedHeader));
    if (input.size % frameSize && (entryIndex + (input.size / frameSize) + 2) < tableSize)
      throw Exception(StatusCode::InputFrameSizeMismatch);

    size_t remaining = input.size, compressOffset{};
    while (remaining) {
      frameSize = std::min(static_cast<size_t>(frameSize), remaining);
      auto frameCompressedSize = ZStdCheck(ZSTD_compress2(*ctx, output.data + compressOffset, output.size - compressOffset, input.data + (input.size - remaining), frameSize));

      entry++->offset = outputOffset;

      outputOffset += frameCompressedSize;
      compressOffset += frameCompressedSize;
      remaining -= frameSize;
    }

    if (outputOffset >= maxCompressedSize)
      throw Exception(StatusCode::CompressedSizeTooLarge);

    if (entry == reinterpret_cast<Entry*>(header.data() + header.size()) - 1) {
      entry++->offset = outputOffset;
      auto headerObject = reinterpret_cast<FixedHeader*>(header.data());
      headerObject->hash = headerObject->CalculateHash(header.data() + sizeof(FixedHeader));
    }

    return compressOffset;
  }

  void Compressor::Compress(const BufferView& input, Buffer& output) {
    output.resize(GetOutputBufferSize(input.size));
    output.resize(Compress(input, BufferView(output)));
  }

  const Buffer& Compressor::GetHeader() {
    if (entry == reinterpret_cast<Entry*>(header.data() + header.size()))
      return header;
    throw Exception(StatusCode::HeaderIncomplete);
  }

  size_t Compressor::GetHeaderSize() {
    return header.size();
  }

  Decompressor::Decompressor(const std::function<void(size_t, size_t, void*)>& readFunction, size_t maxCacheSize) : ctx(std::make_shared<ZDCtx>()), readFunction(readFunction), header(readFunction), seekTable(header.GetSeekTable()), maxCacheSize(maxCacheSize) {}

  void Decompressor::Decompress(size_t offset, size_t size, const BufferView& output) {
    if (offset + size > header.uncompressedSize)
      throw Exception(StatusCode::OutOfBoundsAccess);
    if (output.size < size)
      throw Exception(StatusCode::OutputBufferTooSmall);

    auto frameOffset = std::lldiv(offset, header.frameSize);
    auto frameSize = std::lldiv(static_cast<size_t>(frameOffset.rem + size), header.frameSize);
    auto firstFrame = reinterpret_cast<const Entry*>(seekTable.data()) + frameOffset.quot;
    auto lastFrame = firstFrame + (frameSize.quot + (frameSize.rem ? 1 : 0));

    auto initialOffset = *firstFrame;
    size_t compressedSize{*lastFrame - *firstFrame};

    std::optional<Buffer> inputBuffer;
    if (compressedSize > maxCacheSize)
      inputBuffer.emplace(compressedSize);

    auto& input = inputBuffer ? *inputBuffer : cache;
    input.resize(compressedSize);
    readFunction(header.size + *firstFrame, compressedSize, input.data());

    std::optional<Buffer> frameBuffer;
    if (frameOffset.rem || frameSize.rem)
      frameBuffer.emplace(header.frameSize);

    size_t outputOffset{};
    if (frameOffset.rem) {
      ZStdCheck(ZSTD_decompressDCtx(*ctx, frameBuffer->data(), frameBuffer->size(), input.data(), *(firstFrame + 1) - *firstFrame));
      auto minSize = std::min(size, static_cast<size_t>(static_cast<size_t>(header.frameSize) - frameOffset.rem));
      std::memcpy(output.data, frameBuffer->data() + frameOffset.rem, minSize);
      outputOffset += minSize;
      firstFrame++;
    }

    if (outputOffset < size) {
      compressedSize = *(frameSize.rem ? lastFrame - 1 : lastFrame) - *firstFrame;
      outputOffset += ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data + outputOffset, output.size - outputOffset, input.data() + (*firstFrame - initialOffset), compressedSize));
    }

    if (outputOffset < size && frameSize.rem) {
      ZStdCheck(ZSTD_decompressDCtx(*ctx, frameBuffer->data(), frameBuffer->size(), input.data() + (*(lastFrame - 1) - initialOffset), *lastFrame - *(lastFrame - 1)));
      std::memcpy(output.data + outputOffset, frameBuffer->data(), size - outputOffset);
    }
  }

  void Decompressor::Decompress(size_t offset, size_t size, Buffer& output) {
    output.resize(size);
    Decompress(offset, size, BufferView(output));
  }

  Buffer Decompressor::Decompress(size_t offset, size_t size) {
    zra::Buffer buffer;
    Decompress(offset, size, buffer);
    return buffer;
  }

  FullDecompressor::FullDecompressor(const std::function<void(size_t offset, size_t size, void* buffer)>& readFunction) : ctx(std::make_shared<ZDCtx>()), readFunction(readFunction), header(readFunction), seekTable(header.GetSeekTable()), entry(reinterpret_cast<Entry*>(seekTable.data())) {}

  size_t FullDecompressor::Decompress(const BufferView& output) {
    if (output.size < header.frameSize)
      throw Exception(StatusCode::OutputBufferTooSmall);
    auto lastEntry = std::min(reinterpret_cast<Entry*>(seekTable.data() + seekTable.size() - sizeof(Entry)), entry + (output.size / header.frameSize));
    cache.resize(*lastEntry - *entry);
    readFunction(header.size + *entry, cache.size(), cache.data());
    entry = lastEntry;
    return ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data, output.size, cache.data(), cache.size()));
  }
}  // namespace zra

namespace {
  constexpr ZraStatus MakeStatus(ZraStatusCode zra, zra::i8 zstd = 0) {
    return ZraStatus{zra, zstd};
  }

  constexpr ZraStatus MakeStatus(const zra::Exception& e) {
    return MakeStatus(static_cast<ZraStatusCode>(e.code), e.zstdCode);
  }
}  // namespace

uint16_t ZraGetVersion() {
  return zra::GetVersion();
}

const char* ZraGetErrorString(ZraStatus status) {
  return zra::Exception(static_cast<zra::StatusCode>(status.zra), status.zstd).what();
}

ZraStatus ZraCreateHeader(ZraHeader** header, void (*readFunction)(size_t, size_t, void*)) {
  try {
    *header = reinterpret_cast<ZraHeader*>(new zra::Header(readFunction));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraCreateHeader2(ZraHeader** header, void* buffer, size_t size) {
  try {
    *header = reinterpret_cast<ZraHeader*>(new zra::Header(zra::BufferView(buffer, size)));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

void ZraDeleteHeader(ZraHeader* header) {
  delete reinterpret_cast<zra::Header*>(header);
}

size_t ZraGetVersionWithHeader(ZraHeader* header) {
  return reinterpret_cast<zra::Header*>(header)->version;
}

size_t ZraGetHeaderSizeWithHeader(ZraHeader* header) {
  return reinterpret_cast<zra::Header*>(header)->size;
}

size_t ZraGetUncompressedSizeWithHeader(ZraHeader* header) {
  return reinterpret_cast<zra::Header*>(header)->uncompressedSize;
}

size_t ZraGetFrameSizeWithHeader(ZraHeader* header) {
  return reinterpret_cast<zra::Header*>(header)->frameSize;
}

size_t ZraGetMetadataSize(ZraHeader* header) {
  return reinterpret_cast<zra::Header*>(header)->metaSize;
}

void ZraGetMetadata(ZraHeader* header, void* buffer) {
  auto headerObject = reinterpret_cast<zra::Header*>(header);
  headerObject->GetMetadata(zra::BufferView(buffer, headerObject->metaSize));
}

size_t ZraGetCompressedOutputBufferSize(size_t inputSize, size_t frameSize) {
  return zra::GetOutputBufferSize(inputSize, frameSize);
}

ZraStatus ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize, int8_t compressionLevel, uint32_t frameSize, bool checksum, void* metaBuffer, size_t metaSize) {
  try {
    *outputSize = zra::CompressBuffer(zra::BufferView(inputBuffer, inputSize), zra::BufferView(outputBuffer, zra::GetOutputBufferSize(inputSize, frameSize)), compressionLevel, frameSize, checksum, zra::BufferView(metaBuffer, metaSize));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraDecompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer) {
  try {
    zra::DecompressBuffer(zra::BufferView(inputBuffer, inputSize), zra::BufferView(outputBuffer, reinterpret_cast<zra::FixedHeader*>(inputBuffer)->uncompressedSize));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraDecompressRA(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t offset, size_t size) {
  try {
    zra::DecompressRA(zra::BufferView(inputBuffer, inputSize), zra::BufferView(outputBuffer, size), offset, size);
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel, uint32_t frameSize, bool checksum, void* metaBuffer, size_t metaSize) {
  try {
    *compressor = reinterpret_cast<ZraCompressor*>(new zra::Compressor(size, compressionLevel, frameSize, checksum, zra::BufferView(metaBuffer, metaSize)));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(static_cast<ZraStatusCode>(e.code), e.zstdCode);
  }
}

void ZraDeleteCompressor(ZraCompressor* compressor) {
  delete reinterpret_cast<zra::Compressor*>(compressor);
}

size_t ZraGetOutputBufferSizeWithCompressor(ZraCompressor* compressor, size_t inputSize) {
  return reinterpret_cast<zra::Compressor*>(compressor)->GetOutputBufferSize(inputSize);
}

ZraStatus ZraCompressWithCompressor(ZraCompressor* _compressor, void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize) {
  try {
    auto compressor = reinterpret_cast<zra::Compressor*>(_compressor);
    *outputSize = compressor->Compress(zra::BufferView(inputBuffer, inputSize), zra::BufferView(outputBuffer, compressor->GetOutputBufferSize(inputSize)));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

size_t ZraGetHeaderSizeWithCompressor(ZraCompressor* compressor) {
  return reinterpret_cast<zra::Compressor*>(compressor)->GetHeaderSize();
}

ZraStatus ZraGetHeaderWithCompressor(ZraCompressor* compressor, void* outputBuffer) {
  try {
    auto header = reinterpret_cast<zra::Compressor*>(compressor)->GetHeader();
    std::memcpy(outputBuffer, header.data(), header.size());
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraCreateDecompressor(ZraDecompressor** decompressor, void (*readFunction)(size_t, size_t, void*), size_t maxCacheSize) {
  try {
    *decompressor = reinterpret_cast<ZraDecompressor*>(new zra::Decompressor(readFunction, maxCacheSize));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(static_cast<ZraStatusCode>(e.code), e.zstdCode);
  }
}

void ZraDeleteDecompressor(ZraDecompressor* decompressor) {
  delete reinterpret_cast<zra::Decompressor*>(decompressor);
}

ZraHeader* ZraGetHeaderWithDecompressor(ZraDecompressor* decompressor) {
  return reinterpret_cast<ZraHeader*>(&reinterpret_cast<zra::Decompressor*>(decompressor)->header);
}

ZraStatus ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer) {
  try {
    reinterpret_cast<zra::Decompressor*>(decompressor)->Decompress(offset, size, zra::BufferView(outputBuffer, size));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraCreateFullDecompressor(ZraFullDecompressor** decompressor, void (readFunction)(size_t, size_t, void*), size_t maxCacheSize) {
  try {
    *decompressor = reinterpret_cast<ZraFullDecompressor*>(new zra::FullDecompressor(readFunction));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(static_cast<ZraStatusCode>(e.code), e.zstdCode);
  }
}

void ZraDeleteFullDecompressor(ZraFullDecompressor* decompressor) {
  delete reinterpret_cast<zra::FullDecompressor*>(decompressor);
}

ZraHeader* ZraGetHeaderWithFullDecompressor(ZraFullDecompressor* decompressor) {
  return reinterpret_cast<ZraHeader*>(&reinterpret_cast<zra::FullDecompressor*>(decompressor)->header);
}

ZraStatus ZraDecompressWithFullDecompressor(ZraFullDecompressor* decompressor, void* outputBuffer, size_t outputCapacity, size_t* outputSize) {
  try {
    *outputSize = reinterpret_cast<zra::FullDecompressor*>(decompressor)->Decompress(zra::BufferView(outputBuffer, outputCapacity));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}