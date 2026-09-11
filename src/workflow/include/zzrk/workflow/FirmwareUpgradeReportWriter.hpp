// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace zzrk::workflow {

enum class FirmwareUpgradeReportWriteErrorCode {
    openFailed,
    writeFailed,
    commitFailed,
};

struct FirmwareUpgradeReportWriteError {
    FirmwareUpgradeReportWriteErrorCode code{};
    std::string message;
};

struct FirmwareUpgradeReportWriteOptions {
    bool includeFirmwarePath{};
    std::string applicationVersion;
};

[[nodiscard]] std::optional<FirmwareUpgradeReportWriteError>
writeFirmwareUpgradeReport(
    const FirmwareUpgradeReport& report,
    const std::filesystem::path& outputPath,
    const FirmwareUpgradeReportWriteOptions& options = {});

} // namespace zzrk::workflow
