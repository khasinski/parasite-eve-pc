//! Shared cards, badges and slot-value presentation.
use super::*;

/// Renders a full-width, bounded clickable card that never overflows its parent column or panel.
pub(super) fn interactive_card(
    ui: &mut egui::Ui,
    id_source: impl std::hash::Hash,
    is_selected: bool,
    accent_border: Option<Color32>,
    add_contents: impl FnOnce(&mut egui::Ui),
) -> egui::Response {
    let id = ui.make_persistent_id(id_source);
    let is_hovered = ui
        .ctx()
        .read_response(id)
        .map(|r| r.hovered())
        .unwrap_or(false);

    let fill = if is_selected {
        BG_CARD_SELECTED
    } else if is_hovered {
        BG_CARD_HOVER
    } else {
        BG_CARD
    };

    let border_color = if is_selected {
        accent_border.unwrap_or(BORDER_ACTIVE)
    } else if is_hovered {
        accent_border.unwrap_or(Color32::from_rgb(70, 130, 95))
    } else {
        accent_border
            .map(|c| c.gamma_multiply(0.45))
            .unwrap_or(BORDER_SUBTLE)
    };

    let stroke_width = if is_selected { 1.8_f32 } else { 1.0_f32 };
    let target_width = ui.available_width().max(60.0);
    let inner_width = (target_width - 20.0).max(40.0);

    let frame = egui::Frame::NONE
        .fill(fill)
        .stroke(Stroke::new(stroke_width, border_color))
        .corner_radius(CornerRadius::same(7))
        .inner_margin(Margin::symmetric(10, 7));

    let inner = frame.show(ui, |ui| {
        ui.set_min_width(inner_width);
        ui.set_max_width(inner_width);
        ui.style_mut().wrap_mode = Some(TextWrapMode::Truncate);
        add_contents(ui);
    });

    let resp = ui.interact(inner.response.rect, id, Sense::click());
    if resp.hovered() {
        ui.ctx().set_cursor_icon(CursorIcon::PointingHand);
    }
    resp
}

pub(super) fn badge(ui: &mut egui::Ui, text: &str, bg: Color32, fg: Color32) {
    egui::Frame::NONE
        .fill(bg)
        .corner_radius(CornerRadius::same(4))
        .inner_margin(Margin::symmetric(6, 2))
        .show(ui, |ui| {
            ui.label(RichText::new(text).color(fg).small().strong());
        });
}

pub(super) fn slot_value_icon_and_title(
    save: &PeSaveSlot,
    val: i16,
) -> (&'static str, String, String, Color32) {
    if val <= 0 {
        return (
            "·",
            "Empty Slot".to_string(),
            "Click to place an item or weapon".to_string(),
            TEXT_MUTED,
        );
    }
    if (0x100..0x180).contains(&val) {
        let eq_idx = (val - 0x100) as usize;
        if let Some(rec) = save.equipment.get(eq_idx) {
            if rec.is_active() {
                let eff = rec.effective_stats();
                let mods_count = (0..(rec.tail_count.min(10) as usize))
                    .filter(|&i| (rec.tail_data[i] & 0x1F) != 0)
                    .count();
                if rec.is_armor() {
                    let title = format!(
                        "{}{}",
                        rec.display_name(),
                        if rec.is_engraved() { " ★" } else { "" }
                    );
                    let sub = format!(
                        "Def {} • PE {} • Crit {}  (Mods {}/{})",
                        eff[0], eff[1], eff[2], mods_count, rec.tail_count
                    );
                    return ("🛡", title, sub, ACCENT_CYAN);
                } else {
                    let title = format!(
                        "{}{}",
                        rec.display_name(),
                        if rec.is_engraved() { " ★" } else { "" }
                    );
                    let sub = format!(
                        "Atk {} • Rng {} • Bul {} • Ammo {} (Mods {}/{})",
                        eff[0], eff[1], eff[2], rec.ammo, mods_count, rec.tail_count
                    );
                    return ("🔫", title, sub, ACCENT_GREEN);
                }
            }
        }
        return (
            "❓",
            format!("Unlinked Gear (E#{:02X})", val - 0x100),
            "Inactive equipment record".to_string(),
            ACCENT_AMBER,
        );
    }
    if (1..=0xFF).contains(&val) {
        let meta = item_metadata(val as u8);
        let (icon, color) = match meta.category {
            ItemCategory::Ammo | ItemCategory::Crate => ("弹", ACCENT_AMBER),
            ItemCategory::Consumable => ("💊", Color32::from_rgb(130, 235, 165)),
            ItemCategory::StatUpgrade | ItemCategory::Tool => ("🔧", ACCENT_CYAN),
            ItemCategory::KeyItem => ("🔑", ACCENT_AMBER),
            ItemCategory::TradingCard => ("🃏", Color32::from_rgb(215, 165, 250)),
            ItemCategory::ParasiteEnergy => ("🧬", ACCENT_GREEN),
            _ => ("📦", Color32::WHITE),
        };
        return (
            icon,
            meta.display_name.to_string(),
            meta.description.to_string(),
            color,
        );
    }
    if (0x200..=0x208).contains(&val) {
        let idx = (val - 0x200) as usize;
        let (name, sub) = match idx {
            0 => (
                "9mm Bullets Pool".to_string(),
                format!("Reserve: {} rounds", save.category_counts[0]),
            ),
            1 => (
                "Rockets Pool".to_string(),
                format!("Reserve: {} rockets", save.category_counts[1]),
            ),
            2 => (
                "DNA Bullets Pool".to_string(),
                format!("Reserve: {} rounds", save.category_counts[2]),
            ),
            _ => (
                format!("Special Reserve #{}", idx),
                format!("Raw code 0x{:03X}", val),
            ),
        };
        return ("弹", name, sub, ACCENT_AMBER);
    }
    (
        "📦",
        format!("Code 0x{:04X}", val as u16),
        "Special value".to_string(),
        TEXT_MUTED,
    )
}

pub(super) fn render_stack_controls(
    ui: &mut egui::Ui,
    save: &mut PeSaveSlot,
    list: usize,
    index: usize,
) {
    match save.stack_quantity(list, index) {
        Ok(0) => {}
        Ok(mut quantity) => {
            ui.horizontal(|ui| {
                ui.label(format!("Copies in this slot: {}", quantity));
                if matches!(crate::stacks::read(save), Ok(Some(_))) {
                    let before = quantity;
                    ui.add(egui::DragValue::new(&mut quantity).range(1..=99));
                    if quantity != before {
                        if let Err(e) = save.set_stack_quantity(list, index, quantity) {
                            ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                        }
                    }
                }
            });
            if list == 0
                && save
                    .item_list(0)
                    .get(index)
                    .is_some_and(|&v| (1..256).contains(&v))
                && ui
                    .button("Move whole stack to Wayne's item storage")
                    .clicked()
            {
                if let Err(e) = save.move_inventory_slot(0, index, 1) {
                    ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                }
            }
        }
        Err(e) => {
            ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
        }
    }
}
