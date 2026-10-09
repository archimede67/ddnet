---
title: 'Clear comparison sections and faithful map context'
type: 'feature'
created: '2026-10-09'
status: 'done'
route: 'dispatch'
baseline_commit: '8fc1003d003f264b92536ece8873dc8b6dd7ac76'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Comparison navigation needs resizable, color-coded layer controls and discoverable resource sections. Full-map context loses distant background quads, and unnamed matches receive excessive ambiguity warnings.

**Approach:** Provide four sidebar sections, native divider resizing, colored tree names, faithful background parallax and more accurate matching explanations. The user delegates design and implementation.

## Boundaries & Constraints

**Always:** Read-only sources and editor state; preserve visibility, folding/scroll stability, detail focusing, context choices and bounded geometry rendering. Clearly distinguish an index-based guess from proven identity. Leave user-owned `ddnet-libs` untouched.

**Never:** Merge/edit/save commands, remote operations, suppress genuine duplicate uncertainty, or draw an entire large map each frame.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| Resize | Drag divider; narrow/wide windows; switch sections | Shared sidebar width, native resize cursor, bounded usable canvases; selection preserved | Clamp width; no canvas pan or stuck drag |
| Tree status | Added/removed/changed/unchanged layers and groups | Names green/red/amber/neutral; group status includes children; familiar eyes/folding | Keep non-color status cues |
| Sections | Changes, Layers, Envelopes, Server settings; changed/equal/absent resources | Dedicated two-row section buttons; resources excluded from Changes; P/N scoped to section, skips unchanged | Equal resources accessible even with context off; explicit empty state |
| Background quads | Far-away tile focus, zero/low-parallax scenery; pan/zoom/context cycles | Map context follows current camera, textured quads visible at correct source projection | Preserve clipping, source envelopes, visibility and culling |
| Quad inspection | Selected zero-parallax layer or group | Fit and independent pan still reach its objects | No division by zero or source camera mutation |
| Matching | Unique unnamed content, insertion/reorder, edited unnamed span, duplicates | Unique exact matches unambiguous; indices visible; positional fallback explicitly uncertain | No lost differences, false moves, or changed resource identity rules |
| Lifecycle | Swap, close/reopen, section transitions | Valid selections, fresh caches, correct section navigation and scroll | No stale source access |

</frozen-after-approval>

## Code Map

- `src/game/editor/map_compare.h/.cpp`: Render sidebar currently fixed225 with Changes/Layers; RebuildNavigator, Select, Navigate, RenderLayerTree, DetailValues. Add explicit section state/filtering; retain appropriate selections/scroll between sections. Two rows avoid cramped four-way tabs. Preserve settings full-height and envelope paired views.
- `src/game/editor/editor.cpp`: DoEditorDragBar (~3590), normal toolbox use (~3900). Reuse with stable divider storage and reset comparison cursor each frame. Native tree name styling can use GetButtonColor/DoButtonLogic plus colored label; DoButton_Ex's default label may override text color.
- `map_compare.cpp`: ProjectionCenter(~354) adds full inspection translation to every group. Springlobe sky/sun have zero parallax near origin; distant game Fit translates them offscreen. In map context use `(origin + center) * parallax + offset` for other groups; preserve independent projection for the selected owning group, including siblings. Keep drawing/culling/game-space clipping consistent; Fit uses selected inspection projection. ForceRenderQuads supports textures and envelopes at time zero.
- `src/game/editor/map_diff.h/.cpp`: Match(~258) wrongly marks every exact unnamed match ambiguous (~332). Remove that clause only. Keep duplicate buckets and equal-size compatible AlignSpan decisions. Add trailing positional provenance flag to pairs/entries, set only by AlignSpan; propagate group/layer/resource pairs. Do not equate matching indices with identity.
- `src/test/map_diff_test.cpp`, `src/test/native/map_compare.cpp`: pure matching tests; real SDL inputs, framebuffer/source-state assertions. Adapt existing coordinates/accessors and parallax expectations without dropping cases. Existing parallax test assumes obsolete fixed-background translation. Synthetic names limited to11chars; initialize map display names.

## Tasks & Acceptance

**Execution:**
- [x] Viewer: bounded native sidebar resize; four sections and filtering, scoped navigation, equal/empty resource handling, name colors and unnamed index labels/tooltips.
- [x] Diff core/viewer: unique exact confidence and truthful positional provenance; show both source indices in matching details.
- [x] Rendering: correct non-selected background projection; preserve selected inspection, envelopes and viewport caching.
- [x] Tests/docs: matrix regressions, actual colored-name and textured/parallax framebuffer checks; update `docs/map-comparison.md`.

