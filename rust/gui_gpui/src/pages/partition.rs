// SPDX-License-Identifier: MIT
//! 分区页：GPT 分区表（6 列可排序）+ 备份/写入/擦除 + 进度与取消。
//!
//! SAFETY: 写入/擦除会修改设备闪存；本机验证不得对真实设备触发。

use futures::StreamExt;
use gpui_kit::component::button::Button;
use gpui_kit::component::checkbox::Checkbox;
use gpui_kit::component::progress::Progress;
use gpui_kit::component::table::{Column, ColumnSort, DataTable, TableDelegate, TableState};
use gpui_kit::component::{ActiveTheme, Disableable as _, IconName, WindowExt as _, h_flex, v_flex};
use gpui_kit::*;
use zzrk_sys::{Cancellation, DeviceMode, PartitionEntry};

use crate::state::data_size;
use crate::AppView;

pub(crate) struct PartitionTableDelegate {
    pub(crate) rows: Vec<PartitionEntry>,
    columns: Vec<Column>,
}

impl PartitionTableDelegate {
    pub(crate) fn new() -> Self {
        Self {
            rows: Vec::new(),
            columns: vec![
                Column::new("name", "分区名").width(px(180.)).sortable(),
                Column::new("first", "起始 LBA").width(px(130.)).sortable(),
                Column::new("last", "结束 LBA").width(px(130.)).sortable(),
                Column::new("sectors", "扇区数").width(px(120.)).sortable(),
                Column::new("capacity", "容量").width(px(110.)).sortable(),
                Column::new("attrs", "属性").sortable(),
            ],
        }
    }
}

impl TableDelegate for PartitionTableDelegate {
    fn columns_count(&self, _cx: &App) -> usize {
        self.columns.len()
    }

