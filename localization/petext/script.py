"""A room's scripts, decoded: the task block of its data run (a relocation block: {code offset,
count}, then count entry offsets) holds self-describing bytecode (src/main/script/flow_opcodes.c,
Script_RunQueue):

  u32 header   opcode = h & 0x1FFF, argument count = (h >> 13) & 15, then 3 bits a mode per argument
               from bit 17 (0 an immediate value, 1 an actor variable, 2 a script variable,
               3 an operand slot, 4 a node flag); past five arguments the modes are in the tail
  u32 tail
  u32 args[count]

Scripts run from each entry point to the next; FFFFFFFF words separate them.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from pathlib import Path

MODE_IMMEDIATE = 0


@dataclass
class Instr:
    script: int        # entry point index
    at: int            # byte offset in the task block
    op: int
    args: tuple
    modes: tuple

    def imm(self, k: int):
        """Argument k if it is an immediate value, else None."""
        return self.args[k] if k < len(self.args) and self.modes[k] == MODE_IMMEDIATE else None


def opcodes(src) -> set[int]:
    """The opcodes the game has handlers for (g_ScriptQueueOpcodeHandlers in a port checkout)."""
    text = (Path(src) / "src/main/script/flow_opcodes.c").read_text(errors="replace")
    return {int(m) for m in re.findall(r"\[(\d+)\]\s*=\s*\w+", text)}


def task_block(data: bytes) -> bytes:
    d = struct.unpack_from("<I", data, 4)[0]
    tb = struct.unpack_from("<I", data, d + 0x14)[0]
    if not tb >> 22:
        return b""
    rec = tb & 0x3FFFFF
    size = struct.unpack_from("<I", data, rec)[0] & 0xFFFFFF
    off = struct.unpack_from("<I", data, rec + 4)[0] & 0xFFFFFF
    return data[off:off + size]


def walk(block: bytes, valid: set[int]) -> tuple[list[Instr], bool]:
    """Every instruction of every script, in order; clean is False when a script stops decoding
    before its end (a few bytes of data after the last script don't count)."""
    if len(block) < 8:
        return [], True
    _, count = struct.unpack_from("<II", block, 0)
    entries = sorted(set(struct.unpack_from(f"<{count}I", block, 8)))
    out, clean = [], True
    for n, e in enumerate(entries):
        end = entries[n + 1] if n + 1 < len(entries) else len(block)
        at = e
        while at + 8 <= end:
            h = struct.unpack_from("<I", block, at)[0]
            if h == 0xFFFFFFFF:
                if n + 1 == len(entries):
                    break
                at += 4
                continue
            op, argc, modes = h & 0x1FFF, (h >> 13) & 15, h >> 17
            if op not in valid or at + 8 + 4 * argc > len(block):
                if not (n + 1 == len(entries) and len(block) - at <= 64):
                    clean = False
                break
            tail = struct.unpack_from("<I", block, at + 4)[0]
            args = struct.unpack_from(f"<{argc}I", block, at + 8)
            ms, m = [], modes
            for k in range(argc):
                ms.append(m & 7)
                m >>= 3
                if k == 4:
                    m = tail
            out.append(Instr(n, at, op, args, tuple(ms)))
            at += 8 + 4 * argc
    return out, clean
