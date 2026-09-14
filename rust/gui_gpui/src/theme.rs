// SPDX-License-Identifier: MIT
//! Fluent 主题令牌（对齐 Qt 版规格，亮/暗双套）。

use gpui_kit::component;
use gpui_kit::*;

pub(crate) const ACCENT: u32 = 0x0067C0;
pub(crate) const CONTENT_MARGIN: f32 = 10.;

pub(crate) fn apply_fluent_theme(dark: bool, window: Option<&mut Window>, cx: &mut App) {
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
