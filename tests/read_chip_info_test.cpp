// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/ReadChipInfo.hpp>

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

using zzrk::session::ChipInfo;
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
        std::byte{0x10}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x80}, std::byte{0x00}, std::byte{0x06}, std::byte{0x1b},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

constexpr ChipInfo kExpectedChipInfo{
    .bytes = {
        std::byte{0x38}, std::byte{0x38}, std::byte{0x35}, std::byte{0x33},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    },
};

void testGoldenCommandAndContinuousTags()
{
    FakeTransport transport;
    queueSuccessfulRead(transport, kExpectedChipInfo.bytes, 1);
    queueSuccessfulRead(transport, kExpectedChipInfo.bytes, 2);
    RockusbSession session{transport};

    const auto firstResult = zzrk::session::readChipInfo(session);
    const auto* firstChipInfo = std::get_if<ChipInfo>(&firstResult);
    expect(firstChipInfo != nullptr && *firstChipInfo == kExpectedChipInfo,
           "readChipInfo returns the exact 16-byte payload");
    expect(transport.calls().size() == 3,
           "readChipInfo performs exactly CBW, data-in, and CSW");
    expect(transport.calls().size() >= 3
               && transport.calls()[0].operation == Operation::bulkOut
               && transport.calls()[0].payload
                      == std::vector<std::byte>{kTagOneCommand.begin(),
                                                kTagOneCommand.end()}
               && transport.calls()[1].operation == Operation::bulkIn
               && transport.calls()[1].requestedBytes
                      == zzrk::session::kChipInfoSize
               && transport.calls()[2].requestedBytes
                      == zzrk::protocol::kRockusbStatusSize,
           "tag-1 CBW and fixed transfer sizes match the protocol golden");

    const auto secondResult = zzrk::session::readChipInfo(session);
    const auto* secondChipInfo = std::get_if<ChipInfo>(&secondResult);
    expect(secondChipInfo != nullptr && *secondChipInfo == kExpectedChipInfo,
           "a consecutive typed read also succeeds");
    auto tagTwoCommand = kTagOneCommand;
    tagTwoCommand[4] = std::byte{0x02};
    expect(transport.calls().size() == 6
               && transport.calls()[3].payload
                      == std::vector<std::byte>{tagTwoCommand.begin(),
                                                tagTwoCommand.end()}
               && session.state() == SessionState::ready
               && !transport.scriptMismatch(),
           "consecutive reads use continuous session tags");
}

void testFaultDiscardsPartialDataAndStopsIo()
{
    constexpr std::array<std::byte, 4> partialPayload{
        std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}, std::byte{0xdd}};

    FakeTransport transport;
    transport.queueOutSuccess();
    transport.queueInFailure(
        partialPayload,
        TransportError{TransportErrorCode::timedOut, -7,
                       partialPayload.size()});
    const auto unconsumedStatus = makeStatus(1);
    transport.queueInSuccess(unconsumedStatus);
    RockusbSession session{transport};

    const auto failedResult = zzrk::session::readChipInfo(session);
    const auto* error = std::get_if<SessionError>(&failedResult);
    expect(error != nullptr
               && error->code == SessionErrorCode::transportFailure
               && error->stage == SessionStage::data
               && error->actualBytes == partialPayload.size()
               && error->transportError
                      == TransportError{TransportErrorCode::timedOut, -7,
                                        partialPayload.size()},
           "typed read returns only SessionError when data-in fails partially");
    expect(!std::holds_alternative<ChipInfo>(failedResult)
               && session.state() == SessionState::faulted
               && transport.calls().size() == 2,
           "partial chip bytes are not exposed and CSW is not consumed");

    const auto callCount = transport.calls().size();
    const auto rejectedResult = zzrk::session::readChipInfo(session);
    const auto* rejectedError = std::get_if<SessionError>(&rejectedResult);
    expect(rejectedError != nullptr
               && rejectedError->code == SessionErrorCode::sessionFaulted
               && transport.calls().size() == callCount,
           "a faulted session rejects subsequent typed reads with zero I/O");
}

} // namespace

int main()
{
    testGoldenCommandAndContinuousTags();
    testFaultDiscardsPartialDataAndStopsIo();

    if (failures != 0) {
        std::cerr << failures << " read-chip-info test(s) failed\n";
        return 1;
    }
    return 0;
}
