"""GTAO from MoGe-2 metric geometry (point map + normal), offline still-image experiment.

Replaces the Depth-Anything-V2 relative-depth + horizon-Cavity heuristic (prototype.py) with a
calibrated pipeline: MoGe-2 gives a metric point map, per-pixel normal, and camera intrinsics;
GTAO (Jimenez et al. 2016, "Practical Real-Time Strategies for Accurate Indirect Occlusion")
integrates a closed-form cosine-weighted horizon visibility over that real 3D geometry instead
of a fixed image-relative height field. Ported from the reference math in GameTechDev/XeGTAO
(XeGTAO.hlsli, MainPass), adapted to a single dense NumPy pass since this is not real-time.

Output plugs directly into the existing AE plugin's Geometry Source=External Cavity input
(src/Sa_aohue.cpp baked path) -- no C++/AE-side change needed.
"""
import argparse
import json
import time
from pathlib import Path
from typing import Optional

import numpy as np
from PIL import Image

from prototype import chroma, sheet, save


def disc_blur(field: np.ndarray, radius: float) -> np.ndarray:
    """Anti-aliased circular average, edge-clamped. Ported from the small-radius exact path of
    gather_channel() in ../cool_blur/plugin/CoolBlur_Core.h (edge=0: a flat/uniform disc kernel,
    no bokeh shaping) -- isotropic and non-directional, unlike a square box blur, which matters
    here because MoGe-2's normal decoder leaves a faint periodic (checkerboard-style) artifact
    that a box blur's axis-aligned passband does not fully suppress. See main()'s --normal-blur.
    """
    if radius < .5: return field.copy()
    ir = int(np.ceil(radius))
    pad = [(ir, ir), (ir, ir)] + [(0, 0)] * (field.ndim - 2)
    padded = np.pad(field, pad, mode='edge')
    h, w = field.shape[:2]
    acc = np.zeros_like(field, dtype=np.float64)
    wsum = np.zeros((h, w), dtype=np.float64)
    for dy in range(-ir, ir + 1):
        for dx in range(-ir, ir + 1):
            rn2 = (dx * dx + dy * dy) / (radius * radius)
            if rn2 > 1: continue
            aa = np.clip((1 - rn2 ** .5) * radius, 0, 1)
            if aa <= 0: continue
            acc += padded[ir + dy:ir + dy + h, ir + dx:ir + dx + w] * aa
            wsum += aa
    return acc / (wsum[..., None] if field.ndim == 3 else wsum)


def sample(field: np.ndarray, xs: np.ndarray, ys: np.ndarray) -> np.ndarray:
    """Bilinear-gather field[H,W] or field[H,W,C] at per-pixel float coords xs,ys (clamped)."""
    squeeze = field.ndim == 2
    f = field[..., None] if squeeze else field
    h, w = f.shape[:2]
    xs = np.clip(xs, 0, w - 1)
    ys = np.clip(ys, 0, h - 1)
    x0 = np.floor(xs).astype(np.int32); y0 = np.floor(ys).astype(np.int32)
    x1 = np.minimum(x0 + 1, w - 1); y1 = np.minimum(y0 + 1, h - 1)
    fx = (xs - x0)[..., None]; fy = (ys - y0)[..., None]
    out = (f[y0, x0] * (1 - fx) * (1 - fy) + f[y0, x1] * fx * (1 - fy)
           + f[y1, x0] * (1 - fx) * fy + f[y1, x1] * fx * fy)
    return out[..., 0] if squeeze else out


