# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Python bindings for the VeroScript runtime."""

from __future__ import annotations

import builtins
from dataclasses import dataclass
from enum import IntEnum, IntFlag
import json
from os import PathLike, fspath
from pathlib import Path
from types import TracebackType
from typing import TYPE_CHECKING, Any, Callable, Iterable, Mapping, Sequence, cast

from ._mobile import load_native as _load_native

if TYPE_CHECKING:
	from . import _veroscript as _native
else:
	_native = _load_native()

_PACKAGE_DIRECTORY = Path(__file__).resolve().parent
_LOCALIZATION_DIRECTORY = _PACKAGE_DIRECTORY / "localization"
if _LOCALIZATION_DIRECTORY.is_dir():
	_native.load_catalog_directory(str(_LOCALIZATION_DIRECTORY))


class Backend(IntEnum):
	VM = 0
	MIXED = 1
	JIT = 2


class JitBackend(IntEnum):
	"""Concrete module selection, independent of VM/MIXED/JIT policy."""
	NONE = 0
	EXTERNAL = 1


@dataclass(frozen=True)
class VmBackendInfo:
	"""Owned interpreter metadata; selection is frozen after execution starts."""

	id: str
	version: str
	target: str
	library_path: str
	capabilities: int
	selected: bool
	frozen: bool


class JitBackendCapabilities(IntFlag):
	"""Features of the backend selected by one Session."""

	NONE = 0
	NATIVE_CODE = 0x1
	PORTABLE_EXECUTION = 0x2
	NATIVE_TYPED_LOOPS = 0x4
	DIRECT_SCALAR_CALLS = 0x8
	CONTROLLED_EXECUTION = 0x10


@dataclass(frozen=True)
class JitBackendInfo:
	id: str
	target: str
	capabilities: JitBackendCapabilities
	native_code: bool

	def __init__(self, id: str, target: str, capabilities: int, native_code: bool) -> None:
		# Preserve integer construction from the original public dataclass API.
		object.__setattr__(self, "id", id)
		object.__setattr__(self, "target", target)
		object.__setattr__(self, "capabilities", JitBackendCapabilities(capabilities))
		object.__setattr__(self, "native_code", native_code)


class ScriptCapabilities(IntFlag):
	"""Standard-library side effects granted to a capability-restricted session."""

	NONE = 0
	FILE_READ = 0x1
	FILE_WRITE = 0x2
	FILE_DELETE = 0x4
	ENVIRONMENT_READ = 0x8
	ENVIRONMENT_WRITE = 0x10
	CONSOLE_INPUT = 0x20
	CONSOLE_OUTPUT = 0x40
	TASK_SPAWN = 0x80
	PROCESS_DIRECTORY_CHANGE = 0x100
	MEMORY_ADDRESS = 0x200
	ALL = (
		FILE_READ | FILE_WRITE | FILE_DELETE | ENVIRONMENT_READ
		| ENVIRONMENT_WRITE | CONSOLE_INPUT | CONSOLE_OUTPUT | TASK_SPAWN
		| PROCESS_DIRECTORY_CHANGE | MEMORY_ADDRESS
	)


class RuntimeCapabilities(IntFlag):
	"""Optional features implemented by the loaded VeroScript runtime."""

	NONE = 0
	PACKAGES = 0x1
	HOT_RELOAD = 0x2
	HOST_BINDINGS = 0x4
	BINDING_JSON = 0x8
	REFLECTION_JSON = 0x10
	INSTANCE_INTERCEPT = 0x20
	MUTATION_CALLBACK = 0x40
	CONTAINER_VALUES = 0x80
	RETAINED_CALLABLES = 0x100
	GENERIC_HOST_TYPES = 0x200
	EXTERNAL_SERIALIZATION = 0x400
	JIT_DIAGNOSTICS = 0x800
	LOCALIZATION = 0x1000
	DISTINCT_NUMERIC_PRIMITIVES = 0x2000
	CONTAINER_ENUMERATION = 0x4000
	EXECUTION_GOVERNANCE = 0x8000
	SESSION_JIT_CACHE = 0x10000
	# Legacy runtime-wide flags; selected-module features use JitBackendInfo.
	NATIVE_JIT = 0x20000
	NATIVE_TYPED_LOOPS = 0x40000
	JIT_BACKEND_SELECTION = 0x80000
	MEMORY = 0x200000
	EVAL_ONCE = 0x100000
	MODULE_ENVIRONMENT = 0x400000
	NUMERICS = 0x800000
	VARIANT = 0x1000000
	VM_BACKEND_SELECTION = 0x2000000
	RELOAD_STATUS = 0x4000000


class NumericKind(IntEnum):
	"""Verified Core numeric payload kind; numeric values use HOST_OBJECT."""

	NONE = 0
	BIGINT = 1
	RATIONAL = 2


class CompileCapabilities(IntFlag):
	"""Compile services, independent of runtime and selected JIT capabilities."""

	NONE = 0
	ANNOTATION_PROCESSING = 0x1
	SCRIPT_EXTENSIONS = 0x2
	GENERAL_PROCESSING = 0x4


class DiagnosticSeverity(IntEnum):
	ERROR = 0
	WARNING = 1
	INFORMATION = 2
	HINT = 3


class RunMode(IntEnum):
	EVAL = 0
	SCRIPT = 1
	EVAL_ONCE = 2


class ReloadState(IntEnum):
	"""Settlement of the latest reload operation."""

	NONE = 0
	REJECTED = 1
	PENDING = 2
	ROLLED_BACK = 3
	APPLIED = 4
	APPLIED_WITH_ERRORS = 5
	RESET_REQUIRED = 6


@dataclass(frozen=True, slots=True)
class ReloadStatus:
	"""Reload operation and program generation snapshot."""

	state: ReloadState
	operation_id: int
	previous_generation: int
	candidate_generation: int
	current_generation: int
	committed_generation: int


class PackageKind(IntEnum):
	APPLICATION = 1
	LIBRARY = 2


class MutationKind(IntEnum):
	FIELD_WRITE = 0
	CONTAINER_ADD = 1
	CONTAINER_REMOVE = 2
	CONTAINER_SET = 3
	CONTAINER_CLEAR = 4


