// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareMd5Verifier.hpp>

#include "Md5.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <variant>
#include <vector>

namespace zzrk::workflow {
namespace {

constexpr std::uint64_t kHashChunkBytes = 4U * 1024U * 1024U;
constexpr std::uint64_t kTrailingMd5Bytes = 32U;

[[nodiscard]] FirmwareDigestResult digestFileRange(
    const std::filesystem::path& path, const std::uint64_t bytesToHash)
{
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return FirmwareDigestError{
            .code = FirmwareDigestErrorCode::openFailed,
        };
    }

    Md5 hash;
    std::vector<char> buffer(kHashChunkBytes);
    std::uint64_t remaining = bytesToHash;
    while (remaining > 0U) {
        const auto requested = static_cast<std::streamsize>(
            std::min(remaining, kHashChunkBytes));
        file.read(buffer.data(), requested);
        if (file.gcount() != requested) {
            return FirmwareDigestError{
                .code = FirmwareDigestErrorCode::readFailed,
            };
        }
        hash.update(buffer.data(), static_cast<std::size_t>(requested));
        remaining -= static_cast<std::uint64_t>(requested);
    }
    return hash.finish();
}

} // namespace

FirmwareDigestResult FirmwareMd5Verifier::verify(
    image::RkFirmwareImage& firmware) noexcept
{
    firmware.trailingMd5Verified = false;
    if (firmware.fileSize < kTrailingMd5Bytes) {
        return FirmwareDigestError{
            .code = FirmwareDigestErrorCode::readFailed,
        };
    }

    auto result = digestFileRange(firmware.sourcePath,
                                  firmware.fileSize - kTrailingMd5Bytes);
    if (const auto* actual = std::get_if<std::array<char, 33>>(&result)) {
        if (*actual != firmware.trailingMd5) {
            return FirmwareDigestError{
                .code = FirmwareDigestErrorCode::mismatch,
                .actualMd5 = *actual,
            };
        }
        firmware.trailingMd5Verified = true;
    }
    return result;
}

FirmwareDigestResult FirmwareMd5Verifier::digest(
    const std::filesystem::path& path) noexcept
{
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        return FirmwareDigestError{
            .code = FirmwareDigestErrorCode::readFailed,
        };
    }
    return digestFileRange(path, size);
}

std::string_view describeFirmwareDigestError(
    const FirmwareDigestErrorCode code) noexcept
{
    switch (code) {
    case FirmwareDigestErrorCode::openFailed:
        return "the firmware could not be opened for MD5 verification";
    case FirmwareDigestErrorCode::readFailed:
        return "the firmware could not be read during MD5 verification";
    case FirmwareDigestErrorCode::mismatch:
        return "the firmware MD5 does not match its trailing digest";
    }
    return "unknown firmware MD5 error";
}

} // namespace zzrk::workflow
