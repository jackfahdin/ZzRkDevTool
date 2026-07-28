// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

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
    out,
    in,
};

struct Action {
    Operation operation{};
    std::vector<std::byte> payload;
};

struct TransportState {
    std::vector<Action> actions;
    std::size_t nextAction{};
    bool mismatch{};
    bool closed{};
};

class FakeTransport final : public zzrk::transport::IFlashTransport {
  public:
    explicit FakeTransport(std::shared_ptr<TransportState> state)
        : state_(std::move(state))
    {
        interface_.bulkInEndpoint = 0x81U;
        interface_.bulkOutEndpoint = 0x01U;
        interface_.bulkInMaxPacketSize = 512U;
        interface_.bulkOutMaxPacketSize = 512U;
    }

    [[nodiscard]] const zzrk::transport::FlashInterfaceInfo&
    interfaceInfo() const noexcept override
    {
        return interface_;
    }

    [[nodiscard]] int close() noexcept override
    {
        state_->closed = true;
        return 0;
    }

    [[nodiscard]] TransferResult bulkOut(
        const std::span<const std::byte> bytes,
        std::chrono::milliseconds) noexcept override
    {
        return consume(Operation::out, bytes.size(), {});
    }

    [[nodiscard]] TransferResult bulkIn(
        const std::span<std::byte> bytes,
        std::chrono::milliseconds) noexcept override
    {
        return consume(Operation::in, bytes.size(), bytes);
    }

  private:
    [[nodiscard]] TransferResult consume(
        const Operation operation, const std::size_t requested,
        const std::span<std::byte> destination) noexcept
    {
        if (state_->nextAction >= state_->actions.size()) {
            state_->mismatch = true;
            return TransportError{TransportErrorCode::other, -1, 0};
        }
        const auto& action = state_->actions[state_->nextAction++];
        if (action.operation != operation ||
            (operation == Operation::in && action.payload.size() != requested)) {
            state_->mismatch = true;
            return TransportError{TransportErrorCode::other, -2, 0};
        }
        if (operation == Operation::in) {
            std::copy(action.payload.begin(), action.payload.end(),
                      destination.begin());
        }
        return TransferSuccess{requested};
    }

    std::shared_ptr<TransportState> state_;
    zzrk::transport::FlashInterfaceInfo interface_{};
};

constexpr void writeLe32(zzrk::protocol::RockusbStatusBytes& bytes,
                         const std::size_t offset,
                         const std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

constexpr zzrk::protocol::RockusbStatusBytes
makeStatus(const std::uint32_t tag, const std::uint32_t residue = 0U)
{
    zzrk::protocol::RockusbStatusBytes bytes{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53},
    };
    writeLe32(bytes, 4U, tag);
    writeLe32(bytes, 8U, residue);
    return bytes;
}

template <std::size_t Size>
void queueIn(TransportState& state,
             const std::array<std::byte, Size>& payload)
{
    state.actions.push_back(Action{
        .operation = Operation::in,
        .payload = {payload.begin(), payload.end()},
    });
}

void queueCommand(TransportState& state)
{
    state.actions.push_back(Action{
        .operation = Operation::out,
        .payload = {},
    });
}

class FakeBackend final : public zzrk::workflow::IFirmwareUpgradeBackend {
  public:
    zzrk::transport::ScanResult scan;
    std::unique_ptr<zzrk::transport::IFlashTransport> transport;
    std::size_t scans{};
    std::size_t opens{};

    [[nodiscard]] zzrk::transport::ScanResult scanDevices() override
    {
        ++scans;
        return scan;
    }

    [[nodiscard]] zzrk::workflow::FirmwareUpgradeOpenResult
    openDevice(const zzrk::transport::UsbDeviceInfo&) override
    {
        ++opens;
        if (!transport) {
            return zzrk::workflow::FirmwareUpgradeOpenError{
                .code = zzrk::workflow::FirmwareUpgradeOpenErrorCode::openFailed,
            };
        }
        return std::move(transport);
    }

    void wait(std::chrono::milliseconds) override {}
};

class FakeDigestVerifier final
    : public zzrk::workflow::IFirmwareDigestVerifier {
  public:
    std::array<char, 33> digest{};
    bool mismatch{};

    [[nodiscard]] zzrk::workflow::FirmwareDigestResult
    verify(zzrk::image::RkFirmwareImage& image) noexcept override
    {
        if (mismatch) {
            return zzrk::workflow::FirmwareDigestError{
                .code = zzrk::workflow::FirmwareDigestErrorCode::mismatch,
                .actualMd5 = {},
            };
        }
        image.trailingMd5Verified = true;
        return digest;
    }
};