class ExternalReferenceKind(IntEnum):
	SCRIPT_OBJECT = 0
	HOST_OBJECT = 1


class InterceptResult(IntEnum):
	"""Decision returned by an instance-call interceptor."""

	UNHANDLED = 0
	HANDLED = 1
	FAILED = 2


@dataclass(frozen=True, slots=True)
class ResolvedMethod:
	"""Stable metadata describing the method selected by overload resolution."""

	program_generation: int
	module_identity: int
	declaring_type_identity: int
	method_identity: int
	function_index: int
	module_name: str
	declaring_type_name: str
	method_name: str
	qualified_name: str
	is_virtual: bool
	is_const: bool
	returns_void: bool

	@property
	def declaring_type_short_name(self) -> str:
		"""Return the source-level type name without its module identity prefix."""
		return self.declaring_type_name.rsplit("\x1f", 1)[-1]

	@classmethod
	def _from_native(cls, target: Mapping[str, object]) -> "ResolvedMethod":
		return cls(**cast(Any, dict(target)))


class InterceptContext:
	"""One active intercepted instance call.

    ``proceed()`` may be called at most once, on the callback thread, and only
    while the interceptor is running. The context may be inspected later, but
    can no longer be resumed.
    """

	__slots__ = ("receiver", "target", "arguments", "_context")

	def __init__(self, data: Mapping[str, object]) -> None:
		self.receiver = data["receiver"]
		self.target = ResolvedMethod._from_native(
			cast(Mapping[str, object], data["target"])
		)
		self.arguments = tuple(cast(Sequence[object], data["arguments"]))
		self._context = data["_context"]

	receiver: object
	target: ResolvedMethod
	arguments: tuple[object, ...]

	def proceed(self) -> object:
		"""Invoke the selected method implementation and return its result."""
		return _native._intercept_proceed(self._context)


@dataclass(frozen=True, slots=True)
class HostParameter:
	type_name: str
	name: str = ""
	by_ref: bool = False
	const_ref: bool = False
	documentation: str = ""

	def _native_spec(self) -> tuple[str, int, str, str]:
		if self.by_ref and self.const_ref:
			raise ValueError("a host parameter cannot be both ref and const_ref")
		flags = 1 if self.by_ref else 2 if self.const_ref else 0
		return self.type_name, flags, self.name, self.documentation


@dataclass(frozen=True, slots=True)
class AnnotationArgument:
	"""One named argument accepted by a registered host annotation."""

	name: str
	detail: str = ""
	documentation: str = ""

	def _native_spec(self) -> tuple[str, str, str]:
		return self.name, self.detail, self.documentation


@dataclass(frozen=True, slots=True)
class JitPolicy:
	mixed_hot_call_threshold: int = 1
	max_compile_attempts: int = 4096
	max_cached_entries: int = 4096
	eager_compile_budget: int = 4096
	reuse_session_code: bool = True
	enable_controlled_mixed: bool = False


@dataclass(frozen=True, slots=True)
class JitCacheLimits:
	"""Resource limits for code retained by ordinary session function calls."""

	max_native_bytes: int = 8 * 1024 * 1024
	max_retained_payload_bytes: int = 32 * 1024 * 1024

	@classmethod
	def _from_native(cls, limits: Mapping[str, object]) -> "JitCacheLimits":
		return cls(
			max_native_bytes=int(cast(Any, limits["max_native_bytes"])),
			max_retained_payload_bytes=int(
				cast(Any, limits["max_retained_payload_bytes"])
			),
		)


@dataclass(frozen=True, slots=True)
class JitCacheStats:
	"""Settled resource statistics for the current reusable JIT context."""

	compile_attempts: int
	retained_payload_bytes: int
	native_bytes: int
	resource_limit_reached: bool

	@classmethod
	def _from_native(cls, stats: Mapping[str, object]) -> "JitCacheStats":
		return cls(
			compile_attempts=int(cast(Any, stats["compile_attempts"])),
			retained_payload_bytes=int(
				cast(Any, stats["retained_payload_bytes"])
			),
			native_bytes=int(cast(Any, stats["native_bytes"])),
			resource_limit_reached=bool(stats["resource_limit_reached"]),
		)


@dataclass(frozen=True, slots=True)
class Diagnostic:
	message: str
	message_id: str
	file_label: str
	line: int
	column: int
	arguments: tuple[str, ...] = ()
	severity: DiagnosticSeverity = DiagnosticSeverity.ERROR

	def __post_init__(self) -> None:
		object.__setattr__(self, "severity", DiagnosticSeverity(self.severity))


@dataclass(frozen=True, slots=True)
class MutationEvent:
	object: builtins.object
	object_type_name: str
	field_name: str
	field_index: int
	kind: MutationKind
	object_mutation_version: int
	value: builtins.object

	@classmethod
	def _from_native(
		cls, event: Mapping[str, builtins.object]
	) -> "MutationEvent":
		return cls(
			object=event["object"],
			object_type_name=str(event["object_type_name"]),
			field_name=str(event["field_name"]),
			field_index=int(cast(Any, event["field_index"])),
			kind=MutationKind(int(cast(Any, event["kind"]))),
			object_mutation_version=int(
				cast(Any, event["object_mutation_version"])
			),
			value=event["value"],
		)


@dataclass(frozen=True, slots=True)
class ExternalReference:
	kind: ExternalReferenceKind
	value: object


@dataclass(frozen=True, slots=True)
class RunResult:
	success: bool
	exit_requested: bool
	exit_code: int
	diagnostics: tuple[Diagnostic, ...]

	@classmethod
	def _from_native(cls, result: Mapping[str, object]) -> "RunResult":
		native_diagnostics = cast(
			Iterable[dict[str, Any]], result["diagnostics"]
		)
		diagnostics = tuple(Diagnostic(**item) for item in native_diagnostics)
		return cls(
			success=bool(result["success"]),
			exit_requested=bool(result["exit_requested"]),
			exit_code=int(cast(Any, result["exit_code"])),
			diagnostics=diagnostics,
		)


