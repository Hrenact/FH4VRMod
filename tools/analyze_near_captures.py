"""Archive-independent comparison of the three parked, equal-FOV near-clip images."""
import json
from pathlib import Path
import cv2
import numpy as np

root = Path(__file__).resolve().parents[1]
folder = root / 'reports' / 'near-captures-37024'
images = [cv2.imread(str(folder / f'fh4vr-capture-37024-{i}.png')) for i in (1, 2, 3)]
assert all(im is not None and im.shape == (1440, 2560, 3) for im in images)
result = {}
# Fixed top strip: the observed baseline hole contains bright blue sky here.
# This is an image measurement, not a semantic segmentation of all roof geometry.
result['top_strip_y0_80_bright_pixel_fraction'] = [float((im[:80].mean(axis=2) > 100).mean()) for im in images]
sift = cv2.SIFT_create(nfeatures=5000)
mask = np.zeros((1440, 2560), np.uint8)
mask[780:1300, 800:1900] = 255
features = [sift.detectAndCompute(cv2.cvtColor(im, cv2.COLOR_BGR2GRAY), mask) for im in images]
for i in (1, 2):
    ka, da = features[0]
    kb, db = features[i]
    pairs = cv2.BFMatcher().knnMatch(da, db, k=2)
    good = [a for a, b in pairs if a.distance < .7*b.distance]
    a = np.float32([ka[m.queryIdx].pt for m in good])
    b = np.float32([kb[m.trainIdx].pt for m in good])
    matrix, valid = cv2.estimateAffinePartial2D(a, b, method=cv2.RANSAC, ransacReprojThreshold=1.5)
    residual = np.linalg.norm(a @ matrix[:, :2].T + matrix[:, 2] - b, axis=1)
    result[f'cockpit_1_to_{i+1}'] = dict(scale=float(np.hypot(*matrix[0, :2])),
        inliers=int(valid.sum()), median_residual_px=float(np.median(residual[valid.ravel() != 0])))
result['visual_observation'] = 'Top roof gap visible in 1 and 3; roof fills that region in 2. Lower steering-column structure remains. Static single car/view only; no full stereo or driving validation.'
(folder / 'analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
