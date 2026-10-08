# Comparing open maps in the editor

Open two maps, including two copies of the same file if you want to inspect scratch edits. Right-click the second map's tab and choose **Compare with active map**. The active map is **Before**; the clicked map is **After**. **Tools > Compare maps** and the **Compare maps** quick action open a selector for both sides. A map cannot be compared with its own tab.

Comparison reads current in-memory content, including unsaved edits. Both panes identify the map, saved path, and unsaved state. **Swap** reverses direction and captures the current contents again. Comparison does not save either map. Existing background autosaves continue normally.

## Controls and interpretation

- **Back / Escape** returns to editing. Release held keys and mouse buttons before editing resumes.
- **By layer / By group** changes how the same result is organized. Map metadata, ordered server settings, images, sounds, and envelopes appear in the map-wide entries. Group properties remain inspectable in layer mode.
- The navigator gives change counts. Select a row to inspect its layers or resource. Select a detail below the canvases to read exact Before/After fields. Tile coordinates and object indices are zero-based. Fixed-point coordinates and envelope values explicitly state their units.
- **Prev / Next**, or **P / N**, move through individual changes and across entries. **Fit / Home** fits the selected tile, object, layer, or group. Drag either canvas with the left or middle mouse button to pan both; use the wheel over a canvas to zoom both.
- **Context: off** shows changed content by default. **Context: on** also shows unchanged layers/items as dim context and includes unchanged navigator entries.
- Red **minus** markers identify old content. Green **plus** markers identify new content. Replacements appear on both sides. Added/deleted layers and groups remain navigable, including empty ones. Textual statuses and exact fields accompany color.
- Envelopes in both panes use time zero plus each object's persisted time offset. Parallax is evaluated at the same initial reference position on both sides. Fit and pan use an independent inspection translation so even zero-parallax content can be inspected, while source group offsets and parallax differences stay observable. The viewer has its own camera and clipping; it does not switch the active editing tab or change the source animation evaluators.
- Missing resource content and invalid references are explicit issues. Selecting a resource includes references to affected layers. Sound source shapes and properties are inspected visually; comparison does not play sounds.

## Matching limits

Map files do not contain persistent identities for arbitrary groups, layers, quads, or sound sources. Matching first uses unique gameplay roles, then compatible identical content, then unique compatible names. Distinct names disambiguate identical-content buckets. Remaining equal-sized compatible ordered spans are aligned conservatively. An **[?]** label indicates uncertainty; a matching unnamed or duplicate item does not establish historical identity. Cross-group moves can appear as deletion and addition.

Relative order is compared only among matched items. Inserting a layer or quad therefore does not mark every later item as modified. Resource references use canonical content fingerprints instead of table indices, so renumbering resources and preserving references does not change the visual result. Embedded bytes, external image references, relevant envelope channels and Bezier tangents are compared. Unused tangent/union fields, tile skip/opaque flags, padding, visibility, selection, brush, camera, filenames, and other editor-only state are excluded from content equality. Persisted automapper configuration, seed, and automatic mode are included; its runtime reference-layer selector is not a saved map property.

The snapshots own their comparison data. Tiles and changes are sparse and coordinate-based. Comparison runs on a cancellable worker after const snapshots have been captured on the editor thread. Capturing very large documents and copying embedded resources can briefly occupy the editor thread before cancellation is processed.

## Reproducible native-editor checks

Use a dedicated test profile and scratch tabs. Keep the bundled map files intact. For each fixture (`coverage.map`, `Tutorial.map`, `Tsunami.map`, and `Sunny Side Up.map`):

