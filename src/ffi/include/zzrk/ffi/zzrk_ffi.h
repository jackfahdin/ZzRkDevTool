// SPDX-License-Identifier: MIT

#ifndef ZZRK_FFI_H
#define ZZRK_FFI_H

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32) && defined(ZZRK_FFI_SHARED)
#if defined(ZZRK_FFI_BUILD)
#define ZZRK_FFI_API __declspec(dllexport)
#else
#define ZZRK_FFI_API __declspec(dllimport)
#endif
#else
#define ZZRK_FFI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Generic call status. Domain-specific failure detail is carried by the
// outcome structs and zzrk_last_error_message().
typedef enum zzrk_status {
    ZZRK_OK = 0,
    ZZRK_ERR_INVALID_ARGUMENT = 1,
    ZZRK_ERR_CANCELLED = 2,
    ZZRK_ERR_FAILED = 3,
} zzrk_status;

// Human-readable description of the most recent failure on the calling
// thread. Empty string when the last call succeeded. The pointer is owned
// by the library and invalidated by the next zzrk_* call on this thread.
ZZRK_FFI_API const char* zzrk_last_error_message(void);

// Library version string (static storage, never freed).
ZZRK_FFI_API const char* zzrk_version_string(void);

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------

typedef enum zzrk_device_mode {
    ZZRK_DEVICE_MODE_UNKNOWN = 0,
    ZZRK_DEVICE_MODE_ADB = 1,
    ZZRK_DEVICE_MODE_MASKROM = 2,
    ZZRK_DEVICE_MODE_LOADER = 3,
} zzrk_device_mode;

typedef struct zzrk_usb_device_info {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_usb;
    uint16_t bcd_device;
    uint8_t bus_number;
    uint8_t device_address;
    int32_t mode; // zzrk_device_mode
    char port_path[64];
    uint8_t port_numbers[16];
    uint8_t port_number_count;
} zzrk_usb_device_info;

// Scans for Rockchip USB devices. `devices` may be NULL to only query the
// count. On success *out_count receives the total number of devices found;
// at most `capacity` entries are copied into `devices`.
ZZRK_FFI_API zzrk_status
zzrk_scan_devices(zzrk_usb_device_info* devices, uint32_t capacity,
                  uint32_t* out_count);

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

// Opaque cancellation token. Thread-safe: zzrk_cancellation_cancel may be
// called from any thread while an operation runs on another.
typedef struct zzrk_cancellation zzrk_cancellation;

ZZRK_FFI_API zzrk_cancellation* zzrk_cancellation_new(void);
ZZRK_FFI_API void zzrk_cancellation_cancel(zzrk_cancellation* token);
ZZRK_FFI_API void zzrk_cancellation_free(zzrk_cancellation* token);

// ---------------------------------------------------------------------------
// Firmware image inspection
// ---------------------------------------------------------------------------

typedef struct zzrk_firmware_info {
    char chip[32];
    char model[64];
    char manufacturer[64];
    char md5[33];
    bool md5_verified;
    uint64_t file_size;
    uint32_t partition_count;
} zzrk_firmware_info;

// Parses an RK update image and verifies its trailing MD5. Blocking; large
// images take seconds for the digest pass.
ZZRK_FFI_API zzrk_status zzrk_inspect_firmware(const char* image_path,
                                               zzrk_firmware_info* out_info);

// ---------------------------------------------------------------------------
// Firmware upgrade (update.img)
// ---------------------------------------------------------------------------

typedef enum zzrk_verify_mode {
    ZZRK_VERIFY_MODE_NONE = 0,
    ZZRK_VERIFY_MODE_CRITICAL = 1,
    ZZRK_VERIFY_MODE_ALL = 2,
} zzrk_verify_mode;