**Acceptance Criteria:**
- Given changed/equal sources, when switching all sections, resizing and navigating, then each shows its own content without hidden resources or stale views.
- Given Springlobe-sized maps, when using full context and tree, then geometry remains culled and measured frame timings remain suitable for interactive inspection.
- Given finished changes, when testing source state and rebuilding normal Release, then sources remain unchanged and the executable excludes test instrumentation.

## Implementation Notes

Authorized continuation; no intent gaps or irreversible actions. Cohesive comparison refinement spans viewer, matching, tests and docs. Parent reviews and commits; implementation agent must not commit. Investigate any newly exposed rendering mismatch before changing policy further.

- Implementation clarification confirmed by parent: dedicated Envelopes and Server settings entries are excluded from Changes; image/sound resources and metadata remain in Changes.
- Sidebar uses the native divider with a stable UI identity, bounded by 400 UI units, 45% of available width and paired-canvas space. Native drag ownership resets after forced view changes while retaining ordinary multiple-divider operation.
- Sections retain independent navigator scroll, selected entry/detail and spatial camera. Settings text scroll and distant inspector detail reveal are preserved on return. Empty resource and layer sections have explicit notices.
- Matching provenance is presentation-only. Unique unnamed exact matches retain confidence; duplicate content remains uncertain; only compatible ordered-span fallback sets the new trailing positional flag.
- Full-map context uses the current camera for other groups; the selected owning group and its siblings retain independent inspection translation. Existing zero-parallax clipping coverage was adapted with near-origin synthetic geometry and independent game-space clipping expectations.

## Spec Change Log

## Review Triage Log

| ID | Verdict | Evidence and route |
|---|---|---|
| B1 same-entry section restoration | medium | SetSection returns before restoring saved detail, grouping and camera when the destination entry equals the current entry. Independent section state is lost after inspecting the same entry differently. Patch the private early-return condition; add a same-entry round trip. |
| B2 first settings-to-layer fit | medium | With an uninitialized Layers section, Select(-1) clears the old Settings kind, then RebuildNavigator selects a map entry against the old full-height pane. The layout correction flag is lost. Patch by retaining the transition across fallback selection and fit after layout. |
| B3 unnamed aggregate group label | low | The Changes/by-group override uses raw m_Name, bypassing EntryName's source indices. Direct formatter substitution is a patch. |
| B4 unnamed inspector child label | low | Group detail rows use raw child m_Name, making equal-coordinate edits in unnamed children indistinguishable. Direct formatter substitution is a patch. |
| B5 navigator name tooltips | low | Non-tree rows have no full-name/source-index tooltip after labels became ellipsized. Indices remain in structure details, but discovering clipped names needs a direct tooltip patch. |
| B6 section difference indicators | false | Dedicated, always-visible Envelopes and Server settings buttons, an explicitly section-scoped empty notice and global change total already expose that changes may be elsewhere. The finding establishes an optional design alternative, not a hidden or inaccessible difference or incorrect indicator. No extra counts are required to fix a demonstrated defect. |
| B7 wheel during divider ownership | low | OnInput tests pane bounds but not active UI ownership; dragging beyond the width clamp leaves the cursor over a pane and wheel input zooms it. Add the same active-item guard used for pan and a held-drag wheel assertion. |
| B8 settings layout during resize | medium | Every changed width discards offsets and lays out all commands synchronously, so a continuous sidebar drag repeatedly performs unbounded text layout. Reuse the existing cached width while the divider is held and rebuild once on release, keeping displayed wrapping consistent with cached heights; private cache guard is a patch. |
| B9 visible-quad vector allocation | maybe-false | Allocations are visible-only and measured final Springlobe rendering improved to 2.70 ms with tree at 3.46 ms. No dense-overlap slowdown was demonstrated. A repeatable allocation profile with many simultaneously visible quads would settle the suggested additional optimization; as filed, the residual allocation concern is negligible/unverified and rejected rather than adding a new buffer lifecycle. |
| B10 envelope navigator scroll coverage | low | Existing tests cover Changes/Layers and settings text scroll, while the new envelope navigator has only three items. Add an overflowing envelope section round trip to cover its distinct list state; patch. |
| G1 saved camera verification | medium | The filed mutation removes saved center/zoom restoration without failing the native round-trip assertions, which capture the returned camera as a new baseline. Add pre-departure center/zoom assertions; patch, shared with B1. |
| G2 matching explanation verification | low | Core provenance flags are checked, but native inspector assertions never check positional-versus-duplicate wording. Add actual-selection value assertions for both entry and object presentation; patch. |

