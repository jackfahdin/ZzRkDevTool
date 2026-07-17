// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/ReadFlashId.hpp>

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

using zzrk::session::FlashId;
using zzrk::session::FlashIdKind;
using zzrk::session::IUsbTransport;
using zzrk::session::RockusbSession;
using zzrk::session::SessionError;
using zzrk::session::SessionErrorCode;
using zzrk::session::SessionStage;
using zzrk::session::SessionState;
using zzrk::session::TransferResult;
using zzrk::session::TransferSuccess;
using zzrk::session::TransportError;
using zzrk::session::TransportErrorCode;

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
    std::vector<std::byte> payload;
};

class FakeTransport final : public IUsbTransport {
  public:
    void queueOutSuccess()
    {
        actions_.push_back(Action{
            .operation = Operation::bulkOut,
            .result = TransferSuccess{zzrk::protocol::kRockusbCommandSize},
            .payload = {},
        });
    }

    void queueInSuccess(const std::span<const std::byte> payload)
    {
        actions_.push_back(Action{
            .operation = Operation::bulkIn,
            .result = TransferSuccess{payload.size()},
            .payload = {payload.begin(), payload.end()},
        });
    }

    void queueInFailure(const std::span<const std::byte> partialPayload,
                        const TransportError error)
    {
        actions_.push_back(Action{
            .operation = Operation::bulkIn,
            .result = error,
            .payload = {partialPayload.begin(), partialPayload.end()},
        });
    }

    [[nodiscard]] TransferResult
    bulkOut(const std::span<const std::byte> bytes,
            std::chrono::milliseconds) noexcept override
    {
        calls_.push_back(Call{
            .operation = Operation::bulkOut,
            .requestedBytes = bytes.size(),
            .payload = {bytes.begin(), bytes.end()},
        });
        return consume(Operation::bulkOut, {});
    }

    [[nodiscard]] TransferResult
    bulkIn(const std::span<std::byte> bytes,
           std::chrono::milliseconds) noexcept override
    {
        calls_.push_back(Call{
            .operation = Operation::bulkIn,
            .requestedBytes = bytes.size(),
            .payload = {},
        });
        return consume(Operation::bulkIn, bytes);
    }

    [[nodiscard]] const std::vector<Call>& calls() const noexcept
    {
        return calls_;
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

        const auto& action = actions_[nextAction_++];
        if (action.operation != operation) {
            scriptMismatch_ = true;
            return TransportError{TransportErrorCode::other, -2, 0};
        }

        if (operation == Operation::bulkIn) {
            const auto transferred = std::visit(
                [](const auto& result) { return result.bytesTransferred; },
                action.result);
            const auto copySize =
                std::min({destination.size(), action.payload.size(),
                          transferred});
            std::copy_n(action.payload.begin(), copySize, destination.begin());
        }
        return action.result;
    }

    std::vector<Action> actions_;
    std::vector<Call> calls_;
    std::size_t nextAction_{};
    bool scriptMismatch_{};
};

constexpr void writeLe32(zzrk::protocol::RockusbStatusBytes& bytes,
                         const std::size_t offset,
                         const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

constexpr zzrk::protocol::RockusbStatusBytes
makeStatus(const std::uint32_t tag) noexcept
{
    zzrk::protocol::RockusbStatusBytes bytes{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53},
    };
    writeLe32(bytes, 4, tag);
    return bytes;
}

void queueSuccessfulRead(FakeTransport& transport,
                         const std::span<const std::byte> payload,
                         const std::uint32_t tag)
{
    transport.queueOutSuccess();
    transport.queueInSuccess(payload);
    const auto status = makeStatus(tag);
    transport.queueInSuccess(status);
}

constexpr std::array<std::byte, zzrk::protocol::kRockusbCommandSize>
    kTagOneCommand{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x43},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x05}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x80}, std::byte{0x00}, std::byte{0x06}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

constexpr std::array<std::byte, zzrk::session::kFlashIdSize> kEmmcMarker{
    std::byte{0x45},
    std::byte{0x4d},
    std::byte{0x4d},
    std::byte{0x43},
    std::byte{0x20},
};

