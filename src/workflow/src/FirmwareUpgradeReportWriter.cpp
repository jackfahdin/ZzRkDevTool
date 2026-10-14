// SPDX-License-Identifier: MIT

#include <zzrk/workflow/FirmwareUpgradeReportWriter.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace zzrk::workflow {
namespace {

class JsonWriter final {
  public:
    void key(const std::string_view name)
    {
        beforeValue();
        writeEscaped(name);
        text_ += ": ";
        pendingKey_ = true;
    }

    void beginObject()
    {
        beforeValue();
        text_ += '{';
        containers_.push_back(0U);
    }

    void endObject()
    {
        closeContainer('}');
    }

    void beginArray()
    {
        beforeValue();
        text_ += '[';
        containers_.push_back(0U);
    }

    void endArray()
    {
        closeContainer(']');
    }

    void stringValue(const std::string_view value)
    {
        beforeValue();
        writeEscaped(value);
    }

    void integerValue(const std::uint64_t value)
    {
        beforeValue();
        text_ += std::to_string(value);
    }

    void integerValue(const std::int64_t value)
    {
        beforeValue();
        text_ += std::to_string(value);
    }

    void booleanValue(const bool value)
    {
        beforeValue();
        text_ += value ? "true" : "false";
    }

    void nullValue()
    {
        beforeValue();
        text_ += "null";
    }

    [[nodiscard]] const std::string& text() const noexcept
    {
        return text_;
    }

  private:
    void beforeValue()
    {
        if (pendingKey_) {
            pendingKey_ = false;
            return;
        }
        if (containers_.empty()) {
            return;
        }
        if (containers_.back() != 0U) {
            text_ += ',';
        }
        ++containers_.back();
        text_ += '\n';
        text_.append(containers_.size() * 4U, ' ');
    }

    void closeContainer(const char closing)
    {
        const auto count = containers_.back();
        containers_.pop_back();
        if (count != 0U) {
            text_ += '\n';
            text_.append(containers_.size() * 4U, ' ');
        }
        text_ += closing;
    }

    void writeEscaped(const std::string_view value)
    {
        text_ += '"';
        for (const char ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            if (ch == '"' || ch == '\\') {
                text_ += '\\';
                text_ += ch;
            } else if (byte < 0x20U) {
                constexpr char kHexDigits[] = "0123456789abcdef";
                text_ += "\\u00";
                text_ += kHexDigits[byte >> 4U];
                text_ += kHexDigits[byte & 0x0fU];
            } else {
                text_ += ch;
            }
        }
        text_ += '"';
    }

