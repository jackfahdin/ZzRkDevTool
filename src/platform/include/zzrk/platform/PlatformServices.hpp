// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace zzrk::platform {

class PlatformServices {
  public:
    virtual ~PlatformServices() = default;

    [[nodiscard]] virtual std::string_view platformName() const noexcept = 0;
    [[nodiscard]] virtual std::string usbAccessHelp() const = 0;
};

[[nodiscard]] std::unique_ptr<PlatformServices> createPlatformServices();

} // namespace zzrk::platform