// Mirrors workflow::FirmwareUpgradeStage.
typedef enum zzrk_upgrade_stage {
    ZZRK_UPGRADE_STAGE_PARSE_IMAGE = 0,
    ZZRK_UPGRADE_STAGE_VERIFY_IMAGE = 1,
    ZZRK_UPGRADE_STAGE_PLAN_IMAGE = 2,
    ZZRK_UPGRADE_STAGE_SELECT_DEVICE = 3,
    ZZRK_UPGRADE_STAGE_ENTER_MASKROM = 4,
    ZZRK_UPGRADE_STAGE_WAIT_MASKROM = 5,
    ZZRK_UPGRADE_STAGE_DOWNLOAD_LOADER = 6,
    ZZRK_UPGRADE_STAGE_WAIT_RAM_LOADER = 7,
    ZZRK_UPGRADE_STAGE_CHECK_RAM_LOADER = 8,
    ZZRK_UPGRADE_STAGE_CHECK_STORAGE = 9,
    ZZRK_UPGRADE_STAGE_FLASH = 10,
    ZZRK_UPGRADE_STAGE_RESET = 11,
} zzrk_upgrade_stage;

// Mirrors image::FirmwareFlashPhase; ZZRK_FLASH_PHASE_NONE when absent.
typedef enum zzrk_flash_phase {
    ZZRK_FLASH_PHASE_NONE = -1,
    ZZRK_FLASH_PHASE_WRITE_PARTITION_TABLE = 0,
    ZZRK_FLASH_PHASE_VERIFY_PARTITION_TABLE = 1,
    ZZRK_FLASH_PHASE_ERASE_PARTITION = 2,
    ZZRK_FLASH_PHASE_WRITE_PARTITION = 3,
    ZZRK_FLASH_PHASE_VERIFY_PARTITION = 4,
    ZZRK_FLASH_PHASE_WRITE_LOADER = 5,
    ZZRK_FLASH_PHASE_VERIFY_LOADER = 6,
} zzrk_flash_phase;

typedef struct zzrk_upgrade_progress_event {
    int32_t stage; // zzrk_upgrade_stage
    const char* target; // may be NULL; valid only during the callback
    uint64_t completed_bytes;
    uint64_t total_bytes;
    int32_t flash_phase; // zzrk_flash_phase
} zzrk_upgrade_progress_event;

// Invoked on the operation's worker thread; the implementation must marshal
// to a UI thread if needed. Must not call back into blocking zzrk_* APIs.
typedef void (*zzrk_upgrade_progress_fn)(
    const zzrk_upgrade_progress_event* event, void* user_data);

// Mirrors workflow::FirmwareUpgradeErrorCode; -1 in outcome when succeeded.
typedef enum zzrk_upgrade_error_code {
    ZZRK_UPGRADE_ERROR_NONE = -1,
    ZZRK_UPGRADE_ERROR_CANCELLED = 0,
    ZZRK_UPGRADE_ERROR_IMAGE_INVALID = 1,
    ZZRK_UPGRADE_ERROR_DIGEST_FAILED = 2,
    ZZRK_UPGRADE_ERROR_PLAN_INVALID = 3,
    ZZRK_UPGRADE_ERROR_SCAN_FAILED = 4,
    ZZRK_UPGRADE_ERROR_DEVICE_NOT_FOUND = 5,
    ZZRK_UPGRADE_ERROR_DEVICE_AMBIGUOUS = 6,
    ZZRK_UPGRADE_ERROR_DEVICE_UNSUPPORTED = 7,
    ZZRK_UPGRADE_ERROR_OPEN_FAILED = 8,
    ZZRK_UPGRADE_ERROR_LOADER_RESET_FAILED = 9,
    ZZRK_UPGRADE_ERROR_REENUMERATION_TIMED_OUT = 10,
    ZZRK_UPGRADE_ERROR_LOADER_DOWNLOAD_FAILED = 11,
    ZZRK_UPGRADE_ERROR_LOADER_NOT_READY = 12,
    ZZRK_UPGRADE_ERROR_WRONG_CHIP = 13,
    ZZRK_UPGRADE_ERROR_STORAGE_CHECK_FAILED = 14,
    ZZRK_UPGRADE_ERROR_STORAGE_NOT_EMMC = 15,
    ZZRK_UPGRADE_ERROR_DEVICE_TOO_SMALL = 16,
    ZZRK_UPGRADE_ERROR_FLASH_FAILED = 17,
    ZZRK_UPGRADE_ERROR_RESET_FAILED = 18,
    ZZRK_UPGRADE_ERROR_CLOSE_FAILED = 19,
} zzrk_upgrade_error_code;

