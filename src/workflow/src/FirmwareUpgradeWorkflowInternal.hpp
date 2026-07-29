// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <memory>
#include <string_view>
#include <variant>

namespace zzrk::workflow::detail {

using SelectedDevice =
    std::variant<transport::UsbDeviceInfo, FirmwareUpgradeError>;
using PreparedTransport =
    std::variant<std::unique_ptr<transport::IFlashTransport>,
                 FirmwareUpgradeError>;

[[nodiscard]] bool
isRk3588ChipInfo(const session::ChipInfo& chipInfo) noexcept;

[[nodiscard]] SelectedDevice
selectDevice(const transport::ScanResult& scan, std::string_view portPath);

[[nodiscard]] PreparedTransport
openDevice(IFirmwareUpgradeBackend& backend,
           const transport::UsbDeviceInfo& device,
           FirmwareUpgradeStage stage);

[[nodiscard]] PreparedTransport prepareRamLoader(
    IFirmwareUpgradeBackend& backend,
    std::string_view portPath,
    const transport::UsbDeviceInfo& initialDevice,
    const image::RkLoaderImage* loader,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation);

} // namespace zzrk::workflow::detail
