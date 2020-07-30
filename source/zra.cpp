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

  static_assert(ZSTD_error_maxCode < std::numeric_limits<u32>::max(), "ZSTD error codes have exceeded a 32-bit integer");

  Exception::Exception(StatusCode code, i32 zstdCode) : code(code), zstdCode(zstdCode) {}

  std::string_view Exception::GetExceptionString(StatusCode code) {
    switch (code) {
      case StatusCode::Success:
        return "The operation was successful";
      case StatusCode::ZStdError:
        return "An error was returned by ZStandard";
      case StatusCode::HeaderInvalid:
        return "The header in the supplied buffer was invalid";
      case StatusCode::HeaderIncomplete:
        return "The header hasn't been fully written before being accessed";
      case StatusCode::OutOfBoundsAccess:
        return "The specified offset and size are past the data contained within the buffer";
      case StatusCode::OutputBufferTooSmall:
        return "The output buffer is too small to contain the output (Supply null output buffer to get size)";
      case StatusCode::CompressedSizeTooLarge:
        return "The compressed output's size exceeds the maximum limit";
      case StatusCode::InputFrameSizeMismatch:
        return "The input size is not divisble by the frame size and it isn't the final frame";
    }

    return "The error code is unrecognized";
  }

  const char* Exception::what() const noexcept {
    if (code == StatusCode::ZStdError) {
      static std::string reason;
      reason = GetExceptionString(code);
      reason = (reason + ": ").append(ZSTD_getErrorString(static_cast<ZSTD_ErrorCode>(code)));
      return reason.c_str();
    } else {
      return GetExceptionString(code).data();
    }
  }

  u16 GetVersion() {
    return 0;
  }

  u32 Header::CalculateHash(const u8* rest) const {
    if (rest) {
      return CRC::Calculate(rest, tableSize * sizeof(Entry), CRC::CRC_32());
    } else {
      auto crc = CRC::Calculate(this, offsetof(Header, fixedHash), CRC::CRC_32());
      auto hashOffset = offsetof(Header, variableHash) + sizeof(variableHash);
      crc = CRC::Calculate(reinterpret_cast<const u8*>(this) + hashOffset, sizeof(Header) - hashOffset, CRC::CRC_32(), crc);
      return crc;
    }
  }

  size_t GetOutputBufferSize(size_t inputSize, size_t frameSize) {
    u32 tableSize = (inputSize / frameSize) + ((inputSize % frameSize) ? 1 : 0);
    return sizeof(Header) + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * tableSize);
  }

  size_t CompressBuffer(const BufferView& input, const BufferView& output, i8 compressionLevel, u32 frameSize, bool checksum) {
    u32 tableSize = (input.size / frameSize) + ((input.size % frameSize) ? 1 : 0);
    auto outputSize = sizeof(Header) + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * tableSize);
    if (output.size < outputSize)
      throw Exception(StatusCode::OutputBufferTooSmall);

    size_t outputOffset{};
    auto header = reinterpret_cast<Header*>(output.data);
    *header = Header(input.size, tableSize, frameSize);
    outputOffset += sizeof(Header);

    auto tableOffset = outputOffset;
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

      reinterpret_cast<Entry*>(output.data + tableOffset)->offset = outputOffset - compressedOffset;
      reinterpret_cast<Entry*>(output.data + tableOffset)->size = compressedSize;
      tableOffset += sizeof(Entry);

      outputOffset += compressedSize;
      remaining -= frameSize;

      if (outputOffset >= maxCompressedSize)
        throw Exception(StatusCode::CompressedSizeTooLarge);
    }

    header->variableHash = header->CalculateHash(output.data + sizeof(Header));

    return outputOffset;
  }

  Buffer CompressBuffer(const BufferView& buffer, i8 compressionLevel, u32 frameSize, bool checksum) {
    Buffer output(GetOutputBufferSize(buffer.size, frameSize));
    output.resize(CompressBuffer(buffer, output, compressionLevel, frameSize, checksum));
    output.shrink_to_fit();
    return output;
  }

  void DecompressBuffer(const BufferView& input, const BufferView& output) {
    size_t inputOffset{};
    const Header& header = *reinterpret_cast<const Header*>(input.data);
    if (!header.Valid(input.data + sizeof(Header)))
      throw Exception(StatusCode::HeaderInvalid);
    if (output.size < header.origSize)
      throw Exception(StatusCode::OutputBufferTooSmall);
    inputOffset += header.Size();

    ZDCtx ctx;
    ZStdCheck(ZSTD_decompressDCtx(ctx, output.data, output.size, input.data + inputOffset, input.size - inputOffset));
  }

  Buffer DecompressBuffer(const BufferView& buffer) {
    Buffer output(reinterpret_cast<const Header*>(buffer.data)->origSize);
    DecompressBuffer(buffer, output);
    return output;
  }

  void DecompressRA(const BufferView& input, const BufferView& output, size_t offset, size_t size) {
    const Header& header = *reinterpret_cast<const Header*>(input.data);
    if (!header.Valid(input.data + sizeof(Header)))
      throw Exception(StatusCode::HeaderInvalid);

    if (offset + size >= header.origSize)
      throw Exception(StatusCode::OutOfBoundsAccess);
    if (output.size < size)
      throw Exception(StatusCode::OutputBufferTooSmall);

    std::optional<Buffer> frameBuffer;

    auto frame = std::lldiv(offset, header.frameSize);
    auto frameEntry = reinterpret_cast<const Entry*>(input.data + sizeof(Header)) + frame.quot;

    auto compressedOffset = input.data + header.Size();

    ZDCtx ctx;
    size_t outputOffset{};

    if (frame.rem) {
      frameBuffer.emplace(header.frameSize);
      ZStdCheck(ZSTD_decompressDCtx(ctx, frameBuffer->data(), frameBuffer->size(), compressedOffset + frameEntry->offset, frameEntry->size));
      auto minSize = std::min(size, static_cast<size_t>(static_cast<size_t>(header.frameSize) - frame.rem));
      std::memcpy(output.data, frameBuffer->data() + frame.rem, minSize);
      outputOffset += minSize;
      frameEntry++;
    }

    while (outputOffset != size && outputOffset + frameEntry->size <= size) {
      outputOffset += ZStdCheck(ZSTD_decompressDCtx(ctx, output.data + outputOffset, output.size - outputOffset, compressedOffset + frameEntry->offset, frameEntry->size));
      frameEntry++;
    }

    if (outputOffset != size) {
      if (frameBuffer)
        frameBuffer.emplace(header.frameSize);
      ZStdCheck(ZSTD_decompressDCtx(ctx, frameBuffer->data(), frameBuffer->size(), compressedOffset + frameEntry->offset, frameEntry->size));
      std::memcpy(output.data + outputOffset, frameBuffer->data(), size - outputOffset);
    }
  }

  Buffer DecompressRA(const BufferView& buffer, size_t offset, size_t size) {
    Buffer output(size);
    DecompressRA(buffer, output, offset, size);
    return output;
  }

  Compressor::Compressor(size_t size, i8 compressionLevel, u32 frameSize, bool checksum) : ctx(std::make_shared<ZCCtx>()), frameSize(frameSize), tableSize(static_cast<u32>((size / frameSize) + ((size % frameSize) ? 1 : 0))), entryOffset(sizeof(Header)), header(sizeof(Header) + (sizeof(Entry) * tableSize)) {
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_contentSizeFlag, false);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_checksumFlag, checksum);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_dictIDFlag, false);

    *reinterpret_cast<Header*>(header.data()) = Header(size, tableSize, frameSize);
  }

  size_t Compressor::GetOutputBufferSize(size_t inputSize) const {
    return ZSTD_compressBound(frameSize) * ((inputSize / frameSize) + ((inputSize % frameSize) ? 1 : 0));
  }

  size_t Compressor::Compress(const BufferView& input, const BufferView& output) {
    auto outputSize = GetOutputBufferSize(input.size);
    if (output.size < outputSize)
      throw Exception(StatusCode::OutputBufferTooSmall);

    auto entryIndex = (entryOffset - sizeof(Header)) / sizeof(Entry);
    if (input.size % frameSize && (entryIndex + (input.size / frameSize) + 1) < tableSize)
      throw Exception(StatusCode::InputFrameSizeMismatch);

    size_t compressedSize{};
    size_t remaining = input.size;
    while (remaining) {
      frameSize = std::min(static_cast<size_t>(frameSize), remaining);
      auto frameCompressedSize = ZStdCheck(ZSTD_compress2(*ctx, output.data + compressedSize, output.size - compressedSize, input.data + (input.size - remaining), frameSize));

      reinterpret_cast<Entry*>(header.data() + entryOffset)->offset = outputOffset;
      reinterpret_cast<Entry*>(header.data() + entryOffset)->size = frameCompressedSize;
      entryOffset += sizeof(Entry);
      entryIndex++;

      compressedSize += frameCompressedSize;
      remaining -= frameSize;
    }

    if (entryIndex == tableSize) {
      auto headerObject = reinterpret_cast<Header*>(header.data());
      headerObject->variableHash = headerObject->CalculateHash(header.data() + sizeof(Header));
    }

    outputOffset += compressedSize;
    return compressedSize;
  }

  void Compressor::Compress(const BufferView& input, Buffer& output) {
    output.resize(GetOutputBufferSize(input.size));
    output.resize(Compress(input, BufferView(output)));
  }

  const Buffer& Compressor::GetHeader() {
    if (entryOffset == header.size() && reinterpret_cast<Header*>(header.data())->Valid(header.data() + sizeof(Header)))
      return header;
    throw Exception(StatusCode::HeaderIncomplete);
  }

  size_t Compressor::GetHeaderSize() {
    return header.size();
  }

  Decompressor::Decompressor(const Buffer& header, const std::function<void(size_t, size_t, BufferView)>& readFunction, size_t maxCacheSize) : ctx(std::make_shared<ZDCtx>()), header(header), readFunction(readFunction), maxCacheSize(maxCacheSize) {
    auto _header = reinterpret_cast<Header*>(this->header.data());
    if (this->header.size() < sizeof(Header) || !_header->Valid(this->header.data() + sizeof(Header)) || _header->Size() != this->header.size())
      throw Exception(StatusCode::HeaderInvalid);

    frameSize = reinterpret_cast<Header*>(this->header.data())->frameSize;
    inputSize = reinterpret_cast<Header*>(this->header.data())->origSize;
  }

  void Decompressor::Decompress(size_t offset, size_t size, const BufferView& output) {
    if (offset + size > inputSize)
      throw Exception(StatusCode::OutOfBoundsAccess);
    if (output.size < size)
      throw Exception(StatusCode::OutputBufferTooSmall);

    std::optional<Buffer> frameBuffer;

    auto frame = std::lldiv(offset, frameSize);
    auto frameEntry = reinterpret_cast<const Entry*>(header.data() + sizeof(Header)) + frame.quot;
    auto initialOffset = frameEntry->offset;

    size_t compressedSize{}, readSize{frame.rem + size};
    auto lastFrame = frameEntry + ((readSize / frameSize) + ((readSize % frameSize) ? 1 : 0));
    for (auto currentFrame = frameEntry; currentFrame != lastFrame; currentFrame++)
      compressedSize += currentFrame->size;

    std::optional<Buffer> inputBuffer;
    if (compressedSize > maxCacheSize)
      inputBuffer.emplace(compressedSize);

    auto& input = inputBuffer ? *inputBuffer : cache;

    input.resize(compressedSize);

    readFunction(initialOffset, compressedSize, input);

    size_t outputOffset{};

    if (frame.rem) {
      frameBuffer.emplace(frameSize);
      ZStdCheck(ZSTD_decompressDCtx(*ctx, frameBuffer->data(), frameBuffer->size(), input.data() + (frameEntry->offset - initialOffset), frameEntry->size));
      auto minSize = std::min(size, static_cast<size_t>(static_cast<size_t>(frameSize) - frame.rem));
      std::memcpy(output.data, frameBuffer->data() + frame.rem, minSize);
      outputOffset += minSize;
      frameEntry++;
    }

    while (outputOffset != size && outputOffset + frameEntry->size <= size) {
      outputOffset += ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data + outputOffset, output.size - outputOffset, input.data() + (frameEntry->offset - initialOffset), frameEntry->size));
      frameEntry++;
    }

    if (outputOffset != size) {
      if (frameBuffer)
        frameBuffer.emplace(frameSize);
      ZStdCheck(ZSTD_decompressDCtx(*ctx, frameBuffer->data(), frameBuffer->size(), input.data() + (frameEntry->offset - initialOffset), frameEntry->size));
      std::memcpy(output.data + outputOffset, frameBuffer->data(), size - outputOffset);
    }
  }

  void Decompressor::Decompress(size_t offset, size_t size, Buffer& output) {
    output.resize(size);
    Decompress(offset, size, BufferView(output));
  }

  FullDecompressor::FullDecompressor(Buffer header) : ctx(std::make_shared<ZDCtx>()), header(std::move(header)), entryOffset(sizeof(Header)), frameSize(reinterpret_cast<Header*>(this->header.data())->frameSize) {
    if (!reinterpret_cast<Header*>(this->header.data())->Valid(this->header.data() + sizeof(Header)))
      throw Exception(StatusCode::HeaderInvalid);
  }

  void FullDecompressor::Decompress(Buffer& input, Buffer& output) {
    auto entry = reinterpret_cast<Entry*>(header.data() + entryOffset);

    size_t outputOffset{};
    if (!frame.empty()) {
      auto endIterator = input.begin() + std::min(static_cast<size_t>(entry->size - frame.size()), input.size());
      std::move(input.begin(), endIterator, std::back_inserter(frame));
      input.erase(input.begin(), endIterator);

      if (frame.size() == entry->size) {
        output.resize(frameSize);
        output.resize(ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data(), output.size(), frame.data(), frame.size())));
        outputOffset = output.size();
        frame.clear();

        entryOffset += sizeof(Entry);
        entry++;
      } else {
        output.clear();
        return;
      }
    }

    size_t size{}, outputSize{};
    while (size + entry->size <= input.size() && entryOffset <= header.size()) {
      size += entry->size;
      entryOffset += sizeof(Entry);
      entry++;
      outputSize += frameSize;
    }

    if (size != input.size()) {
      auto endIterator = input.begin() + std::min(size + entry->size, input.size());
      std::move(input.begin() + size, endIterator, std::back_inserter(frame));
      input.erase(input.begin() + size, endIterator);
    }

    output.resize(outputOffset + outputSize);
    output.resize(outputOffset + ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data() + outputOffset, output.size() - outputOffset, input.data(), size)));
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

