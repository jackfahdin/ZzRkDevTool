// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <variant>

namespace zzrk::session {

inline constexpr std::size_t kCapabilitySize = 8;

struct Capability {
    std::array<std::byte, kCapabilitySize> rawBytes{};

    friend constexpr bool operator==(const Capability&,
                                     const Capability&) = default;
};

using ReadCapabilityResult = std::variant<Capability, SessionError>;

[[nodiscard]] ReadCapabilityResult
readCapability(RockusbSession& session) noexcept;

} // namespace zzrk::session
