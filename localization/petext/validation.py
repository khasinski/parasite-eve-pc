"""Runtime text commands, separate from the lossless disc encoder/decoder.

Extraction may preserve arbitrary bytes. Published translations must instead be
complete, supported command streams; raw tag spellings do not bypass these checks.
"""
from . import gametext as gt


def commands(raw: bytes):
    """Yield complete commands (glyphs are one-byte commands); no page terminator."""
    i = 0
    while i < len(raw):
        start, c = i, raw[i]
        i += 1
        if c < 0xF1 or c in (0xF7, 0xF8, 0xFA):
            pass
        elif c == 0xF1:
            if i+3 > len(raw) or raw[i] != 0xC5 or raw[i+1] >= 240 or raw[i+2] >= 240:
                raise ValueError(f'invalid CJK token at byte {start}')
            i += 3
        elif c == 0xFB:
            if i == len(raw) or raw[i] > 9:
                raise ValueError(f'invalid or incomplete FB command at byte {start}')
            sub = raw[i]
            i += 1
            if sub in (7, 9):
                if i == len(raw):
                    raise ValueError(f'missing FB {sub:02X} operand at byte {start}')
                if sub == 9 and not 1 <= raw[i] <= 7:
                    raise ValueError('choice count must be between 1 and 7')
                i += 1
        elif c in (0xFC, 0xFD):
            if i == len(raw):
                raise ValueError(f'missing font-page operand at byte {start}')
            i += 1
        else:
            raise ValueError(f'unsupported control/embedded terminator {c:02X} at byte {start}; '
                             'the compiler supplies the page end')
        yield raw[start:i]


def structure(raw: bytes):
    # Line breaks may be moved for translation. All other commands retain their
    # order and operands, including waits, pauses, highlighting and choices.
    return [c for c in commands(raw) if c[0] >= 0xF8]


def check(raw: bytes, source):
    """source: the English text, or its structure already (a list, from the source manifest)."""
    actual = structure(raw)
    expected = source if isinstance(source, list) else structure(gt.encode(source, extended=True))
    if actual != expected:
        raise ValueError('control commands must match the source in count, order and operands '
                         '(including waits, choices, names, numbers and pauses)')
