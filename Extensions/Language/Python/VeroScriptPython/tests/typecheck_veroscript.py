# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

from collections.abc import Iterator
from typing import assert_type

import veroscript


assert_type(veroscript.__version__, str)
assert_type(veroscript.ABI_VERSION, int)
assert_type(veroscript.RUNTIME_VERSION, str)
assert_type(veroscript.HOST_PLATFORM, str)
assert_type(veroscript.CAPABILITIES, int)
assert_type(veroscript.COMPILE_CAPABILITIES, int)
compile_capability: veroscript.CompileCapabilities = veroscript.CompileCapabilities.ANNOTATION_PROCESSING
assert_type(compile_capability, veroscript.CompileCapabilities)
assert_type(veroscript.Diagnostic("message", "", "file.vs", 1, 1).severity, veroscript.DiagnosticSeverity)
runtime_capability: veroscript.RuntimeCapabilities = (
	veroscript.RuntimeCapabilities.NATIVE_JIT
)
assert_type(runtime_capability, veroscript.RuntimeCapabilities)
run_mode: veroscript.RunMode = veroscript.RunMode.EVAL_ONCE
assert_type(run_mode, veroscript.RunMode)
legacy_backend_info = veroscript.JitBackendInfo("external", "test-target", 1, True)
assert_type(legacy_backend_info.capabilities, veroscript.JitBackendCapabilities)


def console_control(operation: veroscript.ConsoleOperation, first: int, second: int) -> bool:
	assert_type(operation, veroscript.ConsoleOperation)
	return True


def console_query() -> veroscript.ConsoleInfo:
	return veroscript.ConsoleInfo(output_terminal=True, color=True, width=80, height=25)


def check_console_session() -> veroscript.Session:
	return veroscript.Session(
		console_query=console_query,
		console_control=console_control,
		console_write=lambda text: True,
	)


class CustomValue(veroscript.Value):
	pass


def check_value_factory_types(value: CustomValue) -> None:
	assert_type(value.clone(), veroscript.Value)
	assert_type(CustomValue.empty_variant(), veroscript.Value)
	assert_type(CustomValue.array("int", [1]), veroscript.Value)
	assert_type(CustomValue.queue("int", [1]), veroscript.Value)
	assert_type(CustomValue.priority_queue("int", [1]), veroscript.Value)
	assert_type(CustomValue.stack("int", [1]), veroscript.Value)
	assert_type(CustomValue.deque("int", [1]), veroscript.Value)
	assert_type(CustomValue.channel("int", 1), veroscript.Value)
	assert_type(CustomValue.set("int", [1]), veroscript.Value)
	assert_type(CustomValue.map("string", "int", {"one": 1}), veroscript.Value)