size_t ZraGetHeaderSize() {
  return sizeof(zra::Header);
}

ZraStatus ZraValidateHeader(void* headerBuffer, bool fullHeader) {
  auto header = reinterpret_cast<zra::Header*>(headerBuffer);
  auto valid = fullHeader ? header->Valid(static_cast<const zra::u8*>(headerBuffer) + sizeof(zra::Header)) : header->Valid();
  if (!valid)
    return MakeStatus(ZraStatusCode::HeaderInvalid);
  return MakeStatus(ZraStatusCode::Success);
}

size_t ZraGetFullHeaderSize(void* header) {
  return reinterpret_cast<zra::Header*>(header)->Size();
}

size_t ZraGetUncompressedSize(void* header) {
  return reinterpret_cast<zra::Header*>(header)->origSize;
}

size_t ZraGetOutputBufferSize(size_t inputSize, size_t frameSize) {
  return zra::GetOutputBufferSize(inputSize, frameSize);
}

ZraStatus ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t* outputSize, int8_t compressionLevel, uint32_t frameSize, bool checksum) {
  try {
    *outputSize = zra::CompressBuffer(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), zra::GetOutputBufferSize(inputSize, frameSize)), compressionLevel, frameSize, checksum);
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraDecompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer) {
  try {
    zra::DecompressBuffer(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), reinterpret_cast<zra::Header*>(inputBuffer)->origSize));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraDecompressRA(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t offset, size_t size) {
  try {
    zra::DecompressRA(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), size), offset, size);
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}

