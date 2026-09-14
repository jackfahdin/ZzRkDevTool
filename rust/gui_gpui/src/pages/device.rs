// SPDX-License-Identifier: MIT
//! 设备页：6 列设备表格 + 所选设备详情分组。

use gpui_kit::component::button::Button;
use gpui_kit::component::group_box::GroupBox;
use gpui_kit::component::notification::Notification;
use gpui_kit::component::table::{Column, ColumnSort, DataTable, TableDelegate, TableState};
use gpui_kit::component::{ActiveTheme, Disableable as _, IconName, WindowExt as _, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder as _;
use gpui_kit::*;
use zzrk_sys::{DeviceInfo, DeviceMode};

use crate::state::{format_bcd, hex_bytes, mode_label, now_hms};
use crate::AppView;

pub(crate) struct DeviceTableDelegate {
    pub(crate) devices: Vec<DeviceInfo>,
    columns: Vec<Column>,
}

impl DeviceTableDelegate {
    pub(crate) fn new() -> Self {
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
            2 => format!("{:04X}:{:04X}", d.vendor_id, d.product_id).into(),
            3 => format!("{}", d.bus_number).into(),
            4 => d.port_path.clone().into(),
            5 => format_bcd(d.bcd_usb).into(),
            _ => String::new().into(),
        };
        div().child(text).into_any_element()
    }
}

pub(crate) enum DeviceDetail {
    Empty,
    Unsupported,
    Loading,
    Ready(Box<zzrk_sys::DeviceInspection>),
    Failed(String),
}

impl AppView {
    pub(crate) fn load_device_detail(&mut self, window: &mut Window, cx: &mut Context<Self>) {
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

    pub(crate) fn render_device_page(
        &mut self,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) -> impl IntoElement {
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

    /// 刷新完成后由主壳调用：把新设备列表推进表格并维护选中端口。
    pub(crate) fn apply_scanned_devices(
        &mut self,
        devices: Vec<DeviceInfo>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        // 选中端口在新列表中保留
        let keep = self
            .selected_port
            .as_ref()
            .and_then(|p| devices.iter().position(|d| &d.port_path == p));
        self.devices = devices;
        self.device_table.update(cx, |table, cx| {
            table.delegate_mut().devices = self.devices.clone();
            table.refresh(cx);
            if let Some(ix) = keep {
                table.set_selected_row(ix, cx);
            } else if self.devices.is_empty() {
                table.clear_selection(cx);
            }
        });
        self.rebuild_download_device_select(window, cx);
        self.detail = DeviceDetail::Empty;
        self.last_updated = Some(now_hms());
    }
}
