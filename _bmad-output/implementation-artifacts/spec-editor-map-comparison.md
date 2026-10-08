---
title: 'Read-only editor map comparison'
type: 'feature'
created: '2026-10-08'
status: 'done'
route: 'dispatch'
baseline_commit: 'f823d33333723c3577d2b027c8b79f50993d258d'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Collaborators need to inspect meaningful changes between map versions, including unsaved edits, without manually switching tabs and remembering differences.

**Approach:** Add a dedicated, read-only comparison workspace with synchronized Before/After canvases, red deletions, green additions, and navigation by layer or group. Include exact property changes and map-wide changes that a visual overlay cannot explain.

## Boundaries & Constraints

**Always:** Compare current in-memory contents. The active map defaults to Before and the right-clicked map to After; show both names, paths/unsaved status, and a Swap button. Use plus/minus markers and textual statuses alongside color. Preserve source contents, history, selection, brush, camera and dirty state. Existing background autosaves may continue normally.

**Never:** Merge, apply/revert changes, save/export a diff, edit maps inside comparison, or change the map format. Do not serialize maps to obtain comparisons or use raw struct memory equality. Do not claim that matching unnamed/duplicate items proves their historical identity.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| Launch | Two open maps | Tab context action compares directly; Tools > Compare maps opens a pair selector | Disable self-comparison and unavailable pairs |
| Content | Tiles, quads or sound sources differ | Red old content, green new content; replacements show both; exact fields and coordinates available | Invalid references are identified explicitly |
| Structure | Added/deleted/resized/reordered groups or layers | Navigable additions/deletions, dimension/property and relative-order changes | Ambiguous pairing is labeled |
| Resources | Images, sounds or envelopes differ | Resource entries and affected-layer references | Missing content is reported, not treated as equal |
| Map-wide | Metadata or ordered settings differ | Before/after property values and command changes | Preserve duplicate commands and order |
| Equal maps | Same saved content, different editor state | Clear no-differences state | None |
| Lifecycle | Back/Escape, source closes, external map load/drop | Leave comparison safely; no input passes through to editing | Explain if a source became unavailable |

</frozen-after-approval>

## Code Map

- `src/game/editor/editor.cpp`: `DoMapTabs`, `Render`, `OnInput`, `OnUpdate`, `OnRender`, `CloseMap`, `Load`, `HandleMapDrop`; separate comparison routing before editing handlers. Save completion can close a source asynchronously.
- `src/game/editor/popups.cpp`, `editor.h`, `quick_actions.h`: tab menu, Tools menu, pair selector and quick action; update popup dimensions.
- `src/game/editor/mapitems/map.h`, `map_io.cpp`: document ownership and authoritative persisted fields. Saving mutates derived tile flags; never call it for comparison.
- `src/game/map/render_map.h`, `src/game/client/ui_listbox.h`: reuse rendering primitives and list controls. Existing group rendering depends on the active editor camera and cannot be reused unchanged for split panes.
- `CMakeLists.txt`: client editor sources and `TESTS_EXTRA`; current testrunner does not link the editor.

## Tasks & Acceptance

**Execution:**
- [x] `src/game/editor/map_diff.h/.cpp` — implement owned value snapshots and a pure comparison core, independent of client services. Compare all persisted group/layer properties, all seven tile kinds, quads, sound sources, images, sounds, envelopes, automapper settings, map metadata and ordered server settings.
- [x] `src/game/editor/map_diff_snapshot.h/.cpp` — capture const editor documents without mutation. Resolve resource references semantically; compare embedded content, external image references and relevant envelope channels/Bezier tangents. Ignore runtime visibility, selection, filenames, derived opaque/skip flags, padding and inactive union members.
- [x] `src/game/editor/map_compare.h/.cpp` — implement pair selection and comparison workspace: By layer / By group navigator, map-wide section, counts, exact before/after details, previous/next change, fit selection, linked pan/zoom, Swap, and Back/Escape. Show differences by default with an unchanged-context toggle. Render source textures and gameplay parameters with independent projection/clipping and a common fixed animation time, without changing source evaluators.
- [x] `src/game/editor/editor.h`, `editor.cpp`, `popups.cpp`, `quick_actions.h` — integrate entry points and route comparison input/render/update separately. Validate source identity rather than retaining tab indices; invalidate before source deletion. Prevent paint, undo, typing, save and other editing actions, including on the closing frame.
- [x] `src/test/map_diff_test.cpp`, `CMakeLists.txt` — register pure-core tests and client files; cover the matrix, parameter-only tile changes, resource renumbering, duplicate names, object insertion/order, snapshot independence and sparse large maps.
- [x] `docs/map-comparison.md` — document direction, controls, matching limits and reproducible native-editor checks using bundled maps and scratch edits.

