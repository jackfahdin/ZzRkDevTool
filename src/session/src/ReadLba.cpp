// SPDX-License-Identifier: MIT

#include <zzrk/session/ReadLba.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstdint>
#include <limits>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kReadLbaOpcode = 0x14;
constexpr std::uint8_t kReadLbaCommandLength = 10;

constexpr void writeBe32(std::array<std::byte,
                                   protocol::kRockusbCommandBlockSize>& bytes,
                         const std::size_t offset,
                         const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> ((3U - index) * 8U)) & 0xffU);
    }
}

constexpr void writeBe16(std::array<std::byte,
                                   protocol::kRockusbCommandBlockSize>& bytes,
                         const std::size_t offset,
                         const std::uint16_t value) noexcept
{
    bytes[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 1U] = static_cast<std::byte>(value & 0xffU);
}

[[nodiscard]] constexpr SessionError requestError(
    const SessionErrorCode code, const std::size_t expected = 0,
    const std::size_t actual = 0) noexcept
{
    return SessionError{
        .code = code,
        .stage = SessionStage::request,
        .expectedBytes = expected,
        .actualBytes = actual,
        .timeout = {},
        .residue = 0,
        .transportError = std::nullopt,
        .commandEncodeError = std::nullopt,
        .statusDecodeError = std::nullopt,
    };
}

} // namespace

SessionResult readLba(RockusbSession& session, const core::Lba firstLba,
                      const core::SectorCount sectorCount,
                      const std::span<std::byte> destination) noexcept
{
    if (firstLba.value > std::numeric_limits<std::uint32_t>::max()) {
        return requestError(SessionErrorCode::invalidAddress);
    }
    if (sectorCount.value == 0U
        || sectorCount.value > kMaximumLbaSectorsPerCommand
        || sectorCount.value > std::numeric_limits<std::uint16_t>::max()) {
        return requestError(SessionErrorCode::invalidSectorCount);
    }
    const auto expectedBytes = core::toBytes(sectorCount);
    if (!expectedBytes.has_value()
        || expectedBytes->value
               > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max())
        || destination.size()
               != static_cast<std::size_t>(expectedBytes->value)) {
        return requestError(
            SessionErrorCode::bufferSizeMismatch,
            expectedBytes.has_value()
                ? static_cast<std::size_t>(expectedBytes->value)
                : 0U,
            destination.size());
    }

    detail::RockusbReadRequest request{
        .logicalUnit = 0,
        .commandLength = kReadLbaCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kReadLbaOpcode);
    writeBe32(request.commandBlock, 2,
              static_cast<std::uint32_t>(firstLba.value));
    writeBe16(request.commandBlock, 7,
              static_cast<std::uint16_t>(sectorCount.value));
    return detail::RockusbSessionExecutor::executeRead(session, request,
                                                       destination);
}

} // namespace zzrk::session
