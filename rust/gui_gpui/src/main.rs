//! ZzRkDevTool GPUI 窗口（gpui-kit 组件栈），样式对齐 Qt 版骨架规格。
//!
//! SAFETY: 本机插着真实 RK 设备。"开始升级"逻辑已接线（prepare_only 默认
//! 开），但端到端验证不得在真实设备上触发——即使 prepare_only 也会复位
//! 设备进 maskrom 并下载 RAM loader。

use futures::StreamExt;
use gpui_kit::component::button::{Button, ButtonVariants as _};
use gpui_kit::component::checkbox::Checkbox;
use gpui_kit::component::group_box::GroupBox;
use gpui_kit::component::input::{Input, InputState};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::progress::Progress;
use gpui_kit::component::select::{Select, SelectState};
use gpui_kit::component::status_bar::StatusBar;
use gpui_kit::component::tab::{Tab, TabBar};
use gpui_kit::component::table::{Column, ColumnSort, DataTable, TableDelegate, TableState};
use gpui_kit::component::{
    ActiveTheme, Disableable as _, Icon, IconName, Root, TitleBar, WindowExt as _, h_flex, v_flex,
};
use gpui_kit::prelude::FluentBuilder as _;
use gpui_kit::*;
use zzrk_sys::{Cancellation, DeviceInfo, DeviceMode, FirmwareInfo, UpgradeError, UpgradeOutcome};

const FIRMWARE_PATH: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../pd.core.CT1.rk3588s.20260727.161.img"
);

const ACCENT: u32 = 0x0067C0;
const CONTENT_MARGIN: f32 = 10.;

// ---------------------------------------------------------------------------
// Fluent 主题令牌（对齐 Qt 版规格）
// ---------------------------------------------------------------------------

fn apply_fluent_theme(dark: bool, window: Option<&mut Window>, cx: &mut App) {
    component::theme::Theme::change(
        if dark {
            component::theme::ThemeMode::Dark
        } else {
            component::theme::ThemeMode::Light
        },
        window,
        cx,
    );

    let (surface, surface2, text1, text2, fill, hover, pressed, stroke) = if dark {
        (0x202020, 0x2B2B2B, 0xFFFFFF, 0xC5C5C5, 0x323232, 0x3B3B3B, 0x454545, 0x5A5A5A)
    } else {
        (0xF9F9F9, 0xFFFFFF, 0x1A1A1A, 0x5D5D5D, 0xFFFFFF, 0xF5F5F5, 0xE8E8E8, 0xD1D1D1)
    };
    let (error, success, info, warning) = if dark {
        (0xFF99A4, 0x6CCB5F, 0x60CDFF, 0xFCE100)
    } else {
        (0xC42B1C, 0x0F7B0F, 0x005FB8, 0x9D5D00)
    };
    let accent_fg = 0xFFFFFF;

    {
        let theme = component::theme::Theme::global_mut(cx);
        let c = &mut theme.colors;
        let set = |slot: &mut Hsla, value: u32| *slot = rgb(value).into();

        set(&mut c.background, surface);
        set(&mut c.foreground, text1);
        set(&mut c.muted_foreground, text2);
        set(&mut c.border, stroke);
        set(&mut c.accent, ACCENT);
        set(&mut c.primary, ACCENT);
        set(&mut c.primary_active, ACCENT);
        set(&mut c.primary_foreground, accent_fg);
        set(&mut c.secondary, fill);
        set(&mut c.secondary_hover, hover);
        set(&mut c.secondary_active, pressed);
        set(&mut c.secondary_foreground, text1);
        set(&mut c.button, fill);
        set(&mut c.button_hover, hover);
        set(&mut c.button_active, pressed);
        set(&mut c.button_foreground, text1);
        set(&mut c.input, fill);
        set(&mut c.caret, text1);
        set(&mut c.ring, ACCENT);
        set(&mut c.selection, ACCENT);
        set(&mut c.list, surface2);
        set(&mut c.list_even, surface);
        set(&mut c.list_hover, hover);
        set(&mut c.list_active, ACCENT);
        set(&mut c.list_active_border, ACCENT);
        set(&mut c.list_head, surface2);
        set(&mut c.table, surface2);
        set(&mut c.table_even, if dark { 0x282828 } else { 0xF5F5F5 });
        set(&mut c.table_hover, hover);
        set(&mut c.table_active, ACCENT);
        set(&mut c.table_active_border, ACCENT);
        set(&mut c.table_head, surface2);
        set(&mut c.table_head_foreground, text1);
        set(&mut c.table_row_border, stroke);
        set(&mut c.group_box, surface2);
        set(&mut c.group_box_foreground, text1);
        set(&mut c.popover, surface2);
        set(&mut c.popover_foreground, text1);
        set(&mut c.title_bar, surface);
        set(&mut c.title_bar_border, stroke);
        set(&mut c.status_bar, surface);
        set(&mut c.status_bar_border, stroke);
        set(&mut c.tab, surface);
        set(&mut c.tab_active, surface);
        set(&mut c.tab_foreground, text2);
        set(&mut c.tab_active_foreground, text1);
        set(&mut c.tab_bar, surface);
        set(&mut c.progress_bar, ACCENT);
        set(&mut c.danger, error);
        set(&mut c.success, success);
        set(&mut c.info, info);
        set(&mut c.warning, warning);
        set(&mut c.scrollbar_thumb, stroke);
        theme.radius = px(4.);
        theme.tile_radius = px(4.);
    }
    component::theme::Theme::sync_base(cx);
}

