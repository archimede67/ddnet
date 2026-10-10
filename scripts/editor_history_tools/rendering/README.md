# Compare native editor rendering

Use these Windows probes to check tile appearance and rendering responsiveness against the pre-v2 editor. They run actual OpenGL on your GPU in a hidden window, capture map pixels, and measure complete editor frames. Use the [matched headless probes](../matched/README.md) when investigating history costs without native GPU work.

The probe body is checked in as [render_probe.inc](render_probe.inc). Preparation installs it in disposable source copies; it does not change your working editor or require an existing build.

## Prerequisites and inputs

- Windows, an x64 Visual Studio developer PowerShell, Git, CMake, Ninja, Python 3.12+, and the [normal DDNet build dependencies](../../../docs/BUILDING.md).
- Install Pillow with `python -m pip install Pillow`. Optional `python -m pip install psutil` adds sampled process/background-CPU observations; native Windows counters also report process peaks.
- A working OpenGL graphics driver. These native tests cannot use SDL's dummy video driver.
- A checkout containing the pre-v2 baseline Git revision. Fetch the history containing that revision if a shallow clone lacks it.
- An initialized `ddnet-libs` submodule: the revision preparer copies it from your checkout because `git archive` does not include submodule contents. The example intentionally uses these same dependency files for both source versions.

