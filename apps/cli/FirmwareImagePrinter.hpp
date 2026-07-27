// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>

#include <iosfwd>
#include <string_view>

namespace zzrk::cli {

[[nodiscard]] std::string_view describeFirmwareImageError(
    image::FirmwareImageErrorCode code) noexcept;

void printFirmwareImage(const image::RkFirmwareImage& firmware,
                        std::ostream& output);

[[nodiscard]] std::string_view describeFirmwarePlanError(
    image::FirmwarePlanErrorCode code) noexcept;

void printFirmwareFlashPlan(const image::RkFirmwareFlashPlan& plan,
                            std::ostream& output);

} // namespace zzrk::cli
