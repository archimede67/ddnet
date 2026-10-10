import copy
import unittest

from compare import compare, distributions, pool


def fixture(ms=10):
	return {"passed": True, "backend": "native", "fixture_sha256": "fixture", "gl_requested": [3, 3], "contexts": [{"width": 1920, "height": 1080, "center_x": 0, "center_y": 0, "texture_arrays": 1}], "views": [{"zoom": str(zoom), "world_zoom": zoom / 100, "left": 0, "top": 0, "right": zoom, "bottom": zoom} for zoom in (200, 900, 2000)], "drivers": ["same GPU"], "images": {zoom: {"size": [1920, 1080], "rgba_sha256": zoom, "nonblank": True} for zoom in ("100", "900")}, "process_private_peak": 900_000_000, "process_resident_peak": 500_000_000, "samples_ms": {f"{zoom}/{phase}": [ms] * 50 for zoom in (200, 900, 2000) for phase in ("idle", "pan", "edit_frame", "undo_frame", "redo_frame", "reset_undo_frame")}}


class ComparisonTests(unittest.TestCase):
	def test_pool_uses_all_samples_and_rejects_different_sources(self):
		left, right = fixture(10), fixture(30)
		for record in (left, right):
			record.update(client_sha256="binary", source_manifest_sha256="sources", probe="probe")
		combined = pool([left, right])
		self.assertEqual(distributions(combined)["900/idle"]["n"], 100)
		self.assertEqual(distributions(combined)["900/idle"]["p95"], 30)
		self.assertEqual(len(left["samples_ms"]["900/idle"]), 50)
		right["source_manifest_sha256"] = "different"
		with self.assertRaises(ValueError):
			pool([left, right])

	def test_exact_matched_run_passes(self):
		self.assertTrue(compare(fixture(), fixture(), 50)["passed"])

	def test_rejects_each_material_verification_gap(self):
		for mutate in (
			lambda c: c.update(backend="headless"),
			lambda c: c.update(views=["wrong zoom"]),
			lambda c: c.update(process_private_peak=None),
			lambda c: c.update(process_private_peak=1_000_000_000),
			lambda c: c["images"]["100"].update(rgba_sha256="wrong tile rotation"),
			lambda c: c["samples_ms"].pop("900/idle"),
			lambda c: c["samples_ms"].update({"900/idle": []}),
			lambda c: c["samples_ms"].update({"900/redo_frame": [12] * 50}),
			lambda c: c["samples_ms"].update({"900/pan": [float("nan")] * 50}),
		):
			candidate = copy.deepcopy(fixture())
			mutate(candidate)
			self.assertFalse(compare(fixture(), candidate, 50)["passed"])

	def test_matching_empty_or_incomplete_metadata_cannot_pass(self):
		for field, value in (("contexts", []), ("contexts", [{}]), ("drivers", []), ("drivers", [" "]), ("views", []), ("views", [{"zoom": "900"}]), ("views", fixture()["views"][:2])):
			with self.subTest(field=field, value=value):
				baseline, candidate = fixture(), fixture()
				baseline[field] = candidate[field] = value
				self.assertFalse(compare(baseline, candidate, 50)["passed"])

	def test_each_process_peak_must_be_measured_and_strictly_below_limit(self):
		for kind in ("private", "resident"):
			for peak in (None, 0, -1, 1_000_000_000, 1_000_000_001, float("nan")):
				with self.subTest(kind=kind, peak=peak):
					candidate = fixture()
					candidate[f"process_{kind}_peak"] = peak
					self.assertFalse(compare(fixture(), candidate, 50)["passed"])
			candidate = fixture()
			candidate[f"process_{kind}_peak"] = 999_999_999
			self.assertTrue(compare(fixture(), candidate, 50)["passed"])

	def test_absolute_budget_is_retained_when_baseline_meets_it(self):
		self.assertFalse(compare(fixture(49), fixture(51), 50)["passed"])
		self.assertFalse(compare(fixture(16), fixture(17), 16.7)["passed"])
		for budget in (50, 16.7):
			self.assertTrue(compare(fixture(budget), fixture(budget), budget)["passed"])
			self.assertFalse(compare(fixture(budget), fixture(budget + .001), budget)["passed"])


if __name__ == "__main__":
	unittest.main()
