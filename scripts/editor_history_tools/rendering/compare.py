#!/usr/bin/env python3
"""Check matched native captures and zoom-900 performance against pre-v2."""

import argparse
import copy
import json
import math
from pathlib import Path
import statistics


def distributions(record):
	return {phase: {"n": len(values), "median": statistics.median(values), "p95": sorted(values)[math.ceil(.95 * len(values)) - 1], "max": max(values)} for phase, values in record["samples_ms"].items()}


def pool(records):
	"""Pool every sample from repetitions of the same executable and view."""
	result = copy.deepcopy(records[0])
	for record in records[1:]:
		for key in ("backend", "fixture_sha256", "client_sha256", "source_manifest_sha256", "probe", "gl_requested", "contexts", "views", "drivers", "images"):
			if record[key] != result[key]:
				raise ValueError(f"Repetitions differ in {key}")
		if record["samples_ms"].keys() != result["samples_ms"].keys():
			raise ValueError("Repetitions differ in phases")
		for phase, values in record["samples_ms"].items():
			result["samples_ms"][phase].extend(values)
	result["passed"] = all(record["passed"] for record in records)
	for key in ("process_private_peak", "process_resident_peak"):
		result[key] = max(record[key] for record in records) if all(record[key] for record in records) else None
	result["runs"] = [{key: value for key, value in record.items() if key not in ("samples_ms", "distributions_ms")} for record in records]
	return result


def compare(baseline, candidate, budget):
	errors = []
	expected = {f"{zoom}/{phase}" for zoom in (200, 900, 2000) for phase in ("idle", "pan", "edit_frame", "undo_frame", "redo_frame", "reset_undo_frame")}
	for name, record in (("baseline", baseline), ("candidate", candidate)):
		if not record["passed"] or record["backend"] != "native":
			errors.append(f"{name}: successful native run required")
		if set(record["samples_ms"]) != expected or any(len(values) < 50 or any(not math.isfinite(value) or value < 0 for value in values) for values in record["samples_ms"].values()):
			errors.append(f"{name}: all 18 phases require at least 50 finite samples")
		contexts = record.get("contexts", [])
		context_fields = {"width", "height", "center_x", "center_y", "texture_arrays"}
		if not contexts or any(not isinstance(context, dict) or not context_fields <= context.keys() or any(context[field] is None or not str(context[field]).strip() for field in context_fields) for context in contexts):
			errors.append(f"{name}: populated rendering contexts required")
		drivers = record.get("drivers", [])
		if not drivers or any(not isinstance(driver, str) or not driver.strip() for driver in drivers):
			errors.append(f"{name}: populated native drivers required")
		views = record.get("views", [])
		view_fields = {"zoom", "world_zoom", "left", "top", "right", "bottom"}
		if not views or any(not isinstance(view, dict) or not view_fields <= view.keys() or any(view[field] is None or not str(view[field]).strip() for field in view_fields) for view in views) or {str(view["zoom"]) for view in views} != {"200", "900", "2000"}:
			errors.append(f"{name}: populated views at zoom 200, 900 and 2000 required")
	for key in ("fixture_sha256", "gl_requested", "contexts", "views", "drivers"):
		if baseline.get(key) != candidate.get(key):
			errors.append(f"unmatched {key}")
	for zoom in ("100", "900"):
		left, right = baseline["images"].get(zoom), candidate["images"].get(zoom)
		if not left or not right or not left["nonblank"] or not right["nonblank"] or left["size"] != right["size"] or left["rgba_sha256"] != right["rgba_sha256"]:
			errors.append(f"zoom {zoom}: native pixels differ")
	for kind in ("private", "resident"):
		peak = candidate.get(f"process_{kind}_peak")
		if not isinstance(peak, (int, float)) or not 0 < peak < 1_000_000_000:
			errors.append(f"candidate: {kind} process peak must be measured and below 1 GB")
	left, right = (distributions(record) if all(record["samples_ms"].values()) else {} for record in (baseline, candidate))
	for phase, values in left.items():
		if phase.startswith("900/"):
			limit = values["p95"] * 1.1 + .2
			if values["p95"] <= budget:
				limit = min(limit, budget)
			if phase not in right or right[phase]["p95"] > limit:
				errors.append(f"{phase}: p95 exceeds {limit:.6f} ms")
	return {"passed": not errors, "errors": errors, "baseline_ms": left, "candidate_ms": right}


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--baseline", type=Path, action="append", required=True)
	parser.add_argument("--candidate", type=Path, action="append", required=True)
	parser.add_argument("--configuration", choices=("debug", "optimized"), required=True)
	parser.add_argument("--output", type=Path, required=True)
	args = parser.parse_args()
	baseline, candidate = (pool([json.loads(path.read_text()) for path in paths]) for paths in (args.baseline, args.candidate))
	report = compare(baseline, candidate, 50 if args.configuration == "debug" else 16.7)
	report["configuration"] = args.configuration
	for label, record in (("baseline", baseline), ("candidate", candidate)):
		report[label] = {key: value for key, value in record.items() if key not in ("samples_ms", "distributions_ms")}
	args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
	print(json.dumps({"passed": report["passed"], "errors": report["errors"]}))
	return 0 if report["passed"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
