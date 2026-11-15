"""Build the Parasite Eve Translator download for the system this runs on, and check it.

  python -m pip install -r localization/translator/requirements-download.txt
  python -I localization/translator/package.py --out OUT [--english "English pack.pelang"] [--packs DIR]

What it makes (in OUT), named with the release version (CMakeLists.txt's project VERSION, as the
game's and the Save Editor's release files are: 0.12.0 -> 0.12):
  Windows  Parasite-Eve-Translator-0.12-windows-x86_64.exe   one program file
  macOS    Parasite-Eve-Translator-0.12-macos-<chip>.zip      one Parasite Eve Translator.app
  Linux    Parasite-Eve-Translator-0.12-linux-x86_64.tar.gz   one program file
and check/ with what the check saw (report.json, the window's picture, the file it saved).

The check, on the built program itself (no Python of this computer involved):
  1. Start it with a new translation, type a line into a row through the window, save, quit
     (translator/app.py --report). With --english it also loads that English pack first.
  2. The file it saved must be byte for byte what this source tool saves from the same line.
     Then the letters PNG: a new Greek translation saves it (it must look as the source tool's),
     every square painted it is loaded back, and the file must be the source tool's too.
  3. Its data files must be exactly translator/project.py DATA_FILES, and no part of it may hold
     game text: every file and every packed part (opened all the way down) is searched for a
     pack's "PELG" header (a pack stores runs of letters packed, so a pack is found by its header),
     and for the English pack's lines (--english) and the translations' lines (--packs: a folder
     of .pelang) as the text people read and as the game's own letter codes.
Exits 1 on any failure.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import marshal
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCALIZATION = HERE.parent
REPO = LOCALIZATION.parent
sys.path.insert(0, str(LOCALIZATION))
import pelang  # noqa: E402
from translator import project as P  # noqa: E402

NAME = "Parasite Eve Translator"
DOWNLOAD = "Parasite-Eve-Translator"
PINNED = dict(line.split("==") for line in (HERE / "requirements-download.txt").read_text().split()
              if "==" in line and not line.startswith("#"))
TYPED = "Une ligne tapée, à l'essai."         # fits the box as it is: passes with no warning
LANG, LANG_NAME = "fr", "Français"
SHORTEST = 24                                # game text pieces this long or longer are searched for
GRAM = SHORTEST // 2


def target() -> str:
    machine = platform.machine().lower()
    chip = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    system = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux" if sys.platform.startswith("linux") else sys.platform)
    return f"{system}-{chip}"


def release_version(cmake: Path = REPO / "CMakeLists.txt") -> str:
    """The release's version as its file names carry it: project(... VERSION 0.12.0 ...) -> "0.12"."""
    found = re.search(r"^project\(parasite-eve-pc\s+VERSION\s+(\d+)\.(\d+)(?:\.\d+)*\s",
                      cmake.read_text(encoding="utf-8"), re.M)
    if not found:
        fail(f"no project(parasite-eve-pc VERSION x.y.z) in {cmake}")
    return f"{found[1]}.{found[2]}"


def fail(why: str) -> None:
    raise SystemExit(f"FAILED: {why}")


# ---- 1. build ----------------------------------------------------------------------------------------

def check_versions() -> None:
    from importlib.metadata import version
    have = {name: version(name) for name in PINNED}
    if have != PINNED:
        fail(f"build tools {have} are not the pinned {PINNED} (requirements-download.txt)")


def build(work: Path) -> Path:
    """PyInstaller: one file on Windows and Linux; on macOS one .app (a folder the system shows as
    one program). Returns the built program."""
    mac = sys.platform == "darwin"
    args = [sys.executable, "-I", "-m", "PyInstaller", "--noconfirm", "--clean", "--windowed",
            "--onedir" if mac else "--onefile", "--name", NAME, "--paths", str(LOCALIZATION),
            "--distpath", str(work / "dist"), "--workpath", str(work / "build"), "--specpath", str(work)]
    for data in P.DATA_FILES:
        args += ["--add-data", f"{LOCALIZATION / data}{os.pathsep}{Path(data).parent.as_posix()}"]
    if mac:
        args += ["--osx-bundle-identifier", "io.github.parasite-eve-pc.translator"]
    args.append(str(LOCALIZATION / "translate.py"))
    started = time.monotonic()
    subprocess.run(args, check=True)
    print(f"built in {time.monotonic() - started:.0f} s")
    dist = work / "dist"
    built = dist / f"{NAME}.app" if mac else dist / (f"{NAME}.exe" if sys.platform == "win32" else NAME)
    if not built.exists():
        fail(f"PyInstaller made no {built.name}")
    return built


