//! Safe-ish Rust wrapper over the ZzRkDevTool C ABI (`zzrk_ffi`).
//!
//! Raw bindgen output is kept private; consumers use the typed API below.
//! All blocking entry points must be called from worker threads; progress
//! callbacks are invoked on the C++ operation thread and marshalled here
//! through `catch_unwind` so a panicking closure can never cross the FFI
//! boundary.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

mod ffi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

use std::ffi::{CStr, CString};
use std::os::raw::c_void;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::Arc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Human-readable description of the most recent failure on this thread.
pub fn last_error() -> String {
    unsafe { cstr_to_string(ffi::zzrk_last_error_message()) }
}

unsafe fn cstr_to_string(ptr: *const std::os::raw::c_char) -> String {
    if ptr.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(ptr) }
        .to_string_lossy()
        .into_owned()
}

unsafe fn cstr_array_to_string(ptr: *const std::os::raw::c_char) -> String {
    // Fixed-size char arrays inside outcome structs are always NUL-terminated
    // by the C side (copyString truncates with a terminator).
    unsafe { cstr_to_string(ptr) }
}

fn cstring(s: &str) -> Result<CString, String> {
    CString::new(s).map_err(|_| "argument contains an interior NUL byte".to_string())
}

fn cstring_path(path: impl AsRef<Path>) -> Result<CString, String> {
    CString::new(path.as_ref().as_os_str().as_encoded_bytes())
        .map_err(|_| "path contains an interior NUL byte".to_string())
}

macro_rules! impl_from_raw {
    ($ty:ty, $($v:ident),+ $(,)?) => {
        impl $ty {
            /// Maps a raw C ABI discriminant to the enum; `None` when the
            /// value is outside the contract.
            pub fn from_raw(v: i32) -> Option<Self> {
                match v {
                    $(x if x == <$ty>::$v as i32 => Some(<$ty>::$v),)+
                    _ => None,
                }
            }
        }
    };
}

// ---------------------------------------------------------------------------
// Library info
// ---------------------------------------------------------------------------

/// Library version string.
pub fn version() -> String {
    unsafe { cstr_to_string(ffi::zzrk_version_string()) }
}

// ---------------------------------------------------------------------------
// Device discovery
// ---------------------------------------------------------------------------

/// Device operating mode as reported by the scan.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceMode {
    Unknown,
    Adb,
    Maskrom,
    Loader,
    Other(i32),
}

impl From<i32> for DeviceMode {
    fn from(v: i32) -> Self {
        match v as u32 {
            ffi::zzrk_device_mode_ZZRK_DEVICE_MODE_ADB => DeviceMode::Adb,
            ffi::zzrk_device_mode_ZZRK_DEVICE_MODE_MASKROM => DeviceMode::Maskrom,
            ffi::zzrk_device_mode_ZZRK_DEVICE_MODE_LOADER => DeviceMode::Loader,
            ffi::zzrk_device_mode_ZZRK_DEVICE_MODE_UNKNOWN => DeviceMode::Unknown,
            _ => DeviceMode::Other(v),
        }
    }
}

/// One Rockchip USB device discovered by [`scan_devices`].
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub vendor_id: u16,
    pub product_id: u16,
    pub bcd_usb: u16,
    pub bcd_device: u16,
    pub bus_number: u8,
    pub device_address: u8,
    pub mode: DeviceMode,
    pub port_path: String,
}

impl DeviceInfo {
    unsafe fn from_c(d: &ffi::zzrk_usb_device_info) -> Self {
        Self {
            vendor_id: d.vendor_id,
            product_id: d.product_id,
            bcd_usb: d.bcd_usb,
            bcd_device: d.bcd_device,
            bus_number: d.bus_number,
            device_address: d.device_address,
            mode: DeviceMode::from(d.mode),
            port_path: unsafe { cstr_to_string(d.port_path.as_ptr()) },
        }
    }
}

/// Scans for Rockchip USB devices. Returns an empty list when none are
/// attached; `Err` carries `zzrk_last_error_message()` on failure.
pub fn scan_devices() -> Result<Vec<DeviceInfo>, String> {
    unsafe {
        let mut count: u32 = 0;
        let status = ffi::zzrk_scan_devices(std::ptr::null_mut(), 0, &mut count);
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        if count == 0 {
            return Ok(Vec::new());
        }
        let mut raw = vec![std::mem::zeroed::<ffi::zzrk_usb_device_info>(); count as usize];
        let status = ffi::zzrk_scan_devices(raw.as_mut_ptr(), count, &mut count);
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        raw.truncate(count as usize);
        Ok(raw.iter().map(|d| DeviceInfo::from_c(d)).collect())
    }
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

struct CancellationInner {
    raw: *mut ffi::zzrk_cancellation,
}

// The C token is documented thread-safe: zzrk_cancellation_cancel may be
// called from any thread while an operation runs on another.
unsafe impl Send for CancellationInner {}
unsafe impl Sync for CancellationInner {}

impl Drop for CancellationInner {
    fn drop(&mut self) {
        unsafe { ffi::zzrk_cancellation_free(self.raw) }
    }
}

/// Opaque cancellation token. `Clone` shares the underlying token: cancelling
/// any clone cancels the operation. Freed when the last clone drops.
#[derive(Clone)]
pub struct Cancellation {
    inner: Arc<CancellationInner>,
}

impl Cancellation {
    pub fn new() -> Self {
        let raw = unsafe { ffi::zzrk_cancellation_new() };
        assert!(!raw.is_null(), "zzrk_cancellation_new returned null");
        Self {
            inner: Arc::new(CancellationInner { raw }),
        }
    }

    /// Requests cancellation; safe to call from any thread, idempotent.
    pub fn cancel(&self) {
        unsafe { ffi::zzrk_cancellation_cancel(self.inner.raw) }
    }

    fn as_ptr(&self) -> *const ffi::zzrk_cancellation {
        self.inner.raw
    }
}

impl Default for Cancellation {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------
// Firmware image inspection
// ---------------------------------------------------------------------------

/// Parsed metadata of an RK update image.
#[derive(Debug, Clone)]
pub struct FirmwareInfo {
    pub chip: String,
    pub model: String,
    pub manufacturer: String,
    pub md5: String,
    pub md5_verified: bool,
    pub file_size: u64,
    pub partition_count: u32,
}

/// Parses an RK update image and verifies its trailing MD5. Blocking; large
/// images take seconds for the digest pass — call from a worker thread.
pub fn inspect_firmware(path: impl AsRef<Path>) -> Result<FirmwareInfo, String> {
    let path = cstring_path(path)?;
    unsafe {
        let mut info: ffi::zzrk_firmware_info = std::mem::zeroed();
        let status = ffi::zzrk_inspect_firmware(path.as_ptr(), &mut info);
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        Ok(FirmwareInfo {
            chip: cstr_array_to_string(info.chip.as_ptr()),
            model: cstr_array_to_string(info.model.as_ptr()),
            manufacturer: cstr_array_to_string(info.manufacturer.as_ptr()),
            md5: cstr_array_to_string(info.md5.as_ptr()),
            md5_verified: info.md5_verified,
            file_size: info.file_size,
            partition_count: info.partition_count,
        })
    }
}

// ---------------------------------------------------------------------------
// Firmware upgrade
// ---------------------------------------------------------------------------

/// Firmware verification effort during flashing.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VerifyMode {
    None = 0,
    Critical = 1,
    All = 2,
}

impl_from_raw!(VerifyMode, None, Critical, All);

/// Mirrors `zzrk_upgrade_stage`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UpgradeStage {
    ParseImage = 0,
    VerifyImage = 1,
    PlanImage = 2,
    SelectDevice = 3,
    EnterMaskrom = 4,
    WaitMaskrom = 5,
    DownloadLoader = 6,
    WaitRamLoader = 7,
    CheckRamLoader = 8,
    CheckStorage = 9,
    Flash = 10,
    Reset = 11,
}

