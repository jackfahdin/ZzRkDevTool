// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareMd5Verifier.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path makeTempDirectory()
{
    static int counter = 0;
    const auto directory = std::filesystem::temp_directory_path() /
                           ("zzrk-md5-test-" + std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

[[nodiscard]] std::array<char, 33> toDigestArray(const std::string_view hex)
{
    std::array<char, 33> digest{};
    if (hex.size() == 32U) {
        std::copy(hex.cbegin(), hex.cend(), digest.begin());
    }
    return digest;
}

[[nodiscard]] std::filesystem::path writeFile(
    const std::filesystem::path& directory, const std::string_view name,
    const std::string_view contents)
{
    const auto path = directory / std::string{name};
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return path;
}

void testDigestVectors()
{
    const auto directory = makeTempDirectory();
    zzrk::workflow::FirmwareMd5Verifier verifier;

    const struct {
        std::string_view name;
        std::string_view contents;
        std::string_view expected;
    } vectors[] = {
        {"empty.bin", "", "d41d8cd98f00b204e9800998ecf8427e"},
        {"abc.bin", "abc", "900150983cd24fb0d6963f7d28e17f72"},
        {"message.bin", "message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
        {"alphabet.bin", "abcdefghijklmnopqrstuvwxyz",
         "c3fcd3d76192e4007dfb496cca67e13b"},
        {"alnum.bin",
         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
         "d174ab98d277d9f5a5611c2c9f419d9f"},
        {"digits.bin",
         "123456789012345678901234567890123456789012345678901234567890123456"
         "78901234567890",
         "57edf4a22be3c955ac49da2e2107b67a"},
    };
    for (const auto& entry : vectors) {
        const auto path = writeFile(directory, entry.name, entry.contents);
        const auto result = verifier.digest(path);
        const auto* digest = std::get_if<std::array<char, 33>>(&result);
        expect(digest != nullptr && *digest == toDigestArray(entry.expected),
               "RFC 1321 vector digest matches");
    }

    const std::string large(4U * 1024U * 1024U + 13U, 'a');
    const auto largePath = writeFile(directory, "large.bin", large);
    const auto largeResult = verifier.digest(largePath);
    const auto* largeDigest = std::get_if<std::array<char, 33>>(&largeResult);
    expect(largeDigest != nullptr &&
               *largeDigest ==
                   toDigestArray("9e8d7e4313e37b5611e073dc17a7addc"),
           "digest streams files larger than the 4 MiB hash chunk");

    const auto missing = verifier.digest(directory / "missing.bin");
    const auto* missingError =
        std::get_if<zzrk::workflow::FirmwareDigestError>(&missing);
    expect(missingError != nullptr &&
               missingError->code ==
                   zzrk::workflow::FirmwareDigestErrorCode::readFailed,
           "digest on an unreadable path reports readFailed");
    std::filesystem::remove_all(directory);
}

void testVerifySuccess()
{
    const auto directory = makeTempDirectory();
    const std::string contents =
        std::string{"abc"} + "900150983cd24fb0d6963f7d28e17f72";
    const auto path = writeFile(directory, "firmware.img", contents);

    zzrk::image::RkFirmwareImage firmware{};
    firmware.sourcePath = path;
    firmware.fileSize = contents.size();
    firmware.trailingMd5 = toDigestArray("900150983cd24fb0d6963f7d28e17f72");

    zzrk::workflow::FirmwareMd5Verifier verifier;
    const auto result = verifier.verify(firmware);
    const auto* digest = std::get_if<std::array<char, 33>>(&result);
    expect(digest != nullptr && *digest == firmware.trailingMd5 &&
               firmware.trailingMd5Verified,
           "verify accepts a firmware whose trailing MD5 matches");
    std::filesystem::remove_all(directory);
}

void testVerifyMismatch()
{
    const auto directory = makeTempDirectory();
    const std::string contents =
        std::string{"abc"} + "00000000000000000000000000000000";
    const auto path = writeFile(directory, "firmware.img", contents);

    zzrk::image::RkFirmwareImage firmware{};
    firmware.sourcePath = path;
    firmware.fileSize = contents.size();
    firmware.trailingMd5 = toDigestArray("00000000000000000000000000000000");

    zzrk::workflow::FirmwareMd5Verifier verifier;
    const auto result = verifier.verify(firmware);
    const auto* error =
        std::get_if<zzrk::workflow::FirmwareDigestError>(&result);
    expect(error != nullptr &&
               error->code ==
                   zzrk::workflow::FirmwareDigestErrorCode::mismatch &&
               error->actualMd5 ==
                   toDigestArray("900150983cd24fb0d6963f7d28e17f72") &&
               !firmware.trailingMd5Verified,
           "verify reports mismatch with the actual digest");
    std::filesystem::remove_all(directory);
}

void testVerifyFailures()
{
    const auto directory = makeTempDirectory();
    zzrk::workflow::FirmwareMd5Verifier verifier;

    zzrk::image::RkFirmwareImage missing{};
    missing.sourcePath = directory / "missing.img";
    missing.fileSize = 128U;
    const auto missingResult = verifier.verify(missing);
    const auto* missingError =
        std::get_if<zzrk::workflow::FirmwareDigestError>(&missingResult);
    expect(missingError != nullptr &&
               missingError->code ==
                   zzrk::workflow::FirmwareDigestErrorCode::openFailed,
           "verify on an absent firmware reports openFailed");

    zzrk::image::RkFirmwareImage tooSmall{};
    tooSmall.fileSize = 16U;
    const auto smallResult = verifier.verify(tooSmall);
    const auto* smallError =
        std::get_if<zzrk::workflow::FirmwareDigestError>(&smallResult);
    expect(smallError != nullptr &&
               smallError->code ==
                   zzrk::workflow::FirmwareDigestErrorCode::readFailed,
           "verify rejects firmware smaller than the trailing digest");

    const auto path = writeFile(directory, "truncated.img", "abc");
    zzrk::image::RkFirmwareImage truncated{};
    truncated.sourcePath = path;
    truncated.fileSize = 1024U;
    const auto truncatedResult = verifier.verify(truncated);
    const auto* truncatedError =
        std::get_if<zzrk::workflow::FirmwareDigestError>(&truncatedResult);
    expect(truncatedError != nullptr &&
               truncatedError->code ==
                   zzrk::workflow::FirmwareDigestErrorCode::readFailed,
           "verify reports readFailed when the file ends early");
    std::filesystem::remove_all(directory);
}

void testDescribeError()
{
    using zzrk::workflow::FirmwareDigestErrorCode;
    expect(zzrk::workflow::describeFirmwareDigestError(
               FirmwareDigestErrorCode::openFailed) ==
               "the firmware could not be opened for MD5 verification",
           "openFailed description is stable");
    expect(zzrk::workflow::describeFirmwareDigestError(
               FirmwareDigestErrorCode::readFailed) ==
               "the firmware could not be read during MD5 verification",
           "readFailed description is stable");
    expect(zzrk::workflow::describeFirmwareDigestError(
               FirmwareDigestErrorCode::mismatch) ==
               "the firmware MD5 does not match its trailing digest",
           "mismatch description is stable");
}

} // namespace

int main()
{
    testDigestVectors();
    testVerifySuccess();
    testVerifyMismatch();
    testVerifyFailures();
    testDescribeError();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
