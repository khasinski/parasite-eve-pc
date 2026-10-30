"""Explicit translation keys and single-line layout contracts for mod-owned game text."""
import json
from . import gametext as gt, interfaces

def load(path, strings, _stack=()):
    if path is None:
        return strings
    if path.resolve() in _stack:
        raise ValueError("cyclic mod-string manifest include")
    _stack = (*_stack,path.resolve())
    out = dict(strings)
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(manifest, dict):
        for include in manifest['includes']:
            out = load(path.parent / include, out, _stack)
        return out
    for row in manifest:
        key = row.get("key", "")
        if not key.startswith("mod/") or len(key.split("/")) < 3 or key in out:
            raise ValueError(f"invalid or duplicate mod translation key: {key}")
        if key.startswith("mod/ui/") and key != interfaces.key(row.get("source", "")):
            raise ValueError(f"{key}: interface key does not match its source")
        raw = gt.encode(row["text"])
        if any(c >= 0xF1 for c in raw):
            raise ValueError(f"{key}: mod label fallback must be plain single-line text")
        cap = row.get("max_glyphs")
        width = row.get("width")
        if type(cap) is not int or not 1 <= cap <= 128 or type(width) is not int or not 1 <= width <= 320:
            raise ValueError(f"{key}: invalid mod label limits")
        out[key] = dict(row, raw=raw.hex(), mod_label=True, lines_px=gt.lines(raw))
    return out

def check(raw, source, metrics=None):
    from . import menu
    from .validation import commands
    glyphs = list(commands(raw))
    if any(c[0] > 0xF1 for c in glyphs):
        return ["mod labels cannot contain line breaks, waits or commands"]
    errors = interfaces.check(source.get('format',source.get('text','')),raw)
    if len(glyphs) > source["max_glyphs"]:
        errors.append(f'mod label has {len(glyphs)} glyphs; limit is {source["max_glyphs"]}')
    width = menu.width(raw, metrics) if metrics is not None else max(gt.lines(raw))
    if width > source["width"]:
        errors.append(f'mod label is {width} px; limit is {source["width"]} px')
    return errors