void testGoldenCommandExactClassificationAndContinuousTags()
{
    FakeTransport transport;
    queueSuccessfulRead(transport, kEmmcMarker, 1);
    queueSuccessfulRead(transport, kEmmcMarker, 2);
    RockusbSession session{transport};

    const auto firstResult = zzrk::session::readFlashId(session);
    const auto* firstFlashId = std::get_if<FlashId>(&firstResult);
    expect(firstFlashId != nullptr
               && *firstFlashId == FlashId{.rawBytes = kEmmcMarker}
               && zzrk::session::classifyFlashId(*firstFlashId)
                      == FlashIdKind::emmcMediaMarker,
           "the exact ASCII EMMC-space payload is classified as its media marker");
    expect(transport.calls().size() == 3,
           "readFlashId performs exactly CBW, data-in, and CSW");
    expect(transport.calls().size() >= 3
               && transport.calls()[0].operation == Operation::bulkOut
               && transport.calls()[0].payload
                      == std::vector<std::byte>{kTagOneCommand.begin(),
                                                kTagOneCommand.end()}
               && transport.calls()[1].operation == Operation::bulkIn
               && transport.calls()[1].requestedBytes
                      == zzrk::session::kFlashIdSize
               && transport.calls()[2].requestedBytes
                      == zzrk::protocol::kRockusbStatusSize,
           "tag-1 CBW and fixed transfer sizes match the protocol golden");

    const auto secondResult = zzrk::session::readFlashId(session);
    const auto* secondFlashId = std::get_if<FlashId>(&secondResult);
    auto tagTwoCommand = kTagOneCommand;
    tagTwoCommand[4] = std::byte{0x02};
    expect(secondFlashId != nullptr
               && zzrk::session::classifyFlashId(*secondFlashId)
                      == FlashIdKind::emmcMediaMarker
               && transport.calls().size() == 6
               && transport.calls()[3].payload
                      == std::vector<std::byte>{tagTwoCommand.begin(),
                                                tagTwoCommand.end()}
               && session.state() == SessionState::ready
               && !transport.scriptMismatch(),
           "consecutive typed reads use continuous session tags");
}

void testUnknownAndNearMatchRemainOpaque()
{
    constexpr std::array<std::byte, zzrk::session::kFlashIdSize> nearMatch{
        std::byte{0x45},
        std::byte{0x4d},
        std::byte{0x4d},
        std::byte{0x43},
        std::byte{0x00},
    };
    constexpr std::array<std::byte, zzrk::session::kFlashIdSize> unknown{
        std::byte{0xde},
        std::byte{0xad},
        std::byte{0xbe},
        std::byte{0xef},
        std::byte{0x01},
    };

    FakeTransport transport;
    queueSuccessfulRead(transport, nearMatch, 1);
    queueSuccessfulRead(transport, unknown, 2);
    RockusbSession session{transport};

    const auto nearResult = zzrk::session::readFlashId(session);
    const auto* nearFlashId = std::get_if<FlashId>(&nearResult);
    expect(nearFlashId != nullptr && nearFlashId->rawBytes == nearMatch
               && zzrk::session::classifyFlashId(*nearFlashId)
                      == FlashIdKind::opaque,
           "EMMC with a non-space final byte remains opaque");

    const auto unknownResult = zzrk::session::readFlashId(session);
    const auto* unknownFlashId = std::get_if<FlashId>(&unknownResult);
    expect(unknownFlashId != nullptr && unknownFlashId->rawBytes == unknown
               && zzrk::session::classifyFlashId(*unknownFlashId)
                      == FlashIdKind::opaque
               && session.state() == SessionState::ready
               && transport.calls().size() == 6
               && !transport.scriptMismatch(),
           "an unknown five-byte value succeeds without inferred identity");
}

void testFaultDiscardsPartialDataAndStopsIo()
{
    constexpr std::array<std::byte, 4> partialPayload{
        std::byte{0x45},
        std::byte{0x4d},
        std::byte{0x4d},
        std::byte{0x43},
    };

    FakeTransport transport;
    transport.queueOutSuccess();
    transport.queueInFailure(
        partialPayload,
        TransportError{TransportErrorCode::timedOut, -7,
                       partialPayload.size()});
    const auto unconsumedStatus = makeStatus(1);
    transport.queueInSuccess(unconsumedStatus);
    RockusbSession session{transport};

    const auto failedResult = zzrk::session::readFlashId(session);
    const auto* error = std::get_if<SessionError>(&failedResult);
    expect(error != nullptr
               && error->code == SessionErrorCode::transportFailure
               && error->stage == SessionStage::data
               && error->actualBytes == partialPayload.size()
               && error->transportError
                      == TransportError{TransportErrorCode::timedOut, -7,
                                        partialPayload.size()},
           "partial data-in returns only the transport SessionError");
    expect(!std::holds_alternative<FlashId>(failedResult)
               && session.state() == SessionState::faulted
               && transport.calls().size() == 2,
           "partial flash ID bytes are not exposed and CSW is not consumed");

    const auto callCount = transport.calls().size();
    const auto rejectedResult = zzrk::session::readFlashId(session);
    const auto* rejectedError = std::get_if<SessionError>(&rejectedResult);
    expect(rejectedError != nullptr
               && rejectedError->code == SessionErrorCode::sessionFaulted
               && transport.calls().size() == callCount,
           "a faulted session rejects subsequent typed reads with zero I/O");
}

} // namespace

int main()
{
    testGoldenCommandExactClassificationAndContinuousTags();
    testUnknownAndNearMatchRemainOpaque();
    testFaultDiscardsPartialDataAndStopsIo();

    if (failures != 0) {
        std::cerr << failures << " read-flash-id test(s) failed\n";
        return 1;
    }
    return 0;
}
