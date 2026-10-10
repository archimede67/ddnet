---
title: 'Compact comparison controls and replace section buttons with native icons'
type: 'refactor'
created: '2026-10-10'
status: 'done'
route: 'dispatch'
baseline_commit: '2351eb8a6664e10c1aa184d2910fd9e920a3da81'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparison tabs waste vertical space on disabled editor controls, large section buttons and repetitive headings.

**Approach:** Remove the three unused footer actions; replace the disabled mode icons and large section grid with four active section icons; compact the toolbar/headings and remove visible read-only mentions. Retain the familiar editor shell, native button styling and shared sidebar width.

## Boundaries & Constraints

**Always:** All four sections remain discoverable through named tooltips and selected styling; all comparison actions stay reachable at native sidebar limits; source identity/unsaved state and path remain inspectable; native map controls remain unchanged; preserve comparison navigation, independent inspection state, modal/transition ownership and rendering performance.

**Never:** Duplicate sidebar state/layout, mutate sources through comparison controls, weaken source-read-only guards, add a merge feature, or alter unrelated ddnet-libs changes.

## I/O & Edge-Case Matrix

| Scenario | Input/state | Expected behavior | Handling |
|---|---|---|---|
| Compact shell | Map versus comparison, widths100/400, windows800x600/1280x800 | Comparison has one22-high action row, four icons, one-line source headings and no unused footer buttons | Normal editor keeps its existing layout/actions; minimum rendered aspect5:4 remains usable |
| Sections | Click every icon, P/N, empty/equal resources | Correct section and selected icon; named tooltip; section preferences retained | Selector/loading/unavailable controls cannot inspect stale entries |
| Header/footer | Long names/paths, saved/unsaved maps, no differences | Clipped headings identify Before/After; hover reveals path/state; footer shows summary or hovered help | No overlap or read-only badge; hover must not claim map gestures |
| Input ownership | Modal/popup or held tab/divider transition | Moved controls respect same ownership guards | No source edits or mode/history changes; shared resizing remains intact |

</frozen-after-approval>

## Code Map

- `src/game/editor/editor.cpp`: `Render` owns shell geometry and stable divider. Use comparison toolbar height51 instead of78 (tabs20+gap5+row22+margins4), keeping normal layout untouched. Shared Modebar still contains mention/moved notices above the22-high icon row. `RenderModebar` delegates only the comparison icon row to its selected document; normal icons/arrow modes remain guarded. `RenderStatusbar` returns the full supplied tooltip rectangle for comparisons without drawing the three actions. `RenderMenubar` keeps the clock but removes comparison read-only text. Keep foreground ordering and UI disabling around transitions/modals.
- `map_compare.cpp/.h`: expose a small section-bar render method called from existing editor Modebar, reuse `DoButton_FontIcon`, joined corners, existing IDs12/13/15/16 and `SetSection`. Gate activation on READY/current result and enabled/non-modal/non-transition UI. Remove the section grid from `Render`; navigator uses prepared sidebar directly (keep narrow visibility footer). Draw all7 actions in the22-high toolbar row with native5 gaps; widths40/40/62/85/30/30/25 for Back/Swap/By layer-group/Context/Prev/Next/Fit total342, fitting the minimum346 available units. Preserve shortcuts/tooltips. Remove READ ONLY and selector tooltip wording.
- Move the current24-high summary into default status help without overriding hovered controls. Replace name20+path26 with one20-high Before/After title row (name plus unsaved marker), providing existing SourceName/path details on hover via native tooltip handling and stable IDs. Pane top becomes91 instead of168; main sidebar begins73 instead of100. Preserve settings/envelope header offsets relative to their prepared panes; no rendering/diff/cache changes.
- `src/engine/font_icons.h`: use LIST_UL, LAYER_GROUP, CHART_LINE, TERMINAL for Changes/Layers/Envelopes/Server settings. Add sorted CHART_LINE constant for U+F201; fontTools verified that codepoint maps to chart-line in bundled Font_Awesome_6_Free-Solid-900.otf. No new font asset.
- `src/test/native/map_compare.cpp`: adapt SectionPosition/NavigatorTop/ToolbarPosition centrally. ToolbarGeometrySteps and ChromePolishSteps currently assert three disabled modes/footer labels, y168 panes and two toolbar rows; replace those expectations with the requested compact comparison shell while retaining normal-map assertions. Check all four actual icon clicks/tooltips/selection, glyph presence, absent footer actions and source-mode/history safety. Adapt chrome pixel bounds and any old absolute coordinates. Existing focused shared-shell subset covers section/width lifecycle, modals, settings cache and source safety; keep the full corpus registered.
- `docs/map-comparison.md`: describe section icons, hover details, footer summary and compact row instead of large buttons/disabled footer; preserve interaction documentation.

