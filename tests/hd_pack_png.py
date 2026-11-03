"""Offline PNG pack conversion regression; requires Pillow."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image

spec = importlib.util.spec_from_file_location("compress_pack", Path(__file__).resolve().parents[1] / "docs/assets/hd-poc/compress_pack.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackConversion(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.destination = self.root / "converted"
        records = []
        for index, mode in enumerate(("RGB", "RGBA")):
            image = Image.new(mode, (68, 136))
            image.putdata([(i % 256, (i * 7) % 256, (i * 13) % 256) + ((i % 256,) if mode == "RGBA" else ()) for i in range(68 * 136)])
            key = f"{index + 1:016x}"
            path = self.source / (key + (".bmp" if mode == "RGB" else ".png"))
            image.save(path)
            records.append(dict(key=key, file=path.name, atlas_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), room="M001", tiles=[[0, 0]]))
        self.manifest = dict(atlases=records, custom="preserve metadata")
        self.write_manifest()

    def write_manifest(self):
        (self.source / "manifest.json").write_text(json.dumps(self.manifest))

    def test_lossless_and_source_preserved(self):
        before = {p.name: p.read_bytes() for p in self.source.iterdir()}
        result = module.convert(self.source, self.destination, version="0.1")
        after = json.loads((self.destination / "manifest.json").read_text())
        self.assertEqual(after["custom"], "preserve metadata")
        self.assertEqual(after["version"], "0.1")
        self.assertEqual(result["files"], 2)
        for record in after["atlases"]:
            source_record = next(r for r in self.manifest["atlases"] if r["key"] == record["key"])
            with Image.open(self.source / source_record["file"]) as original, Image.open(self.destination / record["file"]) as png:
                self.assertEqual(original.convert("RGBA").tobytes(), png.convert("RGBA").tobytes())
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.source.iterdir()})

    def test_authored_matte_keeps_native_reference_and_tile_boundaries(self):
        import subprocess, sys
        try:
            import numpy as np
            import cv2
        except ImportError:
            self.skipTest("numpy and opencv required for matte authoring")
        root = self.root / "authoring"
        (root / "refs").mkdir(parents=True)
        (root / "HD").mkdir()
        key = "0123456789abcdef"
        refs = np.full((2,16,16), 0x03e0, np.uint16)
        # A diagonal hole crosses a tile boundary. The packed matte must be
        # calculated on the complete native entry, never each tile separately.
        for x in range(32):
            for y in range(16):
                if y > x // 2: refs[x//16,y,x%16] = 0
        refs[0,0,0] = 0x8000 # opaque black is coverage, not a transparent hole
        np.save(root / "refs/ref.npy", refs)
        Image.new("RGB",(128,64),(220,80,30)).save(root / "HD/source.png")
        task = dict(room="M001",group=0,entry=0,control=None,key=key,input="source.png",ref="ref.npy",tiles=[[0,0],[16,0]])
        (root / "tasks.json").write_text(json.dumps(dict(tasks=[task])))
        script = Path(__file__).resolve().parents[1] / "docs/assets/hd-poc/pack.py"
        subprocess.run([sys.executable,str(script),"HD","--root",str(root),"--coverage-aa"],check=True,capture_output=True)
        with Image.open(root / "pack-HD" / (key+".png")) as image:
            rgba = np.array(image)
            self.assertEqual(image.mode,"RGBA")
            self.assertEqual(int(rgba[2,2,3]),255)
            top = rgba[:68]
            self.assertTrue(np.any((top[:,:,3]>0)&(top[:,:,3]<255)))
            for i, ref in enumerate(refs):
                native_rgb = np.stack([(ref&31)*255//31,((ref>>5)&31)*255//31,((ref>>10)&31)*255//31],axis=-1).astype("uint8")
                reference = rgba[70:134,i*68+2:i*68+66]
                expected = np.repeat(np.repeat(native_rgb,4,0),4,1)
                self.assertEqual(reference[:,:,:3].tobytes(), expected.tobytes())
                self.assertTrue(np.all(reference[:,:,3]==255))
            # Far from the diagonal no accidental transparent tile seam.
            self.assertTrue(np.all(top[2:6,62:74,3]==255))

    def test_bad_hash_never_activates_partial_pack(self):
        self.manifest["atlases"][0]["atlas_sha256"] = "0" * 64
        self.write_manifest()
        with self.assertRaises(ValueError): module.convert(self.source, self.destination)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob("converted.partial-*")))

    def test_title_and_widescreen_extras_are_preserved(self):
        self.manifest["extras"] = []
        for filename in ("title.png", "0123456789abcdef-wide.bin"):
            content = b"checked pack extra " + filename.encode()
            (self.source / filename).write_bytes(content)
            self.manifest["extras"].append(dict(file=filename, sha256=hashlib.sha256(content).hexdigest()))
        self.write_manifest()
        module.convert(self.source, self.destination)
        for extra in self.manifest["extras"]:
            self.assertEqual((self.source / extra["file"]).read_bytes(), (self.destination / extra["file"]).read_bytes())

    def test_extra_checksum_and_path_fail_atomically(self):
        for filename in ("title.png", "../outside.bin", "manifest.json", "C:title.png", "0000000000000001.PNG"):
            self.manifest["extras"] = [dict(file=filename, sha256="0"*64)]
            if filename == "title.png": (self.source / filename).write_bytes(b"damaged title")
            self.write_manifest()
            with self.assertRaises(ValueError): module.convert(self.source, self.destination)
            self.assertFalse(self.destination.exists())
            self.assertFalse(list(self.root.glob("converted.partial-*")))

    def test_existing_pack_is_not_overwritten(self):
        self.destination.mkdir()
        marker = self.destination / "working-version"
        marker.write_text("keep")
        with self.assertRaises(FileExistsError): module.convert(self.source, self.destination)
        self.assertEqual(marker.read_text(), "keep")

    def test_unsafe_manifest_path_is_rejected(self):
        self.manifest["atlases"][0]["file"] = "../outside.bmp"
        self.write_manifest()
        with self.assertRaises(ValueError): module.convert(self.source, self.destination)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__": unittest.main()
