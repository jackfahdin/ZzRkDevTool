// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace zzrk::session {

inline constexpr std::size_t kFlashInfoSize = 11;

struct LegacyFlashInfoFields {
    std::uint32_t sectorCount{};
    std::uint16_t blockSizeCode{};
    std::uint8_t pageSizeCode{};
    std::uint8_t eccBits{};
    std::uint8_t accessTime{};
    std::uint8_t manufacturerCode{};
    std::uint8_t flashMask{};

    friend constexpr bool operator==(const LegacyFlashInfoFields&,
                                     const LegacyFlashInfoFields&) = default;
};

struct FlashInfo {
    std::array<std::byte, kFlashInfoSize> rawBytes{};
    LegacyFlashInfoFields legacy;

    friend constexpr bool operator==(const FlashInfo&, const FlashInfo&) =
        default;
};

using ReadFlashInfoResult = std::variant<FlashInfo, SessionError>;

// The session owner must bind this operation only to a device identity and
// Loader revision validated for ReadFlashInfo.
[[nodiscard]] ReadFlashInfoResult
readFlashInfo(RockusbSession& session) noexcept;

} // namespace zzrk::session