## Tasks & Acceptance

**Execution:**
- [x] Editor/comparison/font files — compact presentation using existing shell and controls.
- [x] Native harness — meaningful geometry/glyph/interaction assertions and all affected coordinates.
- [x] Documentation — explain icons and compact headings/footer.

**Acceptance Criteria:**
- Given any comparison section, when inspecting the top/sidebar/footer, then controls match this compact layout and all sections/actions remain usable.
- Given a normal map, when returning from comparison, then its native layout, shared width and editor state remain intact.
- Given long headings or modal input, when hovering/clicking, then identity remains inspectable and source mutation stays blocked.

## Implementation Notes

User explicitly authorized this UI follow-up and allowed icon/design choices. No intent gaps or irreversible actions. Footprint is local editor presentation, one glyph constant, documentation and native harness; no rendering algorithm changes. This supersedes the previous task's comparison two-row/disabled-footer presentation. Parent owns final verification, review and local commit. Implementation child should implement directly from this spec, without recursively invoking a build skill; run only focused native tests before handoff.

## Spec Change Log

## Review Triage Log

All three fresh reviewers completed before triage; the edge-case layer reported no findings. Individual verdicts precede grouping:

| Finding | Verdict | Evidence and route |
|---|---|---|
| Blind1: trailing unsaved marker can be clipped | low | Label ellipsis clips the suffix on long names; full hover state is available but the immediate marker disappears. Patch: move the marker into the short prefix. |
| Blind2: tooltip IDs retain destroyed document strings | low | Dispose erases the document without clearing CTooltips; EndFrame clears hot input, preventing the claimed stale text from rendering, but cached dangling entries remain and accumulate with distinct allocation addresses. Patch: clear the existing native tooltip cache on comparison disposal. |
| Blind3:75-frame hover may precede tooltip delay | false | The native runner explicitly sets cl_refresh_rate60, enforced in client.cpp;75frames exceed the1second fade-in. No faster configured run is demonstrated. |
| Blind4: cached text does not prove rendered tooltip | medium | The checks would survive removal of OnRenderTooltip; screenshots were visually inspected but are not automated rendering assertions. Patch: assert visible wrapped text in the framebuffer. |
| Blind5: no exact label readability oracle at minimum width | low | Hover/bounds do not prove label text, but parent inspected the750x600/sidebar400 screenshot and all labels are readable with native rendering. Reject: a hypothetical font/text regression would require additional per-label rendering oracles, disproportionate to this cosmetic coverage gap. |
| Blind6: not every unavailable state is clicked through | low | Initial selector is directly covered, modal/transition ownership is exercised elsewhere, and RenderSectionbar requires READY/current/enabled/no-modal/no-popup/no-transition. Additional per-state synthetic icon clicks could improve coverage but no failing state was identified. Reject rare hypothetical gap requiring extra state fixtures. |
| Blind7: heading gesture test lacks middle/wheel variants | low | Left-click/no-pan is tested; pan and wheel share the prepared pane containment bounds and existing canvas tests exercise these gestures. No boundary bug demonstrated. Reject rare hypothetical gap requiring extra gesture scenarios. |
| Gaps1: rendered long-source tooltip unverified | medium | Accepted as filed: cache/status/screenshot-save checks survive removing OnRenderTooltip. Patch: native framebuffer oracle proving actual wrapped identity beyond the256-byte status limit. |

