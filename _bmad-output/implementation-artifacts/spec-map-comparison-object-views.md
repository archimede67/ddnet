---
title: 'Familiar layer controls and object-specific comparison views'
type: 'feature'
created: '2026-10-09'
status: 'done'
route: 'dispatch'
baseline_commit: '6618d3210b9d2b260230c1d63c3d06315b7b32e6'
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The comparison layer tree feels unlike the normal editor, clicking an individual change does not focus its location, and settings/envelopes need useful dedicated presentations.

**Approach:** Use familiar compact group/layer controls, unify detail navigation and focusing, and automatically present settings as aligned text and envelopes as paired curves when selected. User delegates design and implementation.

## Boundaries & Constraints

**Always:** Read-only source maps and editor state; preserve both context modes, visibility gates, navigation, scroll stability, and bounded large-map rendering. Show exact values and absent/ambiguous states. Leave user-owned `ddnet-libs` untouched.

**Never:** Merge/edit commands, save source maps, execute settings, remote operations, or add map-sized per-frame work.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| Detail focus | Click distant tile/object, also same detail after panning | Both map panes refocus; group mode targets owning child | Hidden selections retain explanatory notice |
| Familiar tree | Groups, gameplay/decorative layers; folded/scrolled tree | Compact native-style eyes/rows/indentation; selection, folding, status and visibility remain distinct | Fold clamps only when required |
| Ordered settings | Insert/delete/replace; repeated or empty commands | Aligned Before/After lines, real line numbers, neutral unchanged context, exact wrapped text; P/N reveals change | Missing side differs from literal `(absent)` |
| Large settings | Long sequences; cancellation | Bounded alignment work and cached layout; cancellation produces no partial result | Oversized unmatched spans use documented conservative alignment |
| Envelopes | Color/position/sound, Bezier, unequal durations, terminal value unlike first | Paired curves with shared axes, channel legend/toggles, changed-point emphasis; exact inspector fields | No endpoint looping or extrapolation |
| Envelope edges | Added/deleted/empty/single-point, channel-type differences, Swap/reopen | Clear absence/empty/type labels, stable finite graph, fresh selection cache | No invalid source access or source evaluation mutation |
</frozen-after-approval>

## Code Map

- `src/game/editor/map_compare.h/.cpp`: viewer state, RenderLayerTree/Inspector, Select/Navigate/FitSelection; detail click lacks FitSelection. `CListBox::WasItemSelected()` supports repeated clicks. Preserve visibility gates, caches, scroll resets.
- `src/game/editor/editor.cpp:2340`: native tree rows12, spacing2, group gap5, indent12, left eye15 CORNER_L/name CORNER_R, group `#index name`, gameplay style+6, double-click folds, right-eye solos/restores peers. Reuse appearance/gestures, never edit handlers.
- `src/game/editor/map_diff.h/.cpp`: owned snapshots and worker; add settings alignment rows and derive properties from them. Bounded LCS after equal prefix/suffix; cancellation and conservative oversized-span fallback.
- `map_diff_snapshot.cpp:164`, `mapitems/envelope.cpp`, `envelope_editor.cpp:660` under editor: canonical envelope properties, entry indices select const source envelopes. Eval uses Bezier but loops at EndTime; use explicit terminal value and never repeat shorter envelopes. Do not copy CEnvelope (internal pointer).
- `src/test/native/map_compare.cpp`: SDL tests and source-state/framebuffer checks. Adapt TreePosition/fold tests and obsolete settings m_Values assertion. Reuse Click/Delay/accessors; names max11chars.

## Tasks & Acceptance

**Execution:**
- [x] Viewer: compact native-style tree with small textual/color diff badge and tooltip; familiar eye/solo and double-click folding; preserve keyboard and scroll behavior.
- [x] Viewer: shared detail focus behavior for clicks/repeated clicks and keyboard/P/N, with regression coverage.
- [x] Diff core and viewer: settings alignment on worker; full-height shared-scroll text view; changed-row selection and P/N linked to aligned rows. Cache wrap heights by width; render visible rows only; disable canvas gestures here.
- [x] Viewer: automatic envelope-specific paired plots plus existing exact-field inspector; shared time/value bounds, native channel colors/toggles, selected point indication. Cache bounded curves/markers; include tangent bounds and affected-layer references.
- [x] Core/native tests cover matrix; update `docs/map-comparison.md`; record actual validation and screenshots.

