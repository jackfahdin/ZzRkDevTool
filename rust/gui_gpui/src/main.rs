// SPDX-License-Identifier: MIT
//! ZzRkDevTool GPUI 窗口（gpui-kit 组件栈），样式对齐 Qt 版骨架规格。
//!
//! 模块结构：main.rs = 入口 + 窗口骨架（标题栏/标签/状态栏）+ 共享状态；
//! pages/* = 四个标签页；theme.rs = Fluent 主题令牌；state.rs = 共享数据。
//!
//! SAFETY: 本机插着真实 RK 设备。升级/下载/写入/擦除的执行链路已接线，
//! 但在真实设备上不得触发（即使 prepare_only 也会复位设备进 maskrom）。

mod pages;
mod state;
mod theme;

use std::collections::BTreeMap;

use gpui_kit::component::button::{Button, ButtonVariants as _};
use gpui_kit::component::input::InputState;
use gpui_kit::component::select::{SelectState, SelectEvent};
use gpui_kit::component::status_bar::StatusBar;
use gpui_kit::component::tab::{Tab, TabBar};
use gpui_kit::component::table::{TableEvent, TableState};
use gpui_kit::component::{
    ActiveTheme, Icon, IconName, Root, TitleBar, h_flex, v_flex,
};
use gpui_kit::prelude::FluentBuilder as _;
use gpui_kit::*;
use zzrk_sys::{Cancellation, DeviceInfo};

use pages::device::{DeviceDetail, DeviceTableDelegate};
use pages::full_upgrade::{HistoryTableDelegate, PlanPreview, UpgradeStatus};
use pages::image_download::DownloadTableDelegate;
use pages::partition::PartitionTableDelegate;
use state::TabKind;
use theme::{CONTENT_MARGIN, apply_fluent_theme};

pub(crate) struct AppView {
    tab: TabKind,
    dark: bool,
    window_handle: AnyWindowHandle,

    // 全局设备状态（设备页表格选中行 ↔ 下载页设备下拉双向同步）
    pub(crate) devices: Vec<DeviceInfo>,
    pub(crate) device_table: Entity<TableState<DeviceTableDelegate>>,
    pub(crate) selected_port: Option<String>,

    // 设备页
    pub(crate) detail: DeviceDetail,
    pub(crate) detail_busy: bool,

    // 完整升级页
    pub(crate) firmware_input: Entity<InputState>,
    pub(crate) verify_select: Entity<SelectState<component::searchable_list::SearchableVec<String>>>,
    pub(crate) reboot: bool,
    pub(crate) prepare_only: bool,
    pub(crate) plan: PlanPreview,
    pub(crate) upgrade: UpgradeStatus,
    pub(crate) cancellation: Option<Cancellation>,
    pub(crate) history_table: Entity<TableState<HistoryTableDelegate>>,

    // 下载镜像页
    pub(crate) download_device_select:
        Entity<SelectState<component::searchable_list::SearchableVec<String>>>,
    pub(crate) download_layout_select:
        Entity<SelectState<component::searchable_list::SearchableVec<String>>>,
    pub(crate) parameter_input: Entity<InputState>,
    pub(crate) image_dir_input: Entity<InputState>,
    pub(crate) download_table: Entity<TableState<DownloadTableDelegate>>,
    pub(crate) download_layout: Vec<zzrk_sys::PartitionEntry>,
    pub(crate) download_images: BTreeMap<String, String>,
    pub(crate) download_reboot: bool,
    pub(crate) download_busy: bool,
    pub(crate) download_cancellation: Option<Cancellation>,
    pub(crate) download_overall: (f32, bool),
    pub(crate) download_current: (f32, bool),
    pub(crate) download_status: String,

    // 分区页
    pub(crate) partition_table: Entity<TableState<PartitionTableDelegate>>,
    pub(crate) partition_verify: bool,
    pub(crate) partition_busy: bool,
    pub(crate) partition_cancellation: Option<Cancellation>,
    pub(crate) partition_progress: (f32, bool),
    pub(crate) partition_status: String,

    // 状态栏
    pub(crate) status_message: String,
    pub(crate) last_updated: Option<String>,
}

