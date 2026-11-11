//! A room's scripts (`petext/script.py`, `extract_text.py`): the task block of its data run holds
//! self-describing bytecode. Used to generate the catalog's scene views.
use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::Path;

use regex::Regex;

use crate::error::{Error, Result};

const MODE_IMMEDIATE: u8 = 0;
const OP_BOX: u32 = 13;
const OP_STYLED_BOX: u32 = 209;
const OP_ASSIGN: u32 = 10;
const OP_EVAL: u32 = 9;

pub struct Instr {
    pub script: usize,
    pub at: usize,
    pub op: u32,
    pub args: Vec<u32>,
    pub modes: Vec<u8>,
}

impl Instr {
    fn imm(&self, k: usize) -> Option<u32> {
        (k < self.args.len() && self.modes[k] == MODE_IMMEDIATE).then(|| self.args[k])
    }
}

fn u32_le(data: &[u8], at: usize) -> Result<u32> {
    data.get(at..at + 4)
        .map(|bytes| u32::from_le_bytes(bytes.try_into().unwrap()))
        .ok_or_else(|| Error::value("the room script is truncated"))
}

/// The opcodes the game has handlers for, from `g_ScriptQueueOpcodeHandlers` in a port checkout.
pub fn opcodes(src: &Path) -> Result<HashSet<u32>> {
    let text = std::fs::read(src.join("src/main/script/flow_opcodes.c"))?;
    let text = String::from_utf8_lossy(&text);
    let pattern = Regex::new(r"\[([0-9]+)\]\s*=\s*\w+").unwrap();
    Ok(pattern.captures_iter(&text).filter_map(|m| m[1].parse().ok()).collect())
}

pub fn task_block(data: &[u8]) -> Result<&[u8]> {
    let d = u32_le(data, 4)? as usize;
    let tb = u32_le(data, d + 0x14)? as usize;
    if tb >> 22 == 0 {
        return Ok(&[]);
    }
    let rec = tb & 0x3F_FFFF;
    let size = (u32_le(data, rec)? & 0xFF_FFFF) as usize;
    let off = (u32_le(data, rec + 4)? & 0xFF_FFFF) as usize;
    let start = off.min(data.len());
    Ok(&data[start..(off + size).min(data.len())])
}

/// Every instruction of every script; the flag is false when a script stops decoding before its
/// end (a few bytes of data after the last script don't count).
pub fn walk(block: &[u8], valid: &HashSet<u32>) -> Result<(Vec<Instr>, bool)> {
    if block.len() < 8 {
        return Ok((Vec::new(), true));
    }
    let count = u32_le(block, 4)? as usize;
    let mut unique = BTreeSet::new();
    for k in 0..count {
        unique.insert(u32_le(block, 8 + 4 * k)? as usize);
    }
    let entries: Vec<usize> = unique.into_iter().collect();
    let (mut out, mut clean) = (Vec::new(), true);
    for (n, &entry) in entries.iter().enumerate() {
        let last = n + 1 == entries.len();
        let end = if last { block.len() } else { entries[n + 1] };
        let mut at = entry;
        while at + 8 <= end {
            let header = u32_le(block, at)?;
            if header == 0xFFFF_FFFF {
                if last {
                    break;
                }
                at += 4;
                continue;
            }
            let (op, argc, modes) = (header & 0x1FFF, ((header >> 13) & 15) as usize, header >> 17);
            if !valid.contains(&op) || at + 8 + 4 * argc > block.len() {
                if !(last && block.len() - at <= 64) {
                    clean = false;
                }
                break;
            }
            let tail = u32_le(block, at + 4)?;
            let mut args = Vec::with_capacity(argc);
            for k in 0..argc {
                args.push(u32_le(block, at + 8 + 4 * k)?);
            }
            let (mut mode_list, mut m) = (Vec::with_capacity(argc), modes);
            for k in 0..argc {
                mode_list.push((m & 7) as u8);
                m >>= 3;
                if k == 4 {
                    m = tail;
                }
            }
            out.push(Instr { script: n, at, op, args, modes: mode_list });
            at += 8 + 4 * argc;
        }
    }
    Ok((out, clean))
}

/// Every text box a room's scripts open: (page or None, the page's variable when it is computed).
pub fn box_pages(code: &[Instr]) -> Vec<(Option<u32>, (u8, u32))> {
    code.iter()
        .filter(|i| (i.op == OP_BOX || i.op == OP_STYLED_BOX) && !i.args.is_empty())
        .map(|i| (i.imm(0).map(|page| page & 0xFFFF), (i.modes[0], i.args[0])))
        .collect()
}

/// The constants a room's scripts write into each variable: AssignValue of an immediate, or
/// EvalExpr adding or subtracting two immediates.
pub fn constant_writes(code: &[Instr]) -> HashMap<(u8, u32), BTreeSet<u32>> {
    let mut out: HashMap<(u8, u32), BTreeSet<u32>> = HashMap::new();
    for i in code {
        if i.op == OP_ASSIGN && i.args.len() == 2 && i.modes[1] == MODE_IMMEDIATE {
            out.entry((i.modes[0], i.args[0])).or_default().insert(i.args[1] & 0xFFFF);
        } else if i.op == OP_EVAL
            && i.args.len() == 4
            && i.args[0] <= 1
            && i.modes[0] == MODE_IMMEDIATE
            && i.modes[2] == MODE_IMMEDIATE
            && i.modes[3] == MODE_IMMEDIATE
        {
            let value = if i.args[0] == 0 { i.args[2].wrapping_add(i.args[3]) } else { i.args[2].wrapping_sub(i.args[3]) };
            out.entry((i.modes[1], i.args[1])).or_default().insert(value & 0xFFFF);
        }
    }
    out
}