All three independent review layers completed before triage. Edge-case review returned no findings. Each finding was classified separately above before grouping; accepted fixes retain the approved intent and add no public API.

## Verification

- MSVC: `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`; VSLANG=1033.
- Caches: `out/build/map-comparison-release` and Debug `out/build/map-comparison-native`. Configure MAP_COMPARE_NATIVE_TEST=ON; build game-client/testrunner, run core and `ctest --test-dir <cache> -R '^MapCompare.Native$' -V` sequentially. Log under ignored `out/map-compare-smoke/sections-*`; inspect screenshots including800x600.
- Benchmark downloaded Springlobe4 using DDNET_MAP_COMPARE_PERF_BEFORE/AFTER paths from previous spec's run-217tmn8p evidence. Previous full-context mean3.31ms/max4.10; tree mean5.09/max6.12; motion max8.66ms. Keep benchmark separate from builds. Record differences honestly.
- Restore Release MAP_COMPARE_NATIVE_TEST=OFF and rebuild. clang-format20.1.8, git diff --check. Parent runs full core suite and independent review.

### Implementation verification results

- Release matching/render core: 22 tests passed (`out/map-compare-smoke/sections-core.log`). Parent independently ran the full Release suite: 401 passed, three pre-existing disabled (`sections-core-all.log`).
- Final Release native matrix: 1,858 steps, zero failures in 93.05 s; evidence `out/build/map-comparison-release/map-compare-native/run-1hs70y3t`, summary `out/map-compare-smoke/sections-native-release.log`. Covers colored name glyphs, textured zero/low-parallax quads, source envelopes, clipping, selected siblings, native sidebar clamps, held closing input, ordinary multiple-divider behavior, resource section scoping/equality/absence, saved text scroll and distant inspector detail restoration, plus all earlier regression scenarios.
- Inspected final Release layer tree, distant textured sky, and 800x600 maximum-width screenshots. Native name styling, status cues, readable ellipsis and paired canvases are retained.
- Debug: the same 22 core tests passed; native matrix passed 1,858 steps with zero failures in 231.98 s. Evidence `out/build/map-comparison-native/map-compare-native/run-leo8rvi9`; logs `sections-debug-core.log` and `sections-native-debug.log`.

- Initial Springlobe checks passed 82 steps each (`run-g30119nq`, `run-tm3x4_wh`), but the latter had full-context 4.83 ms mean / 5.17 ms max, tree 5.81 / 6.63 ms, layer-motion max 15.79 ms and group-motion max 54.01 ms. This prompted profiling and optimization rather than accepting the spike as complete.
- Temporary per-pane profiling (`run-1ala14ar`, analysis `out/map-compare-smoke/sections-profile-analysis.log`) measured up to 1,190 separate visible-quad batches. Tile-cache rebuilding dominated CPU work (1.06 ms average / 6.43 ms max), while synchronized frame timing also included graphics completion. The 54 ms spike did not recur in that baseline. Profiling was removed from source after investigation.
- Visible styled quads now form one viewport-culled batch per layer/pass, preserving source order, textures and time-zero envelopes; changed bounds share a following marker batch. This retains the existing bounded tile cache. Profiled batch repeat `run-u572h_8i` passed 82 steps: full context 2.86 / 3.25 ms, tree 3.88 / 4.32 ms, layer motion 4.71 / 7.39 ms, group motion 5.37 / 6.97 ms (mean / max). An earlier batched sample (`run-c4w1cm0e`) contained 28-36 ms graphics-completion waits with only 0.65-0.78 ms CPU and no tile rebuilds; timings are hardware/session measurements, not a universal latency guarantee.

- Final unprofiled Release after batching: all 1,858 native steps passed in 92.64 s (`run-qkfpiq9j`, `sections-batched-native-release.log`), including overlapping textured-quad alpha/source-order pixels and unchanged source state. Inspected the distant-sky screenshot with the new overlap fixture.
- Final isolated Springlobe run `run-uz7op30e` passed 82 steps (`sections-batched-springlobe.log`): full context 2.70 / 3.56 ms, tree 3.46 / 4.03 ms, layer motion 3.91 / 7.03 ms, group context 2.47 / 3.01 ms, group motion 3.86 / 5.15 ms, context off 2.09 / 2.68 ms (mean / max). These measured final full/tree/motion values are below the previous spec's reference; source geometry remains viewport-culled and the tile-cache scope is unchanged.

