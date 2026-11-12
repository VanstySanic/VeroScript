# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check the acceptance runner's failure handling without Android or Gradle."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("veroscript_device_run", Path(__file__).with_name("run.py"))
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class DeviceRunnerTests(unittest.TestCase):
	def report(self):
		return {"failures": 0, "identity": "android-x64/test", "pageSize": 4096,
			"cases": [{"name": f"case-{index}", "passed": True} for index in range(24)]}

	def execute(self, report, code=-1, extra=(), raw=None, build_fails=False):
		with tempfile.TemporaryDirectory(prefix="vero android runner ") as temporary:
			root = Path(temporary)
			project = root / "project"
			apk = project / "build/outputs/apk/release/fixture.apk"
			apk.parent.mkdir(parents=True)
			apk.write_bytes(b"APK fixture; native commands are mocked")
			sdk = root / "sdk"
			suffix = ".exe" if os.name == "nt" else ""
			for tool in (sdk / "platform-tools" / ("adb" + suffix),
				sdk / "build-tools/35.0.0" / ("zipalign" + suffix)):
				tool.parent.mkdir(parents=True, exist_ok=True)
				tool.touch()
			aar = root / "base.aar"
			aar.touch()
			output = root / "results"
			output.mkdir()
			report_path = output / "device-smoke.json"
			report_path.write_text('{"acceptancePassed":true}', encoding="utf-8")
			argv = ["run.py", "--aar", str(aar), "--sdk", str(sdk),
				"--serial", "fixture-serial", "--out", str(output), *extra]
			payload = raw if raw is not None else (
				"INSTRUMENTATION_RESULT: report=" + json.dumps(report) +
				f"\nINSTRUMENTATION_CODE: {code}\n")

			def native_command(arguments, **kwargs):
				if "instrument" in arguments:
					kwargs["stdout"].write(payload.encode("utf-8"))
				return subprocess.CompletedProcess(arguments, 1 if build_fails and "assembleRelease" in arguments else 0)

			with patch.object(RUNNER, "PROJECT", project), \
				patch.object(sys, "argv", argv), \
				patch.object(RUNNER.shutil, "which", return_value="fixture-gradle"), \
				patch.object(RUNNER.subprocess, "run", side_effect=native_command), \
				contextlib.redirect_stdout(io.StringIO()):
				try:
					RUNNER.main()
				finally:
					self.saved_report = json.loads(report_path.read_text(encoding="utf-8"))

	def test_successful_report_is_accepted(self):
		self.execute(self.report())
		self.assertIs(self.saved_report["acceptancePassed"], True)

	def test_failed_build_invalidates_a_previous_success(self):
		with self.assertRaises(RuntimeError):
			self.execute(self.report(), build_fails=True)
		self.assertIs(self.saved_report["acceptancePassed"], False)

	def test_failed_case_is_rejected_even_when_adb_exits_zero(self):
		report = self.report()
		report["cases"][0]["passed"] = False
		with self.assertRaises(RuntimeError):
			self.execute(report)

	def test_failure_count_is_rejected_even_when_cases_pass(self):
		report = self.report()
		report["failures"] = 1
		with self.assertRaises(RuntimeError):
			self.execute(report)

	def test_string_pass_flag_is_rejected(self):
		report = self.report()
		report["cases"][0]["passed"] = "false"
		with self.assertRaises(RuntimeError):
			self.execute(report)

	def test_different_instrumentation_completion_code_is_rejected(self):
		with self.assertRaises(RuntimeError):
			self.execute(self.report(), code=-10)

	def test_missing_report_is_rejected(self):
		with self.assertRaises(RuntimeError):
			self.execute(None, raw="INSTRUMENTATION_FAILED: process crashed\nINSTRUMENTATION_CODE: 0\n")

	def test_wrong_abi_or_page_size_is_rejected(self):
		for extra in (("--abi", "arm64-v8a"), ("--page-size", "16384")):
			with self.subTest(extra=extra), self.assertRaises(RuntimeError):
				self.execute(self.report(), extra=extra)
			self.assertIs(self.saved_report["acceptancePassed"], False)


if __name__ == "__main__":
	unittest.main()
