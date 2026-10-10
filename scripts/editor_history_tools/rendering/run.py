#!/usr/bin/env python3
"""Measure matched explicit-zoom editor frames and capture native map pixels in isolated storage."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time

try:
	import psutil
except ImportError:
	psutil = None


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--client", type=Path, required=True)
	parser.add_argument("--fixture", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--backend", choices=("native", "headless"), default="native")
	parser.add_argument("--visual-only", action="store_true")
	parser.add_argument("--memory-diagnostic", action="store_true", help="After timing, record owned bytes and force terminal framebuffer readback")
	parser.add_argument("--samples", type=int, default=50)
	parser.add_argument("--gl-major", type=int, default=3)
	parser.add_argument("--gl-minor", type=int, default=3)
	args = parser.parse_args()
	source, client, fixture, output = (p.resolve() for p in (args.source, args.client, args.fixture, args.output))
	if output.exists() or args.samples < 1:
		parser.error("Use a new --output directory and a positive --samples value")
	(output / "maps").mkdir(parents=True)
	shutil.copy2(fixture, output / "maps/fixture.map")
	(output / "storage.cfg").write_text(f"add_path .\nadd_path {(source / 'data').as_posix()}\n", encoding="utf-8")
	environment = dict(os.environ, SDL_AUDIODRIVER="dummy", RENDER_PROBE_SAMPLES=str(args.samples))
	for key in ("GTEST_FILTER", "RENDER_PROBE_VISUAL_ONLY", "RENDER_PROBE_MEMORY_DIAGNOSTIC", "SDL_VIDEODRIVER"):
		environment.pop(key, None)
	if args.backend == "headless":
		environment["SDL_VIDEODRIVER"] = "dummy"
	if args.visual_only:
		environment["RENDER_PROBE_VISUAL_ONLY"] = "1"
	if args.memory_diagnostic:
		environment["RENDER_PROBE_MEMORY_DIAGNOSTIC"] = "1"
	command = [str(client), "maps/fixture.map", "snd_enable 0", "cl_save_settings 0", "cl_editor 1", "gfx_fullscreen 0", "gfx_screen_width 1920", "gfx_screen_height 1080", "gfx_vsync 0", "gfx_refresh_rate 0", "cl_refresh_rate 0", "cl_refresh_rate_inactive 0", "gfx_backend opengl", f"gfx_gl_major {args.gl_major}", f"gfx_gl_minor {args.gl_minor}"]
	background = psutil.cpu_percent(interval=.5) if psutil else None
	start = time.monotonic()
	peak_resident = peak_private = 0
	startup = None
	if os.name == "nt":
		startup = subprocess.STARTUPINFO()
		startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
		startup.wShowWindow = subprocess.SW_HIDE
	with (output / "stdout.log").open("wb") as stdout, (output / "stderr.log").open("wb") as stderr:
		process = subprocess.Popen(command, cwd=output, env=environment, stdout=stdout, stderr=stderr, startupinfo=startup)
		observed = psutil.Process(process.pid) if psutil else None
		try:
			while process.poll() is None:
				if time.monotonic() - start > 900:
					raise TimeoutError(str(output))
				if observed:
					try:
						memory = observed.memory_info()
						peak_resident = max(peak_resident, getattr(memory, "peak_wset", memory.rss))
						peak_private = max(peak_private, getattr(memory, "peak_pagefile", 0))
					except psutil.NoSuchProcess:
						pass
				time.sleep(.01)
		finally:
			if process.poll() is None:
				process.kill()
				process.wait()
	text = (output / "stdout.log").read_text(encoding="utf-8", errors="replace")
	samples, views, contexts, drivers, memory, owned = {}, [], [], [], [], []
	for line in text.splitlines():
		if line.startswith(("RENDER_SAMPLE ", "RENDER_MEMORY ", "RENDER_VIEW ", "RENDER_CONTEXT ")):
			fields = dict(field.split("=", 1) for field in line.split()[1:])
			if line.startswith("RENDER_SAMPLE "):
				samples.setdefault(fields["zoom"] + "/" + fields["phase"], []).append(float(fields["ms"]))
			elif line.startswith("RENDER_MEMORY "):
				memory.append(fields)
				peak_resident = max(peak_resident, int(fields["peak_resident"]))
				peak_private = max(peak_private, int(fields["peak_private"]))
			elif line.startswith("RENDER_VIEW "):
				views.append(fields)
			else:
				contexts.append(fields)
		elif line.startswith("RENDER_DRIVER "):
			drivers.append(line)
		elif line.startswith("RENDER_OWNED "):
			owned.append(dict(field.split("=", 1) for field in line.split()[1:]))
	images = {}
	if args.backend == "native":
		from PIL import Image
		for zoom in (100, 900):
			path = output / f"scene-{zoom}.png"
			if path.exists():
				with Image.open(path) as picture:
					pixels = picture.convert("RGBA")
					images[str(zoom)] = {"size": pixels.size, "rgba_sha256": hashlib.sha256(pixels.tobytes()).hexdigest(), "png_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "nonblank": len(set(pixels.getdata())) > 32}
	distributions = {phase: {"n": len(values), "median": statistics.median(values), "p95": sorted(values)[math.ceil(.95 * len(values)) - 1], "max": max(values)} for phase, values in samples.items()}
	passed = process.returncode == 0 and "RENDER_PASS" in text and (args.backend == "headless" or len(images) == 2 and all(image["nonblank"] for image in images.values()))
	if not args.visual_only:
		passed = passed and len(distributions) == 18 and all(value["n"] == args.samples for value in distributions.values())
	if args.memory_diagnostic:
		passed = passed and any(value["phase"] == "settled" for value in memory)
	record = {"passed": passed, "exit_code": process.returncode, "backend": args.backend, "gl_requested": [args.gl_major, args.gl_minor], "background_cpu_percent": background, "elapsed_seconds": time.monotonic() - start, "client_sha256": hashlib.sha256(client.read_bytes()).hexdigest(), "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(), "source_manifest_sha256": hashlib.sha256((source / "render-source-manifest.json").read_bytes()).hexdigest(), "probe": json.loads((source / "render-probe.json").read_text()), "process_private_peak": peak_private or None, "process_resident_peak": peak_resident or None, "contexts": contexts, "views": views, "drivers": drivers, "images": images, "samples_ms": samples, "distributions_ms": distributions}
	record.update(memory_diagnostic=args.memory_diagnostic, memory=memory, owned=owned)
	(output / "results.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
	print(json.dumps({key: value for key, value in record.items() if key != "samples_ms"}), flush=True)
	return 0 if passed else 1


if __name__ == "__main__":
	raise SystemExit(main())
