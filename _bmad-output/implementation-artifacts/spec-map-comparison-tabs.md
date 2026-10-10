---
title: 'Persistent, automatically refreshed comparison tabs'
type: 'feature'
created: '2026-10-10'
status: 'done'
route: 'dispatch'
baseline_commit: 'dc936cda42a6d445622a7e190aa64ef55de84c7f'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Leaving a comparison discards its sources and inspection state. Users need several reusable comparisons alongside editable maps, with results reflecting subsequent edits.

**Approach:** Integrate comparison documents into the shared map tab strip. Preserve independent inspection state, reopen closed comparisons without selecting sources again, and automatically refresh affected comparisons before showing their contents.

## Boundaries & Constraints

**Always:** Read-only comparisons; normal map editing/save/undo behavior; safe input transitions and source lifetimes; viewport-bounded rendering and responsive unchanged-tab switching. Preserve the preceding feature's sections, matching explanations and native regressions. Leave user-owned `ddnet-libs` untouched.

**Never:** Merge/edit through a comparison, background-read mutable maps, render/navigate stale snapshot indices against live sources, rebuild hidden comparisons per edit, or require selecting sources again for an available comparison.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected behavior | Failure handling |
|---|---|---|---|
| Multiple documents | A/B and A/C comparisons, duplicate filenames | Distinct labeled tabs; opening an existing ordered pair selects it; independent view/selection/camera/scroll/visibility | Use document identity, not filename |
| Switching | Tab clicks, Ctrl+Tab/Shift+Tab, Escape, held drag/key | Shared strip remains reachable; Escape returns to map without closing comparison; finish source gestures; no destination input leakage | Apply safe transitions at frame boundaries |
| Close/reopen | X/middle-click/Ctrl+F4; reopen last closed | Close only selected document; bounded recent comparison history and discoverable reopen command; reuse live source pair | Release heavy buffers; prune unavailable pairs |
| Refresh | Source edits, undo/redo, settings/envelopes/assets, repeated changes | Mark affected tabs pending; selected comparison refreshes automatically; unchanged switches reuse result | Reject obsolete worker results; show updating state |
| Structural edits | Delete/reorder layers/groups/resources | No stale indexing; retain camera/options and identifiable selection/visibility | Clear unavailable selections rather than select a different object accidentally |
| Source lifecycle | Close cancelled/confirmed, save/Save As, unrelated load/drop | Cancellation preserves comparisons; actual source disposal closes dependent comparisons/history only; save alone needs no refresh | Never bind a new document to an old pointer or path |
| Work/lifetime | Switch/close during capture/compute, quit, many tabs | Coalesce to newest revision; at most one worker globally; no blocking future destruction during UI transitions | Cancel/reap safely; isolate failures per comparison |

</frozen-after-approval>

## Code Map

- `src/game/editor/editor.h/.cpp`: singleton `m_MapCompare`; `DoMapTabs`, `Render`, `OnUpdate/OnInput/OnRender`, `Reset`, `CloseMap`, `Load`. Introduce stable owned comparison documents/controller and centralized tab transitions. Keep backing `m_SelectedMap` valid. Share tab strip before content; pass content rectangle into viewer. Close notifications currently occur before confirmation: move to actual disposal. Loading currently invalidates comparison even on failure: remove that coupling.
- `map_compare.h/.cpp`: Open/Start/Close discard state; OnUpdate captures owned snapshots and publishes worker results. Separate tab suspension from disposal; preserve unchanged state, bookmarks/options across refresh. Projection/Fit/RenderPane/RenderEnvelope use live source arrays: validate membership/revisions before input/render and publication, clear snapshot-address-keyed caches before replacement. Background work must not close global popups.
- `mapitems/map.h/.cpp`, `editor_history.cpp`: monotonic content revision via OnModify and successful RecordAction/Undo/Redo; do not reset on save. History currently misses several quad/settings/brush undo notifications. Ensure in-progress quad/sound operations finalize before switching.
- `editor.cpp` Add/ReplaceImage/Sound, `popups.cpp` image Embed/Make external, `map.cpp` InsertEnvelope: successful direct mutations need notification even without resource reordering. Existing save-derived tile flags are ignored by Capture and need no refresh.
- `popups.cpp`, `quick_actions.h`: route tab-context/Tools comparison creation through open-or-select; add reopen command (and conflict-free shortcut). Keep dialogs visible and associated with the correct map.
- `src/test/native/map_compare.cpp`, `run_map_compare.py`, `docs/map-comparison.md`, `CMakeLists.txt`: adapt native access/layout to document ownership, retain full corpus, add lifecycle/refresh regressions, register new files if needed.

