//! GPUI spike for ZzRkDevTool: device scan + firmware inspection + a minimal
//! firmware-upgrade panel wired through the zzrk-sys FFI wrapper.
//!
//! SAFETY: the "开始升级" path is fully wired but must stay behind
//! `prepare_only` on real hardware — even prepare-only runs reset the device
//! into maskrom and download the RAM loader (see FirmwareUpgradeWorkflow).

use futures::StreamExt;
use gpui::{
    App, Application, Bounds, ClickEvent, Context, ElementId, FocusHandle, KeyDownEvent,
    SharedString, Stateful, Window, WindowBounds, WindowOptions, div, prelude::*, px, rgb, size,
};
use zzrk_sys::{Cancellation, DeviceInfo, DeviceMode, FirmwareInfo, UpgradeError, UpgradeOutcome};

const FIRMWARE_PATH: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../pd.core.CT1.rk3588s.20260727.161.img"
);

enum FirmwareState {
    Idle,
    Inspecting,
    Done(Result<FirmwareInfo, String>),
}

#[derive(Debug)]
enum UpgradeMsg {
    Progress(zzrk_sys::UpgradeProgressEvent),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

#[derive(Debug, Clone)]
struct RunningProgress {
    stage: String,
    completed_bytes: u64,
    total_bytes: u64,
}

enum UpgradeStatus {
    Idle,
    Running(RunningProgress),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

struct DevicesView {
    devices: Vec<DeviceInfo>,
    scan_error: Option<String>,
    firmware: FirmwareState,
    // Upgrade panel
    firmware_path: String,
    path_focus: FocusHandle,
    prepare_only: bool,
    upgrade: UpgradeStatus,
    cancellation: Option<Cancellation>,
}

impl DevicesView {
    fn new(cx: &mut Context<Self>) -> Self {
        Self {
            devices: Vec::new(),
            scan_error: None,
            firmware: FirmwareState::Idle,
            firmware_path: FIRMWARE_PATH.to_string(),
            path_focus: cx.focus_handle(),
            prepare_only: true,
            upgrade: UpgradeStatus::Idle,
            cancellation: None,
        }
    }

    fn refresh_devices(&mut self, cx: &mut Context<Self>) {
        match zzrk_sys::scan_devices() {
            Ok(devices) => {
                self.devices = devices;
                self.scan_error = None;
            }
            Err(err) => self.scan_error = Some(err),
        }
        cx.notify();
    }

    // 2 GB image: MD5 pass takes seconds, so run it on a background executor
    // thread and marshal the result back into the UI context.
    fn inspect_firmware(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.firmware, FirmwareState::Inspecting) {
            return;
        }
        self.firmware = FirmwareState::Inspecting;
        cx.notify();

        let path = self.firmware_path.clone();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::inspect_firmware(path) });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                view.firmware = FirmwareState::Done(result);
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    fn on_path_key(&mut self, event: &KeyDownEvent, cx: &mut Context<Self>) {
        let keystroke = &event.keystroke;
        let plain = !keystroke.modifiers.control
            && !keystroke.modifiers.alt
            && !keystroke.modifiers.platform;
        if keystroke.key == "backspace" {
            self.firmware_path.pop();
        } else if plain {
            if let Some(text) = &keystroke.key_char {
                self.firmware_path.push_str(text);
            }
        }
        cx.notify();
    }

    fn toggle_prepare_only(&mut self, cx: &mut Context<Self>) {
        self.prepare_only = !self.prepare_only;
        cx.notify();
    }

