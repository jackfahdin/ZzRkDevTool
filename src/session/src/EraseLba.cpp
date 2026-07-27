// SPDX-License-Identifier: MIT

#include <zzrk/session/EraseLba.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstdint>
#include <limits>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kEraseLbaOpcode = 0x25;
constexpr std::uint8_t kEraseLbaCommandLength = 10;

constexpr void writeBe32(
    std::array<std::byte, protocol::kRockusbCommandBlockSize>& bytes,
    const std::size_t offset, const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> ((3U - index) * 8U)) & 0xffU);
    }
}

constexpr void writeBe16(
    std::array<std::byte, protocol::kRockusbCommandBlockSize>& bytes,
    const std::size_t offset, const std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>(value & 0xffU);
}

[[nodiscard]] constexpr SessionError requestError(
    const SessionErrorCode code) noexcept
{
    SessionError error{};
    error.code = code;
    error.stage = SessionStage::request;
    return error;
}

} // namespace

SessionResult eraseLba(RockusbSession& session, const core::Lba firstLba,
                       const core::SectorCount sectorCount) noexcept
{
    if (firstLba.value > std::numeric_limits<std::uint32_t>::max()) {
        return requestError(SessionErrorCode::invalidAddress);
    }
    if (sectorCount.value == 0U
        || sectorCount.value > kMaximumEraseLbaSectorsPerCommand
        || sectorCount.value > std::numeric_limits<std::uint16_t>::max()) {
        return requestError(SessionErrorCode::invalidSectorCount);
    }

    detail::RockusbNoDataRequest request{
        .logicalUnit = 0,
        .commandLength = kEraseLbaCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kEraseLbaOpcode);
    writeBe32(request.commandBlock, 2,
              static_cast<std::uint32_t>(firstLba.value));
    writeBe16(request.commandBlock, 7,
              static_cast<std::uint16_t>(sectorCount.value));
    return detail::RockusbSessionExecutor::executeNoData(session, request);
}

} // namespace zzrk::session