class VeroScriptError(_native.Error):
	"""A script compilation or execution failure with structured diagnostics."""

	def __init__(self, operation: str, result: RunResult):
		self.operation = operation
		self.result = result
		primary = next(
			(item for item in result.diagnostics if item.severity is DiagnosticSeverity.ERROR),
			result.diagnostics[0] if result.diagnostics else None,
		)
		detail = primary.message if primary is not None else "unknown engine error"
		super().__init__(f"{operation} failed: {detail}")


Error = _native.Error
ThreadError = _native.ThreadError
TypeMismatchError = _native.TypeMismatchError
CallbackError = _native.CallbackError
Value = _native.Value


class Package(_native.Package):
	"""An immutable, owned VeroScript application or library package snapshot."""

	@classmethod
	def from_file(cls, path: str | PathLike[str]) -> "Package":
		return cls(Path(path).read_bytes())

	@property
	def package_kind(self) -> PackageKind:
		return PackageKind(self.kind)


class RetainedCallable:
	"""A creator-thread-confined retained VeroScript function or closure."""

	def __init__(self, native_callable: _native.Callable) -> None:
		self._native = native_callable

	@property
	def closed(self) -> bool:
		return self._native.closed

	@property
	def is_valid(self) -> bool:
		return self._native.is_valid

	def invoke(self, *arguments: object) -> tuple[object, RunResult]:
		value, native_result = self._native.invoke(*arguments)
		return value, RunResult._from_native(native_result)

	def __call__(self, *arguments: object) -> object:
		value, result = self.invoke(*arguments)
		if not result.success:
			raise VeroScriptError("invoke retained callable", result)
		return value

	def close(self) -> None:
		self._native.close()

	def __enter__(self) -> "RetainedCallable":
		if self.closed:
			raise Error("cannot enter a closed retained callable")
		return self

	def __exit__(
		self,
		exc_type: type[BaseException] | None,
		exc_value: BaseException | None,
		traceback: TracebackType | None,
	) -> None:
		self.close()


def set_language(language_tag: str) -> None:
	"""Select the process-wide diagnostic language (for example, ``en-US``)."""
	_native.set_language(language_tag)


def get_language() -> str:
	return _native.get_language()


def available_languages() -> tuple[str, ...]:
	"""Return runtime-provided metadata for all loaded localization catalogs."""
	return tuple(json.loads(_native.get_available_languages_json()))


def merge_catalog_directory(
	directory: str | PathLike[str], *, override_existing: bool = False
) -> None:
	"""Merge application localization catalogs into the process-wide runtime."""
	_native.merge_catalog_directory(
		str(directory), override_existing=override_existing
	)


def _string_tuple(values: Iterable[str | PathLike[str]]) -> tuple[str, ...]:
	return tuple(str(value) for value in values)


def _parameter_specs(
	parameters: Sequence[str | HostParameter],
) -> tuple[tuple[str, int, str, str], ...]:
	return tuple(
		(parameter if isinstance(parameter, HostParameter) else HostParameter(parameter))
		._native_spec()
		for parameter in parameters
	)


class HostClass:
	"""A Python class exposed as a strongly typed VeroScript host class."""

	def __init__(
		self,
		native_session: _native.Session,
		type_id: int,
		name: str,
		python_type: type,
		kind: str = "class",
	) -> None:
		self._native_session = native_session
		self._type_id = type_id
		self.name = name
		self.python_type = python_type
		self.kind = kind

	def constructor(
		self,
		*,
		parameters: Sequence[str | HostParameter] = (),
		documentation: str = "",
	) -> "HostClass":
		"""Expose the Python class constructor for one VeroScript signature."""
		self._native_session.register_host_member(
			self._type_id,
			0,
			"",
			self.python_type,
			self.name,
			_parameter_specs(parameters),
			documentation,
		)
		return self

	def method(
		self,
		name: str,
		python_name: str | None = None,
		*,
		return_type: str = "void",
		parameters: Sequence[str | HostParameter] = (),
		documentation: str = "",
	) -> "HostClass":
		"""Expose an instance method for one VeroScript signature."""
		callback = getattr(self.python_type, python_name or name)
		self._native_session.register_host_member(
			self._type_id,
			1,
			name,
			callback,
			return_type,
			_parameter_specs(parameters),
			documentation,
		)
		return self

	def static_method(
		self,
		name: str,
		python_name: str | None = None,
		*,
		return_type: str = "void",
		parameters: Sequence[str | HostParameter] = (),
		documentation: str = "",
	) -> "HostClass":
		"""Expose a static/class-level callable for one VeroScript signature."""
		callback = getattr(self.python_type, python_name or name)
		self._native_session.register_host_member(
			self._type_id,
			2,
			name,
			callback,
			return_type,
			_parameter_specs(parameters),
			documentation,
		)
		return self

	def property(
		self,
		name: str,
		python_name: str | None = None,
		*,
		type_name: str,
		writable: bool = True,
		documentation: str = "",
	) -> "HostClass":
		"""Expose a Python ``property`` as a VeroScript property."""
		descriptor = getattr(self.python_type, python_name or name)
		if not isinstance(descriptor, property) or descriptor.fget is None:
			raise TypeError("python_name must identify a readable Python property")
		getter = descriptor.fget
		setter = descriptor.fset
		if writable and setter is None:
			raise TypeError("writable=True requires a Python property setter")
		self._native_session.register_host_member(
			self._type_id,
			3,
			name,
			getter,
			type_name,
			(),
			documentation,
		)
		if writable:
			assert setter is not None
			self._native_session.register_host_member(
				self._type_id,
				4,
				name,
				setter,
				"void",
				_parameter_specs((HostParameter(type_name, name="value"),)),
				documentation,
			)
		return self

	def field(
		self,
		name: str,
		python_name: str | None = None,
		*,
		type_name: str,
		writable: bool = True,
		documentation: str = "",
	) -> "HostClass":
		"""Expose a Python instance attribute as a VeroScript field."""
		attribute_name = python_name or name

		def getter(instance: object) -> object:
			return getattr(instance, attribute_name)

		self._native_session.register_host_member(
			self._type_id,
			6,
			name,
			getter,
			type_name,
			(),
			documentation,
		)
		if writable:
			def setter(instance: object, value: object) -> None:
				setattr(instance, attribute_name, value)

			self._native_session.register_host_member(
				self._type_id,
				4,
				name,
				setter,
				"void",
				_parameter_specs((HostParameter(type_name, name="value"),)),
				documentation,
			)
		return self

	def protocol(
		self,
		*,
		clone: Callable[[object], object] | None = None,
		equals: Callable[[object, object], object] | None = None,
		hash: Callable[[object], int] | None = None,
		subclass_factory: Callable[[str], object] | None = None,
	) -> "HostClass":
		"""Register optional value and script-subclass lifecycle operations.

        ``clone``, ``equals`` and ``hash`` are intended for host structs.
        ``subclass_factory`` is intended for inheritable host classes and
        receives the concrete script type name.
        """
		self._native_session.register_host_protocol(
			self._type_id,
			clone=clone,
			equals=equals,
			hash=hash,
			subclass_factory=subclass_factory,
		)
		return self


