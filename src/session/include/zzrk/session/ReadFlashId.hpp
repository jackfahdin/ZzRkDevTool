// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <variant>

namespace zzrk::session {

inline constexpr std::size_t kFlashIdSize = 5;

// Only the exact wire marker "EMMC " is classified. Every other value stays
// opaque, including IDs emitted by older NAND Loaders.
enum class FlashIdKind {
    opaque,
    emmcMediaMarker,
};

struct FlashId {
    std::array<std::byte, kFlashIdSize> rawBytes{};

    friend constexpr bool operator==(const FlashId&, const FlashId&) = default;
};

[[nodiscard]] FlashIdKind classifyFlashId(const FlashId& flashId) noexcept;

using ReadFlashIdResult = std::variant<FlashId, SessionError>;

// The session owner must bind this operation only to a device identity and
// Loader revision validated for ReadFlashId.
[[nodiscard]] ReadFlashIdResult
readFlashId(RockusbSession& session) noexcept;

} // namespace zzrk::session
