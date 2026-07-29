// SPDX-License-Identifier: MIT

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

int failures{};

void expect(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void writeLe16(const std::span<std::byte> bytes, const std::size_t offset,
               const std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void writeLe32(const std::span<std::byte> bytes, const std::size_t offset,
               const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

std::uint32_t rockchipCrc32(const std::span<const std::byte> bytes)
{
    std::uint32_t crc = 0U;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint32_t>(byte) << 24U;
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x80000000U) != 0U
                ? (crc << 1U) ^ 0x04c10db7U
                : crc << 1U;
        }
    }
    return crc;
}

class TemporaryFile final {
  public:
    TemporaryFile(std::string name, const std::span<const std::byte> bytes)
        : path_(std::filesystem::temp_directory_path() / std::move(name))
    {
        std::ofstream stream{path_, std::ios::binary | std::ios::trunc};
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    TemporaryFile(std::string name, const std::string_view text)
        : path_(std::filesystem::temp_directory_path() / std::move(name))
    {
        std::ofstream stream{path_, std::ios::binary | std::ios::trunc};
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

std::vector<std::byte> makeSparseImage(const bool valid)
{
    constexpr std::size_t headerSize = 28U;
    constexpr std::size_t chunkHeaderSize = 12U;
    constexpr std::size_t blockSize = 4096U;
    std::vector<std::byte> bytes(headerSize + chunkHeaderSize + blockSize);
    writeLe32(bytes, 0U, 0xed26ff3aU);
    writeLe16(bytes, 4U, 1U);
    writeLe16(bytes, 6U, 0U);
    writeLe16(bytes, 8U, headerSize);
    writeLe16(bytes, 10U, chunkHeaderSize);
    writeLe32(bytes, 12U, blockSize);
    writeLe32(bytes, 16U, 1U);
    writeLe32(bytes, 20U, 1U);
    writeLe16(bytes, headerSize, 0xcac1U);
    writeLe32(bytes, headerSize + 4U, 1U);
    writeLe32(bytes, headerSize + 8U,
              valid ? chunkHeaderSize + blockSize : chunkHeaderSize + 1U);
    return bytes;
}

void writeLoaderRecord(const std::span<std::byte> bytes,
                       const std::size_t offset, const std::uint32_t type,
                       const std::string_view name,
                       const std::uint32_t dataOffset,
                       const std::uint32_t dataSize)
{
    bytes[offset] = std::byte{57};
    writeLe32(bytes, offset + 1U, type);
    for (std::size_t index = 0; index < name.size(); ++index) {
        bytes[offset + 5U + index * 2U] =
            static_cast<std::byte>(name[index]);
    }
    writeLe32(bytes, offset + 45U, dataOffset);
    writeLe32(bytes, offset + 49U, dataSize);
}

std::vector<std::byte> makeLoaderImage()
{
    constexpr std::size_t headerSize = 102U;
    constexpr std::size_t recordSize = 57U;
    constexpr std::size_t recordCount = 5U;
    constexpr std::size_t dataOffset = headerSize + recordSize * recordCount;
    constexpr std::size_t loaderSize = dataOffset + 8U + 3U * 512U + 4U;
    std::vector<std::byte> bytes(loaderSize);
    bytes[0] = std::byte{'L'};
    bytes[1] = std::byte{'D'};
    bytes[2] = std::byte{'R'};
    bytes[3] = std::byte{' '};
    writeLe16(bytes, 4U, headerSize);
    writeLe32(bytes, 6U, 0x00010000U);
    writeLe32(bytes, 21U, 0x00003588U);
    bytes[25] = std::byte{1};
    writeLe32(bytes, 26U, headerSize);
    bytes[30] = std::byte{recordSize};
    bytes[31] = std::byte{1};
    writeLe32(bytes, 32U, headerSize + recordSize);
    bytes[36] = std::byte{recordSize};
    bytes[37] = std::byte{3};
    writeLe32(bytes, 38U, headerSize + recordSize * 2U);
    bytes[42] = std::byte{recordSize};
    writeLoaderRecord(bytes, headerSize, 1U, "DDR", dataOffset, 4U);
    writeLoaderRecord(bytes, headerSize + recordSize, 2U, "USB",
                      dataOffset + 4U, 4U);
    writeLoaderRecord(bytes, headerSize + recordSize * 2U, 4U, "FlashHead",
                      dataOffset + 8U, 512U);
    writeLoaderRecord(bytes, headerSize + recordSize * 3U, 4U, "FlashData",
                      dataOffset + 8U + 512U, 512U);
    writeLoaderRecord(bytes, headerSize + recordSize * 4U, 4U, "FlashBoot",
                      dataOffset + 8U + 1024U, 512U);
    for (std::size_t index = dataOffset; index < loaderSize - 4U; ++index) {
        bytes[index] = static_cast<std::byte>(index & 0xffU);
    }
    writeLe32(bytes, loaderSize - 4U,
              rockchipCrc32(std::span<const std::byte>{bytes}.first(
                  loaderSize - 4U)));
    return bytes;
}

void testParameterFile()
{
    constexpr std::string_view parameter{
        "MACHINE_MODEL: unit-rk3588\n"
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x20@0x40(boot_a),"
        "0x20@0x60(boot_b),-@0x80(userdata:grow)\n"};
    TemporaryFile file{"zzrk-parameter-test.txt", parameter};
    const auto parsed = zzrk::image::parseRockchipParameterFile(file.path());
    const auto* layout =
        std::get_if<zzrk::image::RockchipParameterLayout>(&parsed);
    expect(layout != nullptr && layout->model == "unit-rk3588" &&
               layout->partitions.size() == 3U &&
               layout->partitions[1].firstLba == 0x60U &&
               layout->partitions[2].grow,
           "plain parameter.txt produces an ordered GPT layout");

    constexpr std::string_view invalid{
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:-@0x40(userdata:grow),"
        "0x20@0x60(boot)\n"};
    TemporaryFile bad{"zzrk-parameter-invalid-test.txt", invalid};
    const auto rejected = zzrk::image::parseRockchipParameterFile(bad.path());
    expect(std::holds_alternative<zzrk::image::FirmwareImageError>(rejected),
           "a grow partition before another partition is rejected");
}

void testPayloadInspection()
{
    const auto sparseBytes = makeSparseImage(true);
    TemporaryFile sparse{"zzrk-sparse-test.img", sparseBytes};
    const auto inspected = zzrk::image::inspectFirmwarePayload(sparse.path());
    const auto* payload =
        std::get_if<zzrk::image::FirmwarePayloadSource>(&inspected);
    expect(payload != nullptr &&
               payload->format == zzrk::image::PayloadFormat::androidSparse &&
               payload->logicalSize == 4096U,
           "a standalone Android Sparse image reports its expanded size");

    const auto invalidBytes = makeSparseImage(false);
    TemporaryFile invalid{"zzrk-sparse-invalid-test.img", invalidBytes};
    const auto rejected = zzrk::image::inspectFirmwarePayload(invalid.path());
    expect(std::holds_alternative<zzrk::image::FirmwareImageError>(rejected),
           "an inconsistent sparse chunk is rejected before writing");

    constexpr std::array raw{std::byte{1}, std::byte{2}, std::byte{3}};
    TemporaryFile rawFile{"zzrk-raw-test.img", raw};
    const auto rawResult = zzrk::image::inspectFirmwarePayload(rawFile.path());
    const auto* rawPayload =
        std::get_if<zzrk::image::FirmwarePayloadSource>(&rawResult);
    expect(rawPayload != nullptr &&
               rawPayload->format == zzrk::image::PayloadFormat::raw &&
               rawPayload->logicalSize == raw.size(),
           "a regular standalone image remains a raw payload");
}

void testStandaloneLoader()
{
    const auto bytes = makeLoaderImage();
    TemporaryFile file{"zzrk-loader-test.bin", bytes};
    const auto parsed = zzrk::image::parseRkLoaderImage(file.path());
    const auto* loader = std::get_if<zzrk::image::RkLoaderImage>(&parsed);
    expect(loader != nullptr && loader->info.code471.size() == 1U &&
               loader->info.code472.size() == 1U &&
               loader->info.idBlockSize == 6144U,
           "a standalone MiniLoaderAll.bin exposes RAM and IDBlock entries");
    if (loader == nullptr) {
        return;
    }
    const auto idBlock = zzrk::image::buildRkNewIdBlock(*loader);
    const auto* output = std::get_if<std::vector<std::byte>>(&idBlock);
    expect(output != nullptr && output->size() == 6144U,
           "the standalone Loader builds a sector-aligned New IDBlock");
}

} // namespace

int main()
{
    testParameterFile();
    testPayloadInspection();
    testStandaloneLoader();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
