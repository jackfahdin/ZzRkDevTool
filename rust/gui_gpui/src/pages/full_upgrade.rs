// SPDX-License-Identifier: MIT
//! 完整升级页：固件路径 + 计划预览 + 校验/重启/prepare_only + 双进度条 +
//! 任务历史表。升级链路经 background_executor + mpsc + spawn_in 桥接。
//!
//! SAFETY: "开始升级"在真实设备上会复位设备进 maskrom（即使 prepare_only）。

use futures::StreamExt;
use gpui_kit::component::button::{Button, ButtonVariants as _};
use gpui_kit::component::checkbox::Checkbox;
use gpui_kit::component::input::Input;
use gpui_kit::component::notification::Notification;
use gpui_kit::component::progress::Progress;
use gpui_kit::component::select::Select;
use gpui_kit::component::table::{Column, DataTable, TableDelegate, TableState};
use gpui_kit::component::{ActiveTheme, Disableable as _, IconName, WindowExt as _, h_flex, v_flex};
use gpui_kit::*;
use zzrk_sys::{Cancellation, FirmwareInfo, UpgradeError, UpgradeOutcome};

use crate::state::{HistoryEntry, now_hms};
use crate::AppView;

pub(crate) struct HistoryTableDelegate {
    rows: Vec<HistoryEntry>,
    columns: Vec<Column>,
}

impl HistoryTableDelegate {
    pub(crate) fn new() -> Self {
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

pub(crate) enum PlanPreview {
    Empty,
    Loading,
    Ready(Box<FirmwareInfo>),
    Failed(String),
}

#[derive(Debug)]
pub(crate) enum UpgradeMsg {
    Progress(zzrk_sys::UpgradeProgressEvent),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

pub(crate) struct RunningUpgrade {
    pub(crate) current_completed: u64,
    pub(crate) current_total: u64,
    pub(crate) overall_fraction: f32,
    pub(crate) status: String,
}

pub(crate) enum UpgradeStatus {
    Idle,
    Running(RunningUpgrade),
    Done(Result<UpgradeOutcome, UpgradeError>),
}

impl AppView {
    pub(crate) fn browse_firmware(&mut self, window: &mut Window, cx: &mut Context<Self>) {
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

    pub(crate) fn inspect_plan(&mut self, window: &mut Window, cx: &mut Context<Self>) {
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
                                        Ok(o) => format!("成功（prepare_only={}）", o.prepared_only),
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

    pub(crate) fn render_upgrade_page(
        &mut self,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) -> impl IntoElement {
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
}
