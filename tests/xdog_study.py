"""Measure the current line detector and compare it with an XDoG formulation.

CI does not run this; it is the reproduction script for docs/FEATURE_PROPOSALS.md
sections 4 and 5. Requires NumPy only. No AE SDK, no GPU, no model download.

    python3 tests/xdog_study.py
"""
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

HALF = 6  # kernel support used for every detector below (13x13)


def gauss(sigma, half=HALF):
    x = np.arange(-half, half + 1)
    g = np.exp(-x * x / (2 * sigma * sigma))
    g /= g.sum()
    return np.outer(g, g)


def xdog(sigma, k=1.6, tau=1.0):
    """G_sigma - tau * G_(k*sigma), the D_sigma,k,tau of Winnemoeller et al."""
    return gauss(sigma) - tau * gauss(k * sigma)


def current():
    """What src/core.h lineArt() actually convolves with.

    discKernel(1.0) resolves to a single centre tap (the (+-1,0) taps get
    anti-alias weight (1-sqrt(1))*1 == 0 and are dropped), so the small side of
    the "DoG" is the identity. discKernel(1.6) is the 9-tap pillbox below.
    """
    w = np.array([[.185787, .6, .185787], [.6, 1., .6], [.185787, .6, .185787]])
    k = np.zeros((3, 3))
    k[1, 1] = 1.
    k -= w / w.sum()
    out = np.zeros((2 * HALF + 1, 2 * HALF + 1))
    out[HALF - 1:HALF + 2, HALF - 1:HALF + 2] = k
    return out


def conv(img, k):
    return (sliding_window_view(img, k.shape) * k).sum(axis=(-1, -2))


DETECTORS = [
    ("current (delta - pillbox1.6)", current()),
    ("XDoG sigma=0.6  tau=1.0", xdog(0.6)),
    ("XDoG sigma=1.0  tau=1.0", xdog(1.0)),
    ("XDoG sigma=1.6  tau=1.0", xdog(1.6)),
    ("XDoG sigma=1.0  tau=0.98", xdog(1.0, tau=0.98)),
]