class GenericHostClass:
	"""A registered generic host type template awaiting concrete arguments."""

	def __init__(
		self,
		native_session: _native.Session,
		template_id: int,
		name: str,
		arity: int,
		kind: str,
	) -> None:
		self._native_session = native_session
		self._template_id = template_id
		self.name = name
		self.arity = arity
		self.kind = kind

	def instantiate(self, *type_arguments: str) -> HostClass:
		"""Create/find a concrete type, then return its configurable binding."""
		if len(type_arguments) != self.arity:
			raise ValueError(
				f"{self.name} requires {self.arity} type arguments, got {len(type_arguments)}"
			)
		type_id, python_type, concrete_name = self._native_session.instantiate_generic_host_type(
			self._template_id, type_arguments
		)
		return HostClass(
			self._native_session, type_id, concrete_name, python_type, self.kind
		)


class ConsoleOperation(IntEnum):
	"""Terminal controls; colors use ANSI indices 0..15, default=-1."""
	CLEAR = 0
	CLEAR_LINE = 1
	SET_COLOR = 2
	RESET_STYLE = 3
	MOVE_CURSOR = 4
	SET_CURSOR_VISIBLE = 5


@dataclass(frozen=True, slots=True)
class ConsoleInfo:
	"""Output capabilities and visible dimensions, zero when unknown."""
	output_terminal: bool = False
	color: bool = False
	cursor: bool = False
	clear: bool = False
	width: int = 0
	height: int = 0

	def _native(self) -> tuple[int, int, int]:
		return (int(self.output_terminal) | (int(self.color) << 1)
				| (int(self.cursor) << 2) | (int(self.clear) << 3), self.width, self.height)