impl AppView {
    fn new(window: &mut Window, cx: &mut Context<Self>) -> Self {
        let window_handle = window.window_handle();

        let device_table = cx.new(|cx| {
            TableState::new(DeviceTableDelegate::new(), window, cx)
                .sortable(true)
                .row_selectable(true)
        });
        cx.subscribe(&device_table, |this, _table, event: &TableEvent, cx| {
            if let TableEvent::SelectRow(ix) = event {
                let port = this.devices.get(*ix).map(|d| d.port_path.clone());
                this.set_selected_port(port, cx);
            }
        })
        .detach();

        let history_table = cx.new(|cx| {
            TableState::new(HistoryTableDelegate::new(), window, cx).row_selectable(true)
        });
        let firmware_input = cx.new(|cx| {
            InputState::new(window, cx)
                .placeholder("选择 update.img")
                .default_value(crate::FIRMWARE_PATH)
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

        let download_device_select = cx.new(|cx| {
            SelectState::new(
                component::searchable_list::SearchableVec::new(Vec::<String>::new()),
                None,
                window,
                cx,
            )
        });
        let download_layout_select = cx.new(|cx| {
            SelectState::new(
                component::searchable_list::SearchableVec::new(vec![
                    "设备 GPT".to_string(),
                    "parameter.txt".to_string(),
                ]),
                Some(component::IndexPath::new(0)),
                window,
                cx,
            )
        });
        let parameter_input = cx.new(|cx| {
            InputState::new(window, cx).placeholder("设备 GPT 将在读取后显示，或选择 parameter.txt")
        });
        let image_dir_input =
            cx.new(|cx| InputState::new(window, cx).placeholder("选择镜像目录以自动匹配分区"));
        let download_table = cx.new(|cx| {
            TableState::new(DownloadTableDelegate::new(), window, cx).row_selectable(true)
        });
        // 让 delegate 能回写勾选状态
        let weak = download_table.downgrade();
        download_table.update(cx, |state, _cx| {
            state.delegate_mut().set_table_weak(weak);
        });
        let partition_table = cx.new(|cx| {
            TableState::new(PartitionTableDelegate::new(), window, cx)
                .sortable(true)
                .row_selectable(true)
        });

        let mut view = Self {
            tab: TabKind::Device,
            dark: false,
            window_handle,
            devices: Vec::new(),
            device_table,
            selected_port: None,
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
            download_device_select,
            download_layout_select,
            parameter_input,
            image_dir_input,
            download_table,
            download_layout: Vec::new(),
            download_images: BTreeMap::new(),
            download_reboot: true,
            download_busy: false,
            download_cancellation: None,
            download_overall: (0., false),
            download_current: (0., false),
            download_status: "请选择目标设备和分区布局".into(),
            partition_table,
            partition_verify: true,
            partition_busy: false,
            partition_cancellation: None,
            partition_progress: (0., false),
            partition_status: "尚未读取分区表".into(),
            status_message: "就绪".into(),
            last_updated: None,
        };
        view.subscribe_download_device_select(cx);
        cx.subscribe(&view.download_layout_select, |this, _s, event, cx| {
            if let SelectEvent::Confirm(Some(_)) = event {
                this.on_download_layout_changed(cx);
            }
        })
        .detach();
        view
    }

    // ------------------------- 全局选中设备同步 -------------------------

    pub(crate) fn selected_device(&self, _cx: &App) -> Option<DeviceInfo> {
        let port = self.selected_port.as_ref()?;
        self.devices.iter().find(|d| &d.port_path == port).cloned()
    }

    pub(crate) fn set_selected_port(&mut self, port: Option<String>, cx: &mut Context<Self>) {
        if self.selected_port == port {
            return;
        }
        self.selected_port = port;
        // 设备页表格
        let ix = self
            .selected_port
            .as_ref()
            .and_then(|p| self.devices.iter().position(|d| &d.port_path == p));
        self.device_table.update(cx, |table, cx| match ix {
            Some(ix) => table.set_selected_row(ix, cx),
            None => table.clear_selection(cx),
        });
        // 下载页下拉（set_selected_index 需要 window）
        let select = self.download_device_select.clone();
        self.window_handle
            .update(cx, move |_, window, cx| {
                select.update(cx, |select, cx| {
                    select.set_selected_index(ix.map(component::IndexPath::new), window, cx);
                });
            })
            .ok();
        cx.notify();
    }

    fn on_download_layout_changed(&mut self, cx: &mut Context<Self>) {
        // 切换布局来源时清空已加载布局与计划表，等待重新读取/解析。
        self.download_layout.clear();
        self.download_images.clear();
        self.download_table.update(cx, |table, cx| {
            table.delegate_mut().rows.clear();
            table.refresh(cx);
        });
        self.download_status = "请选择目标设备和分区布局".into();
        cx.notify();
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
            view.update_in(cx, |view, window, cx| {
                match result {
                    Ok(devices) => {
                        view.status_message = format!("扫描完成：{} 台设备", devices.len());
                        view.apply_scanned_devices(devices, window, cx);
                    }
                    Err(err) => view.status_message = format!("扫描失败：{err}"),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    // ------------------------- 骨架 -------------------------

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
            TabKind::Download => self.render_download_page(window, cx).into_any_element(),
            TabKind::Device => self.render_device_page(window, cx).into_any_element(),
            TabKind::Upgrade => self.render_upgrade_page(window, cx).into_any_element(),
            TabKind::Partition => self.render_partition_page(window, cx).into_any_element(),
        };

        // Root 自身只渲染内容；对话框/Sheet/通知层要由视图显式组合。
        let dialog_layer = Root::render_dialog_layer(window, cx);
        let sheet_layer = Root::render_sheet_layer(window, cx);
        let notification_layer = Root::render_notification_layer(window, cx);

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
            .children(dialog_layer)
            .children(sheet_layer)
            .children(notification_layer)
    }
}

pub(crate) const FIRMWARE_PATH: &str = concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../pd.core.CT1.rk3588s.20260727.161.img"
);

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
