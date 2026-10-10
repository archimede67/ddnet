---
title: 'Integrate comparison views with the editor shell and precise text differences'
type: 'feature'
created: '2026-10-10'
status: 'done'
route: 'dispatch'
baseline_commit: '54d6d8267fe330eb2fcea9b0dac89aeb37d1166e'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Normal map rendering paints over shared tabs; comparison tabs discard familiar editor chrome, enforce an unnecessarily wide sidebar, and color entire changed server commands.

**Approach:** Finish the comparison-tab experience with correctly layered shared editor chrome, native-width sidebar resizing, and cached inline text differences. Preserve independent read-only comparison documents and their existing features.

## Boundaries & Constraints

**Always:** Visible familiar menus with unavailable commands greyed and nonfunctional; safe document transitions; unchanged source contents/history/visibility during inspection; bounded diff work and visible-row rendering; existing refresh/close/reopen semantics. Leave user-owned ddnet-libs untouched.

**Never:** Merge/edit through comparison, hide unavailable menu items, run normal editing toolbar callbacks in a comparison, repaint a widget twice per frame to fix ordering, or perform text alignment per frame.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected behavior | Failure handling |
|---|---|---|---|
| Foreground chrome | Dense entities/quads, clipped/unclipped groups, pan/zoom, map/comparison switches | Maps cannot obscure tabs/menus; each control processes input once | Preserve GUI-hidden full viewport |
| Shared shell | Ready/pending/selector comparisons; File/Tools/Settings menus and shortcuts | Familiar menu/toolbar/status areas remain; available commands work; unavailable edits stay grey and inert | Dialogs remain reachable; close targets comparison, never background map |
| Inline settings | 1 to 0 argument, separated changes, Unicode, equal/inserted/deleted/empty commands | Only changed substrings red/green; neutral equal text; exact text/wrapping and existing line alignment | Codepoint-safe byte ranges; empty/absent distinguishable |
| Work bounds | Many settings, long strings, cancellation, resize/divider drag/revisit/refresh | Compute immutable ranges once per result with bounded aggregate work; render visible rows only | Conservative changed middle on budget exhaustion; cancel promptly |
| Narrow sidebar | Drag to native 100-unit minimum, 800x600, all sections and fold/eye controls | All sections usable without text overlap; width retained across tabs/refresh | Keep canvases usable and existing scroll/input ownership |

</frozen-after-approval>

## Code Map

- `src/game/editor/editor.cpp/.h`: Render draws DoMapTabs before MapView::Render; later toolbar background deliberately omits top24, exposing world render. Restore foreground ordering without double input. Group rendering owns low-level clip enable/disable, so an outer clip alone is insufficient. Common shell reserves menu20, toolbar78, status16. Reuse rectangles/backgrounds, RenderMenubar and appropriate status/mode presentation; comparison controls replace editing tools. Current comparison early return skips dialogs/tooltips/events. Avoid source-dependent coordinate display in comparison.
- `popups.cpp`, `quick_actions.h`, `quick_action.h`, `editor_ui.cpp`: menu rows already support Checked=-1. Call does not enforce Disabled; toolbar polls raw Input, so visual grey alone is unsafe. Keep New/Open/Help/Exit, Compare/Reopen and comparison Close usable with shared dialog/input lifecycle. Disable map-only Save/As/Copy/Append/Details/Test, editing tools and source-specific settings. Guard execution and deferred confirmations. Global preferences may remain available. Normal map actions must retain behavior.
- `map_compare.cpp/.h`: Render controls/layout/sidebar; min160 vs DoEditorDragBar native100. Share a named minimum; retain adaptive maximum and stable divider. At narrow widths adapt section buttons (compact or stacked), unchanged header, visibility/footer and clipped labels. Preserve per-tab width, focus, sections and scroll.
- `map_diff.cpp/.h`: AlignSettings already aligns exact anchors and positional unmatched rows. Add per-side changed byte ranges to CSettingRow during comparison; preserve line pairing. Decode UTF-8 units with byte boundaries; trim common prefix/suffix and bounded LCS middle, coalesce unmatched spans. Bound per-line and whole-settings work; conservative middle fallback. Reuse CCheck cancellation.
- `map_compare.cpp::RenderSettings`: current full-line coloring and cached widths/row offsets. Draw full text once with neutral base and CTextCursor color splits, preserving wrapping/kerning. STextColorSplit indices are UTF-8 byte offsets (text.cpp uses consumed byte counts); TextEx mutates splits, so copy immutable result ranges into a fresh cursor. Keep absent markers grey and changed gutters meaningful.
- `src/test/map_diff_test.cpp`, `src/test/native/map_compare.cpp`, `docs/map-comparison.md`: independent inline-range oracles, native glyph/color and chrome pixel checks. Existing settings oracle expects full-line color; change its expectation, preserving text/scroll assertions. Many native coordinates use y62/87/111 and min160; adapt to final layout without dropping corpus.