class Session:
	"""One creator-thread-confined VeroScript execution session.

    module_environment accepts an opaque handle from the matching native SDK.
    The session retains its immutable snapshot; the producer may release its
    handle after construction. The environment owns source and stdlib policy.
    """

	def variant(self, type_name: str, payload: Value) -> Value:
		"""Box an explicit Value using its canonical type in this session."""
		if not isinstance(payload, Value):
			raise TypeError("variant payload must be a Value")
		return self._native.variant(type_name, payload)

	@property
	def memory(self) -> Memory:
		return Memory(self._native._memory)

	@property
	def numerics(self) -> Numerics:
		return Numerics(self._native._numerics)

	def set_numeric_limits(
		self, *, max_value_bits: int = 0, max_input_bytes: int = 0,
		max_output_bytes: int = 0, max_scratch_bytes: int = 0,
		max_work_units: int = 0,
	) -> None:
		"""Set numeric operation limits on an idle session.

        Zero selects finite Core defaults, rather than an unlimited budget.
        These limits do not cap the host process's total memory use.
        """
		self._native._numerics(
			"limits", max_value_bits, max_input_bytes, max_output_bytes,
			max_scratch_bytes, max_work_units,
		)

	def set_memory_limits(self, max_buffer_bytes: int = 0, max_retained_bytes: int = 0) -> None:
		self._native._memory("limits", max_buffer_bytes, max_retained_bytes)

	def set_stdlib_limits(self, max_text_bytes: int = 0, max_read_bytes: int = 0,
						  max_split_parts: int = 0) -> None:
		"""Set per-operation limits; zero adds no limit. Requires an idle session."""
		self._native._memory("stdlib_limits", max_text_bytes, max_read_bytes, max_split_parts)

	def __init__(
		self,
		*,
		backend: Backend | int = Backend.MIXED,
		import_paths: Iterable[str | PathLike[str]] = (),
		implicit_import_paths: Iterable[str | PathLike[str]] = (),
		script_args: Iterable[str] = (),
		defines: Iterable[str] = (),
		stdlib_search_paths: Iterable[str | PathLike[str]] = (),
		source_overlays: Mapping[str | PathLike[str], str] | None = None,
		bytecode_cache_directory: str | PathLike[str] = "",
		max_call_depth: int = 0,
		max_instructions: int = 0,
		timeout_milliseconds: int = 0,
		enable_execution_control: bool = False,
		capability_policy_enabled: bool = False,
		allowed_script_capabilities: ScriptCapabilities | int = ScriptCapabilities.NONE,
		use_bytecode_cache: bool = False,
		allow_circular_imports: bool = False,
		allow_project_wide_access: bool = False,
		hot_reload_enabled: bool = False,
		print_callback: Callable[[str], object] | None = None,
		diagnostic_callback: Callable[[str], object] | None = None,
		console_query: Callable[[], ConsoleInfo] | None = None,
		console_control: Callable[[ConsoleOperation, int, int], bool] | None = None,
		console_write: Callable[[str], bool] | None = None,
		module_environment: int | None = None,
	) -> None:
		config = {
			"backend": int(backend),
			"import_paths": _string_tuple(import_paths),
			"implicit_import_paths": _string_tuple(implicit_import_paths),
			"script_args": tuple(script_args),
			"defines": tuple(defines),
			"stdlib_search_paths": (
				*((str(_PACKAGE_DIRECTORY / "stdlib"),) if not module_environment else ()),
				*_string_tuple(stdlib_search_paths),
			),
			"source_overlays": {
				str(path): source for path, source in (source_overlays or {}).items()
			},
			"bytecode_cache_directory": str(bytecode_cache_directory),
			"max_call_depth": max_call_depth,
			"max_instructions": max_instructions,
			"timeout_milliseconds": timeout_milliseconds,
			"enable_execution_control": enable_execution_control,
			"capability_policy_enabled": capability_policy_enabled,
			"allowed_script_capabilities": int(allowed_script_capabilities),
			"use_bytecode_cache": use_bytecode_cache,
			"allow_circular_imports": allow_circular_imports,
			"allow_project_wide_access": allow_project_wide_access,
		}
		self._native = _native.Session(
			config, print_callback, diagnostic_callback,
			None if console_query is None else lambda: console_query()._native(),
			None if console_control is None else lambda op, first, second: console_control(ConsoleOperation(op), first, second),
			console_write,
			module_environment,
		)
		if hot_reload_enabled:
			self._native.set_hot_reload_enabled(True)

	@property
	def closed(self) -> bool:
		return self._native.closed

	@property
	def is_loaded(self) -> bool:
		return self._native.is_loaded

	@property
	def diagnostics(self) -> tuple[Diagnostic, ...]:
		return tuple(Diagnostic(**item) for item in self._native.diagnostics())

	@property
	def hot_reload_enabled(self) -> bool:
		return self._native.hot_reload_enabled

	@property
	def reload_status(self) -> ReloadStatus:
		"""Read the latest reload status on the thread that created the session."""
		status = self._native.reload_status()
		return ReloadStatus(
			state=ReloadState(status["state"]),
			operation_id=status["operation_id"],
			previous_generation=status["previous_generation"],
			candidate_generation=status["candidate_generation"],
			current_generation=status["current_generation"],
			committed_generation=status["committed_generation"],
		)

	def close(self) -> None:
		self._native.close()

	def clear(self) -> None:
		self._native.clear()

	def request_cancellation(self) -> None:
		"""Request cooperative cancellation from any Python thread.

        The session must have been created with ``enable_execution_control=True``.
        This does not asynchronously interrupt a host callback already running.
        """
		self._native.request_cancellation()

	def load_source(self, source: str, file_label: str = "<python>") -> RunResult:
		return RunResult._from_native(self._native.load_source(source, file_label))

	def load_file(self, path: str | PathLike[str]) -> RunResult:
		return RunResult._from_native(self._native.load_file(str(path)))

	def load_application(
		self, absolute_path: str | PathLike[str], *, options: Mapping[str, Any] | None = None
	) -> RunResult:
		"""Load an application container/directory with explicit absolute host options."""
		return RunResult._from_native(
			self._native.load_application(fspath(absolute_path), json.dumps(dict(options or {})))
		)

	def load_package(self, package: Package, logical_path: str = "") -> RunResult:
		return RunResult._from_native(
			self._native.load_package(package, logical_path)
		)

	def reload_package(self, package: Package, logical_path: str = "") -> RunResult:
		return RunResult._from_native(
			self._native.reload_package(package, logical_path)
		)

	def build_package_file(
		self,
		source_path: str | PathLike[str],
		*,
		kind: PackageKind = PackageKind.APPLICATION,
		package_name: str,
		package_version: int = 1,
		metadata: Mapping[str, bytes] | None = None,
		strip_debug_info: bool = False,
	) -> tuple[bytes, RunResult]:
		package_bytes, native_result = self._native.build_package_file(
			str(source_path),
			int(kind),
			package_name,
			package_version,
			metadata,
			strip_debug_info,
		)
		return package_bytes, RunResult._from_native(native_result)

	def run(self, mode: RunMode | int = RunMode.SCRIPT) -> RunResult:
		"""Run initialization and optionally main; EVAL_ONCE permits initializer JIT."""
		return RunResult._from_native(self._native.run(int(mode)))

	def reload(self) -> RunResult:
		"""Hot-reload the current program after its source files change."""
		return RunResult._from_native(self._native.reload())

	def set_annotation_processing_service(self, handle: int | None) -> None:
		"""Attach a valid service handle from a native SDK extension while idle.

        The session copies its configuration, so the producer may then release
        its handle. Keep the native processor module and callback data alive
        until the service is cleared or the session closes. None or zero clears
        the service; arbitrary integer addresses are not valid service handles.
        """
		self._native.set_annotation_processing_service(handle)

	def set_compile_processing_service(self, handle: int | None) -> None:
		"""Attach a native SDK general processing service while idle.

        The session copies the configuration. The producer may release its
        handle afterwards, keeping processor code and callback data alive until
        the service is cleared or the session closes. None or zero clears it.
        """
		self._native.set_compile_processing_service(handle)

	def set_script_compile_extensions(
		self, roots: Iterable[str | PathLike[str] | tuple[str, str]] = (), *,
		configuration: Mapping[str, str] | None = None, allowed_roots: Iterable[str | PathLike[str]] = (),
		allow_source_imports: bool = True, max_instructions: int = 1_000_000,
		timeout_milliseconds: int = 2_000,
	) -> None:
		"""Copy explicit extension configuration while idle. An empty call restores defaults."""
		if not COMPILE_CAPABILITIES & CompileCapabilities.SCRIPT_EXTENSIONS:
			raise NotImplementedError("The loaded runtime does not support script compilation extensions.")
		for name, number in (("max_instructions", max_instructions), ("timeout_milliseconds", timeout_milliseconds)):
			if isinstance(number, bool) or not isinstance(number, int):
				raise TypeError(f"{name} must be an integer")
			if number < 0 or number > 2**64 - 1:
				raise ValueError(f"{name} is outside uint64")
		paths = [(fspath(root[0]), root[1]) if isinstance(root, tuple) else (fspath(root), "") for root in roots]
		self._native.set_script_compile_options(
			paths, dict(configuration or {}), [fspath(root) for root in allowed_roots],
			allow_source_imports, max_instructions, timeout_milliseconds,
		)

	def set_jit_backend(self, path: str | PathLike[str] | None) -> None:
		"""Load a backend file or directory while idle; None or an empty string disables it."""
		self._native.set_jit_backend(None if path is None else fspath(path))

	def set_vm_backend(self, path: str | PathLike[str] | None) -> None:
		"""Select a VM file/directory before execution; None explicitly clears it."""
		self._native.set_vm_backend(None if path is None else fspath(path))

	def get_vm_backend_info(self) -> VmBackendInfo:
		return VmBackendInfo(**self._native.get_vm_backend_info())

	def get_jit_backend(self) -> JitBackend:
		return JitBackend(self._native.get_jit_backend())

	def get_jit_backend_info(self) -> JitBackendInfo:
		"""Return an owned snapshot of the selected backend's identity and features."""
		info = self._native.get_jit_backend_info()
		return JitBackendInfo(
			id=info["id"],
			target=info["target"],
			capabilities=info["capabilities"],
			native_code=info["native_code"],
		)

	def set_jit_policy(self, policy: JitPolicy) -> None:
		self._native.set_jit_policy(
			policy.mixed_hot_call_threshold,
			policy.max_compile_attempts,
			policy.max_cached_entries,
			policy.eager_compile_budget,
			policy.reuse_session_code,
			policy.enable_controlled_mixed,
		)

	def set_jit_cache_limits(self, limits: JitCacheLimits) -> None:
		"""Set retained-code limits and begin a fresh JIT cache context."""
		self._native.set_jit_cache_limits(
			limits.max_native_bytes,
			limits.max_retained_payload_bytes,
		)

	@property
	def jit_cache_limits(self) -> JitCacheLimits:
		return JitCacheLimits._from_native(self._native.jit_cache_limits())

	@property
	def jit_cache_stats(self) -> JitCacheStats:
		"""Return cumulative statistics for reusable ordinary function calls."""
		return JitCacheStats._from_native(self._native.jit_cache_stats())

	@property
	def jit_stats(self) -> Mapping[str, int]:
		"""Return a snapshot of the last invocation's compilation and execution counters.

		Executable-entry counters include failed entries. Nested direct calls and
		typed loops are separate events; retained-cache totals use jit_cache_stats.
		"""
		return self._native.jit_stats()

	def reflection(self) -> Mapping[str, object]:
		"""Return reflection metadata for the currently loaded program."""
		return cast(
			Mapping[str, object],
			json.loads(self._native.export_reflection_json()),
		)

	def bindings(self) -> Mapping[str, object]:
		"""Return metadata for the session's registered host bindings."""
		return cast(
			Mapping[str, object],
			json.loads(self._native.export_bindings_json()),
		)

	def call(self, function_name: str, *arguments: object) -> object:
		value, native_result = self._native.call_function(function_name, *arguments)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"call {function_name}", result)
		return value

	def retain_callable(
		self, function_value: Value, *, weak_this: bool = False
	) -> RetainedCallable:
		return RetainedCallable(
			self._native.retain_callable(function_value, weak_this)
		)

	def construct(self, type_name: str, *arguments: object) -> Value:
		value, native_result = self._native.construct_object(type_name, *arguments)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"construct {type_name}", result)
		return value

	def call_method(self, instance: Value, method_name: str, *arguments: object) -> object:
		value, native_result = self._native.call_method(instance, method_name, *arguments)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"call {method_name}", result)
		return value

	def call_method_bypassing_interceptor(
		self, instance: Value, method_name: str, *arguments: object
	) -> object:
		"""Call a method without invoking the session instance interceptor."""
		value, native_result = self._native.call_method_bypassing_interceptor(
			instance, method_name, *arguments
		)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"call {method_name}", result)
		return value

	def get_field(self, instance: Value, field_name: str) -> object:
		value, native_result = self._native.get_field(instance, field_name)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"get field {field_name}", result)
		return value

	def set_field(self, instance: Value, field_name: str, value: object) -> None:
		result = RunResult._from_native(self._native.set_field(instance, field_name, value))
		if not result.success:
			raise VeroScriptError(f"set field {field_name}", result)

	def get_global(self, name: str) -> object:
		value, native_result = self._native.get_global(name)
		result = RunResult._from_native(native_result)
		if not result.success:
			raise VeroScriptError(f"get global {name}", result)
		return value

	def set_global(self, name: str, value: object) -> None:
		result = RunResult._from_native(self._native.set_global(name, value))
		if not result.success:
			raise VeroScriptError(f"set global {name}", result)

	def has_function(self, name: str, argument_count: int) -> bool:
		return self._native.has_function(name, argument_count)

	def has_method(self, instance: Value, name: str, argument_count: int) -> bool:
		return self._native.has_method(instance, name, argument_count)

	def register_function(
		self,
		name: str,
		callback: Callable[..., object],
		*,
		return_type: str = "void",
		parameters: Sequence[str | HostParameter] = (),
		documentation: str = "",
	) -> None:
		"""Register a typed Python callable before compiling dependent source.

        Mutable ``ref`` parameters are delivered as :class:`Value`; assign to
        ``value.value`` or mutate the explicit value to write back. Scalars are
        converted to ordinary Python values; containers remain :class:`Value`.
        """
		specs = _parameter_specs(parameters)
		self._native.register_function(
			name, callback, return_type, specs, documentation
		)

	def register_annotation(
		self,
		name: str,
		*,
		detail: str = "",
		documentation: str = "",
		arguments: Sequence[AnnotationArgument] = (),
		case_sensitive: bool = True,
	) -> None:
		"""Register annotation documentation and its name matching policy.

        Names match exactly by default. ``case_sensitive=False`` folds ASCII
        letters in annotation names; argument names and metadata keys stay exact.
        Registration does not install a compile-time processor.
        """
		self._native.register_annotation(
			name,
			detail,
			documentation,
			tuple(argument._native_spec() for argument in arguments),
			case_sensitive,
		)

	def register_type_alias(
		self,
		name: str,
		target_type: str,
		*,
		type_parameters: Sequence[str] = (),
		documentation: str = "",
	) -> None:
		"""Register a concrete or generic host-visible type alias."""
		self._native.register_type_alias(
			name, tuple(type_parameters), target_type, documentation
		)

	def register_class(
		self,
		name: str,
		python_type: type,
		*,
		documentation: str = "",
		inheritable: bool = False,
	) -> HostClass:
		"""Register a Python-backed class before compiling dependent source."""
		type_id = self._native.register_host_class(
			name, python_type, documentation, 2, inheritable
		)
		return HostClass(self._native, type_id, name, python_type, "class")

	def register_struct(
		self,
		name: str,
		python_type: type,
		*,
		documentation: str = "",
		inheritable: bool = False,
	) -> HostClass:
		"""Register a Python-backed value type before compiling source.

        Register at least a ``clone`` protocol before using the struct in
        operations that copy values. Hashable map/set keys also require
        ``equals`` and ``hash``.
        """
		type_id = self._native.register_host_class(
			name, python_type, documentation, 1, inheritable
		)
		return HostClass(self._native, type_id, name, python_type, "struct")

	def register_enum(
		self,
		name: str,
		python_type: type[IntEnum] | type[IntFlag],
		*,
		documentation: str = "",
		underlying_type: str = "int64",
	) -> int:
		"""Register a scoped Python ``IntEnum`` or ``IntFlag`` before compiling source."""
		if not issubclass(python_type, (IntEnum, IntFlag)):
			raise TypeError("python_type must derive from IntEnum or IntFlag")
		return self._native.register_host_enum(
			name,
			python_type,
			tuple(
				(member_name, int(member.value))
				for member_name, member in python_type.__members__.items()
			),
			documentation,
			underlying_type,
		)

	def register_generic_class(
		self,
		name: str,
		arity: int,
		factory: Callable[..., type],
		*,
		documentation: str = "",
		inheritable: bool = False,
	) -> GenericHostClass:
		"""Register a generic Python-backed host class template.

        Explicitly instantiate and configure concrete members before loading
        source that uses them. The factory receives canonical script type-name
        strings and returns the Python class for that specialization.
        """
		def dispatch(*type_arguments: str) -> tuple[type, int, str, bool]:
			return factory(*type_arguments), 2, documentation, inheritable

		template_id = self._native.register_generic_host_type(
			name, arity, dispatch, documentation
		)
		return GenericHostClass(self._native, template_id, name, arity, "class")

	def register_generic_struct(
		self,
		name: str,
		arity: int,
		factory: Callable[..., type],
		*,
		documentation: str = "",
		inheritable: bool = False,
	) -> GenericHostClass:
		"""Register a generic Python-backed host struct template."""
		def dispatch(*type_arguments: str) -> tuple[type, int, str, bool]:
			return factory(*type_arguments), 1, documentation, inheritable

		template_id = self._native.register_generic_host_type(
			name, arity, dispatch, documentation
		)
		return GenericHostClass(self._native, template_id, name, arity, "struct")

	def register_serializer(
		self,
		type_name: str,
		version: int,
		serialize: Callable[[str, int, Mapping[str, object]], bytes],
		deserialize: Callable[[bytes, int], Mapping[str, object]],
	) -> None:
		"""Register external object persistence callbacks before loading source."""
		self._native.register_serializer(
			type_name, version, serialize, deserialize
		)

	def serialize_object(self, instance: Value) -> tuple[bytes, int]:
		return self._native.serialize_object(instance)

	def deserialize_object(
		self, instance: Value, data: bytes, encoded_version: int
	) -> None:
		self._native.deserialize_object(instance, data, encoded_version)

	def apply_class_defaults(self, instance: Value) -> RunResult:
		return RunResult._from_native(self._native.apply_class_defaults(instance))

	def enumerate_object_references(
		self, instance: Value
	) -> tuple[ExternalReference, ...]:
		return tuple(
			ExternalReference(ExternalReferenceKind(kind), value)
			for kind, value in self._native.enumerate_object_references(instance)
		)

	def set_mutation_callback(
		self, callback: Callable[[MutationEvent], object] | None
	) -> None:
		if callback is None:
			self._native.set_mutation_callback(None)
			return

		def dispatch(event: Mapping[str, object]) -> object:
			return callback(MutationEvent._from_native(event))

		self._native.set_mutation_callback(dispatch)

	def set_instance_call_interceptor(
		self,
		callback: Callable[
			[InterceptContext], InterceptResult | tuple[InterceptResult, object]
		]
		| None,
	) -> None:
		"""Set or clear the interceptor for resolved script instance calls.

        Return ``UNHANDLED`` to run the normal implementation, ``HANDLED``
        after calling :meth:`InterceptContext.proceed`, or
        ``(HANDLED, replacement)`` to replace the call result. Return
        ``FAILED`` to reject the call.
        """
		if callback is None:
			self._native.set_instance_call_interceptor(None)
			return

		def dispatch(data: Mapping[str, object]) -> object:
			return callback(InterceptContext(data))

		self._native.set_instance_call_interceptor(dispatch)

	def host_function(
		self,
		name: str | None = None,
		*,
		return_type: str = "void",
		parameters: Sequence[str | HostParameter] = (),
		documentation: str = "",
	) -> Callable[[Callable[..., object]], Callable[..., object]]:
		"""Decorator form of :meth:`register_function`."""
		def register(callback: Callable[..., object]) -> Callable[..., object]:
			self.register_function(
				name or callback.__name__,
				callback,
				return_type=return_type,
				parameters=parameters,
				documentation=documentation,
			)
			return callback

		return register

	def set_input(self, text: str) -> None:
		self._native.set_input(text)

	def set_hot_reload_enabled(self, enabled: bool) -> None:
		self._native.set_hot_reload_enabled(enabled)

	def __enter__(self) -> "Session":
		if self.closed:
			raise Error("cannot enter a closed VeroScript session")
		return self

	def __exit__(
		self,
		exc_type: type[BaseException] | None,
		exc_value: BaseException | None,
		traceback: TracebackType | None,
	) -> None:
		self.close()


