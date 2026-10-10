# Editor history verification tools

Use these tools when changing editor history, document storage, or saving. They run the real editor in isolated storage and help check correctness, memory use, and responsiveness. All instrumentation and default fixtures are in the repository; no previous build or benchmark output is needed.

## Choose a check

| Need | Tool | Produces |
| --- | --- | --- |
| Check ordinary editor behavior after a code change | CMake `run_editor_tests` target | Actual-editor test output and success/failure status |
| Check schema, storage and save jobs as well as the editor | CMake `run_cxx_tests` target | Core C++ and actual-editor test output |
| Check history costs and storage invariants | `prepare.py --mode benchmark`, then `run.py` | Timings, allocation/memory counters, assertions and native-export comparisons |
| Try adding an authored property | `prepare.py --mode extension`, then `run.py` | Undo/redo, cancellation and persistence-equality checks for an injected field |
| Measure editing while saving | `prepare.py --mode save`, then `run.py --map ...` | Save enqueue/completion and foreground-frame distributions |
| Compare pre-v2 and v2 history timing | [Matched probes](matched/README.md) | Paired headless measurements without allocation hooks |
| Compare native pixels and rendering speed | [Rendering probes](rendering/README.md) | Screenshots, frame distributions and comparison verdicts |

The last two tools have Windows-specific requirements. Start with ordinary regressions for a routine code change; benchmarking is optional. To understand the implementation, read the [contributor guide](../../docs/editor/EDITOR_HISTORY.md) and [property walkthrough](../../docs/editor/EDITOR_HISTORY_WALKTHROUGH.md).

## Prerequisites

- Follow [Building DDNet](../../docs/BUILDING.md) for your platform's compiler, CMake, Python, Rust and library dependencies. Initialize `ddnet-libs` if using bundled dependencies. These scripts do not install a build environment.
- The main scripts here require Python 3.9+. GTest must be available for `editor-testrunner`; the examples enable CMake's download option, which needs network access if GTest is not already installed. Check configure output confirms GTest is available.
- Optional `python -m pip install psutil` adds sampled process-memory and background-CPU observations. Windows native probes also report working-set/private-commit peaks; unavailable fields on other platforms cannot establish a private-memory budget pass.
- Run commands from the repository root. Use new source-copy and result directories for each exercise. Build directories can be reused only with the same prepared sources and configuration.

The examples use CMake's default generator and Release configuration. Supply your normal dependency/generator options as needed. On Windows, run from a developer shell and replace `build-history/editor-testrunner` with `build-history/editor-testrunner.exe`, or `build-history/Release/editor-testrunner.exe` for a multi-configuration generator. An ordinary `DDNet.exe` does not contain these tests.

## Ordinary regressions

This uses the unmodified checkout; there is no preparation step. For a complete run, clear any inherited `GTEST_FILTER` first: `unset GTEST_FILTER` in a POSIX shell, or `Remove-Item Env:GTEST_FILTER -ErrorAction SilentlyContinue` in PowerShell. Ordinary runners inherit this filter, so leaving one set can silently select only a subset of tests.

```sh
cmake -S . -B build-editor-tests -DCLIENT=ON -DSERVER=ON -DHEADLESS_CLIENT=ON -DDOWNLOAD_GTEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-editor-tests --config Release --target run_editor_tests
```

To run core schema/storage/save-job tests **and** the actual-editor suite, use this build target instead:

```sh
cmake --build build-editor-tests --config Release --target run_cxx_tests
```

`run_cxx_tests` requires `SERVER=ON` and GTest; with the headless client configured above, it also runs `run_editor_tests`. The CMake targets build their runners and locate them automatically. [The test driver](../test_editor_history.py) uses bundled maps and temporary storage, prints the results, and removes its temporary files afterward. A nonzero exit means a test or runner failure.

## Prepare, build and run an exercise

The following creates a benchmark source copy and runs the four bundled fixtures:

