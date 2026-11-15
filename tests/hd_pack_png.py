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

    def test_source_black_does_not_burn_a_contour_into_hd_material(self):
        import subprocess, sys
        try:
            import numpy as np
            import cv2
        except ImportError:
            self.skipTest("numpy and opencv required for matte authoring")
        root = self.root / "contours"
        (root / "refs").mkdir(parents=True)
        (root / "HD").mkdir()
        native = np.zeros((16, 32), np.uint16)
        native[2:14, 2:30] = 0x03e0
        native[2, 2:30] = native[13, 2:30] = 0x8000
        native[2:14, 2] = native[2:14, 29] = 0x8000
        # This interior black stripe crosses the packet boundary: retain it.
        native[6:10, 14:18] = 0x8000
        # The underlay also stores a black pixel just outside a foreground
        # silhouette. Its own coverage is solid here: use the complete view.
        native[7, 7] = 0x8000
        refs = np.stack((native[:, :16], native[:, 16:]))
        np.save(root / "refs/ref.npy", refs)
        foreground = np.zeros_like(native)
        foreground[5:11, 8:13] = 0x03e0
        np.save(root / "refs/front.npy", np.stack((foreground[:, :16], foreground[:, 16:])))
        colour = (220, 80, 30)
        Image.new("RGB", (128, 64), colour).save(root / "HD/source.png")
        script = Path(__file__).resolve().parents[1] / "docs/assets/hd-poc/pack.py"
        for semi in (False, True):
            key = "0123456789abcdef"
            task = dict(room="M001", group=0, entry=0, control=None,
                        key=key, input="source.png", ref="ref.npy",
                        tiles=[[0, 0], [16, 0]], tile_meta=[dict(semi=semi)]*2)
            front = dict(task, key="fedcba9876543210", ref="front.npy", entry=1)
            (root / "tasks.json").write_text(json.dumps(dict(tasks=[task, front])))
            destination = root / ("semi" if semi else "opaque")
            subprocess.run([sys.executable, str(script), "HD", "--root", str(root),
                            "--destination", str(destination), "--coverage-aa",
                            "--preserve-source-black"], check=True, capture_output=True)
            rgba = np.array(Image.open(destination / (key+".png")))
            expected_edge = (0, 0, 0) if semi else colour
            for i in range(2):
                x = i*68+2
                # Black contour is material colour only, never a new hole.
                self.assertEqual(tuple(rgba[12, x+12, :3]), expected_edge)
                self.assertEqual(int(rgba[12, x+12, 3]), 255)
                # Interior black at both sides of the tile cut stays black.
                inside = x+60 if i == 0 else x
                self.assertEqual(tuple(rgba[30, inside, :3]), (0, 0, 0))
                rgb = np.stack([(refs[i]&31)*255//31,
                                ((refs[i]>>5)&31)*255//31,
                                ((refs[i]>>10)&31)*255//31], axis=-1).astype("uint8")
                expected = np.repeat(np.repeat(rgb, 4, 0), 4, 1)
                self.assertEqual(rgba[70:134, x:x+64, :3].tobytes(), expected.tobytes())
            self.assertEqual(tuple(rgba[32, 32, :3]), (0, 0, 0) if semi else colour)
            self.assertEqual(int(rgba[2, 2, 3]), 0)

    def test_underlay_fills_gaps_at_other_static_layers_only(self):
        import subprocess, sys, struct
        try:
            import numpy as np
            import cv2
        except ImportError:
            self.skipTest("numpy and opencv required for underlay authoring")
        root = self.root / "underlays"
        assets, baseline, destination = root/"assets", root/"baseline", root/"fixed"
        (assets/"refs").mkdir(parents=True)
        baseline.mkdir()
        first = np.zeros((32,64), bool)
        first[4:28,4:20] = True
        for y in range(4,28): first[y,4:4+(y-4)//2] = False
        other = np.zeros_like(first)
        other[4:28,40:56] = True
        background = cv2.dilate((first|other).astype(np.uint8), np.ones((3,3),np.uint8)) == 0
        tiles = [[x,y] for y in range(0,32,16) for x in range(0,64,16)]
        tasks, records = [], []
        for entry, mask in [(0,first),(1,other),(12,background)]:
            key = f"{entry+1:016x}"
            native = np.where(mask,0x03e0,0).astype(np.uint16)
            if entry == 12:
                native[15,39] = 0x8000  # opaque black at the foreground edge
                native[0,60] = 0x8000   # unrelated black detail: retain it
            refs = np.stack([native[y:y+16,x:x+16] for x,y in tiles])
            np.save(assets/"refs"/(key+".npy"),refs)
            task = dict(room="M092",group=0,entry=entry,key=key,size=[64,32],
                        control=None,controls=[],tiles=tiles,ref=key+".npy",
                        tile_meta=[dict(semi=False)]*len(tiles))
            tasks.append(task)
            atlas = np.zeros((136,len(tiles)*68,4),np.uint8)
            atlas[:68,:,:3] = (80,100,120) if entry==12 else (200,80,30)
            atlas[:,:,3] = 255
            for i,ref in enumerate(refs):
                if entry == 12:
                    patch = atlas[2:66,i*68+2:i*68+66,:3]
                    patch[np.repeat(np.repeat(ref==0x8000,4,0),4,1)] = 0
                rgb = np.stack([(ref&31)*255//31,((ref>>5)&31)*255//31,
                                ((ref>>10)&31)*255//31],axis=-1).astype(np.uint8)
                rgb = np.repeat(np.repeat(rgb,4,0),4,1)
                atlas[68:136,i*68:(i+1)*68,:3] = np.pad(rgb,((2,2),(2,2),(0,0)),mode="edge")
            path = baseline/(key+".png")
            Image.fromarray(atlas).save(path)
            reference = baseline/(key+".ref.png")
            Image.fromarray(atlas[68:]).save(reference)
            records.append(dict(key=key,file=path.name,reference_file=reference.name))
        (assets/"tasks.json").write_text(json.dumps(dict(tasks=tasks)))
        (baseline/"manifest.json").write_text(json.dumps(dict(atlases=records)))
        before = {p.name:p.read_bytes() for p in baseline.iterdir()}
        script = Path(__file__).resolve().parents[1]/"docs/assets/hd-animated/corrections/issue-56/build_underlays.py"
        result = subprocess.run([sys.executable,str(script),"--assets",str(assets),
                        "--baseline",str(baseline),"--out",str(destination),
                        "--rooms","M092"],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr.decode())
        descriptor = (destination/"2c48651307e4dc15-under.bin").read_bytes()
        group,count,key,left,right,top,bottom = struct.unpack_from("<IIQiiii",descriptor,8)
        cells = [struct.unpack_from("<hh",descriptor,40+i*4) for i in range(count)]
        atlas = np.array(Image.open(destination/f"{key:016x}.png"))
        columns = atlas.shape[1]//68
        world = np.zeros((128,256,4),np.uint8)
        for i,(x,y) in enumerate(cells):
            ax,ay = i%columns*68+2,i//columns*68+2
            world[y*4:y*4+64,x*4:x*4+64] = atlas[ay:ay+64,ax:ax+64]
        # An unreported second foreground needs the same gutter as entry 0.
        self.assertEqual(tuple(world[56,158]),(80,100,120,255))
        # No broad replacement under the object's interior or distant voids.
        self.assertEqual(int(world[60,180,3]),0)
        self.assertEqual(int(world[0,252,3]),0)
        corrected_base = np.array(Image.open(destination/"000000000000000d.png"))
        def base_pixel(x,y):
            i = (y//16)*4+x//16
            return tuple(corrected_base[2+(y%16)*4,i*68+2+(x%16)*4,:3])
        self.assertEqual(base_pixel(39,15),(80,100,120))
        self.assertEqual(base_pixel(60,0),(0,0,0))
        with Image.open(baseline/"000000000000000d.png") as original:
            self.assertEqual(corrected_base[68:].tobytes(), np.array(original)[68:].tobytes())
        self.assertEqual(before,{p.name:p.read_bytes() for p in baseline.iterdir()})
        audit = json.loads((destination.parent/(destination.name+"-audit.json")).read_text())
        self.assertGreater(audit[0]["static_edge_gutter_pixels"],0)

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