impl_from_raw!(
    UpgradeStage,
    ParseImage,
    VerifyImage,
    PlanImage,
    SelectDevice,
    EnterMaskrom,
    WaitMaskrom,
    DownloadLoader,
    WaitRamLoader,
    CheckRamLoader,
    CheckStorage,
    Flash,
    Reset
);

impl UpgradeStage {
    pub fn name(self) -> &'static str {
        match self {
            UpgradeStage::ParseImage => "parse_image",
            UpgradeStage::VerifyImage => "verify_image",
            UpgradeStage::PlanImage => "plan_image",
            UpgradeStage::SelectDevice => "select_device",
            UpgradeStage::EnterMaskrom => "enter_maskrom",
            UpgradeStage::WaitMaskrom => "wait_maskrom",
            UpgradeStage::DownloadLoader => "download_loader",
            UpgradeStage::WaitRamLoader => "wait_ram_loader",
            UpgradeStage::CheckRamLoader => "check_ram_loader",
            UpgradeStage::CheckStorage => "check_storage",
            UpgradeStage::Flash => "flash",
            UpgradeStage::Reset => "reset",
        }
    }
}

/// Mirrors `zzrk_flash_phase` (without the NONE sentinel; see
/// [`UpgradeProgressEvent::flash_phase`]).
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FlashPhase {
    WritePartitionTable = 0,
    VerifyPartitionTable = 1,
    ErasePartition = 2,
    WritePartition = 3,
    VerifyPartition = 4,
    WriteLoader = 5,
    VerifyLoader = 6,
}

impl_from_raw!(
    FlashPhase,
    WritePartitionTable,
    VerifyPartitionTable,
    ErasePartition,
    WritePartition,
    VerifyPartition,
    WriteLoader,
    VerifyLoader
);

/// Mirrors `zzrk_upgrade_error_code`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UpgradeErrorCode {
    None = -1,
    Cancelled = 0,
    ImageInvalid = 1,
    DigestFailed = 2,
    PlanInvalid = 3,
    ScanFailed = 4,
    DeviceNotFound = 5,
    DeviceAmbiguous = 6,
    DeviceUnsupported = 7,
    OpenFailed = 8,
    LoaderResetFailed = 9,
    ReenumerationTimedOut = 10,
    LoaderDownloadFailed = 11,
    LoaderNotReady = 12,
    WrongChip = 13,
    StorageCheckFailed = 14,
    StorageNotEmmc = 15,
    DeviceTooSmall = 16,
    FlashFailed = 17,
    ResetFailed = 18,
    CloseFailed = 19,
}

impl_from_raw!(
    UpgradeErrorCode,
    None,
    Cancelled,
    ImageInvalid,
    DigestFailed,
    PlanInvalid,
    ScanFailed,
    DeviceNotFound,
    DeviceAmbiguous,
    DeviceUnsupported,
    OpenFailed,
    LoaderResetFailed,
    ReenumerationTimedOut,
    LoaderDownloadFailed,
    LoaderNotReady,
    WrongChip,
    StorageCheckFailed,
    StorageNotEmmc,
    DeviceTooSmall,
    FlashFailed,
    ResetFailed,
    CloseFailed
);

/// Progress event delivered from the C++ worker thread.
#[derive(Debug, Clone)]
pub struct UpgradeProgressEvent {
    stage: i32,
    pub target: Option<String>,
    pub completed_bytes: u64,
    pub total_bytes: u64,
    flash_phase: i32,
}

impl UpgradeProgressEvent {
    unsafe fn from_c(e: &ffi::zzrk_upgrade_progress_event) -> Self {
        Self {
            stage: e.stage,
            target: if e.target.is_null() {
                None
            } else {
                Some(unsafe { cstr_to_string(e.target) })
            },
            completed_bytes: e.completed_bytes,
            total_bytes: e.total_bytes,
            flash_phase: e.flash_phase,
        }
    }

    pub fn stage(&self) -> Option<UpgradeStage> {
        UpgradeStage::from_raw(self.stage)
    }

    pub fn flash_phase(&self) -> Option<FlashPhase> {
        FlashPhase::from_raw(self.flash_phase)
    }

    pub fn fraction(&self) -> Option<f64> {
        if self.total_bytes == 0 {
            None
        } else {
            Some(self.completed_bytes as f64 / self.total_bytes as f64)
        }
    }
}

/// Success outcome of a firmware upgrade run.
#[derive(Debug, Clone)]
pub struct UpgradeOutcome {
    pub model: String,
    pub firmware_md5: String,
    pub device_sectors: u64,
    pub partition_bytes_written: u64,
    pub sectors_erased: u64,
    pub partitions_verified: u32,
    pub prepared_only: bool,
    pub partition_table_written: bool,
    pub partition_table_verified: bool,
    pub loader_written: bool,
    pub loader_verified: bool,
    pub reset_accepted: bool,
}

