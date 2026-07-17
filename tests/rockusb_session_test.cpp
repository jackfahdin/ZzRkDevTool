// SPDX-License-Identifier: MIT

#include <zzrk/session/RockusbSession.hpp>

#include "RockusbSessionExecutor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using zzrk::protocol::RockusbStatusBytes;
using zzrk::protocol::StatusDecodeErrorCode;
using zzrk::session::IUsbTransport;
using zzrk::session::kMaximumSessionTimeout;
using zzrk::session::RockusbSession;
using zzrk::session::SessionError;
using zzrk::session::SessionErrorCode;
using zzrk::session::SessionResult;
using zzrk::session::SessionStage;
using zzrk::session::SessionState;
using zzrk::session::SessionSuccess;
using zzrk::session::SessionTimeouts;
using zzrk::session::TransferResult;
using zzrk::session::TransferSuccess;
using zzrk::session::TransportError;
using zzrk::session::TransportErrorCode;
using zzrk::session::detail::RockusbReadRequest;
using zzrk::session::detail::RockusbSessionExecutor;

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

enum class Operation {
    bulkOut,
    bulkIn,
};

struct Action {
    Operation operation{};
    TransferResult result{TransferSuccess{}};
    std::vector<std::byte> payload;
};

struct Call {
    Operation operation{};
    std::size_t requestedBytes{};
    std::chrono::milliseconds timeout{};
    std::vector<std::byte> payload;
};

class FakeTransport final : public IUsbTransport {
  public:
    void queueOut(const TransferResult result)
    {
        actions_.push_back(Action{
            .operation = Operation::bulkOut,
            .result = result,
            .payload = {},
        });
    }

    void queueIn(const std::span<const std::byte> payload,
                 const TransferResult result)
    {
        actions_.push_back(Action{
            .operation = Operation::bulkIn,
            .result = result,
            .payload = {payload.begin(), payload.end()},
        });
    }

    void queueOutSuccess(const std::size_t size)
    {
        queueOut(TransferSuccess{size});
    }

    void queueInSuccess(const std::span<const std::byte> payload)
    {
        queueIn(payload, TransferSuccess{payload.size()});
    }

    void queueInError(const TransportError error)
    {
        queueIn({}, error);
    }

    [[nodiscard]] TransferResult
    bulkOut(const std::span<const std::byte> bytes,
            const std::chrono::milliseconds timeout) noexcept override
    {
        calls_.push_back(Call{
            .operation = Operation::bulkOut,
            .requestedBytes = bytes.size(),
            .timeout = timeout,
            .payload = {bytes.begin(), bytes.end()},
        });
        return consume(Operation::bulkOut, {});
    }

    [[nodiscard]] TransferResult
    bulkIn(const std::span<std::byte> bytes,
           const std::chrono::milliseconds timeout) noexcept override
    {
        calls_.push_back(Call{
            .operation = Operation::bulkIn,
            .requestedBytes = bytes.size(),
            .timeout = timeout,
            .payload = {},
        });
        return consume(Operation::bulkIn, bytes);
    }

    [[nodiscard]] const std::vector<Call>& calls() const noexcept
    {
        return calls_;
    }

    [[nodiscard]] std::size_t consumedActions() const noexcept
    {
        return nextAction_;
    }

    [[nodiscard]] bool scriptMismatch() const noexcept
    {
        return scriptMismatch_;
    }

  private:
    [[nodiscard]] TransferResult
    consume(const Operation operation,
            const std::span<std::byte> destination) noexcept
    {
        if (nextAction_ >= actions_.size()) {
            scriptMismatch_ = true;
            return TransportError{TransportErrorCode::other, -1, 0};
        }

        const auto& action = actions_[nextAction_];
        ++nextAction_;
        if (action.operation != operation) {
            scriptMismatch_ = true;
            return TransportError{TransportErrorCode::other, -2, 0};
        }

        if (operation == Operation::bulkIn) {
            if (const auto* success =
                    std::get_if<TransferSuccess>(&action.result)) {
                const auto copySize =
                    std::min(std::min(destination.size(), action.payload.size()),
                             success->bytesTransferred);
                std::copy_n(action.payload.begin(), copySize,
                            destination.begin());
            }
        }
        return action.result;
    }

    std::vector<Action> actions_;
    std::vector<Call> calls_;
    std::size_t nextAction_{};
    bool scriptMismatch_{};
};