def gtao(points: np.ndarray, normal: np.ndarray, mask: np.ndarray, fx_px: float, fy_px: float,
          radius: float = .5, slices: int = 9, steps: int = 8, falloff: float = .7) -> np.ndarray:
    """GTAO visibility in [0,1] (1=fully open, matches this project's AO convention).

    points, normal: (H,W,3) OpenCV camera space (x=right, y=down, z=forward/away from camera),
    as returned by MoGe. mask: (H,W) bool validity. radius is a WORLD-SPACE distance (meters,
    trusting MoGe's metric-scale estimate) -- this is the calibration the relative-depth horizon
    method (prototype.cavity) cannot do, since it only has an image-relative height field.
    """
    h, w, _ = points.shape
    yy, xx = np.mgrid[:h, :w].astype(np.float64)
    # Invalid (masked-out) pixels can carry inf/nan (e.g. background at infinity); zero them so
    # bilinear gathers near a mask boundary stay finite. sampleValid below still excludes them
    # from the horizon search, this only prevents inf/nan from leaking into that blend.
    P = np.nan_to_num(points.astype(np.float64), nan=0., posinf=0., neginf=0.)
    dist = np.linalg.norm(P, axis=-1, keepdims=True)
    viewVec = -P / np.maximum(dist, 1e-6)
    N = np.nan_to_num(normal.astype(np.float64), nan=0., posinf=0., neginf=0.)
    N /= np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-6)
    N = np.where(np.sum(N * viewVec, axis=-1, keepdims=True) < 0, -N, N)
    valid = mask.astype(np.float64)

    # Perspective screen-space radius (pixels) for the given world radius at this pixel's depth.
    screenRadius = np.clip(radius * fx_px / np.clip(P[..., 2], 1e-3, None), 1., float(max(h, w)))

    visibility = np.zeros((h, w))
    for s in range(slices):
        phi = (s + .5) / slices * np.pi
        cosPhi, sinPhi = np.cos(phi), np.sin(phi)
        directionVec = np.array([cosPhi, sinPhi, 0.])
        dotDV = viewVec @ directionVec
        orthoDirectionVec = directionVec[None, None, :] - dotDV[..., None] * viewVec
        axisVec = np.cross(orthoDirectionVec, viewVec)
        axisVec /= np.maximum(np.linalg.norm(axisVec, axis=-1, keepdims=True), 1e-6)
        projN = N - axisVec * np.sum(N * axisVec, axis=-1, keepdims=True)
        projLen = np.linalg.norm(projN, axis=-1)
        signNorm = np.sign(np.sum(orthoDirectionVec * projN, axis=-1))
        cosNorm = np.clip(np.sum(projN * viewVec, axis=-1) / np.maximum(projLen, 1e-6), -1, 1)
        n = signNorm * np.arccos(cosNorm)
        omega_x, omega_y = cosPhi * screenRadius, -sinPhi * screenRadius

        horizons = []
        for side in (1., -1.):
            hcos = np.cos(n + side * np.pi / 2)  # no-occluder default: normal's own tangent plane
            for step in range(1, steps + 1):
                t = (step / steps) ** 2  # bias samples toward the pixel, like XeGTAO's x^2 spacing
                sx, sy = xx + side * omega_x * t, yy + side * omega_y * t
                samplePos = sample(P, sx, sy)
                sampleValid = sample(valid, sx, sy) > .5
                sampleVec = samplePos - P
                sdist = np.linalg.norm(sampleVec, axis=-1)
                sdir = sampleVec / np.maximum(sdist[..., None], 1e-6)
                shc = np.sum(sdir * viewVec, axis=-1)
                w_falloff = np.clip(1 - (sdist / max(radius, 1e-6) - falloff) / max(1e-3, 1 - falloff), 0, 1)
                blended = np.where(sampleValid, hcos + (shc - hcos) * w_falloff, hcos)
                hcos = np.maximum(hcos, blended)
            horizons.append(hcos)
        horizonCos0, horizonCos1 = horizons
        h0 = -np.arccos(np.clip(horizonCos1, -1, 1))
        h1 = np.arccos(np.clip(horizonCos0, -1, 1))
        iarc0 = (cosNorm + 2 * h0 * np.sin(n) - np.cos(2 * h0 - n)) / 4
        iarc1 = (cosNorm + 2 * h1 * np.sin(n) - np.cos(2 * h1 - n)) / 4
        visibility += projLen * (iarc0 + iarc1)
    visibility = np.clip(visibility / slices, 0, 1)
    return np.where(mask, visibility, 1.)