class ByteOrder(IntEnum):
	LITTLE = 0
	BIG = 1


@dataclass(frozen=True, slots=True)
class MemoryStats:
	owned_bytes: int
	borrowed_bytes: int
	peak_owned_bytes: int


MemoryLease = _native.MemoryLease


class Numerics:
	"""Checked bigint/rational services for a session or active native callback.

    Parse explicit UTF-8 text and retain the returned Value for script calls.
    Conversion returns a Value preserving the requested scalar type. Native
    numeric operations release the GIL and use the Core resource limits.
    """

	def __init__(self, dispatch: Callable[..., Any]) -> None:
		self._dispatch = dispatch

	def parse(self, text: str, kind: NumericKind = NumericKind.BIGINT) -> Value:
		return cast(Value, self._dispatch("parse", text, kind))

	def bigint(self, text: str) -> Value:
		return self.parse(text, NumericKind.BIGINT)

	def rational(self, text: str) -> Value:
		return self.parse(text, NumericKind.RATIONAL)

	def to_text(self, value: Value) -> str:
		return cast(str, self._dispatch("text", value))

	def convert(self, value: Value, type_name: str, *, exact_float: bool = False) -> Value:
		"""Convert to a fixed scalar, optionally requiring an exact float result."""
		return cast(Value, self._dispatch("convert", value, type_name, exact_float))


