// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/core/StorageUnits.hpp>
#include <zzrk/session/RockusbSession.hpp>

#include <cstddef>
#include <span>

namespace zzrk::session {

inline constexpr std::uint64_t kMaximumLbaSectorsPerCommand = 128;

[[nodiscard]] SessionResult readLba(RockusbSession& session,
                                    core::Lba firstLba,
                                    core::SectorCount sectorCount,
                                    std::span<std::byte> destination) noexcept;

} // namespace zzrk::session