- Final normal Release rebuilt with `MAP_COMPARE_NATIVE_TEST=OFF`; `DDNet.exe` is 4,828,160 bytes and contains neither native-test markers nor temporary profiling markers (`sections-final-normal-build.log`, `sections-final-normal-verification.log`). clang-format 20.1.8 dry-run and staged/unstaged `git diff --check` pass. User-owned `ddnet-libs` remains untouched.

- Post-batching Debug run `run-5_k4craw` had one primary synthetic-input failure (the second right-eye click did not restore peers), followed by six dependent hidden-sibling pixel failures. The unchanged first 255 steps passed on diagnostic rerun `run-d174x6kk`; cursor/scroll, pressed-key and active UI ID tracing showed the expected second press/release and sibling restoration. No production or assertion change was made for this transient failure. A full Debug repeat follows with read-only input tracing in the test executable; temporary tracing and early-stop code were restored out of the source.

- Full Debug repeat `run-utue5pix` passed all 1,858 steps with zero failures in 176.74 s (`sections-batched-native-debug-repeat.log`). This executable retained read-only input diagnostics around the previously missed click; all regression assertions and scenarios were unchanged. Final source contains no profiling, click tracing or early stop. The isolated transient native-input failure remains recorded above; it did not recur in the identical isolated or full repeat.

- Parent matrix audit: read the complete implementation diff and incremental batching/tests; verified coverage for all seven matrix rows, inspected native tree/resize/quad screenshots and independently passed the full core suite. Final Debug completion and absence of temporary tracing/early-stop code verified before review.

### Review follow-up verification

- Implemented accepted private patches: saved same-entry section state, post-settings layout fit, indexed aggregate/child labels and navigator tooltips, divider wheel ownership, deferred settings reflow with column clipping, plus envelope scroll and inspector matching explanation regressions. Parent read the complete patch and all new assertions. Focused Release native matrix passed 729 steps in 35.15 s (`run-kzm24div`, `sections-review-native.log`). Full source and unfiltered test binary were restored before parent verification.
- Parent re-ran all 401 Release core tests successfully (`sections-reviewed-core-all.log`). The first full native run (`run-dy_6nbnt`) completed 2,070 steps with four existing tree interaction failures; all new review scenarios passed. Investigation did not accept a passing retry as resolution.
- Controlled timing experiment reproduced the exact four failures (`run-j0ci8rtl`): adding 30 ms per rendered frame during the first synthetic double click spaced releases 701 ms apart, exceeding the UI's 500 ms threshold. Cursor and active/hot identity stayed constant, with no hardware-motion or focus event. The first fold was missed, the next double click folded instead of reopening, and the test targeted hidden descendants. This demonstrates a test-helper timing defect and a missing target-visibility assertion. Native helper correction preserves the actual SDL/UI path and all product assertions; no production tree change is justified by this evidence.

- Corrected native helper passed the same slow-frame experiment (`run-78gyjh9h`, 275 steps, zero failures): release spacing fell to 234 ms. The helper now waits real-time expiry, establishes hover once, sends short complete SDL clicks, checks input delivery/active target/release interval, and validates ancestor expansion. Temporary pacing, tracing and early-stop code were removed. Parent read this final test-only patch; all original product assertions and fixture registrations remain.
- Final parent verification: Release full core 401 passed (three pre-existing disabled); Debug comparison core 22 passed. Full unfiltered native Release `run-e2r_mewm` passed 2,065 steps in 96.38 s; Debug `run-2givg_ba` passed the same 2,065 steps in 175.39 s. Logs: `sections-final-native-release.log`, `sections-final-native-debug.log`. The reduced step count reflects the shorter double-click helper, not removed scenarios. No transient click failure recurred, and the stronger delivery assertions passed.
- Final Springlobe `run-nab4czxv` passed all 82 steps (`sections-final-springlobe.log`): full-map context 2.07/2.10 ms, open tree 2.81/3.30 ms, layer pan/zoom 3.66/5.23 ms, group pan/zoom 3.85/5.25 ms (mean/max, rounded upward for maxima). These are local measured samples, not a hardware-independent guarantee.
- Final normal Release rebuilt and verified with MAP_COMPARE_NATIVE_TEST=OFF, 4,827,648-byte DDNet.exe and no native/trace markers (`sections-reviewed-normal-build.log`, `sections-reviewed-normal-verification.log`). clang-format20.1.8 dry-run/Werror on all seven changed C++ files and staged/unstaged diff checks passed. All accepted review findings are fixed; no accepted fix was deferred. User-owned ddnet-libs remains excluded.
