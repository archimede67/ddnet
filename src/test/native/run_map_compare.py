#!/usr/bin/env python3
"""Run the opt-in real editor test client with isolated storage and an assertion oracle."""
import pathlib
import subprocess
import sys
import tempfile


def main():
    executable, source, output = (pathlib.Path(arg).resolve() for arg in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    # Preserve each run's report/screenshots without using the user's DDNet profile.
    run = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=output))
    save = run / "save"
    (save / "screenshots").mkdir(parents=True)
    (run / "storage.cfg").write_text(
        f"add_path {save.as_posix()}\nadd_path {(source / 'data').as_posix()}\n",
        encoding="utf-8",
    )
    settings = "\n".join([
        "cl_editor 1", "cl_show_welcome 0", "cl_skip_start_menu 1",
        "ed_autosave_interval 0", "snd_enable 0", "gfx_fullscreen 0",
        "gfx_screen_width 1280", "gfx_screen_height 800", "gfx_refresh_rate 60",
        "cl_refresh_rate 60", "gfx_gl_major 3", "gfx_gl_minor 3", "gfx_backgroundrender 1",
        "cl_languagefile \"\"", "logfile \"native-client.log\"",
    ]) + "\n"
    (save / "settings_ddnet.cfg").write_text(settings, encoding="utf-8")
    print(f"Native map-comparison evidence: {run}", flush=True)
    with (run / "process.log").open("w", encoding="utf-8") as log:
        try:
            process = subprocess.run(
                [str(executable), "maps/coverage.map"], cwd=run, stdout=log,
                stderr=subprocess.STDOUT, timeout=600, check=False,
            )
        except subprocess.TimeoutExpired:
            print("FAIL native editor timed out; a working desktop graphics session is required.")
            return 1
    report_path = run / "map-compare-native-results.log"
    report = report_path.read_text(encoding="utf-8") if report_path.exists() else ""
    for line in report.splitlines():
        if line.startswith(("FAIL", "TIMING", "RESULT")):
            print(line)
    if process.returncode or "RESULT failures=0 " not in report or "FAIL " in report:
        print(f"FAIL native editor verification (exit {process.returncode}); see {run}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
