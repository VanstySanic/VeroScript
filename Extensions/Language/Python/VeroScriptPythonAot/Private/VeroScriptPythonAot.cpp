// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptJsonString.h"
#include "VeroScriptPythonAot.h"

#include "VeroScriptAst.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConstantEvaluation.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptVariant.h"
#include "VeroScriptStandardLibraryServices.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		std::string PythonIdentifier(const std::string_view Name)
		{
			static const std::unordered_set<std::string> Keywords = {
			    "False",
			    "None",
			    "True",
			    "and",
			    "as",
			    "assert",
			    "async",
			    "await",
			    "break",
			    "class",
			    "continue",
			    "def",
			    "del",
			    "elif",
			    "else",
			    "except",
			    "finally",
			    "for",
			    "from",
			    "global",
			    "if",
			    "import",
			    "in",
			    "is",
			    "lambda",
			    "nonlocal",
			    "not",
			    "or",
			    "pass",
			    "raise",
			    "return",
			    "try",
			    "while",
			    "with",
			    "yield",
			};
			std::ostringstream Result;
			for (const unsigned char Char : Name)
			{
				if ((Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z') || (Char >= '0' && Char <= '9') || Char == '_')
				{
					Result << static_cast<char>(Char);
				}
				else if (Char == '.' || Char == ':')
				{
					Result << "__";
				}
				else
				{
					Result << "_u" << std::hex << std::setw(2) << std::setfill('0')
					       << static_cast<unsigned int>(Char) << std::dec;
				}
			}
			std::string Out = Result.str();
			if (Out.empty() || (Out.front() >= '0' && Out.front() <= '9'))
			{
				Out.insert(Out.begin(), '_');
			}
			if (Keywords.find(Out) != Keywords.end())
			{
				Out += '_';
			}
			return Out;
		}

		std::string QuotePython(const std::string_view Value)
		{
			std::ostringstream Out;
			Out << '"';
			for (const unsigned char Char : Value)
			{
				switch (Char)
				{
				case '\\':
					{
						Out << "\\\\";
						break;
					}
				case '"':
					{
						Out << "\\\"";
						break;
					}
				case '\n':
					{
						Out << "\\n";
						break;
					}
				case '\r':
					{
						Out << "\\r";
						break;
					}
				case '\t':
					{
						Out << "\\t";
						break;
					}
				case '\b':
					{
						Out << "\\b";
						break;
					}
				case '\f':
					{
						Out << "\\f";
						break;
					}
				default:
					{
						if (Char < 0x20 || Char == 0x7f)
						{
							Out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
							    << static_cast<unsigned int>(Char) << std::dec;
						}
						else
						{
							Out << static_cast<char>(Char);
						}
						break;
					}
				}
			}
			Out << '"';
			return Out.str();
		}

		std::string QuotePythonByteString(const std::string_view Value)
		{
			if (std::all_of(Value.begin(), Value.end(), [](const unsigned char Char)
			{
				return Char < 0x80;
			}))
			{
				return QuotePython(Value);
			}
			std::u16string Validation;
			if (Runtime::Utf8ToUtf16(Value, Validation))
			{
				return QuotePython(Value);
			}
			// Narrow literals may contain arbitrary bytes from \xNN escapes.
			// Keep generated Python source UTF-8 while preserving those bytes.
			std::ostringstream Out;
			Out << "b\"";
			for (const unsigned char Char : Value)
			{
				Out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
				    << static_cast<unsigned int>(Char);
			}
			Out << "\".decode(\"utf-8\", \"surrogateescape\")";
			return Out.str();
		}

		std::string Utf16ToUtf8(const std::u16string_view Text)
		{
			std::string Out;
			for (std::size_t Index = 0; Index < Text.size(); ++Index)
			{
				std::uint32_t CodePoint = Text[Index];
				if (CodePoint >= 0xd800 && CodePoint <= 0xdbff && Index + 1 < Text.size())
				{
					const std::uint32_t Low = Text[Index + 1];
					if (Low >= 0xdc00 && Low <= 0xdfff)
					{
						CodePoint = 0x10000 + ((CodePoint - 0xd800) << 10) + (Low - 0xdc00);
						++Index;
					}
				}
				if (CodePoint <= 0x7f)
				{
					Out.push_back(static_cast<char>(CodePoint));
				}
				else if (CodePoint <= 0x7ff)
				{
					Out.push_back(static_cast<char>(0xc0 | (CodePoint >> 6)));
					Out.push_back(static_cast<char>(0x80 | (CodePoint & 0x3f)));
				}
				else if (CodePoint <= 0xffff)
				{
					Out.push_back(static_cast<char>(0xe0 | (CodePoint >> 12)));
					Out.push_back(static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3f)));
					Out.push_back(static_cast<char>(0x80 | (CodePoint & 0x3f)));
				}
				else
				{
					Out.push_back(static_cast<char>(0xf0 | (CodePoint >> 18)));
					Out.push_back(static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3f)));
					Out.push_back(static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3f)));
					Out.push_back(static_cast<char>(0x80 | (CodePoint & 0x3f)));
				}
			}
			return Out;
		}

		std::string RuntimeSupport()
		{
			return R"PY(# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

# Generated by VeroScriptPythonAot. Requires Python 3.11-3.15.
from __future__ import annotations

import enum as _vs_enum
import copy as _vs_copy
import functools as _vs_functools
import math as _vs_math
import os as _vs_os
import pathlib as _vs_pathlib
import stat as _vs_stat
import re as _vs_re
import struct as _vs_struct
import sys as _vs_sys
import threading as _vs_threading
import time as _vs_time
import weakref as _vs_weakref
from collections import deque as _vs_deque


for _vs_stream in (_vs_sys.stdout, _vs_sys.stderr):
	if hasattr(_vs_stream, "reconfigure"):
		_vs_stream.reconfigure(encoding="utf-8", errors="surrogateescape")


class _VsExit(BaseException):
	def __init__(self, code: int) -> None:
		self.code = int(code)


class _VsThrown(Exception):
	def __init__(self, value: object) -> None:
		super().__init__(_vs_text(value))
		self.value = value


class _VsForContinue(BaseException):
	pass


class _VsSwitchBreak(BaseException):
	pass


_VS_CHANNEL_LOCK = _vs_threading.Lock()
_VS_CHANNELS = _vs_weakref.WeakSet()
_VS_SHUTTING_DOWN = False


class _VsChannel:
	"""The only mutable capability shared between isolated script workers."""

	def __init__(self, type_name: str = "channel<?>") -> None:
		self._vs_type_name = type_name
		self._condition = _vs_threading.Condition()
		self._values = _vs_deque()
		self._capacity = 0
		self._capacity_configured = False
		self._used = False
		with _VS_CHANNEL_LOCK:
			self._closed = _VS_SHUTTING_DOWN
			_VS_CHANNELS.add(self)

	def __deepcopy__(self, memo: dict[int, object]) -> _VsChannel:
		# A channel is an explicit shared capability, not ordinary script state.
		return self

	def _cast_value(self, value: object) -> object:
		element_type = _vs_element_type(self._vs_type_name)
		return _vs_container_value(element_type, value, "channel_send")

	def send(self, value: object) -> None:
		with self._condition:
			self._used = True
			self._condition.wait_for(
				lambda: self._closed
				or self._capacity == 0
				or len(self._values) < self._capacity)
			if self._closed:
				raise RuntimeError("channel is closed")
			self._values.append(self._cast_value(value))
			self._condition.notify_all()

	def try_send(self, value: object) -> bool:
		with self._condition:
			self._used = True
			if self._closed or (
					self._capacity != 0 and len(self._values) >= self._capacity):
				return False
			self._values.append(self._cast_value(value))
			self._condition.notify_all()
			return True

	def send_for(self, value: object, timeout_ms: int) -> bool:
		if timeout_ms < 0:
			raise ValueError("channel timeout must be non-negative")
		with self._condition:
			self._used = True
			ready = self._condition.wait_for(
				lambda: self._closed
				or self._capacity == 0
				or len(self._values) < self._capacity,
				timeout_ms / 1000.0)
			if not ready or self._closed:
				return False
			self._values.append(self._cast_value(value))
			self._condition.notify_all()
			return True

	def recv(self) -> object:
		with self._condition:
			self._used = True
			self._condition.wait_for(lambda: self._closed or bool(self._values))
			if not self._values:
				raise RuntimeError("channel is closed")
			value = self._values.popleft()
			self._condition.notify_all()
			return value

	def try_recv(self) -> _VsArray:
		with self._condition:
			self._used = True
			if not self._values:
				return _VsArray(type_name=f"array<{_vs_element_type(self._vs_type_name)}>")
			value = self._values.popleft()
			self._condition.notify_all()
		return _VsArray(
			[value], type_name=f"array<{_vs_element_type(self._vs_type_name)}>")

	def recv_for(self, timeout_ms: int) -> _VsArray:
		if timeout_ms < 0:
			raise ValueError("channel timeout must be non-negative")
		with self._condition:
			self._used = True
			ready = self._condition.wait_for(
				lambda: self._closed or bool(self._values), timeout_ms / 1000.0)
			if not ready or not self._values:
				return _VsArray(type_name=f"array<{_vs_element_type(self._vs_type_name)}>")
			value = self._values.popleft()
			self._condition.notify_all()
		return _VsArray(
			[value], type_name=f"array<{_vs_element_type(self._vs_type_name)}>")

	def close(self) -> None:
		with self._condition:
			self._closed = True
			self._condition.notify_all()

	def is_closed(self) -> bool:
		with self._condition:
			return self._closed

	def size(self) -> int:
		with self._condition:
			return len(self._values)

	def capacity(self) -> int:
		with self._condition:
			return self._capacity

	def set_capacity(self, capacity: int) -> None:
		capacity = _vs_reserve_count(capacity)
		if capacity <= 0:
			raise ValueError("channel capacity must be positive")
		with self._condition:
			if self._closed or self._used or self._capacity_configured:
				raise RuntimeError(
					"channel capacity can only be configured before first use")
			self._capacity = capacity
			self._capacity_configured = True


_VS_GLOBAL_DEFAULTS: dict[str, object] = {}
_VS_STATIC_DEFAULTS: dict[tuple[type, str], object] = {}
_VS_THREAD_STATE = _vs_threading.local()


class _VsReservedZero(int):
	def __new__(cls, type_name: str):
		value = super().__new__(cls, 0)
		value._vs_type_name = type_name
		return value

	def __reduce__(self):
		return type(self), (self._vs_type_name,)

	def _vs_matches_type(self, type_name: str) -> bool:
		source = _vs_canonical_type(self._vs_type_name)
		target = _vs_canonical_type(type_name)
		if source == target:
			return True
		specification = _vs_integer_spec(source)
		return specification is not None and specification == _vs_integer_spec(target)


def _vs_condition(value: object) -> bool:
	if isinstance(value, _VsAttribute):
		value = value.get()
	if type(value) is not bool:
		raise TypeError("condition must be bool")
	return value


def _vs_equal(left: object, right: object, left_type: str = "", right_type: str = "", storage: bool = False) -> bool:
	left_reserved = isinstance(left, _VsReservedZero)
	right_reserved = isinstance(right, _VsReservedZero)
	if storage and (left_reserved or right_reserved):
		# Default value equality compares actual FValue alternatives, without
		# the numeric or text coercions of an explicit script comparison.
		if left_reserved and right_reserved:
			return True
		other = right if left_reserved else left
		type_name = _vs_resolve_type_name(right_type if left_reserved else left_type)
		cls = _vs_lookup_type(type_name)
		if isinstance(cls, type) and issubclass(cls, _vs_enum.IntEnum):
			if getattr(cls, "_vs_scoped_enum", True):
				return False
			type_name = getattr(cls, "_vs_underlying_type", "int")
		return (_vs_integer_spec(type_name) == (32, True)
				and isinstance(other, int) and not isinstance(other, bool)
				and int(other) == 0)
	if left_type and right_type:
		left_base = _vs_base_type(_vs_resolve_type_name(left_type))
		right_base = _vs_base_type(_vs_resolve_type_name(right_type))
		if left_base == right_base and left_base in {"string", "wstring"}:
			return _vs_compare_text(left, right, "==", left_base == "wstring")
	if left_reserved or right_reserved:
		other = right if left_reserved else left
		if isinstance(other, bool):
			return False
		if isinstance(other, _vs_enum.IntEnum):
			if getattr(type(other), "_vs_scoped_enum", True):
				return False
			return int(left) == int(right)
	return left == right


def _vs_generic_stack() -> list[dict[str, str]]:
	stack = getattr(_VS_THREAD_STATE, "generic_stack", None)
	if stack is None:
		stack = []
		_VS_THREAD_STATE.generic_stack = stack
	return stack


def _vs_resolve_type_name(type_name: str) -> str:
	result = type_name
	for bindings in reversed(_vs_generic_stack()):
		for name, replacement in bindings.items():
			result = _vs_re.sub(
				rf"(?<![A-Za-z0-9_]){_vs_re.escape(name)}(?![A-Za-z0-9_])",
				replacement,
				result)
	return result


def _vs_generic_function(function: object) -> object:
	@_vs_functools.wraps(function)
	def invoke(*args: object, **kwargs: object) -> object:
		raw_bindings = kwargs.pop("___vs_type_args", {})
		if not isinstance(raw_bindings, dict):
			raise TypeError("invalid generated generic type bindings")
		bindings = {
			str(name): _vs_resolve_type_name(str(type_name))
			for name, type_name in raw_bindings.items()
		}
		stack = _vs_generic_stack()
		stack.append(bindings)
		try:
			return function(*args, **kwargs)
		finally:
			stack.pop()
	return invoke


def _vs_thread_values(attribute: str, defaults: dict) -> dict:
	values = getattr(_VS_THREAD_STATE, attribute, None)
	if values is None:
		global_values, static_values = _vs_worker_clone(
			(_VS_GLOBAL_DEFAULTS, _VS_STATIC_DEFAULTS))
		_VS_THREAD_STATE.globals = global_values
		_VS_THREAD_STATE.statics = static_values
		values = getattr(_VS_THREAD_STATE, attribute)
	return values


def _vs_define_global(name: str, value: object) -> None:
	_VS_GLOBAL_DEFAULTS[name] = _vs_clone(value)
	_vs_thread_values("globals", _VS_GLOBAL_DEFAULTS)[name] = value


def _vs_global_get(name: str) -> object:
	# Core reserves global slots as integer zero before module initialization.
	return _vs_thread_values("globals", _VS_GLOBAL_DEFAULTS).get(name, 0)


def _vs_global_set(name: str, value: object) -> object:
	_vs_thread_values("globals", _VS_GLOBAL_DEFAULTS)[name] = value
	return value


def _vs_define_static(owner: type, name: str, value: object) -> None:
	key = (owner, name)
	_VS_STATIC_DEFAULTS[key] = _vs_clone(value)
	_vs_thread_values("statics", _VS_STATIC_DEFAULTS)[key] = value


def _vs_static_get(owner: type, name: str) -> object:
	return _vs_thread_values("statics", _VS_STATIC_DEFAULTS).get((owner, name), 0)


def _vs_discard(value: object) -> None:
	# Complete the receiver call before reading a static field, without retaining it.
	return None


def _vs_static_set(owner: type, name: str, value: object) -> object:
	_vs_thread_values("statics", _VS_STATIC_DEFAULTS)[(owner, name)] = value
	return value


def _vs_member_set(owner: object, name: str, value: object) -> object:
	setattr(owner, name, value)
	return value


def _vs_freeze_thread_defaults() -> None:
	global_values = _vs_thread_values("globals", _VS_GLOBAL_DEFAULTS)
	static_values = _vs_thread_values("statics", _VS_STATIC_DEFAULTS)
	frozen_globals, frozen_statics = _vs_worker_clone(
		(global_values, static_values))
	_VS_GLOBAL_DEFAULTS.clear()
	_VS_GLOBAL_DEFAULTS.update(frozen_globals)
	_VS_STATIC_DEFAULTS.clear()
	_VS_STATIC_DEFAULTS.update(frozen_statics)


)PY"
			       R"PY(
class _VsTaskState:
	def __init__(self, worker: object, channel: _VsChannel, auto_close: bool) -> None:
		self.worker = worker
		self.channel = channel
		self.auto_close = auto_close
		self.done = _vs_threading.Event()
		self.cancel_requested = _vs_threading.Event()
		self.error: BaseException | None = None
		self.thread: _vs_threading.Thread | None = None
		self.joined = False


class _VsTaskRegistry:
	def __init__(self) -> None:
		self.tasks: dict[int, _VsTaskState] = {}
		self.next_task_id = 0


_VS_TASK_LOCK = _vs_threading.Lock()
_VS_ALL_TASKS: list[_VsTaskState] = []
_VS_WORKERS: list[tuple[str, str, bool, str, object]] = []


def _vs_task_registry() -> _VsTaskRegistry:
	registry = getattr(_VS_THREAD_STATE, "task_registry", None)
	if registry is None:
		registry = _VsTaskRegistry()
		_VS_THREAD_STATE.task_registry = registry
	return registry


def _vs_register_worker(
		name: str, module: str, exported: bool,
		channel_type: str, worker: object) -> None:
	_VS_WORKERS.append(
		(name, module or "local", exported, channel_type, worker))


def _vs_resolve_worker(
		name: object, caller_module: str, channel: object) -> object:
	if not isinstance(channel, _VsChannel):
		raise TypeError("spawn second argument must be channel<T>")
	requested = str(name)
	caller = caller_module or "local"
	visible = [entry for entry in _VS_WORKERS
			   if entry[0] == requested and (entry[2] or entry[1] == caller)]
	local = [entry for entry in visible if entry[1] == caller]
	matches = local if local else visible
	if len(matches) != 1:
		raise RuntimeError("spawn target not found")
	expected_type = _vs_canonical_type(matches[0][3])
	actual_type = _vs_canonical_type(channel._vs_type_name)
	if expected_type != actual_type:
		raise RuntimeError(
			f"spawn channel type mismatch: expected {expected_type}, "
			f"got {actual_type}")
	return matches[0][4]


def _vs_spawn(worker: object, channel: _VsChannel, auto_close: bool = False) -> int:
	if not callable(worker):
		raise RuntimeError("spawn target not found")
	if not isinstance(channel, _VsChannel):
		raise TypeError("spawn second argument must be channel<T>")
	registry = _vs_task_registry()
	with _VS_TASK_LOCK:
		if registry.next_task_id >= 2147483647:
			raise RuntimeError("task id space exhausted")
		task_id = registry.next_task_id
		registry.next_task_id += 1
		state = _VsTaskState(worker, channel, auto_close)
		registry.tasks[task_id] = state
		_VS_ALL_TASKS.append(state)

	def run() -> None:
		# Accessing a script global/static lazily clones the frozen module state
		# into this worker. Channel instances deliberately retain identity.
		_VS_THREAD_STATE.task_registry = _VsTaskRegistry()
		_VS_THREAD_STATE.cancel_requested = state.cancel_requested
		try:
			worker(channel)
		except _VsExit as exit_request:
			state.error = RuntimeError(
				f"spawned task called exit({exit_request.code})")
		except BaseException as error:
			state.error = error
		finally:
			if auto_close or state.error is not None:
				channel.close()
			state.done.set()

	state.thread = _vs_threading.Thread(
		target=run, name=f"VeroScript-{task_id}", daemon=True)
	try:
		state.thread.start()
	# An interrupt can arrive after native thread creation. Keep that task tracked.
	except Exception as error:
		with _VS_TASK_LOCK:
			registry.tasks.pop(task_id, None)
			_VS_ALL_TASKS.remove(state)
			state.thread = None
			state.error = error
			state.done.set()
		raise
	return task_id


def _vs_task_state(task_id: int) -> _VsTaskState:
	registry = _vs_task_registry()
	with _VS_TASK_LOCK:
		state = registry.tasks.get(task_id)
	if state is None:
		raise RuntimeError("invalid task id")
	return state


def _vs_join_task_for(task_id: int, timeout_ms: int) -> bool:
	if timeout_ms < 0:
		raise ValueError("task timeout must be non-negative")
	state = _vs_task_state(task_id)
	if state.thread is _vs_threading.current_thread():
		raise RuntimeError("cannot join running task")
	if not state.done.wait(timeout_ms / 1000.0):
		return False
	_vs_finish_task(state)
	return True


def _vs_finish_task(state: _VsTaskState, propagate_error: bool = True) -> None:
	thread = state.thread
	if thread is _vs_threading.current_thread():
		raise RuntimeError("cannot join running task")
	if thread is not None:
		thread.join()
	state.joined = True
	if propagate_error and state.error is not None:
		raise RuntimeError("spawned task failed") from state.error


def _vs_join_timeout_seconds() -> float:
	timeout_text = _vs_os.environ.get("VERO_JOIN_TIMEOUT_MS", "30000")
	try:
		milliseconds = int(timeout_text)
		return (milliseconds if milliseconds >= 0 else 30000) / 1000.0
	except ValueError:
		return 30.0


def _vs_join_task(task_id: int) -> None:
	state = _vs_task_state(task_id)
	if state.thread is _vs_threading.current_thread():
		raise RuntimeError("cannot join running task")
	if not state.done.wait(_vs_join_timeout_seconds()):
		state.channel.close()
		if not state.done.wait(1.0):
			raise RuntimeError("spawned task failed or join_task timed out")
	_vs_finish_task(state)


def _vs_task_done(task_id: int) -> bool:
	return _vs_task_state(task_id).done.is_set()


def _vs_cancel_requested() -> bool:
	marker = getattr(_VS_THREAD_STATE, "cancel_requested", None)
	return marker.is_set() if marker is not None else False


def _vs_cancel_task(task_id: int) -> None:
	state = _vs_task_state(task_id)
	state.cancel_requested.set()
	state.channel.close()
	if state.done.wait(1.0):
		_vs_finish_task(state, propagate_error=False)


def _vs_shutdown() -> None:
	global _VS_SHUTTING_DOWN
	# Mirror VM teardown: unblock channel waits and explicitly join every
	# worker that cooperates. Workers are daemon threads solely so a worker
	# that ignores cancellation cannot keep CPython alive after timeout.
	with _VS_TASK_LOCK:
		_VS_SHUTTING_DOWN = True
	with _VS_CHANNEL_LOCK:
		channels = tuple(_VS_CHANNELS)
	for channel in channels:
		channel.close()
	timeout = _vs_join_timeout_seconds()
	deadline = _vs_time.monotonic() + timeout
	# Workers may publish descendants while this generation is being joined.
	while True:
		with _VS_TASK_LOCK:
			states = tuple(state for state in _VS_ALL_TASKS if not state.joined)
		if not states:
			return
		for state in states:
			remaining = max(0.0, deadline - _vs_time.monotonic())
			if not state.done.wait(remaining):
				raise RuntimeError("spawned task failed or join_task timed out")
			_vs_finish_task(state, propagate_error=False)


class _VsEnum(_vs_enum.IntEnum):
	@classmethod
	def _missing_(cls, value: object):
		number = int(value)
		member = int.__new__(cls, number)
		member._name_ = None
		member._value_ = number
		return member

	def __eq__(self, other: object) -> bool:
		return type(self) is type(other) and int(self) == int(other)

	def __ne__(self, other: object) -> bool:
		return not self == other

	def __hash__(self) -> int:
		return hash((type(self), int(self)))


class _VsFloat32(float):
	def __new__(cls, value: object = 0.0):
		number = float(value)
		try:
			rounded = _vs_struct.unpack("!f", _vs_struct.pack("!f", number))[0]
		except OverflowError:
			rounded = _vs_math.copysign(float("inf"), number)
		return float.__new__(cls, rounded)


class _VsFormatValue:
	def __init__(self, value: object) -> None:
		self.value = value

	def __format__(self, spec: str) -> str:
		if not spec:
			return _vs_text(self.value)
		return format(self.value, spec)


)PY"
			       R"PY(
def _vs_reserve_count(capacity: object) -> int:
	if isinstance(capacity, _VsReservedZero):
		capacity = int(capacity)
	if type(capacity) is not int:
		raise TypeError("reserve expects int capacity")
	if capacity < 0:
		raise ValueError("reserve capacity cannot be negative")
	if capacity > min(9223372036854775807, _vs_sys.maxsize):
		raise OverflowError("reserve capacity exceeds platform range")
	return capacity


def _vs_container_index(index: object, size: int, kind: str) -> int:
	if isinstance(index, _VsReservedZero):
		index = int(index)
	if type(index) is not int:
		raise TypeError(f"{kind} index must be int")
	if index < 0 or index > min(9223372036854775807, _vs_sys.maxsize) or index >= size:
		raise IndexError(f"{kind} index out of bounds")
	return index


def _vs_index(value: object, index: object) -> object:
	if isinstance(value, str):
		raise TypeError("string indexing is unsupported in Python AOT")
	return value[index]


def _vs_scalar_array_type(type_name: str) -> bool:
	return _vs_base_type(_vs_element_type(type_name)) in {
		"char", "int8", "unsigned char", "uint8", "short", "int16",
		"unsigned short", "uint16", "int", "int32", "unsigned int",
		"uint", "unsigned", "uint32", "int64", "long", "uint64",
		"unsigned long", "float", "double", "bool",
	}


def _vs_bool_array_type(type_name: str) -> bool:
	return _vs_base_type(_vs_element_type(type_name)) == "bool"


def _vs_bool_word_bits() -> int:
	return 32 if _vs_os.name == "nt" else _vs_struct.calcsize("P") * 8