typedef struct zzrk_upgrade_outcome {
    int32_t error_code; // zzrk_upgrade_error_code
    int32_t error_stage; // zzrk_upgrade_stage, meaningful on failure
    char error_target[256];
    char model[64];
    char firmware_md5[33];
    uint64_t device_sectors;
    uint64_t partition_bytes_written;
    uint64_t sectors_erased;
    uint32_t partitions_verified;
    bool prepared_only;
    bool partition_table_written;
    bool partition_table_verified;
    bool loader_written;
    bool loader_verified;
    bool reset_accepted;
} zzrk_upgrade_outcome;

// Runs a complete update.img upgrade. Blocking; drive progress through the
// callback and cancel through the token from another thread.
ZZRK_FFI_API zzrk_status zzrk_run_firmware_upgrade(
    const char* port_path, const char* image_path, int32_t verify_mode,
    bool reboot, bool prepare_only, zzrk_upgrade_progress_fn progress_fn,
    void* user_data, const zzrk_cancellation* cancellation,
    zzrk_upgrade_outcome* out_outcome);

// ---------------------------------------------------------------------------
// Device inspection and partitions
// ---------------------------------------------------------------------------

typedef struct zzrk_device_inspection {
    zzrk_usb_device_info device;
    uint8_t chip_info[16];
    uint8_t capability[8];
    uint8_t flash_info[11];
    uint8_t flash_id[5];
    bool flash_id_is_emmc;
    uint32_t legacy_flash_sector_count;
    uint16_t legacy_flash_block_size_code;
    uint8_t legacy_flash_page_size_code;
    uint8_t legacy_flash_ecc_bits;
    uint8_t legacy_flash_access_time;
    uint8_t legacy_flash_manufacturer_code;
    uint8_t legacy_flash_mask;
} zzrk_device_inspection;

ZZRK_FFI_API zzrk_status zzrk_inspect_device(
    const char* port_path, zzrk_device_inspection* out_inspection);

typedef struct zzrk_partition_entry {
    char name[128];
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t sector_count;
    uint64_t attributes;
} zzrk_partition_entry;

// Reads the device GPT. `entries` may be NULL to only query the count.
ZZRK_FFI_API zzrk_status zzrk_read_partitions(
    const char* port_path, zzrk_partition_entry* entries, uint32_t capacity,
    uint32_t* out_count, uint64_t* out_device_sectors);