## Tasks & Acceptance

**Execution:**
- [x] Editor/controller/viewer files — persistent tabs, shared routing, lightweight reopen history, safe transitions and source disposal.
- [x] Map/history/mutation files — reliable revision tracking, bounded refresh scheduling and safe result publication.
- [x] Native tests/docs — matrix scenarios, actual tab gestures and map edits, preserved state, stale-index protection and performance evidence.

**Acceptance Criteria:**
- Given several open map/comparison tabs, when editing either source and returning, then the current diff appears automatically without losing unrelated documents or modifying source editor state.
- Given Springlobe-sized sources, when switching unchanged tabs or editing with hidden comparisons, then no capture/recompute is performed and rendering retains its bounded cost.

## Implementation Notes

Authorized feature continuation and delegated interaction design; no unresolved intent questions or irreversible actions. Existing dirty ddnet-libs is user-owned and excluded, as in prior work. Initial spec measured1551 o200k tokens. Parent retains review/build/commit responsibility.

Implemented a shared map/comparison strip with stable owned comparison documents and ordered-pair reuse. Escape suspends a viewer; X/middle-click/Ctrl+F4 disposes it; Tools and Ctrl+Shift+F4 reopen from a ten-entry lightweight history. Transitions finish source brush/quad/envelope/sound operations, wait for held input to release, and reveal the pending destination during repeated Ctrl+Tab presses. Clean background-map closes retain the current document; dirty closes retain the source's confirmation context.

Content revisions cover OnModify, all three histories and the direct asset/envelope mutation paths. Only the selected stale comparison captures; obsolete results are rejected and at most one active or retired worker exists globally. Object bookmarks preserve surviving selection, visibility, scroll and cameras across refresh; unavailable indexed details clear safely. Actual source disposal prunes only dependent documents/history.

The native corpus covers the lifecycle matrix, native tab shortcuts/clicks, held gestures with undo, source-close cancellation, unrelated document survival, stale workers, pre-capture close/reopen, revision-preserving inspection and real-map capture counts. The optional large-map run records capture/compare/render/motion timings and normal/800x600 full-tree screenshots. Parent independent Debug/core/Springlobe verification and the normal-client rebuild are complete.

## Spec Change Log

## Review Triage Log