// ---------------------------------------------------------------------------
// 通用数据
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TabKind {
    Download,
    Device,
    Upgrade,
    Partition,
}

impl TabKind {
    const ALL: [TabKind; 4] = [
        TabKind::Download,
        TabKind::Device,
        TabKind::Upgrade,
        TabKind::Partition,
    ];

    fn label(self) -> &'static str {
        match self {
            TabKind::Download => "下载镜像",
            TabKind::Device => "设备",
            TabKind::Upgrade => "完整升级",
            TabKind::Partition => "分区",
        }
    }

    fn index(self) -> usize {
        match self {
            TabKind::Download => 0,
            TabKind::Device => 1,
            TabKind::Upgrade => 2,
            TabKind::Partition => 3,
        }
    }

    fn from_index(ix: usize) -> Self {
        Self::ALL[ix.min(3)]
    }
}

fn mode_label(mode: DeviceMode) -> &'static str {
    match mode {
        DeviceMode::Unknown => "unknown",
        DeviceMode::Adb => "ADB",
        DeviceMode::Maskrom => "Maskrom",
        DeviceMode::Loader => "Loader",
        DeviceMode::Other(_) => "other",
    }
}

fn hex_bytes(bytes: &[u8]) -> String {
    bytes
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(" ")
}

fn now_hms() -> String {
    chrono::Local::now().format("%H:%M:%S").to_string()
}

fn format_bcd(bcd: u16) -> String {
    format!("{}.{:02}", (bcd >> 8) & 0xFF, bcd & 0xFF)
}

// ---------------------------------------------------------------------------
// 设备表格
// ---------------------------------------------------------------------------

struct DeviceTableDelegate {
    devices: Vec<DeviceInfo>,
    columns: Vec<Column>,
}

impl DeviceTableDelegate {
    fn new() -> Self {
        Self {
            devices: Vec::new(),
            columns: vec![
                Column::new("status", "状态").width(px(90.)).sortable(),
                Column::new("mode", "设备模式").width(px(120.)).sortable(),
                Column::new("vidpid", "VID:PID").width(px(130.)).sortable(),
                Column::new("bus", "总线").width(px(80.)).sortable(),
                Column::new("port", "端口路径").sortable(),
                Column::new("usb", "USB 版本").width(px(110.)).sortable(),
            ],
        }
    }
}

impl TableDelegate for DeviceTableDelegate {
    fn columns_count(&self, _cx: &App) -> usize {
        self.columns.len()
    }

    fn rows_count(&self, _cx: &App) -> usize {
        self.devices.len()
    }

    fn column(&self, col_ix: usize, _cx: &App) -> Column {
        self.columns[col_ix].clone()
    }

    fn perform_sort(
        &mut self,
        col_ix: usize,
        sort: ColumnSort,
        _window: &mut Window,
        cx: &mut Context<TableState<Self>>,
    ) {
        if matches!(sort, ColumnSort::Default) {
            return;
        }
        let descending = matches!(sort, ColumnSort::Descending);
        let key_of = |d: &DeviceInfo| -> String {
            match col_ix {
                0 => "已连接".to_string(),
                1 => mode_label(d.mode).to_string(),
                2 => format!("{:04x}:{:04x}", d.vendor_id, d.product_id),
                3 => format!("{:03}", d.bus_number),
                4 => d.port_path.clone(),
                5 => format_bcd(d.bcd_usb),
                _ => String::new(),
            }
        };
        self.devices.sort_by(|a, b| {
            let ord = key_of(a).cmp(&key_of(b));
            if descending { ord.reverse() } else { ord }
        });
        cx.notify();
    }

