// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace zzrk::workflow {

enum class FirmwareUpgradeReportStatus {
    running,
    succeeded,
    failed,
    cancelled,
};

enum class FirmwareUpgradeReportStepStatus {
    running,
    completed,
    failed,
    cancelled,
};

struct FirmwareUpgradeReportStep {
    FirmwareUpgradeStage stage{};
    std::string target;
    std::optional<image::FirmwareFlashPhase> flashPhase;
    std::uint64_t startedAfterMilliseconds{};
    std::uint64_t durationMilliseconds{};
    std::uint64_t completedBytes{};
    std::uint64_t totalBytes{};
    FirmwareUpgradeReportStepStatus status{
        FirmwareUpgradeReportStepStatus::running};
};

struct FirmwareUpgradeReport {
    std::int64_t startedAtUnixMilliseconds{};
    std::uint64_t durationMilliseconds{};
    FirmwareUpgradeReportStatus status{FirmwareUpgradeReportStatus::running};
    FirmwareUpgradeRequest request;
    std::string firmwareModel;
    std::optional<std::array<char, 33>> firmwareMd5;
    std::optional<image::RkFirmwareFlashPlan> plan;
    std::optional<transport::UsbDeviceInfo> device;
    std::vector<FirmwareUpgradeReportStep> steps;
    std::optional<FirmwareUpgradeSuccess> success;
    std::optional<FirmwareUpgradeError> error;
};

class FirmwareUpgradeReportBuilder final {
  public:
    explicit FirmwareUpgradeReportBuilder(FirmwareUpgradeRequest request);

    void setPrepared(const PreparedFirmwareUpgrade& prepared);
    void observe(const FirmwareUpgradeEvent& event);
    void finish(const FirmwareUpgradeResult& result);

    [[nodiscard]] const FirmwareUpgradeReport& report() const noexcept;
    [[nodiscard]] FirmwareUpgradeReport takeReport() && noexcept;

  private:
    using Clock = std::chrono::steady_clock;

    void finishActiveStep(FirmwareUpgradeReportStepStatus status,
                          Clock::time_point now);

    FirmwareUpgradeReport report_;
    Clock::time_point started_;
    std::optional<std::size_t> activeStep_;
    bool finished_{};
};

struct FirmwareUpgradeExecution {
    FirmwareUpgradeResult result;
    FirmwareUpgradeReport report;
};

[[nodiscard]] FirmwareUpgradeExecution runFirmwareUpgradeWithReport(
    FirmwareUpgradeWorkflow& workflow, const FirmwareUpgradeRequest& request,
    const FirmwareUpgradeProgressCallback& progress = {},
    const FirmwareUpgradeCancellationCallback& cancellation = {});

[[nodiscard]] FirmwareUpgradeExecution executeFirmwareUpgradeWithReport(
    FirmwareUpgradeWorkflow& workflow, const FirmwareUpgradeRequest& request,
    PreparedFirmwareUpgrade prepared,
    const FirmwareUpgradeProgressCallback& progress = {},
    const FirmwareUpgradeCancellationCallback& cancellation = {});

} // namespace zzrk::workflow
