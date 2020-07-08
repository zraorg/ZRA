#define ZSTD_STATIC_LINKING_ONLY 1

#include "zra.hpp"

#include <zstd.h>

#include <optional>
#include <stdexcept>

#include "zra.h"

namespace zra {
  template <typename Type>
  constexpr Type ZStdCheck(Type object) {
    if (ZSTD_isError(object)) throw Exception(ErrorCode::ZStdError, ZSTD_getErrorName(object));
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

  Exception::Exception(ErrorCode code, const std::string_view& message) : code(code), message(message) {}

  std::string_view Exception::GetExceptionString(ErrorCode code) {
    switch (code) {
      case ErrorCode::Success:
        return "The operation was successful";
      case ErrorCode::ZStdError:
        return "An error was returned by ZStandard";
      case ErrorCode::HeaderInvalid:
        return "The header in the supplied buffer was invalid";
      case ErrorCode::OutOfBoundsAccess:
        return "The specified offset and size are past the data contained within the buffer";
      case ErrorCode::OutputBufferTooSmall:
        return "The output buffer is too small to contain the output (Supply null output buffer to get size)";
      case ErrorCode::CompressedSizeTooLarge:
        return "The compressed output's size exceeds the maximum limit";
      case ErrorCode::InputFrameSizeMismatch:
        return "The input size is not divisble by the frame size and it isn't the final frame";
    }
  }

  const char* Exception::what() const noexcept {
    static std::string reason;
    reason = GetExceptionString(code);
    if (!message.empty())
      reason = (reason + ": ").append(message);
    return reason.c_str();
  }

  u16 GetVersion() {
    return 0;
  }

  ssize_t CompressBuffer(const BufferView& input, const BufferView& output, i8 compressionLevel, u64 frameSize) {
    u32 tableSize = (input.size / frameSize) + ((input.size % frameSize) ? 1 : 0);

    auto outputSize = sizeof(Header) + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * tableSize);
    if (output.size < outputSize || output.data == nullptr)
      return -outputSize;

    size_t outputOffset{};
    *reinterpret_cast<Header*>(output.data) = {
        .inputSize = input.size,
        .tableSize = tableSize,
        .frameSize = frameSize,
    };
    outputOffset += sizeof(Header);

    auto tableOffset = outputOffset;
    outputOffset += tableSize * sizeof(Entry);
    auto compressedOffset = outputOffset;

    ZCCtx ctx;
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_contentSizeFlag, false);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_dictIDFlag, false);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_format, ZSTD_format_e::ZSTD_f_zstd1_magicless);
    ZSTD_CCtx_setParameter(ctx, ZSTD_cParameter::ZSTD_c_srcSizeHint, frameSize);

    auto remaining = input.size;
    while (remaining) {
      frameSize = std::min(static_cast<u64>(frameSize), remaining);

      auto compressedSize = ZStdCheck(ZSTD_compress2(ctx, output.data + outputOffset, output.size - outputOffset, input.data + (input.size - remaining), frameSize));

      reinterpret_cast<Entry*>(output.data + tableOffset)->offset = outputOffset - compressedOffset;
      reinterpret_cast<Entry*>(output.data + tableOffset)->size = compressedSize;
      tableOffset += sizeof(Entry);

      outputOffset += compressedSize;
      remaining -= frameSize;

      if (outputOffset >= maxCompressedSize)
        throw Exception(ErrorCode::CompressedSizeTooLarge);
    }

    return outputOffset;
  }

  Buffer CompressBuffer(const BufferView& buffer, i8 compressionLevel, u64 frameSize) {
    Buffer output(-CompressBuffer(buffer, {}, compressionLevel, frameSize));
    output.resize(CompressBuffer(buffer, output, compressionLevel, frameSize));
    output.shrink_to_fit();
    return output;
  }

  ssize_t DecompressBuffer(const BufferView& input, const BufferView& output) {
    size_t inputOffset{};
    const Header& header = *reinterpret_cast<const Header*>(input.data);
    if (!header.Valid())
      throw Exception(ErrorCode::HeaderInvalid);
    inputOffset += sizeof(Header);

    if (output.size < header.inputSize || output.data == nullptr)
      return -header.inputSize;

    inputOffset += header.tableSize * sizeof(Entry);

    ZDCtx ctx;
    ZSTD_DCtx_setFormat(ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);

    ZStdCheck(ZSTD_decompressDCtx(ctx, output.data, output.size, input.data + inputOffset, input.size - inputOffset));

    return header.inputSize;
  }

  Buffer DecompressBuffer(const BufferView& buffer) {
    Buffer output(-DecompressBuffer(buffer, {}));
    output.resize(DecompressBuffer(buffer, output));
    output.shrink_to_fit();
    return output;
  }

  ssize_t DecompressRA(const BufferView& input, const BufferView& output, size_t offset, size_t size) {
    const Header& header = *reinterpret_cast<const Header*>(input.data);
    if (!header.Valid())
      throw Exception(ErrorCode::HeaderInvalid);

    if (offset + size >= header.inputSize)
      throw Exception(ErrorCode::OutOfBoundsAccess);

    auto outputSize = size - (size % header.frameSize) + header.frameSize;
    if (output.size < outputSize || output.data == nullptr)
      return -outputSize;

    auto frame = std::lldiv(offset, header.frameSize);
    auto frameEntry = reinterpret_cast<const Entry*>(input.data + sizeof(Header)) + frame.quot;

    volatile auto compressedOffset = input.data + header.Size();

    ZDCtx ctx;
    ZSTD_DCtx_setFormat(ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);

    size_t outputOffset{};
    while (outputOffset < size) {
      auto read = ZStdCheck(ZSTD_decompressDCtx(ctx, output.data + outputOffset, output.size - outputOffset, compressedOffset + frameEntry->offset, frameEntry->size));

      if (!outputOffset && frame.rem) {
        auto minSize = std::min(size, static_cast<size_t>(header.frameSize) - frame.rem);
        memcpy(output.data, output.data + frame.rem, minSize);
        read = minSize;
      }

      outputOffset += read;
      frameEntry++;
    }

    return size;
  }

  Buffer DecompressRA(const BufferView& buffer, size_t offset, size_t size) {
    Buffer output(-DecompressRA(buffer, {}, offset, size));
    output.resize(DecompressRA(buffer, output, offset, size));
    output.shrink_to_fit();
    return output;
  }

  Compressor::Compressor(size_t size, i8 compressionLevel, size_t frameSize) : ctx(std::make_shared<ZCCtx>()), frameSize(frameSize), tableSize(static_cast<u32>((size / frameSize) + ((size % frameSize) ? 1 : 0))), header(sizeof(Header) + (sizeof(Entry) * tableSize)), entryOffset(sizeof(Header)) {
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_compressionLevel, compressionLevel);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_contentSizeFlag, false);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_dictIDFlag, false);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_format, ZSTD_format_e::ZSTD_f_zstd1_magicless);
    ZSTD_CCtx_setParameter(*ctx, ZSTD_cParameter::ZSTD_c_srcSizeHint, frameSize);

    *reinterpret_cast<Header*>(header.data()) = {
        .inputSize = size,
        .tableSize = tableSize,
        .frameSize = frameSize,
    };
  }

  ssize_t Compressor::Compress(const BufferView& input, const BufferView& output) {
    auto outputSize = ZSTD_compressBound(frameSize) * ((input.size / frameSize) + ((input.size % frameSize) ? 1 : 0));
    if (output.size < outputSize || output.data == nullptr)
      return -outputSize;

    auto entryIndex = (entryOffset - sizeof(Header)) / sizeof(Entry);
    if (input.size % frameSize && (entryIndex + (input.size / frameSize) + 1) < tableSize)
      throw Exception(ErrorCode::InputFrameSizeMismatch);

    size_t compressedSize{};
    size_t remaining = input.size;
    while (remaining) {
      frameSize = std::min(static_cast<u64>(frameSize), remaining);

      auto frameCompressedSize = ZStdCheck(ZSTD_compress2(*ctx, output.data + compressedSize, output.size - compressedSize, input.data + (input.size - remaining), frameSize));

      reinterpret_cast<Entry*>(header.data() + entryOffset)->offset = outputOffset;
      reinterpret_cast<Entry*>(header.data() + entryOffset)->size = frameCompressedSize;
      entryOffset += sizeof(Entry);
      entryIndex++;

      compressedSize += frameCompressedSize;
      remaining -= frameSize;
    }

    outputOffset += compressedSize;
    return compressedSize;
  }

  void Compressor::Compress(const BufferView& input, Buffer& output) {
    ssize_t size{};
    while (size <= 0) {
      size = Compress(input, BufferView(output));

      if (size < 0)
        output.resize(-size);
    }

    output.resize(size);
  }

  Decompressor::Decompressor(const Buffer& header, const std::function<void(size_t, size_t, BufferView)>& readFunction, size_t maxCacheSize) : ctx(std::make_shared<ZDCtx>()), header(header), readFunction(readFunction), maxCacheSize(maxCacheSize) {
    auto _header = reinterpret_cast<Header*>(this->header.data());
    if (this->header.size() < sizeof(Header) || !_header->Valid() || _header->Size() != this->header.size())
      throw Exception(ErrorCode::HeaderInvalid);

    frameSize = reinterpret_cast<Header*>(this->header.data())->frameSize;
    inputSize = reinterpret_cast<Header*>(this->header.data())->inputSize;

    ZSTD_DCtx_setFormat(*ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);
  }

  ssize_t Decompressor::Decompress(size_t offset, size_t size, const BufferView& output) {
    if (offset + size > inputSize)
      throw Exception(ErrorCode::OutOfBoundsAccess);

    auto outputSize = size - (size % frameSize) + frameSize;
    if (output.size < outputSize || output.data == nullptr)
      return -outputSize;

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
    while (outputOffset < size) {
      auto read = ZStdCheck(ZSTD_decompressDCtx(*ctx, output.data + outputOffset, output.size - outputOffset, input.data() + (frameEntry->offset - initialOffset), frameEntry->size));

      if (!outputOffset && frame.rem) {
        auto minSize = std::min(size, static_cast<size_t>(frameSize) - frame.rem);
        memcpy(output.data, output.data + frame.rem, minSize);
        read = minSize;
      }

      outputOffset += read;
      frameEntry++;
    }

    return size;
  }

  void Decompressor::Decompress(size_t offset, size_t size, Buffer& output) {
    ssize_t bufferSize{};
    while (bufferSize <= 0) {
      bufferSize = Decompress(offset, size, BufferView(output));

      if (bufferSize < 0)
        output.resize(-bufferSize);
    }

    output.resize(bufferSize);
  }

  FullDecompressor::FullDecompressor(Buffer header) : ctx(std::make_shared<ZDCtx>()), header(std::move(header)), entryOffset(sizeof(Header)), frameSize(reinterpret_cast<Header*>(this->header.data())->frameSize) {
    if (!reinterpret_cast<Header*>(this->header.data())->Valid())
      throw Exception(ErrorCode::HeaderInvalid);

    ZSTD_DCtx_setFormat(*ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);
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

uint16_t ZraGetVersion() {
  return zra::GetVersion();
}

const char* ZraGetErrorString(ZraErrorCode code) {
  return zra::Exception::GetExceptionString(static_cast<zra::ErrorCode>(code)).data();
}

ssize_t ZraCompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity, int8_t compressionLevel, uint64_t frameSize) {
  try {
    auto result = zra::CompressBuffer(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), outputCapacity), compressionLevel, frameSize);
    return (result >= 0) ? result : (outputBuffer) ? ZraErrorCode::OutputBufferTooSmall : -result;
  } catch (const zra::Exception& e) {
    return static_cast<ssize_t>(e.code);
  }
}