    fn render_td(
        &mut self,
        row_ix: usize,
        col_ix: usize,
        _window: &mut Window,
        _cx: &mut Context<TableState<Self>>,
    ) -> impl IntoElement {
        let Some(d) = self.devices.get(row_ix) else {
            return div().into_any_element();
        };
        let text: SharedString = match col_ix {
            0 => "✓ 已连接".into(),
            1 => mode_label(d.mode).into(),
            2 => format!("{:04x}:{:04x}", d.vendor_id, d.product_id).into(),
            3 => format!("{}", d.bus_number).into(),
            4 => d.port_path.clone().into(),
            5 => format_bcd(d.bcd_usb).into(),
            _ => String::new().into(),
        };
        div().child(text).into_any_element()
    }
}

// ---------------------------------------------------------------------------
// 任务历史表
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
struct HistoryEntry {
    start_time: String,
    port: String,
    firmware: String,
    result: String,
}

struct HistoryTableDelegate {
    rows: Vec<HistoryEntry>,
    columns: Vec<Column>,
}

impl HistoryTableDelegate {
    fn new() -> Self {
        Self {
            rows: Vec::new(),
            columns: vec![
                Column::new("time", "开始时间").width(px(110.)),
                Column::new("port", "端口").width(px(110.)),
                Column::new("firmware", "固件").sortable(),
                Column::new("result", "结果").width(px(320.)),
            ],
        }
    }

    fn push(&mut self, entry: HistoryEntry) {
        self.rows.insert(0, entry);
        self.rows.truncate(50);
    }
}

impl TableDelegate for HistoryTableDelegate {
    fn columns_count(&self, _cx: &App) -> usize {
        self.columns.len()
    }

    fn rows_count(&self, _cx: &App) -> usize {
        self.rows.len()
    }

    fn column(&self, col_ix: usize, _cx: &App) -> Column {
        self.columns[col_ix].clone()
    }

    fn render_td(
        &mut self,
        row_ix: usize,
        col_ix: usize,
        _window: &mut Window,
        _cx: &mut Context<TableState<Self>>,
    ) -> impl IntoElement {
        let Some(r) = self.rows.get(row_ix) else {
            return div().into_any_element();
        };
        let text: SharedString = match col_ix {
            0 => r.start_time.clone().into(),
            1 => r.port.clone().into(),
            2 => r.firmware.clone().into(),
            3 => r.result.clone().into(),
            _ => String::new().into(),
        };
        div().child(text).into_any_element()
    }
}

// ---------------------------------------------------------------------------
// 设备详情 / 升级状态
// ---------------------------------------------------------------------------

enum DeviceDetail {
    Empty,
    Unsupported,
    Loading,
    Ready(Box<zzrk_sys::DeviceInspection>),
    Failed(String),
}

enum PlanPreview {
    Empty,
    Loading,
    Ready(Box<FirmwareInfo>),
    Failed(String),
}

#[derive(Debug)]
enum UpgradeMsg {
    Progress(zzrk_sys::UpgradeProgressEvent),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

struct RunningUpgrade {
    current_completed: u64,
    current_total: u64,
    overall_fraction: f32,
    status: String,
}

enum UpgradeStatus {
    Idle,
    Running(RunningUpgrade),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

// ---------------------------------------------------------------------------
// 主视图
// ---------------------------------------------------------------------------

struct AppView {
    tab: TabKind,
    dark: bool,

    // 设备页
    devices: Vec<DeviceInfo>,
    device_table: Entity<TableState<DeviceTableDelegate>>,
    detail: DeviceDetail,
    detail_busy: bool,

    // 升级页
    firmware_input: Entity<InputState>,
    verify_select: Entity<SelectState<component::searchable_list::SearchableVec<String>>>,
    reboot: bool,
    prepare_only: bool,
    plan: PlanPreview,
    upgrade: UpgradeStatus,
    cancellation: Option<Cancellation>,
    history_table: Entity<TableState<HistoryTableDelegate>>,

    // 状态栏
    status_message: String,
    last_updated: Option<String>,
}

impl AppView {
    fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
        let device_table = cx.new(|cx| {
            TableState::new(DeviceTableDelegate::new(), window, cx)
                .sortable(true)
                .row_selectable(true)
        });
        let history_table = cx.new(|cx| {
            TableState::new(HistoryTableDelegate::new(), window, cx).row_selectable(true)
        });
        let firmware_input = cx.new(|cx| {
            InputState::new(window, cx)
                .placeholder("选择 update.img")
                .default_value(FIRMWARE_PATH)
        });
        let verify_select = cx.new(|cx| {
            SelectState::new(
                component::searchable_list::SearchableVec::new(vec![
                    "关键分区".to_string(),
                    "全部分区".to_string(),
                    "不回读".to_string(),
                ]),
                Some(component::IndexPath::new(0)),
                window,
                cx,
            )
        });
        Self {
            tab: TabKind::Device,
            dark: false,
            devices: Vec::new(),
            device_table,
            detail: DeviceDetail::Empty,
            detail_busy: false,
            firmware_input,
            verify_select,
            reboot: false,
            prepare_only: true,
            plan: PlanPreview::Empty,
            upgrade: UpgradeStatus::Idle,
            cancellation: None,
            history_table,
            status_message: "就绪".into(),
            last_updated: None,
        }
    }

