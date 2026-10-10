---
title: 'Use one editor shell and sidebar width for map and comparison tabs'
type: 'refactor'
created: '2026-10-10'
status: 'done'
route: 'dispatch'
baseline_commit: '1b04ce5bcab80f1fb3a6a68bfa96132fcc7bc903'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparison tabs still use different toolbar spacing, button sizes and sidebar boundaries. Their private widths make switching documents visibly rearrange the editor.

**Approach:** Reuse the normal editor shell and controls, replacing its central content and sidebar contents for comparisons. Store one sidebar width for every map/comparison tab, including reopened comparisons.

## Boundaries & Constraints

**Always:** Identical shell boundaries, native button heights and gaps; a single width and divider owner; existing read-only, modal, transition and foreground-rendering guarantees; visible, accessible comparison sections. Preserve cached settings wrapping while dragging and independent comparison inspection state except width.

**Never:** Duplicate shell/divider layout, retain per-comparison width copies or restore historical widths, mutate source maps while inspecting, run editing callbacks in comparisons, or add rendering/diff work per unchanged frame. Preserve unrelated ddnet-libs changes.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected behavior | Handling |
|---|---|---|---|
| Shell continuity | Switch normal maps, comparisons, selector/loading states | Same tab/toolbar/sidebar/status boundaries; native22-high toolbar rows with5-unit gap | Foreground chrome processed once; GUI-hidden map viewport preserved |
| Shared width | Resize map or either of two comparisons; revisit, refresh, close/reopen | Every document uses latest editor width | Reopening never restores an old width |
| Bounds | Width100/400,800x600 and1280x800 | Same limits regardless of selected tab; comparison actions and sections remain reachable | Consistent shared clamp; no hidden overflow buttons |
| Input ownership | Held divider, tab switch/close, extra editor, modal | Release barrier prevents gesture transfer; dialogs own input; other dividers work | No source edits or accidental zoom |
| Cached resources | Drag divider in long settings, switch/refresh | Wrapping reflows once after release; inspection preferences remain independent | No snapshot capture for unchanged switching |

</frozen-after-approval>

## Code Map

- `src/game/editor/editor.cpp/.h`: Render already owns menu20/toolbar78/status16, tabs20 plus5 gap,2-unit margins, m_ToolBoxWidth. Remove comparison-only CompareToolbar and sidebar exceptions. Derive/sidebar/background/divider once for both views, with stable editor-owned divider identity. Keep native100–400 width limits shared and clamp before layout. Pass prepared toolbar/sidebar/content rectangles to comparison rendering. Extract the native two-row toolbar split (currently HSplitMid gap5 in DoToolbarLayers) for both consumers; avoid a parallel shell abstraction.
- `editor.cpp::RenderModebar/RenderStatusbar`: reuse normal rendering in comparison, showing native editing controls disabled. Guard mode-button and Left/Right callbacks; quick actions already enforce Disabled. Preserve common global notices and comparison read-only indication without allocating a different shell. Comparison section names stay accessible in its sidebar.
- `map_compare.cpp/.h`: Render owns an extra6-unit margin, independent toolbar lambda, width225 and divider; remove those shell responsibilities. Place comparison actions in the native two rows with5-unit gaps and compact labels (shortcut hints stay in tooltips), using existing DoButton_Editor. Keep all actions within the toolbar at width400/window800. Replace RenderSettings active-divider cache check with shared divider identity. Remove width from CMapCompare and CClosed plus Dispose/Reopen persistence; retain camera/selection/visibility/history.
- `src/test/native/map_compare.cpp`: width/divider accessors and OpenComparison currently copy private widths. SectionPosition/NavigatorTop and toolbar click coordinates encode old layout; update helpers/call sites to final shared geometry while retaining behavioral/pixel assertions. SectionAndSkySteps currently asserts independent normal width and comparison-only resize clamp; replace with shared-state/release-barrier assertions. SectionReviewSteps guards wrapping cache; TabLifecycleSteps covers close/reopen and transitions. ChromePolishSteps supplies dense clipped/unclipped geometry and both window sizes.
- `docs/map-comparison.md`: replace independent-width description with shared width and matching shell/toolbar behavior.

## Tasks & Acceptance

