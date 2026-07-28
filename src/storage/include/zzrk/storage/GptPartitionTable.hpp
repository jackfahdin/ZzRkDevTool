// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace zzrk::storage {

enum class GptErrorCode {
  sessionFailure,
  invalidDeviceSize,
  invalidSignature,
  unsupportedRevision,
  invalidHeaderSize,
  invalidHeaderCrc,
  invalidHeaderRange,
  invalidEntryLayout,
  entryArrayTooLarge,
  invalidEntryArrayCrc,
  invalidPartitionRange,
  invalidPartitionName,
};

struct GptError {
  GptErrorCode code{};
  std::uint64_t lba{};
  std::optional<session::SessionError> sessionError;
};

struct GptPartition {
  std::string name;
  std::array<std::byte, 16> typeGuid{};
  std::array<std::byte, 16> uniqueGuid{};
  std::uint64_t firstLba{};
  std::uint64_t lastLba{};
  std::uint64_t attributes{};

  friend constexpr bool operator==(const GptPartition &,
                                   const GptPartition &) = default;

  [[nodiscard]] constexpr std::uint64_t sectorCount() const noexcept {
    return lastLba - firstLba + 1U;
  }
};

enum class BackupGptStatus {
  notChecked,
  verified,
  unavailable,
};

struct GptPartitionTable {
  std::uint32_t revision{};
  std::uint64_t currentLba{};
  std::uint64_t backupLba{};
  std::uint64_t firstUsableLba{};
  std::uint64_t lastUsableLba{};
  std::array<std::byte, 16> diskGuid{};
  std::uint64_t entryArrayLba{};
  std::uint32_t entryCount{};
  std::uint32_t entrySize{};
  std::vector<GptPartition> partitions;
  BackupGptStatus backupStatus{BackupGptStatus::notChecked};
  std::optional<GptErrorCode> backupError;
};

using GptPartitionTableResult = std::variant<GptPartitionTable, GptError>;

// Parses one GPT copy. headerSector must contain exactly the sector at
// expectedHeaderLba. entryBytes starts at the entry-array LBA declared by the
// header and may include sector padding after the declared array.
[[nodiscard]] GptPartitionTableResult parseGptCopy(
    std::span<const std::byte> headerSector,
    std::span<const std::byte> entryBytes, std::uint64_t expectedHeaderLba,
    std::uint64_t deviceSectors) noexcept;

// Reads and validates the primary GPT. Backup validation is optional because
// the validated RK3588 Loader may return opaque data for disk-tail reads.
[[nodiscard]] GptPartitionTableResult
readDeviceGpt(session::RockusbSession &session, std::uint64_t deviceSectors,
              bool checkBackup) noexcept;

[[nodiscard]] std::string_view describeGptError(GptErrorCode code) noexcept;
[[nodiscard]] std::string_view toString(BackupGptStatus status) noexcept;

} // namespace zzrk::storage