/// Structured failure of a firmware upgrade run.
#[derive(Debug, Clone)]
pub struct UpgradeError {
    /// Raw `zzrk_upgrade_error_code` value.
    pub error_code: i32,
    /// Raw `zzrk_upgrade_stage` value at the point of failure.
    pub error_stage: i32,
    pub error_target: String,
    /// `zzrk_last_error_message()` captured at return.
    pub message: String,
}

impl UpgradeError {
    pub fn code(&self) -> Option<UpgradeErrorCode> {
        UpgradeErrorCode::from_raw(self.error_code)
    }

    pub fn stage(&self) -> Option<UpgradeStage> {
        UpgradeStage::from_raw(self.error_stage)
    }
}

impl std::fmt::Display for UpgradeError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for UpgradeError {}

unsafe extern "C" fn upgrade_progress_trampoline<F: FnMut(UpgradeProgressEvent)>(
    event: *const ffi::zzrk_upgrade_progress_event,
    user_data: *mut c_void,
) {
    // A panicking Rust closure must never unwind across the FFI boundary
    // (that would be undefined behaviour); swallow and keep the C++ side
    // running.
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if user_data.is_null() || event.is_null() {
            return;
        }
        let callback = &mut *(user_data as *mut F);
        callback(UpgradeProgressEvent::from_c(&*event));
    }));
}

fn run_upgrade_with<F: FnMut(UpgradeProgressEvent)>(
    port_path: &str,
    image_path: &Path,
    verify_mode: VerifyMode,
    reboot: bool,
    prepare_only: bool,
    mut progress: F,
    cancellation: Option<&Cancellation>,
) -> Result<UpgradeOutcome, UpgradeError> {
    let port = cstring(port_path).map_err(|message| UpgradeError {
        error_code: ffi::zzrk_status_ZZRK_ERR_INVALID_ARGUMENT as i32,
        error_stage: -1,
        error_target: String::new(),
        message,
    })?;
    let image = cstring_path(image_path).map_err(|message| UpgradeError {
        error_code: ffi::zzrk_status_ZZRK_ERR_INVALID_ARGUMENT as i32,
        error_stage: -1,
        error_target: String::new(),
        message,
    })?;

    let mut outcome: ffi::zzrk_upgrade_outcome = unsafe { std::mem::zeroed() };
    let cancel_ptr = cancellation.map_or(std::ptr::null(), Cancellation::as_ptr);
    let status = unsafe {
        ffi::zzrk_run_firmware_upgrade(
            port.as_ptr(),
            image.as_ptr(),
            verify_mode as i32,
            reboot,
            prepare_only,
            Some(upgrade_progress_trampoline::<F>),
            &mut progress as *mut F as *mut c_void,
            cancel_ptr,
            &mut outcome,
        )
    };

    if status != ffi::zzrk_status_ZZRK_OK {
        return Err(UpgradeError {
            error_code: outcome.error_code,
            error_stage: outcome.error_stage,
            error_target: unsafe { cstr_array_to_string(outcome.error_target.as_ptr()) },
            message: last_error(),
        });
    }
    Ok(UpgradeOutcome {
        model: unsafe { cstr_array_to_string(outcome.model.as_ptr()) },
        firmware_md5: unsafe { cstr_array_to_string(outcome.firmware_md5.as_ptr()) },
        device_sectors: outcome.device_sectors,
        partition_bytes_written: outcome.partition_bytes_written,
        sectors_erased: outcome.sectors_erased,
        partitions_verified: outcome.partitions_verified,
        prepared_only: outcome.prepared_only,
        partition_table_written: outcome.partition_table_written,
        partition_table_verified: outcome.partition_table_verified,
        loader_written: outcome.loader_written,
        loader_verified: outcome.loader_verified,
        reset_accepted: outcome.reset_accepted,
    })
}

/// Runs a complete update.img upgrade. Blocking; drive progress through the
/// callback (invoked on the operation thread) and cancel through the token
/// from another thread.
///
/// WARNING: with `prepare_only == false` this writes to the device. Even with
/// `prepare_only == true` it still scans, may reset the device into maskrom,
/// downloads the RAM loader and reads flash metadata — it only skips the
/// flash/erase phases.
pub fn run_firmware_upgrade(
    port_path: &str,
    image_path: impl AsRef<Path>,
    verify_mode: VerifyMode,
    reboot: bool,
    prepare_only: bool,
    progress: impl FnMut(UpgradeProgressEvent),
    cancellation: Option<&Cancellation>,
) -> Result<UpgradeOutcome, UpgradeError> {
    run_upgrade_with(
        port_path,
        image_path.as_ref(),
        verify_mode,
        reboot,
        prepare_only,
        progress,
        cancellation,
    )
}

// ---------------------------------------------------------------------------
// Device inspection and partitions
// ---------------------------------------------------------------------------

/// Result of `zzrk_inspect_device`.
#[derive(Debug, Clone)]
pub struct DeviceInspection {
    pub device: DeviceInfo,
    pub chip_info: [u8; 16],
    pub capability: [u8; 8],
    pub flash_info: [u8; 11],
    pub flash_id: [u8; 5],
    pub flash_id_is_emmc: bool,
    pub legacy_flash_sector_count: u32,
    pub legacy_flash_block_size_code: u16,
    pub legacy_flash_page_size_code: u8,
    pub legacy_flash_ecc_bits: u8,
    pub legacy_flash_access_time: u8,
    pub legacy_flash_manufacturer_code: u8,
    pub legacy_flash_mask: u8,
}

