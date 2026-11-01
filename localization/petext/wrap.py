"""Automatic line breaks and screens for dialogue pages that overflow their box.

The compiler wraps a page only when it doesn't fit; a page that fits keeps the translator's
layout. Line breaks go at spaces (continuation lines indented as the English indents them: PE
starts them with a space). A screen still too tall is split into screens of the box's height by
adding waits, which the game shows as another screen after a button press. A screen with a choice
is never split, and its choice rows are never rewrapped: the rows must stay on the question's
screen. A word wider than the box stays as it is (the layout check reports it).

Runs after control validation: the waits it adds are the compiler's, not the translator's.
"""
from __future__ import annotations

from .layout import NAME_WIDTH, advance
from .validation import commands

SPACE, BREAK, WAIT = 0x0F, 0xF7, 0xF8
CHOICE = 9                                   # FB 09 n


def indents(english: bytes) -> tuple[bool, bool]:
    """Whether the English starts lines after a break, and screens after a wait, with a space."""
    after_break = english.count(bytes([BREAK, SPACE])) * 2 >= english.count(BREAK) > 0
    after_wait = english.count(bytes([WAIT, SPACE])) * 2 >= english.count(WAIT) > 0
    return after_break, after_wait


def _is_choice(c: bytes) -> bool:
    return c[0] == 0xFB and c[1] == CHOICE


def _wrap_line(line: list[bytes], width: int, name_px: int, indent: bool, characters=None) -> list[list[bytes]]:
    """One line's commands as the lines they fill when broken at spaces."""
    out, cur, cur_w = [], [], 0
    words, word = [], []
    for c in line:                           # words keep their leading spaces
        chars = characters or {}
        prev = chars.get(word[-1], '') if word else ''
        current = chars.get(c, '')
        cjk_break = bool(word and (prev or current) and
                         (not prev or prev not in '（［｛〈《「『【〔〖〘〚“‘') and
                         (not current or current not in '、。，．！？：；）］｝〉》」』】〕〗〙〛”’ー々ぁぃぅぇぉっゃゅょァィゥェォッャュョ'))
        if (c[0] == SPACE and word and word[-1][0] != SPACE) or cjk_break:
            words.append(word)
            word = []
        word.append(c)
    if word:
        words.append(word)
    for w in words:
        w_px = sum(advance(c, name_px) for c in w)
        if cur and cur_w + w_px > width:
            out.append(cur)
            body = w
            while body and body[0][0] == SPACE:   # the break replaces the spaces before the word
                body = body[1:]
            cur = ([bytes([SPACE])] if indent else []) + body
            cur_w = sum(advance(c, name_px) for c in cur)
        else:
            cur += w
            cur_w += w_px
    out.append(cur)
    return out


def wrap(raw: bytes, width: int, lines: int, english: bytes, name_px: int = NAME_WIDTH, characters=None) -> tuple[bytes, int]:
    """(the page rewrapped to the box, the number of screens added)."""
    indent_line, indent_screen = indents(english)
    screens, cur = [], []
    for c in commands(raw):                  # screens (split at waits) of lines (split at breaks)
        if c[0] == WAIT:
            screens.append((cur, c))
            cur = []
        else:
            cur.append(c)
    screens.append((cur, None))
    out, added = [], 0
    for body, wait in screens:
        rows, row = [], []
        for c in body:
            if c[0] == BREAK:
                rows.append(row)
                row = []
            else:
                row.append(c)
        rows.append(row)
        choice_at = next((i for i, r in enumerate(rows) if any(map(_is_choice, r))), None)
        wrapped = []
        for i, r in enumerate(rows):
            if choice_at is not None and i > choice_at:
                wrapped.append(r)            # a choice row
            else:
                wrapped += _wrap_line(r, width, name_px, indent_line, characters)
        groups = [wrapped]
        if choice_at is None and len(wrapped) > lines:
            groups = [wrapped[i:i + lines] for i in range(0, len(wrapped), lines)]
            added += len(groups) - 1
        for g, rows_ in enumerate(groups):
            if g:
                out.append(bytes([WAIT]))
                if indent_screen and rows_ and not (rows_[0] and rows_[0][0][0] == SPACE):
                    rows_[0] = [bytes([SPACE])] + rows_[0]
            for n, r in enumerate(rows_):
                if n:
                    out.append(bytes([BREAK]))
                out += r
        if wait is not None:
            out.append(wait)
    return b"".join(out), added
