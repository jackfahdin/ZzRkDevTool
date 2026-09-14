// SPDX-License-Identifier: MIT

#include <zzrk/ffi/zzrk_ffi.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void testVersionString()
{
    const char* version = zzrk_version_string();
    expect(version != nullptr, "version string is not null");
    expect(version != nullptr && version[0] != '\0',
           "version string is not empty");
}

void testLastErrorLifecycle()
{
    expect(zzrk_last_error_message() != nullptr,
           "error message pointer is not null");
    expect(std::strlen(zzrk_last_error_message()) == 0,
           "error message initially empty");

    zzrk_firmware_info info{};
    const zzrk_status status =
        zzrk_inspect_firmware(nullptr, &info);
    expect(status == ZZRK_ERR_INVALID_ARGUMENT,
           "null image path rejected");
    expect(std::strlen(zzrk_last_error_message()) > 0,
           "error message populated after failure");

    expect(zzrk_version_string() != nullptr, "version call runs");
    expect(std::strlen(zzrk_last_error_message()) == 0,
           "error message cleared by successful call");
}

void testInspectFirmwarePaths()
{
    zzrk_firmware_info info{};
    expect(zzrk_inspect_firmware("/definitely/missing/update.img", &info) ==
               ZZRK_ERR_FAILED,
           "missing image file fails");
    expect(std::strlen(zzrk_last_error_message()) > 0,
           "missing image error message populated");

    expect(zzrk_inspect_firmware("/definitely/missing/update.img", nullptr) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "null out_info rejected");
}

void testScanDevices()
{
    uint32_t total = 12345;
    expect(zzrk_scan_devices(nullptr, 0, &total) == ZZRK_OK,
           "scan without buffer succeeds");

    zzrk_usb_device_info devices[4]{};
    uint32_t count = 12345;
    expect(zzrk_scan_devices(devices, 4, &count) == ZZRK_OK,
           "scan with buffer succeeds");
    expect(count == total, "buffered scan reports the same total count");
    for (uint32_t index = 0; index < count && index < 4; ++index) {
        expect(devices[index].vendor_id == 0x2207,
               "scanned device is a rockchip device");
        expect(devices[index].mode >= ZZRK_DEVICE_MODE_UNKNOWN &&
                   devices[index].mode <= ZZRK_DEVICE_MODE_LOADER,
               "scanned device mode is a valid enum value");
        expect(devices[index].port_path[0] != '\0',
               "scanned device carries a port path");
    }

    if (total > 1) {
        zzrk_usb_device_info single{};
        count = 0;
        expect(zzrk_scan_devices(&single, 1, &count) == ZZRK_OK,
               "truncated scan succeeds");
        expect(count == total,
               "truncated scan still reports the total count");
    }

    expect(zzrk_scan_devices(devices, 4, nullptr) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "null out_count rejected");
    expect(zzrk_scan_devices(nullptr, 4, &count) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "null buffer with nonzero capacity rejected");
}

void testCancellationToken()
{
    zzrk_cancellation* token = zzrk_cancellation_new();
    expect(token != nullptr, "cancellation token created");
    zzrk_cancellation_cancel(token);
    zzrk_cancellation_cancel(token);
    zzrk_cancellation_free(token);
    zzrk_cancellation_free(nullptr);
    zzrk_cancellation_cancel(nullptr);
    expect(true, "cancellation lifecycle is safe");
}

