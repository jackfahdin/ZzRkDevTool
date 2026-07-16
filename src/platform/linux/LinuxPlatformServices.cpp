// SPDX-License-Identifier: MIT

#include <zzrk/platform/PlatformServices.hpp>

namespace zzrk::platform {
namespace {

class LinuxPlatformServices final : public PlatformServices {
  public:
    [[nodiscard]] std::string_view platformName() const noexcept override
    {
        return "Linux";
    }

    [[nodiscard]] std::string usbAccessHelp() const override
    {
        return "Check the Rockchip udev rule and reconnect the device; do not "
               "run the application as root.";
    }
};

} // namespace

std::unique_ptr<PlatformServices> createPlatformServices()
{
    return std::make_unique<LinuxPlatformServices>();
}

} // namespace zzrk::platform