**Execution:**
- [x] Editor/compare files — common shell, native toolbar rows and single sidebar state.
- [x] Native harness — independent geometry/pixel oracles and shared-width lifecycle scenarios; adapt existing affected controls without dropping corpus.
- [x] Documentation — explain shared width and retained comparison preferences.

**Acceptance Criteria:**
- Given a resized sidebar and two comparisons, when switching, refreshing or reopening any tab, then shell boundaries and latest width match the normal editor.
- Given normal and comparison screenshots at both tested sizes, when comparing the toolbar, then row origins/heights/gaps match native geometry and controls fit.
- Given active comparison inspection or modal interaction, when operating the shared shell, then sources remain unchanged and existing navigation/caching behavior holds.

## Implementation Notes

User explicitly requests implementation of this follow-up; no intent gaps or irreversible actions. Existing feature branch is appropriate. Parent owns final verification and local commit; existing ddnet-libs dirt is excluded.

## Spec Change Log

## Review Triage Log

Round1: three context-free, same-capability reviewers launched together; all results collected before triage. Edge-case reviewer returned no findings.

| Finding | Verdict / route | Evidence and decision |
|---|---|---|
| Blind1: square-window toolbar overflow | false / reject | `CGraphics_Threaded::AdjustViewport` clamps rendered aspect to at least5:4; `CUi::Screen` therefore has width>=750, not600. Context ends at669 with width400 and remains accessible. |
| Blind2: narrow status tooltip truncation | low / reject | Native status actions leave66 units at800x600/width400; `RenderTooltip` intentionally ellipsizes within that rectangle. This is the requested normal shell's existing tradeoff at an extreme width. A separate tooltip presentation adds behavior/complexity for an uncommon boundary rather than a direct correction. |
| Blind3: remaining image/sound row splits duplicate helper | low / patch | `DoToolbarImages` and `DoToolbarSounds` still embed the same5-unit split; a future row-spacing change could diverge in those callers. Replace both with the shared helper. |
| Blind4: refresh width assertion uses fixture default | medium / patch | The225 assertion would miss an accidental reset to the harness default. Use a distinct width before refresh and assert it survives. |
| Blind5: focused runner omits selector interaction | false / reject | The focused subset is not final coverage: unfiltered Release and Debug runs execute `ReviewSteps`, asserting native source-row selection and starting the chosen ordered pair. ChromePolish also checks selector shell/menu policy; common geometry/divider coverage applies before its early return. No missing final selector execution. |
| Blind6: held map-to-comparison divider direction untested | low / patch | Existing held-divider cases start in comparisons; map entry additionally calls `FinishMapInteraction`. Extend the existing lifecycle fixture to hold the map divider through selection and prove the release barrier freezes width. |
| Blind7: popup divider dismissal untested | low / patch | ModalOwnership checks popup wheel/click isolation but probes the divider only for file/prompt dialogs. Add a popup dismissal press/continued-movement probe to protect the newly shared divider. |
| Blind8: non-default editor state untested | low / patch | ChromePolish starts with EXTRAEDITOR_NONE, so its callback assertions could miss clearing a previously open editor. Exercise non-default mode/extra-editor state and verify each disabled action and return to editing preserve it. |
| Blind9: settings cache after another tab resizes untested | medium / patch | Active resize exercises width invalidation, but the newly shared width also changes while a cached comparison is hidden. Extend the existing long-settings fixture to resize another tab, return and check remeasurement, retained inspection state and distant-row navigation. |
| Gaps1: Left/Right mutations can cancel in test | medium / patch | Filed broken-verification evidence: ChromePolish checks mode only after both opposite keys. Assert immediately after each press. |
| Gaps2: footer controls could disappear unnoticed | medium / patch | Filed broken-verification evidence: clicks at expected positions only prove no editing action, not presence. Add framebuffer label-presence/disabled-style assertions at native status positions. |

Grouping: Blind8 and Gaps1 share the disabled-control state oracle; others remain distinct. All surviving corrections are small local helper/fixture changes with no public surface or intent change. No deferred findings.

