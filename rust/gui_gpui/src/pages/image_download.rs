// SPDX-License-Identifier: MIT
//! 下载镜像页：设备/布局选择 + 计划表（9 列）+ 镜像目录自动匹配 +
//! 双进度条 + 取消。执行走 run_image_download FFI（本页在真实设备上不得
//! 触发执行——execute 会写闪存）。
//!
//! 镜像文件名匹配规则复刻 Qt 版 MainWorkspaceController：
//! - Loader ← miniloaderall.bin（小写精确匹配）
//! - 分区 ← 依次尝试 `<名>.img` / `<名>.bin` / `<名>`（小写比较）
//! - 未命中再用去槽位名（strip `_a`/`_b`）重复同样三个候选
//! - 手动指定镜像时若文件名基名等于去槽位名（不分大小写），同槽位分区
//!   （如 boot_a/boot_b）一起指派

use std::collections::BTreeMap;

use futures::StreamExt;
use gpui_kit::component::button::{Button, ButtonVariants as _};
use gpui_kit::component::checkbox::Checkbox;
use gpui_kit::component::input::Input;
use gpui_kit::component::progress::Progress;
use gpui_kit::component::select::{Select, SelectState};
use gpui_kit::component::table::{Column, DataTable, TableDelegate, TableState};
use gpui_kit::component::{ActiveTheme, Disableable as _, IconName, WindowExt as _, h_flex, v_flex};
use gpui_kit::*;
use zzrk_sys::{
    Cancellation, DownloadItemKind, DownloadLayout, DownloadPartition, PayloadFormat,
};

use crate::state::{data_size, slot_base_target};
use crate::AppView;

#[derive(Debug, Clone)]
pub(crate) struct DownloadPlanRow {
    pub(crate) kind: DownloadItemKind,
    pub(crate) target: String,
    pub(crate) lba: String,
    pub(crate) capacity: String,
    pub(crate) image_path: Option<String>,
    pub(crate) format: String,
    pub(crate) expanded: String,
    pub(crate) status: String,
    pub(crate) selected: bool,
    pub(crate) forced: bool,
    pub(crate) verify: bool,
}

pub(crate) struct DownloadTableDelegate {
    pub(crate) rows: Vec<DownloadPlanRow>,
    columns: Vec<Column>,
    table: Option<WeakEntity<TableState<Self>>>,
}

impl DownloadTableDelegate {
    pub(crate) fn new() -> Self {
        Self {
            rows: Vec::new(),
            columns: vec![
                Column::new("selected", "选择").width(px(70.)),
                Column::new("target", "目标").width(px(110.)),
                Column::new("lba", "起始 LBA").width(px(110.)),
                Column::new("capacity", "容量").width(px(100.)),
                Column::new("file", "镜像文件").sortable(),
                Column::new("format", "格式").width(px(130.)),
                Column::new("expanded", "展开大小").width(px(100.)),
                Column::new("verify", "校验").width(px(70.)),
                Column::new("status", "状态").width(px(140.)),
            ],
            table: None,
        }
    }

    pub(crate) fn set_table_weak(&mut self, table: WeakEntity<TableState<Self>>) {
        self.table = Some(table);
    }

    fn toggle(&mut self, row_ix: usize, column: usize, checked: bool) {
        let Some(row) = self.rows.get_mut(row_ix) else { return };
        if column == 0 && !row.forced {
            row.selected = checked;
        } else if column == 7 {
            row.verify = checked;
        }
    }
}

