// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareUpgradeReport.hpp>
#include <zzrk/workflow/FirmwareUpgradeReportWriter.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path makeTempDirectory(
    const std::string_view name)
{
    static int counter = 0;
    const auto directory = std::filesystem::temp_directory_path() /
                           ("zzrk-report-test-" + std::string{name} + "-" +
                            std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

[[nodiscard]] zzrk::workflow::PreparedFirmwareUpgrade makePrepared()
{
    zzrk::image::RkFirmwareImage image{};
    image.sourcePath = "/private/releases/update.img";
    image.model = "report-rk3588";

    zzrk::image::RkFirmwareFlashPlan plan{};
    plan.sourcePath = image.sourcePath;
    plan.targetChip = "RK3588";
    plan.targetModel = image.model;
    plan.minimumDeviceSectors = 4096U;
    plan.totalPartitionBytes = 1024U;
    plan.steps.push_back(zzrk::image::FirmwareFlashStep{
        .kind = zzrk::image::FirmwareFlashStepKind::writePartition,
        .target = "boot_a",
        .entryIndex = 2U,
        .firstLba = 64U,
        .sectorCount = 2U,
        .inputSize = 1024U,
        .outputSize = 1024U,
        .format = zzrk::image::PayloadFormat::raw,
    });

    std::array<char, 33> md5{};
    constexpr std::string_view digest = "0123456789abcdef0123456789abcdef";
    std::copy(digest.cbegin(), digest.cend(), md5.begin());
    return zzrk::workflow::PreparedFirmwareUpgrade{
        .image = std::move(image),
        .plan = std::move(plan),
        .md5 = md5,
    };
}

[[nodiscard]] zzrk::workflow::FirmwareUpgradeReport makeSuccessReport()
{
    const zzrk::workflow::FirmwareUpgradeRequest request{
        .portPath = "1-5",
        .imagePath = "/private/releases/update.img",
        .verifyMode = zzrk::image::FirmwareVerifyMode::all,
        .reboot = true,
        .prepareOnly = false,
    };
    const auto prepared = makePrepared();
    zzrk::workflow::FirmwareUpgradeReportBuilder builder{request};
    builder.setPrepared(prepared);
    builder.observe(zzrk::workflow::FirmwareUpgradeEvent{
        .stage = zzrk::workflow::FirmwareUpgradeStage::verifyImage,
        .target = prepared.image.sourcePath.string(),
        .completedBytes = 0U,
        .totalBytes = 0U,
        .flashPhase = std::nullopt,
        .device = std::nullopt,
    });
    builder.observe(zzrk::workflow::FirmwareUpgradeEvent{
        .stage = zzrk::workflow::FirmwareUpgradeStage::selectDevice,
        .target = "1-5",
        .completedBytes = 0U,
        .totalBytes = 0U,
        .flashPhase = std::nullopt,
        .device = zzrk::transport::UsbDeviceInfo{
            .vendorId = 0x2207U,
            .productId = 0x350bU,
            .bcdUsb = 0x0201U,
            .bcdDevice = 0x0223U,
            .busNumber = 1U,
            .portNumbers = {5U},
            .portPath = "1-5",
            .mode = zzrk::transport::DeviceMode::Loader,
            .deviceAddress = 7U,
        },
    });
    builder.observe(zzrk::workflow::FirmwareUpgradeEvent{
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "boot_a",
        .completedBytes = 1024U,
        .totalBytes = 1024U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::writePartition,
        .device = std::nullopt,
    });

    const zzrk::workflow::FirmwareUpgradeResult result{
        zzrk::workflow::FirmwareUpgradeSuccess{
            .portPath = "1-5",
            .model = prepared.image.model,
            .firmwareMd5 = prepared.md5,
            .deviceSectors = 124'190'720U,
            .partitionBytesWritten = 1024U,
            .sectorsErased = 0U,
            .partitionsVerified = 1U,
            .preparedOnly = false,
            .partitionTableWritten = true,
            .partitionTableVerified = true,
            .loaderWritten = true,
            .loaderVerified = true,
            .resetAccepted = true,
        }};
    builder.finish(result);
    return std::move(builder).takeReport();
}

void overrideTimings(zzrk::workflow::FirmwareUpgradeReport& report)
{
    report.startedAtUnixMilliseconds = 1789110000000LL;
    report.durationMilliseconds = 4200U;
    if (report.steps.size() == 3U) {
        report.steps[0].startedAfterMilliseconds = 0U;
        report.steps[0].durationMilliseconds = 100U;
        report.steps[1].startedAfterMilliseconds = 110U;
        report.steps[1].durationMilliseconds = 20U;
        report.steps[2].startedAfterMilliseconds = 200U;
        report.steps[2].durationMilliseconds = 4000U;
    }
}

constexpr std::string_view kExpectedSuccessJson = R"JSON({
    "schemaVersion": 1,
    "application": {
        "name": "ZzRkDevTool",
        "version": "test-version"
    },
    "startedAt": "2026-09-11T07:00:00.000Z",
    "durationMilliseconds": 4200,
    "status": "succeeded",
    "request": {
        "portPath": "1-5",
        "verifyMode": "all",
        "reboot": true,
        "prepareOnly": false
    },
    "firmware": {
        "fileName": "update.img",
        "model": "report-rk3588",
        "md5": "0123456789abcdef0123456789abcdef"
    },
    "device": {
        "portPath": "1-5",
        "vendorId": 8711,
        "productId": 13579,
        "bcdUsb": 513,
        "bcdDevice": 547,
        "busNumber": 1,
        "portNumbers": [
            5
        ],
        "deviceAddress": 7,
        "mode": "Loader"
    },
    "plan": {
        "targetChip": "RK3588",
        "targetModel": "report-rk3588",
        "minimumDeviceSectors": 4096,
        "totalPartitionBytes": 1024,
        "steps": [
            {
                "kind": "write-partition",
                "target": "boot_a",
                "firstLba": 64,
                "sectorCount": 2,
                "inputSize": 1024,
                "outputSize": 1024,
                "format": "Raw",
                "entryIndex": 2
            }
        ]
    },
    "timeline": [
        {
            "stage": "verify-image",
            "target": "update.img",
            "startedAfterMilliseconds": 0,
            "durationMilliseconds": 100,
            "completedBytes": 0,
            "totalBytes": 0,
            "status": "completed",
            "phase": null
        },
        {
            "stage": "select-device",
            "target": "1-5",
            "startedAfterMilliseconds": 110,
            "durationMilliseconds": 20,
            "completedBytes": 0,
            "totalBytes": 0,
            "status": "completed",
            "phase": null
        },
        {
            "stage": "flash",
            "target": "boot_a",
            "startedAfterMilliseconds": 200,
            "durationMilliseconds": 4000,
            "completedBytes": 1024,
            "totalBytes": 1024,
            "status": "completed",
            "phase": "write-partition"
        }
    ],
    "result": {
        "portPath": "1-5",
        "model": "report-rk3588",
        "firmwareMd5": "0123456789abcdef0123456789abcdef",
        "deviceSectors": 124190720,
        "partitionBytesWritten": 1024,
        "sectorsErased": 0,
        "partitionsVerified": 1,
        "preparedOnly": false,
        "partitionTableWritten": true,
        "partitionTableVerified": true,
        "loaderWritten": true,
        "loaderVerified": true,
        "resetAccepted": true
    },
    "error": null
})JSON";