Corrections completed by the original implementation agent. Parent read the complete patch: image/sound row split reuse; distinct310 refresh width; held map-divider transition in addition to the original native tab click; popup dismissal/movement plus retained content probes; per-control/arrow checks of sound/history state and map return; long-settings cross-tab reflow/scroll/selection and distant N with no capture; all three footer labels sampled for presence and grey styling. Final focused run `out/build/map-comparison-native/shared-shell-review-focused/run-thmoblzh`:2112 steps,0 failures. No production behavior change beyond equivalent image/sound helper calls was needed after review.

## Verification

- VS2022 amd64 shell, VSLANG=1033. Configure Release out/build/map-comparison-release and Debug out/build/map-comparison-native with MAP_COMPARE_NATIVE_TEST=ON; build game-client/testrunner. Run full Release core, focused Debug MapDiff/MapCompareRender/Editor, and unfiltered MapCompare.Native CTest in both builds. Child runs only focused affected native cases; parent full suites.
- Add meaningful native width/geometry assertions, actual input through map→comparisonA→comparisonB→map→reopened comparison, bounds/windows, held divider/modal/extra-editor coverage and settings cache. Inspect paired native screenshots. Repeat downloaded Springlobe4 performance fixture; unchanged switches must capture zero times.
- clang-format20.1.8 and git diff checks. Restore Release MAP_COMPARE_NATIVE_TEST=OFF, rebuild DDNet.exe and verify native test markers absent. No push.

### Executed evidence

- Release and Debug native client/test-runner builds pass. Full Release core:403 passed (3 pre-existing disabled). Focused Debug MapDiff/MapCompareRender/Editor:25 passed.
- Focused native run `out/build/map-comparison-native/shared-shell-focused/run-hafskyj4`:2006 steps,0 failures. Matrix audit: ToolbarGeometry/ChromePolish cover identical bounds,22-high controls/five-unit gaps, disabled shell and selector; TabLifecycle covers map/A/B width sharing, refresh, close/reopen and independent cameras without recapture; SectionAndSky covers bounds, release barrier and extra-editor dividers; ModalOwnership covers blocked resize/input; SectionReview covers cached wrapping throughout drag and one reflow on release.
- Parent inspected all8 paired native map/comparison screenshots at800x600 and1280x800, widths100/400. Matching shell boundaries and button rows; comparison actions and all sections remain accessible at both extremes.
- clang-format20.1.8 dry-run and `git diff HEAD --check` pass.
- Pre-review full native CTest passes in both configurations: Debug `out/build/map-comparison-native/map-compare-native/run-xftzmgv0` (254.49s), Release `out/build/map-comparison-release/map-compare-native/run-ydhbjb_g` (163.34s), each3846 steps and0 failures.
- After review corrections: Release client/test-runner rebuild passes; Debug native rebuild passes. Repeated full Release core:403 passed (3 pre-existing disabled); focused Debug:25 passed. Reports are `out/build/map-comparison-release/shared-shell-final-core.log` and `out/build/map-comparison-native/shared-shell-final-core.log`.
- Final unfiltered Debug native CTest:3952 steps,0 failures,272.12s, `out/build/map-comparison-native/map-compare-native/run-d7frudp6`.
- Final unfiltered Release native CTest:3952 steps,0 failures,166.56s, `out/build/map-comparison-release/map-compare-native/run-e82st29d`.
- Isolated downloaded Springlobe4 fixture:100 steps,0 failures, `out/map-compare-smoke/shared-shell-springlobe/run-rg64qeaj`. Largest layer2800x1750 with1,293,219 occupied tiles. Full-map/full-map-with-tree/group steady render means3.44/3.44/2.79ms; pan/zoom means4.17/4.99ms, maxima5.44/15.12ms. Unchanged switch request0.005ms and0 captures;20 hidden edits trigger0 captures, activating the pending tab triggers1. Initial/refresh pair capture remains a separate existing cost (~1.44–1.46s), not steady render work. Source downloaded maps were copied into isolated test storage.
- Final clang-format20.1.8 dry-run and diff checks pass. Restored Release `MAP_COMPARE_NATIVE_TEST=OFF` and rebuilt `out/build/map-comparison-release/DDNet.exe`; ASCII/UTF-16 scans confirm native test markers absent. Review corrections and all acceptance/matrix checks are complete. Local commit only; unrelated ddnet-libs changes remain excluded.