```sh
python scripts/editor_history_tools/prepare.py --source . --output build-history-source --mode benchmark
cmake -S build-history-source -B build-history -DHEADLESS_CLIENT=ON -DCLIENT=ON -DDOWNLOAD_GTEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-history --config Release --target editor-testrunner
python scripts/editor_history_tools/run.py --source build-history-source --client build-history/editor-testrunner --output build-history-results
```

`prepare.py` copies the current sources and installs instrumentation only in that copy. To choose `extension` or `save`, change `--mode` and use new source/build/result directory names throughout. Build the executable from the corresponding prepared copy before running it. When production sources change, prepare and build a new copy.

With no fixture arguments, the runner uses tracked `coverage.map`, `Tutorial.map`, `Gold Mine.map` and `jungle_day.map`. Select one by name with `--fixture Tutorial.map`. To measure a map you supply, append `--map "<path-to-your-map.map>"` to the run command, replacing the placeholder with an existing file. Repeat `--map` for multiple maps. The runner copies each input into isolated storage and does not overwrite the original or load your editor settings. Springlobe maps are optional external inputs, not bundled dependencies.

### External maps and assets

Only the `.map` file is copied automatically. Use a map with embedded resources or resources already supplied in the prepared source's `data/mapres/`. If it needs custom external images or sounds, place those files in the **prepared copy's** `data/mapres/` under the exact referenced names before running. For comparisons, provide identical assets to both prepared copies and record their hashes with your results. Personal resource directories are not searched; missing assets can change rendering or loading behavior.

### Outputs and failures

All paths below are **generated by the commands above**, not files expected in a fresh checkout:

| Location | Contents |
| --- | --- |
| `build-history-source/editor-history-source-manifest.json` | Hashes of copied source files before instrumentation |
| `build-history-source/editor-history-exercise.json` | Mode and source location; required by `run.py` |
| `build-history-results/results.json` | One result per fixture: exit/pass status, hashes, process peaks, parity and save data where applicable |
| `build-history-results/0/`, `1/`, … | One isolated storage directory per input: all `--fixture` entries first, then all `--map` entries; order within each group is preserved |
| Each fixture directory's `stdout.log` / `stderr.log` | Test failures and raw `BENCH`, `MEMORY` or `SAVE_SAMPLE` records |
| Each fixture directory's `maps/` | Copied input and any maps produced by that exercise |

Start with `results.json`, then inspect the corresponding logs. Benchmark phase timings and allocation counters are in `stdout.log`; save raw samples and their distributions are also stored in JSON. A nonzero runner exit means a failed test, native-export mismatch where required, requested save-budget failure, or execution error. The runner stops on the first failed fixture, so check that all requested fixtures appear before treating a batch as complete. Each fixture has a 900-second timeout. A timeout or launch/input error may leave no `results.json`, or only results for earlier fixtures; inspect the current numbered directory's logs and the terminal error in that case. Retain the whole output directory, prepared manifests, source revision/patch, asset hashes and build configuration when sharing results.

The checked-in [baseline export hashes](baseline_export_hashes.json) identify expected normalized outputs from the previous native writer. They are comparison inputs, not paths to missing binaries. Changes to bundled fixtures or the native format may require a deliberately reviewed baseline update.

## What each mode measures

### Benchmark

The bundled protocol uses five warmups and fifty measured samples per ordinary phase, plus ten cold fingerprint constructions. It covers begin/update/commit, cancellation/restoration with a headless frame, metadata, dense fill/resize, resource replacement and object/special-plane edits. Value and fresh persisted-key checks occur outside timed regions. Sparse gestures assert zero captures at begin/update and one at completion.

An external map automatically selects the massive-map protocol; `--massive` selects it for a bundled fixture. It measures complete loading including teardown, the first `OnRender` + `Swap`/`WaitForIdle`, and sparse edit/undo/redo with their following frames. It exercises the game layer and largest ordinary layer with the history panel closed and open. Each case retains exactly fifty roots before five warmups and fifty samples. A map with no ordinary layer reports that workload as absent. Structural checks cover hashing, accounting, viewport reads and retained memory.

