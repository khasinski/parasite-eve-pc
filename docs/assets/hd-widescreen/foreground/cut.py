"""Foreground cutouts for widescreen side strips (GrabCut seeded by the native layer at the seam).

cut.py SIDES_HD.png SPEC.json OUT_DIR
SPEC: {"strips": [{"name": "right", "x0": 1504, "seam": "left", "native_rows": [122, 239],
                   "fg_boxes": [[x0,y0,x1,y1]...], "bg_boxes": [...], "pfg_boxes": [...]}]}
Coordinates are HD pixels inside the strip (4 per native pixel). Writes NAME-mask.png and NAME-overlay.png."""
import json, sys
from pathlib import Path
import numpy as np, cv2
from PIL import Image

src = np.asarray(Image.open(sys.argv[1]).convert('RGB'))
spec = json.loads(Path(sys.argv[2]).read_text()); out = Path(sys.argv[3]); out.mkdir(parents=True, exist_ok=True)
for s in spec['strips']:
    img = np.ascontiguousarray(src[:, s['x0']:s['x0'] + 224]).copy()
    h, w = img.shape[:2]
    m = np.full((h, w), cv2.GC_PR_BGD, np.uint8)
    for x0, y0, x1, y1 in s.get('pfg_boxes', []): m[y0:y1, x0:x1] = cv2.GC_PR_FGD
    for x0, y0, x1, y1 in s.get('bg_boxes', []): m[y0:y1, x0:x1] = cv2.GC_BGD
    for x0, y0, x1, y1 in s.get('fg_boxes', []): m[y0:y1, x0:x1] = cv2.GC_FGD
    for poly in s.get('bg_polys', []): cv2.fillPoly(m, [np.array(poly, np.int32)], cv2.GC_BGD)
    for poly in s.get('fg_polys', []): cv2.fillPoly(m, [np.array(poly, np.int32)], cv2.GC_FGD)
    # The native silhouette at the seam is ground truth for both labels.
    a, b = s['native_rows']; cols = slice(0, 6) if s['seam'] == 'left' else slice(w - 6, w)
    m[:, cols] = cv2.GC_BGD; m[a * 4:(b + 1) * 4, cols] = cv2.GC_FGD
    bgd = np.zeros((1, 65), np.float64); fgd = np.zeros((1, 65), np.float64)
    cv2.grabCut(cv2.cvtColor(img, cv2.COLOR_RGB2BGR), m, None, bgd, fgd, 8, cv2.GC_INIT_WITH_MASK)
    fg = ((m == cv2.GC_FGD) | (m == cv2.GC_PR_FGD)).astype(np.uint8)
    # Keep the component(s) touching the seam silhouette; close small holes.
    fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, np.ones((9, 9), np.uint8))
    n, lab = cv2.connectedComponents(fg)
    keep = set(np.unique(lab[a * 4:(b + 1) * 4, cols])) - {0}
    fg = np.isin(lab, list(keep)).astype(np.uint8)
    Image.fromarray(fg * 255).save(out / f"{s['name']}-mask.png")
    ov = img.copy(); ov[fg == 0] = (ov[fg == 0] * 0.25).astype(np.uint8); ov[fg == 0, 2] = np.maximum(ov[fg == 0, 2], 90)
    Image.fromarray(np.hstack([img, ov])).save(out / f"{s['name']}-overlay.png")
    print(s['name'], 'fg fraction', round(float(fg.mean()), 3))
