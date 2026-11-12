# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Call run(report_path) on a MOBILE CPython worker with at least 8 MiB native stack."""
from __future__ import annotations

import importlib
import json
import os
from pathlib import Path
import platform
import sys


def require(condition: object, message: str) -> None:
	# Acceptance must remain effective when the application starts Python with -O.
	if not condition:
		raise RuntimeError(message)


def device_identity(target_platform: str, native_platform: str) -> tuple[str, str]:
	# iPhone uname.machine is a model identifier. Translation/emulation can
	# also make kernel architecture differ from the extension's process ABI.
	targets = {
		"android": {"android-arm64": ("arm64-v8a", "device"), "android-x64": ("x86_64", "device")},
		"ios": {"ios-arm64": ("arm64", "device"), "ios-simulator-arm64": ("arm64", "simulator"),
				"ios-simulator-x64": ("x86_64", "simulator")},
	}
	identity = targets.get(target_platform, {}).get(native_platform)
	if identity is None:
		raise RuntimeError(f"Unsupported mobile native process platform: {native_platform}")
	return identity


def check_compiler_nesting(session, output: list[str]) -> None:
	"""The embedding host must provide its documented native compiler stack."""
	for depth in (200, 512):
		output.clear()
		source = "void main(){print(" + "(" * depth + "42" + ")" * depth + ");}"
		loaded = session.load_source(source, "/veroscript/mobile-nesting.vs")
		if depth == 200:
			require(loaded.success, f"Nested source failed: {loaded.diagnostics}")
			executed = session.run()
			require(executed.success and output == ["42"], f"Nested execution failed: {executed.diagnostics}, {output}")
		else:
			require(not loaded.success and loaded.diagnostics, "Missing bounded compiler nesting diagnostics")
	output.clear()
	loaded = session.load_source("void main(){print(7);}", "/veroscript/mobile-nesting-recovered.vs")
	require(loaded.success, f"Compiler nesting recovery failed: {loaded.diagnostics}")
	executed = session.run()
	require(executed.success and output == ["7"], "Compiler nesting error prevented session reuse")


def exercise_runtime() -> int:
	"""Shared binding checks, also runnable on the host without a device proof."""
	import veroscript

	require(veroscript.COMPILE_CAPABILITIES & veroscript.CompileCapabilities.ANNOTATION_PROCESSING,
			"Missing compile capability query")
	# Exercise the runtime with mobile-style permissions and execution budgets.
	options = {
		"max_call_depth": 256,
		"max_instructions": 100_000,
		"timeout_milliseconds": 5_000,
		"enable_execution_control": True,
		"capability_policy_enabled": True,
		"allowed_script_capabilities": veroscript.ScriptCapabilities.CONSOLE_OUTPUT,
	}
	tests = 0
	for backend in (veroscript.Backend.VM, veroscript.Backend.MIXED):
		output: list[str] = []
		with veroscript.Session(backend=backend, print_callback=output.append, **options) as session:
			session.set_annotation_processing_service(None)
			session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
			loaded = session.load_source("""
                int value = 7;
                int add(int x) { return x + value; }
                void main()
                {
                    assert(!std::memory::native_addresses_available());
                    auto bytes = new std::memory::buffer(8);
                    std::memory::write<uint64>(bytes, 0, 18446744073709551615u64);
                    auto header = bytes.readonly_slice(0, 8);
                    assert(std::memory::read<uint64>(header, 0) == 18446744073709551615u64);
                    print(value);
                }
            """)
			require(loaded.success, f"Load failed: {loaded.diagnostics}")
			initialized = session.run(veroscript.RunMode.EVAL_ONCE)
			require(initialized.success, f"Initialization failed: {initialized.diagnostics}")
			require(session.call("add", 5) == 12, "Function call failed")
			require(session.get_jit_backend() == veroscript.JitBackend.NONE, "Unexpected native JIT module")
			executed = session.run()
			require(executed.success and output == ["7"], f"Execution failed: {executed.diagnostics}, {output}")
			check_compiler_nesting(session, output)
			tests += 1
	with veroscript.Session(**options) as session:
		session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
		require(session.load_source("void main() { throw \"device-error\"; }").success, "Exception script load failed")
		result = session.run()
		require(not result.success and result.diagnostics, "Missing exception diagnostics")
		require(all(item.severity is veroscript.DiagnosticSeverity.ERROR for item in result.diagnostics),
				"Missing runtime diagnostic severity")
		tests += 1
	with veroscript.Session(**options) as session:
		session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
		source = "int twice(int x) { return x * 2; } function(int)->int getTwice() { return twice; }"
		require(session.load_source(source).success, "Callable script load failed")
		function = session.call("getTwice")
		require(isinstance(function, veroscript.Value), "Missing callable value")
		with session.retain_callable(function) as retained:
			require(retained(9) == 18, "Retained callable failed")
		tests += 1
	return tests


def run(report_path: str | Path) -> dict:
	report_path = Path(report_path)
	# Invalidate an earlier success BEFORE importing or running anything. A
	# failed retry must never leave an old passing report at the requested path.
	report_path.write_text(json.dumps({"schemaVersion": 1, "passed": False}) + "\n", encoding="utf-8")
	import veroscript

	native = importlib.import_module("veroscript._veroscript")
	if sys.platform not in {"android", "ios"} or not native.MOBILE_BUILD_ID:
		raise RuntimeError("Device acceptance requires an actual mobile CPython extension build")
	architecture, variant = device_identity(sys.platform, veroscript.HOST_PLATFORM)
	tests = exercise_runtime()
	report = {"schemaVersion": 1, "passed": True, "testCount": tests, "compilerNestingChecked": True,
			  "buildId": native.MOBILE_BUILD_ID,
			  "platform": sys.platform, "architecture": architecture, "variant": variant,
			  "pythonVersion": platform.python_version(), "abiVersion": veroscript.ABI_VERSION,
			  "productVersion": veroscript.__version__,
			  "pageSize": os.sysconf("SC_PAGESIZE") if sys.platform == "android" else None}
	report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
	return report
