// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string_view>

namespace zzrk::logging {

enum class Level {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off,
};

class LoggingSession final {
  public:
    LoggingSession() noexcept = default;
    ~LoggingSession() noexcept;

    LoggingSession(const LoggingSession&) = delete;
    LoggingSession& operator=(const LoggingSession&) = delete;
    LoggingSession(LoggingSession&& other) noexcept;
    LoggingSession& operator=(LoggingSession&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    friend LoggingSession initializeConsoleLogging(std::string_view, Level) noexcept;

    explicit LoggingSession(std::uint64_t generation) noexcept;

    std::uint64_t generation_{};
};

[[nodiscard]] LoggingSession initializeConsoleLogging(
    std::string_view applicationName,
    Level minimumLevel = Level::Info) noexcept;

void shutdown() noexcept;

void log(Level level, std::string_view message) noexcept;
void trace(std::string_view message) noexcept;
void debug(std::string_view message) noexcept;
void info(std::string_view message) noexcept;
void warning(std::string_view message) noexcept;
void error(std::string_view message) noexcept;
void critical(std::string_view message) noexcept;

} // namespace zzrk::logging
