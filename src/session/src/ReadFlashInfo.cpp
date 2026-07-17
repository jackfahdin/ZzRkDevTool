// SPDX-License-Identifier: MIT

#include <zzrk/session/ReadFlashInfo.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kReadFlashInfoOpcode = 0x1a;
constexpr std::uint8_t kReadFlashInfoCommandLength = 6;

[[nodiscard]] constexpr std::uint16_t
decodeLe16(const std::array<std::byte, kFlashInfoSize>& bytes,
           const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(bytes[offset])
        | (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U));
}

[[nodiscard]] constexpr std::uint32_t
decodeLe32(const std::array<std::byte, kFlashInfoSize>& bytes,
           const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset])
           | (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U)
           | (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

} // namespace

ReadFlashInfoResult readFlashInfo(RockusbSession& session) noexcept
{
    detail::RockusbReadRequest request{
        .logicalUnit = 0,
        .commandLength = kReadFlashInfoCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kReadFlashInfoOpcode);

    std::array<std::byte, kFlashInfoSize> rawBytes{};
    const auto result = detail::RockusbSessionExecutor::executeRead(
        session, request, rawBytes);
    if (const auto* error = std::get_if<SessionError>(&result)) {
        return *error;
    }

    FlashInfo flashInfo{
        .rawBytes = rawBytes,
        .legacy = LegacyFlashInfoFields{
            .sectorCount = decodeLe32(rawBytes, 0),
            .blockSizeCode = decodeLe16(rawBytes, 4),
            .pageSizeCode = std::to_integer<std::uint8_t>(rawBytes[6]),
            .eccBits = std::to_integer<std::uint8_t>(rawBytes[7]),
            .accessTime = std::to_integer<std::uint8_t>(rawBytes[8]),
            .manufacturerCode = std::to_integer<std::uint8_t>(rawBytes[9]),
            .flashMask = std::to_integer<std::uint8_t>(rawBytes[10]),
        },
    };
    return flashInfo;
}

} // namespace zzrk::session