class Memory:
	"""Checked regions owned by a Session or the currently active callback.

    Keep leases until native work completes. Release all memoryview exports
    before closing a lease. Lengths are int64; identities preserve uint64.
    """
	_numeric_types = {
		"int": 1, "int32": 1, "double": 3, "uint32": 5,
		"unsigned int": 5, "int64": 10, "uint64": 11,
		"float": 24, "int8": 25, "uint8": 26, "int16": 27, "uint16": 28,
		"char": 25, "unsigned char": 26, "short": 27, "unsigned short": 28,
		"long": 10, "unsigned long": 11,
	}

	def __init__(self, dispatch: Callable[..., Any]) -> None:
		self._dispatch = dispatch

	@property
	def stats(self) -> MemoryStats:
		return MemoryStats(*self._dispatch("stats"))

	@property
	def native_addresses_available(self) -> bool:
		return bool(self._dispatch("available"))

	def buffer(self, size: int = 0) -> Value:
		return cast(Value, self._dispatch("buffer", size))

	def view(self, read_only: bool = False) -> Value:
		return cast(Value, self._dispatch("view", read_only))

	def size(self, region: Value) -> int:
		return int(self._dispatch("size", region))

	def capacity(self, buffer: Value) -> int:
		return int(self._dispatch("capacity", buffer))

	def resize(self, buffer: Value, size: int, *, reserve_only: bool = False) -> None:
		self._dispatch("resize", buffer, size, reserve_only)

	def slice(self, region: Value, offset: int, count: int, *, read_only: bool = False) -> Value:
		return cast(Value, self._dispatch("slice", region, offset, count, read_only))

	def clone(self, source: Value) -> Value:
		return cast(Value, self._dispatch("clone", source))

	def from_array(self, source: Value) -> Value:
		return cast(Value, self._dispatch("from_array", source))

	def to_array(self, source: Value) -> Value:
		return cast(Value, self._dispatch("to_array", source))

	def copy(self, destination: Value, destination_offset: int, source: Value, source_offset: int, count: int) -> None:
		self._dispatch("copy", destination, destination_offset, source, source_offset, count)

	def fill(self, destination: Value, offset: int, count: int, value: int) -> None:
		self._dispatch("fill", destination, offset, count, value)

	def equal(self, left: Value, left_offset: int, right: Value, right_offset: int, count: int) -> bool:
		return bool(self._dispatch("equal", left, left_offset, right, right_offset, count))

	def read(self, source: Value, offset: int, type_name: str, order: ByteOrder = ByteOrder.LITTLE) -> int | float:
		return cast(int | float, self._dispatch("read", source, offset, self._numeric_types[type_name], int(order)))

	def write(self, destination: Value, offset: int, type_name: str, value: int | float | Value, order: ByteOrder = ByteOrder.LITTLE) -> None:
		encoded = value if isinstance(value, Value) else Value(value)
		self._dispatch("write", destination, offset, self._numeric_types[type_name], int(order), encoded)

	def object_id(self, value: Value) -> int:
		return int(self._dispatch("id", value))

	def object_address(self, value: Value) -> int:
		return int(self._dispatch("address", value))

	def data_address(self, region: Value) -> int:
		return int(self._dispatch("data_address", region))

	def argument_address(self, index: int) -> int:
		"""Query the original ref binding inside an active host callback."""
		return int(self._dispatch("argument_address", index))

	def mutation_version(self, region: Value) -> int:
		return int(self._dispatch("version", region))

	def wrap_external(self, source: object, *, read_only: bool = False) -> Value:
		"""Retain a contiguous Python buffer until all regions/leases release."""
		return cast(Value, self._dispatch("external", source, read_only))

	def invalidate_external(self, region: Value) -> None:
		self._dispatch("invalidate", region)

	def acquire_lease(self, region: Value, *, write: bool = False) -> MemoryLease:
		return cast(MemoryLease, self._dispatch("lease", region, write))


