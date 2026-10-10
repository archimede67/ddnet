# Compare pre-v2 and v2 history timing

These Windows tools compare map loading and complete edit/undo/redo frames between the previous action-based history and the current document history. Use them to investigate performance regressions on large maps. They run the editor with a headless backend, without the allocation hooks used by the [main benchmark](../README.md).

Everything installed by the preparer is checked in under [v1](v1/) and [v2](v2/); [probe-manifest.json](probe-manifest.json) records the probe hashes. No existing instrumented checkout, executable or measurement directory is required.

## Prerequisites and inputs

- Windows, an x64 Visual Studio developer PowerShell, Git, CMake, Ninja, Python 3.10+ and the [normal DDNet dependencies](../../../docs/BUILDING.md).
- Install the runner dependency with `python -m pip install psutil`. Its memory readings use Windows-specific counters.
- A Git checkout containing both the pre-v2 baseline commit and the candidate commit. A shallow clone may need more history fetched from the remote that contains those commits. The checks below must succeed before preparation.
- A map containing a game layer and an ordinary tile layer. The example uses the tracked Tutorial map so it runs without a download. For massive-map measurements, set `$Fixture` to your own existing map's absolute path. Use identical bytes on both sides. Maps must use embedded/bundled resources, or you must supply matching custom assets as described in [external maps and assets](../README.md#external-maps-and-assets).

## Prepare and build

Run from the repository root. This example measures committed `HEAD`; commit your candidate changes first if you want them included. All `build-matched-*` directories below are newly created by these commands. Choose unused names when repeating preparation.

```powershell
$BaselineRevision = 'f823d33333723c3577d2b027c8b79f50993d258d'
$CandidateRevision = (git rev-parse HEAD).Trim()
git cat-file -e "$($BaselineRevision)^{commit}"
if ($LASTEXITCODE -ne 0) { throw 'Fetch the pre-v2 baseline commit before continuing.' }
git cat-file -e "$($CandidateRevision)^{commit}"
if ($LASTEXITCODE -ne 0) { throw 'Candidate commit is unavailable.' }

git worktree add --detach build-matched-v1-src $BaselineRevision
git worktree add --detach build-matched-v2-src $CandidateRevision
git -C build-matched-v1-src submodule update --init --recursive ddnet-libs
git -C build-matched-v2-src submodule update --init --recursive ddnet-libs
python scripts/editor_history_tools/matched/prepare-matched-profile.py --source build-matched-v1-src --legacy
python scripts/editor_history_tools/matched/prepare-matched-profile.py --source build-matched-v2-src
foreach ($Version in 'v1','v2') {
  cmake -S "build-matched-$Version-src" -B "build-matched-$Version" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHEADLESS_CLIENT=ON -DCLIENT=ON -DSERVER=OFF -DDEV=ON -DPREFER_BUNDLED_LIBS=ON -DVIDEORECORDER=OFF -DVULKAN=OFF -DUPNP=OFF -DDOWNLOAD_GTEST=OFF
  cmake --build "build-matched-$Version" --target game-client --parallel 3
}
```

Unlike the main preparer, `prepare-matched-profile.py` **modifies its `--source` in place**. Only pass the disposable worktrees created above, never your working checkout. It installs the appropriate editor factory/probe and a load timer, then writes `matched-profile-manifest.json` with hashes of the instrumented files. It expects the corresponding pre-v2/v2 source layout; an incompatible future layout requires updating the probe. Do not rerun it on already instrumented sources.

Use identical compiler/dependency versions and build flags for both clients. GTest is not needed for this `game-client` protocol. To measure Debug, build another matching pair with `-DCMAKE_BUILD_TYPE=Debug` and distinct build directory names. Stop builds before collecting timings.

## Run one pair

```powershell
$Fixture = (Resolve-Path 'data/maps/Tutorial.map').Path
$env:PERF_PREFILL50 = '1'
Remove-Item Env:PERF_LOAD_ONLY,Env:PERF_HISTORY_PANEL,Env:PERF_ACCUMULATE_HISTORY -ErrorAction SilentlyContinue
python scripts/editor_history_tools/matched/run-matched-profile.py --source build-matched-v1-src --client build-matched-v1/DDNet.exe --fixture "$Fixture" --output build-matched-runs/v1-optimized-tutorial-default-1
python scripts/editor_history_tools/matched/run-matched-profile.py --source build-matched-v2-src --client build-matched-v2/DDNet.exe --fixture "$Fixture" --output build-matched-runs/v2-optimized-tutorial-default-1
Remove-Item Env:PERF_PREFILL50 -ErrorAction SilentlyContinue
```

