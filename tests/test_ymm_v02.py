"""Portable reference/contract tests; these do not execute Direct2D or YMM4."""
import math
from pathlib import Path
import re
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def kernel(scale):
    radius = math.ceil(3 * scale)
    x = np.arange(-radius, radius + 1, dtype=np.float64)
    g = np.exp(-.5 * (x / scale) ** 2)
    return g / g.sum()


def axis_convolve(image, g, axis):
    r = len(g) // 2
    pads = [(0, 0)] * image.ndim
    pads[axis] = (r, r)
    padded = np.pad(image, pads)
    result = np.zeros_like(image)
    for i, w in enumerate(g):
        slices = [slice(None)] * image.ndim
        slices[axis] = slice(i, i + image.shape[axis])
        result += padded[tuple(slices)] * w
    return result


def separable(luma, alpha, scale, dtype=np.float64):
    g = kernel(scale).astype(dtype)
    numerator = axis_convolve(axis_convolve((luma * alpha).astype(dtype), g, 1), g, 0)
    weight = axis_convolve(axis_convolve(alpha.astype(dtype), g, 1), g, 0)
    return np.divide(numerator, weight, out=np.zeros_like(numerator), where=weight > 0)


def direct(luma, alpha, scale):
    g = kernel(scale)
    r = len(g) // 2
    src = np.pad(luma * alpha, r)
    a = np.pad(alpha, r)
    n = np.zeros_like(luma)
    d = np.zeros_like(alpha)
    for y in range(len(g)):
        for x in range(len(g)):
            w = g[x] * g[y]
            n += src[y:y+luma.shape[0], x:x+luma.shape[1]] * w
            d += a[y:y+luma.shape[0], x:x+luma.shape[1]] * w
    return np.divide(n, d, out=np.zeros_like(n), where=d > 0)


def response(luma, alpha, scale=1):
    s, b = kernel(scale), kernel(1.6 * scale)
    s = np.pad(s, (len(b) - len(s)) // 2)
    positive = np.maximum(np.outer(s, s) - np.outer(b, b), 0).sum()
    return (separable(luma, alpha, scale, np.float32)
            - separable(luma, alpha, 1.6 * scale, np.float32)) / positive


class YmmV02(unittest.TestCase):
    def test_kernel_invariants(self):
        for s in (.6, 1, 1.6, 2.5, 4):
            g = kernel(s)
            self.assertAlmostEqual(g.sum(), 1)
            np.testing.assert_array_equal(g, g[::-1])
            self.assertTrue(np.all(g > 0))

    def test_packed_two_pass_matches_independent_2d(self):
        rng = np.random.default_rng(9)
        l = rng.random((13, 17))
        a = rng.random(l.shape)
        a[3:7, 2:9] = 0
        for s in (.6, 1, 1.6, 4, 6.4):
            np.testing.assert_allclose(separable(l, a, s, np.float32), direct(l, a, s), atol=4e-7)

    def test_flat_partial_alpha_and_boundaries(self):
        a = np.random.default_rng(10).random((35, 37))
        a[5:9, 3:7] = 0
        self.assertLess(np.max(np.abs(response(np.full_like(a, .5), a))), 1e-6)
        np.testing.assert_array_equal(response(a, np.zeros_like(a)), 0)

    def test_noise_and_threshold_monotonicity(self):
        l = np.random.default_rng(42).normal(.5, .035, (256, 256))
        r = -response(l, np.ones_like(l))[20:-20, 20:-20]
        counts = [np.count_nonzero(r > t / 255 * .08) for t in (0, 64, 128, 192, 255)]
        self.assertEqual(counts, sorted(counts, reverse=True))
        self.assertLess(np.mean(r > 128 / 255 * .08), .01)
        self.assertIn("saturate(threshold) * .08", (ROOT / "ymm/Shaders/LineMask.hlsl").read_text(encoding="utf-8"))

    def test_polarity(self):
        l = np.full((31, 31), .5)
        l[:, 15] = 0
        r = response(l, np.ones_like(l))
        self.assertLess(r[15, 15], 0)
        np.testing.assert_allclose(r, -response(1-l, np.ones_like(l)), atol=2e-6)

    def test_ui_order_ranges_and_removals(self):
        source = (ROOT / "ymm/SaAohueEffect.cs").read_text(encoding="utf-8")
        labels = re.findall(r'\[Display\(Name = "([^"]+)".*?Order = (\d+)\)\]', source)
        self.assertEqual([n for n, o in sorted(labels, key=lambda p: int(p[1]))],
                         ["検出サイズ", "検出しきい値", "線を反転", "強弱の反映", "点ノイズ除去", "安定性", "適用する側", "変化範囲", "コントラスト", "彩度", "輝度", "色相", "出力"])
        self.assertNotIn("Vibrance", source)
        self.assertNotIn("RgbEncoding", source)
        self.assertIn('AnimationSlider("F1", "%", -100, 400)', source)
        self.assertIn('AnimationSlider("F1", "px", 0, 512)', source)

    def test_processor_contract(self):
        source = (ROOT / "ymm/SaAohueProcessor.cs").read_text(encoding="utf-8")
        self.assertIn("GaussianBlurOptimization.Quality", source)
        self.assertIn("_blur.StandardDeviation = radius", source)
        self.assertIn("bool bypass = radius == 0", source)
        self.assertNotIn("Math.Sqrt(3)", source)

    def test_shader_contract(self):
        source = (ROOT / "ymm/Shaders/Composite.hlsl").read_text(encoding="utf-8")
        self.assertNotIn("isLinear", source)
        self.assertNotIn("vibrance", source)
        self.assertIn("if (contrast != 1)", source)
        self.assertIn("if (!inGamut(target))", source)
        self.assertIn("max(0, 1 + amount * mask)", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
