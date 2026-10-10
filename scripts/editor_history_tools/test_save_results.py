#!/usr/bin/env python3
"""Regression checks for reference save budgets and native memory fallback."""

import unittest

from run import native_peaks, save_acceptance


class SaveResults(unittest.TestCase):
	def record(self, p95=1.0, peak=300_000_000, passed=True):
		return {"passed": passed, "process_private_commit_peak": peak, "save_distributions_ms": {"manual_enqueue": {"p95": p95}, "manual_edit_frame": {"p95": p95}, "manual_total": {"p95": 20_000}}}

	def test_foreground_boundary_is_strict_and_disk_duration_is_separate(self):
		self.assertTrue(save_acceptance(self.record(), "optimized")["passed"])
		for configuration, limit in (("debug", 50.0), ("optimized", 16.7)):
			self.assertTrue(save_acceptance(self.record(p95=limit - .001), configuration)["passed"])
			self.assertFalse(save_acceptance(self.record(p95=limit), configuration)["passed"])
			self.assertFalse(save_acceptance(self.record(p95=float("nan")), configuration)["passed"])

	def test_missing_memory_excess_memory_or_failed_export_cannot_pass(self):
		for peak in (None, 0, 1_000_000_000, 1_100_000_000):
			self.assertFalse(save_acceptance(self.record(peak=peak), "debug")["passed"])
		self.assertFalse(save_acceptance(self.record(passed=False), "debug")["passed"])
		record = self.record()
		record["save_distributions_ms"] = {}
		self.assertFalse(save_acceptance(record, "debug")["passed"])

	def test_native_high_water_survives_missing_psutil_and_later_lower_samples(self):
		text = "MEMORY phase=save_ready process_resident=100 process_private=90 process_peak_resident=300 process_peak_private=400\nMEMORY phase=save_settled process_resident=110 process_private=95 process_peak_resident=250 process_peak_private=350\n"
		self.assertEqual(native_peaks(text), (300, 400))
		self.assertEqual(native_peaks("MEMORY phase=linux process_resident=100 process_private=0\n"), (100, 0))
		self.assertEqual(native_peaks("no measurements"), (0, 0))


if __name__ == "__main__":
	unittest.main()