/// Queries chip/flash metadata of the device at `port_path`. Read-only, but
/// requires the device to be in maskrom/loader mode. Blocking.
pub fn inspect_device(port_path: &str) -> Result<DeviceInspection, String> {
    let port = cstring(port_path)?;
    unsafe {
        let mut raw: ffi::zzrk_device_inspection = std::mem::zeroed();
        let status = ffi::zzrk_inspect_device(port.as_ptr(), &mut raw);
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        Ok(DeviceInspection {
            device: DeviceInfo::from_c(&raw.device),
            chip_info: raw.chip_info,
            capability: raw.capability,
            flash_info: raw.flash_info,
            flash_id: raw.flash_id,
            flash_id_is_emmc: raw.flash_id_is_emmc,
            legacy_flash_sector_count: raw.legacy_flash_sector_count,
            legacy_flash_block_size_code: raw.legacy_flash_block_size_code,
            legacy_flash_page_size_code: raw.legacy_flash_page_size_code,
            legacy_flash_ecc_bits: raw.legacy_flash_ecc_bits,
            legacy_flash_access_time: raw.legacy_flash_access_time,
            legacy_flash_manufacturer_code: raw.legacy_flash_manufacturer_code,
            legacy_flash_mask: raw.legacy_flash_mask,
        })
    }
}

/// One GPT partition entry.
#[derive(Debug, Clone)]
pub struct PartitionEntry {
    pub name: String,
    pub type_guid: [u8; 16],
    pub unique_guid: [u8; 16],
    pub first_lba: u64,
    pub sector_count: u64,
    pub attributes: u64,
}

/// Reads the device GPT. Returns the entries and the device sector count.
/// Read-only, but requires maskrom/loader mode. Blocking.
pub fn read_partitions(port_path: &str) -> Result<(Vec<PartitionEntry>, u64), String> {
    let port = cstring(port_path)?;
    unsafe {
        let mut count: u32 = 0;
        let mut sectors: u64 = 0;
        let status = ffi::zzrk_read_partitions(
            port.as_ptr(),
            std::ptr::null_mut(),
            0,
            &mut count,
            &mut sectors,
        );
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        let mut raw = vec![std::mem::zeroed::<ffi::zzrk_partition_entry>(); count as usize];
        if count > 0 {
            let status = ffi::zzrk_read_partitions(
                port.as_ptr(),
                raw.as_mut_ptr(),
                count,
                &mut count,
                &mut sectors,
            );
            if status != ffi::zzrk_status_ZZRK_OK {
                return Err(last_error());
            }
            raw.truncate(count as usize);
        }
        let entries = raw
            .iter()
            .map(|e| PartitionEntry {
                name: cstr_array_to_string(e.name.as_ptr()),
                type_guid: e.type_guid,
                unique_guid: e.unique_guid,
                first_lba: e.first_lba,
                sector_count: e.sector_count,
                attributes: e.attributes,
            })
            .collect();
        Ok((entries, sectors))
    }
}

// ---------------------------------------------------------------------------
// Partition read / write / erase
// ---------------------------------------------------------------------------

/// Mirrors `zzrk_device_error_code`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceErrorCode {
    None = -1,
    Cancelled = 0,
    ScanFailed = 1,
    DeviceNotFound = 2,
    DeviceAmbiguous = 3,
    DeviceUnsupported = 4,
    OpenFailed = 5,
    ChipInfoFailed = 6,
    CapabilityFailed = 7,
    FlashInfoFailed = 8,
    FlashIdFailed = 9,
    StorageNotEmmc = 10,
    GptFailed = 11,
    PartitionNotFound = 12,
    PartitionAmbiguous = 13,
    InputInvalid = 14,
    InputTooLarge = 15,
    InputOpenFailed = 16,
    InputReadFailed = 17,
    OutputExists = 18,
    OutputOpenFailed = 19,
    OutputWriteFailed = 20,
    OutputFinalizeFailed = 21,
    ReadFailed = 22,
    WriteFailed = 23,
    VerifyFailed = 24,
    EraseFailed = 25,
    CloseFailed = 26,
}

impl_from_raw!(
    DeviceErrorCode,
    None,
    Cancelled,
    ScanFailed,
    DeviceNotFound,
    DeviceAmbiguous,
    DeviceUnsupported,
    OpenFailed,
    ChipInfoFailed,
    CapabilityFailed,
    FlashInfoFailed,
    FlashIdFailed,
    StorageNotEmmc,
    GptFailed,
    PartitionNotFound,
    PartitionAmbiguous,
    InputInvalid,
    InputTooLarge,
    InputOpenFailed,
    InputReadFailed,
    OutputExists,
    OutputOpenFailed,
    OutputWriteFailed,
    OutputFinalizeFailed,
    ReadFailed,
    WriteFailed,
    VerifyFailed,
    EraseFailed,
    CloseFailed
);

/// Mirrors `zzrk_partition_phase`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PartitionPhase {
    Read = 0,
    Write = 1,
    Verify = 2,
    Erase = 3,
}

impl_from_raw!(PartitionPhase, Read, Write, Verify, Erase);

/// Progress event of a partition operation.
#[derive(Debug, Clone)]
pub struct PartitionProgressEvent {
    phase: i32,
    pub partition: String,
    pub completed_bytes: u64,
    pub total_bytes: u64,
}

impl PartitionProgressEvent {
    unsafe fn from_c(e: &ffi::zzrk_partition_progress_event) -> Self {
        Self {
            phase: e.phase,
            partition: if e.partition.is_null() {
                String::new()
            } else {
                unsafe { cstr_to_string(e.partition) }
            },
            completed_bytes: e.completed_bytes,
            total_bytes: e.total_bytes,
        }
    }

    pub fn phase(&self) -> Option<PartitionPhase> {
        PartitionPhase::from_raw(self.phase)
    }
}

/// Success outcome of a partition operation.
#[derive(Debug, Clone)]
pub struct PartitionOutcome {
    phase: i32,
    pub bytes_processed: u64,
    pub sectors_processed: u64,
    pub verified: bool,
}

impl PartitionOutcome {
    pub fn phase(&self) -> Option<PartitionPhase> {
        PartitionPhase::from_raw(self.phase)
    }
}

/// Structured failure of a partition operation or download.
#[derive(Debug, Clone)]
pub struct DeviceOperationError {
    /// Raw `zzrk_device_error_code` value.
    pub error_code: i32,
    pub error_target: String,
    pub message: String,
}

impl DeviceOperationError {
    pub fn code(&self) -> Option<DeviceErrorCode> {
        DeviceErrorCode::from_raw(self.error_code)
    }
}

impl std::fmt::Display for DeviceOperationError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for DeviceOperationError {}

unsafe extern "C" fn partition_progress_trampoline<F: FnMut(PartitionProgressEvent)>(
    event: *const ffi::zzrk_partition_progress_event,
    user_data: *mut c_void,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if user_data.is_null() || event.is_null() {
            return;
        }
        let callback = &mut *(user_data as *mut F);
        callback(PartitionProgressEvent::from_c(&*event));
    }));
}

