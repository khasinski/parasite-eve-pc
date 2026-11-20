"""Pack 0.5.2: pack 0.5.1 with M163's two static atlases rebuilt from a re-registered anchor (#269, #268).

M163's generated anchor 4f85e561 placed the restaurant block, its awning and the hanging
signs elsewhere than the source (registration review: scale 0.92, 25% inliers). One global
affine cannot fix a local layout difference, so the HD restaurant was drawn over the street
(#268) and its cut edges left black texels along the sign and awning (#269). The new anchor
(-v2b, built-in imagegen with the recorded prompt plus a layout note) registers with 86%
inliers; assemble_sources.py and hd-poc/pack.py (--room M163, same flags as the release)
rebuild the view. Only atlases whose material changed are replaced; native references,
every other room and every other file stay byte identical. Materials are encoded exactly as
issue-56/encode_webp.py does (cwebp -q 95 -m 6 -exact).
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
import numpy as np
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument("--baseline", type=Path, required=True)       # published 0.5.1
p.add_argument("--atlases", type=Path, required=True)        # hd-poc/pack.py --room M163 output (BMP)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--version", default="0.5.2")
a = p.parse_args()
assert not a.output.exists(), "output must be a new folder"
m = json.loads((a.baseline / "manifest.json").read_text())
assert m["version"] == "0.5.1"
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
shutil.copytree(a.baseline, a.output)
rebuilt = {r["key"]: r for r in json.loads((a.atlases / "manifest.json").read_text())["atlases"] if r["room"] == "M163"}
audit = []
for e in m["atlases"]:
    if e["room"] != "M163": continue
    bmp = np.array(Image.open(a.atlases / (e["key"] + ".bmp")).convert("RGB"))
    h = bmp.shape[0] // 2
    top, ref_half = bmp[:h], bmp[h:]
    ref = a.output / e["reference_file"]
    assert np.array_equal(ref_half, np.array(Image.open(ref).convert("RGB"))), e["key"] + " native reference changed"
    old = np.array(Image.open(a.baseline / e["file"]).convert("RGB")).astype(int)
    drift = np.abs(old - top).mean()
    if drift < 2.0:                                   # unchanged material (codec noise only): keep 0.5.1 bytes
        audit.append(dict(key=e["key"], replaced=False, mean_abs_vs_051=round(float(drift), 3))); continue
    tmp = a.output / (e["key"] + ".source.png"); Image.fromarray(top).convert("RGBA").save(tmp)
    target = a.output / e["file"]; target.unlink()
    subprocess.run(["cwebp", "-quiet", "-q", "95", "-m", "6", "-exact", str(tmp), "-o", str(target)], check=True); tmp.unlink()
    rgba = Image.open(target).convert("RGBA")
    assert sha(ref) == e["reference_sha256"] and rgba.size == Image.open(ref).size
    e.update(atlas_sha256=sha(target), source_atlas_sha256=sha(a.atlases / (e["key"] + ".bmp")),
             pixel_sha256=hashlib.sha256(rgba.tobytes() + Image.open(ref).convert("RGBA").tobytes()).hexdigest(),
             compressed_bytes=target.stat().st_size + ref.stat().st_size)
    audit.append(dict(key=e["key"], replaced=True, mean_abs_vs_051=round(float(drift), 3), webp_sha256=e["atlas_sha256"]))
assert sum(x["replaced"] for x in audit) == 2, audit
m["version"] = a.version
m["correction_m163"] = ("Issues #269/#268: M163 restaurant, awning and hanging signs re-registered to the original "
                        "layout (new anchor, 86% registration inliers); native planes and every other room unchanged.")
m["image_bytes"] = sum(f.stat().st_size for f in a.output.iterdir() if f.is_file() and f.name != "manifest.json")
m["distribution_bytes"] = m["image_bytes"]
while True:
    data = (json.dumps(m, indent=2) + "\n").encode()
    if m["distribution_bytes"] == m["image_bytes"] + len(data): break
    m["distribution_bytes"] = m["image_bytes"] + len(data)
(a.output / "manifest.json").write_bytes(data)
(a.output.parent / (a.output.name + "-m163.json")).write_text(json.dumps(dict(method=__doc__, atlases=audit), indent=2) + "\n")
print(json.dumps(audit, indent=1))
