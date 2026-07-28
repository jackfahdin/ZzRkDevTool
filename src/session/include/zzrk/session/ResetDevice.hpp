// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <cstdint>

namespace zzrk::session {

enum class ResetMode : std::uint8_t {
    normal = 0,
    massStorage = 1,
    powerOff = 2,
    maskrom = 3,
    disconnect = 4,
};

[[nodiscard]] SessionResult
resetDevice(RockusbSession& session,
            ResetMode mode = ResetMode::normal) noexcept;

} // namespace zzrk::session