// Mirrors workflow::DeviceManagementErrorCode; -1 in outcome when succeeded.
typedef enum zzrk_device_error_code {
    ZZRK_DEVICE_ERROR_NONE = -1,
    ZZRK_DEVICE_ERROR_CANCELLED = 0,
    ZZRK_DEVICE_ERROR_SCAN_FAILED = 1,
    ZZRK_DEVICE_ERROR_DEVICE_NOT_FOUND = 2,
    ZZRK_DEVICE_ERROR_DEVICE_AMBIGUOUS = 3,
    ZZRK_DEVICE_ERROR_DEVICE_UNSUPPORTED = 4,
    ZZRK_DEVICE_ERROR_OPEN_FAILED = 5,
    ZZRK_DEVICE_ERROR_CHIP_INFO_FAILED = 6,
    ZZRK_DEVICE_ERROR_CAPABILITY_FAILED = 7,
    ZZRK_DEVICE_ERROR_FLASH_INFO_FAILED = 8,
    ZZRK_DEVICE_ERROR_FLASH_ID_FAILED = 9,
    ZZRK_DEVICE_ERROR_STORAGE_NOT_EMMC = 10,
    ZZRK_DEVICE_ERROR_GPT_FAILED = 11,
    ZZRK_DEVICE_ERROR_PARTITION_NOT_FOUND = 12,
    ZZRK_DEVICE_ERROR_PARTITION_AMBIGUOUS = 13,
    ZZRK_DEVICE_ERROR_INPUT_INVALID = 14,
    ZZRK_DEVICE_ERROR_INPUT_TOO_LARGE = 15,
    ZZRK_DEVICE_ERROR_INPUT_OPEN_FAILED = 16,
    ZZRK_DEVICE_ERROR_INPUT_READ_FAILED = 17,
    ZZRK_DEVICE_ERROR_OUTPUT_EXISTS = 18,
    ZZRK_DEVICE_ERROR_OUTPUT_OPEN_FAILED = 19,
    ZZRK_DEVICE_ERROR_OUTPUT_WRITE_FAILED = 20,
    ZZRK_DEVICE_ERROR_OUTPUT_FINALIZE_FAILED = 21,
    ZZRK_DEVICE_ERROR_READ_FAILED = 22,
    ZZRK_DEVICE_ERROR_WRITE_FAILED = 23,
    ZZRK_DEVICE_ERROR_VERIFY_FAILED = 24,
    ZZRK_DEVICE_ERROR_ERASE_FAILED = 25,
    ZZRK_DEVICE_ERROR_CLOSE_FAILED = 26,
} zzrk_device_error_code;

typedef enum zzrk_partition_phase {
    ZZRK_PARTITION_PHASE_READ = 0,
    ZZRK_PARTITION_PHASE_WRITE = 1,
    ZZRK_PARTITION_PHASE_VERIFY = 2,
    ZZRK_PARTITION_PHASE_ERASE = 3,
} zzrk_partition_phase;

typedef struct zzrk_partition_progress_event {
    int32_t phase; // zzrk_partition_phase
    const char* partition; // valid only during the callback
    uint64_t completed_bytes;
    uint64_t total_bytes;
} zzrk_partition_progress_event;

typedef void (*zzrk_partition_progress_fn)(
    const zzrk_partition_progress_event* event, void* user_data);

typedef struct zzrk_partition_outcome {
    int32_t error_code; // zzrk_device_error_code
    char error_target[256];
    int32_t phase; // zzrk_partition_phase
    uint64_t bytes_processed;
    uint64_t sectors_processed;
    bool verified;
} zzrk_partition_outcome;

ZZRK_FFI_API zzrk_status zzrk_read_partition(
    const char* port_path, const char* partition, const char* output_path,
    zzrk_partition_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome);

ZZRK_FFI_API zzrk_status zzrk_write_partition(
    const char* port_path, const char* partition, const char* input_path,
    bool verify, zzrk_partition_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome);

ZZRK_FFI_API zzrk_status zzrk_erase_partition(
    const char* port_path, const char* partition,
    zzrk_partition_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_partition_outcome* out_outcome);

// ---------------------------------------------------------------------------
// Image download (loose images + parameter layout)
// ---------------------------------------------------------------------------

typedef enum zzrk_download_layout_source {
    ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT = 0,
    ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE = 1,
} zzrk_download_layout_source;

typedef struct zzrk_download_partition {
    const char* target;
    const char* image_path;
    bool verify;
} zzrk_download_partition;

