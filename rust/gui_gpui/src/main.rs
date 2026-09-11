//! GPUI spike for ZzRkDevTool: window + device scan + background-thread
//! firmware inspection through the zzrk-sys FFI wrapper.

use gpui::{
    App, Application, Bounds, ClickEvent, Context, ElementId, SharedString, Stateful, Window,
    WindowBounds, WindowOptions, div, prelude::*, px, rgb, size,
};
use zzrk_sys::{DeviceInfo, DeviceMode, FirmwareInfo};

const FIRMWARE_PATH: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../pd.core.CT1.rk3588s.20260727.161.img"
);

enum FirmwareState {
    Idle,
    Inspecting,
    Done(Result<FirmwareInfo, String>),
}

struct DevicesView {
    devices: Vec<DeviceInfo>,
    scan_error: Option<String>,
    firmware: FirmwareState,
}

impl DevicesView {
    fn new() -> Self {
        Self {
            devices: Vec::new(),
            scan_error: None,
            firmware: FirmwareState::Idle,
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

        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::inspect_firmware(FIRMWARE_PATH) });
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
}

fn button(
    id: impl Into<ElementId>,
    label: &'static str,
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
        .child(label)
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

impl Render for DevicesView {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
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
            FirmwareState::Idle => format!("firmware: {FIRMWARE_PATH}").into(),
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

        div()
            .flex()
            .flex_col()
            .gap_3()
            .size_full()
            .p_4()
            .bg(rgb(0x1e1e1e))
            .text_color(rgb(0xe0e0e0))
            .child(div().text_xl().child("ZzRkDevTool GPUI spike"))
            .child(div().text_sm().child(format!("core version: {}", zzrk_sys::version())))
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
            .child(div().pt_2().child(firmware_section))
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(760.), px(480.)), cx);
        cx.open_window(
            WindowOptions {
                window_bounds: Some(WindowBounds::Windowed(bounds)),
                titlebar: Some(gpui::TitlebarOptions {
                    title: Some("ZzRk GPUI Spike".into()),
                    ..Default::default()
                }),
                ..Default::default()
            },
            |_, cx| cx.new(|_| DevicesView::new()),
        )
        .unwrap();
        cx.activate(true);
    });
}