    fn set_tab(&mut self, ix: usize, cx: &mut Context<Self>) {
        self.tab = TabKind::from_index(ix);
        cx.notify();
    }

    fn toggle_theme(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.dark = !self.dark;
        let dark = self.dark;
        cx.defer(move |cx| {
            apply_fluent_theme(dark, None, cx);
        });
        cx.notify();
        let _ = window;
    }

    fn connection_text(&self) -> String {
        if self.devices.is_empty() {
            "未检测到设备".into()
        } else {
            format!("已连接 {} 台设备", self.devices.len())
        }
    }

    fn selected_device(&self, cx: &App) -> Option<DeviceInfo> {
        let ix = self.device_table.read(cx).selected_row()?;
        self.devices.get(ix).cloned()
    }

    // ------------------------- 设备扫描 -------------------------

    fn refresh_devices(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.status_message = "正在扫描 USB 设备…".into();
        cx.notify();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::scan_devices() });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                match result {
                    Ok(devices) => {
                        view.devices = devices;
                        view.status_message =
                            format!("扫描完成：{} 台设备", view.devices.len());
                        view.device_table.update(cx, |table, cx| {
                            table.delegate_mut().devices = view.devices.clone();
                            table.refresh(cx);
                        });
                    }
                    Err(err) => view.status_message = format!("扫描失败：{err}"),
                }
                view.last_updated = Some(now_hms());
                view.detail = DeviceDetail::Empty;
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    // ------------------------- 设备详情 -------------------------

