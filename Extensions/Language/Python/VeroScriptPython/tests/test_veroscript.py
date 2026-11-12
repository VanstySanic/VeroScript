# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from __future__ import annotations

import gc
import ctypes
import os
from enum import IntEnum
import re
import sys
import threading
import tempfile
import time
import unittest
import weakref
from pathlib import Path

import veroscript


TEST_JIT_BACKEND = os.environ.get("VEROSCRIPT_TEST_JIT_BACKEND")
TEST_ANNOTATION_LIBRARY = os.environ.get("VEROSCRIPT_TEST_ANNOTATION_LIBRARY")
TEST_BACKENDS = (veroscript.Backend.VM, veroscript.Backend.MIXED) + (
	(veroscript.Backend.JIT,) if TEST_JIT_BACKEND else ()
)


def make_session(*args, **kwargs):
	session = veroscript.Session(*args, **kwargs)
	if TEST_JIT_BACKEND:
		session.set_jit_backend(TEST_JIT_BACKEND)
	return session


with make_session() as _metadata_session:
	TEST_JIT_INFO = _metadata_session.get_jit_backend_info()


SOURCE = r"""
export int SharedValue = 9;

int Add(int left, int right) { return left + right; }
void Bump(ref int value) { value += 1; }

class Counter {
public:
    int value;
    Counter(int initial) { value = initial; }
    int Add(int delta) { value += delta; return value; }
};

void main() { print("python-ok"); }
"""


