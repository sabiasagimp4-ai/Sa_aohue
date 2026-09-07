# Output-preserving performance changes (2026-09-07)

Baseline: `88ab06d4b25a15b91eb0ade1756ceb00579a6802` (`master` at start).
Target is the standalone **Sa_aohue** repository, not the older copy in Sa_Plugins.

## What changes

- Compute disc taps once per call, retaining the original tap order, float expressions,
  fractional-alpha weights and edge clamping.
- Fuse the two fixed disc evaluations and binary threshold into one line plane. Do not
  replace the radius-1 weighted multiply/divide with a plain luminance copy: that can
  change rounding for fractional alpha and flip the threshold decision.
- Blur that line plane in place with one reusable scratch plane for all six axis passes.
- Process the vertical prefix sums in blocks of up to 16 adjacent columns for contiguous
  memory access. Each column still accumulates from top to bottom in float, and uses
  the same prefix subtraction and division. No rolling-sum reassociation or fast-math.
- Use the calling thread for one range and at most 15 worker threads. Join all workers
  on launch failure and rethrow worker exceptions after joining so render's existing
  error handler can handle allocation failure.
- Composite Amount=0 returns the existing origin-aware copy before allocating fields.
  Other output modes still calculate the mask even when Amount=0.

Parameter order/defaults, match name, pixel formats, color transform, gamut search,
blur radius rounding, alpha weighting and full-input SmartFX checkout are unchanged.
No GPU, approximate color lookup, persistent cache, SDK change or new runtime dependency.

## Memory

Peak full-frame float planes during `lineArt`, including `Field.d` and `Field.valid`:

| | Baseline | Optimized |
|---|---:|---:|
| Planes | 7 | 4 |
| 1920×1080 | 55.37 MiB | 31.64 MiB |
| 3840×2160 | 221.48 MiB | 126.56 MiB |

This is a **42.9% reduction in the full-frame planes**, not process RSS. The new vertical
prefix workspace uses up to `2 * (height+1) * min(width,16) * sizeof(float)` per active
range (maximum 16 ranges); at 4K and 16 ranges this adds at most 4.22 MiB. Small tap
vectors, threads, allocator overhead and AE-owned source/output worlds are excluded.
Previously each vertical range held two single-column prefix arrays. No scratch or
frame data persists beyond the render call.

## Measured CPU core time

Linux x86-64 container, AMD EPYC 9V74, 9 logical CPUs, GCC 13.3,
`-O2 -std=c++17 -pthread -ffp-contract=off`. Radius=24, Contrast=1, Threshold=0.5,
random luminance, alpha=0.3 every seventh pixel and 1 elsewhere. Seven measured runs
per version, alternating order after a bitwise comparison/warm-up; median shown.

| Frame | Baseline | Optimized | Speedup |
|---|---:|---:|---:|
| 1920×1080 | 95.540 ms | 36.231 ms | 2.64× |
| 3840×2160 | 386.106 ms | 183.617 ms | 2.10× |

Timings cover `lineArt` including allocations and thread launches. They exclude RGB→L,
final chroma/compositing, SDK checkout and AE scheduling. These are not Windows/AE
render-time claims and will vary by CPU and concurrent rendering load.

## Verification and reproduction

```sh
python tests/run_portable.py
python tests/run_portable.py --bench 1920 1080
python tests/run_portable.py --bench 3840 2160
python tests/run_portable.py --sanitize
```

The runner reads baseline `src/core.h` from git history and compiles both revisions
into distinct namespaces. Requires a full clone containing that baseline commit.
Works with GCC/Clang or `cl` in a VS x64 Developer Command Prompt; `CXX` overrides it.
MSVC uses `/O2 /fp:precise`, matching the production project's default FP model.

- 1,150 bitwise comparisons of disc kernels, both box axes and the complete lineArt
  pipeline: flat/step/random fields, opaque/zero/fractional alpha, 1-pixel and narrow
  images, non-block-aligned widths, 8193-pixel axes, radius up to 512 (box radius up to
  1024), contrast 0.1/1/4, threshold 0/0.5/1 and both invert values.
- Range-coverage and worker-exception propagation checks.
- Address/undefined-behavior sanitizer check uses the same regression. In this container
  LeakSanitizer cannot inspect `/proc` under the runner; use
  `ASAN_OPTIONS=detect_leaks=0 python tests/run_portable.py --sanitize` here only.
- GitHub Actions runs the SDK-free regression with GCC and MSVC, plus Linux sanitizers.
- Extended `tests/check_ae.cpp` for Amount=0 translated crops and out-of-input zero fill.

Windows SDK adapter tests and a new `.aex` build require the existing Windows/AE SDK
setup and have not been run in this Linux environment. The tracked `build/Sa_aohue.aex`
remains the old binary; rebuild from these sources to obtain the optimized plugin.
Actual AE-host playback, ROI and SmartFX behavior still require host verification.
