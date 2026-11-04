//! Command-line argument schema.
use super::*;

#[derive(Parser, Debug)]
#[command(
    name = "pe1-save-editor",
    version,
    about = "Parasite Eve (SLUS-006.62 / PC Port) Save Editor & PsyZ Memory Card Manager (GUI + CLI)"
)]
pub struct Cli {
    /// Data-only custom item/effect definitions, independent of installed game mods.
    #[arg(long, global = true)]
    pub definitions: Option<PathBuf>,

    /// Optional path to open directly in GUI when no subcommand is provided (e.g. `bu00`, `.srm`, `.mcr`)
    pub path: Option<PathBuf>,

    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Subcommand, Debug)]
pub enum Command {
    /// Launch the graphical Save Editor (default when run without arguments)
    Gui {
        /// Optional path to a PsyZ directory (`bu00`/`bu10`), raw slot file, or PS1 card (`.srm`/`.mcr`/`.gme`)
        path: Option<PathBuf>,
    },
    /// Inspect all slots or a specific slot in a PsyZ folder (`bu00`) or PS1 memory card (`.srm`/`.mcr`)
    Inspect {
        /// Path to `bu00` directory, raw slot file, or `.srm`/`.mcr`/`.mcd`/`.gme`/`.mcs` file
        path: PathBuf,
        /// Optional 1-based slot number (1..=15, where 1 = Slot A)
        #[arg(short, long)]
        slot: Option<usize>,
        /// Show detailed inventory, equipment records, mods, and Wayne's storage
        #[arg(short, long)]
        verbose: bool,
    },
    /// Edit a save slot from the command line and automatically recalculate CRC-16 + battle state
    Edit(EditArgs),
    /// Import PS1 memory card saves (`.srm`, `.mcr`, `.mcd`, `.gme`, `.mcs`) into a PsyZ `bu00` or `bu10` directory
    ImportPsyz {
        /// Source PS1 memory card or single save file
        source: PathBuf,
        /// Target PsyZ directory (e.g. `./bu00`)
        target_dir: PathBuf,
    },
    /// Export a PsyZ directory (`bu00`/`bu10`) or save into a 128 KB PS1 memory card image (`.srm`/`.mcr`)
    ExportCard {
        /// Source PsyZ directory (e.g. `./bu00`) or save file
        source: PathBuf,
        /// Target `.srm` or `.mcr` file path
        target_file: PathBuf,
    },
    /// Export a save slot to a human-readable JSON file
    ExportJson {
        /// Source container (`bu00`, `.srm`, `.mcr`, or raw slot)
        source: PathBuf,
        /// Output `.json` file path
        output: PathBuf,
        /// 1-based slot number (1..=15). Defaults to the first active slot.
        #[arg(short, long)]
        slot: Option<usize>,
    },
    /// Import a JSON save slot back into a container (`bu00`, `.srm`, `.mcr`, or raw slot)
    ImportJson {
        /// Input `.json` file path
        input: PathBuf,
        /// Target container (`bu00`, `.srm`, `.mcr`, or raw slot)
        target: PathBuf,
        /// Optional 1-based target slot override (1..=15)
        #[arg(short, long)]
        slot: Option<usize>,
    },
    /// Show where the game keeps its memory cards (and old pre-0.7 folders), with save counts
    Cards,
    /// List all 256 Parasite Eve Item IDs, 32 Weapon/Armor Mods, and 20 Parasite Energy spells
    ListItems {
        /// Optional case-insensitive search filter
        #[arg(short, long)]
        filter: Option<String>,
    },
}

#[derive(Args, Debug)]
pub struct EditArgs {
    /// Path to `bu00` directory, raw slot file, or `.srm`/`.mcr`/`.mcd`/`.gme`/`.mcs` file
    pub path: PathBuf,

    /// 1-based slot number to edit (1..=15, where 1 = Slot A). Defaults to the first active slot.
    #[arg(short, long)]
    pub slot: Option<usize>,

    /// Set Aya's display level (1..=99)
    #[arg(long)]
    pub level: Option<u8>,

    /// Set current HP (1..=999)
    #[arg(long)]
    pub hp: Option<u16>,

    /// Set maximum HP (1..=999). The game sets it from the HP stat, so the stat is raised to
    /// match (exactly with a disc). The top is 869, or 999 with HP Max Up armor equipped.
    #[arg(long)]
    pub max_hp: Option<u16>,

    /// Set total EXP
    #[arg(long)]
    pub exp: Option<u32>,

    /// Set Bonus Points (BP, 0..=99999)
    #[arg(long)]
    pub bp: Option<u32>,

    /// Set Aya's base bag capacity (10..=46; armor can add up to 4 slots)
    #[arg(long, value_parser = clap::value_parser!(u8).range(10..=46))]
    pub slots: Option<u8>,

    /// Parasite Eve (USA) disc image (`.cue`, `.bin` or `.iso`) to read item records and the bag
    /// table from. Defaults to the disc in the game's `parasite-eve.toml`.
    #[arg(long)]
    pub disc: Option<PathBuf>,

    /// Set story day (1..=6)
    #[arg(long)]
    pub day: Option<i16>,

    /// Set current room/map ID (1..=437)
    #[arg(long)]
    pub map: Option<i16>,

    /// Set EX Game playthrough round count (0 = 1st Playthrough, 1 = 2nd Round, etc.)
    #[arg(long)]
    pub ex_round: Option<u8>,

    /// Mark or unmark as Clear Data / EX Game save
    #[arg(long)]
    pub clear_data: Option<bool>,

    /// Set Aya's name (max 12 chars)
    #[arg(long)]
    pub aya_name: Option<String>,

    /// Set 9mm / standard bullets pool count
    #[arg(long)]
    pub ammo_9mm: Option<u16>,

    /// Set rockets pool count
    #[arg(long)]
    pub ammo_rocket: Option<u16>,

    /// Set DNA / Maeda bullets pool count
    #[arg(long)]
    pub ammo_dna: Option<u16>,

    /// Add an item by hex (`0x36`) or decimal (`54`) ID to the first free inventory slot (auto-creates equipment record for weapons/armor)
    #[arg(long)]
    pub add_item: Vec<String>,

    /// Repair equipment icons/model classes from older editor versions, preserving upgrades
    #[arg(long)]
    pub repair_equipment_types: bool,

    /// Sort Aya's bag (weapons, armor, ammo, medicine, other, key items; the equipped gun and
    /// armor stay equipped)
    #[arg(long)]
    pub sort_bag: bool,

    /// Sort Wayne's item shelf and equipment vault
    #[arg(long)]
    pub sort_wayne: bool,

    /// Cheat: Max Aya stats (Lv 99, top HP: 869, or 999 with HP Max Up armor; 99,999 BP, 46 item
    /// slots, max stat growth)
    #[arg(long)]
    pub max_aya: bool,

    /// Cheat: Unlock all playable Parasite Energy spells
    #[arg(long)]
    pub unlock_all_pe: bool,

    /// Cheat: Max all ammo pools (999) and refill all weapons
    #[arg(long)]
    pub max_ammo: bool,

    /// Cheat: Upgrade all active weapons and armor (+999 bonus stats, 10 mod slots, Wayne engraving)
    #[arg(long)]
    pub max_gear: bool,

    /// Cheat: Add and equip an ultimate P90 + Cm Suit 2 (old gear stays in the bag), plus Super
    /// Tool Kit + Full Recover. The suit's HP Max Up lets --max-aya reach 999 HP
    #[arg(long)]
    pub ultimate_loadout: bool,
}