constexpr void writeLe32(RockusbStatusBytes& bytes, const std::size_t offset,
                         const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

constexpr RockusbStatusBytes makeStatus(const std::uint32_t tag,
                                        const std::uint32_t residue = 0,
                                        const std::uint8_t status = 0)
{
    RockusbStatusBytes bytes{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53},
    };
    writeLe32(bytes, 4, tag);
    writeLe32(bytes, 8, residue);
    bytes[12] = static_cast<std::byte>(status);
    return bytes;
}

RockusbReadRequest makeRequest()
{
    RockusbReadRequest request{
        .commandLength = 1,
    };
    request.commandBlock[0] = std::byte{0x12};
    return request;
}

SessionResult executeRead(RockusbSession& session,
                          const RockusbReadRequest& request,
                          const std::span<std::byte> destination) noexcept
{
    return RockusbSessionExecutor::executeRead(session, request, destination);
}

const SessionError* getError(const SessionResult& result,
                             const std::string_view description)
{
    const auto* error = std::get_if<SessionError>(&result);
    expect(error != nullptr, description);
    return error;
}

const SessionSuccess* getSuccess(const SessionResult& result,
                                 const std::string_view description)
{
    const auto* success = std::get_if<SessionSuccess>(&result);
    expect(success != nullptr, description);
    return success;
}

void queueSuccessfulRead(FakeTransport& transport,
                         const std::span<const std::byte> payload,
                         const std::uint32_t tag)
{
    transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
    transport.queueInSuccess(payload);
    const auto status = makeStatus(tag);
    transport.queueInSuccess(status);
}

void expectCall(const FakeTransport& transport, const std::size_t index,
                const Operation operation, const std::size_t requestedBytes,
                const std::chrono::milliseconds timeout,
                const std::string_view description)
{
    if (transport.calls().size() <= index) {
        expect(false, description);
        return;
    }
    const auto& call = transport.calls()[index];
    expect(call.operation == operation && call.requestedBytes == requestedBytes
               && call.timeout == timeout,
           description);
}

void expectFaultedCallDoesNoIo(RockusbSession& session,
                               FakeTransport& transport,
                               const RockusbReadRequest& request,
                               const std::span<std::byte> destination)
{
    const auto callCount = transport.calls().size();
    const auto result = executeRead(session, request, destination);
    const auto* error =
        getError(result, "faulted session rejects another command");
    expect(error != nullptr
               && error->code == SessionErrorCode::sessionFaulted,
           "faulted session reports its terminal state");
    expect(transport.calls().size() == callCount,
           "faulted session performs no further transport calls");
}

void testReadSuccessAndCallOrder()
{
    constexpr SessionTimeouts timeouts{11ms, 22ms, 33ms};
    constexpr std::array<std::byte, 4> firstPayload{
        std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};
    constexpr std::array<std::byte, 4> secondPayload{
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};

    FakeTransport transport;
    queueSuccessfulRead(transport, firstPayload, 1);
    queueSuccessfulRead(transport, secondPayload, 2);
    RockusbSession session{transport, timeouts};
    auto request = makeRequest();
    std::array<std::byte, firstPayload.size()> destination{};

    const auto firstResult = executeRead(session, request, destination);
    const auto* firstSuccess =
        getSuccess(firstResult, "first read command succeeds");
    expect(firstSuccess != nullptr && firstSuccess->status.tag == 1
               && firstSuccess->dataBytesTransferred == destination.size(),
           "read success exposes the internally assigned tag and byte count");
    expect(destination == firstPayload,
           "read command returns the exact transport payload");
    expect(session.state() == SessionState::ready,
           "successful transaction leaves the session ready");
    expect(transport.calls().size() == 3,
           "read transaction performs exactly CBW, data-in, and CSW");
    expectCall(transport, 0, Operation::bulkOut,
               zzrk::protocol::kRockusbCommandSize, timeouts.command,
               "CBW is sent first with the command timeout");
    expectCall(transport, 1, Operation::bulkIn, destination.size(),
               timeouts.data, "data is received second with the data timeout");
    expectCall(transport, 2, Operation::bulkIn,
               zzrk::protocol::kRockusbStatusSize, timeouts.status,
               "CSW is received last with the status timeout");
    expect(transport.calls()[0].payload[4] == std::byte{0x01}
               && transport.calls()[0].payload[5] == std::byte{0x00}
               && transport.calls()[0].payload[12] == std::byte{0x80},
           "session generates tag 1 and a device-to-host CBW");

    destination.fill(std::byte{0x00});
    const auto secondResult = executeRead(session, request, destination);
    const auto* secondSuccess =
        getSuccess(secondResult, "second read command succeeds");
    expect(secondSuccess != nullptr && secondSuccess->status.tag == 2,
           "session advances its tag for the next command");
    expect(destination == secondPayload,
           "second transaction returns its own payload");
    expect(transport.calls().size() == 6 && !transport.scriptMismatch(),
           "successive transactions remain strictly serialized");
}

