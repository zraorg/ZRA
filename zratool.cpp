#include <chrono>
#include <fstream>
#include <iostream>

#include "include/zra.hpp"

void compressionBenchmark(const std::function<zra::Buffer()>& compressFunction, const std::function<zra::Buffer(const zra::BufferView&)>& decompressFunction, const std::string& name, size_t originalSize) {
  auto start = std::chrono::high_resolution_clock::now();
  auto compressed = compressFunction();
  auto end = std::chrono::high_resolution_clock::now();
  auto compressTime = end - start;

  start = std::chrono::high_resolution_clock::now();
  decompressFunction(compressed);
  end = std::chrono::high_resolution_clock::now();
  auto decompressTime = end - start;

  constexpr auto bytesInMb = 1024 * 1024;

  std::string speedCompressing = (std::chrono::duration_cast<std::chrono::milliseconds>(compressTime).count() != 0) ? std::to_string(originalSize / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::milliseconds>(compressTime) * bytesInMb).count()) : "Inf";
  std::string speedDecompressing = (std::chrono::duration_cast<std::chrono::milliseconds>(decompressTime).count() != 0) ? std::to_string(originalSize / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::milliseconds>(decompressTime) * bytesInMb).count()) : "Inf";

  std::cout << std::dec << name + " Compression Summary:\n"
            << "Size:\n* Compressed: " << (compressed.size() / bytesInMb) << " MB (" << compressed.size() << " bytes)\n* Uncompressed: " << (originalSize / bytesInMb) << " MB (" << originalSize << " bytes)\n"
            << "Time:\n* Compressing: " << std::chrono::duration_cast<std::chrono::milliseconds>(compressTime).count() << " ms\n* Decompressing: " << std::chrono::duration_cast<std::chrono::milliseconds>(decompressTime).count() << " ms\n"
            << "Speed:\n* Compressing: " << speedCompressing << " MB/s\n* Decompressing: " << speedDecompressing << " MB/s"
            << std::endl;
}

template <typename raType>
void randomAccessBenchmark(raType raFunction, size_t offset, size_t size, const std::string& name) {
  auto start = std::chrono::high_resolution_clock::now();
  raFunction(offset, size);
  auto end = std::chrono::high_resolution_clock::now();
  auto accessTime = end - start;

  constexpr auto bytesInMb = 1024 * 1024;

  std::string speed = (std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime).count() != 0) ? std::to_string(size / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime) * bytesInMb).count()) : "Inf";

  std::cout << std::dec << name + " RA Summary:\n"
            << "Offset: " << offset << ", Size: " << size << " bytes\n"
            << "Time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime).count() << " ns\n"
            << "Speed: " << speed << " MB/s"
            << std::endl;
}

zra::Buffer ReadFile(const std::string& name) {
  zra::Buffer input;
  std::ifstream iStream(name, std::ios::ate | std::ios::binary | std::ios::in);
  iStream.unsetf(std::ios::skipws);

  ssize_t size = iStream.tellg();
  iStream.seekg(0);

  if (size <= 0)
    throw std::runtime_error("The specified file doesn't exist or is empty");

  input.resize(size);
  iStream.read(reinterpret_cast<char*>(input.data()), input.size());

  return input;
}

struct IFile {
  std::ifstream stream;
  ssize_t size;
};