def current_memory() -> Memory:
	"""Access memory services while a registered native callback is running."""
	return Memory(_native._memory_current)


def current_numerics() -> Numerics:
	"""Access numeric services only while a registered native callback runs."""
	return Numerics(_native._numerics_current)


ABI_VERSION = _native.ABI_VERSION
RUNTIME_VERSION = _native.RUNTIME_VERSION
HOST_PLATFORM = _native.HOST_PLATFORM
CAPABILITIES = _native.CAPABILITIES
COMPILE_CAPABILITIES = _native.COMPILE_CAPABILITIES
__version__ = RUNTIME_VERSION.partition(" ")[0]

__all__ = [
	"COMPILE_CAPABILITIES", "CompileCapabilities", "DiagnosticSeverity",
	"ByteOrder", "Memory", "MemoryLease", "MemoryStats", "current_memory",
	"NumericKind", "Numerics", "current_numerics",
	"ABI_VERSION", "AnnotationArgument", "Backend", "CAPABILITIES", "CallbackError", "ConsoleInfo", "ConsoleOperation", "Diagnostic",
	"HOST_PLATFORM", "__version__",
	"Error", "ExternalReference", "ExternalReferenceKind", "GenericHostClass", "HostClass", "HostParameter",
	"InterceptContext", "InterceptResult",
	"JitBackend", "JitBackendCapabilities", "JitBackendInfo", "VmBackendInfo", "JitCacheLimits", "JitCacheStats", "JitPolicy", "MutationEvent", "MutationKind",
	"Package", "PackageKind", "RUNTIME_VERSION",
	"ReloadState", "ReloadStatus", "ResolvedMethod", "RetainedCallable", "RunMode", "RunResult", "RuntimeCapabilities", "ScriptCapabilities", "Session", "ThreadError", "TypeMismatchError",
	"Value", "VeroScriptError",
	"available_languages", "get_language", "merge_catalog_directory", "set_language",
]
