#!/usr/bin/env python3
"""Run the registered editor adapter tests in isolated storage directories.

Configure with CLIENT=ON, HEADLESS_CLIENT=ON and GTest, then build
run_editor_tests. GTEST_FILTER selects adapter cases; --fixture overrides
bundled fixtures. No production editor contains a test trigger.
"""

from pathlib import Path
import argparse
import base64
import os
import shutil
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--client", type=Path, required=True)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--fixture", type=Path, action="append")
	args = parser.parse_args()
	source = args.source.resolve()
	client = args.client.resolve()
	fixtures = args.fixture or [source / "data/maps/Gold Mine.map", source / "data/maps/Tutorial.map", source / "data/themes/jungle_day.map"]
	for fixture in fixtures:
		fixture = fixture.resolve()
		with tempfile.TemporaryDirectory(prefix="ddnet-editor-history-") as directory:
			work = Path(directory)
			(work / "maps").mkdir()
			shutil.copyfile(fixture, work / "maps/fixture.map")
			shutil.copyfile(fixture, work / "maps/append-input.map")
			(work / "mapres").mkdir()
			for name in ("retained-input", "preparation-original", "preparation-replace", "preparation-add", "automatic-input", "automatic-replace"):
				shutil.copyfile(source / "data/editor/cursor.png", work / f"mapres/{name}.png")
			# Self-generated Ogg Opus mono silence, 48 kHz / 6 kbit/s, 40 and 80 ms.
			# The runner needs no encoder or external fixture dependency.
			for name, encoded in (
				("sound-original", "T2dnUwACAAAAAAAAAAAWxKxvAAAAANHTLDkBE09wdXNIZWFkAQE4AYC7AAAAAABPZ2dTAAAAAAAAAAAAABbErG8BAAAAbjT28wE9T3B1c1RhZ3MMAAAATGF2ZjYxLjcuMTAwAQAAAB0AAABlbmNvZGVyPUxhdmM2MS4xOS4xMDAgbGlib3B1c09nZ1MABLgIAAAAAAAAFsSsbwIAAABILqNgAwcGBggL5jsjq2AICKyzDsYICKyzDsY="),
				(
					"sound-replace",
					"T2dnUwACAAAAAAAAAABvQVkiAAAAAPJjZWcBE09wdXNIZWFkAQE4AYC7AAAAAABPZ2dTAAAAAAAAAAAAAG9BWSIBAAAAGH6kOwE9T3B1c1RhZ3MMAAAATGF2ZjYxLjcuMTAwAQAAAB0AAABlbmNvZGVyPUxhdmM2MS4xOS4xMDAgbGlib3B1c09nZ1MABDgQAAAAAAAAb0FZIgIAAADKQ9geBQcGBgYGCAvmOyOrYAgIrLMOxggIrLMOxggIrLMOxggIrLMOxg==",
				),
			):
				(work / f"mapres/{name}.opus").write_bytes(base64.b64decode(encoded))
			(work / "editor/automap").mkdir(parents=True)
			(work / "editor/automap/automatic-input.rules").write_text("[Retained]\nIndex 7\nPos 0 0 INDEX 1\n", encoding="utf-8")
			(work / "storage.cfg").write_text(f"add_path .\nadd_path {source / 'data'}\n", encoding="utf-8")
			environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
			# ImageCopy exercises a real impossible malloc; ASan must return nullptr.
			environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":allocator_may_return_null=1"
			process = subprocess.run([str(client), "maps/fixture.map", "snd_enable 0", "cl_save_settings 0", "gfx_fullscreen 0", "cl_editor 1"], cwd=work, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=300, check=False)
			print(f"\nEditor fixture: {fixture.name}", flush=True)
			print(process.stdout, end="", flush=True)
			if process.returncode or "editor-history-tests: PASS" not in process.stdout:
				print(f"Editor test process exited with code {process.returncode}", flush=True)
				return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
