// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/workflow/FirmwareUpgradeWorkflow.hpp>
#include <zzrk/workflow/ImageDownloadWorkflow.hpp>

#include <string_view>

namespace zzrk::workflow {

class FirmwareMd5Verifier final : public IFirmwareDigestVerifier,
                                  public IFileDigestProvider {
  public:
    [[nodiscard]] FirmwareDigestResult
    verify(image::RkFirmwareImage& firmware) noexcept override;

    [[nodiscard]] FirmwareDigestResult
    digest(const std::filesystem::path& path) noexcept override;
};

[[nodiscard]] std::string_view
describeFirmwareDigestError(FirmwareDigestErrorCode code) noexcept;

} // namespace zzrk::workflow
