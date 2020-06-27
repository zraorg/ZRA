#include <chrono>
#include <fstream>
#include <iostream>

#include "zra.hpp"

template <typename compressType, typename decompressType>
void compressionBenchmark(compressType compressFunction, decompressType decompressFunction, const std::string& name, size_t originalSize) {
  auto start = std::chrono::high_resolution_clock::now();
  auto compressed = compressFunction();
  auto end = std::chrono::high_resolution_clock::now();
  auto compressTime = end - start;

  start = std::chrono::high_resolution_clock::now();
  decompressFunction(compressed);
  end = std::chrono::high_resolution_clock::now();
  auto decompressTime = end - start;

  constexpr auto bytesInMb = 1024 * 1024;

  std::cout << std::dec << name + " Compression Summary:\n"
            << "Size:\n* Compressed: " << (compressed.size() / bytesInMb) << " MB (" << compressed.size() << " bytes)\n* Uncompressed: " << (originalSize / bytesInMb) << " MB (" << originalSize << " bytes)\n"
            << "Time:\n* Compressing: " << std::chrono::duration_cast<std::chrono::milliseconds>(compressTime).count() << " ms\n* Decompressing: " << std::chrono::duration_cast<std::chrono::milliseconds>(decompressTime).count() << " ms\n"
            << "Speed:\n* Compressing: " << (originalSize / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::milliseconds>(compressTime) * bytesInMb).count()) << " MB/s\n* Decompressing: " << (originalSize / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::milliseconds>(decompressTime) * bytesInMb).count()) << " MB/s"
            << std::endl;
}

template <typename raType>
void randomAccessBenchmark(raType raFunction, size_t offset, size_t size) {
  auto start = std::chrono::high_resolution_clock::now();
  raFunction(offset, size);
  auto end = std::chrono::high_resolution_clock::now();
  auto accessTime = end - start;

  constexpr auto bytesInMb = 1024 * 1024;

  std::string speed = (std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime).count() != 0) ? std::to_string(size / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime) * bytesInMb).count()) : "Inf";

  std::cout << std::dec << "RA Benchmark:\n"
            << "Offset: " << offset << ", Size: " << size << " bytes\n"
            << "Time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(accessTime).count() << " ns\n"
            << "Speed: " << speed << " MB/s"
            << std::endl;
}

zra::Buffer ReadFile(const std::string& name) {
  zra::Buffer input;
  std::ifstream iStream(name, std::ios::ate | std::ios::binary | std::ios::in);
  iStream.unsetf(std::ios::skipws);

  auto size = iStream.tellg();
  iStream.seekg(0);

  if (size == 0)
    throw std::runtime_error("The specified file is empty");

  input.resize(size);
  iStream.read(reinterpret_cast<char*>(input.data()), input.size());

  return input;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cout << argv[0] << "{mode} {file}" << std::endl;
    std::exit(0);
  }

  int compressionLevel{0};

  if (argc > 3)
    compressionLevel = std::atoi(argv[3]);

  std::string fileName{};
  size_t fileSize{};

  std::string_view type(argv[1]);
  if (type == "c") {
    zra::Buffer input = ReadFile(argv[2]);
    zra::Buffer output = zra::compressNonRaBuffer(input, compressionLevel);

    fileName = std::string(argv[2]) + ".c";
    fileSize = output.size();

    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);
    oStream.write(reinterpret_cast<char*>(output.data()), output.size());
  } else if (type == "rac") {
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
    zra::u16 frameSize = 16384;

    if (argc > 4)
      frameSize = std::atoi(argv[4]);

    std::ifstream iStream(argv[2], std::ios::ate | std::ios::binary | std::ios::in);
    iStream.unsetf(std::ios::skipws);

    size_t size = iStream.tellg();
    if (size == 0)
      throw std::runtime_error("The specified file is empty");

    iStream.seekg(0);

    fileName = std::string(argv[2]) + ".sc";
    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);

    zra::Compressor compressor(size, compressionLevel, frameSize);

    size_t bufferSize = 10'000'000;
    if (argc > 5)
      bufferSize = std::atoi(argv[5]) * 1'000'000;

    zra::Buffer input(bufferSize - (bufferSize % frameSize) + frameSize);

    std::cout << std::dec << "0% (0/" << size << " bytes)" << std::flush;

    oStream.seekp(compressor.header.size());

    zra::Buffer data;
    size_t offset{}, compressedSize{};
    while (offset < size) {
      auto read = iStream.read(reinterpret_cast<char*>(input.data()), input.size()).gcount();
      input.resize(read);
      offset += read;

      compressor.Compress(input, data);
      compressedSize += data.size();

      oStream.write(reinterpret_cast<char*>(data.data()), data.size());

      std::cout << "\r" << std::dec << (offset * 100) / size << "% (" << offset << "/" << size << " bytes)" << std::flush;
    }

    oStream.seekp(0);
    oStream.write(reinterpret_cast<char*>(compressor.header.data()), compressor.header.size());

    fileSize = compressedSize;

    std::cout << std::endl;
  } else if (type == "d") {
    zra::Buffer input = ReadFile(argv[2]);
    zra::Buffer output = zra::decompressNonRaBuffer(input);

    fileName = std::string(argv[2]) + ".d";
    fileSize = output.size();

    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);
    oStream.write(reinterpret_cast<char*>(output.data()), output.size());
  } else if (type == "rad") {
    zra::Buffer input = ReadFile(argv[2]);
    zra::Buffer output = zra::DecompressBuffer(input);

    fileName = std::string(argv[2]) + ".rad";
    fileSize = output.size();

    std::ofstream oStream(fileName, std::ios::binary | std::ios::out | std::ios::trunc);
    oStream.write(reinterpret_cast<char*>(output.data()), output.size());
  } else if (type == "sd") {
    std::ifstream iStream(argv[2], std::ios::ate | std::ios::binary | std::ios::in);
    iStream.unsetf(std::ios::skipws);

    size_t size = iStream.tellg();
    if (size == 0)
      throw std::runtime_error("The specified file is empty");

    iStream.seekg(0);

    zra::Buffer header(sizeof(zra::Header));
    iStream.read(reinterpret_cast<char*>(header.data()), header.size());
    header.resize(reinterpret_cast<zra::Header*>(header.data())->Size());
    iStream.read(reinterpret_cast<char*>(header.data() + sizeof(zra::Header)), header.size() - sizeof(zra::Header));
    size -= header.size();

    zra::Decompressor decompressor(header);

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
      auto read = iStream.read(reinterpret_cast<char*>(input.data()), input.size()).gcount();
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
    zra::Buffer input = ReadFile(argv[2]);

    compressionBenchmark([input, compressionLevel] { return zra::compressNonRaBuffer(input, compressionLevel); }, zra::decompressNonRaBuffer, "Non-RA", input.size());
    compressionBenchmark([input, compressionLevel] { return zra::CompressBuffer(input, compressionLevel); }, zra::DecompressBuffer, "RA", input.size());

    size_t offset{0x1000};
    size_t raSize{0x100000};

    if (argc > 4)
      offset = std::atoi(argv[4]);

    if (argc > 5)
      raSize = std::atoi(argv[5]);

    auto buffer = zra::CompressBuffer(input, compressionLevel);
    randomAccessBenchmark([buffer](zra::u64 offset, size_t size) { return zra::DecompressRA(buffer, offset, size); }, offset, raSize);
  }

  if (fileSize || !fileName.empty())
    std::cout << "Output Size (" << fileName << "): " << std::to_string(fileSize) << " bytes" << std::endl;
}