// SPDX-License-Identifier: MIT

#include <zzrk/platform/PlatformServices.hpp>

namespace zzrk::platform {
namespace {

class WindowsPlatformServices final : public PlatformServices {
  public:
    [[nodiscard]] std::string_view platformName() const noexcept override
    {
        return "Windows";
    }

    [[nodiscard]] std::string usbAccessHelp() const override
    {
        return "Check that a supported WinUSB/libusb driver is installed.";
    }
};

} // namespace

std::unique_ptr<PlatformServices> createPlatformServices()
{
    return std::make_unique<WindowsPlatformServices>();
}

} // namespace zzrk::platform
