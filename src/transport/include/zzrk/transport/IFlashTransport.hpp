// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/session/IUsbTransport.hpp>

#include <cstdint>

namespace zzrk::transport {

struct FlashInterfaceInfo {
    std::uint8_t interfaceNumber{};
    std::uint8_t alternateSetting{};
    std::uint8_t bulkInEndpoint{};
    std::uint8_t bulkOutEndpoint{};
    std::uint16_t bulkInMaxPacketSize{};
    std::uint16_t bulkOutMaxPacketSize{};

    friend constexpr bool operator==(const FlashInterfaceInfo&,
                                     const FlashInterfaceInfo&) = default;
};

class IFlashTransport : public session::IUsbTransport {
  public:
    ~IFlashTransport() override = default;

    [[nodiscard]] virtual const FlashInterfaceInfo&
    interfaceInfo() const noexcept = 0;

    [[nodiscard]] virtual int close() noexcept = 0;
};

} // namespace zzrk::transport
