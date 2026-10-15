// SPDX-License-Identifier: MIT

#include <zzrk/logging/Logging.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace zzrk::logging {
namespace {

struct LoggerState {
    std::uint64_t generation{};
    std::shared_ptr<spdlog::logger> logger;
};

std::atomic<std::uint64_t> nextGeneration{0};
// Support libc++ versions without the C++20 atomic<shared_ptr> specialization.
std::mutex loggerStateMutex;
std::shared_ptr<const LoggerState> activeLoggerState;

std::shared_ptr<const LoggerState> loadLoggerState()
{
    const std::lock_guard lock{loggerStateMutex};
    return activeLoggerState;
}

std::shared_ptr<const LoggerState> exchangeLoggerState(
    std::shared_ptr<const LoggerState> state)
{
    const std::lock_guard lock{loggerStateMutex};
    return std::exchange(activeLoggerState, std::move(state));
}

[[nodiscard]] constexpr spdlog::level toSpdlogLevel(const Level level) noexcept
{
    switch (level) {
    case Level::Trace:
        return spdlog::level::trace;
    case Level::Debug:
        return spdlog::level::debug;
    case Level::Info:
        return spdlog::level::info;
    case Level::Warning:
        return spdlog::level::warn;
    case Level::Error:
        return spdlog::level::err;
    case Level::Critical:
        return spdlog::level::critical;
    case Level::Off:
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

void reportLoggingFailure(const char* message) noexcept
{
    std::fputs("ZzRkDevTool logging failure: ", stderr);
    std::fputs(message != nullptr ? message : "unknown error", stderr);
    std::fputc('\n', stderr);
}

void handleSpdlogError(const std::string& message) noexcept
{
    reportLoggingFailure(message.c_str());
}

void flushLogger(const std::shared_ptr<const LoggerState>& state) noexcept
{
    if (state != nullptr && state->logger != nullptr) {
        state->logger->flush();
    }
}

void releaseSession(const std::uint64_t generation) noexcept
{
    if (generation == 0) {
        return;
    }

    std::shared_ptr<const LoggerState> state;
    {
        const std::lock_guard lock{loggerStateMutex};
        if (activeLoggerState != nullptr
            && activeLoggerState->generation == generation) {
            state = std::exchange(activeLoggerState, nullptr);
        }
    }
    flushLogger(state);
}

} // namespace

LoggingSession::LoggingSession(const std::uint64_t generation) noexcept
    : generation_(generation)
{
}

LoggingSession::~LoggingSession() noexcept
{
    releaseSession(generation_);
}

LoggingSession::LoggingSession(LoggingSession&& other) noexcept
    : generation_(std::exchange(other.generation_, 0))
{
}

LoggingSession& LoggingSession::operator=(LoggingSession&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    releaseSession(generation_);
    generation_ = std::exchange(other.generation_, 0);
    return *this;
}

LoggingSession::operator bool() const noexcept
{
    const auto state = loadLoggerState();
    return generation_ != 0 && state != nullptr
        && state->generation == generation_;
}

LoggingSession initializeConsoleLogging(const std::string_view applicationName,
                                        const Level minimumLevel) noexcept
{
    try {
        auto logger = spdlog::create<spdlog::sinks::stderr_color_sink_mt>(
            applicationName.empty() ? "ZzRkDevTool"
                                    : std::string{applicationName});
        logger->set_level(toSpdlogLevel(minimumLevel));
        logger->flush_on(spdlog::level::warn);
        logger->set_error_handler(handleSpdlogError);
        logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] [%n] %v");
        const auto generation = nextGeneration.fetch_add(1) + 1;
        auto state = std::make_shared<LoggerState>(
            LoggerState{generation, std::move(logger)});
        const auto previousState = exchangeLoggerState(std::move(state));
        flushLogger(previousState);
        return LoggingSession{generation};
    } catch (const std::exception& exception) {
        reportLoggingFailure(exception.what());
    } catch (...) {
        reportLoggingFailure("unknown initialization error");
    }

    return {};
}

void shutdown() noexcept
{
    const auto state = exchangeLoggerState(nullptr);
    flushLogger(state);
}

void log(const Level level, const std::string_view message) noexcept
{
    const auto state = loadLoggerState();
    if (state != nullptr && state->logger != nullptr) {
        state->logger->log(toSpdlogLevel(level), message);
    }
}

void trace(const std::string_view message) noexcept
{
    log(Level::Trace, message);
}

void debug(const std::string_view message) noexcept
{
    log(Level::Debug, message);
}

void info(const std::string_view message) noexcept
{
    log(Level::Info, message);
}

void warning(const std::string_view message) noexcept
{
    log(Level::Warning, message);
}

void error(const std::string_view message) noexcept
{
    log(Level::Error, message);
}

void critical(const std::string_view message) noexcept
{
    log(Level::Critical, message);
}

} // namespace zzrk::logging