// Mirrors workflow::ImageDownloadStage.
typedef enum zzrk_download_stage {
    ZZRK_DOWNLOAD_STAGE_INSPECT_FILES = 0,
    ZZRK_DOWNLOAD_STAGE_VERIFY_FILES = 1,
    ZZRK_DOWNLOAD_STAGE_SELECT_DEVICE = 2,
    ZZRK_DOWNLOAD_STAGE_ENTER_MASKROM = 3,
    ZZRK_DOWNLOAD_STAGE_WAIT_MASKROM = 4,
    ZZRK_DOWNLOAD_STAGE_DOWNLOAD_LOADER = 5,
    ZZRK_DOWNLOAD_STAGE_WAIT_RAM_LOADER = 6,
    ZZRK_DOWNLOAD_STAGE_CHECK_RAM_LOADER = 7,
    ZZRK_DOWNLOAD_STAGE_CHECK_STORAGE = 8,
    ZZRK_DOWNLOAD_STAGE_VALIDATE_LAYOUT = 9,
    ZZRK_DOWNLOAD_STAGE_ERASE = 10,
    ZZRK_DOWNLOAD_STAGE_WRITE = 11,
    ZZRK_DOWNLOAD_STAGE_VERIFY = 12,
    ZZRK_DOWNLOAD_STAGE_RESET = 13,
} zzrk_download_stage;

typedef struct zzrk_download_progress_event {
    int32_t stage; // zzrk_download_stage
    const char* target; // may be NULL; valid only during the callback
    uint64_t completed_bytes;
    uint64_t total_bytes;
    uint64_t completed_plan_bytes;
    uint64_t total_plan_bytes;
    int32_t item_index; // -1 when none
} zzrk_download_progress_event;

typedef void (*zzrk_download_progress_fn)(
    const zzrk_download_progress_event* event, void* user_data);

// Mirrors workflow::ImageDownloadErrorCode; -1 in outcome when succeeded.
typedef enum zzrk_download_error_code {
    ZZRK_DOWNLOAD_ERROR_NONE = -1,
    ZZRK_DOWNLOAD_ERROR_CANCELLED = 0,
    ZZRK_DOWNLOAD_ERROR_INVALID_REQUEST = 1,
    ZZRK_DOWNLOAD_ERROR_IMAGE_INVALID = 2,
    ZZRK_DOWNLOAD_ERROR_DIGEST_FAILED = 3,
    ZZRK_DOWNLOAD_ERROR_DIGEST_CHANGED = 4,
    ZZRK_DOWNLOAD_ERROR_LAYOUT_INVALID = 5,
    ZZRK_DOWNLOAD_ERROR_SCAN_FAILED = 6,
    ZZRK_DOWNLOAD_ERROR_DEVICE_NOT_FOUND = 7,
    ZZRK_DOWNLOAD_ERROR_DEVICE_AMBIGUOUS = 8,
    ZZRK_DOWNLOAD_ERROR_DEVICE_UNSUPPORTED = 9,
    ZZRK_DOWNLOAD_ERROR_LOADER_REQUIRED = 10,
    ZZRK_DOWNLOAD_ERROR_OPEN_FAILED = 11,
    ZZRK_DOWNLOAD_ERROR_LOADER_RESET_FAILED = 12,
    ZZRK_DOWNLOAD_ERROR_REENUMERATION_TIMED_OUT = 13,
    ZZRK_DOWNLOAD_ERROR_LOADER_DOWNLOAD_FAILED = 14,
    ZZRK_DOWNLOAD_ERROR_LOADER_NOT_READY = 15,
    ZZRK_DOWNLOAD_ERROR_WRONG_CHIP = 16,
    ZZRK_DOWNLOAD_ERROR_STORAGE_CHECK_FAILED = 17,
    ZZRK_DOWNLOAD_ERROR_STORAGE_NOT_EMMC = 18,
    ZZRK_DOWNLOAD_ERROR_DEVICE_TOO_SMALL = 19,
    ZZRK_DOWNLOAD_ERROR_DEVICE_LAYOUT_CHANGED = 20,
    ZZRK_DOWNLOAD_ERROR_FLASH_FAILED = 21,
    ZZRK_DOWNLOAD_ERROR_RESET_FAILED = 22,
    ZZRK_DOWNLOAD_ERROR_CLOSE_FAILED = 23,
} zzrk_download_error_code;

