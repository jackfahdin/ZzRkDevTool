// SPDX-License-Identifier: MIT

#include <zzrk/image/RkFirmwareImage.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

namespace zzrk::image {
namespace {

constexpr std::size_t kRkfwMinimumHeaderSize = 41;
constexpr std::size_t kRkfwObservedHeaderSize = 102;
constexpr std::size_t kRkafHeaderSize = 0x8c;
constexpr std::size_t kRkafEntrySize = 0x70;
constexpr std::size_t kMaximumEntryCount = 4096;
constexpr std::size_t kTrailingMd5Size = 32;
constexpr std::size_t kMaximumParameterSize = 1024U * 1024U;
constexpr std::size_t kSparseHeaderSize = 28;
constexpr std::size_t kSparseChunkHeaderSize = 12;
constexpr std::uint16_t kSparseRawChunk = 0xcac1;
constexpr std::uint16_t kSparseFillChunk = 0xcac2;
constexpr std::uint16_t kSparseDontCareChunk = 0xcac3;
constexpr std::uint16_t kSparseCrc32Chunk = 0xcac4;
constexpr std::uint32_t kMaximumSparseChunks = 10'000'000;
constexpr std::size_t kRkLoaderHeaderSize = 102;
constexpr std::size_t kRkLoaderEntryMinimumSize = 57;
constexpr std::uint32_t kRkLoaderEntryType = 4;
constexpr std::uint64_t kRkLoaderAlignment = 2048;

[[nodiscard]] constexpr std::uint16_t
readLe16(const std::span<const std::byte> bytes,
         const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset])
        | (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t
readLe32(const std::span<const std::byte> bytes,
         const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset])
           | (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] constexpr bool addFits(const std::uint64_t left,
                                     const std::uint64_t right,
                                     const std::uint64_t limit) noexcept
{
    return left <= limit && right <= limit - left;
}

[[nodiscard]] FirmwareImageError makeError(
    const FirmwareImageErrorCode code, const std::uint64_t offset = 0,
    const std::size_t entryIndex = 0) noexcept
{
    return FirmwareImageError{
        .code = code,
        .offset = offset,
        .entryIndex = entryIndex,
    };
}

[[nodiscard]] bool readAt(std::ifstream& stream, const std::uint64_t offset,
                          const std::span<std::byte> destination) noexcept
{
    if (offset
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        return false;
    }
    stream.read(reinterpret_cast<char*>(destination.data()),
                static_cast<std::streamsize>(destination.size()));
    return stream.good()
           || (stream.eof()
               && stream.gcount()
                      == static_cast<std::streamsize>(destination.size()));
}

[[nodiscard]] bool hasSignature(const std::span<const std::byte> bytes,
                                const std::string_view signature) noexcept
{
    if (bytes.size() < signature.size()) {
        return false;
    }
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (std::to_integer<unsigned char>(bytes[index])
            != static_cast<unsigned char>(signature[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string readFixedString(
    const std::span<const std::byte> bytes) noexcept
{
    std::string value;
    value.reserve(bytes.size());
    for (const std::byte byte : bytes) {
        const auto character = std::to_integer<unsigned char>(byte);
        if (character == 0U) {
            break;
        }
        value.push_back(static_cast<char>(character));
    }
    return value;
}

[[nodiscard]] std::string readUtf16LeName(
    const std::span<const std::byte> bytes) noexcept
{
    std::string value;
    value.reserve(bytes.size() / 2U);
    for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
        const std::uint16_t character = readLe16(bytes, offset);
        if (character == 0U) {
            break;
        }
        if (character > 0x7fU) {
            return {};
        }
        value.push_back(static_cast<char>(character));
    }
    return value;
}

[[nodiscard]] std::uint32_t rockchipCrc32(
    const std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0;
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

[[nodiscard]] constexpr std::uint64_t alignUp(
    const std::uint64_t value, const std::uint64_t alignment) noexcept
{
    return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] std::variant<FirmwareLoaderInfo, FirmwareImageError>
parseLoader(std::ifstream& stream, const FirmwareEntry& entry,
            const std::size_t entryIndex) noexcept
{
    if (entry.contentSize < kRkLoaderHeaderSize + 4U) {
        return makeError(FirmwareImageErrorCode::invalidLoader,
                         entry.fileOffset, entryIndex);
    }
    std::vector<std::byte> bytes(entry.contentSize);
    if (!readAt(stream, entry.fileOffset, bytes)) {
        return makeError(FirmwareImageErrorCode::shortRead,
                         entry.fileOffset, entryIndex);
    }
    const auto loader = std::span<const std::byte>{bytes};
    if ((!hasSignature(loader, "LDR ") && !hasSignature(loader, "BOOT"))
        || readLe16(loader, 4) < kRkLoaderHeaderSize
        || readLe16(loader, 4) > entry.contentSize - 4U
        || rockchipCrc32(loader.first(loader.size() - 4U))
               != readLe32(loader, loader.size() - 4U)) {
        return makeError(FirmwareImageErrorCode::invalidLoader,
                         entry.fileOffset, entryIndex);
    }

    struct Table {
        std::uint8_t count{};
        std::uint32_t offset{};
        std::uint8_t entrySize{};
        std::uint32_t expectedType{};
    };
    const std::array tables{
        Table{std::to_integer<std::uint8_t>(loader[25]), readLe32(loader, 26),
              std::to_integer<std::uint8_t>(loader[30]), 1U},
        Table{std::to_integer<std::uint8_t>(loader[31]), readLe32(loader, 32),
              std::to_integer<std::uint8_t>(loader[36]), 2U},
        Table{std::to_integer<std::uint8_t>(loader[37]), readLe32(loader, 38),
              std::to_integer<std::uint8_t>(loader[42]),
              kRkLoaderEntryType},
    };

    FirmwareLoaderInfo info{};
    info.version = readLe32(loader, 6);
    info.chipType = readLe32(loader, 21);
    info.rc4Disable = std::to_integer<std::uint8_t>(loader[44]) != 0U;
    bool foundHead = false;
    bool foundData = false;
    bool foundBoot = false;
    for (const auto& table : tables) {
        if (table.count == 0U) {
            continue;
        }
        const std::uint64_t tableSize =
            static_cast<std::uint64_t>(table.count) * table.entrySize;
        if (table.count > 64U || table.entrySize < kRkLoaderEntryMinimumSize
            || !addFits(table.offset, tableSize, loader.size() - 4U)) {
            return makeError(FirmwareImageErrorCode::invalidLoader,
                             entry.fileOffset + table.offset, entryIndex);
        }
        for (std::size_t index = 0; index < table.count; ++index) {
            const std::uint64_t relativeEntryOffset =
                table.offset + index * table.entrySize;
            const auto record = loader.subspan(
                static_cast<std::size_t>(relativeEntryOffset),
                table.entrySize);
            const std::string name = readUtf16LeName(record.subspan(5, 40));
            const std::uint32_t dataOffset = readLe32(record, 45);
            const std::uint32_t dataSize = readLe32(record, 49);
            if (std::to_integer<std::uint8_t>(record[0])
                    < kRkLoaderEntryMinimumSize
                || readLe32(record, 1) != table.expectedType || name.empty()
                || dataSize == 0U
                || !addFits(dataOffset, dataSize, loader.size() - 4U)) {
                return makeError(FirmwareImageErrorCode::invalidLoader,
                                 entry.fileOffset + relativeEntryOffset,
                                 entryIndex);
            }
            if (table.expectedType != kRkLoaderEntryType) {
                continue;
            }
            const FirmwareLoaderComponent component{
                .relativeOffset = dataOffset,
                .size = dataSize,
            };
            if (name == "FlashHead" && !foundHead) {
                info.flashHead = component;
                foundHead = true;
            } else if (name == "FlashData" && !foundData) {
                info.flashData = component;
                foundData = true;
            } else if (name == "FlashBoot" && !foundBoot) {
                info.flashBoot = component;
                foundBoot = true;
            }
        }
    }
    if (!foundHead || !foundData || !foundBoot
        || info.flashHead.size % 512U != 0U
        || info.flashData.size % 512U != 0U
        || info.flashBoot.size % 512U != 0U) {
        return makeError(FirmwareImageErrorCode::invalidLoader,
                         entry.fileOffset, entryIndex);
    }
    info.idBlockSize = alignUp(info.flashHead.size, kRkLoaderAlignment)
                       + alignUp(info.flashData.size, kRkLoaderAlignment)
                       + alignUp(info.flashBoot.size, kRkLoaderAlignment);
    return info;
}

[[nodiscard]] std::string readChipName(
    const std::span<const std::byte> bytes) noexcept
{
    std::string chip = readFixedString(bytes);
    std::ranges::reverse(chip);
    return chip;
}

[[nodiscard]] PayloadFormat detectPayloadFormat(
    const std::array<std::byte, 4>& prefix) noexcept
{
    const std::span<const std::byte> bytes{prefix};
    if (readLe32(bytes, 0) == 0xed26ff3aU) {
        return PayloadFormat::androidSparse;
    }
    if (hasSignature(bytes, "PARM")) {
        return PayloadFormat::rockchipParameter;
    }
    return PayloadFormat::raw;
}

[[nodiscard]] bool isLowerHex(const char character) noexcept
{
    return (character >= '0' && character <= '9')
           || (character >= 'a' && character <= 'f');
}

[[nodiscard]] bool parseHex64(std::string_view text,
                              std::uint64_t& value) noexcept
{
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return false;
    }
    const auto converted = std::from_chars(text.data(),
                                           text.data() + text.size(), value, 16);
    return converted.ec == std::errc{}
           && converted.ptr == text.data() + text.size();
}

[[nodiscard]] std::variant<std::vector<FirmwarePartition>, FirmwareImageError>
parseParameter(std::ifstream& stream, const FirmwareEntry& entry,
               const std::size_t entryIndex) noexcept
{
    if (entry.contentSize < 12U || entry.contentSize > kMaximumParameterSize) {
        return makeError(FirmwareImageErrorCode::invalidParameter,
                         entry.fileOffset, entryIndex);
    }
    std::vector<std::byte> bytes(entry.contentSize);
    if (!readAt(stream, entry.fileOffset, bytes)) {
        return makeError(FirmwareImageErrorCode::shortRead,
                         entry.fileOffset, entryIndex);
    }
    const std::span<const std::byte> parameter{bytes};
    if (!hasSignature(parameter, "PARM")) {
        return makeError(FirmwareImageErrorCode::invalidParameter,
                         entry.fileOffset, entryIndex);
    }
    const std::uint32_t textSize = readLe32(parameter, 4);
    if (textSize > entry.contentSize - 12U
        || static_cast<std::uint64_t>(textSize) + 12U != entry.contentSize) {
        return makeError(FirmwareImageErrorCode::invalidParameter,
                         entry.fileOffset + 4U, entryIndex);
    }
    const std::string text = readFixedString(parameter.subspan(8, textSize));
    constexpr std::string_view key{"CMDLINE:"};
    const std::size_t keyOffset = text.find(key);
    if (keyOffset == std::string::npos) {
        return makeError(FirmwareImageErrorCode::invalidParameter,
                         entry.fileOffset + 8U, entryIndex);
    }
    const std::size_t lineEnd = text.find('\n', keyOffset);
    const std::string_view commandLine{text.data() + keyOffset + key.size(),
                                       (lineEnd == std::string::npos
                                            ? text.size()
                                            : lineEnd)
                                           - keyOffset - key.size()};
    const std::size_t layoutOffset = commandLine.find(':');
    if (!commandLine.starts_with("mtdparts=")
        || layoutOffset == std::string_view::npos
        || layoutOffset + 1U >= commandLine.size()) {
        return makeError(FirmwareImageErrorCode::invalidPartitionLayout,
                         entry.fileOffset + 8U + keyOffset, entryIndex);
    }

    std::vector<FirmwarePartition> partitions;
    std::string_view layout = commandLine.substr(layoutOffset + 1U);
    while (!layout.empty()) {
        const std::size_t comma = layout.find(',');
        const std::string_view token = layout.substr(0, comma);
        const std::size_t at = token.find('@');
        const std::size_t open = token.find('(', at);
        const std::size_t close = token.rfind(')');
        if (at == std::string_view::npos || open == std::string_view::npos
            || close != token.size() - 1U || at == 0U || open <= at + 1U) {
            return makeError(FirmwareImageErrorCode::invalidPartitionLayout,
                             entry.fileOffset + 8U + keyOffset, entryIndex);
        }

        const std::string_view sizeText = token.substr(0, at);
        const std::string_view offsetText = token.substr(at + 1U,
                                                        open - at - 1U);
        std::string_view nameText = token.substr(open + 1U,
                                                close - open - 1U);
        const std::size_t option = nameText.find(':');
        const std::string_view optionText = option == std::string_view::npos
                                                ? std::string_view{}
                                                : nameText.substr(option + 1U);
        if (option != std::string_view::npos) {
            nameText = nameText.substr(0, option);
        }
        std::uint64_t firstLba = 0;
        std::uint64_t sectorCount = 0;
        if (nameText.empty() || !parseHex64(offsetText, firstLba)
            || (sizeText != "-" && !parseHex64(sizeText, sectorCount))
            || (sizeText != "-" && sectorCount == 0U)) {
            return makeError(FirmwareImageErrorCode::invalidPartitionLayout,
                             entry.fileOffset + 8U + keyOffset, entryIndex);
        }
        const bool grow = sizeText == "-" || optionText == "grow";
        FirmwarePartition partition{
            .name = std::string{nameText},
            .firstLba = firstLba,
            .sectorCount = sizeText == "-"
                               ? std::nullopt
                               : std::optional<std::uint64_t>{sectorCount},
            .grow = grow,
        };
        if (!partitions.empty()) {
            const auto& previous = partitions.back();
            if (!previous.sectorCount.has_value()
                || !addFits(previous.firstLba, *previous.sectorCount,
                            partition.firstLba)
                || previous.firstLba + *previous.sectorCount
                       > partition.firstLba) {
                return makeError(
                    FirmwareImageErrorCode::invalidPartitionLayout,
                    entry.fileOffset + 8U + keyOffset, entryIndex);
            }
        }
        partitions.push_back(std::move(partition));
        if (comma == std::string_view::npos) {
            break;
        }
        layout.remove_prefix(comma + 1U);
    }
    return partitions;
}

[[nodiscard]] std::optional<FirmwareImageError>
validateSparse(std::ifstream& stream, FirmwareEntry& entry,
               const std::size_t entryIndex) noexcept
{
    if (entry.contentSize < kSparseHeaderSize) {
        return makeError(FirmwareImageErrorCode::invalidSparseImage,
                         entry.fileOffset, entryIndex);
    }
    std::array<std::byte, kSparseHeaderSize> headerBytes{};
    if (!readAt(stream, entry.fileOffset, headerBytes)) {
        return makeError(FirmwareImageErrorCode::shortRead,
                         entry.fileOffset, entryIndex);
    }
    const std::span<const std::byte> header{headerBytes};
    const std::uint16_t majorVersion = readLe16(header, 4);
    const std::uint16_t fileHeaderSize = readLe16(header, 8);
    const std::uint16_t chunkHeaderSize = readLe16(header, 10);
    const std::uint32_t blockSize = readLe32(header, 12);
    const std::uint32_t totalBlocks = readLe32(header, 16);
    const std::uint32_t totalChunks = readLe32(header, 20);
    if (majorVersion != 1U || fileHeaderSize < kSparseHeaderSize
        || chunkHeaderSize < kSparseChunkHeaderSize || blockSize == 0U
        || blockSize % 512U != 0U || totalBlocks == 0U
        || totalChunks == 0U || totalChunks > kMaximumSparseChunks
        || fileHeaderSize > entry.contentSize) {
        return makeError(FirmwareImageErrorCode::invalidSparseImage,
                         entry.fileOffset, entryIndex);
    }
    if (totalBlocks
        > std::numeric_limits<std::uint64_t>::max() / blockSize) {
        return makeError(FirmwareImageErrorCode::invalidSparseImage,
                         entry.fileOffset + 16U, entryIndex);
    }
    entry.logicalSize = static_cast<std::uint64_t>(totalBlocks) * blockSize;

    std::uint64_t relativeOffset = fileHeaderSize;
    std::uint64_t expandedBlocks = 0;
    std::vector<std::byte> chunkBytes(chunkHeaderSize);
    for (std::uint32_t chunkIndex = 0; chunkIndex < totalChunks; ++chunkIndex) {
        if (!addFits(relativeOffset, chunkHeaderSize, entry.contentSize)
            || !readAt(stream, entry.fileOffset + relativeOffset, chunkBytes)) {
            return makeError(FirmwareImageErrorCode::invalidSparseImage,
                             entry.fileOffset + relativeOffset, entryIndex);
        }
        const std::span<const std::byte> chunk{chunkBytes};
        const std::uint16_t chunkType = readLe16(chunk, 0);
        const std::uint32_t chunkBlocks = readLe32(chunk, 4);
        const std::uint32_t totalSize = readLe32(chunk, 8);
        if (totalSize < chunkHeaderSize
            || expandedBlocks
                   > std::numeric_limits<std::uint64_t>::max() - chunkBlocks) {
            return makeError(FirmwareImageErrorCode::invalidSparseImage,
                             entry.fileOffset + relativeOffset, entryIndex);
        }
        std::uint64_t expectedPayload = 0;
        switch (chunkType) {
        case kSparseRawChunk:
            expectedPayload =
                static_cast<std::uint64_t>(chunkBlocks) * blockSize;
            break;
        case kSparseFillChunk:
            expectedPayload = 4;
            break;
        case kSparseDontCareChunk:
            expectedPayload = 0;
            break;
        case kSparseCrc32Chunk:
            expectedPayload = 4;
            break;
        default:
            return makeError(FirmwareImageErrorCode::invalidSparseImage,
                             entry.fileOffset + relativeOffset, entryIndex);
        }
        if (expectedPayload
                != static_cast<std::uint64_t>(totalSize - chunkHeaderSize)
            || !addFits(relativeOffset, totalSize, entry.contentSize)) {
            return makeError(FirmwareImageErrorCode::invalidSparseImage,
                             entry.fileOffset + relativeOffset, entryIndex);
        }
        expandedBlocks += chunkBlocks;
        relativeOffset += totalSize;
    }
    if (relativeOffset != entry.contentSize || expandedBlocks != totalBlocks) {
        return makeError(FirmwareImageErrorCode::invalidSparseImage,
                         entry.fileOffset + relativeOffset, entryIndex);
    }
    return std::nullopt;
}

} // namespace

RkFirmwareImageResult
parseRkFirmwareImage(const std::filesystem::path& path) noexcept
{
    std::error_code filesystemError;
    const std::uint64_t fileSize = std::filesystem::file_size(path,
                                                               filesystemError);
    if (filesystemError) {
        return makeError(FirmwareImageErrorCode::fileStatFailed);
    }
    if (fileSize < kRkfwObservedHeaderSize + kTrailingMd5Size) {
        return makeError(FirmwareImageErrorCode::fileTooSmall);
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return makeError(FirmwareImageErrorCode::fileOpenFailed);
    }

    std::array<std::byte, kRkfwObservedHeaderSize> rkfwBytes{};
    if (!readAt(stream, 0, rkfwBytes)) {
        return makeError(FirmwareImageErrorCode::shortRead);
    }
    const std::span<const std::byte> rkfw{rkfwBytes};
    if (!hasSignature(rkfw, "RKFW")) {
        return makeError(FirmwareImageErrorCode::invalidRkfwSignature);
    }

    RkFirmwareImage image;
    image.sourcePath = path;
    image.fileSize = fileSize;
    image.headerSize = readLe16(rkfw, 4);
    if (image.headerSize < kRkfwMinimumHeaderSize
        || image.headerSize > rkfwBytes.size()) {
        return makeError(FirmwareImageErrorCode::invalidRkfwHeader, 4);
    }
    image.version = FirmwareVersion{
        .major = std::to_integer<std::uint8_t>(rkfw[9]),
        .minor = std::to_integer<std::uint8_t>(rkfw[8]),
        .patch = readLe16(rkfw, 6),
    };
    image.timestamp = FirmwareTimestamp{
        .year = readLe16(rkfw, 14),
        .month = std::to_integer<std::uint8_t>(rkfw[16]),
        .day = std::to_integer<std::uint8_t>(rkfw[17]),
        .hour = std::to_integer<std::uint8_t>(rkfw[18]),
        .minute = std::to_integer<std::uint8_t>(rkfw[19]),
        .second = std::to_integer<std::uint8_t>(rkfw[20]),
    };
    image.chip = readChipName(rkfw.subspan(21, 4));
    image.bootOffset = readLe32(rkfw, 25);
    image.bootSize = readLe32(rkfw, 29);
    image.firmwareOffset = readLe32(rkfw, 33);
    image.firmwareSize = readLe32(rkfw, 37);

    if (!addFits(image.bootOffset, image.bootSize, fileSize)
        || image.bootOffset < image.headerSize) {
        return makeError(FirmwareImageErrorCode::invalidBootRegion,
                         image.bootOffset);
    }
    if (!addFits(image.firmwareOffset, image.firmwareSize, fileSize)
        || image.firmwareOffset < image.bootOffset + image.bootSize) {
        return makeError(FirmwareImageErrorCode::invalidFirmwareRegion,
                         image.firmwareOffset);
    }

    std::array<std::byte, 4> signature{};
    if (!readAt(stream, image.bootOffset, signature)) {
        return makeError(FirmwareImageErrorCode::shortRead, image.bootOffset);
    }
    const std::span<const std::byte> bootSignature{signature};
    if (!hasSignature(bootSignature, "BOOT")
        && !hasSignature(bootSignature, "LDR ")) {
        return makeError(FirmwareImageErrorCode::invalidBootSignature,
                         image.bootOffset);
    }
    image.bootSignature = readFixedString(bootSignature);

    std::array<std::byte, kRkafHeaderSize> rkafBytes{};
    if (!readAt(stream, image.firmwareOffset, rkafBytes)) {
        return makeError(FirmwareImageErrorCode::shortRead,
                         image.firmwareOffset);
    }
    const std::span<const std::byte> rkaf{rkafBytes};
    if (!hasSignature(rkaf, "RKAF")) {
        return makeError(FirmwareImageErrorCode::invalidRkafSignature,
                         image.firmwareOffset);
    }
    const std::uint64_t rkafDeclaredSize =
        static_cast<std::uint64_t>(readLe32(rkaf, 4)) + 4U;
    if (rkafDeclaredSize != image.firmwareSize) {
        return makeError(FirmwareImageErrorCode::rkafSizeMismatch,
                         image.firmwareOffset + 4U);
    }
    image.model = readFixedString(rkaf.subspan(8, 0x40));
    image.manufacturer = readFixedString(rkaf.subspan(0x48, 0x40));

    const std::uint32_t entryCount = readLe32(rkaf, 0x88);
    if (entryCount == 0U || entryCount > kMaximumEntryCount) {
        return makeError(FirmwareImageErrorCode::invalidEntryCount,
                         image.firmwareOffset + 0x88U);
    }
    const std::uint64_t entryTableSize =
        static_cast<std::uint64_t>(entryCount) * kRkafEntrySize;
    if (!addFits(kRkafHeaderSize, entryTableSize, image.firmwareSize)) {
        return makeError(FirmwareImageErrorCode::invalidEntryTable,
                         image.firmwareOffset + kRkafHeaderSize);
    }

    std::vector<std::byte> entryBytes(static_cast<std::size_t>(entryTableSize));
    if (!readAt(stream, image.firmwareOffset + kRkafHeaderSize, entryBytes)) {
        return makeError(FirmwareImageErrorCode::shortRead,
                         image.firmwareOffset + kRkafHeaderSize);
    }
    image.entries.reserve(entryCount);
    for (std::size_t index = 0; index < entryCount; ++index) {
        const auto entry = std::span<const std::byte>{entryBytes}.subspan(
            index * kRkafEntrySize, kRkafEntrySize);
        FirmwareEntry parsed{
            .name = readFixedString(entry.subspan(0, 0x20)),
            .path = readFixedString(entry.subspan(0x20, 0x40)),
            .containerOffset = readLe32(entry, 0x60),
            .storedSize = readLe32(entry, 0x68),
            .contentSize = readLe32(entry, 0x6c),
        };
        parsed.fileOffset = image.firmwareOffset + parsed.containerOffset;
        parsed.selfEntry = parsed.path == "SELF";
        if (!addFits(parsed.containerOffset, parsed.contentSize,
                     image.firmwareSize)) {
            return makeError(FirmwareImageErrorCode::invalidEntryRegion,
                             parsed.fileOffset, index);
        }
        if (parsed.contentSize > 0U) {
            std::array<std::byte, 4> prefix{};
            if (!readAt(stream, parsed.fileOffset, prefix)) {
                return makeError(FirmwareImageErrorCode::shortRead,
                                 parsed.fileOffset, index);
            }
            parsed.format = detectPayloadFormat(prefix);
            parsed.logicalSize = parsed.contentSize;
            if (parsed.format == PayloadFormat::androidSparse) {
                if (const auto error = validateSparse(stream, parsed, index)) {
                    return *error;
                }
            }
        }
        image.entries.push_back(std::move(parsed));
    }

    const auto loader = std::ranges::find(
        image.entries, std::string{"bootloader"}, &FirmwareEntry::name);
    if (loader == image.entries.end()) {
        return makeError(FirmwareImageErrorCode::invalidLoader,
                         image.firmwareOffset);
    }
    const std::size_t loaderIndex = static_cast<std::size_t>(
        std::distance(image.entries.begin(), loader));
    auto parsedLoader = parseLoader(stream, *loader, loaderIndex);
    if (const auto* error = std::get_if<FirmwareImageError>(&parsedLoader)) {
        return *error;
    }
    image.loader = std::get<FirmwareLoaderInfo>(std::move(parsedLoader));

    const auto parameter = std::ranges::find(
        image.entries, std::string{"parameter"}, &FirmwareEntry::name);
    if (parameter == image.entries.end()
        || parameter->format != PayloadFormat::rockchipParameter) {
        return makeError(FirmwareImageErrorCode::invalidParameter,
                         image.firmwareOffset);
    }
    const std::size_t parameterIndex = static_cast<std::size_t>(
        std::distance(image.entries.begin(), parameter));
    auto parsedPartitions = parseParameter(stream, *parameter, parameterIndex);
    if (const auto* error = std::get_if<FirmwareImageError>(&parsedPartitions)) {
        return *error;
    }
    image.partitions =
        std::move(std::get<std::vector<FirmwarePartition>>(parsedPartitions));

    const std::uint64_t payloadEnd = image.firmwareOffset + image.firmwareSize;
    if (fileSize - payloadEnd != kTrailingMd5Size) {
        return makeError(FirmwareImageErrorCode::invalidTrailingMd5,
                         payloadEnd);
    }
    std::array<std::byte, kTrailingMd5Size> md5Bytes{};
    if (!readAt(stream, payloadEnd, md5Bytes)) {
        return makeError(FirmwareImageErrorCode::shortRead, payloadEnd);
    }
    for (std::size_t index = 0; index < md5Bytes.size(); ++index) {
        const char character = static_cast<char>(
            std::to_integer<unsigned char>(md5Bytes[index]));
        if (!isLowerHex(character)) {
            return makeError(FirmwareImageErrorCode::invalidTrailingMd5,
                             payloadEnd + index);
        }
        image.trailingMd5[index] = character;
    }
    image.trailingMd5.back() = '\0';
    return image;
}

RkFirmwareFlashPlanResult
buildRkFirmwareFlashPlan(const RkFirmwareImage& image) noexcept
{
    if (!image.trailingMd5Verified) {
        return FirmwarePlanError{
            .code = FirmwarePlanErrorCode::firmwareMd5NotVerified,
            .target = "firmware MD5",
        };
    }
    if (image.chip != "3588") {
        return FirmwarePlanError{
            .code = FirmwarePlanErrorCode::unsupportedTargetChip,
            .target = image.chip,
        };
    }

    const auto loader = std::ranges::find(
        image.entries, std::string{"bootloader"}, &FirmwareEntry::name);
    if (loader == image.entries.end() || !image.loader.has_value()) {
        return FirmwarePlanError{
            .code = FirmwarePlanErrorCode::missingLoader,
            .target = "bootloader",
        };
    }
    const auto parameter = std::ranges::find(
        image.entries, std::string{"parameter"}, &FirmwareEntry::name);
    if (parameter == image.entries.end() || image.partitions.empty()) {
        return FirmwarePlanError{
            .code = FirmwarePlanErrorCode::missingParameter,
            .target = "parameter",
        };
    }

    for (std::size_t left = 0; left < image.partitions.size(); ++left) {
        for (std::size_t right = left + 1U; right < image.partitions.size();
             ++right) {
            if (image.partitions[left].name == image.partitions[right].name) {
                return FirmwarePlanError{
                    .code = FirmwarePlanErrorCode::duplicatePartition,
                    .target = image.partitions[left].name,
                };
            }
        }
    }

    RkFirmwareFlashPlan plan{
        .sourcePath = image.sourcePath,
        .targetChip = "RK" + image.chip,
        .targetModel = image.model,
        .minimumDeviceSectors =
            64U + image.loader->idBlockSize / 512U,
        .totalPartitionBytes = 0,
        .steps = {},
    };
    const auto loaderIndex = static_cast<std::size_t>(
        std::distance(image.entries.begin(), loader));
    plan.steps.push_back(FirmwareFlashStep{
        .kind = FirmwareFlashStepKind::upgradeLoader,
        .target = "eMMC IDBlock",
        .entryIndex = loaderIndex,
        .inputSize = loader->contentSize,
        .outputSize = image.loader->idBlockSize,
        .format = PayloadFormat::raw,
    });
    const auto parameterIndex = static_cast<std::size_t>(
        std::distance(image.entries.begin(), parameter));
    plan.steps.push_back(FirmwareFlashStep{
        .kind = FirmwareFlashStepKind::ensurePartitionTable,
        .target = "primary and backup GPT",
        .entryIndex = parameterIndex,
        .inputSize = parameter->contentSize,
        .outputSize = parameter->logicalSize,
        .format = parameter->format,
    });

    for (const auto& partition : image.partitions) {
        if (partition.sectorCount.has_value()) {
            if (partition.firstLba
                > std::numeric_limits<std::uint64_t>::max()
                      - *partition.sectorCount) {
                return FirmwarePlanError{
                    .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                    .target = partition.name,
                };
            }
            if (partition.firstLba + *partition.sectorCount
                > std::numeric_limits<std::uint64_t>::max() - 33U) {
                return FirmwarePlanError{
                    .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                    .target = partition.name,
                };
            }
            plan.minimumDeviceSectors = std::max(
                plan.minimumDeviceSectors,
                partition.firstLba + *partition.sectorCount + 33U);
        } else {
            if (partition.firstLba
                > std::numeric_limits<std::uint64_t>::max() - 34U) {
                return FirmwarePlanError{
                    .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                    .target = partition.name,
                };
            }
            plan.minimumDeviceSectors = std::max(
                plan.minimumDeviceSectors, partition.firstLba + 34U);
        }
    }

    for (std::size_t entryIndex = 0; entryIndex < image.entries.size();
         ++entryIndex) {
        const auto& entry = image.entries[entryIndex];
        if (entry.selfEntry || entry.name == "package-file"
            || entry.name == "bootloader" || entry.name == "parameter") {
            continue;
        }
        const auto partition = std::ranges::find(
            image.partitions, entry.name, &FirmwarePartition::name);
        if (partition == image.partitions.end()) {
            return FirmwarePlanError{
                .code = FirmwarePlanErrorCode::targetPartitionMissing,
                .target = entry.name,
                .entryIndex = entryIndex,
            };
        }
        if (entry.logicalSize
            > std::numeric_limits<std::uint64_t>::max() - 511U) {
            return FirmwarePlanError{
                .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                .target = entry.name,
                .entryIndex = entryIndex,
            };
        }
        const std::uint64_t requiredSectors =
            (entry.logicalSize + 511U) / 512U;
        if (partition->sectorCount.has_value()
            && *partition->sectorCount
                   > std::numeric_limits<std::uint64_t>::max() / 512U) {
            return FirmwarePlanError{
                .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                .target = entry.name,
                .entryIndex = entryIndex,
            };
        }
        if (partition->sectorCount.has_value()
            && requiredSectors > *partition->sectorCount) {
            return FirmwarePlanError{
                .code = FirmwarePlanErrorCode::targetPartitionTooSmall,
                .target = entry.name,
                .entryIndex = entryIndex,
            };
        }
        if (!partition->sectorCount.has_value()) {
            if (partition->firstLba
                > std::numeric_limits<std::uint64_t>::max()
                      - requiredSectors - 33U) {
                return FirmwarePlanError{
                    .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                    .target = entry.name,
                    .entryIndex = entryIndex,
                };
            }
            plan.minimumDeviceSectors = std::max(
                plan.minimumDeviceSectors,
                partition->firstLba + requiredSectors + 33U);
        }
        if (plan.totalPartitionBytes
            > std::numeric_limits<std::uint64_t>::max() - entry.logicalSize) {
            return FirmwarePlanError{
                .code = FirmwarePlanErrorCode::partitionRangeOverflow,
                .target = entry.name,
                .entryIndex = entryIndex,
            };
        }
        plan.totalPartitionBytes += entry.logicalSize;
        plan.steps.push_back(FirmwareFlashStep{
            .kind = FirmwareFlashStepKind::writePartition,
            .target = entry.name,
            .entryIndex = entryIndex,
            .firstLba = partition->firstLba,
            .sectorCount = partition->sectorCount.value_or(0U),
            .inputSize = entry.contentSize,
            .outputSize = entry.logicalSize,
            .format = entry.format,
        });
    }
    return plan;
}

} // namespace zzrk::image
