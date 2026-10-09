---
title: 'Full-map context in the comparison viewer'
type: 'feature'
created: '2026-10-09'
status: 'done'
route: 'oneshot'
baseline_commit: '2f7a822a05c2f54560e64ac3e50d28913de874df'
review_loop_iteration: 1
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparing an isolated layer or group makes it difficult to locate edits within a large map. The user needs the surrounding map to understand where changes were made while retaining the responsiveness of the previous performance fix, including on Springlobe 4.

**Approach:** Extend the existing context toggle to show the whole map dimmed, with the selected layer/group and its red/green changes clearly emphasized. Keep exact read-only comparison and linked navigation. Bound rendering work to the viewport, reuse tile geometry instead of rebuilding it every frame, and verify both visual correctness and large-map rendering performance, including navigation.

</frozen-after-approval>

## Implementation Notes

- Continue on `feature/editor-map-comparison`; the existing user-owned `ddnet-libs` change remains untouched. No unresolved intent choice, irreversible action, or public API is needed; this is one private viewer enhancement plus its regression checks.
- `CMapCompare::RenderPane` currently restricts layers to the selection. Render surrounding layers in source group/layer order with neutral dimming, then selected content prominently; do not show unrelated changes in red/green. Fit remains tied to the selection. Background gameplay text and sound handles are unnecessary clutter; selected overlays remain available.
- Reuse `IGraphics::RenderTileLayer` and its existing packed tile vertex format for a comparison-owned viewport cache. The normal editor's tile renderer is immediate; the game `CRenderLayerTile` depends on serialized `IMap` and is unsuitable for unsaved editor snapshots. Use canonical cells, the existing row/column culling helper, group clipping and the scale-20 limit. Keep the immediate culling fallback for unbuffered backends.
- Cache only the visible rectangle plus a small margin per layer; rebuild when the viewport leaves it or its scale changes substantially. Release GPU containers on close, reopen, Swap and invalidation. No framebuffer capture or engine API change is required. Cull quad context against evaluated bounds.
- Planned footprint: `src/game/editor/map_compare.h/.cpp`, native regression/benchmark code and runner in `src/test/native/`, and `docs/map-comparison.md`. Native checks will verify cross-group context, selection prominence, ordering/clipping/projection, cache reuse/invalidation, and source preservation. Benchmark real Springlobe 4 with context enabled and disabled at rest and while panning/zooming.
- Implemented a private RAII render cache in `map_compare.cpp`, keyed by immutable snapshot layer. Cached tile vertices cover the clipped viewport plus 16 tiles on each edge and are replaced when panning leaves that area or zooming in makes it unnecessarily large. Uniform tint reuses the same geometry across selection/context styles. The legacy sparse immediate path remains available.
- Context uses 25% opacity for surrounding layers and 65% for unchanged selected content, with full-strength change highlights. Context is sorted by each source's group/layer indices, and selected layers are drawn last. Fixed-envelope quad bounds cull off-screen objects. Cache release follows existing comparison lifecycle, including Swap's recapture path.
- Initial real Springlobe 4 Release benchmark (2800-by-1750 densest layer, 1,293,219 occupied tiles): full-map context mean 2.78 ms in layer mode and 2.81 ms in group mode. This is comparable to the previous selected-only context measurements of about 3 ms. Expanded navigation and correctness checks follow.
- Review corrections use a separate final diff pass after all context, so unchanged siblings cannot cover highlights. Untextured layers take the existing immediate path because Vulkan's untextured tile pipeline expects a different stride. Fixed-time quad bounds are cached and culled in blocks of 64 objects before visiting visible objects. Context toggling scrolls the navigator to retain the visible selection.

## Review Triage Log

Independent blind review returned six findings (floor: min(floor(sqrt(33.928) + 1), 10) = 6). All six were verified and routed to local patches; no intent changes or deferred work.

1. **medium / patch — untextured Vulkan vertex stride.** Verified `CreateTileGraphicsPipelineImpl` fixes untextured stride at 8 bytes versus the cache's packed 12-byte vertices. Texture validity now gates the buffered path. Added an untextured context pixel and a command-stream check that packed tile draws always select a textured pipeline. Vulkan is disabled in this local build, so backend execution is not claimed.
2. **medium / patch — repeated off-screen quad envelope evaluation.** The prior context loop evaluated bounds for every quad every frame. Cached fixed-time bounds and aggregate object blocks now reject off-screen objects before rendering. The native fixture includes 4,096 distant quads plus one visible quad and exercises context navigation.
3. **medium / patch — later unchanged siblings obscured group highlights.** Verified the former single-pass loop drew each layer's context and highlights before the next layer. Rendering now finishes context before the final diff pass. Three overlapping unchanged white siblings cover the changed tile in the regression fixture; the red/green pixel must remain full strength.
4. **medium / patch — white texture did not verify packed UVs.** Added distinct multicolor asymmetric atlas tiles with all eight flip/rotation combinations and framebuffer comparisons between cached and immediate context output.
5. **medium / patch — cache rebuilds lacked pixel checks.** Added framebuffer comparisons after panning beyond the margin to distant occupied cells, zooming in to shrink the buffer, moving to an empty region, and returning. This covers the cache invalidation branches independently of timing.
6. **medium / patch — zero reference position did not test parallax.** Added a visible background quad with Before/After parallax 0%/50%, a nonzero shared reference, and probes at both expected and rejected positions before and after linked pan/zoom.

## Verification

- Release native suite: 549 scripted steps passed, including all new framebuffer, cache, ordering, parallax and lifecycle regressions, the two-million-tile fixture, and the four-map corpus. Evidence: `out/map-compare-smoke/context-final-native.log`, run `run-0xj2z2g6`.
- Final Springlobe 4 Release benchmark: full context averaged 2.34 ms per frame in layer mode and 2.37 ms in group mode; rapid pan/zoom averaged 3.65/4.08 ms with maxima 5.60/5.25 ms respectively. Context off averaged 2.20/1.89 ms. All 54 benchmark steps passed. Evidence: `out/map-compare-smoke/context-final-springlobe.log`, run `run-rdtfftmv`. Timings include framebuffer synchronization; they are local measurements, not hardware-independent guarantees. The full-context result remains comparable to or faster than the previous roughly 3 ms selected-only context measurement.
- Debug core suite: all 397 tests passed; three pre-existing tests remain disabled (`out/map-compare-smoke/context-core.log`).
- Debug native suite: all 549 steps passed with assertions enabled (`out/map-compare-smoke/context-final-debug-native.log`, run `run-bbrcoziu`).
- Visually inspected the final Springlobe full-context screenshot and synthetic layer/group screenshots. Selected navigator rows remain visible after toggling context, surrounding content is neutral and dim, and selected changes remain prominent. Source map files were copied to isolated test storage and never saved.
- Clang-format 20.1.8 and `git diff --check` passed. Rebuilt the ordinary Release `out/build/map-comparison-release/DDNet.exe` with `MAP_COMPARE_NATIVE_TEST=OFF` (`out/map-compare-smoke/context-normal-build.log`). The user's pre-existing `ddnet-libs` change remains untouched and outside the commit.
