// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <zzrk/transport/LibusbBulkTransport.hpp>

#include <thread>
#include <utility>

namespace zzrk::workflow {
namespace {

[[nodiscard]] constexpr FirmwareUpgradeOpenErrorCode
mapOpenError(const transport::LibusbOpenErrorCode code) noexcept
{
    using transport::LibusbOpenErrorCode;
    switch (code) {
    case LibusbOpenErrorCode::invalidSelection:
        return FirmwareUpgradeOpenErrorCode::invalidSelection;
    case LibusbOpenErrorCode::initializationFailed:
        return FirmwareUpgradeOpenErrorCode::initializationFailed;
    case LibusbOpenErrorCode::enumerationFailed:
        return FirmwareUpgradeOpenErrorCode::enumerationFailed;
    case LibusbOpenErrorCode::topologyReadFailed:
        return FirmwareUpgradeOpenErrorCode::topologyReadFailed;
    case LibusbOpenErrorCode::descriptorReadFailed:
        return FirmwareUpgradeOpenErrorCode::descriptorReadFailed;
    case LibusbOpenErrorCode::deviceNotFound:
        return FirmwareUpgradeOpenErrorCode::deviceNotFound;
    case LibusbOpenErrorCode::deviceAmbiguous:
        return FirmwareUpgradeOpenErrorCode::deviceAmbiguous;
    case LibusbOpenErrorCode::configurationReadFailed:
        return FirmwareUpgradeOpenErrorCode::configurationReadFailed;
    case LibusbOpenErrorCode::rockusbInterfaceNotFound:
        return FirmwareUpgradeOpenErrorCode::interfaceNotFound;
    case LibusbOpenErrorCode::rockusbInterfaceAmbiguous:
        return FirmwareUpgradeOpenErrorCode::interfaceAmbiguous;
    case LibusbOpenErrorCode::invalidEndpointLayout:
        return FirmwareUpgradeOpenErrorCode::invalidEndpointLayout;
    case LibusbOpenErrorCode::openFailed:
        return FirmwareUpgradeOpenErrorCode::openFailed;
    case LibusbOpenErrorCode::kernelDriverQueryFailed:
        return FirmwareUpgradeOpenErrorCode::driverQueryFailed;
    case LibusbOpenErrorCode::kernelDriverActive:
        return FirmwareUpgradeOpenErrorCode::driverActive;
    case LibusbOpenErrorCode::claimFailed:
        return FirmwareUpgradeOpenErrorCode::claimFailed;
    case LibusbOpenErrorCode::resourceExhausted:
        return FirmwareUpgradeOpenErrorCode::resourceExhausted;
    }
    return FirmwareUpgradeOpenErrorCode::openFailed;
}

class LibusbFirmwareUpgradeBackend final : public IFirmwareUpgradeBackend {
  public:
    [[nodiscard]] transport::ScanResult scanDevices() override
    {
        return scanner_.scanRockchipDevices();
    }

    [[nodiscard]] FirmwareUpgradeOpenResult
    openDevice(const transport::UsbDeviceInfo& device) override
    {
        auto opened = device.mode == transport::DeviceMode::Loader
                          ? transport::LibusbBulkTransport::open(device)
                          : transport::LibusbBulkTransport::openMaskrom(device);
        if (const auto* error =
                std::get_if<transport::LibusbOpenError>(&opened)) {
            return FirmwareUpgradeOpenError{
                .code = mapOpenError(error->code),
                .nativeCode = error->nativeCode,
                .accessDenied = transport::isAccessDenied(*error),
            };
        }
        std::unique_ptr<transport::IFlashTransport> transport = std::move(
            std::get<std::unique_ptr<transport::LibusbBulkTransport>>(opened));
        return transport;
    }

    void wait(const std::chrono::milliseconds duration) override
    {
        std::this_thread::sleep_for(duration);
    }

  private:
    transport::UsbDeviceScanner scanner_;
};

} // namespace

std::unique_ptr<IFirmwareUpgradeBackend>
createLibusbFirmwareUpgradeBackend()
{
    return std::make_unique<LibusbFirmwareUpgradeBackend>();
}

} // namespace zzrk::workflow
