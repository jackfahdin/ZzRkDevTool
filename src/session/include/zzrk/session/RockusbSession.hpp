// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/IUsbTransport.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace zzrk::session {

inline constexpr std::chrono::milliseconds kMaximumSessionTimeout{300'000};

namespace detail {
class RockusbSessionExecutor;
} // namespace detail

struct SessionTimeouts {
    std::chrono::milliseconds command{5'000};
    std::chrono::milliseconds data{30'000};
    std::chrono::milliseconds status{5'000};

    friend constexpr bool operator==(const SessionTimeouts&,
                                     const SessionTimeouts&) = default;
};

enum class SessionState {
    ready,
    faulted,
};

enum class StatusResiduePolicy {
    requireZero,
    allowNonZeroOnSuccess,
};

enum class SessionStage {
    request,
    command,
    data,
    status,
};

enum class SessionErrorCode {
    sessionFaulted,
    invalidTimeout,
    commandEncodeFailed,
    emptyDataBuffer,
    bufferTooLarge,
    invalidAddress,
    invalidSectorCount,
    bufferSizeMismatch,
    transportFailure,
    shortTransfer,
    invalidTransferCount,
    deviceCommandFailed,
    statusDecodeFailed,
    nonZeroResidue,
};

struct SessionError {
    SessionErrorCode code{};
    SessionStage stage{SessionStage::request};
    std::size_t expectedBytes{};
    std::size_t actualBytes{};
    std::chrono::milliseconds timeout{};
    std::uint32_t residue{};
    std::optional<TransportError> transportError;
    std::optional<protocol::CommandEncodeError> commandEncodeError;
    std::optional<protocol::StatusDecodeError> statusDecodeError;

    friend constexpr bool operator==(const SessionError&,
                                     const SessionError&) = default;
};

struct SessionSuccess {
    protocol::RockusbStatus status;
    std::size_t dataBytesTransferred{};

    friend constexpr bool operator==(const SessionSuccess&,
                                     const SessionSuccess&) = default;
};

using SessionResult = std::variant<SessionSuccess, SessionError>;

class RockusbSession final {
  public:
    // Calls are synchronous and not thread-safe. The owner serializes access
    // and keeps transport alive longer than this session.
    explicit RockusbSession(IUsbTransport& transport,
                            SessionTimeouts timeouts = {},
                            StatusResiduePolicy statusResiduePolicy =
                                StatusResiduePolicy::requireZero) noexcept;

    RockusbSession(const RockusbSession&) = delete;
    RockusbSession& operator=(const RockusbSession&) = delete;
    RockusbSession(RockusbSession&&) = delete;
    RockusbSession& operator=(RockusbSession&&) = delete;

    [[nodiscard]] SessionState state() const noexcept;

  private:
    friend class detail::RockusbSessionExecutor;

    [[nodiscard]] SessionResult fault(SessionError error) noexcept;

    IUsbTransport& transport_;
    SessionTimeouts timeouts_;
    StatusResiduePolicy statusResiduePolicy_;
    SessionState state_{SessionState::ready};
    std::uint32_t nextTag_{1};
};

} // namespace zzrk::session
