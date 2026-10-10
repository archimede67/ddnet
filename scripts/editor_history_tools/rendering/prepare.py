#!/usr/bin/env python3
"""Prepare an isolated revision for matching native/headless editor rendering probes."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--revision", help="Local git revision; omit to copy current working sources")
	parser.add_argument("--legacy", action="store_true")
	args = parser.parse_args()
	source, output = args.source.resolve(), args.output.resolve()
	if output.exists() or source.is_relative_to(output):
		parser.error("--output must be a new directory outside copied source subdirectories")
	directories = (".cargo", "cmake", "data", "datasrc", "ddnet-libs", "other", "scripts", "src")
	if any(output.is_relative_to(source / name) for name in directories):
		parser.error("--output cannot be inside a copied source directory")
	output.mkdir(parents=True)
	if args.revision:
		revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", args.revision], text=True).strip()
		archive = subprocess.check_output(["git", "-C", str(source), "archive", revision])
		with tarfile.open(fileobj=io.BytesIO(archive)) as contents:
			contents.extractall(output, filter="data")
		shutil.copytree(source / "ddnet-libs", output / "ddnet-libs", dirs_exist_ok=True)
	else:
		revision = "working-tree"
		for path in source.iterdir():
			if path.is_file() and path.name != ".git":
				shutil.copy2(path, output / path.name)
			elif path.is_dir() and path.name in directories:
				shutil.copytree(path, output / path.name)
	manifest = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (output / "src").rglob("*") if path.is_file()}
	(output / "render-source-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
	assets = Path(__file__).resolve().parent
	probe = (assets / "render_probe.inc").read_text(encoding="utf-8")
	if args.legacy:
		probe = "#define RENDER_PROBE_LEGACY\n" + probe
		factory = output / "src/game/editor/editor.cpp"
		text = factory.read_text(encoding="utf-8")
		anchor = "IEditor *CreateEditor() { return new CEditor; }"
		assert anchor in text
		factory.write_text(text.replace(anchor, '#include "render_probe.inc"', 1), encoding="utf-8")
	else:
		(output / "src/game/editor/editor_factory.cpp").write_text('#include "editor.h"\n#include "render_probe.inc"\n', encoding="utf-8")
	(output / "src/game/editor/render_probe.inc").write_text(probe, encoding="utf-8")
	# Native GPU rendering in a hidden window avoids disturbing the user's apps.
	backend = output / "src/engine/client/backend_sdl.cpp"
	text = backend.read_text(encoding="utf-8")
	anchor = "\tm_pWindow = SDL_CreateWindow("
	assert anchor in text
	backend.write_text(text.replace(anchor, "\tSdlFlags |= SDL_WINDOW_HIDDEN;\n" + anchor, 1), encoding="utf-8")
	(output / "render-probe.json").write_text(json.dumps({"revision": revision, "legacy": args.legacy, "probe_sha256": hashlib.sha256(probe.encode()).hexdigest()}, indent=2), encoding="utf-8")
	print(output)


if __name__ == "__main__":
	main()