    fn load_device_detail(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.detail_busy {
            return;
        }
        let Some(device) = self.selected_device(cx) else {
            self.status_message = "请先选择设备".into();
            cx.notify();
            return;
        };
        if device.mode != DeviceMode::Loader {
            self.detail = DeviceDetail::Unsupported;
            self.status_message = "当前模式不支持详情与分区操作".into();
            window.push_notification(
                Notification::warning("当前模式不支持详情与分区操作").title("设备"),
                cx,
            );
            cx.notify();
            return;
        }
        self.detail_busy = true;
        self.detail = DeviceDetail::Loading;
        cx.notify();
        let port = device.port_path.clone();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::inspect_device(&port) });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                view.detail_busy = false;
                view.detail = match result {
                    Ok(d) => {
                        view.status_message = "详情读取完成".into();
                        DeviceDetail::Ready(Box::new(d))
                    }
                    Err(e) => {
                        view.status_message = format!("详情读取失败：{e}");
                        DeviceDetail::Failed(e)
                    }
                };
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    // ------------------------- 固件浏览与检查 -------------------------

    fn browse_firmware(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let task = cx.prompt_for_paths(PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some("选择 update.img".into()),
        });
        cx.spawn_in(window, async move |view, cx| {
            match task.await {
                Ok(Ok(Some(paths))) => {
                    if let Some(path) = paths.into_iter().next() {
                        let text = path.to_string_lossy().into_owned();
                        view.update_in(cx, |view, window, cx| {
                            view.firmware_input.update(cx, |input, cx| {
                                input.set_value(text, window, cx);
                            });
                            cx.notify();
                        })
                        .ok();
                        view.update_in(cx, |view, window, cx| view.inspect_plan(window, cx))
                            .ok();
                    }
                }
                Ok(Ok(None)) | Err(_) => {}
                Ok(Err(err)) => {
                    // X11 转发下 portal 常不可用：兜底保留文本输入。
                    view.update(cx, |view, cx| {
                        view.status_message =
                            format!("文件对话框不可用（{err}），请直接输入路径");
                        cx.notify();
                    })
                    .ok();
                }
            }
        })
        .detach();
    }

    fn inspect_plan(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.plan, PlanPreview::Loading) {
            return;
        }
        let path = self.firmware_input.read(cx).value().to_string();
        if path.trim().is_empty() {
            self.status_message = "未选择固件".into();
            cx.notify();
            return;
        }
        self.plan = PlanPreview::Loading;
        cx.notify();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::inspect_firmware(&path) });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                view.plan = match result {
                    Ok(info) => {
                        view.status_message = format!("固件就绪：{} 个分区", info.partition_count);
                        PlanPreview::Ready(Box::new(info))
                    }
                    Err(e) => {
                        view.status_message = format!("固件检查失败：{e}");
                        PlanPreview::Failed(e)
                    }
                };
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    // ------------------------- 升级 -------------------------

    fn verify_mode(&self, cx: &App) -> zzrk_sys::VerifyMode {
        match self.verify_select.read(cx).selected_value().map(String::as_str) {
            Some("全部分区") => zzrk_sys::VerifyMode::All,
            Some("不回读") => zzrk_sys::VerifyMode::None,
            _ => zzrk_sys::VerifyMode::Critical,
        }
    }

    fn request_start_upgrade(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.upgrade, UpgradeStatus::Running(_)) {
            return;
        }
        if self.devices.is_empty() {
            self.status_message = "未检测到设备：请先在设备页刷新".into();
            window.push_notification(
                Notification::warning("未检测到设备：请先在设备页刷新").title("完整升级"),
                cx,
            );
            cx.notify();
            return;
        }
        let prepare_only = self.prepare_only;
        let view = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _window, _cx| {
            let view = view.clone();
            alert
                .confirm()
                .title("开始完整升级？")
                .description(if prepare_only {
                    "prepare_only 已开启：不会写入闪存，但设备仍会复位进入 \
                     maskrom 并下载 RAM loader。"
                } else {
                    "将写入设备闪存，此操作不可撤销。"
                })
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    view.update(cx, |view, cx| view.start_upgrade(window, cx))
                        .ok();
                    true
                })
        });
    }

    fn start_upgrade(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.upgrade, UpgradeStatus::Running(_)) {
            return;
        }
        let Some(port_path) = self
            .selected_device(cx)
            .or_else(|| self.devices.first().cloned())
            .map(|d| d.port_path)
        else {
            self.status_message = "未检测到设备".into();
            cx.notify();
            return;
        };
        let image_path = self.firmware_input.read(cx).value().to_string();
        let verify_mode = self.verify_mode(cx);
        let reboot = self.reboot;
        let prepare_only = self.prepare_only;

        let token = Cancellation::new();
        self.cancellation = Some(token.clone());
        self.upgrade = UpgradeStatus::Running(RunningUpgrade {
            current_completed: 0,
            current_total: 0,
            overall_fraction: 0.,
            status: "正在启动升级…".into(),
        });
        self.status_message = "升级进行中…".into();
        cx.notify();

        let start_time = now_hms();
        let firmware_name = image_path.clone();
        let bg_port_path = port_path.clone();
        let (tx, mut rx) = futures::channel::mpsc::unbounded::<UpgradeMsg>();
        let tx_progress = tx.clone();
        cx.background_executor()
            .spawn(async move {
                let result = zzrk_sys::run_firmware_upgrade(
                    &bg_port_path,
                    &image_path,
                    verify_mode,
                    reboot,
                    prepare_only,
                    move |event| {
                        let _ = tx_progress.unbounded_send(UpgradeMsg::Progress(event));
                    },
                    Some(&token),
                );
                let _ = tx.unbounded_send(UpgradeMsg::Done(result));
            })
            .detach();

        cx.spawn_in(window, async move |view, cx| {
            while let Some(msg) = rx.next().await {
                let keep_going = view
                    .update(cx, |view, cx| {
                        match msg {
                            UpgradeMsg::Progress(event) => {
                                let stage_no = event.stage().map_or(0, |s| s as i32) as f32;
                                let phase = event.fraction().unwrap_or(0.) as f32;
                                view.upgrade = UpgradeStatus::Running(RunningUpgrade {
                                    current_completed: event.completed_bytes,
                                    current_total: event.total_bytes,
                                    overall_fraction: ((stage_no + phase) / 12.).min(1.),
                                    status: format!(
                                        "阶段 {}：{}/{} 字节",
                                        event
                                            .stage()
                                            .map_or_else(|| "?".to_string(), |s| s.name().to_string()),
                                        event.completed_bytes,
                                        event.total_bytes,
                                    ),
                                });
                            }
                            UpgradeMsg::Done(result) => {
                                let entry = HistoryEntry {
                                    start_time: start_time.clone(),
                                    port: port_path.clone(),
                                    firmware: firmware_name.clone(),
                                    result: match &result {
                                        Ok(o) => format!(
                                            "成功（prepare_only={}）",
                                            o.prepared_only
                                        ),
                                        Err(e) => format!("失败：{}", e.message),
                                    },
                                };
                                view.history_table.update(cx, |table, cx| {
                                    table.delegate_mut().push(entry);
                                    table.refresh(cx);
                                });
                                view.status_message = match &result {
                                    Ok(_) => "升级完成".into(),
                                    Err(e) => format!("升级失败：{}", e.message),
                                };
                                view.upgrade = UpgradeStatus::Done(result);
                                view.cancellation = None;
                            }
                        }
                        cx.notify();
                    })
                    .is_ok();
                if !keep_going {
                    break;
                }
            }
        })
        .detach();
    }

    fn cancel_upgrade(&mut self, cx: &mut Context<Self>) {
        if let Some(token) = &self.cancellation {
            token.cancel();
            self.status_message = "正在取消…".into();
            cx.notify();
        }
    }

    // ------------------------- 页面渲染 -------------------------

    fn render_device_page(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let theme = cx.theme().clone();
        let selected_mode = self.selected_device(cx).map(|d| d.mode);
        let detail_supported = selected_mode == Some(DeviceMode::Loader);

        let detail_body: AnyElement = match &self.detail {
            DeviceDetail::Empty => div()
                .text_color(theme.muted_foreground)
                .child("选择设备后点击“读取详情”")
                .into_any_element(),
            DeviceDetail::Unsupported => div()
                .text_color(theme.muted_foreground)
                .child("当前模式不支持详情与分区操作")
                .into_any_element(),
            DeviceDetail::Loading => div()
                .text_color(theme.muted_foreground)
                .child("正在读取详情…")
                .into_any_element(),
            DeviceDetail::Failed(err) => div()
                .text_color(theme.danger)
                .child(format!("读取失败：{err}"))
                .into_any_element(),
            DeviceDetail::Ready(d) => {
                let emmc = if d.flash_id_is_emmc {
                    format!(
                        "{:.1} GB ({} 扇区)",
                        d.legacy_flash_sector_count as f64 * 512. / 1e9,
                        d.legacy_flash_sector_count
                    )
                } else {
                    "非 eMMC".to_string()
                };
                let rows: [(&str, String); 6] = [
                    ("ChipInfo", hex_bytes(&d.chip_info)),
                    ("Capability", hex_bytes(&d.capability)),
                    ("FlashID", hex_bytes(&d.flash_id)),
                    ("eMMC 容量", emmc),
                    ("FlashInfo", hex_bytes(&d.flash_info)),
                    (
                        "USB 接口",
                        format!(
                            "bus {} addr {} (USB {})",
                            d.device.bus_number,
                            d.device.device_address,
                            format_bcd(d.device.bcd_usb)
                        ),
                    ),
                ];
                v_flex()
                    .gap_1()
                    .children(rows.iter().map(|(k, v)| {
                        h_flex()
                            .gap_2()
                            .child(
                                div()
                                    .w(px(110.))
                                    .text_color(theme.muted_foreground)
                                    .child(k.to_string()),
                            )
                            .child(div().child(v.clone()))
                    }))
                    .into_any_element()
            }
        };

        v_flex()
            .size_full()
            .gap_3()
            .child(
                div()
                    .flex_1()
                    .min_h(px(200.))
                    .child(DataTable::new(&self.device_table).stripe(true).bordered(false)),
            )
            .child(
                GroupBox::new()
                    .title("所选设备详情")
                    .child(
                        v_flex()
                            .gap_2()
                            .child(
                                h_flex()
                                    .gap_2()
                                    .child(
                                        Button::new("load-detail")
                                            .outline()
                                            .icon(IconName::Search)
                                            .label("读取详情")
                                            .disabled(!detail_supported || self.detail_busy)
                                            .on_click(cx.listener(|view, _, window, cx| {
                                                view.load_device_detail(window, cx);
                                            })),
                                    )
                                    .when(!detail_supported, |this| {
                                        this.child(
                                            div()
                                                .text_sm()
                                                .text_color(theme.muted_foreground)
                                                .child("当前模式不支持详情与分区操作"),
                                        )
                                    }),
                            )
                            .child(detail_body),
                    ),
            )
    }

    fn render_upgrade_page(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let theme = cx.theme().clone();
        let running = matches!(self.upgrade, UpgradeStatus::Running(_));

        let plan_body: AnyElement = match &self.plan {
            PlanPreview::Empty => div()
                .text_color(theme.muted_foreground)
                .child("未选择固件")
                .into_any_element(),
            PlanPreview::Loading => div()
                .text_color(theme.muted_foreground)
                .child("正在检查固件（MD5）…")
                .into_any_element(),
            PlanPreview::Failed(err) => div()
                .text_color(theme.danger)
                .child(format!("固件检查失败：{err}"))
                .into_any_element(),
            PlanPreview::Ready(info) => div()
                .text_sm()
                .child(format!(
                    "chip={}  model={}  manufacturer={}  分区 {} 个  大小 {} 字节  md5={}  verified={}",
                    info.chip,
                    info.model,
                    info.manufacturer,
                    info.partition_count,
                    info.file_size,
                    info.md5,
                    info.md5_verified,
                ))
                .into_any_element(),
        };

        let (overall, current, stage_status) = match &self.upgrade {
            UpgradeStatus::Running(r) => (
                r.overall_fraction * 100.,
                if r.current_total > 0 {
                    r.current_completed as f32 * 100. / r.current_total as f32
                } else {
                    0.
                },
                r.status.clone(),
            ),
            UpgradeStatus::Done(Ok(_)) => (100., 100., "升级完成".to_string()),
            UpgradeStatus::Done(Err(e)) => (0., 0., format!("升级失败：{}", e.message)),
            UpgradeStatus::Idle => (0., 0., "就绪".to_string()),
        };

        v_flex()
            .size_full()
            .gap_3()
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(60.)).child("固件"))
                    .child(div().flex_1().child(Input::new(&self.firmware_input).disabled(running)))
                    .child(
                        Button::new("browse")
                            .outline()
                            .icon(IconName::FolderOpen)
                            .label("浏览")
                            .disabled(running)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.browse_firmware(window, cx);
                            })),
                    )
                    .child(
                        Button::new("inspect-plan")
                            .ghost()
                            .icon(IconName::RotateCw)
                            .label("检查")
                            .disabled(running || matches!(self.plan, PlanPreview::Loading))
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.inspect_plan(window, cx);
                            })),
                    ),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(60.)).child("升级计划"))
                    .child(
                        div()
                            .flex_1()
                            .h(px(88.))
                            .border_1()
                            .border_color(theme.border)
                            .rounded_md()
                            .p_2()
                            .child(plan_body),
                    ),
            )
            .child(
                h_flex()
                    .gap_3()
                    .child(div().w(px(60.)).child("校验"))
                    .child(div().w(px(140.)).child(Select::new(&self.verify_select)))
                    .child(
                        Checkbox::new("reboot")
                            .label("完成后重启")
                            .checked(self.reboot)
                            .on_click(cx.listener(|view, checked, _, cx| {
                                view.reboot = *checked;
                                cx.notify();
                            })),
                    )
                    .child(
                        Checkbox::new("prepare-only")
                            .label("prepare_only（不写闪存）")
                            .checked(self.prepare_only)
                            .on_click(cx.listener(|view, checked, _, cx| {
                                view.prepare_only = *checked;
                                cx.notify();
                            })),
                    )
                    .child(div().flex_1())
                    .child(
                        Button::new("start-upgrade")
                            .primary()
                            .icon(IconName::Play)
                            .label("开始升级")
                            .disabled(running)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.request_start_upgrade(window, cx);
                            })),
                    )
                    .child(
                        Button::new("cancel-upgrade")
                            .outline()
                            .icon(IconName::Close)
                            .label("取消")
                            .disabled(!running)
                            .on_click(cx.listener(|view, _, _, cx| {
                                view.cancel_upgrade(cx);
                            })),
                    ),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(60.)).child("总体"))
                    .child(div().flex_1().child(Progress::new("overall").value(overall))),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(60.)).child("当前"))
                    .child(div().flex_1().child(Progress::new("current").value(current))),
            )
            .child(div().text_sm().text_color(theme.muted_foreground).child(stage_status))
            .child(
                h_flex()
                    .child(div().child("任务历史（本次会话）"))
                    .child(div().flex_1())
                    .child(
                        Button::new("export-history")
                            .ghost()
                            .icon(IconName::FileText)
                            .label("导出")
                            .disabled(true),
                    ),
            )
            .child(
                div()
                    .flex_1()
                    .min_h(px(120.))
                    .child(DataTable::new(&self.history_table).stripe(true).bordered(false)),
            )
    }

    fn render_title_bar(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let theme = cx.theme().clone();
        // gpui-component 的 WindowControls 只在 client decorations 生效时绘制；
        // 本机 X11 无合成器会回退 server decorations 且 WM 不画边框，此时补
        // 一套等效按钮（真实桌面 client decorations 下不显示，避免重复）。
        let needs_fallback_controls =
            !matches!(window.window_decorations(), Decorations::Client { .. });
        let maximized = window.is_maximized();
        TitleBar::new().h(px(32.)).child(
            h_flex()
                .w_full()
                .child(
                    h_flex()
                        .gap_2()
                        .child(Icon::new(IconName::Cpu).size(px(16.)).text_color(theme.foreground)),
                )
                .child(
                    div()
                        .flex_1()
                        .flex()
                        .justify_center()
                        .child(div().font_weight(FontWeight::SEMIBOLD).child("ZzRkDevTool")),
                )
                .child(
                    Button::new("theme-toggle")
                        .ghost()
                        .icon(if self.dark { IconName::Sun } else { IconName::Moon })
                        .on_click(cx.listener(|view, _, window, cx| {
                            view.toggle_theme(window, cx);
                        })),
                )
                .when(needs_fallback_controls, |this| {
                    this.child(
                        Button::new("win-min")
                            .ghost()
                            .icon(IconName::Minus)
                            .on_click(|_, window, _| window.minimize_window()),
                    )
                    .child(
                        Button::new("win-max")
                            .ghost()
                            .icon(if maximized {
                                IconName::WindowRestore
                            } else {
                                IconName::WindowMaximize
                            })
                            .on_click(|_, window, _| window.zoom_window()),
                    )
                    .child(
                        Button::new("win-close")
                            .ghost()
                            .icon(IconName::WindowClose)
                            .on_click(|_, window, _| window.remove_window()),
                    )
                }),
        )
    }

    fn render_tab_bar(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        TabBar::new("main-tabs")
            .underline()
            .selected_index(self.tab.index())
            .children(TabKind::ALL.iter().map(|t| Tab::new().label(t.label())))
            .on_click(cx.listener(|view, ix: &usize, _window, cx| {
                view.set_tab(*ix, cx);
            }))
    }

    fn render_status_bar(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let theme = cx.theme().clone();
        let updated = self
            .last_updated
            .as_ref()
            .map_or("更新于 --:--:--".to_string(), |t| format!("更新于 {t}"));
        StatusBar::new()
            .left(
                h_flex()
                    .gap_3()
                    .child(div().text_sm().child(self.status_message.clone()))
                    .child(
                        div()
                            .text_sm()
                            .text_color(theme.muted_foreground)
                            .child(self.connection_text()),
                    ),
            )
            .right(
                h_flex()
                    .gap_2()
                    .items_center()
                    .child(
                        Button::new("status-refresh")
                            .ghost()
                            .icon(IconName::RotateCw)
                            .w(px(30.))
                            .h(px(30.))
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.refresh_devices(window, cx);
                            })),
                    )
                    .child(
                        div()
                            .text_sm()
                            .text_color(theme.muted_foreground)
                            .child(updated),
                    ),
            )
    }
}