    fn start_upgrade(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if matches!(self.upgrade, UpgradeStatus::Running(_)) {
            return;
        }
        let Some(port_path) = self.devices.first().map(|d| d.port_path.clone()) else {
            self.upgrade = UpgradeStatus::Done(Err(UpgradeError {
                error_code: -1,
                error_stage: -1,
                error_target: String::new(),
                message: "no device: click 刷新 / Refresh first".into(),
            }));
            cx.notify();
            return;
        };

        let token = Cancellation::new();
        self.cancellation = Some(token.clone());
        self.upgrade = UpgradeStatus::Running(RunningProgress {
            stage: "starting".into(),
            completed_bytes: 0,
            total_bytes: 0,
        });
        cx.notify();

        let image_path = self.firmware_path.clone();
        let prepare_only = self.prepare_only;
        let reboot = false;

        // Blocking FFI call on a background thread; progress events are
        // pushed into an unbounded channel (the callback cannot await) and
        // drained by a UI-context task below.
        let (tx, mut rx) = futures::channel::mpsc::unbounded::<UpgradeMsg>();
        let tx_progress = tx.clone();
        cx.background_executor()
            .spawn(async move {
                let result = zzrk_sys::run_firmware_upgrade(
                    &port_path,
                    &image_path,
                    zzrk_sys::VerifyMode::None,
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
                                view.upgrade = UpgradeStatus::Running(RunningProgress {
                                    stage: event
                                        .stage()
                                        .map_or_else(|| "unknown".into(), |s| s.name().into()),
                                    completed_bytes: event.completed_bytes,
                                    total_bytes: event.total_bytes,
                                });
                            }
                            UpgradeMsg::Done(result) => {
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
        }
        cx.notify();
    }
}

fn button(
    id: impl Into<ElementId>,
    label: impl Into<SharedString>,
    on_click: impl Fn(&ClickEvent, &mut Window, &mut App) + 'static,
) -> Stateful<gpui::Div> {
    div()
        .id(id)
        .px_3()
        .py_1()
        .bg(rgb(0x2d6cdf))
        .rounded_md()
        .text_color(rgb(0xffffff))
        .cursor_pointer()
        .child(label.into())
        .on_click(on_click)
}

fn mode_label(mode: DeviceMode) -> &'static str {
    match mode {
        DeviceMode::Unknown => "unknown",
        DeviceMode::Adb => "adb",
        DeviceMode::Maskrom => "maskrom",
        DeviceMode::Loader => "loader",
        DeviceMode::Other(_) => "other",
    }
}

fn format_upgrade_result(result: &Result<UpgradeOutcome, UpgradeError>) -> String {
    match result {
        Ok(o) => format!(
            "upgrade ok: model={} md5={} device_sectors={} written={} erased={} \
             verified={} prepare_only={} pt_written={} loader_written={}",
            o.model,
            o.firmware_md5,
            o.device_sectors,
            o.partition_bytes_written,
            o.sectors_erased,
            o.partitions_verified,
            o.prepared_only,
            o.partition_table_written,
            o.loader_written,
        ),
        Err(e) => format!(
            "upgrade failed: code={:?}({}) stage={:?}({}) target={} msg={}",
            e.code(),
            e.error_code,
            e.stage(),
            e.error_stage,
            e.error_target,
            e.message,
        ),
    }
}

impl Render for DevicesView {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let device_rows: Vec<_> = self
            .devices
            .iter()
            .map(|d| {
                div().child(format!(
                    "vid={:04x} pid={:04x} mode={} port={} (bus {} addr {})",
                    d.vendor_id,
                    d.product_id,
                    mode_label(d.mode),
                    d.port_path,
                    d.bus_number,
                    d.device_address,
                ))
            })
            .collect();

        let device_section: SharedString = if let Some(err) = &self.scan_error {
            format!("scan failed: {err}").into()
        } else if self.devices.is_empty() {
            "no Rockchip devices found".into()
        } else {
            format!("{} device(s)", self.devices.len()).into()
        };

        let firmware_section: SharedString = match &self.firmware {
            FirmwareState::Idle => "firmware: (not inspected)".into(),
            FirmwareState::Inspecting => "inspecting firmware (MD5 pass)…".into(),
            FirmwareState::Done(Ok(info)) => format!(
                "chip={} model={} manufacturer={} md5={} verified={} size={} partitions={}",
                info.chip,
                info.model,
                info.manufacturer,
                info.md5,
                info.md5_verified,
                info.file_size,
                info.partition_count,
            )
            .into(),
            FirmwareState::Done(Err(err)) => format!("inspect failed: {err}").into(),
        };

