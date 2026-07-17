// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <variant>

namespace zzrk::session {

inline constexpr std::size_t kChipInfoSize = 16;

struct ChipInfo {
    std::array<std::byte, kChipInfoSize> bytes{};

    friend constexpr bool operator==(const ChipInfo&, const ChipInfo&) = default;
};

using ReadChipInfoResult = std::variant<ChipInfo, SessionError>;

// The session owner must bind this operation only to a device identity and
// Loader revision validated for ReadChipInfo.
[[nodiscard]] ReadChipInfoResult
readChipInfo(RockusbSession& session) noexcept;

} // namespace zzrk::session