| Finding | Verdict | Evidence and route |
|---|---|---|
| Blind 1: selector IDs collide with shared strip | medium | RenderSelector uses the same two map member IDs as the simultaneously rendered strip; earlier button logic consumes releases. Patch: separate selector IDs and native source-selection regression. |
| Blind 2: property popup drag can be interrupted | medium | Ctrl+Tab currently ignores ordinary open popups; group/tile/sound property trackers commit on the popup's END callback. Patch: respect popup ownership for document shortcuts, retaining its release path. |
| Blind 3: interrupted tangent edits miss revision | medium | Both tangent branches assign Bezier values while dragging but notify only on release; Stop does not record tangents. Patch: notify actual tangent mutations immediately, with interrupted-switch coverage. |
| Blind 4: quad envelope preview edits miss revision | medium | DoQuadEnvPoint directly writes point values without OnModify/history. Revision reuse newly exposes stale results. Patch: notify actual preview mutations and verify refresh; pre-existing preview undo limitations are not expanded here. |
| Blind 5: ordinary-map tab tooltips cleared | low | Render calls DoMapTabs then clears m_aTooltip again before the normal content. Patch: remove the second clear. |
| Blind 6: hidden GUI leaves black top strip | low | BackgroundView unconditionally loses 44 units even when the shared strip is hidden. Patch: condition the crop on GUI visibility. |
| Blind 7: comparison shortcuts bypass open popups | medium | OnInput handles Escape/P/N/Home before Ui popup processing. Patch: give open popups input ownership; verify dismissal does not leave the comparison. |
| Blind 8: inactive section detail lost on refresh | medium | RestoreView unconditionally clears each section detail; only the active detail has a stable bookmark. Patch: apply the existing detail-bookmark logic per section, retaining only safely identified details. |
| Blind 9: reopen loses inspector keyboard focus | low | Open resets LayerTreeActive and CClosed does not retain it. Patch: include this existing boolean in saved inspection state. |
| Blind 10: history records editor-only SHIFT_BY as modification | low | RecordAction now dirties this preference as well; its Undo/Redo already dirtied it before this change. The added nuisance is an unnecessary dirty marker/refresh on initial preference adjustment, with no content loss. Rejected low: infrequent preference adjustment and fixing it requires action-level content classification across bulk/history paths, beyond a direct correction. |
| Edge 1: selector ID collision | medium | Independently confirmed at RenderSelector/DoMapTabs; same root cause as Blind 1, grouped after individual verdict. Patch. |
| Edge 2: envelope preview stale result | medium | Independently confirmed direct m_aValues writes and revision-only Current check; same root cause as Blind 4. Patch. |
| Gap 1: inactive comparison click untested | medium | Filed mutation demonstration shows the current click assertion already selects AB before clicking AB. Patch: select AC then click AB and assert actual transition and retained state. |
| Gap 2: surviving detail refresh untested | medium | Filed mutation demonstration removes RestoreView's active detail loop without failing existing tests. Patch: create an earlier difference and assert the original coordinates survive changed numeric indexing. |
| Gap 3: direct sound mutation refresh untested | medium | Filed callback search finds only image Add/Replace tests. Patch: exercise AddSound/ReplaceSound with valid fixtures and assert revision plus refreshed sound result. |

All three independent review layers completed before triage. Patch groups: selector identity (Blind 1/Edge 1); popup ownership (Blind 2/7); tangent notification (Blind 3); preview notification (Blind 4/Edge 2); tooltip/background corrections (Blind 5/6); section detail restoration (Blind 8); focus restoration (Blind 9); and the three distinct verification gaps. No deferred findings.

Patch disposition: all accepted groups implemented by the original implementation agent and read by the parent. Focused native review mode passed 428 steps (`run-8o6_m4xc`). Parent full patched Release native run passed 2,491 steps with zero failures in 108.85s (`run-r80abp5w`); Release core passed 401 tests. New coverage checks both source selectors, inactive tab selection, active/inactive detail identity after index shifts, reopened inspector arrows, popup input ownership/property END history, both interrupted tangents, quad preview MOVE/ROTATE, and actual Opus Add/Replace callback content. The reviewed diff has been rewritten to include the patches.

## Design Notes

Use the existing tab experience, with identifiable Before/After titles. Recent closed comparisons are session-local, capped at ten lightweight descriptors; reopening requires their source map tabs to remain open. New comparison selection reuses an existing ordered pair; Swap selects an existing reversed pair if present. Inactive tabs mark pending without capturing; activation refreshes the latest revision. Preserve stable object bookmarks where possible; never treat a reused numeric index as identity. Keep captured worker data owned and cancellable; retired work must not accumulate across rapid switches. Snapshot capture stays on the UI thread; measure its cost separately from unchanged switching/rendering.