def program_of(built: Path) -> Path:
    return built / "Contents" / "MacOS" / NAME if built.suffix == ".app" else built


# ---- 2. the built program, used --------------------------------------------------------------------

def first_plain_row(game: P.Game) -> str:
    return next(k for k in game.keys if k.startswith("dlg/") and game.rows[k].get("structure") == []
                and game.rows[k].get("boxes") == ["default"])


def use_it(built: Path, check: Path, english: Path | None) -> dict:
    game = P.Game()
    key = first_plain_row(game)
    saved, report, shot = check / f"Parasite Eve - {LANG_NAME}.pelang", check / "report.json", check / "window.png"
    for old in (saved, report, shot, check / "settings.json"):
        old.unlink(missing_ok=True)
    args = [str(program_of(built)), str(saved), "--new", LANG, LANG_NAME, "--select", key, "--type", TYPED,
            "--settings", str(check / "settings.json"), "--report", str(report), "--screenshot", str(shot)]
    if english:
        args += ["--english", str(english)]
    started = time.monotonic()
    done = subprocess.run(args, timeout=600, capture_output=True, text=True)
    seconds = time.monotonic() - started
    if not report.is_file():
        fail(f"the program wrote no report (exit {done.returncode}): {done.stdout[-600:]} {done.stderr[-600:]}")
    said = json.loads(report.read_text(encoding="utf-8"))
    print(json.dumps(said, ensure_ascii=False, indent=1))
    problems = []
    if done.returncode != 0 or not said.get("ok"):
        problems.append(f"the run failed (exit {done.returncode}): {said.get('error')}")
    if not said.get("frozen"):
        problems.append("the report doesn't come from the packaged program")
    if (said.get("row"), said.get("text_in_row"), said.get("row_state")) != (key, TYPED, "ok"):
        problems.append("the typed line did not land in its row, or did not pass")
    if (said.get("in_pack"), said.get("left_out")) != (1, {}):
        problems.append("the saved file doesn't hold the one typed line")
    if english and said.get("english_rows") != len(P.read_english(english, game)[0]):
        problems.append("the English pack did not load whole")
    if not problems:
        same = P.Translation(game, LANG, LANG_NAME)
        same.set_text(key, TYPED)
        with tempfile.TemporaryDirectory() as tmp:
            same.save(Path(tmp) / "same.pelang")
            if saved.read_bytes() != (Path(tmp) / "same.pelang").read_bytes():
                problems.append("the saved file is not what the source tool saves from the same line")
    if problems:
        fail("; ".join(problems))
    print(f"the built program: started, typed, saved and quit in {seconds:.1f} s; "
          f"its file is byte for byte the source tool's")
    return said


LETTERS_LANG, LETTERS_NAME = "el", "Ελληνικά"


