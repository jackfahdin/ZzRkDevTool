// SPDX-License-Identifier: MIT

#include "RockusbSessionExecutor.hpp"

#include <limits>
#include <utility>

namespace zzrk::session {
namespace {

using namespace std::chrono_literals;

constexpr auto kMaximumTransferSize =
    static_cast<std::size_t>(std::numeric_limits<int>::max());

[[nodiscard]] constexpr SessionError
makeError(const SessionErrorCode code, const SessionStage stage) noexcept
{
    SessionError error{};
    error.code = code;
    error.stage = stage;
    return error;
}

[[nodiscard]] constexpr bool
isValidTimeout(const std::chrono::milliseconds timeout) noexcept
{
    return timeout > 0ms && timeout <= kMaximumSessionTimeout;
}

[[nodiscard]] std::optional<SessionError>
validateTimeout(const std::chrono::milliseconds timeout,
                const SessionStage stage) noexcept
{
    if (isValidTimeout(timeout)) {
        return std::nullopt;
    }
    auto error = makeError(SessionErrorCode::invalidTimeout, stage);
    error.timeout = timeout;
    return error;
}

[[nodiscard]] std::optional<SessionError>
validateTransfer(const TransferResult& result, const SessionStage stage,
                 const std::size_t expectedBytes) noexcept
{
    if (const auto* transportError = std::get_if<TransportError>(&result)) {
        const auto errorCode = transportError->bytesTransferred > expectedBytes
                                   ? SessionErrorCode::invalidTransferCount
                                   : SessionErrorCode::transportFailure;
        auto error = makeError(errorCode, stage);
        error.expectedBytes = expectedBytes;
        error.actualBytes = transportError->bytesTransferred;
        error.transportError = *transportError;
        return error;
    }

    const auto actualBytes = std::get<TransferSuccess>(result).bytesTransferred;
    if (actualBytes < expectedBytes) {
        auto error = makeError(SessionErrorCode::shortTransfer, stage);
        error.expectedBytes = expectedBytes;
        error.actualBytes = actualBytes;
        return error;
    }
    if (actualBytes > expectedBytes) {
        auto error = makeError(SessionErrorCode::invalidTransferCount, stage);
        error.expectedBytes = expectedBytes;
        error.actualBytes = actualBytes;
        return error;
    }
    return std::nullopt;
}

} // namespace

RockusbSession::RockusbSession(IUsbTransport& transport,
                               const SessionTimeouts timeouts) noexcept
    : transport_(transport), timeouts_(timeouts)
{
}

SessionResult detail::RockusbSessionExecutor::executeRead(
    RockusbSession& session,
    const detail::RockusbReadRequest& request,
    const std::span<std::byte> destination) noexcept
{
    if (session.state_ == SessionState::faulted) {
        return makeError(SessionErrorCode::sessionFaulted,
                         SessionStage::request);
    }

    if (auto error =
            validateTimeout(session.timeouts_.command, SessionStage::command)) {
        return *error;
    }
    if (auto error =
            validateTimeout(session.timeouts_.data, SessionStage::data)) {
        return *error;
    }
    if (auto error =
            validateTimeout(session.timeouts_.status, SessionStage::status)) {
        return *error;
    }

    if (destination.empty()) {
        return makeError(SessionErrorCode::emptyDataBuffer,
                         SessionStage::request);
    }
    if (destination.size() > kMaximumTransferSize) {
        auto error =
            makeError(SessionErrorCode::bufferTooLarge, SessionStage::request);
        error.expectedBytes = kMaximumTransferSize;
        error.actualBytes = destination.size();
        return error;
    }
    const protocol::RockusbCommand command{
        .tag = session.nextTag_,
        .transferLength = static_cast<std::uint32_t>(destination.size()),
        .direction = protocol::DataDirection::deviceToHost,
        .logicalUnit = request.logicalUnit,
        .commandLength = request.commandLength,
        .commandBlock = request.commandBlock,
    };
    const auto encoded = protocol::encodeCommand(command);
    const auto* commandBytes =
        std::get_if<protocol::RockusbCommandBytes>(&encoded);
    if (commandBytes == nullptr) {
        auto error = makeError(SessionErrorCode::commandEncodeFailed,
                               SessionStage::request);
        error.commandEncodeError =
            std::get<protocol::CommandEncodeError>(encoded);
        return error;
    }

    ++session.nextTag_;
    if (session.nextTag_ == 0U) {
        ++session.nextTag_;
    }
    if (auto error = validateTransfer(
            session.transport_.bulkOut(*commandBytes,
                                       session.timeouts_.command),
            SessionStage::command, commandBytes->size())) {
        return session.fault(std::move(*error));
    }

    if (auto error = validateTransfer(
            session.transport_.bulkIn(destination, session.timeouts_.data),
            SessionStage::data,
            destination.size())) {
        return session.fault(std::move(*error));
    }

    protocol::RockusbStatusBytes statusBytes{};
    if (auto error = validateTransfer(
            session.transport_.bulkIn(statusBytes, session.timeouts_.status),
            SessionStage::status, statusBytes.size())) {
        return session.fault(std::move(*error));
    }

    auto decoded = protocol::decodeStatus(statusBytes, command.tag);
    const auto* status = std::get_if<protocol::RockusbStatus>(&decoded);
    if (status == nullptr) {
        auto statusError = std::get<protocol::StatusDecodeError>(decoded);
        auto error = makeError(SessionErrorCode::statusDecodeFailed,
                               SessionStage::status);
        error.statusDecodeError = statusError;
        if (statusError.code == protocol::StatusDecodeErrorCode::commandFailed) {
            if (statusError.dataResidue != 0U) {
                error.code = SessionErrorCode::nonZeroResidue;
                error.residue = statusError.dataResidue;
                return session.fault(std::move(error));
            }
            error.code = SessionErrorCode::deviceCommandFailed;
            return error;
        }
        return session.fault(std::move(error));
    }
    if (status->dataResidue != 0U) {
        auto error = makeError(SessionErrorCode::nonZeroResidue,
                               SessionStage::status);
        error.residue = status->dataResidue;
        return session.fault(std::move(error));
    }

    return SessionSuccess{
        .status = *status,
        .dataBytesTransferred = destination.size(),
    };
}

SessionResult detail::RockusbSessionExecutor::executeWrite(
    RockusbSession& session,
    const detail::RockusbWriteRequest& request,
    const std::span<const std::byte> source) noexcept
{
    if (session.state_ == SessionState::faulted) {
        return makeError(SessionErrorCode::sessionFaulted,
                         SessionStage::request);
    }
    if (auto error =
            validateTimeout(session.timeouts_.command, SessionStage::command)) {
        return *error;
    }
    if (auto error =
            validateTimeout(session.timeouts_.data, SessionStage::data)) {
        return *error;
    }
    if (auto error =
            validateTimeout(session.timeouts_.status, SessionStage::status)) {
        return *error;
    }
    if (source.empty()) {
        return makeError(SessionErrorCode::emptyDataBuffer,
                         SessionStage::request);
    }
    if (source.size() > kMaximumTransferSize) {
        auto error =
            makeError(SessionErrorCode::bufferTooLarge, SessionStage::request);
        error.expectedBytes = kMaximumTransferSize;
        error.actualBytes = source.size();
        return error;
    }

    const protocol::RockusbCommand command{
        .tag = session.nextTag_,
        .transferLength = static_cast<std::uint32_t>(source.size()),
        .direction = protocol::DataDirection::hostToDevice,
        .logicalUnit = request.logicalUnit,
        .commandLength = request.commandLength,
        .commandBlock = request.commandBlock,
    };
    const auto encoded = protocol::encodeCommand(command);
    const auto* commandBytes =
        std::get_if<protocol::RockusbCommandBytes>(&encoded);
    if (commandBytes == nullptr) {
        auto error = makeError(SessionErrorCode::commandEncodeFailed,
                               SessionStage::request);
        error.commandEncodeError =
            std::get<protocol::CommandEncodeError>(encoded);
        return error;
    }

    ++session.nextTag_;
    if (session.nextTag_ == 0U) {
        ++session.nextTag_;
    }
    if (auto error = validateTransfer(
            session.transport_.bulkOut(*commandBytes,
                                       session.timeouts_.command),
            SessionStage::command, commandBytes->size())) {
        return session.fault(std::move(*error));
    }
    if (auto error = validateTransfer(
            session.transport_.bulkOut(source, session.timeouts_.data),
            SessionStage::data, source.size())) {
        return session.fault(std::move(*error));
    }

    protocol::RockusbStatusBytes statusBytes{};
    if (auto error = validateTransfer(
            session.transport_.bulkIn(statusBytes, session.timeouts_.status),
            SessionStage::status, statusBytes.size())) {
        return session.fault(std::move(*error));
    }
    auto decoded = protocol::decodeStatus(statusBytes, command.tag);
    const auto* status = std::get_if<protocol::RockusbStatus>(&decoded);
    if (status == nullptr) {
        const auto statusError =
            std::get<protocol::StatusDecodeError>(decoded);
        auto error = makeError(SessionErrorCode::statusDecodeFailed,
                               SessionStage::status);
        error.statusDecodeError = statusError;
        if (statusError.code == protocol::StatusDecodeErrorCode::commandFailed) {
            if (statusError.dataResidue != 0U) {
                error.code = SessionErrorCode::nonZeroResidue;
                error.residue = statusError.dataResidue;
                return session.fault(std::move(error));
            }
            error.code = SessionErrorCode::deviceCommandFailed;
            return error;
        }
        return session.fault(std::move(error));
    }
    if (status->dataResidue != 0U) {
        auto error = makeError(SessionErrorCode::nonZeroResidue,
                               SessionStage::status);
        error.residue = status->dataResidue;
        return session.fault(std::move(error));
    }
    return SessionSuccess{
        .status = *status,
        .dataBytesTransferred = source.size(),
    };
}

SessionResult detail::RockusbSessionExecutor::executeNoData(
    RockusbSession& session,
    const detail::RockusbNoDataRequest& request) noexcept
{
    if (session.state_ == SessionState::faulted) {
        return makeError(SessionErrorCode::sessionFaulted,
                         SessionStage::request);
    }
    if (auto error =
            validateTimeout(session.timeouts_.command, SessionStage::command)) {
        return *error;
    }
    if (auto error =
            validateTimeout(session.timeouts_.status, SessionStage::status)) {
        return *error;
    }

    const protocol::RockusbCommand command{
        .tag = session.nextTag_,
        .transferLength = 0,
        .direction = protocol::DataDirection::hostToDevice,
        .logicalUnit = request.logicalUnit,
        .commandLength = request.commandLength,
        .commandBlock = request.commandBlock,
    };
    const auto encoded = protocol::encodeCommand(command);
    const auto* commandBytes =
        std::get_if<protocol::RockusbCommandBytes>(&encoded);
    if (commandBytes == nullptr) {
        auto error = makeError(SessionErrorCode::commandEncodeFailed,
                               SessionStage::request);
        error.commandEncodeError =
            std::get<protocol::CommandEncodeError>(encoded);
        return error;
    }

    ++session.nextTag_;
    if (session.nextTag_ == 0U) {
        ++session.nextTag_;
    }
    if (auto error = validateTransfer(
            session.transport_.bulkOut(*commandBytes,
                                       session.timeouts_.command),
            SessionStage::command, commandBytes->size())) {
        return session.fault(std::move(*error));
    }

    protocol::RockusbStatusBytes statusBytes{};
    if (auto error = validateTransfer(
            session.transport_.bulkIn(statusBytes, session.timeouts_.status),
            SessionStage::status, statusBytes.size())) {
        return session.fault(std::move(*error));
    }
    auto decoded = protocol::decodeStatus(statusBytes, command.tag);
    const auto* status = std::get_if<protocol::RockusbStatus>(&decoded);
    if (status == nullptr) {
        const auto statusError =
            std::get<protocol::StatusDecodeError>(decoded);
        auto error = makeError(SessionErrorCode::statusDecodeFailed,
                               SessionStage::status);
        error.statusDecodeError = statusError;
        if (statusError.code == protocol::StatusDecodeErrorCode::commandFailed) {
            error.code = SessionErrorCode::deviceCommandFailed;
            return error;
        }
        return session.fault(std::move(error));
    }
    if (status->dataResidue != 0U) {
        auto error = makeError(SessionErrorCode::nonZeroResidue,
                               SessionStage::status);
        error.residue = status->dataResidue;
        return session.fault(std::move(error));
    }
    return SessionSuccess{.status = *status, .dataBytesTransferred = 0};
}

SessionState RockusbSession::state() const noexcept
{
    return state_;
}

SessionResult RockusbSession::fault(SessionError error) noexcept
{
    state_ = SessionState::faulted;
    return error;
}

} // namespace zzrk::session
