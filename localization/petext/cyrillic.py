"""Russian, Ukrainian and Belarusian: native lookalikes plus 52 authored 12x12 glyphs.

No font profile or pack-format change: Latin 0x4D..0xBB stays stable, new art is
0xBC..0xEF, and visually identical letters use existing ASCII/Latin glyphs.
Uppercase art occupies rows 1..8; lowercase 3..8, with descenders through row 10.
Only these stroke patterns are authored here; no disc font pixels are distributed.
"""
from dataclasses import dataclass
from pathlib import Path
from . import latin

RUSSIAN = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
UKRAINIAN_EXTRA = "ЄІЇҐ"
BELARUSIAN_EXTRA = "Ў"
ALPHABET = "".join(s + s.lower() for s in (RUSSIAN, UKRAINIAN_EXTRA, BELARUSIAN_EXTRA))
ALIASES = dict(zip("АВЕКМНОРСТХаеорсух", "ABEKMHOPCTXaeopcyx"))
ALIASES.update({"Ё":"Ë", "ё":"ë", "І":"I", "і":"i", "Ї":"Ï", "ї":"ï"})
FIRST_CODE, CELL = 0xBC, 12

# Each pattern starts at column 1. Dot is empty; # is a stroke. Shadow is generated.
PATTERNS = {
"Б": (1,"#####/#..../#..../####./#...#/#...#/#...#/####."),
"Г": (1,"#####/#..../#..../#..../#..../#..../#..../#...."),
"Д": (1,"..####./..#..#./..#..#./..#..#./..#..#./.#...#./#######/#.....#/#.....#"),
"Ж": (1,"#...#...#/.#..#..#./..#.#.#../...###.../..#.#.#../.#..#..#./#...#...#/#...#...#"),
"З": (1,".###./#...#/....#/..##./....#/....#/#...#/.###."),
"И": (1,"#....#/#....#/#...##/#..#.#/#.#..#/##...#/#....#/#....#"),
"Й": (0,".#..#./..##../#....#/#...##/#..#.#/#.#..#/##...#/#....#/#....#"),
"Л": (1,"..####/..#..#/..#..#/..#..#/..#..#/..#..#/.#...#/#....#"),
"П": (1,"######/#....#/#....#/#....#/#....#/#....#/#....#/#....#"),
"У": (1,"#....#/#....#/.#..#./..#.#./...#../...#../..#.../##...."),
"Ф": (1,"...#.../.#####./#..#..#/#..#..#/#..#..#/.#####./...#.../...#..."),
"Ц": (1,"#....#./#....#./#....#./#....#./#....#./#....#./#....#./#######/......#/......#"),
"Ч": (1,"#....#/#....#/#....#/.#####/.....#/.....#/.....#/.....#"),
"Ш": (1,"#..#..#/#..#..#/#..#..#/#..#..#/#..#..#/#..#..#/#..#..#/#######"),
"Щ": (1,"#..#..#./#..#..#./#..#..#./#..#..#./#..#..#./#..#..#./#..#..#./########/.......#/.......#"),
"Ъ": (1,"###..../..#..../..#..../..####./..#...#/..#...#/..#...#/..####."),
"Ы": (1,"#......#/#......#/#......#/####...#/#...#..#/#...#..#/#...#..#/####...#"),
"Ь": (1,"#..../#..../#..../####./#...#/#...#/#...#/####."),
"Э": (1,".###./#...#/....#/..###/....#/....#/#...#/.###."),
"Ю": (1,"#...###./#..#...#/#..#...#/####...#/#..#...#/#..#...#/#..#...#/#...###."),
"Я": (1,".#####/#....#/#....#/.#####/...#.#/..#..#/.#...#/#....#"),
"б": (1,"...##/..#../.#.../.###./#...#/#...#/#...#/.###."),
"в": (3,"####./#...#/####./#...#/#...#/####."),
"г": (3,"####/#.../#.../#.../#.../#..."),
"д": (3,"..###./..#.#./..#.#./.#..#./.#..#./######/#....#/#....#"),
"ж": (3,"#..#..#/.#.#.#./..###../.#.#.#./#..#..#/#..#..#"),
"з": (3,"###./...#/.##./...#/...#/###."),
"и": (3,"#...#/#..##/#.#.#/##..#/#...#/#...#"),
"й": (0,".#.#./..#../...../#...#/#..##/#.#.#/##..#/#...#/#...#"),
"к": (3,"#...#/#..#./###../#..#./#...#/#...#"),
"л": (3,"..###/..#.#/..#.#/..#.#/.#..#/#...#"),
"м": (3,"#.....#/##...##/#.#.#.#/#..#..#/#.....#/#.....#"),
"н": (3,"#...#/#...#/#####/#...#/#...#/#...#"),
"п": (3,"#####/#...#/#...#/#...#/#...#/#...#"),
"т": (3,"#####/..#../..#../..#../..#../..#.."),
"ф": (1,"...#.../...#.../.#####./#..#..#/#..#..#/#..#..#/#..#..#/.#####./...#.../...#..."),
"ц": (3,"#...#./#...#./#...#./#...#./#...#./######/.....#/.....#"),
"ч": (3,"#...#/#...#/.####/....#/....#/....#"),
"ш": (3,"#..#..#/#..#..#/#..#..#/#..#..#/#..#..#/#######"),
"щ": (3,"#..#..#./#..#..#./#..#..#./#..#..#./#..#..#./########/.......#/.......#"),
"ъ": (3,"##..../.#..../.####./.#...#/.#...#/.####."),
"ы": (3,"#.....#/#.....#/####..#/#...#.#/#...#.#/####..#"),
"ь": (3,"#..../#..../####./#...#/#...#/####."),
"э": (3,".###./#...#/..###/....#/#...#/.###."),
"ю": (3,"#..###./#.#...#/###...#/#.#...#/#.#...#/#..###."),
"я": (3,".####/#...#/.####/..#.#/.#..#/#...#"),
"Є": (1,".####/#..../#..../####./#..../#..../#..../.####"),
"є": (3,".###./#...#/###../#..../#...#/.###."),
"Ґ": (0,"....#/#####/#..../#..../#..../#..../#..../#..../#...."),
"ґ": (2,"...#/####/#.../#.../#.../#.../#..."),
"Ў": (0,".#..#./..##../#....#/.#..#./..#.#./...#../...#../..#.../##...."),
"ў": (0,".#.#./..#../...../#...#/#...#/.#.#./..#../..#../.#.../#...."),
}

