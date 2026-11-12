# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build an AAR consumer and run its checks on one explicitly selected Android target."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


PACKAGE = "com.vanstysanic.veroscript.devicesmoke"
PROJECT = Path(__file__).resolve().parent


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--aar", type=Path, required=True)
	parser.add_argument("--jit-aar", type=Path)
	parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT"), required=not (os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")))
	parser.add_argument("--gradle", default="gradle", help="Gradle 8.11.1 executable")
	parser.add_argument("--java-home", type=Path, help="JDK 17 home; otherwise use the current JAVA_HOME")
	parser.add_argument("--serial", required=True, help="Explicit adb device/emulator serial")
	parser.add_argument("--abi", choices=("arm64-v8a", "x86_64"), help="Force one APK ABI during installation")
	parser.add_argument("--page-size", type=int, choices=(4096, 16384), help="Require this native sysconf page size")
	parser.add_argument("--debug", action="store_true", help="Use debug; the default release build exercises R8")
	parser.add_argument("--out", type=Path, required=True, help="Directory for the APK, logs and JSON report")
	args = parser.parse_args()
	output = args.out.resolve()
	output.mkdir(parents=True, exist_ok=True)
	qualification = {"acceptancePassed": False, "requestedAbi": args.abi,
		"requestedPageSize": args.page_size, "requestedJit": bool(args.jit_aar)}

	def save_report(report):
		(output / "device-smoke.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

	save_report(qualification)
	for archive in (args.aar, args.jit_aar):
		if archive is not None and not archive.is_file():
			parser.error(f"Missing AAR: {archive}")
	gradle = shutil.which(args.gradle)
	if not gradle:
		parser.error(f"Cannot find Gradle 8.11.1: {args.gradle}")
	sdk = args.sdk.resolve()
	suffix = ".exe" if os.name == "nt" else ""
	adb = sdk / "platform-tools" / ("adb" + suffix)
	zipalign = sdk / "build-tools/35.0.0" / ("zipalign" + suffix)
	for tool in (adb, zipalign):
		if not tool.is_file():
			parser.error(f"Missing Android SDK tool: {tool}")
	env = os.environ.copy()
	env["ANDROID_HOME"] = str(sdk)
	if args.java_home:
		env["JAVA_HOME"] = str(args.java_home.resolve())
	commands = []

	def command(name: str, arguments: list[str | Path], timeout: int = 1800, check: bool = True) -> str:
		argv = list(map(str, arguments))
		with (output / (name + ".log")).open("wb") as log:
			result = subprocess.run(argv, env=env, stdin=subprocess.DEVNULL,
				stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
		commands.append({"command": argv, "exitCode": result.returncode})
		(output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
		text = (output / (name + ".log")).read_text(encoding="utf-8", errors="replace")
		if check and result.returncode:
			raise RuntimeError(f"{name} failed; see {output / (name + '.log')}")
		return text

	variant = "debug" if args.debug else "release"
	build = [gradle, "--project-dir", str(PROJECT), "--no-daemon", "--console=plain",
		"-PveroscriptAar=" + str(args.aar.resolve())]
	if args.jit_aar:
		build.append("-PveroscriptJitAar=" + str(args.jit_aar.resolve()))
	command("build", build + ["assemble" + variant.capitalize()])
	artifacts = list((PROJECT / "build/outputs/apk" / variant).glob("*.apk"))
	if len(artifacts) != 1:
		raise RuntimeError(f"Expected one {variant} APK: {artifacts}")
	apk = output / ("device-smoke-" + variant + ".apk")
	shutil.copy2(artifacts[0], apk)
	command("zipalign", [zipalign, "-c", "-P", "16", "-v", "4", apk], timeout=60)
	device = [str(adb), "-s", args.serial]
	# Only this dedicated fixture's data is replaced; the selected target is kept running.
	command("uninstall", device + ["uninstall", PACKAGE], timeout=30, check=False)
	install = device + ["install"]
	if args.abi:
		install += ["--abi", args.abi]
	command("install", install + [str(apk)], timeout=120)
	raw = command("instrumentation", device + ["shell", "am", "instrument", "-w", "-r",
		PACKAGE + "/.DeviceSmoke"], timeout=300)
	marker = "INSTRUMENTATION_RESULT: report="
	if marker not in raw:
		raise RuntimeError(f"Android did not return an acceptance report: {output / 'instrumentation.log'}")
	report = json.loads(raw.split(marker, 1)[1].split("\nINSTRUMENTATION_", 1)[0].strip())
	report.update(qualification)
	with apk.open("rb") as stream:
		report["apkSha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
	save_report(report)
	expected = 25 if args.jit_aar else 24
	if (report.get("failures") != 0 or len(report.get("cases", [])) != expected
		or not all(case.get("passed") is True for case in report["cases"])
		or "INSTRUMENTATION_CODE: -1" not in raw.splitlines()):
		raise RuntimeError(f"Android acceptance failed: {output / 'device-smoke.json'}")
	if args.abi:
		prefix = "android-arm64/" if args.abi == "arm64-v8a" else "android-x64/"
		if not report["identity"].startswith(prefix):
			raise RuntimeError(f"Android ran a different native ABI: {report['identity']}")
	if args.page_size and report["pageSize"] != args.page_size:
		raise RuntimeError(f"Android used {report['pageSize']} byte pages, expected {args.page_size}")
	report["acceptancePassed"] = True
	save_report(report)
	print(f"Passed {expected} Android checks: {report['identity']}; {report['pageSize']} byte pages")
	print(output / "device-smoke.json")


if __name__ == "__main__":
	sys.stdout.reconfigure(encoding="utf-8")
	main()
