"""Run: python prototype/check_gtao.py. Synthetic invariants for gtao(), no MoGe inference needed."""
import json
from pathlib import Path
import numpy as np
from gtao import gtao, sample

out = Path('results/check_gtao'); out.mkdir(parents=True, exist_ok=True)
h, w = 80, 120
yy, xx = np.mgrid[:h, :w].astype(np.float64)
fx_px = fy_px = 100.
z = 2.
X = (xx - w / 2) / fx_px * z; Y = (yy - h / 2) / fy_px * z
wall = np.stack([X, Y, np.full((h, w), z)], axis=-1)
flat_normal = np.zeros((h, w, 3)); flat_normal[..., 2] = -1.
mask = np.ones((h, w), dtype=bool)

# A flat wall facing the camera has (near-)no self-occlusion away from the frame border.
vis_wall = gtao(wall, flat_normal, mask, fx_px, fy_px, radius=.3)
interior = vis_wall[10:-10, 10:-10]
assert interior.min() > .85, interior.min()

# A concave dent occludes its own center more than the flat wall does at the same point.
bowl = wall.copy()
bowl[..., 2] += .3 * np.exp(-((xx - w / 2) ** 2 + (yy - h / 2) ** 2) / 200)
gx, gy = np.gradient(bowl[..., 2], axis=1), np.gradient(bowl[..., 2], axis=0)
bowl_normal = np.stack([-gx, -gy, np.ones((h, w))], axis=-1)
bowl_normal /= np.linalg.norm(bowl_normal, axis=-1, keepdims=True)
vis_bowl = gtao(bowl, bowl_normal, mask, fx_px, fy_px, radius=.3)
assert vis_bowl[h // 2, w // 2] < vis_wall[h // 2, w // 2] - .1, (vis_bowl[h // 2, w // 2], vis_wall[h // 2, w // 2])

# A larger radius sees further into the same dent, so occlusion should not decrease with radius.
vis_bowl_r6 = gtao(bowl, bowl_normal, mask, fx_px, fy_px, radius=.6)
assert (1 - vis_bowl_r6[h // 2, w // 2]) >= (1 - vis_bowl[h // 2, w // 2]) - 1e-6

# Points/normals carrying inf/nan at invalid pixels must not leak into a finite result.
broken = wall.copy(); broken[5, 5] = np.inf
broken_normal = flat_normal.copy(); broken_normal[5, 5] = np.nan
broken_mask = mask.copy(); broken_mask[5, 5] = False
vis_broken = gtao(broken, broken_normal, broken_mask, fx_px, fy_px, radius=.3)
assert np.isfinite(vis_broken).all()

# Bilinear sampler: exact grid points reproduce the field, and 3D fields keep their last axis.
field = xx + yy * 10
assert np.allclose(sample(field, xx, yy), field)
field3 = np.stack([field, field], axis=-1)
assert np.allclose(sample(field3, xx, yy), field3)

metrics = {
    'flat_wall_interior_min': float(interior.min()), 'flat_wall_interior_mean': float(interior.mean()),
    'bowl_center_visibility_r3': float(vis_bowl[h // 2, w // 2]), 'bowl_center_visibility_r6': float(vis_bowl_r6[h // 2, w // 2]),
    'flat_center_visibility': float(vis_wall[h // 2, w // 2]),
}
(out / 'metrics.json').write_text(json.dumps(metrics, indent=2)); print('PASS', metrics)
