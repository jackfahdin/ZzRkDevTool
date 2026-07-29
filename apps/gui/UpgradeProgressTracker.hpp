// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareFlasher.hpp>
#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zzrk::gui {

struct UpgradeProgressSnapshot {
  int overallPercent{};
  int operationPercent{};
  bool operationDeterminate{};
};

class UpgradeProgressTracker final {
public:
  UpgradeProgressTracker(const image::RkFirmwareFlashPlan &plan,
                         image::FirmwareVerifyMode verifyMode);

  [[nodiscard]] UpgradeProgressSnapshot
  update(const workflow::FirmwareUpgradeEvent &event);

  [[nodiscard]] UpgradeProgressSnapshot complete() noexcept;

private:
  struct WorkUnit {
    image::FirmwareFlashPhase phase{};
    std::string target;
    std::uint64_t weight{};
  };

  [[nodiscard]] std::optional<std::size_t>
  findWorkUnit(const workflow::FirmwareUpgradeEvent &event) const noexcept;

  std::vector<WorkUnit> m_workUnits;
  std::uint64_t m_totalWeight{};
  int m_overallPercent{};
};

} // namespace zzrk::gui
