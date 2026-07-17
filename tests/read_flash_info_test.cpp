// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>

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

using zzrk::session::FlashInfo;
using zzrk::session::IUsbTransport;
using zzrk::session::LegacyFlashInfoFields;
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
        std::byte{0x0b}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x80}, std::byte{0x00}, std::byte{0x06}, std::byte{0x1a},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

constexpr std::array<std::byte, zzrk::session::kFlashInfoSize> kPayload{
    std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x07},
    std::byte{0x00}, std::byte{0x04}, std::byte{0x04}, std::byte{0x00},
    std::byte{0x28}, std::byte{0x00}, std::byte{0x01},
};

constexpr FlashInfo kExpectedFlashInfo{
    .rawBytes = kPayload,
    .legacy = LegacyFlashInfoFields{
        .sectorCount = 124'190'720,
        .blockSizeCode = 1'024,
        .pageSizeCode = 4,
        .eccBits = 0,
        .accessTime = 40,
        .manufacturerCode = 0,
        .flashMask = 1,
    },
};

void testGoldenCommandDecodeAndContinuousTags()
{
    FakeTransport transport;
    queueSuccessfulRead(transport, kPayload, 1);
    queueSuccessfulRead(transport, kPayload, 2);
    RockusbSession session{transport};

    const auto firstResult = zzrk::session::readFlashInfo(session);
    const auto* firstFlashInfo = std::get_if<FlashInfo>(&firstResult);
    expect(firstFlashInfo != nullptr && *firstFlashInfo == kExpectedFlashInfo,
           "readFlashInfo explicitly decodes every field and preserves raw bytes");
    expect(transport.calls().size() == 3,
           "readFlashInfo performs exactly CBW, data-in, and CSW");
    expect(transport.calls().size() >= 3
               && transport.calls()[0].operation == Operation::bulkOut
               && transport.calls()[0].payload
                      == std::vector<std::byte>{kTagOneCommand.begin(),
                                                kTagOneCommand.end()}
               && transport.calls()[1].operation == Operation::bulkIn
               && transport.calls()[1].requestedBytes
                      == zzrk::session::kFlashInfoSize
               && transport.calls()[2].requestedBytes
                      == zzrk::protocol::kRockusbStatusSize,
           "tag-1 CBW and fixed transfer sizes match the protocol golden");

    const auto secondResult = zzrk::session::readFlashInfo(session);
    const auto* secondFlashInfo = std::get_if<FlashInfo>(&secondResult);
    auto tagTwoCommand = kTagOneCommand;
    tagTwoCommand[4] = std::byte{0x02};
    expect(secondFlashInfo != nullptr && *secondFlashInfo == kExpectedFlashInfo
               && transport.calls().size() == 6
               && transport.calls()[3].payload
                      == std::vector<std::byte>{tagTwoCommand.begin(),
                                                tagTwoCommand.end()}
               && session.state() == SessionState::ready
               && !transport.scriptMismatch(),
           "consecutive typed reads use continuous session tags");
}

void testZeroLegacyCodesRemainOpaqueAndKeepSessionReady()
{
    auto zeroBlockPayload = kPayload;
    zeroBlockPayload[4] = std::byte{0x00};
    zeroBlockPayload[5] = std::byte{0x00};
    auto zeroPagePayload = kPayload;
    zeroPagePayload[6] = std::byte{0x00};

    FakeTransport transport;
    queueSuccessfulRead(transport, zeroBlockPayload, 1);
    queueSuccessfulRead(transport, zeroPagePayload, 2);
    RockusbSession session{transport};

    const auto blockResult = zzrk::session::readFlashInfo(session);
    const auto* zeroBlockInfo = std::get_if<FlashInfo>(&blockResult);
    expect(zeroBlockInfo != nullptr
               && zeroBlockInfo->legacy.blockSizeCode == 0
               && zeroBlockInfo->legacy.pageSizeCode == 4
               && zeroBlockInfo->rawBytes == zeroBlockPayload
               && session.state() == SessionState::ready,
           "zero legacy block code remains opaque and is returned unchanged");

    const auto pageResult = zzrk::session::readFlashInfo(session);
    const auto* zeroPageInfo = std::get_if<FlashInfo>(&pageResult);
    expect(zeroPageInfo != nullptr
               && zeroPageInfo->legacy.blockSizeCode == 1'024
               && zeroPageInfo->legacy.pageSizeCode == 0
               && zeroPageInfo->rawBytes == zeroPagePayload
               && session.state() == SessionState::ready
               && transport.calls().size() == 6
               && !transport.scriptMismatch(),
           "zero legacy page code remains opaque and session stays usable");
}

void testFaultDiscardsPartialDataAndStopsIo()
{
    constexpr std::array<std::byte, 4> partialPayload{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x07}};

    FakeTransport transport;
    transport.queueOutSuccess();
    transport.queueInFailure(
        partialPayload,
        TransportError{TransportErrorCode::timedOut, -7,
                       partialPayload.size()});
    const auto unconsumedStatus = makeStatus(1);
    transport.queueInSuccess(unconsumedStatus);
    RockusbSession session{transport};

    const auto failedResult = zzrk::session::readFlashInfo(session);
    const auto* error = std::get_if<SessionError>(&failedResult);
    expect(error != nullptr
               && error->code == SessionErrorCode::transportFailure
               && error->stage == SessionStage::data
               && error->actualBytes == partialPayload.size()
               && error->transportError
                      == TransportError{TransportErrorCode::timedOut, -7,
                                        partialPayload.size()},
           "partial data-in returns only the transport SessionError");
    expect(!std::holds_alternative<FlashInfo>(failedResult)
               && session.state() == SessionState::faulted
               && transport.calls().size() == 2,
           "partial flash bytes are not exposed and CSW is not consumed");

    const auto callCount = transport.calls().size();
    const auto rejectedResult = zzrk::session::readFlashInfo(session);
    const auto* rejectedError = std::get_if<SessionError>(&rejectedResult);
    expect(rejectedError != nullptr
               && rejectedError->code == SessionErrorCode::sessionFaulted
               && transport.calls().size() == callCount,
           "a faulted session rejects subsequent typed reads with zero I/O");
}

} // namespace

int main()
{
    testGoldenCommandDecodeAndContinuousTags();
    testZeroLegacyCodesRemainOpaqueAndKeepSessionReady();
    testFaultDiscardsPartialDataAndStopsIo();

    if (failures != 0) {
        std::cerr << failures << " read-flash-info test(s) failed\n";
        return 1;
    }
    return 0;
}