ssize_t ZraDecompressBuffer(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity) {
  try {
    auto result = zra::DecompressBuffer(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), outputCapacity));
    return (result >= 0) ? result : (outputBuffer) ? ZraErrorCode::OutputBufferTooSmall : -result;
  } catch (const zra::Exception& e) {
    return static_cast<ssize_t>(e.code);
  }
}

ssize_t ZraDecompressRA(void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity, size_t offset, size_t size) {
  try {
    auto result = zra::DecompressRA(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), outputCapacity), offset, size);
    return (result >= 0) ? result : (outputBuffer) ? ZraErrorCode::OutputBufferTooSmall : -result;
  } catch (const zra::Exception& e) {
    return static_cast<ssize_t>(e.code);
  }
}

ZraErrorCode ZraCreateCompressor(ZraCompressor** compressor, size_t size, int8_t compressionLevel, uint64_t frameSize) {
  try {
    *compressor = reinterpret_cast<ZraCompressor*>(new zra::Compressor(size, compressionLevel, frameSize));
    return ZraErrorCode::Success;
  } catch (const zra::Exception& e) {
    return static_cast<ZraErrorCode>(e.code);
  }
}

void ZraDeleteCompressor(ZraCompressor* compressor) {
  delete reinterpret_cast<zra::Compressor*>(compressor);
}

