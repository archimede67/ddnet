---
title: 'Inspect comparison layers and control their visibility'
type: 'feature'
created: '2026-10-09'
status: 'done'
route: 'oneshot'
baseline_commit: '98856ba2a9d4826a2e7ca5e9b5670db2b013f6c8'
review_loop_iteration: 1
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparing maps needs individual layer/group visibility controls and an understandable overview of structural changes without crowding the existing controls. Toggling the Unchanged section currently forces the navigator to jump.

**Approach:** Keep Changes as the focused diff navigator and add a separate Layers sidebar view containing a collapsible group/layer hierarchy, text-and-color diff states, and visibility controls. Viewer-only visibility applies to both sides, preserving source maps and large-map rendering performance. Collapsing or expanding sections preserves the user's scroll position and inspected content, allowing only the necessary clamp if content becomes shorter than the viewport.

</frozen-after-approval>

## Implementation Notes

- Continue on `feature/editor-map-comparison`; leave the known user-owned `ddnet-libs` change untouched. The user delegated UI design choices. This is one bounded viewer-navigation enhancement with no public API, irreversible action, or unresolved intent choice; use the oneshot route.
- Considered adding eye controls to every Changes row versus a dedicated layer tree. Choose two sidebar tabs (Changes / Layers) using the existing summary-row space: the tree preserves group hierarchy while the Changes list remains optimized for actual differences. Preserve canvas and toolbar space; independent scroll states for the views.
- Footprint: `map_compare.h/.cpp` own all new state and controls; `src/test/native/map_compare.cpp` verifies actual UI/render behavior; update `docs/map-comparison.md`. No map model changes and no changes to general-purpose scrolling APIs.
- Own visibility/expansion state per matched result entry, reset on recomputation/reopen. A group visibility gate preserves individual child choices; a hidden parent suppresses its layers in both panes. Eye controls and Show all affect only the viewer. Filter before geometry generation/submission; reuse current viewport culling and GPU buffers. Hidden entries remain in diff navigation/counts and are visibly identified as hidden.
- Layer tree includes unchanged and added/deleted empty groups, shows aggregate child changes on group rows, and labels additions green, deletions red, modifications amber, and unchanged neutral, retaining matching ambiguity notices. Selecting a group/layer targets the corresponding inspector; Prev/Next still visits actual differences and reveals the tree ancestor when needed.
- Remove forced scroll-to-selection on Unchanged expansion/collapse. Preserve current selection, camera and detail while folding sections; hide rows without replacing the inspected item. Natural scroll bounds still apply. Keep changes-only filtering behavior when context is explicitly switched off.
- Verify native eye/fold controls without unintended selection changes, independent parent/child gates, statuses, view switching, missing sides, empty groups, hidden current difference feedback, Show all and reset lifecycle, source visibility flags/dirty state, framebuffer exclusion and cache reuse, unchanged-section scrolled toggles, compact-window layout, and Springlobe 4 performance. Review all findings, rebuild the normal Release client, and commit own files only.
- Implemented viewer-owned masks before render-list construction, a Layers tree with aligned eye controls and cached aggregate change counts, and a shared visibility summary/Show all footer. Both views use text plus color for statuses. Tree selection uses the existing inspector, and P/N reveal the focused child's parent without unhiding anything. Added tree keyboard focus so arrows/Page/End do not accidentally navigate the inspector.
- Removing unconditional ScrollToSelected preserves the Changes viewport and inspection. Also reset only the relevant viewer scroll widget when a fold makes content fit: generic CScrollRegion preserves old animation positions when it stops overflowing, which otherwise resurrects the previous offset on expansion. No general scrolling API changed.
- Native verification extends source-state checks to group/layer visibility flags, samples actual hidden-layer pixels, verifies Swap/reopen state, and covers both overflowing and collapse-to-fit boundaries in each sidebar. Initial fixture names exceeded DDNet's 11-character names; shortened them so test lookup matches persisted names.

## Review Triage Log

- One context-free same-model blind review (`/root/layer_visibility_review`); no additional layers configured. All six findings checked against surrounding code, with no deferrals.
- **medium / patched:** Swap bypasses ReleaseBuffers; resize plus accumulation retained visibility against potentially reordered entries and doubled counts. Assign default entry states and reset the tree on each new result. Regression swaps a hidden group twice and checks visibility and exact aggregate counts each time.
- **medium / patched:** Folding until content fits clears displayed offset but not CScrollRegion's stored animation position. Reset the viewer's corresponding list/region on the fit transition; new tests fold/expand both a short Changes section and a large second tree group after wheel scrolling.
- **medium / patched:** Hidden entries looked visible in Changes. Their rows now explicitly say Hidden, including descendants of hidden groups; the shared footer reports hidden count and offers Show all.
- **medium / patched:** Selecting a group can initially select a child detail while m_FocusEntry still names the group. Pane notices now use the actual selected detail first. The native test selects a group whose first child difference is hidden and checks notice-colored pixels in both panes.
- **medium / patched:** Layers had no keyboard focus handling, so arrows reached the inspector. Added explicit tree/inspector focus and arrow/Page/End handling, including parent selection and folding; native keyboard tests cover these paths.
- **medium / patched:** Unchanged tree entries with context off produced unexplained empty panes. Added an inline Enable Context notice while retaining the user's context choice; native framebuffer checks cover the notice.

## Verification

- Core Release: 397 tests passed, three pre-existing disabled; `out/map-compare-smoke/layers-core.log`.
- Release native: all 1,214 steps passed, including bundled maps, viewport pixels, source visibility flags, parent/child eye controls, group/layer selection, keyboard focus, short/long section scrolling and Swap/reopen. `out/map-compare-smoke/layers-native-release2.log`, evidence `out/build/map-comparison-release/map-compare-native/run-lxdbnrdo`. Inspected Layers screenshots at 1280x800 and 800x600: names/statuses, eye controls, inspector, and notices remain separated and readable.
- Debug native: the same 1,214 steps passed with zero failures; `out/map-compare-smoke/layers-native-debug.log`, evidence `out/build/map-comparison-native/map-compare-native/run-rkhs__8b`. The isolated initial context-toggle failure did not recur in either final Release or Debug run; the final harness records actual and target context for diagnosis.
- Springlobe 4: 82 steps passed on the downloaded 2800x1750, 1,293,219-occupied-tile fixture. Full-map context averaged 2.59 ms/frame in Changes and 2.73 ms with Layers open (max 3.03 ms); group context 2.52 ms. Rapid pan/zoom averaged 4.61/5.45 ms with maxima 6.13/8.52 ms. These are renderer timings including framebuffer synchronization on this Windows/OpenGL 3.3 machine, not a cross-hardware FPS guarantee. `out/map-compare-smoke/layers-springlobe.log`, evidence `out/build/map-comparison-release/map-compare-native/run-vybzfsff`.
- Normal Release client rebuilt successfully at `out/build/map-comparison-release/DDNet.exe` with `MAP_COMPARE_NATIVE_TEST=OFF`; `out/map-compare-smoke/layers-build-normal.log`. Formatting (clang-format 20.1.8) and `git diff --check` passed. The existing ignored-nodiscard compiler warning in the fit helper remains unchanged.
