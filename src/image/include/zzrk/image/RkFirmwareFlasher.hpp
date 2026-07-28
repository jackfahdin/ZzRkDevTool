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
    verifyPartition,
    writeLoader,
    verifyLoader,
};

struct FirmwareFlashProgress {
    FirmwareFlashPhase phase{};
    std::string target;
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
};

using FirmwareFlashProgressCallback =
    std::function<void(const FirmwareFlashProgress&)>;
using FirmwareFlashCancellationCallback = std::function<bool()>;

enum class FirmwareVerifyMode {
    none,
    critical,
    all,
};

struct FirmwareFlashOptions {
    bool writePartitionTable{true};
    bool eraseSparsePartitions{true};
    bool upgradeLoader{true};
    FirmwareVerifyMode verifyMode{FirmwareVerifyMode::critical};
};

enum class FirmwareFlashErrorCode {
    invalidPlan,
    deviceTooSmall,
    addressOverflow,
    imageOpenFailed,
    imageSeekFailed,
    imageReadFailed,
    unsupportedPayload,
    invalidLoader,
    loaderUnsupported,
    partitionTableGenerationFailed,
    eraseFailed,
    writeFailed,
    verifyFailed,
    cancelled,
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
    std::uint32_t partitionsVerified{};
    bool partitionTableWritten{};
    bool partitionTableVerified{};
    bool loaderWritten{};
    bool loaderVerified{};
};

using FirmwareFlashResult =
    std::variant<FirmwareFlashSuccess, FirmwareFlashError>;

[[nodiscard]] FirmwareFlashResult flashRkFirmwareImage(
    session::RockusbSession& session, const RkFirmwareImage& image,
    const RkFirmwareFlashPlan& plan, std::uint64_t deviceSectors,
    const FirmwareFlashOptions& options = {},
    const FirmwareFlashProgressCallback& progress = {},
    const FirmwareFlashCancellationCallback& cancellation = {});

} // namespace zzrk::image
