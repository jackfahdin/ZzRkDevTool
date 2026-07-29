// SPDX-License-Identifier: MIT

#include <zzrk/workflow/DeviceManagementWorkflow.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

zzrk::transport::UsbDeviceInfo supportedLoader()
{
    return zzrk::transport::UsbDeviceInfo{
        .vendorId = 0x2207U,
        .productId = 0x350bU,
        .bcdUsb = 0x0201U,
        .bcdDevice = 0x0223U,
        .busNumber = 1U,
        .portNumbers = {5U},
        .portPath = "1-5",
        .mode = zzrk::transport::DeviceMode::Loader,
        .deviceAddress = 7U,
    };
}

class ErrorTransport final : public zzrk::transport::IFlashTransport {
  public:
    explicit ErrorTransport(std::shared_ptr<bool> closed)
        : closed_(std::move(closed))
    {
    }

    [[nodiscard]] const zzrk::transport::FlashInterfaceInfo&
    interfaceInfo() const noexcept override
    {
        return interface_;
    }

    [[nodiscard]] int close() noexcept override
    {
        *closed_ = true;
        return 0;
    }

    [[nodiscard]] zzrk::session::TransferResult bulkOut(
        std::span<const std::byte>, std::chrono::milliseconds) noexcept override
    {
        return zzrk::session::TransportError{
            zzrk::session::TransportErrorCode::other, -5, 0U};
    }

    [[nodiscard]] zzrk::session::TransferResult bulkIn(
        std::span<std::byte>, std::chrono::milliseconds) noexcept override
    {
        return zzrk::session::TransportError{
            zzrk::session::TransportErrorCode::other, -5, 0U};
    }

  private:
    std::shared_ptr<bool> closed_;
    zzrk::transport::FlashInterfaceInfo interface_{};
};

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
                .code =
                    zzrk::workflow::FirmwareUpgradeOpenErrorCode::openFailed,
            };
        }
        return std::move(transport);
    }

    void wait(std::chrono::milliseconds) override {}
};

void expectInspectionError(FakeBackend& backend,
                           const zzrk::workflow::DeviceManagementErrorCode code,
                           const std::string_view description)
{
    zzrk::workflow::DeviceManagementWorkflow workflow{backend};
    const auto result = workflow.inspect("1-5");
    const auto* error =
        std::get_if<zzrk::workflow::DeviceManagementError>(&result);
    expect(error != nullptr && error->code == code, description);
}

void testSelectionFailures()
{
    FakeBackend scanFailure;
    scanFailure.scan.error = zzrk::transport::ScanError{-1, "scan failed"};
    expectInspectionError(scanFailure,
                          zzrk::workflow::DeviceManagementErrorCode::scanFailed,
                          "scan failures are preserved");

    FakeBackend missing;
    expectInspectionError(missing,
                          zzrk::workflow::DeviceManagementErrorCode::deviceNotFound,
                          "missing port is rejected");

    FakeBackend duplicate;
    duplicate.scan.devices = {supportedLoader(), supportedLoader()};
    expectInspectionError(
        duplicate,
        zzrk::workflow::DeviceManagementErrorCode::deviceAmbiguous,
        "duplicate physical port is rejected");

    FakeBackend unsupported;
    auto maskrom = supportedLoader();
    maskrom.mode = zzrk::transport::DeviceMode::Maskrom;
    unsupported.scan.devices.push_back(maskrom);
    expectInspectionError(
        unsupported,
        zzrk::workflow::DeviceManagementErrorCode::deviceUnsupported,
        "non-Loader device is rejected");
}

void testInspectionSessionFailure()
{
    FakeBackend backend;
    backend.scan.devices.push_back(supportedLoader());
    auto closed = std::make_shared<bool>(false);
    auto transport = std::make_unique<ErrorTransport>(closed);
    backend.transport = std::move(transport);

    zzrk::workflow::DeviceManagementWorkflow workflow{backend};
    const auto result = workflow.inspect("1-5");
    const auto* error =
        std::get_if<zzrk::workflow::DeviceManagementError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::DeviceManagementErrorCode::chipInfoFailed,
           "inspect maps session failures");
    expect(*closed, "inspect closes transport after session failure");
}

void testFilePreconditions()
{
    FakeBackend backend;
    zzrk::workflow::DeviceManagementWorkflow workflow{backend};
    const auto directory = std::filesystem::temp_directory_path();
    const auto output = directory / "zzrk-device-management-existing.img";
    {
        std::ofstream file{output, std::ios::binary | std::ios::trunc};
        file << "existing";
    }
    const auto read = workflow.readPartition("1-5", "boot_a", output);
    const auto* readError =
        std::get_if<zzrk::workflow::DeviceManagementError>(&read);
    expect(readError != nullptr &&
               readError->code ==
                   zzrk::workflow::DeviceManagementErrorCode::outputExists,
           "partition backup never overwrites an existing output");
    expect(backend.scans == 0U,
           "output collision is rejected before opening USB");
    std::error_code removeError;
    std::filesystem::remove(output, removeError);

    const auto missingInput =
        directory / "zzrk-device-management-missing-input.img";
    std::filesystem::remove(missingInput, removeError);
    const auto write =
        workflow.writePartition("1-5", "boot_a", missingInput, true);
    const auto* writeError =
        std::get_if<zzrk::workflow::DeviceManagementError>(&write);
    expect(writeError != nullptr &&
               writeError->code ==
                   zzrk::workflow::DeviceManagementErrorCode::inputInvalid,
           "missing partition image is rejected");
    expect(backend.scans == 0U,
           "invalid input is rejected before opening USB");
}

} // namespace

int main()
{
    testSelectionFailures();
    testInspectionSessionFailure();
    testFilePreconditions();
    return failures == 0 ? 0 : 1;
}
