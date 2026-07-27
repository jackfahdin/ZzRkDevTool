// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/RockusbSession.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace zzrk::session::detail {

struct RockusbReadRequest {
    std::uint8_t logicalUnit{};
    std::uint8_t commandLength{};
    std::array<std::byte, protocol::kRockusbCommandBlockSize> commandBlock{};
};

struct RockusbWriteRequest {
    std::uint8_t logicalUnit{};
    std::uint8_t commandLength{};
    std::array<std::byte, protocol::kRockusbCommandBlockSize> commandBlock{};
};

struct RockusbNoDataRequest {
    std::uint8_t logicalUnit{};
    std::uint8_t commandLength{};
    std::array<std::byte, protocol::kRockusbCommandBlockSize> commandBlock{};
};

class RockusbSessionExecutor final {
  public:
    // On any error, destination can contain partial or untrusted data.
    [[nodiscard]] static SessionResult
    executeRead(RockusbSession& session, const RockusbReadRequest& request,
                std::span<std::byte> destination) noexcept;

    [[nodiscard]] static SessionResult
    executeWrite(RockusbSession& session, const RockusbWriteRequest& request,
                 std::span<const std::byte> source) noexcept;

    [[nodiscard]] static SessionResult
    executeNoData(RockusbSession& session,
                  const RockusbNoDataRequest& request) noexcept;
};

} // namespace zzrk::session::detail
