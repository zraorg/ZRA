#define ZSTD_STATIC_LINKING_ONLY 1

#include "zra.hpp"

#include <zstd.h>

#include <optional>
#include <stdexcept>
#include <utility>

namespace zra {
  template <typename Type>
  constexpr Type zStdCheck(Type object) {
    if (ZSTD_isError(object)) throw std::runtime_error(ZSTD_getErrorName(object));
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

  Buffer CompressBuffer(const Buffer& input, i8 compressionLevel, u16 frameSize) {
    auto inputSize = input.size();
    u32 tableSize = (inputSize / frameSize) + ((inputSize % frameSize) ? 1 : 0);

    Buffer output(sizeof(Header) + (tableSize * sizeof(Entry)) + (ZSTD_compressBound(frameSize) * tableSize));

    size_t outputOffset{};
    *reinterpret_cast<Header*>(output.data()) = {
        .inputSize = inputSize,
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

    auto remaining = inputSize;
    while (remaining) {
      frameSize = std::min(static_cast<u64>(frameSize), remaining);

      auto compressedSize = zStdCheck(ZSTD_compress2(ctx, output.data() + outputOffset, output.size() - outputOffset, input.data() + (inputSize - remaining), frameSize));

      reinterpret_cast<Entry*>(output.data() + tableOffset)->offset = outputOffset - compressedOffset;
      reinterpret_cast<Entry*>(output.data() + tableOffset)->size = compressedSize;
      tableOffset += sizeof(Entry);

      outputOffset += compressedSize;
      remaining -= frameSize;
    }

    output.resize(outputOffset);
    output.shrink_to_fit();

    return output;
  }

  Buffer DecompressBuffer(const Buffer& input) {
    size_t inputOffset{};
    const Header& header = *reinterpret_cast<const Header*>(input.data());
    inputOffset += sizeof(Header);

    inputOffset += header.tableSize * sizeof(Entry);

    ZDCtx ctx;
    ZSTD_DCtx_setFormat(ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);

    Buffer output(header.inputSize);
    zStdCheck(ZSTD_decompressDCtx(ctx, output.data(), output.size(), input.data() + inputOffset, input.size() - inputOffset));

    return output;
  }

  Buffer DecompressRA(const Buffer& input, u64 offset, size_t size) {
    const Header& header = *reinterpret_cast<const Header*>(input.data());

    if (offset + size >= header.inputSize)
      throw std::invalid_argument("The specified offset and size are past the data contained within the buffer");

    auto frame = std::lldiv(offset, header.frameSize);
    auto frameEntry = reinterpret_cast<const Entry*>(input.data() + sizeof(Header)) + frame.quot;

    auto compressedOffset = input.data() + header.Size();

    ZDCtx ctx;
    ZSTD_DCtx_setFormat(ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);

    Buffer output(size - (size % header.frameSize) + header.frameSize);

    size_t outputOffset{};
    while (outputOffset < size) {
      auto read = zStdCheck(ZSTD_decompressDCtx(ctx, output.data() + outputOffset, output.size() - outputOffset, compressedOffset + frameEntry->offset, frameEntry->size));

      if (!outputOffset && frame.rem) {
        auto minSize = std::min(size, static_cast<size_t>(header.frameSize) - frame.rem);
        memcpy(output.data(), output.data() + frame.rem, minSize);
        read = minSize;
      }

      outputOffset += read;
      frameEntry++;
    }

    output.resize(size);
    output.shrink_to_fit();

    return output;
  }

  Compressor::Compressor(size_t size, i8 compressionLevel, u16 frameSize) : ctx(std::make_shared<ZCCtx>()), frameSize(frameSize), tableSize(static_cast<u32>((size / frameSize) + ((size % frameSize) ? 1 : 0))), header(sizeof(Header) + (sizeof(Entry) * tableSize)), entryOffset(sizeof(Header)) {
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

  void Compressor::Compress(const Buffer& input, Buffer& output) {
    auto inputSize = input.size();
    auto entryIndex = (entryOffset - sizeof(Header)) / sizeof(Entry);
    if (inputSize % frameSize && (entryIndex + (inputSize / frameSize) + 1) < tableSize)
      throw std::runtime_error("Input size is not divisble by the frame size and isn't the final frame");

    output.resize(ZSTD_compressBound(frameSize) * ((inputSize / frameSize) + ((inputSize % frameSize) ? 1 : 0)));

    size_t outputOffset{};
    size_t remaining = inputSize;
    while (remaining) {
      frameSize = std::min(static_cast<u64>(frameSize), remaining);

      auto compressedSize = zStdCheck(ZSTD_compress2(*ctx, output.data() + outputOffset, output.size() - outputOffset, input.data() + (inputSize - remaining), frameSize));

      reinterpret_cast<Entry*>(header.data() + entryOffset)->offset = outputOffset;
      reinterpret_cast<Entry*>(header.data() + entryOffset)->size = compressedSize;
      entryOffset += sizeof(Entry);
      entryIndex++;

      outputOffset += compressedSize;
      remaining -= frameSize;
    }

    output.resize(outputOffset);
  }

  Decompressor::Decompressor(Buffer header) : ctx(std::make_shared<ZDCtx>()), header(std::move(header)), entryOffset(sizeof(Header)), frameSize(reinterpret_cast<Header*>(this->header.data())->frameSize) {
    ZSTD_DCtx_setFormat(*ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);
  }

  void Decompressor::Decompress(Buffer& input, Buffer& output) {
    auto entry = reinterpret_cast<Entry*>(header.data() + entryOffset);

    size_t outputOffset{};
    if (!frame.empty()) {
      auto endIterator = input.begin() + std::min(static_cast<size_t>(entry->size - frame.size()), input.size());
      std::move(input.begin(), endIterator, std::back_inserter(frame));
      input.erase(input.begin(), endIterator);

      if (frame.size() == entry->size) {
        output.resize(frameSize);
        output.resize(zStdCheck(ZSTD_decompressDCtx(*ctx, output.data(), output.size(), frame.data(), frame.size())));
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
    output.resize(outputOffset + zStdCheck(ZSTD_decompressDCtx(*ctx, output.data() + outputOffset, output.size() - outputOffset, input.data(), size)));
  }

  RADecompressor::RADecompressor(Buffer header, std::function<void(size_t, size_t, Buffer&)> readFunction, size_t maxCacheSize) : header(std::move(header)), readFunction(std::move(readFunction)), frameSize(reinterpret_cast<Header*>(this->header.data())->frameSize), inputSize(reinterpret_cast<Header*>(this->header.data())->inputSize), maxCacheSize(maxCacheSize) {
    ZSTD_DCtx_setFormat(*ctx, ZSTD_format_e::ZSTD_f_zstd1_magicless);
  }

  void RADecompressor::Decompress(size_t offset, size_t size, Buffer& output) {
    if (offset + size >= inputSize)
      throw std::invalid_argument("The specified offset and size are past the data contained within the buffer");

    auto frame = std::lldiv(offset, frameSize);
    auto frameEntry = reinterpret_cast<const Entry*>(header.data() + sizeof(Header)) + frame.quot;
    auto initialOffset = frameEntry->offset;

    output.resize(size - (size % frameSize) + frameSize);

    size_t compressedSize{}, readSize{frame.rem + size};
    auto lastFrame = frameEntry + ((readSize / frameSize) + ((readSize % frameSize) ? 1 : 0));
    for (auto currentFrame = frameEntry; currentFrame != lastFrame; currentFrame++)
      compressedSize += currentFrame->size;

    std::optional<Buffer> inputBuffer;
    if (compressedSize > maxCacheSize)
      inputBuffer.emplace(compressedSize);

    auto& input = inputBuffer ? *inputBuffer : cache;

    input.resize(compressedSize);

    readFunction(frameEntry->offset, compressedSize, input);

    size_t outputOffset{};
    while (outputOffset < size) {
      auto read = zStdCheck(ZSTD_decompressDCtx(*ctx, output.data() + outputOffset, output.size() - outputOffset, input.data() + (frameEntry->offset - initialOffset), frameEntry->size));

      if (!outputOffset && frame.rem) {
        auto minSize = std::min(size, static_cast<size_t>(frameSize) - frame.rem);
        memcpy(output.data(), output.data() + frame.rem, minSize);
        read = minSize;
      }

      outputOffset += read;
      frameEntry++;
    }

    output.resize(size);
  }
}  // namespace zra