impl TableDelegate for DownloadTableDelegate {
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
        let Some(row) = self.rows.get(row_ix) else {
            return div().into_any_element();
        };
        if col_ix == 0 || col_ix == 7 {
            let checked = if col_ix == 0 { row.selected } else { row.verify };
            let weak = self.table.clone();
            return div()
                .child(
                    Checkbox::new(("dl-cell", row_ix * 16 + col_ix))
                        .checked(checked)
                        .disabled(col_ix == 0 && row.forced)
                        .on_click(move |checked, _window, cx| {
                            if let Some(weak) = &weak {
                                weak.update(cx, |state, cx| {
                                    state.delegate_mut().toggle(row_ix, col_ix, *checked);
                                    cx.notify();
                                })
                                .ok();
                            }
                        }),
                )
                .into_any_element();
        }
        let text: SharedString = match col_ix {
            1 => row.target.clone().into(),
            2 => row.lba.clone().into(),
            3 => row.capacity.clone().into(),
            4 => row
                .image_path
                .clone()
                .unwrap_or_else(|| "-".to_string())
                .into(),
            5 => row.format.clone().into(),
            6 => row.expanded.clone().into(),
            8 => row.status.clone().into(),
            _ => String::new().into(),
        };
        div().child(text).into_any_element()
    }
}

#[derive(Debug)]
pub(crate) enum DownloadMsg {
    Progress(zzrk_sys::DownloadProgressEvent),
    Done(Result<zzrk_sys::DownloadOutcome, zzrk_sys::DownloadError>),
}

fn download_stage_text(stage: Option<zzrk_sys::DownloadStage>) -> &'static str {
    use zzrk_sys::DownloadStage as S;
    match stage {
        Some(S::InspectFiles) => "检查镜像",
        Some(S::VerifyFiles) => "校验 MD5",
        Some(S::SelectDevice) => "选择设备",
        Some(S::EnterMaskrom) => "进入 Maskrom",
        Some(S::WaitMaskrom) => "等待 Maskrom",
        Some(S::DownloadLoader) => "下载 RAM Loader",
        Some(S::WaitRamLoader) => "等待 RAM Loader",
        Some(S::CheckRamLoader) => "检查 RAM Loader",
        Some(S::CheckStorage) => "检查 eMMC",
        Some(S::ValidateLayout) => "核对 GPT",
        Some(S::Erase) => "擦除",
        Some(S::Write) => "写入",
        Some(S::Verify) => "回读校验",
        Some(S::Reset) => "复位设备",
        None => "处理",
    }
}

impl AppView {
    pub(crate) fn download_layout_is_parameter(&self, cx: &App) -> bool {
        self.download_layout_select
            .read(cx)
            .selected_value()
            .map(String::as_str)
            == Some("parameter.txt")
    }

    /// 设备列表变化后重建设备下拉（SearchableVec 不支持整表替换）。
    pub(crate) fn rebuild_download_device_select(
        &mut self,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let items: Vec<String> = self
            .devices
            .iter()
            .map(|d| format!("{} ({})", d.port_path, crate::state::mode_label(d.mode)))
            .collect();
        let selected = self
            .selected_port
            .as_ref()
            .and_then(|p| self.devices.iter().position(|d| &d.port_path == p))
            .map(component::IndexPath::new);
        self.download_device_select = cx.new(|cx| {
            SelectState::new(
                component::searchable_list::SearchableVec::new(items),
                selected,
                window,
                cx,
            )
        });
        self.subscribe_download_device_select(cx);
    }

    // ------------------------- 布局加载 -------------------------