zzrk::workflow::PreparedFirmwareUpgrade makePrepared(
    const std::array<char, 33>& digest)
{
    zzrk::image::RkFirmwareImage image{};
    image.sourcePath = "firmware.img";
    image.model = "test-rk3588";
    image.trailingMd5Verified = true;
    zzrk::image::RkFirmwareFlashPlan plan{};
    plan.sourcePath = image.sourcePath;
    plan.targetChip = "RK3588";
    plan.minimumDeviceSectors = 1024U;
    return zzrk::workflow::PreparedFirmwareUpgrade{
        .image = std::move(image),
        .plan = std::move(plan),
        .md5 = digest,
    };
}

void testActiveRamLoaderPrepareOnly()
{
    auto state = std::make_shared<TransportState>();
    queueCommand(*state);
    queueIn(*state, makeStatus(1U, 0x06000000U));

    constexpr std::array<std::byte, 16> chipInfo{
        std::byte{'8'}, std::byte{'8'}, std::byte{'5'}, std::byte{'3'},
    };
    queueCommand(*state);
    queueIn(*state, chipInfo);
    queueIn(*state, makeStatus(2U, 0x10000000U));

    constexpr std::array<std::byte, zzrk::session::kFlashIdSize> emmc{
        std::byte{'E'}, std::byte{'M'}, std::byte{'M'}, std::byte{'C'},
        std::byte{' '},
    };
    queueCommand(*state);
    queueIn(*state, emmc);
    queueIn(*state, makeStatus(1U, 0x05000000U));

    constexpr std::array<std::byte, zzrk::session::kFlashInfoSize> flashInfo{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x07},
        std::byte{0x00}, std::byte{0x04}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x28}, std::byte{0x00}, std::byte{0x01},
    };
    queueCommand(*state);
    queueIn(*state, flashInfo);
    queueIn(*state, makeStatus(2U, 0x0b000000U));

    FakeBackend backend;
    backend.scan.devices.push_back(zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0200U,
        .bcdDevice = 0x0100U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = zzrk::transport::DeviceMode::Maskrom,
        .deviceAddress = 7U,
    });
    backend.transport = std::make_unique<FakeTransport>(state);

    FakeDigestVerifier verifier;
    verifier.digest[0] = 'a';
    zzrk::workflow::FirmwareUpgradeWorkflow workflow{backend, verifier};
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = "1-5",
        .imagePath = "firmware.img",
        .prepareOnly = true,
    };
    const auto result = workflow.execute(request, makePrepared(verifier.digest));
    const auto* success =
        std::get_if<zzrk::workflow::FirmwareUpgradeSuccess>(&result);
    expect(success != nullptr && success->preparedOnly &&
               success->deviceSectors == 124'190'720U,
           "active RAM Loader completes the no-write preparation workflow");
    expect(backend.scans == 1U && backend.opens == 1U && state->closed &&
               !state->mismatch && state->nextAction == state->actions.size(),
           "workflow consumes one strictly framed RAM Loader session");
}

void testChangedDigestFailsBeforeUsbAccess()
{
    FakeBackend backend;
    FakeDigestVerifier verifier;
    verifier.digest[0] = 'a';
    auto prepared = makePrepared(verifier.digest);
    verifier.mismatch = true;
    zzrk::workflow::FirmwareUpgradeWorkflow workflow{backend, verifier};
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = "1-5",
        .imagePath = "firmware.img",
        .prepareOnly = true,
    };
    const auto result = workflow.execute(request, std::move(prepared));
    const auto* error =
        std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::FirmwareUpgradeErrorCode::digestFailed &&
               backend.scans == 0U && backend.opens == 0U,
           "a changed firmware digest is rejected before any USB access");
}

void testDuplicatePortIsRejectedBeforeOpen()
{
    FakeBackend backend;
    const zzrk::transport::UsbDeviceInfo device{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0201U,
        .bcdDevice = 0x0223U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = zzrk::transport::DeviceMode::Loader,
        .deviceAddress = 7U,
    };
    backend.scan.devices = {device, device};
    FakeDigestVerifier verifier;
    verifier.digest[0] = 'a';
    zzrk::workflow::FirmwareUpgradeWorkflow workflow{backend, verifier};
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = "1-5",
        .imagePath = "firmware.img",
        .prepareOnly = true,
    };
    const auto result = workflow.execute(request, makePrepared(verifier.digest));
    const auto* error =
        std::get_if<zzrk::workflow::FirmwareUpgradeError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::FirmwareUpgradeErrorCode::deviceAmbiguous &&
               backend.opens == 0U,
           "duplicate devices at one physical port are rejected before open");
}

} // namespace

int main()
{
    testActiveRamLoaderPrepareOnly();
    testChangedDigestFailsBeforeUsbAccess();
    testDuplicatePortIsRejectedBeforeOpen();
    if (failures != 0) {
        std::cerr << failures << " firmware workflow test(s) failed\n";
        return 1;
    }
    return 0;
}
