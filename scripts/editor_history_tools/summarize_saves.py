#!/usr/bin/env python3
"""Summarize saved raw save-benchmark results without dropping frame samples."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


def summarize(values):
	ordered = sorted(values)
	return {
		"n": len(ordered),
		"median": statistics.median(ordered),
		"p95": ordered[math.ceil(.95 * len(ordered)) - 1],
		"max": ordered[-1],
	}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--input", action="append", required=True, help="LABEL=path/to/results.json; repeat for each configuration/run")
	parser.add_argument("--output", type=Path, required=True)
	args = parser.parse_args()
	results = []
	for entry in args.input:
		label, filename = entry.split("=", 1)
		path = Path(filename)
		for record in json.loads(path.read_text(encoding="utf-8")):
			samples = record.get("save_samples_ms", {})
			if not samples:
				continue
			foreground = [value for phase, values in samples.items() if phase.endswith("_frame") and not phase.endswith("_completion_frame") for value in values]
			results.append({
				"label": label,
				"raw_results": path.as_posix(),
				"raw_results_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
				"fixture_sha256": record["fixture_sha256"],
				"client_sha256": record["client_sha256"],
				"passed": record["passed"],
				"performance_acceptance": record.get("performance_acceptance"),
				"background_cpu_percent": record["background_cpu_percent"],
				"process_resident_peak": record["process_resident_peak"],
				"process_private_commit_peak": record["process_private_commit_peak"],
				"distributions_ms": {phase: summarize(values) for phase, values in samples.items()},
				"all_foreground_frames_ms": summarize(foreground),
			})
	args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
	print(f"Summarized {len(results)} fixture runs into {args.output}")


if __name__ == "__main__":
	main()