fn run_partition_op<F: FnMut(PartitionProgressEvent)>(
    operation: &str,
    mut progress: F,
    cancellation: Option<&Cancellation>,
    call: impl FnOnce(
        ffi::zzrk_partition_progress_fn,
        *mut c_void,
        *const ffi::zzrk_cancellation,
        *mut ffi::zzrk_partition_outcome,
    ) -> ffi::zzrk_status,
) -> Result<PartitionOutcome, DeviceOperationError> {
    let mut outcome: ffi::zzrk_partition_outcome = unsafe { std::mem::zeroed() };
    let cancel_ptr = cancellation.map_or(std::ptr::null(), Cancellation::as_ptr);
    let status = call(
        Some(partition_progress_trampoline::<F>),
        &mut progress as *mut F as *mut c_void,
        cancel_ptr,
        &mut outcome,
    );
    if status != ffi::zzrk_status_ZZRK_OK {
        let _ = operation;
        return Err(DeviceOperationError {
            error_code: outcome.error_code,
            error_target: unsafe { cstr_array_to_string(outcome.error_target.as_ptr()) },
            message: last_error(),
        });
    }
    Ok(PartitionOutcome {
        phase: outcome.phase,
        bytes_processed: outcome.bytes_processed,
        sectors_processed: outcome.sectors_processed,
        verified: outcome.verified,
    })
}

/// Reads `partition` from the device into `output_path`. Blocking.
/// Refuses to overwrite an existing output file (C side reports
/// `OutputExists`).
pub fn read_partition(
    port_path: &str,
    partition: &str,
    output_path: impl AsRef<Path>,
    progress: impl FnMut(PartitionProgressEvent),
    cancellation: Option<&Cancellation>,
) -> Result<PartitionOutcome, DeviceOperationError> {
    let port = cstring(port_path).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    let name = cstring(partition).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    let output = cstring_path(&output_path).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    run_partition_op("partition read", progress, cancellation, |cb, ud, cancel, out| unsafe {
        ffi::zzrk_read_partition(port.as_ptr(), name.as_ptr(), output.as_ptr(), cb, ud, cancel, out)
    })
}

/// Writes `input_path` to `partition` on the device. Blocking.
/// WARNING: this writes to the device.
pub fn write_partition(
    port_path: &str,
    partition: &str,
    input_path: impl AsRef<Path>,
    verify: bool,
    progress: impl FnMut(PartitionProgressEvent),
    cancellation: Option<&Cancellation>,
) -> Result<PartitionOutcome, DeviceOperationError> {
    let port = cstring(port_path).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    let name = cstring(partition).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    let input = cstring_path(&input_path).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    run_partition_op("partition write", progress, cancellation, |cb, ud, cancel, out| unsafe {
        ffi::zzrk_write_partition(
            port.as_ptr(),
            name.as_ptr(),
            input.as_ptr(),
            verify,
            cb,
            ud,
            cancel,
            out,
        )
    })
}

/// Erases `partition` on the device. Blocking.
/// WARNING: this erases device data.
pub fn erase_partition(
    port_path: &str,
    partition: &str,
    progress: impl FnMut(PartitionProgressEvent),
    cancellation: Option<&Cancellation>,
) -> Result<PartitionOutcome, DeviceOperationError> {
    let port = cstring(port_path).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    let name = cstring(partition).map_err(|message| DeviceOperationError {
        error_code: DeviceErrorCode::None as i32,
        error_target: String::new(),
        message,
    })?;
    run_partition_op("partition erase", progress, cancellation, |cb, ud, cancel, out| unsafe {
        ffi::zzrk_erase_partition(port.as_ptr(), name.as_ptr(), cb, ud, cancel, out)
    })
}

// ---------------------------------------------------------------------------
// Image download (loose images + parameter layout)
// ---------------------------------------------------------------------------

/// Mirrors `zzrk_download_layout_source`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DownloadLayoutSource {
    DeviceGpt = 0,
    ParameterFile = 1,
}

impl_from_raw!(DownloadLayoutSource, DeviceGpt, ParameterFile);

/// Layout source for an image download.
#[derive(Debug, Clone, Copy)]
pub enum DownloadLayout<'a> {
    /// Read the GPT from the device itself.
    DeviceGpt,
    /// Parse a Rockchip parameter file.
    ParameterFile(&'a Path),
}

/// One loose-image partition to download.
#[derive(Debug, Clone, Copy)]
pub struct DownloadPartition<'a> {
    pub target: &'a str,
    pub image_path: &'a Path,
    pub verify: bool,
}

/// Mirrors `zzrk_download_stage`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DownloadStage {
    InspectFiles = 0,
    VerifyFiles = 1,
    SelectDevice = 2,
    EnterMaskrom = 3,
    WaitMaskrom = 4,
    DownloadLoader = 5,
    WaitRamLoader = 6,
    CheckRamLoader = 7,
    CheckStorage = 8,
    ValidateLayout = 9,
    Erase = 10,
    Write = 11,
    Verify = 12,
    Reset = 13,
}

impl_from_raw!(
    DownloadStage,
    InspectFiles,
    VerifyFiles,
    SelectDevice,
    EnterMaskrom,
    WaitMaskrom,
    DownloadLoader,
    WaitRamLoader,
    CheckRamLoader,
    CheckStorage,
    ValidateLayout,
    Erase,
    Write,
    Verify,
    Reset
);

/// Mirrors `zzrk_download_error_code`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DownloadErrorCode {
    None = -1,
    Cancelled = 0,
    InvalidRequest = 1,
    ImageInvalid = 2,
    DigestFailed = 3,
    DigestChanged = 4,
    LayoutInvalid = 5,
    ScanFailed = 6,
    DeviceNotFound = 7,
    DeviceAmbiguous = 8,
    DeviceUnsupported = 9,
    LoaderRequired = 10,
    OpenFailed = 11,
    LoaderResetFailed = 12,
    ReenumerationTimedOut = 13,
    LoaderDownloadFailed = 14,
    LoaderNotReady = 15,
    WrongChip = 16,
    StorageCheckFailed = 17,
    StorageNotEmmc = 18,
    DeviceTooSmall = 19,
    DeviceLayoutChanged = 20,
    FlashFailed = 21,
    ResetFailed = 22,
    CloseFailed = 23,
}

