// SPDX-License-Identifier: MIT
//! 页面间共享的枚举、数据结构与格式化辅助。

use zzrk_sys::DeviceMode;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum TabKind {
    Download,
    Device,
    Upgrade,
    Partition,
}

impl TabKind {
    pub(crate) const ALL: [TabKind; 4] = [
        TabKind::Download,
        TabKind::Device,
        TabKind::Upgrade,
        TabKind::Partition,
    ];

    pub(crate) fn label(self) -> &'static str {
        match self {
            TabKind::Download => "下载镜像",
            TabKind::Device => "设备",
            TabKind::Upgrade => "完整升级",
            TabKind::Partition => "分区",
        }
    }

    pub(crate) fn index(self) -> usize {
        match self {
            TabKind::Download => 0,
            TabKind::Device => 1,
            TabKind::Upgrade => 2,
            TabKind::Partition => 3,
        }
    }

    pub(crate) fn from_index(ix: usize) -> Self {
        Self::ALL[ix.min(3)]
    }
}

pub(crate) fn mode_label(mode: DeviceMode) -> &'static str {
    match mode {
        DeviceMode::Unknown => "unknown",
        DeviceMode::Adb => "ADB",
        DeviceMode::Maskrom => "Maskrom",
        DeviceMode::Loader => "Loader",
        DeviceMode::Other(_) => "other",
    }
}

/// 大写十六进制、空格分隔（对齐 Qt 版 hexBytes）。
pub(crate) fn hex_bytes(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(" ")
}

pub(crate) fn now_hms() -> String {
    chrono::Local::now().format("%H:%M:%S").to_string()
}

pub(crate) fn format_bcd(bcd: u16) -> String {
    format!("{}.{:02}", (bcd >> 8) & 0xFF, bcd & 0xFF)
}

pub(crate) fn data_size(bytes: u64) -> String {
    if bytes >= 1_000_000_000 {
        format!("{:.2} GB", bytes as f64 / 1e9)
    } else if bytes >= 1_000_000 {
        format!("{:.2} MB", bytes as f64 / 1e6)
    } else if bytes >= 1_000 {
        format!("{:.2} KB", bytes as f64 / 1e3)
    } else {
        format!("{bytes} B")
    }
}

/// Qt 版 slotBaseTarget：去掉 `_a`/`_b` 槽位后缀。
pub(crate) fn slot_base_target(target: &str) -> &str {
    if target.len() > 2 && (target.ends_with("_a") || target.ends_with("_b")) {
        &target[..target.len() - 2]
    } else {
        target
    }
}

/// 升级任务历史行。
#[derive(Debug, Clone)]
pub(crate) struct HistoryEntry {
    pub(crate) start_time: String,
    pub(crate) port: String,
    pub(crate) firmware: String,
    pub(crate) result: String,
}
