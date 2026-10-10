#!/usr/bin/env python3
"""Run a prepared editor benchmark, extension or save exercise on local maps.

Optional psutil adds process high-water measurements; C++ allocation counters
come from the benchmark executable itself. Output and fixture files are retained
under a new --output directory for inspection. Input maps are copied, never
overwritten; personal editor settings are not loaded.
"""

from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import statistics
import time

try:
	import psutil
except ImportError:
	psutil = None


def save_acceptance(record, configuration):
	limit = 50.0 if configuration == "debug" else 16.7
	violations = []
	phases = record["save_distributions_ms"]
	if not phases:
		violations.append("No save performance samples")
	for phase, values in phases.items():
		if phase.endswith(("_enqueue", "_frame")) and (not math.isfinite(values["p95"]) or values["p95"] >= limit):
			violations.append(f"{phase}: p95 {values['p95']} ms is not below {limit} ms")
	peak = record["process_private_commit_peak"]
	if not peak:
		violations.append("Private process peak is unavailable")
	elif peak >= 1_000_000_000:
		violations.append(f"Private process peak {peak} bytes is not below 1 GB")
	return {"configuration": configuration, "passed": record["passed"] and not violations, "foreground_p95_limit_ms": limit, "private_peak_limit_bytes": 1_000_000_000, "violations": violations}