    pub(crate) fn choose_parameter_file(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let task = cx.prompt_for_paths(PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some("选择 parameter.txt".into()),
        });
        cx.spawn_in(window, async move |view, cx| {
            match task.await {
                Ok(Ok(Some(paths))) => {
                    if let Some(path) = paths.into_iter().next() {
                        let text = path.to_string_lossy().into_owned();
                        view.update_in(cx, |view, window, cx| {
                            view.parameter_input.update(cx, |input, cx| {
                                input.set_value(text.clone(), window, cx);
                            });
                            view.load_parameter_layout(text, window, cx);
                        })
                        .ok();
                    }
                }
                Ok(Ok(None)) | Err(_) => {}
                Ok(Err(err)) => {
                    view.update(cx, |view, cx| {
                        view.download_status =
                            format!("文件对话框不可用（{err}），请手工输入路径后再试");
                        cx.notify();
                    })
                    .ok();
                }
            }
        })
        .detach();
    }

    fn load_parameter_layout(&mut self, path: String, window: &mut Window, cx: &mut Context<Self>) {
        self.download_status = "正在解析 parameter.txt…".into();
        cx.notify();
        let task = cx.background_executor().spawn(async move {
            zzrk_sys::prepare_image_download(
                None,
                DownloadLayout::ParameterFile(std::path::Path::new(&path)),
                None,
                true,
                true,
                &[],
            )
        });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                match result {
                    Ok(prepared) => {
                        view.download_layout = prepared.layout;
                        view.download_status = format!(
                            "布局已加载：{} 个分区",
                            view.download_layout.len()
                        );
                    }
                    Err(e) => {
                        view.download_layout.clear();
                        view.download_status = format!("布局解析失败：{e}");
                    }
                }
                view.rebuild_download_rows(cx);
                view.rematch_download_images(cx);
            })
            .ok();
        })
        .detach();
    }

    pub(crate) fn read_device_gpt_for_download(
        &mut self,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let Some(port) = self.selected_port.clone() else {
            self.download_status = "请先选择目标设备".into();
            cx.notify();
            return;
        };
        self.download_status = "正在读取设备 GPT…".into();
        cx.notify();
        let task = cx
            .background_executor()
            .spawn(async move { zzrk_sys::read_partitions(&port) });
        cx.spawn_in(window, async move |view, cx| {
            let result = task.await;
            view.update(cx, |view, cx| {
                match result {
                    Ok((partitions, sectors)) => {
                        view.download_layout = partitions;
                        view.download_status = format!(
                            "已读取设备 GPT：{} 个分区（{} 扇区）",
                            view.download_layout.len(),
                            sectors
                        );
                    }
                    Err(e) => {
                        view.download_layout.clear();
                        view.download_status = format!("读取设备 GPT 失败：{e}");
                    }
                }
                view.rebuild_download_rows(cx);
                view.rematch_download_images(cx);
            })
            .ok();
        })
        .detach();
    }

    // ------------------------- 计划表构建与匹配 -------------------------

    fn rebuild_download_rows(&mut self, cx: &mut Context<Self>) {
        let parameter_layout = self.download_layout_is_parameter(cx);
        let mut rows = vec![DownloadPlanRow {
            kind: DownloadItemKind::Loader,
            target: "Loader".into(),
            lba: "64".into(),
            capacity: "-".into(),
            image_path: self.download_images.get("Loader").cloned(),
            format: "-".into(),
            expanded: "-".into(),
            status: if self.download_images.contains_key("Loader") {
                "就绪".into()
            } else {
                "未选择".into()
            },
            selected: self.download_images.contains_key("Loader"),
            forced: false,
            verify: true,
        }];
        if parameter_layout && !self.download_layout.is_empty() {
            let parameter = self
                .parameter_input
                .read(cx)
                .value()
                .to_string();
            rows.push(DownloadPlanRow {
                kind: DownloadItemKind::PartitionTable,
                target: "GPT".into(),
                lba: "主 / 备".into(),
                capacity: data_size(67 * 512),
                image_path: Some(parameter),
                format: PayloadFormat::RockchipParameter.name().into(),
                expanded: data_size(67 * 512),
                status: "就绪".into(),
                selected: true,
                forced: true,
                verify: true,
            });
        }
        for p in &self.download_layout {
            let image = self.download_images.get(&p.name).cloned();
            rows.push(DownloadPlanRow {
                kind: DownloadItemKind::Partition,
                target: p.name.clone(),
                lba: format!("{}", p.first_lba),
                capacity: if p.sector_count > 0 {
                    data_size(p.sector_count * 512)
                } else {
                    "-".into()
                },
                image_path: image.clone(),
                format: "-".into(),
                expanded: "-".into(),
                status: if image.is_some() { "就绪" } else { "未选择" }.into(),
                selected: image.is_some(),
                forced: false,
                verify: true,
            });
        }
        self.download_table.update(cx, |table, cx| {
            table.delegate_mut().rows = rows;
            table.refresh(cx);
        });
        cx.notify();
    }

    /// 镜像目录自动匹配（Qt matchDownloadDirectory 的 Rust 复刻）。
    pub(crate) fn rematch_download_images(&mut self, cx: &mut Context<Self>) {
        let dir = self.image_dir_input.read(cx).value().to_string();
        if dir.trim().is_empty() {
            return;
        }
        let Ok(read_dir) = std::fs::read_dir(&dir) else {
            self.download_status = "镜像目录不存在".into();
            cx.notify();
            return;
        };
        let mut files: BTreeMap<String, std::path::PathBuf> = BTreeMap::new();
        for entry in read_dir.flatten() {
            let path = entry.path();
            if path.is_file() {
                if let Some(name) = entry.file_name().to_str() {
                    files.insert(name.to_lowercase(), path);
                }
            }
        }
        let find_candidate = |stem: &str| -> Option<String> {
            for name in [
                format!("{stem}.img"),
                format!("{stem}.bin"),
                stem.to_string(),
            ] {
                if let Some(path) = files.get(&name.to_lowercase()) {
                    return Some(path.to_string_lossy().into_owned());
                }
            }
            None
        };
        if let Some(loader) = files.get("miniloaderall.bin") {
            self.download_images
                .insert("Loader".into(), loader.to_string_lossy().into_owned());
        }
        let layout = self.download_layout.clone();
        for partition in &layout {
            let image = find_candidate(&partition.name)
                .or_else(|| find_candidate(slot_base_target(&partition.name)));
            if let Some(image) = image {
                self.download_images.insert(partition.name.clone(), image);
            }
        }
        let matched = self.download_images.len();
        self.download_status = format!("已匹配 {matched} 个可下载目标");
        self.rebuild_download_rows(cx);
        cx.notify();
    }

    pub(crate) fn choose_image_directory(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let task = cx.prompt_for_paths(PathPromptOptions {
            files: false,
            directories: true,
            multiple: false,
            prompt: Some("选择镜像目录".into()),
        });
        cx.spawn_in(window, async move |view, cx| {
            match task.await {
                Ok(Ok(Some(paths))) => {
                    if let Some(path) = paths.into_iter().next() {
                        let text = path.to_string_lossy().into_owned();
                        view.update_in(cx, |view, window, cx| {
                            view.image_dir_input.update(cx, |input, cx| {
                                input.set_value(text, window, cx);
                            });
                            view.rematch_download_images(cx);
                        })
                        .ok();
                    }
                }
                Ok(Ok(None)) | Err(_) => {}
                Ok(Err(err)) => {
                    view.update(cx, |view, cx| {
                        view.download_status = format!("目录对话框不可用（{err}）");
                        cx.notify();
                    })
                    .ok();
                }
            }
        })
        .detach();
    }

    /// 手工为选中行指派镜像（含槽位配对）。
    pub(crate) fn choose_image_for_row(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let Some(row_ix) = self.download_table.read(cx).selected_row() else {
            self.download_status = "请先在计划表中选择目标行".into();
            cx.notify();
            return;
        };
        let Some(row) = self
            .download_table
            .read(cx)
            .delegate()
            .rows
            .get(row_ix)
            .cloned()
        else {
            return;
        };
        if row.kind == DownloadItemKind::PartitionTable {
            return;
        }
        let task = cx.prompt_for_paths(PathPromptOptions {
            files: true,
            directories: false,
            multiple: false,
            prompt: Some(
                if row.kind == DownloadItemKind::Loader {
                    "选择 MiniLoaderAll.bin"
                } else {
                    "选择分区镜像"
                }
                .into(),
            ),
        });
        cx.spawn_in(window, async move |view, cx| {
            if let Ok(Ok(Some(paths))) = task.await {
                if let Some(path) = paths.into_iter().next() {
                    let text = path.to_string_lossy().into_owned();
                    view.update(cx, |view, cx| {
                        view.assign_image(&row.target, &text);
                        view.rebuild_download_rows(cx);
                        cx.notify();
                    })
                    .ok();
                }
            }
        })
        .detach();
    }

    fn assign_image(&mut self, target: &str, path: &str) {
        // Qt assignDownloadImage(applySlotPair=true)：文件名基名等于去槽位名
        // 时把同槽位的所有分区一起指派。
        let base = slot_base_target(target).to_string();
        let file_stem = std::path::Path::new(path)
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_lowercase();
        let mut targets = vec![target.to_string()];
        if target != "Loader" && base != target && file_stem == base.to_lowercase() {
            targets = self
                .download_layout
                .iter()
                .filter(|p| slot_base_target(&p.name) == base)
                .map(|p| p.name.clone())
                .collect();
        }
        for t in targets {
            self.download_images.insert(t, path.to_string());
        }
    }

    pub(crate) fn clear_image_for_row(&mut self, cx: &mut Context<Self>) {
        let Some(row_ix) = self.download_table.read(cx).selected_row() else {
            return;
        };
        let Some(row) = self
            .download_table
            .read(cx)
            .delegate()
            .rows
            .get(row_ix)
            .cloned()
        else {
            return;
        };
        if row.kind == DownloadItemKind::PartitionTable {
            return;
        }
        self.download_images.remove(&row.target);
        self.rebuild_download_rows(cx);
        cx.notify();
    }

    // ------------------------- 执行 -------------------------

    pub(crate) fn request_start_download(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.download_busy {
            return;
        }
        if self.selected_port.is_none() {
            self.download_status = "请先选择目标设备".into();
            cx.notify();
            return;
        }
        let view = cx.entity().downgrade();
        window.open_alert_dialog(cx, move |alert, _window, _cx| {
            let view = view.clone();
            alert
                .confirm()
                .title("开始下载镜像？")
                .description("将擦除并写入设备闪存，此操作不可撤销。")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    view.update(cx, |view, cx| view.start_download(window, cx))
                        .ok();
                    true
                })
        });
        cx.notify();
    }

    fn start_download(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.download_busy {
            return;
        }
        let Some(port_path) = self.selected_port.clone() else {
            return;
        };
        let rows = self.download_table.read(cx).delegate().rows.clone();
        let mut loader_path: Option<std::path::PathBuf> = None;
        let mut verify_loader = true;
        let mut verify_partition_table = true;
        let mut partitions: Vec<(String, String, bool)> = Vec::new();
        for row in &rows {
            if !row.selected {
                continue;
            }
            match row.kind {
                DownloadItemKind::Loader => {
                    if let Some(p) = &row.image_path {
                        loader_path = Some(std::path::PathBuf::from(p));
                        verify_loader = row.verify;
                    }
                }
                DownloadItemKind::PartitionTable => verify_partition_table = row.verify,
                DownloadItemKind::Partition => {
                    if let Some(p) = &row.image_path {
                        partitions.push((row.target.clone(), p.clone(), row.verify));
                    }
                }
            }
        }
        if partitions.is_empty() && loader_path.is_none() {
            self.download_status = "没有可执行的下载目标".into();
            cx.notify();
            return;
        }

        let parameter_layout = self.download_layout_is_parameter(cx);
        let parameter_path = self.parameter_input.read(cx).value().to_string();

        let token = Cancellation::new();
        self.download_cancellation = Some(token.clone());
        self.download_busy = true;
        self.download_overall = (0., false);
        self.download_current = (0., true);
        self.download_status = "正在启动下载…".into();
        self.download_table.update(cx, |table, cx| {
            for row in &mut table.delegate_mut().rows {
                if row.selected {
                    row.status = "等待".into();
                }
            }
            table.refresh(cx);
        });
        cx.notify();

        let (tx, mut rx) = futures::channel::mpsc::unbounded::<DownloadMsg>();
        let tx_progress = tx.clone();
        cx.background_executor()
            .spawn(async move {
                let parts: Vec<DownloadPartition> = partitions
                    .iter()
                    .map(|(t, p, v)| DownloadPartition {
                        target: t,
                        image_path: std::path::Path::new(p),
                        verify: *v,
                    })
                    .collect();
                let layout = if parameter_layout {
                    DownloadLayout::ParameterFile(std::path::Path::new(&parameter_path))
                } else {
                    DownloadLayout::DeviceGpt
                };
                let result = zzrk_sys::run_image_download(
                    &port_path,
                    layout,
                    loader_path.as_deref(),
                    verify_loader,
                    verify_partition_table,
                    &parts,
                    true,
                    move |event| {
                        let _ = tx_progress.unbounded_send(DownloadMsg::Progress(event));
                    },
                    Some(&token),
                );
                let _ = tx.unbounded_send(DownloadMsg::Done(result));
            })
            .detach();

        cx.spawn_in(window, async move |view, cx| {
            while let Some(msg) = rx.next().await {
                let keep_going = view
                    .update(cx, |view, cx| {
                        match msg {
                            DownloadMsg::Progress(event) => {
                                let overall = if event.total_plan_bytes > 0 {
                                    event.completed_plan_bytes as f32 * 100.
                                        / event.total_plan_bytes as f32
                                } else {
                                    0.
                                };
                                let indeterminate = event.total_bytes == 0;
                                let current = if indeterminate {
                                    0.
                                } else {
                                    event.completed_bytes as f32 * 100.
                                        / event.total_bytes as f32
                                };
                                view.download_overall = (overall, event.total_plan_bytes == 0);
                                view.download_current = (current, indeterminate);
                                view.download_status = format!(
                                    "{}：{} ({}/{})",
                                    download_stage_text(event.stage()),
                                    event.target.clone().unwrap_or_default(),
                                    event.completed_bytes,
                                    event.total_bytes,
                                );
                                // 逐目标更新状态列
                                if let Some(target) = &event.target {
                                    let label = download_stage_text(event.stage());
                                    view.download_table.update(cx, |table, cx| {
                                        for row in &mut table.delegate_mut().rows {
                                            if &row.target == target {
                                                row.status = label.into();
                                            }
                                        }
                                        table.refresh(cx);
                                    });
                                }
                            }
                            DownloadMsg::Done(result) => {
                                let ok = result.is_ok();
                                let message = match &result {
                                    Ok(_) => "下载完成".to_string(),
                                    Err(e) => format!("下载失败：{}", e.message),
                                };
                                view.download_table.update(cx, |table, cx| {
                                    for row in &mut table.delegate_mut().rows {
                                        if row.selected {
                                            row.status =
                                                if ok { "完成" } else { "失败" }.into();
                                        }
                                    }
                                    table.refresh(cx);
                                });
                                view.download_status = message;
                                view.download_busy = false;
                                view.download_cancellation = None;
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

    pub(crate) fn cancel_download(&mut self, cx: &mut Context<Self>) {
        if let Some(token) = &self.download_cancellation {
            token.cancel();
            self.download_status = "正在取消…".into();
            cx.notify();
        }
    }
}

impl AppView {
    /// 订阅设备下拉变更（重建 select 后需重新订阅）。
    pub(crate) fn subscribe_download_device_select(&mut self, cx: &mut Context<Self>) {
        cx.subscribe(
            &self.download_device_select.clone(),
            |this, _state, event: &component::select::SelectEvent<_>, cx| {
                if let component::select::SelectEvent::Confirm(Some(value)) = event {
                    let port = value.split(" (").next().unwrap_or(value).to_string();
                    this.set_selected_port(Some(port), cx);
                }
            },
        )
        .detach();
    }

    pub(crate) fn render_download_page(
        &mut self,
        _window: &mut Window,
        cx: &mut Context<Self>,
    ) -> impl IntoElement {
        let theme = cx.theme().clone();
        let busy = self.download_busy;
        let parameter_layout = self.download_layout_is_parameter(cx);
        let has_device = self.selected_port.is_some();

        v_flex()
            .size_full()
            .gap_3()
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(70.)).child("目标设备"))
                    .child(
                        div()
                            .w(px(200.))
                            .child(Select::new(&self.download_device_select)),
                    )
                    .child(div().w(px(70.)).child("分区布局"))
                    .child(
                        div()
                            .w(px(160.))
                            .child(Select::new(&self.download_layout_select)),
                    )
                    .child(
                        Button::new("read-gpt")
                            .outline()
                            .icon(IconName::RotateCw)
                            .label("读GPT")
                            .disabled(busy || parameter_layout || !has_device)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.read_device_gpt_for_download(window, cx);
                            })),
                    ),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(70.)).child("布局文件"))
                    .child(
                        div().flex_1().child(
                            Input::new(&self.parameter_input).disabled(busy),
                        ),
                    )
                    .child(
                        Button::new("choose-parameter")
                            .outline()
                            .icon(IconName::FolderOpen)
                            .label("浏览")
                            .disabled(busy || !parameter_layout)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.choose_parameter_file(window, cx);
                            })),
                    )
                    // portal 在本环境悬挂：手工输入路径后用“解析”触发加载
                    .child(
                        Button::new("parse-parameter")
                            .ghost()
                            .label("解析")
                            .disabled(busy || !parameter_layout)
                            .on_click(cx.listener(|view, _, window, cx| {
                                let path = view.parameter_input.read(cx).value().to_string();
                                view.load_parameter_layout(path, window, cx);
                            })),
                    ),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(div().w(px(70.)).child("镜像目录"))
                    .child(
                        div().flex_1().child(
                            Input::new(&self.image_dir_input).disabled(busy),
                        ),
                    )
                    .child(
                        Button::new("choose-dir")
                            .outline()
                            .icon(IconName::FolderOpen)
                            .label("浏览")
                            .disabled(busy)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.choose_image_directory(window, cx);
                            })),
                    )
                    // portal 兜底：手工输入目录后用“匹配”触发自动匹配
                    .child(
                        Button::new("match-dir")
                            .ghost()
                            .label("匹配")
                            .disabled(busy)
                            .on_click(cx.listener(|view, _, _, cx| {
                                view.rematch_download_images(cx);
                            })),
                    ),
            )
            .child(
                div()
                    .flex_1()
                    .min_h(px(180.))
                    .child(DataTable::new(&self.download_table).stripe(true).bordered(false)),
            )
            .child(
                h_flex()
                    .gap_2()
                    .child(
                        Button::new("choose-image")
                            .outline()
                            .icon(IconName::FolderOpen)
                            .label("选择镜像")
                            .disabled(busy)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.choose_image_for_row(window, cx);
                            })),
                    )
                    .child(
                        Button::new("clear-image")
                            .outline()
                            .icon(IconName::CircleX)
                            .label("清除")
                            .disabled(busy)
                            .on_click(cx.listener(|view, _, _, cx| {
                                view.clear_image_for_row(cx);
                            })),
                    )
                    .child(
                        Checkbox::new("download-reboot")
                            .label("完成后重启")
                            .checked(self.download_reboot)
                            .on_click(cx.listener(|view, checked, _, cx| {
                                view.download_reboot = *checked;
                                cx.notify();
                            })),
                    )
                    .child(div().flex_1())
                    .child(
                        Button::new("start-download")
                            .primary()
                            .icon(IconName::Play)
                            .label("开始下载")
                            .disabled(busy)
                            .on_click(cx.listener(|view, _, window, cx| {
                                view.request_start_download(window, cx);
                            })),
                    )
                    .child(
                        Button::new("cancel-download")
                            .outline()
                            .icon(IconName::Close)
                            .label("取消")
                            .disabled(!busy)
                            .on_click(cx.listener(|view, _, _, cx| {
                                view.cancel_download(cx);
                            })),
                    ),
            )
            .child(
                v_flex()
                    .gap_2()
                    .child(
                        h_flex()
                            .gap_2()
                            .child(div().w(px(70.)).child("完整计划"))
                            .child(
                                div().flex_1().child(
                                    Progress::new("download-overall")
                                        .value(self.download_overall.0),
                                ),
                            ),
                    )
                    .child(
                        h_flex()
                            .gap_2()
                            .child(div().w(px(70.)).child("当前分区"))
                            .child(
                                div().flex_1().child(
                                    Progress::new("download-current")
                                        .loading(self.download_current.1)
                                        .value(self.download_current.0),
                                ),
                            ),
                    ),
            )
            .child(
                div()
                    .text_sm()
                    .text_color(theme.muted_foreground)
                    .child(self.download_status.clone()),
            )
    }
}