void testRequestAndTimeoutValidation()
{
    constexpr std::array<std::byte, 4> payload{};

    {
        FakeTransport transport;
        RockusbSession session{transport};
        std::span<std::byte> destination;
        const auto result = executeRead(session, makeRequest(), destination);
        const auto* error = getError(result, "empty read buffer fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::emptyDataBuffer,
               "read API rejects an empty destination");
        expect(transport.calls().empty()
                   && session.state() == SessionState::ready,
               "request validation error performs no I/O and remains ready");
    }

    {
        FakeTransport transport;
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        auto invalidRequest = makeRequest();
        invalidRequest.logicalUnit = 16;
        const auto invalidResult =
            executeRead(session, invalidRequest, destination);
        const auto* error =
            getError(invalidResult, "unencodable read request fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::commandEncodeFailed
                   && error->commandEncodeError.has_value(),
               "session preserves command codec validation details");

        queueSuccessfulRead(transport, payload, 1);
        const auto validResult =
            executeRead(session, makeRequest(), destination);
        const auto* success =
            getSuccess(validResult, "valid command after validation failure works");
        expect(success != nullptr && success->status.tag == 1,
               "pre-I/O validation failure does not consume an internal tag");
    }

    {
        FakeTransport transport;
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        auto request = makeRequest();

        request.commandLength = 0;
        const auto zeroResult = executeRead(session, request, destination);
        const auto* zeroError =
            getError(zeroResult, "zero command length fails in session");
        expect(zeroError != nullptr
                   && zeroError->code == SessionErrorCode::commandEncodeFailed,
               "session rejects a zero-length command block before I/O");

        request.commandLength = 17;
        const auto longResult = executeRead(session, request, destination);
        const auto* longError =
            getError(longResult, "oversized command length fails in session");
        expect(longError != nullptr
                   && longError->code == SessionErrorCode::commandEncodeFailed,
               "session rejects an oversized command block before I/O");
        expect(transport.calls().empty()
                   && session.state() == SessionState::ready,
               "command length validation leaves transport and state untouched");

        request.commandLength = 1;
        queueSuccessfulRead(transport, payload, 1);
        const auto validResult = executeRead(session, request, destination);
        const auto* success = getSuccess(
            validResult, "valid request after command length failures works");
        expect(success != nullptr && success->status.tag == 1,
               "command length failures do not consume an internal tag");
    }

    {
        FakeTransport transport;
        RockusbSession session{
            transport,
            SessionTimeouts{.command = 0ms, .data = 1ms, .status = 1ms}};
        std::array<std::byte, payload.size()> destination{};
        const auto result =
            executeRead(session, makeRequest(), destination);
        const auto* error = getError(result, "zero timeout fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTimeout
                   && error->stage == SessionStage::command
                   && error->timeout == 0ms,
               "zero is rejected instead of becoming an infinite USB timeout");
        expect(transport.calls().empty()
                   && session.state() == SessionState::ready,
               "timeout validation performs no I/O and remains recoverable");
    }

    {
        FakeTransport transport;
        RockusbSession session{
            transport,
            SessionTimeouts{.command = 1ms, .data = -1ms, .status = 1ms}};
        std::array<std::byte, payload.size()> destination{};
        const auto result =
            executeRead(session, makeRequest(), destination);
        const auto* error = getError(result, "negative data timeout fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTimeout
                   && error->stage == SessionStage::data
                   && error->timeout == -1ms,
               "negative data timeout is rejected before I/O");
        expect(transport.calls().empty(),
               "negative data timeout cannot reach the transport");
    }

    {
        const auto tooLarge = kMaximumSessionTimeout + 1ms;
        FakeTransport transport;
        RockusbSession session{
            transport,
            SessionTimeouts{.command = 1ms,
                            .data = 1ms,
                            .status = tooLarge}};
        std::array<std::byte, payload.size()> destination{};
        const auto result =
            executeRead(session, makeRequest(), destination);
        const auto* error = getError(result, "oversized status timeout fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTimeout
                   && error->stage == SessionStage::status
                   && error->timeout == tooLarge,
               "timeout exceeding the session policy limit is rejected");
        expect(transport.calls().empty(),
               "oversized status timeout cannot reach the transport");
    }
}