def native_peaks(text):
	resident = private = 0
	for line in text.splitlines():
		if not line.startswith("MEMORY "):
			continue
		fields = dict(field.split("=", 1) for field in line.split()[1:])
		resident = max(resident, int(fields.get("process_resident", 0)), int(fields.get("process_peak_resident", 0)))
		private = max(private, int(fields.get("process_private", 0)), int(fields.get("process_peak_private", 0)))
	return resident, private


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--client", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--fixture", action="append")
	parser.add_argument("--map", type=Path, action="append", help="Local external map; copied into the isolated test directory")
	parser.add_argument("--massive", action="store_true", help="Use the massive viewport/50-revision protocol (automatic for external maps)")
	parser.add_argument("--save-budget", choices=("debug", "optimized"), help="Enforce the reference save-frame and 1 GB private-memory budgets; requires save-mode external maps")
	args = parser.parse_args()
	source, client, output = args.source.resolve(), args.client.resolve(), args.output.resolve()
	exercise = json.loads((source / "editor-history-exercise.json").read_text(encoding="utf-8"))
	mode = exercise["mode"]
	if args.save_budget and mode != "save":
		parser.error("--save-budget requires --mode save preparation")
	if output.exists():
		parser.error("--output must be a new directory")
	output.mkdir(parents=True)
	fixtures = (args.fixture or []) + (args.map or [])
	if not fixtures:
		fixtures = ["coverage.map", "Tutorial.map", "Gold Mine.map", "jungle_day.map"]
	expected = json.loads((Path(__file__).resolve().parent / "baseline_export_hashes.json").read_text(encoding="utf-8"))
	results = []
	for index, name in enumerate(fixtures):
		work = output / str(index)
		(work / "maps").mkdir(parents=True)
		external = isinstance(name, Path) or Path(name).is_file()
		if args.save_budget and not external:
			parser.error("--save-budget requires external maps")
		fixture = Path(name).resolve() if external else source / "data" / ("themes" if name == "jungle_day.map" else "maps") / name
		name = str(name)
		shutil.copy2(fixture, work / "maps/fixture.map")
		(work / "storage.cfg").write_text(f"add_path .\nadd_path {(source / 'data').as_posix()}\n", encoding="utf-8")
		massive = mode == "benchmark" and (external or args.massive)
		selection = "CEditorHistoryBenchmark.*" if mode == "benchmark" else "CEditorHistoryRuntime.AuthoritativeFieldAndOrdinaryToolExtensionExercise"
		if mode == "save":
			selection = "CEditorHistorySaveBenchmark.ForegroundLatencyDuringSaves" if external else "CEditorHistorySaveBenchmark.NativeBaselineExportParity"
		if massive:
			selection = "CEditorHistoryMassiveBenchmark.*"
		environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", GTEST_FILTER=selection)
		environment.pop("DDNET_BENCH_DENSE", None)
		environment.pop("DDNET_BENCH_MASSIVE", None)
		if massive:
			environment["DDNET_BENCH_MASSIVE"] = "1"
		if name == "coverage.map":
			environment["DDNET_BENCH_DENSE"] = "1"
		background = psutil.cpu_percent(interval=0.5) if psutil else None
		start = time.perf_counter()
		peak_resident = peak_commit = 0
		with (work / "stdout.log").open("wb") as stdout, (work / "stderr.log").open("wb") as stderr:
			process = subprocess.Popen([str(client), "maps/fixture.map", "snd_enable 0", "cl_save_settings 0", "gfx_fullscreen 0", "cl_editor 1", "stdout_output_level -3"], cwd=work, env=environment, stdout=stdout, stderr=stderr)
			observed = psutil.Process(process.pid) if psutil else None
			try:
				while process.poll() is None:
					if time.perf_counter() - start > 900:
						raise TimeoutError(f"Editor exercise exceeded 900 seconds: {work}")
					if observed:
						try:
							memory = observed.memory_info()
							peak_resident = max(peak_resident, getattr(memory, "peak_wset", memory.rss))
							peak_commit = max(peak_commit, getattr(memory, "peak_pagefile", 0))
						except psutil.NoSuchProcess:
							pass
					time.sleep(0.01)
			finally:
				if process.poll() is None:
					process.kill()
					process.wait()
		text = (work / "stdout.log").read_text(encoding="utf-8", errors="replace")
		native_resident, native_private = native_peaks(text)
		peak_resident = max(peak_resident, native_resident)
		peak_commit = max(peak_commit, native_private)
		samples = {}
		for line in text.splitlines():
			if line.startswith("SAVE_SAMPLE "):
				fields = dict(field.split("=", 1) for field in line.split()[1:])
				samples.setdefault(fields["phase"], []).append(float(fields["milliseconds"]))
		record = {
			"fixture": name,
			"mode": mode,
			"protocol": "massive" if massive else mode,
			"elapsed_seconds": time.perf_counter() - start,
			"exit_code": process.returncode,
			"passed": "editor-history-tests: PASS" in text,
			"background_cpu_percent": background,
			"process_resident_peak": peak_resident or None,
			"process_private_commit_peak": peak_commit or None,
			"save_samples_ms": samples,
			"save_distributions_ms": {phase: {"n": len(values), "median": statistics.median(values), "p95": sorted(values)[math.ceil(.95 * len(values)) - 1], "max": max(values)} for phase, values in samples.items()},
			"client_sha256": hashlib.sha256(client.read_bytes()).hexdigest(),
			"fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
		}
		if args.save_budget:
			record["performance_acceptance"] = save_acceptance(record, args.save_budget)
		normalized = work / "maps/normalized-baseline.map"
		if normalized.exists():
			record["normalized_sha256"] = hashlib.sha256(normalized.read_bytes()).hexdigest()
			reference = next((entry for entry in expected if entry["fixture"] == name), None)
			if reference:
				record["baseline_writer_byte_parity"] = record["normalized_sha256"] == reference["old_sha256"]
		results.append(record)
		(output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
		print(json.dumps({key: value for key, value in record.items() if key != "save_samples_ms"}), flush=True)
		for line in text.splitlines():
			if line.startswith(("BENCH ", "MEMORY ", "FIXTURE ")) or "FAILED" in line:
				print(line, flush=True)
		require_parity = (mode == "benchmark" and not massive) or (mode == "save" and not external)
		if process.returncode or not record["passed"] or (require_parity and record.get("baseline_writer_byte_parity") is not True) or (args.save_budget and not record["performance_acceptance"]["passed"]):
			return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
