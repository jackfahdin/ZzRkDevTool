// SPDX-License-Identifier: MIT

#include <zzrk/transport/LibusbHotplugMonitor.hpp>

#include <libusb.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <sstream>
#include <thread>
#include <utility>

static_assert(LIBUSB_API_VERSION >= 0x0100010C,
              "ZzRkDevTool requires libusb headers version 1.0.30 or newer");

namespace zzrk::transport {
namespace {

constexpr std::uint16_t kRockchipVendorId = 0x2207U;

[[nodiscard]] std::string describeError(const int code)
{
    const char* const name = libusb_error_name(code);
    const char* const detail = libusb_strerror(static_cast<libusb_error>(code));

    std::string message = name != nullptr ? name : "libusb error";
    if (detail != nullptr && message != detail) {
        message += ": ";
        message += detail;
    }
    return message;
}

[[nodiscard]] bool isSupportedVersion(const libusb_version& version) noexcept
{
    if (version.major != 1U) {
        return version.major > 1U;
    }
    if (version.minor != 0U) {
        return version.minor > 0U;
    }
    return version.micro >= 30U;
}

[[nodiscard]] std::string versionText(const libusb_version& version)
{
    std::ostringstream stream;
    stream << version.major << '.' << version.minor << '.' << version.micro;
    return stream.str();
}

} // namespace

struct LibusbHotplugMonitor::Impl {
    libusb_context* context{};
    libusb_hotplug_callback_handle callbackHandle{};
    bool callbackRegistered{};
    std::jthread eventThread;
    std::atomic_bool stopping{};
    std::atomic_bool running{};
    UsbHotplugEventCallback eventCallback;
    UsbHotplugErrorCallback errorCallback;

    static int LIBUSB_CALL handleHotplug(libusb_context*, libusb_device*,
                                         const libusb_hotplug_event event,
                                         void* const userData) noexcept
    {
        auto* const self = static_cast<Impl*>(userData);
        if (self == nullptr || self->stopping.load()) {
            return 0;
        }

        try {
            if (self->eventCallback) {
                self->eventCallback(
                    event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT
                        ? UsbHotplugEvent::Left
                        : UsbHotplugEvent::Arrived);
            }
        } catch (...) {
            // Exceptions must never cross libusb's C callback boundary.
        }
        return 0;
    }

    void notifyError(const int code) noexcept
    {
        running.store(false);
        try {
            if (errorCallback) {
                errorCallback(UsbHotplugError{code, describeError(code)});
            }
        } catch (...) {
            // An observer failure must not escape the event thread.
        }
    }

    void runEvents() noexcept
    {
        while (!stopping.load()) {
            const int result = libusb_handle_events(context);
            if (stopping.load()) {
                break;
            }
            if (result == LIBUSB_SUCCESS
                || result == LIBUSB_ERROR_INTERRUPTED) {
                continue;
            }
            notifyError(result);
            break;
        }
        running.store(false);
    }
};

LibusbHotplugMonitor::LibusbHotplugMonitor()
    : impl_(std::make_unique<Impl>())
{
}

LibusbHotplugMonitor::~LibusbHotplugMonitor()
{
    stop();
}

std::optional<UsbHotplugError> LibusbHotplugMonitor::start(
    UsbHotplugEventCallback eventCallback,
    UsbHotplugErrorCallback errorCallback)
{
    if (impl_->context != nullptr) {
        return UsbHotplugError{
            LIBUSB_ERROR_BUSY,
            "The libusb hotplug monitor is already started",
        };
    }
    if (!eventCallback) {
        return UsbHotplugError{
            LIBUSB_ERROR_INVALID_PARAM,
            "The libusb hotplug event callback is required",
        };
    }

    const libusb_version* const runtimeVersion = libusb_get_version();
    if (runtimeVersion == nullptr || !isSupportedVersion(*runtimeVersion)) {
        const std::string actual = runtimeVersion == nullptr
            ? std::string{"unknown"}
            : versionText(*runtimeVersion);
        return UsbHotplugError{
            LIBUSB_ERROR_NOT_SUPPORTED,
            "libusb 1.0.30 or newer is required; runtime version is " + actual,
        };
    }

    int result = libusb_init(&impl_->context);
    if (result < 0) {
        impl_->context = nullptr;
        return UsbHotplugError{result, describeError(result)};
    }

    if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) == 0) {
        libusb_exit(impl_->context);
        impl_->context = nullptr;
        return UsbHotplugError{
            LIBUSB_ERROR_NOT_SUPPORTED,
            "The installed libusb 1.0.30 runtime does not provide hotplug "
            "support on this platform",
        };
    }

    impl_->eventCallback = std::move(eventCallback);
    impl_->errorCallback = std::move(errorCallback);
    impl_->stopping.store(false);
    result = libusb_hotplug_register_callback(
        impl_->context,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
            | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_NO_FLAGS,
        kRockchipVendorId,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        &Impl::handleHotplug,
        impl_.get(),
        &impl_->callbackHandle);
    if (result < 0) {
        impl_->eventCallback = {};
        impl_->errorCallback = {};
        libusb_exit(impl_->context);
        impl_->context = nullptr;
        return UsbHotplugError{result, describeError(result)};
    }
    impl_->callbackRegistered = true;
    impl_->running.store(true);

    try {
        impl_->eventThread = std::jthread([implementation = impl_.get()] {
            implementation->runEvents();
        });
    } catch (const std::exception& error) {
        impl_->running.store(false);
        libusb_hotplug_deregister_callback(impl_->context,
                                            impl_->callbackHandle);
        impl_->callbackRegistered = false;
        libusb_exit(impl_->context);
        impl_->context = nullptr;
        impl_->eventCallback = {};
        impl_->errorCallback = {};
        return UsbHotplugError{LIBUSB_ERROR_OTHER, error.what()};
    }

    return std::nullopt;
}

void LibusbHotplugMonitor::stop() noexcept
{
    if (impl_ == nullptr || impl_->context == nullptr) {
        return;
    }

    impl_->stopping.store(true);
    libusb_interrupt_event_handler(impl_->context);
    if (impl_->eventThread.joinable()) {
        impl_->eventThread.join();
    }
    if (impl_->callbackRegistered) {
        libusb_hotplug_deregister_callback(impl_->context,
                                            impl_->callbackHandle);
        impl_->callbackRegistered = false;
    }
    libusb_exit(impl_->context);
    impl_->context = nullptr;
    impl_->eventCallback = {};
    impl_->errorCallback = {};
    impl_->running.store(false);
}

bool LibusbHotplugMonitor::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->running.load();
}

} // namespace zzrk::transport
