// SPDX-License-Identifier: MIT

#include <zzrk/storage/GptPartitionTable.hpp>

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/ReadLba.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace zzrk::storage {
namespace {

constexpr std::size_t kSectorSize =
    static_cast<std::size_t>(core::kSectorSize);
constexpr std::uint32_t kGptRevision = 0x00010000U;
constexpr std::uint32_t kMinimumHeaderSize = 92U;
constexpr std::uint32_t kMinimumEntrySize = 128U;
constexpr std::uint32_t kMaximumEntryCount = 4096U;
constexpr std::uint64_t kMaximumEntryArrayBytes = 16U * 1024U * 1024U;

struct Header {
  std::uint32_t revision{};
  std::uint32_t headerSize{};
  std::uint64_t currentLba{};
  std::uint64_t backupLba{};
  std::uint64_t firstUsableLba{};
  std::uint64_t lastUsableLba{};
  std::array<std::byte, 16> diskGuid{};
  std::uint64_t entryArrayLba{};
  std::uint32_t entryCount{};
  std::uint32_t entrySize{};
  std::uint32_t entryArrayCrc{};
};

using HeaderResult = std::variant<Header, GptError>;

[[nodiscard]] constexpr std::uint16_t
readLe16(const std::span<const std::byte> bytes,
         const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(bytes[offset]) |
      (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t
readLe32(const std::span<const std::byte> bytes,
         const std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4U; ++index) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + index])
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] constexpr std::uint64_t
readLe64(const std::span<const std::byte> bytes,
         const std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8U; ++index) {
    value |= std::to_integer<std::uint64_t>(bytes[offset + index])
             << (index * 8U);
  }
  return value;
}