class VeroScriptPythonTests(unittest.TestCase):
	def test_console_host_controls_and_cleanup(self):
		for backend in TEST_BACKENDS:
			commands, output = [], []
			def control(op, first, second):
				commands.append((op, first, second))
				return True
			def write(text):
				output.append(text)
				return True
			with make_session(backend=backend,
					console_query=lambda: veroscript.ConsoleInfo(True, True, True, True, 80, 25),
					console_control=control, console_write=write) as session:
				session.load_source('void main() { assert(std::console::width() == 80); '
					'std::console::write("A"); std::console::write("B"); '
					'std::console::set_color("red", "default"); std::console::set_cursor_visible(false); }')
				self.assertTrue(session.run().success)
				self.assertEqual(output, ["A", "B"])
				self.assertEqual([c[0] for c in commands], [veroscript.ConsoleOperation.SET_COLOR,
					veroscript.ConsoleOperation.SET_CURSOR_VISIBLE, veroscript.ConsoleOperation.RESET_STYLE,
					veroscript.ConsoleOperation.SET_CURSOR_VISIBLE])

	def test_console_callback_failure_is_reported(self):
		def broken():
			raise ValueError("console query failed")
		with make_session(console_query=broken) as session:
			session.load_source('void main() { std::console::supports_color(); }')
			with self.assertRaisesRegex(veroscript.CallbackError, "console query failed"):
				session.run()

	def test_console_callbacks_preserve_controls_and_cleanup(self) -> None:
		for backend in TEST_BACKENDS:
			events = []

			def control(operation, first, second):
				events.append((operation, first, second))
				return True

			with self.subTest(backend=backend), make_session(
				backend=backend,
				console_query=lambda: veroscript.ConsoleInfo(True, True, True, True, 80, 25),
				console_control=control,
				console_write=lambda text: events.append(("write", text)) or True,
			) as session:
				source = """
                void main() {
                    assert(std::console::is_output_terminal());
                    assert(std::console::supports_color());
                    assert(std::console::supports_cursor());
                    assert(std::console::supports_clear());
                    assert(std::console::width() == 80);
                    assert(std::console::height() == 25);
                    assert(std::console::set_color("red", "default"));
                    assert(std::console::move_cursor(2, 3));
                    assert(!std::console::move_cursor(80, 0));
                    assert(std::console::clear_line("all"));
                    assert(std::console::set_cursor_visible(false));
                    std::console::write("exact");
                }
                """
				loaded = session.load_source(source, "console-callbacks.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
			self.assertIn((veroscript.ConsoleOperation.SET_COLOR, 1, -1), events)
			self.assertIn((veroscript.ConsoleOperation.MOVE_CURSOR, 2, 3), events)
			self.assertNotIn((veroscript.ConsoleOperation.MOVE_CURSOR, 80, 0), events)
			self.assertIn((veroscript.ConsoleOperation.CLEAR_LINE, 2, 0), events)
			self.assertIn((veroscript.ConsoleOperation.SET_CURSOR_VISIBLE, 0, 0), events)
			self.assertIn((veroscript.ConsoleOperation.RESET_STYLE, 0, 0), events)
			self.assertIn((veroscript.ConsoleOperation.SET_CURSOR_VISIBLE, 1, 0), events)
			self.assertIn(("write", "exact"), events)

	def test_console_callbacks_reject_close_during_execution(self) -> None:
		for phase in ("query", "control", "write"):
			with self.subTest(phase=phase):
				states = []

				def attempt_close():
					try:
						session.close()
					except veroscript.Error:
						states.append(session.closed)
					else:
						states.append(True)

				def query():
					if phase == "query":
						attempt_close()
					return veroscript.ConsoleInfo(True, True, True, True, 80, 25)

				def control(*args):
					if phase == "control":
						attempt_close()
					return True

				def write(text):
					if phase == "write":
						attempt_close()
					return True

				with make_session(console_query=query, console_control=control, console_write=write) as session:
					loaded = session.load_source(
						'void main() { std::console::supports_color(); std::console::set_color("red", "default"); std::console::write("text"); } int alive() { return 42; }',
						"console-close.vs",
					)
					self.assertTrue(loaded.success, loaded.diagnostics)
					self.assertTrue(session.run().success)
					self.assertTrue(states)
					self.assertEqual(states, [False] * len(states))
					self.assertEqual(session.call("alive"), 42)

	def test_session_closes_before_reentrant_host_finalization(self) -> None:
		events = []
		session = make_session(hot_reload_enabled=True)

		class Host:
			def __del__(self):
				events.append(("closed", session.closed))
				try:
					session.close()
				except Exception as error:
					events.append(("recursive", type(error).__name__))
				else:
					events.append(("recursive", "ok"))

		session.register_class("Host", Host).constructor()
		self.assertTrue(session.load_source("Host retained = Host(); void main() {}", "close-finalizer.vs").success)
		self.assertTrue(session.run().success)
		self.assertEqual(events, [])
		session.close()
		self.assertEqual(events, [("closed", True), ("recursive", "ok")])
		self.assertTrue(session.closed)

	def test_close_during_execution_preserves_a_live_session(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				states = []

				def close_during_callback():
					try:
						session.close()
					except veroscript.Error as error:
						states.append((session.closed, "active operation" in str(error)))
					else:
						states.append((True, False))

				session.register_function("TryClose", close_during_callback)
				source = """
                void attempt() { TryClose(); }
                function()->void get_attempt() { return attempt; }
                class Test {
                public:
                    Test() { TryClose(); }
                    void attempt() { TryClose(); }
                };
                int alive() { return 42; }
                void main() { TryClose(); }
                """
				self.assertTrue(session.load_source(source, "active-close.vs").success)
				session.call("attempt")
				self.assertTrue(session.run().success)
				with session.retain_callable(session.call("get_attempt")) as retained:
					retained()
				instance = session.construct("Test")
				session.call_method(instance, "attempt")
				self.assertEqual(states, [(False, True)] * 5)
				self.assertFalse(session.closed)
				self.assertEqual(session.call("alive"), 42)

	def test_clear_rejects_close_from_host_finalizers(self) -> None:
		with make_session(hot_reload_enabled=True) as session:
			states = []

			class Host:
				def __del__(self):
					try:
						session.close()
					except veroscript.Error as error:
						states.append((session.closed, "active operation" in str(error)))
					else:
						states.append((True, False))

			session.register_class("Host", Host).constructor()
			self.assertTrue(session.load_source("Host retained = Host(); void main() {}", "clear-finalizer.vs").success)
			self.assertTrue(session.run().success)
			session.clear()
			self.assertEqual(states, [(False, True)])
			self.assertFalse(session.closed)
			self.assertTrue(session.load_source("int alive() { return 42; }", "after-clear.vs").success)
			self.assertEqual(session.call("alive"), 42)

	def test_close_detaches_callbacks_before_finalizers_collect_garbage(self) -> None:
		events = []
		session = make_session()

		class Callback:
			def __call__(self):
				pass

			def __del__(self):
				events.append(session.closed)
				gc.collect()
				session.close()

		for index in range(8):
			session.register_function("Collect" + str(index), Callback())
		session.close()
		self.assertEqual(events, [True] * 8)
		self.assertTrue(session.closed)

	def test_script_compile_extensions(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "extension.vs"
			path.write_text('''import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
out.add_declaration(ctx.top_scope(),"int generated(){return 17;}");};}
export void register_extension(std::compiler::registration reg){
std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
std::compiler::processor_definition d=std::compiler::processor_definition();d.id="python.generated";d.version="0.1.1";
d.phases=array<std::compiler::phase>{std::compiler::phase::members};d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);}
''', encoding="utf-8")
			with make_session() as session:
				session.set_script_compile_extensions([path], configuration={"mode": "test"})
				self.assertTrue(session.load_source("int main(){return generated();}", str(Path(directory) / "main.vs")).success)
				self.assertEqual(session.run().exit_code, 17)
				session.set_script_compile_extensions(allow_source_imports=False)
				self.assertFalse(session.load_source('import compiler "extension.vs";int main(){return 0;}', str(Path(directory) / "main.vs")).success)
				session.set_script_compile_extensions()
				with self.assertRaises(ValueError):
					session.set_script_compile_extensions(max_instructions=-1)

	def test_compile_capabilities_and_diagnostic_severity(self) -> None:
		self.assertEqual(veroscript.CompileCapabilities.ANNOTATION_PROCESSING, 1)
		self.assertTrue(veroscript.COMPILE_CAPABILITIES & veroscript.CompileCapabilities.ANNOTATION_PROCESSING)
		self.assertTrue(veroscript.COMPILE_CAPABILITIES & veroscript.CompileCapabilities.SCRIPT_EXTENSIONS)
		self.assertTrue(veroscript.COMPILE_CAPABILITIES & veroscript.CompileCapabilities.GENERAL_PROCESSING)
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.MODULE_ENVIRONMENT)
		legacy = veroscript.Diagnostic("message", "", "legacy.vs", 1, 1)
		self.assertIs(legacy.severity, veroscript.DiagnosticSeverity.ERROR)
		with make_session() as session:
			failed = session.load_source("int broken( {", "severity.vs")
			self.assertFalse(failed.success)
			self.assertTrue(failed.diagnostics)
			self.assertTrue(all(item.severity is veroscript.DiagnosticSeverity.ERROR for item in failed.diagnostics))
			self.assertEqual(session.diagnostics, failed.diagnostics)

	def test_compile_processing_handle_validation_and_session_guards(self) -> None:
		with make_session() as session:
			session.set_compile_processing_service(None)
			session.set_compile_processing_service(0)
			for invalid in (True, False, "handle", object()):
				with self.assertRaises(TypeError):
					session.set_compile_processing_service(invalid)
				with self.assertRaises(TypeError):
					veroscript.Session(module_environment=invalid)
			for invalid in (-1, 1 << (ctypes.sizeof(ctypes.c_void_p) * 8)):
				with self.assertRaises(OverflowError):
					session.set_compile_processing_service(invalid)
				with self.assertRaises(OverflowError):
					veroscript.Session(module_environment=invalid)
			errors = []

			def foreign_thread():
				try:
					session.set_compile_processing_service(None)
				except Exception as error:
					errors.append(error)

			worker = threading.Thread(target=foreign_thread)
			worker.start()
			worker.join()
			self.assertEqual(len(errors), 1)
			self.assertIsInstance(errors[0], veroscript.ThreadError)
			session.register_function("ClearGeneral", lambda: session.set_compile_processing_service(None))
			self.assertTrue(session.load_source("void attempt(){ClearGeneral();}").success)
			with self.assertRaises(veroscript.CallbackError):
				session.call("attempt")
		with self.assertRaises(veroscript.Error):
			session.set_compile_processing_service(None)
		with veroscript.Session(module_environment=0) as empty:
			self.assertTrue(empty.load_source("int main(){return 1;}").success)

	def test_compile_extension_budget_and_configuration_reentry(self) -> None:
		with make_session() as session:
			for invalid in (-1, 1 << 64):
				with self.assertRaises(OverflowError):
					session._native.set_script_compile_options([], {}, [], True, invalid, 2000)
			with self.assertRaises(TypeError):
				session._native.set_script_compile_options([], {}, [], True, True, 2000)

			class CloseOnBool:
				def __bool__(self):
					session.close()
					return True

			with self.assertRaisesRegex(veroscript.Error, "active operation"):
				session._native.set_script_compile_options([], {}, [], CloseOnBool(), 1000, 2000)
			self.assertFalse(session.closed)
			self.assertTrue(session.load_source("int main(){return 3;}").success)

	@unittest.skipUnless(TEST_ANNOTATION_LIBRARY, "Native SDK service fixture was not built")
	def test_native_general_processing_service_lifetime(self) -> None:
		producer = ctypes.CDLL(TEST_ANNOTATION_LIBRARY)
		producer.CreatePythonCompileProcessingService.argtypes = []
		producer.CreatePythonCompileProcessingService.restype = ctypes.c_void_p
		producer.ReleasePythonCompileProcessingService.argtypes = [ctypes.c_void_p]
		producer.ReleasePythonCompileProcessingService.restype = None
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				handle = producer.CreatePythonCompileProcessingService()
				self.assertTrue(handle)
				try:
					session.set_compile_processing_service(handle)
				finally:
					producer.ReleasePythonCompileProcessingService(handle)
				result = session.load_source("int main(){return NativeGenerated();}")
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(session.run().exit_code, 37)
				session.clear()
				session.set_compile_processing_service(None)
				self.assertFalse(session.load_source("int main(){return NativeGenerated();}").success)

	@unittest.skipUnless(TEST_ANNOTATION_LIBRARY, "Native SDK service fixture was not built")
	def test_native_module_environment_lifetime_and_source_policy(self) -> None:
		producer = ctypes.CDLL(TEST_ANNOTATION_LIBRARY)
		producer.CreatePythonModuleEnvironment.argtypes = [ctypes.c_uint32]
		producer.CreatePythonModuleEnvironment.restype = ctypes.c_void_p
		producer.ReleasePythonModuleEnvironment.argtypes = [ctypes.c_void_p]
		producer.ReleasePythonModuleEnvironment.restype = None
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend):
				handle = producer.CreatePythonModuleEnvironment(0)
				self.assertTrue(handle)
				try:
					session = make_session(backend=backend, module_environment=handle)
				finally:
					producer.ReleasePythonModuleEnvironment(handle)
				with session:
					result = session.load_source('import "library.vs"; int main(){return answer();}')
					self.assertTrue(result.success, result.diagnostics)
					self.assertEqual(session.run().exit_code, 42)
					session.clear()
					result = session.load_source('import "undeclared.vs"; int main(){return 0;}')
					self.assertFalse(result.success)

		handle = producer.CreatePythonModuleEnvironment(1)
		self.assertTrue(handle)
		try:
			session = make_session(module_environment=handle)
		finally:
			producer.ReleasePythonModuleEnvironment(handle)
		with session:
			with self.assertRaisesRegex(veroscript.Error, "environment-owned"):
				session.register_function("Local", lambda: 1, return_type="int")
			result = session.load_source('import "library.vs"; int main(){return answer();}')
			self.assertTrue(result.success, result.diagnostics)
			self.assertEqual(session.run().exit_code, 42)

	def test_annotation_service_handle_validation_and_session_guards(self) -> None:
		with make_session() as session:
			session.set_annotation_processing_service(None)
			session.set_annotation_processing_service(0)
			for invalid in (True, False, "handle", object()):
				with self.subTest(invalid=invalid), self.assertRaises(TypeError):
					session.set_annotation_processing_service(invalid)
			for invalid in (-1, 1 << (ctypes.sizeof(ctypes.c_void_p) * 8)):
				with self.subTest(invalid=invalid), self.assertRaises(OverflowError):
					session.set_annotation_processing_service(invalid)
			errors = []

			def foreign_thread():
				try:
					session.set_annotation_processing_service(None)
				except Exception as error:
					errors.append(error)

			worker = threading.Thread(target=foreign_thread)
			worker.start()
			worker.join(timeout=1)
			self.assertFalse(worker.is_alive())
			self.assertEqual(len(errors), 1)
			self.assertIsInstance(errors[0], veroscript.ThreadError)
			session.register_function("ClearService", lambda: session.set_annotation_processing_service(None))
			self.assertTrue(session.load_source("void attempt() { ClearService(); }", "busy-service.vs").success)
			with self.assertRaises(veroscript.CallbackError):
				session.call("attempt")
			self.assertFalse(session.closed)
		with self.assertRaisesRegex(veroscript.Error, "session is closed"):
			session.set_annotation_processing_service(None)

	def test_reentrant_load_reports_failure_without_replacing_the_program(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				nested = []
				session.register_function("NestedLoad", lambda: nested.append(
					session.load_source("int replacement() { return 99; }", "nested-load.vs")))
				self.assertTrue(session.load_source(
					"void attempt() { NestedLoad(); } int original() { return 42; }", "original.vs").success)
				session.call("attempt")
				self.assertEqual(len(nested), 1)
				self.assertFalse(nested[0].success)
				self.assertEqual(nested[0].exit_code, 2)
				self.assertTrue(nested[0].diagnostics)
				self.assertIn("reentry", nested[0].diagnostics[0].message)
				self.assertIs(nested[0].diagnostics[0].severity, veroscript.DiagnosticSeverity.ERROR)
				self.assertEqual(session.call("original"), 42)

	@unittest.skipUnless(TEST_ANNOTATION_LIBRARY, "Native annotation service fixture was not built")
	def test_native_annotation_service_generation_lifetime_and_severity(self) -> None:
		producer = ctypes.CDLL(TEST_ANNOTATION_LIBRARY)
		producer.CreatePythonAnnotationService.argtypes = [ctypes.c_uint32]
		producer.CreatePythonAnnotationService.restype = ctypes.c_void_p
		producer.ReleasePythonAnnotationService.argtypes = [ctypes.c_void_p]
		producer.ReleasePythonAnnotationService.restype = None
		source = (
			"struct Generated { @PythonGenerated int value = 42; }; "
			"int read() { Generated value = Generated(); return value.GeneratedValue(); }"
		)
		for backend in TEST_BACKENDS:
			for severity in veroscript.DiagnosticSeverity:
				with self.subTest(backend=backend, severity=severity), make_session(backend=backend) as session:
					handle = producer.CreatePythonAnnotationService(int(severity))
					self.assertTrue(handle)
					try:
						session.set_annotation_processing_service(handle)
					finally:
						producer.ReleasePythonAnnotationService(handle)
					# The session owns a configuration copy after the producer releases its handle.
					loaded = session.load_source(source, "generated-severity.vs")
					self.assertEqual(loaded.success, severity is not veroscript.DiagnosticSeverity.ERROR)
					reported = [item for item in loaded.diagnostics if item.message == "Python native annotation diagnostic"]
					self.assertEqual(len(reported), 1)
					self.assertIs(reported[0].severity, severity)
					self.assertEqual(session.diagnostics, loaded.diagnostics)
					if loaded.success:
						self.assertEqual(session.call("read"), 42)
						session.clear()
						self.assertTrue(session.load_source(source, "retained-service.vs").success)
						self.assertEqual(session.call("read"), 42)
						failed = session.load_source(source + " int broken() { return MissingFunction(); }", "mixed-severity.vs")
						self.assertFalse(failed.success)
						self.assertTrue(any(item.severity is severity for item in failed.diagnostics))
						primary = next(item for item in failed.diagnostics if item.severity is veroscript.DiagnosticSeverity.ERROR)
						self.assertEqual(str(veroscript.VeroScriptError("compile", failed)), "compile failed: " + primary.message)
					session.set_annotation_processing_service(None)
					self.assertFalse(session.load_source(source, "cleared-service.vs").success)

	def test_malformed_mapping_items_raise_python_errors(self) -> None:
		for item in (1, ("key",), ("key", 1, 2)):
			with self.subTest(item=item):
				class InvalidMapping:
					def items(self):
						return [item]

				mapping = InvalidMapping()
				with self.assertRaisesRegex(TypeError, "mapping items"):
					veroscript.Value.map("int", "int", mapping)
				with self.assertRaisesRegex(TypeError, "mapping items"):
					veroscript._native.Session({"source_overlays": mapping})
				with make_session() as session:
					with self.assertRaisesRegex(TypeError, "mapping items"):
						session._native.build_package_file("missing.vs", 1, "p", 1, mapping)

	def test_deserializer_rejects_malformed_mapping_without_mutating_object(self) -> None:
		class InvalidMapping:
			def items(self):
				return [("value",)]

		with make_session() as session:
			session.register_serializer("Persisted", 1, lambda *args: b"", lambda *args: InvalidMapping())
			self.assertTrue(session.load_source("class Persisted { public: int value = 7; };", "mapping.vs").success)
			instance = session.construct("Persisted")
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.deserialize_object(instance, b"", 1)
			self.assertIsInstance(raised.exception.args[0], TypeError)
			self.assertEqual(session.get_field(instance, "value"), 7)

	def test_host_registration_revalidates_after_python_conversions(self) -> None:
		class Host:
			pass

		class Mode(IntEnum):
			ONE = 1

		for operation in ("function", "member", "annotation", "alias", "generic", "enum", "enum_value"):
			with self.subTest(operation=operation), make_session() as session:
				native = session._native
				type_id = native.register_host_class("Host", Host)
				template_id = native.register_generic_host_type("Box", 1, lambda *args: Host)

				class ClosingSequence:
					def __iter__(self):
						session.close()
						return iter((("ONE", 1),) if operation == "enum" else ())

				class ClosingIndex:
					def __index__(self) -> int:
						session.close()
						return 0

				with self.assertRaisesRegex(veroscript.Error, "session is closed"):
					if operation == "function":
						native.register_function("f", lambda: None, "void", ClosingSequence())
					elif operation == "member":
						native.register_host_member(type_id, 2, "f", lambda self: None, "void", ClosingSequence())
					elif operation == "annotation":
						native.register_annotation("marker", arguments=ClosingSequence())
					elif operation == "alias":
						native.register_type_alias("Alias", ClosingSequence(), "int")
					elif operation == "generic":
						native.instantiate_generic_host_type(template_id, ClosingSequence())
					elif operation == "enum":
						native.register_host_enum("Mode", Mode, ClosingSequence())
					else:
						native.register_host_enum("Mode", Mode, [("ONE", ClosingIndex())])
				self.assertTrue(session.closed)

	def test_native_session_cannot_be_reinitialized_after_close(self) -> None:
		session = make_session()
		session.close()
		with self.assertRaisesRegex(veroscript.Error, "cannot be reinitialized"):
			session._native.__init__({})

	def test_argument_sequences_are_snapshots_during_numeric_conversion(self) -> None:
		class ClearingIndex:
			def __index__(self) -> int:
				values.clear()
				return 3

		values = [ClearingIndex(), 7]
		result = veroscript.Value.array("int", values)
		self.assertEqual(result.to_python(), [3, 7])
		self.assertEqual(values, [])

		with make_session() as session:
			class Mode(IntEnum):
				ONE = 3
				TWO = 7

			class ClearingEnumValue:
				def __index__(self) -> int:
					members.clear()
					return 3

			members = [("ONE", ClearingEnumValue()), ("TWO", 7)]
			session._native.register_host_enum("Mode", Mode, members)
			self.assertEqual(members, [])
			self.assertTrue(session.load_source("int sum() { return cast<int>(Mode::ONE) + cast<int>(Mode::TWO); }", "snapshot.vs").success)
			self.assertEqual(session.call("sum"), 10)

	def test_container_construction_retains_temporary_value_elements(self) -> None:
		for name in ("array", "queue", "priority_queue", "stack", "deque", "set"):
			with self.subTest(container=name):
				references: list[weakref.ReferenceType[veroscript.Value]] = []

				class TemporaryValue(veroscript.Value):
					pass

				def elements():
					for number in range(32):
						value = TemporaryValue(number)
						references.append(weakref.ref(value))
						yield value

				result = getattr(veroscript.Value, name)("int", elements())
				self.assertEqual(sorted(result.to_python()), list(range(32)))
				self.assertTrue(all(reference() is None for reference in references))

		nested = veroscript.Value.array(
			"array<int>", (veroscript.Value.array("int", [number]) for number in range(4))
		)
		self.assertEqual(nested.to_python(), [[0], [1], [2], [3]])

		class ClearingIndex:
			def __index__(self) -> int:
				values.clear()
				return 7

		values = [veroscript.Value(3), ClearingIndex()]
		result = veroscript.Value.array("int", values)
		self.assertEqual(values, [])
		self.assertEqual(result.to_python(), [3, 7])

	def test_memory_rejects_uninitialized_values(self) -> None:
		class UninitializedValue(veroscript.Value):
			def __init__(self) -> None:
				pass

		with make_session() as session:
			memory = session.memory
			region = memory.buffer(4)
			for value in (veroscript.Value.__new__(veroscript.Value), UninitializedValue()):
				for operation in (
					lambda: memory.size(value),
					lambda: memory.clone(value),
					lambda: memory.copy(region, 0, value, 0, 4),
					lambda: memory.write(region, 0, "int32", value),
				):
					with self.assertRaisesRegex(veroscript.Error, "value is uninitialized"):
						operation()
			self.assertEqual(memory.read(region, 0, "int32"), 0)

	@unittest.skipIf(sys.version_info < (3, 12), "Python buffer exporters require CPython 3.12")
	def test_memory_lease_closes_before_external_release_callback(self) -> None:
		events: list[tuple[str, bool]] = []

		class ReentrantExporter:
			def __buffer__(self, flags: int) -> memoryview:
				return memoryview(bytearray(b"abcd"))

			def __release_buffer__(self, view: memoryview) -> None:
				events.append(("closed", lease.closed))
				try:
					lease.close()
				except veroscript.Error:
					events.append(("recursive_close", False))
				else:
					events.append(("recursive_close", True))
				try:
					exported = memoryview(lease)
				except BufferError:
					events.append(("export_rejected", True))
				else:
					# Release immediately even on a broken implementation.
					exported.release()
					events.append(("export_rejected", False))
				try:
					lease.__enter__()
				except veroscript.Error:
					events.append(("enter_rejected", True))
				else:
					events.append(("enter_rejected", False))
				view.release()

		session = make_session()
		region = session.memory.wrap_external(ReentrantExporter())
		lease = session.memory.acquire_lease(region)
		session.close()
		del region
		lease.close()
		self.assertEqual(events, [("closed", True), ("recursive_close", True),
								  ("export_rejected", True), ("enter_rejected", True)])
		self.assertTrue(lease.closed)
		lease.close()
		self.assertEqual(len(events), 4)

	def test_memory_revalidates_session_after_python_argument_conversion(self) -> None:
		for operation in ("index", "boolean"):
			with self.subTest(operation=operation):
				session = make_session()
				memory = session.memory
				region = memory.buffer(8)

				class CloseDuringConversion:
					def __index__(self) -> int:
						session.close()
						return 4

					def __bool__(self) -> bool:
						session.close()
						return True

				value = CloseDuringConversion()
				with self.assertRaisesRegex(veroscript.Error, "session is closed"):
					if operation == "index":
						memory.buffer(value)
					else:
						memory.acquire_lease(region, write=value)
				self.assertTrue(session.closed)
				session.close()

		if sys.version_info >= (3, 12):
			session = make_session()
			memory = session.memory
			storage = bytearray(b"abcd")
			released: list[bool] = []

			class CloseDuringBufferExport:
				def __buffer__(self, flags: int) -> memoryview:
					session.close()
					return memoryview(storage)

				def __release_buffer__(self, view: memoryview) -> None:
					released.append(True)
					view.release()

			with self.assertRaisesRegex(veroscript.Error, "session is closed"):
				memory.wrap_external(CloseDuringBufferExport())
			self.assertTrue(session.closed)
			self.assertEqual(released, [True])
			storage.extend(b"e")

	def test_memory_callback_addresses_preserve_ref_bindings(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				seen: list[int] = []

				def inspect_ref(value: veroscript.Value) -> int:
					memory = veroscript.current_memory()
					address = memory.argument_address(0)
					self.assertGreater(address, 0)
					seen.append(address)
					value.value += 1
					return address

				session.register_function("inspect_ref", inspect_ref, return_type="uint64",
					parameters=(veroscript.HostParameter("int", by_ref=True),))
				loaded = session.load_source("""
                    int check() {
                        int value = 7;
                        uint64 first = inspect_ref(ref value);
                        uint64 second = inspect_ref(ref value);
                        assert(first == second);
                        return value;
                    }
                    void main() {}
                """, "python-memory-ref.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("check"), 9)
				self.assertEqual(len(seen), 2)
				with self.assertRaisesRegex(veroscript.Error, "active native callback"):
					veroscript.current_memory().stats

	def test_memory_services_in_host_struct_clone_callbacks(self) -> None:
		class MemoryPoint:
			def __init__(self, value: int) -> None:
				self.value = value

		for backend in TEST_BACKENDS:
			snapshots: list[veroscript.MemoryStats] = []

			def clone(point: object) -> object:
				self.assertIsInstance(point, MemoryPoint)
				snapshots.append(veroscript.current_memory().stats)
				return MemoryPoint(point.value)

			with self.subTest(backend=backend), make_session(backend=backend) as session:
				(session.register_struct("MemoryPoint", MemoryPoint)
					.constructor(parameters=("int",))
					.field("value", type_name="int")
					.protocol(clone=clone))
				loaded = session.load_source("""
                    int check() {
                        MemoryPoint first = MemoryPoint(3);
                        MemoryPoint second = first;
                        return second.value;
                    }
                    void main() {}
                """, "python-memory-protocol.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("check"), 3)
				self.assertTrue(snapshots)
				self.assertTrue(all(isinstance(item, veroscript.MemoryStats) for item in snapshots))
				with self.assertRaisesRegex(veroscript.Error, "active native callback"):
					veroscript.current_memory().stats

	def test_integer_overflow_reports_an_exception_without_mutation(self) -> None:
		for number in [-(1 << 63) - 1, -(1 << 100), 1 << 64, 1 << 100]:
			with self.subTest(number=number), self.assertRaises(OverflowError):
				veroscript.Value(number)
		for kind, bits in [("int8", 8), ("int16", 16), ("int32", 32), ("int64", 64)]:
			low, high = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
			value = veroscript.Value(7, kind=kind)
			for number in [low - 1, high + 1, -(1 << 100), 1 << 100]:
				with self.subTest(kind=kind, number=number), self.assertRaises(OverflowError):
					veroscript.Value(number, kind=kind)
				with self.subTest(assign=kind, number=number), self.assertRaises(OverflowError):
					value.value = number
				self.assertEqual(value.kind, kind)
				self.assertEqual(value.to_python(), 7)
			self.assertEqual(veroscript.Value(low, kind=kind).to_python(), low)
			self.assertEqual(veroscript.Value(high, kind=kind).to_python(), high)
		self.assertEqual(veroscript.Value((1 << 64) - 1).to_python(), (1 << 64) - 1)
		with self.assertRaises(OverflowError):
			veroscript.Value.array("int64", [1, -(1 << 100)])
		with self.assertRaises(OverflowError):
			veroscript.Value.map("int64", "int64", {1: 1 << 100})
		with make_session() as session:
			region = session.memory.buffer(8)
			session.memory.write(region, 0, "uint64", (1 << 64) - 1)
			version = session.memory.mutation_version(region)
			for number in [-(1 << 63) - 1, -(1 << 100), 1 << 64, 1 << 100]:
				with self.subTest(write=number), self.assertRaises(OverflowError):
					session.memory.write(region, 0, "int64", number)
				self.assertEqual(session.memory.read(region, 0, "uint64"), (1 << 64) - 1)
				self.assertEqual(session.memory.mutation_version(region), version)

	def test_standard_library_limits(self) -> None:
		with make_session() as session:
			session.set_stdlib_limits(max_text_bytes=5)
			self.assertTrue(session.load_source('void main(){format("{}{}","abc","abc");}').success)
			self.assertFalse(session.run().success)
			session.set_stdlib_limits()
			self.assertTrue(session.run().success)
			for bad in [-1, 1 << 64]:
				with self.subTest(limit=bad), self.assertRaises(OverflowError):
					session.set_stdlib_limits(max_text_bytes=bad)

	def test_memory_buffers_leases_external_and_session_lifetime(self) -> None:
		session = make_session()
		session.set_memory_limits(64, 128)
		memory = session.memory
		buffer = memory.buffer(16)
		memory.write(buffer, 1, "uint64", 18446744073709551615, veroscript.ByteOrder.BIG)
		self.assertEqual(memory.read(buffer, 1, "uint64", veroscript.ByteOrder.BIG), 18446744073709551615)
		snapshot = memory.to_array(buffer)
		independent = memory.from_array(snapshot)
		self.assertNotEqual(memory.object_id(buffer), memory.object_id(independent))
		view = memory.slice(buffer, 0, 8, read_only=True)
		with self.assertRaises(veroscript.Error):
			memory.fill(view, 0, 0, 1)
		lease = memory.acquire_lease(buffer, write=True)
		exported = memoryview(lease)
		exported[0] = 42
		with self.assertRaises(BufferError):
			lease.close()
		with self.assertRaises(veroscript.Error):
			memory.resize(buffer, 17)
		exported.release()
		lease.close()
		self.assertEqual(memory.read(buffer, 0, "uint8"), 42)
		memory.resize(buffer, 17)
		with self.assertRaises(veroscript.Error):
			memory.size(view)
		external_bytes = bytearray(b"abc")
		external = memory.wrap_external(external_bytes)
		with self.assertRaises(BufferError):
			external_bytes.extend(b"d")
		memory.fill(external, 1, 1, 122)
		self.assertEqual(external_bytes, b"azc")
		retained = memory.acquire_lease(external)
		session.close()
		del external
		retained.close()
		gc.collect()
		external_bytes.extend(b"d")
		self.assertEqual(external_bytes, b"azcd")


	def test_memory_host_global_and_script_share_identity(self) -> None:
		with make_session(backend=veroscript.Backend.VM, hot_reload_enabled=True) as session:
			session.load_source("std::memory::buffer bytes = new std::memory::buffer(); void main(){} void verify(){assert(bytes.size()==8);assert(bytes.get(0)==42u);}")
			self.assertTrue(session.run().success)
			region=session.memory.buffer(8)
			session.memory.write(region,0,"uint8",42)
			session.set_global("bytes",region)
			session.call("verify")
			got=session.get_global("bytes")
			self.assertEqual(session.memory.object_id(region),session.memory.object_id(got))

	def test_memory_host_identity_in_worker_callbacks(self) -> None:
		class Box:
			pass

		source = """
        import "concurrent.vs";
        PythonMemoryBox owner = PythonMemoryBox();
        export void memory_worker(channel<uint64> result) { result.send(host_memory_identity()); }
        void main() {}
        void verify_workers(uint64 expected) {
            channel<uint64> result = channel<uint64>();
            int named = std::concurrent::spawn("memory_worker", result);
            std::concurrent::join_task(named); assert(result.recv() == expected);
            int callable = std::concurrent::spawn(memory_worker, result);
            std::concurrent::join_task(callable); assert(result.recv() == expected);
            auto closure = [result]() -> void => result.send(host_memory_identity());
            int lambda = std::concurrent::spawn(closure);
            std::concurrent::join_task(lambda); assert(result.recv() == expected);
        }
        """
		for backend in TEST_BACKENDS:
			callback_threads: list[int] = []
			with self.subTest(backend=backend), make_session(
				backend=backend, hot_reload_enabled=True
			) as session:
				session.register_class("PythonMemoryBox", Box).constructor()

				def host_memory_identity() -> int:
					callback_threads.append(threading.get_ident())
					return veroscript.current_memory().object_id(owner)

				session.register_function("host_memory_identity", host_memory_identity, return_type="uint64")
				loaded = session.load_source(source, "python-worker-memory.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertTrue(session.run().success)
				owner = session.get_global("owner")
				self.assertIsInstance(owner, veroscript.Value)
				expected = session.memory.object_id(owner)
				self.assertGreater(expected, 0)
				session.call("verify_workers", veroscript.Value(expected, kind="uint64"))
				self.assertEqual(len(callback_threads), 3)
				self.assertTrue(all(thread != threading.get_ident() for thread in callback_threads))

	def test_shared_handle_aliases_and_weak_lifetime_across_backends(self) -> None:
		source = """
        struct Point { int x = 1; }
        shared<T> keep_shared<T>(handle<T> value) { return value; }
        void main() {
            Point source = Point();
            shared<Point> owner = shared(source);
            std::handle<Point> alias = owner.keep_shared();
            assert(alias == owner);
            alias.value.x = 7;
            assert(owner.value.x == 7 && source.x == 1);
            weak<shared<Point>> observer = weak<handle<Point>>(owner);
            std::shared<Point> locked = observer.lock();
            owner = nullptr; alias = nullptr;
            assert(!observer.expired() && locked.value.x == 7);
            locked = nullptr;
            assert(observer.expired() && observer.lock() == nullptr);
            print("shared-handle-ok");
        }
        """
		for backend in TEST_BACKENDS:
			output: list[str] = []
			with self.subTest(backend=backend), make_session(
				backend=backend, print_callback=output.append
			) as session:
				loaded = session.load_source(source, "shared-handle-test.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(output, ["shared-handle-ok"])

	def test_vm_backend_selection_and_information(self) -> None:
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.VM_BACKEND_SELECTION)
		with veroscript.Session(backend=veroscript.Backend.VM) as session:
			initial = session.get_vm_backend_info()
			self.assertEqual(initial.id, "vs-bytecode")
			self.assertTrue(initial.selected)
			self.assertFalse(initial.frozen)
			session.set_vm_backend("none")
			self.assertFalse(session.get_vm_backend_info().selected)
			session.set_vm_backend("auto")
			self.assertEqual(session.get_vm_backend_info(), initial)
			session.set_jit_backend("none")
			session.set_jit_backend("auto")
			with self.assertRaises(veroscript.Error):
				session.set_vm_backend("missing-vm-backend")
			self.assertEqual(session.get_vm_backend_info(), initial)
			path = os.environ.get("VEROSCRIPT_TEST_VM_BACKEND")
			if path:
				session.set_vm_backend(Path(path))
				self.assertTrue(session.get_vm_backend_info().library_path)
			self.assertTrue(session.load_source("int answer(){return 42;}", "vm-choice.vs").success)
			self.assertEqual(session.call("answer"), 42)
			self.assertTrue(session.get_vm_backend_info().frozen)
			session.clear()
			with self.assertRaises(veroscript.Error):
				session.set_vm_backend(None)
			self.assertTrue(session.get_vm_backend_info().frozen)
		with veroscript.Session() as session:
			session.set_vm_backend(None)
			self.assertFalse(session.get_vm_backend_info().selected)
			self.assertTrue(session.load_source("int answer(){return 42;}", "no-vm-choice.vs").success)
			with self.assertRaises(veroscript.Error):
				session.call("answer")
			self.assertFalse(session.get_vm_backend_info().frozen)
			if path:
				session.set_vm_backend(path)
				self.assertEqual(session.call("answer"), 42)

	def test_jit_backend_selection_and_information(self) -> None:
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.JIT_BACKEND_SELECTION)
		with veroscript.Session(backend=veroscript.Backend.JIT) as session:
			self.assertEqual(session.get_jit_backend(), veroscript.JitBackend.NONE)
			empty_info = session.get_jit_backend_info()
			self.assertEqual(empty_info.id, "none")
			self.assertEqual(empty_info.capabilities, veroscript.JitBackendCapabilities.NONE)
			self.assertFalse(empty_info.native_code)
			self.assertTrue(session.load_source("int value(){return 49;}void main(){print(value());}", "choice.vs").success)
			self.assertFalse(session.run().success)
			if TEST_JIT_BACKEND:
				session.set_jit_backend(Path(TEST_JIT_BACKEND))
				self.assertEqual(session.get_jit_backend(), veroscript.JitBackend.EXTERNAL)
				selected_info = session.get_jit_backend_info()
				self.assertEqual(selected_info.id, "asmjit")
				self.assertIsInstance(selected_info.capabilities, veroscript.JitBackendCapabilities)
				self.assertEqual(
					selected_info.native_code,
					bool(selected_info.capabilities & veroscript.JitBackendCapabilities.NATIVE_CODE),
				)
				self.assertEqual(session.call("value"), 49)
				self.assertGreater(session.jit_stats["dispatches"], 0)
				with self.assertRaises(veroscript.Error):
					session.set_jit_backend("missing-backend")
				self.assertEqual(session.get_jit_backend_info(), selected_info)
			session.set_jit_backend(None)
			self.assertEqual(session.get_jit_backend(), veroscript.JitBackend.NONE)
			self.assertEqual(session.get_jit_backend_info(), empty_info)

			if TEST_JIT_BACKEND:
				# Metadata owns its strings and remains valid after factory unload.
				self.assertEqual(selected_info.id, "asmjit")
				self.assertTrue(selected_info.target)

	@unittest.skipUnless(TEST_JIT_INFO.native_code, "native backend required")
	def test_controlled_mixed_policy(self) -> None:
		self.assertFalse(veroscript.JitPolicy().enable_controlled_mixed)
		for enabled in (False, True):
			with make_session(backend=veroscript.Backend.MIXED, max_instructions=10000) as session:
				session.set_jit_policy(veroscript.JitPolicy(enable_controlled_mixed=enabled))
				loaded = session.load_source("int work(int n){return n+1;}int main(){return work(6);}", "controlled-python.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(result.exit_code, 7)
				self.assertEqual(session.jit_stats["controlled_native_executions"] != 0, enabled)
				self.assertEqual(session.jit_stats["aot_executions"], 0)

	def test_jit_backend_info_accepts_integer_masks(self) -> None:
		from dataclasses import replace

		mask = int(veroscript.JitBackendCapabilities.NATIVE_CODE) | (1 << 20)
		info = veroscript.JitBackendInfo("external", "test-target", mask, True)
		self.assertIsInstance(info.capabilities, veroscript.JitBackendCapabilities)
		self.assertEqual(int(info.capabilities), mask)
		self.assertTrue(info.capabilities & veroscript.JitBackendCapabilities.NATIVE_CODE)
		self.assertEqual(replace(info), info)

	def test_eval_once_initializes_without_main_across_backends(self) -> None:
		self.assertEqual(veroscript.RunMode.EVAL_ONCE, 2)
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.EVAL_ONCE)
		source = """
        int total = 0;
        for (int i = 0; i < 100; i++) { total += i; }
        print(total);
        void main() { print("main"); }
        """
		for backend in TEST_BACKENDS:
			output: list[str] = []
			with self.subTest(backend=backend), make_session(
				backend=backend, print_callback=output.append
			) as session:
				loaded = session.load_source(source, "python-eval-once.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				for _ in range(2):
					result = session.run(veroscript.RunMode.EVAL_ONCE)
					self.assertTrue(result.success, result.diagnostics)
					self.assertEqual(output, ["4950"])
					output.clear()
				if backend == veroscript.Backend.JIT:
					self.assertGreater(session.jit_stats["compile_attempts"], 0)
				output.clear()
				result = session.run(veroscript.RunMode.SCRIPT)
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(output, ["4950", "main"])
				with self.assertRaises(ValueError):
					session.run(3)

	def test_eval_once_preserves_hot_reload_globals(self) -> None:
		for backend in TEST_BACKENDS:
			output: list[str] = []
			with self.subTest(backend=backend), make_session(
				backend=backend, hot_reload_enabled=True, print_callback=output.append
			) as session:
				loaded = session.load_source(
					'int count = 7; print(count); void main() { print("main"); }',
					"python-eval-once-hot-reload.vs",
				)
				self.assertTrue(loaded.success, loaded.diagnostics)
				first = session.run(veroscript.RunMode.EVAL_ONCE)
				self.assertTrue(first.success, first.diagnostics)
				self.assertEqual(output, ["7"])
				output.clear()
				session.set_global("count", 19)
				second = session.run(veroscript.RunMode.EVAL_ONCE)
				self.assertTrue(second.success, second.diagnostics)
				self.assertEqual(output, [])
				self.assertEqual(session.get_global("count"), 19)
				self.assertEqual(session.jit_stats["compile_attempts"], 0)

	def test_public_version_matches_runtime(self) -> None:
		self.assertRegex(veroscript.__version__, r"^\d+\.\d+\.\d+$")
		self.assertTrue(veroscript.RUNTIME_VERSION.startswith(veroscript.__version__))
		version_header = (
			Path(__file__).resolve().parents[5]
			/ "VeroScript"
			/ "Public"
			/ "VeroScriptVersion.h"
		)
		match = re.search(
			r"VEROSCRIPT_VERSION\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
			version_header.read_text(encoding="utf-8"),
		)
		self.assertIsNotNone(match)
		assert match is not None
		self.assertEqual(".".join(match.groups()), veroscript.__version__)

	def test_host_platform_and_runtime_capabilities_are_exposed(self) -> None:
		self.assertTrue(veroscript.HOST_PLATFORM)
		self.assertEqual(veroscript.RuntimeCapabilities.NATIVE_JIT, 0x20000)
		self.assertEqual(veroscript.RuntimeCapabilities.NATIVE_TYPED_LOOPS, 0x40000)
		self.assertTrue(
			veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.PACKAGES
		)
		if (
			veroscript.CAPABILITIES
			& veroscript.RuntimeCapabilities.NATIVE_TYPED_LOOPS
		):
			self.assertTrue(
				veroscript.CAPABILITIES
				& veroscript.RuntimeCapabilities.NATIVE_JIT
			)

	def test_execution_governance_configuration_and_cancellation(self) -> None:
		self.assertEqual(veroscript.ScriptCapabilities.ALL, 0x3FF)
		with make_session(
			backend=veroscript.Backend.VM,
			enable_execution_control=True,
			capability_policy_enabled=True,
			allowed_script_capabilities=veroscript.ScriptCapabilities.CONSOLE_OUTPUT,
		) as session:
			loaded = session.load_source("void main() { while (true) {} }", "cancel.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			cancellation_errors: list[BaseException] = []

			def request_from_worker() -> None:
				time.sleep(0.01)
				try:
					session.request_cancellation()
				except BaseException as error:  # Surface worker errors to unittest.
					cancellation_errors.append(error)

			worker = threading.Thread(target=request_from_worker)
			worker.start()
			result = session.run()
			worker.join(timeout=1)
			self.assertFalse(worker.is_alive(), "cancellation worker did not complete")
			self.assertEqual(cancellation_errors, [])
			self.assertFalse(result.success)

	def test_process_directory_change_requires_both_capabilities(self) -> None:
		source = 'import "io.vs"; void main() { std::filesystem::chdir("."); }'
		required = (
			veroscript.ScriptCapabilities.ENVIRONMENT_WRITE
			| veroscript.ScriptCapabilities.PROCESS_DIRECTORY_CHANGE
		)
		for allowed in (
			veroscript.ScriptCapabilities.NONE,
			veroscript.ScriptCapabilities.ENVIRONMENT_WRITE,
			veroscript.ScriptCapabilities.PROCESS_DIRECTORY_CHANGE,
			required,
		):
			with self.subTest(allowed=allowed), make_session(
				backend=veroscript.Backend.VM,
				capability_policy_enabled=True,
				allowed_script_capabilities=allowed,
			) as session:
				loaded = session.load_source(source, "directory-capability.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertEqual(result.success, allowed == required)

	def test_annotation_and_type_alias_registration(self) -> None:
		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_annotation(
				"Range",
				detail="numeric range",
				documentation="Constrains a numeric value.",
				arguments=(
					veroscript.AnnotationArgument("minimum"),
					veroscript.AnnotationArgument("maximum"),
				),
			)
			session.register_type_alias(
				"Meters", "double", documentation="Distance in meters."
			)
			loaded = session.load_source(
				"Meters Identity(Meters value) { return value; }\nvoid main() {}",
				"host-metadata.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("Identity", 2.5), 2.5)
			exported = session.bindings()
			self.assertIn("Range", str(exported))
			self.assertIn("Meters", str(exported))

	def test_annotation_name_policy(self) -> None:
		source = r'''
            @Strict @gUIwIDGET(Name=3) @STRICT @Unknown class Card {};
            void main() {
                type<Card> t=type<Card>();
                assert(t.annotation_count()==4);
                assert(t.has_annotation("Strict") && !t.has_annotation("strict"));
                assert(t.has_annotation("GUIWIDGET") && t.has_annotation("@guiwidget"));
                assert(t.has_annotation("Unknown") && !t.has_annotation("unknown"));
                assert(t.annotation(0).definition().case_sensitive());
                annotation_info a=t.annotation(1);
                assert(a.name()=="@gUIwIDGET" && a.matches_name("@GUIWIDGET"));
                assert(!a.matches_name("GUIWIDGET"));
                assert(a.definition().name()=="@GuiWidget" && !a.definition().case_sensitive());
                assert(len(a.find_arguments("Name"))==1 && len(a.find_arguments("name"))==0);
                assert(!t.annotation(2).definition().is_valid());
                assert(t.metadata("Name")=="3" && !t.has_metadata("name"));
            }
        '''
		with tempfile.TemporaryDirectory() as directory:
			path = Path(directory) / "annotation-name-policy.vs"
			path.write_text(source, encoding="utf-8")
			for backend in TEST_BACKENDS:
				with self.subTest(backend=backend), make_session(backend=backend) as session:
					session.register_annotation("@Strict")
					session.register_annotation("@GuiWidget", case_sensitive=False,
						arguments=(veroscript.AnnotationArgument("Name"),))
					policies = {item["name"]: item["caseSensitive"]
								for item in session.bindings()["annotations"]}
					self.assertEqual(policies, {"@Strict": True, "@GuiWidget": False})
					loaded = session.load_source(source, str(path))
					self.assertTrue(loaded.success, loaded.diagnostics)
					ran = session.run()
					self.assertTrue(ran.success, ran.diagnostics)
					if backend is veroscript.Backend.JIT:
						self.assertGreater(session.jit_stats["dispatches"], 0)
					data, built = session.build_package_file(path, package_name="annotation-policy")
					self.assertTrue(built.success, built.diagnostics)
				# The serialized package retains its producer's name policy.
				with make_session(backend=backend) as consumer:
					loaded = consumer.load_package(veroscript.Package(data))
					self.assertTrue(loaded.success, loaded.diagnostics)
					ran = consumer.run()
					self.assertTrue(ran.success, ran.diagnostics)

	def test_annotation_name_policy_conflicts_do_not_change_registry(self) -> None:
		for existing, sensitive, conflicting, conflict_sensitive in (
			("@Widget", False, "@wIDGET", True),
			("@Widget", True, "@wIDGET", False),
			("@Widget", True, "@Widget", True),
		):
			with self.subTest(sensitive=sensitive, conflict_sensitive=conflict_sensitive), make_session() as session:
				session.register_annotation(existing, case_sensitive=sensitive)
				before = session.bindings()
				with self.assertRaises(veroscript.Error):
					session.register_annotation(conflicting, case_sensitive=conflict_sensitive)
				self.assertEqual(session.bindings(), before)
				session.register_annotation("@Other")

	@unittest.skipUnless(TEST_ANNOTATION_LIBRARY, "Native annotation service fixture was not built")
	def test_annotation_name_policy_reaches_native_processor(self) -> None:
		producer = ctypes.CDLL(TEST_ANNOTATION_LIBRARY)
		producer.CreatePythonAnnotationService.argtypes = [ctypes.c_uint32]
		producer.CreatePythonAnnotationService.restype = ctypes.c_void_p
		producer.ReleasePythonAnnotationService.argtypes = [ctypes.c_void_p]
		producer.ReleasePythonAnnotationService.restype = None
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				session.register_annotation("@PythonGenerated", case_sensitive=False)
				handle = producer.CreatePythonAnnotationService(int(veroscript.DiagnosticSeverity.WARNING))
				self.assertTrue(handle)
				try:
					session.set_annotation_processing_service(handle)
				finally:
					producer.ReleasePythonAnnotationService(handle)
				loaded = session.load_source(
					"struct Generated { @pYTHONgENERATED int value=42; }; "
					"int read() { Generated value=Generated(); return value.GeneratedValue(); }",
					"annotation-name-processor.vs",
				)
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("read"), 42)

	def test_annotation_name_policy_revalidates_session_after_conversion(self) -> None:
		with make_session() as session:
			class CloseDuringConversion:
				def __bool__(self):
					session.close()
					return False

			with self.assertRaisesRegex(veroscript.Error, "session is closed"):
				session.register_annotation("@Widget", case_sensitive=CloseDuringConversion())
			self.assertTrue(session.closed)

	def test_native_type_stub_is_deployed(self) -> None:
		package_directory = Path(veroscript.__file__).resolve().parent
		self.assertTrue((package_directory / "_veroscript.pyi").is_file())

	def test_wide_string_round_trip(self) -> None:
		value = veroscript.Value("VeroScript 世界", "wstring")
		self.assertEqual(value.value, "VeroScript 世界")
		value.value = "更新后的文本"
		self.assertEqual(value.value, "更新后的文本")

	def test_null_value_round_trip(self) -> None:
		self.assertEqual(veroscript.Value().kind, "invalid")
		value = veroscript.Value(None)
		self.assertEqual(value.kind, "null")
		self.assertIsNone(value.value)
		value.value = None
		self.assertEqual(value.kind, "null")
		self.assertIsNone(value.to_python())
		with self.assertRaises(TypeError):
			veroscript.Value(None, "int")
		integer = veroscript.Value(1, "int32")
		with self.assertRaises(TypeError):
			integer.value = None

	def test_session_callback_cycle_is_collectable(self) -> None:
		session = make_session(backend=veroscript.Backend.VM)
		session.set_mutation_callback(
			lambda _event, owner=session: owner.closed
		)
		reference = weakref.ref(session)
		del session
		gc.collect()
		self.assertIsNone(reference())

	def test_session_values_calls_and_objects(self) -> None:
		output: list[str] = []
		with make_session(
			backend=veroscript.Backend.VM,
			hot_reload_enabled=True,
			print_callback=output.append,
		) as session:
			loaded = session.load_source(SOURCE, "python-test.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("Add", 20, 22), 42)

			mutable = veroscript.Value(4, "int32")
			session.call("Bump", mutable)
			self.assertEqual(mutable.value, 5)

			counter = session.construct("Counter", 10)
			self.assertEqual(session.call_method(counter, "Add", 7), 17)
			self.assertEqual(session.get_field(counter, "value"), 17)

			self.assertEqual(session.get_global("SharedValue"), 9)
			session.set_global("SharedValue", 11)
			self.assertEqual(session.get_global("SharedValue"), 11)

			ran = session.run()
			self.assertTrue(ran.success, ran.diagnostics)
			self.assertIn("python-ok", output)

	def test_instance_call_interceptor_and_bypass(self) -> None:
		contexts: list[veroscript.InterceptContext] = []
		with make_session(backend=veroscript.Backend.VM) as session:
			loaded = session.load_source(SOURCE, "python-interceptor.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			counter = session.construct("Counter", 10)

			def replace(context: veroscript.InterceptContext) -> object:
				contexts.append(context)
				self.assertEqual(context.target.method_name, "Add")
				self.assertEqual(context.target.declaring_type_short_name, "Counter")
				self.assertEqual(context.arguments, (7,))
				return veroscript.InterceptResult.HANDLED, 99

			session.set_instance_call_interceptor(replace)
			self.assertEqual(session.call_method(counter, "Add", 7), 99)
			self.assertEqual(session.get_field(counter, "value"), 10)
			self.assertEqual(
				session.call_method_bypassing_interceptor(counter, "Add", 2), 12
			)

			def proceed(context: veroscript.InterceptContext) -> object:
				self.assertEqual(context.proceed(), 15)
				return veroscript.InterceptResult.HANDLED

			session.set_instance_call_interceptor(proceed)
			self.assertEqual(session.call_method(counter, "Add", 3), 15)

			session.set_instance_call_interceptor(
				lambda _context: veroscript.InterceptResult.UNHANDLED
			)
			self.assertEqual(session.call_method(counter, "Add", 1), 16)

			with self.assertRaises(veroscript.Error):
				contexts[0].proceed()

			def fail(_context: veroscript.InterceptContext) -> object:
				raise LookupError("interceptor failed")

			session.set_instance_call_interceptor(fail)
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.call_method(counter, "Add", 1)
			self.assertIsInstance(raised.exception.args[0], LookupError)
			session.set_instance_call_interceptor(None)
			self.assertEqual(session.call_method(counter, "Add", 1), 17)

	def test_interceptor_fixed_array_return_ownership(self) -> None:
		source = (
			"int copies=0;int destroyed=0;"
			"struct S{int value=7;S()=default;"
			"S(const S& other){value=other.value;copies++;}~S(){destroyed++;}};"
			"using Values=S[2];class Maker{public:Values values;Values Read(){return values;}};"
			"Maker maker=Maker();"
			"int take(){copies=0;destroyed=0;Values result=maker.Read();result[0].value=9;"
			"return copies*100+maker.values[0].value;}"
			"int disposed(){return destroyed;}void main(){}"
		)
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(
				backend=backend, hot_reload_enabled=True
			) as session:
				loaded = session.load_source(source, "python-intercept-fixed-array.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertTrue(session.run(veroscript.RunMode.EVAL).success)

				def forward(context: veroscript.InterceptContext) -> veroscript.InterceptResult:
					context.proceed()
					return veroscript.InterceptResult.HANDLED

				session.set_instance_call_interceptor(forward)
				self.assertEqual(session.call("take"), 207)
				self.assertEqual(session.call("disposed"), 2)

				session.set_instance_call_interceptor(
					lambda context: (veroscript.InterceptResult.HANDLED, context.proceed())
				)
				self.assertEqual(session.call("take"), 407)
				self.assertEqual(session.call("disposed"), 4)

				def fail(context: veroscript.InterceptContext) -> veroscript.InterceptResult:
					context.proceed()
					return veroscript.InterceptResult.FAILED

				session.set_instance_call_interceptor(fail)
				with self.assertRaises(veroscript.Error):
					session.call("take")
				self.assertEqual(session.call("disposed"), 2)

	def test_interceptor_proceed_requires_callback_thread(self) -> None:
		with make_session(backend=veroscript.Backend.VM) as session:
			loaded = session.load_source(
				"class Counter{public:int value=0;int bump(){return ++value;}};",
				"interceptor-thread.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			instance = session.construct("Counter")
			errors, returned, contexts = [], [], []

			def intercept(context):
				contexts.append(context)

				def worker():
					try:
						returned.append(context.proceed())
					except Exception as error:
						errors.append(error)

				thread = threading.Thread(target=worker)
				thread.start()
				thread.join()
				return (veroscript.InterceptResult.HANDLED if returned
						else veroscript.InterceptResult.UNHANDLED)

			session.set_instance_call_interceptor(intercept)
			self.assertEqual(session.call_method(instance, "bump"), 1)
			self.assertEqual(returned, [])
			self.assertEqual(len(errors), 1)
			self.assertIsInstance(errors[0], veroscript.ThreadError)
			with self.assertRaisesRegex(veroscript.Error, "expired"):
				contexts[0].proceed()

			session.set_instance_call_interceptor(
				lambda context: (veroscript.InterceptResult.HANDLED, context.proceed())
			)
			self.assertEqual(session.call_method(instance, "bump"), 2)
			self.assertEqual(session.get_field(instance, "value"), 2)

	def test_value_setter_reports_closed_module_owner(self) -> None:
		with make_session(backend=veroscript.Backend.VM) as session:
			self.assertTrue(session.load_source("class Item{}; Item make(){return Item();}").success)
			value = session.call("make")
		self.assertEqual(value.kind, "invalid")
		for replacement in (None, 7):
			with self.subTest(replacement=replacement), self.assertRaises(veroscript.Error):
				value.value = replacement

	def test_value_setter_reports_foreign_thread(self) -> None:
		with make_session(backend=veroscript.Backend.VM) as session:
			self.assertTrue(session.load_source("class Item{}; Item make(){return Item();}").success)
			value = session.call("make")
			errors = []

			def worker():
				for replacement in (None, 7):
					try:
						value.value = replacement
					except Exception as error:
						errors.append(error)

			thread = threading.Thread(target=worker)
			thread.start()
			thread.join()
			self.assertEqual(len(errors), 2)
			self.assertTrue(all(isinstance(error, veroscript.ThreadError) for error in errors))
			self.assertEqual(value.kind, "object")

	def test_diagnostics_are_structured(self) -> None:
		veroscript.set_language("en-US")
		with make_session(backend=veroscript.Backend.VM) as session:
			result = session.load_source("void main() { MissingCall(); }", "bad-python.vs")
			self.assertFalse(result.success)
			self.assertTrue(result.diagnostics)
			self.assertEqual(result.diagnostics[0].file_label, "bad-python.vs")
			self.assertTrue(result.diagnostics[0].message)
			self.assertNotEqual(result.diagnostics[0].message, result.diagnostics[0].message_id)
			self.assertIn("unknown function", result.diagnostics[0].message)
			self.assertEqual(result.diagnostics[0].arguments, ("MissingCall",))

	def test_application_localization_catalog_merge(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			language_directory = Path(directory) / "en-US"
			language_directory.mkdir()
			(language_directory / "python-module.json").write_text(
				'{"python.module.message": "Python module message"}',
				encoding="utf-8",
			)
			veroscript.merge_catalog_directory(directory)

	def test_loaded_locale_uses_runtime_localization(self) -> None:
		try:
			veroscript.set_language("fr-FR")
			with make_session(backend=veroscript.Backend.VM) as session:
				result = session.load_source("void main() { MissingCall(); }")
				self.assertFalse(result.success)
				self.assertEqual(
					result.diagnostics[0].message_id, "diag.unknown_function"
				)
				self.assertIn("fonction inconnue", result.diagnostics[0].message)
		finally:
			veroscript.set_language("en-US")

	def test_session_rejects_foreign_thread(self) -> None:
		session = make_session(backend=veroscript.Backend.VM)
		errors: list[BaseException] = []

		def use_from_worker() -> None:
			try:
				session.load_source("void main() {}")
			except BaseException as error:
				errors.append(error)

		worker = threading.Thread(target=use_from_worker)
		worker.start()
		worker.join()
		session.close()
		self.assertEqual(len(errors), 1)
		self.assertIsInstance(errors[0], veroscript.ThreadError)

	def test_mixed_and_jit_backends(self) -> None:
		source = """
        int Add(int left, int right) { return left + right; }
        int DirectLeaf(int value) {
            for (int index = 0; index < 4; index++) {
                value = (value * 3 + 1) % 10007;
            }
            return value;
        }
        int RunDirectStats(int value) {
            int total = 0;
            for (int index = 0; index < 3; index++) {
                total += DirectLeaf(value + index);
            }
            return total;
        }
        int FastContainers() {
            array<int> values = array<int>();
            values.push(4);
            values.push(5);
            set<int> unique = set<int>();
            unique.add(4);
            unique.add(5);
            return values[0] + values[1] + (unique.has(5) ? 1 : 0);
        }
        void main() {}
        """
		native_codegen_available = TEST_JIT_INFO.native_code
		native_scalar_cfg_available = native_codegen_available and (
			veroscript.HOST_PLATFORM.endswith("-x64")
			or veroscript.HOST_PLATFORM.endswith("-arm64")
		)
		native_typed_loops_available = bool(
			TEST_JIT_INFO.capabilities & veroscript.JitBackendCapabilities.NATIVE_TYPED_LOOPS
		)
		for backend in TEST_BACKENDS[1:]:
			with self.subTest(backend=backend), make_session(
				backend=backend
			) as session:
				self.assertTrue(session.load_source(source, "backend-test.vs").success)
				for _ in range(40):
					self.assertEqual(session.call("Add", 2, 3), 5)
					self.assertEqual(session.call("FastContainers"), 10)
				self.assertEqual(session.call("RunDirectStats", 2), 849)
				stats = session.jit_stats
				for name in (
					"scalar_method_fast_path_mixed",
					"scalar_function_fast_path_mixed",
					"direct_native_calls",
					"cost_model_vm_fallbacks",
					"native_typed_loop_executions",
				):
					self.assertIn(name, stats)
					self.assertIsInstance(stats[name], int)
					self.assertGreaterEqual(stats[name], 0)
				if native_scalar_cfg_available:
					self.assertGreater(stats["direct_native_calls"], 0)

		if native_scalar_cfg_available:
			cost_model_source = """
            int PartiallyTyped(int n) {
                array<int> values = array<int>();
                values.reserve(64);
                for (int i = 0; i < 64; i++) values.push(i);
                uint32 state = 17u;
                int total = 0;
                for (int i = 0; i < n; i++) {
                    state = state ^ (state << 13u);
                    state = state ^ (state >> 17u);
                    state = state ^ (state << 5u);
                    total += values[to_int(state & 63u)];
                    print(total);
                }
                return total;
            }
            void main() {}
            """
			with make_session(backend=veroscript.Backend.JIT) as session:
				loaded = session.load_source(
					cost_model_source, "cost-model-stats-test.vs"
				)
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("PartiallyTyped", 0), 0)
				self.assertGreater(session.jit_stats["cost_model_vm_fallbacks"], 0)

		if native_typed_loops_available:
			typed_loop_source = """
            interface ICounter { virtual int update(int salt) = 0; };
            class Counter : ICounter {
            public:
                int value = 1;
                int update(int salt) {
                    value = (value + (salt % 17) + 1) % 1000000007;
                    return value;
                }
            };
            int RunBound(int limit) {
                Counter counter = Counter();
                int state = 7;
                for (int i = 0; i < limit; i++) {
                    state = (state * 75 + 74) % 65521;
                    counter.update(state);
                }
                return counter.value + state;
            }
            int RunVirtual(int limit) {
                ICounter counter = Counter();
                int state = 7;
                int value = 1;
                for (int i = 0; i < limit; i++) {
                    state = (state * 75 + 74) % 65521;
                    value = counter.update(state);
                }
                return value + state;
            }
            void main() { print(RunBound(10000)); print(RunVirtual(10000)); }
            """
			output: list[str] = []
			with make_session(
				backend=veroscript.Backend.MIXED,
				print_callback=output.append,
			) as session:
				loaded = session.load_source(
					typed_loop_source, "typed-loop-stats-test.vs"
				)
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(output, ["95018", "95018"])
				self.assertGreaterEqual(
					session.jit_stats["native_typed_loop_executions"], 2
				)

	def test_nested_exit_propagates_across_backends(self) -> None:
		source = """
        int StopWithValue() { exit(13); return 0; }
        int Invoke() { return 1 + StopWithValue(); }
        void main() { print(Invoke()); assert(false); }
        """
		for backend in TEST_BACKENDS:
			output: list[str] = []
			with self.subTest(backend=backend), make_session(
				backend=backend,
				print_callback=output.append,
			) as session:
				loaded = session.load_source(source, "nested-exit-test.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(result.exit_code, 13)
				self.assertEqual(output, [])

	def test_jit_statistics_count_actual_entries_per_invocation(self) -> None:
		source = """
        int Answer() { return 7; }
        int Sum(int n) { int total = 0; for (int i = 0; i < n; i++) total += i; return total; }
        int Protected() { try { return 9; } finally { int ignored = 1; } }
        int Divide(int left, int right) { return left / right; }
        void main() {}
        """
		execution_keys = (
			"native_leaf_executions", "native_cfg_executions", "bridge_executions",
			"linear_executions", "vm_trampoline_executions",
			"controlled_native_executions", "aot_executions",
		)
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				session.set_jit_policy(veroscript.JitPolicy(
					mixed_hot_call_threshold=1, max_compile_attempts=128,
					max_cached_entries=128, eager_compile_budget=128,
					reuse_session_code=True, enable_controlled_mixed=True,
				))
				loaded = session.load_source(source, "python-jit-execution-statistics.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				for key in execution_keys:
					self.assertEqual(session.jit_stats[key], 0)
				for call in range(2):
					self.assertEqual(session.call("Answer"), 7)
					stats = session.jit_stats
					for key in execution_keys:
						self.assertIs(type(stats[key]), int)
						self.assertGreaterEqual(stats[key], 0)
					self.assertEqual(stats["scalar_native_fast_path_calls"], 0)
					self.assertEqual(stats["aot_executions"], 0)
					if backend is veroscript.Backend.VM or TEST_JIT_BACKEND:
						self.assertEqual(sum(stats[key] for key in execution_keys),
							0 if backend is veroscript.Backend.VM else 1)
					if call == 0:
						cold = session.jit_cache_stats
					elif TEST_JIT_BACKEND:
						self.assertEqual(stats["compile_attempts"], 0)
						self.assertEqual(session.jit_cache_stats.compile_attempts, cold.compile_attempts)
				self.assertEqual(session.call("Sum", 16), 120)
				self.assertEqual(session.call("Protected"), 9)
				with self.assertRaises(veroscript.Error):
					session.call("Divide", 17, 0)
				failed = session.jit_stats
				if backend is veroscript.Backend.VM or TEST_JIT_BACKEND:
					self.assertEqual(sum(failed[key] for key in execution_keys),
						0 if backend is veroscript.Backend.VM else 1)
				failed_snapshot = dict(failed)
				session.clear()
				for key in execution_keys:
					self.assertEqual(session.jit_stats[key], 0)
				# Previously returned mappings are owning snapshots, not live counters.
				self.assertEqual(dict(failed), failed_snapshot)

	@unittest.skipUnless(TEST_JIT_BACKEND, "requires an explicitly selected JIT backend")
	def test_session_jit_cache_policy_limits_and_statistics(self) -> None:
		source = """
        int Triple(int value) { return value * 3; }
        int Cached(int value) { return Triple(value) + 1; }
        void main() {}
        """
		with make_session(backend=veroscript.Backend.JIT) as session:
			self.assertEqual(
				session.jit_cache_limits,
				veroscript.JitCacheLimits(),
			)
			limits = veroscript.JitCacheLimits(
				max_native_bytes=4 * 1024 * 1024,
				max_retained_payload_bytes=16 * 1024 * 1024,
			)
			session.set_jit_cache_limits(limits)
			self.assertEqual(session.jit_cache_limits, limits)

			# Calling set_jit_policy with public defaults must preserve the
			# optimized session-code reuse default.
			session.set_jit_policy(veroscript.JitPolicy())
			loaded = session.load_source(source, "python-session-jit-cache.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("Cached", 9), 28)
			cold = session.jit_cache_stats
			self.assertIsInstance(cold.compile_attempts, int)
			self.assertIsInstance(cold.retained_payload_bytes, int)
			self.assertIsInstance(cold.native_bytes, int)
			self.assertIsInstance(cold.resource_limit_reached, bool)

			self.assertEqual(session.call("Cached", 9), 28)
			self.assertEqual(session.jit_stats["compile_attempts"], 0)
			warm = session.jit_cache_stats
			self.assertEqual(warm.compile_attempts, cold.compile_attempts)
			if cold.compile_attempts > 0:
				self.assertGreater(cold.retained_payload_bytes, 0)

			session.set_jit_policy(
				veroscript.JitPolicy(reuse_session_code=False)
			)
			self.assertEqual(
				session.jit_cache_stats,
				veroscript.JitCacheStats(0, 0, 0, False),
			)
			self.assertEqual(session.call("Cached", 9), 28)
			uncached_attempts = session.jit_stats["compile_attempts"]
			self.assertEqual(session.call("Cached", 9), 28)
			if uncached_attempts > 0:
				self.assertGreater(session.jit_stats["compile_attempts"], 0)

			if cold.compile_attempts > 0:
				session.set_jit_policy(veroscript.JitPolicy())
				session.set_jit_cache_limits(
					veroscript.JitCacheLimits(
						max_native_bytes=4 * 1024 * 1024,
						max_retained_payload_bytes=1,
					)
				)
				self.assertEqual(session.call("Cached", 9), 28)
				limited = session.jit_cache_stats
				self.assertTrue(limited.resource_limit_reached)
				self.assertEqual(limited.retained_payload_bytes, 0)
				self.assertEqual(limited.native_bytes, 0)

	@unittest.skipUnless(TEST_JIT_BACKEND, "requires an explicitly selected JIT backend")
	def test_jit_polymorphic_dispatch_fast_path(self) -> None:
		source = """
        interface IDispatch { virtual int add(int value) = 0; };
        class AddOne : IDispatch {
            int add(int value) { return value + 1; }
        };
        class AddTwo : IDispatch {
            int add(int value) { return value + 2; }
        };
        int RunDispatchStats() {
            IDispatch first = AddOne();
            IDispatch second = AddTwo();
            IDispatch selected = first;
            int total = 0;
            for (int index = 0; index < 16; index++) {
                if ((index & 1) == 0) selected = first;
                else selected = second;
                total = selected.add(total);
            }
            return total;
        }
        void main() {}
        """
		with make_session(backend=veroscript.Backend.JIT) as session:
			loaded = session.load_source(source, "python-jit-dispatch.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("RunDispatchStats"), 24)
			stats = session.jit_stats
			self.assertGreaterEqual(stats["dispatch_attempts"], stats["dispatches"])
			self.assertGreaterEqual(
				stats["dispatches"], stats["scalar_method_fast_path_i32"]
			)
			native_scalar_cfg_available = TEST_JIT_INFO.native_code and veroscript.HOST_PLATFORM.endswith(("-x64", "-arm64"))
			# Portable builds must still execute the dispatch correctly, while
			# native scalar CFG builds additionally exercise this fast path.
			if native_scalar_cfg_available:
				self.assertGreaterEqual(stats["scalar_method_fast_path_i32"], 16)

	def test_python_callback_exception_does_not_cross_c_abi(self) -> None:
		def fail_callback(_: str) -> None:
			raise ValueError("callback failed")

		with make_session(
			backend=veroscript.Backend.VM,
			print_callback=fail_callback,
		) as session:
			self.assertTrue(session.load_source("void main() { print(1); }").success)
			with self.assertRaises(veroscript.CallbackError):
				session.run()

	def test_typed_python_host_functions_and_ref_writeback(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(
				backend=backend
			) as session:
				session.register_function(
					"py_add",
					lambda left, right: left + right,
					return_type="int",
					parameters=("int", "int"),
				)

				@session.host_function(
					"py_bump",
					parameters=(veroscript.HostParameter("int", by_ref=True),),
				)
				def py_bump(value: veroscript.Value) -> None:
					value.value += 10

				source = """
                int UsePython(int value) {
                    py_bump(ref value);
                    return py_add(value, 2);
                }
                int UseCapturedPython(int value) {
                    auto bump_capture=[&captured=value]()->int {
                        py_bump(ref captured);
                        return captured;
                    };
                    int result=bump_capture();
                    assert(value==result);
                    return py_add(result,2);
                }
                void main() {}
                """
				loaded = session.load_source(source, "python-host-function.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("UsePython", 5), 17)
				self.assertEqual(session.call("UseCapturedPython", 5), 17)

	def test_native_signature_nesting_is_bounded_before_registration(self) -> None:
		class NestedTypeHost:
			@staticmethod
			def echo(value):
				return value

		with make_session(backend=veroscript.Backend.VM) as session:
			# Depth starts at zero, so 31 containers plus their scalar leaf are
			# the ABI's 32 accepted descriptor levels.
			accepted = "array<" * 31 + "int" + ">" * 31
			session.register_function(
				"accepted_depth", lambda value: value,
				return_type=accepted, parameters=(accepted,),
			)
			binding = session.register_class("NestedTypeHost", NestedTypeHost)
			binding.static_method("Accepted", "echo", return_type=accepted, parameters=(accepted,))
			for depth in (32, 4096):
				rejected = "array<" * depth + "int" + ">" * depth
				with self.subTest(depth=depth, signature="function return"):
					with self.assertRaises(ValueError):
						session.register_function("rejected_return", lambda: None, return_type=rejected)
				with self.subTest(depth=depth, signature="function parameter"):
					with self.assertRaises(ValueError):
						session.register_function("rejected_parameter", lambda value: None, parameters=(rejected,))
				with self.subTest(depth=depth, signature="member return"):
					with self.assertRaises(ValueError):
						binding.static_method("RejectedReturn", "echo", return_type=rejected)
				with self.subTest(depth=depth, signature="member parameter"):
					with self.assertRaises(ValueError):
						binding.static_method("RejectedParameter", "echo", parameters=(rejected,))

	def test_scalar_aliases_in_python_host_function_signatures(self) -> None:
		aliases = {
			"char": 7,
			"unsigned char": 7,
			"short": 700,
			"unsigned short": 700,
			"unsigned int": 70_000,
			"long": 5_000_000_001,
			"unsigned long": 5_000_000_001,
		}
		for type_name, value in aliases.items():
			with self.subTest(type_name=type_name), make_session(
				backend=veroscript.Backend.VM
			) as session:
				session.register_function(
					"py_echo",
					lambda argument: argument,
					return_type=type_name,
					parameters=(type_name,),
				)
				loaded = session.load_source(
					f"{type_name} UseAlias({type_name} value) "
					"{ return py_echo(value); } void main() {}",
					"python-host-scalar-alias.vs",
				)
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("UseAlias", value), value)

	def test_script_worker_invokes_python_host_function_with_gil(self) -> None:
		owner_thread = threading.get_ident()
		callback_threads: list[int] = []
		output: list[str] = []

		def python_worker_value() -> int:
			# Executing Python operations here also verifies that the native
			# worker trampoline acquired the GIL before entering the callback.
			callback_threads.append(threading.get_ident())
			return 42

		source = """
        import "concurrent.vs";
        export void invoke_python_worker(channel<int> result) {
            result.send(python_worker_value());
        }
        void main() {
            task<int> work = std::concurrent::spawn_task<int>("invoke_python_worker");
            print(work.await());
        }
        """
		with make_session(
			backend=veroscript.Backend.VM,
			print_callback=output.append,
		) as session:
			session.register_function(
				"python_worker_value",
				python_worker_value,
				return_type="int",
			)
			loaded = session.load_source(source, "python-worker-callback.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			ran = session.run()
			self.assertTrue(ran.success, ran.diagnostics)

		self.assertEqual(output, ["42"])
		self.assertEqual(len(callback_threads), 1)
		self.assertNotEqual(callback_threads[0], owner_thread)

	def test_callable_tasks_and_reflection_descriptors_cross_python_api(self) -> None:
		source = """
        import "concurrent.vs";
        int twice(int value)
        {
            return value * 2;
        }
        int task_result()
        {
            task<int> work = std::concurrent::spawn_task<int>(twice, 21);
            return work.await();
        }
        class Counter
        {
        public:
            int value = 7;
        };
        field_info<Counter> make_info()
        {
            return field_info<Counter>(type<Counter>().qualified_name, 0);
        }
        int inspect(field_info<Counter> info)
        {
            Counter item = Counter();
            return info.get<int>(item);
        }
        void main()
        {
        }
        """
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(
				backend=backend
			) as owner:
				loaded = owner.load_source(source, "python-new-core.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(owner.call("task_result"), 42)
				descriptor = owner.call("make_info")
				self.assertIsInstance(descriptor, veroscript.Value)
				self.assertEqual(descriptor.kind, "object")
				self.assertEqual(owner.call("inspect", descriptor), 7)
				with make_session(backend=backend) as foreign:
					loaded = foreign.load_source(source, "python-new-core.vs")
					self.assertTrue(loaded.success, loaded.diagnostics)
					with self.assertRaisesRegex(
						veroscript.VeroScriptError,
						"foreign reflection descriptor|value belongs to a foreign session",
					):
						foreign.call("inspect", descriptor)

	def test_script_worker_python_exception_is_preserved(self) -> None:
		def fail_from_worker() -> int:
			raise ValueError("python worker failed")

		source = """
        import "concurrent.vs";
        export void invoke_failing_python_worker(channel<int> result) {
            result.send(fail_from_worker());
        }
        void main() {
            task<int> work = std::concurrent::spawn_task<int>(
                "invoke_failing_python_worker");
            int ignored = work.await();
        }
        """
		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_function(
				"fail_from_worker",
				fail_from_worker,
				return_type="int",
			)
			loaded = session.load_source(source, "python-worker-exception.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			with self.assertRaisesRegex(
				veroscript.CallbackError, "python worker failed"
			):
				session.run()

	def test_retained_function_closure_and_ref_writeback(self) -> None:
		source = """
        int ScriptRef(ref int value) { value += 3; return value; }
        function(ref int)->int GetRefCallable() { return ScriptRef; }
        function(int)->int MakeAdder(int base) {
            return [base](int value) -> int { return base + value; };
        }
        void main() {}
        """
		with make_session(backend=veroscript.Backend.MIXED) as session:
			loaded = session.load_source(source, "python-callable.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)

			function_value = session.call("GetRefCallable")
			self.assertIsInstance(function_value, veroscript.Value)
			with session.retain_callable(function_value) as callable_value:
				mutable = veroscript.Value(4, "int32")
				self.assertEqual(callable_value(mutable), 7)
				self.assertEqual(mutable.value, 7)
				self.assertTrue(callable_value.is_valid)

				errors: list[BaseException] = []

				def invoke_from_worker() -> None:
					try:
						callable_value(1)
					except BaseException as error:
						errors.append(error)

				worker = threading.Thread(target=invoke_from_worker)
				worker.start()
				worker.join()
				self.assertEqual(len(errors), 1)
				self.assertIsInstance(errors[0], veroscript.ThreadError)

			closure_value = session.call("MakeAdder", 10)
			closure = session.retain_callable(closure_value)
			self.assertEqual(closure(5), 15)
			session.close()
			self.assertFalse(closure.is_valid)
			with self.assertRaises(veroscript.Error):
				closure(1)
			closure.close()

	def test_initialized_lambda_captures_keep_shared_and_independent_state(self) -> None:
		source = """
        function()->int make_counter(int seed) {
            return [count = seed]() mutable -> int { return ++count; };
        }
        function()->int make_reference() {
            int seed = 40;
            return [&value = seed]() -> int { return ++value; };
        }
        int invoke(function()->int operation) { return operation(); }
        """
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				loaded = session.load_source(source, "python-initialized-captures.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				first = session.call("make_counter", 40)
				alias = first.clone()
				other = session.call("make_counter", 100)
				with session.retain_callable(first) as retained:
					del first
					gc.collect()
					self.assertEqual(retained(), 41)
					self.assertEqual(session.call("invoke", alias), 42)
					self.assertEqual(retained(), 43)
					self.assertEqual(session.call("invoke", other), 101)
				reference = session.call("make_reference")
				with session.retain_callable(reference) as retained:
					del reference
					gc.collect()
					self.assertEqual(retained(), 41)
					self.assertEqual(retained(), 42)
					session.close()
					self.assertFalse(retained.is_valid)

	def test_local_static_storage_follows_execution_state_lifetime(self) -> None:
		source = """
        int next_static(int seed) { static int count = seed; return ++count; }
        function()->int make_counter(int seed) {
            return [seed]() -> int { static int count = seed; return ++count; };
        }
        int batch() {
            auto first = make_counter(10);
            auto second = make_counter(90);
            return first() * 10000 + second() * 100 + first();
        }
        class Lifetime { public: ~Lifetime() { notify_destroyed(); } };
        void touch_static() { static Lifetime instance = Lifetime(); }
        """
		for backend in TEST_BACKENDS:
			for persistent in (False, True):
				destroyed = []
				with self.subTest(backend=backend, persistent=persistent):
					with make_session(backend=backend, hot_reload_enabled=persistent) as session:
						session.register_function("notify_destroyed", lambda: destroyed.append(True))
						loaded = session.load_source(source, "python-local-statics.vs")
						self.assertTrue(loaded.success, loaded.diagnostics)
						self.assertEqual(session.call("next_static", 30), 31)
						self.assertEqual(session.call("next_static", 99), 32 if persistent else 100)
						self.assertEqual(session.call("batch"), 111213)
						self.assertEqual(session.call("batch"), 141516 if persistent else 111213)
						session.call("touch_static")
						session.call("touch_static")
						self.assertEqual(len(destroyed), 0 if persistent else 2)
					self.assertEqual(len(destroyed), 1 if persistent else 2)

	def test_external_object_serialization_and_defaults(self) -> None:
		serialized_views: list[tuple[str, int]] = []

		def serialize(
			type_name: str, mutation_version: int, fields: dict[str, object]
		) -> bytes:
			serialized_views.append((type_name, mutation_version))
			return int(fields["value"]).to_bytes(4, "little", signed=True)

		def deserialize(data: bytes, encoded_version: int) -> dict[str, object]:
			self.assertEqual(encoded_version, 3)
			return {"value": int.from_bytes(data, "little", signed=True)}

		source = """
        class PersistedCounter {
        public:
            int value = 5;
            PersistedCounter(int initial) { value = initial; }
        };
        void main() {}
        """
		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_serializer(
				"PersistedCounter", 3, serialize, deserialize
			)
			loaded = session.load_source(source, "python-serialization.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			instance = session.construct("PersistedCounter", 19)
			data, version = session.serialize_object(instance)
			self.assertEqual(version, 3)
			self.assertEqual(int.from_bytes(data, "little", signed=True), 19)
			self.assertTrue(serialized_views[0][0].endswith("PersistedCounter"))

			session.set_field(instance, "value", 1)
			session.deserialize_object(instance, data, version)
			self.assertEqual(session.get_field(instance, "value"), 19)

			defaults = session.apply_class_defaults(instance)
			self.assertTrue(defaults.success, defaults.diagnostics)
			self.assertEqual(session.get_field(instance, "value"), 19)

	def test_serializer_exception_is_preserved(self) -> None:
		def fail_serialize(
			_type_name: str, _mutation_version: int, _fields: dict[str, object]
		) -> bytes:
			raise LookupError("serialization callback failed")

		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_serializer(
				"SerializedFailure",
				1,
				fail_serialize,
				lambda _data, _version: {},
			)
			loaded = session.load_source(
				"class SerializedFailure { public: int value; "
				"SerializedFailure() { value = 1; } }; void main() {}"
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			instance = session.construct("SerializedFailure")
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.serialize_object(instance)
			self.assertIsInstance(raised.exception.args[0], LookupError)

	def test_structured_mutation_callback_and_exception(self) -> None:
		events: list[veroscript.MutationEvent] = []
		source = """
        class MutableCounter {
        public:
            int value;
            MutableCounter(int initial) { value = initial; }
        };
        void main() {}
        """
		with make_session(
			backend=veroscript.Backend.VM, hot_reload_enabled=True
		) as session:
			session.set_mutation_callback(events.append)
			loaded = session.load_source(source, "python-mutation.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			instance = session.construct("MutableCounter", 1)
			events.clear()
			session.set_field(instance, "value", 8)
			self.assertTrue(events)
			event = events[-1]
			self.assertEqual(event.kind, veroscript.MutationKind.FIELD_WRITE)
			self.assertEqual(event.field_name, "value")
			self.assertEqual(event.value, 8)
			self.assertGreater(event.object_mutation_version, 0)

			def fail_mutation(_event: veroscript.MutationEvent) -> None:
				raise RuntimeError("mutation callback failed")

			session.set_mutation_callback(fail_mutation)
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.set_field(instance, "value", 9)
			self.assertIsInstance(raised.exception.args[0], RuntimeError)
			session.set_mutation_callback(None)

	def test_object_reference_enumeration_returns_owned_values(self) -> None:
		source = """
        class ReferencedObject {
        public:
            int value;
            ReferencedObject(int initial) { value = initial; }
        };
        class ReferenceHolder {
        public:
            ReferencedObject child;
            ReferenceHolder(ReferencedObject value) { child = value; }
        };
        void main() {}
        """
		with make_session(backend=veroscript.Backend.VM) as session:
			loaded = session.load_source(source, "python-references.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			child = session.construct("ReferencedObject", 12)
			holder = session.construct("ReferenceHolder", child)
			references = session.enumerate_object_references(holder)
			self.assertEqual(len(references), 1)
			self.assertEqual(
				references[0].kind, veroscript.ExternalReferenceKind.SCRIPT_OBJECT
			)
			self.assertIsInstance(references[0].value, veroscript.Value)
			self.assertEqual(
				session.get_field(references[0].value, "value"), 12
			)

	def test_python_host_function_exception_is_preserved(self) -> None:
		def fail() -> int:
			raise LookupError("host callback failed")

		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_function("py_fail", fail, return_type="int")
			loaded = session.load_source("void main() { py_fail(); }")
			self.assertTrue(loaded.success, loaded.diagnostics)
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.run()
			self.assertIsInstance(raised.exception.args[0], LookupError)

	def test_python_host_callbacks_support_objects_and_containers(self) -> None:
		class Box:
			def __init__(self, value: int) -> None:
				self.value = value

		def echo_box(value: Box) -> Box:
			self.assertIsInstance(value, Box)
			return value

		def echo_array(value: object) -> object:
			self.assertIsInstance(value, veroscript.Value)
			return value

		with make_session(backend=veroscript.Backend.VM) as session:
			(
				session.register_class("PythonBox", Box)
				.constructor(parameters=("int",))
				.field("value", type_name="int")
			)
			session.register_function(
				"python_echo_box",
				echo_box,
				return_type="PythonBox",
				parameters=("PythonBox",),
			)
			session.register_function(
				"python_echo_array",
				echo_array,
				return_type="array<int>",
				parameters=("array<int>",),
			)
			loaded = session.load_source(
				"""
                int ReadPythonBox() {
                    PythonBox box = PythonBox(23);
                    return python_echo_box(box).value;
                }
                int ReadPythonArray() {
                    array<int> values = array<int>();
                    values.push(4);
                    values.push(9);
                    return python_echo_array(values)[1];
                }
                void main() {}
                """,
				"python-host-composite.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("ReadPythonBox"), 23)
			self.assertEqual(session.call("ReadPythonArray"), 9)

	def test_python_host_subtype_is_accepted_as_a_registered_base_type(self) -> None:
		class Animal:
			pass

		class Dog(Animal):
			pass

		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_class("Animal", Animal)
			session.register_class("Dog", Dog)
			session.register_function("make_dog", Dog, return_type="Dog")
			session.register_function(
				"accept_animal",
				lambda value: 1 if isinstance(value, Animal) else 0,
				return_type="int",
				parameters=("Animal",),
			)
			loaded = session.load_source(
				"int TestPythonHostSubtype() { "
				"return accept_animal(cast<Animal>(make_dog())); }\n"
				"void main() {}\n",
				"python-host-subtype.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("TestPythonHostSubtype"), 1)

	def test_host_values_keep_safe_type_checks_after_origin_session_closes(self) -> None:
		class Animal:
			pass

		class Dog(Animal):
			def __init__(self):
				self.value = 42

		for backend in TEST_BACKENDS:
			for use_constructor in (False, True):
				with self.subTest(backend=backend, constructor=use_constructor):
					origin = make_session(backend=backend)
					origin.register_class("Dog", Dog).constructor()
					if use_constructor:
						producer = "Dog()"
					else:
						origin.register_function("Produce", Dog, return_type="Dog")
						producer = "Produce()"
					origin.load_source(f"Dog Make() {{ return {producer}; }} void main() {{}}")
					value = origin.call("Make")
					# Registering a base after producing the first value must
					# update existing objects' type lookup, too.
					origin.register_class("Animal", Animal)
					origin.close()
					del origin
					gc.collect()
					copied = value.clone()
					del value
					with make_session(backend=backend) as consumer:
						consumer.register_class("Animal", Animal)
						consumer.register_class("Dog", Dog).field("value", type_name="int")
						consumer.register_function("Produce", lambda: copied, return_type="Dog")
						consumer.register_function("Accept", lambda obj: obj.value,
							return_type="int", parameters=("Animal",))
						consumer.load_source("int Check() { return Accept(cast<Animal>(Produce())); } "
							"int Field() { return Produce().value; } void main() {}")
						self.assertEqual(consumer.call("Check"), 42)
						self.assertEqual(consumer.call("Field"), 42)

	def test_host_value_type_registry_does_not_retain_unrelated_python_classes(self) -> None:
		class Kept:
			pass

		class Unrelated:
			pass

		reference = weakref.ref(Unrelated)
		origin = make_session()
		origin.register_class("Unrelated", Unrelated)
		origin.register_class("Kept", Kept).constructor()
		origin.load_source("Kept Make() { return Kept(); } void main() {}")
		value = origin.call("Make")
		origin.close()
		del origin, Unrelated
		gc.collect()
		self.assertIsNone(reference())
		self.assertEqual(value.kind, "host_object")

	def test_python_host_enum_callbacks(self) -> None:
		class PythonMode(IntEnum):
			OFF = 0
			READ = 3

		with make_session(backend=veroscript.Backend.VM) as session:
			session.register_enum("PythonMode", PythonMode)
			session.register_function(
				"python_mode_value",
				lambda value: int(value),
				return_type="int",
				parameters=("enum<PythonMode>",),
			)
			session.register_function(
				"python_read_mode",
				lambda: PythonMode.READ,
				return_type="enum<PythonMode>",
			)
			loaded = session.load_source(
				"""
                int ReadPythonMode() {
                    return python_mode_value(python_read_mode());
                }
                void main() {}
                """,
				"python-host-enum.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("ReadPythonMode"), 3)

	def test_python_host_struct_value_protocol(self) -> None:
		class PythonPoint:
			def __init__(self, x: int, y: int) -> None:
				self.x = x
				self.y = y

		clones: list[tuple[int, int]] = []

		def clone(point: object) -> object:
			assert isinstance(point, PythonPoint)
			clones.append((point.x, point.y))
			return PythonPoint(point.x, point.y)

		def equals(left: object, right: object) -> bool:
			assert isinstance(left, PythonPoint)
			assert isinstance(right, PythonPoint)
			return (left.x, left.y) == (right.x, right.y)

		def point_hash(point: object) -> int:
			assert isinstance(point, PythonPoint)
			return hash((point.x, point.y))

		with make_session(backend=veroscript.Backend.VM) as session:
			(
				session.register_struct("PythonPoint", PythonPoint)
				.constructor(parameters=("int", "int"))
				.field("x", type_name="int")
				.field("y", type_name="int")
				.protocol(clone=clone, equals=equals, hash=point_hash)
			)
			loaded = session.load_source(
				"""
                int ExercisePointProtocol() {
                    map<PythonPoint,int> values = map<PythonPoint,int>();
                    PythonPoint inserted = PythonPoint(3, 4);
                    values[inserted] = 17;
                    inserted.x = 99;
                    PythonPoint equal_key = PythonPoint(3, 4);
                    return values[equal_key];
                }
                void main() {}
                """,
				"python-host-protocol.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("ExercisePointProtocol"), 17)
			self.assertTrue(clones)

	def test_host_value_protocols_outlive_session_and_release_with_last_value(self) -> None:
		clones: list[int] = []

		class Point:
			def __init__(self, x):
				self.x = x

		class Protocols:
			def clone(self, point):
				clones.append(point.x)
				return Point(point.x)

			def equals(self, left, right):
				return left.x == right.x

			def hash(self, point):
				return point.x

		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend):
				callbacks = Protocols()
				reference = weakref.ref(callbacks)
				session = make_session(backend=backend)
				session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
					clone=callbacks.clone, equals=callbacks.equals, hash=callbacks.hash)
				session.load_source("Point Make() { return Point(42); } void main() {}")
				value = session.call("Make")
				equal_value = session.call("Make")
				previous = veroscript.Value.map("Point", "int", {value: 17})
				session.close()
				del session, callbacks
				gc.collect()
				self.assertIsNotNone(reference())
				current = veroscript.Value.map("Point", "int", {value: 42})
				keys = veroscript.Value.set("Point", (value,))
				self.assertEqual(previous[equal_value], 17)
				self.assertEqual(current[equal_value], 42)
				self.assertIn(equal_value, keys)
				clones.clear()
				with make_session(backend=backend) as consumer:
					consumer.register_struct("Point", Point)
					loaded = consumer.load_source(
						"Point Copy(Point input) { Point copy = input; return copy; } void main() {}")
					self.assertTrue(loaded.success, loaded.diagnostics)
					cloned_value = consumer.call("Copy", value)
				self.assertTrue(clones)
				self.assertEqual(current[cloned_value], 42)
				copied_key = current.keys()[0]
				del value, equal_value, cloned_value, previous, current, keys
				gc.collect()
				self.assertIsNotNone(reference())
				self.assertEqual(veroscript.Value.map("Point", "int", {copied_key: 9})[copied_key], 9)
				del copied_key
				gc.collect()
				self.assertIsNone(reference())

	def test_value_protocol_errors_belong_to_the_current_operation(self) -> None:
		class Point:
			def __init__(self, x):
				self.x = x

		for close_producer in (False, True):
			original = LookupError("value protocol failed")
			failure = [None]

			def hash_value(point):
				if failure[0] == "hash":
					raise original
				return 1

			def equals(left, right):
				if failure[0] == "equals":
					raise original
				return left.x == right.x

			with make_session(backend=veroscript.Backend.VM) as producer:
				producer.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
					clone=lambda point: Point(point.x), equals=equals, hash=hash_value)
				self.assertTrue(producer.load_source(
					"Point Make(){return Point(42);} int Alive(){return 42;} void main(){}"
				).success)
				value = producer.call("Make")
				query = producer.call("Make")
				values = veroscript.Value.set("Point", (value,))
				mapping = veroscript.Value.map("Point", "int", {value: 17})
				if close_producer:
					producer.close()
				operations = {
					"set membership": lambda: query in values,
					"set contains": lambda: values.contains(query),
					"map subscript": lambda: mapping[query],
					"map get": lambda: mapping.get(query),
					"map membership": lambda: query in mapping,
					"set construction": lambda: veroscript.Value.set("Point", (value, query)),
					"map construction": lambda: veroscript.Value.map("Point", "int", {value: 1, query: 2}),
				}
				for kind in ("hash", "equals"):
					for name, operation in operations.items():
						with self.subTest(close_producer=close_producer, protocol=kind, operation=name):
							failure[0] = kind
							try:
								with self.assertRaises(veroscript.CallbackError) as raised:
									operation()
								self.assertIs(raised.exception.args[0], original)
							finally:
								failure[0] = None
							self.assertIn(query, values)
							self.assertEqual(mapping[query], 17)
							self.assertEqual(values.count, 1)
							if not close_producer:
								self.assertEqual(producer.call("Alive"), 42)

	def test_nested_value_protocol_errors_preserve_session_callback_errors(self) -> None:
		class Point:
			def __init__(self, x):
				self.x = x

		for close_producer in (False, True):
			for outer_failure in (False, True):
				with self.subTest(close_producer=close_producer, outer_failure=outer_failure):
					protocol_error = ValueError("nested value protocol failed")
					outer_error = LookupError("earlier session callback failed")
					failure = [False]
					caught = []

					def hash_value(point):
						if failure[0]:
							raise protocol_error
						return point.x

					def callback(text):
						if text == "first":
							raise outer_error
						if text == "nested":
							failure[0] = True
							try:
								with self.assertRaises(veroscript.CallbackError) as raised:
									query in values
								caught.append(raised.exception.args[0])
							finally:
								failure[0] = False

					with make_session(backend=veroscript.Backend.VM, print_callback=callback) as producer:
						producer.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
							clone=lambda point: Point(point.x), equals=lambda left, right: left.x == right.x,
							hash=hash_value)
						source = ('Point Make(){return Point(42);} int Alive(){return 42;} '
							  'void Trigger(){' + ('print("first");' if outer_failure else '') +
							  'print("nested");} void main(){}')
						self.assertTrue(producer.load_source(source).success)
						value = producer.call("Make")
						query = producer.call("Make")
						values = veroscript.Value.set("Point", (value,))
						with make_session(backend=veroscript.Backend.VM, print_callback=callback) as foreign:
							if close_producer:
								producer.close()
								self.assertTrue(foreign.load_source(source.replace("Point Make(){return Point(42);}", "")).success)
							owner = foreign if close_producer else producer
							if outer_failure:
								with self.assertRaises(veroscript.CallbackError) as raised:
									owner.call("Trigger")
								self.assertIs(raised.exception.args[0], outer_error)
							else:
								# Core preserves the actual int32 zero return value for this void call.
								self.assertEqual(owner.call("Trigger"), 0)
							self.assertEqual(len(caught), 1)
							self.assertIs(caught[0], protocol_error)
							self.assertEqual(owner.call("Alive"), 42)
							self.assertIn(query, values)

	def test_host_protocol_cycles_preserve_escaped_values_until_final_release(self) -> None:
		class Point:
			def __init__(self, x):
				self.x = x

		for backend in TEST_BACKENDS:
			for retain_native in (False, True):
				with self.subTest(backend=backend, retain_native=retain_native):
					released = []

					class Protocols:
						def clone(self, point):
							return Point(point.x)

						def equals(self, left, right):
							return left.x == right.x

						def hash(self, point):
							return point.x

						def __del__(self):
							released.append(True)

					def produce():
						session = make_session(backend=backend)
						callbacks = Protocols()
						callbacks.owner = session._native if retain_native else session
						session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
							clone=callbacks.clone, equals=callbacks.equals, hash=callbacks.hash)
						self.assertTrue(session.load_source(
							"Point Make() { return Point(42); } void main() {}").success)
						value = session.call("Make")
						equal_value = session.call("Make")
						mapping = veroscript.Value.map("Point", "int", {value: 17})
						return value, equal_value, mapping, weakref.ref(callbacks)

					value, equal_value, mapping, reference = produce()
					gc.collect()
					self.assertIsNotNone(reference())
					self.assertEqual(released, [])
					self.assertEqual(mapping[equal_value], 17)
					self.assertIn(equal_value, veroscript.Value.set("Point", (value,)))
					del value, equal_value, mapping
					gc.collect()
					self.assertIsNone(reference())
					self.assertEqual(released, [True])

	def test_session_gc_finalizes_before_clearing_shared_protocol_state(self) -> None:
		class Point:
			def __init__(self, x):
				self.x = x

		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend):
				def produce():
					session = make_session(backend=backend)
					session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
						clone=lambda point: Point(point.x),
						equals=lambda left, right: left.x == right.x,
						hash=lambda point: point.x)
					session.set_mutation_callback(lambda event, owner=session._native: owner.closed)
					self.assertTrue(session.load_source(
						"Point Make() { return Point(42); } void main() {}").success)
					value = session.call("Make")
					equal_value = session.call("Make")
					return value, equal_value, veroscript.Value.map("Point", "int", {value: 17})

				value, equal_value, mapping = produce()
				gc.collect()
				self.assertEqual(mapping[equal_value], 17)
				self.assertEqual(veroscript.Value.map("Point", "int", {value: 9})[equal_value], 9)
				del value, equal_value, mapping
				gc.collect()

	def test_host_protocol_cycle_without_values_is_collectable(self) -> None:
		class Point:
			pass

		session = make_session(backend=veroscript.Backend.VM)
		session.register_struct("Point", Point).protocol(
			clone=lambda point, owner=session: Point(),
			equals=lambda left, right, owner=session: True,
			hash=lambda point, owner=session: 42)
		reference = weakref.ref(session)
		del session
		gc.collect()
		self.assertIsNone(reference())

	def test_host_value_created_before_protocol_registration_retains_gc_anchor(self) -> None:
		class Point:
			pass

		def produce():
			session = make_session(backend=veroscript.Backend.VM)
			host = session.register_struct("Point", Point).constructor()
			self.assertTrue(session.load_source("Point Make() { return Point(); } void main() {}").success)
			value = session.call("Make")
			clone = lambda point, owner=session._native: Point()
			host.protocol(clone=clone)
			return value, weakref.ref(clone)

		value, reference = produce()
		gc.collect()
		self.assertIsNotNone(reference())
		self.assertEqual(value.kind, "host_object")
		del value
		gc.collect()
		self.assertIsNone(reference())

	def test_failed_host_protocol_registration_releases_gc_anchors(self) -> None:
		class Point:
			pass

		with make_session(backend=veroscript.Backend.VM) as session:
			# Reference classes reject value-clone protocols at native commit.
			host = session.register_class("Point", Point).constructor()
			self.assertTrue(session.load_source("Point Make() { return Point(); } void main() {}").success)
			value = session.call("Make")
			clone = lambda point, owner=session._native: Point()
			reference = weakref.ref(clone)
			with self.assertRaises(veroscript.Error):
				host.protocol(clone=clone)
			del clone
			gc.collect()
			self.assertIsNone(reference())
			self.assertEqual(value.kind, "host_object")

	@unittest.skipUnless(sys.version_info[:2] == (3, 11), "synchronous allocation-triggered GC fixture requires CPython 3.11")
	def test_host_protocol_registration_rejects_gc_finalizer_close_reentry(self) -> None:
		class Point:
			pass

		events = []
		with make_session(backend=veroscript.Backend.VM) as session:
			host = session.register_struct("Point", Point)
			method = session._native.register_host_protocol
			arguments = (host._type_id, lambda point: Point(),
						 lambda left, right: True, lambda point: 42)

			class Garbage:
				def __del__(self):
					try:
						session.close()
					except veroscript.Error as error:
						events.append(str(error))
					else:
						events.append("session closed during registration")

			gc.collect()
			thresholds, enabled = gc.get_threshold(), gc.isenabled()
			gc.disable()
			pending = Garbage()
			pending.cycle = pending
			del pending
			gc.set_threshold(1, 1, 1)
			gc.enable()
			try:
				method(*arguments)
			finally:
				gc.disable()
				gc.set_threshold(*thresholds)
				if enabled:
					gc.enable()
			self.assertEqual(len(events), 1)
			self.assertIn("active operation", events[0])
			self.assertFalse(session.closed)
			host.constructor()
			self.assertTrue(session.load_source(
				"Point Make() { return Point(); } void main() {}").success)
			value = session.call("Make")
			self.assertEqual(veroscript.Value.map("Point", "int", {value: 17})[value], 17)

	@unittest.skipUnless(sys.version_info[:2] == (3, 11), "synchronous allocation-triggered GC fixture requires CPython 3.11")
	def test_host_protocol_registration_preserves_nested_value_gc_anchor(self) -> None:
		class Point:
			pass

		def produce():
			session = make_session(backend=veroscript.Backend.VM)
			host = session.register_struct("Point", Point).constructor()
			self.assertTrue(session.load_source(
				"Point Make() { return Point(); } void main() {}").success)
			values, events = [], []
			clone = lambda point, owner=session._native: Point()
			reference = weakref.ref(clone)
			method = session._native.register_host_protocol
			arguments = (host._type_id, clone)

			class Garbage:
				def __del__(self):
					try:
						method(*arguments)
					except veroscript.Error as error:
						events.append(str(error))
					else:
						events.append("nested protocol registration succeeded")
					values.append(session.call("Make"))

			gc.collect()
			thresholds, enabled = gc.get_threshold(), gc.isenabled()
			gc.disable()
			pending = Garbage()
			pending.cycle = pending
			del pending
			gc.set_threshold(1, 1, 1)
			gc.enable()
			try:
				method(*arguments)
			finally:
				gc.disable()
				gc.set_threshold(*thresholds)
				if enabled:
					gc.enable()
			self.assertEqual(len(events), 1)
			self.assertIn("active operation", events[0])
			self.assertEqual(len(values), 1)
			return values[0], reference

		value, reference = produce()
		gc.collect()
		self.assertIsNotNone(reference())
		del value
		gc.collect()
		self.assertIsNone(reference())

	def test_host_protocol_last_value_release_on_worker_thread(self) -> None:
		released_on: list[int] = []

		class Point:
			def __init__(self, x):
				self.x = x

		class Protocols:
			def clone(self, point):
				return Point(point.x)

			def equals(self, left, right):
				return left.x == right.x

			def hash(self, point):
				return point.x

			def __del__(self):
				released_on.append(threading.get_ident())

		callbacks = Protocols()
		reference = weakref.ref(callbacks)
		session = make_session(backend=veroscript.Backend.VM)
		session.register_struct("Point", Point).constructor(parameters=("int",)).protocol(
			clone=callbacks.clone, equals=callbacks.equals, hash=callbacks.hash)
		session.load_source("Point Make() { return Point(42); } void main() {}")
		values = [session.call("Make")]
		session.close()
		del session, callbacks
		self.assertIsNotNone(reference())
		worker = threading.Thread(target=lambda: values.pop())
		worker.start()
		worker.join(timeout=10)
		self.assertFalse(worker.is_alive())
		self.assertIsNone(reference())
		self.assertEqual(released_on, [worker.ident])

	def test_python_generic_host_class_explicit_instantiation(self) -> None:
		created_for: list[str] = []

		def make_box(type_name: str) -> type:
			created_for.append(type_name)

			class PythonBox:
				def __init__(self, value: object) -> None:
					self.value = value

			return PythonBox

		with make_session(backend=veroscript.Backend.VM) as session:
			template = session.register_generic_class("PythonBox", 1, make_box)
			(
				template.instantiate("int")
				.constructor(parameters=("int",))
				.field("value", type_name="int")
			)
			loaded = session.load_source(
				"""
                int ReadGenericPythonBox() {
                    PythonBox<int> box = PythonBox<int>(31);
                    return box.value;
                }
                void main() {}
                """,
				"python-generic-host.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("ReadGenericPythonBox"), 31)
			self.assertEqual(created_for, ["int"])
			same = template.instantiate("int")
			self.assertEqual(same.name, "PythonBox<int>")

		def fail_factory(_type_name: str) -> type:
			raise LookupError("generic factory failed")

		with make_session(backend=veroscript.Backend.VM) as session:
			template = session.register_generic_class("FailingBox", 1, fail_factory)
			with self.assertRaises(veroscript.CallbackError) as raised:
				template.instantiate("int")
			self.assertIsInstance(raised.exception.args[0], LookupError)

	def test_python_host_class_constructor_and_method(self) -> None:
		live_instances: weakref.WeakSet[PythonCounter]

		class PythonCounter:
			def __init__(self, initial: int) -> None:
				self._value = initial
				self.raw_value = initial
				live_instances.add(self)

			def add(self, delta: int) -> int:
				self._value += delta
				return self._value

			@staticmethod
			def twice(value: int) -> int:
				return value * 2

			@property
			def current(self) -> int:
				return self._value

			@current.setter
			def current(self, value: int) -> None:
				self._value = value

		live_instances = weakref.WeakSet()
		source = """
        int UsePythonCounter(int initial, int delta) {
            PythonCounter counter(initial);
            return counter.Add(delta);
        }
        int UsePythonMembers() {
            PythonCounter counter(1);
            counter.Current = 10;
            counter.Current += 2;
            counter.RawValue = 20;
            counter.RawValue += 3;
            return counter.Current + counter.RawValue + PythonCounter.Twice(3);
        }
        void main() {}
        """
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				binding = session.register_class("PythonCounter", PythonCounter)
				binding.constructor(parameters=("int",))
				binding.method("Add", "add", return_type="int", parameters=("int",))
				binding.static_method(
					"Twice", "twice", return_type="int", parameters=("int",)
				)
				binding.property("Current", "current", type_name="int")
				binding.field("RawValue", "raw_value", type_name="int")
				loaded = session.load_source(source, "python-host-class.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("UsePythonCounter", 10, 7), 17)
				self.assertEqual(session.call("UsePythonMembers"), 41)
				self.assertEqual(len(live_instances), 0)

	def test_python_host_method_exception_is_preserved(self) -> None:
		class FailingHost:
			def fail(self) -> int:
				raise RuntimeError("host method failed")

		with make_session(backend=veroscript.Backend.VM) as session:
			binding = session.register_class("FailingHost", FailingHost)
			binding.constructor()
			binding.method("Fail", "fail", return_type="int")
			source = "void main() { FailingHost host = FailingHost(); host.Fail(); }"
			loaded = session.load_source(source, "python-host-error.vs")
			self.assertTrue(loaded.success, loaded.diagnostics)
			with self.assertRaises(veroscript.CallbackError) as raised:
				session.run()
			self.assertIsInstance(raised.exception.args[0], RuntimeError)

	def test_typed_container_values(self) -> None:
		array = veroscript.Value.array("int", [1, 2, 3])
		self.assertEqual(array.type_name, "array<int>")
		self.assertEqual(array.count, 3)
		self.assertEqual(len(array), 3)
		self.assertEqual(list(array), [1, 2, 3])
		self.assertEqual(array[0], 1)
		self.assertEqual(array[-1], 3)
		self.assertEqual(array[::-1], [3, 2, 1])
		self.assertIn(2, array)
		self.assertNotIn(8, array)
		with self.assertRaises(IndexError):
			_ = array[3]
		self.assertEqual(array.to_python(), [1, 2, 3])

		priority_queue = veroscript.Value.priority_queue("int", [3, 1, 2])
		self.assertEqual(priority_queue.kind, "priority_queue")
		self.assertEqual(priority_queue.count, 3)
		self.assertEqual(sorted(priority_queue.to_python()), [1, 2, 3])

		mapping = veroscript.Value.map("string", "int", {"one": 1, "two": 2})
		self.assertEqual(mapping.count, 2)
		self.assertEqual(len(mapping), 2)
		self.assertEqual(mapping.get("two"), 2)
		self.assertIsNone(mapping.get("missing"))
		self.assertEqual(mapping["one"], 1)
		self.assertIn("two", mapping)
		self.assertNotIn("missing", mapping)
		self.assertEqual(set(mapping), {"one", "two"})
		self.assertEqual(set(mapping.keys()), {"one", "two"})
		self.assertEqual(set(mapping.values()), {1, 2})
		self.assertEqual(dict(mapping.items()), {"one": 1, "two": 2})
		self.assertEqual(mapping.to_python(), {"one": 1, "two": 2})
		with self.assertRaises(KeyError):
			_ = mapping["missing"]

		ordered = veroscript.Value.map(
			"string", "int", {"first": 1, "second": 2}, ordered=True
		)
		self.assertEqual(list(ordered), ["first", "second"])
		self.assertEqual(ordered.items(), [("first", 1), ("second", 2)])

		values = veroscript.Value.set("int", [1, 2, 2])
		self.assertEqual(values.count, 2)
		self.assertEqual(len(values), 2)
		self.assertTrue(values.contains(2))
		self.assertFalse(values.contains(3))
		self.assertIn(2, values)
		self.assertNotIn(3, values)
		self.assertEqual(set(values), {1, 2})
		self.assertEqual(values.to_python(), {1, 2})

		nested = veroscript.Value.map("string", "array<int>", {"numbers": array})
		self.assertEqual(nested.to_python(), {"numbers": [1, 2, 3]})

		scalar_aliases = {
			"char": 7,
			"unsigned char": 7,
			"short": 700,
			"unsigned short": 700,
			"unsigned int": 70_000,
			"long": 5_000_000_001,
			"unsigned long": 5_000_000_001,
		}
		for type_name, value in scalar_aliases.items():
			with self.subTest(container_scalar_alias=type_name):
				aliased = veroscript.Value.array(type_name, [value])
				self.assertEqual(aliased.to_python(), [value])
		self.assertEqual(
			veroscript.Value.map("long", "unsigned long", {1: 2}).to_python(),
			{1: 2},
		)
		self.assertEqual(
			veroscript.Value.set("unsigned long", [1, 2, 2]).to_python(),
			{1, 2},
		)

		source = """
        int Sum(array<int> values) {
            return values[0] + values[1] + values[2];
        }
        array<int> MakeArray() { array<int> result = {4, 5}; return result; }
        ordered_map<string, int> MakeMap() {
            return ordered_map<string, int>{"left": 6, "right": 7};
        }
        set<int> MakeSet() { return set<int>{8, 9, 8}; }
        void main() {}
        """
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				loaded = session.load_source(source, "python-container.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("Sum", array), 6)
				returned = session.call("MakeArray")
				self.assertIsInstance(returned, veroscript.Value)
				self.assertEqual(returned.to_python(), [4, 5])
				returned_map = session.call("MakeMap")
				self.assertIsInstance(returned_map, veroscript.Value)
				self.assertEqual(returned_map.items(), [("left", 6), ("right", 7)])
				returned_set = session.call("MakeSet")
				self.assertIsInstance(returned_set, veroscript.Value)
				self.assertEqual(returned_set.to_python(), {8, 9})

	def test_character_key_containers_round_trip_without_truncation(self) -> None:
		mapping = veroscript.Value.map("char", "int", {65: 1, -1: 2})
		self.assertEqual(mapping.type_name, "map<char,int>")
		self.assertEqual(mapping.get(65), 1)
		self.assertEqual(mapping[-1], 2)
		self.assertEqual(dict(mapping.items()), {65: 1, -1: 2})
		self.assertEqual(mapping.to_python(), {65: 1, -1: 2})

		ordered = veroscript.Value.map(
			"char", "int", {65: 1, 66: 2}, ordered=True
		)
		self.assertEqual(ordered.type_name, "ordered_map<char,int>")
		self.assertEqual(ordered.items(), [(65, 1), (66, 2)])

		values = veroscript.Value.set("char", [65, -1, 65])
		self.assertEqual(values.type_name, "set<char>")
		self.assertEqual(values.count, 2)
		self.assertIn(65, values)
		self.assertIn(-1, values)
		self.assertEqual(values.to_python(), {65, -1})

		with self.assertRaises(OverflowError):
			veroscript.Value.map("char", "int", {128: 1})
		with self.assertRaises(OverflowError):
			veroscript.Value.set("char", [-129])

	def test_updated_core_language_surface_and_character_containers(self) -> None:
		source = r'''
        int Feature() {
            int value = 0, rounds = 0;
            do { value += 2; ++rounds; } while (rounds < 3);
            for (int left = 0, right = 3; left < right; ++left, --right) {
                value += left + right;
            }
            return value + 0b10_10;
        }
        char Character() { return '\x41'; }
        string RawText() { return r"C:\temp\n"; }
        ordered_map<char, int> CharacterMap() {
            return ordered_map<char, int>{'A': 1, '\x42': 2};
        }
        set<char> CharacterSet() { return set<char>{'A', '\x42', 'A'}; }
        void main() {}
        '''
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				loaded = session.load_source(source, "python-updated-core.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("Feature"), 22)
				self.assertEqual(session.call("Character"), 65)
				self.assertEqual(session.call("RawText"), r"C:\temp\n")

				returned_map = session.call("CharacterMap")
				self.assertIsInstance(returned_map, veroscript.Value)
				self.assertEqual(returned_map.type_name, "ordered_map<char,int>")
				self.assertEqual(returned_map.items(), [(65, 1), (66, 2)])
				self.assertEqual(returned_map.get(65), 1)
				self.assertEqual(returned_map.to_python(), {65: 1, 66: 2})

				returned_set = session.call("CharacterSet")
				self.assertIsInstance(returned_set, veroscript.Value)
				self.assertEqual(returned_set.type_name, "set<char>")
				self.assertEqual(returned_set.count, 2)
				self.assertIn(65, returned_set)
				self.assertIn(66, returned_set)
				self.assertEqual(returned_set.to_python(), {65, 66})

	def test_additional_sequences_and_threaded_channel(self) -> None:
		queue = veroscript.Value.queue("int", [1, 2, 3])
		stack = veroscript.Value.stack("int", [1, 2, 3])
		deque = veroscript.Value.deque("string", ["left", "right"])
		self.assertEqual(queue.to_python(), [1, 2, 3])
		self.assertEqual(stack.to_python(), [1, 2, 3])
		self.assertEqual(deque.to_python(), ["left", "right"])

		channel = veroscript.Value.channel("int", 1)
		self.assertEqual(channel.channel_capacity, 1)
		self.assertFalse(channel.channel_closed)
		self.assertTrue(channel.try_send(7))
		self.assertFalse(channel.try_send(8))
		self.assertEqual(channel.count, 1)
		self.assertEqual(channel.try_receive(), 7)
		self.assertIsNone(channel.try_receive())
		self.assertIsNone(channel.receive_for(0))
		self.assertTrue(channel.send_for(9, 0))
		self.assertEqual(channel.receive(), 9)
		started = threading.Event()
		received: list[object] = []

		def receive() -> None:
			started.set()
			received.append(channel.receive())

		worker = threading.Thread(target=receive)
		worker.start()
		self.assertTrue(started.wait(1.0))
		channel.send(42)
		worker.join(2.0)
		self.assertFalse(worker.is_alive())
		self.assertEqual(received, [42])
		channel.close_channel()
		self.assertTrue(channel.channel_closed)
		self.assertFalse(channel.try_send(1))
		with self.assertRaises(veroscript.Error):
			channel.receive()

	def test_reflection_bindings_jit_and_languages(self) -> None:
		self.assertIn("en-US", veroscript.available_languages())
		backend = veroscript.Backend.JIT if TEST_JIT_BACKEND else veroscript.Backend.MIXED
		with make_session(backend=backend) as session:
			session.set_jit_policy(
				veroscript.JitPolicy(mixed_hot_call_threshold=2)
			)
			session.register_function(
				"py_identity",
				lambda value: value,
				return_type="int",
				parameters=("int",),
			)
			bindings = session.bindings()
			self.assertTrue(
				any(item["name"] == "py_identity" for item in bindings["functions"])
			)
			loaded = session.load_source(
				"export int Reflected(int value) { return py_identity(value); } "
				"void main() {}",
				"reflection-python.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			reflection = session.reflection()
			self.assertTrue(
				any(item["name"] == "Reflected" for item in reflection["freeFunctions"])
			)
			for _ in range(4):
				self.assertEqual(session.call("Reflected", 9), 9)
			self.assertIn("compile_attempts", session.jit_stats)

	def test_generic_static_field_value_member_calls(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(backend=backend) as session:
				loaded = session.load_source(
					"class Item{public:int number=7;int Read() const{return number;}};"
					"class Cache<T>{public:static Item value=Item();};"
					"int read(){auto value=Cache<int>::value.Read();return value;}void main(){}",
					"python-generic-static-value.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("read"), 7)

	def test_cleanup_configuration_reports_callback_exception(self) -> None:
		for operation in ("clear", "disable_hot_reload"):
			with self.subTest(operation=operation):
				original = LookupError("cleanup callback failed")
				lines = []

				def fail_callback(text):
					lines.append(text)
					raise original

				with make_session(backend=veroscript.Backend.VM, hot_reload_enabled=True,
						print_callback=fail_callback) as session:
					loaded = session.load_source(
						'struct Held { Held()=default; ~Held(){print("retired");} }; '
						'Held held=Held(); void main(){}', "cleanup-callback.vs")
					self.assertTrue(loaded.success, loaded.diagnostics)
					self.assertTrue(session.run(veroscript.RunMode.EVAL).success)
					with self.assertRaises(veroscript.CallbackError) as raised:
						if operation == "clear":
							session.clear()
						else:
							session.set_hot_reload_enabled(False)
					self.assertIs(raised.exception.args[0], original)
					self.assertEqual(lines, ["retired"])
					self.assertFalse(session.closed)
					if operation == "disable_hot_reload":
						self.assertFalse(session.hot_reload_enabled)
					self.assertTrue(session.load_source('int alive(){return 42;} void main(){}').success)
					self.assertEqual(session.call("alive"), 42)

	def test_cleanup_configuration_rejects_callback_reentry(self) -> None:
		for operation in ("clear", "disable_hot_reload"):
			with self.subTest(operation=operation):
				lines = []

				def callback(text):
					lines.append(text)
					with self.assertRaises(veroscript.Error):
						session.close()
					with self.assertRaises(veroscript.Error):
						session.clear()
					with self.assertRaises(veroscript.Error):
						session.set_hot_reload_enabled(False)
					self.assertFalse(session.closed)

				with make_session(backend=veroscript.Backend.VM, hot_reload_enabled=True,
						print_callback=callback) as session:
					loaded = session.load_source(
						'struct Held { Held()=default; ~Held(){print("retired");} }; '
						'Held held=Held(); void main(){}', "cleanup-reentry.vs")
					self.assertTrue(loaded.success, loaded.diagnostics)
					self.assertTrue(session.run(veroscript.RunMode.EVAL).success)
					if operation == "clear":
						self.assertIsNone(session.clear())
					else:
						self.assertIsNone(session.set_hot_reload_enabled(False))
						self.assertFalse(session.hot_reload_enabled)
					self.assertEqual(lines, ["retired"])
					self.assertFalse(session.closed)
					self.assertTrue(session.load_source('int alive(){return 42;} void main(){}').success)
					self.assertEqual(session.call("alive"), 42)

	def test_pending_callback_error_survives_rejected_nested_operations(self) -> None:
		for operation in ("run", "clear", "disable_hot_reload"):
			for nested in ("clear", "load_source", "get_global"):
				with self.subTest(operation=operation, nested=nested):
					original = LookupError("first callback failure")
					lines = []
					nested_results = []
					nested_errors = []

					def callback(text):
						lines.append(text)
						if text == "first":
							raise original
						if text == "second":
							try:
								if nested == "clear":
									nested_results.append(session.clear())
								elif nested == "load_source":
									nested_results.append(session.load_source("void main(){}", "nested-cleanup.vs"))
								else:
									nested_results.append(session.get_global("marker"))
							except BaseException as error:
								nested_errors.append(error)

					with make_session(backend=veroscript.Backend.VM, hot_reload_enabled=True,
							print_callback=callback) as session:
						if operation == "run":
							source = 'int marker=42;void main(){print("first");print("second");}'
						else:
							source = ('struct Held{Held()=default;~Held(){print("first");print("second");}};'
								'int marker=42;Held held=Held();void main(){}')
						self.assertTrue(session.load_source(source, "pending-cleanup.vs").success)
						if operation != "run":
							self.assertTrue(session.run(veroscript.RunMode.EVAL).success)
						with self.assertRaises(veroscript.CallbackError) as raised:
							if operation == "run":
								session.run()
							elif operation == "clear":
								session.clear()
							else:
								session.set_hot_reload_enabled(False)
						self.assertIs(raised.exception.args[0], original)
						self.assertEqual(lines, ["first", "second"])
						if nested == "load_source":
							self.assertFalse(nested_errors)
							self.assertEqual(len(nested_results), 1)
							self.assertFalse(nested_results[0].success)
							self.assertEqual(nested_results[0].exit_code, 2)
							self.assertIn("reentry", nested_results[0].diagnostics[0].message)
						else:
							self.assertFalse(nested_results)
							self.assertEqual(len(nested_errors), 1)
							self.assertIsInstance(nested_errors[0], veroscript.Error)
							self.assertNotIsInstance(nested_errors[0], veroscript.CallbackError)
						self.assertFalse(session.closed)
						if operation == "disable_hot_reload":
							self.assertFalse(session.hot_reload_enabled)
						self.assertTrue(session.load_source("int alive(){return 42;}void main(){}").success)
						self.assertEqual(session.call("alive"), 42)

	def test_file_hot_reload(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			source_path = Path(directory) / "reload.vs"
			source_path.write_text(
				"int Current() { return 1; } void main() {}", encoding="utf-8"
			)
			with make_session(
				backend=veroscript.Backend.VM, hot_reload_enabled=True
			) as session:
				loaded = session.load_file(source_path)
				self.assertTrue(loaded.success, loaded.diagnostics)
				self.assertEqual(session.call("Current"), 1)
				source_path.write_text(
					"int Current() { return 2; } void main() {}", encoding="utf-8"
				)
				reloaded = session.reload()
				self.assertTrue(reloaded.success, reloaded.diagnostics)
				self.assertEqual(session.call("Current"), 2)

	def test_hot_reload_settlement(self) -> None:
		self.assertTrue(veroscript.CAPABILITIES & veroscript.RuntimeCapabilities.RELOAD_STATUS)
		with tempfile.TemporaryDirectory() as directory, make_session(
			backend=veroscript.Backend.VM, hot_reload_enabled=True
		) as session:
			path = Path(directory) / "settlement.vs"
			path.write_text(
				"class Held { public: int value = 9; }; Held Hold() { return Held(); } "
				"int Current() { return 1; } void main() {}", encoding="utf-8"
			)
			self.assertEqual(session.reload_status.state, veroscript.ReloadState.NONE)
			loaded = session.load_file(path)
			self.assertTrue(loaded.success, loaded.diagnostics)
			initialized = session.run(veroscript.RunMode.EVAL)
			self.assertTrue(initialized.success, initialized.diagnostics)
			initial = session.reload_status
			self.assertEqual(initial.operation_id, 0)
			self.assertGreater(initial.committed_generation, 0)
			self.assertEqual(initial.current_generation, initial.committed_generation)

			held = session.call("Hold")
			self.assertIsInstance(held, veroscript.Value)
			path.write_text("int Current( {", encoding="utf-8")
			blocked = session.reload()
			self.assertFalse(blocked.success)
			self.assertTrue(any("release external value roots" in d.message for d in blocked.diagnostics),
							blocked.diagnostics)
			self.assertEqual(session.reload_status, initial)
			del held
			rejected = session.reload()
			self.assertFalse(rejected.success)
			self.assertEqual(session.reload_status.state, veroscript.ReloadState.REJECTED)
			self.assertEqual(session.reload_status.committed_generation, initial.committed_generation)

			path.write_text(
				"int Divide(int n) { return 10 / n; } int Pending = Divide(0); "
				"int Current() { return 2; } void main() {}", encoding="utf-8"
			)
			staged = session.reload()
			self.assertTrue(staged.success, staged.diagnostics)
			pending = session.reload_status
			self.assertEqual(pending.state, veroscript.ReloadState.PENDING)
			self.assertNotEqual(pending.operation_id, initial.operation_id)
			self.assertEqual(pending.previous_generation, initial.committed_generation)
			self.assertEqual(pending.committed_generation, initial.committed_generation)
			self.assertEqual(pending.current_generation, pending.candidate_generation)
			self.assertNotEqual(pending.candidate_generation, pending.committed_generation)

			self.assertFalse(session.run(veroscript.RunMode.EVAL).success)
			restored = session.reload_status
			self.assertEqual(restored.state, veroscript.ReloadState.ROLLED_BACK)
			self.assertEqual(restored.operation_id, pending.operation_id)
			self.assertEqual(restored.current_generation, initial.committed_generation)
			self.assertEqual(restored.committed_generation, initial.committed_generation)
			self.assertEqual(session.call("Current"), 1)

			path.write_text("int Current() { return 3; } void main() {}", encoding="utf-8")
			staged = session.reload()
			self.assertTrue(staged.success, staged.diagnostics)
			self.assertEqual(session.call("Current"), 3)
			applied = session.reload_status
			self.assertEqual(applied.state, veroscript.ReloadState.APPLIED)
			self.assertEqual(applied.committed_generation, applied.candidate_generation)

			path.write_text(
				"int Current() { return 4; } int Divide(int n) { return 10 / n; } "
				"void main() { Divide(0); }", encoding="utf-8"
			)
			staged = session.reload()
			self.assertTrue(staged.success, staged.diagnostics)
			self.assertFalse(session.run().success)
			with_errors = session.reload_status
			self.assertEqual(with_errors.state, veroscript.ReloadState.APPLIED_WITH_ERRORS)
			self.assertNotEqual(with_errors.operation_id, applied.operation_id)
			self.assertEqual(with_errors.committed_generation, with_errors.candidate_generation)
			self.assertEqual(session.call("Current"), 4)

	def test_in_memory_source_overlay(self) -> None:
		with make_session(
			backend=veroscript.Backend.VM,
			source_overlays={
				"virtual/helper.vs": "export int OverlayValue() { return 42; }"
			},
		) as session:
			loaded = session.load_source(
				'import "helper.vs"; int ReadOverlay() { return OverlayValue(); } '
				"void main() {}",
				"virtual/main.vs",
			)
			self.assertTrue(loaded.success, loaded.diagnostics)
			self.assertEqual(session.call("ReadOverlay"), 42)

	def test_package_build_read_and_load(self) -> None:
		with self.assertRaises(veroscript.Error):
			veroscript.Package(b"not-a-veroscript-package")

		with tempfile.TemporaryDirectory() as directory:
			source_path = Path(directory) / "package-main.vs"
			package_path = Path(directory) / "python-test.vsapp"
			source_path.write_text("int main() { return 7; }", encoding="utf-8")
			with make_session(backend=veroscript.Backend.VM) as builder:
				package_bytes, result = builder.build_package_file(
					source_path,
					package_name="python-test",
					package_version=3,
					metadata={"origin": b"python"},
				)
			self.assertTrue(result.success, result.diagnostics)
			self.assertTrue(package_bytes)
			package_path.write_bytes(package_bytes)

			with make_session(backend=veroscript.Backend.VM) as application_runner:
				loaded_application = application_runner.load_application(
					package_path.resolve(), options={"cacheRoot": str((Path(directory) / "cache").resolve())}
				)
				self.assertTrue(loaded_application.success, loaded_application.diagnostics)
				self.assertEqual(application_runner.run().exit_code, 7)
				with self.assertRaises(veroscript.Error):
					application_runner.load_application(package_path.resolve(), options={"unknown": True})
				self.assertEqual(application_runner.run().exit_code, 7)

			package = veroscript.Package.from_file(package_path)
			self.assertEqual(
				package.package_kind, veroscript.PackageKind.APPLICATION
			)
			self.assertEqual(package.name, "python-test")
			self.assertEqual(package.version, 3)
			self.assertEqual(package.metadata["origin"], b"python")
			self.assertTrue(package.canonical_signed_bytes)
			with self.assertRaisesRegex(veroscript.Error, "Package cannot be reinitialized"):
				package.__init__(package_bytes)
			self.assertEqual(package.name, "python-test")
			self.assertEqual(package.version, 3)

			with make_session(backend=veroscript.Backend.VM) as runner:
				loaded = runner.load_package(package, str(package_path))
				self.assertTrue(loaded.success, loaded.diagnostics)
				ran = runner.run()
				self.assertTrue(ran.success, ran.diagnostics)
				self.assertEqual(ran.exit_code, 7)

	@unittest.skipIf(sys.version_info < (3, 12), "Python buffer hooks require CPython 3.12+")
	def test_package_initialization_cannot_replace_a_reentrant_commit(self) -> None:
		with tempfile.TemporaryDirectory() as directory:
			source_path = Path(directory) / "package.vs"
			source_path.write_text("int main() { return 7; }", encoding="utf-8")
			with make_session(backend=veroscript.Backend.VM) as builder:
				package_bytes, result = builder.build_package_file(
					source_path, package_name="reentrant-package",
				)
			self.assertTrue(result.success, result.diagnostics)
			package = veroscript.Package.__new__(veroscript.Package)
			released = []

			class ReentrantBuffer:
				def __buffer__(self, flags):
					return memoryview(package_bytes)

				def __release_buffer__(self, view):
					package.__init__(package_bytes)
					released.append(True)

			with self.assertRaisesRegex(veroscript.Error, "Package cannot be reinitialized"):
				package.__init__(ReentrantBuffer())
			self.assertEqual(released, [True])
			self.assertEqual(package.name, "reentrant-package")
			with make_session(backend=veroscript.Backend.VM) as runner:
				self.assertTrue(runner.load_package(package).success)
				self.assertEqual(runner.run().exit_code, 7)

	def test_implicit_public_macros_reach_consumers(self) -> None:
		for backend in TEST_BACKENDS:
			with self.subTest(backend=backend), make_session(
				backend=backend, implicit_import_paths=["macro-sdk/global.vs"],
				source_overlays={
					"macro-sdk/global.vs": "#define PRIVATE_VALUE 999\n#define_export SDK_VALUE 42\n",
					"macro-sdk/dep.vs": "export int answer(){return SDK_VALUE;}\n",
				},
			) as session:
				loaded = session.load_source(
					'#ifdef PRIVATE_VALUE\n#error private macro leaked\n#endif\n'
					'import "dep.vs"; int main(){return answer();}', "macro-sdk/main.vs")
				self.assertTrue(loaded.success, loaded.diagnostics)
				result = session.run()
				self.assertTrue(result.success, result.diagnostics)
				self.assertEqual(result.exit_code, 42)
				self.assertEqual(session.call("answer"), 42)

	def test_subinterpreter_import_is_rejected(self) -> None:
		try:
			import _interpreters
		except ImportError:
			self.skipTest("CPython subinterpreter test API is unavailable")

		interpreter = _interpreters.create(_interpreters.new_config("isolated"))
		try:
			result = _interpreters.run_string(interpreter, "import veroscript")
		finally:
			_interpreters.destroy(interpreter)
		self.assertIsNotNone(result)
		self.assertEqual(result.type.__name__, "ImportError")
		self.assertIn("does not support loading in subinterpreters", result.msg)


if __name__ == "__main__":
	unittest.main()