ssize_t ZraCompressWithCompressor(ZraCompressor* compressor, void* inputBuffer, size_t inputSize, void* outputBuffer, size_t outputCapacity) {
  try {
    auto result = reinterpret_cast<zra::Compressor*>(compressor)->Compress(zra::BufferView(reinterpret_cast<zra::u8*>(inputBuffer), inputSize), zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), outputCapacity));
    return (result >= 0) ? result : (outputBuffer) ? ZraErrorCode::OutputBufferTooSmall : -result;
  } catch (const zra::Exception& e) {
    return static_cast<ssize_t>(e.code);
  }
}

ssize_t ZraGetHeaderWithCompressor(ZraCompressor* _compressor, void* outputBuffer, size_t outputCapacity) {
  auto compressor = reinterpret_cast<zra::Compressor*>(_compressor);

  if (outputBuffer == nullptr)
    return compressor->header.size();
  if (outputCapacity < compressor->header.size())
    return ZraErrorCode::OutputBufferTooSmall;

  memcpy(outputBuffer, compressor->header.data(), compressor->header.size());
  return compressor->header.size();
}

ssize_t ZraGetHeaderSize(void* _header, size_t headerSize) {
  auto header = reinterpret_cast<zra::Header*>(_header);

  if (header == nullptr)
    return sizeof(*header);
  if (headerSize < sizeof(*header))
    return ZraErrorCode::OutputBufferTooSmall;

  if (!header->Valid())
    return ZraErrorCode::HeaderInvalid;

  return header->Size();
}