    std::string text_;
    std::vector<std::size_t> containers_;
    bool pendingKey_{};
};

[[nodiscard]] std::string digestText(const std::array<char, 33>& digest)
{
    const auto end = std::find(digest.cbegin(), digest.cend(), '\0');
    return std::string{digest.data(),
                       static_cast<std::size_t>(end - digest.cbegin())};
}

[[nodiscard]] std::string isoTimestampUtc(const std::int64_t unixMilliseconds)
{
    const auto seconds = static_cast<std::time_t>(unixMilliseconds / 1000);
    const auto milliseconds = static_cast<int>(unixMilliseconds % 1000);
    std::tm brokenDown{};
#ifdef _WIN32
    gmtime_s(&brokenDown, &seconds);
#else
    gmtime_r(&seconds, &brokenDown);
#endif
    // printf widths are minimums: six signed int date fields, milliseconds,
    // separators and the terminator can require up to 78 bytes.
    std::array<char, 80> buffer{};
    std::snprintf(buffer.data(), buffer.size(),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  brokenDown.tm_year + 1900, brokenDown.tm_mon + 1,
                  brokenDown.tm_mday, brokenDown.tm_hour, brokenDown.tm_min,
                  brokenDown.tm_sec, milliseconds);
    return buffer.data();
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
    const FirmwareUpgradeReportStatus status) noexcept
{
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
    const FirmwareUpgradeReportStepStatus status) noexcept
{
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
    const FirmwareUpgradeErrorCode code) noexcept
{
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
    const FirmwareDigestErrorCode code) noexcept
{
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
    const FirmwareUpgradeOpenErrorCode code) noexcept
{
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

[[nodiscard]] std::string redactedTarget(
    const FirmwareUpgradeReport& report, const std::string& target,
    const bool includeFirmwarePath)
{
    if (includeFirmwarePath || target.empty()) {
        return target;
    }
    const auto requestPath = report.request.imagePath.string();
    if (target == requestPath ||
        (report.plan.has_value() &&
         target == report.plan->sourcePath.string())) {
        return report.request.imagePath.filename().string();
    }
    return target;
}

void writeDeviceObject(JsonWriter& writer,
                       const transport::UsbDeviceInfo& device)
{
    writer.beginObject();
    writer.key("portPath");
    writer.stringValue(device.portPath);
    writer.key("vendorId");
    writer.integerValue(static_cast<std::int64_t>(device.vendorId));
    writer.key("productId");
    writer.integerValue(static_cast<std::int64_t>(device.productId));
    writer.key("bcdUsb");
    writer.integerValue(static_cast<std::int64_t>(device.bcdUsb));
    writer.key("bcdDevice");
    writer.integerValue(static_cast<std::int64_t>(device.bcdDevice));
    writer.key("busNumber");
    writer.integerValue(static_cast<std::int64_t>(device.busNumber));
    writer.key("portNumbers");
    writer.beginArray();
    for (const auto port : device.portNumbers) {
        writer.integerValue(static_cast<std::int64_t>(port));
    }
    writer.endArray();
    writer.key("deviceAddress");
    writer.integerValue(static_cast<std::int64_t>(device.deviceAddress));
    writer.key("mode");
    writer.stringValue(transport::toString(device.mode));
    writer.endObject();
}

void writePlanObject(JsonWriter& writer,
                     const image::RkFirmwareFlashPlan& plan)
{
    writer.beginObject();
    writer.key("targetChip");
    writer.stringValue(plan.targetChip);
    writer.key("targetModel");
    writer.stringValue(plan.targetModel);
    writer.key("minimumDeviceSectors");
    writer.integerValue(plan.minimumDeviceSectors);
    writer.key("totalPartitionBytes");
    writer.integerValue(plan.totalPartitionBytes);
    writer.key("steps");
    writer.beginArray();
    for (const auto& step : plan.steps) {
        writer.beginObject();
        writer.key("kind");
        writer.stringValue(flashStepKindText(step.kind));
        writer.key("target");
        writer.stringValue(step.target);
        writer.key("firstLba");
        writer.integerValue(step.firstLba);
        writer.key("sectorCount");
        writer.integerValue(step.sectorCount);
        writer.key("inputSize");
        writer.integerValue(step.inputSize);
        writer.key("outputSize");
        writer.integerValue(step.outputSize);
        writer.key("format");
        writer.stringValue(image::toString(step.format));
        writer.key("entryIndex");
        if (step.entryIndex.has_value()) {
            writer.integerValue(static_cast<std::int64_t>(*step.entryIndex));
        } else {
            writer.nullValue();
        }
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
}

void writeSuccessObject(JsonWriter& writer,
                        const FirmwareUpgradeSuccess& success)
{
    writer.beginObject();
    writer.key("portPath");
    writer.stringValue(success.portPath);
    writer.key("model");
    writer.stringValue(success.model);
    writer.key("firmwareMd5");
    writer.stringValue(digestText(success.firmwareMd5));
    writer.key("deviceSectors");
    writer.integerValue(success.deviceSectors);
    writer.key("partitionBytesWritten");
    writer.integerValue(success.partitionBytesWritten);
    writer.key("sectorsErased");
    writer.integerValue(success.sectorsErased);
    writer.key("partitionsVerified");
    writer.integerValue(static_cast<std::int64_t>(success.partitionsVerified));
    writer.key("preparedOnly");
    writer.booleanValue(success.preparedOnly);
    writer.key("partitionTableWritten");
    writer.booleanValue(success.partitionTableWritten);
    writer.key("partitionTableVerified");
    writer.booleanValue(success.partitionTableVerified);
    writer.key("loaderWritten");
    writer.booleanValue(success.loaderWritten);
    writer.key("loaderVerified");
    writer.booleanValue(success.loaderVerified);
    writer.key("resetAccepted");
    writer.booleanValue(success.resetAccepted);
    writer.endObject();
}

void writeErrorObject(JsonWriter& writer,
                      const FirmwareUpgradeReport& report,
                      const FirmwareUpgradeError& error,
                      const bool includeFirmwarePath)
{
    writer.beginObject();
    writer.key("code");
    writer.stringValue(workflowErrorText(error.code));
    writer.key("stage");
    writer.stringValue(toString(error.stage));
    writer.key("target");
    writer.stringValue(redactedTarget(report, error.target,
                                      includeFirmwarePath));
    writer.key("completedBytes");
    writer.integerValue(error.completedBytes);
    writer.key("details");
    writer.beginObject();
    if (error.imageError.has_value()) {
        writer.key("imageErrorCode");
        writer.stringValue(imageErrorText(error.imageError->code));
        writer.key("imageOffset");
        writer.integerValue(error.imageError->offset);
        writer.key("imageEntryIndex");
        writer.integerValue(
            static_cast<std::int64_t>(error.imageError->entryIndex));
    }
    if (error.digestError.has_value()) {
        writer.key("digestErrorCode");
        writer.stringValue(digestErrorText(error.digestError->code));
        writer.key("actualMd5");
        writer.stringValue(digestText(error.digestError->actualMd5));
    }
    if (error.planError.has_value()) {
        writer.key("planErrorCode");
        writer.stringValue(planErrorText(error.planError->code));
        writer.key("planTarget");
        writer.stringValue(redactedTarget(report, error.planError->target,
                                          includeFirmwarePath));
        writer.key("planEntryIndex");
        writer.integerValue(
            static_cast<std::int64_t>(error.planError->entryIndex));
    }
    if (error.scanError.has_value()) {
        writer.key("scanNativeCode");
        writer.integerValue(static_cast<std::int64_t>(error.scanError->code));
        writer.key("scanMessage");
        writer.stringValue(error.scanError->message);
    }
    if (error.openError.has_value()) {
        writer.key("openErrorCode");
        writer.stringValue(openErrorText(error.openError->code));
        writer.key("openNativeCode");
        writer.integerValue(
            static_cast<std::int64_t>(error.openError->nativeCode));
        writer.key("accessDenied");
        writer.booleanValue(error.openError->accessDenied);
    }
    if (error.loaderError.has_value()) {
        writer.key("loaderErrorCode");
        writer.stringValue(loaderErrorText(error.loaderError->code));
        writer.key("loaderStage");
        writer.stringValue(error.loaderError->stage);
        writer.key("loaderCompletedBytes");
        writer.integerValue(error.loaderError->completedBytes);
    }
    if (error.sessionError.has_value()) {
        writer.key("sessionErrorCode");
        writer.stringValue(sessionErrorText(error.sessionError->code));
        writer.key("sessionStage");
        writer.stringValue(sessionStageText(error.sessionError->stage));
        writer.key("expectedBytes");
        writer.integerValue(static_cast<std::int64_t>(
            error.sessionError->expectedBytes));
        writer.key("actualBytes");
        writer.integerValue(
            static_cast<std::int64_t>(error.sessionError->actualBytes));
        writer.key("timeoutMilliseconds");
        writer.integerValue(static_cast<std::int64_t>(
            error.sessionError->timeout.count()));
        writer.key("residue");
        writer.integerValue(
            static_cast<std::int64_t>(error.sessionError->residue));
    }
    if (error.flashError.has_value()) {
        writer.key("flashErrorCode");
        writer.stringValue(flashErrorText(error.flashError->code));
        writer.key("flashTarget");
        writer.stringValue(error.flashError->target);
        writer.key("flashCompletedBytes");
        writer.integerValue(error.flashError->completedBytes);
    }
    writer.endObject();
    writer.endObject();
}

void writeReportObject(JsonWriter& writer,
                       const FirmwareUpgradeReport& report,
                       const FirmwareUpgradeReportWriteOptions& options)
{
    writer.beginObject();
    writer.key("schemaVersion");
    writer.integerValue(std::uint64_t{1});
    writer.key("application");
    writer.beginObject();
    writer.key("name");
    writer.stringValue("ZzRkDevTool");
    writer.key("version");
    writer.stringValue(options.applicationVersion);
    writer.endObject();
    writer.key("startedAt");
    writer.stringValue(isoTimestampUtc(report.startedAtUnixMilliseconds));
    writer.key("durationMilliseconds");
    writer.integerValue(report.durationMilliseconds);
    writer.key("status");
    writer.stringValue(reportStatusText(report.status));
    writer.key("request");
    writer.beginObject();
    writer.key("portPath");
    writer.stringValue(report.request.portPath);
    writer.key("verifyMode");
    writer.stringValue(verifyModeText(report.request.verifyMode));
    writer.key("reboot");
    writer.booleanValue(report.request.reboot);
    writer.key("prepareOnly");
    writer.booleanValue(report.request.prepareOnly);
    writer.endObject();
    writer.key("firmware");
    writer.beginObject();
    writer.key("fileName");
    writer.stringValue(report.request.imagePath.filename().string());
    writer.key("model");
    writer.stringValue(report.firmwareModel);
    writer.key("md5");
    if (report.firmwareMd5.has_value()) {
        writer.stringValue(digestText(*report.firmwareMd5));
    } else {
        writer.nullValue();
    }
    if (options.includeFirmwarePath) {
        writer.key("path");
        writer.stringValue(report.request.imagePath.string());
    }
    writer.endObject();
    writer.key("device");
    if (report.device.has_value()) {
        writeDeviceObject(writer, *report.device);
    } else {
        writer.nullValue();
    }
    writer.key("plan");
    if (report.plan.has_value()) {
        writePlanObject(writer, *report.plan);
    } else {
        writer.nullValue();
    }
    writer.key("timeline");
    writer.beginArray();
    for (const auto& step : report.steps) {
        writer.beginObject();
        writer.key("stage");
        writer.stringValue(toString(step.stage));
        writer.key("target");
        writer.stringValue(redactedTarget(report, step.target,
                                          options.includeFirmwarePath));
        writer.key("startedAfterMilliseconds");
        writer.integerValue(step.startedAfterMilliseconds);
        writer.key("durationMilliseconds");
        writer.integerValue(step.durationMilliseconds);
        writer.key("completedBytes");
        writer.integerValue(step.completedBytes);
        writer.key("totalBytes");
        writer.integerValue(step.totalBytes);
        writer.key("status");
        writer.stringValue(stepStatusText(step.status));
        writer.key("phase");
        if (step.flashPhase.has_value()) {
            writer.stringValue(flashPhaseText(*step.flashPhase));
        } else {
            writer.nullValue();
        }
        writer.endObject();
    }
    writer.endArray();
    writer.key("result");
    if (report.success.has_value()) {
        writeSuccessObject(writer, *report.success);
    } else {
        writer.nullValue();
    }
    writer.key("error");
    if (report.error.has_value()) {
        writeErrorObject(writer, report, *report.error,
                         options.includeFirmwarePath);
    } else {
        writer.nullValue();
    }
    writer.endObject();
}

[[nodiscard]] std::filesystem::path makeTemporaryPath(
    const std::filesystem::path& outputPath)
{
    static std::atomic<std::uint64_t> counter{};
    const auto stamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto unique = std::to_string(stamp) + "-" +
                        std::to_string(counter.fetch_add(1U));
    auto name = outputPath.filename();
    name += "." + unique + ".tmp";
    return outputPath.parent_path() / name;
}

} // namespace

std::optional<FirmwareUpgradeReportWriteError> writeFirmwareUpgradeReport(
    const FirmwareUpgradeReport& report,
    const std::filesystem::path& outputPath,
    const FirmwareUpgradeReportWriteOptions& options)
{
    JsonWriter writer;
    writeReportObject(writer, report, options);

    const auto temporaryPath = makeTemporaryPath(outputPath);
    {
        std::ofstream output{temporaryPath,
                             std::ios::binary | std::ios::trunc};
        if (!output.is_open()) {
            const auto openError =
                std::error_code{errno, std::generic_category()};
            return FirmwareUpgradeReportWriteError{
                .code = FirmwareUpgradeReportWriteErrorCode::openFailed,
                .message = openError.message(),
            };
        }
        output.write(writer.text().data(),
                     static_cast<std::streamsize>(writer.text().size()));
        output.flush();
        if (!output) {
            const auto writeError =
                std::error_code{errno, std::generic_category()};
            std::error_code ignored;
            std::filesystem::remove(temporaryPath, ignored);
            return FirmwareUpgradeReportWriteError{
                .code = FirmwareUpgradeReportWriteErrorCode::writeFailed,
                .message = writeError.message(),
            };
        }
    }

#ifdef _WIN32
    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);
#endif
    std::error_code commitError;
    std::filesystem::rename(temporaryPath, outputPath, commitError);
    if (commitError) {
        std::error_code ignored;
        std::filesystem::remove(temporaryPath, ignored);
        return FirmwareUpgradeReportWriteError{
            .code = FirmwareUpgradeReportWriteErrorCode::commitFailed,
            .message = commitError.message(),
        };
    }
    return std::nullopt;
}

} // namespace zzrk::workflow
