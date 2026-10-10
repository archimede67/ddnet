---
title: 'Keep unchanged comparison content neutral after layer property edits'
type: 'bugfix'
created: '2026-10-10'
status: 'done'
route: 'oneshot'
baseline_commit: '1d69b758179c9c7de616d5b405ff91c81348604c'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Identical tiles appear red Before and green After when a layer property changes. The supplied 1AndOnLyLife comparison shows a Game layer resize from999 to955 tiles; the unchanged cells have no change markers but are tinted as edits.

**Approach:** Reserve red/green content tint for actual tile/object differences or wholly added/removed layers. Keep unchanged selected content neutral in layer/group/map context, while properties and draw order remain inspectable in the tree and detail inspector. Preserve source rendering properties, viewport culling, cache reuse and comparison-only state.

</frozen-after-approval>

## Implementation Notes

- No intent gaps, irreversible actions or public API changes. Continue on feature/editor-map-comparison, leaving the existing ddnet-libs change untouched. Footprint: private RenderPane behavior, native regression harness and comparison documentation.
- Root cause: map_compare.cpp RenderPane computes WholeLayer from any layer property/reorder or selected-group property/reorder, draws every occupied tile with Highlight, but derives markers only from m_vTiles. Restrict whole-layer tint to unmatched layers (Before or After absent). m_vTiles/m_vObjects already describe actual changes, including additions/removals. Apply consistently to tiles, quads and sounds; preserve actual object reorder highlights from m_vObjects. Context uses source colors/envelopes with existing neutral opacity. Keep properties, geometry/projection and diff matching unchanged.
- Metadata-only comparison with Context off may legitimately show no tile/object content. Reuse the existing notice if needed to explain that Context reveals content while properties remain in the inspector; avoid scanning every tile per frame.
- Add native framebuffer checks for resize plus changed/unchanged cells, layer/group metadata and reordering, whole-layer add/remove, actual quad/sound changes and unchanged objects. Cover Context off/selection/map and layer/group modes with a deterministic white source texture. Keep genuine content differences and source-state assertions. Adapt any pre-existing whole-layer-property assumptions, including the performance fixture comment; do not remove meaningful renderer coverage.
- Reproduce with the exact downloaded 1AndOnLyLife pair from the screenshot using isolated storage, inspecting the Game layer and shared cells versus real tile changes. Reuse the existing optional local-map runner copy mechanism; never save user maps. Record a screenshot/pixel evidence and map dimensions.
- Build Debug and Release native clients. Run focused new rendering checks in Debug, existing MapDiff/MapCompareRender/Editor tests, unfiltered Release native suite and isolated Springlobe4 benchmark. Inspect relevant screenshots. Run clang-format20.1.8/diff checks, restore Release MAP_COMPARE_NATIVE_TEST=OFF, rebuild normal DDNet.exe and scan for native instrumentation markers. Run the oneshot independent review, triage findings, and commit locally without pushing.

- Initial focused native checks passed118 steps after correcting a fixture name exceeding the group-name limit. Exact downloaded pair reproduced width999to955 and matched resize-independent rendering with zero differing sampled pixels; the screenshot shows neutral unchanged terrain and retained tile highlights. Debug MapDiff/MapCompareRender/Editor passed25 tests.
- First full Release suite reached the final bundled map but exceeded its existing 10,000-frame overall budget after adding the new scenarios. Raised total budget to 12,000 frames for the expanded suite; retained the 600-frame per-step timeout and the runner's 600-second ceiling. Both initial full runs also exposed four pixel expectations in the old distant-Fit fixture that depended on a rename tinting unchanged tiles. Changed that fixture to include a real index 1-to-2 tile replacement at (2000, 900) and explicitly assert the replacement exists before testing Fit and clipping. The original evidence summary missed these four failures; the full logs were rechecked and this record corrected.

## Review Triage Log

One context-free blind review completed. All five findings individually verified and patched; no deferred findings.

1. **low / patch:** Context-off notice promised other differences for identical maps. Reused the original unchanged-content wording for both unchanged and property-only selections.
2. **medium / patch:** Synthetic group selection did not set m_ByGroup, so child inspector behavior was untested. Enter group mode through the actual toolbar and assert child resize/reorder details remain available.
3. **low / patch:** Only layer reordering was present. Added a second matched group and changed their order, retaining a group property edit and assertions on matching/reorder state.
4. **low / patch:** Cropped tile was counted but not sampled. Added Before-red/After-absent pixel checks across contexts and group/layer selection.
5. **medium / patch:** Local-map metadata-cleared comparison could pass with two blank frames. Added independent positive controls for shared occupied cell rendering and actual side-specific diff-mark pixels. The first control incorrectly assumed the change at (870, 47) was occupied on both sides; corrected it to check the known change and independently locate an occupied changed cell on each side of the viewport.

## Verification

- Debug focused native suite: 126 steps, zero failures (`out/build/map-comparison-native/map-compare-native/run-b7rn9mgg`). Covers independent framebuffer expectations in all three contexts and both navigation modes, including cropped tiles, metadata-only layers, group/layer reordering, quads and sounds. Source snapshots, visibility, histories and brush remain unchanged.
- Debug MapDiff/MapCompareRender/Editor: 25 tests passed. Release core suite: 403 tests passed; three pre-existing tests disabled (`out/map-compare-smoke/tint-final-release-core.log`).
- Exact downloaded 1AndOnLyLife pair: Release targeted native suite, 13 steps, zero failures (`out/map-compare-smoke/tint-reported-final/run-sxboh3m3`). Reproduced width 999 to 955. Positive controls found 439/460 visible shared-cell pixels and 50/54 actual diff-mark pixels in Before/After. Clearing resize/order metadata produced zero differing sampled pixels. Visually inspected the real-map full-context screenshot and the synthetic group-context screenshot: unchanged content is neutral and genuine changes remain highlighted.
- Final unfiltered Release native suite: 4,319 steps, zero failures in 185.37 seconds (`out/build/map-comparison-release/map-compare-native/run-qfdjx9b0`), including the corrected distant-Fit fixture and all bundled-map checks.
- Isolated Springlobe 4 benchmark: 100 steps, zero failures (`out/map-compare-smoke/tint-springlobe/run-h0zqv1un`). Largest layer 2,800 by 1,750, 1,293,219 occupied cells. Full-map context mean 3.41 ms/frame; with layer tree 4.23 ms; group context 3.13 ms. Pan/zoom means 4.86/4.87 ms, maxima 7.67/7.31 ms. These remain in the preceding implementation's range. Initial/refresh captures remain separate (1.49–1.52 seconds); unchanged tab switching still performs zero captures and repeated edits coalesce into one refresh.
- Final Debug and Release native builds succeeded; clang-format 20.1.8 dry-run and git diff whitespace checks passed. Existing C4834 warning in map_compare.cpp is unchanged.
- Restored Release `MAP_COMPARE_NATIVE_TEST=OFF` and rebuilt normal `out/build/map-comparison-release/DDNet.exe`. Verified the cache flag and absence of all native harness marker strings in both ASCII and UTF-16LE. No user map files saved; pre-existing ddnet-libs changes remain excluded.
