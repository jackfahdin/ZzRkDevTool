// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace zzrk::transport {

enum class UsbHotplugEvent {
    Arrived,
    Left,
};

struct UsbHotplugError {
    int code{};
    std::string message;
};

using UsbHotplugEventCallback = std::function<void(UsbHotplugEvent)>;
using UsbHotplugErrorCallback = std::function<void(const UsbHotplugError&)>;

class LibusbHotplugMonitor final {
public:
    LibusbHotplugMonitor();
    ~LibusbHotplugMonitor();

    LibusbHotplugMonitor(const LibusbHotplugMonitor&) = delete;
    LibusbHotplugMonitor& operator=(const LibusbHotplugMonitor&) = delete;
    LibusbHotplugMonitor(LibusbHotplugMonitor&&) = delete;
    LibusbHotplugMonitor& operator=(LibusbHotplugMonitor&&) = delete;

    [[nodiscard]] std::optional<UsbHotplugError> start(
        UsbHotplugEventCallback eventCallback,
        UsbHotplugErrorCallback errorCallback = {});
    void stop() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace zzrk::transport
