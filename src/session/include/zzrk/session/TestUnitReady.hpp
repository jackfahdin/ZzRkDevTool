// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

namespace zzrk::session {

[[nodiscard]] SessionResult testUnitReady(RockusbSession &session) noexcept;

} // namespace zzrk::session
