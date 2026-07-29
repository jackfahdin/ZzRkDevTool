// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>
#include <zzrk/session/IUsbTransport.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace zzrk::image {

enum class MaskromLoaderErrorCode {
  invalidImage,
  imageOpenFailed,
  imageSeekFailed,
  imageReadFailed,
  transferFailed,
  shortTransfer,
  cancelled,
};

struct MaskromLoaderError {
  MaskromLoaderErrorCode code{};
  std::string stage;
  std::uint64_t completedBytes{};
  std::optional<session::TransportError> transportError;
};

struct MaskromLoaderSuccess {
  std::uint32_t entriesDownloaded{};
  std::uint64_t bytesDownloaded{};
};

using MaskromLoaderResult =
    std::variant<MaskromLoaderSuccess, MaskromLoaderError>;
using MaskromLoaderProgressCallback =
    std::function<void(std::string_view, std::uint64_t, std::uint64_t)>;
using MaskromLoaderCancellationCallback = std::function<bool()>;

[[nodiscard]] MaskromLoaderResult downloadRkMaskromLoader(
    session::IUsbTransport &transport, const RkFirmwareImage &image,
    const MaskromLoaderProgressCallback &progress = {},
    const MaskromLoaderCancellationCallback &cancellation = {});

[[nodiscard]] MaskromLoaderResult downloadRkMaskromLoader(
    session::IUsbTransport &transport, const RkLoaderImage &loader,
    const MaskromLoaderProgressCallback &progress = {},
    const MaskromLoaderCancellationCallback &cancellation = {});

} // namespace zzrk::image
