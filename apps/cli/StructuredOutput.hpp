// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/session/ReadCapability.hpp>
#include <zzrk/session/ReadChipInfo.hpp>
#include <zzrk/session/ReadFlashId.hpp>
#include <zzrk/session/ReadFlashInfo.hpp>
#include <zzrk/storage/GptPartitionTable.hpp>
#include <zzrk/transport/LibusbBulkTransport.hpp>
#include <zzrk/transport/UsbDeviceScanner.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <iosfwd>
#include <string_view>

namespace zzrk::cli {

struct DeviceInfoReport {
  transport::UsbDeviceInfo device;
  transport::FlashInterfaceInfo interfaceInfo;
  session::ChipInfo chipInfo;
  session::Capability capability;
  session::FlashInfo flashInfo;
  session::FlashId flashId;
};

void printDeviceInfo(const DeviceInfoReport &report, std::ostream &output);
void printDeviceInfoJson(const DeviceInfoReport &report, std::ostream &output);

void printFirmwareImageJson(const image::RkFirmwareImage &firmware,
                            std::ostream &output);
void printFirmwareFlashPlanJson(const image::RkFirmwareFlashPlan &plan,
                                std::ostream &output);

void printJsonError(std::string_view command, std::string_view code,
                    std::string_view message, std::ostream &output);

void printFirmwareUpgradeEventJson(
    const workflow::FirmwareUpgradeEvent &event, std::ostream &output);
void printFirmwareUpgradeSuccessJson(
    const workflow::FirmwareUpgradeSuccess &success, std::ostream &output);
void printFirmwareUpgradeErrorJson(
    const workflow::FirmwareUpgradeError &error, std::string_view message,
    std::ostream &output);

void printPartitions(const storage::GptPartitionTable &table,
                     std::uint64_t deviceSectors, std::ostream &output);
void printPartitionsJson(const storage::GptPartitionTable &table,
                         std::uint64_t deviceSectors, std::ostream &output);
void printPartitionOperationJson(std::string_view command,
                                 std::string_view partition,
                                 std::uint64_t bytes,
                                 std::uint64_t sectors, bool verified,
                                 std::ostream &output);

} // namespace zzrk::cli
