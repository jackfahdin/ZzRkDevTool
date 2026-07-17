// SPDX-License-Identifier: MIT

#include <zzrk/session/ReadChipInfo.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kReadChipInfoOpcode = 0x1b;
constexpr std::uint8_t kReadChipInfoCommandLength = 6;

} // namespace

ReadChipInfoResult readChipInfo(RockusbSession& session) noexcept
{
    detail::RockusbReadRequest request{
        .logicalUnit = 0,
        .commandLength = kReadChipInfoCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kReadChipInfoOpcode);

    ChipInfo chipInfo{};
    const auto result = detail::RockusbSessionExecutor::executeRead(
        session, request, chipInfo.bytes);
    if (const auto* error = std::get_if<SessionError>(&result)) {
        return *error;
    }
    return chipInfo;
}

} // namespace zzrk::session
