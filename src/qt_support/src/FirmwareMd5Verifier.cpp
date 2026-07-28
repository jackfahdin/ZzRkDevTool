// SPDX-License-Identifier: MIT

#include <zzrk/qt/FirmwareMd5Verifier.hpp>

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>

namespace zzrk::qt {
namespace {

constexpr qint64 kHashChunkBytes = 4 * 1024 * 1024;
constexpr std::uint64_t kTrailingMd5Bytes = 32U;

[[nodiscard]] QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromUtf8(path.native());
#endif
}

[[nodiscard]] std::array<char, 33> toDigestArray(const QByteArray& digest)
{
    std::array<char, 33> value{};
    if (digest.size() == 32) {
        std::copy(digest.cbegin(), digest.cend(), value.begin());
    }
    return value;
}

} // namespace

workflow::FirmwareDigestResult FirmwareMd5Verifier::verify(
    image::RkFirmwareImage& firmware) noexcept
{
    firmware.trailingMd5Verified = false;
    if (firmware.fileSize < kTrailingMd5Bytes ||
        firmware.fileSize - kTrailingMd5Bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<qint64>::max())) {
        return workflow::FirmwareDigestError{
            .code = workflow::FirmwareDigestErrorCode::readFailed,
        };
    }

    QFile file{toQString(firmware.sourcePath)};
    if (!file.open(QIODevice::ReadOnly)) {
        return workflow::FirmwareDigestError{
            .code = workflow::FirmwareDigestErrorCode::openFailed,
        };
    }

    QCryptographicHash hash{QCryptographicHash::Md5};
    QByteArray buffer(kHashChunkBytes, Qt::Uninitialized);
    qint64 remaining = static_cast<qint64>(firmware.fileSize -
                                           kTrailingMd5Bytes);
    while (remaining > 0) {
        const qint64 requested = std::min(remaining, kHashChunkBytes);
        const qint64 bytesRead = file.read(buffer.data(), requested);
        if (bytesRead != requested) {
            return workflow::FirmwareDigestError{
                .code = workflow::FirmwareDigestErrorCode::readFailed,
            };
        }
        hash.addData(QByteArrayView{buffer.constData(), bytesRead});
        remaining -= bytesRead;
    }

    const auto actual = toDigestArray(hash.result().toHex());
    if (actual != firmware.trailingMd5) {
        return workflow::FirmwareDigestError{
            .code = workflow::FirmwareDigestErrorCode::mismatch,
            .actualMd5 = actual,
        };
    }
    firmware.trailingMd5Verified = true;
    return actual;
}

std::string_view describeFirmwareDigestError(
    const workflow::FirmwareDigestErrorCode code) noexcept
{
    using workflow::FirmwareDigestErrorCode;
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

} // namespace zzrk::qt