void testTransportErrorsFaultTheSession()
{
    constexpr std::array<std::byte, 4> payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    const auto request = makeRequest();

    {
        FakeTransport transport;
        transport.queueOut(
            TransportError{TransportErrorCode::disconnected, -4, 5});
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "CBW transport error fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::transportFailure
                   && error->stage == SessionStage::command
                   && error->actualBytes == 5
                   && error->transportError
                          == TransportError{TransportErrorCode::disconnected,
                                            -4, 5},
               "CBW error retains category, native code, and partial count");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 1,
               "CBW error faults immediately before later stages");
        expectFaultedCallDoesNoIo(session, transport, request, destination);
    }

    {
        FakeTransport transport;
        transport.queueOut(TransportError{
            TransportErrorCode::io,
            -5,
            zzrk::protocol::kRockusbCommandSize + 1,
        });
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error =
            getError(result, "impossible failed-transfer count fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTransferCount
                   && error->stage == SessionStage::command
                   && error->transportError.has_value(),
               "failed transport counts cannot exceed the supplied span");
        expect(session.state() == SessionState::faulted,
               "impossible failed-transfer count faults the session");
    }

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInError(
            TransportError{TransportErrorCode::timedOut, -7, 2});
        const auto status = makeStatus(1);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "data transport error fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::transportFailure
                   && error->stage == SessionStage::data
                   && error->actualBytes == 2
                   && error->transportError.has_value()
                   && error->transportError->code
                          == TransportErrorCode::timedOut,
               "data error retains its partial transfer count");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 2
                   && transport.consumedActions() == 2,
               "data error faults without trying to consume a CSW");
        expectFaultedCallDoesNoIo(session, transport, request, destination);
    }

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInError(
            TransportError{TransportErrorCode::stalled, -9, 0});
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "CSW transport error fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::transportFailure
                   && error->stage == SessionStage::status
                   && error->transportError.has_value()
                   && error->transportError->code
                          == TransportErrorCode::stalled,
               "CSW error retains its transport category");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 3,
               "CSW transport error leaves the session faulted");
        expectFaultedCallDoesNoIo(session, transport, request, destination);
    }
}

void testShortAndImpossibleCountsFaultTheSession()
{
    constexpr std::array<std::byte, 4> payload{
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
    const auto request = makeRequest();

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize - 1);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "short CBW fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::shortTransfer
                   && error->stage == SessionStage::command
                   && error->expectedBytes
                          == zzrk::protocol::kRockusbCommandSize
                   && error->actualBytes
                          == zzrk::protocol::kRockusbCommandSize - 1,
               "short CBW reports exact lengths");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 1,
               "short CBW faults before data-in");
    }

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueIn(payload, TransferSuccess{payload.size() - 1});
        const auto status = makeStatus(1, 1);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "short data-in fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::shortTransfer
                   && error->stage == SessionStage::data
                   && error->expectedBytes == payload.size()
                   && error->actualBytes == payload.size() - 1,
               "short data-in reports exact lengths");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 2
                   && transport.consumedActions() == 2,
               "short data-in conservatively faults without reading CSW");
    }

    {
        const auto status = makeStatus(1);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueIn(
            status,
            TransferSuccess{zzrk::protocol::kRockusbStatusSize - 1});
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "short CSW fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::shortTransfer
                   && error->stage == SessionStage::status
                   && error->expectedBytes
                          == zzrk::protocol::kRockusbStatusSize
                   && error->actualBytes
                          == zzrk::protocol::kRockusbStatusSize - 1,
               "short CSW is rejected before decoding");
        expect(session.state() == SessionState::faulted,
               "short CSW faults the session");
    }

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize + 1);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error =
            getError(result, "impossible transfer count fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTransferCount
                   && error->stage == SessionStage::command,
               "transport cannot report more bytes than its supplied span");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 1,
               "impossible transport count faults immediately");
    }

    {
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueIn(payload, TransferSuccess{payload.size() + 1});
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error =
            getError(result, "impossible data-in count fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTransferCount
                   && error->stage == SessionStage::data,
               "data-in cannot report more bytes than its destination");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 2,
               "impossible data-in count faults before CSW");
    }

    {
        const auto status = makeStatus(1);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueIn(
            status,
            TransferSuccess{zzrk::protocol::kRockusbStatusSize + 1});
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error =
            getError(result, "impossible CSW count fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::invalidTransferCount
                   && error->stage == SessionStage::status,
               "CSW cannot report more bytes than its fixed buffer");
        expect(session.state() == SessionState::faulted
                   && transport.calls().size() == 3,
               "impossible CSW count faults the session");
    }
}