Commands below run from the repository root and use the tracked Tutorial map. For large-map checks, change `$Fixture` to the absolute path of an existing map you supply; Springlobe is not bundled. Use the same map on both sides, with embedded/bundled resources or identical custom resources in each prepared copy; see [external maps and assets](../README.md#external-maps-and-assets).

## Prepare and build

All `build-render-*` source, build and result directories are created by the commands here. Use new source-copy/result names for a repeat experiment. The baseline comes from Git; the candidate copies your current working sources, including uncommitted edits.

```powershell
$BaselineRevision = 'f823d33333723c3577d2b027c8b79f50993d258d'
git cat-file -e "$($BaselineRevision)^{commit}"
if ($LASTEXITCODE -ne 0) { throw 'Fetch the pre-v2 baseline commit before continuing.' }
git submodule update --init --recursive ddnet-libs
python scripts/editor_history_tools/rendering/prepare.py --source . --output build-render-v1-source --revision $BaselineRevision --legacy
python scripts/editor_history_tools/rendering/prepare.py --source . --output build-render-v2-source
foreach ($Version in 'v1','v2') {
  cmake -S "build-render-$Version-source" -B "build-render-$Version" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCLIENT=ON -DSERVER=OFF -DHEADLESS_CLIENT=OFF -DPREFER_BUNDLED_LIBS=ON -DVULKAN=OFF -DVIDEORECORDER=OFF -DUPNP=OFF -DDOWNLOAD_GTEST=OFF
  cmake --build "build-render-$Version" --target game-client --parallel 3
}
```

Use identical compiler, dependency versions, flags and graphics drivers for each pair. GTest is not needed. To check Debug, create another pair of build directories with `-DCMAKE_BUILD_TYPE=Debug` and pass `--configuration debug` to the comparator. Stop builds before timing.

Each prepared copy contains `render-source-manifest.json` (source hashes before instrumentation) and `render-probe.json` (requested revision, legacy mode and probe hash). The preparer replaces the editor factory and enables a hidden window **inside the copies only**. It expects the selected pre-v2/v2 source layout; adapt the probe if that layout changes. Prepare/build a new copy after production changes. The embedded client version string may come from an enclosing checkout; use the explicit revision and manifests to identify measured sources.

## Run and compare

```powershell
$Fixture = (Resolve-Path 'data/maps/Tutorial.map').Path
python scripts/editor_history_tools/rendering/run.py --source build-render-v1-source --client build-render-v1/DDNet.exe --fixture "$Fixture" --output build-render-v1-results
python scripts/editor_history_tools/rendering/run.py --source build-render-v2-source --client build-render-v2/DDNet.exe --fixture "$Fixture" --output build-render-v2-results
python scripts/editor_history_tools/rendering/compare.py --baseline build-render-v1-results/results.json --candidate build-render-v2-results/results.json --configuration optimized --output build-render-comparison.json
```

Run serially. Each runner needs a new output directory and copies the input there, leaving your original map and editor settings unchanged. The comparator writes the explicitly named JSON file; use a new name if you want to preserve an earlier report.

| Generated file | Contents |
| --- | --- |
| Each run's `results.json` | Raw frame samples, distributions, pass status, memory, viewport/driver data and input/executable/source/probe hashes |
| Each run's `stdout.log`, `stderr.log` | Full probe output and failures |
| Each run's `scene-100.png`, `scene-900.png` | Native map captures at the indicated zoom levels |
| Each run's `maps/fixture.map`, `storage.cfg` | Copied map and isolated storage configuration |
| `build-render-comparison.json` | Comparison `passed` flag, failure reasons and baseline/candidate distributions and metadata |

A runner exit of zero means its workload completed and produced expected samples/captures. The comparator makes a separate pixel/performance/memory decision and exits nonzero on failure. Inspect the screenshots as well as the report. Runs have a 900-second timeout; execution errors may leave logs without `results.json`. Inspect those logs and the terminal error.

When sharing results, also retain the full prepared source/probe manifests, compiler/CMake configuration and asset hashes. Record the baseline and candidate Git revisions. If the candidate has uncommitted changes, preserve the patch and any untracked source files (or retain the prepared source copy). Candidate probe metadata says `working-tree`, and the result JSON stores only the source manifest's hash, so the result directory alone cannot reconstruct the measured source.

## What is checked

Captures draw the visible map at zoom 100 and 900, with animation stopped, centered on the first spawn (map center if none), before save/history operations. `ReadFramebuffer` reads GPU pixels without requiring window focus. Full runs retain fifty undo entries and measure zoom 200/900/2000 with ten warmups and fifty samples per phase by default: idle, deterministic pan, edit, undo, redo and reset undo. Values and history size are checked.

Every timed sample includes `OnUpdate`, `OnRender`, `Swap` and backend `WaitForIdle`. Edit phases include the operation. PNG encoding, startup loading, spawn search and history prefill are outside frame timings but inside process peaks. `WaitForIdle` synchronizes the engine render-command thread; it adds no explicit GPU fence. Hidden-window presentation can differ from an interactive window, so these are frame-time measurements, not a prediction of the visible editor's FPS. Audio is disabled.

The comparator requires matching inputs, view, driver and backend metadata; exact native pixels; the candidate's separately measured private/resident process peaks each below 1 GB; and every zoom-900 phase p95 within baseline × 1.10 + 0.2 ms. Where the baseline meets 50 ms Debug or 16.7 ms optimized, the candidate must also meet that bound. Other zooms and maxima are reported without altering the verdict's limits. These are reference performance criteria, not guarantees for every machine. The contributor guide's [performance summary](../../../docs/editor/EDITOR_HISTORY.md#performance-evidence-and-limits) records a private-memory failure; a timing improvement alone is not a complete pass.

Repeat `--baseline` and `--candidate` to pool repetitions. Repetitions on each side must match executable, source, pixels and view. Include all samples from the implementation being evaluated, including slow ones; retain other implementations' runs separately. For broader coverage, repeat the comparison with multiple representative large maps, recording each input's hash and keeping comparisons separate for different maps.

## Additional diagnostics

- **Atlas rendering:** rerun both clients with `--visual-only --gl-major 1 --gl-minor 5` and new output directories. Inspect both captures and compare their `images` RGBA hashes in JSON; the full comparator requires timing samples and therefore cannot accept visual-only runs. The actual driver/context and texture-array capability are recorded.
- **Headless frames:** `--backend headless` requires separately compiled headless probe clients. It cannot verify pixels or pass native comparison.
- **Memory:** `--memory-diagnostic` adds history/cache byte categories and process observations around a terminal framebuffer readback and short idle interval. These categories overlap. This is a separate diagnostic run, not proof that all native allocations belong to history or that readback reclaims them.

The commands above generate screenshots, raw logs and measurement results for your sources and machine. Retain them together with the source and build metadata described above when sharing a comparison.
