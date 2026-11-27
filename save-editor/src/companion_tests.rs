use crate::{companion::*, memcard::MemoryCardContainer, mod_save, save::PeSaveSlot};
use std::{fs, path::PathBuf};
fn temp() -> PathBuf {
    static NEXT: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let p = std::env::temp_dir().join(format!(
        "pe-editor-companion-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
    ));
    fs::create_dir_all(&p).unwrap();
    p
}
fn base() -> PeSaveSlot {
    let mut b = vec![0; 8192];
    b[..2].copy_from_slice(b"SC");
    b[0x140] = 255;
    b[0x142] = 255;
    b[0x12c] = 46;
    let tail = &mut b[mod_save::OFFSET..];
    tail[..4].copy_from_slice(b"PEMC");
    tail[4] = 1;
    tail[6] = 20;
    let window = mod_save::crc32(&tail[0x200..0x288]);
    put32(tail, 12, window);
    let checksum = mod_save::crc32(&tail[..20]);
    put32(tail, 8, checksum);
    PeSaveSlot::from_bytes(&b, 0, "BASLUS-006620000000A").unwrap()
}
pub(crate) fn fixture() -> PeSaveSlot {
    let mut s = base();
    s.inventory_items[0] = 6;
    s.storage[1] = 6;
    let mut counts = vec![0; 204];
    counts[0] = 1;
    counts[1] = 1;
    counts[4..8].copy_from_slice(&[6, 2, 6, 3]);
    let sections = [
        ("future_mod/opaque", 7, vec![0x71; 37]),
        ("pe_economy/policy", 1, vec![0x5a; 160]),
        ("pe_host/content", 1, vec![0; 2048]),
        ("pe_stacks/counts", 1, counts),
    ];
    let size = 96 + sections.len() * 64 + sections.iter().map(|(_, _, b)| b.len()).sum::<usize>();
    let mut bytes = vec![0; size];
    bytes[..4].copy_from_slice(b"PEEX");
    bytes[4] = 1;
    bytes[6] = 96;
    put32(&mut bytes, 8, 1);
    put32(&mut bytes, 12, size as u32);
    bytes[16] = sections.len() as u8;
    bytes[20..27].copy_from_slice(b"fixture");
    let mut next = 96 + sections.len() * 64;
    for (i, (key, version, data)) in sections.iter().enumerate() {
        let p = 96 + i * 64;
        bytes[p..p + key.len()].copy_from_slice(key.as_bytes());
        bytes[p + 48] = *version;
        bytes[p + 50] = 1;
        put32(&mut bytes, p + 52, next as u32);
        put32(&mut bytes, p + 56, data.len() as u32);
        put32(&mut bytes, p + 60, mod_save::crc32(data));
        bytes[next..next + data.len()].copy_from_slice(data);
        next += data.len();
    }
    let crc = mod_save::crc32(&bytes);
    put32(&mut bytes, 88, crc);
    let mut c = Companion {
        bytes,
        equipment_snapshot: vec![0; 4096],
    };
    c.bind(&mut s.raw_bytes, &s.filename, 2).unwrap();
    s.companion = Some(c);
    s
}
fn opaque(s: &PeSaveSlot) -> Vec<u8> {
    s.companion
        .as_ref()
        .unwrap()
        .section("future_mod/opaque")
        .unwrap()
        .unwrap()
        .1
}
fn write_initial(s: &mut PeSaveSlot, folder: &std::path::Path) -> PathBuf {
    let path = folder.join(&s.filename);
    crate::memcard::write_slot(s, &path, false).unwrap();
    path
}
#[test]
fn paired_save_and_stack_edit_roundtrip() {
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let original = fs::read(&path).unwrap();
    let mut card = MemoryCardContainer::open(&folder).unwrap();
    let slot = card.slots[0].as_mut().unwrap();
    assert_eq!(slot.stack_quantity(0, 0).unwrap(), 3);
    slot.set_stack_quantity(0, 0, 17).unwrap();
    slot.bonus_points = 123;
    card.save().unwrap();
    let card = MemoryCardContainer::open(&folder).unwrap();
    let saved = card.slots[0].as_ref().unwrap();
    assert_eq!(saved.stack_quantity(0, 0).unwrap(), 17);
    assert_eq!(saved.bonus_points, 123);
    assert_eq!(opaque(saved), opaque(&s));
    let old = load(&original, &folder.join(".pe-ext"), &s.filename)
        .unwrap()
        .unwrap();
    old.validate_link(&original, &s.filename).unwrap();
    assert_ne!(fs::read(path).unwrap(), original);
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn missing_corrupt_and_mismatched_companions_refuse() {
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let raw = fs::read(&path).unwrap();
    let l = link(&raw).unwrap().unwrap();
    let file = folder
        .join(".pe-ext")
        .join(format!("{}.{:08x}.pex", s.filename, u32at(&l, 4)));
    let bytes = fs::read(&file).unwrap();
    fs::remove_file(&file).unwrap();
    assert!(MemoryCardContainer::open(&path).is_err());
    let mut bad = bytes.clone();
    bad[200] ^= 1;
    fs::write(&file, bad).unwrap();
    assert!(MemoryCardContainer::open(&path).is_err());
    fs::write(&file, bytes).unwrap();
    assert!(MemoryCardContainer::open(&path).is_ok());
    assert_eq!(fs::read(&path).unwrap(), raw);
    let mut wrong = s.companion.clone().unwrap();
    let mut other = raw.clone();
    wrong.bind(&mut other, "different", 99).unwrap();
    fs::write(&file, &wrong.bytes).unwrap();
    assert!(MemoryCardContainer::open(&path).is_err());
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn move_sort_replace_and_full_stack_are_consistent() {
    let mut s = fixture();
    s.sort_aya_bag();
    s.sort_wayne_storage();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 3);
    s.move_inventory_slot(0, 0, 1).unwrap();
    let at = s.storage.iter().position(|&v| v == 6).unwrap();
    assert_eq!(s.stack_quantity(1, at).unwrap(), 7);
    assert_eq!(s.inventory_items[0], 0);
    s.move_inventory_slot(1, at, 0).unwrap();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 7);
    s.set_stack_quantity(0, 0, 99).unwrap();
    let before = serde_json::to_string(&s).unwrap();
    assert!(s.add_item_to_inventory(6).is_err());
    assert_eq!(serde_json::to_string(&s).unwrap(), before);
    s.replace_inventory_slot(0, 0, 0).unwrap();
    s.validate_inventory().unwrap();
    assert_eq!(crate::stacks::read(&s).unwrap().unwrap()[0][6], 0);
}
#[test]
fn capacity_failures_and_orphans_never_write() {
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let before = fs::read(&path).unwrap();
    s.set_stack_quantity(1, 1, 98).unwrap();
    let json = serde_json::to_string(&s).unwrap();
    assert!(s.move_inventory_slot(0, 0, 1).is_err());
    assert_eq!(json, serde_json::to_string(&s).unwrap());
    let mut card = MemoryCardContainer::open(&folder).unwrap();
    card.slots[0].as_mut().unwrap().inventory_items[0] = 0;
    assert!(card.save().is_err());
    assert_eq!(fs::read(path).unwrap(), before);
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn json_clone_clear_style_and_all_card_formats_keep_companions() {
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let json = serde_json::to_string(&s).unwrap();
    let mut restored: PeSaveSlot = serde_json::from_str(&json).unwrap();
    restored.validate_inventory().unwrap();
    restored.set_stack_quantity(0, 0, 12).unwrap();
    let mut card = MemoryCardContainer::open(&path).unwrap();
    card.slots[0] = Some(restored);
    card.clone_slot(0, 1).unwrap();
    card.kind = crate::memcard::ContainerKind::PsyzDirectory;
    card.path = folder.join("cloned");
    card.save().unwrap();
    let mut card = MemoryCardContainer::open(&card.path).unwrap();
    card.slots[1].as_mut().unwrap().is_clear_data = true;
    card.save().unwrap();
    assert!(!card.path.join("BASLUS-006620000000B").exists());
    let mut card = MemoryCardContainer::open(&card.path).unwrap();
    let image = folder.join("export.mcr");
    card.export_to_ps1_card_image(&image).unwrap();
    let mut imagecard = MemoryCardContainer::open(&image).unwrap();
    imagecard.save().unwrap();
    let target = folder.join("reimport");
    imagecard.export_to_psyz_dir(&target).unwrap();
    let imported = MemoryCardContainer::open(&target).unwrap();
    assert_eq!(
        imported.slots[1]
            .as_ref()
            .unwrap()
            .stack_quantity(0, 0)
            .unwrap(),
        12
    );
    assert_eq!(opaque(imported.slots[1].as_ref().unwrap()), opaque(&s));
    let single_image = folder.join("single.mcr");
    MemoryCardContainer::open(&path)
        .unwrap()
        .export_to_ps1_card_image(&single_image)
        .unwrap();
    // MCS/GME package variants use the same namespaced companions.
    for (name, kind, bytes) in [
        (
            "save.mcs",
            crate::memcard::ContainerKind::McsSingleFile,
            vec![0; crate::memcard::MCS_SAVE_SIZE],
        ),
        (
            "save.gme",
            crate::memcard::ContainerKind::DexDriveGmeImage,
            [
                vec![0; crate::memcard::GME_HEADER_SIZE],
                fs::read(&single_image).unwrap(),
            ]
            .concat(),
        ),
    ] {
        let mut one = MemoryCardContainer::open(&path).unwrap();
        one.kind = kind;
        one.path = folder.join(name);
        one.raw_container_bytes_for_test(bytes);
        one.save().unwrap();
        let loaded = MemoryCardContainer::open(&one.path).unwrap();
        assert_eq!(
            loaded.slots[0]
                .as_ref()
                .unwrap()
                .stack_quantity(0, 0)
                .unwrap(),
            3
        );
    }
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn old_save_import_carries_companion_and_keeps_existing_destination() {
    let folder = temp();
    let source = folder.join("source");
    fs::create_dir_all(&source).unwrap();
    let mut s = fixture();
    let path = write_initial(&mut s, &source);
    let target = folder.join("target");
    let plan = crate::card_location::plan_import(&source, &target).unwrap();
    assert_eq!(crate::card_location::run_import(&plan).unwrap(), 1);
    assert!(MemoryCardContainer::open(&target).unwrap().slots[0]
        .as_ref()
        .unwrap()
        .companion
        .is_some());
    let before = fs::read(target.join(&s.filename)).unwrap();
    assert_eq!(crate::card_location::run_import(&plan).unwrap(), 0);
    assert_eq!(fs::read(target.join(&s.filename)).unwrap(), before);
    assert!(path.exists());
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn unsupported_and_malformed_stack_sections_are_not_overwritten() {
    let mut s = fixture();
    let c = s.companion.as_mut().unwrap();
    let (section, mut b) = c.section("pe_stacks/counts").unwrap().unwrap();
    b[0] = 101;
    c.replace(&section, &b).unwrap();
    let before = serde_json::to_string(&s).unwrap();
    assert!(s.set_stack_quantity(0, 0, 4).is_err());
    assert!(s.validate_inventory().is_err());
    assert_eq!(before, serde_json::to_string(&s).unwrap());
}
#[test]
fn native_host_accepts_published_companion() {
    let Some(exe) = std::env::var_os("PE_COMPANION_ORACLE") else {
        return;
    };
    let folder = temp();
    let mut s = fixture();
    s.set_stack_quantity(0, 0, 17).unwrap();
    let path = write_initial(&mut s, &folder);
    assert!(std::process::Command::new(exe)
        .arg(&path)
        .status()
        .unwrap()
        .success());
    fs::remove_dir_all(folder).unwrap();
}
#[test]
fn legacy_stacks_and_duplicate_slots_have_exact_copy_semantics() {
    let mut s = base();
    s.inventory_items[0] = 6;
    s.inventory_items[1] = 6;
    let mut b = vec![0; 204];
    b[0] = 1;
    b[4] = 6;
    b[5] = 8;
    mod_save::replace_record(&mut s.raw_bytes, "pe_stacks.counts", 1, &b, None).unwrap();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 9);
    assert_eq!(s.stack_quantity(0, 1).unwrap(), 1);
    assert!(s.set_stack_quantity(0, 0, 99).is_err());
    s.set_stack_quantity(0, 0, 98).unwrap();
    s.replace_inventory_slot(0, 0, 0).unwrap();
    assert_eq!(s.stack_quantity(0, 1).unwrap(), 1);
    s.validate_inventory().unwrap();
}
#[test]
fn content_identity_protects_custom_gear_and_allows_unrelated_changes() {
    let mut s = fixture();
    s.equipment[0] = crate::save::EquipmentRecord::from_item_template(0, 0x44);
    s.inventory_items[2] = 0x100;
    let mut baseline = vec![0; 4096];
    for (i, r) in s.equipment.iter().enumerate() {
        r.write(&mut baseline[i * 32..i * 32 + 32]);
    }
    let c = s.companion.as_mut().unwrap();
    c.equipment_snapshot = baseline;
    let (section, mut ledger) = c.section("pe_host/content").unwrap().unwrap();
    ledger[..4].copy_from_slice(b"PEC1");
    put32(&mut ledger, 4, 2);
    ledger[12] = 1;
    ledger[13] = 1;
    ledger[16..26].copy_from_slice(b"mod_a/item");
    ledger[64] = 1;
    ledger[66] = 1;
    put32(&mut ledger, 640, 1);
    ledger[646] = 1;
    c.replace(&section, &ledger).unwrap();
    s.prepare_companion_inventory().unwrap();
    s.equipment[1] = crate::save::EquipmentRecord::from_item_template(1, 0x45);
    s.prepare_companion_inventory().unwrap();
    let old = s.equipment[0].clone();
    s.equipment[0].item_id = 0x45;
    s.to_bytes();
    assert!(s.prepare_companion_inventory().is_err());
    s.equipment[0] = old;
    s.prepare_companion_inventory().unwrap();
}
#[test]
fn failed_native_publication_keeps_previous_pair_valid() {
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let original = fs::read(&path).unwrap();
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        let locked = fs::OpenOptions::new()
            .read(true)
            .share_mode(1)
            .open(&path)
            .unwrap();
        let mut card = MemoryCardContainer::open(&path).unwrap();
        card.slots[0]
            .as_mut()
            .unwrap()
            .set_stack_quantity(0, 0, 18)
            .unwrap();
        assert!(card.save().is_err());
        assert_eq!(fs::read(&path).unwrap(), original);
        drop(locked);
    }
    let saved = MemoryCardContainer::open(&path).unwrap();
    assert_eq!(
        saved.slots[0]
            .as_ref()
            .unwrap()
            .stack_quantity(0, 0)
            .unwrap(),
        3
    );
    fs::remove_dir_all(folder).unwrap();
}

#[test]
fn cli_quantity_json_and_card_commands_preserve_provider_data() {
    use clap::Parser;
    let folder = temp();
    let mut s = fixture();
    let path = write_initial(&mut s, &folder);
    let run = |args: Vec<String>| {
        let cli = crate::cli::Cli::try_parse_from(args).unwrap();
        crate::cli::run_cli_command(cli.command.unwrap()).unwrap();
    };
    run(vec![
        "editor".into(),
        "edit".into(),
        path.to_string_lossy().into(),
        "--stack-quantity".into(),
        "bag:1:21".into(),
    ]);
    let json = folder.join("slot.json");
    run(vec![
        "editor".into(),
        "export-json".into(),
        path.to_string_lossy().into(),
        json.to_string_lossy().into(),
    ]);
    run(vec![
        "editor".into(),
        "import-json".into(),
        json.to_string_lossy().into(),
        path.to_string_lossy().into(),
    ]);
    let image = folder.join("card.mcr");
    run(vec![
        "editor".into(),
        "export-card".into(),
        path.to_string_lossy().into(),
        image.to_string_lossy().into(),
    ]);
    let target = folder.join("target");
    run(vec![
        "editor".into(),
        "import-psyz".into(),
        image.to_string_lossy().into(),
        target.to_string_lossy().into(),
    ]);
    let loaded = MemoryCardContainer::open(target).unwrap();
    let final_slot = loaded.slots[0].as_ref().unwrap();
    assert_eq!(final_slot.stack_quantity(0, 0).unwrap(), 21);
    assert_eq!(opaque(final_slot), opaque(&s));
    assert_eq!(
        final_slot
            .companion
            .as_ref()
            .unwrap()
            .section("pe_economy/policy")
            .unwrap()
            .unwrap()
            .1,
        vec![0x5a; 160]
    );
    fs::remove_dir_all(folder).unwrap();
}

#[test]
fn transitional_legacy_counts_never_resurrect_cleared_companion_stacks() {
    let mut s = fixture();
    let mut old = vec![0; 204];
    old[0] = 1;
    old[4] = 6;
    old[5] = 4;
    mod_save::replace_record(&mut s.raw_bytes, "pe_stacks.counts", 1, &old, None).unwrap();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 3);
    s.set_stack_quantity(0, 0, 1).unwrap();
    s.set_stack_quantity(1, 1, 1).unwrap();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 1);
    let d = mod_save::ModData::inspect(&s.raw_bytes).unwrap();
    let record = d
        .records
        .iter()
        .find(|r| r.id == mod_save::id("pe_stacks.counts"))
        .unwrap();
    assert_eq!(record.data, vec![0; 204]);
    // Match the game's transitional migration: an empty companion can still import the old record.
    mod_save::replace_record(&mut s.raw_bytes, "pe_stacks.counts", 1, &old, None).unwrap();
    assert_eq!(s.stack_quantity(0, 0).unwrap(), 5);
}