@dataclass(frozen=True)
class Glyph:
    char: str
    code: int
    rows: tuple[int, ...]
    advance: int

def _glyphs():
    out=[]
    for c in ALPHABET:
        if c in ALIASES: continue
        top, pattern=PATTERNS[c]
        lines=pattern.split("/")
        assert top+len(lines)<=11 and max(map(len,lines))<=10
        rows=[0]*CELL
        for y,line in enumerate(lines,top):
            for x,pixel in enumerate(line,1):
                if pixel=="#":rows[y]|=1<<x
        right=max(x for bits in rows for x in range(CELL) if bits&(1<<x))
        out.append(Glyph(c,FIRST_CODE+len(out),tuple(rows),right+2))
    return out

GLYPHS=_glyphs()
BY_CODE={g.code:g for g in GLYPHS}
CODE_OF={g.char:g.code for g in GLYPHS}
def _existing(c):
    if c in latin.CODE_OF:return latin.CODE_OF[c]
    if 'A'<=c<='Z':return 0x10+ord(c)-ord('A')
    if 'a'<=c<='z':return 0x30+ord(c)-ord('a')
    raise ValueError(c)
CODE_OF.update({c:_existing(base) for c,base in ALIASES.items()})
assert len(GLYPHS)==52 and GLYPHS[-1].code==0xEF
assert latin.GLYPHS[-1].code<FIRST_CODE and GLYPHS[-1].code<=0xF0
assert len(CODE_OF)==len(ALPHABET)

def compose(g):
    strokes=[int(bool(g.rows[y]&(1<<x))) for y in range(CELL) for x in range(CELL)]
    return latin._shadow(strokes)

def write_c_header(path):
    lines=['/* Generated by pelang.py cyrillic-header; authored art in petext/cyrillic.py. */',
           '#ifndef PE_LANG_CYRILLIC_DATA_H','#define PE_LANG_CYRILLIC_DATA_H',
           '#define PE_CYRILLIC_FIRST_CODE 0xBC','#define PE_CYRILLIC_LAST_CODE 0xEF',
           '#define PE_CYRILLIC_COUNT 52',
           'typedef struct PeCyrillicGlyph { unsigned char code, advance; unsigned short rows[12]; } PeCyrillicGlyph;',
           'static const PeCyrillicGlyph kPeCyrillicGlyphs[] = {']
    for g in GLYPHS:
        lines.append('    {0x%02X, %d, {%s}}, /* U+%04X */'%(g.code,g.advance,', '.join('0x%03X'%row for row in g.rows),ord(g.char)))
    lines+=['};','#endif','']
    Path(path).write_text('\n'.join(lines),encoding='utf-8')