impl_from_raw!(
    DownloadErrorCode,
    None,
    Cancelled,
    InvalidRequest,
    ImageInvalid,
    DigestFailed,
    DigestChanged,
    LayoutInvalid,
    ScanFailed,
    DeviceNotFound,
    DeviceAmbiguous,
    DeviceUnsupported,
    LoaderRequired,
    OpenFailed,
    LoaderResetFailed,
    ReenumerationTimedOut,
    LoaderDownloadFailed,
    LoaderNotReady,
    WrongChip,
    StorageCheckFailed,
    StorageNotEmmc,
    DeviceTooSmall,
    DeviceLayoutChanged,
    FlashFailed,
    ResetFailed,
    CloseFailed
);

/// Progress event of an image download.
#[derive(Debug, Clone)]
pub struct DownloadProgressEvent {
    stage: i32,
    pub target: Option<String>,
    pub completed_bytes: u64,
    pub total_bytes: u64,
    pub completed_plan_bytes: u64,
    pub total_plan_bytes: u64,
    /// -1 when none.
    pub item_index: i32,
}

impl DownloadProgressEvent {
    unsafe fn from_c(e: &ffi::zzrk_download_progress_event) -> Self {
        Self {
            stage: e.stage,
            target: if e.target.is_null() {
                None
            } else {
                Some(unsafe { cstr_to_string(e.target) })
            },
            completed_bytes: e.completed_bytes,
            total_bytes: e.total_bytes,
            completed_plan_bytes: e.completed_plan_bytes,
            total_plan_bytes: e.total_plan_bytes,
            item_index: e.item_index,
        }
    }

    pub fn stage(&self) -> Option<DownloadStage> {
        DownloadStage::from_raw(self.stage)
    }
}

/// Success outcome of an image download.
#[derive(Debug, Clone)]
pub struct DownloadOutcome {
    pub device_sectors: u64,
    pub partition_bytes_written: u64,
    pub sectors_erased: u64,
    pub items_written: u32,
    pub items_verified: u32,
    pub loader_written: bool,
    pub partition_table_written: bool,
    pub reset_accepted: bool,
}

/// Structured failure of an image download.
#[derive(Debug, Clone)]
pub struct DownloadError {
    /// Raw `zzrk_download_error_code` value.
    pub error_code: i32,
    /// Raw `zzrk_download_stage` value at the point of failure.
    pub error_stage: i32,
    pub error_target: String,
    /// -1 when none.
    pub error_item_index: i32,
    pub message: String,
}

impl DownloadError {
    pub fn code(&self) -> Option<DownloadErrorCode> {
        DownloadErrorCode::from_raw(self.error_code)
    }

    pub fn stage(&self) -> Option<DownloadStage> {
        DownloadStage::from_raw(self.error_stage)
    }
}

impl std::fmt::Display for DownloadError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for DownloadError {}

unsafe extern "C" fn download_progress_trampoline<F: FnMut(DownloadProgressEvent)>(
    event: *const ffi::zzrk_download_progress_event,
    user_data: *mut c_void,
) {
    let _ = catch_unwind(AssertUnwindSafe(|| unsafe {
        if user_data.is_null() || event.is_null() {
            return;
        }
        let callback = &mut *(user_data as *mut F);
        callback(DownloadProgressEvent::from_c(&*event));
    }));
}

fn run_download_with<F: FnMut(DownloadProgressEvent)>(
    port_path: &str,
    layout: DownloadLayout,
    loader_path: Option<&Path>,
    verify_loader: bool,
    verify_partition_table: bool,
    partitions: &[DownloadPartition],
    reboot: bool,
    mut progress: F,
    cancellation: Option<&Cancellation>,
) -> Result<DownloadOutcome, DownloadError> {
    let invalid = |message: String| DownloadError {
        error_code: DownloadErrorCode::InvalidRequest as i32,
        error_stage: -1,
        error_target: String::new(),
        error_item_index: -1,
        message,
    };

    let port = cstring(port_path).map_err(&invalid)?;
    let (layout_source, parameter) = match layout {
        DownloadLayout::DeviceGpt => (DownloadLayoutSource::DeviceGpt, None),
        DownloadLayout::ParameterFile(path) => (
            DownloadLayoutSource::ParameterFile,
            Some(cstring_path(path).map_err(&invalid)?),
        ),
    };
    let loader = loader_path.map(cstring_path).transpose().map_err(&invalid)?;

    // Keep the CStrings alive for the duration of the FFI call; the C side
    // copies the contents synchronously during prepare.
    let mut strings = Vec::with_capacity(partitions.len() * 2);
    let mut raw_partitions = Vec::with_capacity(partitions.len());
    for p in partitions {
        let target = cstring(p.target).map_err(&invalid)?;
        let image = cstring_path(p.image_path).map_err(&invalid)?;
        strings.push(target);
        strings.push(image);
    }
    for (index, p) in partitions.iter().enumerate() {
        raw_partitions.push(ffi::zzrk_download_partition {
            target: strings[index * 2].as_ptr(),
            image_path: strings[index * 2 + 1].as_ptr(),
            verify: p.verify,
        });
    }

    let mut outcome: ffi::zzrk_download_outcome = unsafe { std::mem::zeroed() };
    let cancel_ptr = cancellation.map_or(std::ptr::null(), Cancellation::as_ptr);
    let status = unsafe {
        ffi::zzrk_run_image_download(
            port.as_ptr(),
            layout_source as i32,
            parameter.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
            loader.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
            verify_loader,
            verify_partition_table,
            raw_partitions.as_ptr(),
            raw_partitions.len() as u32,
            reboot,
            Some(download_progress_trampoline::<F>),
            &mut progress as *mut F as *mut c_void,
            cancel_ptr,
            &mut outcome,
        )
    };

    if status != ffi::zzrk_status_ZZRK_OK {
        return Err(DownloadError {
            error_code: outcome.error_code,
            error_stage: outcome.error_stage,
            error_target: unsafe { cstr_array_to_string(outcome.error_target.as_ptr()) },
            error_item_index: outcome.error_item_index,
            message: last_error(),
        });
    }
    Ok(DownloadOutcome {
        device_sectors: outcome.device_sectors,
        partition_bytes_written: outcome.partition_bytes_written,
        sectors_erased: outcome.sectors_erased,
        items_written: outcome.items_written,
        items_verified: outcome.items_verified,
        loader_written: outcome.loader_written,
        partition_table_written: outcome.partition_table_written,
        reset_accepted: outcome.reset_accepted,
    })
}

