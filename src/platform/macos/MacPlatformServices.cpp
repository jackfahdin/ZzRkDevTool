// SPDX-License-Identifier: MIT

#include <zzrk/platform/PlatformServices.hpp>

namespace zzrk::platform {
namespace {

class MacPlatformServices final : public PlatformServices {
  public:
    [[nodiscard]] std::string_view platformName() const noexcept override
    {
        return "macOS";
    }

    [[nodiscard]] std::string usbAccessHelp() const override
    {
        return "Reconnect the device and check whether another application "
               "owns the USB interface.";
    }
};

} // namespace

std::unique_ptr<PlatformServices> createPlatformServices()
{
    return std::make_unique<MacPlatformServices>();
}

} // namespace zzrk::platform
