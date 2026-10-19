"""Measure image scaling for the parked F1 intervention; no process access."""
import json
import math
import shutil
from pathlib import Path
import cv2
import numpy as np
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--pid',type=int,default=7448)
parser.add_argument('--target-deg',type=float,default=58)
args=parser.parse_args()

root = Path(__file__).resolve().parents[1]
dest = root / 'reports' / f'fov-captures-{args.pid}'
dest.mkdir(exist_ok=True)
game = Path(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4')
images = []
for i in range(1, 4):
    source = game / f'fh4vr-capture-{args.pid}-{i}.png'
    shutil.copy2(source, dest / source.name)
    images.append(cv2.imread(str(source), cv2.IMREAD_GRAYSCALE))

sift = cv2.SIFT_create(nfeatures=12000)
results = {}
for region, bounds in {'cockpit': (600, 850, 1980, 1380), 'outside': (850, 300, 2300, 790)}.items():
    mask = np.zeros_like(images[0])
    x0, y0, x1, y1 = bounds
    mask[y0:y1, x0:x1] = 255
    scale=math.tan(math.radians(53/2))/math.tan(math.radians(args.target_deg/2))
    height,width=mask.shape
    transform=np.float32([[scale,0,width/2*(1-scale)],[0,scale,height/2*(1-scale)]])
    wide_mask=cv2.warpAffine(mask,transform,(width,height))
    features = [sift.detectAndCompute(im, wide_mask if i==1 else mask) for i,im in enumerate(images)]
    for index in (1, 2):
        ka, da = features[0]
        kb, db = features[index]
        matches = cv2.BFMatcher().knnMatch(da, db, k=2)
        good = [a for a, b in matches if a.distance < .7*b.distance]
        a = np.float32([ka[m.queryIdx].pt for m in good])
        b = np.float32([kb[m.trainIdx].pt for m in good])
        matrix, valid = cv2.estimateAffinePartial2D(a, b, method=cv2.RANSAC, ransacReprojThreshold=1.5)
        keep = valid.ravel().astype(bool)
        residual = np.linalg.norm(a @ matrix[:, :2].T + matrix[:, 2] - b, axis=1)
        results[f'{region}_1_to_{index+1}'] = dict(matches=len(good), inliers=int(keep.sum()), matrix=matrix.tolist(), scale=float(np.hypot(matrix[0,0], matrix[1,0])), median_residual_px=float(np.median(residual[keep])))
results['predicted_scale_for_plus5'] = {str(base): math.tan(math.radians(base/2))/math.tan(math.radians((base+5)/2)) for base in (48,53)}
results['target_deg']=args.target_deg
results['predicted_scale_53_to_target']=scale
results['limitations'] = 'Similarity fits do not establish FOV axis or final projection matrix; moving vegetation, exposure, HUD and mirrors excluded where practical.'
(dest / 'analysis.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps(results, indent=2))
