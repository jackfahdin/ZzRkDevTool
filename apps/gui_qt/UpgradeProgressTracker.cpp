// SPDX-License-Identifier: MIT

#include "UpgradeProgressTracker.hpp"

#include <zzrk/core/StorageUnits.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace zzrk::gui {
namespace {

constexpr int kFlashStartPercent = 5;
constexpr int kFlashEndPercent = 99;
constexpr std::uint64_t kPrimaryGptBytes = 34U * core::kSectorSize;
constexpr std::uint64_t kAllGptBytes = 67U * core::kSectorSize;

[[nodiscard]] constexpr std::uint64_t
saturatingAdd(const std::uint64_t left, const std::uint64_t right) noexcept {
  return right > std::numeric_limits<std::uint64_t>::max() - left
             ? std::numeric_limits<std::uint64_t>::max()
             : left + right;
}

[[nodiscard]] constexpr std::uint64_t
unitWeight(const image::FirmwareFlashStep &step) noexcept {
  return std::max<std::uint64_t>(step.outputSize, 1U);
}

} // namespace

UpgradeProgressTracker::UpgradeProgressTracker(
    const image::RkFirmwareFlashPlan &plan,
    const image::FirmwareVerifyMode verifyMode) {
  const auto addUnit = [this](const image::FirmwareFlashPhase phase,
                              std::string target, const std::uint64_t weight) {
    m_workUnits.push_back(WorkUnit{
        .phase = phase,
        .target = std::move(target),
        .weight = std::max<std::uint64_t>(weight, 1U),
    });
    m_totalWeight = saturatingAdd(m_totalWeight, m_workUnits.back().weight);
  };

  for (const auto &step : plan.steps) {
    if (step.kind == image::FirmwareFlashStepKind::upgradeLoader) {
      addUnit(image::FirmwareFlashPhase::writeLoader, {}, unitWeight(step));
      if (verifyMode != image::FirmwareVerifyMode::none) {
        addUnit(image::FirmwareFlashPhase::verifyLoader, {}, unitWeight(step));
      }
      break;
    }
  }

  addUnit(image::FirmwareFlashPhase::writePartitionTable, {}, kAllGptBytes);
  if (verifyMode != image::FirmwareVerifyMode::none) {
    addUnit(image::FirmwareFlashPhase::verifyPartitionTable, {},
            kPrimaryGptBytes);
  }

  for (const auto &step : plan.steps) {
    if (step.kind != image::FirmwareFlashStepKind::writePartition) {
      continue;
    }
    if (step.format == image::PayloadFormat::androidSparse) {
      addUnit(image::FirmwareFlashPhase::erasePartition, step.target,
              unitWeight(step));
    }
    addUnit(image::FirmwareFlashPhase::writePartition, step.target,
            unitWeight(step));
  }

  for (const auto &step : plan.steps) {
    if (step.kind == image::FirmwareFlashStepKind::writePartition &&
        image::shouldVerifyFirmwarePartition(verifyMode, step.target)) {
      addUnit(image::FirmwareFlashPhase::verifyPartition, step.target,
              unitWeight(step));
    }
  }
}

UpgradeProgressSnapshot
UpgradeProgressTracker::update(const workflow::FirmwareUpgradeEvent &event) {
  UpgradeProgressSnapshot snapshot{};
    if (event.totalBytes != 0U) {
        const auto completed = std::min(event.completedBytes, event.totalBytes);
        snapshot.operationPercent = static_cast<int>(
            static_cast<long double>(completed) * 100.0L
            / static_cast<long double>(event.totalBytes));
    snapshot.operationDeterminate = true;
  }

  int candidate = m_overallPercent;
  using workflow::FirmwareUpgradeStage;
  switch (event.stage) {
  case FirmwareUpgradeStage::parseImage:
  case FirmwareUpgradeStage::verifyImage:
  case FirmwareUpgradeStage::planImage:
    candidate = 0;
    break;
  case FirmwareUpgradeStage::selectDevice:
  case FirmwareUpgradeStage::enterMaskrom:
    candidate = 1;
    break;
  case FirmwareUpgradeStage::waitForMaskrom:
    candidate = 2;
    break;
  case FirmwareUpgradeStage::downloadLoader:
    candidate =
        snapshot.operationDeterminate ? 2 + snapshot.operationPercent / 100 : 2;
    break;
  case FirmwareUpgradeStage::waitForRamLoader:
    candidate = 3;
    break;
  case FirmwareUpgradeStage::checkRamLoader:
    candidate = 4;
    break;
  case FirmwareUpgradeStage::checkStorage:
    candidate = kFlashStartPercent;
    break;
  case FirmwareUpgradeStage::flash: {
    const auto index = findWorkUnit(event);
    if (!index.has_value() || m_totalWeight == 0U) {
      candidate = kFlashStartPercent;
      break;
    }
    std::uint64_t completedWeight = 0U;
    for (std::size_t current = 0; current < *index; ++current) {
      completedWeight =
          saturatingAdd(completedWeight, m_workUnits[current].weight);
    }
    const long double fraction =
        event.totalBytes == 0U ? 0.0L
                               : static_cast<long double>(std::min(
                                     event.completedBytes, event.totalBytes)) /
                                     static_cast<long double>(event.totalBytes);
    const long double weighted =
        static_cast<long double>(completedWeight) +
        static_cast<long double>(m_workUnits[*index].weight) * fraction;
    const long double flashRange =
        static_cast<long double>(kFlashEndPercent - kFlashStartPercent);
    candidate = kFlashStartPercent +
                static_cast<int>(flashRange * weighted /
                                 static_cast<long double>(m_totalWeight));
    break;
  }
  case FirmwareUpgradeStage::reset:
    candidate = kFlashEndPercent;
    break;
  }

  m_overallPercent = std::max(m_overallPercent, std::clamp(candidate, 0, 100));
  snapshot.overallPercent = m_overallPercent;
  return snapshot;
}

UpgradeProgressSnapshot UpgradeProgressTracker::complete() noexcept {
  m_overallPercent = 100;
  return UpgradeProgressSnapshot{
      .overallPercent = 100,
      .operationPercent = 100,
      .operationDeterminate = true,
  };
}

std::optional<std::size_t> UpgradeProgressTracker::findWorkUnit(
    const workflow::FirmwareUpgradeEvent &event) const noexcept {
  if (!event.flashPhase.has_value()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < m_workUnits.size(); ++index) {
    const auto &unit = m_workUnits[index];
    if (unit.phase == *event.flashPhase &&
        (unit.target.empty() || unit.target == event.target)) {
      return index;
    }
  }
  return std::nullopt;
}

} // namespace zzrk::gui
