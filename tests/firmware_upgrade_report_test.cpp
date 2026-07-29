// SPDX-License-Identifier: MIT

#include <zzrk/qt/FirmwareUpgradeReportWriter.hpp>
#include <zzrk/workflow/FirmwareUpgradeReport.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
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

void testReportModelAndDefaultRedaction()
{
    const auto report = makeSuccessReport();
    expect(report.status ==
               zzrk::workflow::FirmwareUpgradeReportStatus::succeeded &&
               report.plan.has_value() && report.device.has_value() &&
               report.success.has_value() && !report.error.has_value(),
           "completed report retains plan, device identity, and success");
    expect(report.steps.size() == 3U &&
               std::ranges::all_of(report.steps, [](const auto& step) {
                   return step.status == zzrk::workflow::
                                             FirmwareUpgradeReportStepStatus::
                                                 completed;
               }),
           "completed report closes every observed timeline step");

    QTemporaryDir directory;
    expect(directory.isValid(), "temporary report directory is available");
    const auto path = std::filesystem::path{
        directory.filePath(QStringLiteral("report.json")).toStdString()};
    const auto error = zzrk::qt::writeFirmwareUpgradeReport(
        report, path,
        zzrk::qt::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = false,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "diagnostic report is written atomically");

    QFile file{QString::fromStdString(path.string())};
    expect(file.open(QIODevice::ReadOnly), "written diagnostic report opens");
    const QByteArray bytes = file.readAll();
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    const auto firmware = root.value(QStringLiteral("firmware")).toObject();
    expect(root.value(QStringLiteral("schemaVersion")).toInt() == 1 &&
               root.value(QStringLiteral("status")).toString() ==
                   QStringLiteral("succeeded") &&
               firmware.value(QStringLiteral("fileName")).toString() ==
                   QStringLiteral("update.img") &&
               !firmware.contains(QStringLiteral("path")),
           "report JSON exposes its schema and hides the firmware path");
    expect(!bytes.contains("/private/releases") &&
               root.value(QStringLiteral("device")).toObject().value(
                   QStringLiteral("portPath")) == QStringLiteral("1-5") &&
               root.value(QStringLiteral("plan")).toObject().value(
                   QStringLiteral("targetChip")) == QStringLiteral("RK3588"),
           "default report redacts event paths while retaining diagnostics");
}

void testExplicitPathAndFailureStatus()
{
    auto report = makeSuccessReport();
    QTemporaryDir directory;
    const auto path = std::filesystem::path{
        directory.filePath(QStringLiteral("full-report.json")).toStdString()};
    const auto error = zzrk::qt::writeFirmwareUpgradeReport(
        report, path,
        zzrk::qt::FirmwareUpgradeReportWriteOptions{
            .includeFirmwarePath = true,
            .applicationVersion = "test-version",
        });
    expect(!error.has_value(), "explicit full-path report is written");
    QFile file{QString::fromStdString(path.string())};
    expect(file.open(QIODevice::ReadOnly) &&
               file.readAll().contains("/private/releases/update.img"),
           "full firmware path appears only after explicit opt-in");

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
    const auto failed = std::move(builder).takeReport();
    expect(failed.status ==
               zzrk::workflow::FirmwareUpgradeReportStatus::failed &&
               failed.steps.size() == 1U &&
               failed.steps.front().status ==
                   zzrk::workflow::FirmwareUpgradeReportStepStatus::failed &&
               failed.error.has_value(),
           "failed report marks the active operation and terminal error");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application{argc, argv};
    testReportModelAndDefaultRedaction();
    testExplicitPathAndFailureStatus();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
