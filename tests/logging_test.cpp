// SPDX-License-Identifier: MIT

#include <zzrk/logging/Logging.hpp>

#include <iostream>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

} // namespace

int main()
{
    using zzrk::logging::Level;

    auto first = zzrk::logging::initializeConsoleLogging("logging-test-1",
                                                          Level::Off);
    expect(static_cast<bool>(first), "first logging session is active");

    auto second = zzrk::logging::initializeConsoleLogging("logging-test-2",
                                                           Level::Off);
    expect(!static_cast<bool>(first),
           "reinitialization retires the previous session");
    expect(static_cast<bool>(second), "new logging session is active");

    auto moved = std::move(second);
    expect(!static_cast<bool>(second), "moved-from session is inactive");
    expect(static_cast<bool>(moved), "moved-to session retains ownership");

    first = {};
    expect(static_cast<bool>(moved),
           "releasing a retired session preserves the current session");

    zzrk::logging::trace("trace");
    zzrk::logging::debug("debug");
    zzrk::logging::info("info");
    zzrk::logging::warning("warning");
    zzrk::logging::error("error");
    zzrk::logging::critical("critical");

    zzrk::logging::shutdown();
    expect(!static_cast<bool>(moved),
           "explicit shutdown retires the active session");
    zzrk::logging::info("logging after shutdown is a safe no-op");

    return failures == 0 ? 0 : 1;
}
