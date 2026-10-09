---
title: 'Restore context choices and navigate actual map differences'
type: 'feature'
created: '2026-10-09'
status: 'done'
route: 'oneshot'
baseline_commit: 'f09a410e983ee4b1ede70d826ce3c5bd7e775940'
review_loop_iteration: 1
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Full-map context replaced the useful selected-layer/group context. Prev/Next can stop on unchanged navigator entries, and the mixed changed/unchanged list makes review harder.

**Approach:** Offer context off, context within the current selection, and full-map context. Prev/Next and P/N move between reported differences, focusing the corresponding tile/object or property and skipping unchanged entries and ambiguity-only information. Put unchanged navigator items in a separate collapsible section, as the user invited experimentation with that layout. Preserve read-only source state and the large-map rendering improvements.

</frozen-after-approval>

## Implementation Notes

- Continue on the existing feature branch. The known user-owned `ddnet-libs` change is excluded. This is one small comparison-browsing enhancement with no public API, irreversible action or unresolved intent choice.
- `CMapCompare::Navigate` currently uses the visible navigator on crossing an entry boundary. `RebuildDetails` includes child changes even for group-property rows in layer mode, causing repeat stops. Separate the changed-entry sequence from display rows; only group mode aggregates child detail targets. Skip matching-only structure rows during traversal and update focus consistently in both directions.
- Extend the existing context state/control to Off → Layer/Group → Map → Off. Restrict the context pass to selected layers in selection mode; retain full-map culling and caching in map mode. Context switches preserve the current camera and selection where possible.
- Group unchanged eligible entries after changed entries beneath a collapsible header. Group child changes count as changes. Start collapsed for changed maps; enable an expanded view for equal maps when context is chosen. Keyboard list movement must skip the header, and collapse/filter changes must leave selection valid.
- Footprint: `src/game/editor/map_compare.h/.cpp`, native viewer checks in `src/test/native/map_compare.cpp`, and `docs/map-comparison.md`. Verify context cycling, actual tile/object/property targets, both navigation directions and wraparound, no duplicate child visits in layer mode, unchanged sections, equal maps, source preservation and the existing Springlobe benchmark.
- Implemented a separate changed-entry sequence and skipped matching-only notices inside group details. The display sequence uses a nonselectable collapsible section header; arrow, End and Page Down movement resolves to real rows. Per-group counts aggregate children only in group mode. On equal maps, selection context chooses an available spatial entry rather than initially showing metadata with blank canvases.
- Layer property and structural targets now fit the corresponding child even in group mode. Existing full-map cache/culling code is unchanged; selection context restricts which layers enter that rendering path.
- The child-fit restriction applies only to actual differences: an unchanged ambiguous matching notice still permits group fitting. The existing distant-tile framebuffer regression caught this during the final run; the correction preserves that behavior. Synthetic native fixtures now initialize their display names, matching the normal editor's tab setup rather than rendering uninitialized test labels.
- Native checks exercise all three context states and compare actual framebuffer colors, toolbar/keyboard navigation, camera targets, group/layer switching, section expansion/collapse, equal-map no-op navigation, source preservation, and a child property alongside a distant changed sibling. Initial native expectations omitted the ten property differences belonging to an added group; corrected to include them, after which the 854-step intermediate run passed.

## Review Triage Log

- Blind review (`/root/navigation_review`, same-model context-free reviewer), one round. No additional review layers configured. All five findings checked against surrounding code and native evidence; nothing deferred.
- **medium / patched:** End or Page Down on the collapsed trailing header restored the previous row instead of reaching the last real row. The skip now falls back to the adjacent real row; native checks also cover the header-only equal-map case.
- **medium / patched:** In layer mode group labels still aggregated child counts although the detail list now contains only the group's own changes. Aggregation is now gated by group mode; the Navigation fixture has one own property difference and separately listed child changes.
- **medium / patched:** Enabling selection context on equal maps initially selected map metadata and rendered no layers. Replacement selection now prefers a layer or a group containing layers; a native assertion checks spatial selection.
- **medium / patched:** Group navigation to child property/structure targets fitted the aggregate group. Fit now limits any layer detail to its owning layer, while group properties still fit the group. The new child-property fixture checks the expected camera center despite a 4096-column sibling.
- **low / patched:** Initial test expected an added empty group to contribute only one structural target, causing six assertion failures and invalid wraparound expectations. The snapshot also reports nine persisted properties plus the name, so the corrected fixture expects sixteen total differences and explicitly visits those properties. This was a test expectation error, not a product navigation failure.

## Verification

- Release core suite: all 397 tests passed; three pre-existing disabled tests. `out/map-compare-smoke/navigation-core.log`.
- Final production-code Release native run: 876 steps, zero failures, including all bundled maps (`coverage`, `Tutorial`, `Tsunami`, `Sunny Side Up`), source-state preservation and framebuffer assertions. `out/map-compare-smoke/navigation-native-release-final2.log`, evidence `out/build/map-comparison-release/map-compare-native/run-b_365a3x`.
- Debug native run: the same 876 steps passed with zero failures. `out/map-compare-smoke/navigation-native-debug.log`, evidence `out/build/map-comparison-native/map-compare-native/run-lncql60k`.
- Release rerun after initializing synthetic display names: all 876 steps passed. `out/map-compare-smoke/navigation-native-release-final3.log`, evidence `out/build/map-comparison-release/map-compare-native/run-poatp1pc`. Inspected the unchanged-section and selection-context screenshots; group counts and labels match the mode.
- Springlobe 4 local fixture: 2800-by-1750 densest layer, 1,293,219 occupied tiles; 67 steps, zero failures. Selection context averaged 2.53 ms/frame; full-map context 2.43 ms in layer mode and 2.61 ms in group mode. Rapid pan/zoom averaged 4.89/4.69 ms, maxima 11.46/5.95 ms. Timings include framebuffer synchronization on this Windows/OpenGL 3.3 machine and are not a cross-hardware FPS guarantee. Evidence: `out/map-compare-smoke/navigation-springlobe.log`, `out/build/map-comparison-release/map-compare-native/run-y9zq6grb`.
- Formatting and whitespace checks passed with clang-format 20.1.8 and `git diff --check`. Restored Release `MAP_COMPARE_NATIVE_TEST=OFF` and rebuilt the normal client at `out/build/map-comparison-release/DDNet.exe`; `out/map-compare-smoke/navigation-build-normal.log`. The existing MSVC ignored-nodiscard warning in the fit helper remains outside this change.
