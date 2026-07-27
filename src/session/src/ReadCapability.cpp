// SPDX-License-Identifier: MIT

#include <zzrk/session/ReadCapability.hpp>

#include "RockusbSessionExecutor.hpp"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace zzrk::session {
namespace {

constexpr std::uint8_t kReadCapabilityOpcode = 0xaa;
constexpr std::uint8_t kReadCapabilityCommandLength = 6;

} // namespace

ReadCapabilityResult readCapability(RockusbSession& session) noexcept
{
    detail::RockusbReadRequest request{
        .logicalUnit = 0,
        .commandLength = kReadCapabilityCommandLength,
    };
    request.commandBlock[0] = static_cast<std::byte>(kReadCapabilityOpcode);

    Capability capability{};
    const auto result = detail::RockusbSessionExecutor::executeRead(
        session, request, capability.rawBytes);
    if (const auto* error = std::get_if<SessionError>(&result)) {
        return *error;
    }
    return capability;
}

} // namespace zzrk::session