def edge_response(kern, angle_deg, n=161):
    y, x = np.mgrid[-n // 2:n // 2 + 1, -n // 2:n // 2 + 1].astype(float)
    t = np.radians(angle_deg)
    img = np.clip(x * np.cos(t) + y * np.sin(t) + .5, 0, 1)
    return np.abs(conv(img, kern)).max()


def anisotropy():
    print("=== 1. Straight-edge response vs edge angle (0-90 deg) ===")
    print(f"{'detector':32s}{'0deg':>9s}{'45deg':>9s}{'max':>9s}{'min':>9s}{'spread':>9s}")
    for name, k in DETECTORS:
        r = np.array([edge_response(k, a) for a in np.arange(0, 91, 1.)])
        print(f"{name:32s}{r[0]:9.5f}{r[45]:9.5f}{r.max():9.5f}{r.min():9.5f}"
              f"{(r.max() - r.min()) / r.mean() * 100:8.1f}%")


def detection(threshold=0.015):
    print(f"\n=== 2. Line response vs background noise (threshold {threshold}) ===")
    rng = np.random.default_rng(11)
    n = 400
    base = np.full((n, n), .5)
    mask = np.zeros((n, n), bool)
    y, x = np.mgrid[0:n, 0:n].astype(float)
    for i, (ang, width) in enumerate([(0, 1), (0, 2), (30, 1), (30, 2), (45, 1), (45, 2), (63, 1), (63, 2)]):
        t = np.radians(ang)
        off = 40 + i * 42
        m = np.abs((x - off) * np.cos(t) + (y - off) * np.sin(t)) < width / 2
        base[m] -= .20
        mask |= m
    inner = slice(HALF, n - HALF)
    on_line = mask[inner, inner]
    flat = ~sliding_window_view(mask, (2 * HALF + 1, 2 * HALF + 1)).any(axis=(-1, -2))
    # 0.035 is the flat-background sigma measured on image.png in PIPELINE.md section 5.
    for label, noise in (("none", 0.), ("sigma=0.035 (real footage)", .035), ("sigma=0.07", .07)):
        img = base + rng.normal(0, noise, base.shape) if noise else base.copy()
        print(f"\n  background noise: {label}")
        print(f"  {'detector':32s}{'line':>10s}{'flat 1sig':>11s}{'SNR':>7s}{'false pos':>11s}")
        for name, k in DETECTORS:
            d = conv(img, k)
            sig = np.median(np.abs(d[on_line]))
            nz = d[flat].std()
            fp = (np.abs(d[flat]) > threshold).mean() * 100
            print(f"  {name:32s}{sig:10.5f}{nz:11.5f}{sig / max(nz, 1e-9):7.1f}{fp:10.2f}%")


def float32_precision():
    print("\n=== 3. float32 cancellation as tau approaches 1 ===")
    print("  A: subtract two blurred images.  B: fuse into one kernel first.")
    rng = np.random.default_rng(3)
    img = rng.random((512, 512)) * .2 + .4
    win64 = sliding_window_view(img, (2 * HALF + 1,) * 2)
    i32 = img.astype(np.float32)
    win32 = sliding_window_view(i32, (2 * HALF + 1,) * 2)
    gs, gk = gauss(1.0), gauss(1.6)
    for tau in (1.0, .98, .995, .999):
        ref = (win64 * gs).sum(axis=(-1, -2)) - tau * (win64 * gk).sum(axis=(-1, -2))
        a = ((win32 * gs.astype(np.float32)).sum(axis=(-1, -2), dtype=np.float32)
             - np.float32(tau) * (win32 * gk.astype(np.float32)).sum(axis=(-1, -2), dtype=np.float32))
        b = (win32 * (gs - tau * gk).astype(np.float32)).sum(axis=(-1, -2), dtype=np.float32)
        scale = np.abs(ref).max()
        print(f"  tau={tau:<6} |D|max={scale:.3e}  A err={np.abs(a - ref).max():.2e}"
              f"  B err={np.abs(b - ref).max():.2e}  (Line Threshold step = 3.0e-05)")


def tau_bias():
    print("\n=== 4. tau < 1 biases flat areas by (1-tau)*L ===")
    print(f"  {'luminance':>10s}" + "".join(f"{f'tau={t}':>12s}" for t in (1.0, .99, .98, .95)))
    for L in (.1, .3, .5, .8, 1.0):
        img = np.full((40, 40), L)
        row = f"  {L:10.2f}"
        for tau in (1.0, .99, .98, .95):
            row += f"{conv(img, xdog(1.0, tau=tau)).mean():12.5f}"
        print(row)
    print("  Compare with the 0.015 default threshold: a flat bright surface alone can cross it.")


def response_scale():
    print("\n=== 5. Positive-lobe L1 norm (scale of the ideal-step response) ===")
    ref = current()[current() > 0].sum()
    for name, k in DETECTORS:
        pos = k[k > 0].sum()
        print(f"  {name:32s}{pos:9.5f}  (x{pos / ref:.3f} of current)")
    print("  The 0..0.03 Line Threshold range was scaled for the current detector.")


def blur_quantization():
    print("\n=== 6. fastBlur radius quantization (src/core.h fastBlurInPlace) ===")
    print("  r = max(1, round(radius/sqrt(3))), three box passes")
    print(f"  {'Radius':>8s}{'box r':>7s}{'effective sigma':>17s}")
    for radius in (1., 1.5, 2., 2.5, 2.6, 3., 4., 4.4, 6., 24.):
        r = max(1, int(np.round(radius / np.sqrt(3))))
        sigma = np.sqrt(3 * ((2 * r + 1) ** 2 - 1) / 12)
        print(f"  {radius:8.2f}{r:7d}{sigma:17.4f}")
    print("  Radius 1.00-2.59 all collapse to the same output; sigma below 1.41 is unreachable.")


if __name__ == '__main__':
    anisotropy()
    detection()
    float32_precision()
    tau_bias()
    response_scale()
    blur_quantization()