    fn rows_count(&self, _cx: &App) -> usize {
        self.rows.len()
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
        self.rows.sort_by(|a, b| {
            let ord = match col_ix {
                0 => a.name.cmp(&b.name),
                1 => a.first_lba.cmp(&b.first_lba),
                2 => (a.first_lba + a.sector_count).cmp(&(b.first_lba + b.sector_count)),
                3 => a.sector_count.cmp(&b.sector_count),
                4 => (a.sector_count * 512).cmp(&(b.sector_count * 512)),
                5 => a.attributes.cmp(&b.attributes),
                _ => std::cmp::Ordering::Equal,
            };
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
        let Some(p) = self.rows.get(row_ix) else {
            return div().into_any_element();
        };
        let text: SharedString = match col_ix {
            0 => p.name.clone().into(),
            1 => format!("{}", p.first_lba).into(),
            2 => format!("{}", p.first_lba + p.sector_count.saturating_sub(1)).into(),
            3 => format!("{}", p.sector_count).into(),
            4 => data_size(p.sector_count * 512).into(),
            5 => format!("0x{:016X}", p.attributes).into(),
            _ => String::new().into(),
        };
        div().child(text).into_any_element()
    }
}

#[derive(Debug)]
pub(crate) enum PartitionMsg {
    Progress(zzrk_sys::PartitionProgressEvent),
    Done(Result<zzrk_sys::PartitionOutcome, zzrk_sys::DeviceOperationError>),
}

impl AppView {
    /// 分区页目标设备：全局选中的 Loader 设备。
    fn partition_target_port(&self, _cx: &App) -> Option<(String, DeviceMode)> {
        let port = self.selected_port.as_ref()?;
        let device = self.devices.iter().find(|d| &d.port_path == port)?;
        Some((device.port_path.clone(), device.mode))
    }

    pub(crate) fn reload_partitions(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.partition_busy {
            return;
        }
        let Some((port, _mode)) = self.partition_target_port(cx) else {
            self.partition_status = "未选择设备（请在设备页或下载页选择）".into();
            cx.notify();
            return;
        };
        self.partition_status = "正在读取分区表…".into();
        cx.notify();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::read_partitions(&port) });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                match result {
                    Ok((partitions, sectors)) => {
                        view.partition_status =
                            format!("已读取 {} 个分区（设备 {} 扇区）", partitions.len(), sectors);
                        view.partition_table.update(cx, |table, cx| {
                            table.delegate_mut().rows = partitions;
                            table.refresh(cx);
                        });
                    }
                    Err(e) => {
                        view.partition_status = format!("读取分区表失败：{e}");
                    }
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
    }

    fn selected_partition(&self, cx: &App) -> Option<PartitionEntry> {
        let ix = self.partition_table.read(cx).selected_row()?;
        self.partition_table.read(cx).delegate().rows.get(ix).cloned()
    }

    /// 备份分区：选择输出文件后直接读（FFI 拒绝覆盖已存在文件）。
    pub(crate) fn backup_selected_partition(
        &mut self,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        if self.partition_busy {
            return;
        }
        let Some(partition) = self.selected_partition(cx) else {
            self.partition_status = "请先选择分区".into();
            cx.notify();
            return;
        };
        let Some((port, _)) = self.partition_target_port(cx) else {
            return;
        };
        let suggested = format!("{}.img", partition.name);
        let task = cx.prompt_for_new_path(std::path::Path::new("."), Some(&suggested));
        cx.spawn_in(window, async move |view, cx| {
            match task.await {
                Ok(Ok(Some(path))) => {
                    view.update_in(cx, |view, window, cx| {
                        view.run_partition_op(
                            format!("备份分区 {}", partition.name),
                            window,
                            cx,
                            move |progress, token| {
                                zzrk_sys::read_partition(
                                    &port,
                                    &partition.name,
                                    &path,
                                    progress,
                                    token,
                                )
                            },
                        );
                    })
                    .ok();
                }
                Ok(Ok(None)) => {}
                _ => {
                    view.update(cx, |view, cx| {
                        view.partition_status = "文件对话框不可用，无法选择输出路径".into();
                        cx.notify();
                    })
                    .ok();
                }
            }
        })
        .detach();
    }

    /// 写入镜像：浏览输入文件 → 确认对话框 → write_partition。
    pub(crate) fn write_selected_partition(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.partition_busy {
            return;
        }
        let Some(partition) = self.selected_partition(cx) else {
            self.partition_status = "请先选择分区".into();
            cx.notify();
            return;
        };
        let Some((port, _)) = self.partition_target_port(cx) else {
            return;
        };
        let verify = self.partition_verify;
        let part_name = partition.name.clone();
        let part_first = partition.first_lba;
        let part_sectors = partition.sector_count;
        let task = cx.prompt_for_paths(PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some("选择分区镜像".into()),
        });
        let view = cx.entity().downgrade();
        cx.spawn_in(window, async move |cx_unused, acx| {
            let _ = cx_unused;
            if let Ok(Ok(Some(paths))) = task.await {
                let Some(path) = paths.into_iter().next() else { return };
                let image = path.to_string_lossy().into_owned();
                let image_name = std::path::Path::new(&image)
                    .file_name()
                    .and_then(|n| n.to_str())
                    .unwrap_or(&image)
                    .to_string();
                let description = format!(
                    "端口 {port}，分区 {part_name}，LBA {}..{}，容量 {}，镜像 {image_name}",
                    part_first,
                    part_first + part_sectors.saturating_sub(1),
                    data_size(part_sectors * 512),
                );
                view.update_in(acx, move |_view: &mut AppView, window, cx| {
                    let view2 = cx.entity().downgrade();
                    window.open_alert_dialog(cx, move |alert, _window, _cx| {
                        let view2 = view2.clone();
                        let image = image.clone();
                        let port = port.clone();
                        let name = part_name.clone();
                        alert
                            .confirm()
                            .title(format!("写入分区 {name}？"))
                            .description(description.clone())
                            .show_cancel(true)
                            .on_ok(move |_, window, cx| {
                                let image = image.clone();
                                let port = port.clone();
                                let name = name.clone();
                                view2
                                    .update(cx, |view, cx| {
                                        view.run_partition_op(
                                            "写入分区".to_string(),
                                            window,
                                            cx,
                                            move |progress, token| {
                                                zzrk_sys::write_partition(
                                                    &port, &name, &image, verify,
                                                    progress, token,
                                                )
                                            },
                                        );
                                    })
                                    .ok();
                                true
                            })
                    });
                })
                .ok();
            }
        })
        .detach();
    }