**Acceptance Criteria:**
- Given two unsaved maps, when launched through either entry point, then the same current-content comparison appears with explicit direction.
- Given an inserted layer or quad, when comparing, then subsequent unchanged items are not reported as modified merely because indices shifted; actual draw-order changes remain visible.
- Given layer/group mode changes, when navigating, then the same diff is regrouped and additions/deletions remain inspectable.
- Given resource renumbering with references preserved, when comparing, then no false visual changes appear; changed resource content is reported.
- Given editing shortcuts and mouse actions in comparison, when returning, then source contents, histories and editing context are unchanged by the viewer.

## Implementation Notes

- Approval received; implementation branch is `feature/editor-map-comparison`. Existing `ddnet-libs` checkout changes belong to the user.
- Build preflight succeeded in `out/build/map-comparison` (MSVC Debug, Ninja, GTest enabled). Enter the compiler environment with `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`. Missing WavPack was extracted from the pinned dependency commit into `%LOCALAPPDATA%/ddnet-build-deps/c0e6703fbcdbe03df2f26875427ec3951ce4ec21`; include/library paths are already cached and its DLL is in the build directory.

- Parent acceptance audit read the staged implementation diff. All matrix rows have passing coverage: launch and lifecycle via the native client harness; content/structure/resources/map-wide via MapDiff tests and native frames; equal maps and preservation via both. The native run completed 348 scripted steps with zero failures across coverage, Tutorial, Tsunami and Sunny Side Up, including actual tab/Tools popup callbacks, pair selection, Swap, group/context buttons, resize, Escape with a held mouse, source close, external load, and attempted editing shortcuts.
- Native reopening found stale inspector rows after a changed comparison followed by an equal comparison; Start now clears detail/value state. The full corpus passed after the fix. Build-local harness and evidence: `out/map-compare-smoke/smoke_editor.cpp`, `smoke-results.log`, and `save/screenshots/`. The initial stale-object startup crash was eliminated by a full clean build with `VSLANG=1033`.
- Latest clean MSVC Debug build and 15 focused / 393 full-suite tests passed (3 pre-existing disabled tests). Debug corpus identity pipelines, including extra before/after snapshot and preservation assertions, took approximately 0.1s / 14.5s / 4.7s / 4.4s respectively; these are not isolated comparison timings. Optimized performance and additional adapter/presentation edge checks follow review.

- Final review corrections cover event consumption, zero-parallax inspection, measured scrollable property rows, visible navigator selection, cancellation and buffer release, visible-row tile rendering, and actual resource reference identity. All surviving patch findings have been addressed. The final client contains no test instrumentation when `MAP_COMPARE_NATIVE_TEST=OFF`.
- Reproducible native coverage is delivered in `src/test/native/map_compare.cpp` and `run_map_compare.py`, enabled with `MAP_COMPARE_NATIVE_TEST=ON`; `run_map_compare_native` and CTest `MapCompare.Native` run the same isolated-profile suite. It checks actual snapshot capture, nonempty brush/history preservation, held closing input, reopening/cancellation, mode and context selection, wrapped values, and deterministic red/green framebuffer pixels alongside the four bundled-map fixtures.
- Final fresh MSVC Debug and Release builds passed with English Ninja dependency prefixes. Both C++ suites passed all 395 tests (3 pre-existing disabled); the focused suite passed all 17 tests. Clang-format 20.1.8 and whitespace checks passed. Final Release native verification passed all 414 steps, with evidence in `out/build/map-comparison-release/map-compare-native/run-v1mcb2so/` and `out/map-compare-smoke/release-final-native-tests.log`. Parent visually inspected the rendered layer and group screenshots. The normal executable was rebuilt at `out/build/map-comparison-release/DDNet.exe` with native instrumentation disabled.
- Isolated Release capture-pair / comparison timings in milliseconds: coverage 0.26 / 0.24, Tutorial 463.72 / 32.52, Tsunami 127.84 / 21.34, Sunny Side Up 135.54 / 28.67. Snapshot capture remains synchronous; very large documents may pause the editor during capture. This corpus is evidence for these maps and scenarios, not a guarantee for every real-world map or graphics backend. Native rendering was tested on Windows/OpenGL 3.3; other platforms/backends were not exercised here.
- Final Debug native CTest also passed all 414 steps with zero failures, including exact framebuffer color tolerance and the real scroll-region extent (103 units for the long value): `out/build/map-comparison-native/map-compare-native/run-bdfq5_hj/`. Review completed with no deferred findings.

## Spec Change Log

## Review Triage Log

