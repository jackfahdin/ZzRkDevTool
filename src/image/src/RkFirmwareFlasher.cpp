// SPDX-License-Identifier: MIT

#include <zzrk/image/RkFirmwareFlasher.hpp>

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/EraseLba.hpp>
#include <zzrk/session/ReadLba.hpp>
#include <zzrk/session/WriteLba.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace zzrk::image {
namespace {

constexpr std::size_t kSectorSize =
    static_cast<std::size_t>(core::kSectorSize);
constexpr std::size_t kTransferSectors =
    static_cast<std::size_t>(session::kMaximumWriteLbaSectorsPerCommand);
constexpr std::size_t kTransferBytes = kTransferSectors * kSectorSize;
constexpr std::size_t kGptEntryCount = 128;
constexpr std::size_t kGptEntrySize = 128;
constexpr std::size_t kGptHeaderSize = 92;
constexpr std::size_t kPrimaryGptSectors = 34;
constexpr std::size_t kBackupGptSectors = 33;
constexpr std::uint32_t kSparseMagic = 0xed26ff3aU;
constexpr std::uint16_t kSparseRawChunk = 0xcac1U;
constexpr std::uint16_t kSparseFillChunk = 0xcac2U;
constexpr std::uint16_t kSparseDontCareChunk = 0xcac3U;
constexpr std::uint16_t kSparseCrc32Chunk = 0xcac4U;

struct GptBuffers {
    std::array<std::byte, kPrimaryGptSectors * kSectorSize> primary{};
    std::array<std::byte, kBackupGptSectors * kSectorSize> backup{};
};

[[nodiscard]] FirmwareFlashError makeError(
    const FirmwareFlashErrorCode code, std::string target,
    const std::uint64_t completedBytes = 0) noexcept
{
    FirmwareFlashError error{};
    error.code = code;
    error.target = std::move(target);
    error.completedBytes = completedBytes;
    return error;
}

[[nodiscard]] FirmwareFlashError makeSessionError(
    const FirmwareFlashErrorCode code, std::string target,
    const session::SessionError& sessionError,
    const std::uint64_t completedBytes = 0) noexcept
{
    auto error = makeError(code, std::move(target), completedBytes);
    error.sessionError = sessionError;
    return error;
}

void report(const FirmwareFlashProgressCallback& callback,
            const FirmwareFlashPhase phase, const std::string_view target,
            const std::uint64_t completedBytes,
            const std::uint64_t totalBytes)
{
    if (callback) {
        callback(FirmwareFlashProgress{
            .phase = phase,
            .target = std::string{target},
            .completedBytes = completedBytes,
            .totalBytes = totalBytes,
        });
    }
}

constexpr void writeLe16(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

constexpr void writeLe32(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

constexpr void writeLe64(const std::span<std::byte> bytes,
                         const std::size_t offset,
                         const std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

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
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + index])
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint32_t crc32(const std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

[[nodiscard]] constexpr std::uint64_t
hashText(const std::string_view text, std::uint64_t hash) noexcept
{
    for (const char character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::array<std::byte, 16>
makeGuid(const std::string_view scope, const std::string_view name,
         const std::uint64_t discriminator) noexcept
{
    std::uint64_t left = hashText(scope, 14695981039346656037ULL);
    left = hashText(name, left) ^ discriminator;
    std::uint64_t right = hashText(name, 1099511628211ULL);
    right = hashText(scope, right) ^ (discriminator * 0x9e3779b97f4a7c15ULL);
    std::array<std::byte, 16> guid{};
    for (std::size_t index = 0; index < 8U; ++index) {
        guid[index] = static_cast<std::byte>((left >> (index * 8U)) & 0xffU);
        guid[index + 8U] =
            static_cast<std::byte>((right >> (index * 8U)) & 0xffU);
    }
    guid[7] = static_cast<std::byte>(
        (std::to_integer<std::uint8_t>(guid[7]) & 0x0fU) | 0x40U);
    guid[8] = static_cast<std::byte>(
        (std::to_integer<std::uint8_t>(guid[8]) & 0x3fU) | 0x80U);
    return guid;
}

void copyGuid(const std::span<std::byte> destination,
              const std::size_t offset,
              const std::array<std::byte, 16>& guid) noexcept
{
    std::copy(guid.begin(), guid.end(), destination.begin()
                                               + static_cast<std::ptrdiff_t>(
                                                   offset));
}

[[nodiscard]] std::variant<GptBuffers, FirmwareFlashError>
makeGpt(const RkFirmwareImage& image, const std::uint64_t deviceSectors)
{
    if (deviceSectors < 68U || image.partitions.empty()
        || image.partitions.size() > kGptEntryCount) {
        return makeError(FirmwareFlashErrorCode::partitionTableGenerationFailed,
                         "GPT");
    }
    const std::uint64_t lastUsableLba = deviceSectors - 34U;
    for (const auto& partition : image.partitions) {
        const std::uint64_t count = partition.sectorCount.value_or(
            lastUsableLba >= partition.firstLba
                ? lastUsableLba - partition.firstLba + 1U
                : 0U);
        if (count == 0U || partition.firstLba < 34U
            || partition.firstLba > lastUsableLba
            || count - 1U > lastUsableLba - partition.firstLba) {
            return makeError(
                FirmwareFlashErrorCode::partitionTableGenerationFailed,
                partition.name);
        }
    }

    GptBuffers buffers{};
    auto primary = std::span<std::byte>{buffers.primary};
    auto backup = std::span<std::byte>{buffers.backup};

    constexpr std::size_t mbrPartition = 446;
    primary[mbrPartition + 4U] = std::byte{0xee};
    writeLe32(primary, mbrPartition + 8U, 1U);
    writeLe32(primary, mbrPartition + 12U,
              deviceSectors - 1U > std::numeric_limits<std::uint32_t>::max()
                  ? std::numeric_limits<std::uint32_t>::max()
                  : static_cast<std::uint32_t>(deviceSectors - 1U));
    primary[510] = std::byte{0x55};
    primary[511] = std::byte{0xaa};

    constexpr std::array<std::byte, 16> basicDataGuid{
        std::byte{0xa2}, std::byte{0xa0}, std::byte{0xd0}, std::byte{0xeb},
        std::byte{0xe5}, std::byte{0xb9}, std::byte{0x33}, std::byte{0x44},
        std::byte{0x87}, std::byte{0xc0}, std::byte{0x68}, std::byte{0xb6},
        std::byte{0xb7}, std::byte{0x26}, std::byte{0x99}, std::byte{0xc7},
    };
    auto entries = primary.subspan(2U * kSectorSize,
                                   kGptEntryCount * kGptEntrySize);
    for (std::size_t index = 0; index < image.partitions.size(); ++index) {
        const auto& partition = image.partitions[index];
        auto entry = entries.subspan(index * kGptEntrySize, kGptEntrySize);
        copyGuid(entry, 0, basicDataGuid);
        copyGuid(entry, 16,
                 makeGuid(image.model, partition.name,
                          partition.firstLba ^ static_cast<std::uint64_t>(index)));
        writeLe64(entry, 32, partition.firstLba);
        const std::uint64_t endingLba =
            partition.sectorCount.has_value()
                ? partition.firstLba + *partition.sectorCount - 1U
                : lastUsableLba;
        writeLe64(entry, 40, endingLba);
        const std::size_t nameLength = std::min<std::size_t>(
            partition.name.size(), (kGptEntrySize - 56U) / 2U);
        for (std::size_t character = 0; character < nameLength; ++character) {
            writeLe16(entry, 56U + character * 2U,
                      static_cast<std::uint8_t>(partition.name[character]));
        }
    }
    const std::uint32_t entriesCrc = crc32(entries);

    auto primaryHeader = primary.subspan(kSectorSize, kSectorSize);
    constexpr std::array<std::byte, 8> signature{
        std::byte{'E'}, std::byte{'F'}, std::byte{'I'}, std::byte{' '},
        std::byte{'P'}, std::byte{'A'}, std::byte{'R'}, std::byte{'T'},
    };
    std::copy(signature.begin(), signature.end(), primaryHeader.begin());
    writeLe32(primaryHeader, 8, 0x00010000U);
    writeLe32(primaryHeader, 12, kGptHeaderSize);
    writeLe64(primaryHeader, 24, 1U);
    writeLe64(primaryHeader, 32, deviceSectors - 1U);
    writeLe64(primaryHeader, 40, 34U);
    writeLe64(primaryHeader, 48, lastUsableLba);
    copyGuid(primaryHeader, 56,
             makeGuid(image.model, "disk", deviceSectors));
    writeLe64(primaryHeader, 72, 2U);
    writeLe32(primaryHeader, 80, kGptEntryCount);
    writeLe32(primaryHeader, 84, kGptEntrySize);
    writeLe32(primaryHeader, 88, entriesCrc);
    writeLe32(primaryHeader, 16,
              crc32(primaryHeader.first(kGptHeaderSize)));

    std::copy(entries.begin(), entries.end(), backup.begin());
    auto backupHeader = backup.subspan(32U * kSectorSize, kSectorSize);
    std::copy(primaryHeader.begin(), primaryHeader.end(), backupHeader.begin());
    writeLe32(backupHeader, 16, 0U);
    writeLe64(backupHeader, 24, deviceSectors - 1U);
    writeLe64(backupHeader, 32, 1U);
    writeLe64(backupHeader, 72, deviceSectors - 33U);
    writeLe32(backupHeader, 16,
              crc32(backupHeader.first(kGptHeaderSize)));
    return buffers;
}

[[nodiscard]] bool seekTo(std::ifstream& stream,
                          const std::uint64_t offset) noexcept
{
    if (offset
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return static_cast<bool>(stream);
}

[[nodiscard]] bool readExact(std::ifstream& stream,
                             const std::span<std::byte> destination) noexcept
{
    stream.read(reinterpret_cast<char*>(destination.data()),
                static_cast<std::streamsize>(destination.size()));
    return stream.gcount()
               == static_cast<std::streamsize>(destination.size())
           && static_cast<bool>(stream);
}

[[nodiscard]] std::optional<FirmwareFlashError>
writeBuffer(session::RockusbSession& rockusb,
            const std::span<const std::byte> source, std::uint64_t firstLba,
            const std::string_view target,
            const std::uint64_t completedBefore = 0)
{
    if (source.empty() || source.size() % kSectorSize != 0U) {
        return makeError(FirmwareFlashErrorCode::invalidPlan,
                         std::string{target}, completedBefore);
    }
    std::size_t offset = 0;
    while (offset < source.size()) {
        const std::size_t bytes =
            std::min(kTransferBytes, source.size() - offset);
        const std::uint64_t sectors = bytes / kSectorSize;
        const auto result = session::writeLba(
            rockusb, core::Lba{firstLba}, core::SectorCount{sectors},
            source.subspan(offset, bytes));
        if (const auto* error = std::get_if<session::SessionError>(&result)) {
            return makeSessionError(FirmwareFlashErrorCode::writeFailed,
                                    std::string{target}, *error,
                                    completedBefore + offset);
        }
        firstLba += sectors;
        offset += bytes;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FirmwareFlashError>
eraseRange(session::RockusbSession& rockusb, std::uint64_t firstLba,
           std::uint64_t sectorCount, const std::string_view target,
           const FirmwareFlashProgressCallback& progress)
{
    const std::uint64_t totalBytes = sectorCount * core::kSectorSize;
    std::uint64_t erased = 0;
    report(progress, FirmwareFlashPhase::erasePartition, target, 0,
           totalBytes);
    while (erased < sectorCount) {
        const std::uint64_t sectors = std::min(
            session::kMaximumEraseLbaSectorsPerCommand,
            sectorCount - erased);
        const auto result = session::eraseLba(
            rockusb, core::Lba{firstLba + erased},
            core::SectorCount{sectors});
        if (const auto* error = std::get_if<session::SessionError>(&result)) {
            return makeSessionError(FirmwareFlashErrorCode::eraseFailed,
                                    std::string{target}, *error,
                                    erased * core::kSectorSize);
        }
        erased += sectors;
        report(progress, FirmwareFlashPhase::erasePartition, target,
               erased * core::kSectorSize, totalBytes);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FirmwareFlashError>
writeRawEntry(session::RockusbSession& rockusb, std::ifstream& stream,
              const FirmwareEntry& entry, const FirmwareFlashStep& step,
              const FirmwareFlashProgressCallback& progress)
{
    if (!seekTo(stream, entry.fileOffset)) {
        return makeError(FirmwareFlashErrorCode::imageSeekFailed, step.target);
    }
    std::vector<std::byte> buffer(kTransferBytes);
    std::uint64_t consumed = 0;
    report(progress, FirmwareFlashPhase::writePartition, step.target, 0,
           entry.logicalSize);
    while (consumed < entry.contentSize) {
        std::fill(buffer.begin(), buffer.end(), std::byte{0});
        const std::size_t bytes = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(),
                                    entry.contentSize - consumed));
        if (!readExact(stream, std::span<std::byte>{buffer}.first(bytes))) {
            return makeError(FirmwareFlashErrorCode::imageReadFailed,
                             step.target, consumed);
        }
        const std::size_t transferBytes =
            ((bytes + kSectorSize - 1U) / kSectorSize) * kSectorSize;
        const std::uint64_t lba =
            step.firstLba + consumed / core::kSectorSize;
        if (auto error = writeBuffer(
                rockusb, std::span<const std::byte>{buffer}.first(transferBytes),
                lba, step.target, consumed)) {
            return error;
        }
        consumed += bytes;
        report(progress, FirmwareFlashPhase::writePartition, step.target,
               consumed, entry.logicalSize);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FirmwareFlashError>
writeSparseEntry(session::RockusbSession& rockusb, std::ifstream& stream,
                 const FirmwareEntry& entry, const FirmwareFlashStep& step,
                 const FirmwareFlashProgressCallback& progress)
{
    if (!seekTo(stream, entry.fileOffset)) {
        return makeError(FirmwareFlashErrorCode::imageSeekFailed, step.target);
    }
    std::array<std::byte, 28> headerBytes{};
    if (!readExact(stream, headerBytes)) {
        return makeError(FirmwareFlashErrorCode::imageReadFailed, step.target);
    }
    const auto header = std::span<const std::byte>{headerBytes};
    const std::uint32_t magic = readLe32(header, 0);
    const std::uint16_t fileHeaderSize = readLe16(header, 8);
    const std::uint16_t chunkHeaderSize = readLe16(header, 10);
    const std::uint32_t blockSize = readLe32(header, 12);
    const std::uint32_t totalChunks = readLe32(header, 20);
    if (magic != kSparseMagic || fileHeaderSize < headerBytes.size()
        || chunkHeaderSize < 12U || blockSize == 0U
        || blockSize % core::kSectorSize != 0U) {
        return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                         step.target);
    }
    if (!seekTo(stream, entry.fileOffset + fileHeaderSize)) {
        return makeError(FirmwareFlashErrorCode::imageSeekFailed, step.target);
    }

    std::vector<std::byte> chunkHeader(chunkHeaderSize);
    std::vector<std::byte> buffer(kTransferBytes);
    std::uint64_t logicalBytes = 0;
    std::uint64_t currentLba = step.firstLba;
    report(progress, FirmwareFlashPhase::writePartition, step.target, 0,
           entry.logicalSize);
    for (std::uint32_t chunkIndex = 0; chunkIndex < totalChunks;
         ++chunkIndex) {
        if (!readExact(stream, chunkHeader)) {
            return makeError(FirmwareFlashErrorCode::imageReadFailed,
                             step.target, logicalBytes);
        }
        const auto chunk = std::span<const std::byte>{chunkHeader};
        const std::uint16_t type = readLe16(chunk, 0);
        const std::uint32_t chunkBlocks = readLe32(chunk, 4);
        const std::uint32_t totalSize = readLe32(chunk, 8);
        const std::uint64_t chunkBytes =
            static_cast<std::uint64_t>(chunkBlocks) * blockSize;
        if (logicalBytes > entry.logicalSize
            || chunkBytes > entry.logicalSize - logicalBytes
            || totalSize < chunkHeaderSize) {
            return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                             step.target, logicalBytes);
        }

        if (type == kSparseRawChunk) {
            if (static_cast<std::uint64_t>(totalSize - chunkHeaderSize)
                != chunkBytes) {
                return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                                 step.target, logicalBytes);
            }
            std::uint64_t remaining = chunkBytes;
            while (remaining > 0U) {
                const std::size_t bytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(buffer.size(), remaining));
                if (!readExact(stream,
                               std::span<std::byte>{buffer}.first(bytes))) {
                    return makeError(FirmwareFlashErrorCode::imageReadFailed,
                                     step.target, logicalBytes);
                }
                if (auto error = writeBuffer(
                        rockusb,
                        std::span<const std::byte>{buffer}.first(bytes),
                        currentLba, step.target, logicalBytes)) {
                    return error;
                }
                const std::uint64_t sectors = bytes / kSectorSize;
                currentLba += sectors;
                logicalBytes += bytes;
                remaining -= bytes;
                report(progress, FirmwareFlashPhase::writePartition,
                       step.target, logicalBytes, entry.logicalSize);
            }
        } else if (type == kSparseFillChunk) {
            if (totalSize - chunkHeaderSize != 4U) {
                return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                                 step.target, logicalBytes);
            }
            std::array<std::byte, 4> fill{};
            if (!readExact(stream, fill)) {
                return makeError(FirmwareFlashErrorCode::imageReadFailed,
                                 step.target, logicalBytes);
            }
            for (std::size_t index = 0; index < buffer.size(); ++index) {
                buffer[index] = fill[index % fill.size()];
            }
            std::uint64_t remaining = chunkBytes;
            while (remaining > 0U) {
                const std::size_t bytes = static_cast<std::size_t>(
                    std::min<std::uint64_t>(buffer.size(), remaining));
                if (auto error = writeBuffer(
                        rockusb,
                        std::span<const std::byte>{buffer}.first(bytes),
                        currentLba, step.target, logicalBytes)) {
                    return error;
                }
                const std::uint64_t sectors = bytes / kSectorSize;
                currentLba += sectors;
                logicalBytes += bytes;
                remaining -= bytes;
                report(progress, FirmwareFlashPhase::writePartition,
                       step.target, logicalBytes, entry.logicalSize);
            }
        } else if (type == kSparseDontCareChunk) {
            if (totalSize != chunkHeaderSize) {
                return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                                 step.target, logicalBytes);
            }
            currentLba += chunkBytes / core::kSectorSize;
            logicalBytes += chunkBytes;
            report(progress, FirmwareFlashPhase::writePartition, step.target,
                   logicalBytes, entry.logicalSize);
        } else if (type == kSparseCrc32Chunk) {
            if (totalSize - chunkHeaderSize != 4U || chunkBytes != 0U) {
                return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                                 step.target, logicalBytes);
            }
            std::array<std::byte, 4> ignoredCrc{};
            if (!readExact(stream, ignoredCrc)) {
                return makeError(FirmwareFlashErrorCode::imageReadFailed,
                                 step.target, logicalBytes);
            }
        } else {
            return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                             step.target, logicalBytes);
        }
    }
    if (logicalBytes != entry.logicalSize) {
        return makeError(FirmwareFlashErrorCode::unsupportedPayload,
                         step.target, logicalBytes);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<FirmwareFlashError>
verifyBuffer(session::RockusbSession& rockusb,
             const std::span<const std::byte> expected,
             const std::uint64_t firstLba, const std::string_view target)
{
    std::vector<std::byte> actual(expected.size());
    const auto result = session::readLba(
        rockusb, core::Lba{firstLba},
        core::SectorCount{expected.size() / kSectorSize}, actual);
    if (const auto* error = std::get_if<session::SessionError>(&result)) {
        return makeSessionError(FirmwareFlashErrorCode::verifyFailed,
                                std::string{target}, *error);
    }
    if (!std::equal(expected.begin(), expected.end(), actual.begin())) {
        return makeError(FirmwareFlashErrorCode::verifyFailed,
                         std::string{target});
    }
    return std::nullopt;
}

} // namespace

FirmwareFlashResult flashRkFirmwareImage(
    session::RockusbSession& rockusb, const RkFirmwareImage& image,
    const RkFirmwareFlashPlan& plan, const std::uint64_t deviceSectors,
    const FirmwareFlashOptions& options,
    const FirmwareFlashProgressCallback& progress)
{
    if (plan.targetChip != "RK3588" || plan.sourcePath != image.sourcePath
        || plan.minimumDeviceSectors == 0U) {
        return makeError(FirmwareFlashErrorCode::invalidPlan, "image");
    }
    if (deviceSectors < plan.minimumDeviceSectors) {
        return makeError(FirmwareFlashErrorCode::deviceTooSmall, "eMMC");
    }
    if (deviceSectors > std::numeric_limits<std::uint32_t>::max()) {
        return makeError(FirmwareFlashErrorCode::addressOverflow, "eMMC");
    }

    std::ifstream stream{image.sourcePath, std::ios::binary};
    if (!stream) {
        return makeError(FirmwareFlashErrorCode::imageOpenFailed,
                         image.sourcePath.string());
    }

    std::optional<GptBuffers> gpt;
    FirmwareFlashSuccess success{};
    if (options.writePartitionTable) {
        auto generated = makeGpt(image, deviceSectors);
        if (const auto* error = std::get_if<FirmwareFlashError>(&generated)) {
            return *error;
        }
        gpt = std::get<GptBuffers>(std::move(generated));
        report(progress, FirmwareFlashPhase::writePartitionTable, "GPT", 0,
               (kPrimaryGptSectors + kBackupGptSectors) * kSectorSize);
        if (auto error = writeBuffer(rockusb, gpt->primary, 0, "primary GPT")) {
            return *error;
        }
        if (auto error = writeBuffer(rockusb, gpt->backup,
                                     deviceSectors - kBackupGptSectors,
                                     "backup GPT")) {
            return *error;
        }
        success.partitionTableWritten = true;
        report(progress, FirmwareFlashPhase::writePartitionTable, "GPT",
               (kPrimaryGptSectors + kBackupGptSectors) * kSectorSize,
               (kPrimaryGptSectors + kBackupGptSectors) * kSectorSize);
    }

    if (options.verifyPartitionTable && gpt.has_value()) {
        // RK3588 Loader 0x0223 returns 0xCC for the disk-tail backup GPT even
        // after a successful write. Verify the readable primary GPT strictly.
        report(progress, FirmwareFlashPhase::verifyPartitionTable, "GPT", 0,
               kPrimaryGptSectors * kSectorSize);
        if (auto error = verifyBuffer(rockusb, gpt->primary, 0,
                                      "primary GPT")) {
            return *error;
        }
        success.partitionTableVerified = true;
        report(progress, FirmwareFlashPhase::verifyPartitionTable, "GPT",
               kPrimaryGptSectors * kSectorSize,
               kPrimaryGptSectors * kSectorSize);
    }

    for (const auto& step : plan.steps) {
        if (step.kind != FirmwareFlashStepKind::writePartition) {
            continue;
        }
        if (!step.entryIndex.has_value()
            || *step.entryIndex >= image.entries.size()) {
            return makeError(FirmwareFlashErrorCode::invalidPlan, step.target,
                             success.partitionBytesWritten);
        }
        const auto& entry = image.entries[*step.entryIndex];
        if (entry.logicalSize > step.sectorCount * core::kSectorSize
            || step.firstLba + step.sectorCount > deviceSectors) {
            return makeError(FirmwareFlashErrorCode::invalidPlan, step.target,
                             success.partitionBytesWritten);
        }

        if (entry.format == PayloadFormat::androidSparse
            && options.eraseSparsePartitions) {
            if (auto error = eraseRange(rockusb, step.firstLba,
                                        step.sectorCount, step.target,
                                        progress)) {
                return *error;
            }
            success.sectorsErased += step.sectorCount;
        }

        std::optional<FirmwareFlashError> error;
        if (entry.format == PayloadFormat::raw) {
            error = writeRawEntry(rockusb, stream, entry, step, progress);
        } else if (entry.format == PayloadFormat::androidSparse) {
            error = writeSparseEntry(rockusb, stream, entry, step, progress);
        } else {
            error = makeError(FirmwareFlashErrorCode::unsupportedPayload,
                              step.target, success.partitionBytesWritten);
        }
        if (error.has_value()) {
            error->completedBytes += success.partitionBytesWritten;
            return *error;
        }
        success.partitionBytesWritten += entry.logicalSize;
    }
    return success;
}

} // namespace zzrk::image