/// Runs a loose-image download. Blocking.
/// WARNING: this writes to the device (erase/write phases).
pub fn run_image_download(
    port_path: &str,
    layout: DownloadLayout,
    loader_path: Option<&Path>,
    verify_loader: bool,
    verify_partition_table: bool,
    partitions: &[DownloadPartition],
    reboot: bool,
    progress: impl FnMut(DownloadProgressEvent),
    cancellation: Option<&Cancellation>,
) -> Result<DownloadOutcome, DownloadError> {
    run_download_with(
        port_path,
        layout,
        loader_path,
        verify_loader,
        verify_partition_table,
        partitions,
        reboot,
        progress,
        cancellation,
    )
}

/// Mirrors `zzrk_download_item_kind`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DownloadItemKind {
    Loader = 0,
    PartitionTable = 1,
    Partition = 2,
}

impl_from_raw!(DownloadItemKind, Loader, PartitionTable, Partition);

/// Mirrors `zzrk_payload_format`.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PayloadFormat {
    Unknown = 0,
    Raw = 1,
    AndroidSparse = 2,
    RockchipParameter = 3,
}

impl_from_raw!(PayloadFormat, Unknown, Raw, AndroidSparse, RockchipParameter);

impl PayloadFormat {
    pub fn name(self) -> &'static str {
        match self {
            PayloadFormat::Unknown => "-",
            PayloadFormat::Raw => "Raw",
            PayloadFormat::AndroidSparse => "Android Sparse",
            PayloadFormat::RockchipParameter => "parameter.txt",
        }
    }
}

/// One prepared image-download plan item (see `zzrk_download_plan_item`).
#[derive(Debug, Clone)]
pub struct DownloadPlanItem {
    kind: i32,
    format: i32,
    pub target: String,
    pub source_path: String,
    pub first_lba: u64,
    pub sector_count: u64,
    pub output_size: u64,
    pub verify: bool,
    pub md5: String,
}

impl DownloadPlanItem {
    pub fn kind(&self) -> Option<DownloadItemKind> {
        DownloadItemKind::from_raw(self.kind)
    }

    pub fn format(&self) -> Option<PayloadFormat> {
        PayloadFormat::from_raw(self.format)
    }
}

/// Result of [`prepare_image_download`]: the prepared items plus every
/// partition of the layout (parameter file or device GPT).
#[derive(Debug, Clone)]
pub struct PreparedDownload {
    pub items: Vec<DownloadPlanItem>,
    pub layout: Vec<PartitionEntry>,
}

/// Prepares an image-download plan (parse + inspect + digests) without
/// executing it. With `DownloadLayout::ParameterFile` this is file-only and
/// never touches a device; with `DownloadLayout::DeviceGpt` the device GPT
/// is read first (read-only; `port_path` required). Blocking.
pub fn prepare_image_download(
    port_path: Option<&str>,
    layout: DownloadLayout,
    loader_path: Option<&Path>,
    verify_loader: bool,
    verify_partition_table: bool,
    partitions: &[DownloadPartition],
) -> Result<PreparedDownload, String> {
    let port = port_path.map(cstring).transpose()?;
    let (layout_source, parameter) = match layout {
        DownloadLayout::DeviceGpt => (DownloadLayoutSource::DeviceGpt, None),
        DownloadLayout::ParameterFile(path) => (
            DownloadLayoutSource::ParameterFile,
            Some(cstring_path(path)?),
        ),
    };
    let loader = loader_path.map(cstring_path).transpose()?;

    let mut strings = Vec::with_capacity(partitions.len() * 2);
    let mut raw_partitions = Vec::with_capacity(partitions.len());
    for p in partitions {
        strings.push(cstring(p.target)?);
        strings.push(cstring_path(p.image_path)?);
    }
    for (index, p) in partitions.iter().enumerate() {
        raw_partitions.push(ffi::zzrk_download_partition {
            target: strings[index * 2].as_ptr(),
            image_path: strings[index * 2 + 1].as_ptr(),
            verify: p.verify,
        });
    }

    unsafe {
        #[allow(clippy::too_many_arguments)]
        let call = |items: *mut ffi::zzrk_download_plan_item,
                    capacity: u32,
                    layout_buf: *mut ffi::zzrk_partition_entry,
                    layout_capacity: u32,
                    count: &mut u32,
                    layout_count: &mut u32| {
            ffi::zzrk_prepare_image_download(
                port.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
                layout_source as i32,
                parameter.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
                loader.as_ref().map_or(std::ptr::null(), |p| p.as_ptr()),
                verify_loader,
                verify_partition_table,
                raw_partitions.as_ptr(),
                raw_partitions.len() as u32,
                items,
                capacity,
                count,
                layout_buf,
                layout_capacity,
                layout_count,
            )
        };

        let mut count: u32 = 0;
        let mut layout_count: u32 = 0;
        let status = call(
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            0,
            &mut count,
            &mut layout_count,
        );
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        let mut raw =
            vec![std::mem::zeroed::<ffi::zzrk_download_plan_item>(); count as usize];
        let mut raw_layout =
            vec![std::mem::zeroed::<ffi::zzrk_partition_entry>(); layout_count as usize];
        if count > 0 || layout_count > 0 {
            let status = call(
                raw.as_mut_ptr(),
                count,
                raw_layout.as_mut_ptr(),
                layout_count,
                &mut count,
                &mut layout_count,
            );
            if status != ffi::zzrk_status_ZZRK_OK {
                return Err(last_error());
            }
            raw.truncate(count as usize);
            raw_layout.truncate(layout_count as usize);
        }
        Ok(PreparedDownload {
            items: raw
                .iter()
                .map(|item| DownloadPlanItem {
                    kind: item.kind,
                    format: item.format,
                    target: cstr_array_to_string(item.target.as_ptr()),
                    source_path: cstr_array_to_string(item.source_path.as_ptr()),
                    first_lba: item.first_lba,
                    sector_count: item.sector_count,
                    output_size: item.output_size,
                    verify: item.verify,
                    md5: cstr_array_to_string(item.md5.as_ptr()),
                })
                .collect(),
            layout: raw_layout
                .iter()
                .map(|e| PartitionEntry {
                    name: cstr_array_to_string(e.name.as_ptr()),
                    type_guid: e.type_guid,
                    unique_guid: e.unique_guid,
                    first_lba: e.first_lba,
                    sector_count: e.sector_count,
                    attributes: e.attributes,
                })
                .collect(),
        })
    }
}

