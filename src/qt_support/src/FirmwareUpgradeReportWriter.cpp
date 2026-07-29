// SPDX-License-Identifier: MIT

#include <zzrk/qt/FirmwareUpgradeReportWriter.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QString>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace zzrk::qt {
namespace {

[[nodiscard]] QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    return QString::fromUtf8(path.native());
#endif
}

[[nodiscard]] QString toQString(const std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString digestText(const std::array<char, 33>& digest)
{
    const auto end = std::find(digest.cbegin(), digest.cend(), '\0');
    return QString::fromLatin1(
        digest.data(), static_cast<qsizetype>(end - digest.cbegin()));
}

[[nodiscard]] QJsonValue jsonInteger(const std::uint64_t value)
{
    return QJsonValue{static_cast<qint64>(value)};
}

[[nodiscard]] constexpr std::string_view verifyModeText(
    const image::FirmwareVerifyMode mode) noexcept
{
    switch (mode) {
    case image::FirmwareVerifyMode::none:
        return "none";
    case image::FirmwareVerifyMode::critical:
        return "critical";
    case image::FirmwareVerifyMode::all:
        return "all";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view reportStatusText(
    const workflow::FirmwareUpgradeReportStatus status) noexcept
{
    using workflow::FirmwareUpgradeReportStatus;
    switch (status) {
    case FirmwareUpgradeReportStatus::running:
        return "running";
    case FirmwareUpgradeReportStatus::succeeded:
        return "succeeded";
    case FirmwareUpgradeReportStatus::failed:
        return "failed";
    case FirmwareUpgradeReportStatus::cancelled:
        return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view stepStatusText(
    const workflow::FirmwareUpgradeReportStepStatus status) noexcept
{
    using workflow::FirmwareUpgradeReportStepStatus;
    switch (status) {
    case FirmwareUpgradeReportStepStatus::running:
        return "running";
    case FirmwareUpgradeReportStepStatus::completed:
        return "completed";
    case FirmwareUpgradeReportStepStatus::failed:
        return "failed";
    case FirmwareUpgradeReportStepStatus::cancelled:
        return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view flashPhaseText(
    const image::FirmwareFlashPhase phase) noexcept
{
    using image::FirmwareFlashPhase;
    switch (phase) {
    case FirmwareFlashPhase::writePartitionTable:
        return "write-partition-table";
    case FirmwareFlashPhase::verifyPartitionTable:
        return "verify-partition-table";
    case FirmwareFlashPhase::erasePartition:
        return "erase-partition";
    case FirmwareFlashPhase::writePartition:
        return "write-partition";
    case FirmwareFlashPhase::verifyPartition:
        return "verify-partition";
    case FirmwareFlashPhase::writeLoader:
        return "write-loader";
    case FirmwareFlashPhase::verifyLoader:
        return "verify-loader";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view flashStepKindText(
    const image::FirmwareFlashStepKind kind) noexcept
{
    using image::FirmwareFlashStepKind;
    switch (kind) {
    case FirmwareFlashStepKind::upgradeLoader:
        return "upgrade-loader";
    case FirmwareFlashStepKind::ensurePartitionTable:
        return "ensure-partition-table";
    case FirmwareFlashStepKind::writePartition:
        return "write-partition";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view workflowErrorText(
    const workflow::FirmwareUpgradeErrorCode code) noexcept
{
    using workflow::FirmwareUpgradeErrorCode;
    switch (code) {
    case FirmwareUpgradeErrorCode::cancelled:
        return "cancelled";
    case FirmwareUpgradeErrorCode::imageInvalid:
        return "image-invalid";
    case FirmwareUpgradeErrorCode::digestFailed:
        return "digest-failed";
    case FirmwareUpgradeErrorCode::planInvalid:
        return "plan-invalid";
    case FirmwareUpgradeErrorCode::scanFailed:
        return "scan-failed";
    case FirmwareUpgradeErrorCode::deviceNotFound:
        return "device-not-found";
    case FirmwareUpgradeErrorCode::deviceAmbiguous:
        return "device-ambiguous";
    case FirmwareUpgradeErrorCode::deviceUnsupported:
        return "device-unsupported";
    case FirmwareUpgradeErrorCode::openFailed:
        return "open-failed";
    case FirmwareUpgradeErrorCode::loaderResetFailed:
        return "loader-reset-failed";
    case FirmwareUpgradeErrorCode::reenumerationTimedOut:
        return "reenumeration-timeout";
    case FirmwareUpgradeErrorCode::loaderDownloadFailed:
        return "loader-download-failed";
    case FirmwareUpgradeErrorCode::loaderNotReady:
        return "loader-not-ready";
    case FirmwareUpgradeErrorCode::wrongChip:
        return "wrong-chip";
    case FirmwareUpgradeErrorCode::storageCheckFailed:
        return "storage-check-failed";
    case FirmwareUpgradeErrorCode::storageNotEmmc:
        return "storage-not-emmc";
    case FirmwareUpgradeErrorCode::deviceTooSmall:
        return "device-too-small";
    case FirmwareUpgradeErrorCode::flashFailed:
        return "flash-failed";
    case FirmwareUpgradeErrorCode::resetFailed:
        return "reset-failed";
    case FirmwareUpgradeErrorCode::closeFailed:
        return "close-failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view imageErrorText(
    const image::FirmwareImageErrorCode code) noexcept
{
    using image::FirmwareImageErrorCode;
    switch (code) {
    case FirmwareImageErrorCode::fileStatFailed:
        return "file-stat-failed";
    case FirmwareImageErrorCode::fileOpenFailed:
        return "file-open-failed";
    case FirmwareImageErrorCode::fileTooSmall:
        return "file-too-small";
    case FirmwareImageErrorCode::seekFailed:
        return "seek-failed";
    case FirmwareImageErrorCode::shortRead:
        return "short-read";
    case FirmwareImageErrorCode::invalidRkfwSignature:
        return "invalid-rkfw-signature";
    case FirmwareImageErrorCode::invalidRkfwHeader:
        return "invalid-rkfw-header";
    case FirmwareImageErrorCode::invalidBootRegion:
        return "invalid-boot-region";
    case FirmwareImageErrorCode::invalidBootSignature:
        return "invalid-boot-signature";
    case FirmwareImageErrorCode::invalidLoader:
        return "invalid-loader";
    case FirmwareImageErrorCode::invalidFirmwareRegion:
        return "invalid-firmware-region";
    case FirmwareImageErrorCode::invalidRkafSignature:
        return "invalid-rkaf-signature";
    case FirmwareImageErrorCode::rkafSizeMismatch:
        return "rkaf-size-mismatch";
    case FirmwareImageErrorCode::invalidEntryCount:
        return "invalid-entry-count";
    case FirmwareImageErrorCode::invalidEntryTable:
        return "invalid-entry-table";
    case FirmwareImageErrorCode::invalidEntryRegion:
        return "invalid-entry-region";
    case FirmwareImageErrorCode::invalidParameter:
        return "invalid-parameter";
    case FirmwareImageErrorCode::invalidPartitionLayout:
        return "invalid-partition-layout";
    case FirmwareImageErrorCode::invalidSparseImage:
        return "invalid-sparse-image";
    case FirmwareImageErrorCode::invalidTrailingMd5:
        return "invalid-trailing-md5";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view digestErrorText(
    const workflow::FirmwareDigestErrorCode code) noexcept
{
    using workflow::FirmwareDigestErrorCode;
    switch (code) {
    case FirmwareDigestErrorCode::openFailed:
        return "open-failed";
    case FirmwareDigestErrorCode::readFailed:
        return "read-failed";
    case FirmwareDigestErrorCode::mismatch:
        return "mismatch";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view planErrorText(
    const image::FirmwarePlanErrorCode code) noexcept
{
    using image::FirmwarePlanErrorCode;
    switch (code) {
    case FirmwarePlanErrorCode::unsupportedTargetChip:
        return "unsupported-target-chip";
    case FirmwarePlanErrorCode::missingLoader:
        return "missing-loader";
    case FirmwarePlanErrorCode::missingParameter:
        return "missing-parameter";
    case FirmwarePlanErrorCode::firmwareMd5NotVerified:
        return "firmware-md5-not-verified";
    case FirmwarePlanErrorCode::duplicatePartition:
        return "duplicate-partition";
    case FirmwarePlanErrorCode::targetPartitionMissing:
        return "target-partition-missing";
    case FirmwarePlanErrorCode::targetPartitionTooSmall:
        return "target-partition-too-small";
    case FirmwarePlanErrorCode::partitionRangeOverflow:
        return "partition-range-overflow";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view openErrorText(
    const workflow::FirmwareUpgradeOpenErrorCode code) noexcept
{
    using workflow::FirmwareUpgradeOpenErrorCode;
    switch (code) {
    case FirmwareUpgradeOpenErrorCode::invalidSelection:
        return "invalid-selection";
    case FirmwareUpgradeOpenErrorCode::initializationFailed:
        return "initialization-failed";
    case FirmwareUpgradeOpenErrorCode::enumerationFailed:
        return "enumeration-failed";
    case FirmwareUpgradeOpenErrorCode::topologyReadFailed:
        return "topology-read-failed";
    case FirmwareUpgradeOpenErrorCode::descriptorReadFailed:
        return "descriptor-read-failed";
    case FirmwareUpgradeOpenErrorCode::deviceNotFound:
        return "device-not-found";
    case FirmwareUpgradeOpenErrorCode::deviceAmbiguous:
        return "device-ambiguous";
    case FirmwareUpgradeOpenErrorCode::configurationReadFailed:
        return "configuration-read-failed";
    case FirmwareUpgradeOpenErrorCode::interfaceNotFound:
        return "interface-not-found";
    case FirmwareUpgradeOpenErrorCode::interfaceAmbiguous:
        return "interface-ambiguous";
    case FirmwareUpgradeOpenErrorCode::invalidEndpointLayout:
        return "invalid-endpoint-layout";
    case FirmwareUpgradeOpenErrorCode::openFailed:
        return "open-failed";
    case FirmwareUpgradeOpenErrorCode::driverQueryFailed:
        return "driver-query-failed";
    case FirmwareUpgradeOpenErrorCode::driverActive:
        return "driver-active";
    case FirmwareUpgradeOpenErrorCode::claimFailed:
        return "claim-failed";
    case FirmwareUpgradeOpenErrorCode::resourceExhausted:
        return "resource-exhausted";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view loaderErrorText(
    const image::MaskromLoaderErrorCode code) noexcept
{
    using image::MaskromLoaderErrorCode;
    switch (code) {
    case MaskromLoaderErrorCode::invalidImage:
        return "invalid-image";
    case MaskromLoaderErrorCode::imageOpenFailed:
        return "image-open-failed";
    case MaskromLoaderErrorCode::imageSeekFailed:
        return "image-seek-failed";
    case MaskromLoaderErrorCode::imageReadFailed:
        return "image-read-failed";
    case MaskromLoaderErrorCode::transferFailed:
        return "transfer-failed";
    case MaskromLoaderErrorCode::shortTransfer:
        return "short-transfer";
    case MaskromLoaderErrorCode::cancelled:
        return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view sessionErrorText(
    const session::SessionErrorCode code) noexcept
{
    using session::SessionErrorCode;
    switch (code) {
    case SessionErrorCode::sessionFaulted:
        return "session-faulted";
    case SessionErrorCode::invalidTimeout:
        return "invalid-timeout";
    case SessionErrorCode::commandEncodeFailed:
        return "command-encode-failed";
    case SessionErrorCode::emptyDataBuffer:
        return "empty-data-buffer";
    case SessionErrorCode::bufferTooLarge:
        return "buffer-too-large";
    case SessionErrorCode::invalidAddress:
        return "invalid-address";
    case SessionErrorCode::invalidSectorCount:
        return "invalid-sector-count";
    case SessionErrorCode::bufferSizeMismatch:
        return "buffer-size-mismatch";
    case SessionErrorCode::transportFailure:
        return "transport-failure";
    case SessionErrorCode::shortTransfer:
        return "short-transfer";
    case SessionErrorCode::invalidTransferCount:
        return "invalid-transfer-count";
    case SessionErrorCode::deviceCommandFailed:
        return "device-command-failed";
    case SessionErrorCode::statusDecodeFailed:
        return "status-decode-failed";
    case SessionErrorCode::nonZeroResidue:
        return "non-zero-residue";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view sessionStageText(
    const session::SessionStage stage) noexcept
{
    using session::SessionStage;
    switch (stage) {
    case SessionStage::request:
        return "request";
    case SessionStage::command:
        return "command";
    case SessionStage::data:
        return "data";
    case SessionStage::status:
        return "status";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view flashErrorText(
    const image::FirmwareFlashErrorCode code) noexcept
{
    using image::FirmwareFlashErrorCode;
    switch (code) {
    case FirmwareFlashErrorCode::invalidPlan:
        return "invalid-plan";
    case FirmwareFlashErrorCode::deviceTooSmall:
        return "device-too-small";
    case FirmwareFlashErrorCode::addressOverflow:
        return "address-overflow";
    case FirmwareFlashErrorCode::imageOpenFailed:
        return "image-open-failed";
    case FirmwareFlashErrorCode::imageSeekFailed:
        return "image-seek-failed";
    case FirmwareFlashErrorCode::imageReadFailed:
        return "image-read-failed";
    case FirmwareFlashErrorCode::unsupportedPayload:
        return "unsupported-payload";
    case FirmwareFlashErrorCode::invalidLoader:
        return "invalid-loader";
    case FirmwareFlashErrorCode::loaderUnsupported:
        return "loader-unsupported";
    case FirmwareFlashErrorCode::partitionTableGenerationFailed:
        return "partition-table-generation-failed";
    case FirmwareFlashErrorCode::eraseFailed:
        return "erase-failed";
    case FirmwareFlashErrorCode::writeFailed:
        return "write-failed";
    case FirmwareFlashErrorCode::verifyFailed:
        return "verify-failed";
    case FirmwareFlashErrorCode::cancelled:
        return "cancelled";
    }
    return "unknown";
}

[[nodiscard]] QString redactedTarget(
    const workflow::FirmwareUpgradeReport& report,
    const std::string& target, const bool includeFirmwarePath)
{
    if (includeFirmwarePath || target.empty()) {
        return QString::fromStdString(target);
    }
    const auto requestPath = report.request.imagePath.string();
    if (target == requestPath ||
        (report.plan.has_value() &&
         target == report.plan->sourcePath.string())) {
        return toQString(report.request.imagePath.filename());
    }
    return QString::fromStdString(target);
}

[[nodiscard]] QJsonObject deviceObject(
    const transport::UsbDeviceInfo& device)
{
    QJsonArray ports;
    for (const auto port : device.portNumbers) {
        ports.append(static_cast<int>(port));
    }
    return QJsonObject{
        {QStringLiteral("portPath"), QString::fromStdString(device.portPath)},
        {QStringLiteral("vendorId"), static_cast<int>(device.vendorId)},
        {QStringLiteral("productId"), static_cast<int>(device.productId)},
        {QStringLiteral("bcdUsb"), static_cast<int>(device.bcdUsb)},
        {QStringLiteral("bcdDevice"), static_cast<int>(device.bcdDevice)},
        {QStringLiteral("busNumber"), static_cast<int>(device.busNumber)},
        {QStringLiteral("portNumbers"), ports},
        {QStringLiteral("deviceAddress"),
         static_cast<int>(device.deviceAddress)},
        {QStringLiteral("mode"), toQString(transport::toString(device.mode))},
    };
}

[[nodiscard]] QJsonObject planObject(
    const image::RkFirmwareFlashPlan& plan)
{
    QJsonArray steps;
    for (const auto& step : plan.steps) {
        QJsonObject value{
            {QStringLiteral("kind"), toQString(flashStepKindText(step.kind))},
            {QStringLiteral("target"), QString::fromStdString(step.target)},
            {QStringLiteral("firstLba"), jsonInteger(step.firstLba)},
            {QStringLiteral("sectorCount"), jsonInteger(step.sectorCount)},
            {QStringLiteral("inputSize"), jsonInteger(step.inputSize)},
            {QStringLiteral("outputSize"), jsonInteger(step.outputSize)},
            {QStringLiteral("format"), toQString(image::toString(step.format))},
        };
        value.insert(QStringLiteral("entryIndex"),
                     step.entryIndex.has_value()
                         ? QJsonValue{static_cast<qint64>(*step.entryIndex)}
                         : QJsonValue{QJsonValue::Null});
        steps.append(value);
    }
    return QJsonObject{
        {QStringLiteral("targetChip"), QString::fromStdString(plan.targetChip)},
        {QStringLiteral("targetModel"),
         QString::fromStdString(plan.targetModel)},
        {QStringLiteral("minimumDeviceSectors"),
         jsonInteger(plan.minimumDeviceSectors)},
        {QStringLiteral("totalPartitionBytes"),
         jsonInteger(plan.totalPartitionBytes)},
        {QStringLiteral("steps"), steps},
    };
}

[[nodiscard]] QJsonObject successObject(
    const workflow::FirmwareUpgradeSuccess& success)
{
    return QJsonObject{
        {QStringLiteral("portPath"), QString::fromStdString(success.portPath)},
        {QStringLiteral("model"), QString::fromStdString(success.model)},
        {QStringLiteral("firmwareMd5"), digestText(success.firmwareMd5)},
        {QStringLiteral("deviceSectors"), jsonInteger(success.deviceSectors)},
        {QStringLiteral("partitionBytesWritten"),
         jsonInteger(success.partitionBytesWritten)},
        {QStringLiteral("sectorsErased"), jsonInteger(success.sectorsErased)},
        {QStringLiteral("partitionsVerified"),
         static_cast<int>(success.partitionsVerified)},
        {QStringLiteral("preparedOnly"), success.preparedOnly},
        {QStringLiteral("partitionTableWritten"),
         success.partitionTableWritten},
        {QStringLiteral("partitionTableVerified"),
         success.partitionTableVerified},
        {QStringLiteral("loaderWritten"), success.loaderWritten},
        {QStringLiteral("loaderVerified"), success.loaderVerified},
        {QStringLiteral("resetAccepted"), success.resetAccepted},
    };
}

[[nodiscard]] QJsonObject errorObject(
    const workflow::FirmwareUpgradeReport& report,
    const workflow::FirmwareUpgradeError& error,
    const bool includeFirmwarePath)
{
    QJsonObject details;
    if (error.imageError.has_value()) {
        details.insert(QStringLiteral("imageErrorCode"),
                       toQString(imageErrorText(error.imageError->code)));
        details.insert(QStringLiteral("imageOffset"),
                       jsonInteger(error.imageError->offset));
        details.insert(QStringLiteral("imageEntryIndex"),
                       static_cast<qint64>(error.imageError->entryIndex));
    }
    if (error.digestError.has_value()) {
        details.insert(QStringLiteral("digestErrorCode"),
                       toQString(digestErrorText(error.digestError->code)));
        details.insert(QStringLiteral("actualMd5"),
                       digestText(error.digestError->actualMd5));
    }
    if (error.planError.has_value()) {
        details.insert(QStringLiteral("planErrorCode"),
                       toQString(planErrorText(error.planError->code)));
        details.insert(QStringLiteral("planTarget"),
                       redactedTarget(report, error.planError->target,
                                      includeFirmwarePath));
        details.insert(QStringLiteral("planEntryIndex"),
                       static_cast<qint64>(error.planError->entryIndex));
    }
    if (error.scanError.has_value()) {
        details.insert(QStringLiteral("scanNativeCode"),
                       error.scanError->code);
        details.insert(QStringLiteral("scanMessage"),
                       QString::fromStdString(error.scanError->message));
    }
    if (error.openError.has_value()) {
        details.insert(QStringLiteral("openErrorCode"),
                       toQString(openErrorText(error.openError->code)));
        details.insert(QStringLiteral("openNativeCode"),
                       error.openError->nativeCode);
        details.insert(QStringLiteral("accessDenied"),
                       error.openError->accessDenied);
    }
    if (error.loaderError.has_value()) {
        details.insert(QStringLiteral("loaderErrorCode"),
                       toQString(loaderErrorText(error.loaderError->code)));
        details.insert(QStringLiteral("loaderStage"),
                       QString::fromStdString(error.loaderError->stage));
        details.insert(QStringLiteral("loaderCompletedBytes"),
                       jsonInteger(error.loaderError->completedBytes));
    }
    if (error.sessionError.has_value()) {
        details.insert(QStringLiteral("sessionErrorCode"),
                       toQString(sessionErrorText(error.sessionError->code)));
        details.insert(QStringLiteral("sessionStage"),
                       toQString(sessionStageText(error.sessionError->stage)));
        details.insert(QStringLiteral("expectedBytes"),
                       static_cast<qint64>(error.sessionError->expectedBytes));
        details.insert(QStringLiteral("actualBytes"),
                       static_cast<qint64>(error.sessionError->actualBytes));
        details.insert(QStringLiteral("timeoutMilliseconds"),
                       static_cast<qint64>(
                           error.sessionError->timeout.count()));
        details.insert(QStringLiteral("residue"),
                       static_cast<qint64>(error.sessionError->residue));
    }
    if (error.flashError.has_value()) {
        details.insert(QStringLiteral("flashErrorCode"),
                       toQString(flashErrorText(error.flashError->code)));
        details.insert(QStringLiteral("flashTarget"),
                       QString::fromStdString(error.flashError->target));
        details.insert(QStringLiteral("flashCompletedBytes"),
                       jsonInteger(error.flashError->completedBytes));
    }

    return QJsonObject{
        {QStringLiteral("code"), toQString(workflowErrorText(error.code))},
        {QStringLiteral("stage"), toQString(workflow::toString(error.stage))},
        {QStringLiteral("target"),
         redactedTarget(report, error.target, includeFirmwarePath)},
        {QStringLiteral("completedBytes"), jsonInteger(error.completedBytes)},
        {QStringLiteral("details"), details},
    };
}

[[nodiscard]] QJsonObject reportObject(
    const workflow::FirmwareUpgradeReport& report,
    const FirmwareUpgradeReportWriteOptions& options)
{
    QJsonObject firmware{
        {QStringLiteral("fileName"),
         toQString(report.request.imagePath.filename())},
        {QStringLiteral("model"),
         QString::fromStdString(report.firmwareModel)},
        {QStringLiteral("md5"),
         report.firmwareMd5.has_value()
             ? QJsonValue{digestText(*report.firmwareMd5)}
             : QJsonValue{QJsonValue::Null}},
    };
    if (options.includeFirmwarePath) {
        firmware.insert(QStringLiteral("path"),
                        toQString(report.request.imagePath));
    }

    QJsonArray timeline;
    for (const auto& step : report.steps) {
        QJsonObject value{
            {QStringLiteral("stage"), toQString(workflow::toString(step.stage))},
            {QStringLiteral("target"),
             redactedTarget(report, step.target,
                            options.includeFirmwarePath)},
            {QStringLiteral("startedAfterMilliseconds"),
             jsonInteger(step.startedAfterMilliseconds)},
            {QStringLiteral("durationMilliseconds"),
             jsonInteger(step.durationMilliseconds)},
            {QStringLiteral("completedBytes"),
             jsonInteger(step.completedBytes)},
            {QStringLiteral("totalBytes"), jsonInteger(step.totalBytes)},
            {QStringLiteral("status"), toQString(stepStatusText(step.status))},
        };
        value.insert(QStringLiteral("phase"),
                     step.flashPhase.has_value()
                         ? QJsonValue{toQString(
                               flashPhaseText(*step.flashPhase))}
                         : QJsonValue{QJsonValue::Null});
        timeline.append(value);
    }

    QJsonObject root{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("application"),
         QJsonObject{
             {QStringLiteral("name"), QStringLiteral("ZzRkDevTool")},
             {QStringLiteral("version"),
              QString::fromStdString(options.applicationVersion)},
         }},
        {QStringLiteral("startedAt"),
         QDateTime::fromMSecsSinceEpoch(report.startedAtUnixMilliseconds,
                                        QTimeZone::UTC)
             .toString(Qt::ISODateWithMs)},
        {QStringLiteral("durationMilliseconds"),
         jsonInteger(report.durationMilliseconds)},
        {QStringLiteral("status"), toQString(reportStatusText(report.status))},
        {QStringLiteral("request"),
         QJsonObject{
             {QStringLiteral("portPath"),
              QString::fromStdString(report.request.portPath)},
             {QStringLiteral("verifyMode"),
              toQString(verifyModeText(report.request.verifyMode))},
             {QStringLiteral("reboot"), report.request.reboot},
             {QStringLiteral("prepareOnly"), report.request.prepareOnly},
         }},
        {QStringLiteral("firmware"), firmware},
        {QStringLiteral("device"),
         report.device.has_value()
             ? QJsonValue{deviceObject(*report.device)}
             : QJsonValue{QJsonValue::Null}},
        {QStringLiteral("plan"),
         report.plan.has_value() ? QJsonValue{planObject(*report.plan)}
                                 : QJsonValue{QJsonValue::Null}},
        {QStringLiteral("timeline"), timeline},
        {QStringLiteral("result"),
         report.success.has_value()
             ? QJsonValue{successObject(*report.success)}
             : QJsonValue{QJsonValue::Null}},
        {QStringLiteral("error"),
         report.error.has_value()
             ? QJsonValue{errorObject(report, *report.error,
                                      options.includeFirmwarePath)}
             : QJsonValue{QJsonValue::Null}},
    };
    return root;
}

} // namespace

std::optional<FirmwareUpgradeReportWriteError> writeFirmwareUpgradeReport(
    const workflow::FirmwareUpgradeReport& report,
    const std::filesystem::path& outputPath,
    const FirmwareUpgradeReportWriteOptions& options)
{
    QSaveFile output{toQString(outputPath)};
    if (!output.open(QIODevice::WriteOnly)) {
        return FirmwareUpgradeReportWriteError{
            .code = FirmwareUpgradeReportWriteErrorCode::openFailed,
            .message = output.errorString().toStdString(),
        };
    }

    const auto bytes = QJsonDocument{reportObject(report, options)}.toJson(
        QJsonDocument::Indented);
    if (output.write(bytes) != bytes.size()) {
        return FirmwareUpgradeReportWriteError{
            .code = FirmwareUpgradeReportWriteErrorCode::writeFailed,
            .message = output.errorString().toStdString(),
        };
    }
    if (!output.commit()) {
        return FirmwareUpgradeReportWriteError{
            .code = FirmwareUpgradeReportWriteErrorCode::commitFailed,
            .message = output.errorString().toStdString(),
        };
    }
    return std::nullopt;
}

} // namespace zzrk::qt
