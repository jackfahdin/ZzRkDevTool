// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/RockusbSession.hpp>

#include <cstdint>

namespace zzrk::session {

inline constexpr std::uint64_t kMaximumEraseLbaSectorsPerCommand = 32U * 1024U;

[[nodiscard]] SessionResult
eraseLba(RockusbSession& session, core::Lba firstLba,
         core::SectorCount sectorCount) noexcept;

} // namespace zzrk::session