**Acceptance Criteria:**
- Switching entry kinds selects an appropriate view without extra toolbar clutter or losing the map camera to resource gestures.
- Swap/reopen/invalidation reset caches safely.
- Native source-state assertions and large Springlobe benchmark continue passing; normal Release executable is rebuilt without test instrumentation.

## Implementation Notes

- Authorized design delegation; no unresolved intent gaps or irreversibles. Dispatch footprint: viewer, settings diff, tests, docs.

- Implemented compact native rows/eye solo/double-click folding, shared SelectDetail focusing, aligned settings worker rows with virtualized wrapped text, and cached paired envelope plots. Source maps remain const; comparison caches reset on source lifecycle changes. Parent read the full staged diff, including tests and documentation.
- Matrix audit: NavigationSteps tests tile/quad/repeated clicks and group ownership; FullContextSteps/SectionScrollSteps cover native eyes/solo/folding, visibility and scroll; SettingsAlignment tests cover insertion/deletion/replacement/duplicates/empty/absence plus large fallback/cancellation; ObjectViewSteps covers resource navigation, terminal framebuffer checks, type/empty/single-point/added/deleted screenshots, Swap/reopen and source state. All executed in both native configurations.
- Release and Debug filtered core: 21 passed each. Native: 1,433 steps, zero failures each; Release run-luqdelz1, Debug run-bvoa8svu. Parent inspected 800x600 tree/settings/position-envelope screenshots; long wrapped settings and remaining envelope cases inspected during implementation. Logs: out/map-compare-smoke/views-*.
- Springlobe run-lo1r8kwv: 82 steps passed; full-map context mean2.68ms/max3.04ms, Layers open mean4.32ms/max4.69ms, layer/group motion maxima8.31/9.51ms. Dense native tree has measurable additional UI cost versus prior recorded tree, while geometry remains culled/bounded. Normal Release client rebuilt with instrumentation OFF. No cross-hardware FPS guarantee.

## Spec Change Log

## Review Triage Log

All three same-model reviews completed: blind and edge via native agents; verification via T3 after the native thread limit. Findings below were classified individually before grouping. Every accepted finding has a direct private implementation, documentation, or regression-test correction; no public surface or new behavior scope is required.

| Finding | Verdict / route | Evidence |
|---|---|---|
| Blind 1: fold resets inspection | medium / patch | Tree calls Select even for the already selected group; Select resets detail and fits. Guard redundant selection and test a panned group with child detail. |
| Blind 2: marker keyboard focus | medium / patch | Marker calls SelectDetail without clearing tree focus, while navigator consumes arrows first. Transfer focus on the hit. |
| Blind 3: uniform samples omit pulses | medium / patch | A millisecond pulse within a 100-second envelope can fall between sample times. Label the bounded curve preview as sampled; exact points/fields remain inspectable. |
| Blind 4: hidden selected channels | low / patch | DrawPoint correctly obeys channel visibility, but docs promise unconditional visibility. Clarify the policy and show a no-channels notice. |
| Blind 5: overlapping marker hits | medium / patch | Every point hit can call SelectDetail, then the prior selected point is drawn/hit again. Choose one nearest visible point and apply selection once. |
| Blind 6: point versus field highlight | low / patch | Parser retains point index; all visible channels at that point are highlighted. Narrow docs to selected point, keeping exact field values in inspector. |
| Blind 7: settings scroll resurrection | medium / patch | CScrollRegion::End returns without clearing stored offset when content fits. Reset this viewer region on measured overflow-to-fit resize, preserving selection. |
| Blind 8: endpoint test accepts marker | medium / patch | Current terminal pixel predicate accepts independent markers. Assert linear/Bezier curve interiors away from markers, including near-terminal segments. |
| Blind 9: dense marker coverage | medium / patch | New fixtures contain at most two points. Add dense marker-budget/selected-point and marker-click coverage. |
| Edge 1: selected group fold | medium / patch | Independently confirms Blind 1; same redundant Select call. |
| Edge 2: large constant range | medium / patch | At large fixed-point values float ULP exceeds 0.05 padding; equal bounds make Position divide by zero. Guarantee representable nonzero range and test rendering. |
| Gap 1: curve presence | medium / patch | Pre-verified mutation removes curve draw while markers satisfy tests. Add independent interior pixels for linear and Bezier plots. |
| Gap 2: displayed settings sides | medium / patch | Pre-verified rendering-only Before-index substitution escapes alignment tests. Assert rendered command/gutter regions for side-specific insertion/absence/empty fixtures. |
| Gap 3: settings click selection | medium / patch | Existing click checks only camera. Assert clicked property and next navigation destination. |
| Gap 4: envelope click selection | medium / patch | No marker-selection assertion exists. Assert both pane hits, selected field and exact values. |
| Parent: focus on reopen | medium / patch | New navigator SetActive reads persistent m_LayerTreeActive; Open resets lists but not this flag, so prior inspector focus survives reopening. Reset initial navigator focus and cover reopen. |


