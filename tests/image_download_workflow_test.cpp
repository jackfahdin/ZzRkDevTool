// SPDX-License-Identifier: MIT

#include <zzrk/protocol/RockusbCodec.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/workflow/ImageDownloadWorkflow.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

int failures{};

void expect(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class TemporaryFile final {
  public:
    TemporaryFile(std::string name, const std::string_view contents)
        : path_(std::filesystem::temp_directory_path() / std::move(name))
    {
        std::ofstream stream{path_, std::ios::binary | std::ios::trunc};
        stream.write(contents.data(),
                     static_cast<std::streamsize>(contents.size()));
    }

    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class FakeDigestProvider final
    : public zzrk::workflow::IFileDigestProvider {
  public:
    bool changed{};
    std::size_t calls{};

    [[nodiscard]] zzrk::workflow::FirmwareDigestResult
    digest(const std::filesystem::path&) noexcept override
    {
        ++calls;
        std::array<char, 33> value{};
        value[0] = changed ? 'b' : 'a';
        return value;
    }
};

enum class Direction {
    out,
    in,
};

struct Action {
    Direction direction{};
    std::vector<std::byte> payload;
};

struct TransportState {
    std::vector<Action> actions;
    std::size_t next{};
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

    [[nodiscard]] zzrk::session::TransferResult
    bulkOut(const std::span<const std::byte> bytes,
            std::chrono::milliseconds) noexcept override
    {
        return consume(Direction::out, bytes.size(), {});
    }

    [[nodiscard]] zzrk::session::TransferResult
    bulkIn(const std::span<std::byte> bytes,
           std::chrono::milliseconds) noexcept override
    {
        return consume(Direction::in, bytes.size(), bytes);
    }

  private:
    [[nodiscard]] zzrk::session::TransferResult consume(
        const Direction direction, const std::size_t requested,
        const std::span<std::byte> destination) noexcept
    {
        if (state_->next >= state_->actions.size()) {
            state_->mismatch = true;
            return zzrk::session::TransportError{
                .code = zzrk::session::TransportErrorCode::other,
            };
        }
        const auto& action = state_->actions[state_->next++];
        if (action.direction != direction ||
            (direction == Direction::in &&
             action.payload.size() != requested)) {
            state_->mismatch = true;
            return zzrk::session::TransportError{
                .code = zzrk::session::TransportErrorCode::other,
            };
        }
        if (direction == Direction::in) {
            std::copy(action.payload.begin(), action.payload.end(),
                      destination.begin());
        }
        return zzrk::session::TransferSuccess{
            .bytesTransferred = requested,
        };
    }

    std::shared_ptr<TransportState> state_;
    zzrk::transport::FlashInterfaceInfo interface_{};
};

class FakeBackend final : public zzrk::workflow::IFirmwareUpgradeBackend {
  public:
    zzrk::transport::ScanResult scan;
    std::unique_ptr<zzrk::transport::IFlashTransport> transport;
    std::size_t opens{};

    [[nodiscard]] zzrk::transport::ScanResult scanDevices() override
    {
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

void writeLe32(zzrk::protocol::RockusbStatusBytes& bytes,
               const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

zzrk::protocol::RockusbStatusBytes makeStatus(const std::uint32_t tag)
{
    zzrk::protocol::RockusbStatusBytes bytes{
        std::byte{0x55}, std::byte{0x53}, std::byte{0x42}, std::byte{0x53},
    };
    writeLe32(bytes, 4U, tag);
    return bytes;
}

void queueOut(TransportState& state)
{
    state.actions.push_back(Action{.direction = Direction::out, .payload = {}});
}

template <std::size_t Size>
void queueIn(TransportState& state,
             const std::array<std::byte, Size>& payload)
{
    state.actions.push_back(Action{
        .direction = Direction::in,
        .payload = {payload.begin(), payload.end()},
    });
}

void queueStatus(TransportState& state, const std::uint32_t tag)
{
    queueIn(state, makeStatus(tag));
}

void queueWrite(TransportState& state, const std::uint32_t tag)
{
    queueOut(state);
    queueOut(state);
    queueStatus(state, tag);
}

zzrk::transport::UsbDeviceInfo makeDevice(
    const zzrk::transport::DeviceMode mode)
{
    return zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = static_cast<std::uint16_t>(
            mode == zzrk::transport::DeviceMode::Loader ? 0x0201U : 0x0200U),
        .bcdDevice = static_cast<std::uint16_t>(
            mode == zzrk::transport::DeviceMode::Loader ? 0x0223U : 0x0100U),
        .busNumber = 1U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = mode,
        .deviceAddress = 7U,
    };
}

zzrk::workflow::ImageDownloadPrepareRequest makePrepareRequest(
    const TemporaryFile& parameter, const TemporaryFile& boot)
{
    return zzrk::workflow::ImageDownloadPrepareRequest{
        .layoutSource =
            zzrk::workflow::ImageDownloadLayoutSource::parameterFile,
        .deviceSectors = 0U,
        .devicePartitions = {},
        .parameterPath = parameter.path(),
        .loaderPath = {},
        .verifyLoader = false,
        .verifyPartitionTable = false,
        .partitions = {
            zzrk::workflow::ImageDownloadPartitionSelection{
                .target = "boot_a",
                .imagePath = boot.path(),
                .verify = false,
            },
        },
    };
}

void queueSuccessfulDownload(TransportState& state)
{
    queueOut(state);
    queueStatus(state, 1U);
    constexpr std::array<std::byte, 16> chip{
        std::byte{'8'}, std::byte{'8'}, std::byte{'5'}, std::byte{'3'},
    };
    queueOut(state);
    queueIn(state, chip);
    queueStatus(state, 2U);
    constexpr std::array<std::byte, zzrk::session::kFlashIdSize> emmc{
        std::byte{'E'}, std::byte{'M'}, std::byte{'M'}, std::byte{'C'},
        std::byte{' '},
    };
    queueOut(state);
    queueIn(state, emmc);
    queueStatus(state, 3U);
    constexpr std::array<std::byte, zzrk::session::kFlashInfoSize> flashInfo{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x07},
        std::byte{0x00}, std::byte{0x04}, std::byte{0x04}, std::byte{0x00},
        std::byte{0x28}, std::byte{0x00}, std::byte{0x01},
    };
    queueOut(state);
    queueIn(state, flashInfo);
    queueStatus(state, 4U);
    queueWrite(state, 5U);
    queueWrite(state, 6U);
    queueWrite(state, 7U);
}

void testParameterDownloadWritesOnlySelectedItems()
{
    constexpr std::string_view parameterText{
        "MACHINE_MODEL: test-rk3588\n"
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a),"
        "-@0x8100(userdata:grow)\n"};
    TemporaryFile parameter{"zzrk-download-parameter.txt", parameterText};
    TemporaryFile boot{"zzrk-download-boot.img", "abc"};
    FakeDigestProvider digests;
    FakeBackend backend;
    backend.scan.devices.push_back(
        makeDevice(zzrk::transport::DeviceMode::Loader));
    auto state = std::make_shared<TransportState>();
    queueSuccessfulDownload(*state);
    backend.transport = std::make_unique<FakeTransport>(state);
    zzrk::workflow::ImageDownloadWorkflow workflow{backend, digests};
    auto prepared = workflow.prepare(makePrepareRequest(parameter, boot));
    auto* plan =
        std::get_if<zzrk::workflow::PreparedImageDownloadPlan>(&prepared);
    expect(plan != nullptr && plan->items.size() == 2U &&
               plan->items[0].kind ==
                   zzrk::workflow::ImageDownloadItemKind::partitionTable &&
               plan->items[1].target == "boot_a",
           "parameter mode prepares GPT and only the selected partition");
    if (plan == nullptr) {
        return;
    }
    const auto result = workflow.execute(
        zzrk::workflow::ImageDownloadRequest{
            .portPath = "1-5",
            .reboot = false,
        },
        std::move(*plan));
    const auto* success =
        std::get_if<zzrk::workflow::ImageDownloadSuccess>(&result);
    expect(success != nullptr && success->partitionTableWritten &&
               success->itemsWritten == 2U &&
               success->partitionBytesWritten == 3U &&
               !success->resetAccepted && state->closed &&
               !state->mismatch && state->next == state->actions.size(),
           "one session writes GPT and the selected raw image without reset");
}

void testDigestChangeStopsBeforeUsbAccess()
{
    constexpr std::string_view parameterText{
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a),"
        "-@0x8100(userdata:grow)\n"};
    TemporaryFile parameter{"zzrk-download-md5-parameter.txt", parameterText};
    TemporaryFile boot{"zzrk-download-md5-boot.img", "abc"};
    FakeDigestProvider digests;
    FakeBackend backend;
    backend.scan.devices.push_back(
        makeDevice(zzrk::transport::DeviceMode::Loader));
    zzrk::workflow::ImageDownloadWorkflow workflow{backend, digests};
    auto prepared = workflow.prepare(makePrepareRequest(parameter, boot));
    auto* plan =
        std::get_if<zzrk::workflow::PreparedImageDownloadPlan>(&prepared);
    expect(plan != nullptr, "the digest-change fixture prepares normally");
    if (plan == nullptr) {
        return;
    }
    digests.changed = true;
    const auto result = workflow.execute(
        zzrk::workflow::ImageDownloadRequest{.portPath = "1-5"},
        std::move(*plan));
    const auto* error =
        std::get_if<zzrk::workflow::ImageDownloadError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::ImageDownloadErrorCode::digestChanged &&
               backend.opens == 0U,
           "a changed source digest fails before opening USB");
}

