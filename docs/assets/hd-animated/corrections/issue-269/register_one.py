"""register_one.py ROOT AID CANDIDATE: register_anchors.py's global affine for one anchor, into ROOT/registered-anchors."""
import sys, json
from pathlib import Path
import cv2, numpy as np
from PIL import Image
R = Path(sys.argv[1]); aid = sys.argv[2]; cand = Path(sys.argv[3]); A = Path("/Users/hasik/Projects/parasite-eve-pc/docs/assets/hd-animated")
job = next(j for j in json.loads((A / "anchor-jobs.json").read_text()) if j["id"] == aid)
sift = cv2.SIFT_create(nfeatures=4000); clahe = cv2.createCLAHE(2, (8, 8))
source = np.array(Image.open(job["source"]).convert("RGB")); h, w = source.shape[:2]
image = np.array(Image.open(cand).convert("RGB").resize((w * 4, h * 4), Image.Resampling.LANCZOS))
small = cv2.resize(image, (w, h), interpolation=cv2.INTER_AREA)
ka, da = sift.detectAndCompute(clahe.apply(cv2.cvtColor(source, cv2.COLOR_RGB2GRAY)), None)
kb, db = sift.detectAndCompute(clahe.apply(cv2.cvtColor(small, cv2.COLOR_RGB2GRAY)), None)
pairs = cv2.BFMatcher().knnMatch(db, da, k=2); good = [m for pr in pairs if len(pr) == 2 for m, n in [pr] if m.distance < .72 * n.distance]
src = np.float32([kb[m.queryIdx].pt for m in good]); dst = np.float32([ka[m.trainIdx].pt for m in good])
matrix, inl = cv2.estimateAffine2D(src, dst, method=cv2.RANSAC, ransacReprojThreshold=1.5, maxIters=6000)
count = int(inl.sum()); corners = np.float32([[0, 0], [w, 0], [0, h], [w, h]]); moved = corners @ matrix[:, :2].T + matrix[:, 2]
disp = float(np.max(np.linalg.norm(moved - corners, axis=1))); sv = np.linalg.svd(matrix[:, :2], compute_uv=False)
err = np.linalg.norm(src @ matrix[:, :2].T + matrix[:, 2] - dst, axis=1)
rec = dict(id=aid, views=job["views"], candidate=str(cand), matches=len(good), inliers=count, inlier_ratio=count / max(len(good), 1),
           matrix_native=matrix.tolist(), corner_displacement=disp, median_inlier_error=float(np.median(err[inl[:, 0] != 0])))
ok = count >= 12 and np.all((sv > .85) & (sv < 1.18)) and disp < .14 * np.hypot(w, h)
high = matrix.copy(); high[:, 2] *= 4
warped = cv2.warpAffine(image, high, (w * 4, h * 4), flags=cv2.INTER_LANCZOS4)
valid = cv2.warpAffine(np.ones((h * 4, w * 4), np.uint8), high, (w * 4, h * 4), flags=cv2.INTER_NEAREST)
alpha = np.clip(cv2.distanceTransform(np.pad(valid, 1), cv2.DIST_L2, 5)[1:-1, 1:-1] / 8, 0, 1)[..., None]
restored = np.array(Image.open(A / "source-sr-full" / (aid + ".png")).convert("RGB"))
fixed = np.uint8(np.clip(np.rint(warped * alpha + restored * (1 - alpha)), 0, 255))
rec["status"] = "global affine registered" if ok else "uncertain fit"
(R / "registered-anchors").mkdir(exist_ok=True); Image.fromarray(fixed).save(R / "registered-anchors" / (aid + ".png"))
(R / "anchor-alignment.json").write_text(json.dumps({"method": "register_anchors.py global affine (single anchor)", "anchors": [rec]}, indent=2))
print(json.dumps({k: v for k, v in rec.items() if k != "matrix_native"}))
