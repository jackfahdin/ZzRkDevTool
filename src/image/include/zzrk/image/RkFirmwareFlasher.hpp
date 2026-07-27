// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/session/RockusbSession.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace zzrk::image {

enum class FirmwareFlashPhase {
    writePartitionTable,
    verifyPartitionTable,
    erasePartition,
    writePartition,
};

struct FirmwareFlashProgress {
    FirmwareFlashPhase phase{};
    std::string target;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
};

using FirmwareFlashProgressCallback =
    std::function<void(const FirmwareFlashProgress&)>;

struct FirmwareFlashOptions {
    bool writePartitionTable{true};
    bool verifyPartitionTable{true};
    bool eraseSparsePartitions{true};
};

enum class FirmwareFlashErrorCode {
    invalidPlan,
    deviceTooSmall,
    addressOverflow,
    imageOpenFailed,
    imageSeekFailed,
    imageReadFailed,
    unsupportedPayload,
    partitionTableGenerationFailed,
    eraseFailed,
    writeFailed,
    verifyFailed,
};

struct FirmwareFlashError {
    FirmwareFlashErrorCode code{};
    std::string target;
    std::uint64_t completedBytes{};
    std::optional<session::SessionError> sessionError;
};

struct FirmwareFlashSuccess {
    std::uint64_t partitionBytesWritten{};
    std::uint64_t sectorsErased{};
    bool partitionTableWritten{};
    bool partitionTableVerified{};
    bool loaderPreserved{true};
};

using FirmwareFlashResult =
    std::variant<FirmwareFlashSuccess, FirmwareFlashError>;

[[nodiscard]] FirmwareFlashResult flashRkFirmwareImage(
    session::RockusbSession& session, const RkFirmwareImage& image,
    const RkFirmwareFlashPlan& plan, std::uint64_t deviceSectors,
    const FirmwareFlashOptions& options = {},
    const FirmwareFlashProgressCallback& progress = {});

} // namespace zzrk::image