1. Open the fixture twice. Change only editor visibility, selection, camera, and animation state in the second tab. Launch from the first tab's active context into the second tab, then repeat through Tools. Both comparisons should report no content differences. Enable context and inspect the actual textures and layout.
2. In the second tab, paint and erase a tile without saving. Change a tele/tune number, switch number/delay, or speedup force/max-speed/angle while keeping its type unchanged. Compare again. Check old/new markers, exact coordinates and fields, direction, and Swap.
3. Insert a decorative layer before an existing layer, and insert a quad before existing quads. The inserted items should appear as additions; later unchanged items should remain unchanged. Swap two existing layers/quads to check relative draw-order reporting. Add/delete an empty group and resize a tile layer to check structural rows and coordinate alignment.
4. Change a group's offset, parallax, and clipping. Inspect both panes, then both navigation modes, linked pan/zoom, Fit, and window resizing. Check the selected source textures at multiple zoom levels and envelope time zero.
5. Replace an embedded image or sound, change an envelope point and a used Bezier tangent, edit map metadata, and insert duplicate server-setting commands. Check resource entries, affected-layer references, exact values, command positions, and order. Renumber equivalent resources with references preserved and check for false changes.
6. Before comparison, record dirty flags, history sizes, current layer/group/quad/envelope selections, brush, and both source camera states. While comparing, attempt painting, deletion, undo/redo, typing, save shortcuts, and brush shortcuts. Return through both Back and Escape, including while a mouse button is held. These attempts must leave source editing state and content unchanged, apart from independently scheduled autosave bookkeeping.
7. Close a source through an already pending save completion; also load/drop a map externally while comparison is open. The viewer must stop referencing the source, display the reason, and safely return without sending the closing input to editing.
8. Record snapshot/comparison time, memory behavior, and navigation responsiveness on the largest fixture, including cancellation during comparison. Automated loading alone does not validate the viewer.

## Build and automated checks

Use a compiler environment appropriate for your platform. On Windows, use a Visual Studio Developer PowerShell and set `VSLANG=1033` **before the first CMake configure**, so Ninja records the matching English include-dependency prefix. Use a fresh build directory if the compiler language or cached dependency prefix has changed.

```sh
cmake -S . -B build/map-comparison -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDOWNLOAD_GTEST=ON
cmake --build build/map-comparison --target game-client testrunner
build/map-comparison/testrunner --gtest_filter=MapDiff.*:Editor.*
```

Append `.exe` to executable names on Windows. Supply your platform's dependency options as usual.

For reproducible native verification, configure a separate build with `-DMAP_COMPARE_NATIVE_TEST=ON`. This replaces the editor factory only in that opt-in configuration; the normal client contains no test access or instrumentation. The test client requires a working desktop graphics session and OpenGL 3.3. It is unsuitable for a headless client build.

```sh
cmake -S . -B build/map-comparison-native -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMAP_COMPARE_NATIVE_TEST=ON
cmake --build build/map-comparison-native --target run_map_compare_native
# Once built, the same suite is registered with CTest:
ctest --test-dir build/map-comparison-native -R '^MapCompare.Native$' --output-on-failure
```

The runner creates isolated storage below the build directory, retains per-run reports and screenshots, and fails on assertion failures, crashes, or timeout. The checked-in harness exercises the real editor and input/render lifecycle. It covers special tile arrays, angle-only speedup edits, ignored fields, semantic resource renumbering with actual presentation references, snapshot independence, a nonempty brush and source histories, held closing input, immediate reopen/cancel cleanup, changed-to-equal reopening, Home and navigator mode/context changes, wrapped inspector values, and distant zero-parallax content. A solid white source texture produces deterministic red/green tile-interior framebuffer samples; those pixels are asserted independently of screenshot writes. The bundled-map corpus also records isolated capture-of-both-sources and pure comparison timings.

The pure-core suite covers coordinate-preserving resize, all tile value fields, owned snapshots, layer/object insertion and order, duplicate names, semantic resource renumbering, missing content, metadata and ordered duplicate commands, group changes, envelope/source properties, cancellation, and sparse large extents. Native rendering and source-state checks must be recorded separately; passing this corpus does not establish that all real-world maps are issue-free.