Blind4/Gaps1 share the missing rendered-tooltip oracle; grouped for one patch. Blind1 and Blind2 remain separate trivial private corrections. No intent gaps, spec loopback or deferred entries.

Patch outcome: unsaved markers precede ellipsized names, comparison disposal clears native tooltip cache, and an independent framebuffer oracle checks the full wrapped identity and literal path tail beyond256 bytes. Focused Debug polish passed1,493 steps with zero failures (`run-357u0xhx`); both source oracles reported zero missing glyph pixels. Parent re-read both changed files and inspected the After tooltip screenshot.

## Verification

Step-03 audit: parent read the staged baseline diff and inspected normal/comparison screenshots at both sidebar limits, minimum aspect, selector and long-path tooltip. Final focused Debug native run `out/build/map-comparison-native/map-compare-native/run-ge437fhc` passed 2,355 steps (zero failures, 96.04s). Compact-shell row is covered by ToolbarGeometrySteps/ChromePolishSteps; sections by real icon clicks, selected glyphs and SectionAndSkySteps/SectionReviewRegressionSteps; header/footer by long saved/unsaved identity, missing path, stable hover/no-pan and status rectangle assertions; input ownership by native menus/modals, source invariants, held tab/divider transitions and shared-width lifecycle. These covering checks all ran and passed. Debug MapDiff/MapCompareRender/Editor also passed all25 tests. Earlier runs exposed corrected fixture initialization and a transient native input press/active-target failure; assertions were preserved. Full parent Release/performance verification follows review.

- Build native Debug/Release clients and test runners using VS2022 amd64, VSLANG=1033, MAP_COMPARE_NATIVE_TEST=ON. Child runs focused DDNET_MAP_COMPARE_SHARED_SHELL=1. Parent audits matrix and screenshots, reviews, then runs final full Release core, focused Debug MapDiff/MapCompareRender/Editor and unfiltered Release MapCompare.Native CTest; no filter flags left active for full suite.
- Inspect screenshots at both sizes/width extremes plus long-heading tooltip, active section icons and selector. Repeat isolated downloaded Springlobe4 benchmark with unchanged switching/no-capture assertions.
- clang-format20.1.8 and diff checks. Restore Release native test flag OFF, rebuild normal DDNet.exe and scan ASCII/UTF-16 for instrumentation markers. Commit only this task's files locally; no push.

Final verification after review patches:
- Debug targeted core25/25; Release core403/403 enabled tests (three pre-existing disabled).
- Unfiltered Release MapCompare.Native CTest passed4,196 steps, zero failures,179.36s. Evidence: `out/build/map-comparison-release/map-compare-native/run-ikt7yvwh`; log `out/map-compare-smoke/compact-final-release-native.log`.
- Isolated downloaded Springlobe4 pair passed100 steps;2800x1750,1,293,219 occupied tiles. Full-map/tree/group steady averages3.10/3.95/3.47ms; pan/zoom averages4.84/4.91ms, maxima7.24/7.66ms. Unchanged switch0.0044ms with zero captures; hidden edits cause no captures and pending activation captures once. Capture of both sources remains a separate1.42-1.56second operation. Evidence: `out/map-compare-smoke/compact-springlobe/run-iia5_b0a`.
- clang-format20.1.8 and `git diff HEAD --check` passed.
- Release restored to MAP_COMPARE_NATIVE_TEST=OFF and normal DDNet.exe rebuilt. All six instrumentation markers absent in both ASCII and UTF-16LE. Existing ddnet-libs changes excluded; local commit only.
