---
title: 'Inspect a comparison change by clicking either map pane'
type: 'feature'
created: '2026-10-10'
status: 'done'
route: 'oneshot'
baseline_commit: '6f78488490f1d5afcb4ebd56939cf6ef9c2a7ed5'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** A visible change in either comparison map pane cannot be selected directly for inspection; users must locate it in the change list.

**Approach:** A left click on changed tiles, quads or sound sources selects and reveals the corresponding detail and owning layer/group. Preserve drag-to-pan, zoom, source maps and efficient rendering. Reuse the existing selection/scroll components and the renderer's visibility, clipping, transforms and draw order.

</frozen-after-approval>

## Implementation Notes

- No intent gaps or irreversible work. Continue on feature/editor-map-comparison; leave the pre-existing ddnet-libs change alone. Private viewer state/helpers only, native tests and documentation; no merge/edit commands or public APIs. Independent read-only investigation completed.
- `src/game/editor/map_compare.cpp/.h`: commit clicks on left release inside the original pane with a small drag threshold. Left/middle dragging keeps panning; mixed buttons, wheel gestures, modal interruptions and tab/source transitions must not leave a pending pick. Empty/unchanged/absent-side content does not invent a change selection.
- Perform hit testing only on a click frame during RenderPane traversal, preserving its two passes, LayerVisible, source ordering, ProjectionCenter and game-space clipping. Binary-search sorted tile changes at one coordinate; use visible quad geometry/envelope transforms and actual sound circle/rectangle or center marker. Prefer the last drawn eligible change, with focused overlays above context. Do not scan map-sized tile arrays or add per-frame hit buffers.
- After both panes finish, reveal the owning layer (or parent in group mode) and select the exact tile/object detail via existing list/tree scrolling. Keep zoom and the inspected scene stable rather than fitting on a canvas click. Rebase the shared parallax origin when a different context group becomes the inspection group. Existing inspector clicks/Home continue to fit. Settings/envelope-specific selection remains intact.
- `src/test/native/map_compare.cpp`: exercise real SDL press/release in both panes; additions, deletions, replacements; layer/group modes; list scrolling and tree expansion; full-map context across groups including parallax/clipping; hidden and overlapping objects; transformed quads and sound shapes; unchanged/empty clicks; left/middle drag, jitter, gesture cancellation; immutable source snapshots. Add a focused native mode, integrate into full suite and preserve timeouts.
- `docs/map-comparison.md`: document click versus drag, eligibility and overlap behavior. Build Debug/Release native clients; run focused interactions, core tests and unfiltered Release suite. Use an isolated Springlobe 4 performance run and real-map picking on the reported 1AndOnLyLife pair. Inspect screenshot evidence, run clang-format 20.1.8 and diff checks, restore native testing OFF and rebuild normal DDNet.exe with instrumentation-marker scan. Complete independent oneshot blind review, triage each finding and commit locally without pushing.
- Initial focused Debug checks passed 338 steps, including native tree expansion and exact inspector selection, with 25 focused core tests passing. Initial Release core passed 403 tests (three pre-existing disabled). Real-map clicks succeeded on both sides; the framebuffer comparison initially counted 12 cursor pixels because its direct reference draw excludes the editor cursor. Moved the cursor outside the panes before that comparison; the 27-step real-map run then passed with zero failures.

## Review Triage Log

One independent context-free blind review; all six findings verified and patched, none deferred.

