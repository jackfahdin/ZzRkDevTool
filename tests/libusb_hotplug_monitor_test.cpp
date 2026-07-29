// SPDX-License-Identifier: MIT

#include <zzrk/transport/LibusbHotplugMonitor.hpp>

#include <libusb.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

struct libusb_context {};
struct libusb_device {};

namespace {

using namespace std::chrono_literals;

struct FakeEvent {
    int result{LIBUSB_SUCCESS};
    libusb_hotplug_event event{LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED};
};

libusb_context g_context;
libusb_device g_device;
libusb_context* g_contextValue = &g_context;
libusb_device* g_deviceValue = &g_device;
libusb_version g_version{1U, 0U, 30U, 0U, "", ""};
int g_initializationResult{LIBUSB_SUCCESS};
int g_hasHotplug{1};
int g_registrationResult{LIBUSB_SUCCESS};
libusb_hotplug_callback_fn g_callback{};
void* g_callbackData{};
int g_registeredEvents{};
int g_registeredVendor{};
int g_registeredProduct{};
int g_registeredClass{};
int g_initCalls{};
int g_exitCalls{};
int g_registerCalls{};
int g_deregisterCalls{};
int g_interruptCalls{};
std::mutex g_eventMutex;
std::condition_variable g_eventCondition;
std::deque<FakeEvent> g_events;
bool g_interrupted{};
int g_failures{};

void expect(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void resetFakeLibusb()
{
    g_version = libusb_version{1U, 0U, 30U, 0U, "", ""};
    g_initializationResult = LIBUSB_SUCCESS;
    g_hasHotplug = 1;
    g_registrationResult = LIBUSB_SUCCESS;
    g_callback = nullptr;
    g_callbackData = nullptr;
    g_registeredEvents = 0;
    g_registeredVendor = 0;
    g_registeredProduct = 0;
    g_registeredClass = 0;
    g_initCalls = 0;
    g_exitCalls = 0;
    g_registerCalls = 0;
    g_deregisterCalls = 0;
    g_interruptCalls = 0;
    std::lock_guard lock{g_eventMutex};
    g_events.clear();
    g_interrupted = false;
}

void queueEvent(const FakeEvent event)
{
    {
        std::lock_guard lock{g_eventMutex};
        g_events.push_back(event);
    }
    g_eventCondition.notify_all();
}

template <typename Predicate>
bool waitUntil(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

void testRejectsOldRuntime()
{
    resetFakeLibusb();
    g_version.micro = 29U;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto error = monitor.start([](zzrk::transport::UsbHotplugEvent) {});
    expect(error.has_value() && error->code == LIBUSB_ERROR_NOT_SUPPORTED,
           "a libusb runtime older than 1.0.30 is rejected");
    expect(error.has_value()
               && error->message.find("1.0.29") != std::string::npos,
           "the version error reports the actual runtime version");
    expect(g_initCalls == 0, "an old runtime is rejected before context init");
}

void testRejectsMissingCapability()
{
    resetFakeLibusb();
    g_hasHotplug = 0;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto error = monitor.start([](zzrk::transport::UsbHotplugEvent) {});
    expect(error.has_value() && error->code == LIBUSB_ERROR_NOT_SUPPORTED,
           "a runtime without hotplug capability is rejected");
    expect(g_initCalls == 1 && g_exitCalls == 1,
           "capability rejection releases the initialized context");
    expect(g_registerCalls == 0,
           "a callback is not registered without hotplug capability");
}

void testReportsInitializationFailure()
{
    resetFakeLibusb();
    g_initializationResult = LIBUSB_ERROR_ACCESS;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto error = monitor.start([](zzrk::transport::UsbHotplugEvent) {});
    expect(error.has_value() && error->code == LIBUSB_ERROR_ACCESS,
           "libusb initialization failures retain their native code");
    expect(g_initCalls == 1 && g_exitCalls == 0 && g_registerCalls == 0,
           "a failed context is neither registered nor released");
}

void testReportsRegistrationFailure()
{
    resetFakeLibusb();
    g_registrationResult = LIBUSB_ERROR_NO_MEM;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto error = monitor.start([](zzrk::transport::UsbHotplugEvent) {});
    expect(error.has_value() && error->code == LIBUSB_ERROR_NO_MEM,
           "callback registration preserves the libusb error code");
    expect(g_registerCalls == 1 && g_exitCalls == 1,
           "registration failure releases the initialized context");
}

void testDispatchesEventsAndStopsCleanly()
{
    resetFakeLibusb();
    std::mutex callbackMutex;
    std::vector<zzrk::transport::UsbHotplugEvent> observed;
    std::vector<zzrk::transport::UsbHotplugError> errors;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto startError = monitor.start(
        [&](const zzrk::transport::UsbHotplugEvent event) {
            std::lock_guard lock{callbackMutex};
            observed.push_back(event);
        },
        [&](const zzrk::transport::UsbHotplugError& error) {
            std::lock_guard lock{callbackMutex};
            errors.push_back(error);
        });
    expect(!startError.has_value() && monitor.isRunning(),
           "a capable libusb runtime starts the hotplug event loop");
    expect(g_registeredEvents
                   == (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
                       | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
               && g_registeredVendor == 0x2207
               && g_registeredProduct == LIBUSB_HOTPLUG_MATCH_ANY
               && g_registeredClass == LIBUSB_HOTPLUG_MATCH_ANY,
           "the callback filters Rockchip devices and both event types");

    queueEvent({LIBUSB_SUCCESS, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED});
    queueEvent({LIBUSB_SUCCESS, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT});
    expect(waitUntil([&] {
               std::lock_guard lock{callbackMutex};
               return observed.size() == 2U;
           }),
           "arrival and departure events reach the observer");
    {
        std::lock_guard lock{callbackMutex};
        expect(observed.size() == 2U
                   && observed[0] == zzrk::transport::UsbHotplugEvent::Arrived
                   && observed[1] == zzrk::transport::UsbHotplugEvent::Left,
               "hotplug event types retain their order");
        expect(errors.empty(), "successful event handling reports no error");
    }

    monitor.stop();
    expect(!monitor.isRunning(), "stop clears the running state");
    expect(g_interruptCalls == 1 && g_deregisterCalls == 1
               && g_exitCalls == 1,
           "stop wakes, joins, deregisters, and releases the event context");
}

void testReportsEventLoopFailure()
{
    resetFakeLibusb();
    std::mutex callbackMutex;
    std::vector<zzrk::transport::UsbHotplugError> errors;
    zzrk::transport::LibusbHotplugMonitor monitor;
    const auto startError = monitor.start(
        [](zzrk::transport::UsbHotplugEvent) {},
        [&](const zzrk::transport::UsbHotplugError& error) {
            std::lock_guard lock{callbackMutex};
            errors.push_back(error);
        });
    expect(!startError.has_value(), "the event error test starts normally");
    queueEvent({LIBUSB_ERROR_IO,
                LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED});
    expect(waitUntil([&] {
               std::lock_guard lock{callbackMutex};
               return !errors.empty();
           }),
           "an event-loop failure reaches the error observer");
    {
        std::lock_guard lock{callbackMutex};
        expect(errors.size() == 1U && errors.front().code == LIBUSB_ERROR_IO,
               "the event-loop error preserves its native code");
    }
    expect(!monitor.isRunning(),
           "an event-loop failure clears the running state");
    monitor.stop();
    expect(g_deregisterCalls == 1 && g_exitCalls == 1,
           "a failed event loop still releases registration and context");
}

} // namespace

extern "C" {

const libusb_version* LIBUSB_CALL libusb_get_version(void)
{
    return &g_version;
}

int LIBUSB_CALL libusb_init(libusb_context** context)
{
    ++g_initCalls;
    *context = g_initializationResult == LIBUSB_SUCCESS ? g_contextValue
                                                        : nullptr;
    return g_initializationResult;
}

void LIBUSB_CALL libusb_exit(libusb_context*)
{
    ++g_exitCalls;
}

int LIBUSB_CALL libusb_has_capability(const std::uint32_t capability)
{
    return capability == LIBUSB_CAP_HAS_HOTPLUG ? g_hasHotplug : 0;
}

int LIBUSB_CALL libusb_hotplug_register_callback(
    libusb_context*, const int events, const int, const int vendorId,
    const int productId, const int deviceClass,
    const libusb_hotplug_callback_fn callback, void* const userData,
    libusb_hotplug_callback_handle* const callbackHandle)
{
    ++g_registerCalls;
    g_registeredEvents = events;
    g_registeredVendor = vendorId;
    g_registeredProduct = productId;
    g_registeredClass = deviceClass;
    if (g_registrationResult == LIBUSB_SUCCESS) {
        g_callback = callback;
        g_callbackData = userData;
        *callbackHandle = 17;
    }
    return g_registrationResult;
}

void LIBUSB_CALL libusb_hotplug_deregister_callback(
    libusb_context*, libusb_hotplug_callback_handle)
{
    ++g_deregisterCalls;
    g_callback = nullptr;
    g_callbackData = nullptr;
}

int LIBUSB_CALL libusb_handle_events(libusb_context*)
{
    FakeEvent event;
    {
        std::unique_lock lock{g_eventMutex};
        g_eventCondition.wait(lock, [] {
            return g_interrupted || !g_events.empty();
        });
        if (g_interrupted) {
            g_interrupted = false;
            return LIBUSB_SUCCESS;
        }
        event = g_events.front();
        g_events.pop_front();
    }
    if (event.result == LIBUSB_SUCCESS && g_callback != nullptr) {
        g_callback(g_contextValue, g_deviceValue, event.event, g_callbackData);
    }
    return event.result;
}

void LIBUSB_CALL libusb_interrupt_event_handler(libusb_context*)
{
    ++g_interruptCalls;
    {
        std::lock_guard lock{g_eventMutex};
        g_interrupted = true;
    }
    g_eventCondition.notify_all();
}

const char* LIBUSB_CALL libusb_error_name(const int errorCode)
{
    switch (errorCode) {
    case LIBUSB_ERROR_IO:
        return "LIBUSB_ERROR_IO";
    case LIBUSB_ERROR_NO_MEM:
        return "LIBUSB_ERROR_NO_MEM";
    default:
        return "LIBUSB_ERROR";
    }
}

const char* LIBUSB_CALL libusb_strerror(const int)
{
    return "Fake libusb error";
}

} // extern "C"

int main()
{
    testRejectsOldRuntime();
    testRejectsMissingCapability();
    testReportsInitializationFailure();
    testReportsRegistrationFailure();
    testDispatchesEventsAndStopsCleanly();
    testReportsEventLoopFailure();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
