//! Safe-ish Rust wrapper over the ZzRkDevTool C ABI (`zzrk_ffi`).
//!
//! Raw bindgen output is kept private; consumers use the typed API below.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

mod ffi {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

use std::ffi::{CStr, CString};
use std::path::Path;

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

/// Library version string.
pub fn version() -> String {
    unsafe { cstr_to_string(ffi::zzrk_version_string()) }
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
        Ok(raw
            .iter()
            .map(|d| DeviceInfo {
                vendor_id: d.vendor_id,
                product_id: d.product_id,
                bcd_usb: d.bcd_usb,
                bcd_device: d.bcd_device,
                bus_number: d.bus_number,
                device_address: d.device_address,
                mode: DeviceMode::from(d.mode),
                port_path: cstr_to_string(d.port_path.as_ptr()),
            })
            .collect())
    }
}

/// Parses an RK update image and verifies its trailing MD5. Blocking; large
/// images take seconds for the digest pass — call from a worker thread.
pub fn inspect_firmware(path: impl AsRef<Path>) -> Result<FirmwareInfo, String> {
    let path = CString::new(path.as_ref().as_os_str().as_encoded_bytes())
        .map_err(|_| "path contains an interior NUL byte".to_string())?;
    unsafe {
        let mut info: ffi::zzrk_firmware_info = std::mem::zeroed();
        let status = ffi::zzrk_inspect_firmware(path.as_ptr(), &mut info);
        if status != ffi::zzrk_status_ZZRK_OK {
            return Err(last_error());
        }
        Ok(FirmwareInfo {
            chip: cstr_to_string(info.chip.as_ptr()),
            model: cstr_to_string(info.model.as_ptr()),
            manufacturer: cstr_to_string(info.manufacturer.as_ptr()),
            md5: cstr_to_string(info.md5.as_ptr()),
            md5_verified: info.md5_verified,
            file_size: info.file_size,
            partition_count: info.partition_count,
        })
    }
}

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
}
