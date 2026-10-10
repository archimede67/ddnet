#!/usr/bin/env python3
"""Create an isolated source copy for history benchmarks, extension or save checks.

The original checkout is never patched. Configure/build the printed source copy
with the normal CMake options, HEADLESS_CLIENT=ON, and available GTest libraries.
"""

from pathlib import Path
import argparse
import hashlib
import json
import shutil


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--mode", choices=("benchmark", "extension", "save"), required=True)
	args = parser.parse_args()
	source = args.source.resolve()
	destination = args.output.resolve()
	assets = Path(__file__).resolve().parent
	directories = (".cargo", "cmake", "data", "datasrc", "ddnet-libs", "other", "scripts", "src")
	if destination.exists() or source.is_relative_to(destination):
		parser.error("--output must be a new directory and cannot contain the source checkout")
	if any(destination.is_relative_to(source / name) for name in directories):
		parser.error("--output cannot be inside a directory being copied")
	if not (source / "src/test/editor_runtime/history.cpp").is_file():
		parser.error("--source must contain the registered editor runtime harness")
	if (source / "editor-history-exercise.json").exists():
		parser.error("--source must be an unmodified checkout, not a previously prepared exercise")
	destination.mkdir(parents=True)
	for path in source.iterdir():
		if path.is_file() and path.name != ".git":
			shutil.copy2(path, destination / path.name)
		elif path.name in directories and path.is_dir():
			shutil.copytree(path, destination / path.name)
	manifest = {str(path.relative_to(destination)): hashlib.sha256(path.read_bytes()).hexdigest() for path in (destination / "src").rglob("*") if path.is_file()}
	(destination / "editor-history-source-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
	tests = destination / "src/test/editor_runtime/history.cpp"
	text = tests.read_text(encoding="utf-8")
	marker = "\tclass CTestEditor final : public CEditor"
	if marker not in text:
		raise RuntimeError("Runtime harness layout changed; adapt this exercise before running it")
	if args.mode == "extension":
		values = destination / "src/game/editor/mapitems/document_values.h"
		records = values.read_text(encoding="utf-8")
		anchor = "class CLayerGroupValues : public CDocumentIdentity, public CNamedDocumentValues<12>\n{\npublic:\n"
		if anchor not in records:
			raise RuntimeError("Authoritative record layout changed")
		values.write_text(records.replace(anchor, anchor + "\tint m_ExtensionExerciseValue = 0;\n", 1), encoding="utf-8")
		text = text.replace(marker, (assets / "extension_test.inc").read_text(encoding="utf-8") + "\n" + marker, 1)
	elif args.mode == "save":
		headers = "#include <atomic>\n#include <chrono>\n#include <cstdio>\n#include <cstdlib>\n#define DDNET_BENCH_NO_ALLOCATION_HOOKS\n"
		text = headers + (assets / "process_memory.inc").read_text(encoding="utf-8") + "\n" + text
		text = text.replace(marker, (assets / "save_benchmark.inc").read_text(encoding="utf-8") + "\n" + marker, 1)
		cmake = destination / "CMakeLists.txt"
		cmake.write_text(cmake.read_text(encoding="utf-8") + "\nif(WIN32 AND TARGET editor-testrunner)\n  target_link_libraries(editor-testrunner psapi)\nendif()\n", encoding="utf-8")
	else:
		headers = "#include <chrono>\n#include <cstdio>\n#include <algorithm>\n#include <game/editor/mapitems/document_export.h>\n"
		text = headers + (assets / "allocation_hooks.inc").read_text(encoding="utf-8") + "\n" + (assets / "process_memory.inc").read_text(encoding="utf-8") + "\n" + text
		text = text.replace(marker, (assets / "benchmark_tests.inc").read_text(encoding="utf-8") + "\n" + marker, 1)
		anchor = "\t\t\tgs_pEditor = this;\n"
		if anchor not in text:
			raise RuntimeError("Runtime startup layout changed")
		text = text.replace(anchor, anchor + """
			if(std::getenv("DDNET_BENCH_MASSIVE"))
			{
				const auto Start = std::chrono::steady_clock::now();
				CEditor::OnRender();
				Graphics()->Swap();
				Graphics()->WaitForIdle();
				const auto FirstFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
				std::printf("BENCH phase=startup_load_and_first_frame samples=1 milliseconds=%.6f first_frame_ms=%.6f\\n", benchmark_allocations::g_LoadMilliseconds.load() + FirstFrame, FirstFrame);
				benchmark_allocations::PrintMemory("first_frame");
			}
""", 1)
		map_io = destination / "src/game/editor/mapitems/map_io.cpp"
		io = map_io.read_text(encoding="utf-8")
		anchor = "bool CEditorMap::Load(const char *pFilename, int StorageType, const FErrorHandler &ErrorHandler)\n{"
		if anchor not in io:
			raise RuntimeError("Map load entry point changed")
		preamble = """#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace benchmark_allocations
{
	extern std::atomic<double> g_LoadMilliseconds;
	void PrintMemory(const char *pPhase);
	struct CLoadTimer
	{
		bool m_Enabled;
		std::chrono::steady_clock::time_point m_Start = std::chrono::steady_clock::now();
		~CLoadTimer()
		{
			if(!m_Enabled)
				return;
			const auto Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
			g_LoadMilliseconds.store(Milliseconds);
			std::printf("BENCH phase=startup_load samples=1 milliseconds=%.6f\\n", Milliseconds);
			PrintMemory("loaded");
		}
	};
}
"""
		# Declared before every Load local, so its destructor includes their teardown.
		io = preamble + io.replace(anchor, anchor + '\n\tbenchmark_allocations::CLoadTimer LoadTimer{std::getenv("DDNET_BENCH_MASSIVE") != nullptr && str_comp(pFilename, "maps/fixture.map") == 0};', 1)
		map_io.write_text(io, encoding="utf-8")
		cmake = destination / "CMakeLists.txt"
		cmake.write_text(cmake.read_text(encoding="utf-8") + "\nif(WIN32 AND TARGET editor-testrunner)\n  target_link_libraries(editor-testrunner psapi)\nendif()\n", encoding="utf-8")
		capture = destination / "src/game/editor/mapitems/document_graph.cpp"
		graph = capture.read_text(encoding="utf-8")
		anchor = "std::optional<CEditorDocumentValues> CEditorMap::CaptureDocument(const CEditorDocumentValues *pPrevious, std::string &Error) const\n{"
		if anchor not in graph:
			raise RuntimeError("Capture entry point changed")
		graph = "#include <atomic>\n#include <cstddef>\nnamespace benchmark_allocations { extern std::atomic<std::size_t> g_Captures; }\n" + graph.replace(anchor, anchor + "\n\tbenchmark_allocations::g_Captures.fetch_add(1, std::memory_order_relaxed);", 1)
		capture.write_text(graph, encoding="utf-8")
	anchor = 'log_info("editor-history-tests", "%s", Result == 0 && ::testing::UnitTest::GetInstance()->test_to_run_count() > 0 ? "PASS" : "FAIL");'
	if anchor not in text:
		raise RuntimeError("Runtime completion marker changed")
	text = "#include <cstdio>\n" + text.replace(anchor, 'std::printf("editor-history-tests: %s\\n", Result == 0 && ::testing::UnitTest::GetInstance()->test_to_run_count() > 0 ? "PASS" : "FAIL");', 1)
	tests.write_text(text, encoding="utf-8")
	(destination / "editor-history-exercise.json").write_text(json.dumps({"mode": args.mode, "original": str(source)}, indent=2), encoding="utf-8")
	print(destination)


if __name__ == "__main__":
	main()
