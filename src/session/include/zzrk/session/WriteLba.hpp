// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/RockusbSession.hpp>

#include <cstdint>
#include <span>

namespace zzrk::session {

inline constexpr std::uint64_t kMaximumWriteLbaSectorsPerCommand = 128;

[[nodiscard]] SessionResult
writeLba(RockusbSession& session, core::Lba firstLba,
         core::SectorCount sectorCount,
         std::span<const std::byte> source) noexcept;

} // namespace zzrk::session