## Verification

- Final parent verification after all review patches: Release native 2,491 steps / zero failures / 108.85s (`run-r80abp5w`); Debug native 2,491 steps / zero failures / 200.08s (`run-xz30_jph`); Release core 401 passed (3 existing disabled), Debug focused core 23 passed. clang-format20.1.8 dry-run and diff checks passed. Final Release `MAP_COMPARE_NATIVE_TEST=OFF` configure/build succeeded; normal `DDNet.exe` contains none of the native-test marker strings.
- Final Springlobe repeat (`tabs-final-springlobe/run-1abpdnn9`): 99 steps passed; unchanged switching captured zero times and 20 hidden edits still coalesced to one activation capture. Capture 1.46–1.47s, pure compare 170ms, unchanged selection request 0.0072ms. Full-map layer/tree/group render means 2.96/3.56/2.88ms; pan/zoom maxima 7.82/5.89ms. Final multi-comparison and compact 800x600 Springlobe screenshots visually checked. Heavy capture remains on the UI thread and is explicitly documented; no claim of instant large-map refresh.
- Parent independent verification: complete baseline diff read; all seven matrix rows audited against the executed native report. Multiple-pair reuse/state, held-input transitions/undo, close/reopen/history bound, all three histories/assets refresh, replacement-index safety, save/cancelled and actual source disposal/unrelated load, and obsolete/global-worker checks all passed. Release core: 401 tests passed (3 existing disabled); Debug focused core: 23 passed; Debug native: 2,284 steps, zero failures, 194.91s (`run-wq6luk9p`).
- Springlobe 4 independent Release run (`tabs-springlobe/run-nn4f9nl8`): 99 steps passed. Largest layer 2800x1750, 1,293,219 occupied tiles; unchanged switching captured zero times; 20 hidden edits captured zero times until activation, then exactly once. Request cost 0.0055ms; snapshot capture about 1.46–1.59s on the UI thread, pure comparison 173ms. Full-map layer/tree render means 2.52/3.56ms; group full-map mean 5.77ms, maximum 9.18ms; pan/zoom maxima 5.56/6.96ms. Normal and 800x600 screenshots visually inspected. Capture pause remains a documented limitation; steady rendering and unchanged switching remain responsive.
- Initial implementation verification (2026-10-10, superseded by the final runs above): Release `game-client` and `testrunner` build passed. Unfiltered native CTest passed with `RESULT failures=0 steps=2284` in 101.14 seconds (`run-4uqb4xgc`); report, process log and screenshots copied to `out/map-compare-smoke/tabs-release-native-evidence`. The multi-document strip screenshot was inspected. clang-format20.1.8 dry-run and diff checks passed. Test-only physical relative mouse motion is drained before editor update to prevent external cursor interference; SDL button/key delivery, hover, cursor-target and timing assertions remain intact.
- Configure native ON and build game-client/testrunner in Release `out/build/map-comparison-release` and Debug `out/build/map-comparison-native` via VS2022 amd64 developer shell. Full Release core suite; Debug comparison core; unfiltered `ctest --test-dir <cache> -R '^MapCompare.Native$' -V` sequentially. Keep artifacts in ignored `out/map-compare-smoke/tabs-*`.
- Cover all matrix rows, including all three histories, direct asset changes, unchanged save, stale completion, cancelled source close, multiple dependents, held switching and reopened state. Keep SDL timing/delivery assertions; update coordinates without removing existing behaviors.
- Springlobe local pair from prior sections spec: measure pending/ready switching and refresh counts, capture cost and full-map/tree/motion rendering; inspect normal and 800x600 screenshots. Previous full-map/tree means2.07/2.81ms, motion maxima5.25ms.
- clang-format20.1.8 and diff checks. Restore Release native OFF, rebuild normal DDNet.exe and check instrumentation absence. Parent independently reviews, verifies and commits locally.