    /// 擦除分区：确认对话框 → erase_partition。
    pub(crate) fn erase_selected_partition(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.partition_busy {
            return;
        }
        let Some(partition) = self.selected_partition(cx) else {
            self.partition_status = "请先选择分区".into();
            cx.notify();
            return;
        };
        let Some((port, _)) = self.partition_target_port(cx) else {
            return;
        };
        let view = cx.entity().downgrade();
        let part_name = partition.name.clone();
        let description = format!(
            "端口 {port}，分区 {part_name}，LBA {}..{}，容量 {}",
            partition.first_lba,
            partition.first_lba + partition.sector_count.saturating_sub(1),
            data_size(partition.sector_count * 512),
        );
        window.open_alert_dialog(cx, move |alert, _window, _cx| {
            let view = view.clone();
            let port = port.clone();
            let name = part_name.clone();
            alert
                .confirm()
                .title(format!("擦除分区 {name}？"))
                .description(description.clone())
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let port = port.clone();
                    let name = name.clone();
                    view.update(cx, |view, cx| {
                        view.run_partition_op("擦除分区".to_string(), window, cx, {
                            move |progress, token| {
                                zzrk_sys::erase_partition(&port, &name, progress, token)
                            }
                        });
                    })
                    .ok();
                    true
                })
        });
    }

    fn run_partition_op(
        &mut self,
        label: String,
        window: &mut Window,
        cx: &mut Context<Self>,
        op: impl FnOnce(
                &mut dyn FnMut(zzrk_sys::PartitionProgressEvent),
                Option<&Cancellation>,
            ) -> Result<zzrk_sys::PartitionOutcome, zzrk_sys::DeviceOperationError>
            + Send
            + 'static,
    ) {
        if self.partition_busy {
            return;
        }
        let token = Cancellation::new();
        self.partition_cancellation = Some(token.clone());
        self.partition_busy = true;
        self.partition_progress = (0., true);
        self.partition_status = format!("{label}进行中…");
        cx.notify();

        let (tx, mut rx) = futures::channel::mpsc::unbounded::<PartitionMsg>();
        let tx_progress = tx.clone();
        cx.background_executor()
            .spawn(async move {
                let result = op(
                    &mut move |event| {
                        let _ = tx_progress.unbounded_send(PartitionMsg::Progress(event));
                    },
                    Some(&token),
                );
                let _ = tx.unbounded_send(PartitionMsg::Done(result));
            })
            .detach();

        cx.spawn_in(window, async move |view, cx| {
            while let Some(msg) = rx.next().await {
                let keep_going = view
                    .update(cx, |view, cx| {
                        match msg {
                            PartitionMsg::Progress(event) => {
                                let indeterminate = event.total_bytes == 0;
                                view.partition_progress = (
                                    if indeterminate {
                                        0.
                                    } else {
                                        event.completed_bytes as f32 * 100.
                                            / event.total_bytes as f32
                                    },
                                    indeterminate,
                                );
                                view.partition_status = format!(
                                    "{}：{}/{} 字节",
                                    event.partition,
                                    event.completed_bytes,
                                    event.total_bytes,
                                );
                            }
                            PartitionMsg::Done(result) => {
                                view.partition_status = match &result {
                                    Ok(o) => format!(
                                        "操作完成：{} 字节{}",
                                        o.bytes_processed,
                                        if o.verified { "，已校验" } else { "" }
                                    ),
                                    Err(e) => format!("操作失败：{}", e.message),
                                };
                                view.partition_busy = false;
                                view.partition_cancellation = None;
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

    pub(crate) fn cancel_partition_op(&mut self, cx: &mut Context<Self>) {
        if let Some(token) = &self.partition_cancellation {
            token.cancel();
            self.partition_status = "正在取消…".into();
            cx.notify();
        }
    }

    pub(crate) fn render_partition_page(
        &mut self,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) -> impl IntoElement {
        let theme = cx.theme().clone();
        let busy = self.partition_busy;
        let target = self.partition_target_port(cx);
        let loader_device = matches!(target, Some((_, DeviceMode::Loader)));
        let has_selection = self.partition_table.read(cx).selected_row().is_some();

        let target_text = match &target {
            Some((port, mode)) => format!("目标设备：{port}（{}）", crate::state::mode_label(*mode)),
            None => "目标设备：未选择（请在设备页或下载页选择）".to_string(),
        };

        v_flex()
            .size_full()
            .gap_3()
            .child(
                h_flex()
                    .gap_2()
                    .child(
                        Button::new("reload-gpt")
                            .outline()
                            .icon(IconName::RotateCw)
                            .label("重新读取 GPT")
                            .disabled(busy || target.is_none())
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.reload_partitions(window, cx);
                            })),
                    )
                    .child(
                        div()
                            .text_sm()
                            .text_color(theme.muted_foreground)
                            .child(target_text),
                    ),
            )
            .child(
                div()
                    .flex_1()
                    .min_h(px(200.))
                    .child(DataTable::new(&self.partition_table).stripe(true).bordered(false)),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(
                        Checkbox::new("partition-verify")
                            .label("写入后逐块回读校验")
                            .checked(self.partition_verify)
                            .on_click(cx.listener(|view, checked, _, cx| {
                                view.partition_verify = *checked;
                                cx.notify();
                            })),
                    )
                    .child(div().flex_1())
                    .child(
                        Button::new("backup-partition")
                            .outline()
                            .icon(IconName::FileText)
                            .label("备份分区")
                            .disabled(busy || !loader_device || !has_selection)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.backup_selected_partition(window, cx);
                            })),
                    )
                    .child(
                        Button::new("write-partition")
                            .outline()
                            .icon(IconName::ArrowDown)
                            .label("写入镜像")
                            .disabled(busy || !loader_device || !has_selection)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.write_selected_partition(window, cx);
                            })),
                    )
                    .child(
                        Button::new("erase-partition")
                            .outline()
                            .icon(IconName::Delete)
                            .label("擦除分区")
                            .disabled(busy || !loader_device || !has_selection)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.erase_selected_partition(window, cx);
                            })),
                    )
                    .child(
                        Button::new("cancel-partition")
                            .outline()
                            .icon(IconName::Close)
                            .label("取消")
                            .disabled(!busy)
                            .on_click(cx.listener(|view, _, _, cx| {
                                view.cancel_partition_op(cx);
                            })),
                    ),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(70.)).child("当前操作"))
                    .child(
                        div().flex_1().child(
                            Progress::new("partition-op")
                                .loading(self.partition_progress.1)
                                .value(self.partition_progress.0),
                        ),
                    ),
            )
            .child(
                div()
                    .text_sm()
                    .text_color(theme.muted_foreground)
                    .child(self.partition_status.clone()),
            )
    }
}