void testRunFirmwareUpgradeGuards()
{
    zzrk_upgrade_outcome outcome{};
    expect(zzrk_run_firmware_upgrade(nullptr, "image.img", 1, true, false,
                                     nullptr, nullptr, nullptr,
                                     &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "null port path rejected");
    expect(zzrk_run_firmware_upgrade("1-2", nullptr, 1, true, false, nullptr,
                                     nullptr, nullptr,
                                     &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "null image path rejected");
    expect(zzrk_run_firmware_upgrade("1-2", "image.img", 7, true, false,
                                     nullptr, nullptr, nullptr,
                                     &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "invalid verify mode rejected");
    expect(zzrk_run_firmware_upgrade("1-2", "image.img", 1, true, false,
                                     nullptr, nullptr, nullptr,
                                     nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "null outcome rejected");
}

void testRunFirmwareUpgradeMissingImage()
{
    zzrk_upgrade_outcome outcome{};
    const zzrk_status status = zzrk_run_firmware_upgrade(
        "1-2", "/definitely/missing/update.img", ZZRK_VERIFY_MODE_CRITICAL,
        true, false, nullptr, nullptr, nullptr, &outcome);
    expect(status == ZZRK_ERR_FAILED, "missing image upgrade fails");
    expect(outcome.error_code == ZZRK_UPGRADE_ERROR_IMAGE_INVALID,
           "missing image reports image-invalid code");
    expect(outcome.error_stage == ZZRK_UPGRADE_STAGE_PARSE_IMAGE,
           "missing image fails at parse-image stage");
    expect(std::strlen(zzrk_last_error_message()) > 0,
           "upgrade failure error message populated");

    zzrk_cancellation* token = zzrk_cancellation_new();
    zzrk_cancellation_cancel(token);
    zzrk_upgrade_outcome cancelled{};
    const zzrk_status cancelledStatus = zzrk_run_firmware_upgrade(
        "1-2", "/definitely/missing/update.img", ZZRK_VERIFY_MODE_CRITICAL,
        true, false, nullptr, nullptr, token, &cancelled);
    expect(cancelledStatus == ZZRK_ERR_FAILED,
           "image parse precedes cancellation checks");
    expect(cancelled.error_code == ZZRK_UPGRADE_ERROR_IMAGE_INVALID,
           "pre-cancelled missing image still reports image-invalid");
    zzrk_cancellation_free(token);
}

void testDeviceOperationGuards()
{
    zzrk_device_inspection inspection{};
    expect(zzrk_inspect_device(nullptr, &inspection) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "inspect device null port rejected");
    expect(zzrk_inspect_device("1-2", nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "inspect device null output rejected");

    uint32_t count = 0;
    uint64_t sectors = 0;
    expect(zzrk_read_partitions(nullptr, nullptr, 0, &count, &sectors) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "read partitions null port rejected");
    expect(zzrk_read_partitions("1-2", nullptr, 0, nullptr, &sectors) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "read partitions null count rejected");
    expect(zzrk_read_partitions("1-2", nullptr, 0, &count, nullptr) ==
               ZZRK_ERR_INVALID_ARGUMENT,
           "read partitions null sectors rejected");

    zzrk_partition_outcome outcome{};
    expect(zzrk_read_partition(nullptr, "boot", "out.img", nullptr, nullptr,
                               nullptr,
                               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "read partition null port rejected");
    expect(zzrk_read_partition("1-2", nullptr, "out.img", nullptr, nullptr,
                               nullptr,
                               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "read partition null partition rejected");
    expect(zzrk_read_partition("1-2", "boot", nullptr, nullptr, nullptr,
                               nullptr,
                               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "read partition null output rejected");
    expect(zzrk_read_partition("1-2", "boot", "out.img", nullptr, nullptr,
                               nullptr, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "read partition null outcome rejected");

    expect(zzrk_write_partition("1-2", "boot", nullptr, true, nullptr,
                                nullptr, nullptr,
                                &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "write partition null input rejected");
    expect(zzrk_erase_partition("1-2", nullptr, nullptr, nullptr, nullptr,
                                &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "erase partition null partition rejected");
}

void testRunImageDownloadGuards()
{    zzrk_download_outcome outcome{};
    const zzrk_download_partition partition{
        .target = "boot", .image_path = "boot.img", .verify = true};
    expect(zzrk_run_image_download(
               nullptr, ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT, nullptr, "loader.bin",
               true, true, &partition, 1, true, nullptr, nullptr, nullptr,
               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "download null port rejected");
    expect(zzrk_run_image_download(
               "1-2", ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT, nullptr, "loader.bin",
               true, true, nullptr, 1, true, nullptr, nullptr, nullptr,
               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "download null partitions with count rejected");
    expect(zzrk_run_image_download(
               "1-2", ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT, nullptr, "loader.bin",
               true, true, &partition, 1, true, nullptr, nullptr, nullptr,
               nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "download null outcome rejected");
    expect(zzrk_run_image_download(
               "1-2", 42, nullptr, "loader.bin", true, true, &partition, 1,
               true, nullptr, nullptr, nullptr,
               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "download invalid layout source rejected");
    expect(zzrk_run_image_download(
               "1-2", ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE, nullptr,
               "loader.bin", true, true, &partition, 1, true, nullptr,
               nullptr, nullptr, &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "parameter layout without parameter path rejected");

    const zzrk_download_partition nullTarget{
        .target = nullptr, .image_path = "boot.img", .verify = true};
    expect(zzrk_run_image_download(
               "1-2", ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT, nullptr, "loader.bin",
               true, true, &nullTarget, 1, true, nullptr, nullptr, nullptr,
               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "download null partition target rejected");
    const zzrk_download_partition nullPath{
        .target = "boot", .image_path = nullptr, .verify = true};
    expect(zzrk_run_image_download(
               "1-2", ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT, nullptr, "loader.bin",
               true, true, &nullPath, 1, true, nullptr, nullptr, nullptr,
               &outcome) == ZZRK_ERR_INVALID_ARGUMENT,
           "download null partition path rejected");
}

void testPrepareImageDownloadGuards()
{
    uint32_t count = 0;
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT,
                                       nullptr, nullptr, true, true, nullptr, 0,
                                       nullptr, 0, &count,
                                       nullptr, 0, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "prepare device-GPT layout without port rejected");
    expect(zzrk_prepare_image_download("1-2", ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       nullptr, nullptr, true, true, nullptr, 0,
                                       nullptr, 0, &count,
                                       nullptr, 0, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "prepare parameter layout without path rejected");
    expect(zzrk_prepare_image_download(nullptr, 42, nullptr, nullptr, true, true,
                                       nullptr, 0, nullptr, 0, &count,
                                       nullptr, 0, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "prepare invalid layout source rejected");
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       "/definitely/missing/parameter.txt",
                                       nullptr, true, true, nullptr, 0, nullptr, 0, nullptr,
                                       nullptr, 0, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "prepare null out_count rejected");
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       "/definitely/missing/parameter.txt",
                                       nullptr, true, true, nullptr, 0, nullptr, 4, &count,
                                       nullptr, 0, nullptr) == ZZRK_ERR_INVALID_ARGUMENT,
           "prepare null items with nonzero capacity rejected");

    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       "/definitely/missing/parameter.txt",
                                       nullptr, true, true, nullptr, 0, nullptr,
                                       0, &count, nullptr, 0, nullptr) == ZZRK_ERR_FAILED,
           "prepare with missing parameter file fails");
    expect(std::strlen(zzrk_last_error_message()) > 0,
           "prepare failure error message populated");
}

void testPrepareImageDownloadParameterFile()
{
    constexpr std::string_view parameter{
        "MACHINE_MODEL: ffi-smoke\n"
        "TYPE: GPT\n"
        "CMDLINE:mtdparts=rk29xxnand:0x20@0x40(boot_a),"
        "0x20@0x60(boot_b),-@0x80(userdata:grow)\n"};
    const auto path = std::filesystem::temp_directory_path() /
                      "zzrk-ffi-prepare-test-parameter.txt";
    {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out << parameter;
    }

    uint32_t count = 0;
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       path.string().c_str(), nullptr, true, true,
                                       nullptr, 0, nullptr, 0, &count,
                                       nullptr, 0, nullptr) == ZZRK_OK,
           "prepare from parameter file succeeds");
    expect(count == 1, "parameter-only plan contains the GPT item");

    zzrk_download_plan_item items[4]{};
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       path.string().c_str(), nullptr, true, true,
                                       nullptr, 0, items, 4, &count,
                                       nullptr, 0, nullptr) == ZZRK_OK,
           "prepare with item buffer succeeds");
    expect(count == 1 && items[0].kind == ZZRK_DOWNLOAD_ITEM_PARTITION_TABLE &&
               std::string_view{items[0].target} == "GPT" &&
               items[0].sector_count == 67 &&
               items[0].format == ZZRK_PAYLOAD_FORMAT_ROCKCHIP_PARAMETER,
           "GPT plan item carries kind, target, sectors and format");
    expect(std::strlen(items[0].md5) == 32,
           "GPT plan item carries an MD5 digest");

    zzrk_partition_entry layout[8]{};
    uint32_t layoutCount = 0;
    expect(zzrk_prepare_image_download(nullptr, ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE,
                                       path.string().c_str(), nullptr, true, true,
                                       nullptr, 0, nullptr, 0, &count, layout, 8,
                                       &layoutCount) == ZZRK_OK,
           "prepare reports the full layout");
    expect(layoutCount == 3 &&
               std::string_view{layout[0].name} == "boot_a" &&
               layout[0].first_lba == 0x40 && layout[0].sector_count == 0x20 &&
               std::string_view{layout[2].name} == "userdata",
           "layout entries carry every parameter-file partition");

    std::filesystem::remove(path);
}

} // namespace

int main()
{
    testVersionString();
    testLastErrorLifecycle();
    testInspectFirmwarePaths();
    testScanDevices();
    testCancellationToken();
    testRunFirmwareUpgradeGuards();
    testRunFirmwareUpgradeMissingImage();
    testDeviceOperationGuards();
    testRunImageDownloadGuards();
    testPrepareImageDownloadGuards();
    testPrepareImageDownloadParameterFile();

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed\n";
        return 1;
    }
    std::cout << "ffi_smoke_test: all expectations passed\n";
    return 0;
}
