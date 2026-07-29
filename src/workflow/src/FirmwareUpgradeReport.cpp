// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <chrono>
#include <utility>
#include <variant>

namespace zzrk::workflow {
namespace {

[[nodiscard]] std::uint64_t elapsedMilliseconds(
    const std::chrono::steady_clock::time_point started,
    const std::chrono::steady_clock::time_point now) noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - started)
            .count());
}

[[nodiscard]] bool sameStep(const FirmwareUpgradeReportStep& step,
                            const FirmwareUpgradeEvent& event) noexcept
{
    return step.stage == event.stage && step.target == event.target &&
           step.flashPhase == event.flashPhase;
}

} // namespace

FirmwareUpgradeReportBuilder::FirmwareUpgradeReportBuilder(
    FirmwareUpgradeRequest request)
    : started_(Clock::now())
{
    report_.startedAtUnixMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    report_.request = std::move(request);
}

void FirmwareUpgradeReportBuilder::setPrepared(
    const PreparedFirmwareUpgrade& prepared)
{
    if (finished_) {
        return;
    }
    report_.firmwareModel = prepared.image.model;
    report_.firmwareMd5 = prepared.md5;
    report_.plan = prepared.plan;
}

void FirmwareUpgradeReportBuilder::observe(const FirmwareUpgradeEvent& event)
{
    if (finished_) {
        return;
    }
    if (event.device.has_value()) {
        report_.device = event.device;
    }

    const auto now = Clock::now();
    if (activeStep_.has_value() &&
        sameStep(report_.steps[*activeStep_], event)) {
        auto& step = report_.steps[*activeStep_];
        step.completedBytes = event.completedBytes;
        step.totalBytes = event.totalBytes;
        return;
    }

    finishActiveStep(FirmwareUpgradeReportStepStatus::completed, now);
    report_.steps.push_back(FirmwareUpgradeReportStep{
        .stage = event.stage,
        .target = event.target,
        .flashPhase = event.flashPhase,
        .startedAfterMilliseconds = elapsedMilliseconds(started_, now),
        .completedBytes = event.completedBytes,
        .totalBytes = event.totalBytes,
    });
    activeStep_ = report_.steps.size() - 1U;
}

void FirmwareUpgradeReportBuilder::finish(const FirmwareUpgradeResult& result)
{
    if (finished_) {
        return;
    }
    const auto now = Clock::now();
    report_.durationMilliseconds = elapsedMilliseconds(started_, now);

    if (const auto* error = std::get_if<FirmwareUpgradeError>(&result)) {
        const auto stepStatus =
            error->code == FirmwareUpgradeErrorCode::cancelled
                ? FirmwareUpgradeReportStepStatus::cancelled
                : FirmwareUpgradeReportStepStatus::failed;
        if (!activeStep_.has_value() ||
            report_.steps[*activeStep_].stage != error->stage ||
            report_.steps[*activeStep_].target != error->target) {
            finishActiveStep(FirmwareUpgradeReportStepStatus::completed, now);
            report_.steps.push_back(FirmwareUpgradeReportStep{
                .stage = error->stage,
                .target = error->target,
                .flashPhase = {},
                .startedAfterMilliseconds = report_.durationMilliseconds,
                .durationMilliseconds = 0U,
                .completedBytes = error->completedBytes,
                .totalBytes = 0U,
                .status = stepStatus,
            });
            activeStep_ = report_.steps.size() - 1U;
        }
        finishActiveStep(stepStatus, now);
        report_.status = error->code == FirmwareUpgradeErrorCode::cancelled
                             ? FirmwareUpgradeReportStatus::cancelled
                             : FirmwareUpgradeReportStatus::failed;
        report_.error = *error;
    } else {
        finishActiveStep(FirmwareUpgradeReportStepStatus::completed, now);
        report_.status = FirmwareUpgradeReportStatus::succeeded;
        report_.success = std::get<FirmwareUpgradeSuccess>(result);
    }
    finished_ = true;
}

const FirmwareUpgradeReport& FirmwareUpgradeReportBuilder::report() const noexcept
{
    return report_;
}

FirmwareUpgradeReport FirmwareUpgradeReportBuilder::takeReport() && noexcept
{
    return std::move(report_);
}

void FirmwareUpgradeReportBuilder::finishActiveStep(
    const FirmwareUpgradeReportStepStatus status, const Clock::time_point now)
{
    if (!activeStep_.has_value()) {
        return;
    }
    auto& step = report_.steps[*activeStep_];
    const auto elapsed = elapsedMilliseconds(started_, now);
    step.durationMilliseconds = elapsed - step.startedAfterMilliseconds;
    step.status = status;
    activeStep_.reset();
}

FirmwareUpgradeExecution runFirmwareUpgradeWithReport(
    FirmwareUpgradeWorkflow& workflow, const FirmwareUpgradeRequest& request,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    FirmwareUpgradeReportBuilder builder{request};
    const auto observedProgress = [&](const FirmwareUpgradeEvent& event) {
        builder.observe(event);
        if (progress) {
            progress(event);
        }
    };

    auto prepared = workflow.prepare(request.imagePath, observedProgress);
    if (const auto* error = std::get_if<FirmwareUpgradeError>(&prepared)) {
        FirmwareUpgradeResult result{*error};
        builder.finish(result);
        return FirmwareUpgradeExecution{
            .result = std::move(result),
            .report = std::move(builder).takeReport(),
        };
    }

    auto value =
        std::get<PreparedFirmwareUpgrade>(std::move(prepared));
    builder.setPrepared(value);
    auto result = workflow.execute(request, std::move(value), observedProgress,
                                   cancellation);
    builder.finish(result);
    return FirmwareUpgradeExecution{
        .result = std::move(result),
        .report = std::move(builder).takeReport(),
    };
}

FirmwareUpgradeExecution executeFirmwareUpgradeWithReport(
    FirmwareUpgradeWorkflow& workflow, const FirmwareUpgradeRequest& request,
    PreparedFirmwareUpgrade prepared,
    const FirmwareUpgradeProgressCallback& progress,
    const FirmwareUpgradeCancellationCallback& cancellation)
{
    FirmwareUpgradeReportBuilder builder{request};
    builder.setPrepared(prepared);
    const auto observedProgress = [&](const FirmwareUpgradeEvent& event) {
        builder.observe(event);
        if (progress) {
            progress(event);
        }
    };
    auto result = workflow.execute(request, std::move(prepared),
                                   observedProgress, cancellation);
    builder.finish(result);
    return FirmwareUpgradeExecution{
        .result = std::move(result),
        .report = std::move(builder).takeReport(),
    };
}

} // namespace zzrk::workflow