[[nodiscard]] std::uint32_t
crc32(const std::span<const std::byte> bytes) noexcept {
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

[[nodiscard]] constexpr bool
allZero(const std::span<const std::byte> bytes) noexcept {
  return std::ranges::all_of(bytes,
                             [](const std::byte value) { return value == std::byte{}; });
}

[[nodiscard]] GptError makeError(const GptErrorCode code,
                                 const std::uint64_t lba) noexcept {
  GptError error{};
  error.code = code;
  error.lba = lba;
  return error;
}

[[nodiscard]] HeaderResult parseHeader(
    const std::span<const std::byte> sector,
    const std::uint64_t expectedHeaderLba,
    const std::uint64_t deviceSectors) noexcept {
  if (deviceSectors < 68U || sector.size() != kSectorSize) {
    return makeError(GptErrorCode::invalidDeviceSize, expectedHeaderLba);
  }
  constexpr std::array signature{std::byte{'E'}, std::byte{'F'},
                                 std::byte{'I'}, std::byte{' '},
                                 std::byte{'P'}, std::byte{'A'},
                                 std::byte{'R'}, std::byte{'T'}};
  if (!std::ranges::equal(signature, sector.first(signature.size()))) {
    return makeError(GptErrorCode::invalidSignature, expectedHeaderLba);
  }
  const auto revision = readLe32(sector, 8U);
  if (revision != kGptRevision) {
    return makeError(GptErrorCode::unsupportedRevision, expectedHeaderLba);
  }
  const auto headerSize = readLe32(sector, 12U);
  if (headerSize < kMinimumHeaderSize || headerSize > kSectorSize) {
    return makeError(GptErrorCode::invalidHeaderSize, expectedHeaderLba);
  }
  const auto expectedCrc = readLe32(sector, 16U);
  std::array<std::byte, kSectorSize> headerBytes{};
  std::copy_n(sector.begin(), headerSize, headerBytes.begin());
  std::fill_n(headerBytes.begin() + 16, 4U, std::byte{});
  if (crc32(std::span{headerBytes}.first(headerSize)) != expectedCrc) {
    return makeError(GptErrorCode::invalidHeaderCrc, expectedHeaderLba);
  }

  Header header{
      .revision = revision,
      .headerSize = headerSize,
      .currentLba = readLe64(sector, 24U),
      .backupLba = readLe64(sector, 32U),
      .firstUsableLba = readLe64(sector, 40U),
      .lastUsableLba = readLe64(sector, 48U),
      .entryArrayLba = readLe64(sector, 72U),
      .entryCount = readLe32(sector, 80U),
      .entrySize = readLe32(sector, 84U),
      .entryArrayCrc = readLe32(sector, 88U),
  };
  std::copy_n(sector.begin() + 56, header.diskGuid.size(),
              header.diskGuid.begin());
  if (header.currentLba != expectedHeaderLba ||
      header.currentLba >= deviceSectors || header.backupLba >= deviceSectors ||
      header.backupLba == header.currentLba ||
      header.firstUsableLba > header.lastUsableLba ||
      header.lastUsableLba >= deviceSectors) {
    return makeError(GptErrorCode::invalidHeaderRange, expectedHeaderLba);
  }
  if (header.entryCount == 0U || header.entryCount > kMaximumEntryCount ||
      header.entrySize < kMinimumEntrySize || header.entrySize % 8U != 0U) {
    return makeError(GptErrorCode::invalidEntryLayout, expectedHeaderLba);
  }
  const std::uint64_t entryBytes =
      static_cast<std::uint64_t>(header.entryCount) * header.entrySize;
  if (entryBytes > kMaximumEntryArrayBytes) {
    return makeError(GptErrorCode::entryArrayTooLarge, expectedHeaderLba);
  }
  const std::uint64_t entrySectors =
      (entryBytes + core::kSectorSize - 1U) / core::kSectorSize;
  if (header.entryArrayLba >= deviceSectors ||
      entrySectors > deviceSectors - header.entryArrayLba) {
    return makeError(GptErrorCode::invalidEntryLayout, expectedHeaderLba);
  }
  return header;
}

[[nodiscard]] bool appendUtf8(std::string &output,
                              const std::uint32_t codePoint) {
  if (codePoint == 0U || codePoint > 0x10ffffU ||
      (codePoint >= 0xd800U && codePoint <= 0xdfffU)) {
    return false;
  }
  if (codePoint <= 0x7fU) {
    output.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  } else if (codePoint <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
    output.push_back(
        static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
    output.push_back(
        static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
    output.push_back(
        static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
  }
  return true;
}

[[nodiscard]] std::optional<std::string>
decodeName(const std::span<const std::byte> bytes) {
  std::string result;
  for (std::size_t offset = 0; offset + 1U < bytes.size(); offset += 2U) {
    const std::uint16_t first = readLe16(bytes, offset);
    if (first == 0U) {
      return result;
    }
    std::uint32_t codePoint = first;
    if (first >= 0xd800U && first <= 0xdbffU) {
      if (offset + 3U >= bytes.size()) {
        return std::nullopt;
      }
      const std::uint16_t second = readLe16(bytes, offset + 2U);
      if (second < 0xdc00U || second > 0xdfffU) {
        return std::nullopt;
      }
      codePoint = 0x10000U +
                  ((static_cast<std::uint32_t>(first) - 0xd800U) << 10U) +
                  (static_cast<std::uint32_t>(second) - 0xdc00U);
      offset += 2U;
    } else if (first >= 0xdc00U && first <= 0xdfffU) {
      return std::nullopt;
    }
    if (!appendUtf8(result, codePoint)) {
      return std::nullopt;
    }
  }
  return result;
}

using ReadBytesResult = std::variant<std::vector<std::byte>, GptError>;

[[nodiscard]] ReadBytesResult readSectors(session::RockusbSession &rockusb,
                                          const std::uint64_t firstLba,
                                          const std::uint64_t count) {
  if (count == 0U || count >
                         std::numeric_limits<std::size_t>::max() /
                             kSectorSize) {
    return makeError(GptErrorCode::invalidEntryLayout, firstLba);
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(count) * kSectorSize);
  std::uint64_t completed = 0;
  while (completed < count) {
    const std::uint64_t chunk = std::min<std::uint64_t>(
        session::kMaximumLbaSectorsPerCommand, count - completed);
    auto destination = std::span{bytes}.subspan(
        static_cast<std::size_t>(completed) * kSectorSize,
        static_cast<std::size_t>(chunk) * kSectorSize);
    const auto result = session::readLba(
        rockusb, core::Lba{firstLba + completed}, core::SectorCount{chunk},
        destination);
    if (const auto *error = std::get_if<session::SessionError>(&result)) {
      auto gptError = makeError(GptErrorCode::sessionFailure,
                                firstLba + completed);
      gptError.sessionError = *error;
      return gptError;
    }
    completed += chunk;
  }
  return bytes;
}

[[nodiscard]] GptPartitionTableResult readCopy(
    session::RockusbSession &rockusb, const std::uint64_t headerLba,
    const std::uint64_t deviceSectors) {
  auto headerRead = readSectors(rockusb, headerLba, 1U);
  if (const auto *error = std::get_if<GptError>(&headerRead)) {
    return *error;
  }
  const auto &headerBytes = std::get<std::vector<std::byte>>(headerRead);
  auto parsedHeader = parseHeader(headerBytes, headerLba, deviceSectors);
  if (const auto *error = std::get_if<GptError>(&parsedHeader)) {
    return *error;
  }
  const auto &header = std::get<Header>(parsedHeader);
  const std::uint64_t bytes =
      static_cast<std::uint64_t>(header.entryCount) * header.entrySize;
  const std::uint64_t sectors =
      (bytes + core::kSectorSize - 1U) / core::kSectorSize;
  auto entries = readSectors(rockusb, header.entryArrayLba, sectors);
  if (const auto *error = std::get_if<GptError>(&entries)) {
    return *error;
  }
  return parseGptCopy(headerBytes,
                      std::get<std::vector<std::byte>>(entries), headerLba,
                      deviceSectors);
}

} // namespace

GptPartitionTableResult parseGptCopy(
    const std::span<const std::byte> headerSector,
    const std::span<const std::byte> entryBytes,
    const std::uint64_t expectedHeaderLba,
    const std::uint64_t deviceSectors) noexcept {
  auto parsedHeader =
      parseHeader(headerSector, expectedHeaderLba, deviceSectors);
  if (const auto *error = std::get_if<GptError>(&parsedHeader)) {
    return *error;
  }
  const auto &header = std::get<Header>(parsedHeader);
  const std::uint64_t requiredBytes64 =
      static_cast<std::uint64_t>(header.entryCount) * header.entrySize;
  if (requiredBytes64 > entryBytes.size()) {
    return makeError(GptErrorCode::invalidEntryLayout, expectedHeaderLba);
  }
  const auto requiredBytes = static_cast<std::size_t>(requiredBytes64);
  if (crc32(entryBytes.first(requiredBytes)) != header.entryArrayCrc) {
    return makeError(GptErrorCode::invalidEntryArrayCrc, header.entryArrayLba);
  }

  GptPartitionTable table{
      .revision = header.revision,
      .currentLba = header.currentLba,
      .backupLba = header.backupLba,
      .firstUsableLba = header.firstUsableLba,
      .lastUsableLba = header.lastUsableLba,
      .diskGuid = header.diskGuid,
      .entryArrayLba = header.entryArrayLba,
      .entryCount = header.entryCount,
      .entrySize = header.entrySize,
      .partitions = {},
      .backupStatus = BackupGptStatus::notChecked,
      .backupError = std::nullopt,
  };
  for (std::uint32_t index = 0; index < header.entryCount; ++index) {
    const auto entry = entryBytes.subspan(
        static_cast<std::size_t>(index) * header.entrySize, header.entrySize);
    if (allZero(entry.first(16U))) {
      continue;
    }
    const std::uint64_t firstLba = readLe64(entry, 32U);
    const std::uint64_t lastLba = readLe64(entry, 40U);
    if (firstLba < header.firstUsableLba || firstLba > lastLba ||
        lastLba > header.lastUsableLba) {
      return makeError(GptErrorCode::invalidPartitionRange,
                       header.entryArrayLba);
    }
    const auto decoded = decodeName(entry.subspan(56U));
    if (!decoded.has_value() || decoded->empty()) {
      return makeError(GptErrorCode::invalidPartitionName,
                       header.entryArrayLba);
    }
    GptPartition partition{
        .name = *decoded,
        .firstLba = firstLba,
        .lastLba = lastLba,
        .attributes = readLe64(entry, 48U),
    };
    std::copy_n(entry.begin(), 16U, partition.typeGuid.begin());
    std::copy_n(entry.begin() + 16, 16U, partition.uniqueGuid.begin());
    table.partitions.push_back(std::move(partition));
  }
  return table;
}

GptPartitionTableResult
readDeviceGpt(session::RockusbSession &rockusb,
              const std::uint64_t deviceSectors,
              const bool checkBackup) noexcept {
  auto primary = readCopy(rockusb, 1U, deviceSectors);
  if (const auto *error = std::get_if<GptError>(&primary)) {
    return *error;
  }
  auto table = std::get<GptPartitionTable>(std::move(primary));
  if (!checkBackup) {
    return table;
  }
  auto backup = readCopy(rockusb, table.backupLba, deviceSectors);
  if (const auto *error = std::get_if<GptError>(&backup)) {
    table.backupStatus = BackupGptStatus::unavailable;
    table.backupError = error->code;
    return table;
  }
  const auto &backupTable = std::get<GptPartitionTable>(backup);
  const bool matches = backupTable.backupLba == table.currentLba &&
                       backupTable.currentLba == table.backupLba &&
                       backupTable.diskGuid == table.diskGuid &&
                       backupTable.firstUsableLba == table.firstUsableLba &&
                       backupTable.lastUsableLba == table.lastUsableLba &&
                       backupTable.partitions == table.partitions;
  table.backupStatus = matches ? BackupGptStatus::verified
                               : BackupGptStatus::unavailable;
  if (!matches) {
    table.backupError = GptErrorCode::invalidHeaderRange;
  }
  return table;
}

std::string_view describeGptError(const GptErrorCode code) noexcept {
  switch (code) {
  case GptErrorCode::sessionFailure:
    return "the GPT could not be read from eMMC";
  case GptErrorCode::invalidDeviceSize:
    return "the device is too small for GPT";
  case GptErrorCode::invalidSignature:
    return "the GPT signature is invalid";
  case GptErrorCode::unsupportedRevision:
    return "the GPT revision is unsupported";
  case GptErrorCode::invalidHeaderSize:
    return "the GPT header size is invalid";
  case GptErrorCode::invalidHeaderCrc:
    return "the GPT header CRC is invalid";
  case GptErrorCode::invalidHeaderRange:
    return "the GPT header contains an invalid LBA range";
  case GptErrorCode::invalidEntryLayout:
    return "the GPT entry layout is invalid";
  case GptErrorCode::entryArrayTooLarge:
    return "the GPT entry array exceeds the safety limit";
  case GptErrorCode::invalidEntryArrayCrc:
    return "the GPT entry-array CRC is invalid";
  case GptErrorCode::invalidPartitionRange:
    return "a GPT partition range is invalid";
  case GptErrorCode::invalidPartitionName:
    return "a GPT partition name is invalid";
  }
  return "unknown GPT error";
}

std::string_view toString(const BackupGptStatus status) noexcept {
  switch (status) {
  case BackupGptStatus::notChecked:
    return "not-checked";
  case BackupGptStatus::verified:
    return "verified";
  case BackupGptStatus::unavailable:
    return "unavailable";
  }
  return "unknown";
}

} // namespace zzrk::storage