## Review Fix Evidence

- All 16 accepted findings corrected, with no deferrals. Folding guards both entry and mode so a newly requested group view still aggregates children. Marker hit testing chooses one nearest visible changed point, then transfers keyboard focus; hidden channels retain their explicit policy. Settings offsets reset after a measured fit transition; Open restores initial navigator focus.
- Parent read the complete 532-line incremental fix diff. Expanded native assertions verify panned child-detail folding, both-pane overlapping marker hits and exact inspector values, subsequent arrow navigation, render-only settings-side/index mistakes using independent expected glyph regions, changed-row clicks/P/N, width transitions, and reopening.
- Independent framebuffer assertions now sample linear and Bezier interiors away from markers, including near-terminal positions. A 1,025-point fixture verifies selected point 777 outside the overview set; int-max constant values verify finite visible curves and exact raw fields. Parent inspected dense and large-value screenshots. Sampled preview limits and point/channel highlight policy are visible/documented.
- Final targeted Debug native: 1,576 steps, zero failures, 169.34s; evidence out/build/map-comparison-native/map-compare-native/run-g1zj8671. Formatting and diff-check passed.

## Verification

- Initialize MSVC via `C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation`; VSLANG=1033. Caches: `out/build/map-comparison-release`, `out/build/map-comparison-native` (Debug); avoid old map-comparison cache.
- Native instrumentation ON; build game-client/testrunner, run core and `ctest --test-dir <build> -R '^MapCompare.Native$' -V` sequentially in Release/Debug. Logs: ignored `out/map-compare-smoke/views-*`. Inspect screenshots including 800x600.
- Benchmark downloaded Springlobe4 pair using DDNET_MAP_COMPARE_PERF_BEFORE/AFTER (see docs); previous Layers full-context mean2.73ms/max3.03ms, pan/zoom max8.52ms.
- Restore Release MAP_COMPARE_NATIVE_TEST=OFF and rebuild. Format with clang-format20.1.8; git diff --check. Parent independently reviews and commits; implementation agent must not commit.

- Parent full Release core suite: 399 passed, three pre-existing disabled; out/map-compare-smoke/views-core-all.log.

- Final parent Release native run: 1,576 steps, zero failures, 79.37s; out/map-compare-smoke/views-final-release-native.log. All added regressions passed in both build configurations.
- Final Springlobe run-217tmn8p: 82 steps passed on 2800x1750 / 1,293,219 occupied tiles. Full-map context mean3.31ms/max4.10ms; Layers open mean5.09ms/max6.12ms; layer/group pan-zoom maxima8.66/8.24ms. This run confirms bounded rendering but also the denser tree's measurable UI cost; no claim of identical cost or cross-hardware FPS. Evidence: out/map-compare-smoke/views-final-springlobe.log.
- Normal Release DDNet.exe successfully rebuilt with MAP_COMPARE_NATIVE_TEST=OFF; out/map-compare-smoke/views-final-normal-build.log. All review findings addressed, none deferred. User-owned ddnet-libs excluded from the commit.
