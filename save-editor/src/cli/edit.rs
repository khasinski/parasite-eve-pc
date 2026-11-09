//! Edit commands.
use super::*;

fn parse_u8_hex_or_dec(s: &str) -> Result<u8> {
    let trimmed = s.trim();
    if let Some(hex) = trimmed
        .strip_prefix("0x")
        .or_else(|| trimmed.strip_prefix("0X"))
    {
        Ok(u8::from_str_radix(hex, 16)?)
    } else {
        Ok(trimmed.parse::<u8>()?)
    }
}

pub(super) fn cmd_edit(args: EditArgs) -> Result<()> {
    let game = match &args.disc {
        Some(disc) => Some(crate::disc::load_disc(disc)?),
        None => crate::disc::load_configured_disc(),
    };
    match &game {
        Some(g) => println!("Game data: {}", g.source.display()),
        None if !args.add_item.is_empty() || args.ultimate_loadout => println!(
            "Using verified USA equipment templates. Use --disc for exact HP growth calculations."
        ),
        None => {}
    }

    let mut container = MemoryCardContainer::open(&args.path)?;
    let idx = resolve_slot_index(&container, args.slot)?;
    let save = container.slots[idx].as_mut().unwrap();

    if args.repair_equipment_types {
        println!(
            "Repaired {} equipment icon/model classes",
            save.repair_equipment_types()
        );
    }
    if args.max_aya {
        save.apply_cheat_max_aya();
    }
    if args.unlock_all_pe {
        save.apply_cheat_unlock_all_pe();
    }
    if args.max_ammo {
        save.apply_cheat_max_ammo();
    }
    if args.max_gear {
        save.apply_cheat_max_equipped_gear();
    }
    if args.ultimate_loadout {
        save.apply_cheat_give_ultimate_loadout()?;
    }

    if let Some(lv) = args.level {
        save.set_display_level(lv);
    }
    if let Some(max_hp) = args.max_hp {
        save.max_hp = max_hp.clamp(1, 999);
    }
    if let Some(hp) = args.hp {
        save.current_hp = hp.clamp(1, 999);
    }
    if let Some(exp) = args.exp {
        save.total_exp = exp;
    }
    if let Some(bp) = args.bp {
        save.bonus_points = bp.min(99_999);
    }
    if let Some(slots) = args.slots {
        save.inventory_slot_count = slots;
    }
    if let Some(day) = args.day {
        save.play_time_class = day.clamp(1, 6);
        save.current_story_day = day.clamp(1, 8) as i8;
    }
    if let Some(map) = args.map {
        save.current_map_number = map;
    }
    if let Some(ex) = args.ex_round {
        save.new_game_count = ex;
    }
    if let Some(cd) = args.clear_data {
        save.is_clear_data = cd;
    }
    if let Some(name) = args.aya_name {
        save.aya_name = name;
    }
    if let Some(ammo) = args.ammo_9mm {
        save.category_counts[0] = ammo;
    }
    if let Some(rockets) = args.ammo_rocket {
        save.category_counts[1] = rockets;
    }
    if let Some(dna) = args.ammo_dna {
        save.category_counts[2] = dna;
    }

    for item_str in &args.add_item {
        let item_id = parse_u8_hex_or_dec(item_str)
            .with_context(|| format!("Invalid item ID '{}'", item_str))?;
        let added_slot = save.add_item_to_inventory(item_id)?;
        println!(
            "Added item 0x{:02X} to inventory slot #{}",
            item_id, added_slot
        );
    }

    for spec in &args.stack_quantity {
        let fields: Vec<_> = spec.split(':').collect();
        anyhow::ensure!(
            fields.len() == 3,
            "Use --stack-quantity bag:1:20 (or storage/vault)"
        );
        let list = match fields[0] {
            "bag" => 0,
            "storage" => 1,
            "vault" => 2,
            _ => bail!("Unknown inventory list"),
        };
        let index = fields[1]
            .parse::<usize>()?
            .checked_sub(1)
            .context("Stack slot starts at 1")?;
        save.set_stack_quantity(list, index, fields[2].parse()?)?;
    }
    if args.sort_bag {
        save.sort_aya_bag();
    }
    if args.sort_wayne {
        save.sort_wayne_storage();
    }

    let _ = save.to_bytes();
    let updated_summary = save.clone();
    container.save()?;

    println!(
        "Successfully updated Slot {} in {}:",
        idx + 1,
        container.path.display()
    );
    print_slot_summary(&updated_summary, false);
    Ok(())
}
