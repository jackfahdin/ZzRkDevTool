// SPDX-License-Identifier: MIT

#include "UpgradeProgressTracker.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

zzrk::image::RkFirmwareFlashPlan makePlan() {
  using zzrk::image::FirmwareFlashStep;
  using zzrk::image::FirmwareFlashStepKind;
  using zzrk::image::PayloadFormat;
  zzrk::image::RkFirmwareFlashPlan plan{};
  plan.targetChip = "RK3588";
  plan.steps = {
      FirmwareFlashStep{
          .kind = FirmwareFlashStepKind::upgradeLoader,
          .target = "eMMC IDBlock",
          .entryIndex = std::nullopt,
          .firstLba = 0U,
          .sectorCount = 0U,
          .inputSize = 0U,
          .outputSize = 320U * 1024U,
          .format = PayloadFormat::raw,
      },
      FirmwareFlashStep{
          .kind = FirmwareFlashStepKind::ensurePartitionTable,
          .target = "primary and backup GPT",
          .entryIndex = std::nullopt,
          .firstLba = 0U,
          .sectorCount = 0U,
          .inputSize = 0U,
          .outputSize = 34U * 1024U,
          .format = PayloadFormat::raw,
      },
      FirmwareFlashStep{
          .kind = FirmwareFlashStepKind::writePartition,
          .target = "boot_a",
          .entryIndex = std::nullopt,
          .firstLba = 0U,
          .sectorCount = 0U,
          .inputSize = 0U,
          .outputSize = 80U * 1024U * 1024U,
          .format = PayloadFormat::raw,
      },
      FirmwareFlashStep{
          .kind = FirmwareFlashStepKind::writePartition,
          .target = "super",
          .entryIndex = std::nullopt,
          .firstLba = 0U,
          .sectorCount = 0U,
          .inputSize = 0U,
          .outputSize = 5ULL * 1024U * 1024U * 1024U,
          .format = PayloadFormat::androidSparse,
      },
  };
  return plan;
}

zzrk::workflow::FirmwareUpgradeEvent
flashEvent(const zzrk::image::FirmwareFlashPhase phase, std::string target,
           const std::uint64_t completed, const std::uint64_t total) {
  return zzrk::workflow::FirmwareUpgradeEvent{
      .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
      .target = std::move(target),
      .completedBytes = completed,
      .totalBytes = total,
      .flashPhase = phase,
      .device = std::nullopt,
  };
}

void testCriticalProgressIsCumulative() {
  using zzrk::image::FirmwareFlashPhase;
  zzrk::gui::UpgradeProgressTracker tracker{
      makePlan(), zzrk::image::FirmwareVerifyMode::critical};

  std::vector<zzrk::workflow::FirmwareUpgradeEvent> events{
      {.stage = zzrk::workflow::FirmwareUpgradeStage::selectDevice,
       .target = {},
       .completedBytes = 0U,
       .totalBytes = 0U,
       .flashPhase = std::nullopt,
       .device = std::nullopt},
      {.stage = zzrk::workflow::FirmwareUpgradeStage::checkStorage,
       .target = {},
       .completedBytes = 0U,
       .totalBytes = 0U,
       .flashPhase = std::nullopt,
       .device = std::nullopt},
      flashEvent(FirmwareFlashPhase::writeLoader, {}, 160U, 320U),
      flashEvent(FirmwareFlashPhase::writeLoader, {}, 320U, 320U),
      flashEvent(FirmwareFlashPhase::verifyLoader, {}, 320U, 320U),
      flashEvent(FirmwareFlashPhase::writePartitionTable, {}, 1U, 1U),
      flashEvent(FirmwareFlashPhase::verifyPartitionTable, {}, 1U, 1U),
      flashEvent(FirmwareFlashPhase::writePartition, "boot_a", 80U, 80U),
      flashEvent(FirmwareFlashPhase::erasePartition, "super", 50U, 100U),
      flashEvent(FirmwareFlashPhase::erasePartition, "super", 100U, 100U),
      flashEvent(FirmwareFlashPhase::writePartition, "super", 100U, 100U),
      flashEvent(FirmwareFlashPhase::verifyPartition, "boot_a", 80U, 80U),
  };

  int previous = 0;
  bool sawIndependentPercentages = false;
  for (const auto &event : events) {
    const auto snapshot = tracker.update(event);
    expect(snapshot.overallPercent >= previous,
           "overall firmware progress never moves backwards");
    expect(snapshot.overallPercent >= 0 && snapshot.overallPercent <= 99,
           "in-flight overall firmware progress stays below completion");
    expect(snapshot.operationPercent >= 0 && snapshot.operationPercent <= 100,
           "current operation progress remains a valid percentage");
    sawIndependentPercentages =
        sawIndependentPercentages ||
        (snapshot.operationDeterminate &&
         snapshot.operationPercent != snapshot.overallPercent);
    previous = snapshot.overallPercent;
  }
  expect(previous == 99,
         "the final flash event reaches 99 percent before reset completion");
  expect(sawIndependentPercentages,
         "overall and current operation percentages remain independent");

  const auto complete = tracker.complete();
  expect(complete.overallPercent == 100 && complete.operationPercent == 100,
         "a successful terminal result completes both progress values");
}

void testVerificationPolicyMatchesFlasher() {
  using zzrk::image::FirmwareVerifyMode;
  expect(zzrk::image::shouldVerifyFirmwarePartition(
             FirmwareVerifyMode::critical, "boot_a"),
         "critical verification includes boot_a");
  expect(!zzrk::image::shouldVerifyFirmwarePartition(
             FirmwareVerifyMode::critical, "super"),
         "critical verification excludes super");
  expect(zzrk::image::shouldVerifyFirmwarePartition(FirmwareVerifyMode::all,
                                                    "super"),
         "all verification includes super");
  expect(!zzrk::image::shouldVerifyFirmwarePartition(FirmwareVerifyMode::none,
                                                     "boot_a"),
         "none verification excludes every partition");
}

void testVerificationModesReachTheFinalFlashBoundary() {
  using zzrk::image::FirmwareFlashPhase;
  using zzrk::image::FirmwareVerifyMode;

  zzrk::gui::UpgradeProgressTracker allTracker{makePlan(),
                                               FirmwareVerifyMode::all};
  const auto allComplete = allTracker.update(flashEvent(
      FirmwareFlashPhase::verifyPartition, "super", 100U, 100U));
  expect(allComplete.overallPercent == 99,
         "all verification reaches the final in-flight boundary");

  zzrk::gui::UpgradeProgressTracker noneTracker{makePlan(),
                                                FirmwareVerifyMode::none};
  const auto noneComplete = noneTracker.update(flashEvent(
      FirmwareFlashPhase::writePartition, "super", 100U, 100U));
  expect(noneComplete.overallPercent == 99,
         "none verification reaches the final in-flight boundary");
}

} // namespace

int main() {
  testCriticalProgressIsCumulative();
  testVerificationPolicyMatchesFlasher();
  testVerificationModesReachTheFinalFlashBoundary();
  if (failures != 0) {
    std::cerr << failures << " upgrade progress test(s) failed\n";
    return 1;
  }
  return 0;
}
