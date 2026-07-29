// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>
#include <zzrk/workflow/ImageDownloadWorkflow.hpp>

#include <string_view>

namespace zzrk::qt {

class FirmwareMd5Verifier final : public workflow::IFirmwareDigestVerifier,
                                  public workflow::IFileDigestProvider {
  public:
    [[nodiscard]] workflow::FirmwareDigestResult
    verify(image::RkFirmwareImage& firmware) noexcept override;

    [[nodiscard]] workflow::FirmwareDigestResult
    digest(const std::filesystem::path& path) noexcept override;
};

[[nodiscard]] std::string_view describeFirmwareDigestError(
    workflow::FirmwareDigestErrorCode code) noexcept;

} // namespace zzrk::qt