ZraStatus ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel, uint32_t frameSize, bool checksum) {
  try {
    *compressor = reinterpret_cast<ZraCompressor*>(new zra::Compressor(size, compressionLevel, frameSize, checksum));
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
    *outputSize = compressor->Compress(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), compressor->GetOutputBufferSize(inputSize)));
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

ZraStatus ZraCreateDecompressor(ZraDecompressor** decompressor, void* header, void (*readFunction)(size_t, size_t, void*), size_t maxCacheSize) {
  try {
    *decompressor = reinterpret_cast<ZraDecompressor*>(new zra::Decompressor(
        zra::Buffer(reinterpret_cast<zra::u8*>(header), reinterpret_cast<zra::u8*>(header) + reinterpret_cast<zra::Header*>(header)->Size()), [readFunction](size_t off, size_t size, zra::BufferView buffer) { readFunction(off, size, buffer.data); }, maxCacheSize));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(static_cast<ZraStatusCode>(e.code), e.zstdCode);
  }
}

void ZraDeleteDecompressor(ZraDecompressor* decompressor) {
  delete reinterpret_cast<zra::Decompressor*>(decompressor);
}

ZraStatus ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer) {
  try {
    reinterpret_cast<zra::Decompressor*>(decompressor)->Decompress(offset, size, zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), size));
    return MakeStatus(ZraStatusCode::Success);
  } catch (const zra::Exception& e) {
    return MakeStatus(e);
  }
}