Allocation hooks count requested C++ `new`/`delete` bytes, including aligned forms; they exclude native `malloc`, Rust, allocator bookkeeping and GPU/audio allocations. Transient bytes are additional live C++ bytes above a phase's starting value. Document-union accounting deduplicates shared allocations but excludes its own accounting/journal bookkeeping and allocator/control-block overhead. Process measurements include test setup, native caches and diagnostic fingerprint pins. Categories overlap and must not be added together. This assertion-heavy protocol has different overhead from the matched timing probes.

### Extension

The preparer adds an authoring-only integer to `CLayerGroupValues` and an ordinary `Timeline.Edit` test. Capture, restore, equality, export and history mappings are unchanged. The exercise checks undo/redo, 200-update cancellation, redo preservation, no-op behavior and persisted-content equality. It does not add a UI control or native map field. Use an unmodified checkout: the [walkthrough](../../docs/editor/EDITOR_HISTORY_WALKTHROUGH.md) gives a separate test route if you already added the practice field yourself.

### Save responsiveness

Prepare with `--mode save` and pass a map through `--map`. Without an external-map argument this mode only checks normalized native-export parity on the bundled fixtures; it does not measure save responsiveness.

This complete example creates a separate save build and uses the bundled Tutorial map as an explicitly supplied input. Replace that input with your existing large map to investigate large-map stalls. Apply the executable-path adjustments described under prerequisites on Windows.

```sh
python scripts/editor_history_tools/prepare.py --source . --output build-history-save-source --mode save
cmake -S build-history-save-source -B build-history-save -DHEADLESS_CLIENT=ON -DCLIENT=ON -DDOWNLOAD_GTEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-history-save --config Release --target editor-testrunner
python scripts/editor_history_tools/run.py --source build-history-save-source --client build-history-save/editor-testrunner --map data/maps/Tutorial.map --output build-history-save-results
python scripts/editor_history_tools/summarize_saves.py --input release=build-history-save-results/results.json --output build-history-save-summary.json
```

The save workload retains fifty undo entries with the history panel visible. It measures manual-save and actual autosave enqueue time, total completion time, and foreground edit/undo/redo/idle frames during saving. Frames execute `OnUpdate`, `OnRender`, `Swap` and `WaitForIdle`; 16 ms pacing sleeps are outside frame timings and inside total save duration. No allocation hooks are installed, and GPU/audio work is excluded.

By default it performs three manual and three automatic saves, then three overlapping copy saves (two to the same destination). Saved maps must be readable and jobs must succeed. Set `DDNET_SAVE_BENCH_REPEATS` to a positive integer to change repetitions. For comparisons with an older implementation, `DDNET_SAVE_BENCH_NO_OVERLAP=1` omits overlap, and `DDNET_SAVE_BENCH_LEGACY_MEMORY=1` omits the eventual-current diagnostic assertion. Keep settings consistent across a comparison and unset overrides for the normal protocol.

The final command above recomputes distributions from the save run's raw JSON samples and creates a summary per fixture/run with median, nearest-rank p95 and maximum. Repeat `--input LABEL=...` to include other runs; it does not pool repetitions together. Completion frames are annotated separately and counted only once in the combined foreground distribution.

Optionally append `--save-budget optimized` (Release/RelWithDebInfo) or `--save-budget debug` to the **run** command. These enforce the reference p95 limits of 16.7 or 50 ms for foreground phases and a private process peak below 1 GB. Missing private-memory measurements cannot pass. These machine-dependent performance verdicts are separate from functional correctness; save completion itself can still take seconds.

## Comparing results

Record the source revision, compiler/configuration, dependencies, map hash, backend and background activity. Stop competing builds and preserve slow samples. Headless timings include the editor render path and synchronization but exclude native GPU/audio work.

The contributor guide summarizes [performance evidence and limits](../../docs/editor/EDITOR_HISTORY.md#performance-evidence-and-limits), including measured tradeoffs and remaining limitations. Use these tools to generate new evidence for your checkout; exact historical timings are not a portable pass criterion.