def infer(image: Image.Image, model=None):
    import torch
    from moge.model.v2 import MoGeModel
    local = Path(__file__).resolve().parents[1] / 'models/moge-2-vitb-normal'
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    if model is None:
        name = str(local / 'model.pt') if (local / 'model.pt').exists() else 'Ruicheng/moge-2-vitb-normal'
        model = MoGeModel.from_pretrained(name).to(device).eval()
    arr = np.asarray(image.convert('RGB')) / 255.
    x = torch.tensor(arr, dtype=torch.float32, device=device).permute(2, 0, 1)
    with torch.inference_mode():
        out = model.infer(x)
    to_np = lambda v: v.detach().float().cpu().numpy()
    return {k: (to_np(v) if hasattr(v, 'detach') else v) for k, v in out.items()}, model


def main():
    p = argparse.ArgumentParser()
    p.add_argument('input'); p.add_argument('--out', default='results/gtao')
    p.add_argument('--radius', type=float, default=.5, help='world-space AO radius in meters (MoGe metric estimate)')
    p.add_argument('--slices', type=int, default=9); p.add_argument('--steps', type=int, default=8)
    p.add_argument('--amount', type=float, default=1); p.add_argument('--max-size', type=int, default=0)
    p.add_argument('--normal-blur', type=float, default=6.,
                    help='disc-blur radius (px) applied to the predicted normal before GTAO; '
                         'suppresses a periodic decoder artifact from MoGe-2 that GTAO otherwise '
                         'amplifies into visible block noise. 0 disables.')
    a = p.parse_args(); out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    im = Image.open(a.input).convert('RGB')
    if a.max_size > 0: im.thumbnail((a.max_size, a.max_size))
    rgb = np.asarray(im) / 255.
    start = time.perf_counter()
    geo, _ = infer(im)
    infer_seconds = time.perf_counter() - start
    points, normal, mask, intr = geo['points'], geo['normal'], geo['mask'] > .5, geo['intrinsics']
    if a.normal_blur > 0:
        normal = disc_blur(normal, a.normal_blur)
        normal /= np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-6)
    h, w = mask.shape
    fx_px, fy_px = float(intr[0, 0]) * w, float(intr[1, 1]) * h
    start = time.perf_counter()
    visibility = gtao(points, normal, mask, fx_px, fy_px, a.radius, a.slices, a.steps)
    cavity = np.where(mask, 1 - visibility, 0.)
    comp = chroma(rgb, cavity, a.amount)
    ao_seconds = time.perf_counter() - start
    np.save(out / 'points.npy', points)
    z = points[..., 2]; zvalid = z[mask & np.isfinite(z)]
    zlo, zhi = (float(zvalid.min()), float(zvalid.max())) if zvalid.size else (0., 1.)
    zpreview = np.where(mask, np.clip((z - zlo) / max(zhi - zlo, 1e-6), 0, 1), 0.)
    Image.fromarray(np.uint16(zpreview * 65535 + .5)).save(out / 'depth_preview.png')
    Image.fromarray(np.uint16(np.clip(cavity, 0, 1) * 65535 + .5)).save(out / 'cavity16.png')
    save(out / 'cavity.png', cavity); save(out / 'ao.png', visibility)
    save(out / 'composite.png', comp); save(out / 'input.png', rgb)
    sheet(out / 'comparison.png', [('RGB', rgb), ('Cavity (GTAO)', cavity), ('Composite', comp)])
    metrics = {'infer_seconds': infer_seconds, 'gtao_seconds': ao_seconds, 'cavity_mean': float(cavity.mean()),
               'radius_m': a.radius, 'slices': a.slices, 'steps': a.steps}
    (out / 'metrics.json').write_text(json.dumps(metrics, indent=2)); print(metrics)


if __name__ == '__main__':
    main()