impl Render for AppView {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let theme = cx.theme().clone();

        let content: AnyElement = match self.tab {
            TabKind::Device => self.render_device_page(window, cx).into_any_element(),
            TabKind::Upgrade => self.render_upgrade_page(window, cx).into_any_element(),
            TabKind::Download => v_flex()
                .size_full()
                .justify_center()
                .items_center()
                .gap_2()
                .child(
                    Icon::new(IconName::Inbox)
                        .size(px(32.))
                        .text_color(theme.muted_foreground),
                )
                .child(div().text_lg().child("下载镜像"))
                .child(
                    div()
                        .text_sm()
                        .text_color(theme.muted_foreground)
                        .child("待实现"),
                )
                .into_any_element(),
            TabKind::Partition => v_flex()
                .size_full()
                .justify_center()
                .items_center()
                .gap_2()
                .child(
                    Icon::new(IconName::Inbox)
                        .size(px(32.))
                        .text_color(theme.muted_foreground),
                )
                .child(div().text_lg().child("分区"))
                .child(
                    div()
                        .text_sm()
                        .text_color(theme.muted_foreground)
                        .child("待实现"),
                )
                .into_any_element(),
        };

        v_flex()
            .size_full()
            .bg(theme.background)
            .text_color(theme.foreground)
            .child(self.render_title_bar(window, cx))
            .child(self.render_tab_bar(window, cx))
            .child(
                div()
                    .flex_1()
                    .overflow_hidden()
                    .p(px(CONTENT_MARGIN))
                    .child(content),
            )
            .child(self.render_status_bar(window, cx))
    }
}

fn main() {
    gpui_kit::application()
        .with_assets(gpui_kit::assets::Assets)
        .run(|cx| {
            gpui_kit::init(cx);
            apply_fluent_theme(false, None, cx);
            cx.spawn(async move |cx| {
                cx.open_window(
                    WindowOptions {
                        window_bounds: Some(WindowBounds::Windowed(Bounds {
                            origin: point(px(80.), px(80.)),
                            size: size(px(980.), px(820.)),
                        })),
                        window_min_size: Some(size(px(800.), px(700.))),
                        window_decorations: Some(WindowDecorations::Client),
                        ..TitleBar::window_options()
                    },
                    |window, cx| {
                        let view = cx.new(|cx| AppView::new(window, cx));
                        cx.new(|cx| Root::new(view, window, cx))
                    },
                )
                .expect("failed to open window");
            })
            .detach();
        });
}
