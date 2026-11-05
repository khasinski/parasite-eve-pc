"""Build and smoke-test the standalone editor on the current desktop platform."""
import argparse
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from test_roundtrip import run

EDITOR = Path(__file__).resolve().parent
APP = "Parasite Eve Translation Editor"
PLATFORMS = {"win32": "windows", "darwin": "macos", "linux": "linux"}


def build(output):
    system = PLATFORMS[sys.platform]
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "aarch64": "arm64"}.get(machine, machine)
    name = f"pe-translation-editor-{system}-{arch}"
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pe-editor-") as folder:
        work = Path(folder)
        dist = work / "dist"
        subprocess.run([
            sys.executable, "-m", "PyInstaller", "--noconfirm", "--onedir", "--windowed",
            "--name", APP, "--paths", str(EDITOR.parent),
            "--add-data", f"{EDITOR / 'catalog.json'}:.",
            "--add-data", f"{EDITOR / 'fonts'}:fonts",
            "--specpath", str(work), "--workpath", str(work / "work"),
            "--distpath", str(dist), str(EDITOR / "app.py"),
        ], cwd=work, check=True)
        if system == "macos":
            bundle = dist / (APP + ".app")
            executable = bundle / "Contents" / "MacOS" / APP
        else:
            bundle = dist / APP
            executable = bundle / (APP + (".exe" if system == "windows" else ""))
        proof = work / "smoke.txt"
        subprocess.run([str(executable), "--smoke", str(proof)], cwd=work, timeout=90, check=True)
        result = proof.read_text(encoding="utf-8")
        if not result.startswith("2 compiled box(es). Layout and control checks passed."):
            raise RuntimeError(f"Packaged demo did not pass: {result}")
        print(result)

        release = work / "release"
        release.mkdir()
        run(release)
        shutil.copy2(EDITOR / "START-HERE.md", release)
        shutil.copy2(EDITOR / "fonts" / "OFL.txt", release / "FONT-LICENSE.txt")
        shutil.copy2(EDITOR / "fonts" / "SOURCE.md", release / "FONT-SOURCE.md")
        if system == "macos":
            # Preserve the symlinks inside the signed application bundle.
            shutil.move(str(bundle), str(release / bundle.name))
        else:
            shutil.move(str(bundle), str(release / APP))
        archive = shutil.make_archive(str(output / name), "zip" if system == "windows" else "gztar",
                                      root_dir=release)
        print(f"Built and tested {archive}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    build(parser.parse_args().out)
