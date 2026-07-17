// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace zzrk::protocol {

inline constexpr std::size_t kRockusbCommandSize = 31;
inline constexpr std::size_t kRockusbStatusSize = 13;
inline constexpr std::size_t kRockusbCommandBlockSize = 16;

using RockusbCommandBytes = std::array<std::byte, kRockusbCommandSize>;
using RockusbStatusBytes = std::array<std::byte, kRockusbStatusSize>;

enum class DataDirection : std::uint8_t {
    hostToDevice = 0x00,
    deviceToHost = 0x80,
};

struct RockusbCommand {
    std::uint32_t tag{};
    std::uint32_t transferLength{};
    DataDirection direction{DataDirection::hostToDevice};
    std::uint8_t logicalUnit{};
    std::uint8_t commandLength{};
    std::array<std::byte, kRockusbCommandBlockSize> commandBlock{};
};

enum class CommandEncodeErrorCode {
    invalidLogicalUnit,
    invalidCommandLength,
    invalidDirection,
};

struct CommandEncodeError {
    CommandEncodeErrorCode code{};
    std::uint8_t value{};

    friend constexpr bool operator==(const CommandEncodeError&,
                                     const CommandEncodeError&) = default;
};

using CommandEncodeResult =
    std::variant<RockusbCommandBytes, CommandEncodeError>;

[[nodiscard]] CommandEncodeResult
encodeCommand(const RockusbCommand& command) noexcept;

struct RockusbStatus {
    std::uint32_t tag{};
    std::uint32_t dataResidue{};

    friend constexpr bool operator==(const RockusbStatus&,
                                     const RockusbStatus&) = default;
};

enum class StatusDecodeErrorCode {
    unexpectedSize,
    invalidSignature,
    tagMismatch,
    commandFailed,
    phaseError,
    invalidStatus,
};

struct StatusDecodeError {
    StatusDecodeErrorCode code{};
    std::size_t actualSize{};
    std::uint32_t expectedTag{};
    std::uint32_t actualTag{};
    std::uint32_t actualSignature{};
    std::uint32_t dataResidue{};
    std::uint8_t rawStatus{};

    friend constexpr bool operator==(const StatusDecodeError&,
                                     const StatusDecodeError&) = default;
};

using StatusDecodeResult = std::variant<RockusbStatus, StatusDecodeError>;

[[nodiscard]] StatusDecodeResult
decodeStatus(std::span<const std::byte> bytes,
             std::uint32_t expectedTag) noexcept;

} // namespace zzrk::protocol