Each run requires a new output directory and copies the map into its own `maps/fixture.map`. Its generated `storage.cfg` uses that directory and the specified source's `data/`; your original map and editor settings are unchanged. A nonzero exit indicates a workload or execution failure. Successful completion is not an automatic performance verdict. The default timeout is 600 seconds; pass `--timeout SECONDS` to change it. A timeout or startup error can leave logs without `results.json`; inspect the terminal error and those logs.

| Generated file in each run directory | Purpose |
| --- | --- |
| `results.json` | Raw phase samples, distributions, memory, fixture/executable hashes and pass/exit status |
| `stdout.log`, `stderr.log` | Full probe output and failures |
| `maps/fixture.map`, `storage.cfg` | Isolated test input and storage configuration |

For other workloads, set these variables **before both runs** and use new output names:

| Workload | Environment |
| --- | --- |
| Default view with fifty entries | `PERF_PREFILL50=1`; unset the other three variables below |
| History panel open with fifty entries | `PERF_PREFILL50=1`, `PERF_HISTORY_PANEL=1`; unset `PERF_LOAD_ONLY` and `PERF_ACCUMULATE_HISTORY` |
| Load and first frame only | `PERF_LOAD_ONLY=1`; unset `PERF_PREFILL50`, `PERF_HISTORY_PANEL` and `PERF_ACCUMULATE_HISTORY` |

Unset these variables after the experiment. Do not enable `PERF_ACCUMULATE_HISTORY` for this protocol. Record background CPU activity separately; this runner measures memory but does not sample background CPU for you.

## Pool the results

Create `build-matched-runs/metadata.json` yourself with the following contents for the example pair. `name` is the existing run directory relative to `--root`; `variant` is a label you choose for the fixture, not a file path.

```json
[
  {"name":"v1-optimized-tutorial-default-1","version":"v1","config":"optimized","variant":"tutorial","history_panel":false,"load_only":false,"prefilled_entries":50},
  {"name":"v2-optimized-tutorial-default-1","version":"v2","config":"optimized","variant":"tutorial","history_panel":false,"load_only":false,"prefilled_entries":50}
]
```

```powershell
python scripts/editor_history_tools/matched/aggregate-matched-profile.py --root build-matched-runs --metadata build-matched-runs/metadata.json --output build-matched-pooled
```

This creates `distributions.csv` and `runs.json` in a new output directory. The CSV contains pooled median, nearest-rank p95 and maximum per version/configuration/variant/view/layer/phase. The JSON retains per-run loading, memory, hashes and metadata. Keep each original run directory to retain raw samples, together with the prepared manifest, source revisions, asset hashes and CMake configuration. For larger batches, include every successful run in the metadata, use `config: "debug"` for Debug and `history_panel: true` for panel runs. Load-only records use `load_only: true, prefilled_entries: 0`; include operation runs too because the CSV requires operation samples.

## Measurement protocol and limits

The default and panel workloads prefill fifty undo entries before warmup: v1 has fifty actions; v2 has those entries plus its baseline (51 roots). Each layer receives five warmups and fifty measured edit/undo/redo sequences. The game and largest ordinary layer are selected identically, values and timeline size are checked, and an untimed undo resets each sample. Frames execute `OnRender`, `Swap` and `WaitForIdle` on the headless backend, excluding native GPU/audio costs.

Aggregation discards only warmup samples 0–4 and pools the remaining samples, never the per-run percentiles. History time is begin + commit; complete edit adds write and following render; complete undo/redo includes traversal and following render. The main assertion-heavy massive benchmark retains exactly fifty **roots**, so its numbers are a different protocol.

For broader coverage, choose two representative large maps. For each map and source version, run three optimized repetitions of load-only, default view and history-panel view, plus one Debug operation run for each view (44 invocations total). Record each map's hash and use the same bytes on both sides. Reverse version order in the second optimized repetition. Compute optimized load comparisons from the median of the three load-only runs. Keep all slow samples and compare the same inputs, retained history and build configuration. The contributor guide summarizes [performance evidence and limits](../../../docs/editor/EDITOR_HISTORY.md#performance-evidence-and-limits); these runs generate evidence for your own inputs and machine.