IFile GetIFile(const std::string& name) {
  std::ifstream iStream(name, std::ios::ate | std::ios::binary | std::ios::in);
  iStream.unsetf(std::ios::skipws);

  ssize_t size = iStream.tellg();
  iStream.seekg(0);

  if (size <= 0)
    throw std::runtime_error("The specified file doesn't exist or is empty");

  return IFile{std::move(iStream), size};
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cout << argv[0] << "{mode} {file}" << std::endl;
    std::exit(0);
  }

  std::string fileName{};
  size_t fileSize{};

  std::string_view type(argv[1]);
  if (type == "c") {
    int compressionLevel{0};
    if (argc > 3)
      compressionLevel = std::atoi(argv[3]);
    zra::u16 frameSize = 16384;
    if (argc > 4)
      frameSize = std::atoi(argv[4]);

    zra::Buffer input = ReadFile(argv[2]);
    zra::Buffer output = zra::CompressBuffer(input, compressionLevel, frameSize);

    fileName = std::string(argv[2]) + ".rac";
    fileSize = output.size();

    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);
    oStream.write(reinterpret_cast<char*>(output.data()), output.size());
  } else if (type == "sc") {
    int compressionLevel{0};
    if (argc > 3)
      compressionLevel = std::atoi(argv[3]);
    zra::u16 frameSize = 16384;
    if (argc > 4)
      frameSize = std::atoi(argv[4]);

    auto iFile = GetIFile(argv[2]);

    fileName = std::string(argv[2]) + ".sc";
    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);

    zra::Compressor compressor(iFile.size, compressionLevel, frameSize);

    size_t bufferSize = 10'000'000;
    if (argc > 5)
      bufferSize = std::atoi(argv[5]) * 1'000'000;

    zra::Buffer input(bufferSize - (bufferSize % frameSize) + frameSize);

    std::cout << std::dec << "0% (0/" << iFile.size << " bytes)" << std::flush;

    oStream.seekp(compressor.header.size());

    zra::Buffer data;
    size_t offset{}, compressedSize{};
    while (offset < iFile.size) {
      auto read = iFile.stream.read(reinterpret_cast<char*>(input.data()), input.size()).gcount();
      input.resize(read);
      offset += read;

      compressor.Compress(input, data);
      compressedSize += data.size();

      oStream.write(reinterpret_cast<char*>(data.data()), data.size());

      std::cout << "\r" << std::dec << (offset * 100) / iFile.size << "% (" << offset << "/" << iFile.size << " bytes)" << std::flush;
    }

    oStream.seekp(0);
    oStream.write(reinterpret_cast<char*>(compressor.header.data()), compressor.header.size());

    fileSize = compressedSize;

    std::cout << std::endl;
  } else if (type == "d") {
    zra::Buffer input = ReadFile(argv[2]);
    zra::Buffer output = zra::DecompressBuffer(input);

    fileName = std::string(argv[2]) + ".rad";
    fileSize = output.size();

    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);
    oStream.write(reinterpret_cast<char*>(output.data()), output.size());
  } else if (type == "sd") {
    auto iFile = GetIFile(argv[2]);

    zra::Buffer header(sizeof(zra::Header));
    iFile.stream.read(reinterpret_cast<char*>(header.data()), header.size());
    header.resize(reinterpret_cast<zra::Header*>(header.data())->Size());
    iFile.stream.read(reinterpret_cast<char*>(header.data() + sizeof(zra::Header)), header.size() - sizeof(zra::Header));
    auto size = iFile.size - header.size();

    zra::FullDecompressor decompressor(header);

    fileName = std::string(argv[2]) + ".sd";
    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);

    size_t bufferSize = 10'000'000;
    if (argc > 3)
      bufferSize = std::atoi(argv[3]) * 1'000'000;

    zra::Buffer input(bufferSize);

    std::cout << std::dec << "0% (0/" << size << " bytes)" << std::flush;

    zra::Buffer data;
    size_t offset{}, uncompressedSize{};
    while (offset < size) {
      auto read = iFile.stream.read(reinterpret_cast<char*>(input.data()), input.size()).gcount();
      input.resize(read);
      offset += read;

      decompressor.Decompress(input, data);
      uncompressedSize += data.size();

      oStream.write(reinterpret_cast<char*>(data.data()), data.size());

      std::cout << "\r" << std::dec << (offset * 100) / size << "% (" << offset << "/" << size << " bytes)" << std::flush;
    }

    fileSize = uncompressedSize;

    std::cout << std::endl;
  } else if (type == "b") {
    int compressionLevel{0};
    if (argc > 3)
      compressionLevel = std::atoi(argv[3]);
    zra::u16 frameSize = 16384;
    if (argc > 4)
      frameSize = std::atoi(argv[4]);
    size_t bufferSize = 10'000'000;
    if (argc > 5)
      bufferSize = std::atoi(argv[5]) * 1'000'000;

    zra::Buffer input = ReadFile(argv[2]);

    compressionBenchmark([input, compressionLevel, frameSize] { return zra::CompressBuffer(input, compressionLevel, frameSize); }, [](const zra::BufferView& view) { return zra::DecompressBuffer(view); }, "In-Memory", input.size());
    compressionBenchmark([compressionLevel, frameSize, bufferSize, argv] {
      auto iFile = GetIFile(argv[2]);

      zra::Compressor compressor(iFile.size, compressionLevel, frameSize);

      zra::Buffer input(bufferSize - (bufferSize % frameSize) + frameSize);

      zra::Buffer data, output(compressor.header.size());
      size_t offset{};
      while (offset < iFile.size) {
        auto read = iFile.stream.read(reinterpret_cast<char*>(input.data()), input.size()).gcount();
        input.resize(read);
        offset += read;

        compressor.Compress(input, data);
        output.insert(output.end(), data.begin(), data.end());
      }

      memcpy(output.data(), compressor.header.data(), compressor.header.size());

      return output; }, [bufferSize, argv](const zra::BufferView& view) {

      zra::Buffer header(view.data, view.data + reinterpret_cast<zra::Header*>(view.data)->Size());
      auto size = view.size - header.size();

      zra::FullDecompressor decompressor(header);

      zra::Buffer input(bufferSize);

      zra::Buffer data, output;
      size_t offset{header.size()};
      while (offset < size) {
        auto read = std::min(view.size - offset, input.size());
        memcpy(input.data(), view.data + offset, read);
        input.resize(read);
        offset += read;

        decompressor.Decompress(input, data);
        output.insert(output.end(), data.begin(), data.end());
      }

      return output; }, "Streaming", input.size());

    size_t offset{0x1000};
    size_t raSize{0x10000};

    if (argc > 6)
      offset = std::atoi(argv[4]);

    if (argc > 7)
      raSize = std::atoi(argv[5]);

    auto buffer = zra::CompressBuffer(input, compressionLevel);
    randomAccessBenchmark([buffer](zra::u64 offset, size_t size) { return zra::DecompressRA(buffer, offset, size); }, offset, raSize, "In-Memory");

    zra::Buffer header(buffer.data(), buffer.data() + reinterpret_cast<zra::Header*>(buffer.data())->Size());
    zra::Decompressor raDecompressor(header, [&buffer, &header](size_t off, size_t sz, zra::BufferView view) {
      memcpy(view.data, buffer.data() + header.size() + off, sz);
    });
    randomAccessBenchmark([&raDecompressor](zra::u64 offset, size_t size) { zra::Buffer output; return raDecompressor.Decompress(offset, size, output); }, offset, raSize, "Streaming");
  }

  if (fileSize || !fileName.empty())
    std::cout << "Output Size (" << fileName << "): " << std::to_string(fileSize) << " bytes" << std::endl;
}