void testMaskromRequiresLoader()
{
    constexpr std::string_view parameterText{
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a),"
        "-@0x8100(userdata:grow)\n"};
    TemporaryFile parameter{"zzrk-download-maskrom-parameter.txt",
                            parameterText};
    TemporaryFile boot{"zzrk-download-maskrom-boot.img", "abc"};
    FakeDigestProvider digests;
    FakeBackend backend;
    backend.scan.devices.push_back(
        makeDevice(zzrk::transport::DeviceMode::Maskrom));
    zzrk::workflow::ImageDownloadWorkflow workflow{backend, digests};
    auto prepared = workflow.prepare(makePrepareRequest(parameter, boot));
    auto* plan =
        std::get_if<zzrk::workflow::PreparedImageDownloadPlan>(&prepared);
    expect(plan != nullptr, "the Maskrom fixture prepares without USB access");
    if (plan == nullptr) {
        return;
    }
    const auto result = workflow.execute(
        zzrk::workflow::ImageDownloadRequest{.portPath = "1-5"},
        std::move(*plan));
    const auto* error =
        std::get_if<zzrk::workflow::ImageDownloadError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::ImageDownloadErrorCode::loaderRequired &&
               backend.opens == 0U,
           "Maskrom refuses a download plan without MiniLoaderAll.bin");
}