| Finding | Verdict | Evidence and route |
|---|---|---|
| Blind 1: Home changes selection | medium | Verified OnInput forwards Home and CListBox::DoStart consumes HOTKEY_HOME. Patch: return after Fit. |
| Blind 2: zero-parallax Fit/pan | medium | Fit divides by substituted 1 while rendering multiplies camera by 0; distant content stays outside the pane. Patch the private inspection projection consistently. |
| Blind 3: long inspector values | medium | Fixed 76-unit rows render unrestricted wrapped text; a long command exceeds its scroll extent. Patch measured row sizing. |
| Blind 4: filtered-out selection | medium | Group-to-layer toggle keeps a group excluded by RebuildNavigator when only children changed. Patch selection remapping. |
| Blind 5: retained memory | medium | EndFrame clears pointers but retains owned snapshots and full results after returning to editing. Patch completion/reaping and release existing buffers. |
| Blind 6: cancellation gaps | medium | Equal and resource byte comparisons/hash paths have no checkpoints; replacement of an unfinished async future can wait. Patch checkpoints and avoid replacing pending work on UI thread. |
| Blind 7: full-map render scans | medium | Changed-cell and overlay loops scan every cell despite coordinate sorting. Patch visible-row bounds. |
| Blind 8: resource reference identity | medium | Content-equivalent resources share keys; the inspector lists all matching keys rather than the selected table entry's actual references. Patch presentation-only source reference identities. |
| Blind 9: final outgoing Bezier tangent | false | map_io.cpp:406-409 explicitly persists outgoing tangents on every Bezier point, including the last. Comparing this persisted authoring value is intentional; dropping it would hide saved edits. |
| Blind 10: adapter tests absent | medium | All committed tests construct snapshots directly; actual Capture checks exist only in ignored local harness. Patch by delivering and registering native adapter regression cases. |
| Edge 1: Home fallthrough | medium | Same verified Home/CListBox interaction as Blind 1. Patch together. |
| Edge 2: zero-parallax Fit | medium | Same demonstrated projection mismatch as Blind 2. Patch together. |
| Edge 3: overflowing values | medium | Same fixed row/unbounded wrapped text as Blind 3. Patch together. |
| Edge 4: uncancellable equality | medium | Equal loops lack Check.Tick; same root cause as Blind 6. Patch together. |
| Gap 1: actual speedup angle capture | medium | Pre-verified gap: core tests bypass Capture, local native mutations do not exercise speedup angle. Patch registered actual-document adapter assertions. |
| Gap 2: reversed tile payloads untested | medium | Pre-verified gap: counts/coordinates do not prove Before/After payload direction. Patch exact replacement/addition/deletion/reverse assertions. |
| Gap 3: local-only closing test | medium | Pre-verified gap: held-input/source preservation test is ignored and not delivered. Patch by promoting the working harness into a registered native test workflow. |
| Gap 4: screenshot lacks pixel oracle | medium | Pre-verified gap: screenshot writing is asserted, rendering was checked by parent image inspection, but no repeatable pixel oracle detects missing tile interiors. Patch deterministic native pixel assertions. |

Surviving findings require direct corrections to existing private viewer state/loops and registration/extension of the already-working test harness. They add no user-facing feature or public API; all route to patch. No intent gap or pre-existing issue was found.

## Design Notes

Match unique gameplay roles, then identical compatible content, then unique compatible names. Conservatively align remaining ordered spans and flag ambiguity; never discard unmatched data. Compare relative order among matched items, not absolute indices. Cross-group moves may appear as removal/addition. Compare tile coordinates over both extents, not linear array offsets. Store sparse changes and cached resource fingerprints; avoid unbounded all-pairs matching or per-cell text allocations. Comparison computation must remain cancellable for expensive inputs.

Snapshots own comparison data; render resources may depend on source lifetime only while explicitly validated. Do not copy `CEditorMap` wholesale or switch the active editor tab to render a pane.

## Verification

- Configure a dedicated MSVC Debug build in `out/build/map-comparison`; build `game-client` and `testrunner`.
- Run focused `MapDiff.*` and `Editor.*` tests, then the existing C++ suite; run formatting and `git diff --check`.
- Native-editor checks with `coverage.map`, `Tutorial.map`, `Tsunami.map` and `Sunny Side Up.map`: self-content comparisons, scratch unsaved tile/parameter/quad/resource/structure edits, group transforms/clipping, both navigation modes, resizing, closing and attempted edits. Check source state before/after and record timing on the largest fixtures. Loader-only tests do not count as viewer validation.
- Record actual build/test/runtime evidence and any untested cases. Passing this corpus does not establish that all real-world maps are issue-free.