def _vs_reserved_array_capacity(type_name: str, capacity: int) -> int:
	if not _vs_bool_array_type(type_name) or capacity == 0:
		return capacity
	word_bits = _vs_bool_word_bits()
	return _vs_reserve_count(((capacity + word_bits - 1) // word_bits) * word_bits)


def _vs_check_container_value(type_name: str, value: object, kind: str) -> object:
	# Dynamic containers validate the actual payload, unlike same-type copies.
	if isinstance(value, _VsReservedZero):
		if type_name and _vs_integer_spec(_vs_base_type(type_name)) != (32, True):
			raise TypeError(f"{kind} value type mismatch")
		return int(value)
	return value


def _vs_container_value(type_name: str, value: object, kind: str) -> object:
	value = _vs_check_container_value(type_name, value, kind)
	return _vs_cast(type_name, value) if type_name else value


class _VsArray(list):
	def __init__(self, values: object = (), type_name: str = "array<?>") -> None:
		self._vs_type_name = type_name
		self._vs_capacity = 0
		super().__init__()
		for value in values:
			self.push(value)
		# VM list initializers and container snapshots reserve their final size.
		self._vs_capacity = _vs_reserved_array_capacity(type_name, len(self))

	def __getitem__(self, index: object) -> object:
		checked = _vs_container_index(index, len(self), "array")
		return super().__getitem__(checked)

	def __setitem__(self, index: object, value: object) -> None:
		checked = _vs_container_index(index, len(self), "array")
		element_type = _vs_element_type(self._vs_type_name)
		super().__setitem__(checked, _vs_container_value(element_type, value, "array"))

	def push(self, value: object) -> _VsArray:
		if len(self) == self._vs_capacity:
			if _vs_bool_array_type(self._vs_type_name):
				word_bits = _vs_bool_word_bits()
				if self._vs_capacity == 0:
					self._vs_capacity = word_bits
				elif _vs_os.name == "nt":
					self._vs_capacity += max(self._vs_capacity // 2, word_bits)
				else:
					self._vs_capacity *= 2
			elif self._vs_capacity == 0:
				self._vs_capacity = 1
			elif _vs_os.name == "nt":
				self._vs_capacity += max(self._vs_capacity // 2, 1)
			else:
				self._vs_capacity *= 2
		element_type = _vs_element_type(self._vs_type_name)
		self.append(_vs_container_value(element_type, value, "array"))
		return self

	def size(self) -> int:
		return len(self)

	def capacity(self) -> int:
		return self._vs_capacity

	def reserve(self, capacity: int) -> _VsArray:
		requested = _vs_reserved_array_capacity(
			self._vs_type_name, _vs_reserve_count(capacity))
		if requested > self._vs_capacity:
			self._vs_capacity = requested
		return self

	def clear(self) -> _VsArray:
		super().clear()
		if not _vs_scalar_array_type(self._vs_type_name):
			self._vs_capacity = 0
		return self

	def remove_at(self, index: int) -> _VsArray:
		checked = _vs_container_index(index, len(self), "remove_at")
		super().__delitem__(checked)
		return self

	def remove(self, value: object) -> bool:
		value = _vs_check_container_value(_vs_element_type(self._vs_type_name), value, "array")
		try:
			super().remove(value)
			return True
		except ValueError:
			return False

	def clone(self) -> _VsArray:
		return _vs_clone(self)

	def __eq__(self, other: object) -> bool:
		return self is other

	def __ne__(self, other: object) -> bool:
		return self is not other


class _VsMap(dict):
	def __init__(self, *args: object, value_type: str = "", type_name: str = "map<?,?>") -> None:
		self._vs_value_type = value_type
		self._vs_type_name = type_name
		super().__init__()
		for key, value in dict(*args).items():
			self[key] = value

	def __getitem__(self, key: object) -> object:
		key = _vs_check_container_value(_vs_element_type(self._vs_type_name), key, "map key")
		return super().__getitem__(key)

	def __contains__(self, key: object) -> bool:
		key = _vs_check_container_value(_vs_element_type(self._vs_type_name), key, "map key")
		return super().__contains__(key)

	def __setitem__(self, key: object, value: object) -> None:
		args = _vs_type_args(self._vs_type_name)
		cast_key = _vs_container_value(args[0] if args else "", key, "map key")
		value_type = args[1] if len(args) > 1 else self._vs_value_type
		super().__setitem__(cast_key, _vs_container_value(value_type, value, "map"))

	def has(self, key: object) -> bool:
		return key in self

	def reserve(self, capacity: int) -> _VsMap:
		_vs_reserve_count(capacity)
		return self

	def clear(self) -> _VsMap:
		super().clear()
		return self

	def remove(self, key: object) -> bool:
		if key not in self:
			return False
		del self[key]
		return True

	def size(self) -> int:
		return len(self)

	def keys(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		key_type = args[0] if args else ""
		return _VsArray(
			(_vs_cast(key_type, key) if key_type else _vs_clone(key)
			 for key in super().keys()),
			type_name=f"array<{key_type or '?'}>")

	def values(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		value_type = args[1] if len(args) > 1 else self._vs_value_type
		return _VsArray(
			(_vs_cast(value_type, value) if value_type else _vs_clone(value)
			 for value in super().values()),
			type_name=f"array<{value_type or '?'}>")

	def clone(self) -> _VsMap:
		return _vs_clone(self)

	def entries(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		key_type = args[0] if args else ""
		value_type = args[1] if len(args) > 1 else self._vs_value_type
		pair_type = f"pair<{key_type or '?'},{value_type or '?'}>"
		return _VsArray(
			(_VsPair(
				_vs_cast(key_type, key) if key_type else _vs_clone(key),
				_vs_cast(value_type, value) if value_type else _vs_clone(value),
				type_name=pair_type)
			 for key, value in self.items()),
			type_name=f"array<{pair_type}>")

	def __eq__(self, other: object) -> bool:
		return self is other

	def __ne__(self, other: object) -> bool:
		return self is not other


class _VsSet(set):
	def __init__(self, values: object = (), type_name: str = "set<?>") -> None:
		self._vs_type_name = type_name
		super().__init__()
		for value in values:
			self.add(value)

	def __contains__(self, value: object) -> bool:
		value = _vs_check_container_value(_vs_element_type(self._vs_type_name), value, "set")
		return super().__contains__(value)

	def has(self, value: object) -> bool:
		return value in self

	def reserve(self, capacity: int) -> _VsSet:
		_vs_reserve_count(capacity)
		return self

	def add(self, value: object) -> _VsSet:
		args = _vs_type_args(self._vs_type_name)
		super().add(_vs_container_value(args[0] if args else "", value, "set"))
		return self

	def clear(self) -> _VsSet:
		super().clear()
		return self

	def remove(self, value: object) -> bool:
		if value not in self:
			return False
		super().remove(value)
		return True

	def values(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		element_type = args[0] if args else ""
		return _VsArray(
			(_vs_cast(element_type, value) if element_type else _vs_clone(value)
			 for value in self),
			type_name=f"array<{element_type or '?'}>")

	def size(self) -> int:
		return len(self)

	def clone(self) -> _VsSet:
		return _vs_clone(self)

	def __eq__(self, other: object) -> bool:
		return self is other

	def __ne__(self, other: object) -> bool:
		return self is not other


class _VsDeque(_vs_deque):
	def __init__(self, values: object = (), type_name: str = "deque<?>") -> None:
		self._vs_type_name = type_name
		super().__init__()
		for value in values:
			self.push_back(value)

	def __getitem__(self, index: object) -> object:
		checked = _vs_container_index(index, len(self), "deque")
		return super().__getitem__(checked)

	def __setitem__(self, index: object, value: object) -> None:
		checked = _vs_container_index(index, len(self), "deque")
		args = _vs_type_args(self._vs_type_name)
		super().__setitem__(checked, _vs_container_value(args[0] if args else "", value, "deque"))

	def push(self, value: object) -> _VsDeque:
		args = _vs_type_args(self._vs_type_name)
		self.append(_vs_container_value(args[0] if args else "", value, "deque"))
		return self

	def push_back(self, value: object) -> _VsDeque:
		args = _vs_type_args(self._vs_type_name)
		self.append(_vs_container_value(args[0] if args else "", value, "deque"))
		return self

	def push_front(self, value: object) -> _VsDeque:
		args = _vs_type_args(self._vs_type_name)
		self.appendleft(_vs_container_value(args[0] if args else "", value, "deque"))
		return self

	def pop_back(self) -> object:
		return self.pop()

	def pop_front(self) -> object:
		return self.popleft()

	def front(self) -> object:
		return super().__getitem__(0)

	def back(self) -> object:
		return super().__getitem__(-1)

	def size(self) -> int:
		return len(self)

	def clear(self) -> _VsDeque:
		super().clear()
		return self

	def values(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		element_type = args[0] if args else ""
		return _VsArray(
			(_vs_cast(element_type, value) if element_type else _vs_clone(value)
			 for value in self),
			type_name=f"array<{element_type or '?'}>")

	def __eq__(self, other: object) -> bool:
		return self is other

	def __ne__(self, other: object) -> bool:
		return self is not other


class _VsQueue(_VsDeque):
	def pop(self) -> object:
		return self.popleft()


class _VsStack(_VsArray):
	def top(self) -> object:
		return list.__getitem__(self, -1)

	def values(self) -> _VsArray:
		args = _vs_type_args(self._vs_type_name)
		element_type = args[0] if args else ""
		return _VsArray(
			(_vs_cast(element_type, value) if element_type else _vs_clone(value)
			 for value in self),
			type_name=f"array<{element_type or '?'}>")


)PY"
			       R"PY(
_VS_PAIR_EMPTY = object()


class _VsPair:
	def __init__(
			self,
			first: object = _VS_PAIR_EMPTY,
			second: object = _VS_PAIR_EMPTY,
			type_name: str = "pair<?,?>") -> None:
		self._vs_source_type_name = type_name
		self._vs_type_name = _vs_mangle_generic_type(type_name)
		args = _vs_type_args(type_name)
		self._vs_first_type = args[0] if len(args) > 0 else ""
		self._vs_second_type = args[1] if len(args) > 1 else ""
		self.first = (
			_vs_default(self._vs_first_type)
			if first is _VS_PAIR_EMPTY and self._vs_first_type
			else None if first is _VS_PAIR_EMPTY
			else first)
		self.second = (
			_vs_default(self._vs_second_type)
			if second is _VS_PAIR_EMPTY and self._vs_second_type
			else None if second is _VS_PAIR_EMPTY
			else second)

	@property
	def first(self) -> object:
		return self._first

	@first.setter
	def first(self, value: object) -> None:
		self._first = _vs_cast(self._vs_first_type, value) if self._vs_first_type else value

	@property
	def second(self) -> object:
		return self._second

	@second.setter
	def second(self, value: object) -> None:
		self._second = _vs_cast(self._vs_second_type, value) if self._vs_second_type else value

	@property
	def key(self) -> object:
		return self._first

	@key.setter
	def key(self, value: object) -> None:
		self.first = value

	@property
	def value(self) -> object:
		return self._second

	@value.setter
	def value(self, value: object) -> None:
		self.second = value

	def __iter__(self):
		yield self.first
		yield self.second

	def __eq__(self, other: object) -> bool:
		return (isinstance(other, _VsPair)
				and self._vs_type_name == other._vs_type_name
				and _vs_equal(self.first, other.first, self._vs_first_type, other._vs_first_type, True)
				and _vs_equal(self.second, other.second, self._vs_second_type, other._vs_second_type, True))

	def __ne__(self, other: object) -> bool:
		return not self == other


_VS_EMPTY = object()


class _VsOptional:
	def __init__(self, value: object = _VS_EMPTY, type_name: str = "optional<?>") -> None:
		self._vs_type_name = type_name
		args = _vs_type_args(type_name)
		self._value = (
			_vs_cast(args[0], value)
			if value is not _VS_EMPTY and args
			else value)

	def __deepcopy__(self, memo: dict[int, object]) -> _VsOptional:
		result = type(self).__new__(type(self))
		memo[id(self)] = result
		result._vs_type_name = self._vs_type_name
		# Empty state is an identity sentinel; active payloads still share the
		# worker snapshot's memo so aliases and cycles retain their graph shape.
		result._value = (_VS_EMPTY if self._value is _VS_EMPTY
				else _vs_copy.deepcopy(self._value, memo))
		return result

	def has_value(self) -> bool:
		return self._value is not _VS_EMPTY

	def get(self) -> object:
		if self._value is _VS_EMPTY:
			raise RuntimeError("optional has no value")
		return _vs_clone(self._value)

	def value_or(self, fallback: object) -> object:
		args = _vs_type_args(self._vs_type_name)
		value = fallback if self._value is _VS_EMPTY else self._value
		return _vs_cast(args[0], value) if args else _vs_clone(value)

	def reset(self) -> _VsOptional:
		self._value = _VS_EMPTY
		return self

	def __eq__(self, other: object) -> bool:
		if not isinstance(other, _VsOptional) or _vs_canonical_type(self._vs_type_name) != _vs_canonical_type(other._vs_type_name):
			return False
		if self.has_value() != other.has_value():
			return False
		if not self.has_value():
			return True
		return _vs_equal(self._value, other._value,
			_vs_element_type(self._vs_type_name), _vs_element_type(other._vs_type_name), True)

	def __ne__(self, other: object) -> bool:
		return not self == other


class _VsAttribute:
	def __init__(self, value=_VS_EMPTY, type_name="attribute<?>"):
		self._vs_type_name = _vs_resolve_type_name(type_name)
		self._config = None
		if value is not _VS_EMPTY:
			self.set(value)

	def set(self, value):
		value_type = _vs_type_args(self._vs_type_name)[0]
		next_config = ("value", _vs_cast(value_type, value))
		self._config = next_config

	def bind(self, getter):
		self._config = ("getter", getter)

	def bind_optional(self, getter):
		self._config = ("reader", getter)

	def has_same_configuration(self, other):
		return self._config is other._config

	def try_get(self):
		config = self._config
		value_type = _vs_type_args(self._vs_type_name)[0]
		result_type = f"optional<{value_type}>"
		if config is None:
			return _VsOptional(type_name=result_type)
		kind, payload = config
		del config
		if kind == "reader":
			return _vs_cast(result_type, payload())
		return _VsOptional(payload() if kind == "getter" else payload, type_name=result_type)

	def get(self):
		result = self.try_get()
		if not result.has_value():
			raise RuntimeError("std::attribute has no value")
		return result.get()

	def get_or(self, fallback):
		return self.try_get().value_or(fallback)

	def is_set(self):
		return self._config is not None

	def is_bound(self):
		return self._config is not None and self._config[0] != "value"

	def reset(self):
		self._config = None

	def __call__(self):
		return self.get()

	def __bool__(self):
		return bool(self.get())


def _vs_binding_target(target):
	if target is None:
		raise RuntimeError("null method binding target")
	return target


def _vs_bound_attribute(getter, type_name):
	result = _VsAttribute(type_name=type_name)
	result.bind(getter)
	return result


class _VsDelegate:
	_vs_type_name = "delegate"

	def __init__(self) -> None:
		self._handler = None
		self._bound = False

	def bind(self, handler: object) -> None:
		self._handler = handler
		self._bound = True

	def unbind(self) -> None:
		self._handler = None
		self._bound = False

	def is_bound(self) -> bool:
		return self._bound

	def execute(self, *args: object) -> object:
		if not self._bound:
			raise AssertionError("delegate is not bound")
		return self._handler(*args)

	def execute_if_bound(self, *args: object) -> bool:
		if not self._bound:
			return False
		self._handler(*args)
		return True


class _VsMulticastDelegate:
	_vs_type_name = "multicast_delegate"

	def __init__(self) -> None:
		self.handlers = _VsArray()

	def add(self, handler: object) -> None:
		self.handlers.push(handler)

	def __add__(self, handler: object) -> _VsMulticastDelegate:
		self.add(handler)
		return self

	def remove(self, handler: object) -> None:
		self.handlers.remove(handler)

	def __sub__(self, handler: object) -> _VsMulticastDelegate:
		self.remove(handler)
		return self

	def broadcast(self, *args: object) -> None:
		for handler in list(self.handlers):
			handler(*args)

	def count(self) -> int:
		return len(self.handlers)

	def clear(self) -> None:
		self.handlers.clear()


delegate = _VsDelegate
multicast_delegate = _VsMulticastDelegate


_VS_ALIASES: dict[str, str] = {}
_VS_VALUE_TYPES: set[str] = set()
_VS_USER_TYPES: dict[str, type] = {}
_VS_INTEGER_SPECS: dict[str, tuple[int, bool]] = {
	"char": (8, True), "int8": (8, True), "signed char": (8, True),
	"uint8": (8, False), "unsigned char": (8, False),
	"short": (16, True), "signed short": (16, True), "int16": (16, True),
	"uint16": (16, False), "unsigned short": (16, False),
	"int": (32, True), "signed": (32, True),
	"signed int": (32, True), "int32": (32, True),
	"unsigned": (32, False), "unsigned int": (32, False), "uint32": (32, False),
	"long": (64, True), "signed long": (64, True), "int64": (64, True),
	"unsigned long": (64, False), "uint64": (64, False),
}
_VS_INTEGER_RANGES: dict[str, tuple[int, int]] = {
	name: (
		(-(1 << (bits - 1)), (1 << (bits - 1)) - 1)
		if signed else (0, (1 << bits) - 1))
	for name, (bits, signed) in _VS_INTEGER_SPECS.items()
}


@_vs_functools.cache
def _vs_base_type(type_name: str) -> str:
	name = type_name.strip()
	while name in _VS_ALIASES:
		name = _VS_ALIASES[name]
	generic = name.find("<")
	if generic >= 0:
		name = name[:generic]
	while name.endswith("[]"):
		name = "array"
	return name.replace("const ", "").strip()


@_vs_functools.cache
def _vs_type_args(type_name: str) -> tuple[str, ...]:
	name = type_name.strip()
	seen: set[str] = set()
	while name in _VS_ALIASES and name not in seen:
		seen.add(name)
		name = _VS_ALIASES[name].strip()
	start = name.find("<")
	if start < 0 or not name.endswith(">"):
		return ()
	result: list[str] = []
	depth = 0
	current: list[str] = []
	for char in name[start + 1:-1]:
		if char == "<":
			depth += 1
		elif char == ">":
			depth -= 1
		if char == "," and depth == 0:
			result.append("".join(current).strip())
			current.clear()
		else:
			current.append(char)
	result.append("".join(current).strip())
	return tuple(result)


def _vs_mangle_generic_type(type_name: str) -> str:
	base = _vs_base_type(type_name)
	args = _vs_type_args(type_name)
	if not args:
		return base
	return base + "#" + "".join(
		f"{len(arg.encode('utf-8'))}:{arg}" for arg in args)


@_vs_functools.cache
def _vs_element_type(type_name: str) -> str:
	name = type_name.strip()
	while name in _VS_ALIASES:
		name = _VS_ALIASES[name].strip()
	if name.endswith("[]"):
		return name[:-2].strip()
	args = _vs_type_args(name)
	return args[0] if args else ""


def _vs_canonical_type(type_name: str) -> str:
	name = _vs_resolve_type_name(type_name).strip()
	seen: set[str] = set()
	while name in _VS_ALIASES and name not in seen:
		seen.add(name)
		name = _VS_ALIASES[name].strip()
	if name.endswith("[]"):
		return f"array<{_vs_canonical_type(name[:-2])}>"
	args = _vs_type_args(name)
	base = _vs_base_type(name)
	if not args:
		return base
	return f"{base}<{','.join(_vs_canonical_type(arg) for arg in args)}>"


def _vs_is_builtin_pair(type_name: str) -> bool:
	return _vs_base_type(type_name) == "pair" and len(_vs_type_args(type_name)) == 2


def _vs_default(type_name: str) -> object:
	type_name = _vs_resolve_type_name(type_name)
	base = _vs_base_type(type_name)
	if base in {"bool"}:
		return False
	if base in {"float", "double"}:
		return 0.0
	if base in {"string", "wstring"}:
		return ""
	if base in {"array"}:
		return _VsArray(type_name=type_name)
	if base in {"map", "ordered_map"}:
		args = _vs_type_args(type_name)
		return _VsMap(
			value_type=args[1] if len(args) > 1 else "",
			type_name=type_name)
	if base == "set":
		return _VsSet(type_name=type_name)
	if base == "deque":
		return _VsDeque(type_name=type_name)
	if base == "queue":
		return _VsQueue(type_name=type_name)
	if base == "stack":
		return _VsStack(type_name=type_name)
	if _vs_is_builtin_pair(type_name):
		return _VsPair(type_name=type_name)
	if base == "attribute" and _vs_lookup_type(base) is _VsAttribute:
		return _VsAttribute(type_name=type_name)
	if base == "optional":
		return _VsOptional(type_name=type_name)
	if base == "channel":
		return _VsChannel(type_name=type_name)
	if base in _VS_INTEGER_SPECS:
		return 0
	cls = _vs_lookup_type(base)
	if isinstance(cls, type) and issubclass(cls, _vs_enum.IntEnum):
		return cls(0)
	return cls() if base in _VS_VALUE_TYPES and isinstance(cls, type) else None


def _vs_name(name: str) -> str:
	result = []
	for byte in name.encode("utf-8"):
		char = chr(byte)
		if (65 <= byte <= 90) or (97 <= byte <= 122) or (48 <= byte <= 57) or byte == 95:
			result.append(char)
		elif byte in {46, 58}:
			result.append("__")
		else:
			result.append(f"_u{byte:02x}")
	value = "".join(result) or "_"
	if value[0].isdigit():
		value = "_" + value
	return value


def _vs_clone_struct(value: object, cls: type) -> object:
	result = cls.__new__(cls)
	for name, type_name in cls._vs_field_types.items():
		setattr(result, name, _vs_cast(type_name, getattr(value, name)))
	return result


def _vs_clone(value: object) -> object:
	if isinstance(value, _VsAttribute):
		result = _VsAttribute(type_name=value._vs_type_name)
		result._config = value._config
		return result
	if isinstance(value, _VsChannel):
		# Channel identity is the explicit synchronized cross-worker capability.
		return value
	if isinstance(value, _VsOptional):
		if not value.has_value():
			return _VsOptional(type_name=value._vs_type_name)
		return _VsOptional(_vs_clone(value.get()), type_name=value._vs_type_name)
	if isinstance(value, _VsMap):
		result = _VsMap(value_type=value._vs_value_type, type_name=value._vs_type_name)
		for key, item in value.items():
			result[_vs_clone(key)] = _vs_clone(item)
		return result
	if isinstance(value, _VsSet):
		return _VsSet((_vs_clone(item) for item in value), type_name=value._vs_type_name)
	if isinstance(value, _VsQueue):
		return _VsQueue((_vs_clone(item) for item in value), type_name=value._vs_type_name)
	if isinstance(value, _VsStack):
		return _VsStack((_vs_clone(item) for item in value), type_name=value._vs_type_name)
	if isinstance(value, _VsDeque):
		return _VsDeque((_vs_clone(item) for item in value), type_name=value._vs_type_name)
	if isinstance(value, _VsArray):
		return _VsArray((_vs_clone(item) for item in value), type_name=value._vs_type_name)
	if isinstance(value, _VsPair):
		return _VsPair(
			_vs_clone(value.first),
			_vs_clone(value.second),
			type_name=value._vs_source_type_name)
	field_types = getattr(type(value), "_vs_field_types", None)
	if getattr(type(value), "_vs_value_type", False) and isinstance(field_types, dict):
		return _vs_clone_struct(value, type(value))
	return value


def _vs_register_type(name: str, cls: type) -> None:
	_VS_USER_TYPES[_vs_base_type(name)] = cls


def _vs_lookup_type(name: str) -> object:
	base = _vs_base_type(name)
	return _VS_USER_TYPES.get(base, globals().get(_vs_name(base)))


def _vs_worker_clone(value: object) -> object:
	"""Clone isolated VM state while preserving channel capability identity."""
	return _vs_copy.deepcopy(value)


)PY"
			       R"PY(
def _vs_cast(type_name: str, value: object) -> object:
	type_name = _vs_resolve_type_name(type_name)
	# Core copies a reserved integer-zero slot unchanged between equal types.
	if isinstance(value, _VsReservedZero) and value._vs_matches_type(type_name):
		return value
	base = _vs_base_type(type_name)
	if base == "attribute" and _vs_lookup_type(base) is _VsAttribute:
		if isinstance(value, _VsAttribute):
			return _vs_clone(value)
		return _VsAttribute(value, type_name=type_name)
	if isinstance(value, _VsAttribute):
		value = value.get()
	if value is None:
		return None
	if base == "bool":
		return bool(value)
	if base == "float":
		return _VsFloat32(value)
	if base == "double":
		return float(value)
	if base in {"string", "wstring"}:
		return _vs_text(value)
	if base == "channel":
		if not isinstance(value, _VsChannel):
			raise TypeError(f"expected channel value for {type_name}")
		return value
	if base in {"array"}:
		if isinstance(value, _VsArray):
			return value
		element_type = _vs_element_type(type_name)
		return _VsArray(
			(_vs_cast(element_type, item) if element_type else _vs_clone(item)
			 for item in value),
			type_name=type_name)
	if base in {"map", "ordered_map"}:
		if isinstance(value, _VsMap):
			return value
		args = _vs_type_args(type_name)
		result = _VsMap(
			value_type=args[1] if len(args) > 1 else "",
			type_name=type_name)
		for key, item in value.items():
			result[key] = item
		return result
	if base == "set":
		if isinstance(value, _VsSet):
			return value
		args = _vs_type_args(type_name)
		result = _VsSet(type_name=type_name)
		for item in value:
			result.add(_vs_cast(args[0], item) if args else _vs_clone(item))
		return result
	if base in {"deque", "queue", "stack"}:
		cls = {"deque": _VsDeque, "queue": _VsQueue, "stack": _VsStack}[base]
		if isinstance(value, cls):
			return value
		result = cls(type_name=type_name)
		args = _vs_type_args(type_name)
		for item in value:
			result.append(_vs_cast(args[0], item) if args else _vs_clone(item))
		return result
	if base == "optional":
		if not isinstance(value, _VsOptional):
			raise TypeError(f"expected optional value for {type_name}")
		args = _vs_type_args(type_name)
		if not value.has_value():
			return _VsOptional(type_name=type_name)
		inner = _vs_cast(args[0], value.get()) if args else _vs_clone(value.get())
		return _VsOptional(inner, type_name=type_name)
	if base in _VS_INTEGER_SPECS:
		number = int(value)
		low, high = _VS_INTEGER_RANGES[base]
		if number < low or number > high:
			raise OverflowError(f"VeroScript {base} overflow: {number}")
		return number
	cls = _vs_lookup_type(base)
	if isinstance(cls, type) and issubclass(cls, _vs_enum.IntEnum):
		underlying = getattr(cls, "_vs_underlying_type", "int")
		return cls(_vs_cast(underlying, value))
	if _vs_is_builtin_pair(type_name):
		if not isinstance(value, _VsPair):
			raise TypeError(f"expected pair value for {type_name}")
		args = _vs_type_args(type_name)
		first = _vs_cast(args[0], value.first) if len(args) > 0 else _vs_clone(value.first)
		second = _vs_cast(args[1], value.second) if len(args) > 1 else _vs_clone(value.second)
		return _VsPair(first, second, type_name=type_name)
	if base in _VS_VALUE_TYPES:
		if not isinstance(value, cls):
			raise TypeError(f"cannot cast {type(value).__name__} to {base}")
		return _vs_clone_struct(value, cls)
	return value


def _vs_integer_spec(type_name: str) -> tuple[int, bool] | None:
	type_name = _vs_resolve_type_name(type_name)
	base = _vs_base_type(type_name)
	return _VS_INTEGER_SPECS.get(base)


def _vs_wrap_integer(type_name: str, value: object) -> int:
	spec = _vs_integer_spec(type_name)
	if spec is None:
		return int(value)
	bits, signed = spec
	modulus = 1 << bits
	number = int(value) % modulus
	if signed and number >= (1 << (bits - 1)):
		number -= modulus
	return number


def _vs_unary(type_name: str, operation: str, value: object) -> object:
	spec = _vs_integer_spec(type_name)
	if spec is None:
		result = -value if operation == "neg" else ~int(value)
		return _vs_cast(type_name, result)
	_, signed = spec
	number = int(value)
	if operation == "bitnot":
		return _vs_wrap_integer(type_name, ~number)
	result = -number
	return _vs_cast(type_name, result) if signed else _vs_wrap_integer(type_name, result)


def _vs_binary(type_name: str, operation: str, left: object, right: object) -> object:
	if operation == "add" and (isinstance(left, str) or isinstance(right, str)):
		wide = _vs_base_type(_vs_resolve_type_name(type_name)) == "wstring"
		return _vs_add(left, right, wide)
	spec = _vs_integer_spec(type_name)
	if operation == "div":
		result = _vs_div(left, right)
	elif operation == "mod":
		result = _vs_mod(left, right)
	elif operation == "add":
		result = left + right
	elif operation == "sub":
		result = left - right
	elif operation == "mul":
		result = left * right
	elif operation == "and":
		result = int(left) & int(right)
	elif operation == "or":
		result = int(left) | int(right)
	elif operation == "xor":
		result = int(left) ^ int(right)
	elif operation in {"shl", "shr"}:
		count = int(right)
		bits = spec[0] if spec is not None else 32
		if count < 0 or count >= bits:
			raise ValueError("shift count out of range")
		if operation == "shl":
			result = int(left) << count
			return _vs_wrap_integer(type_name, result)
		result = int(left) >> count
	else:
		raise RuntimeError(f"unsupported numeric operation: {operation}")
	if spec is None:
		return _vs_cast(type_name, result)
	_, signed = spec
	return _vs_cast(type_name, result) if signed else _vs_wrap_integer(type_name, result)


def _vs_checked_cast(type_name: str, value: object) -> object:
	type_name = _vs_resolve_type_name(type_name)
	base = _vs_base_type(type_name)
	cls = _vs_lookup_type(base)
	if isinstance(value, _VsReservedZero) and value._vs_matches_type(type_name):
		if (isinstance(cls, type) and issubclass(cls, _vs_enum.IntEnum)
				and getattr(cls, "_vs_scoped_enum", True)):
			value = int(value)
		else:
			return value
	if value is None:
		if (isinstance(cls, type)
				and base not in _VS_VALUE_TYPES
				and not issubclass(cls, _vs_enum.IntEnum)):
			return None
		raise TypeError(f"cannot cast nullptr to {base}")
	if isinstance(cls, type) and issubclass(cls, _vs_enum.IntEnum):
		if isinstance(value, _vs_enum.IntEnum) and not isinstance(value, cls):
			raise TypeError(f"cannot cast {type(value).__name__} to {base}")
	elif isinstance(cls, type):
		if not isinstance(value, cls):
			raise TypeError(f"cannot cast {type(value).__name__} to {base}")
	return _vs_cast(type_name, value)


def _vs_static_call(_receiver: object, function: object, *args: object) -> object:
	return function(*args)


def _vs_try_cast(type_name: str, value: object) -> _VsOptional:
	type_name = _vs_resolve_type_name(type_name)
	try:
		return _VsOptional(
			_vs_checked_cast(type_name, value),
			type_name=f"optional<{type_name}>")
	except (OverflowError, TypeError, ValueError):
		return _VsOptional(type_name=f"optional<{type_name}>")


def _vs_is_type(type_name: str, value: object) -> bool:
	type_name = _vs_resolve_type_name(type_name)
	if isinstance(value, _VsReservedZero):
		value = int(value)
	base = _vs_base_type(type_name)
	if base == "bool":
		return type(value) is bool
	if _vs_integer_spec(base) is not None:
		return type(value) is int
	if base == "float":
		return isinstance(value, _VsFloat32)
	if base == "double":
		return type(value) is float
	if base in {"string", "wstring"}:
		return type(value) is str
	if _vs_is_builtin_pair(type_name):
		return isinstance(value, _VsPair)
	cls = _vs_lookup_type(base)
	return isinstance(cls, type) and isinstance(value, cls)


def _vs_text(value: object) -> str:
	if value is None:
		return "nullptr"
	if value is True:
		return "true"
	if value is False:
		return "false"
	if isinstance(value, _vs_enum.IntEnum):
		name = getattr(type(value), '_vs_type_name', type(value).__name__)
		return f"{name.split(chr(31), 1)[-1]}({int(value)})"
	if isinstance(value, (_VsArray, _VsMap, _VsSet, _VsDeque)):
		return f"{value._vs_type_name}[{len(value)}]"
	if isinstance(value, _VsPair):
		return value._vs_type_name
	if isinstance(value, _VsFloat32):
		if _vs_math.isnan(value):
			if _vs_sys.platform in {"darwin", "ios"}:
				return "nan"
			negative = _vs_math.copysign(1.0, value) < 0.0
			return "-nan(ind)" if negative and _vs_os.name == "nt" else "-nan" if negative else "nan"
		return format(value, ".9g")
	if isinstance(value, float):
		if _vs_math.isnan(value):
			if _vs_sys.platform in {"darwin", "ios"}:
				return "nan"
			negative = _vs_math.copysign(1.0, value) < 0.0
			return "-nan(ind)" if negative and _vs_os.name == "nt" else "-nan" if negative else "nan"
		return format(value, ".15g")
	return str(value)


def _vs_math_unary(operation: str, value: object) -> float:
	number = float(value)
	if operation in {"floor", "ceil", "trunc"} and not _vs_math.isfinite(number):
		return number
	try:
		result = float(getattr(_vs_math, operation)(number))
		if operation in {"floor", "ceil", "trunc"} and result == 0.0:
			return _vs_math.copysign(0.0, number)
		return result
	except OverflowError:
		return float("inf")
	except ValueError:
		if operation in {"log", "log2"} and number == 0.0:
			return -float("inf")
		return -float("nan")


def _vs_math_binary(operation: str, left: object, right: object) -> float:
	left, right = float(left), float(right)
	try:
		return float(getattr(_vs_math, operation)(left, right))
	except OverflowError:
		negative = left < 0.0 and _vs_math.isfinite(right) and right.is_integer() and abs(right) % 2.0 == 1.0
		return -float("inf") if negative else float("inf")
	except ValueError:
		if operation == "pow" and left == 0.0 and right < 0.0:
			negative = _vs_math.copysign(1.0, left) < 0.0 and _vs_math.isfinite(right) and right.is_integer() and abs(right) % 2.0 == 1.0
			return -float("inf") if negative else float("inf")
		return -float("nan")


def _vs_round(value: object) -> float:
	number = float(value)
	if not _vs_math.isfinite(number):
		return number
	if number == 0.0:
		return number
	# Adding 0.5 can itself round at half/integer boundaries. Split first;
	# modf also preserves the sign when a negative number rounds to zero.
	fraction, integral = _vs_math.modf(number)
	if abs(fraction) >= 0.5:
		return integral + _vs_math.copysign(1.0, number)
	return integral


def _vs_min(left: object, right: object) -> object:
	return left if left < right else right


def _vs_max(left: object, right: object) -> object:
	return left if left > right else right


)PY"
			       R"PY(
def _vs_checked_length(length: int) -> int:
	if length < 0 or length > 9223372036854775807:
		raise OverflowError("len result exceeds int64 range")
	return length


def _vs_len(value: object) -> int:
	if isinstance(value, str):
		return _vs_checked_length(len(value.encode("utf-8", errors="surrogateescape")))
	return _vs_checked_length(len(value))


def _vs_wlen(value: object) -> int:
	return _vs_checked_length(len(str(value).encode("utf-16-le")) // 2)


def _vs_range(count: object) -> _VsArray:
	count = _vs_cast("int64", count)
	if count > 1000000:
		raise ValueError("range count too large (max 1000000)")
	return _VsArray(range(count), type_name="array<int>")


def _vs_substr(value: object, start: object, count: object) -> str:
	data = str(value).encode("utf-8", errors="surrogateescape")
	begin = _vs_reserve_count(_vs_cast("int64", start))
	length = _vs_cast("int64", count)
	if begin < 0 or length < 0 or begin > len(data):
		raise IndexError("substr start/count out of range")
	return data[begin:begin + length].decode("utf-8", errors="surrogateescape")



def _vs_numeric_type(name):
	name=_vs_canonical_type(_vs_resolve_type_name(name))
	if _vs_integer_spec(name) is None and name not in ("float","double"):
		raise TypeError("numeric type required")

def _vs_random_next(e):
	if not e:
		e.push(0x6d2b79f5)
	x=e[0]
	if not x:
		raise ValueError("random engine state must be nonzero")
	x^=(x<<13)&0xffffffff; x^=x>>17; x^=(x<<5)&0xffffffff
	e[0]=x
	return x

def _vs_random_index(e,n):
	if n<=0:
		raise ValueError("random bound must be positive")
	b=4294967295; wide=n>2147483647; size=b*b if wide else b; limit=size-size%n
	while True:
		x=_vs_random_next(e)-1
		if wide:
			x=x*b+_vs_random_next(e)-1
		if x<limit:
			return x%n

def _vs_array_reverse(a):
	list.reverse(a)

def _vs_array_rotate(a,m):
	if m<0 or m>len(a):
		raise IndexError("rotate middle out of range")
	# Bypass scalar conversion and cloning while moving existing values.
	list.__setitem__(a,slice(None),list.__getitem__(a,slice(m,None))+list.__getitem__(a,slice(None,m)))

def _vs_array_shuffle(a,e):
	if a is e:
		raise ValueError("shuffle target aliases engine")
	for n in range(len(a),1,-1):
		j=_vs_random_index(e,n)
		x=list.__getitem__(a,n-1); y=list.__getitem__(a,j)
		list.__setitem__(a,n-1,y); list.__setitem__(a,j,x)

def _vs_foundation_bytes(s):
	return str(s).encode("utf-8",errors="surrogateescape")
def _vs_foundation_text(s):
	return s.decode("utf-8",errors="surrogateescape")
def _vs_string_starts(s,p):
	return _vs_foundation_bytes(s).startswith(_vs_foundation_bytes(p))
def _vs_string_ends(s,p):
	return _vs_foundation_bytes(s).endswith(_vs_foundation_bytes(p))
def _vs_string_last_find(s,p):
	return _vs_foundation_bytes(s).rfind(_vs_foundation_bytes(p))
def _vs_string_repeat(s,n):
	if n<0:
		raise ValueError("repeat count must be non-negative")
	data=_vs_foundation_bytes(s)
	if len(data)*n>9223372036854775807:
		raise OverflowError("repeat size overflow")
	return _vs_foundation_text(data*n) if data else ""
def _vs_string_replace(s,p,r):
	return _vs_foundation_text(_vs_foundation_bytes(s).replace(_vs_foundation_bytes(p),_vs_foundation_bytes(r))) if _vs_foundation_bytes(p) else str(s)
def _vs_string_trim(s,left,right):
	b=_vs_foundation_bytes(s); space=b" \t\n\r\v\f"
	if left:
		b=b.lstrip(space)
	if right:
		b=b.rstrip(space)
	return _vs_foundation_text(b)

def _vs_find(haystack: object, needle: object) -> int:
	return str(haystack).encode("utf-8", errors="surrogateescape").find(
		str(needle).encode("utf-8", errors="surrogateescape"))


def _vs_char_code(value: object, index: object) -> int:
	data = str(value).encode("utf-8", errors="surrogateescape")
	position = _vs_reserve_count(_vs_cast("int64", index))
	if position < 0 or position >= len(data):
		raise IndexError("char index out of range")
	return data[position]


def _vs_split(value: object, separator: object) -> _VsArray:
	delimiter = _vs_foundation_bytes(separator)
	if not delimiter:
		raise ValueError("split separator must not be empty")
	parts = _vs_foundation_bytes(value).split(delimiter)
	return _VsArray((_vs_foundation_text(part) for part in parts), type_name="array<string>")


def _vs_join(parts: object, separator: object) -> str:
	joined = str(separator).join(str(part) for part in parts)
	# Byte fragments can complete a UTF-8 sequence across a join boundary.
	return joined if joined.isascii() else _vs_foundation_text(_vs_foundation_bytes(joined))


def _vs_ascii_case(value: object, upper: bool) -> str:
	data = bytearray(str(value).encode("utf-8", errors="surrogateescape"))
	for index, byte in enumerate(data):
		if upper and 97 <= byte <= 122:
			data[index] = byte - 32
		elif not upper and 65 <= byte <= 90:
			data[index] = byte + 32
	return bytes(data).decode("utf-8", errors="surrogateescape")


def _vs_add(left: object, right: object, wide: bool = False) -> object:
	if isinstance(left, str) or isinstance(right, str):
		left_text, right_text = _vs_text(left), _vs_text(right)
		joined = left_text + right_text
		if wide or joined.isascii():
			return joined
		return _vs_foundation_text(_vs_foundation_bytes(joined))
	return left + right


def _vs_compare_text(left: str | _VsReservedZero, right: str | _VsReservedZero, operation: str, wide: bool = False) -> bool:
	# Core string equality compares bytes; wstring ordering compares UTF-16
	# code units, whose order differs from supplementary Unicode scalar values.
	# ASCII has the same order in all three representations and needs no copy.
	if isinstance(left, _VsReservedZero) or isinstance(right, _VsReservedZero):
		if not (isinstance(left, _VsReservedZero) and isinstance(right, _VsReservedZero)):
			raise TypeError("cannot compare strings with different runtime types")
		# Both slots still contain the same actual int32 zero, not text values.
		if operation in ("==", "<=", ">="):
			return True
		if operation in ("!=", "<", ">"):
			return False
		raise ValueError("unsupported text comparison")
	if left is right and operation in ("==", "!=", "<", "<=", ">", ">="):
		return operation in ("==", "<=", ">=")
	if not (left.isascii() and right.isascii()):
		if wide:
			left = left.encode("utf-16-be", errors="surrogatepass")
			right = right.encode("utf-16-be", errors="surrogatepass")
		else:
			left = _vs_foundation_bytes(left)
			right = _vs_foundation_bytes(right)
	if operation == "==":
		return left == right
	if operation == "!=":
		return left != right
	if operation == "<":
		return left < right
	if operation == "<=":
		return left <= right
	if operation == ">":
		return left > right
	if operation == ">=":
		return left >= right
	raise ValueError("unsupported text comparison")


def _vs_less_method(receiver_type: str, argument_type: str):
	cls = _vs_lookup_type(_vs_resolve_type_name(receiver_type))
	if not isinstance(cls, type):
		return None
	argument_type = _vs_canonical_type(argument_type)
	argument_cls = _vs_lookup_type(argument_type)
	if (isinstance(argument_cls, type) and issubclass(argument_cls, _vs_enum.IntEnum)
			and not getattr(argument_cls, "_vs_scoped_enum", True)):
		argument_type = _vs_canonical_type(argument_cls._vs_underlying_type)
	for owner in cls.__mro__:
		method = owner.__dict__.get("__lt__")
		parameter_type = getattr(method, "_vs_parameter_type", None)
		if parameter_type is None:
			continue
		parameter_type = _vs_canonical_type(parameter_type)
		parameter_match_type = parameter_type
		parameter_cls = _vs_lookup_type(parameter_type)
		if (isinstance(parameter_cls, type) and issubclass(parameter_cls, _vs_enum.IntEnum)
				and not getattr(parameter_cls, "_vs_scoped_enum", True)):
			parameter_match_type = _vs_canonical_type(parameter_cls._vs_underlying_type)
		matches = parameter_match_type == argument_type
		if _vs_integer_spec(parameter_match_type) is not None or parameter_match_type in {"float", "double"}:
			matches = matches or (_vs_integer_spec(argument_type) is not None
					or argument_type in {"bool", "float", "double"})
		if isinstance(parameter_cls, type):
			matches = matches or (isinstance(argument_cls, type)
					and issubclass(argument_cls, parameter_cls))
			matches = matches or (argument_type == "nullptr"
					and not getattr(parameter_cls, "_vs_value_type", False)
					and not issubclass(parameter_cls, _vs_enum.IntEnum))
		if matches:
			virtual = method._vs_virtual
			for base in owner.__mro__[1:]:
				inherited = base.__dict__.get("__lt__")
				if (_vs_canonical_type(getattr(inherited, "_vs_parameter_type", ""))
						== _vs_canonical_type(method._vs_parameter_type)):
					virtual = virtual or inherited._vs_virtual
			return cls, method, parameter_type, virtual
	return None


def _vs_call_less(resolved, receiver: object, argument: object) -> bool:
	cls, method, _, virtual = resolved
	if not isinstance(receiver, cls):
		raise TypeError("bound method call requires object")
	if virtual:
		for owner in type(receiver).__mro__:
			candidate = owner.__dict__.get("__lt__")
			if (_vs_canonical_type(getattr(candidate, "_vs_parameter_type", ""))
					== _vs_canonical_type(method._vs_parameter_type)):
				method = candidate
				break
	return method(receiver, argument)


def _vs_ordered_left(left: object, operation: str, left_type: str, right_type: str):
	# Core tries the left same-name member, then the right same-name member,
	# before a mirrored relation. Python's reflected/subclass priority differs.
	if operation == "<":
		resolved = _vs_less_method(left_type, right_type)
		if resolved is not None:
			return left, resolved, False
	if operation in {"<", ">"}:
		resolved = _vs_less_method(right_type, left_type)
		if resolved is not None:
			# Capture a value argument before evaluating the right receiver.
			return _vs_cast(resolved[2], left), resolved, True
	return left, None, False


def _vs_ordered_compare(prepared_left, right: object, operation: str, left_type: str, right_type: str) -> bool:
	left, resolved, right_receiver = prepared_left
	if resolved is not None:
		if right_receiver:
			return _vs_call_less(resolved, right, left)
		return _vs_call_less(resolved, left, _vs_cast(resolved[2], right))
	if left_type and right_type:
		left_base = _vs_base_type(_vs_resolve_type_name(left_type))
		right_base = _vs_base_type(_vs_resolve_type_name(right_type))
		if left_base == right_base == "wstring":
			return _vs_compare_text(left, right, operation, True)
	# Python collections expose lexicographic/subset ordering; Core container
	# values do not. A script record on either side retains operator dispatch.
	containers = (_VsArray, _VsMap, _VsSet, _VsDeque)
	if isinstance(left, containers) and isinstance(right, containers):
		raise TypeError("comparison operands must be numeric")
	# bool is an integer subclass in Python, but is not a Core numeric value.
	# Leave script operator overloads on record operands on their existing path.
	if (isinstance(left, (bool, int, float)) and isinstance(right, (bool, int, float))
			and (isinstance(left, bool) or isinstance(right, bool))):
		raise TypeError("comparison operands must be numeric")
	if operation == "<":
		return left < right
	if operation == "<=":
		return left <= right
	if operation == ">":
		return left > right
	if operation == ">=":
		return left >= right
	raise ValueError("unsupported ordered comparison")


def _vs_div(left: object, right: object) -> object:
	if isinstance(left, int) and isinstance(right, int):
		if right == 0:
			raise ZeroDivisionError("VeroScript integer division by zero")
		quotient = abs(left) // abs(right)
		return -quotient if (left < 0) != (right < 0) else quotient
	return left / right


def _vs_mod(left: object, right: object) -> object:
	if isinstance(left, int) and isinstance(right, int):
		return left - _vs_div(left, right) * right
	raise TypeError("modulo is only defined for integers")


def _vs_live_length_snapshot(values: object):
	length = len(values)
	for index in range(length):
		yield values[index]


_VS_MAX_FORMAT_WIDTH = 1024


def _vs_byte_size(value: str) -> int:
	return len(value.encode("utf-8", errors="surrogateescape"))


def _vs_truncate_bytes(value: str, count: int) -> str:
	return value.encode("utf-8", errors="surrogateescape")[:count].decode(
		"utf-8", errors="surrogateescape")


def _vs_parse_format_uint(text: str, cursor: int) -> tuple[int, int]:
	if cursor >= len(text) or not ("0" <= text[cursor] <= "9"):
		raise ValueError("format: expected digits")
	value = 0
	while cursor < len(text) and "0" <= text[cursor] <= "9":
		value = value * 10 + ord(text[cursor]) - ord("0")
		if value > _VS_MAX_FORMAT_WIDTH:
			raise ValueError("format: width/precision out of range")
		cursor += 1
	return value, cursor


def _vs_parse_signed_text(value: str, bits: int) -> int:
	body = value
	negative = False
	if body.startswith(("+", "-")):
		negative = body[0] == "-"
		body = body[1:]
	if not body or any(char < "0" or char > "9" for char in body):
		raise ValueError("integer conversion requires an integer value")
	number = int(body)
	if negative:
		number = -number
	low = -(1 << (bits - 1))
	high = (1 << (bits - 1)) - 1
	if number < low or number > high:
		raise ValueError("integer conversion is out of range")
	return number


def _vs_parse_unsigned_text(value: str, bits: int) -> int:
	body = value[1:] if value.startswith("+") else value
	if (not body or value.startswith("-")
			or any(char < "0" or char > "9" for char in body)):
		raise ValueError("unsigned conversion requires a non-negative integer")
	number = int(body)
	if number >= (1 << bits):
		raise ValueError("unsigned conversion is out of range")
	return number


def _vs_parse_float_text(value: str) -> float:
	body = value[1:] if value.startswith(("+", "-")) else value
	exponent = body.find("e")
	if exponent < 0:
		exponent = body.find("E")
	mantissa = body if exponent < 0 else body[:exponent]
	exponent_text = "" if exponent < 0 else body[exponent + 1:]
	if exponent >= 0:
		if exponent_text.startswith(("+", "-")):
			exponent_text = exponent_text[1:]
		if (not exponent_text
				or any(char < "0" or char > "9" for char in exponent_text)):
			raise ValueError("floating conversion requires a finite numeric value")
	if mantissa.count(".") > 1:
		raise ValueError("floating conversion requires a finite numeric value")
	digits = mantissa.replace(".", "")
	if not digits or any(char < "0" or char > "9" for char in digits):
		raise ValueError("floating conversion requires a finite numeric value")
	number = float(value)
	if not _vs_math.isfinite(number) or (number == 0.0 and any(char != "0" for char in digits)):
		raise ValueError("floating conversion requires a finite numeric value")
	return number


def _vs_apply_width(
		value: str,
		width: int,
		align: str = ">",
		zero: bool = False,
		prefix_length: int = 0) -> str:
	size = _vs_byte_size(value)
	if width <= size:
		return value
	padding = width - size
	if align == "<":
		return value + " " * padding
	if align == "^":
		left = padding // 2
		return " " * left + value + " " * (padding - left)
	if zero:
		return value[:prefix_length] + "0" * padding + value[prefix_length:]
	return " " * padding + value


def _vs_format(pattern: object, *args: object) -> str:
	if len(args) == 1 and isinstance(args[0], (list, tuple)):
		args = tuple(args[0])
	text = str(pattern)
	result: list[str] = []
	cursor = 0
	automatic_index = 0
	while cursor < len(text):
		if text[cursor] == "{":
			if cursor + 1 >= len(text):
				raise ValueError("format: trailing '{'")
			if text[cursor + 1] == "{":
				result.append("{")
				cursor += 2
				continue
			end = cursor + 1
			argument_index = automatic_index
			explicit_index = False
			if end < len(text) and "0" <= text[end] <= "9":
				argument_index, end = _vs_parse_format_uint(text, end)
				explicit_index = True
			align = ">"
			width = 0
			precision: int | None = None
			type_code = ""
			if end < len(text) and text[end] == ":":
				end += 1
				if end < len(text) and text[end] in "<>^":
					align = text[end]
					end += 1
				if end < len(text) and "0" <= text[end] <= "9":
					width, end = _vs_parse_format_uint(text, end)
				if end < len(text) and text[end] == ".":
					precision, end = _vs_parse_format_uint(text, end + 1)
				if end < len(text) and text[end] in "xX":
					type_code = text[end]
					end += 1
			if end >= len(text) or text[end] != "}":
				raise ValueError("format: invalid placeholder")
			if argument_index >= len(args):
				raise ValueError("format: not enough arguments")
			value = _vs_text(args[argument_index])
			if type_code:
				integer = (_vs_parse_signed_text(value, 64) if value.startswith("-")
						   else _vs_parse_unsigned_text(value, 64)) & 0xffffffffffffffff
				value = format(integer, type_code)
			elif precision is not None:
				value = format(_vs_parse_float_text(value), f".{precision}f")
			result.append(_vs_apply_width(value, width, align))
			if not explicit_index:
				automatic_index += 1
			cursor = end + 1
			continue
		if text[cursor] == "}":
			if cursor + 1 < len(text) and text[cursor + 1] == "}":
				result.append("}")
				cursor += 2
				continue
			raise ValueError("format: unmatched '}'")
		result.append(text[cursor])
		cursor += 1
	return _vs_join(result, "")


)PY"
			       R"PY(
def _vs_printf(pattern: object, *args: object) -> str:
	if len(args) == 1 and isinstance(args[0], (list, tuple)):
		args = tuple(args[0])
	text = str(pattern)
	result: list[str] = []
	cursor = 0
	argument_index = 0
	while cursor < len(text):
		if text[cursor] != "%":
			result.append(text[cursor])
			cursor += 1
			continue
		if cursor + 1 >= len(text):
			raise ValueError("formatf: trailing '%'")
		if text[cursor + 1] == "%":
			result.append("%")
			cursor += 2
			continue
		end = cursor + 1
		left = plus = space = alternate = zero = False
		while end < len(text) and text[end] in "-+ #0":
			flag = text[end]
			left = left or flag == "-"
			plus = plus or flag == "+"
			space = space or flag == " "
			alternate = alternate or flag == "#"
			zero = zero or flag == "0"
			end += 1
		width = 0
		if end < len(text) and text[end] == "*":
			raise ValueError("formatf: dynamic width '*' is not supported")
		if end < len(text) and "0" <= text[end] <= "9":
			width, end = _vs_parse_format_uint(text, end)
		precision: int | None = None
		if end < len(text) and text[end] == ".":
			if end + 1 < len(text) and text[end + 1] == "*":
				raise ValueError("formatf: dynamic precision '*' is not supported")
			precision, end = _vs_parse_format_uint(text, end + 1)
		if end < len(text) and text[end] in "hl":
			modifier = text[end]
			end += 1
			if end < len(text) and text[end] == modifier:
				end += 1
		elif end < len(text) and text[end] in "jzt":
			end += 1
		if end >= len(text):
			raise ValueError("formatf: incomplete conversion")
		conversion = text[end]
		end += 1
		if conversion not in "diuoxXfFeEgGcs":
			raise ValueError(f"formatf: unsupported conversion '%{conversion}'")
		if conversion in "cs" and (plus or space or alternate or zero):
			raise ValueError("formatf: only '-' and width are valid flags for %c/%s")
		if conversion == "c" and precision is not None:
			raise ValueError("formatf: precision is not valid for %c")
		if argument_index >= len(args):
			raise ValueError("formatf: not enough arguments")
		argument = _vs_text(args[argument_index])
		argument_index += 1
		value = ""
		prefix_length = 0
		numeric = False
		integer_conversion = False
		if conversion in "di":
			number = _vs_parse_signed_text(argument, 64)
			negative = number < 0
			digits = str(abs(number))
			if precision == 0 and number == 0:
				digits = ""
			if precision is not None:
				digits = digits.rjust(precision, "0")
			sign = "-" if negative else "+" if plus else " " if space else ""
			value = sign + digits
			prefix_length = len(sign)
			numeric = integer_conversion = True
		elif conversion in "uoxX":
			number = _vs_parse_unsigned_text(argument, 64)
			if conversion == "u":
				digits = str(number)
			elif conversion == "o":
				digits = format(number, "o")
			else:
				digits = format(number, conversion)
			if precision == 0 and number == 0:
				digits = ""
			if precision is not None:
				digits = digits.rjust(precision, "0")
			prefix = ""
			if alternate and number != 0 and conversion in "xX":
				prefix = "0X" if conversion == "X" else "0x"
			elif alternate and conversion == "o" and not digits.startswith("0"):
				prefix = "0"
			value = prefix + digits
			prefix_length = len(prefix)
			numeric = integer_conversion = True
		elif conversion in "fFeEgG":
			number = _vs_parse_float_text(argument)
			format_type = conversion.lower()
			format_precision = 6 if precision is None else precision
			spec = ("+" if plus else " " if space else "")
			if alternate:
				spec += "#"
			spec += f".{format_precision}{format_type}"
			value = format(number, spec)
			if conversion.isupper():
				value = value.upper()
			prefix_length = 1 if value.startswith(("-", "+", " ")) else 0
			numeric = True
		elif conversion == "c":
			try:
				code = _vs_parse_signed_text(argument, 64)
			except ValueError:
				encoded = argument.encode("utf-8", errors="surrogateescape")
				if len(encoded) != 1:
					raise ValueError("formatf: %c requires an integer or one-byte string")
				value = argument
			else:
				if code < 0 or code > 255:
					raise ValueError("formatf: %c value is outside byte range")
				value = bytes([code]).decode("utf-8", errors="surrogateescape")
		else:
			value = argument
			if precision is not None and _vs_byte_size(value) > precision:
				value = _vs_truncate_bytes(value, precision)
		allow_zero = (zero and numeric and not left
					  and (precision is None or not integer_conversion))
		result.append(_vs_apply_width(
			value, width, "<" if left else ">", allow_zero, prefix_length))
		cursor = end
	return _vs_join(result, "")


def _vs_print(*args: object) -> None:
	if len(args) > 1 and isinstance(args[0], str):
		print(_vs_format(args[0], *args[1:]))
	else:
		print(" ".join(_vs_text(value) for value in args))


def _vs_eprint(*args: object) -> None:
	print(" ".join(_vs_text(value) for value in args), file=_vs_sys.stderr)


def _vs_console_write(value: object, error: bool = False) -> None:
	stream = _vs_sys.stderr if error else _vs_sys.stdout
	stream.write(_vs_text(value))


def _vs_console_is_interactive() -> bool:
	try:
		return bool(_vs_sys.stdin.isatty()) and _vs_console_is_output_terminal()
	except (OSError, ValueError, AttributeError):
		return False


def _vs_console_is_output_terminal() -> bool:
	try:
		return bool(_vs_sys.stdout.isatty())
	except (OSError, ValueError, AttributeError):
		return False


_vs_console_kernel = None
_vs_console_win_types = None
_vs_console_windows_defaults = _vs_threading.local()


def _vs_console_windows_types():
	global _vs_console_win_types
	if _vs_console_win_types is None:
		import ctypes
		class Coord(ctypes.Structure):
			_fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]
		class Rect(ctypes.Structure):
			_fields_ = [("left", ctypes.c_int16), ("top", ctypes.c_int16),
						("right", ctypes.c_int16), ("bottom", ctypes.c_int16)]
		class BufferInfo(ctypes.Structure):
			_fields_ = [("size", Coord), ("position", Coord), ("attributes", ctypes.c_uint16),
						("window", Rect), ("maximum", Coord)]
		class CursorInfo(ctypes.Structure):
			_fields_ = [("size", ctypes.c_uint32), ("visible", ctypes.c_int32)]
		_vs_console_win_types = ctypes, Coord, BufferInfo, CursorInfo
	return _vs_console_win_types


def _vs_console_windows():
	if _vs_sys.platform != "win32":
		return None
	import ctypes
	global _vs_console_kernel
	if _vs_console_kernel is None:
		_vs_console_kernel = ctypes.WinDLL("kernel32", use_last_error=True)
		_vs_console_kernel.GetStdHandle.restype = ctypes.c_void_p
	kernel = _vs_console_kernel
	handle = kernel.GetStdHandle(-11)
	mode = ctypes.c_uint32()
	if not handle or not kernel.GetConsoleMode(ctypes.c_void_p(handle), ctypes.byref(mode)):
		return None
	return kernel, ctypes.c_void_p(handle), mode


def _vs_console_windows_buffer():
	windows = _vs_console_windows()
	if windows is None:
		return None
	ctypes, _, BufferInfo, _ = _vs_console_windows_types()
	kernel, handle, _ = windows
	buffer = BufferInfo()
	if not kernel.GetConsoleScreenBufferInfo(handle, ctypes.byref(buffer)):
		return None
	return kernel, handle, buffer


def _vs_console_supports_color() -> bool:
	if not _vs_console_is_output_terminal():
		return False
	if _vs_sys.platform == "win32":
		return _vs_console_windows_buffer() is not None
	return _vs_os.environ.get("TERM", "") not in ("", "dumb")


def _vs_console_supports_cursor() -> bool:
	return _vs_console_supports_color()


def _vs_console_supports_clear() -> bool:
	return _vs_console_supports_color()


def _vs_console_size():
	if not _vs_console_is_output_terminal():
		return 0, 0
	if _vs_sys.platform == "win32":
		windows = _vs_console_windows_buffer()
		if windows is None:
			return 0, 0
		window = windows[2].window
		return max(0, window.right - window.left + 1), max(0, window.bottom - window.top + 1)
	try:
		size = _vs_os.get_terminal_size(_vs_sys.stdout.fileno())
		return size.columns, size.lines
	except (OSError, ValueError, AttributeError):
		return 0, 0


def _vs_console_width() -> int:
	return _vs_console_size()[0]


def _vs_console_height() -> int:
	return _vs_console_size()[1]


_vs_console_dirty_style = False
_vs_console_dirty_cursor = False
_vs_console_lock = _vs_threading.RLock()


def _vs_console_locked(operation):
	@_vs_functools.wraps(operation)
	def invoke(*args, **kwargs):
		with _vs_console_lock:
			return operation(*args, **kwargs)
	return invoke


@_vs_console_locked
def _vs_console_sequence(text: str) -> bool:
	if not _vs_console_supports_color():
		return False
	windows = _vs_console_windows() if _vs_sys.platform == "win32" else None
	if _vs_sys.platform == "win32" and windows is None:
		return False
	try:
		_vs_sys.stdout.flush()
	except (OSError, ValueError):
		return False
	if windows is not None:
		kernel, handle, mode = windows
		if not kernel.SetConsoleMode(handle, mode.value | 4):
			return False
	try:
		_vs_sys.stdout.write(text)
		_vs_sys.stdout.flush()
		return True
	except (OSError, ValueError):
		return False
	finally:
		if windows is not None:
			kernel.SetConsoleMode(handle, mode.value)


@_vs_console_locked
def _vs_console_windows_control(operation: str, first: int = 0, second: int = 0) -> bool:
	if not _vs_console_is_output_terminal():
		return False
	try:
		_vs_sys.stdout.flush()
	except (OSError, ValueError):
		return False
	windows = _vs_console_windows_buffer()
	if windows is None:
		return False
	ctypes, Coord, _, CursorInfo = _vs_console_windows_types()
	kernel, handle, buffer = windows
	window = buffer.window
	if operation == "visible":
		cursor = CursorInfo()
		if not kernel.GetConsoleCursorInfo(handle, ctypes.byref(cursor)):
			return False
		cursor.visible = first
		return bool(kernel.SetConsoleCursorInfo(handle, ctypes.byref(cursor)))
	if operation == "move":
		if first >= window.right - window.left + 1 or second >= window.bottom - window.top + 1:
			return False
		return bool(kernel.SetConsoleCursorPosition(handle, Coord(window.left + first, window.top + second)))
	def fill(column: int, row: int, count: int) -> bool:
		written = ctypes.c_uint32()
		position = Coord(column, row)
		return (bool(kernel.FillConsoleOutputCharacterW(handle, ctypes.c_wchar(" "),
					ctypes.c_uint32(count), position, ctypes.byref(written)))
				and written.value == count
				and bool(kernel.FillConsoleOutputAttribute(handle, ctypes.c_uint16(buffer.attributes),
					ctypes.c_uint32(count), position, ctypes.byref(written)))
				and written.value == count)
	if operation == "line":
		column, row = buffer.position.x, buffer.position.y
		if column < window.left or column > window.right:
			return False
		count = (window.right - column + 1 if first == 0 else column - window.left + 1
				 if first == 1 else window.right - window.left + 1)
		return fill(column if first == 0 else window.left, row, count)
	if operation == "clear":
		for row in range(window.top, window.bottom + 1):
			if not fill(window.left, row, window.right - window.left + 1):
				return False
		return bool(kernel.SetConsoleCursorPosition(handle, Coord(window.left, window.top)))
	raise RuntimeError("std::console: invalid operation")


def _vs_console_clear() -> bool:
	if _vs_sys.platform == "win32":
		return _vs_console_windows_control("clear")
	return _vs_console_sequence("\x1b[2J\x1b[H")


def _vs_console_clear_line(mode: str) -> bool:
	modes = {"after": 0, "before": 1, "all": 2}
	if mode not in modes:
		raise RuntimeError("std::console::clear_line: expected all, before or after")
	if _vs_sys.platform == "win32":
		return _vs_console_windows_control("line", modes[mode])
	return _vs_console_sequence(f"\x1b[{modes[mode]}K")


def _vs_console_windows_palette(palette: int) -> bool:
	try:
		_vs_sys.stdout.flush()
	except (OSError, ValueError):
		return False
	windows = _vs_console_windows_buffer()
	if windows is None:
		return False
	kernel, handle, current = windows
	return bool(kernel.SetConsoleTextAttribute(handle, (current.attributes & ~0xff) | palette))


@_vs_console_locked
def _vs_console_set_color(foreground: str, background: str) -> bool:
	global _vs_console_dirty_style
	names = ("black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
			 "bright_black", "bright_red", "bright_green", "bright_yellow",
			 "bright_blue", "bright_magenta", "bright_cyan", "bright_white")
	def color(name: str, bg: bool) -> int:
		if name == "default":
			return 49 if bg else 39
		if name not in names:
			raise RuntimeError("std::console::set_color: unknown color name")
		index = names.index(name)
		return (40 if bg else 30) + index if index < 8 else (100 if bg else 90) + index - 8
	first, second = color(foreground, False), color(background, True)
	if not _vs_console_supports_color():
		return False
	_vs_console_mark(True, False)
	if _vs_sys.platform == "win32":
		default = getattr(_vs_console_windows_defaults, "palette", 7)
		if _vs_console_windows_state is not None and _vs_console_windows_state[4]:
			default = _vs_console_windows_state[2].attributes & 0xff
		palette = (0, 4, 2, 6, 1, 5, 3, 7)
		def bits(name: str, bg: bool) -> int:
			if name == "default":
				return default & (0xf0 if bg else 0x0f)
			index = names.index(name)
			value = palette[index % 8] | (8 if index >= 8 else 0)
			return value << 4 if bg else value
		return _vs_console_windows_palette(bits(foreground, False) | bits(background, True))
	return _vs_console_sequence(f"\x1b[{first};{second}m")


@_vs_console_locked
def _vs_console_reset_style() -> bool:
	if not _vs_console_supports_color():
		return False
	_vs_console_mark(True, False)
	if _vs_sys.platform == "win32":
		default = getattr(_vs_console_windows_defaults, "palette", 7)
		if _vs_console_windows_state is not None and _vs_console_windows_state[4]:
			default = _vs_console_windows_state[2].attributes & 0xff
		return _vs_console_windows_palette(default)
	return _vs_console_sequence("\x1b[0m")


def _vs_console_move_cursor(column: int, row: int) -> bool:
	if not (0 <= column <= 32766 and 0 <= row <= 32766):
		raise RuntimeError("std::console::move_cursor coordinates must be in 0..32766")
	width, height = _vs_console_size()
	if (width > 0 and column >= width) or (height > 0 and row >= height):
		return False
	if _vs_sys.platform == "win32":
		return _vs_console_windows_control("move", column, row)
	return _vs_console_sequence(f"\x1b[{row + 1};{column + 1}H")


@_vs_console_locked
def _vs_console_set_cursor_visible(visible: bool) -> bool:
	global _vs_console_dirty_cursor
	if not _vs_console_supports_cursor():
		return False
	_vs_console_mark(False, True)
	if _vs_sys.platform == "win32":
		return _vs_console_windows_control("visible", int(visible))
	return _vs_console_sequence("\x1b[?25h" if visible else "\x1b[?25l")


_vs_console_windows_state = None


def _vs_console_mark(style: bool, cursor: bool) -> None:
	global _vs_console_dirty_style, _vs_console_dirty_cursor, _vs_console_windows_state
	if not _vs_console_dirty_style and not _vs_console_dirty_cursor and _vs_sys.platform == "win32":
		ctypes, _, BufferInfo, CursorInfo = _vs_console_windows_types()
		windows = _vs_console_windows()
		if windows is not None:
			kernel, handle, _ = windows
			buffer, info = BufferInfo(), CursorInfo()
			have_buffer = bool(kernel.GetConsoleScreenBufferInfo(handle, ctypes.byref(buffer)))
			have_cursor = bool(kernel.GetConsoleCursorInfo(handle, ctypes.byref(info)))
			if have_buffer:
				_vs_console_windows_defaults.palette = buffer.attributes & 0xff
			_vs_console_windows_state = (kernel, handle, buffer, info, have_buffer, have_cursor)
	_vs_console_dirty_style |= style
	_vs_console_dirty_cursor |= cursor


@_vs_console_locked
def _vs_console_restore() -> None:
	global _vs_console_dirty_style, _vs_console_dirty_cursor, _vs_console_windows_state
	restore_style, restore_cursor = _vs_console_dirty_style, _vs_console_dirty_cursor
	if not restore_style and not restore_cursor:
		return
	try:
		try:
			_vs_sys.stdout.flush()
		except Exception:
			pass
		if _vs_console_windows_state is not None:
			import ctypes
			kernel, handle, buffer, cursor, have_buffer, have_cursor = _vs_console_windows_state
			if restore_style and have_buffer:
				try:
					kernel.SetConsoleTextAttribute(handle, buffer.attributes)
				except Exception:
					pass
				restore_style = False
			if restore_cursor and have_cursor:
				try:
					kernel.SetConsoleCursorInfo(handle, ctypes.byref(cursor))
				except Exception:
					pass
				restore_cursor = False
		if restore_style:
			try:
				_vs_console_reset_style()
			except Exception:
				pass
		if restore_cursor:
			try:
				if _vs_sys.platform == "win32":
					_vs_console_windows_control("visible", 1)
				else:
					_vs_console_sequence("\x1b[?25h")
			except Exception:
				pass
	except Exception:
		pass
	finally:
		_vs_console_dirty_style = _vs_console_dirty_cursor = False
		_vs_console_windows_state = None


def _vs_assert(condition: object, message: object = "assertion failed") -> None:
	if not _vs_condition(condition):
		raise AssertionError(_vs_text(message))


def _vs_setenv(name: object, value: object) -> None:
	key = str(name)
	text = str(value)
	if not key or "=" in key or "\x00" in key or "\x00" in text:
		raise ValueError("invalid environment variable name or value")
	_vs_os.environ[key] = text


def _vs_throw(value: object) -> None:
	raise _VsThrown(value)


def _vs_read_bool() -> bool:
	value = _vs_read_token()
	if value == "true":
		return True
	if value == "false":
		return False
	raise ValueError("read_bool expects true or false")


def _vs_read_int() -> int:
	token = _vs_read_token()
	if not token:
		raise ValueError("read_int: unexpected end of input")
	return _vs_parse_signed_text(token, 32)


def _vs_read_double() -> float:
	token = _vs_read_token()
	if not token:
		raise ValueError("read_double: unexpected end of input")
	return _vs_parse_float_text(token)


_vs_input_pushback = ""
_vs_input_stream = None


def _vs_read_char() -> str:
	global _vs_input_pushback, _vs_input_stream
	if _vs_input_stream is not _vs_sys.stdin:
		# Core treats only LF as the line delimiter; retain standalone CR bytes.
		if hasattr(_vs_sys.stdin, "reconfigure"):
			_vs_sys.stdin.reconfigure(newline="\n")
		_vs_input_stream = _vs_sys.stdin
	if _vs_input_pushback:
		value = _vs_input_pushback
		_vs_input_pushback = ""
		return value
	return _vs_sys.stdin.read(1)


def _vs_unread_char(value: str) -> None:
	global _vs_input_pushback
	_vs_input_pushback = value


def _vs_read_line() -> str:
	prefix = _vs_read_char()
	if not prefix:
		return ""
	if prefix == "\n":
		return ""
	value = prefix + _vs_sys.stdin.readline()
	if value.endswith("\n"):
		value = value[:-1]
	if value.endswith("\r"):
		value = value[:-1]
	return value


def _vs_read_token() -> str:
	whitespace = {" ", "\t", "\n", "\r", "\v", "\f"}
	value = _vs_read_char()
	while value and value in whitespace:
		value = _vs_read_char()
	token: list[str] = []
	while value and value not in whitespace:
		token.append(value)
		value = _vs_read_char()
	if value:
		_vs_unread_char(value)
	if not token:
		raise ValueError("unexpected end of input")
	return "".join(token)


def _vs_read_file(path: object) -> str:
	return _vs_pathlib.Path(str(path)).read_bytes().decode(
		"utf-8", errors="surrogateescape")


def _vs_write_file(path: object, text: object) -> None:
	_vs_pathlib.Path(str(path)).write_bytes(
		str(text).encode("utf-8", errors="surrogateescape"))


def _vs_append_file(path: object, text: object) -> None:
	with _vs_pathlib.Path(str(path)).open("ab") as stream:
		stream.write(str(text).encode("utf-8", errors="surrogateescape"))


def _vs_delete_file(path: object) -> bool:
	target = _vs_pathlib.Path(str(path))
	try:
		target.unlink()
	except FileNotFoundError:
		return False
	return True


def _vs_exists(path: object) -> bool:
	try:
		_vs_pathlib.Path(str(path)).stat()
	except (FileNotFoundError, NotADirectoryError):
		return False
	return True


def _vs_is_file(path: object) -> bool:
	try:
		status = _vs_pathlib.Path(str(path)).stat()
	except (FileNotFoundError, NotADirectoryError):
		return False
	return _vs_stat.S_ISREG(status.st_mode)


def _vs_is_directory(path: object) -> bool:
	try:
		status = _vs_pathlib.Path(str(path)).stat()
	except (FileNotFoundError, NotADirectoryError):
		return False
	return _vs_stat.S_ISDIR(status.st_mode)


def _vs_remove_dir(path: object) -> bool:
	target = _vs_pathlib.Path(str(path))
	try:
		status = target.lstat()
	except (FileNotFoundError, NotADirectoryError):
		return False
	if not _vs_stat.S_ISDIR(status.st_mode):
		raise RuntimeError("remove_dir requires a directory")
	try:
		target.rmdir()
	except FileNotFoundError:
		return False
	return True


def _vs_list_dir(path: object) -> _VsArray:
	return _VsArray(
		(entry.name for entry in _vs_pathlib.Path(str(path)).iterdir()),
		type_name="array<string>")


)PY";
		}

		bool CompileCheck(const FProgram& Program, std::string& OutError, std::vector<FMethodBindingCodegenInfo>& OutBindings, std::unique_ptr<FBytecodeModule>& OutModule)
		{
			if (RequiresCoreTemplateExport(Program))
			{
				OutError = "AOT: extended template applications require the Core AOT exporter";
				return false;
			}
			VeroScriptCompiler Compiler;
			Compiler.SetCollectRuntimeMemoryTypes(true);
			std::unique_ptr<FBytecodeModule> Module = Compiler.Compile(Program);
			if (!Module || Compiler.HasError())
			{
				OutError = Compiler.HasError()
				    ? Compiler.GetError().Message
				    : "Python AOT: compile check failed";
				return false;
			}
			if (Numerics::UsesRuntimeNumerics(*Module, nullptr))
			{
				OutError = "AOT: bigint/rational require the VeroScript numeric runtime; standalone output is unsupported";
				return false;
			}
			if (Variant::UsesRuntime(*Module))
			{
				OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (StandardLibrary::UsesRuntimeFoundation(*Module))
			{
				OutError = "AOT: standard-library foundation operations require the VeroScript runtime; standalone output is unsupported";
				return false;
			}
			if (Compiler.UsesRuntimeMemoryTypes() || Memory::UsesRuntimeMemory(*Module, nullptr))
			{
				OutError =
				    "AOT: std::memory requires the VeroScript runtime; standalone output is unsupported";
				return false;
			}

			OutBindings = Compiler.GetMethodBindingCodegenInfo();
			OutModule = std::move(Module);
			return true;
		}

		class FPythonEmitter
		{
		public:
			explicit FPythonEmitter(const FProgram& InProgram, const std::vector<FMethodBindingCodegenInfo>& InBindings) : Program(InProgram), MethodBindings(InBindings)
			{
				for (const FTypeAliasDecl& Alias : Program.TypeAliases)
				{
					Aliases[Alias.Name] = Alias.TypeName;
				}
				for (const FStructDecl& Struct : Program.Structs)
				{
					StructNames.insert(Struct.Name);
					TypeNames.insert(Struct.Name);
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					TypeNames.insert(Class.Name);
					if ((IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						bHasStandardEvents = true;
					}
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					TypeNames.insert(Interface.Name);
				}
				for (const FEnumDecl& Enum : Program.Enums)
				{
					TypeNames.insert(Enum.Name);
					EnumNames.insert(Enum.Name);
					if (!Enum.bScoped)
					{
						for (const FEnumMemberDecl& Member : Enum.Members)
						{
							UnscopedEnumMembers[Member.Name] =
							    PythonIdentifier(Enum.Name) + "." + PythonIdentifier(Member.Name);
						}
					}
				}
				for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
				{
					if (Statement && Statement->Kind == EStmtKind::VarDecl)
					{
						GlobalDeclarations.push_back(static_cast<const FVarDeclStmt*>(Statement.get()));
					}
					else if (Statement && Statement->Kind == EStmtKind::VarDeclGroup)
					{
						for (const auto& Global : static_cast<const FVarDeclGroupStmt&>(*Statement).Declarations)
						{
							if (Global)
							{
								GlobalDeclarations.push_back(Global.get());
							}
						}
					}
				}
				for (const FVarDeclStmt* Global : GlobalDeclarations)
				{
					GlobalNames.insert(Global->Name);
					GlobalTypes[Global->Name] = Global->TypeName;
				}
				for (const FFunctionDecl& Function : Program.Functions)
				{
					FunctionReturnTypes[Function.Name] = Function.ReturnType;
				}
			}

			bool Emit(
			    std::string& OutPython,
			    std::string& OutError,
			    std::string* const OutRuntime = nullptr,
			    std::string* const OutProgram = nullptr)
			{
				if (!ValidateProgram())
				{
					OutError = Error;
					return false;
				}
				const std::string Runtime = RuntimeSupport();
				Out << Runtime;
				EmitAliasTable();
				EmitEnums();
				EmitInterfaces();
				EmitStructsAndClasses();
				EmitTypeRegistrations();
				EmitFunctions();
				EmitWorkerRegistrations();
				EmitReservedSlots();
				EmitStaticFieldInitializers();
				EmitTopLevel();
				EmitEntryPoint();
				if (bFailed)
				{
					OutError = Error;
					return false;
				}
				OutPython = Out.str();
				if (OutRuntime)
				{
					*OutRuntime = Runtime;
					*OutRuntime += "\n__all__ = [___vs_name for ___vs_name in globals() "
					               "if ___vs_name.startswith(('_vs', '_Vs', '_VS'))]\n";
				}
				if (OutProgram)
				{
					*OutProgram = "# Generated VeroScript Python AOT program fragment.\n"
					              "if __package__:\n"
					              "\tfrom ._veroscript_runtime import *\n"
					              "else:\n"
					              "\tfrom _veroscript_runtime import *\n\n";
					OutProgram->append(OutPython.substr(Runtime.size()));
				}
				return true;
			}

		private:
			enum class EBreakTarget
			{
				Loop,
				Switch
			};
			enum class EContinueTarget
			{
				NativeLoop,
				TransformedFor
			};
			struct FLocalState
			{
				std::unordered_set<std::string> Names;
				std::unordered_map<std::string, std::string> Types;
				std::unordered_map<std::string, std::string> PythonNames;
			};

			static std::string Pad(const int Indent)
			{
				return std::string(static_cast<std::size_t>(Indent), '\t');
			}

			bool AttributeModule(const std::string& Path) const
			{
				return StandardLibrary::IsAttributeImplementationModule(Program, Path);
			}

			bool HasStandardAttribute() const
			{
				for (const auto& D : Program.Structs)
				{
					if (D.Name == "attribute" && AttributeModule(D.ModulePath)) return true;
				}

				return false;
			}

			bool IsMethodBindingFactoryCall(const FCallExpr& Call) const
			{
				if (Call.TypeArgs.empty()) return false;
				if (FindMethodBindingCodegenInfo(MethodBindings, Call.Location)) return true;
				return std::any_of(Program.Functions.begin(), Program.Functions.end(),
				    [&](const auto& D)
				{
					return D.Name == Call.Callee && StandardLibrary::IsMethodBindingFactoryDeclaration(D);
				});
			}

			bool IsWeakAttributeFactoryCall(const FCallExpr& Call) const
			{
				return Call.Callee == "__std_make_weak_attribute" && !Call.TypeArgs.empty() &&
				    std::any_of(Program.Functions.begin(), Program.Functions.end(),
				        [&](const auto& D)
				{
					return D.Name == Call.Callee && AttributeModule(D.ModulePath);
				});
			}

			static bool IsStandardEventClass(const FClassDecl& Class)
			{
				if (Class.Name != "delegate" && Class.Name != "multicast_delegate")
				{
					return false;
				}
				std::string ModulePath = Class.ModulePath;
				std::replace(ModulePath.begin(), ModulePath.end(), '\\', '/');
				constexpr std::string_view StandardPath = "/stdlib/delegate.vs";
				return ModulePath == "stdlib/delegate.vs" || (ModulePath.size() >= StandardPath.size() && ModulePath.compare(ModulePath.size() - StandardPath.size(), StandardPath.size(), StandardPath) == 0);
			}

			FLocalState SaveLocalState() const
			{
				return {LocalNames, LocalTypes, LocalPythonNames};
			}

			void RestoreLocalState(FLocalState State)
			{
				LocalNames = std::move(State.Names);
				LocalTypes = std::move(State.Types);
				LocalPythonNames = std::move(State.PythonNames);
			}

			void ResetLocalState()
			{
				LocalNames.clear();
				LocalTypes.clear();
				LocalPythonNames.clear();
				UsedPythonLocalNames.clear();
			}

			std::string DeclareLocal(const std::string& Name, const std::string& TypeName)
			{
				const std::string Preferred = PythonIdentifier(Name);
				std::string Emitted = Preferred;
				if (GlobalNames.find(Name) != GlobalNames.end() || UsedPythonLocalNames.find(Emitted) != UsedPythonLocalNames.end())
				{
					do
					{
						Emitted = "___vs_local_" + std::to_string(NextTempId++) + "_" + Preferred;
					}
					while (UsedPythonLocalNames.find(Emitted) != UsedPythonLocalNames.end());
				}
				UsedPythonLocalNames.insert(Emitted);
				LocalNames.insert(Name);
				LocalTypes[Name] = TypeName;
				LocalPythonNames[Name] = Emitted;
				return Emitted;
			}

			void Fail(const std::string& Message, const FSourceLocation& Location = {})
			{
				if (bFailed)
				{
					return;
				}
				bFailed = true;
				std::ostringstream Text;
				if (!Location.FileLabel.empty())
				{
					Text << Location.FileLabel << ':' << Location.Line << ':' << Location.Column << ": ";
				}
				Text << "Python AOT: " << Message;
				Error = Text.str();
			}

			std::string ResolveAlias(std::string TypeName) const
			{
				std::unordered_set<std::string> Seen;
				while (Seen.insert(TypeName).second)
				{
					const auto It = Aliases.find(TypeName);
					if (It == Aliases.end())
					{
						break;
					}
					TypeName = It->second;
				}
				return TypeName;
			}

			static std::string BaseType(std::string TypeName)
			{
				TypeName.erase(std::remove_if(TypeName.begin(), TypeName.end(),
				                   [](const char Char)
				{
					return Char == '&';
				}),
				    TypeName.end());
				while (TypeName.rfind("const ", 0) == 0)
				{
					TypeName.erase(0, 6);
				}
				const std::size_t Generic = TypeName.find('<');
				if (Generic != std::string::npos)
				{
					TypeName.resize(Generic);
				}
				if (TypeName.size() >= 2 && TypeName.compare(TypeName.size() - 2, 2, "[]") == 0)
				{
					return "array";
				}
				return TypeName;
			}

			bool ValidateTypeParameters(
			    const std::vector<std::string>& TypeParameters,
			    const std::string& Kind,
			    const std::string& Name,
			    const FSourceLocation& Location,
			    const bool bAllowStandardConcurrentType = false)
			{
				if (!TypeParameters.empty() && Kind != "function" && !bAllowStandardConcurrentType)
				{
					Fail("open generic " + Kind + " '" + Name + "' is not supported yet", Location);
					return false;
				}
				// Python's runtime types are dynamic. The standard concurrent task wrappers
				// intentionally share one generated class; their T-dependent storage and
				// casts remain inside compile-checked generic functions.
				return true;
			}

			static std::string NormalizeSymbolName(std::string Name)
			{
				if (Name.rfind("::", 0) == 0)
				{
					Name.erase(0, 2);
				}
				for (std::size_t Pos = 0;
				    (Pos = Name.find("::", Pos)) != std::string::npos;)
				{
					Name.replace(Pos, 2, ".");
					++Pos;
				}
				return Name;
			}

			static std::string NamespaceOfSymbol(std::string Name)
			{
				Name = NormalizeSymbolName(std::move(Name));
				const std::size_t Generic = Name.find('<');
				if (Generic != std::string::npos)
				{
					Name.resize(Generic);
				}
				const std::size_t Dot = Name.rfind('.');
				return Dot == std::string::npos ? std::string{} : Name.substr(0, Dot);
			}

			bool NamespaceExists(const std::string& Name) const
			{
				if (Name.empty())
				{
					return false;
				}
				if (std::any_of(Program.Namespaces.begin(), Program.Namespaces.end(),
				        [&](const FNamespaceDecl& Decl)
				{
					return Decl.Name == Name;
				}))
				{
					return true;
				}
				const std::string Prefix = Name + ".";
				const auto HasPrefix = [&](const std::string& Symbol)
				{
					return Symbol.rfind(Prefix, 0) == 0;
				};
				for (const FStructDecl& Decl : Program.Structs)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FClassDecl& Decl : Program.Classes)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FInterfaceDecl& Decl : Program.Interfaces)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FEnumDecl& Decl : Program.Enums)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FTypeAliasDecl& Decl : Program.TypeAliases)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FFunctionDecl& Decl : Program.Functions)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FVarDeclStmt* Global : GlobalDeclarations)
				{
					if (HasPrefix(Global->Name))
					{
						return true;
					}
				}
				return false;
			}

			bool IsSymbolVisible(
			    const std::string& SymbolModulePath,
			    const bool bExported) const
			{
				if (SymbolModulePath == ActiveModulePath || SymbolModulePath.empty() || ActiveModulePath.empty() || (Program.ProjectVisibleModulePaths.find(SymbolModulePath) != Program.ProjectVisibleModulePaths.end() && Program.ProjectVisibleModulePaths.find(ActiveModulePath) != Program.ProjectVisibleModulePaths.end()))
				{
					return true;
				}
				if (!bExported)
				{
					return false;
				}
				std::unordered_set<std::string> Visible{ActiveModulePath};
				std::vector<std::string> Queue;
				for (const FImportDecl& Import : Program.Imports)
				{
					if (Import.ImporterModulePath == ActiveModulePath && !Import.ResolvedPath.empty())
					{
						Visible.insert(Import.ResolvedPath);
						Queue.push_back(Import.ResolvedPath);
					}
				}
				std::unordered_set<std::string> Visited;
				while (!Queue.empty())
				{
					std::string Current = std::move(Queue.back());
					Queue.pop_back();
					if (!Visited.insert(Current).second)
					{
						continue;
					}
					for (const FImportDecl& Import : Program.Imports)
					{
						if (Import.bReExport && Import.ImporterModulePath == Current && !Import.ResolvedPath.empty())
						{
							Visible.insert(Import.ResolvedPath);
							Queue.push_back(Import.ResolvedPath);
						}
					}
				}
				return Visible.find(SymbolModulePath) != Visible.end();
			}

			std::vector<std::string> BuildSymbolLookupNames(
			    std::string Name,
			    const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				if (Name.empty())
				{
					return {};
				}
				const bool bQualified = Name.rfind("::", 0) == 0 || Name.find('.') != std::string::npos || Name.find("::") != std::string::npos || Name.find('\x1F') != std::string::npos;
				Name = NormalizeSymbolName(std::move(Name));
				if (bQualified)
				{
					return {Name};
				}
				std::vector<std::string> Result;
				const auto Add = [&](const std::string& Candidate)
				{
					if (std::find(Result.begin(), Result.end(), Candidate) == Result.end())
					{
						Result.push_back(Candidate);
					}
				};
				std::string Scope = ActiveNamespace;
				while (!Scope.empty())
				{
					Add(Scope + "." + Name);
					const std::size_t Dot = Scope.rfind('.');
					Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
				}
				Add(Name);
				for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
				{
					if (Directive.ModulePath != ActiveModulePath || (!Directive.ScopeNamespace.empty() && ActiveNamespace != Directive.ScopeNamespace && ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0))
					{
						continue;
					}
					const auto LookupLocation = ReferenceLocation ? ResolveProgramLookupLocation(&Program, *ReferenceLocation) : FSourceLocation{};
					if (ReferenceLocation && !LookupLocation.FileLabel.empty() && (LookupLocation.Line < Directive.Location.Line || (LookupLocation.Line == Directive.Location.Line && LookupLocation.Column < Directive.Location.Column)))
					{
						continue;
					}
					const bool bRoot = Directive.NamespaceName.rfind("::", 0) == 0;
					std::string DirectiveScope = bRoot ? std::string{} : Directive.ScopeNamespace;
					for (;;)
					{
						const std::string DirectiveName = NormalizeSymbolName(Directive.NamespaceName);
						const std::string Target = DirectiveScope.empty()
						    ? DirectiveName
						    : DirectiveScope + "." + DirectiveName;
						if (NamespaceExists(Target))
						{
							Add(Target + "." + Name);
							break;
						}
						if (DirectiveScope.empty())
						{
							break;
						}
						const std::size_t Dot = DirectiveScope.rfind('.');
						DirectiveScope = Dot == std::string::npos
						    ? std::string{}
						    : DirectiveScope.substr(0, Dot);
					}
				}
				return Result;
			}

			std::string ResolveTypeSymbol(
			    const std::string& Name,
			    const FSourceLocation* const Location = nullptr) const
			{
				for (const std::string& Candidate : BuildSymbolLookupNames(Name, Location))
				{
					const std::string Type = BaseType(ResolveAlias(Candidate));
					if (TypeNames.find(Type) != TypeNames.end()) return Type;
				}
				return {};
			}

			const FVarDeclStmt* ResolveGlobal(
			    const std::string& Name,
			    const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				for (const std::string& Candidate : BuildSymbolLookupNames(Name, ReferenceLocation))
				{
					const FVarDeclStmt* Selected = nullptr;
					for (const FVarDeclStmt* Declaration : GlobalDeclarations)
					{
						const auto& Global = *Declaration;
						const std::string ModulePath = !Global.ModulePath.empty()
						    ? Global.ModulePath
						    : std::string(Global.Location.FileLabel);
						if (Global.Name != Candidate || !IsSymbolVisible(ModulePath, Global.bExported))
						{
							continue;
						}
						if (ModulePath == ActiveModulePath)
						{
							return &Global;
						}
						if (!Selected)
						{
							Selected = &Global;
						}
					}
					if (Selected)
					{
						return Selected;
					}
				}
				return nullptr;
			}

			const FFunctionDecl* ResolveFunction(
			    const std::string& Name,
			    const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				for (const std::string& Candidate : BuildSymbolLookupNames(Name, ReferenceLocation))
				{
					const FFunctionDecl* Selected = nullptr;
					for (const FFunctionDecl& Function : Program.Functions)
					{
						if (Function.Name != Candidate || Function.bDeleted || !Function.Body || !IsSymbolVisible(Function.ModulePath, Function.bExported))
						{
							continue;
						}
						if (Function.ModulePath == ActiveModulePath)
						{
							return &Function;
						}
						if (!Selected)
						{
							Selected = &Function;
						}
					}
					if (Selected)
					{
						return Selected;
					}
				}
				return nullptr;
			}

			static std::string GlobalStorageName(const FVarDeclStmt& Global)
			{
				std::string Identity = Global.Name;
				std::string ModulePath = !Global.ModulePath.empty()
				    ? Global.ModulePath
				    : std::string(Global.Location.FileLabel);
				if (!ModulePath.empty())
				{
					std::replace(ModulePath.begin(), ModulePath.end(), '\\', '/');
					Identity += '\x1F';
					Identity += ModulePath;
				}
				// This is a dictionary key, not emitted Python syntax. Preserve the full
				// identity so distinct module paths cannot collide through identifier
				// sanitization (for example ':' and '.' both flattening to underscores).
				return Identity;
			}

			static std::string FunctionIdentity(const FFunctionDecl& Function)
			{
				std::string Identity = Function.Name;
				if (!Function.ModulePath.empty())
				{
					std::string ModulePath = Function.ModulePath;
					std::replace(ModulePath.begin(), ModulePath.end(), '\\', '/');
					Identity += '\x1F';
					Identity += ModulePath;
				}
				return Identity;
			}

			static std::string FunctionSymbolLabel(const FFunctionDecl& Function)
			{
				if (Function.ModulePath.empty())
				{
					return Function.Name;
				}
				return Function.Name + " [" + Function.ModulePath + "]";
			}

			static std::string HexIdentity(const std::string_view Identity)
			{
				static constexpr char Digits[] = "0123456789abcdef";
				std::string Result;
				Result.reserve(Identity.size() * 2);
				for (const unsigned char Byte : Identity)
				{
					Result.push_back(Digits[Byte >> 4]);
					Result.push_back(Digits[Byte & 0x0F]);
				}
				return Result;
			}

			static std::string FunctionPythonName(const FFunctionDecl& Function)
			{
				if (Function.Name == "main")
				{
					return "_vs_script_main";
				}
				if (Function.bExported || Function.ModulePath.empty())
				{
					return PythonIdentifier(Function.Name);
				}
				return "VSP_PrivateFunction_" + HexIdentity(FunctionIdentity(Function));
			}

			bool IsSuperReceiver(const FExpr& Expr) const
			{
				if (CurrentTypeName.empty() || Expr.Kind != EExprKind::Identifier) return false;
				const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
				return Name == "Super" && LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) == CurrentFields.end() && CurrentStaticFields.find(Name) == CurrentStaticFields.end() && ResolveGlobal(Name, &Expr.Location) == nullptr && TypeNames.find(BaseType(ResolveAlias(Name))) == TypeNames.end();
			}

			bool TryExtractNamespaceReceiver(const FExpr& Expr, std::string& OutName) const
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
					if (IsSuperReceiver(Expr) || LocalNames.find(Name) != LocalNames.end() || CurrentFields.find(Name) != CurrentFields.end() || CurrentStaticFields.find(Name) != CurrentStaticFields.end() || ResolveGlobal(Name, &Expr.Location) != nullptr)
					{
						return false;
					}
					OutName = NormalizeSymbolName(Name);
					return true;
				}
				if (Expr.Kind != EExprKind::MemberAccess)
				{
					return false;
				}
				const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
				if (!Access.Object || !TryExtractNamespaceReceiver(*Access.Object, OutName))
				{
					return false;
				}
				OutName += "." + Access.Member;
				return true;
			}

			bool TryBuildNamespaceMemberName(
			    const FExpr& Receiver,
			    const std::string& Member,
			    std::string& OutName) const
			{
				std::string ReceiverName;
				if (!TryExtractNamespaceReceiver(Receiver, ReceiverName) || !NamespaceExists(ReceiverName))
				{
					return false;
				}
				OutName = ReceiverName + "." + Member;
				return true;
			}

			static std::string FunctionValueReturnType(const std::string& TypeName)
			{
				if (TypeName.rfind("fn(", 0) != 0 && TypeName.rfind("function(", 0) != 0)
				{
					return {};
				}
				const std::size_t Open = TypeName.find('(');
				int Depth = 0;
				for (std::size_t Index = Open; Index < TypeName.size(); ++Index)
				{
					if (TypeName[Index] == '(')
					{
						++Depth;
					}
					else if (TypeName[Index] == ')' && --Depth == 0)
					{
						const std::size_t Arrow = TypeName.find("->", Index + 1);
						if (Arrow == std::string::npos)
						{
							return {};
						}
						return TypeName.substr(Arrow + 2);
					}
				}
				return {};
			}

			bool IsSupportedTypeName(const std::string& SourceTypeName) const
			{
				const std::string TypeName = ResolveAlias(SourceTypeName);
				const std::string Base = BaseType(TypeName);
				// Function values are ordinary Python callables in generated output. Source
				// lambdas remain deliberately rejected below, but references to declared
				// functions can safely be stored in locals, fields, and globals.
				if (TypeName.rfind("fn(", 0) == 0 || TypeName.rfind("function(", 0) == 0)
				{
					return true;
				}
				if (ActiveTypeParameters.find(Base) != ActiveTypeParameters.end())
				{
					return true;
				}
				static const std::unordered_set<std::string> Scalars = {
				    "void",
				    "bool",
				    "char",
				    "float",
				    "double",
				    "string",
				    "wstring",
				    "int",
				    "signed",
				    "signed int",
				    "short",
				    "signed short",
				    "long",
				    "signed long",
				    "signed char",
				    "unsigned char",
				    "unsigned short",
				    "int8",
				    "int16",
				    "int32",
				    "int64",
				    "uint8",
				    "uint16",
				    "uint32",
				    "uint64",
				    "unsigned",
				    "unsigned int",
				    "unsigned long",
				};
				if (Scalars.find(Base) != Scalars.end() || TypeNames.find(Base) != TypeNames.end())
				{
					return true;
				}
				const std::vector<std::string> Args = TypeArguments(TypeName);
				std::size_t Expected = 0;
				if (Base == "array" || Base == "set" || Base == "deque" || Base == "queue" || Base == "stack" || Base == "optional")
				{
					Expected = 1;
				}
				else if (Base == "channel")
				{
					Expected = 1;
				}
				else if (Base == "map" || Base == "ordered_map" || Base == "pair")
				{
					Expected = 2;
				}
				else
				{
					return false;
				}
				// C-style array spellings such as string[] do not expose angle-bracket args.
				if (Base == "array" && Args.empty() && TypeName.size() >= 2 && TypeName.compare(TypeName.size() - 2, 2, "[]") == 0)
				{
					return true;
				}
				if (Args.size() != Expected)
				{
					return false;
				}
				for (const std::string& Arg : Args)
				{
					if (!IsSupportedTypeName(Arg))
					{
						return false;
					}
				}
				return true;
			}

			bool ValidateSupportedType(
			    const std::string& TypeName,
			    const std::string& Context,
			    const FSourceLocation& Location)
			{
				if (IsSupportedTypeName(TypeName))
				{
					return true;
				}
				Fail("type '" + TypeName + "' used by " + Context + " is not supported in standalone Python output", Location);
				return false;
			}

			bool ValidateParams(
			    const std::vector<FFunctionParam>& Params,
			    const FSourceLocation& Location)
			{
				for (const FFunctionParam& Param : Params)
				{
					if (!ValidateSupportedType(
					        Param.TypeName, "parameter '" + Param.Name + "'", Location))
					{
						return false;
					}
					if (Param.bIsRef)
					{
						Fail("ref/out parameter '" + Param.Name + "' is not supported yet", Location);
						return false;
					}
				}
				return true;
			}

			bool ValidateProgram()
			{
				std::unordered_map<std::string, std::string> EmittedSymbols;
				auto AddEmittedSymbol = [&](const std::string& SourceName,
				                            const std::string& PythonName, const FSourceLocation& Location) -> bool
				{
					if (PythonName.rfind("_vs", 0) == 0 || PythonName.rfind("___vs", 0) == 0)
					{
						Fail("declaration '" + SourceName + "' uses a reserved Python AOT name", Location);
						return false;
					}
					const auto Existing = EmittedSymbols.find(PythonName);
					if (Existing != EmittedSymbols.end() && Existing->second != SourceName)
					{
						Fail("declarations '" + Existing->second + "' and '" + SourceName + "' map to the same Python symbol", Location);
						return false;
					}
					EmittedSymbols[PythonName] = SourceName;
					return true;
				};
				std::unordered_set<std::string> DeclaredTypeNames;
				std::unordered_set<std::string> DeclaredStandardEventNames;
				auto AddTypeSymbol = [&](const std::string& Name, const FSourceLocation& Location) -> bool
				{
					if (!DeclaredTypeNames.insert(Name).second)
					{
						Fail("duplicate flattened type name '" + Name + "' is not supported", Location);
						return false;
					}
					return AddEmittedSymbol(Name, PythonIdentifier(Name), Location);
				};
				for (const FEnumDecl& Enum : Program.Enums)
				{
					if (!AddTypeSymbol(Enum.Name, Enum.Location))
					{
						return false;
					}
					if (!Enum.bScoped)
					{
						for (const FEnumMemberDecl& Member : Enum.Members)
						{
							if (!AddEmittedSymbol(Member.Name, PythonIdentifier(Member.Name), Member.Location))
							{
								return false;
							}
						}
					}
				}
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (!AddTypeSymbol(Struct.Name, Struct.Location))
					{
						return false;
					}
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if ((IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						if (DeclaredStandardEventNames.insert(Class.Name).second && !AddTypeSymbol(Class.Name, Class.Location))
						{
							return false;
						}
						continue;
					}
					if (!AddTypeSymbol(Class.Name, Class.Location))
					{
						return false;
					}
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					if (!AddTypeSymbol(Interface.Name, Interface.Location))
					{
						return false;
					}
				}
				for (const FVarDeclStmt* Declaration : GlobalDeclarations)
				{
					const auto& Global = *Declaration;
					if (!AddEmittedSymbol(
					        Global.Name, GlobalStorageName(Global), Global.Location))
					{
						return false;
					}
				}
				std::unordered_map<std::string, const FFunctionDecl*> FunctionNames;
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if (AttributeModule(Function.ModulePath) ||
					    (!Function.Body && Function.Name == "__std_bind_method"))
					{
						continue;
					}

					ActiveTypeParameters.clear();
					ActiveTypeParameters.insert(Function.TypeParams.begin(), Function.TypeParams.end());
					if (Function.bPrecompiledPackage)
					{
						Fail("precompiled package functions cannot be emitted as standalone Python", Function.Location);
						return false;
					}
					if (!ValidateSupportedType(
					        Function.ReturnType, "function '" + Function.Name + "'", Function.Location) ||
					    !ValidateTypeParameters(Function.TypeParams, "function", Function.Name, Function.Location) || !ValidateParams(Function.Parameters, Function.Location))
					{
						return false;
					}
					if (!Function.bDeleted && Function.Body)
					{
						const std::string PythonName = FunctionPythonName(Function);
						if (Function.Name != "main" && !AddEmittedSymbol(FunctionSymbolLabel(Function), PythonName, Function.Location))
						{
							return false;
						}
						if (!FunctionNames.emplace(FunctionIdentity(Function), &Function).second)
						{
							Fail("overloaded free function '" + Function.Name + "' is not supported yet", Function.Location);
							return false;
						}
					}
					if (Function.Name == "main" && !Function.bDeleted && Function.Body)
					{
						const bool bEntry = Program.EntryModulePath.empty() || Function.ModulePath == Program.EntryModulePath;
						if (!bEntry)
						{
							Fail("an imported main() is not supported", Function.Location);
							return false;
						}
						if (MainFunction)
						{
							Fail("multiple entry main() declarations", Function.Location);
							return false;
						}
						MainFunction = &Function;
					}
				}
				ActiveTypeParameters.clear();
				if (!MainFunction)
				{
					Fail("requires void/int main() or main(string[] args) in the entry module");
					return false;
				}
				if (MainFunction->Parameters.size() > 1 || (MainFunction->Parameters.size() == 1 && BaseType(ResolveAlias(MainFunction->Parameters[0].TypeName)) != "array"))
				{
					Fail("main must take no parameters or one string[] parameter", MainFunction->Location);
					return false;
				}
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (AttributeModule(Struct.ModulePath))
					{
						continue;
					}

					ActiveTypeParameters.clear();
					ActiveTypeParameters.insert(Struct.TypeParams.begin(), Struct.TypeParams.end());
					if (Struct.bPrecompiledPackage || !ValidateTypeParameters(Struct.TypeParams, "struct", Struct.Name, Struct.Location))
					{
						if (!bFailed)
						{
							Fail("precompiled package structs cannot be emitted as standalone Python", Struct.Location);
						}
						return false;
					}
					for (const FFieldDecl& Field : Struct.Fields)
					{
						if (!ValidateSupportedType(Field.TypeName,
						        "field '" + Struct.Name + "." + Field.Name + "'", Field.Location))
						{
							return false;
						}
					}
					if (!ValidateTypeMembers(Struct.Methods, Struct.Constructors, Struct.Destructor.get(), Struct.Name))
					{
						return false;
					}
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if ((IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						continue;
					}
					ActiveTypeParameters.clear();
					ActiveTypeParameters.insert(Class.TypeParams.begin(), Class.TypeParams.end());
					const bool bStandardConcurrentType =
					    (Class.Name == "task" || Class.Name == "task_group") && Class.ModulePath.find("concurrent.vs") != std::string::npos;
					if (Class.bPrecompiledPackage || !ValidateTypeParameters(Class.TypeParams, "class", Class.Name, Class.Location, bStandardConcurrentType))
					{
						if (!bFailed)
						{
							Fail("precompiled package classes cannot be emitted as standalone Python", Class.Location);
						}
						return false;
					}
					if (Class.BaseTypeNames.size() > 1)
					{
						Fail("multiple concrete class bases are not supported because Python MRO cannot preserve VeroScript constructor order", Class.Location);
						return false;
					}
					if (Class.ClassDefaults)
					{
						Fail("class-default blocks are not supported in standalone Python output", Class.Location);
						return false;
					}
					for (const FFieldDecl& Field : Class.Fields)
					{
						if (!ValidateSupportedType(Field.TypeName,
						        "field '" + Class.Name + "." + Field.Name + "'", Field.Location))
						{
							return false;
						}
					}
					if (!ValidateTypeMembers(Class.Methods, Class.Constructors, Class.Destructor.get(), Class.Name))
					{
						return false;
					}
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					ActiveTypeParameters.clear();
					ActiveTypeParameters.insert(
					    Interface.TypeParams.begin(), Interface.TypeParams.end());
					if (Interface.bPrecompiledPackage || !ValidateTypeParameters(Interface.TypeParams, "interface", Interface.Name, Interface.Location))
					{
						if (!bFailed)
						{
							Fail("precompiled package interfaces cannot be emitted as standalone Python", Interface.Location);
						}
						return false;
					}
					for (const FFieldDecl& Field : Interface.Fields)
					{
						if (!ValidateSupportedType(Field.TypeName,
						        "field '" + Interface.Name + "." + Field.Name + "'", Field.Location))
						{
							return false;
						}
					}
					std::vector<FConstructorDecl> Empty;
					if (!ValidateTypeMembers(Interface.Methods, Empty, nullptr, Interface.Name))
					{
						return false;
					}
				}
				ActiveTypeParameters.clear();
				return true;
			}

			bool ValidateTypeMembers(
			    const std::vector<FMethodDecl>& Methods,
			    const std::vector<FConstructorDecl>& Constructors,
			    const FDestructorDecl* Destructor,
			    const std::string& TypeName)
			{
				std::unordered_set<std::string> Names;
				for (const FMethodDecl& Method : Methods)
				{
					if (!ValidateSupportedType(
					        Method.ReturnType, "method '" + TypeName + "." + Method.Name + "'", Method.Location))
					{
						return false;
					}
					if (Method.Name.find("operator") != std::string::npos && Method.Name != "operator<")
					{
						Fail("operator methods are not supported yet", Method.Location);
						return false;
					}
					if (!Method.bDeleted && !Names.insert(Method.Name).second)
					{
						Fail("overloaded method '" + TypeName + "." + Method.Name + "' is not supported yet", Method.Location);
						return false;
					}
					if (!ValidateParams(Method.Parameters, Method.Location))
					{
						return false;
					}
				}
				std::unordered_set<std::size_t> ConstructorArities;
				for (const FConstructorDecl& Constructor : Constructors)
				{
					if (Constructor.bDeleted)
					{
						continue;
					}
					if (!ValidateParams(Constructor.Parameters, Constructor.Location))
					{
						return false;
					}
					std::size_t Minimum = Constructor.Parameters.size();
					while (Minimum > 0 && Constructor.Parameters[Minimum - 1].DefaultValue)
					{
						--Minimum;
					}
					for (std::size_t Arity = Minimum; Arity <= Constructor.Parameters.size(); ++Arity)
					{
						if (!ConstructorArities.insert(Arity).second)
						{
							Fail("constructors of '" + TypeName + "' overlap at the same argument count", Constructor.Location);
							return false;
						}
					}
				}
				if (Destructor && Destructor->Body)
				{
					Fail("user destructor bodies are not supported yet", Destructor->Location);
					return false;
				}
				return true;
			}

			void EmitAliasTable()
			{
				for (const auto& Entry : Aliases)
				{
					Out << "_VS_ALIASES[" << QuotePython(Entry.first) << "] = "
					    << QuotePython(Entry.second) << "\n";
				}
				for (const std::string& Struct : StructNames)
				{
					Out << "_VS_VALUE_TYPES.add(" << QuotePython(BaseType(Struct)) << ")\n";
				}
				Out << '\n';
			}

			void EmitEnums()
			{
				for (const FEnumDecl& Enum : Program.Enums)
				{
					if (Enum.bPrecompiledPackage)
					{
						Fail("precompiled package enums cannot be emitted as standalone Python", Enum.Location);
						return;
					}
					const std::string PythonName = PythonIdentifier(Enum.Name);
					Out << "class " << PythonName << "(_VsEnum):\n";
					if (Enum.Members.empty())
					{
						Out << "\tpass\n";
					}
					else
					{
						for (const FEnumMemberDecl& Member : Enum.Members)
						{
							Out << "\t" << PythonIdentifier(Member.Name) << " = " << Member.Value << "\n";
						}
					}
					std::string DisplayName = Enum.Name;
					if (!Enum.bExported && !Enum.ModulePath.empty())
					{
						DisplayName = Enum.ModulePath;
						std::replace(DisplayName.begin(), DisplayName.end(), '\\', '/');
						DisplayName += '\x1F';
						DisplayName += Enum.Name;
					}
					Out << PythonName << "._vs_type_name = " << QuotePython(DisplayName) << "\n\n";
					Out << PythonName << "._vs_underlying_type = "
					    << QuotePython(Enum.UnderlyingTypeName) << "\n\n";
					Out << PythonName << "._vs_scoped_enum = " << (Enum.bScoped ? "True" : "False") << "\n\n";
					if (!Enum.bScoped)
					{
						for (const FEnumMemberDecl& Member : Enum.Members)
						{
							Out << PythonIdentifier(Member.Name) << " = " << PythonIdentifier(Enum.Name)
							    << '.' << PythonIdentifier(Member.Name) << "\n";
						}
						Out << '\n';
					}
				}
			}

			template <typename TDecl>
			void AddDeclaredMembers(const TDecl& Decl)
			{
				for (const FFieldDecl& Field : Decl.Fields)
				{
					if (Field.bIsStatic)
					{
						CurrentStaticFields.insert(Field.Name);
					}
					else
					{
						CurrentFields.insert(Field.Name);
					}
					CurrentFieldTypes[Field.Name] = Field.TypeName;
				}
				for (const FMethodDecl& Method : Decl.Methods)
				{
					CurrentMethodReturnTypes[Method.Name] = Method.ReturnType;
				}
			}

			void AddInheritedMembers(
			    const std::string& RawTypeName,
			    std::unordered_set<std::string>& Visited)
			{
				const std::string TypeName = BaseType(ResolveAlias(RawTypeName));
				if (TypeName.empty() || !Visited.insert(TypeName).second)
				{
					return;
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					if (Interface.Name != TypeName)
					{
						continue;
					}
					for (const std::string& Base : Interface.BaseInterfaceNames)
					{
						AddInheritedMembers(Base, Visited);
					}
					AddDeclaredMembers(Interface);
					return;
				}
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (Struct.Name != TypeName)
					{
						continue;
					}
					if (!Struct.BaseTypeName.empty())
					{
						AddInheritedMembers(Struct.BaseTypeName, Visited);
					}
					for (const std::string& Interface : Struct.InterfaceTypeNames)
					{
						AddInheritedMembers(Interface, Visited);
					}
					AddDeclaredMembers(Struct);
					return;
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (Class.Name != TypeName)
					{
						continue;
					}
					for (const std::string& Base : Class.BaseTypeNames)
					{
						AddInheritedMembers(Base, Visited);
					}
					for (const std::string& Interface : Class.InterfaceTypeNames)
					{
						AddInheritedMembers(Interface, Visited);
					}
					AddDeclaredMembers(Class);
					return;
				}
			}

			bool IsTypeDependencyReady(
			    const std::string& RawTypeName,
			    const std::unordered_set<std::string>& Declared,
			    const std::unordered_set<std::string>& Emitted) const
			{
				const std::string TypeName = BaseType(ResolveAlias(RawTypeName));
				return Declared.find(TypeName) == Declared.end() || Emitted.find(TypeName) != Emitted.end();
			}

			void EmitInterface(const FInterfaceDecl& Interface)
			{
				ActiveModulePath = Interface.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Interface.Name);
				ActiveTypeParameters.clear();
				ActiveTypeParameters.insert(
				    Interface.TypeParams.begin(), Interface.TypeParams.end());
				Out << "class " << PythonIdentifier(Interface.Name);
				EmitBaseList(Interface.BaseInterfaceNames, {});
				Out << ":\n";
				CurrentTypeName = Interface.Name;
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				std::unordered_set<std::string> Visited;
				for (const std::string& Base : Interface.BaseInterfaceNames)
				{
					AddInheritedMembers(Base, Visited);
				}
				AddDeclaredMembers(Interface);
				if (Interface.Methods.empty() && Interface.Fields.empty())
				{
					Out << "\tpass\n";
				}
				for (const FMethodDecl& Method : Interface.Methods)
				{
					EmitMethod(Method, 1, true);
				}
				Out << '\n';
			}

			void EmitInterfaces()
			{
				std::unordered_set<std::string> Declared;
				std::unordered_set<std::string> Emitted;
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					Declared.insert(Interface.Name);
				}
				while (Emitted.size() < Program.Interfaces.size())
				{
					bool bProgress = false;
					for (const FInterfaceDecl& Interface : Program.Interfaces)
					{
						if (Emitted.find(Interface.Name) != Emitted.end())
						{
							continue;
						}
						bool bReady = true;
						for (const std::string& Base : Interface.BaseInterfaceNames)
						{
							bReady = bReady && IsTypeDependencyReady(Base, Declared, Emitted);
						}
						if (!bReady)
						{
							continue;
						}
						EmitInterface(Interface);
						if (bFailed)
						{
							return;
						}
						Emitted.insert(Interface.Name);
						bProgress = true;
					}
					if (!bProgress)
					{
						Fail("cannot order interface inheritance dependencies");
						return;
					}
				}
				CurrentTypeName.clear();
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
			}

			void EmitBaseList(
			    const std::vector<std::string>& Primary,
			    const std::vector<std::string>& Interfaces)
			{
				Out << '(';
				bool bFirst = true;
				for (const std::string& Base : Primary)
				{
					if (!bFirst)
					{
						Out << ", ";
					}
					bFirst = false;
					Out << PythonIdentifier(Base);
				}
				for (const std::string& Base : Interfaces)
				{
					if (!bFirst)
					{
						Out << ", ";
					}
					bFirst = false;
					Out << PythonIdentifier(Base);
				}
				if (bFirst)
				{
					Out << "object";
				}
				Out << ')';
			}

			template <typename TDecl>
			void EmitUserType(const TDecl& Decl, const bool bValueType)
			{
				ActiveModulePath = Decl.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Decl.Name);
				ActiveTypeParameters.clear();
				ActiveTypeParameters.insert(Decl.TypeParams.begin(), Decl.TypeParams.end());
				std::string DisplayName = Decl.Name;
				if (!Decl.bExported && !Decl.ModulePath.empty())
				{
					DisplayName = Decl.ModulePath;
					std::replace(DisplayName.begin(), DisplayName.end(), '\\', '/');
					DisplayName += '\x1F';
					DisplayName += Decl.Name;
				}
				Out << "class " << PythonIdentifier(Decl.Name);
				if constexpr (std::is_same_v<TDecl, FStructDecl>)
				{
					std::vector<std::string> Primary;
					if (!Decl.BaseTypeName.empty())
					{
						Primary.push_back(Decl.BaseTypeName);
					}
					EmitBaseList(Primary, Decl.InterfaceTypeNames);
				}
				else
				{
					EmitBaseList(Decl.BaseTypeNames, Decl.InterfaceTypeNames);
				}
				Out << ":\n";
				CurrentTypeName = Decl.Name;
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				std::unordered_set<std::string> Visited;
				if constexpr (std::is_same_v<TDecl, FStructDecl>)
				{
					if (!Decl.BaseTypeName.empty())
					{
						AddInheritedMembers(Decl.BaseTypeName, Visited);
					}
				}
				else
				{
					for (const std::string& Base : Decl.BaseTypeNames)
					{
						AddInheritedMembers(Base, Visited);
					}
				}
				for (const std::string& Interface : Decl.InterfaceTypeNames)
				{
					AddInheritedMembers(Interface, Visited);
				}
				AddDeclaredMembers(Decl);
				Out << "\t_vs_type_name = " << QuotePython(DisplayName) << "\n";
				if (bValueType)
				{
					Out << "\t_vs_value_type = True\n";
					if constexpr (std::is_same_v<TDecl, FStructDecl>)
					{
						if (!Decl.BaseTypeName.empty())
						{
							Out << "\t_vs_field_types = dict(getattr("
							    << PythonIdentifier(Decl.BaseTypeName)
							    << ", \"_vs_field_types\", {}))\n";
						}
						else
						{
							Out << "\t_vs_field_types = {}\n";
						}
						for (const FFieldDecl& Field : Decl.Fields)
						{
							if (!Field.bIsStatic)
							{
								Out << "\t_vs_field_types[" << QuotePython(PythonIdentifier(Field.Name))
								    << "] = " << QuotePython(Field.TypeName) << "\n";
							}
						}
					}
				}
				Out << "\tdef __str__(self) -> str:\n"
				    << "\t\treturn getattr(type(self), \"_vs_type_name\", "
				    << QuotePython(DisplayName) << ")\n\n";
				if (bValueType)
				{
					Out << "\tdef __eq__(self, other: object) -> bool:\n"
					    << "\t\tif type(self) is not type(other):\n"
					    << "\t\t\treturn False\n"
					    << "\t\tfor ___vs_name_value, ___vs_field_type in self._vs_field_types.items():\n"
					    << "\t\t\tif not _vs_equal(getattr(self, ___vs_name_value), getattr(other, ___vs_name_value), ___vs_field_type, ___vs_field_type, True):\n"
					    << "\t\t\t\treturn False\n"
					    << "\t\treturn True\n\n"
					    << "\tdef __ne__(self, other: object) -> bool:\n"
					    << "\t\treturn not self == other\n\n";
				}
				for (const FFieldDecl& Field : Decl.Fields)
				{
					if (Field.bIsStatic)
					{
						// Static initialization is deferred until every generated function and
						// type exists, matching VeroScript's module-init phase.
						Out << "\t" << PythonIdentifier(Field.Name) << " = None\n";
					}
				}
				EmitConstructor(Decl.Fields, Decl.Constructors);
				for (const FMethodDecl& Method : Decl.Methods)
				{
					if (!Method.bDeleted)
					{
						EmitMethod(Method, 1, false);
					}
				}
				Out << '\n';
				CurrentTypeName.clear();
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				ActiveTypeParameters.clear();
			}

			void EmitStructsAndClasses()
			{
				std::unordered_set<std::string> DeclaredStructs;
				std::unordered_set<std::string> EmittedStructs;
				for (const FStructDecl& Struct : Program.Structs)
				{
					DeclaredStructs.insert(Struct.Name);
				}
				while (EmittedStructs.size() < Program.Structs.size())
				{
					bool bProgress = false;
					for (const FStructDecl& Struct : Program.Structs)
					{
						if (EmittedStructs.find(Struct.Name) != EmittedStructs.end())
						{
							continue;
						}
						if (!Struct.BaseTypeName.empty() && !IsTypeDependencyReady(Struct.BaseTypeName, DeclaredStructs, EmittedStructs))
						{
							continue;
						}
						if (AttributeModule(Struct.ModulePath))
						{
							EmittedStructs.insert(Struct.Name);
							bProgress = true;
							continue;
						}
						EmitUserType(Struct, true);
						if (bFailed)
						{
							return;
						}
						EmittedStructs.insert(Struct.Name);
						bProgress = true;
					}
					if (!bProgress)
					{
						Fail("cannot order struct inheritance dependencies");
						return;
					}
				}

				std::unordered_set<std::string> DeclaredClasses;
				std::unordered_set<std::string> EmittedClasses;
				for (const FClassDecl& Class : Program.Classes)
				{
					if (!(IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						DeclaredClasses.insert(Class.Name);
					}
				}
				while (EmittedClasses.size() < DeclaredClasses.size())
				{
					bool bProgress = false;
					for (const FClassDecl& Class : Program.Classes)
					{
						if ((IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
						{
							continue;
						}
						if (EmittedClasses.find(Class.Name) != EmittedClasses.end())
						{
							continue;
						}
						bool bReady = true;
						for (const std::string& Base : Class.BaseTypeNames)
						{
							bReady = bReady && IsTypeDependencyReady(Base, DeclaredClasses, EmittedClasses);
						}
						if (!bReady)
						{
							continue;
						}
						EmitUserType(Class, false);
						if (bFailed)
						{
							return;
						}
						EmittedClasses.insert(Class.Name);
						bProgress = true;
					}
					if (!bProgress)
					{
						Fail("cannot order class inheritance dependencies");
						return;
					}
				}
			}

			void EmitReservedSlots()
			{
				// Module initialization starts with integer-zero slots, before any initializer.
				const std::string SavedModule = ActiveModulePath;
				const std::string SavedNamespace = ActiveNamespace;
				for (const FVarDeclStmt* Global : GlobalDeclarations)
				{
					ActiveModulePath = !Global->ModulePath.empty()
					    ? Global->ModulePath
					    : std::string(Global->Location.FileLabel);
					ActiveNamespace = NamespaceOfSymbol(Global->Name);
					const std::string Type = Global->TypeName == "auto" && Global->Initializer
					    ? InferExprType(*Global->Initializer)
					    : Global->TypeName;
					Out << "_vs_define_global(" << QuotePython(GlobalStorageName(*Global))
					    << ", _VsReservedZero(" << QuotePython(Type) << "))\n";
				}
				const auto ReserveStatics = [&](const auto& Declarations)
				{
					for (const auto& Decl : Declarations)
					{
						for (const FFieldDecl& Field : Decl.Fields)
						{
							if (Field.bIsStatic)
							{
								Out << "_vs_define_static(" << PythonIdentifier(Decl.Name) << ", "
								    << QuotePython(PythonIdentifier(Field.Name)) << ", _VsReservedZero("
								    << QuotePython(Field.TypeName) << "))\n";
							}
						}
					}
				};
				ReserveStatics(Program.Structs);
				ReserveStatics(Program.Classes);
				ActiveModulePath = SavedModule;
				ActiveNamespace = SavedNamespace;
			}

			template <typename TDecl>
			void EmitStaticFieldInitializersFor(const TDecl& Decl)
			{
				ActiveModulePath = Decl.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Decl.Name);
				CurrentTypeName = Decl.Name;
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				std::unordered_set<std::string> Visited;
				if constexpr (std::is_same_v<TDecl, FStructDecl>)
				{
					if (!Decl.BaseTypeName.empty())
					{
						AddInheritedMembers(Decl.BaseTypeName, Visited);
					}
				}
				else
				{
					for (const std::string& Base : Decl.BaseTypeNames)
					{
						AddInheritedMembers(Base, Visited);
					}
				}
				for (const std::string& Interface : Decl.InterfaceTypeNames)
				{
					AddInheritedMembers(Interface, Visited);
				}
				AddDeclaredMembers(Decl);
				for (const FFieldDecl& Field : Decl.Fields)
				{
					if (!Field.bIsStatic)
					{
						continue;
					}
					Out << "_vs_define_static(" << PythonIdentifier(Decl.Name) << ", "
					    << QuotePython(PythonIdentifier(Field.Name)) << ", _vs_cast("
					    << QuotePython(Field.TypeName) << ", ";
					if (Field.DefaultValue)
					{
						Out << EmitExpr(*Field.DefaultValue, &Field.TypeName);
					}
					else
					{
						Out << "_vs_default(" << QuotePython(Field.TypeName) << ')';
					}
					Out << "))\n";
				}
			}

			void EmitStaticFieldInitializers()
			{
				for (const FStructDecl& Struct : Program.Structs)
				{
					EmitStaticFieldInitializersFor(Struct);
					if (bFailed)
					{
						return;
					}
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if ((IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						continue;
					}
					EmitStaticFieldInitializersFor(Class);
					if (bFailed)
					{
						return;
					}
				}
				CurrentTypeName.clear();
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				Out << '\n';
			}

			void EmitConstructor(
			    const std::vector<FFieldDecl>& Fields,
			    const std::vector<FConstructorDecl>& Constructors)
			{
				Out << "\tdef __init__(self, *___vs_args: object) -> None:\n";
				std::vector<const FConstructorDecl*> Active;
				for (const FConstructorDecl& Constructor : Constructors)
				{
					if (!Constructor.bDeleted)
					{
						Active.push_back(&Constructor);
					}
				}
				if (Active.empty())
				{
					Out << "\t\tsuper().__init__()\n";
					EmitFieldInitializers(Fields, 2, nullptr);
					Out << "\t\tif ___vs_args:\n"
					    << "\t\t\traise TypeError(" << QuotePython(CurrentTypeName + " expects no constructor arguments") << ")\n"
					    << "\t\treturn\n";
					return;
				}
				bool bFirst = true;
				for (const FConstructorDecl* Constructor : Active)
				{
					std::size_t Minimum = Constructor->Parameters.size();
					while (Minimum > 0 && Constructor->Parameters[Minimum - 1].DefaultValue)
					{
						--Minimum;
					}
					Out << "\t\t" << (bFirst ? "if " : "elif ");
					bFirst = false;
					if (Minimum == Constructor->Parameters.size())
					{
						Out << "len(___vs_args) == " << Minimum;
					}
					else
					{
						Out << Minimum << " <= len(___vs_args) <= " << Constructor->Parameters.size();
					}
					Out << ":\n";
					ResetLocalState();
					for (std::size_t Index = 0; Index < Constructor->Parameters.size(); ++Index)
					{
						const FFunctionParam& Param = Constructor->Parameters[Index];
						const std::string ParamName = DeclareLocal(Param.Name, Param.TypeName);
						Out << "\t\t\t" << ParamName << " = _vs_cast("
						    << QuotePython(Param.TypeName) << ", ";
						if (Param.DefaultValue)
						{
							Out << "___vs_args[" << Index << "] if len(___vs_args) > " << Index
							    << " else " << EmitExpr(*Param.DefaultValue);
						}
						else
						{
							Out << "___vs_args[" << Index << ']';
						}
						Out << ")\n";
					}
					for (const FBaseCtorInit& Base : Constructor->BaseInits)
					{
						Out << "\t\t\t" << PythonIdentifier(Base.BaseName) << ".__init__(self";
						for (const std::unique_ptr<FExpr>& Arg : Base.Args)
						{
							Out << ", " << EmitExpr(*Arg);
						}
						Out << ")\n";
					}
					if (Constructor->BaseInits.empty())
					{
						Out << "\t\t\tsuper().__init__()\n";
					}
					EmitFieldInitializers(Fields, 3, Constructor);
					if (Constructor->Body)
					{
						EmitBlockContents(*Constructor->Body, 3);
					}
					Out << "\t\t\treturn\n";
				}
				Out << "\t\traise TypeError(" << QuotePython("no matching constructor for " + CurrentTypeName) << ")\n";
			}

			void EmitFieldInitializers(
			    const std::vector<FFieldDecl>& Fields,
			    const int Indent,
			    const FConstructorDecl* Constructor)
			{
				for (const FFieldDecl& Field : Fields)
				{
					if (Field.bIsStatic)
					{
						continue;
					}
					Out << Pad(Indent) << "self." << PythonIdentifier(Field.Name) << " = _vs_cast("
					    << QuotePython(Field.TypeName) << ", ";
					const FFieldCtorInit* FieldInit = nullptr;
					if (Constructor)
					{
						const auto InitIt = std::find_if(
						    Constructor->FieldInits.begin(),
						    Constructor->FieldInits.end(),
						    [&](const FFieldCtorInit& Init)
						{
							return Init.FieldName == Field.Name;
						});
						if (InitIt != Constructor->FieldInits.end())
						{
							FieldInit = &*InitIt;
						}
					}
					if (FieldInit)
					{
						const std::string ResolvedFieldType = ResolveAlias(Field.TypeName);
						const std::string FieldBase = BaseType(ResolvedFieldType);
						const bool bScalarField = IsUnsignedIntegerType(ResolvedFieldType) || IsSignedIntegerType(ResolvedFieldType) || FieldBase == "bool" || FieldBase == "float" || FieldBase == "double" || FieldBase == "string" || FieldBase == "wstring" || EnumNames.find(FieldBase) != EnumNames.end();
						if (bScalarField && FieldInit->Args.empty())
						{
							Out << "_vs_default(" << QuotePython(Field.TypeName) << ')';
						}
						else if (bScalarField && FieldInit->Args.size() == 1)
						{
							Out << EmitExpr(*FieldInit->Args.front(), &Field.TypeName);
						}
						else if (bScalarField)
						{
							Fail(
							    "scalar field initializer for '" + Field.Name + "' expects at most one argument",
							    FieldInit->Location);
							Out << "None";
						}
						else
						{
							FCallExpr Construction(Field.TypeName, {});
							Construction.BorrowArgumentsFrom(FieldInit->Args);
							Construction.Location = FieldInit->Location;
							Out << EmitExpr(Construction, &Field.TypeName);
						}
					}
					else if (Field.DefaultValue)
					{
						Out << EmitExpr(*Field.DefaultValue, &Field.TypeName);
					}
					else
					{
						Out << "_vs_default(" << QuotePython(Field.TypeName) << ')';
					}
					Out << ")\n";
				}
			}

			void EmitMethod(const FMethodDecl& Method, const int Indent, const bool bInterface)
			{
				const auto SavedModule = ActiveModulePath;
				const auto SavedNamespace = ActiveNamespace;
				bInStaticMethod = Method.bIsStatic;
				ResetLocalState();
				CurrentReturnType = Method.ReturnType;
				if (Method.bIsStatic)
				{
					Out << Pad(Indent) << "@staticmethod\n";
				}
				Out << Pad(Indent) << "def "
				    << (Method.Name == "operator<" ? "__lt__" : PythonIdentifier(Method.Name))
				    << '(';
				bool bNeedComma = false;
				if (!Method.bIsStatic)
				{
					Out << "self";
					bNeedComma = true;
				}
				EmitParameters(Method.Parameters, bNeedComma);
				Out << "):\n";
				if (!Method.Body || (bInterface && Method.bPureVirtual))
				{
					Out << Pad(Indent + 1) << "raise NotImplementedError\n";
				}
				else
				{
					EmitGlobalDeclarations(*Method.Body, Indent + 1);
					EmitParameterCasts(Method.Parameters, Indent + 1);
					if (!Method.Body->DefinitionModulePath.empty())
					{
						ActiveModulePath = Method.Body->DefinitionModulePath;
						ActiveNamespace = Method.Body->DefinitionNamespace;
					}
					EmitBlockContents(*Method.Body, Indent + 1);
				}
				if (Method.Name == "operator<" && Method.Parameters.size() == 1)
				{
					Out << Pad(Indent) << "__lt__._vs_parameter_type = "
					    << QuotePython(Method.Parameters.front().TypeName) << "\n"
					    << Pad(Indent) << "__lt__._vs_virtual = "
					    << ((Method.bVirtual || Method.bOverride || Method.bPureVirtual || bInterface) ? "True" : "False") << "\n";
				}
				Out << '\n';
				bInStaticMethod = false;
				ActiveModulePath = SavedModule;
				ActiveNamespace = SavedNamespace;
				CurrentReturnType.clear();
			}

			void EmitParameters(const std::vector<FFunctionParam>& Params, bool bNeedComma)
			{
				for (const FFunctionParam& Param : Params)
				{
					if (bNeedComma)
					{
						Out << ", ";
					}
					bNeedComma = true;
					if (Param.bIsVariadic)
					{
						Out << '*';
					}
					const std::string TypeName = Param.bIsVariadic
					    ? "array<" + Param.TypeName + ">"
					    : Param.TypeName;
					Out << DeclareLocal(Param.Name, TypeName);
					if (Param.DefaultValue)
					{
						Out << '=' << EmitExpr(*Param.DefaultValue);
					}
				}
			}

			void EmitParameterCasts(const std::vector<FFunctionParam>& Params, const int Indent)
			{
				for (const FFunctionParam& Param : Params)
				{
					const auto Found = LocalPythonNames.find(Param.Name);
					const std::string ParamName = Found != LocalPythonNames.end()
					    ? Found->second
					    : PythonIdentifier(Param.Name);
					Out << Pad(Indent) << ParamName << " = _vs_cast("
					    << QuotePython(Param.bIsVariadic
					               ? "array<" + Param.TypeName + ">"
					               : Param.TypeName)
					    << ", " << (Param.bIsVariadic ? "_VsArray(" : "")
					    << ParamName << (Param.bIsVariadic ? ")" : "") << ")\n";
				}
			}

			void EmitFunctions()
			{
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if (AttributeModule(Function.ModulePath) ||
					    (!Function.Body && Function.Name == "__std_bind_method"))
					{
						continue;
					}

					if (Function.bDeleted || !Function.Body)
					{
						continue;
					}
					ActiveTypeParameters.clear();
					ActiveTypeParameters.insert(Function.TypeParams.begin(), Function.TypeParams.end());
					CurrentTypeName.clear();
					CurrentFunctionName = Function.Name;
					CurrentFunctionModulePath = Function.ModulePath;
					ActiveModulePath = Function.ModulePath;
					ActiveNamespace = NamespaceOfSymbol(Function.Name);
					CurrentFields.clear();
					ResetLocalState();
					CurrentReturnType = Function.ReturnType;
					const std::string Name = FunctionPythonName(Function);
					if (!Function.TypeParams.empty())
					{
						Out << "@_vs_generic_function\n";
					}
					Out << "def " << Name << '(';
					EmitParameters(Function.Parameters, false);
					const bool bNeedsCallerModule = Function.Name == "__std_spawn" && Function.ModulePath.find("concurrent.vs") != std::string::npos;
					if (bNeedsCallerModule)
					{
						if (!Function.Parameters.empty())
						{
							Out << ", ";
						}
						Out << "___vs_caller_module="
						    << QuotePython(Program.EntryModulePath.empty()
						               ? "local"
						               : Program.EntryModulePath);
					}
					Out << "):\n";
					EmitGlobalDeclarations(*Function.Body, 1);
					EmitParameterCasts(Function.Parameters, 1);
					if (!Function.Body->DefinitionModulePath.empty())
					{
						ActiveModulePath = Function.Body->DefinitionModulePath;
						ActiveNamespace = Function.Body->DefinitionNamespace;
					}
					EmitBlockContents(*Function.Body, 1);
					Out << '\n';
					if (bFailed)
					{
						return;
					}
				}
				CurrentReturnType.clear();
				CurrentFunctionName.clear();
				CurrentFunctionModulePath.clear();
				ActiveModulePath.clear();
				ActiveNamespace.clear();
				ActiveTypeParameters.clear();
			}

			void EmitTypeRegistrations()
			{
				for (const FEnumDecl& Enum : Program.Enums)
				{
					Out << "_vs_register_type(" << QuotePython(Enum.Name) << ", "
					    << PythonIdentifier(Enum.Name) << ")\n";
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					Out << "_vs_register_type(" << QuotePython(Interface.Name) << ", "
					    << PythonIdentifier(Interface.Name) << ")\n";
				}
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (AttributeModule(Struct.ModulePath))
					{
						if (Struct.Name == "attribute")
						{
							Out << "_vs_register_type(\"attribute\", _VsAttribute)\n";
						}

						continue;
					}
					Out << "_vs_register_type(" << QuotePython(Struct.Name) << ", "
					    << PythonIdentifier(Struct.Name) << ")\n";
				}
				if (bHasStandardEvents)
				{
					Out << "_vs_register_type(\"delegate\", _VsDelegate)\n"
					    << "_vs_register_type(\"multicast_delegate\", _VsMulticastDelegate)\n";
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (!(IsStandardEventClass(Class) || AttributeModule(Class.ModulePath)))
					{
						Out << "_vs_register_type(" << QuotePython(Class.Name) << ", "
						    << PythonIdentifier(Class.Name) << ")\n";
					}
				}
				Out << '\n';
			}

			void EmitWorkerRegistrations()
			{
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if (Function.bDeleted || !Function.Body || Function.ReturnType != "void" || Function.Parameters.size() != 1 || Function.Parameters[0].bIsVariadic || BaseType(ResolveAlias(Function.Parameters[0].TypeName)) != "channel")
					{
						continue;
					}
					Out << "_vs_register_worker(" << QuotePython(Function.Name) << ", "
					    << QuotePython(Function.ModulePath.empty() ? "local" : Function.ModulePath)
					    << ", " << (Function.bExported ? "True" : "False") << ", "
					    << QuotePython(Function.Parameters[0].TypeName) << ", "
					    << FunctionPythonName(Function) << ")\n";
				}
				Out << '\n';
			}

			void EmitGlobalDeclarations(const FBlockStmt& Body, const int Indent)
			{
				(void)Body;
				(void)Indent;
				// Script globals are routed through generated thread-local storage so
				// workers receive isolated VM-style state. No Python global statement is
				// needed (or valid) for those logical variables.
			}

			bool IsLocalInScopes(
			    const std::string& Name,
			    const std::vector<std::unordered_set<std::string>>& Scopes) const
			{
				for (auto It = Scopes.rbegin(); It != Scopes.rend(); ++It)
				{
					if (It->find(Name) != It->end())
					{
						return true;
					}
				}
				return false;
			}

			void CollectAssignedGlobalsExpr(
			    const FExpr* Expr,
			    const std::vector<std::unordered_set<std::string>>& Scopes,
			    std::vector<std::string>& OutNames) const
			{
				if (!Expr)
				{
					return;
				}
				switch (Expr->Kind)
				{
				case EExprKind::Unary:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FUnaryExpr&>(*Expr).Operand.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::Binary:
					{
						const auto& Node = static_cast<const FBinaryExpr&>(*Expr);
						CollectAssignedGlobalsExpr(Node.Left.get(), Scopes, OutNames);
						CollectAssignedGlobalsExpr(Node.Right.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::Assign:
					{
						const auto& Node = static_cast<const FAssignExpr&>(*Expr);
						if (Node.Target && Node.Target->Kind == EExprKind::Identifier)
						{
							const std::string& Name =
							    static_cast<const FIdentifierExpr&>(*Node.Target).Name;
							if (GlobalNames.find(Name) != GlobalNames.end() && !IsLocalInScopes(Name, Scopes))
							{
								OutNames.push_back(Name);
							}
						}
						CollectAssignedGlobalsExpr(Node.Target.get(), Scopes, OutNames);
						CollectAssignedGlobalsExpr(Node.Value.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::Conditional:
					{
						const auto& Node = static_cast<const FConditionalExpr&>(*Expr);
						CollectAssignedGlobalsExpr(Node.Condition.get(), Scopes, OutNames);
						CollectAssignedGlobalsExpr(Node.ThenExpr.get(), Scopes, OutNames);
						CollectAssignedGlobalsExpr(Node.ElseExpr.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::Ref:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FRefExpr&>(*Expr).Operand.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::Call:
					{
						const auto& Node = static_cast<const FCallExpr&>(*Expr);
						CollectAssignedGlobalsExpr(Node.CalleeExpr.get(), Scopes, OutNames);
						for (const FExpr* Arg : Node.GetArguments())
						{
							CollectAssignedGlobalsExpr(Arg, Scopes, OutNames);
						}
						return;
					}
				case EExprKind::MethodCall:
					{
						const auto& Node = static_cast<const FMethodCallExpr&>(*Expr);
						CollectAssignedGlobalsExpr(Node.Object.get(), Scopes, OutNames);
						for (const FExpr* Arg : Node.GetArguments())
						{
							CollectAssignedGlobalsExpr(Arg, Scopes, OutNames);
						}
						return;
					}
				case EExprKind::MemberAccess:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FMemberAccessExpr&>(*Expr).Object.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::IndexAccess:
					{
						const auto& Node = static_cast<const FIndexAccessExpr&>(*Expr);
						CollectAssignedGlobalsExpr(Node.Object.get(), Scopes, OutNames);
						CollectAssignedGlobalsExpr(Node.Index.get(), Scopes, OutNames);
						return;
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Item : static_cast<const FArrayInitExpr&>(*Expr).Elements)
						{
							CollectAssignedGlobalsExpr(Item.get(), Scopes, OutNames);
						}
						return;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(*Expr).Entries)
						{
							CollectAssignedGlobalsExpr(Entry.Key.get(), Scopes, OutNames);
							CollectAssignedGlobalsExpr(Entry.Value.get(), Scopes, OutNames);
						}
						return;
					}
				case EExprKind::Lambda:
					return;
				default:
					return;
				}
			}

			void CollectAssignedGlobalsBlock(
			    const FBlockStmt& Block,
			    std::vector<std::unordered_set<std::string>>& Scopes,
			    std::vector<std::string>& OutNames,
			    const bool bPushScope) const
			{
				if (bPushScope)
				{
					Scopes.emplace_back();
				}
				for (const auto& Child : Block.Statements)
				{
					if (Child)
					{
						CollectAssignedGlobalsStmt(*Child, Scopes, OutNames);
					}
				}
				if (bPushScope)
				{
					Scopes.pop_back();
				}
			}

			void CollectAssignedGlobalsStmt(
			    const FStmt& Stmt,
			    std::vector<std::unordered_set<std::string>>& Scopes,
			    std::vector<std::string>& OutNames) const
			{
				switch (Stmt.Kind)
				{
				case EStmtKind::Expr:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FExprStmt&>(Stmt).Expression.get(), Scopes, OutNames);
						return;
					}
				case EStmtKind::Block:
					{
						CollectAssignedGlobalsBlock(
						    static_cast<const FBlockStmt&>(Stmt), Scopes, OutNames, true);
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(Stmt).Declarations)
						{
							if (Declaration)
							{
								CollectAssignedGlobalsStmt(*Declaration, Scopes, OutNames);
							}
						}
						return;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Node = static_cast<const FVarDeclStmt&>(Stmt);
						CollectAssignedGlobalsExpr(Node.Initializer.get(), Scopes, OutNames);
						Scopes.back().insert(Node.Name);
						return;
					}
				case EStmtKind::Return:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FReturnStmt&>(Stmt).Value.get(), Scopes, OutNames);
						return;
					}
				case EStmtKind::If:
					{
						const auto& Node = static_cast<const FIfStmt&>(Stmt);
						CollectAssignedGlobalsExpr(Node.Condition.get(), Scopes, OutNames);
						if (Node.ThenBranch)
						{
							CollectAssignedGlobalsBlock(
							    *Node.ThenBranch, Scopes, OutNames, true);
						}
						if (Node.ElseBranch)
						{
							CollectAssignedGlobalsBlock(
							    *Node.ElseBranch, Scopes, OutNames, true);
						}
						return;
					}
				case EStmtKind::While:
					{
						const auto& Node = static_cast<const FWhileStmt&>(Stmt);
						CollectAssignedGlobalsExpr(Node.Condition.get(), Scopes, OutNames);
						if (Node.Body)
						{
							CollectAssignedGlobalsBlock(*Node.Body, Scopes, OutNames, true);
						}
						return;
					}
				case EStmtKind::For:
					{
						const auto& Node = static_cast<const FForStmt&>(Stmt);
						Scopes.emplace_back();
						if (Node.Init)
						{
							CollectAssignedGlobalsStmt(*Node.Init, Scopes, OutNames);
						}
						CollectAssignedGlobalsExpr(Node.Condition.get(), Scopes, OutNames);
						if (Node.Body)
						{
							CollectAssignedGlobalsBlock(*Node.Body, Scopes, OutNames, true);
						}
						CollectAssignedGlobalsExpr(Node.Step.get(), Scopes, OutNames);
						Scopes.pop_back();
						return;
					}
				case EStmtKind::ForEach:
					{
						const auto& Node = static_cast<const FForEachStmt&>(Stmt);
						CollectAssignedGlobalsExpr(Node.Collection.get(), Scopes, OutNames);
						Scopes.emplace_back();
						Scopes.back().insert(Node.ElementName);
						if (Node.bHasKeyBinding)
						{
							Scopes.back().insert(Node.KeyName);
						}
						if (Node.Body)
						{
							CollectAssignedGlobalsBlock(*Node.Body, Scopes, OutNames, false);
						}
						Scopes.pop_back();
						return;
					}
				case EStmtKind::Try:
					{
						const auto& Node = static_cast<const FTryStmt&>(Stmt);
						if (Node.TryBlock)
						{
							CollectAssignedGlobalsBlock(*Node.TryBlock, Scopes, OutNames, true);
						}
						for (const FCatchClause& Catch : Node.Catches)
						{
							Scopes.emplace_back();
							if (!Catch.Name.empty())
							{
								Scopes.back().insert(Catch.Name);
							}
							if (Catch.Body)
							{
								CollectAssignedGlobalsBlock(*Catch.Body, Scopes, OutNames, false);
							}
							Scopes.pop_back();
						}
						if (Node.FinallyBlock)
						{
							CollectAssignedGlobalsBlock(
							    *Node.FinallyBlock, Scopes, OutNames, true);
						}
						return;
					}
				case EStmtKind::Throw:
					{
						CollectAssignedGlobalsExpr(
						    static_cast<const FThrowStmt&>(Stmt).Value.get(), Scopes, OutNames);
						return;
					}
				case EStmtKind::Switch:
					{
						const auto& Node = static_cast<const FSwitchStmt&>(Stmt);
						CollectAssignedGlobalsExpr(Node.Discriminant.get(), Scopes, OutNames);
						for (const FSwitchCase& Case : Node.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								CollectAssignedGlobalsExpr(Label.get(), Scopes, OutNames);
							}
							if (Case.Body)
							{
								CollectAssignedGlobalsBlock(*Case.Body, Scopes, OutNames, true);
							}
						}
						return;
					}
				default:
					return;
				}
			}

			std::string EmitFunctionValue(
			    const FFunctionDecl& Function, const FSourceLocation& Location)
			{
				if (std::any_of(Function.Parameters.begin(), Function.Parameters.end(),
				        [](const FFunctionParam& Parameter)
				{
					return Parameter.bIsVariadic;
				}))
				{
					Fail("variadic function values are not supported yet", Location);
					return "None";
				}
				return FunctionPythonName(Function);
			}

			std::string EmitIdentifier(
			    const std::string& Name,
			    const FSourceLocation* const Location = nullptr)
			{
				const auto LocalIt = LocalPythonNames.find(Name);
				if (LocalIt != LocalPythonNames.end())
				{
					return LocalIt->second;
				}
				const auto EnumIt = UnscopedEnumMembers.find(Name);
				if (EnumIt != UnscopedEnumMembers.end())
				{
					return EnumIt->second;
				}
				if (!bInStaticMethod && !CurrentTypeName.empty() && LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) != CurrentFields.end())
				{
					return "self." + PythonIdentifier(Name);
				}
				if (!CurrentTypeName.empty() && LocalNames.find(Name) == LocalNames.end() && CurrentStaticFields.find(Name) != CurrentStaticFields.end())
				{
					std::unordered_set<std::string> Visited;
					const std::string Owner = FindStaticFieldOwner(CurrentTypeName, Name, Visited);
					return "_vs_static_get(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Name)) + ")";
				}
				if (LocalNames.find(Name) == LocalNames.end())
				{
					if (const FVarDeclStmt* Global = ResolveGlobal(Name, Location))
					{
						return "_vs_global_get(" + QuotePython(GlobalStorageName(*Global)) + ")";
					}
					if (const FFunctionDecl* Function = ResolveFunction(Name, Location))
					{
						return EmitFunctionValue(*Function, Location ? *Location : Function->Location);
					}
					if (const std::string Type = ResolveTypeSymbol(Name, Location); !Type.empty())
					{
						return PythonIdentifier(Type);
					}
				}
				return PythonIdentifier(Name);
			}

			std::string EmitArguments(const FExprArgumentView Args)
			{
				std::ostringstream Text;
				for (std::size_t Index = 0; Index < Args.size(); ++Index)
				{
					if (Index)
					{
						Text << ", ";
					}
					if (Args[Index])
					{
						Text << EmitExpr(*Args[Index]);
					}
				}
				return Text.str();
			}

			const FFunctionDecl* FindFunctionDecl(const std::string& Name) const
			{
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if (Function.Name == Name && !Function.bDeleted && Function.Body)
					{
						return &Function;
					}
				}
				return nullptr;
			}

			bool BindGenericTypePattern(
			    const std::string& Pattern,
			    const std::string& Actual,
			    const std::unordered_set<std::string>& Parameters,
			    std::unordered_map<std::string, std::string>& Bindings) const
			{
				const std::string ResolvedPattern = ResolveAlias(Pattern);
				const std::string ResolvedActual = ResolveAlias(Actual);
				const std::string PatternBase = BaseType(ResolvedPattern);
				if (Parameters.find(PatternBase) != Parameters.end() && ResolvedPattern.find('<') == std::string::npos)
				{
					if (ResolvedActual.empty())
					{
						return false;
					}
					const auto Existing = Bindings.find(PatternBase);
					if (Existing == Bindings.end())
					{
						Bindings.emplace(PatternBase, ResolvedActual);
						return true;
					}
					return Existing->second == ResolvedActual;
				}
				if (PatternBase != BaseType(ResolvedActual))
				{
					return false;
				}
				const std::vector<std::string> PatternArgs = TypeArguments(ResolvedPattern);
				const std::vector<std::string> ActualArgs = TypeArguments(ResolvedActual);
				if (PatternArgs.empty())
				{
					return true;
				}
				if (PatternArgs.size() != ActualArgs.size())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < PatternArgs.size(); ++Index)
				{
					if (!BindGenericTypePattern(
					        PatternArgs[Index], ActualArgs[Index], Parameters, Bindings))
					{
						return false;
					}
				}
				return true;
			}

			static std::string ApplyGenericTypeBindings(
			    const std::string& TypeName,
			    const std::unordered_map<std::string, std::string>& Bindings)
			{
				std::string Result;
				for (std::size_t Index = 0; Index < TypeName.size();)
				{
					const unsigned char Character = static_cast<unsigned char>(TypeName[Index]);
					if (std::isalpha(Character) || Character == '_')
					{
						std::size_t End = Index + 1;
						while (End < TypeName.size())
						{
							const unsigned char Next = static_cast<unsigned char>(TypeName[End]);
							if (!std::isalnum(Next) && Next != '_')
							{
								break;
							}
							++End;
						}
						const std::string Token = TypeName.substr(Index, End - Index);
						const auto Found = Bindings.find(Token);
						Result += Found == Bindings.end() ? Token : Found->second;
						Index = End;
						continue;
					}
					Result.push_back(TypeName[Index++]);
				}
				return Result;
			}

			std::string InferGenericCallReturnType(const FCallExpr& Call) const
			{
				const FFunctionDecl* Function = ResolveFunction(Call.Callee, &Call.Location);
				if (!Function)
				{
					return {};
				}
				if (Function->TypeParams.empty())
				{
					return Function->ReturnType;
				}
				std::unordered_map<std::string, std::string> Bindings;
				if (!Call.TypeArgs.empty())
				{
					for (std::size_t Index = 0;
					    Index < Call.TypeArgs.size() && Index < Function->TypeParams.size(); ++Index)
					{
						Bindings[Function->TypeParams[Index]] = Call.TypeArgs[Index];
					}
				}
				else
				{
					const std::unordered_set<std::string> Parameters(
					    Function->TypeParams.begin(), Function->TypeParams.end());
					const FExprArgumentView Args = Call.GetArguments();
					const std::size_t Count = std::min(Args.size(), Function->Parameters.size());
					for (std::size_t Index = 0; Index < Count; ++Index)
					{
						if (Args[Index])
						{
							(void)BindGenericTypePattern(
							    Function->Parameters[Index].TypeName,
							    InferExprType(*Args[Index]), Parameters, Bindings);
						}
					}
				}
				for (std::size_t Index = 0; Index < Function->TypeParams.size(); ++Index)
				{
					const std::string& Parameter = Function->TypeParams[Index];
					if (Bindings.find(Parameter) == Bindings.end() && Index < Function->TypeParamDefaults.size() && !Function->TypeParamDefaults[Index].empty())
					{
						Bindings[Parameter] = ApplyGenericTypeBindings(
						    Function->TypeParamDefaults[Index], Bindings);
					}
				}
				return ApplyGenericTypeBindings(Function->ReturnType, Bindings);
			}

			std::string EmitGenericCallBindings(
			    const FCallExpr& Call,
			    const FExprArgumentView Args,
			    const FFunctionDecl* const ResolvedFunction = nullptr)
			{
				const FFunctionDecl* Function = ResolvedFunction != nullptr
				    ? ResolvedFunction
				    : ResolveFunction(Call.Callee, &Call.Location);
				if (!Function || Function->TypeParams.empty())
				{
					return {};
				}
				std::unordered_map<std::string, std::string> Bindings;
				if (!Call.TypeArgs.empty())
				{
					if (Call.TypeArgs.size() > Function->TypeParams.size())
					{
						Fail("generic call has too many type arguments", Call.Location);
						return {};
					}
					for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
					{
						Bindings[Function->TypeParams[Index]] = Call.TypeArgs[Index];
					}
				}
				else
				{
					const std::unordered_set<std::string> Parameters(
					    Function->TypeParams.begin(), Function->TypeParams.end());
					const std::size_t Count = std::min(Args.size(), Function->Parameters.size());
					for (std::size_t Index = 0; Index < Count; ++Index)
					{
						if (Args[Index])
						{
							(void)BindGenericTypePattern(
							    Function->Parameters[Index].TypeName,
							    InferExprType(*Args[Index]), Parameters, Bindings);
						}
					}
				}
				for (std::size_t Index = 0; Index < Function->TypeParams.size(); ++Index)
				{
					const std::string& Parameter = Function->TypeParams[Index];
					if (Bindings.find(Parameter) != Bindings.end())
					{
						continue;
					}
					if (Index < Function->TypeParamDefaults.size() && !Function->TypeParamDefaults[Index].empty())
					{
						Bindings[Parameter] = ApplyGenericTypeBindings(
						    Function->TypeParamDefaults[Index], Bindings);
						continue;
					}
					Fail("cannot resolve generic type argument '" + Parameter + "' for function '" + Function->Name + "'", Call.Location);
					return {};
				}
				std::ostringstream Result;
				Result << (Args.size() == 0 ? "" : ", ") << "___vs_type_args={";
				for (std::size_t Index = 0; Index < Function->TypeParams.size(); ++Index)
				{
					if (Index)
					{
						Result << ", ";
					}
					const std::string& Parameter = Function->TypeParams[Index];
					Result << QuotePython(Parameter) << ": _vs_resolve_type_name("
					       << QuotePython(Bindings.at(Parameter)) << ')';
				}
				Result << '}';
				return Result.str();
			}

			const FMethodDecl* FindMethodDecl(
			    const std::string& TypeName,
			    const std::string& MethodName,
			    std::string& OutOwner,
			    std::unordered_set<std::string>& Visited) const
			{
				const std::string Base = BaseType(ResolveAlias(TypeName));
				if (Base.empty() || !Visited.insert(Base).second)
				{
					return nullptr;
				}
				auto FindDeclared = [&](const auto& Decl) -> const FMethodDecl*
				{
					for (const FMethodDecl& Method : Decl.Methods)
					{
						if (!Method.bDeleted && Method.Name == MethodName)
						{
							OutOwner = Decl.Name;
							return &Method;
						}
					}
					return nullptr;
				};
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (Struct.Name != Base)
					{
						continue;
					}
					if (const FMethodDecl* Method = FindDeclared(Struct))
					{
						return Method;
					}
					if (!Struct.BaseTypeName.empty())
					{
						if (const FMethodDecl* Method = FindMethodDecl(
						        Struct.BaseTypeName, MethodName, OutOwner, Visited))
						{
							return Method;
						}
					}
					for (const std::string& Interface : Struct.InterfaceTypeNames)
					{
						if (const FMethodDecl* Method = FindMethodDecl(
						        Interface, MethodName, OutOwner, Visited))
						{
							return Method;
						}
					}
					return nullptr;
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (Class.Name != Base)
					{
						continue;
					}
					if (const FMethodDecl* Method = FindDeclared(Class))
					{
						return Method;
					}
					for (const std::string& Parent : Class.BaseTypeNames)
					{
						if (const FMethodDecl* Method = FindMethodDecl(
						        Parent, MethodName, OutOwner, Visited))
						{
							return Method;
						}
					}
					for (const std::string& Interface : Class.InterfaceTypeNames)
					{
						if (const FMethodDecl* Method = FindMethodDecl(
						        Interface, MethodName, OutOwner, Visited))
						{
							return Method;
						}
					}
					return nullptr;
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					if (Interface.Name != Base)
					{
						continue;
					}
					if (const FMethodDecl* Method = FindDeclared(Interface))
					{
						return Method;
					}
					for (const std::string& Parent : Interface.BaseInterfaceNames)
					{
						if (const FMethodDecl* Method = FindMethodDecl(
						        Parent, MethodName, OutOwner, Visited))
						{
							return Method;
						}
					}
					return nullptr;
				}
				return nullptr;
			}

			std::string EmitResolvedMethodCall(
			    const std::string& ReceiverType,
			    const std::string& Object,
			    const std::string& MethodName,
			    const std::string& Arguments) const
			{
				if (HasStandardAttribute() && BaseType(ResolveAlias(ReceiverType)) == "attribute")
				{
					return Object + "." + PythonIdentifier(MethodName) + "(" + Arguments + ")";
				}

				std::string Owner;
				std::unordered_set<std::string> Visited;
				const FMethodDecl* Method = FindMethodDecl(
				    ReceiverType, MethodName, Owner, Visited);
				if (!Method)
				{
					return Object + "." + PythonIdentifier(MethodName) + "(" + Arguments + ")";
				}
				if (Method->bIsStatic)
				{
					const std::string Function = PythonIdentifier(Owner) + "." + PythonIdentifier(MethodName);
					if (Object == PythonIdentifier(Owner))
					{
						return Function + "(" + Arguments + ")";
					}
					return "_vs_static_call(" + Object + ", " + Function + (Arguments.empty() ? std::string{} : ", " + Arguments) + ")";
				}
				if (!Method->bVirtual && !Method->bOverride && !Method->bPureVirtual)
				{
					return PythonIdentifier(Owner) + "." + PythonIdentifier(MethodName) + "(" + Object + (Arguments.empty() ? std::string{} : ", " + Arguments) + ")";
				}
				return Object + "." + PythonIdentifier(MethodName) + "(" + Arguments + ")";
			}

			std::string EmitCall(const FCallExpr& Call)
			{
				if (Call.Callee == "__numeric_type" && Call.TypeArgs.size() == 1) return "_vs_numeric_type(" + QuotePython(Call.TypeArgs[0]) + ")";
				const FExprArgumentView Args = Call.GetArguments();
				if (Call.bStandardLibraryQualified && (Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !Args.empty() && ResolveAlias(InferExprType(*Args[0])) != "string")
				{
					Fail("callable task entry is not supported by Python AOT",
					    Call.Location);
					return "None";
				}
				const std::string Arguments = EmitArguments(Args);
				const auto Arg = [&](const std::size_t Index)
				{
					return Index < Args.size() && Args[Index]
					    ? EmitExpr(*Args[Index])
					    : std::string{"None"};
				};
				if (Call.CalleeExpr)
				{
					return EmitExpr(*Call.CalleeExpr) + "(" + Arguments + ")";
				}
				if (const FVarDeclStmt* CallableGlobal = ResolveGlobal(
				        Call.Callee, &Call.Location))
				{
					if (!FunctionValueReturnType(ResolveAlias(CallableGlobal->TypeName)).empty())
					{
						return "_vs_global_get(" + QuotePython(GlobalStorageName(*CallableGlobal)) + ")(" + Arguments + ")";
					}
				}
				const FFunctionDecl* const ResolvedFunction =
				    ResolveFunction(Call.Callee, &Call.Location);
				const std::string Name = ResolvedFunction != nullptr
				    ? ResolvedFunction->Name
				    : Call.Callee;
				if (!Call.TypeArgs.empty() && Call.Callee != "cast" && Call.Callee != "try_cast" && Call.Callee != "const_cast" && ResolvedFunction == nullptr && !FindMethodBindingCodegenInfo(MethodBindings, Call.Location) && TypeNames.find(BaseType(ResolveAlias(Call.Callee))) == TypeNames.end() && BaseType(ResolveAlias(Call.Callee)) != "channel")
				{
					Fail("explicit generic calls are not supported yet", Call.Location);
					return "None";
				}
				if (IsMethodBindingFactoryCall(Call) || IsWeakAttributeFactoryCall(Call))
				{
					const auto* Info = FindMethodBindingCodegenInfo(MethodBindings, Call.Location);
					if (Name == "__std_make_weak_attribute" || Name == "__std_make_weak_attribute_by_name" ||
					    !Info || !Info->bConstantName || Info->Selection.bHost)
					{
						Fail(
						    "Python AOT: weak, dynamic or host method binding requires the VeroScript runtime",
						    Call.Location);
						return "None";
					}
					std::vector<std::string> Parameters;
					std::string Return;
					Runtime::ParseFunctionType(Info->FunctionType, Parameters, Return);
					if (Runtime::IsReferenceTypeName(Return) ||
					    std::any_of(Parameters.begin(), Parameters.end(), Runtime::IsReferenceTypeName))
					{
						Fail("Python AOT: reference method binding requires the VeroScript runtime",
						    Call.Location);
						return "None";
					}
					std::string Names;
					for (std::size_t I = 0; I < Parameters.size(); ++I)
					{
						if (I)
						{
							Names += ",";
						}
						Names += "___vs_arg" + std::to_string(I);
					}
					std::string ReceiverType = InferExprType(*Args[0]);
					if (Call.TypeArgs.size() == 2)
					{
						ReceiverType = Call.TypeArgs[1];
					}

					const std::string Body = EmitResolvedMethodCall(
					    ReceiverType, "___vs_target", Info->Selection.MethodName, Names);
					const std::string Bound = "(lambda ___vs_target: (lambda " + Names + ": " + Body +
					    "))(_vs_binding_target(" + EmitExpr(*Args[0]) + "))";
					if (Info->FactoryName == "__std_bind_method") return Bound;
					return "_vs_bound_attribute(" + Bound + ", " +
					    QuotePython("attribute<" + Call.TypeArgs[0] + ">") + ")";
				}
				std::string CallBindings = EmitGenericCallBindings(Call, Args, ResolvedFunction);
				if (bFailed)
				{
					return "None";
				}
				if (Name == "__std_spawn")
				{
					const std::string CallerModule = CurrentFunctionModulePath.empty()
					    ? (Program.EntryModulePath.empty() ? "local" : Program.EntryModulePath)
					    : CurrentFunctionModulePath;
					CallBindings += (Arguments.empty() && CallBindings.empty() ? "" : ", ") + std::string{"___vs_caller_module="} + QuotePython(CallerModule);
				}
				if ((Name == "cast" || Name == "try_cast" || Name == "const_cast") && Call.TypeArgs.size() == 1)
				{
					if (Name == "try_cast")
					{
						return "_vs_try_cast(" + QuotePython(Call.TypeArgs[0]) + ", " + Arguments + ")";
					}
					return "_vs_checked_cast(" + QuotePython(Call.TypeArgs[0]) + ", " + Arguments + ")";
				}
				const auto MethodIt = CurrentMethodReturnTypes.find(Name);
				if (!CurrentTypeName.empty() && MethodIt != CurrentMethodReturnTypes.end())
				{
					return EmitResolvedMethodCall(
					    CurrentTypeName,
					    bInStaticMethod ? PythonIdentifier(CurrentTypeName) : "self",
					    Name,
					    Arguments);
				}
				// Source-defined functions take precedence over builtin spellings. This
				// preserves legal user shadowing (for example a local `now()`), while
				// explicit std aliases whose canonical names are `__std_*` still call
				// their imported source wrappers instead of being mistaken for raw
				// implementation intrinsics.
				const bool bUserShadowsNamespaceOnlyBuiltin =
				    !Call.bStandardLibraryQualified && !StandardLibrary::RequiredNamespaceForFunction(Name).empty();
				if (ResolvedFunction != nullptr && (bUserShadowsNamespaceOnlyBuiltin || Name.rfind("__std_", 0) == 0))
				{
					return FunctionPythonName(*ResolvedFunction) + "(" + Arguments + CallBindings + ")";
				}
				if (Name == "print")
				{
					return "_vs_print(" + Arguments + ")";
				}
				if (Name == "eprint")
				{
					return "_vs_eprint(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_write")
				{
					return "_vs_console_write(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_error")
				{
					return "_vs_console_write(" + Arguments + ", True)";
				}
				if (Call.bStandardLibraryQualified && Name == "console_flush")
				{
					return "_vs_sys.stdout.flush()";
				}
				if (Call.bStandardLibraryQualified && Name == "console_clear")
				{
					return "_vs_console_clear()";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_color")
				{
					return "_vs_console_supports_color(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_cursor")
				{
					return "_vs_console_supports_cursor(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_supports_clear")
				{
					return "_vs_console_supports_clear(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_is_output_terminal")
				{
					return "_vs_console_is_output_terminal(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_set_color")
				{
					return "_vs_console_set_color(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_reset_style")
				{
					return "_vs_console_reset_style(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_clear_line")
				{
					return "_vs_console_clear_line(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_move_cursor")
				{
					return "_vs_console_move_cursor(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_set_cursor_visible")
				{
					return "_vs_console_set_cursor_visible(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_width")
				{
					return "_vs_console_width(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_height")
				{
					return "_vs_console_height(" + Arguments + ")";
				}
				if (Call.bStandardLibraryQualified && Name == "console_is_interactive")
				{
					return "_vs_console_is_interactive()";
				}
				if (Call.bStandardLibraryQualified && Name.rfind("console_", 0) == 0)
				{
					Fail("unknown console builtin '" + Name + "' is not supported in standalone Python output", Call.Location);
					return "None";
				}
				if (Name == "len" || Name == "__array_prepare_sort")
				{
					const std::string ArgType = Args.size() == 1 && Args[0]
					    ? BaseType(ResolveAlias(InferExprType(*Args[0])))
					    : std::string{};
					return (ArgType == "wstring" ? "_vs_wlen(" : "_vs_len(") + Arguments + ")";
				}
				if (Name == "assert")
				{
					return "_vs_assert(" + Arguments + ")";
				}
				if (Name == "format" || Name == "__format")
				{
					return "_vs_format(" + Arguments + ")";
				}
				if (Name == "formatf" || Name == "__format_printf")
				{
					return "_vs_printf(" + Arguments + ")";
				}
				if (Name == "printf")
				{
					return "_vs_print(_vs_printf(" + Arguments + "))";
				}
				if (Name == "abs" || Name == "min" || Name == "max")
				{
					const std::string Function = Name == "abs" ? "abs" : "_vs_" + Name;
					return CastTo(InferExprType(Call), Function + "(" + Arguments + ")");
				}
				if (Name == "exit")
				{
					return "(_ for _ in ()).throw(_VsExit(" + Arguments + "))";
				}
				if (Name == "range")
				{
					return "_vs_range(" + Arguments + ")";
				}
				if (Name == "now")
				{
					return "_vs_time.time()";
				}
				if (Name == "steady_now")
				{
					return "_vs_time.monotonic()";
				}
				if (Name == "sleep")
				{
					return "_vs_time.sleep((" + Arguments + ") / 1000.0)";
				}
				if (Name == "getenv")
				{
					return "_vs_os.environ.get(" + Arguments + ", \"\")";
				}
				if (Name == "setenv")
				{
					return "_vs_setenv(" + Arguments + ")";
				}
				if (Name == "to_string" || Name == "to_wstring")
				{
					return "_vs_text(" + Arguments + ")";
				}
				if (Name == "to_int")
				{
					return "_vs_cast(\"int\", " + Arguments + ")";
				}
				if (Name == "to_int64")
				{
					return "_vs_cast(\"int64\", " + Arguments + ")";
				}
				if (Name == "to_unsigned")
				{
					return "_vs_cast(\"unsigned\", " + Arguments + ")";
				}
				if (Name == "to_uint64")
				{
					return "_vs_cast(\"uint64\", " + Arguments + ")";
				}
				if (Name == "__to_int8")
				{
					return "_vs_cast(\"int8\", " + Arguments + ")";
				}
				if (Name == "__to_uint8")
				{
					return "_vs_cast(\"uint8\", " + Arguments + ")";
				}
				if (Name == "__to_int16")
				{
					return "_vs_cast(\"int16\", " + Arguments + ")";
				}
				if (Name == "__to_uint16")
				{
					return "_vs_cast(\"uint16\", " + Arguments + ")";
				}
				if (Name == "to_double")
				{
					return "_vs_cast(\"double\", " + Arguments + ")";
				}
				if (Name == "to_float")
				{
					return "_vs_cast(\"float\", " + Arguments + ")";
				}
				if (Name == "__sqrt")
				{
					return "_vs_math_unary(\"sqrt\", " + Arguments + ")";
				}
				if (Name == "__sin")
				{
					return "_vs_math_unary(\"sin\", " + Arguments + ")";
				}
				if (Name == "__cos")
				{
					return "_vs_math_unary(\"cos\", " + Arguments + ")";
				}
				if (Name == "__tan")
				{
					return "_vs_math_unary(\"tan\", " + Arguments + ")";
				}
				if (Name == "__floor")
				{
					return "_vs_math_unary(\"floor\", " + Arguments + ")";
				}
				if (Name == "__ceil")
				{
					return "_vs_math_unary(\"ceil\", " + Arguments + ")";
				}
				if (Name == "__round")
				{
					return "_vs_round(" + Arguments + ")";
				}
				if (Name == "__trunc")
				{
					return "_vs_math_unary(\"trunc\", " + Arguments + ")";
				}
				if (Name == "__pow")
				{
					return "_vs_math_binary(\"pow\", " + Arguments + ")";
				}
				if (Name == "__exp")
				{
					return "_vs_math_unary(\"exp\", " + Arguments + ")";
				}
				if (Name == "__log")
				{
					return "_vs_math_unary(\"log\", " + Arguments + ")";
				}
				if (Name == "__log2")
				{
					return "_vs_math_unary(\"log2\", " + Arguments + ")";
				}
				if (Name == "__asin")
				{
					return "_vs_math_unary(\"asin\", " + Arguments + ")";
				}
				if (Name == "__acos")
				{
					return "_vs_math_unary(\"acos\", " + Arguments + ")";
				}
				if (Name == "__atan2")
				{
					return "_vs_math_binary(\"atan2\", " + Arguments + ")";
				}
				if (Name == "__isfinite")
				{
					return "_vs_math.isfinite(float(" + Arguments + "))";
				}
				if (Name == "__isnan")
				{
					return "_vs_math.isnan(float(" + Arguments + "))";
				}
				if (Name == "__substr")
				{
					return "_vs_substr(" + Arguments + ")";
				}
				if (Name == "__string_starts_with") return "_vs_string_starts(" + Arguments + ")";
				if (Name == "__string_ends_with") return "_vs_string_ends(" + Arguments + ")";
				if (Name == "__string_last_find") return "_vs_string_last_find(" + Arguments + ")";
				if (Name == "__string_repeat") return "_vs_string_repeat(" + Arguments + ")";
				if (Name == "__string_replace") return "_vs_string_replace(" + Arguments + ")";
				if (Name == "__string_trim") return "_vs_string_trim(" + Arguments + ")";
				if (Name == "__array_reverse") return "_vs_array_reverse(" + Arguments + ")";
				if (Name == "__array_rotate") return "_vs_array_rotate(" + Arguments + ")";
				if (Name == "__array_shuffle") return "_vs_array_shuffle(" + Arguments + ")";
				if (Name == "__random_next_uint") return "_vs_random_next(" + Arguments + ")";
				if (Name == "__random_index") return "_vs_random_index(" + Arguments + ")";
				if (Name == "__find")
				{
					return "_vs_find(" + Arguments + ")";
				}
				if (Name == "__char_code")
				{
					return "_vs_char_code(" + Arguments + ")";
				}
				if (Name == "__split")
				{
					return "_vs_split(" + Arguments + ")";
				}
				if (Name == "__join")
				{
					return "_vs_join(" + Arguments + ")";
				}
				if (Name == "__to_lower")
				{
					return "_vs_ascii_case(" + Arguments + ", False)";
				}
				if (Name == "__to_upper")
				{
					return "_vs_ascii_case(" + Arguments + ", True)";
				}
				if (Name == "__read_line" || Name == "read_line")
				{
					return "_vs_read_line()";
				}
				if (Name == "__read_string" || Name == "read_string" || Name == "__read_token" || Name == "read_token")
				{
					return "_vs_read_token()";
				}
				if (Name == "__read_int" || Name == "read_int")
				{
					return "_vs_read_int()";
				}
				if (Name == "__read_double" || Name == "read_double")
				{
					return "_vs_read_double()";
				}
				if (Name == "__read_bool" || Name == "read_bool")
				{
					return "_vs_read_bool()";
				}
				if (Name == "__read_file")
				{
					return "_vs_read_file(" + Arguments + ")";
				}
				if (Name == "__write_file")
				{
					return "_vs_write_file(" + Arguments + ")";
				}
				if (Name == "__append_file")
				{
					return "_vs_append_file(" + Arguments + ")";
				}
				if (Name == "__exists")
				{
					return "_vs_exists(" + Arguments + ")";
				}
				if (Name == "__is_file")
				{
					return "_vs_is_file(" + Arguments + ")";
				}
				if (Name == "__is_directory")
				{
					return "_vs_is_directory(" + Arguments + ")";
				}
				if (Name == "__remove_dir")
				{
					return "_vs_remove_dir(" + Arguments + ")";
				}
				if (Name == "__delete_file")
				{
					return "_vs_delete_file(" + Arguments + ")";
				}
				if (Name == "__mkdir")
				{
					return "_vs_pathlib.Path(" + Arguments + ").mkdir(parents=True, exist_ok=True)";
				}
				if (Name == "__list_dir")
				{
					return "_vs_list_dir(" + Arguments + ")";
				}
				if (Name == "__cwd")
				{
					return "str(_vs_pathlib.Path.cwd())";
				}
				if (Name == "__chdir")
				{
					return "_vs_os.chdir(" + Arguments + ")";
				}
				if (Name == "__channel_send")
				{
					return Arg(0) + ".send(" + Arg(1) + ")";
				}
				if (Name == "__channel_recv")
				{
					return Arg(0) + ".recv()";
				}
				if (Name == "__channel_close")
				{
					return Arg(0) + ".close()";
				}
				if (Name == "__channel_try_send")
				{
					return Arg(0) + ".try_send(" + Arg(1) + ")";
				}
				if (Name == "__channel_send_for")
				{
					return Arg(0) + ".send_for(" + Arg(1) + ", " + Arg(2) + ")";
				}
				if (Name == "__channel_try_recv")
				{
					return Arg(0) + ".try_recv()";
				}
				if (Name == "__channel_recv_for")
				{
					return Arg(0) + ".recv_for(" + Arg(1) + ")";
				}
				if (Name == "__channel_is_closed")
				{
					return Arg(0) + ".is_closed()";
				}
				if (Name == "__channel_size")
				{
					return Arg(0) + ".size()";
				}
				if (Name == "__channel_capacity")
				{
					return Arg(0) + ".capacity()";
				}
				if (Name == "__channel_set_capacity")
				{
					return Arg(0) + ".set_capacity(" + Arg(1) + ")";
				}
				if (Name == "__spawn" || Name == "__spawn_result")
				{
					std::string CallerModuleExpression = QuotePython(
					    CurrentFunctionModulePath.empty() ? "local" : CurrentFunctionModulePath);
					if (CurrentFunctionName == "__std_spawn" && CurrentFunctionModulePath.find("concurrent.vs") != std::string::npos)
					{
						CallerModuleExpression = "___vs_caller_module";
					}
					return "_vs_spawn(_vs_resolve_worker(" + Arg(0) + ", " + CallerModuleExpression + ", " + Arg(1) + "), " + Arg(1) + ", " + (Name == "__spawn_result" ? "True" : "False") + ")";
				}
				if (Name == "__join_task")
				{
					return "_vs_join_task(" + Arg(0) + ")";
				}
				if (Name == "__join_task_for")
				{
					return "_vs_join_task_for(" + Arg(0) + ", " + Arg(1) + ")";
				}
				if (Name == "__task_done")
				{
					return "_vs_task_done(" + Arg(0) + ")";
				}
				if (Name == "__cancel_requested")
				{
					return "_vs_cancel_requested()";
				}
				if (Name == "__cancel_task")
				{
					return "_vs_cancel_task(" + Arg(0) + ")";
				}
				const std::string Base = BaseType(ResolveAlias(Name));
				const auto TypedConstructor = [&](const std::string& PythonType)
				{
					return PythonType + "(" + Arguments + (Arguments.empty() ? std::string{} : ", ") + "type_name=_vs_resolve_type_name(" + QuotePython(Name) + "))";
				};
				if (Base == "array")
				{
					return TypedConstructor("_VsArray");
				}
				if (Base == "map" || Base == "ordered_map")
				{
					return TypedConstructor("_VsMap");
				}
				if (Base == "set")
				{
					return TypedConstructor("_VsSet");
				}
				if (Base == "deque")
				{
					return TypedConstructor("_VsDeque");
				}
				if (Base == "queue")
				{
					return TypedConstructor("_VsQueue");
				}
				if (Base == "stack")
				{
					return TypedConstructor("_VsStack");
				}
				if (Base == "pair" && TypeArguments(ResolveAlias(Name)).size() == 2)
				{
					return TypedConstructor("_VsPair");
				}
				if (HasStandardAttribute() && Base == "attribute")
				{
					return "_VsAttribute(" + Arguments + (Arguments.empty() ? "" : ", ") +
					    "type_name=_vs_resolve_type_name(" + QuotePython(Name) + "))";
				}

				if (Base == "optional")
				{
					return "_VsOptional(" + Arguments + (Arguments.empty() ? std::string{} : ", ") + "type_name=_vs_resolve_type_name(" + QuotePython(Name) + "))";
				}
				if (Base == "channel")
				{
					std::string ChannelTypeName = Name;
					if (!Call.TypeArgs.empty())
					{
						ChannelTypeName += '<';
						for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
						{
							if (Index)
							{
								ChannelTypeName += ',';
							}
							ChannelTypeName += Call.TypeArgs[Index];
						}
						ChannelTypeName += '>';
					}
					return "_VsChannel(type_name=_vs_resolve_type_name(" + QuotePython(ChannelTypeName) + "))";
				}
				if (bHasStandardEvents && Base == "delegate")
				{
					return "_VsDelegate()";
				}
				if (bHasStandardEvents && Base == "multicast_delegate")
				{
					return "_VsMulticastDelegate()";
				}
				if (TypeNames.find(Base) != TypeNames.end())
				{
					return PythonIdentifier(Base) + "(" + Arguments + ")";
				}
				if (Base == "int" || Base == "int64" || Base == "uint64" || Base == "unsigned" || Base == "float" || Base == "double" || Base == "bool" || Base == "string")
				{
					const std::string Value = Arguments.empty() ? "_vs_default(" + QuotePython(Base) + ")" : Arguments;
					return "_vs_cast(" + QuotePython(Base) + ", " + Value + ")";
				}
				if (EnumNames.find(Base) != EnumNames.end())
				{
					const std::string Value = Arguments.empty()
					    ? "_vs_default(" + QuotePython(Name) + ")"
					    : Arguments;
					return "_vs_cast(" + QuotePython(Name) + ", " + Value + ")";
				}
				static const std::unordered_set<std::string> UnsupportedBuiltins = {
				    "__weak_create",
				    "__weak_lock",
				    "__weak_expired",
				    "__weak_reset",
				    "__reflection_string",
				    "__reflection_int",
				    "__reflection_bool",
				    "__reflection_int64",
				    "spawn",
				    "join_task",
				};
				if (UnsupportedBuiltins.find(Name) != UnsupportedBuiltins.end() || Name.rfind("__", 0) == 0)
				{
					Fail("builtin '" + Name + "' is not supported yet", Call.Location);
					return "None";
				}
				return (ResolvedFunction != nullptr
				               ? FunctionPythonName(*ResolvedFunction)
				               : PythonIdentifier(Name)) +
				    "(" + Arguments + CallBindings + ")";
			}

			std::string EmitMethodCall(const FMethodCallExpr& Call)
			{
				if (Call.TypeArgs.size() == 1 && (Call.Method == "get" || Call.Method == "set" || Call.Method == "invoke" || Call.Method == "invoke_static"))
				{
					Fail("executable reflection is not supported by Python AOT", Call.Location);
					return "None";
				}
				const FExprArgumentView Args = Call.GetArguments();
				if (HasStandardAttribute() && Call.Object &&
				    BaseType(ResolveAlias(InferExprType(*Call.Object))) == "attribute" &&
				    Call.Method == "try_get" && Args.size() == 1)
				{
					Fail("writable ref attribute reads are not supported by Python AOT", Call.Location);
					return "None";
				}
				if (HasStandardAttribute() && Call.Object &&
				    BaseType(ResolveAlias(InferExprType(*Call.Object))) == "attribute" &&
				    Call.Method == "has_same_configuration" && Args.size() == 1 &&
				    Args[0]->Kind == EExprKind::Ref)
				{
					const auto& Operand = *static_cast<const FRefExpr&>(*Args[0]).Operand;
					return EmitExpr(*Call.Object) + ".has_same_configuration(" + EmitExpr(Operand) + ")";
				}
				const std::string Arguments = EmitArguments(Args);
				std::string NamespaceName;
				if (Call.Object && TryBuildNamespaceMemberName(*Call.Object, Call.Method, NamespaceName))
				{
					if (const FVarDeclStmt* CallableGlobal = ResolveGlobal(
					        NamespaceName, &Call.Location))
					{
						if (!FunctionValueReturnType(
						        ResolveAlias(CallableGlobal->TypeName))
						        .empty())
						{
							return "_vs_global_get(" + QuotePython(GlobalStorageName(*CallableGlobal)) + ")(" + Arguments + ")";
						}
					}
					if (const FFunctionDecl* Function = ResolveFunction(
					        NamespaceName, &Call.Location))
					{
						if (!Call.TypeArgs.empty())
						{
							Fail("explicit generic namespace calls are not supported yet", Call.Location);
							return "None";
						}
						return FunctionPythonName(*Function) + "(" + Arguments + ")";
					}
				}
				const std::string Object = EmitExpr(*Call.Object);
				if (!Call.TypeArgs.empty())
				{
					Fail("explicit generic method calls are not supported yet", Call.Location);
					return "None";
				}
				return EmitResolvedMethodCall(
				    InferExprType(*Call.Object), Object, Call.Method, Arguments);
			}

			static std::vector<std::string> TypeArguments(const std::string& TypeName)
			{
				const std::size_t Start = TypeName.find('<');
				if (Start == std::string::npos || TypeName.empty() || TypeName.back() != '>')
				{
					return {};
				}
				std::vector<std::string> Result;
				std::string Current;
				int Depth = 0;
				for (std::size_t Index = Start + 1; Index + 1 < TypeName.size(); ++Index)
				{
					const char Char = TypeName[Index];
					if (Char == '<')
					{
						++Depth;
					}
					else if (Char == '>')
					{
						--Depth;
					}
					if (Char == ',' && Depth == 0)
					{
						Result.push_back(Current);
						Current.clear();
					}
					else
					{
						Current.push_back(Char);
					}
				}
				Result.push_back(Current);
				for (std::string& Arg : Result)
				{
					const std::size_t First = Arg.find_first_not_of(" \t");
					const std::size_t Last = Arg.find_last_not_of(" \t");
					Arg = First == std::string::npos ? std::string{} : Arg.substr(First, Last - First + 1);
				}
				return Result;
			}

			std::string FindFieldType(
			    const std::string& RawTypeName,
			    const std::string& FieldName,
			    std::unordered_set<std::string>& Visited) const
			{
				const std::string TypeName = BaseType(ResolveAlias(RawTypeName));
				if (TypeName.empty() || !Visited.insert(TypeName).second)
				{
					return {};
				}
				auto FindDeclared = [&](const auto& Decl) -> std::string
				{
					for (const FFieldDecl& Field : Decl.Fields)
					{
						if (Field.Name == FieldName)
						{
							return Field.TypeName;
						}
					}
					return {};
				};
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (Struct.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Struct); !Found.empty())
					{
						return Found;
					}
					if (!Struct.BaseTypeName.empty())
					{
						if (std::string Found = FindFieldType(Struct.BaseTypeName, FieldName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					for (const std::string& Interface : Struct.InterfaceTypeNames)
					{
						if (std::string Found = FindFieldType(Interface, FieldName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (Class.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Class); !Found.empty())
					{
						return Found;
					}
					for (const std::string& Base : Class.BaseTypeNames)
					{
						if (std::string Found = FindFieldType(Base, FieldName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					for (const std::string& Interface : Class.InterfaceTypeNames)
					{
						if (std::string Found = FindFieldType(Interface, FieldName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					if (Interface.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Interface); !Found.empty())
					{
						return Found;
					}
					for (const std::string& Base : Interface.BaseInterfaceNames)
					{
						if (std::string Found = FindFieldType(Base, FieldName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				return {};
			}

			std::string FindStaticFieldOwner(
			    const std::string& RawTypeName,
			    const std::string& FieldName,
			    std::unordered_set<std::string>& Visited) const
			{
				const std::string TypeName = BaseType(ResolveAlias(RawTypeName));
				if (TypeName.empty() || !Visited.insert(TypeName).second)
				{
					return {};
				}
				auto HasStatic = [&](const auto& Decl)
				{
					return std::any_of(Decl.Fields.begin(), Decl.Fields.end(),
					    [&](const FFieldDecl& Field)
					{
						return Field.bIsStatic && Field.Name == FieldName;
					});
				};
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (Struct.Name != TypeName)
					{
						continue;
					}
					if (HasStatic(Struct))
					{
						return Struct.Name;
					}
					if (!Struct.BaseTypeName.empty())
					{
						if (std::string Found = FindStaticFieldOwner(
						        Struct.BaseTypeName, FieldName, Visited);
						    !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (Class.Name != TypeName)
					{
						continue;
					}
					if (HasStatic(Class))
					{
						return Class.Name;
					}
					for (const std::string& Base : Class.BaseTypeNames)
					{
						if (std::string Found = FindStaticFieldOwner(
						        Base, FieldName, Visited);
						    !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				return {};
			}

			std::string FindMethodReturnType(
			    const std::string& RawTypeName,
			    const std::string& MethodName,
			    std::unordered_set<std::string>& Visited) const
			{
				const std::string TypeName = BaseType(ResolveAlias(RawTypeName));
				if (TypeName.empty() || !Visited.insert(TypeName).second)
				{
					return {};
				}
				auto FindDeclared = [&](const auto& Decl) -> std::string
				{
					for (const FMethodDecl& Method : Decl.Methods)
					{
						if (Method.Name == MethodName)
						{
							return Method.ReturnType;
						}
					}
					return {};
				};
				for (const FStructDecl& Struct : Program.Structs)
				{
					if (Struct.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Struct); !Found.empty())
					{
						return Found;
					}
					if (!Struct.BaseTypeName.empty())
					{
						if (std::string Found = FindMethodReturnType(Struct.BaseTypeName, MethodName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					for (const std::string& Interface : Struct.InterfaceTypeNames)
					{
						if (std::string Found = FindMethodReturnType(Interface, MethodName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				for (const FClassDecl& Class : Program.Classes)
				{
					if (Class.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Class); !Found.empty())
					{
						return Found;
					}
					for (const std::string& Base : Class.BaseTypeNames)
					{
						if (std::string Found = FindMethodReturnType(Base, MethodName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					for (const std::string& Interface : Class.InterfaceTypeNames)
					{
						if (std::string Found = FindMethodReturnType(Interface, MethodName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				for (const FInterfaceDecl& Interface : Program.Interfaces)
				{
					if (Interface.Name != TypeName)
					{
						continue;
					}
					if (std::string Found = FindDeclared(Interface); !Found.empty())
					{
						return Found;
					}
					for (const std::string& Base : Interface.BaseInterfaceNames)
					{
						if (std::string Found = FindMethodReturnType(Base, MethodName, Visited); !Found.empty())
						{
							return Found;
						}
					}
					return {};
				}
				return {};
			}

			bool IsUnsignedIntegerType(const std::string& TypeName) const
			{
				const std::string Base = BaseType(ResolveAlias(TypeName));
				return Base == "uint8" || Base == "uint16" || Base == "uint32" || Base == "uint64" || Base == "unsigned" || Base == "unsigned int" || Base == "unsigned long" || Base == "unsigned char" || Base == "unsigned short";
			}

			bool IsSignedIntegerType(const std::string& TypeName) const
			{
				const std::string Base = BaseType(ResolveAlias(TypeName));
				return Base == "int8" || Base == "int16" || Base == "int32" || Base == "int64" || Base == "int" || Base == "signed" || Base == "char" || Base == "signed int" || Base == "short" || Base == "signed short" || Base == "long" || Base == "signed long" || Base == "signed char";
			}

			std::string PromoteIntegerType(const std::string& TypeName) const
			{
				const std::string Base = BaseType(ResolveAlias(TypeName));
				if (Base == "uint64" || Base == "unsigned long")
				{
					return "uint64";
				}
				if (Base == "int64" || Base == "long" || Base == "signed long")
				{
					return "int64";
				}
				if (IsUnsignedIntegerType(Base))
				{
					return "unsigned int";
				}
				if (IsSignedIntegerType(Base))
				{
					return "int";
				}
				return TypeName;
			}

			std::string InferBinaryType(
			    const EBinaryOp Op,
			    const std::string& LeftType,
			    const std::string& RightType) const
			{
				if (Op >= EBinaryOp::Equal && Op <= EBinaryOp::LogicalOr)
				{
					return "bool";
				}
				const std::string Left = ResolveAlias(LeftType);
				const std::string Right = ResolveAlias(RightType);
				const std::string LeftBase = BaseType(Left);
				const std::string RightBase = BaseType(Right);
				if (Op == EBinaryOp::Add && (LeftBase == "string" || RightBase == "string" || LeftBase == "wstring" || RightBase == "wstring"))
				{
					return LeftBase == "wstring" || RightBase == "wstring" ? "wstring" : "string";
				}
				if (LeftBase == "double" || RightBase == "double")
				{
					return "double";
				}
				if (LeftBase == "float" || RightBase == "float")
				{
					return "float";
				}
				if (Op == EBinaryOp::ShiftLeft || Op == EBinaryOp::ShiftRight)
				{
					return PromoteIntegerType(Left);
				}
				if (LeftBase == "uint64" || RightBase == "uint64" || LeftBase == "unsigned long" || RightBase == "unsigned long")
				{
					return "uint64";
				}
				if (LeftBase == "int64" || RightBase == "int64" || LeftBase == "long" || RightBase == "long")
				{
					return "int64";
				}
				if (IsUnsignedIntegerType(Left) && IsUnsignedIntegerType(Right))
				{
					return "unsigned int";
				}
				if (IsSignedIntegerType(Left) && IsSignedIntegerType(Right))
				{
					return "int";
				}
				return !Left.empty() ? Left : Right;
			}

			std::string InferExprType(const FExpr& Expr) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
					{
						const auto& Value = static_cast<const FIntegerLiteralExpr&>(Expr);
						return Value.bIs64 ? (Value.bUnsigned ? "uint64" : "int64")
						                   : (Value.bUnsigned ? "unsigned" : "int");
					}
				case EExprKind::FloatLiteral:
					return static_cast<const FFloatLiteralExpr&>(Expr).bFloat32 ? "float" : "double";
				case EExprKind::StringLiteral:
					return "string";
				case EExprKind::WStringLiteral:
					return "wstring";
				case EExprKind::BooleanLiteral:
					return "bool";
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						const auto Local = LocalTypes.find(Name);
						if (Local != LocalTypes.end())
						{
							return Local->second;
						}
						const auto Field = CurrentFieldTypes.find(Name);
						if (Field != CurrentFieldTypes.end())
						{
							return Field->second;
						}
						if (const FVarDeclStmt* Global = ResolveGlobal(Name, &Expr.Location))
						{
							return Global->TypeName;
						}
						return ResolveTypeSymbol(Name, &Expr.Location);
					}
				case EExprKind::This:
					return CurrentTypeName;
				case EExprKind::Unary:
					{
						const auto& Node = static_cast<const FUnaryExpr&>(Expr);
						if (Node.Op == EUnaryOp::Not)
						{
							return "bool";
						}
						const std::string Operand = InferExprType(*Node.Operand);
						return IsUnsignedIntegerType(Operand) || IsSignedIntegerType(Operand)
						    ? PromoteIntegerType(Operand)
						    : Operand;
					}
				case EExprKind::Binary:
					{
						const auto& Node = static_cast<const FBinaryExpr&>(Expr);
						return InferBinaryType(
						    Node.Op, InferExprType(*Node.Left), InferExprType(*Node.Right));
					}
				case EExprKind::Assign:
					return InferExprType(*static_cast<const FAssignExpr&>(Expr).Target);
				case EExprKind::Conditional:
					return InferExprType(*static_cast<const FConditionalExpr&>(Expr).ThenExpr);
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						if (Call.Callee == "__std_bind_method" && IsMethodBindingFactoryCall(Call))
						{
							return Call.TypeArgs[0];
						}
						if (IsMethodBindingFactoryCall(Call) && Call.Callee != "__std_bind_method")
						{
							return "attribute<" + Call.TypeArgs[0] + ">";
						}
						if (HasStandardAttribute() && LocalTypes.count(Call.Callee) &&
						    BaseType(ResolveAlias(LocalTypes.at(Call.Callee))) == "attribute")
						{
							const auto Types = TypeArguments(LocalTypes.at(Call.Callee));
							if (!Types.empty()) return Types[0];
						}

						const FExprArgumentView Args = Call.GetArguments();
						if (const FVarDeclStmt* Global = ResolveGlobal(
						        Call.Callee, &Call.Location))
						{
							if (const std::string ReturnType = FunctionValueReturnType(
							        ResolveAlias(Global->TypeName));
							    !ReturnType.empty())
							{
								return ReturnType;
							}
						}
						if ((Call.Callee == "cast" || Call.Callee == "const_cast") && Call.TypeArgs.size() == 1)
						{
							return Call.TypeArgs[0];
						}
						if (Call.Callee == "try_cast" && Call.TypeArgs.size() == 1)
						{
							return "optional<" + Call.TypeArgs[0] + ">";
						}
						if (const std::string ReturnType = InferGenericCallReturnType(Call);
						    !ReturnType.empty())
						{
							return ReturnType;
						}
						if (const FFunctionDecl* Function = ResolveFunction(
						        Call.Callee, &Call.Location))
						{
							return Function->ReturnType;
						}
						if (TypeNames.find(BaseType(ResolveAlias(Call.Callee))) != TypeNames.end())
						{
							return Call.Callee;
						}
						if (Call.Callee == "abs" && Args.size() == 1 && Args[0])
						{
							return InferExprType(*Args[0]);
						}
						if ((Call.Callee == "min" || Call.Callee == "max") && Args.size() == 2 && Args[0] && Args[1])
						{
							return InferBinaryType(
							    EBinaryOp::Add, InferExprType(*Args[0]), InferExprType(*Args[1]));
						}
						if (Call.Callee == "to_int" || Call.Callee == "__read_int" || Call.Callee == "read_int")
						{
							return "int";
						}
						if (Call.Callee == "len" || Call.Callee == "__array_prepare_sort" || Call.Callee == "__find" || Call.Callee == "__channel_size" || Call.Callee == "__channel_capacity" || Call.Callee == "to_int64")
						{
							return "int64";
						}
						if (Call.Callee == "to_uint64")
						{
							return "uint64";
						}
						if (Call.Callee == "to_unsigned")
						{
							return "unsigned int";
						}
						if (Call.Callee == "__to_int8")
						{
							return "int8";
						}
						if (Call.Callee == "__to_uint8")
						{
							return "uint8";
						}
						if (Call.Callee == "__to_int16")
						{
							return "int16";
						}
						if (Call.Callee == "__to_uint16")
						{
							return "uint16";
						}
						if (Call.Callee == "to_float")
						{
							return "float";
						}
						if (Call.Callee == "to_double" || (Call.bStandardLibraryQualified && (Call.Callee == "now" || Call.Callee == "steady_now")) || Call.Callee == "__sqrt" || Call.Callee == "__sin" || Call.Callee == "__cos" || Call.Callee == "__tan" || Call.Callee == "__floor" || Call.Callee == "__ceil" || Call.Callee == "__round" || Call.Callee == "__trunc" || Call.Callee == "__pow" || Call.Callee == "__exp" || Call.Callee == "__log" || Call.Callee == "__log2" || Call.Callee == "__asin" || Call.Callee == "__acos" || Call.Callee == "__atan2" || Call.Callee == "__read_double" || Call.Callee == "read_double")
						{
							return "double";
						}
						if (Call.Callee == "to_string" || Call.Callee == "to_wstring" || Call.Callee == "format" || Call.Callee == "formatf" || Call.Callee == "__read_line" || Call.Callee == "read_line" || Call.Callee == "__read_string" || Call.Callee == "read_string" || Call.Callee == "__read_token" || Call.Callee == "read_token")
						{
							return "string";
						}
						if (Call.Callee == "__read_bool" || Call.Callee == "read_bool" || Call.Callee == "__isfinite" || Call.Callee == "__isnan")
						{
							return "bool";
						}
						if (Call.Callee == "range")
						{
							return "array<int>";
						}
						return Call.Callee;
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						std::string NamespaceName;
						if (Call.Object && TryBuildNamespaceMemberName(*Call.Object, Call.Method, NamespaceName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobal(
							        NamespaceName, &Call.Location))
							{
								if (const std::string ReturnType = FunctionValueReturnType(
								        ResolveAlias(Global->TypeName));
								    !ReturnType.empty())
								{
									return ReturnType;
								}
							}
							if (const FFunctionDecl* Function = ResolveFunction(
							        NamespaceName, &Call.Location))
							{
								return Function->ReturnType;
							}
						}
						if (Call.Object)
						{
							std::unordered_set<std::string> Visited;
							const std::string Type = FindMethodReturnType(
							    InferExprType(*Call.Object), Call.Method, Visited);
							if (!Type.empty())
							{
								return Type;
							}
						}
						if (Call.Method == "size" || Call.Method == "capacity" || Call.Method == "count")
						{
							return "int64";
						}
						if (Call.Method == "has_value")
						{
							return "bool";
						}
						if (Call.Object)
						{
							const std::string ObjectType = ResolveAlias(InferExprType(*Call.Object));
							const std::string ObjectBase = BaseType(ObjectType);
							const std::vector<std::string> Args = TypeArguments(ObjectType);
							if (Call.Method == "keys" && (ObjectBase == "map" || ObjectBase == "ordered_map") && Args.size() > 1)
							{
								return "array<" + Args[0] + ">";
							}
							if (Call.Method == "entries" && (ObjectBase == "map" || ObjectBase == "ordered_map") && Args.size() > 1)
							{
								return "array<pair<" + Args[0] + "," + Args[1] + ">>";
							}
							if (Call.Method == "values")
							{
								if ((ObjectBase == "map" || ObjectBase == "ordered_map") && Args.size() > 1)
								{
									return "array<" + Args[1] + ">";
								}
								if (!Args.empty())
								{
									return "array<" + Args[0] + ">";
								}
							}
							if ((Call.Method == "front" || Call.Method == "back" || Call.Method == "top" || Call.Method == "pop" || Call.Method == "pop_front" || Call.Method == "pop_back") && !Args.empty())
							{
								return Args[0];
							}
						}
						if ((Call.Method == "get" || Call.Method == "value_or") && Call.Object)
						{
							const std::string ObjectType = ResolveAlias(InferExprType(*Call.Object));
							const std::vector<std::string> Args = TypeArguments(ObjectType);
							if (BaseType(ObjectType) == "optional" && !Args.empty())
							{
								return Args.front();
							}
						}
						return {};
					}
				case EExprKind::MemberAccess:
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
						std::string NamespaceName;
						if (Member.Object && TryBuildNamespaceMemberName(*Member.Object, Member.Member, NamespaceName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobal(
							        NamespaceName, &Member.Location))
							{
								return Global->TypeName;
							}
							if (const std::string Type = ResolveTypeSymbol(NamespaceName, &Member.Location); !Type.empty())
							{
								return Type;
							}
						}
						if (Member.Object)
						{
							std::unordered_set<std::string> Visited;
							const std::string Type = FindFieldType(
							    InferExprType(*Member.Object), Member.Member, Visited);
							if (!Type.empty())
							{
								return Type;
							}
						}
						const auto Field = CurrentFieldTypes.find(Member.Member);
						return Field != CurrentFieldTypes.end() ? Field->second : std::string{};
					}
				case EExprKind::IndexAccess:
					{
						const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
						const std::string Container = ResolveAlias(InferExprType(*Index.Object));
						const std::vector<std::string> Args = TypeArguments(Container);
						const std::string Base = BaseType(Container);
						if ((Base == "map" || Base == "ordered_map") && Args.size() > 1)
						{
							return Args[1];
						}
						if (!Args.empty())
						{
							return Args.front();
						}
						return Base == "string" || Base == "wstring" ? Base : std::string{};
					}
				case EExprKind::ArrayInit:
					return static_cast<const FArrayInitExpr&>(Expr).TypeName;
				case EExprKind::MapInit:
					return static_cast<const FMapInitExpr&>(Expr).TypeName;
				default:
					return {};
				}
			}

			std::string InferComparisonOperandType(const FExpr& Expr) const
			{
				if (Expr.Kind == EExprKind::NullptrLiteral)
				{
					return "nullptr";
				}
				if (Expr.Kind == EExprKind::Conditional)
				{
					const auto& Conditional = static_cast<const FConditionalExpr&>(Expr);
					if (InferComparisonOperandType(*Conditional.ThenExpr) == "nullptr" &&
					    InferComparisonOperandType(*Conditional.ElseExpr) == "nullptr")
					{
						return "nullptr";
					}
				}
				// Null literals need their static conversion identity for member lookup;
				// unknown types and nullable object values retain ordinary inference.
				return ResolveAlias(InferExprType(Expr));
			}

			std::string CastTo(const std::string& TypeName, const std::string& Value) const
			{
				return TypeName.empty() || TypeName == "void"
				    ? Value
				    : "_vs_cast(" + QuotePython(TypeName) + ", " + Value + ")";
			}

			std::string EmitExpr(const FExpr& Expr, const std::string* TypeHint = nullptr)
			{
				if (bFailed)
				{
					return "None";
				}
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
					{
						const auto& Value = static_cast<const FIntegerLiteralExpr&>(Expr);
						if (Value.bIs64)
						{
							return Value.bUnsigned
							    ? std::to_string(Value.UnsignedValue64)
							    : std::to_string(Value.Value64);
						}
						return Value.bUnsigned ? std::to_string(Value.UnsignedValue) : std::to_string(Value.Value);
					}
				case EExprKind::FloatLiteral:
					{
						const auto& Value = static_cast<const FFloatLiteralExpr&>(Expr);
						std::ostringstream Text;
						Text << std::setprecision(17) << Value.Value;
						std::string Result = Text.str();
						if (Result.find_first_of(".eE") == std::string::npos)
						{
							Result += ".0";
						}
						return Value.bFloat32 ? "_VsFloat32(" + Result + ")" : Result;
					}
				case EExprKind::StringLiteral:
					return QuotePythonByteString(static_cast<const FStringLiteralExpr&>(Expr).Value);
				case EExprKind::WStringLiteral:
					return QuotePython(Utf16ToUtf8(static_cast<const FWStringLiteralExpr&>(Expr).Value));
				case EExprKind::BooleanLiteral:
					return static_cast<const FBooleanLiteralExpr&>(Expr).Value ? "True" : "False";
				case EExprKind::NullptrLiteral:
					return "None";
				case EExprKind::This:
					return "self";
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (IsSuperReceiver(Expr))
						{
							Fail("Super receivers are not supported yet", Expr.Location);
							return "None";
						}
						return EmitIdentifier(Name, &Expr.Location);
					}
				case EExprKind::Unary:
					{
						const auto& Node = static_cast<const FUnaryExpr&>(Expr);
						if (Node.Op == EUnaryOp::Not)
						{
							return "(not _vs_condition(" + EmitExpr(*Node.Operand) + "))";
						}
						return "_vs_unary(" + QuotePython(InferExprType(Expr)) + ", " + QuotePython(Node.Op == EUnaryOp::Negate ? "neg" : "bitnot") + ", " + EmitExpr(*Node.Operand) + ")";
					}
				case EExprKind::Binary:
					{
						const auto& Node = static_cast<const FBinaryExpr&>(Expr);
						const std::string Left = EmitExpr(*Node.Left);
						const std::string Right = EmitExpr(*Node.Right);
						const char* Operators[] = {"+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "and", "or", "&", "|", "^", "<<", ">>", "in"};
						if ((Node.Op >= EBinaryOp::Equal && Node.Op <= EBinaryOp::LogicalOr) || Node.Op == EBinaryOp::In)
						{
							if (Node.Op == EBinaryOp::LogicalAnd || Node.Op == EBinaryOp::LogicalOr)
							{
								return "(_vs_condition(" + Left + ") " + Operators[static_cast<std::size_t>(Node.Op)] +
								    " _vs_condition(" + Right + "))";
							}
							if (Node.Op >= EBinaryOp::Equal && Node.Op <= EBinaryOp::GreaterEqual)
							{
								const std::string LeftType = BaseType(ResolveAlias(InferExprType(*Node.Left)));
								const std::string RightType = BaseType(ResolveAlias(InferExprType(*Node.Right)));
								const bool bEquality = Node.Op == EBinaryOp::Equal || Node.Op == EBinaryOp::NotEqual;
								if (LeftType == RightType && (LeftType == "wstring" || (LeftType == "string" && bEquality)))
								{
									return "_vs_compare_text(" + Left + ", " + Right + ", " + QuotePython(Operators[static_cast<std::size_t>(Node.Op)]) + (LeftType == "wstring" ? ", True)" : ", False)");
								}
								const auto IsNumeric = [&](const std::string& Type)
								{
									return IsSignedIntegerType(Type) || IsUnsignedIntegerType(Type) || Type == "float" || Type == "double";
								};
								if (bEquality && !(IsNumeric(LeftType) && IsNumeric(RightType)))
								{
									const std::string Comparison = "_vs_equal(" + Left + ", " + Right + ", " +
									    QuotePython(LeftType) + ", " + QuotePython(RightType) + ')';
									return Node.Op == EBinaryOp::NotEqual ? "(not " + Comparison + ')' : Comparison;
								}
								if (!bEquality && !(IsNumeric(LeftType) && IsNumeric(RightType)))
								{
									const std::string Comparison = QuotePython(Operators[static_cast<std::size_t>(Node.Op)]) + ", " +
									    QuotePython(InferComparisonOperandType(*Node.Left)) + ", " +
									    QuotePython(InferComparisonOperandType(*Node.Right));
									return "_vs_ordered_compare(_vs_ordered_left(" + Left + ", " + Comparison + "), " +
									    Right + ", " + Comparison + ')';
								}
							}

							return "(" + Left + " " + Operators[static_cast<std::size_t>(Node.Op)] + " " +
							    Right + ")";
						}
						const char* Operations[] = {"add", "sub", "mul", "div", "mod", "", "", "", "", "", "", "", "", "and", "or", "xor", "shl", "shr", ""};
						return "_vs_binary(" + QuotePython(InferExprType(Expr)) + ", " + QuotePython(Operations[static_cast<std::size_t>(Node.Op)]) + ", " + Left + ", " + Right + ")";
					}
				case EExprKind::Assign:
					{
						const auto& Node = static_cast<const FAssignExpr&>(Expr);
						if (!Node.Target || Node.Target->Kind != EExprKind::Identifier || Node.bFromPostfixIncDec)
						{
							Fail("this assignment expression shape is not supported yet", Expr.Location);
							return "None";
						}
						const std::string Target = EmitExpr(*Node.Target);
						std::string Value = EmitExpr(*Node.Value);
						if (Node.bIsCompound)
						{
							Value = EmitCompound(
							    Node.CompoundOp, Target, Value, InferBinaryType(Node.CompoundOp, InferExprType(*Node.Target), InferExprType(*Node.Value)));
						}
						Value = CastTo(InferExprType(*Node.Target), Value);
						const std::string& Name =
						    static_cast<const FIdentifierExpr&>(*Node.Target).Name;
						if (LocalNames.find(Name) == LocalNames.end() && !CurrentTypeName.empty() &&
						    (bInStaticMethod || CurrentFields.find(Name) == CurrentFields.end()) &&
						    CurrentStaticFields.find(Name) != CurrentStaticFields.end())
						{
							std::unordered_set<std::string> Visited;
							const std::string Owner = FindStaticFieldOwner(CurrentTypeName, Name, Visited);
							return "_vs_static_set(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Name)) + ", " + Value + ")";
						}
						if (LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) == CurrentFields.end())
						{
							if (const FVarDeclStmt* Global = ResolveGlobal(Name, &Expr.Location))
							{
								return "_vs_global_set(" + QuotePython(GlobalStorageName(*Global)) + ", " + Value + ")";
							}
						}
						if (LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) != CurrentFields.end())
						{
							return "_vs_member_set(self, " + QuotePython(PythonIdentifier(Name)) + ", " + Value + ")";
						}
						return "(" + Target + " := " + Value + ")";
					}
				case EExprKind::Conditional:
					{
						const auto& Node = static_cast<const FConditionalExpr&>(Expr);
						return "(" + EmitExpr(*Node.ThenExpr) + " if _vs_condition(" + EmitExpr(*Node.Condition) + ") else " + EmitExpr(*Node.ElseExpr) + ")";
					}
				case EExprKind::Ref:
					{
						Fail("ref expressions are not supported yet", Expr.Location);
						return "None";
					}
				case EExprKind::Call:
					return EmitCall(static_cast<const FCallExpr&>(Expr));
				case EExprKind::MethodCall:
					return EmitMethodCall(static_cast<const FMethodCallExpr&>(Expr));
				case EExprKind::MemberAccess:
					{
						const auto& Node = static_cast<const FMemberAccessExpr&>(Expr);
						std::string NamespaceName;
						if (Node.Object && TryBuildNamespaceMemberName(*Node.Object, Node.Member, NamespaceName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobal(
							        NamespaceName, &Node.Location))
							{
								return "_vs_global_get(" + QuotePython(GlobalStorageName(*Global)) + ")";
							}
							if (const FFunctionDecl* Function = ResolveFunction(
							        NamespaceName, &Node.Location))
							{
								return EmitFunctionValue(*Function, Node.Location);
							}
							if (const std::string Type = ResolveTypeSymbol(NamespaceName, &Node.Location); !Type.empty())
							{
								return PythonIdentifier(Type);
							}
						}
						std::unordered_set<std::string> Visited;
						const std::string StaticOwner = FindStaticFieldOwner(
						    InferExprType(*Node.Object), Node.Member, Visited);
						if (!StaticOwner.empty())
						{
							const std::string Value = "_vs_static_get(" + PythonIdentifier(StaticOwner) + ", " + QuotePython(PythonIdentifier(Node.Member)) + ")";
							const std::string Object = EmitExpr(*Node.Object);
							return Object == PythonIdentifier(StaticOwner) ? Value
							    : "(_vs_discard(" + Object + ") or " + Value + ")";
						}
						return EmitExpr(*Node.Object) + "." + PythonIdentifier(Node.Member);
					}
				case EExprKind::IndexAccess:
					{
						const auto& Node = static_cast<const FIndexAccessExpr&>(Expr);
						return "_vs_index(" + EmitExpr(*Node.Object) + ", " + EmitExpr(*Node.Index) + ")";
					}
				case EExprKind::ArrayInit:
					{
						const auto& Node = static_cast<const FArrayInitExpr&>(Expr);
						const std::string TypeName = !Node.TypeName.empty() ? Node.TypeName : TypeHint ? *TypeHint
						                                                                               : "array";
						const std::string Base = BaseType(ResolveAlias(TypeName));
						std::ostringstream Values;
						for (std::size_t Index = 0; Index < Node.Elements.size(); ++Index)
						{
							if (Index)
							{
								Values << ", ";
							}
							Values << EmitExpr(*Node.Elements[Index]);
						}
						const std::string TypeArgument = ", type_name=_vs_resolve_type_name(" + QuotePython(TypeName) + ")";
						if (Base == "set")
						{
							return "_VsSet([" + Values.str() + "]" + TypeArgument + ")";
						}
						if (Base == "deque")
						{
							return "_VsDeque([" + Values.str() + "]" + TypeArgument + ")";
						}
						if (Base == "queue")
						{
							return "_VsQueue([" + Values.str() + "]" + TypeArgument + ")";
						}
						if (Base == "stack")
						{
							return "_VsStack([" + Values.str() + "]" + TypeArgument + ")";
						}
						if (TypeNames.find(Base) != TypeNames.end())
						{
							return PythonIdentifier(Base) + "(" + Values.str() + ")";
						}
						return "_VsArray([" + Values.str() + "]" + TypeArgument + ")";
					}
				case EExprKind::MapInit:
					{
						const auto& Node = static_cast<const FMapInitExpr&>(Expr);
						const std::string TypeName = !Node.TypeName.empty()
						    ? Node.TypeName
						    : TypeHint ? *TypeHint
						               : "map";
						std::ostringstream Values;
						for (std::size_t Index = 0; Index < Node.Entries.size(); ++Index)
						{
							if (Index)
							{
								Values << ", ";
							}
							Values << EmitExpr(*Node.Entries[Index].Key) << ": " << EmitExpr(*Node.Entries[Index].Value);
						}
						return "_VsMap({" + Values.str() + "}, type_name=_vs_resolve_type_name(" + QuotePython(TypeName) + "))";
					}
				case EExprKind::Lambda:
					{
						// Python closures do not implement VeroScript capture snapshots,
						// receiver ownership, or typed lambda parameter scopes. Reject
						// every expression position, including returns and call arguments.
						Fail("source lambdas are not supported yet", Expr.Location);
						return "None";
					}
				default:
					{
						break;
					}
				}
				Fail("unsupported expression", Expr.Location);
				return "None";
			}

			std::string EmitCompound(
			    const EBinaryOp Op,
			    const std::string& Left,
			    const std::string& Right,
			    const std::string& TargetType)
			{
				const char* Operations[] = {"add", "sub", "mul", "div", "mod", "", "", "", "", "", "", "", "", "and", "or", "xor", "shl", "shr", ""};
				return "_vs_binary(" + QuotePython(TargetType) + ", " + QuotePython(Operations[static_cast<std::size_t>(Op)]) + ", " + Left + ", " + Right + ")";
			}

			bool CaptureStorageBinding(const FExpr& Expr, const int Indent,
			    std::string& Read, std::string& WritePrefix, std::string& WriteSuffix)
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
					Read = EmitIdentifier(Name, &Expr.Location);
					if (LocalNames.find(Name) == LocalNames.end() && !CurrentTypeName.empty() &&
					    (bInStaticMethod || CurrentFields.find(Name) == CurrentFields.end()) &&
					    CurrentStaticFields.find(Name) != CurrentStaticFields.end())
					{
						std::unordered_set<std::string> Visited;
						const std::string Owner = FindStaticFieldOwner(CurrentTypeName, Name, Visited);
						WritePrefix = "_vs_static_set(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Name)) + ", ";
						WriteSuffix = ")";
					}
					else if (LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) == CurrentFields.end())
					{
						if (const FVarDeclStmt* Global = ResolveGlobal(Name, &Expr.Location))
						{
							WritePrefix = "_vs_global_set(" + QuotePython(GlobalStorageName(*Global)) + ", ";
							WriteSuffix = ")";
						}
					}
					if (WritePrefix.empty())
					{
						WritePrefix = Read + " = ";
					}
					return true;
				}
				if (Expr.Kind != EExprKind::MemberAccess) return false;
				const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
				std::string NamespaceName;
				if (Member.Object && TryBuildNamespaceMemberName(*Member.Object, Member.Member, NamespaceName))
				{
					if (const FVarDeclStmt* Global = ResolveGlobal(NamespaceName, &Expr.Location))
					{
						Read = "_vs_global_get(" + QuotePython(GlobalStorageName(*Global)) + ')';
						WritePrefix = "_vs_global_set(" + QuotePython(GlobalStorageName(*Global)) + ", ";
						WriteSuffix = ")";
						return true;
					}
				}
				std::unordered_set<std::string> Visited;
				const std::string Owner = FindStaticFieldOwner(InferExprType(*Member.Object), Member.Member, Visited);
				if (!Owner.empty())
				{
					Out << Pad(Indent) << EmitExpr(*Member.Object) << "\n";
					Read = "_vs_static_get(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Member.Member)) + ')';
					WritePrefix = "_vs_static_set(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Member.Member)) + ", ";
					WriteSuffix = ")";
					return true;
				}
				const std::string Receiver = "___vs_receiver_" + std::to_string(NextTempId++);
				Out << Pad(Indent) << Receiver << " = " << EmitExpr(*Member.Object) << "\n";
				Read = Receiver + '.' + PythonIdentifier(Member.Member);
				WritePrefix = Read + " = ";
				return true;
			}

			void EmitAssignmentStatement(const FAssignExpr& Assign, const int Indent)
			{
				if (!Assign.Target || !Assign.Value)
				{
					Fail("invalid assignment", Assign.Location);
					return;
				}
				std::string Target;
				std::string ThreadLocalSetter;
				std::string ContainerWriteback;
				if (Assign.Target->Kind == EExprKind::Identifier)
				{
					const std::string& Name =
					    static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
					if (LocalNames.find(Name) == LocalNames.end() && !CurrentTypeName.empty() &&
					    (bInStaticMethod || CurrentFields.find(Name) == CurrentFields.end()) &&
					    CurrentStaticFields.find(Name) != CurrentStaticFields.end())
					{
						std::unordered_set<std::string> Visited;
						const std::string Owner = FindStaticFieldOwner(CurrentTypeName, Name, Visited);
						Target = "_vs_static_get(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Name)) + ")";
						ThreadLocalSetter = "_vs_static_set(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Name)) + ", ";
					}
					else if (LocalNames.find(Name) == LocalNames.end() && CurrentFields.find(Name) == CurrentFields.end())
					{
						if (const FVarDeclStmt* Global = ResolveGlobal(Name, &Assign.Location))
						{
							Target = "_vs_global_get(" + QuotePython(GlobalStorageName(*Global)) + ")";
							ThreadLocalSetter = "_vs_global_set(" + QuotePython(GlobalStorageName(*Global)) + ", ";
						}
					}
				}
				else if (Assign.Target->Kind == EExprKind::MemberAccess)
				{
					const auto& Member = static_cast<const FMemberAccessExpr&>(*Assign.Target);
					std::string NamespaceName;
					if (Member.Object && TryBuildNamespaceMemberName(*Member.Object, Member.Member, NamespaceName))
					{
						if (const FVarDeclStmt* Global = ResolveGlobal(
						        NamespaceName, &Assign.Location))
						{
							Target = "_vs_global_get(" + QuotePython(GlobalStorageName(*Global)) + ")";
							ThreadLocalSetter = "_vs_global_set(" + QuotePython(GlobalStorageName(*Global)) + ", ";
						}
					}
					if (ThreadLocalSetter.empty())
					{
						std::unordered_set<std::string> Visited;
						const std::string Owner = FindStaticFieldOwner(
						    InferExprType(*Member.Object), Member.Member, Visited);
						if (!Owner.empty())
						{
							Out << Pad(Indent) << EmitExpr(*Member.Object) << "\n";
							Target = "_vs_static_get(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Member.Member)) + ")";
							ThreadLocalSetter = "_vs_static_set(" + PythonIdentifier(Owner) + ", " + QuotePython(PythonIdentifier(Member.Member)) + ", ";
						}
					}
				}
				if (Assign.Target->Kind == EExprKind::MemberAccess)
				{
					if (ThreadLocalSetter.empty())
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(*Assign.Target);
						const std::string ObjectTemp = "___vs_object_" + std::to_string(NextTempId++);
						Out << Pad(Indent) << ObjectTemp << " = " << EmitExpr(*Member.Object) << "\n";
						Target = ObjectTemp + "." + PythonIdentifier(Member.Member);
					}
				}
				else if (Assign.Target->Kind == EExprKind::IndexAccess)
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(*Assign.Target);
					std::vector<const FIndexAccessExpr*> Path;
					const FExpr* Root = Assign.Target.get();
					bool ReferencePath = true;
					while (Root->Kind == EExprKind::IndexAccess)
					{
						const auto& Step = static_cast<const FIndexAccessExpr&>(*Root);
						const std::string Type = ResolveAlias(InferExprType(*Step.Object));
						ReferencePath = ReferencePath && (Runtime::IsDynamicArrayTypeName(Type) ||
						    Runtime::IsDequeTypeName(Type) || Runtime::IsNativeArrayTypeName(Type) ||
						    Runtime::IsScriptArgsArrayTypeName(Type));
						Path.push_back(&Step);
						Root = Step.Object.get();
					}
					ReferencePath = ReferencePath && Path.size() > 1 &&
					    (Root->Kind == EExprKind::Identifier || Root->Kind == EExprKind::MemberAccess);
					std::string Read, WritePrefix, WriteSuffix;
					if (ReferencePath && CaptureStorageBinding(*Root, Indent, Read, WritePrefix, WriteSuffix))
					{
						// GetIndexPathRef retains the root binding and indices, not a row snapshot.
						Target = Read;
						for (auto Step = Path.rbegin(); Step != Path.rend(); ++Step)
						{
							const std::string IndexTemp = "___vs_index_" + std::to_string(NextTempId++);
							Out << Pad(Indent) << IndexTemp << " = " << EmitExpr(*(*Step)->Index) << "\n";
							Target += '[' + IndexTemp + ']';
						}
					}
					else
					{
						const bool Bound = CaptureStorageBinding(*Index.Object, Indent, Read, WritePrefix, WriteSuffix);
						const std::string ObjectTemp = "___vs_object_" + std::to_string(NextTempId++);
						const std::string IndexTemp = "___vs_index_" + std::to_string(NextTempId++);
						Out << Pad(Indent) << ObjectTemp << " = " << (Bound ? Read : EmitExpr(*Index.Object)) << "\n";
						Out << Pad(Indent) << IndexTemp << " = " << EmitExpr(*Index.Index) << "\n";
						Target = ObjectTemp + '[' + IndexTemp + ']';
						if (Bound)
						{
							ContainerWriteback = WritePrefix + ObjectTemp + WriteSuffix;
						}
					}
				}
				else if (Target.empty())
				{
					Target = EmitExpr(*Assign.Target);
				}
				std::string Value = EmitExpr(*Assign.Value);
				if (Assign.bIsCompound)
				{
					Value = EmitCompound(
					    Assign.CompoundOp, Target, Value, InferBinaryType(Assign.CompoundOp, InferExprType(*Assign.Target), InferExprType(*Assign.Value)));
				}
				Value = CastTo(InferExprType(*Assign.Target), Value);
				if (!ThreadLocalSetter.empty())
				{
					Out << Pad(Indent) << ThreadLocalSetter << Value << ")\n";
				}
				else
				{
					Out << Pad(Indent) << Target << " = " << Value << "\n";
				}
				if (!ContainerWriteback.empty())
				{
					Out << Pad(Indent) << ContainerWriteback << "\n";
				}
			}

			void EmitBlockContents(const FBlockStmt& Block, const int Indent)
			{
				FLocalState Saved = SaveLocalState();
				if (Block.Statements.empty())
				{
					Out << Pad(Indent) << "pass\n";
					RestoreLocalState(std::move(Saved));
					return;
				}
				for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
				{
					if (Statement)
					{
						EmitStmt(*Statement, Indent);
					}
					if (bFailed)
					{
						break;
					}
				}
				RestoreLocalState(std::move(Saved));
			}

			void EmitStmt(const FStmt& Stmt, const int Indent)
			{
				switch (Stmt.Kind)
				{
				case EStmtKind::Block:
					{
						EmitBlockContents(static_cast<const FBlockStmt&>(Stmt), Indent);
						return;
					}
				case EStmtKind::Expr:
					{
						const auto& Node = static_cast<const FExprStmt&>(Stmt);
						if (Node.Expression && Node.Expression->Kind == EExprKind::Assign)
						{
							EmitAssignmentStatement(static_cast<const FAssignExpr&>(*Node.Expression), Indent);
						}
						else
						{
							Out << Pad(Indent) << (Node.Expression ? EmitExpr(*Node.Expression) : "None") << "\n";
						}
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						for (const auto& Declaration : static_cast<const FVarDeclGroupStmt&>(Stmt).Declarations)
						{
							if (Declaration)
							{
								EmitStmt(*Declaration, Indent);
							}

							if (bFailed) return;
						}
						return;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Node = static_cast<const FVarDeclStmt&>(Stmt);
						if (Node.bIsLocalStatic)
						{
							Fail("local static storage requires the VeroScript runtime; Python source output is unsupported", Node.Location);
							return;
						}
						if (bEmittingTopLevel)
						{
							ActiveModulePath = !Node.ModulePath.empty()
							    ? Node.ModulePath
							    : std::string(Node.Location.FileLabel);
							ActiveNamespace = NamespaceOfSymbol(Node.Name);
						}
						if (Node.Initializer && Node.Initializer->Kind == EExprKind::Lambda)
						{
							Fail("source lambdas are not supported yet", Node.Initializer->Location);
							return;
						}
						if (Node.bIsRef)
						{
							Fail("reference local '" + Node.Name + "' is not supported yet", Node.Location);
							return;
						}
						const std::string TypeName = Node.TypeName == "auto" && Node.Initializer
						    ? InferExprType(*Node.Initializer)
						    : Node.TypeName;
						if (TypeName.empty() || TypeName == "auto")
						{
							Fail("cannot resolve local type for '" + Node.Name + "'", Node.Location);
							return;
						}
						if (!ValidateSupportedType(
						        TypeName, "local '" + Node.Name + "'", Node.Location))
						{
							return;
						}
						// A declaration's initializer resolves names in the enclosing
						// scope; only later declarations see this new local binding.
						const std::string Initializer = Node.Initializer
						    ? EmitExpr(*Node.Initializer, &TypeName)
						    : "_vs_default(" + QuotePython(TypeName) + ')';
						const std::string EmittedName = bEmittingTopLevel
						    ? GlobalStorageName(Node)
						    : DeclareLocal(Node.Name, TypeName);
						Out << Pad(Indent);
						if (bEmittingTopLevel)
						{
							Out << "_vs_define_global(" << QuotePython(EmittedName) << ", ";
						}
						else
						{
							Out << EmittedName << " = ";
						}
						Out << "_vs_cast(" << QuotePython(TypeName) << ", " << Initializer;
						Out << (bEmittingTopLevel ? "))\n" : ")\n");
						return;
					}
				case EStmtKind::TypeAlias:
					{
						Fail("local type aliases are not supported yet", Stmt.Location);
						return;
					}
				case EStmtKind::Return:
					{
						const auto& Node = static_cast<const FReturnStmt&>(Stmt);
						Out << Pad(Indent) << "return";
						if (Node.Value)
						{
							Out << " _vs_cast(" << QuotePython(CurrentReturnType) << ", " << EmitExpr(*Node.Value) << ')';
						}
						Out << "\n";
						return;
					}
				case EStmtKind::If:
					{
						const auto& Node = static_cast<const FIfStmt&>(Stmt);
						Out << Pad(Indent) << "if _vs_condition(" << EmitExpr(*Node.Condition) << "):\n";
						if (Node.ThenBranch)
						{
							EmitBlockContents(*Node.ThenBranch, Indent + 1);
						}
						else
						{
							Out << Pad(Indent + 1) << "pass\n";
						}
						if (Node.ElseBranch)
						{
							Out << Pad(Indent) << "else:\n";
							EmitBlockContents(*Node.ElseBranch, Indent + 1);
						}
						return;
					}
				case EStmtKind::While:
					{
						const auto& Node = static_cast<const FWhileStmt&>(Stmt);
						Out << Pad(Indent) << "while _vs_condition(" << EmitExpr(*Node.Condition) << "):\n";
						BreakTargets.push_back(EBreakTarget::Loop);
						ContinueTargets.push_back(EContinueTarget::NativeLoop);
						if (Node.Body)
						{
							EmitBlockContents(*Node.Body, Indent + 1);
						}
						else
						{
							Out << Pad(Indent + 1) << "pass\n";
						}
						ContinueTargets.pop_back();
						BreakTargets.pop_back();
						return;
					}
				case EStmtKind::For:
					{
						const auto& Node = static_cast<const FForStmt&>(Stmt);
						FLocalState Saved = SaveLocalState();
						if (Node.Init)
						{
							EmitStmt(*Node.Init, Indent);
						}
						Out << Pad(Indent) << "while " << (Node.Condition ? "_vs_condition(" + EmitExpr(*Node.Condition) + ')' : "True") << ":\n";
						Out << Pad(Indent + 1) << "try:\n";
						BreakTargets.push_back(EBreakTarget::Loop);
						ContinueTargets.push_back(EContinueTarget::TransformedFor);
						if (Node.Body)
						{
							EmitBlockContents(*Node.Body, Indent + 2);
						}
						else
						{
							Out << Pad(Indent + 2) << "pass\n";
						}
						ContinueTargets.pop_back();
						BreakTargets.pop_back();
						Out << Pad(Indent + 1) << "except _VsForContinue:\n"
						    << Pad(Indent + 2) << "pass\n";
						if (Node.Step)
						{
							if (Node.Step->Kind == EExprKind::Assign)
							{
								EmitAssignmentStatement(static_cast<const FAssignExpr&>(*Node.Step), Indent + 1);
							}
							else
							{
								Out << Pad(Indent + 1) << EmitExpr(*Node.Step) << "\n";
							}
						}
						RestoreLocalState(std::move(Saved));
						return;
					}
				case EStmtKind::ForEach:
					{
						const auto& Node = static_cast<const FForEachStmt&>(Stmt);
						if (Node.bIsRef)
						{
							Fail("reference foreach bindings are not supported yet", Node.Location);
							return;
						}
						const std::string CollectionType = ResolveAlias(InferExprType(*Node.Collection));
						const std::string CollectionBase = BaseType(CollectionType);
						const std::string CollectionExpr = EmitExpr(*Node.Collection);
						FLocalState Saved = SaveLocalState();
						const std::string CollectionName = "___vs_foreach_collection_" + std::to_string(NextTempId++);
						const std::string ItemName = "___vs_foreach_item_" + std::to_string(NextTempId++);
						const std::string ElementType = Node.ElementTypeName == "auto"
						    ? ([&]()
						{
							const std::vector<std::string> Args = TypeArguments(CollectionType);
							return (CollectionBase == "map" || CollectionBase == "ordered_map") && Args.size() > 1 ? Args[1] : !Args.empty() ? Args.front()
							                                                                                                                 : std::string{};
						})()
						    : Node.ElementTypeName;
						if (!ValidateSupportedType(
						        CollectionType, "foreach collection", Node.Location) ||
						    !ValidateSupportedType(
						        ElementType, "foreach element '" + Node.ElementName + "'", Node.Location) ||
						    (Node.bHasKeyBinding && !ValidateSupportedType(Node.KeyTypeName, "foreach key '" + Node.KeyName + "'", Node.Location)))
						{
							RestoreLocalState(std::move(Saved));
							return;
						}
						const std::string ElementName = DeclareLocal(Node.ElementName, ElementType);
						Out << Pad(Indent) << CollectionName << " = " << CollectionExpr << "\n";
						const bool bMap = CollectionBase == "map" || CollectionBase == "ordered_map";
						const bool bPairEntry = bMap && !Node.bHasKeyBinding && BaseType(ResolveAlias(Node.ElementTypeName)) == "pair" && TypeArguments(ResolveAlias(Node.ElementTypeName)).size() == 2;
						if (bMap && Node.bHasKeyBinding)
						{
							const std::string KeyName = DeclareLocal(Node.KeyName, Node.KeyTypeName);
							Out << Pad(Indent) << "for " << ItemName << " in list("
							    << CollectionName << ".keys()):\n";
							Out << Pad(Indent + 1) << KeyName << " = _vs_cast("
							    << QuotePython(Node.KeyTypeName) << ", " << ItemName << ")\n";
							Out << Pad(Indent + 1) << ElementName << " = _vs_cast("
							    << QuotePython(ElementType) << ", " << CollectionName << '[' << ItemName << "])\n";
						}
						else if (bPairEntry)
						{
							Out << Pad(Indent) << "for " << ItemName << " in " << CollectionName << ".entries():\n";
							Out << Pad(Indent + 1) << ElementName << " = _vs_cast("
							    << QuotePython(ElementType) << ", " << ItemName << ")\n";
						}
						else if (bMap)
						{
							Out << Pad(Indent) << "for " << ItemName << " in list("
							    << CollectionName << ".keys()):\n";
							Out << Pad(Indent + 1) << ElementName << " = _vs_cast("
							    << QuotePython(ElementType) << ", " << CollectionName << '[' << ItemName << "])\n";
						}
						else if (CollectionBase == "array")
						{
							Out << Pad(Indent) << "for " << ItemName << " in _vs_live_length_snapshot("
							    << CollectionName << "):\n";
							Out << Pad(Indent + 1) << ElementName << " = _vs_cast("
							    << QuotePython(ElementType) << ", " << ItemName << ")\n";
						}
						else
						{
							Out << Pad(Indent) << "for " << ItemName << " in _vs_clone("
							    << CollectionName << "):\n";
							Out << Pad(Indent + 1) << ElementName << " = _vs_cast("
							    << QuotePython(ElementType) << ", " << ItemName << ")\n";
						}
						BreakTargets.push_back(EBreakTarget::Loop);
						ContinueTargets.push_back(EContinueTarget::NativeLoop);
						if (Node.Body)
						{
							EmitBlockContents(*Node.Body, Indent + 1);
						}
						ContinueTargets.pop_back();
						BreakTargets.pop_back();
						RestoreLocalState(std::move(Saved));
						return;
					}
				case EStmtKind::Break:
					{
						Out << Pad(Indent)
						    << (!BreakTargets.empty() && BreakTargets.back() == EBreakTarget::Switch
						               ? "raise _VsSwitchBreak\n"
						               : "break\n");
						return;
					}
				case EStmtKind::Continue:
					{
						Out << Pad(Indent)
						    << (!ContinueTargets.empty() && ContinueTargets.back() == EContinueTarget::TransformedFor
						               ? "raise _VsForContinue\n"
						               : "continue\n");
						return;
					}
				case EStmtKind::Try:
					{
						EmitTry(static_cast<const FTryStmt&>(Stmt), Indent);
						return;
					}
				case EStmtKind::Throw:
					{
						const auto& Node = static_cast<const FThrowStmt&>(Stmt);
						Out << Pad(Indent) << (Node.Value ? "_vs_throw(" + EmitExpr(*Node.Value) + ")" : "raise") << "\n";
						return;
					}
				case EStmtKind::Switch:
					{
						EmitSwitch(static_cast<const FSwitchStmt&>(Stmt), Indent);
						return;
					}
				case EStmtKind::Fallthrough:
					{
						Fail("switch fallthrough is not supported yet", Stmt.Location);
						return;
					}
				case EStmtKind::Goto:
				case EStmtKind::Label:
					{
						Fail("goto and labels are not supported by the Python backend", Stmt.Location);
						return;
					}
				default:
					{
						Fail("unsupported statement", Stmt.Location);
						return;
					}
				}
			}

			void EmitTry(const FTryStmt& Node, const int Indent)
			{
				Out << Pad(Indent) << "try:\n";
				if (Node.TryBlock)
				{
					EmitBlockContents(*Node.TryBlock, Indent + 1);
				}
				else
				{
					Out << Pad(Indent + 1) << "pass\n";
				}
				if (!Node.Catches.empty())
				{
					const std::string ExceptionName = "___vs_exception_" + std::to_string(NextTempId++);
					Out << Pad(Indent) << "except _VsThrown as " << ExceptionName << ":\n";
					bool bFirst = true;
					bool bCatchAll = false;
					for (const FCatchClause& Catch : Node.Catches)
					{
						FLocalState Saved = SaveLocalState();
						if (Catch.bIsRef)
						{
							Fail("reference catch bindings are not supported yet", Catch.Location);
							RestoreLocalState(std::move(Saved));
							return;
						}
						if (Catch.bCatchAll)
						{
							Out << Pad(Indent + 1) << (bFirst ? "if True:\n" : "else:\n");
							bCatchAll = true;
						}
						else
						{
							Out << Pad(Indent + 1) << (bFirst ? "if " : "elif ")
							    << "_vs_is_type(" << QuotePython(Catch.TypeName) << ", "
							    << ExceptionName << ".value):\n";
						}
						bFirst = false;
						if (!Catch.Name.empty())
						{
							const std::string CatchName = DeclareLocal(Catch.Name, Catch.TypeName);
							Out << Pad(Indent + 2) << CatchName << " = _vs_cast("
							    << QuotePython(Catch.TypeName) << ", " << ExceptionName << ".value)\n";
						}
						if (Catch.Body)
						{
							EmitBlockContents(*Catch.Body, Indent + 2);
						}
						else
						{
							Out << Pad(Indent + 2) << "pass\n";
						}
						RestoreLocalState(std::move(Saved));
					}
					if (!bCatchAll)
					{
						Out << Pad(Indent + 1) << "else:\n"
						    << Pad(Indent + 2) << "raise\n";
					}
				}
				if (Node.FinallyBlock)
				{
					Out << Pad(Indent) << "finally:\n";
					EmitBlockContents(*Node.FinallyBlock, Indent + 1);
				}
			}

			bool IsTerminalSwitchBreak(const FBlockStmt& Body) const
			{
				return !Body.Statements.empty() && Body.Statements.back() && Body.Statements.back()->Kind == EStmtKind::Break;
			}

			void EmitSwitch(const FSwitchStmt& Node, const int Indent)
			{
				const std::string ValueName = "___vs_switch_" + std::to_string(NextTempId++);
				Out << Pad(Indent) << ValueName << " = " << EmitExpr(*Node.Discriminant) << "\n";
				Out << Pad(Indent) << "try:\n";
				const int CaseIndent = Indent + 1;
				bool bFirst = true;
				auto EmitCase = [&](const FSwitchCase& Case)
				{
					if (!Case.Body)
					{
						return;
					}
					if (!Case.bIsDefault && !IsTerminalSwitchBreak(*Case.Body))
					{
						Fail("each Python AOT switch case must end with break; fallthrough is not supported yet", Case.Body->Location);
						return;
					}
					if (Case.bIsDefault)
					{
						Out << Pad(CaseIndent) << (bFirst ? "if True:\n" : "else:\n");
					}
					else
					{
						Out << Pad(CaseIndent) << (bFirst ? "if " : "elif ");
						for (std::size_t Index = 0; Index < Case.Labels.size(); ++Index)
						{
							if (Index)
							{
								Out << " or ";
							}
							Out << ValueName << " == " << EmitExpr(*Case.Labels[Index]);
						}
						Out << ":\n";
					}
					bFirst = false;
					FLocalState Saved = SaveLocalState();
					const std::size_t Count = Case.Body->Statements.size();
					if (Count == 0)
					{
						Out << Pad(CaseIndent + 1) << "pass\n";
					}
					BreakTargets.push_back(EBreakTarget::Switch);
					for (std::size_t Index = 0; Index < Count; ++Index)
					{
						if (Case.Body->Statements[Index])
						{
							EmitStmt(*Case.Body->Statements[Index], CaseIndent + 1);
						}
					}
					BreakTargets.pop_back();
					RestoreLocalState(std::move(Saved));
				};
				// Python's else must be last even when the source default label is not.
				for (const FSwitchCase& Case : Node.Cases)
				{
					if (!Case.bIsDefault)
					{
						EmitCase(Case);
					}
					if (bFailed)
					{
						return;
					}
				}
				for (const FSwitchCase& Case : Node.Cases)
				{
					if (Case.bIsDefault)
					{
						EmitCase(Case);
					}
					if (bFailed)
					{
						return;
					}
				}
				if (bFirst)
				{
					Out << Pad(CaseIndent) << "pass\n";
				}
				Out << Pad(Indent) << "except _VsSwitchBreak:\n"
				    << Pad(Indent + 1) << "pass\n";
			}

			void EmitTopLevel()
			{
				CurrentTypeName.clear();
				CurrentFields.clear();
				CurrentStaticFields.clear();
				CurrentFieldTypes.clear();
				CurrentMethodReturnTypes.clear();
				ResetLocalState();
				bEmittingTopLevel = true;
				for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
				{
					if (Statement && Statement->Kind == EStmtKind::VarDecl)
					{
						const auto& Global = static_cast<const FVarDeclStmt&>(*Statement);
						ActiveModulePath = !Global.ModulePath.empty()
						    ? Global.ModulePath
						    : std::string(Global.Location.FileLabel);
						ActiveNamespace = NamespaceOfSymbol(Global.Name);
					}
					if (Statement)
					{
						EmitStmt(*Statement, 0);
					}
					if (bFailed)
					{
						return;
					}
				}
				bEmittingTopLevel = false;
				ActiveModulePath.clear();
				ActiveNamespace.clear();
				Out << "_vs_freeze_thread_defaults()\n";
				if (!Program.TopLevelStatements.empty())
				{
					Out << '\n';
				}
			}

			void EmitEntryPoint()
			{
				Out << "if __name__ == \"__main__\":\n"
				    << "\ttry:\n"
				    << "\t\ttry:\n"
				    << "\t\t\t___vs_result = _vs_script_main(";
				if (MainFunction && MainFunction->Parameters.size() == 1)
				{
					Out << "_VsArray(_vs_sys.argv[1:], type_name=\"array<string>\")";
				}
				Out << ")\n"
				    << "\t\texcept _VsExit as ___vs_exit:\n"
				    << "\t\t\traise SystemExit(___vs_exit.code) from None\n"
				    << "\t\tif isinstance(___vs_result, int):\n"
				    << "\t\t\traise SystemExit(___vs_result)\n"
				    << "\tfinally:\n"
				    << "\t\ttry:\n"
				    << "\t\t\t_vs_shutdown()\n"
				    << "\t\tfinally:\n"
				    << "\t\t\t_vs_console_restore()\n";
			}

			const FProgram& Program;
			const std::vector<FMethodBindingCodegenInfo>& MethodBindings;
			std::ostringstream Out;
			std::string Error;
			bool bFailed = false;
			std::unordered_map<std::string, std::string> Aliases;
			std::unordered_map<std::string, std::string> UnscopedEnumMembers;
			std::unordered_set<std::string> StructNames;
			std::unordered_set<std::string> TypeNames;
			std::unordered_set<std::string> EnumNames;
			std::unordered_set<std::string> GlobalNames;
			std::vector<const FVarDeclStmt*> GlobalDeclarations;
			std::unordered_map<std::string, std::string> GlobalTypes;
			std::unordered_map<std::string, std::string> FunctionReturnTypes;
			std::unordered_set<std::string> LocalNames;
			std::unordered_map<std::string, std::string> LocalTypes;
			std::unordered_map<std::string, std::string> LocalPythonNames;
			std::unordered_set<std::string> UsedPythonLocalNames;
			std::unordered_set<std::string> CurrentFields;
			std::unordered_set<std::string> CurrentStaticFields;
			std::unordered_map<std::string, std::string> CurrentFieldTypes;
			std::unordered_map<std::string, std::string> CurrentMethodReturnTypes;
			std::unordered_set<std::string> ActiveTypeParameters;
			std::string CurrentReturnType;
			std::string CurrentTypeName;
			std::string CurrentFunctionName;
			std::string CurrentFunctionModulePath;
			std::string ActiveModulePath;
			std::string ActiveNamespace;
			bool bInStaticMethod = false;
			bool bEmittingTopLevel = false;
			std::vector<EBreakTarget> BreakTargets;
			std::vector<EContinueTarget> ContinueTargets;
			std::uint32_t NextTempId = 0;
			const FFunctionDecl* MainFunction = nullptr;
			bool bHasStandardEvents = false;
		};

		FCompileProcessingOptions PythonCompileProcessingOptions(const FCodegenOptions& Options)
		{
			auto Processing = Options.CompileProcessing;
			if (Options.Bindings)
			{
				for (auto Definition : GetAnnotationDefinitions(*Options.Bindings))
				{
					Processing.AnnotationDefinitions.push_back(std::move(Definition));
				}
			}
			return Processing;
		}

		// Own text views before output mutation; callers may reuse result storage.
		bool EmitPythonFromSourceInternal(
		    std::string OwnedSource,
		    std::string Label,
		    std::string& OutPython,
		    std::string& OutError,
		    const std::vector<std::string>& ImportPaths,
		    const std::vector<std::string>& Defines,
		    const std::vector<FPredefinedMacro>& PredefinedMacros,
		    const std::vector<std::string>& HostTypeNames,
		    const std::vector<std::string>& ImplicitImportPaths,
		    const bool bAllowCircularImports,
		    const bool bAllowProjectWideAccess,
		    const FAnnotationProcessingOptions& Processing = {}, const FCodegenOptions* CompileOptions = nullptr,
		    const bool bFromFile = false)
		{
			OutPython.clear();
			OutError.clear();
			if (bFromFile && Label.empty())
			{
				OutError = "Python AOT: script path must not be empty";
				return false;
			}
			if (Label.empty())
			{
				Label = "aot.vs";
			}

			VeroScriptModuleLoader Loader(
			    Label, CompileOptions ? CompileOptions->CompileProcessing.ModuleEnvironment : nullptr);
			if (CompileOptions)
			{
				Loader.SetCompileProcessing(PythonCompileProcessingOptions(*CompileOptions));
				Loader.SetScriptCompileExtensions(CompileOptions->ScriptCompileExtensions);
				for (const auto& O : CompileOptions->SourceOverlays)
				{
					Loader.SetSourceOverlay(O.Path, O.Source);
				}
				for (const auto& P : CompileOptions->StdlibSearchPaths)
				{
					Loader.AddStdlibSearchRoot(P);
				}
			}
			for (const std::string& Path : ImportPaths)
			{
				Loader.AddImportPath(Path);
			}
			for (const std::string& Path : ImplicitImportPaths)
			{
				Loader.AddImplicitImportPath(Path);
			}
			Loader.SetAllowCircularImports(bAllowCircularImports);
			Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
			Loader.SetDefines(Defines);
			Loader.SetPredefinedMacros(PredefinedMacros);
			if (!HostTypeNames.empty())
			{
				Loader.SetHostTypeNames(std::unordered_set<std::string>(HostTypeNames.begin(), HostTypeNames.end()));
			}
			if (!bFromFile)
			{
				Loader.SetSourceOverlay(Label, OwnedSource);
			}

			std::unique_ptr<FProgram> Program =
			    bFromFile ? Loader.LoadMainModule() : Loader.LoadSourceModule(OwnedSource, Label);
			if (!Program || Loader.HasError())
			{
				OutError = Loader.HasError() ? Loader.GetError().Message : "Python AOT: module load failed";
				return false;
			}
			auto Prepared = PrepareCodegenProgram(std::move(Program), Processing, nullptr, OutError);
			if (!Prepared) return false;
			return EmitPythonFromProgram(*Prepared, OutPython, OutError);
		}

		bool ValidatePythonBundleName(
		    const std::string_view BundleName,
		    std::string& OutError)
		{
			if (BundleName.empty())
			{
				OutError = "Python AOT: source-bundle name must not be empty";
				return false;
			}
			for (const unsigned char Character : BundleName)
			{
				if (!(std::isalnum(Character) || Character == '_' || Character == '-'))
				{
					OutError = "Python AOT: source-bundle name must be a portable file stem";
					return false;
				}
			}
			return true;
		}

		std::string JsonEscape(const std::string_view Value)
		{
			return JsonStringDetail::Escape(Value, JsonStringDetail::EControlEscapeStyle::Unicode);
		}

		bool BuildPythonBundleParts(
		    std::string Program,
		    std::string Runtime,
		    std::string BundleName,
		    FPythonAotSourceBundle& OutBundle,
		    std::string& OutError)
		{
			OutBundle = {};
			OutError.clear();
			if (!ValidatePythonBundleName(BundleName, OutError))
			{
				return false;
			}
			if (Program.empty())
			{
				OutError = "Python AOT: generated Python source must not be empty";
				return false;
			}
			const std::string Entry =
			    "# SPDX-FileCopyrightText: VanstySanic\n# SPDX-License-Identifier: MIT\n\n# Generated by VeroScriptPythonAot.\n"
			    "import runpy as _vs_runpy\n\n"
			    "_vs_runpy.run_module(\n"
			    "\tf\"{__package__}._veroscript_program\" if __package__ "
			    "else \"_veroscript_program\",\n"
			    "\trun_name=\"__main__\",\n"
			    ")\n";
			OutBundle.Files.push_back({"__main__.py", Entry});
			OutBundle.Files.push_back({"__init__.py",
			    "\"\"\"Generated standalone VeroScript Python AOT package.\"\"\"\n"});
			if (!Runtime.empty())
			{
				OutBundle.Files.push_back(
				    {"_veroscript_runtime.py", std::move(Runtime)});
			}
			OutBundle.Files.push_back(
			    {"_veroscript_program.py", std::move(Program)});

			std::ostringstream Manifest;
			Manifest << "{\"version\":1,\"kind\":\"veroscript.python-aot-source-bundle\",";
			Manifest << "\"name\":\"" << JsonEscape(BundleName)
			         << "\",\"entry\":\"__main__.py\",\"files\":[";
			for (std::size_t Index = 0; Index < OutBundle.Files.size(); ++Index)
			{
				if (Index)
				{
					Manifest << ',';
				}
				Manifest << '"' << JsonEscape(OutBundle.Files[Index].RelativePath) << '"';
			}
			Manifest << "],\"run\":\"python <output-directory>\"}";
			OutBundle.ManifestJson = Manifest.str();
			return true;
		}
	}

	bool EmitPythonFromProgram(const FProgram& InputProgram, std::string& OutPython, std::string& OutError)
	{
		OutPython.clear();
		OutError.clear();
		auto PreparedInput = PrepareCodegenProgram(InputProgram, {}, nullptr, OutError);
		if (!PreparedInput) return false;
		const auto& Program = *PreparedInput;

		std::vector<FMethodBindingCodegenInfo> MethodBindings;
		std::unique_ptr<FBytecodeModule> CheckedModule;
		if (!CompileCheck(Program, OutError, MethodBindings, CheckedModule))
		{
			return false;
		}
		auto ConstantProgram = CloneProgramWithConstantValues(Program, *CheckedModule, OutError);
		if (!ConstantProgram) return false;
		if (HasScopedTypeDeclarations(*ConstantProgram))
		{
			OutError = "AOT: nested and local types require the VeroScript runtime; standalone output is unsupported";
			return false;
		}
		if (Variant::UsesRuntime(*ConstantProgram))
		{
			OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
			return false;
		}
		FPythonEmitter Emitter(*ConstantProgram, MethodBindings);
		return Emitter.Emit(OutPython, OutError);
	}

	bool BuildPythonSourceBundle(
	    const std::string_view GeneratedPython,
	    const std::string_view BundleName,
	    FPythonAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		return BuildPythonBundleParts(
		    std::string(GeneratedPython), {}, std::string(BundleName), OutBundle, OutError);
	}

	bool EmitPythonSourceBundleFromProgram(
	    const FProgram& InputProgram,
	    const std::string_view BundleName,
	    FPythonAotSourceBundle& OutBundle,
	    std::string& OutError)
	{
		const std::string Name(BundleName);
		OutBundle = {};
		OutError.clear();
		auto PreparedInput = PrepareCodegenProgram(InputProgram, {}, nullptr, OutError);
		if (!PreparedInput) return false;
		const auto& Program = *PreparedInput;

		std::vector<FMethodBindingCodegenInfo> MethodBindings;
		std::unique_ptr<FBytecodeModule> CheckedModule;
		if (!CompileCheck(Program, OutError, MethodBindings, CheckedModule))
		{
			return false;
		}
		auto ConstantProgram = CloneProgramWithConstantValues(Program, *CheckedModule, OutError);
		if (!ConstantProgram) return false;
		if (HasScopedTypeDeclarations(*ConstantProgram))
		{
			OutError = "AOT: nested and local types require the VeroScript runtime; standalone output is unsupported";
			return false;
		}
		if (Variant::UsesRuntime(*ConstantProgram))
		{
			OutError = "diag.aot_variant_unsupported: variant requires the VeroScript runtime; standalone output is unsupported";
			return false;
		}
		FPythonEmitter Emitter(*ConstantProgram, MethodBindings);
		std::string Complete;
		std::string Runtime;
		std::string ProgramSource;
		if (!Emitter.Emit(Complete, OutError, &Runtime, &ProgramSource))
		{
			return false;
		}
		return BuildPythonBundleParts(
		    std::move(ProgramSource), std::move(Runtime), Name, OutBundle, OutError);
	}

	bool EmitPythonFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    std::string& OutPython,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		return EmitPythonFromSourceInternal(std::string(Source), std::string(SourceLabel), OutPython, OutError,
		    ImportPaths, Defines, {}, {}, ImplicitImportPaths,
		    bAllowCircularImports, bAllowProjectWideAccess);
	}

	static bool EmitPythonSourceBundleWithOptions(std::string Source, std::string Label,
	    std::string Name, const FCodegenOptions& Options, FPythonAotSourceBundle& Bundle,
	    std::string& Error, const bool bFromFile)
	{
		if (bFromFile && Label.empty())
		{
			Bundle = {};
			Error = "Python AOT: script path must not be empty";
			return false;
		}
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			Bundle = {};
			Error.clear();
			if (!ApplyCodegenModuleEnvironment(Effective, Error)) return false;
			return EmitPythonSourceBundleWithOptions(std::move(Source), std::move(Label), std::move(Name), Effective, Bundle, Error, bFromFile);
		}
		Bundle = {};
		Error.clear();
		if (Options.Bindings &&
		    (!Options.Bindings->GetTypes().empty() || !Options.Bindings->GetFunctions().empty()))
		{
			Error = "Python AOT: standalone output cannot use runtime host bindings";
			return false;
		}
		VeroScriptModuleLoader Loader(Label, Options.CompileProcessing.ModuleEnvironment);
		Loader.SetCompileProcessing(PythonCompileProcessingOptions(Options));
		Loader.SetScriptCompileExtensions(Options.ScriptCompileExtensions);
		Loader.SetDefines(Options.Defines);
		Loader.SetPredefinedMacros(Options.PredefinedMacros);
		Loader.SetAllowCircularImports(Options.bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(Options.bAllowProjectWideAccess);
		for (const auto& P : Options.ImportPaths)
		{
			Loader.AddImportPath(P);
		}
		for (const auto& P : Options.StdlibSearchPaths)
		{
			Loader.AddStdlibSearchRoot(P);
		}
		for (const auto& P : Options.ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(P);
		}
		for (const auto& O : Options.SourceOverlays)
		{
			Loader.SetSourceOverlay(O.Path, O.Source);
		}

		auto Program = bFromFile ? Loader.LoadMainModule() : Loader.LoadSourceModule(Source, Label);
		if (!Program || Loader.HasError())
		{
			Error = Loader.HasError() ? Loader.GetError().Message : "Python AOT: module load failed";
			return false;
		}
		auto Prepared =
		    PrepareCodegenProgram(std::move(Program), Options.AnnotationProcessing, nullptr, Error);
		return Prepared && EmitPythonSourceBundleFromProgram(*Prepared, Name, Bundle, Error);
	}
	bool EmitPythonSourceBundleFromSource(std::string_view Source, std::string_view Label, std::string_view Name,
	    const FCodegenOptions& Options, FPythonAotSourceBundle& Bundle, std::string& Error)
	{
		return EmitPythonSourceBundleWithOptions(std::string(Source), std::string(Label.empty() ? "aot.vs" : Label),
		    std::string(Name), Options, Bundle, Error, false);
	}
	bool EmitPythonSourceBundleFromFile(std::string_view Path, std::string_view Name, const FCodegenOptions& Options,
	    FPythonAotSourceBundle& Bundle, std::string& Error)
	{
		return EmitPythonSourceBundleWithOptions({}, std::string(Path), std::string(Name), Options, Bundle, Error, true);
	}
	bool EmitPythonSourceBundleFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const std::string_view BundleName,
	    FPythonAotSourceBundle& OutBundle,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		const std::string OwnedSource(Source);
		const std::string Label = SourceLabel.empty()
		    ? "aot.vs"
		    : std::string(SourceLabel);
		const std::string Name(BundleName);
		OutBundle = {};
		OutError.clear();
		VeroScriptModuleLoader Loader(Label);
		for (const std::string& Path : ImportPaths)
		{
			Loader.AddImportPath(Path);
		}
		for (const std::string& Path : ImplicitImportPaths)
		{
			Loader.AddImplicitImportPath(Path);
		}
		Loader.SetAllowCircularImports(bAllowCircularImports);
		Loader.SetAllowProjectWideAccess(bAllowProjectWideAccess);
		Loader.SetDefines(Defines);
		Loader.SetSourceOverlay(Label, OwnedSource);
		std::unique_ptr<FProgram> Program =
		    Loader.LoadSourceModule(OwnedSource, Label);
		if (!Program || Loader.HasError())
		{
			OutError = Loader.HasError()
			    ? Loader.GetError().Message
			    : "Python AOT: module load failed";
			return false;
		}
		auto Prepared = PrepareCodegenProgram(std::move(Program), {}, nullptr, OutError);
		return Prepared && EmitPythonSourceBundleFromProgram(*Prepared, Name, OutBundle, OutError);
	}

	static bool ReadPythonScript(
	    const std::string& Path, std::string& OutSource, std::string& OutError)
	{
		const std::filesystem::path NativePath = PathUtil::FromUtf8(Path);
		std::error_code StatusError;
		if (std::filesystem::is_directory(NativePath, StatusError))
		{
			OutError = "Python AOT: cannot open script: " + Path;
			return false;
		}
		std::ifstream Input(NativePath, std::ios::binary);
		if (!Input)
		{
			OutError = "Python AOT: cannot open script: " + Path;
			return false;
		}
		// Stream extraction must observe input errors before emitting any output.
		char Chunk[8192];
		while (Input.read(Chunk, sizeof(Chunk)) || Input.gcount() > 0)
		{
			OutSource.append(Chunk, static_cast<std::size_t>(Input.gcount()));
		}
		if (Input.bad() || !Input.eof())
		{
			OutError = "Python AOT: failed while reading script: " + Path;
			return false;
		}
		return true;
	}

	bool EmitPythonFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view OutputPath,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		const std::string Path(ScriptPath), OutPath(OutputPath);
		OutError.clear();
		std::string Source;
		if (!ReadPythonScript(Path, Source, OutError)) return false;
		std::string Python;
		if (!EmitPythonFromSource(Source, Path, Python, OutError, ImportPaths,
		        Defines, ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess))
		{
			return false;
		}
		const std::filesystem::path FinalPath = PathUtil::FromUtf8(OutPath);
		const std::filesystem::path TempPath = PathUtil::MakeTemporarySiblingPath(FinalPath);
		std::ofstream Output(TempPath, std::ios::binary | std::ios::trunc);
		if (!Output)
		{
			OutError = "Python AOT: cannot write output: " + OutPath;
			return false;
		}
		Output << Python;
		Output.flush();
		if (!Output)
		{
			Output.close();
			std::error_code RemoveError;
			std::filesystem::remove(TempPath, RemoveError);
			OutError = "Python AOT: failed while writing output: " + OutPath;
			return false;
		}
		Output.close();
		if (!Output)
		{
			std::error_code RemoveError;
			std::filesystem::remove(TempPath, RemoveError);
			OutError = "Python AOT: failed while closing output: " + OutPath;
			return false;
		}
		std::error_code ReplaceError;
		if (!PathUtil::ReplaceFileAtomically(TempPath, FinalPath, ReplaceError))
		{
			std::error_code RemoveError;
			std::filesystem::remove(TempPath, RemoveError);
			OutError = "Python AOT: cannot replace output: " + OutPath;
			return false;
		}
		return true;
	}

	bool EmitPythonSourceBundleFromFile(
	    const std::string_view ScriptPath,
	    const std::string_view BundleName,
	    FPythonAotSourceBundle& OutBundle,
	    std::string& OutError,
	    const std::vector<std::string>& ImportPaths,
	    const std::vector<std::string>& Defines,
	    const std::vector<std::string>& ImplicitImportPaths,
	    const bool bAllowCircularImports,
	    const bool bAllowProjectWideAccess)
	{
		const std::string Path(ScriptPath), Name(BundleName);
		OutBundle = {};
		OutError.clear();
		std::string Source;
		if (!ReadPythonScript(Path, Source, OutError)) return false;
		return EmitPythonSourceBundleFromSource(
		    Source, Path, Name, OutBundle, OutError,
		    ImportPaths, Defines, ImplicitImportPaths,
		    bAllowCircularImports, bAllowProjectWideAccess);
	}

	bool WritePythonSourceBundle(
	    const FPythonAotSourceBundle& Bundle,
	    const std::string_view OutputDirectory,
	    std::string& OutError)
	{
		const std::string Directory(OutputDirectory);
		OutError.clear();
		if (Directory.empty() || Bundle.Files.empty() || Bundle.ManifestJson.empty())
		{
			OutError = "Python AOT: source bundle and output directory must not be empty";
			return false;
		}
		std::error_code Error;
		std::filesystem::path Final = std::filesystem::absolute(
		    PathUtil::FromUtf8(Directory), Error)
		                                  .lexically_normal();
		if (Error || Final.empty() || Final == Final.root_path())
		{
			OutError = "Python AOT: unsafe source-bundle output directory";
			return false;
		}
		const std::filesystem::path CurrentDirectory =
		    std::filesystem::current_path(Error);
		if (Error || Final == CurrentDirectory)
		{
			OutError = "Python AOT: unsafe source-bundle output directory";
			return false;
		}
		const std::filesystem::file_status FinalStatus =
		    std::filesystem::symlink_status(Final, Error);
		if (Error && Error != std::errc::no_such_file_or_directory)
		{
			OutError = "Python AOT: cannot inspect source-bundle output directory";
			return false;
		}
		Error.clear();
		const bool bFinalExists = std::filesystem::exists(FinalStatus);
		if (bFinalExists)
		{
			if (std::filesystem::is_symlink(FinalStatus))
			{
				OutError = "Python AOT: source-bundle output must not be a symlink";
				return false;
			}
			if (!std::filesystem::is_directory(FinalStatus))
			{
				OutError = "Python AOT: existing source-bundle output must be a regular directory";
				return false;
			}
			const bool bEmpty = std::filesystem::is_empty(Final, Error);
			const std::filesystem::path ExistingManifest =
			    Final / "python-aot.manifest.json";
			if (Error || (!bEmpty && !std::filesystem::is_regular_file(ExistingManifest, Error)))
			{
				OutError = "Python AOT: refusing to replace a non-bundle directory";
				return false;
			}
			if (!bEmpty)
			{
				const std::uintmax_t ManifestSize =
				    std::filesystem::file_size(ExistingManifest, Error);
				if (Error || ManifestSize > 1024 * 1024)
				{
					OutError = "Python AOT: refusing to replace a directory without an owned manifest";
					return false;
				}
				std::ifstream ManifestInput(ExistingManifest, std::ios::binary);
				std::ostringstream ManifestBuffer;
				ManifestBuffer << ManifestInput.rdbuf();
				const std::string ExistingManifestJson = ManifestBuffer.str();
				constexpr std::string_view OwnedManifestPrefix =
				    "{\"version\":1,\"kind\":\"veroscript.python-aot-source-bundle\"";
				if ((!ManifestInput.good() && !ManifestInput.eof()) || ExistingManifestJson.compare(0, OwnedManifestPrefix.size(), OwnedManifestPrefix) != 0)
				{
					OutError = "Python AOT: refusing to replace a directory without an owned manifest";
					return false;
				}
				constexpr std::string_view FilesWithoutRuntime =
				    "\"entry\":\"__main__.py\",\"files\":[\"__main__.py\","
				    "\"__init__.py\",\"_veroscript_program.py\"]";
				constexpr std::string_view FilesWithRuntime =
				    "\"entry\":\"__main__.py\",\"files\":[\"__main__.py\","
				    "\"__init__.py\",\"_veroscript_runtime.py\","
				    "\"_veroscript_program.py\"]";
				const bool bHasRuntime = ExistingManifestJson.find(FilesWithRuntime) != std::string::npos;
				if (!bHasRuntime && ExistingManifestJson.find(FilesWithoutRuntime) == std::string::npos)
				{
					OutError = "Python AOT: refusing to replace a directory with an invalid owned manifest";
					return false;
				}
				std::unordered_set<std::string> ExpectedFiles = {
				    "__main__.py",
				    "__init__.py",
				    "_veroscript_program.py",
				    "python-aot.manifest.json",
				};
				if (bHasRuntime)
				{
					ExpectedFiles.insert("_veroscript_runtime.py");
				}
				std::unordered_set<std::string> ExistingFiles;
				for (std::filesystem::directory_iterator Iterator(Final, Error), End;
				    !Error && Iterator != End; Iterator.increment(Error))
				{
					const std::filesystem::directory_entry& Entry = *Iterator;
					const std::string Name = PathUtil::ToUtf8(Entry.path().filename());
					const std::filesystem::file_status Status = Entry.symlink_status(Error);
					if (!Error && Name == "__pycache__" && std::filesystem::is_directory(Status))
					{
						for (std::filesystem::directory_iterator CacheIterator(
						         Entry.path(), Error),
						    CacheEnd;
						    !Error && CacheIterator != CacheEnd; CacheIterator.increment(Error))
						{
							const std::filesystem::directory_entry& CacheEntry = *CacheIterator;
							const std::filesystem::file_status CacheStatus =
							    CacheEntry.symlink_status(Error);
							const std::string CacheName =
							    PathUtil::ToUtf8(CacheEntry.path().filename());
							const bool bKnownStem = CacheName.rfind("__main__.cpython-", 0) == 0 || CacheName.rfind("__init__.cpython-", 0) == 0 || CacheName.rfind("_veroscript_runtime.cpython-", 0) == 0 || CacheName.rfind("_veroscript_program.cpython-", 0) == 0;
							if (Error || std::filesystem::is_symlink(CacheStatus) || !std::filesystem::is_regular_file(CacheStatus) || !bKnownStem || CacheName.size() < 4 || CacheName.compare(CacheName.size() - 4, 4, ".pyc") != 0)
							{
								OutError = "Python AOT: refusing to replace a source-bundle cache with foreign content";
								return false;
							}
						}
						if (Error)
						{
							OutError = "Python AOT: cannot inspect the source-bundle bytecode cache";
							return false;
						}
						continue;
					}
					if (Error || std::filesystem::is_symlink(Status) || !std::filesystem::is_regular_file(Status) || ExpectedFiles.find(Name) == ExpectedFiles.end() || !ExistingFiles.insert(Name).second)
					{
						OutError = "Python AOT: refusing to replace a source-bundle directory with foreign content";
						return false;
					}
				}
				if (Error || ExistingFiles != ExpectedFiles)
				{
					OutError = "Python AOT: refusing to replace an incomplete source-bundle directory";
					return false;
				}
			}
		}

		const std::filesystem::path Temporary = PathUtil::MakeTemporarySiblingPath(Final);
		std::filesystem::path Backup = Temporary;
		Backup += ".previous";
		std::filesystem::create_directories(Final.parent_path(), Error);
		if (Error)
		{
			OutError = "Python AOT: cannot create bundle parent directory: " + Error.message();
			return false;
		}
		const bool bCreatedTemporary =
		    std::filesystem::create_directory(Temporary, Error);
		if (Error || !bCreatedTemporary)
		{
			OutError = "Python AOT: cannot stage source bundle: " + Error.message();
			return false;
		}

		std::unordered_set<std::string> RelativePaths;
		auto WriteFile = [&](const std::string& Relative, const std::string_view Contents)
		{
			if (Contents.size() > static_cast<std::size_t>(
			                          std::numeric_limits<std::streamsize>::max()) ||
			    Relative.empty() || Relative.find('\\') != std::string::npos || Relative.find('\0') != std::string::npos || Relative.back() == '/')
			{
				return false;
			}
			std::filesystem::path RelativePath =
			    PathUtil::FromUtf8(Relative).lexically_normal();
			if (RelativePath.empty() || RelativePath.is_absolute() || RelativePath.has_root_name() || Relative.find(':') != std::string::npos)
			{
				return false;
			}
			for (const auto& Part : RelativePath)
			{
				if (Part == ".." || Part == ".")
				{
					return false;
				}
			}
			std::string Key = PathUtil::ToUtf8(RelativePath);
			#if defined(_WIN32)
			std::transform(Key.begin(), Key.end(), Key.begin(),
			    [](const unsigned char Character)
			{
				return static_cast<char>(std::tolower(Character));
			});
			#endif
			if (!RelativePaths.insert(Key).second)
			{
				return false;
			}
			const std::filesystem::path Destination = Temporary / RelativePath;
			std::filesystem::create_directories(Destination.parent_path(), Error);
			if (Error)
			{
				return false;
			}
			std::ofstream Output(Destination, std::ios::binary | std::ios::trunc);
			if (!Output)
			{
				return false;
			}
			Output.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
			Output.flush();
			return static_cast<bool>(Output);
		};
		for (const FPythonAotSourceFile& File : Bundle.Files)
		{
			if (!WriteFile(File.RelativePath, File.Contents))
			{
				std::filesystem::remove_all(Temporary, Error);
				OutError = "Python AOT: invalid or unwritable source-bundle file";
				return false;
			}
		}
		if (!WriteFile("python-aot.manifest.json", Bundle.ManifestJson + "\n"))
		{
			std::filesystem::remove_all(Temporary, Error);
			OutError = "Python AOT: cannot write source-bundle manifest";
			return false;
		}

		const bool bHadOriginal = std::filesystem::exists(Final, Error);
		if (Error)
		{
			std::filesystem::remove_all(Temporary, Error);
			OutError = "Python AOT: cannot inspect bundle output";
			return false;
		}
		if (bHadOriginal)
		{
			std::filesystem::rename(Final, Backup, Error);
			if (Error)
			{
				std::filesystem::remove_all(Temporary, Error);
				OutError = "Python AOT: cannot stage previous source bundle";
				return false;
			}
		}
		std::filesystem::rename(Temporary, Final, Error);
		if (Error)
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Temporary, Ignored);
			if (bHadOriginal)
			{
				std::filesystem::rename(Backup, Final, Ignored);
			}
			OutError = "Python AOT: cannot replace source bundle: " + Error.message();
			return false;
		}
		if (bHadOriginal)
		{
			std::error_code Ignored;
			std::filesystem::remove_all(Backup, Ignored);
		}
		return true;
	}

	static bool EmitPythonWithOptions(
	    std::string Source,
	    std::string Label,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult,
	    const bool bFromFile)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			OutResult = {};
			if (!ApplyCodegenModuleEnvironment(Effective, OutResult.Error)) return false;
			return EmitPythonWithOptions(std::move(Source), std::move(Label), Effective, OutResult, bFromFile);
		}
		OutResult = {};
		if (Options.Bindings && (!Options.Bindings->GetFunctions().empty() || !Options.Bindings->GetTypes().empty()))
		{
			OutResult.Error = "Python AOT: host bindings are not available in standalone Python output";
			return false;
		}
		OutResult.bSuccess = EmitPythonFromSourceInternal(std::move(Source), std::move(Label),
		    OutResult.Output, OutResult.Error, Options.ImportPaths, Options.Defines, Options.PredefinedMacros,
		    {}, Options.ImplicitImportPaths, Options.bAllowCircularImports, Options.bAllowProjectWideAccess,
		    Options.AnnotationProcessing, &Options, bFromFile);
		if (!OutResult.bSuccess)
		{
			OutResult.Output.clear();
		}

		return OutResult.bSuccess;
	}

	bool FPythonCodegenBackend::EmitFromSource(
	    const std::string_view Source,
	    const std::string_view SourceLabel,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		return EmitPythonWithOptions(std::string(Source), std::string(SourceLabel), Options, OutResult, false);
	}

	bool FPythonCodegenBackend::EmitFromFile(
	    const std::string_view ScriptPath,
	    const FCodegenOptions& Options,
	    FCodegenResult& OutResult)
	{
		return EmitPythonWithOptions({}, std::string(ScriptPath), Options, OutResult, true);
	}
}