ssize_t ZraGetUncompressedSize(void* _header, size_t headerSize) {
  auto header = reinterpret_cast<zra::Header*>(_header);

  if (header == nullptr)
    return sizeof(*header);
  if (headerSize < sizeof(*header))
    return ZraErrorCode::OutputBufferTooSmall;

  if (!header->Valid())
    return ZraErrorCode::HeaderInvalid;

  return header->inputSize;
}

ZraErrorCode ZraCreateDecompressor(ZraDecompressor** decompressor, void* header, size_t headerSize, void (*readFunction)(size_t, size_t, void*), size_t maxCacheSize) {
  try {
    *decompressor = reinterpret_cast<ZraDecompressor*>(new zra::Decompressor(
        zra::Buffer(reinterpret_cast<zra::u8*>(header), reinterpret_cast<zra::u8*>(header) + headerSize), [readFunction](size_t off, size_t size, zra::BufferView buffer) { readFunction(off, size, buffer.data); }, maxCacheSize));
    return ZraErrorCode::Success;
  } catch (const zra::Exception& e) {
    return static_cast<ZraErrorCode>(e.code);
  }
}

void ZraDeleteDecompressor(ZraDecompressor* decompressor) {
  delete reinterpret_cast<zra::Decompressor*>(decompressor);
}

ssize_t ZraDecompressWithDecompressor(ZraDecompressor* decompressor, size_t offset, size_t size, void* outputBuffer, size_t outputCapacity) {
  try {
    auto result = reinterpret_cast<zra::Decompressor*>(decompressor)->Decompress(offset, size, zra::BufferView(reinterpret_cast<zra::u8*>(outputBuffer), outputCapacity));
    return (result >= 0) ? result : (outputBuffer) ? ZraErrorCode::OutputBufferTooSmall : -result;
  } catch (const zra::Exception& e) {
    return static_cast<ssize_t>(e.code);
  }
}