def made_up_letter(k: int) -> tuple[str, ...]:
    """A made-up letter for square k (12 rows of '.'/'#'), each one different."""
    w = 3 + k % 6
    pts = {(1, y) for y in range(2, 9)} | {(w, y) for y in range(2 + k % 3, 9)} | {(x, 8) for x in range(1, w + 1)}
    pts |= {(x, 2 + k % 5) for x in range(1, w + 1)} | {(1 + (k // 6) % w, 1)}
    return tuple("".join("#" if (x, y) in pts else "." for x in range(12)) for y in range(12))


def letters_work(built: Path, check: Path) -> None:
    """The letters PNG in the built program: a new Greek translation saves its letters PNG
    (--save-letters), which must look as the source tool's; every square painted, the program loads
    it back (--letters) and saves; the file must be byte for byte what the source tool saves with the
    same letters."""
    from PIL import Image
    from translator import letterart as LA
    game = P.Game()
    saved, report = check / f"Parasite Eve - {LETTERS_NAME}.pelang", check / "letters-report.json"
    empty, painted = check / "letters.png", check / "letters-painted.png"
    for old in (saved, report, empty, painted):
        old.unlink(missing_ok=True)

    def run(*args) -> dict:
        report.unlink(missing_ok=True)
        done = subprocess.run([str(program_of(built)), str(saved), *args, "--settings", str(check / "settings.json"),
                               "--report", str(report)], timeout=600, capture_output=True, text=True)
        if not report.is_file():
            fail(f"letters: the program wrote no report (exit {done.returncode}): {done.stderr[-600:]}")
        said = json.loads(report.read_text(encoding="utf-8"))
        if done.returncode != 0 or not said.get("ok") or not said.get("frozen"):
            fail(f"letters: the run failed (exit {done.returncode}): {said.get('error')}")
        return said

    run("--new", LETTERS_LANG, LETTERS_NAME, "--save-letters", str(empty))
    rows = LA.wanted(LETTERS_LANG)
    shapes = LA.every_shape(rows)
    source = Image.open(io.BytesIO(LA.png(rows, "Greek", {}))).convert("RGB")
    theirs = Image.open(empty).convert("RGB")
    if theirs.size != source.size or theirs.tobytes() != source.tobytes():
        fail("letters: the built program's letters PNG is not the source tool's")
    letters = {s.char: made_up_letter(k) for k, s in enumerate(shapes)}
    img = theirs.copy()
    for shape, (x, y) in LA.squares(rows, False).items():
        for yy, row in enumerate(letters[shape.char]):
            for xx, v in enumerate(row):
                if v == "#":
                    img.putpixel((x + xx, y + yy), (255, 255, 255))
    img.save(painted)
    said = run("--letters", str(painted))
    if said.get("letters_drawn") != sorted(letters) or said["letters"].get("notes"):
        fail(f"letters: the built program did not take every painted letter: {said.get('letters')}")
    same = P.Translation(game, LETTERS_LANG, LETTERS_NAME)
    for ch, strokes in letters.items():
        same.set_letter(ch, strokes)
    with tempfile.TemporaryDirectory() as tmp:
        same.save(Path(tmp) / "same.pelang")
        if saved.read_bytes() != (Path(tmp) / "same.pelang").read_bytes():
            fail("letters: the saved file is not what the source tool saves with the same letters")
    print(f"the built program: saved the {len(shapes)} Greek letters as a PNG (as the source tool draws it), "
          "loaded them painted, and its file is byte for byte the source tool's")


# ---- 3. what is inside ---------------------------------------------------------------------------------

class Needles:
    """Game text to look for. Indexed by GRAM-byte pieces: a needle of SHORTEST bytes or more
    covers one GRAM-aligned piece of any blob it sits in, so a blob is looked at every GRAM bytes."""

    def __init__(self) -> None:
        self.where: dict[bytes, str] = {}
        self.grams: dict[bytes, list[tuple[bytes, int]]] = {}

    def add(self, needle: bytes, source: str) -> None:
        if len(needle) < SHORTEST or needle in self.where:
            return
        self.where[needle] = source
        for at in range(len(needle) - GRAM + 1):
            self.grams.setdefault(needle[at:at + GRAM], []).append((needle, at))

    def add_pack(self, path: Path, game: P.Game) -> None:
        for raw in pelang.read_pack(path)[2].values():
            self.add(raw, f"{path.name} (letter codes)")
        for text in P.read_pack_text(path, game).texts.values():
            for piece in re.split(r"\{[^}]*\}", text):
                self.add(piece.encode("utf-8"), f"{path.name} (text)")

    def find(self, blob: bytes) -> list[str]:
        hits = []
        for i in range(0, max(0, len(blob) - GRAM + 1), GRAM):
            for needle, at in self.grams.get(blob[i:i + GRAM], ()):
                start = i - at
                if start >= 0 and blob[start:start + len(needle)] == needle:
                    hits.append(self.where[needle])
        return hits


def parts(name: str, blob: bytes, depth: int = 0):
    """(name, bytes) of a file and of everything packed in it: PyInstaller archives, their
    compiled modules, and zips."""
    yield name, blob
    if depth > 4:
        return
    if blob.startswith(b"PK\x03\x04") or b"PK\x05\x06" in blob[-66000:]:
        try:
            with zipfile.ZipFile(io.BytesIO(blob)) as z:
                for info in z.infolist():
                    if not info.is_dir():
                        yield from parts(f"{name}/{info.filename}", z.read(info), depth + 1)
            return
        except (zipfile.BadZipFile, NotImplementedError, OSError):
            pass
    if b"MEI\014\013\012\013\016" not in blob:      # PyInstaller's archive mark
        return
    from PyInstaller.archive.readers import CArchiveReader, NotAnArchiveError, ArchiveReadError
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(blob)
    try:
        try:
            archive = CArchiveReader(f.name)
        except (NotAnArchiveError, ArchiveReadError, ValueError, OSError):
            return
        for entry, info in archive.toc.items():
            if info[-1] == "z":
                pyz = archive.open_embedded_archive(entry)
                for module in pyz.toc:
                    code = pyz.extract(module)
                    yield f"{name}/{entry}/{module}", marshal.dumps(code) if code is not None else b""
            else:
                yield from parts(f"{name}/{entry}", archive.extract(entry), depth + 1)
    finally:
        os.unlink(f.name)


def files_of(built: Path):
    if built.is_dir():
        for path in sorted(built.rglob("*")):
            if path.is_file() and not path.is_symlink():
                yield path.relative_to(built).as_posix(), path.read_bytes()
    else:
        yield built.name, built.read_bytes()


def look_inside(built: Path, needles: Needles) -> None:
    data, hits, count, size = set(), [], 0, 0
    for name, blob in (p for f in files_of(built) for p in parts(*f)):
        count, size = count + 1, size + len(blob)
        bits = name.replace("\\", "/").split("/")
        if "data" in bits[:-1] and bits[-1].endswith(".json"):
            data.add("data/" + "/".join(bits[bits.index("data") + 1:]))
        if blob[:4] == b"PELG":
            hits.append(f"{name}: a language pack")
        hits += [f"{name}: {source}" for source in sorted(set(needles.find(blob)))]
    print(f"looked inside: {count:,} parts, {size / 1e6:.0f} MB unpacked; "
          f"searched for {len(needles.where):,} pieces of game text")
    if data != set(P.DATA_FILES):
        fail(f"the data files inside are {sorted(data)}, not {sorted(P.DATA_FILES)}")
    if hits:
        fail("game text inside the download:\n  " + "\n  ".join(hits[:40]))
    print("inside: only the tool's own data files, and no game text")


# ---- 4. the download -----------------------------------------------------------------------------------

def make_download(built: Path, out: Path, version: str) -> Path:
    name = f"{DOWNLOAD}-{version}-{target()}"
    if built.suffix == ".app":
        download = out / f"{name}.zip"
        download.unlink(missing_ok=True)
        subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(built), str(download)], check=True)
    elif sys.platform == "win32":
        download = out / f"{name}.exe"
        shutil.copyfile(built, download)
    else:
        download = out / f"{name}.tar.gz"
        with tarfile.open(download, "w:gz") as tar:
            info = tar.gettarinfo(str(built), arcname=NAME)
            info.mode, info.uid, info.gid, info.uname, info.gname = 0o755, 0, 0, "", ""
            with open(built, "rb") as f:
                tar.addfile(info, f)
    digest = hashlib.sha256(download.read_bytes()).hexdigest()
    (out / f"{download.name}.sha256").write_text(f"{digest}  {download.name}\n", encoding="utf-8")
    print(f"download: {download} ({download.stat().st_size / 1e6:.1f} MB), sha256 {digest}")
    return download


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build and check the Parasite Eve Translator download")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--english", type=Path, help="the English pack: loaded by the check, its lines searched for")
    ap.add_argument("--packs", type=Path, help="a folder of .pelang translations whose lines are searched for")
    ap.add_argument("--built", type=Path, help="check this already built program instead of building")
    a = ap.parse_args(argv)
    version = release_version()
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=True)
    check = a.out / "check"
    check.mkdir(exist_ok=True)
    if a.english and not a.english.is_file():
        fail(f"no English pack at {a.english}")
    if a.built:
        built = a.built.resolve()
    else:
        check_versions()
        built = build(a.out / "work")
    use_it(built, check, a.english)
    letters_work(built, check)
    game, needles = P.Game(), Needles()
    for pack in ([a.english] if a.english else []) + (sorted(a.packs.glob("*.pelang")) if a.packs else []):
        needles.add_pack(pack, game)
    look_inside(built, needles)
    make_download(built, a.out, version)
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
