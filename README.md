<p align="center"><b>ZStandard Random Access</b> (<code>ZRA</code>) allows random access inside an archive compressed using <a href="https://github.com/facebook/zstd">ZStandard</a><br>
<img align="center" alt="C/C++ CI" src="https://github.com/PixelyIon/ZRA/workflows/C/C++%20CI/badge.svg"/>
</p>

***
### Format
#### How is this done?
ZSTD has the concept of a [Frame](https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#frames) which can be decompressed independently from the rest of the file. A ZSTD archive is made of multiple concatenated frames which are decompressed one after another.  

We exploit that fact to break the file into uniformly sized frames (`Frame Size`) and creating a seek-table which contains the offset of each frame within in the file which can be indexed by simply dividing the offset by the frame size.  

#### Header
We store data that's required for decompression or other functionality inside an archive header, that contains the following:  
* **ZSTD Skippable Frame** - The entire header is inside a ZSTD Skippable Frame so that ZRA is fully compatible with any regular ZSTD decompressor
* **CRC-32 Hash** - A CRC-32 hash of the entire header to ensure integrity of the file is always preserved 
* **Metadata Section** - A section where data which might be used by a ZRA decompressor on the other side but not a part of the archive's contents itself
* **Seek-Table** - A table with 40-bit entries containing the offset of individual frames
***
### Usage
#### Compression
* In-Memory
  ```cpp
  zra::Buffer input = GetInput(); // A `zra::Buffer` full of data to be compressed
  zra::Buffer output = zra::CompressBuffer(input);
  ```
* Streaming
  ```cpp
  auto size = input.size();
  zra::Compressor compressor(size);
  output.seek(compressor.GetHeaderSize());

  zra::Buffer buffer; // The buffer is reused to prevent constant reallocation
  while (size) {
      auto readSize = std::min(maxChunkSize, size);
      compressor.Compress(input.read(readSize), buffer);
      output.write(buffer);
      size -= readSize;
  }
  
  output.seek(0);
  output.write(compressor.GetHeader());
  
  // Note: `input` and `output` in the example hold an internal offset that is automatically 
  // modified based on operations performed on them, similar to ifstream/ofstream from C++ 
  // Standard Library
  ```
#### Decompression (Entire File)
* In-Memory
  ```cpp
    zra::Buffer input = GetInput(); // A `zra::Buffer` with the entire archive
    zra::Buffer output = zra::DecompressBuffer(input);
  ```
* Streaming
  ```cpp
  zra::FullDecompressor decompressor([&input](size_t offset, size_t size, void* output) {
    input.seek(offset);
    input.read(output, size);
  });

  auto remaining = decompressor.header.uncompressedSize;
  zra::Buffer buffer(bufferSize); // The buffer is reused to prevent constant reallocation
  while (remaining) {
      auto amount = decompressor.Decompress(buffer);
      output.write(buffer, amount);
      remaining -= amount;
  }
  ```
#### Decompression (Random-Access)
* In-Memory
  ```cpp
  zra::Buffer input = GetInput();
  zra::Buffer output = zra::DecompressRA(input, offset, size);
  ```
* Streaming
  ```cpp
  zra::Decompressor decompressor([&input](size_t offset, size_t size, void* output) {
    input.seek(offset);
    input.read(output, size);
  });
  zra::Buffer output = decompressor.Decompress(offset, size);
  // or, to prevent buffer reallocation
  decompressor.Decompress(offset, size, output);
  ```
#### Retrieving Header
* Using `readFunction`
  ```cpp
  zra::Header header([&input](size_t offset, size_t size, void* output) {
    input.seek(offset);
    input.read(output, size);
  });
  ```
* Using a pointer to the archive
  ```cpp
  zra::Header header(input.data());
  ```
* From `Decompressor`/`FullDecompressor`
  ```cpp
  zra::Decompressor decompressor(...); // or zra::FullDecompressor
  decompressor.header;
  ```
***
### License
We use a simple **3-clause BSD** license located at [LICENSE](LICENSE.md) for easy integration into projects while being compatible with the libraries we utilize