        let path_focused = self.path_focus.is_focused(window);
        let path_border = if path_focused { 0x2d6cdf } else { 0x555555 };

        let prepare_label = if self.prepare_only {
            "[x] prepare_only（仅准备，不写闪存）"
        } else {
            "[ ] prepare_only（仅准备，不写闪存）"
        };

        let upgrade_running = matches!(self.upgrade, UpgradeStatus::Running(_));
        let upgrade_section: SharedString = match &self.upgrade {
            UpgradeStatus::Idle => "upgrade: idle".into(),
            UpgradeStatus::Running(p) => {
                let pct = if p.total_bytes > 0 {
                    format!(" ({:.1}%)", p.completed_bytes as f64 * 100.0 / p.total_bytes as f64)
                } else {
                    String::new()
                };
                format!(
                    "upgrade running: stage={} bytes={}/{}{}",
                    p.stage, p.completed_bytes, p.total_bytes, pct
                )
                .into()
            }
            UpgradeStatus::Done(result) => format_upgrade_result(result).into(),
        };

        div()
            .flex()
            .flex_col()
            .gap_3()
            .size_full()
            .p_4()
            .bg(rgb(0x1e1e1e))
            .text_color(rgb(0xe0e0e0))
            .child(div().text_xl().child("ZzRkDevTool GPUI spike"))
            .child(
                div()
                    .text_sm()
                    .child(format!("core version: {}", zzrk_sys::version())),
            )
            .child(
                div()
                    .flex()
                    .gap_2()
                    .child(button("refresh", "刷新 / Refresh", cx.listener(
                        |view, _, _, cx| view.refresh_devices(cx),
                    )))
                    .child(button("inspect", "检查固件 / Inspect firmware", cx.listener(
                        |view, _, window, cx| view.inspect_firmware(window, cx),
                    ))),
            )
            .child(div().child(device_section))
            .child(div().flex().flex_col().gap_1().children(device_rows))
            .child(div().child(firmware_section))
            // ------------------------- upgrade panel -------------------------
            .child(div().pt_2().text_lg().child("固件升级 / Firmware upgrade"))
            .child(
                div()
                    .id("path-input")
                    .track_focus(&self.path_focus)
                    .border_1()
                    .border_color(rgb(path_border))
                    .rounded_md()
                    .px_2()
                    .py_1()
                    .w(px(640.))
                    .cursor_text()
                    .child(self.firmware_path.clone())
                    .on_click(cx.listener(|view, _, window, _cx| {
                        view.path_focus.focus(window);
                    }))
                    .on_key_down(cx.listener(|view, event, _window, cx| {
                        view.on_path_key(event, cx);
                    })),
            )
            .child(
                div()
                    .flex()
                    .gap_2()
                    .child(button("prepare-only", prepare_label, cx.listener(
                        |view, _, _, cx| view.toggle_prepare_only(cx),
                    )))
                    .child(button("start-upgrade", "开始升级 / Start", cx.listener(
                        |view, _, window, cx| view.start_upgrade(window, cx),
                    )))
                    .child(button("cancel-upgrade", "取消 / Cancel", cx.listener(
                        |view, _, _, cx| view.cancel_upgrade(cx),
                    ))),
            )
            .child(div().child(if upgrade_running {
                format!("{upgrade_section}  (取消可用)")
            } else {
                upgrade_section.to_string()
            }))
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(760.), px(560.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                titlebar: Some(gpui::TitlebarOptions {
                    title: Some("ZzRk GPUI Spike".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |_, cx| cx.new(DevicesView::new),
        )
        .unwrap();
        cx.activate(true);
    });
}