typedef struct zzrk_download_outcome {
    int32_t error_code; // zzrk_download_error_code
    int32_t error_stage; // zzrk_download_stage, meaningful on failure
    char error_target[256];
    int32_t error_item_index; // -1 when none
    uint64_t device_sectors;
    uint64_t partition_bytes_written;
    uint64_t sectors_erased;
    uint32_t items_written;
    uint32_t items_verified;
    bool loader_written;
    bool partition_table_written;
    bool reset_accepted;
} zzrk_download_outcome;

// Runs a loose-image download: prepares from the parameter file or the
// device GPT (read internally), then executes. `loader_path` is required
// when the layout does not provide one; pass NULL when unused. Blocking.
ZZRK_FFI_API zzrk_status zzrk_run_image_download(
    const char* port_path, int32_t layout_source, const char* parameter_path,
    const char* loader_path, bool verify_loader, bool verify_partition_table,
    const zzrk_download_partition* partitions, uint32_t partition_count,
    bool reboot, zzrk_download_progress_fn progress_fn, void* user_data,
    const zzrk_cancellation* cancellation,
    zzrk_download_outcome* out_outcome);

// ---------------------------------------------------------------------------
// Image download plan preparation (no execution, no device writes)
// ---------------------------------------------------------------------------

// Mirrors workflow::ImageDownloadItemKind.
typedef enum zzrk_download_item_kind {
    ZZRK_DOWNLOAD_ITEM_LOADER = 0,
    ZZRK_DOWNLOAD_ITEM_PARTITION_TABLE = 1,
    ZZRK_DOWNLOAD_ITEM_PARTITION = 2,
} zzrk_download_item_kind;

// Mirrors image::PayloadFormat.
typedef enum zzrk_payload_format {
    ZZRK_PAYLOAD_FORMAT_UNKNOWN = 0,
    ZZRK_PAYLOAD_FORMAT_RAW = 1,
    ZZRK_PAYLOAD_FORMAT_ANDROID_SPARSE = 2,
    ZZRK_PAYLOAD_FORMAT_ROCKCHIP_PARAMETER = 3,
} zzrk_payload_format;

typedef struct zzrk_download_plan_item {
    int32_t kind; // zzrk_download_item_kind
    int32_t format; // zzrk_payload_format
    char target[128];
    char source_path[1024];
    uint64_t first_lba;
    uint64_t sector_count;
    uint64_t output_size;
    bool verify;
    char md5[33];
} zzrk_download_plan_item;

// Prepares an image-download plan (parse + inspect + digests) without
// executing it. With ZZRK_DOWNLOAD_LAYOUT_PARAMETER_FILE this is file-only
// and never touches a device (`port_path` may be NULL). With
// ZZRK_DOWNLOAD_LAYOUT_DEVICE_GPT the device GPT is read first (read-only;
// `port_path` required). `items` may be NULL to only query the count; at
// most `capacity` entries are copied. The layout partitions (every partition
// of the parameter file or device GPT, including ones without a selected
// image) are reported through `layout`/`out_layout_count` when both are
// non-NULL. Blocking; digest passes on large images take time.
ZZRK_FFI_API zzrk_status zzrk_prepare_image_download(
    const char* port_path, int32_t layout_source, const char* parameter_path,
    const char* loader_path, bool verify_loader, bool verify_partition_table,
    const zzrk_download_partition* partitions, uint32_t partition_count,
    zzrk_download_plan_item* items, uint32_t capacity, uint32_t* out_count,
    zzrk_partition_entry* layout, uint32_t layout_capacity,
    uint32_t* out_layout_count);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ZZRK_FFI_H