// ---------------------------------------------------------------------------
// Tests (no device write paths; a real device may be attached, scan only)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_is_not_empty() {
        assert!(!version().is_empty());
    }

    #[test]
    fn scan_devices_succeeds() {
        // A device may or may not be attached; the call itself must succeed.
        let devices = scan_devices().expect("scan_devices failed");
        for d in &devices {
            assert!(!d.port_path.is_empty());
        }
    }

    #[test]
    fn inspect_missing_firmware_errors_with_message() {
        let err = inspect_firmware("definitely/not/a/firmware.img")
            .expect_err("expected an error for a missing file");
        assert!(!err.is_empty());
    }

    #[test]
    fn cancellation_lifecycle_clone_and_cancel() {
        let token = Cancellation::new();
        let clone = token.clone();
        token.cancel();
        clone.cancel(); // idempotent, shared flag
        drop(token);
        clone.cancel(); // still valid: Arc keeps the token alive
        drop(clone); // frees the C token
    }

    #[test]
    fn cancellation_is_send_and_sync() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<Cancellation>();
    }

    #[test]
    fn upgrade_with_missing_image_returns_structured_error() {
        // Fails during parse, before any device access.
        let err = run_firmware_upgrade(
            "1-6",
            "definitely/not/a/firmware.img",
            VerifyMode::None,
            false,
            true,
            |_| {},
            None,
        )
        .expect_err("expected an error for a missing image");
        assert_eq!(err.code(), Some(UpgradeErrorCode::ImageInvalid));
        assert_eq!(err.stage(), Some(UpgradeStage::ParseImage));
        assert!(!err.message.is_empty());
    }

    #[test]
    fn upgrade_progress_closure_panic_does_not_cross_ffi() {
        // The panicking closure is swallowed by the trampoline; the call
        // still completes normally with its regular error.
        let err = run_firmware_upgrade(
            "1-6",
            "definitely/not/a/firmware.img",
            VerifyMode::None,
            false,
            true,
            |_| panic!("boom in progress callback"),
            None,
        )
        .expect_err("expected an error for a missing image");
        assert_eq!(err.code(), Some(UpgradeErrorCode::ImageInvalid));
    }

    #[test]
    fn interior_nul_arguments_are_rejected() {
        let err = run_firmware_upgrade(
            "1-\u{0}6",
            "x.img",
            VerifyMode::None,
            false,
            true,
            |_| {},
            None,
        )
        .expect_err("expected an error for an interior NUL");
        assert!(!err.message.is_empty());

        assert!(inspect_device("1-\u{0}6").is_err());
        assert!(read_partitions("1-\u{0}6").is_err());
        assert!(inspect_firmware("x\u{0}.img").is_err());
    }

    #[test]
    fn inspect_device_with_unknown_port_errors() {
        // Scan-only failure path: no device matches the port.
        let err = inspect_device("no-such-port-0")
            .expect_err("expected an error for an unknown port");
        assert!(!err.is_empty());
    }

    #[test]
    fn read_partitions_with_unknown_port_errors() {
        let err = read_partitions("no-such-port-0")
            .expect_err("expected an error for an unknown port");
        assert!(!err.is_empty());
    }

    #[test]
    fn image_download_with_missing_parameter_file_errors() {
        // Fails during prepare (parameter file parse), before device access.
        let err = run_image_download(
            "no-such-port-0",
            DownloadLayout::ParameterFile(Path::new("definitely/not/a/parameter.txt")),
            None,
            false,
            false,
            &[],
            false,
            |_| {},
            None,
        )
        .expect_err("expected an error for a missing parameter file");
        assert!(!err.message.is_empty());
    }

    #[test]
    fn prepare_image_download_missing_parameter_file_errors() {
        let err = prepare_image_download(
            None,
            DownloadLayout::ParameterFile(Path::new("definitely/not/a/parameter.txt")),
            None,
            true,
            true,
            &[],
        )
        .expect_err("expected an error for a missing parameter file");
        assert!(!err.is_empty());
    }

    #[test]
    fn prepare_image_download_from_parameter_file() {
        let path = std::env::temp_dir().join("zzrk-rs-prepare-test-parameter.txt");
        std::fs::write(
            &path,
            "MACHINE_MODEL: rs-prepare\nTYPE: GPT\n\
             CMDLINE:mtdparts=rk29xxnand:0x20@0x40(boot_a),0x20@0x60(boot_b),-@0x80(userdata:grow)\n",
        )
        .unwrap();

        let prepared = prepare_image_download(
            None,
            DownloadLayout::ParameterFile(&path),
            None,
            true,
            true,
            &[],
        )
        .expect("prepare from parameter file should succeed (file-only)");
        assert_eq!(prepared.items.len(), 1);
        assert_eq!(prepared.items[0].kind(), Some(DownloadItemKind::PartitionTable));
        assert_eq!(prepared.items[0].target, "GPT");
        assert_eq!(prepared.items[0].sector_count, 67);
        assert_eq!(
            prepared.items[0].format(),
            Some(PayloadFormat::RockchipParameter)
        );
        assert_eq!(prepared.items[0].md5.len(), 32);
        assert_eq!(prepared.layout.len(), 3);
        assert_eq!(prepared.layout[0].name, "boot_a");
        assert_eq!(prepared.layout[0].first_lba, 0x40);
        assert_eq!(prepared.layout[0].sector_count, 0x20);
        assert_eq!(prepared.layout[2].name, "userdata");

        std::fs::remove_file(&path).ok();
    }

    #[test]
    fn prepare_image_download_rejects_interior_nul() {
        assert!(
            prepare_image_download(
                Some("1-\u{0}6"),
                DownloadLayout::DeviceGpt,
                None,
                true,
                true,
                &[],
            )
            .is_err()
        );
    }
}
