---
title: 'Keep large-map comparison rendering responsive'
type: 'bugfix'
created: '2026-10-09'
status: 'done'
route: 'oneshot'
baseline_commit: 'c5d85fef34e00652db22dddf5fe4fc73f3ee7b49'
review_loop_iteration: 1
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparing very large maps zooms out excessively and renders so many tiles that the editor drops below one frame per second. The user reproduced this with Springlobe 4 from downloadedmaps, with context both enabled and disabled.

**Approach:** Keep comparison at a useful inspection scale and bound rendering to the visible region, eliminating excessive per-tile draw submissions. Preserve exact read-only comparison, linked navigation, both context modes, and access to every change through pan and change navigation. Reproduce and measure the fix with the user's map and add a native regression for large tile layers.

</frozen-after-approval>

## Implementation Notes

- Continue on the existing feature branch; the pre-existing `ddnet-libs` change remains outside this fix. No intent gap or irreversible action is needed.
- Investigated `CMapCompare::FitSelection`, wheel zoom, `RenderPane`, and `DrawTiles`: fit/wheel allow a scale of 4096, row culling still scans all columns, and each changed tile opens separate outline and symbol batches. The normal editor zoom limit is 2000 percent (world scale 20), a suitable comparison upper bound; automatic fit should prefer a useful close inspection when the whole selection would exceed this range.
- Bounded Fit and wheel zoom to world scale 20. When a selection exceeds that range, Fit chooses visible changed content, then occupied content. Hidden unchanged layers cannot displace the changed layer's anchor, and clipping is applied in inspection coordinates before choosing bounds or content.
- Added the private header-only `map_compare_render.h` helper to seek visible columns within each visible row. Tiles, change highlights, and special-tile labels use it with the actual clipped viewport. Tile marker outlines and plus/minus symbols share a render batch per layer.
- Extended `src/test/native/map_compare.cpp` with a dense 2048-by-1024 fixture (2,097,152 occupied tiles), layer/group and context coverage, 131,072 tile-only replacements near a far-edge viewport, deterministic framebuffer samples, a marker submission bound, and hidden-layer/clipping Fit regressions. Optional local fixtures are copied into isolated storage by `run_map_compare.py`; source maps are never saved or modified.
- Added deterministic `src/test/map_compare_render.cpp` checks for far-edge culling cost, sparse/empty rows, and viewport boundaries, and registered the private header and tests in CMake. Updated `docs/map-comparison.md` with zoom behavior and reproducible benchmarks. No map format, comparison semantics, merge action, or shared rendering API changed.

## Review Triage Log

Blind review returned four findings (floor: min(floor(sqrt(14.227) + 1), 10) = 4). All were verified and patched; nothing was deferred.

1. **medium / patch — hidden unchanged content could steal the oversized group Fit anchor.** The original first-candidate latch chose the first occupied child even with context disabled. Fit now filters hidden entries and ranks changed/focused content; the native hidden-layer case samples the far-away changed tile successfully in both panes.
2. **medium / patch — Fit could choose content outside group clipping.** Bounds and the first-cell anchor originally ignored clipping. Fit now intersects them with the group clip in inspection coordinates and skips fully clipped candidates; a near-origin cell plus a distant clip regression confirms the visible far tile is shown.
3. **medium / patch — the performance fixture covered only whole-layer drawing.** Renaming the layer made both context modes take the same whole-layer path. The added tile-only phase restores layer/group names, verifies an empty property diff and 131,072 tile changes, and exercises context off/on at the far right edge.
4. **medium / patch — timing logs and zoom assertions could miss rendering regressions.** Added a 640,000-cell traversal test with fewer than 2,000 coordinate reads for a 36-cell viewport, exact far-edge framebuffer samples, and fewer than 20 render submissions for a changed-tile pane. These checks catch off-screen scans, missing visible tiles, and per-tile marker batches independently of machine speed.

## Verification

- Baseline Springlobe 4 benchmark: largest occupied layer 2800-by-1750, 1,293,219 occupied tiles; Fit zoom 253.646, wheel zoom 4096, mean rendering 42.69 ms with context off and 42.01 ms on. The user-reported sub-1-FPS rate was not reproduced on this machine, but the excessive rendering workload was confirmed. Evidence: `out/map-compare-smoke/perf-before.log`.
- Final Springlobe 4 Release benchmark: Fit/wheel zoom 20; context off 2.98 ms, on 2.88 ms, maximum zoom-out 3.05 ms, group context on 2.89 ms and off 2.69 ms. All 46 native steps passed. Timings include framebuffer synchronization and are measurements, not hardware-independent thresholds. Evidence: `out/map-compare-smoke/perf-review-springlobe.log` and native run `run-d10lkk8u`.
- Release native suite: 487 steps passed, including pixel, culling-related far-edge, batching, Fit, input, source-state, and four-map corpus checks. Dense whole-layer and tile-only mean frame measurements were 3.58–4.80 ms across the tested modes. Evidence: `out/map-compare-smoke/perf-review-native.log`.
- Debug core suite: 397 tests passed; three pre-existing disabled tests remain disabled. Evidence: `out/map-compare-smoke/perf-review-debug-core.log`. Focused Release checks: 19 passed (`MapDiff`, `Editor`, and `MapCompareRender`).
- Debug native suite: the same 487 steps passed with assertions enabled (`out/map-compare-smoke/perf-review-debug-native.log`).
- Clang-format 20.1.8 and `git diff --check` passed. Inspected the final Springlobe 4 framebuffer screenshot: both panes show actual map content at the bounded inspection scale.
- Restored the Release build to `MAP_COMPARE_NATIVE_TEST=OFF` and rebuilt the ordinary `out/build/map-comparison-release/DDNet.exe` successfully. Evidence: `out/map-compare-smoke/perf-final-normal-build.log`. The pre-existing `ddnet-libs` submodule change remains untouched.
