// SPDX-License-Identifier: MIT

#include <zzrk/session/ReadFlashId.hpp>

#include "RockusbSessionExecutor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kReadFlashIdOpcode = 0x01;
constexpr std::uint8_t kReadFlashIdCommandLength = 6;

constexpr std::array<std::byte, kFlashIdSize> kEmmcMediaMarker{
    std::byte{0x45},
    std::byte{0x4d},
    std::byte{0x4d},
    std::byte{0x43},
    std::byte{0x20},
};

} // namespace

FlashIdKind classifyFlashId(const FlashId& flashId) noexcept
{
    return flashId.rawBytes == kEmmcMediaMarker
               ? FlashIdKind::emmcMediaMarker
               : FlashIdKind::opaque;
}

ReadFlashIdResult readFlashId(RockusbSession& session) noexcept
{
    detail::RockusbReadRequest request{
        .logicalUnit = 0,
        .commandLength = kReadFlashIdCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kReadFlashIdOpcode);

    std::array<std::byte, kFlashIdSize> rawBytes{};
    const auto result = detail::RockusbSessionExecutor::executeRead(
        session, request, rawBytes);
    if (const auto* error = std::get_if<SessionError>(&result)) {
        return *error;
    }

    return FlashId{.rawBytes = rawBytes};
}

} // namespace zzrk::session