## Tasks & Acceptance

**Execution:**
- [x] Editor/menu/action files — shared safe shell, foreground rendering and command policy.
- [x] map_diff files and core tests — bounded cached inline ranges and edge-case/cancellation tests.
- [x] map_compare files — split-color text and usable native-minimum sidebar.
- [x] Native harness/docs — matrix regressions, screenshots, source invariants and performance evidence.

**Acceptance Criteria:**
- Given any map/comparison document, when switching and using its chrome, then tabs remain unobscured and available actions target the correct document.
- Given sv_pauseable 1 versus sv_pauseable 0, when viewing settings, then only 1/0 are red/green and sv_pauseable remains neutral, including wrapped text.
- Given Springlobe-sized maps, when viewing full context or switching unchanged comparisons, then bounded rendering and zero recapture on unchanged switching remain intact.

## Implementation Notes

Continuation explicitly requests all four related comparison refinements; no unresolved intent or irreversible operations. Existing branch fits; ddnet-libs dirt is pre-existing. Parent retains full verification, review, normal-client rebuild and local commit.

## Spec Change Log

## Review Triage Log

All three same-capability reviewers completed before triage; edge-case review returned no findings. Each finding assessed individually before grouping:

| Finding | Verdict / route | Evidence |
|---|---|---|
| B1 modal marker/focus clicks | medium / patch | RenderEnvelope tests raw MouseButtonClicked without a modal guard; file browser/prompt only overwrite HotItem later. RenderSettings similarly changes keyboard focus. Existing dialog/popup state directly establishes the missing guard. |
| B2 modal scroll ownership | medium / patch | CScrollRegion::End consumes wheel input based on HotScrollRegion, independent of list Active; UpdateHotScrollRegion checks Ui Enabled, which comparison leaves true during modal rendering. Suspend comparison scroll interaction while preserving dialog input. |
| B3 malformed UTF-8 equality | medium / patch | str_utf8_decode consumes E1+continuation before rejecting x/y; invalid identity uses only E1. Loader copies command bytes without UTF-8 validation. Reset invalid decode progress to one byte and regression-test the cited pair. |
| B4 inline fallback notice | low / reject | Fallback is deliberately conservative and documented. Missing an additional UI notice is cosmetic; common argument edits trim to tiny spans, while reaching this fallback requires unusually large/dense changed text. A separate result flag and presentation branch add complexity disproportionate to this rare case. |
| B5 menubar exit policy | low / patch | RenderMenubar directly calls OnClose while Exit uses HasUnsavedData. This retains maps, so it does not itself lose edits, but exposes inconsistent exit behavior in the newly shared comparison shell. Use the existing Exit action. |
| B6 close/exit wording | low / patch | Prompt reads static Close map label/description even though callback now closes comparison. Generic Close tab wording accurately describes both; remove misleading Escape claim from shared Exit description. |
| B7 action prompt regression | medium / patch | Tests invoke callbacks and P navigation but never Ctrl+P, filtering or prompt activation. Native prompt workflow is newly exposed. |
| B8 file dialog routed input | medium / patch | Successful test calls CallbackOpenMap directly; it proves lifecycle but not text/filter/navigation/Enter ownership. Drive Load through the native dialog controls. |
| B9 framebuffer read failure diagnostics | low / reject | New oracle reads can dereference a failed second framebuffer capture. The runner already fails crashes; this only loses diagnostics on exceptional graphics/readback failure. Additional guards are not justified for this rare test-only condition. |
| B10 tab-row pixel coverage | medium / patch | Chrome oracle samples y21 while actual tabs occupy y22-42; screenshot inspection passed but assertions would miss a row-specific overpaint regression. Add actual tab/control row samples at both resolutions and comparison mode. |
| G1 action prompt verification | medium / patch | Accepted pre-verified search evidence: no CPrompt workflow in tests; deleting comparison m_Prompt.OnInput is undetected. Cover Ctrl+P/filter/allowed activation/Escape and disabled actions. |
| G2 Settings guards verification | medium / patch | Accepted pre-verified evidence: Settings menu is only opened/screenshot, underlying editor-wide brush/unused-tile/envelope flags are not asserted. Click disabled controls and verify unchanged values. |

