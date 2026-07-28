// SPDX-License-Identifier: MIT

#include <zzrk/session/ResetDevice.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstdint>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kResetDeviceOpcode = 0xffU;
constexpr std::uint8_t kResetDeviceCommandLength = 6U;

} // namespace

SessionResult resetDevice(RockusbSession& session,
                          const ResetMode mode) noexcept
{
    detail::RockusbNoDataRequest request{
        .logicalUnit = 0,
        .commandLength = kResetDeviceCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kResetDeviceOpcode);
    request.commandBlock[1] = static_cast<std::byte>(mode);
    return detail::RockusbSessionExecutor::executeNoData(session, request);
}

} // namespace zzrk::session