void testStatusValidationAndState()
{
    constexpr std::array<std::byte, 4> payload{
        std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};
    const auto request = makeRequest();

    {
        auto status = makeStatus(1);
        status[0] = std::byte{0x00};
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "invalid CSW signature fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::statusDecodeFailed
                   && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::invalidSignature,
               "session preserves invalid-signature codec details");
        expect(session.state() == SessionState::faulted,
               "invalid CSW signature loses synchronization");
        expectFaultedCallDoesNoIo(session, transport, request, destination);
    }

    {
        const auto status = makeStatus(9);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "mismatched CSW tag fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::statusDecodeFailed
                   && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::tagMismatch
                   && error->statusDecodeError->expectedTag == 1
                   && error->statusDecodeError->actualTag == 9,
               "CSW must match the session-generated CBW tag");
        expect(session.state() == SessionState::faulted,
               "tag mismatch faults the session");
    }

    {
        const auto failedStatus = makeStatus(1, 0, 1);
        const auto successStatus = makeStatus(2);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(failedStatus);
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(successStatus);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};

        const auto failedResult = executeRead(session, request, destination);
        const auto* error =
            getError(failedResult, "device command failure is returned");
        expect(error != nullptr
                   && error->code == SessionErrorCode::deviceCommandFailed
                   && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::commandFailed
                   && error->statusDecodeError->dataResidue == 0,
               "matching command-failed CSW remains an explicit error");
        expect(session.state() == SessionState::ready,
               "complete matching command-failed CSW preserves framing");

        const auto nextResult = executeRead(session, request, destination);
        const auto* success = getSuccess(
            nextResult, "session can execute after command-failed CSW");
        expect(success != nullptr && success->status.tag == 2
                   && session.state() == SessionState::ready,
               "recoverable device failure advances tag and remains usable");
    }

    {
        const auto status = makeStatus(1, 4, 1);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error =
            getError(result, "command failure with residue fails closed");
        expect(error != nullptr
                   && error->code == SessionErrorCode::nonZeroResidue
                   && error->residue == payload.size()
                   && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::commandFailed,
               "command-failed CSW still requires consistent residue");
        expect(session.state() == SessionState::faulted,
               "command failure with inconsistent residue faults the session");
    }

    {
        const auto status = makeStatus(1, 0, 2);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "phase-error CSW fails");
        expect(error != nullptr && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::phaseError,
               "session preserves phase-error status");
        expect(session.state() == SessionState::faulted,
               "phase error faults the session");
    }

    {
        const auto status = makeStatus(1, 0, 0x7f);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "unknown CSW status fails");
        expect(error != nullptr && error->statusDecodeError.has_value()
                   && error->statusDecodeError->code
                          == StatusDecodeErrorCode::invalidStatus,
               "session rejects an unknown CSW status value");
        expect(session.state() == SessionState::faulted,
               "unknown CSW status faults the session");
    }

    {
        const auto status = makeStatus(1, 7, 0);
        FakeTransport transport;
        transport.queueOutSuccess(zzrk::protocol::kRockusbCommandSize);
        transport.queueInSuccess(payload);
        transport.queueInSuccess(status);
        RockusbSession session{transport};
        std::array<std::byte, payload.size()> destination{};
        const auto result = executeRead(session, request, destination);
        const auto* error = getError(result, "nonzero CSW residue fails");
        expect(error != nullptr
                   && error->code == SessionErrorCode::nonZeroResidue
                   && error->stage == SessionStage::status
                   && error->residue == 7,
               "full data transfer requires zero CSW residue");
        expect(session.state() == SessionState::faulted,
               "inconsistent CSW residue faults the session");
    }
}

} // namespace

int main()
{
    testReadSuccessAndCallOrder();
    testRequestAndTimeoutValidation();
    testTransportErrorsFaultTheSession();
    testShortAndImpossibleCountsFaultTheSession();
    testStatusValidationAndState();

    if (failures != 0) {
        std::cerr << failures << " RockUSB session test(s) failed\n";
        return 1;
    }
    return 0;
}
