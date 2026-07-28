// SPDX-License-Identifier: MIT

#pragma once

#include <zzrk/image/RkFirmwareImage.hpp>

#include <array>
#include <string_view>
#include <variant>

namespace zzrk::cli {

enum class FirmwareMd5ErrorCode {
    openFailed,
    readFailed,
    mismatch,
};

struct FirmwareMd5Error {
    FirmwareMd5ErrorCode code{};
    std::array<char, 33> actualMd5{};
};

using FirmwareMd5Result =
    std::variant<std::array<char, 33>, FirmwareMd5Error>;

[[nodiscard]] FirmwareMd5Result
verifyFirmwareMd5(image::RkFirmwareImage& firmware) noexcept;

[[nodiscard]] std::string_view
describeFirmwareMd5Error(FirmwareMd5ErrorCode code) noexcept;

} // namespace zzrk::cli