def check_public_api(session: veroscript.Session, value: veroscript.Value) -> None:
	assert_type(session.closed, bool)
	assert_type(session.reload_status, veroscript.ReloadStatus)
	assert_type(session.reload_status.state, veroscript.ReloadState)
	assert_type(session.reload_status.committed_generation, int)
	assert_type(value.value, object)
	assert_type(value.type_name, str)
	assert_type(value.numeric_kind, int)
	box = session.variant("int", veroscript.Value(42, kind="int32"))
	assert_type(box, veroscript.Value)
	assert_type(veroscript.Value.empty_variant(), veroscript.Value)
	box.assign_variant(box)
	assert_type(box.variant_has_value(), bool)
	assert_type(box.variant_type_name(), str)
	assert_type(box.variant_get(), veroscript.Value)
	box.set_variant_empty()
	numerics = session.numerics
	assert_type(numerics, veroscript.Numerics)
	assert_type(veroscript.NumericKind(value.numeric_kind), veroscript.NumericKind)
	session.set_numeric_limits(max_value_bits=65536)
	integer = numerics.bigint("18446744073709551615")
	assert_type(integer, veroscript.Value)
	assert_type(numerics.rational("1/3"), veroscript.Value)
	assert_type(numerics.parse("42", veroscript.NumericKind.BIGINT), veroscript.Value)
	assert_type(numerics.to_text(integer), str)
	assert_type(numerics.convert(integer, "uint64", exact_float=True), veroscript.Value)
	assert_type(veroscript.current_numerics(), veroscript.Numerics)
	assert_type(len(value), int)
	assert_type(iter(value), Iterator[object])
	assert_type(value[0], object)
	assert_type(value.keys(), list[object])
	assert_type(value.items(), list[tuple[object, object]])
	assert_type(
		session.load_source("void main() {}", "typecheck.vs"),
		veroscript.RunResult,
	)
	assert_type(session.call("Function"), object)
	assert_type(session.call_method(value, "Method"), object)
	assert_type(session.call_method_bypassing_interceptor(value, "Method"), object)
	assert_type(veroscript.Value.array("int", [1, 2]), veroscript.Value)
	assert_type(veroscript.available_languages(), tuple[str, ...])
	policy = veroscript.JitPolicy()
	assert_type(policy.reuse_session_code, bool)
	limits = veroscript.JitCacheLimits()
	session.set_jit_policy(policy)
	session.set_jit_cache_limits(limits)
	assert_type(session.jit_cache_limits, veroscript.JitCacheLimits)
	assert_type(session.jit_cache_stats, veroscript.JitCacheStats)
	session.set_jit_backend(None)
	session.set_vm_backend(None)
	vm_info = session.get_vm_backend_info()
	assert_type(vm_info, veroscript.VmBackendInfo)
	assert_type(vm_info.capabilities, int)
	assert_type(vm_info.library_path, str)
	assert_type(vm_info.selected, bool)
	assert_type(vm_info.frozen, bool)
	session.set_annotation_processing_service(None)
	session.set_annotation_processing_service(0)
	assert_type(session.get_jit_backend(), veroscript.JitBackend)
	info = session.get_jit_backend_info()
	assert_type(info, veroscript.JitBackendInfo)
	assert_type(info.capabilities, veroscript.JitBackendCapabilities)
	assert_type(info.native_code, bool)
	session.register_annotation(
		"Range", arguments=(veroscript.AnnotationArgument("minimum"),), case_sensitive=False
	)
	session.register_type_alias("Meters", "double")
	session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
	memory = session.memory
	assert_type(memory, veroscript.Memory)
	assert_type(memory.stats, veroscript.MemoryStats)
	assert_type(memory.stats.owned_bytes, int)
	assert_type(memory.native_addresses_available, bool)
	region = memory.buffer(8)
	assert_type(region, veroscript.Value)
	assert_type(memory.slice(region, 0, 8, read_only=True), veroscript.Value)
	assert_type(memory.object_id(region), int)
	assert_type(memory.read(region, 0, "uint64", veroscript.ByteOrder.BIG), int | float)
	with memory.acquire_lease(region, write=True) as lease:
		assert_type(lease, veroscript.MemoryLease)
		assert_type(lease.size, int)
		assert_type(lease.closed, bool)
		assert_type(lease.writable, bool)
		assert_type(memoryview(lease), memoryview)
	assert_type(veroscript.current_memory(), veroscript.Memory)

	def intercept(context: veroscript.InterceptContext) -> veroscript.InterceptResult:
		assert_type(context.receiver, object)
		assert_type(context.target, veroscript.ResolvedMethod)
		assert_type(context.target.declaring_type_short_name, str)
		assert_type(context.arguments, tuple[object, ...])
		assert_type(context.proceed(), object)
		return veroscript.InterceptResult.UNHANDLED

	session.set_instance_call_interceptor(intercept)
	session.set_instance_call_interceptor(None)


def check_host_class(binding: veroscript.HostClass) -> None:
	assert_type(binding.field("Value", "value", type_name="int"), veroscript.HostClass)
	assert_type(binding.protocol(clone=lambda value: value), veroscript.HostClass)


def check_host_struct(session: veroscript.Session) -> None:
	class Point:
		pass

	assert_type(session.register_struct("Point", Point), veroscript.HostClass)
	template = session.register_generic_class("Box", 1, lambda _type: Point)
	assert_type(template, veroscript.GenericHostClass)
	assert_type(template.instantiate("int"), veroscript.HostClass)