void testSharedSlotImageIsHashedOncePerPhase()
{
    constexpr std::string_view parameterText{
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x100@0x8000(boot_a),"
        "0x100@0x8100(boot_b),-@0x8200(userdata:grow)\n"};
    TemporaryFile parameter{"zzrk-download-slots-parameter.txt",
                            parameterText};
    TemporaryFile boot{"zzrk-download-slots-boot.img", "abc"};
    FakeDigestProvider digests;
    FakeBackend backend;
    zzrk::workflow::ImageDownloadWorkflow workflow{backend, digests};
    auto request = makePrepareRequest(parameter, boot);
    request.partitions.push_back(
        zzrk::workflow::ImageDownloadPartitionSelection{
            .target = "boot_b",
            .imagePath = boot.path(),
            .verify = true,
        });
    const auto prepared = workflow.prepare(request);
    const auto* plan =
        std::get_if<zzrk::workflow::PreparedImageDownloadPlan>(&prepared);
    expect(plan != nullptr && plan->items.size() == 3U
               && digests.calls == 2U,
           "a shared A/B image is hashed once alongside parameter.txt");
}

} // namespace

int main()
{
    testParameterDownloadWritesOnlySelectedItems();
    testDigestChangeStopsBeforeUsbAccess();
    testMaskromRequiresLoader();
    testSharedSlotImageIsHashedOncePerPhase();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