1. **medium / patch:** Rebased origin was missing from per-section saved cameras. Added origin to the existing section state and its save/restore path, with Changes/Layers round-trip assertions.
2. **medium / patch:** Clearing held input on SDL focus loss could finish a click. Require a real left-button release captured by OnInput; modal/zoom/tab/source cancellation still clears the gesture. Added native SDL focus-loss/regain coverage. Render-time event lookup was too late in the input lifecycle; the regression exposed it and release tracking now persists from OnInput to rendering.
3. **medium / patch:** Quad hit testing omitted the visible +/- glyph and minimum marker dimensions. Share the exact line geometry between rendering and picking; test the glyph outside a rotated quad's body on both sides.
4. **medium / patch:** Fully transparent context quad geometry could capture clicks. Exclude zero color-envelope alpha and all-zero rendered vertex alpha while keeping visible selection markers eligible. Test both invisibility sources and marker selection.
5. **medium / patch:** Quad markers are drawn after the entire layer batch but hit testing interleaved them with geometry. Apply the final marker candidate after geometry; test an earlier quad's outline above a later quad's interior.
6. **medium / patch:** Every overlapping object hit linearly searched all object changes, making a dense click quadratic. Build a source-index-to-detail lookup only on the click frame and use constant-time lookup per candidate; test 4,096 overlapping changed quads and the winning source index.

## Verification

- Review fixes passed all 425 focused native Debug steps (`out/build/map-comparison-native/map-compare-native/run-m08tffpt`) in 24.87 seconds. Debug and Release native clients build successfully. Inspected the actual canvas-picked-tile screenshot with the exact inspector row selected.
- Expanded the existing full-suite frame budget from 12,000 to 14,000 to accommodate the new interaction steps; the individual-step timeout and Python process timeout remain unchanged.
- First unfiltered Release run completed 4,741 steps with two failures in older fixtures. The new interaction tests left a cursor over a framebuffer color sample; the context fixture now parks the cursor outside the panes before sampling. The old pan fixture teleported from the inspector and pressed in the same frame, leaving that row's shared-UI hot item eligible to consume the press. It now allows the hover state to update before pressing, as the existing Click helper does, and asserts there is no hot/active UI item. The color and camera assertions remain unchanged. All new interaction assertions passed; the measured 4,096-object click frame was 3.95 ms in that run.
- Final unfiltered Release native suite: **4,743 steps, zero failures**, 202.23 seconds (`out/build/map-comparison-release/map-compare-native/run-lern0fno`, summary `out/map-compare-smoke/pick-final-release-native-2.log`). The corrected color/pan fixtures pass; dense 4,096-object click frame **4.01 ms**. Inspected this run's `save/screenshots/canvas-picked-tile.png` and confirmed the clicked After tile, exact selected inspector row and owning layer in the native shell.
- Final downloaded 1AndOnLyLife pair: **27 steps, zero failures**, including both-side exact tile picks with stable camera and source-integrity/framebuffer assertions (`out/map-compare-smoke/pick-reported-final/run-4_spjv26`).
- Core tests: Debug focused **25 passed**; full Release **403 passed**, three pre-existing disabled (`out/map-compare-smoke/pick-debug-core.log`, `pick-release-core.log`). Final Debug/Release native builds, clang-format 20.1.8 dry-run and git diff whitespace checks pass.
- Isolated Springlobe 4 run: **100 steps, zero failures**, 2,800 × 1,750 tiles / 1,293,219 occupied cells (`out/map-compare-smoke/pick-springlobe/run-bi99i37z`). Mean render times: changes only **2.63 ms**, selection context **2.41 ms**, full map **2.98 ms**, full map with tree **3.73 ms**, group full map **2.67 ms**. Pan/zoom means **4.39 / 6.55 ms**, observed maxima **7.83 / 18.85 ms** for layer/group views. These are local measurements, not a universal frame-rate guarantee. Initial pair capture **1.38 seconds**, refresh capture **1.43 seconds**; this existing snapshot cost remains separate from frame rendering.
- Restored Release `MAP_COMPARE_NATIVE_TEST=OFF`, rebuilt `out/build/map-comparison-release/DDNet.exe`, and verified ASCII/UTF-16 test markers are absent (`out/map-compare-smoke/pick-normal-build.log`). No outstanding review findings, no deferred work, source maps remain read-only. Local commit only; the pre-existing ddnet-libs change stays outside it.