void testDeterministicOutputAndDefaultRedaction()
{
    auto report = makeSuccessReport();
    overrideTimings(report);

    const auto directory = makeTempDirectory("deterministic");
    const auto path = directory / "report.json";
    const auto error = zzrk::workflow::writeFirmwareUpgradeReport(
        report, path,
        zzrk::workflow::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "diagnostic report is written atomically");

    const auto text = readTextFile(path);
    if (text != kExpectedSuccessJson) {
        std::cerr << "FAIL: deterministic report matches the expected JSON\n"
                  << "actual:\n"
                  << text << '\n';
        ++failures;
    }
    expect(text.find("/private/releases") == std::string::npos &&
               text.find("\"path\"") == std::string::npos,
           "default report redacts the full firmware path");
    std::filesystem::remove_all(directory);
}

void testExplicitPathOptIn()
{
    auto report = makeSuccessReport();
    const auto directory = makeTempDirectory("full-path");
    const auto path = directory / "full-report.json";
    const auto error = zzrk::workflow::writeFirmwareUpgradeReport(
        report, path,
        zzrk::workflow::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = true,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "explicit full-path report is written");
    const auto text = readTextFile(path);
    expect(text.find("\"path\": \"/private/releases/update.img\"") !=
               std::string::npos,
           "full firmware path appears only after explicit opt-in");
    std::filesystem::remove_all(directory);
}