Groups after individual verdicts: B1+B2 modal ownership; B7+G1 prompt coverage; B3 byte identity, B5 exit, B6 wording, B8 dialog input, B10 chrome pixels, G2 Settings coverage remain distinct trivial patches. No intent or spec changes needed; no deferred findings.

All accepted patches implemented by the original implementation agent. Comparison rendering temporarily disables underlying UI interaction during dialogs/popups; existing button/hotkey/scroll internals respect that scope, and raw marker/focus probes explicitly reject modals. Invalid UTF-8 decoding now advances one byte on errors. Shared X uses the existing Exit confirmation, and Close tab wording covers both document kinds. Native tests now drive prompt filtering, actual Load text/list/Enter, disabled Settings clicks, unsaved Exit cancellation and actual tab/control pixel rows at both sizes. Modal tests cover three sections and three modal types with wheel, keyboard, click and uncovered-marker positive controls. Focused post-patch run-rs9o2v8q passed658 steps; inline core2/2. Parent inspected both comparison chrome screenshots and the complete incremental patch.

## Verification

- Release/Debug caches: out/build/map-comparison-release and out/build/map-comparison-native. VS2022 amd64 developer shell, VSLANG=1033. Configure MAP_COMPARE_NATIVE_TEST=ON; build game-client/testrunner. Full Release core; focused Debug MapDiff/MapCompareRender/Editor core; unfiltered native CTest both configurations. Preserve evidence under ignored out/map-compare-smoke/polish-*.
- Matrix tests must execute; inspect 1280x800 and 800x600 screenshots, native pixel/text/menu/disabled-action assertions, source invariants and complete old corpus. Parent repeats downloaded Springlobe pair via DDNET_MAP_COMPARE_PERF_BEFORE/AFTER, checking unchanged captures and full-context/tree/motion timings. Prior layer/tree means about3/3.6ms; capture about1.5s is documented.
- clang-format20.1.8, git diff checks. Restore Release MAP_COMPARE_NATIVE_TEST=OFF, build normal DDNet.exe, verify native instrumentation absent. No push.

Parent verification before review: Release core403 passed (three pre-existing disabled); Debug focused25 passed. Full Release native2716 steps passed in124.52s: out/build/map-comparison-release/map-compare-native/run-2s4s6emi. Matrix coverage: ChromePolishSteps foreground/shared-shell/Load/disabled-action/source assertions; SettingsInlineByteRanges and native independent glyph oracles for argument/Unicode/empty lines; SettingsInlineAggregateBudgetAndLongLineCancellation and native resize/cache/scroll cases; SectionAndSkySteps all4 sections, fold/eye/Show all and source/camera invariants at100. Inspected1280/800 chrome, menus, wrapped text and minimum-width tree screenshots.

Springlobe4 parent run out/map-compare-smoke/polish-springlobe/run-hgx978j7 passed99 steps:2800x1750,1,293,219 occupied tiles; full-map/layer-tree/group means3.19/3.68/3.01ms; motion maxima7.34/8.04ms; unchanged switch0.039ms with zero captures;20 hidden edits zero captures, activation one capture. Existing main-thread snapshot cost1.39-1.55s remains documented.
Full Debug native run-ude1nil2 passed2716 steps in216.53s before review patches.

Final parent verification after all review patches: Release core403 passed (three pre-existing disabled); focused Debug25 passed. Full unfiltered native suites both passed3237 steps with zero failures: Release run-w6bvfvlt in156.11s, Debug run-u7yj1eu5 in280.82s. Inspected final800-wide wrapped inline text and100-unit layer tree screenshots, plus1280/800 foreground comparison chrome. clang-format20.1.8 and git diff checks passed. Final unified review diff rewritten to include all patched production and test files.

Final quiet Springlobe4 run out/map-compare-smoke/polish-springlobe-final/run-s7szgcrh passed99 steps. Full-map/layer-tree/group render means5.36/5.88/4.45ms; pan/zoom maxima8.51/8.36ms. Unchanged switch0.0055ms with zero captures; hidden repeated edits zero captures and activation one. Initial/refresh capture1.70-1.86s and worker193ms were measured separately; the existing main-thread snapshot pause remains a documented limitation. No review findings deferred.

Normal Release restored with MAP_COMPARE_NATIVE_TEST=OFF and game-client build passed. Verified out/build/map-comparison-release/DDNet.exe contains none of the native harness markers (including DDNET_MAP_COMPARE_POLISH). The pre-existing ddnet-libs change remains untouched. Local commit only; no push.