void testFailureReportStructure()
{
    zzrk::workflow::FirmwareUpgradeReportBuilder builder{
        zzrk::workflow::FirmwareUpgradeRequest{
            .portPath = "1-5",
            .imagePath = "update.img",
        }};
    builder.observe(zzrk::workflow::FirmwareUpgradeEvent{
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = "dtbo_b",
        .completedBytes = 443U,
        .totalBytes = 443U,
        .flashPhase = zzrk::image::FirmwareFlashPhase::verifyPartition,
        .device = std::nullopt,
    });
    zzrk::workflow::FirmwareUpgradeError failure{};
    failure.code = zzrk::workflow::FirmwareUpgradeErrorCode::flashFailed;
    failure.stage = zzrk::workflow::FirmwareUpgradeStage::flash;
    failure.target = "dtbo_b";
    failure.completedBytes = 443U;
    const zzrk::workflow::FirmwareUpgradeResult result{failure};
    builder.finish(result);
    auto failed = std::move(builder).takeReport();
    expect(failed.status ==
                   zzrk::workflow::FirmwareUpgradeReportStatus::failed &&
               failed.steps.size() == 1U &&
               failed.steps.front().status ==
                   zzrk::workflow::FirmwareUpgradeReportStepStatus::failed &&
               failed.error.has_value(),
           "failed report marks the active operation and terminal error");
    failed.startedAtUnixMilliseconds = 1789110000000LL;
    failed.durationMilliseconds = 12U;
    failed.steps.front().startedAfterMilliseconds = 5U;
    failed.steps.front().durationMilliseconds = 7U;

    const auto directory = makeTempDirectory("failure");
    const auto path = directory / "failed-report.json";
    const auto error = zzrk::workflow::writeFirmwareUpgradeReport(
        failed, path,
        zzrk::workflow::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "failed report is written");
    const auto text = readTextFile(path);
    expect(text.find("\"status\": \"failed\"") != std::string::npos &&
               text.find("\"code\": \"flash-failed\"") != std::string::npos &&
               text.find("\"stage\": \"flash\"") != std::string::npos &&
               text.find("\"target\": \"dtbo_b\"") != std::string::npos &&
               text.find("\"completedBytes\": 443") != std::string::npos &&
               text.find("\"phase\": \"verify-partition\"") !=
                   std::string::npos &&
               text.find("\"details\": {}") != std::string::npos &&
               text.find("\"result\": null") != std::string::npos &&
               text.find("\"device\": null") != std::string::npos &&
               text.find("\"plan\": null") != std::string::npos &&
               text.find("\"md5\": null") != std::string::npos,
           "failed report JSON carries the typed error and null placeholders");
    std::filesystem::remove_all(directory);
}

void testStringEscaping()
{
    const std::string tricky =
        std::string{"quote\"back\\slash"} + '\x01' + "固件";
    zzrk::workflow::FirmwareUpgradeReportBuilder builder{
        zzrk::workflow::FirmwareUpgradeRequest{
            .portPath = "1-5",
            .imagePath = "update.img",
        }};
    builder.observe(zzrk::workflow::FirmwareUpgradeEvent{
        .stage = zzrk::workflow::FirmwareUpgradeStage::flash,
        .target = tricky,
        .completedBytes = 1U,
        .totalBytes = 1U,
        .flashPhase = std::nullopt,
        .device = std::nullopt,
    });
    zzrk::workflow::FirmwareUpgradeError failure{};
    failure.code = zzrk::workflow::FirmwareUpgradeErrorCode::flashFailed;
    failure.stage = zzrk::workflow::FirmwareUpgradeStage::flash;
    failure.target = tricky;
    const zzrk::workflow::FirmwareUpgradeResult result{failure};
    builder.finish(result);
    auto report = std::move(builder).takeReport();

    const auto directory = makeTempDirectory("escaping");
    const auto path = directory / "escaped-report.json";
    const auto error = zzrk::workflow::writeFirmwareUpgradeReport(
        report, path,
        zzrk::workflow::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "escaping report is written");
    const auto text = readTextFile(path);
    const std::string escaped =
        std::string{"quote\\\"back\\\\slash\\u0001"} + "固件";
    expect(text.find(escaped) != std::string::npos,
           "quotes, backslashes, and control characters are escaped while "
           "UTF-8 passes through");
    expect(text.find(tricky) == std::string::npos,
           "raw control characters never appear in the JSON output");
    std::filesystem::remove_all(directory);
}

void testOpenFailure()
{
    const auto report = makeSuccessReport();
    const auto path = std::filesystem::temp_directory_path() /
                      "zzrk-report-missing-directory" / "report.json";
    std::filesystem::remove_all(path.parent_path());
    const auto error = zzrk::workflow::writeFirmwareUpgradeReport(
        report, path,
        zzrk::workflow::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = "test-version",
        });
    expect(error.has_value() &&
               error->code == zzrk::workflow::
                                  FirmwareUpgradeReportWriteErrorCode::
                                      openFailed &&
               !error->message.empty(),
           "an unwritable output path reports openFailed with a message");
    expect(!std::filesystem::exists(path),
           "failed report writes leave no partial output behind");
}

} // namespace

int main()
{
    testDeterministicOutputAndDefaultRedaction();
    testExplicitPathOptIn();
    testFailureReportStructure();
    testStringEscaping();
    testOpenFailure();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
