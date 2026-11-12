// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#define PY_SSIZE_T_CLEAN
// A stock Windows CPython distribution does not ship pythonXY_d.lib. Keep the
// extension compatible with that release ABI even in a Debug CMake build.
#if defined(_WIN32) && defined(_DEBUG)
#define VS_PYTHON_RESTORE_DEBUG
#undef _DEBUG
#endif
#include <Python.h>

#if defined(VEROSCRIPT_PYTHON_MOBILE_BUILD)
#if PY_VERSION_HEX < 0x030D0000 || PY_VERSION_HEX >= 0x030F0000 || defined(Py_DEBUG) || defined(Py_GIL_DISABLED)
#error Mobile VeroScript requires normal CPython 3.13 or 3.14 target headers.
#endif
#endif
#ifndef VEROSCRIPT_PYTHON_BUILD_ID
#define VEROSCRIPT_PYTHON_BUILD_ID ""
#endif
#if defined(VS_PYTHON_RESTORE_DEBUG)
#define _DEBUG
#undef VS_PYTHON_RESTORE_DEBUG
#endif

#include "VeroScriptInterop.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

static int InitializeVeroScriptModule(PyObject* ModuleObject);

namespace
{
	thread_local vs_interop_call_context* ActiveMemoryCall = nullptr;
	struct FActiveMemoryCall
	{
		explicit FActiveMemoryCall(vs_interop_call_context* Call) : Previous(ActiveMemoryCall)
		{
			ActiveMemoryCall = Call;
		}

		~FActiveMemoryCall()
		{
			ActiveMemoryCall = Previous;
		}

		vs_interop_call_context* Previous;
	};
	void VS_INTEROP_CALL DestroyPythonHostObject(void* Payload);
	uint8_t VS_INTEROP_CALL IsPythonHostObjectType(
	    void* Payload,
	    void* UserData,
	    vs_interop_string_view ExpectedTypeName);

	PyObject* GError = nullptr;
	PyObject* GThreadError = nullptr;
	PyObject* GTypeMismatchError = nullptr;
	PyObject* GCallbackError = nullptr;
	// Synchronous callbacks report to the invoking Python operation, which may
	// use an escaped host value owned by a different or already closed session.
	thread_local PyObject** ActiveCallbackError = nullptr;
	PyInterpreterState* GMainInterpreter = nullptr;

	PyTypeObject GValueType = {PyVarObject_HEAD_INIT(nullptr, 0)};
	PyTypeObject GPackageType = {PyVarObject_HEAD_INIT(nullptr, 0)};
	PyTypeObject GCallableType = {PyVarObject_HEAD_INIT(nullptr, 0)};
	PyTypeObject GSessionType = {PyVarObject_HEAD_INIT(nullptr, 0)};
	PyMappingMethods GValueMapping = {};
	PySequenceMethods GValueSequence = {};

	bool CheckInterpreter()
	{
		if (GMainInterpreter == nullptr || PyInterpreterState_Get() == GMainInterpreter)
		{
			return true;
		}
		PyErr_SetString(PyExc_RuntimeError,
		    "VeroScriptPython objects may only be used in the main CPython interpreter");
		return false;
	}

	struct FPyValue
	{
		PyObject_HEAD vs_interop_value* Handle = nullptr;
	};

	struct FPyPackage
	{
		PyObject_HEAD vs_interop_package* Handle = nullptr;
	};

	struct FPyCallable
	{
		PyObject_HEAD vs_interop_callable* Handle = nullptr;
		PyObject* SessionObject = nullptr;
		unsigned long OwnerThread = 0;
	};

	struct FPyOwnedRef
	{
		explicit FPyOwnedRef(PyObject* InObject = nullptr) : Object(InObject)
		{
		}

		~FPyOwnedRef()
		{
			Py_XDECREF(Object);
		}

		FPyOwnedRef(const FPyOwnedRef&) = delete;

		FPyOwnedRef& operator=(const FPyOwnedRef&) = delete;

		PyObject* Get() const
		{
			return Object;
		}

		PyObject* Release()
		{
			PyObject* Result = Object;
			Object = nullptr;
			return Result;
		}

		PyObject* Object = nullptr;
	};

	struct FPyGilGuard
	{
		FPyGilGuard() = default;

		~FPyGilGuard()
		{
			PyGILState_Release(State);
		}

		FPyGilGuard(const FPyGilGuard&) = delete;

		FPyGilGuard& operator=(const FPyGilGuard&) = delete;

		PyGILState_STATE State = PyGILState_Ensure();
	};

	PyObject* SnapshotSequence(PyObject* Object, const char* Message)
	{
		FPyOwnedRef Sequence(PySequence_Fast(Object, Message));
		// Numeric and truth conversions can run Python and mutate an input list.
		// Keep the items alive and the indexed length stable across those calls.
		return Sequence.Get() != nullptr ? PySequence_Tuple(Sequence.Get()) : nullptr;
	}

	PyObject* CheckedMappingItems(PyObject* Mapping)
	{
		FPyOwnedRef Items(PyMapping_Items(Mapping));
		if (Items.Get() == nullptr)
		{
			return nullptr;
		}
		for (Py_ssize_t Index = 0; Index < PyList_GET_SIZE(Items.Get()); ++Index)
		{
			PyObject* Item = PyList_GET_ITEM(Items.Get(), Index);
			if (!PyTuple_Check(Item) || PyTuple_GET_SIZE(Item) != 2)
			{
				PyErr_SetString(PyExc_TypeError, "mapping items must be (key, value) tuples");
				return nullptr;
			}
		}
		return Items.Release();
	}

	struct FHostFunctionState;
	struct FHostTypeState;
	struct FHostMemberState;
	struct FHostProtocolCallbackState;
	struct FGenericHostTypeState;
	struct FSerializerState;
	struct FHostTypeRegistry
	{
		// Values can outlive their originating session. Weak type references keep
		// compatibility checks independent of its callback graph without retaining
		// unrelated Python classes (or their session reference cycles).
		FPyOwnedRef Types{PyDict_New()};
		std::size_t RegisteredTypeCount = 0;
	};

	// Accessed under the GIL. Membership proves the payload layout without
	// relying on type names or session-local IDs, including after producer close.
	std::unordered_set<const void*> GPythonHostPayloads;
	struct FHostObjectPayload
	{
		~FHostObjectPayload()
		{
			GPythonHostPayloads.erase(this);
		}

		std::shared_ptr<FHostTypeRegistry> Registry;
		FPyOwnedRef Object;
		// A separate Python reference exposes native value ownership to cyclic GC.
		// The per-type list also covers values created before protocol registration.
		FPyOwnedRef ProtocolCallbacks;
	};

	struct FCallbackState
	{
		std::size_t Owners = 1;
		std::atomic_size_t ActiveOperations{0};
		PyObject* PrintCallback = nullptr;
		PyObject* DiagnosticCallback = nullptr;
		PyObject* ConsoleQueryCallback = nullptr;
		PyObject* ConsoleControlCallback = nullptr;
		PyObject* ConsoleWriteCallback = nullptr;
		PyObject* InstanceInterceptor = nullptr;
		PyObject* MutationCallback = nullptr;
		PyObject* CallbackError = nullptr;
		std::vector<FHostFunctionState*> HostFunctions;
		std::vector<FHostTypeState*> HostTypes;
		std::vector<FHostMemberState*> HostMembers;
		std::vector<FHostProtocolCallbackState*> HostProtocolCallbacks;
		std::vector<FGenericHostTypeState*> GenericHostTypes;
		std::vector<FSerializerState*> Serializers;
		std::shared_ptr<FHostTypeRegistry> HostTypeRegistry;
	};

	// Core destruction may succeed during an active call. The Python adapter
	// rejects that request because native code still references this callback graph.
	// Worker callbacks can release the GIL before this scope ends.
	struct FCallbackActivity
	{
		explicit FCallbackActivity(FCallbackState* InState) : State(InState)
		{
			if (State != nullptr)
			{
				State->ActiveOperations.fetch_add(1, std::memory_order_relaxed);
			}
		}
		~FCallbackActivity()
		{
			Finish();
		}
		void Finish()
		{
			if (auto* Finished = std::exchange(State, nullptr))
			{
				Finished->ActiveOperations.fetch_sub(1, std::memory_order_relaxed);
			}
		}
		FCallbackActivity(const FCallbackActivity&) = delete;
		FCallbackActivity& operator=(const FCallbackActivity&) = delete;
		FCallbackState* State;
	};

	struct FHostFunctionState
	{
		~FHostFunctionState()
		{
			Py_XDECREF(Callable);
		}

		FCallbackState* Owner = nullptr;
		PyObject* Callable = nullptr;
		vs_interop_native_type ReturnType = VS_INTEROP_NATIVE_VOID;
		std::string ReturnTypeName;
		std::vector<vs_interop_native_type> ParameterTypes;
		std::vector<std::uint32_t> ParameterFlags;
	};

	struct FHostTypeState
	{
		~FHostTypeState()
		{
			Py_XDECREF(PythonType);
		}

		FCallbackState* Owner = nullptr;
		std::uint32_t TypeId = 0;
		std::string Name;
		PyObject* PythonType = nullptr;
		FPyOwnedRef ProtocolCallbacks;
	};

	struct FHostMemberState
	{
		~FHostMemberState()
		{
			Py_XDECREF(Callable);
		}

		FCallbackState* Owner = nullptr;
		FHostTypeState* HostType = nullptr;
		PyObject* Callable = nullptr;
		vs_interop_host_member_kind Kind = VS_INTEROP_HOST_METHOD;
		vs_interop_native_type ReturnType = VS_INTEROP_NATIVE_VOID;
		std::string ReturnTypeName;
		std::vector<vs_interop_native_type> ParameterTypes;
	};

	enum class EPythonHostProtocolKind : std::uint8_t
	{
		Clone,
		Equals,
		Hash,
		SubclassFactory
	};

	struct FHostProtocolCallbackState
	{
		~FHostProtocolCallbackState()
		{
			Py_XDECREF(Callable);
		}

		FCallbackState* Owner = nullptr;
		FHostTypeState* HostType = nullptr;
		PyObject* Callable = nullptr;
		EPythonHostProtocolKind Kind = EPythonHostProtocolKind::Clone;
	};

	struct FGenericHostTypeState
	{
		~FGenericHostTypeState()
		{
			Py_XDECREF(Factory);
		}

		FCallbackState* Owner = nullptr;
		std::uint32_t TemplateId = 0;
		std::string Name;
		PyObject* Factory = nullptr;
	};

	struct FSerializerState
	{
		~FSerializerState()
		{
			Py_XDECREF(Serialize);
			Py_XDECREF(Deserialize);
		}

		FCallbackState* Owner = nullptr;
		PyObject* Serialize = nullptr;
		PyObject* Deserialize = nullptr;
	};

	struct FPySession
	{
		PyObject_HEAD vs_interop_session* Handle = nullptr;
		FCallbackState* Callbacks = nullptr;
		unsigned long OwnerThread = 0;
	};

	vs_interop_string_view View(const char* Data, const Py_ssize_t Size)
	{
		return {Data, static_cast<std::uint64_t>(Size)};
	}

	vs_interop_string_view View(const std::string& Text)
	{
		return {Text.data(), static_cast<std::uint64_t>(Text.size())};
	}

	bool IsValidByteView(const vs_interop_byte_view Bytes)
	{
		return Bytes.size == 0 || Bytes.data != nullptr;
	}

	const char* ByteData(const vs_interop_byte_view Bytes)
	{
		return Bytes.size == 0 ? "" : reinterpret_cast<const char*>(Bytes.data);
	}

	PyObject* UnicodeFromView(const vs_interop_string_view Text)
	{
		if (Text.size != 0 && Text.data == nullptr)
		{
			PyErr_SetString(GError, "VeroScript returned an invalid string view");
			return nullptr;
		}
		if (Text.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "VeroScript string is too large for Python");
			return nullptr;
		}
		return PyUnicode_DecodeUTF8(Text.size == 0 ? "" : Text.data,
		    static_cast<Py_ssize_t>(Text.size), "strict");
	}

	const char* StatusName(const vs_interop_status Status)
	{
		switch (Status)
		{
		case VS_INTEROP_STATUS_OK:
			return "ok";
		case VS_INTEROP_STATUS_INVALID_ARGUMENT:
			return "invalid argument";
		case VS_INTEROP_STATUS_OUT_OF_MEMORY:
			return "out of memory";
		case VS_INTEROP_STATUS_WRONG_THREAD:
			return "wrong thread";
		case VS_INTEROP_STATUS_ENGINE_ERROR:
			return "engine error";
		case VS_INTEROP_STATUS_TYPE_MISMATCH:
			return "type mismatch";
		case VS_INTEROP_STATUS_BUFFER_TOO_SMALL:
			return "buffer too small";
		case VS_INTEROP_STATUS_CALLBACK_ERROR:
			return "callback error";
		case VS_INTEROP_STATUS_UNSUPPORTED:
			return "unsupported";
		case VS_INTEROP_STATUS_INTERNAL_ERROR:
			return "internal error";
		default:
			return "unknown error";
		}
	}

	PyObject* ExceptionForStatus(const vs_interop_status Status)
	{
		if (Status == VS_INTEROP_STATUS_WRONG_THREAD)
		{
			return GThreadError;
		}
		if (Status == VS_INTEROP_STATUS_TYPE_MISMATCH)
		{
			return GTypeMismatchError;
		}
		if (Status == VS_INTEROP_STATUS_CALLBACK_ERROR)
		{
			return GCallbackError;
		}
		if (Status == VS_INTEROP_STATUS_OUT_OF_MEMORY)
		{
			return PyExc_MemoryError;
		}
		return GError;
	}

	std::string CopyLastError(const FPySession* Self) noexcept
	{
		try
		{
			if (Self == nullptr || Self->Handle == nullptr)
			{
				return {};
			}
			std::uint64_t Required = 0;
			const vs_interop_status SizeStatus =
			    vs_interop_session_copy_last_error(Self->Handle, nullptr, 0, &Required);
			if ((SizeStatus != VS_INTEROP_STATUS_OK && SizeStatus != VS_INTEROP_STATUS_BUFFER_TOO_SMALL) || Required == 0 || Required >= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
			{
				return {};
			}
			const std::uint64_t Capacity = Required;
			std::vector<char> Buffer(static_cast<std::size_t>(Capacity) + 1, '\0');
			if (vs_interop_session_copy_last_error(
			        Self->Handle, Buffer.data(), Capacity, &Required) != VS_INTEROP_STATUS_OK)
			{
				return {};
			}
			return std::string(Buffer.data());
		}
		catch (...)
		{
			return {};
		}
	}

	PyObject* RaiseStatus(FPySession* Self, const vs_interop_status Status, const char* Operation)
	{
		try
		{
			std::string Message = Operation;
			Message += " failed (";
			Message += StatusName(Status);
			Message += ')';
			const std::string LastError = CopyLastError(Self);
			if (!LastError.empty())
			{
				Message += ": ";
				Message += LastError;
			}
			PyErr_SetString(ExceptionForStatus(Status), Message.c_str());
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "VeroScript operation failed");
		}
		return nullptr;
	}

	bool CheckSession(FPySession* Self)
	{
		if (Self->Handle == nullptr)
		{
			PyErr_SetString(GError, "VeroScript session is closed");
			return false;
		}
		if (Self->OwnerThread != PyThread_get_thread_ident())
		{
			PyErr_SetString(GThreadError,
			    "VeroScript Session methods must run on the thread that created the session");
			return false;
		}
		return true;
	}

	// Operation entry points own their pending errors. A rejected nested call
	// must neither clear nor consume a failure already recorded by its caller.
	// This scope begins and ends with the GIL held; callback activity keeps the
	// graph alive while a nested operation temporarily hides the outer error.
	struct FCallbackErrorScope
	{
		explicit FCallbackErrorScope(FPySession* InSession)
		    : Session(InSession), Previous(ActiveCallbackError)
		{
			FCallbackState* State = Session->Callbacks;
			ActiveCallbackError = State != nullptr ? &State->CallbackError : nullptr;
			if (State == nullptr) return;
			if (State->ActiveOperations.load(std::memory_order_relaxed) > 1)
			{
				SavedError = std::exchange(State->CallbackError, nullptr);
			}
			else
			{
				Py_CLEAR(State->CallbackError);
			}
		}
		~FCallbackErrorScope()
		{
			ActiveCallbackError = Previous;
			if (SavedError != nullptr)
			{
				FCallbackState* State = Session->Callbacks;
				if (State != nullptr)
				{
					// The earlier error wins if a nested operation left another one.
					PyObject* NestedError = std::exchange(State->CallbackError, SavedError);
					SavedError = nullptr;
					Py_XDECREF(NestedError);
				}
				Py_XDECREF(SavedError);
			}
		}
		FCallbackErrorScope(const FCallbackErrorScope&) = delete;
		FCallbackErrorScope& operator=(const FCallbackErrorScope&) = delete;
		FPySession* Session;
		PyObject** Previous;
		PyObject* SavedError = nullptr;
	};

	bool RaiseStoredCallbackError(PyObject*& Pending)
	{
		if (Pending == nullptr)
		{
			return false;
		}
		PyObject* Error = std::exchange(Pending, nullptr);
		PyErr_SetObject(GCallbackError, Error);
		Py_DECREF(Error);
		return true;
	}

	bool RaiseCallbackError(FPySession* Self)
	{
		return Self->Callbacks != nullptr && RaiseStoredCallbackError(Self->Callbacks->CallbackError);
	}

	void StorePendingCallbackError(FCallbackState* State, PyObject*& Value)
	{
		PyObject** Pending = ActiveCallbackError;
		if (Pending == nullptr && State != nullptr)
		{
			Pending = &State->CallbackError;
		}
		if (Pending != nullptr && *Pending == nullptr && Value != nullptr)
		{
			*Pending = std::exchange(Value, nullptr);
		}
	}

	struct FValueCallbackErrorScope
	{
		FValueCallbackErrorScope() : Previous(std::exchange(ActiveCallbackError, &Error))
		{
		}
		~FValueCallbackErrorScope()
		{
			ActiveCallbackError = Previous;
			Py_XDECREF(Error);
		}
		FValueCallbackErrorScope(const FValueCallbackErrorScope&) = delete;
		FValueCallbackErrorScope& operator=(const FValueCallbackErrorScope&) = delete;
		PyObject* Error = nullptr;
		PyObject** Previous;
	};

	template <auto Function>
	struct TValueCallbackBoundary;

	template <typename Result, typename... Arguments, Result (*Function)(Arguments...)>
	struct TValueCallbackBoundary<Function>
	{
		static Result Invoke(Arguments... Args)
		{
			FValueCallbackErrorScope Scope;
			Result Output = Function(Args...);
			if (Scope.Error != nullptr)
			{
				if constexpr (std::is_same_v<Result, PyObject*>)
				{
					Py_XDECREF(Output);
				}
				RaiseStoredCallbackError(Scope.Error);
				if constexpr (std::is_same_v<Result, PyObject*>)
				{
					return nullptr;
				}
				else
				{
					return static_cast<Result>(-1);
				}
			}
			return Output;
		}
	};

	void StoreCallbackException(FCallbackState* State)
	{
		if (State == nullptr && ActiveCallbackError == nullptr)
		{
			PyErr_Clear();
			return;
		}
		PyObject* Type = nullptr;
		PyObject* Value = nullptr;
		PyObject* Traceback = nullptr;
		PyErr_Fetch(&Type, &Value, &Traceback);
		PyErr_NormalizeException(&Type, &Value, &Traceback);
		if (Value == nullptr)
		{
			Value = PyUnicode_FromString("Python callback failed");
		}
		StorePendingCallbackError(State, Value);
		Py_XDECREF(Type);
		Py_XDECREF(Value);
		Py_XDECREF(Traceback);
		PyErr_Clear();
	}

	void RecordHostCallbackException(vs_interop_call_context* Context, FCallbackState* State)
	{
		PyObject* Type = nullptr;
		PyObject* Value = nullptr;
		PyObject* Traceback = nullptr;
		PyErr_Fetch(&Type, &Value, &Traceback);
		PyErr_NormalizeException(&Type, &Value, &Traceback);
		if (Value == nullptr)
		{
			Value = PyObject_CallNoArgs(PyExc_RuntimeError);
		}
		PyObject* Text = Value != nullptr ? PyObject_Str(Value) : nullptr;
		if (Text != nullptr)
		{
			Py_ssize_t Size = 0;
			const char* Utf8 = PyUnicode_AsUTF8AndSize(Text, &Size);
			if (Utf8 != nullptr)
			{
				vs_interop_call_context_set_error(Context, View(Utf8, Size));
			}
			else
			{
				PyErr_Clear();
			}
			Py_DECREF(Text);
		}
		else
		{
			PyErr_Clear();
			static constexpr char Fallback[] = "Python host callback failed";
			vs_interop_call_context_set_error(Context,
			    {Fallback, static_cast<std::uint64_t>(sizeof(Fallback) - 1)});
		}
		StorePendingCallbackError(State, Value);
		Py_XDECREF(Type);
		Py_XDECREF(Value);
		Py_XDECREF(Traceback);
		PyErr_Clear();
	}

	void InvokeLineCallback(PyObject* Callback, const vs_interop_string_view Line,
	    FCallbackState* State)
	{
		if (Callback == nullptr)
		{
			return;
		}
		PyObject* Argument = UnicodeFromView(Line);
		if (Argument == nullptr)
		{
			StoreCallbackException(State);
			return;
		}
		PyObject* Result = PyObject_CallOneArg(Callback, Argument);
		Py_DECREF(Argument);
		if (Result == nullptr)
		{
			StoreCallbackException(State);
			return;
		}
		Py_DECREF(Result);
	}

	void VS_INTEROP_CALL OnPrint(const vs_interop_string_view Line, void* UserData)
	{
		auto* State = static_cast<FCallbackState*>(UserData);
		const PyGILState_STATE Gil = PyGILState_Ensure();
		const FCallbackActivity Activity(State);
		if (State != nullptr)
		{
			InvokeLineCallback(State->PrintCallback, Line, State);
		}
		PyGILState_Release(Gil);
	}

	void VS_INTEROP_CALL OnDiagnostic(const vs_interop_string_view Line, void* UserData)
	{
		auto* State = static_cast<FCallbackState*>(UserData);
		const PyGILState_STATE Gil = PyGILState_Ensure();
		const FCallbackActivity Activity(State);
		if (State != nullptr)
		{
			InvokeLineCallback(State->DiagnosticCallback, Line, State);
		}
		PyGILState_Release(Gil);
	}

	uint8_t VS_INTEROP_CALL OnConsoleQuery(vs_interop_console_info* Info, void* UserData)
	{
		FPyGilGuard Gil;
		auto* State = static_cast<FCallbackState*>(UserData);
		const FCallbackActivity Activity(State);
		FPyOwnedRef Result(PyObject_CallNoArgs(State->ConsoleQueryCallback));
		if (!Result.Get())
		{
			StoreCallbackException(State);
			return 0;
		}
		if (!PyTuple_Check(Result.Get()) || PyTuple_GET_SIZE(Result.Get()) != 3)
		{
			PyErr_SetString(PyExc_TypeError, "console query must return (flags, width, height)");
			StoreCallbackException(State);
			return 0;
		}
		long Values[3]{};
		for (int Index = 0; Index < 3; ++Index)
		{
			Values[Index] = PyLong_AsLong(PyTuple_GET_ITEM(Result.Get(), Index));
			if (PyErr_Occurred() || Values[Index] < 0 || Values[Index] > INT32_MAX || (Index == 0 && Values[Index] > 15))
			{
				if (!PyErr_Occurred())
				{
					PyErr_SetString(PyExc_ValueError, "invalid console flags or dimensions");
				}

				StoreCallbackException(State);
				return 0;
			}
		}
		Info->flags = static_cast<uint32_t>(Values[0]);
		Info->width = static_cast<int32_t>(Values[1]);
		Info->height = static_cast<int32_t>(Values[2]);
		return 1;
	}
	uint8_t VS_INTEROP_CALL OnConsoleControl(uint32_t Operation, int32_t First, int32_t Second, void* UserData)
	{
		FPyGilGuard Gil;
		auto* State = static_cast<FCallbackState*>(UserData);
		const FCallbackActivity Activity(State);
		FPyOwnedRef Result(
		    PyObject_CallFunction(State->ConsoleControlCallback, "Iii", Operation, First, Second));
		if (!Result.Get())
		{
			StoreCallbackException(State);
			return 0;
		}
		if (!PyBool_Check(Result.Get()))
		{
			PyErr_SetString(PyExc_TypeError, "console control callback must return bool");
			StoreCallbackException(State);
			return 0;
		}
		return Result.Get() == Py_True ? 1 : 0;
	}
	uint8_t VS_INTEROP_CALL OnConsoleWrite(vs_interop_string_view Text, void* UserData)
	{
		FPyGilGuard Gil;
		auto* State = static_cast<FCallbackState*>(UserData);
		const FCallbackActivity Activity(State);
		FPyOwnedRef Argument(UnicodeFromView(Text));
		if (!Argument.Get())
		{
			StoreCallbackException(State);
			return 0;
		}
		FPyOwnedRef Result(PyObject_CallOneArg(State->ConsoleWriteCallback, Argument.Get()));
		if (!Result.Get())
		{
			StoreCallbackException(State);
			return 0;
		}
		if (!PyBool_Check(Result.Get()))
		{
			PyErr_SetString(PyExc_TypeError, "console write callback must return bool");
			StoreCallbackException(State);
			return 0;
		}
		return Result.Get() == Py_True ? 1 : 0;
	}

	const char* KindName(const vs_interop_value_kind Kind)
	{
		switch (Kind)
		{
		case VS_INTEROP_VALUE_VARIANT:
			return "variant";
		case VS_INTEROP_VALUE_INVALID:
			return "invalid";
		case VS_INTEROP_VALUE_NULL:
			return "null";
		case VS_INTEROP_VALUE_INT8:
			return "int8";
		case VS_INTEROP_VALUE_UINT8:
			return "uint8";
		case VS_INTEROP_VALUE_INT16:
			return "int16";
		case VS_INTEROP_VALUE_UINT16:
			return "uint16";
		case VS_INTEROP_VALUE_INT32:
			return "int32";
		case VS_INTEROP_VALUE_UINT32:
			return "uint32";
		case VS_INTEROP_VALUE_INT64:
			return "int64";
		case VS_INTEROP_VALUE_UINT64:
			return "uint64";
		case VS_INTEROP_VALUE_FLOAT:
			return "float";
		case VS_INTEROP_VALUE_DOUBLE:
			return "double";
		case VS_INTEROP_VALUE_BOOL:
			return "bool";
		case VS_INTEROP_VALUE_STRING:
			return "string";
		case VS_INTEROP_VALUE_WSTRING:
			return "wstring";
		case VS_INTEROP_VALUE_ENUM:
			return "enum";
		case VS_INTEROP_VALUE_OBJECT:
			return "object";
		case VS_INTEROP_VALUE_ARRAY:
			return "array";
		case VS_INTEROP_VALUE_MAP:
			return "map";
		case VS_INTEROP_VALUE_SET:
			return "set";
		case VS_INTEROP_VALUE_QUEUE:
			return "queue";
		case VS_INTEROP_VALUE_STACK:
			return "stack";
		case VS_INTEROP_VALUE_ORDERED_MAP:
			return "ordered_map";
		case VS_INTEROP_VALUE_DEQUE:
			return "deque";
		case VS_INTEROP_VALUE_CHANNEL:
			return "channel";
		case VS_INTEROP_VALUE_FUNCTION:
			return "function";
		case VS_INTEROP_VALUE_HOST_OBJECT:
			return "host_object";
		case VS_INTEROP_VALUE_NATIVE_ARRAY:
			return "native_array";
		case VS_INTEROP_VALUE_MULTI_MAP:
			return "multi_map";
		case VS_INTEROP_VALUE_ORDERED_MULTI_MAP:
			return "ordered_multi_map";
		case VS_INTEROP_VALUE_PRIORITY_QUEUE:
			return "priority_queue";
		case VS_INTEROP_VALUE_MULTI_SET:
			return "multi_set";
		case VS_INTEROP_VALUE_ORDERED_MULTI_SET:
			return "ordered_multi_set";
		default:
			return "unknown";
		}
	}

	bool IsAssignableScalarKind(const vs_interop_value_kind Kind)
	{
		switch (Kind)
		{
		case VS_INTEROP_VALUE_INVALID:
		case VS_INTEROP_VALUE_NULL:
		case VS_INTEROP_VALUE_INT8:
		case VS_INTEROP_VALUE_UINT8:
		case VS_INTEROP_VALUE_INT16:
		case VS_INTEROP_VALUE_UINT16:
		case VS_INTEROP_VALUE_INT32:
		case VS_INTEROP_VALUE_UINT32:
		case VS_INTEROP_VALUE_INT64:
		case VS_INTEROP_VALUE_UINT64:
		case VS_INTEROP_VALUE_FLOAT:
		case VS_INTEROP_VALUE_DOUBLE:
		case VS_INTEROP_VALUE_BOOL:
		case VS_INTEROP_VALUE_STRING:
		case VS_INTEROP_VALUE_WSTRING:
		case VS_INTEROP_VALUE_ENUM:
			return true;
		default:
			return false;
		}
	}

	bool ParseNativeTypeName(const std::string_view Name, vs_interop_native_type* OutType)
	{
		if (Name == "variant")
		{
			*OutType = VS_INTEROP_NATIVE_VARIANT;
			return true;
		}
		if (Name == "void")
		{
			*OutType = VS_INTEROP_NATIVE_VOID;
		}
		else if (Name == "char" || Name == "int8")
		{
			*OutType = VS_INTEROP_NATIVE_INT8;
		}
		else if (Name == "unsigned char" || Name == "uint8")
		{
			*OutType = VS_INTEROP_NATIVE_UINT8;
		}
		else if (Name == "short" || Name == "int16")
		{
			*OutType = VS_INTEROP_NATIVE_INT16;
		}
		else if (Name == "unsigned short" || Name == "uint16")
		{
			*OutType = VS_INTEROP_NATIVE_UINT16;
		}
		else if (Name == "int" || Name == "int32")
		{
			*OutType = VS_INTEROP_NATIVE_INT32;
		}
		else if (Name == "uint" || Name == "uint32" || Name == "unsigned" || Name == "unsigned int")
		{
			*OutType = VS_INTEROP_NATIVE_UINT32;
		}
		else if (Name == "long" || Name == "int64")
		{
			*OutType = VS_INTEROP_NATIVE_INT64;
		}
		else if (Name == "unsigned long" || Name == "uint64")
		{
			*OutType = VS_INTEROP_NATIVE_UINT64;
		}
		else if (Name == "float")
		{
			*OutType = VS_INTEROP_NATIVE_FLOAT;
		}
		else if (Name == "double")
		{
			*OutType = VS_INTEROP_NATIVE_DOUBLE;
		}
		else if (Name == "bool")
		{
			*OutType = VS_INTEROP_NATIVE_BOOL;
		}
		else if (Name == "string")
		{
			*OutType = VS_INTEROP_NATIVE_STRING;
		}
		else
		{
			return false;
		}
		return true;
	}

	const char* NativeTypeValueKindName(const vs_interop_native_type Type)
	{
		switch (Type)
		{
		case VS_INTEROP_NATIVE_VARIANT:
			return "variant";
		case VS_INTEROP_NATIVE_INT8:
			return "int8";
		case VS_INTEROP_NATIVE_UINT8:
			return "uint8";
		case VS_INTEROP_NATIVE_INT16:
			return "int16";
		case VS_INTEROP_NATIVE_UINT16:
			return "uint16";
		case VS_INTEROP_NATIVE_INT32:
			return "int32";
		case VS_INTEROP_NATIVE_UINT32:
			return "uint32";
		case VS_INTEROP_NATIVE_INT64:
			return "int64";
		case VS_INTEROP_NATIVE_UINT64:
			return "uint64";
		case VS_INTEROP_NATIVE_FLOAT:
			return "float";
		case VS_INTEROP_NATIVE_DOUBLE:
			return "double";
		case VS_INTEROP_NATIVE_BOOL:
			return "bool";
		case VS_INTEROP_NATIVE_STRING:
			return "string";
		default:
			return nullptr;
		}
	}

	const char* ScriptScalarValueKindName(const std::string_view TypeName)
	{
		if (TypeName == "wstring")
		{
			return "wstring";
		}
		vs_interop_native_type Type = VS_INTEROP_NATIVE_VOID;
		return ParseNativeTypeName(TypeName, &Type)
		    ? NativeTypeValueKindName(Type)
		    : nullptr;
	}

	bool SetSignedInteger(vs_interop_value* Handle, PyObject* Object,
	    const int Bits, const char* Kind)
	{
		int Overflow = 0;
		const long long Number = PyLong_AsLongLongAndOverflow(Object, &Overflow);
		if (PyErr_Occurred())
		{
			return false;
		}
		if (Overflow != 0)
		{
			PyErr_Format(PyExc_OverflowError, "%s value is out of range", Kind);
			return false;
		}
		const long long Minimum = Bits == 64 ? std::numeric_limits<long long>::min()
		                                     : -(1LL << (Bits - 1));
		const long long Maximum = Bits == 64 ? std::numeric_limits<long long>::max()
		                                     : (1LL << (Bits - 1)) - 1;
		if (Number < Minimum || Number > Maximum)
		{
			PyErr_Format(PyExc_OverflowError, "%s value is out of range", Kind);
			return false;
		}
		vs_interop_status Status = VS_INTEROP_STATUS_INVALID_ARGUMENT;
		switch (Bits)
		{
		case 8:
			{
				Status = vs_interop_value_set_int8(Handle, static_cast<std::int8_t>(Number));
				break;
			}
		case 16:
			{
				Status = vs_interop_value_set_int16(Handle, static_cast<std::int16_t>(Number));
				break;
			}
		case 32:
			{
				Status = vs_interop_value_set_int32(Handle, static_cast<std::int32_t>(Number));
				break;
			}
		case 64:
			{
				Status = vs_interop_value_set_int64(Handle, static_cast<std::int64_t>(Number));
				break;
			}
		default:
			{
				break;
			}
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "set integer value");
			return false;
		}
		return true;
	}

	bool SetUnsignedInteger(vs_interop_value* Handle, PyObject* Object,
	    const int Bits, const char* Kind)
	{
		const unsigned long long Number = PyLong_AsUnsignedLongLong(Object);
		if (PyErr_Occurred())
		{
			return false;
		}
		const unsigned long long Maximum = Bits == 64
		    ? std::numeric_limits<unsigned long long>::max()
		    : (1ULL << Bits) - 1;
		if (Number > Maximum)
		{
			PyErr_Format(PyExc_OverflowError, "%s value is out of range", Kind);
			return false;
		}
		vs_interop_status Status = VS_INTEROP_STATUS_INVALID_ARGUMENT;
		switch (Bits)
		{
		case 8:
			{
				Status = vs_interop_value_set_uint8(Handle, static_cast<std::uint8_t>(Number));
				break;
			}
		case 16:
			{
				Status = vs_interop_value_set_uint16(Handle, static_cast<std::uint16_t>(Number));
				break;
			}
		case 32:
			{
				Status = vs_interop_value_set_uint32(Handle, static_cast<std::uint32_t>(Number));
				break;
			}
		case 64:
			{
				Status = vs_interop_value_set_uint64(Handle, static_cast<std::uint64_t>(Number));
				break;
			}
		default:
			{
				break;
			}
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "set unsigned integer value");
			return false;
		}
		return true;
	}

	bool SetString(vs_interop_value* Handle, PyObject* Object)
	{
		Py_ssize_t Size = 0;
		const char* Text = PyUnicode_AsUTF8AndSize(Object, &Size);
		if (Text == nullptr)
		{
			return false;
		}
		const vs_interop_status Status = vs_interop_value_set_string(Handle, View(Text, Size));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "set string value");
			return false;
		}
		return true;
	}

	bool SetWideString(vs_interop_value* Handle, PyObject* Object)
	{
		FPyOwnedRef Bytes(PyUnicode_AsEncodedString(Object, "utf-16-le", "strict"));
		if (Bytes.Get() == nullptr)
		{
			return false;
		}
		const Py_ssize_t ByteCount = PyBytes_GET_SIZE(Bytes.Get());
		std::vector<std::uint16_t> Units(static_cast<std::size_t>(ByteCount / 2));
		const unsigned char* Source = reinterpret_cast<const unsigned char*>(
		    PyBytes_AS_STRING(Bytes.Get()));
		for (Py_ssize_t Index = 0; Index < ByteCount / 2; ++Index)
		{
			Units[static_cast<std::size_t>(Index)] = static_cast<std::uint16_t>(
			    Source[Index * 2] | static_cast<std::uint16_t>(Source[Index * 2 + 1] << 8));
		}
		const vs_interop_status Status = vs_interop_value_set_wstring(
		    Handle, Units.data(), static_cast<std::uint64_t>(Units.size()));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "set wide string value");
			return false;
		}
		return true;
	}

	bool SetValueFromPython(vs_interop_value* Handle, PyObject* Object,
	    const char* RequestedKind, const char* TypeName)
	{
		const auto CompleteWrite = [](const vs_interop_status Status,
		                               const char* Operation) -> bool
		{
			if (Status != VS_INTEROP_STATUS_OK)
			{
				RaiseStatus(nullptr, Status, Operation);
				return false;
			}
			return true;
		};
		if (PyObject_TypeCheck(Object, &GValueType))
		{
			const vs_interop_status Status = vs_interop_value_assign(
			    Handle, reinterpret_cast<FPyValue*>(Object)->Handle);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				RaiseStatus(nullptr, Status, "assign VeroScript value");
				return false;
			}
			return true;
		}
		if (Object == Py_None)
		{
			if (RequestedKind != nullptr && std::strcmp(RequestedKind, "invalid") == 0)
			{
				return CompleteWrite(vs_interop_value_reset(Handle), "reset value");
			}
			if (RequestedKind != nullptr && std::strcmp(RequestedKind, "null") != 0)
			{
				PyErr_Format(PyExc_TypeError,
				    "None is not compatible with VeroScript value kind: %s", RequestedKind);
				return false;
			}
			const vs_interop_status Status = vs_interop_value_set_null(Handle);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				RaiseStatus(nullptr, Status, "set null value");
				return false;
			}
			return true;
		}
		if (RequestedKind == nullptr)
		{
			if (PyBool_Check(Object))
			{
				RequestedKind = "bool";
			}
			else if (PyLong_Check(Object))
			{
				int Overflow = 0;
				const long long Number = PyLong_AsLongLongAndOverflow(Object, &Overflow);
				if (PyErr_Occurred())
				{
					return false;
				}
				if (Overflow == 0)
				{
					RequestedKind = Number >= std::numeric_limits<std::int32_t>::min() && Number <= std::numeric_limits<std::int32_t>::max() ? "int32" : "int64";
				}
				else if (Overflow > 0)
				{
					RequestedKind = "uint64";
				}
				else
				{
					PyErr_SetString(PyExc_OverflowError, "integer value is below the int64 minimum");
					return false;
				}
			}
			else if (PyFloat_Check(Object))
			{
				RequestedKind = "double";
			}
			else if (PyUnicode_Check(Object))
			{
				RequestedKind = "string";
			}
			else
			{
				PyErr_Format(PyExc_TypeError,
				    "cannot convert %.200s to a VeroScript value", Py_TYPE(Object)->tp_name);
				return false;
			}
		}

		if (std::strcmp(RequestedKind, "invalid") == 0)
		{
			return CompleteWrite(vs_interop_value_reset(Handle), "reset value");
		}
		if (std::strcmp(RequestedKind, "int8") == 0)
		{
			return SetSignedInteger(Handle, Object, 8, "int8");
		}
		if (std::strcmp(RequestedKind, "uint8") == 0)
		{
			return SetUnsignedInteger(Handle, Object, 8, "uint8");
		}
		if (std::strcmp(RequestedKind, "int16") == 0)
		{
			return SetSignedInteger(Handle, Object, 16, "int16");
		}
		if (std::strcmp(RequestedKind, "uint16") == 0)
		{
			return SetUnsignedInteger(Handle, Object, 16, "uint16");
		}
		if (std::strcmp(RequestedKind, "int32") == 0)
		{
			return SetSignedInteger(Handle, Object, 32, "int32");
		}
		if (std::strcmp(RequestedKind, "uint32") == 0)
		{
			return SetUnsignedInteger(Handle, Object, 32, "uint32");
		}
		if (std::strcmp(RequestedKind, "int64") == 0)
		{
			return SetSignedInteger(Handle, Object, 64, "int64");
		}
		if (std::strcmp(RequestedKind, "uint64") == 0)
		{
			return SetUnsignedInteger(Handle, Object, 64, "uint64");
		}
		if (std::strcmp(RequestedKind, "bool") == 0)
		{
			const int Truth = PyObject_IsTrue(Object);
			if (Truth < 0)
			{
				return false;
			}
			return CompleteWrite(vs_interop_value_set_bool(
			                         Handle, static_cast<std::uint8_t>(Truth)),
			    "set boolean value");
		}
		if (std::strcmp(RequestedKind, "float") == 0 || std::strcmp(RequestedKind, "double") == 0)
		{
			const double Number = PyFloat_AsDouble(Object);
			if (PyErr_Occurred())
			{
				return false;
			}
			const vs_interop_status Status = std::strcmp(RequestedKind, "float") == 0
			    ? vs_interop_value_set_float(Handle, static_cast<float>(Number))
			    : vs_interop_value_set_double(Handle, Number);
			return CompleteWrite(Status, "set floating point value");
		}
		if (std::strcmp(RequestedKind, "string") == 0)
		{
			return SetString(Handle, Object);
		}
		if (std::strcmp(RequestedKind, "wstring") == 0)
		{
			return SetWideString(Handle, Object);
		}
		if (std::strcmp(RequestedKind, "enum") == 0)
		{
			if (TypeName == nullptr || TypeName[0] == '\0')
			{
				PyErr_SetString(PyExc_ValueError, "enum values require type_name");
				return false;
			}
			const long long Number = PyLong_AsLongLong(Object);
			if (PyErr_Occurred())
			{
				return false;
			}
			return CompleteWrite(vs_interop_value_set_enum(Handle,
			                         {TypeName, static_cast<std::uint64_t>(std::strlen(TypeName))}, Number),
			    "set enum value");
		}

		PyErr_Format(PyExc_ValueError, "unsupported VeroScript value kind: %s", RequestedKind);
		return false;
	}

	bool AssignScalarValue(vs_interop_value* Destination, const vs_interop_value* Source)
	{
		switch (vs_interop_value_get_kind(Source))
		{
		case VS_INTEROP_VALUE_INVALID:
			return vs_interop_value_reset(Destination) == VS_INTEROP_STATUS_OK;
		case VS_INTEROP_VALUE_NULL:
			return vs_interop_value_set_null(Destination) == VS_INTEROP_STATUS_OK;
		case VS_INTEROP_VALUE_INT8:
			{
				std::int8_t Value = 0;
				return vs_interop_value_get_int8(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int8(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_UINT8:
			{
				std::uint8_t Value = 0;
				return vs_interop_value_get_uint8(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint8(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_INT16:
			{
				std::int16_t Value = 0;
				return vs_interop_value_get_int16(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int16(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_UINT16:
			{
				std::uint16_t Value = 0;
				return vs_interop_value_get_uint16(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint16(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_INT32:
			{
				std::int32_t Value = 0;
				return vs_interop_value_get_int32(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int32(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_UINT32:
			{
				std::uint32_t Value = 0;
				return vs_interop_value_get_uint32(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint32(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_INT64:
			{
				std::int64_t Value = 0;
				return vs_interop_value_get_int64(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_int64(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_UINT64:
			{
				std::uint64_t Value = 0;
				return vs_interop_value_get_uint64(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_uint64(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_FLOAT:
			{
				float Value = 0.0F;
				return vs_interop_value_get_float(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_float(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_DOUBLE:
			{
				double Value = 0.0;
				return vs_interop_value_get_double(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_double(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_BOOL:
			{
				std::uint8_t Value = 0;
				return vs_interop_value_get_bool(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_bool(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_STRING:
			{
				vs_interop_string_view Value{};
				return vs_interop_value_get_string(Source, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_string(Destination, Value) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_WSTRING:
			{
				const std::uint16_t* Data = nullptr;
				std::uint64_t Size = 0;
				return vs_interop_value_get_wstring(Source, &Data, &Size) == VS_INTEROP_STATUS_OK && vs_interop_value_set_wstring(Destination, Data, Size) == VS_INTEROP_STATUS_OK;
			}
		case VS_INTEROP_VALUE_ENUM:
			{
				vs_interop_string_view TypeName{};
				std::int64_t Value = 0;
				return vs_interop_value_get_enum(Source, &TypeName, &Value) == VS_INTEROP_STATUS_OK && vs_interop_value_set_enum(Destination, TypeName, Value) == VS_INTEROP_STATUS_OK;
			}
		default:
			return false;
		}
	}

	PyObject* ScalarFromValue(const vs_interop_value* Handle, bool* IsScalar)
	{
		*IsScalar = true;
		switch (vs_interop_value_get_kind(Handle))
		{
		case VS_INTEROP_VALUE_INVALID:
		case VS_INTEROP_VALUE_NULL:
			{
				Py_RETURN_NONE;
			}
		case VS_INTEROP_VALUE_INT8:
			{
				std::int8_t Value = 0;
				if (vs_interop_value_get_int8(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_UINT8:
			{
				std::uint8_t Value = 0;
				if (vs_interop_value_get_uint8(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromUnsignedLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_INT16:
			{
				std::int16_t Value = 0;
				if (vs_interop_value_get_int16(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_UINT16:
			{
				std::uint16_t Value = 0;
				if (vs_interop_value_get_uint16(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromUnsignedLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_INT32:
			{
				std::int32_t Value = 0;
				if (vs_interop_value_get_int32(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_UINT32:
			{
				std::uint32_t Value = 0;
				if (vs_interop_value_get_uint32(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromUnsignedLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_INT64:
			{
				std::int64_t Value = 0;
				if (vs_interop_value_get_int64(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromLongLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_UINT64:
			{
				std::uint64_t Value = 0;
				if (vs_interop_value_get_uint64(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromUnsignedLongLong(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_FLOAT:
			{
				float Value = 0.0F;
				if (vs_interop_value_get_float(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyFloat_FromDouble(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_DOUBLE:
			{
				double Value = 0.0;
				if (vs_interop_value_get_double(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyFloat_FromDouble(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_BOOL:
			{
				std::uint8_t Value = 0;
				if (vs_interop_value_get_bool(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyBool_FromLong(Value != 0);
				}
				break;
			}
		case VS_INTEROP_VALUE_STRING:
			{
				vs_interop_string_view Value{};
				if (vs_interop_value_get_string(Handle, &Value) == VS_INTEROP_STATUS_OK)
				{
					return UnicodeFromView(Value);
				}
				break;
			}
		case VS_INTEROP_VALUE_WSTRING:
			{
				const std::uint16_t* Data = nullptr;
				std::uint64_t Size = 0;
				if (vs_interop_value_get_wstring(Handle, &Data, &Size) != VS_INTEROP_STATUS_OK)
				{
					break;
				}
				if (Size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX) / 2)
				{
					PyErr_SetString(PyExc_OverflowError, "wide string is too large for Python");
					return nullptr;
				}
				if (Size != 0 && Data == nullptr)
				{
					PyErr_SetString(GError, "VeroScript returned an invalid wide string view");
					return nullptr;
				}
				const std::uint16_t Marker = 1;
				int ByteOrder = *reinterpret_cast<const std::uint8_t*>(&Marker) == 1 ? -1 : 1;
				return PyUnicode_DecodeUTF16(Size == 0 ? "" : reinterpret_cast<const char*>(Data),
				    static_cast<Py_ssize_t>(Size * 2), "strict", &ByteOrder);
			}
		case VS_INTEROP_VALUE_ENUM:
			{
				vs_interop_string_view Type{};
				std::int64_t Value = 0;
				if (vs_interop_value_get_enum(Handle, &Type, &Value) == VS_INTEROP_STATUS_OK)
				{
					return PyLong_FromLongLong(Value);
				}
				break;
			}
		default:
			{
				*IsScalar = false;
				return nullptr;
			}
		}
		PyErr_SetString(GTypeMismatchError, "failed to read VeroScript scalar value");
		return nullptr;
	}

	PyObject* NewValueTakingOwnership(vs_interop_value* Handle)
	{
		auto* Object = reinterpret_cast<FPyValue*>(GValueType.tp_alloc(&GValueType, 0));
		if (Object == nullptr)
		{
			vs_interop_value_destroy(Handle);
			return nullptr;
		}
		Object->Handle = Handle;
		return reinterpret_cast<PyObject*>(Object);
	}

	PyObject* TakeValue(vs_interop_value* Handle)
	{
		bool IsScalar = false;
		PyObject* Result = ScalarFromValue(Handle, &IsScalar);
		if (IsScalar)
		{
			vs_interop_value_destroy(Handle);
			return Result;
		}
		return NewValueTakingOwnership(Handle);
	}

	PyObject* ValueNew(PyTypeObject* Type, PyObject*, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		auto* Self = reinterpret_cast<FPyValue*>(Type->tp_alloc(Type, 0));
		if (Self != nullptr)
		{
			Self->Handle = nullptr;
		}
		return reinterpret_cast<PyObject*>(Self);
	}

	int ValueInit(FPyValue* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		PyObject* Object = nullptr;
		const char* Kind = nullptr;
		const char* TypeName = nullptr;
		static const char* Names[] = {"value", "kind", "type_name", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "|Ozz:Value",
		        const_cast<char**>(Names), &Object, &Kind, &TypeName))
		{
			return -1;
		}
		if (Self->Handle == nullptr)
		{
			const vs_interop_status Status = vs_interop_value_create(&Self->Handle);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				RaiseStatus(nullptr, Status, "create value");
				return -1;
			}
		}
		else if (vs_interop_value_reset(Self->Handle) != VS_INTEROP_STATUS_OK)
		{
			PyErr_SetString(GError, "failed to reset VeroScript value");
			return -1;
		}
		if (Object == nullptr)
		{
			if (Kind == nullptr)
			{
				const vs_interop_status Status = vs_interop_value_reset(Self->Handle);
				if (Status != VS_INTEROP_STATUS_OK)
				{
					RaiseStatus(nullptr, Status, "reset value");
					return -1;
				}
				return 0;
			}
			Object = Py_None;
		}
		return SetValueFromPython(Self->Handle, Object, Kind, TypeName) ? 0 : -1;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return -1;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while creating VeroScript value");
		return -1;
	}

	void ValueDealloc(FPyValue* Self)
	{
		vs_interop_value_destroy(Self->Handle);
		Self->Handle = nullptr;
		Py_TYPE(Self)->tp_free(reinterpret_cast<PyObject*>(Self));
	}

	PyObject* ValueGetValue(FPyValue* Self, void*)
	{
		bool IsScalar = false;
		PyObject* Result = ScalarFromValue(Self->Handle, &IsScalar);
		if (!IsScalar && Result == nullptr)
		{
			PyErr_SetString(GTypeMismatchError,
			    "this VeroScript value is not scalar; use Session object/container APIs");
		}
		return Result;
	}

	int ValueSetValue(FPyValue* Self, PyObject* Object, void*)
	{
		if (Object == nullptr)
		{
			PyErr_SetString(PyExc_TypeError, "VeroScript Value.value cannot be deleted");
			return -1;
		}
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		const char* RequestedKind = KindName(Kind);
		std::string TypeName;
		if (Kind == VS_INTEROP_VALUE_ENUM)
		{
			vs_interop_string_view TypeView{};
			if (vs_interop_value_get_type_name(Self->Handle, &TypeView) != VS_INTEROP_STATUS_OK)
			{
				PyErr_SetString(GTypeMismatchError, "failed to read enum type name");
				return -1;
			}
			if (TypeView.size > static_cast<std::uint64_t>(
			                        std::numeric_limits<std::size_t>::max()) ||
			    (TypeView.size != 0 && TypeView.data == nullptr))
			{
				PyErr_SetString(GError, "VeroScript returned an invalid enum type name");
				return -1;
			}
			try
			{
				TypeName.assign(TypeView.size == 0 ? "" : TypeView.data,
				    static_cast<std::size_t>(TypeView.size));
			}
			catch (const std::bad_alloc&)
			{
				PyErr_NoMemory();
				return -1;
			}
		}
		if (!IsAssignableScalarKind(Kind))
		{
			PyErr_SetString(GTypeMismatchError, "only scalar VeroScript values are assignable");
			return -1;
		}
		return SetValueFromPython(Self->Handle, Object, RequestedKind,
		           TypeName.empty() ? nullptr : TypeName.c_str())
		    ? 0
		    : -1;
	}

	PyObject* ValueGetKind(FPyValue* Self, void*)
	{
		return PyUnicode_FromString(KindName(vs_interop_value_get_kind(Self->Handle)));
	}

	PyObject* ValueGetNumericKind(FPyValue* Self, void*)
	{
		if (!CheckInterpreter()) return nullptr;
		return PyLong_FromUnsignedLong(vs_interop_value_numeric_kind(Self->Handle));
	}

	PyObject* ValueGetTypeName(FPyValue* Self, void*)
	{
		vs_interop_string_view TypeName{};
		const vs_interop_status Status = vs_interop_value_get_type_name(Self->Handle, &TypeName);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "get value type name");
		}
		return UnicodeFromView(TypeName);
	}

	PyObject* ValueClone(FPyValue* Self, PyObject*)
	{
		vs_interop_value* Clone = nullptr;
		const vs_interop_status Status = vs_interop_value_clone(Self->Handle, &Clone);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "clone value");
		}
		return NewValueTakingOwnership(Clone);
	}

	struct FTemporaryValues
	{
		~FTemporaryValues()
		{
			for (vs_interop_value* Value : Owned)
			{
				vs_interop_value_destroy(Value);
			}
		}

		std::vector<vs_interop_value*> Handles;
		std::vector<vs_interop_value*> Owned;
		FPyOwnedRef RetainedElements;
	};

	void RememberOwnedValue(std::vector<vs_interop_value*>& Owned,
	    vs_interop_value* Value)
	{
		try
		{
			Owned.push_back(Value);
		}
		catch (...)
		{
			vs_interop_value_destroy(Value);
			throw;
		}
	}

	bool AppendTypedValue(FTemporaryValues& Values, PyObject* Object,
	    const std::string_view DeclaredType)
	{
		if (PyObject_TypeCheck(Object, &GValueType))
		{
			Values.Handles.push_back(reinterpret_cast<FPyValue*>(Object)->Handle);
			return true;
		}
		const char* Kind = ScriptScalarValueKindName(DeclaredType);
		if (Kind == nullptr)
		{
			PyErr_SetString(PyExc_TypeError,
			    "non-scalar container elements must be explicit VeroScript Value objects");
			return false;
		}
		vs_interop_value* Value = nullptr;
		const vs_interop_status Status = vs_interop_value_create(&Value);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "create container element");
			return false;
		}
		RememberOwnedValue(Values.Owned, Value);
		if (!SetValueFromPython(Value, Object, Kind, nullptr))
		{
			return false;
		}
		Values.Handles.push_back(Value);
		return true;
	}

	bool BuildTypedValues(PyObject* Iterable, const std::string_view DeclaredType,
	    FTemporaryValues& Values)
	{
		// Explicit Value elements contribute borrowed native handles. Retain the
		// snapshot until the caller finishes consuming those handles, including
		// elements yielded only by a generator or removed from the input list.
		Py_XSETREF(Values.RetainedElements.Object,
		    SnapshotSequence(Iterable, "container elements must be iterable"));
		if (Values.RetainedElements.Get() == nullptr)
		{
			return false;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Values.RetainedElements.Get());
		Values.Handles.reserve(static_cast<std::size_t>(Count));
		Values.Owned.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			if (!AppendTypedValue(
			        Values, PySequence_Fast_GET_ITEM(Values.RetainedElements.Get(), Index), DeclaredType))
			{
				return false;
			}
		}
		return true;
	}

	PyObject* CreateSequenceValue(PyObject* Args, const vs_interop_value_kind Kind,
	    const char* FunctionName)
	try
	{
		const char* ElementType = nullptr;
		Py_ssize_t ElementTypeSize = 0;
		PyObject* Elements = nullptr;
		if (!PyArg_ParseTuple(Args, "s#O", &ElementType, &ElementTypeSize, &Elements))
		{
			return nullptr;
		}
		FTemporaryValues Values;
		if (!BuildTypedValues(Elements,
		        {ElementType, static_cast<std::size_t>(ElementTypeSize)}, Values))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = Kind == VS_INTEROP_VALUE_ARRAY
			    ? vs_interop_value_create_array(Output, View(ElementType, ElementTypeSize),
			          Values.Handles.data(), static_cast<std::uint64_t>(Values.Handles.size()))
			    : vs_interop_value_create_sequence(Output, Kind, View(ElementType, ElementTypeSize),
			          Values.Handles.data(), static_cast<std::uint64_t>(Values.Handles.size()));
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Output);
			return RaiseStatus(nullptr, Status, FunctionName);
		}
		return NewValueTakingOwnership(Output);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected container construction error");
		return nullptr;
	}

	PyObject* ValueArray(PyObject*, PyObject* Args)
	{
		return CreateSequenceValue(Args, VS_INTEROP_VALUE_ARRAY, "create array value");
	}

	PyObject* ValueQueue(PyObject*, PyObject* Args)
	{
		return CreateSequenceValue(Args, VS_INTEROP_VALUE_QUEUE, "create queue value");
	}

	PyObject* ValuePriorityQueue(PyObject*, PyObject* Args)
	{
		return CreateSequenceValue(
		    Args, VS_INTEROP_VALUE_PRIORITY_QUEUE, "create priority queue value");
	}

	PyObject* ValueStack(PyObject*, PyObject* Args)
	{
		return CreateSequenceValue(Args, VS_INTEROP_VALUE_STACK, "create stack value");
	}

	PyObject* ValueDeque(PyObject*, PyObject* Args)
	{
		return CreateSequenceValue(Args, VS_INTEROP_VALUE_DEQUE, "create deque value");
	}

	PyObject* ValueSet(PyObject*, PyObject* Args)
	{
		const char* ElementType = nullptr;
		Py_ssize_t ElementTypeSize = 0;
		PyObject* Elements = nullptr;
		if (!PyArg_ParseTuple(Args, "s#O", &ElementType, &ElementTypeSize, &Elements))
		{
			return nullptr;
		}
		try
		{
			FTemporaryValues Values;
			if (!BuildTypedValues(Elements,
			        {ElementType, static_cast<std::size_t>(ElementTypeSize)}, Values))
			{
				return nullptr;
			}
			vs_interop_value* Output = nullptr;
			vs_interop_status Status = vs_interop_value_create(&Output);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				Status = vs_interop_value_create_set(Output, View(ElementType, ElementTypeSize),
				    Values.Handles.data(), static_cast<std::uint64_t>(Values.Handles.size()));
			}
			if (Status != VS_INTEROP_STATUS_OK)
			{
				vs_interop_value_destroy(Output);
				return RaiseStatus(nullptr, Status, "create set value");
			}
			return NewValueTakingOwnership(Output);
		}
		catch (const std::bad_alloc&)
		{
			return PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "unexpected set construction error");
			return nullptr;
		}
	}

	PyObject* ValueMap(PyObject*, PyObject* Args, PyObject* Keywords)
	{
		const char* KeyType = nullptr;
		Py_ssize_t KeyTypeSize = 0;
		const char* MappedType = nullptr;
		Py_ssize_t MappedTypeSize = 0;
		PyObject* Mapping = nullptr;
		int Ordered = 0;
		static const char* Names[] = {
		    "key_type", "value_type", "mapping", "ordered", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#s#O|p:map",
		        const_cast<char**>(Names), &KeyType, &KeyTypeSize,
		        &MappedType, &MappedTypeSize, &Mapping, &Ordered))
		{
			return nullptr;
		}
		FPyOwnedRef Items(CheckedMappingItems(Mapping));
		if (Items.Get() == nullptr)
		{
			return nullptr;
		}
		try
		{
			FTemporaryValues Keys;
			FTemporaryValues MappedValues;
			const Py_ssize_t Count = PyList_GET_SIZE(Items.Get());
			Keys.Handles.reserve(static_cast<std::size_t>(Count));
			Keys.Owned.reserve(static_cast<std::size_t>(Count));
			MappedValues.Handles.reserve(static_cast<std::size_t>(Count));
			MappedValues.Owned.reserve(static_cast<std::size_t>(Count));
			for (Py_ssize_t Index = 0; Index < Count; ++Index)
			{
				PyObject* Item = PyList_GET_ITEM(Items.Get(), Index);
				if (!PyTuple_Check(Item) || PyTuple_GET_SIZE(Item) != 2 || !AppendTypedValue(Keys, PyTuple_GET_ITEM(Item, 0), {KeyType, static_cast<std::size_t>(KeyTypeSize)}) || !AppendTypedValue(MappedValues, PyTuple_GET_ITEM(Item, 1), {MappedType, static_cast<std::size_t>(MappedTypeSize)}))
				{
					return nullptr;
				}
			}
			vs_interop_value* Output = nullptr;
			vs_interop_status Status = vs_interop_value_create(&Output);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				Status = vs_interop_value_create_map(Output, View(KeyType, KeyTypeSize),
				    View(MappedType, MappedTypeSize), static_cast<std::uint8_t>(Ordered),
				    Keys.Handles.data(), MappedValues.Handles.data(),
				    static_cast<std::uint64_t>(Keys.Handles.size()));
			}
			if (Status != VS_INTEROP_STATUS_OK)
			{
				vs_interop_value_destroy(Output);
				return RaiseStatus(nullptr, Status, "create map value");
			}
			return NewValueTakingOwnership(Output);
		}
		catch (const std::bad_alloc&)
		{
			return PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "unexpected map construction error");
			return nullptr;
		}
	}

	PyObject* PythonFromValueDeep(const vs_interop_value* Value);

	enum class EMapSnapshotPart
	{
		Keys,
		Values,
		Items
	};

	struct FMapSnapshotContext
	{
		PyObject* List = nullptr;
		EMapSnapshotPart Part = EMapSnapshotPart::Keys;
		bool bDeep = false;
	};

	PyObject* PythonFromBorrowedValue(
	    const vs_interop_value* Value,
	    const bool bDeep)
	{
		if (bDeep)
		{
			return PythonFromValueDeep(Value);
		}
		vs_interop_value* Clone = nullptr;
		const vs_interop_status Status = vs_interop_value_clone(Value, &Clone);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "clone enumerated container value");
		}
		return TakeValue(Clone);
	}

	vs_interop_callback_result VS_INTEROP_CALL AppendMapSnapshotEntry(
	    const vs_interop_value* Key,
	    const vs_interop_value* Mapped,
	    void* UserData)
	{
		auto& Context = *static_cast<FMapSnapshotContext*>(UserData);
		FPyOwnedRef First(PythonFromBorrowedValue(
		    Context.Part == EMapSnapshotPart::Values ? Mapped : Key, Context.bDeep));
		if (First.Get() == nullptr)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		PyObject* Item = First.Get();
		FPyOwnedRef Pair;
		if (Context.Part == EMapSnapshotPart::Items)
		{
			FPyOwnedRef Second(PythonFromBorrowedValue(Mapped, Context.bDeep));
			if (Second.Get() == nullptr)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			Pair.Object = PyTuple_Pack(2, First.Get(), Second.Get());
			if (Pair.Get() == nullptr)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			Item = Pair.Get();
		}
		return PyList_Append(Context.List, Item) == 0
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	struct FSetSnapshotContext
	{
		PyObject* List = nullptr;
		bool bDeep = false;
	};

	vs_interop_callback_result VS_INTEROP_CALL AppendSetSnapshotElement(
	    const vs_interop_value* Element,
	    void* UserData)
	{
		auto& Context = *static_cast<FSetSnapshotContext*>(UserData);
		FPyOwnedRef Item(PythonFromBorrowedValue(Element, Context.bDeep));
		if (Item.Get() == nullptr)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return PyList_Append(Context.List, Item.Get()) == 0
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}

	PyObject* CollectMapSnapshot(
	    const vs_interop_value* Value,
	    const EMapSnapshotPart Part,
	    const bool bDeep)
	{
		FPyOwnedRef List(PyList_New(0));
		if (List.Get() == nullptr)
		{
			return nullptr;
		}
		FMapSnapshotContext Context{List.Get(), Part, bDeep};
		const vs_interop_status Status = vs_interop_value_map_enumerate(
		    Value, AppendMapSnapshotEntry, &Context);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			if (Status == VS_INTEROP_STATUS_CALLBACK_ERROR && PyErr_Occurred())
			{
				return nullptr;
			}
			return RaiseStatus(nullptr, Status, "enumerate map value");
		}
		return List.Release();
	}

	PyObject* CollectSetSnapshot(const vs_interop_value* Value, const bool bDeep)
	{
		FPyOwnedRef List(PyList_New(0));
		if (List.Get() == nullptr)
		{
			return nullptr;
		}
		FSetSnapshotContext Context{List.Get(), bDeep};
		const vs_interop_status Status = vs_interop_value_set_enumerate(
		    Value, AppendSetSnapshotElement, &Context);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			if (Status == VS_INTEROP_STATUS_CALLBACK_ERROR && PyErr_Occurred())
			{
				return nullptr;
			}
			return RaiseStatus(nullptr, Status, "enumerate set value");
		}
		return List.Release();
	}

	bool IsSequenceValueKind(const vs_interop_value_kind Kind)
	{
		return Kind == VS_INTEROP_VALUE_ARRAY || Kind == VS_INTEROP_VALUE_NATIVE_ARRAY || Kind == VS_INTEROP_VALUE_QUEUE || Kind == VS_INTEROP_VALUE_PRIORITY_QUEUE || Kind == VS_INTEROP_VALUE_STACK || Kind == VS_INTEROP_VALUE_DEQUE;
	}

	PyObject* CollectSequenceSnapshot(const vs_interop_value* Value, const bool bDeep)
	{
		std::uint64_t Count = 0;
		if (vs_interop_value_sequence_count(Value, &Count) != VS_INTEROP_STATUS_OK || Count > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(GError, "failed to inspect VeroScript sequence");
			return nullptr;
		}
		PyObject* List = PyList_New(static_cast<Py_ssize_t>(Count));
		if (List == nullptr)
		{
			return nullptr;
		}
		for (std::uint64_t Index = 0; Index < Count; ++Index)
		{
			vs_interop_value* Element = nullptr;
			vs_interop_status Status = vs_interop_value_create(&Element);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				Status = vs_interop_value_sequence_get(Value, Index, Element);
			}
			if (Status != VS_INTEROP_STATUS_OK)
			{
				vs_interop_value_destroy(Element);
				Py_DECREF(List);
				return RaiseStatus(nullptr, Status, "read VeroScript sequence element");
			}
			PyObject* PythonElement = bDeep
			    ? PythonFromValueDeep(Element)
			    : TakeValue(Element);
			if (bDeep)
			{
				vs_interop_value_destroy(Element);
			}
			if (PythonElement == nullptr)
			{
				Py_DECREF(List);
				return nullptr;
			}
			PyList_SET_ITEM(List, static_cast<Py_ssize_t>(Index), PythonElement);
		}
		return List;
	}

	struct FRecursionGuard
	{
		~FRecursionGuard()
		{
			if (bEntered)
			{
				Py_LeaveRecursiveCall();
			}
		}

		bool bEntered = false;
	};

	PyObject* PythonFromValueDeep(const vs_interop_value* Value)
	{
		bool IsScalar = false;
		PyObject* Scalar = ScalarFromValue(Value, &IsScalar);
		if (IsScalar)
		{
			return Scalar;
		}
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Value);
		if (!IsSequenceValueKind(Kind) && Kind != VS_INTEROP_VALUE_MAP && Kind != VS_INTEROP_VALUE_ORDERED_MAP && Kind != VS_INTEROP_VALUE_SET)
		{
			vs_interop_value* Clone = nullptr;
			const vs_interop_status Status = vs_interop_value_clone(Value, &Clone);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				return RaiseStatus(nullptr, Status, "clone opaque VeroScript value");
			}
			return NewValueTakingOwnership(Clone);
		}
		FRecursionGuard Guard;
		if (Py_EnterRecursiveCall(" while converting a VeroScript container") != 0)
		{
			return nullptr;
		}
		Guard.bEntered = true;
		if (IsSequenceValueKind(Kind))
		{
			return CollectSequenceSnapshot(Value, true);
		}
		if (Kind == VS_INTEROP_VALUE_SET)
		{
			FPyOwnedRef Elements(CollectSetSnapshot(Value, true));
			return Elements.Get() == nullptr ? nullptr : PySet_New(Elements.Get());
		}
		FPyOwnedRef Items(CollectMapSnapshot(Value, EMapSnapshotPart::Items, true));
		if (Items.Get() == nullptr)
		{
			return nullptr;
		}
		FPyOwnedRef Result(PyDict_New());
		if (Result.Get() == nullptr)
		{
			return nullptr;
		}
		const Py_ssize_t Count = PyList_GET_SIZE(Items.Get());
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			PyObject* Pair = PyList_GET_ITEM(Items.Get(), Index);
			if (PyDict_SetItem(Result.Get(), PyTuple_GET_ITEM(Pair, 0),
			        PyTuple_GET_ITEM(Pair, 1)) < 0)
			{
				return nullptr;
			}
		}
		return Result.Release();
	}

	PyObject* ValueToPython(FPyValue* Self, PyObject*)
	{
		return PythonFromValueDeep(Self->Handle);
	}

	vs_interop_status GetContainerCount(
	    const vs_interop_value* Value,
	    std::uint64_t& Count)
	{
		vs_interop_status Status = VS_INTEROP_STATUS_TYPE_MISMATCH;
		switch (vs_interop_value_get_kind(Value))
		{
		case VS_INTEROP_VALUE_ARRAY:
		case VS_INTEROP_VALUE_NATIVE_ARRAY:
		case VS_INTEROP_VALUE_QUEUE:
		case VS_INTEROP_VALUE_PRIORITY_QUEUE:
		case VS_INTEROP_VALUE_STACK:
		case VS_INTEROP_VALUE_DEQUE:
			{
				Status = vs_interop_value_sequence_count(Value, &Count);
				break;
			}
		case VS_INTEROP_VALUE_MAP:
		case VS_INTEROP_VALUE_ORDERED_MAP:
			{
				Status = vs_interop_value_map_count(Value, &Count);
				break;
			}
		case VS_INTEROP_VALUE_SET:
			{
				Status = vs_interop_value_set_count(Value, &Count);
				break;
			}
		case VS_INTEROP_VALUE_CHANNEL:
			{
				Status = vs_interop_value_channel_count(Value, &Count);
				break;
			}
		default:
			{
				break;
			}
		}
		return Status;
	}

	PyObject* ValueGetCount(FPyValue* Self, void*)
	{
		std::uint64_t Count = 0;
		const vs_interop_status Status = GetContainerCount(Self->Handle, Count);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "get container count");
		}
		return PyLong_FromUnsignedLongLong(Count);
	}

	bool ReadContainerTypeArguments(const vs_interop_value* Value,
	    std::vector<std::string>& Arguments)
	{
		vs_interop_string_view TypeView{};
		if (vs_interop_value_get_type_name(Value, &TypeView) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		if (TypeView.size > static_cast<std::uint64_t>(
		                        std::numeric_limits<std::size_t>::max()) ||
		    (TypeView.size != 0 && TypeView.data == nullptr))
		{
			return false;
		}
		const std::string_view Type(TypeView.size == 0 ? "" : TypeView.data,
		    static_cast<std::size_t>(TypeView.size));
		const std::size_t Open = Type.find('<');
		if (Open == std::string_view::npos || Type.empty() || Type.back() != '>')
		{
			return false;
		}
		std::size_t Start = Open + 1;
		std::size_t Depth = 0;
		for (std::size_t Index = Start; Index < Type.size() - 1; ++Index)
		{
			if (Type[Index] == '<')
			{
				++Depth;
			}
			else if (Type[Index] == '>')
			{
				if (Depth == 0)
				{
					return false;
				}
				--Depth;
			}
			else if (Type[Index] == ',' && Depth == 0)
			{
				Arguments.emplace_back(Type.substr(Start, Index - Start));
				Start = Index + 1;
			}
		}
		Arguments.emplace_back(Type.substr(Start, Type.size() - 1 - Start));
		return true;
	}

	PyObject* LookupMapValue(
	    FPyValue* Self,
	    PyObject* KeyObject,
	    bool& bFound)
	try
	{
		bFound = false;
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		if (Kind != VS_INTEROP_VALUE_MAP && Kind != VS_INTEROP_VALUE_ORDERED_MAP)
		{
			PyErr_SetString(GTypeMismatchError, "Value.get requires a map value");
			return nullptr;
		}
		std::vector<std::string> TypeArguments;
		if (!ReadContainerTypeArguments(Self->Handle, TypeArguments) || TypeArguments.size() != 2)
		{
			PyErr_SetString(GError, "failed to read map type arguments");
			return nullptr;
		}
		FTemporaryValues Key;
		Key.Handles.reserve(1);
		Key.Owned.reserve(1);
		if (!AppendTypedValue(Key, KeyObject, TypeArguments[0]))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		std::uint8_t Found = 0;
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_value_map_get(Self->Handle, Key.Handles[0], Output, &Found);
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Output);
			return RaiseStatus(nullptr, Status, "read map value");
		}
		if (Found == 0)
		{
			vs_interop_value_destroy(Output);
			return nullptr;
		}
		bFound = true;
		return TakeValue(Output);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected map lookup error");
		return nullptr;
	}

	PyObject* ValueMapGet(FPyValue* Self, PyObject* Args)
	{
		PyObject* KeyObject = nullptr;
		PyObject* Default = Py_None;
		if (!PyArg_ParseTuple(Args, "O|O:get", &KeyObject, &Default))
		{
			return nullptr;
		}
		bool bFound = false;
		PyObject* Result = LookupMapValue(Self, KeyObject, bFound);
		if (Result != nullptr)
		{
			return Result;
		}
		if (PyErr_Occurred())
		{
			return nullptr;
		}
		Py_INCREF(Default);
		return Default;
	}

	int ValueContainsSlot(FPyValue* Self, PyObject* Object)
	try
	{
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		if (Kind == VS_INTEROP_VALUE_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MAP)
		{
			bool bFound = false;
			PyObject* Result = LookupMapValue(Self, Object, bFound);
			Py_XDECREF(Result);
			return PyErr_Occurred() ? -1 : (bFound ? 1 : 0);
		}
		if (Kind == VS_INTEROP_VALUE_SET)
		{
			std::vector<std::string> TypeArguments;
			if (!ReadContainerTypeArguments(Self->Handle, TypeArguments) || TypeArguments.size() != 1)
			{
				PyErr_SetString(GError, "failed to read set type argument");
				return -1;
			}
			FTemporaryValues Element;
			Element.Handles.reserve(1);
			Element.Owned.reserve(1);
			if (!AppendTypedValue(Element, Object, TypeArguments[0]))
			{
				return -1;
			}
			std::uint8_t Contains = 0;
			const vs_interop_status Status = vs_interop_value_set_contains(
			    Self->Handle, Element.Handles[0], &Contains);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				RaiseStatus(nullptr, Status, "check set membership");
				return -1;
			}
			return Contains != 0 ? 1 : 0;
		}
		if (IsSequenceValueKind(Kind))
		{
			FPyOwnedRef Values(CollectSequenceSnapshot(Self->Handle, false));
			return Values.Get() == nullptr ? -1 : PySequence_Contains(Values.Get(), Object);
		}
		PyErr_SetString(PyExc_TypeError, "this VeroScript value is not a searchable container");
		return -1;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return -1;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected container membership error");
		return -1;
	}

	PyObject* ValueContains(FPyValue* Self, PyObject* Object)
	{
		const int Result = ValueContainsSlot(Self, Object);
		return Result < 0 ? nullptr : PyBool_FromLong(Result);
	}

	Py_ssize_t ValueLength(FPyValue* Self)
	{
		std::uint64_t Count = 0;
		const vs_interop_status Status = GetContainerCount(Self->Handle, Count);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "get container length");
			return -1;
		}
		if (Count > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "VeroScript container is too large for Python");
			return -1;
		}
		return static_cast<Py_ssize_t>(Count);
	}

	PyObject* ReadSequenceItem(FPyValue* Self, const Py_ssize_t Index)
	{
		vs_interop_value* Element = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Element);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_value_sequence_get(
			    Self->Handle, static_cast<std::uint64_t>(Index), Element);
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Element);
			return RaiseStatus(nullptr, Status, "read VeroScript sequence item");
		}
		return TakeValue(Element);
	}

	PyObject* ValueSubscript(FPyValue* Self, PyObject* Key)
	{
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		if (Kind == VS_INTEROP_VALUE_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MAP)
		{
			bool bFound = false;
			PyObject* Result = LookupMapValue(Self, Key, bFound);
			if (Result != nullptr || PyErr_Occurred())
			{
				return Result;
			}
			PyErr_SetObject(PyExc_KeyError, Key);
			return nullptr;
		}
		if (!IsSequenceValueKind(Kind))
		{
			PyErr_SetString(PyExc_TypeError, "this VeroScript value does not support indexing");
			return nullptr;
		}
		const Py_ssize_t Count = ValueLength(Self);
		if (Count < 0)
		{
			return nullptr;
		}
		if (PyIndex_Check(Key))
		{
			Py_ssize_t Index = PyNumber_AsSsize_t(Key, PyExc_IndexError);
			if (Index == -1 && PyErr_Occurred())
			{
				return nullptr;
			}
			if (Index < 0)
			{
				Index += Count;
			}
			if (Index < 0 || Index >= Count)
			{
				PyErr_SetString(PyExc_IndexError, "VeroScript sequence index out of range");
				return nullptr;
			}
			return ReadSequenceItem(Self, Index);
		}
		if (PySlice_Check(Key))
		{
			Py_ssize_t Start = 0;
			Py_ssize_t Stop = 0;
			Py_ssize_t Step = 0;
			Py_ssize_t SliceLength = 0;
			if (PySlice_GetIndicesEx(Key, Count, &Start, &Stop, &Step, &SliceLength) < 0)
			{
				return nullptr;
			}
			PyObject* Result = PyList_New(SliceLength);
			if (Result == nullptr)
			{
				return nullptr;
			}
			Py_ssize_t SourceIndex = Start;
			for (Py_ssize_t OutputIndex = 0; OutputIndex < SliceLength;
			    ++OutputIndex, SourceIndex += Step)
			{
				PyObject* Item = ReadSequenceItem(Self, SourceIndex);
				if (Item == nullptr)
				{
					Py_DECREF(Result);
					return nullptr;
				}
				PyList_SET_ITEM(Result, OutputIndex, Item);
			}
			return Result;
		}
		PyErr_SetString(PyExc_TypeError, "VeroScript sequence indices must be integers or slices");
		return nullptr;
	}

	PyObject* ValueIter(FPyValue* Self)
	{
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		PyObject* Snapshot = nullptr;
		if (Kind == VS_INTEROP_VALUE_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MAP)
		{
			Snapshot = CollectMapSnapshot(Self->Handle, EMapSnapshotPart::Keys, false);
		}
		else if (Kind == VS_INTEROP_VALUE_SET)
		{
			Snapshot = CollectSetSnapshot(Self->Handle, false);
		}
		else if (IsSequenceValueKind(Kind))
		{
			Snapshot = CollectSequenceSnapshot(Self->Handle, false);
		}
		else
		{
			PyErr_SetString(PyExc_TypeError, "this VeroScript value is not iterable");
			return nullptr;
		}
		if (Snapshot == nullptr)
		{
			return nullptr;
		}
		PyObject* Iterator = PyObject_GetIter(Snapshot);
		Py_DECREF(Snapshot);
		return Iterator;
	}

	PyObject* ValueMapSnapshotMethod(
	    FPyValue* Self,
	    const EMapSnapshotPart Part,
	    const char* MethodName)
	{
		const vs_interop_value_kind Kind = vs_interop_value_get_kind(Self->Handle);
		if (Kind != VS_INTEROP_VALUE_MAP && Kind != VS_INTEROP_VALUE_ORDERED_MAP)
		{
			PyErr_Format(GTypeMismatchError, "Value.%s requires a map value", MethodName);
			return nullptr;
		}
		return CollectMapSnapshot(Self->Handle, Part, false);
	}

	PyObject* ValueMapKeys(FPyValue* Self, PyObject*)
	{
		return ValueMapSnapshotMethod(Self, EMapSnapshotPart::Keys, "keys");
	}

	PyObject* ValueMapValues(FPyValue* Self, PyObject*)
	{
		return ValueMapSnapshotMethod(Self, EMapSnapshotPart::Values, "values");
	}

	PyObject* ValueMapItems(FPyValue* Self, PyObject*)
	{
		return ValueMapSnapshotMethod(Self, EMapSnapshotPart::Items, "items");
	}

	PyObject* ValueChannel(PyObject*, PyObject* Args)
	{
		const char* ElementType = nullptr;
		Py_ssize_t ElementTypeSize = 0;
		unsigned long long Capacity = 0;
		if (!PyArg_ParseTuple(Args, "s#|K:channel", &ElementType, &ElementTypeSize, &Capacity))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Status = vs_interop_value_create_bounded_channel(
			    Output, View(ElementType, ElementTypeSize), Capacity);
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Output);
			return RaiseStatus(nullptr, Status, "create channel value");
		}
		return NewValueTakingOwnership(Output);
	}

	PyObject* RaiseBufferError(const vs_interop_status Status,
	    vs_interop_buffer* Error, const char* Fallback)
	{
		PyObject* Message = nullptr;
		if (Error != nullptr)
		{
			const vs_interop_byte_view Bytes = vs_interop_buffer_view(Error);
			if (Bytes.size <= static_cast<std::uint64_t>(PY_SSIZE_T_MAX) && IsValidByteView(Bytes))
			{
				Message = PyUnicode_DecodeUTF8(ByteData(Bytes),
				    static_cast<Py_ssize_t>(Bytes.size), "replace");
			}
			vs_interop_buffer_destroy(Error);
		}
		if (Message == nullptr)
		{
			PyErr_Clear();
			Message = PyUnicode_FromString(Fallback);
		}
		if (Message == nullptr)
		{
			return nullptr;
		}
		PyErr_SetObject(ExceptionForStatus(Status), Message);
		Py_DECREF(Message);
		return nullptr;
	}

	PyObject* ValueChannelSend(FPyValue* Self, PyObject* Object)
	try
	{
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "Value.send requires a channel value");
			return nullptr;
		}
		std::vector<std::string> TypeArguments;
		if (!ReadContainerTypeArguments(Self->Handle, TypeArguments) || TypeArguments.size() != 1)
		{
			PyErr_SetString(GError, "failed to read channel element type");
			return nullptr;
		}
		FTemporaryValues Element;
		Element.Handles.reserve(1);
		Element.Owned.reserve(1);
		if (!AppendTypedValue(Element, Object, TypeArguments[0]))
		{
			return nullptr;
		}
		vs_interop_buffer* Error = nullptr;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_value_channel_send(Self->Handle, Element.Handles[0], &Error);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseBufferError(Status, Error, "send to VeroScript channel failed");
		}
		vs_interop_buffer_destroy(Error);
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected channel send error");
		return nullptr;
	}

	PyObject* ValueChannelTrySend(FPyValue* Self, PyObject* Object)
	try
	{
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "Value.try_send requires a channel value");
			return nullptr;
		}
		std::vector<std::string> TypeArguments;
		if (!ReadContainerTypeArguments(Self->Handle, TypeArguments) || TypeArguments.size() != 1)
		{
			PyErr_SetString(GError, "failed to read channel element type");
			return nullptr;
		}
		FTemporaryValues Element;
		Element.Handles.reserve(1);
		Element.Owned.reserve(1);
		if (!AppendTypedValue(Element, Object, TypeArguments[0]))
		{
			return nullptr;
		}
		std::uint8_t Sent = 0;
		const vs_interop_status Status = vs_interop_value_channel_try_send(
		    Self->Handle, Element.Handles[0], &Sent);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "try send to VeroScript channel");
		}
		return PyBool_FromLong(Sent != 0);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected channel try-send error");
		return nullptr;
	}

	PyObject* ValueChannelSendFor(FPyValue* Self, PyObject* Args)
	try
	{
		PyObject* Object = nullptr;
		unsigned long long TimeoutMs = 0;
		if (!PyArg_ParseTuple(Args, "OK:send_for", &Object, &TimeoutMs))
		{
			return nullptr;
		}
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "Value.send_for requires a channel value");
			return nullptr;
		}
		std::vector<std::string> TypeArguments;
		if (!ReadContainerTypeArguments(Self->Handle, TypeArguments) || TypeArguments.size() != 1)
		{
			PyErr_SetString(GError, "failed to read channel element type");
			return nullptr;
		}
		FTemporaryValues Element;
		Element.Handles.reserve(1);
		Element.Owned.reserve(1);
		if (!AppendTypedValue(Element, Object, TypeArguments[0]))
		{
			return nullptr;
		}
		std::uint8_t Sent = 0;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_value_channel_send_for(
		        Self->Handle, Element.Handles[0], TimeoutMs, &Sent);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "timed send to VeroScript channel");
		}
		return PyBool_FromLong(Sent != 0);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected channel timed-send error");
		return nullptr;
	}

	PyObject* ValueChannelReceive(FPyValue* Self, PyObject*)
	{
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "Value.receive requires a channel value");
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create channel receive value");
		}
		vs_interop_buffer* Error = nullptr;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_value_channel_receive(Self->Handle, Output, &Error);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Output);
			return RaiseBufferError(Status, Error, "receive from VeroScript channel failed");
		}
		vs_interop_buffer_destroy(Error);
		return TakeValue(Output);
	}

	PyObject* ValueChannelReceiveOptional(
	    FPyValue* Self,
	    const std::uint64_t TimeoutMs,
	    const bool bTimed)
	{
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "channel receive requires a channel value");
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create channel receive value");
		}
		std::uint8_t Received = 0;
		Py_BEGIN_ALLOW_THREADS
		    Status = bTimed
		    ? vs_interop_value_channel_receive_for(Self->Handle, TimeoutMs, Output, &Received)
		    : vs_interop_value_channel_try_receive(Self->Handle, Output, &Received);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_value_destroy(Output);
			return RaiseStatus(nullptr, Status, "receive from VeroScript channel");
		}
		if (!Received)
		{
			vs_interop_value_destroy(Output);
			Py_RETURN_NONE;
		}
		return TakeValue(Output);
	}

	PyObject* ValueChannelTryReceive(FPyValue* Self, PyObject*)
	{
		return ValueChannelReceiveOptional(Self, 0, false);
	}

	PyObject* ValueChannelReceiveFor(FPyValue* Self, PyObject* Args)
	{
		unsigned long long TimeoutMs = 0;
		if (!PyArg_ParseTuple(Args, "K:receive_for", &TimeoutMs))
		{
			return nullptr;
		}
		return ValueChannelReceiveOptional(Self, TimeoutMs, true);
	}

	PyObject* ValueGetChannelCapacity(FPyValue* Self, void*)
	{
		std::uint64_t Capacity = 0;
		const vs_interop_status Status =
		    vs_interop_value_channel_capacity(Self->Handle, &Capacity);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "get channel capacity");
		}
		return PyLong_FromUnsignedLongLong(Capacity);
	}

	PyObject* ValueGetChannelClosed(FPyValue* Self, void*)
	{
		std::uint8_t Closed = 0;
		const vs_interop_status Status =
		    vs_interop_value_channel_is_closed(Self->Handle, &Closed);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "query channel state");
		}
		return PyBool_FromLong(Closed != 0);
	}

	PyObject* ValueChannelClose(FPyValue* Self, PyObject*)
	{
		if (vs_interop_value_get_kind(Self->Handle) != VS_INTEROP_VALUE_CHANNEL)
		{
			PyErr_SetString(GTypeMismatchError, "Value.close_channel requires a channel value");
			return nullptr;
		}
		const vs_interop_status Status = vs_interop_value_channel_close(Self->Handle);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "close VeroScript channel");
		}
		Py_RETURN_NONE;
	}

	PyObject* ValueRepr(FPyValue* Self)
	{
		PyObject* Value = ValueGetValue(Self, nullptr);
		if (Value == nullptr)
		{
			PyErr_Clear();
			return PyUnicode_FromFormat("veroscript.Value(<%s>)",
			    KindName(vs_interop_value_get_kind(Self->Handle)));
		}
		PyObject* Result = PyUnicode_FromFormat("veroscript.Value(%R, '%s')", Value,
		    KindName(vs_interop_value_get_kind(Self->Handle)));
		Py_DECREF(Value);
		return Result;
	}

	#include "VeroScriptPythonVariant.inl"
	PyMethodDef ValueMethods[] = {
	    {"empty_variant", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantEmpty>::Invoke), METH_CLASS | METH_NOARGS, "Create an empty variant."},
	    {"set_variant_empty", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantSetEmpty>::Invoke), METH_NOARGS, "Replace with an empty variant."},
	    {"assign_variant", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantAssign>::Invoke), METH_O, "Assign another variant Value."},
	    {"variant_has_value", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantHasValue>::Invoke), METH_NOARGS, "Whether the variant has a payload."},
	    {"variant_type_name", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantTypeName>::Invoke), METH_NOARGS, "Canonical payload type, or empty."},
	    {"variant_get", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueVariantGet>::Invoke), METH_NOARGS, "Extract an owned payload snapshot."},
	    {"clone", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueClone>::Invoke), METH_NOARGS,
	        "Return an independently owned clone of this value."},
	    {"array", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueArray>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create an array from an element type and iterable."},
	    {"queue", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueQueue>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create a queue from an element type and iterable."},
	    {"priority_queue", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValuePriorityQueue>::Invoke),
	        METH_CLASS | METH_VARARGS,
	        "Create a priority queue from an element type and iterable."},
	    {"stack", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueStack>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create a stack from an element type and iterable."},
	    {"deque", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueDeque>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create a deque from an element type and iterable."},
	    {"channel", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannel>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create a typed channel."},
	    {"set", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueSet>::Invoke), METH_CLASS | METH_VARARGS,
	        "Create a set from an element type and iterable."},
	    {"map", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueMap>::Invoke),
	        METH_CLASS | METH_VARARGS | METH_KEYWORDS,
	        "Create a map from key/value types and a mapping."},
	    {"to_python", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueToPython>::Invoke), METH_NOARGS,
	        "Recursively convert representable values to Python containers and scalars."},
	    {"get", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueMapGet>::Invoke), METH_VARARGS,
	        "Read a map entry, returning an optional default when absent."},
	    {"contains", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueContains>::Invoke), METH_O,
	        "Check container membership."},
	    {"keys", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueMapKeys>::Invoke), METH_NOARGS,
	        "Return an owned snapshot list of map keys."},
	    {"values", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueMapValues>::Invoke), METH_NOARGS,
	        "Return an owned snapshot list of map values."},
	    {"items", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueMapItems>::Invoke), METH_NOARGS,
	        "Return an owned snapshot list of map key/value pairs."},
	    {"send", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelSend>::Invoke), METH_O,
	        "Send a value through a channel."},
	    {"try_send", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelTrySend>::Invoke), METH_O,
	        "Try to send without blocking; return false when full or closed."},
	    {"send_for", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelSendFor>::Invoke), METH_VARARGS,
	        "Send with a timeout in milliseconds."},
	    {"receive", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelReceive>::Invoke), METH_NOARGS,
	        "Receive a value from a channel, blocking with the GIL released."},
	    {"try_receive", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelTryReceive>::Invoke), METH_NOARGS,
	        "Try to receive without blocking; return None when empty or closed."},
	    {"receive_for", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelReceiveFor>::Invoke), METH_VARARGS,
	        "Receive with a timeout in milliseconds; return None on timeout or close."},
	    {"close_channel", reinterpret_cast<PyCFunction>(TValueCallbackBoundary<&ValueChannelClose>::Invoke), METH_NOARGS,
	        "Close a channel."},
	    {nullptr, nullptr, 0, nullptr}};

	PyGetSetDef ValueGetSet[] = {
	    {const_cast<char*>("value"), reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetValue>::Invoke),
	        reinterpret_cast<setter>(TValueCallbackBoundary<&ValueSetValue>::Invoke),
	        const_cast<char*>("Python scalar value"), nullptr},
	    {const_cast<char*>("kind"), reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetKind>::Invoke), nullptr,
	        const_cast<char*>("Interop value kind"), nullptr},
	    {const_cast<char*>("numeric_kind"), reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetNumericKind>::Invoke), nullptr,
	        const_cast<char*>("Verified numeric kind: 0 other, 1 bigint, 2 rational"), nullptr},
	    {const_cast<char*>("type_name"), reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetTypeName>::Invoke), nullptr,
	        const_cast<char*>("VeroScript type name"), nullptr},
	    {const_cast<char*>("count"), reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetCount>::Invoke), nullptr,
	        const_cast<char*>("container element count"), nullptr},
	    {const_cast<char*>("channel_capacity"),
	        reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetChannelCapacity>::Invoke), nullptr,
	        const_cast<char*>("channel capacity; zero means unbounded"), nullptr},
	    {const_cast<char*>("channel_closed"),
	        reinterpret_cast<getter>(TValueCallbackBoundary<&ValueGetChannelClosed>::Invoke), nullptr,
	        const_cast<char*>("whether this channel is closed"), nullptr},
	    {nullptr, nullptr, nullptr, nullptr, nullptr}};

	PyObject* PackageNew(PyTypeObject* Type, PyObject*, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		auto* Self = reinterpret_cast<FPyPackage*>(Type->tp_alloc(Type, 0));
		if (Self != nullptr)
		{
			Self->Handle = nullptr;
		}
		return reinterpret_cast<PyObject*>(Self);
	}

	int PackageInit(FPyPackage* Self, PyObject* Args, PyObject* Keywords)
	{
		PyObject* Data = nullptr;
		static const char* Names[] = {"data", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "O:Package",
		        const_cast<char**>(Names), &Data))
		{
			return -1;
		}
		if (!CheckInterpreter()) return -1;
		if (Self->Handle != nullptr)
		{
			PyErr_SetString(GError, "VeroScript Package cannot be reinitialized");
			return -1;
		}
		Py_buffer Buffer{};
		if (PyObject_GetBuffer(Data, &Buffer, PyBUF_CONTIG_RO) != 0)
		{
			return -1;
		}
		if (Buffer.len < 0 || (Buffer.len != 0 && Buffer.buf == nullptr))
		{
			PyBuffer_Release(&Buffer);
			PyErr_SetString(PyExc_BufferError, "package data exported an invalid buffer");
			return -1;
		}
		vs_interop_package* Package = nullptr;
		vs_interop_buffer* Error = nullptr;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_package_read(
		        {static_cast<const std::uint8_t*>(Buffer.buf),
		            static_cast<std::uint64_t>(Buffer.len)},
		        &Package, &Error);
		Py_END_ALLOW_THREADS
		    PyBuffer_Release(&Buffer);
		if (Status != VS_INTEROP_STATUS_OK || Package == nullptr)
		{
			PyObject* Message = nullptr;
			if (Error != nullptr)
			{
				const vs_interop_byte_view Bytes = vs_interop_buffer_view(Error);
				if (Bytes.size <= static_cast<std::uint64_t>(PY_SSIZE_T_MAX) && IsValidByteView(Bytes))
				{
					Message = PyUnicode_DecodeUTF8(
					    ByteData(Bytes),
					    static_cast<Py_ssize_t>(Bytes.size), "replace");
				}
				vs_interop_buffer_destroy(Error);
			}
			if (Message == nullptr)
			{
				PyErr_Clear();
				Message = PyUnicode_FromString(Status == VS_INTEROP_STATUS_OK
				        ? "VeroScript package reader returned no package"
				        : "invalid VeroScript package data");
			}
			if (Message != nullptr)
			{
				PyErr_SetObject(ExceptionForStatus(Status), Message);
				Py_DECREF(Message);
			}
			vs_interop_package_destroy(Package);
			return -1;
		}
		vs_interop_buffer_destroy(Error);
		// Buffer hooks and the GIL-free read can initialize this object reentrantly.
		// Published package handles stay immutable while native loads borrow them.
		if (Self->Handle != nullptr)
		{
			vs_interop_package_destroy(Package);
			PyErr_SetString(GError, "VeroScript Package cannot be reinitialized");
			return -1;
		}
		Self->Handle = Package;
		return 0;
	}

	void PackageDealloc(FPyPackage* Self)
	{
		vs_interop_package_destroy(Self->Handle);
		Self->Handle = nullptr;
		Py_TYPE(Self)->tp_free(reinterpret_cast<PyObject*>(Self));
	}

	PyObject* PackageGetKind(FPyPackage* Self, void*)
	{
		return PyLong_FromUnsignedLong(vs_interop_package_get_kind(Self->Handle));
	}

	PyObject* PackageGetName(FPyPackage* Self, void*)
	{
		return UnicodeFromView(vs_interop_package_get_name(Self->Handle));
	}

	PyObject* PackageGetVersion(FPyPackage* Self, void*)
	{
		return PyLong_FromUnsignedLongLong(vs_interop_package_get_version(Self->Handle));
	}

	PyObject* PackageGetMetadata(FPyPackage* Self, void*)
	{
		const std::uint64_t Count = vs_interop_package_metadata_count(Self->Handle);
		if (Count > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "package metadata is too large for Python");
			return nullptr;
		}
		PyObject* Result = PyDict_New();
		if (Result == nullptr)
		{
			return nullptr;
		}
		for (std::uint64_t Index = 0; Index < Count; ++Index)
		{
			vs_interop_package_metadata Metadata{};
			const vs_interop_status Status =
			    vs_interop_package_get_metadata(Self->Handle, Index, &Metadata);
			if (Status != VS_INTEROP_STATUS_OK || Metadata.value.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX) || !IsValidByteView(Metadata.value))
			{
				Py_DECREF(Result);
				if (Status != VS_INTEROP_STATUS_OK)
				{
					RaiseStatus(nullptr, Status, "read package metadata");
				}
				else if (!IsValidByteView(Metadata.value))
				{
					PyErr_SetString(GError, "package metadata contains an invalid byte view");
				}
				else
				{
					PyErr_SetString(PyExc_OverflowError,
					    "package metadata value is too large for Python");
				}
				return nullptr;
			}
			PyObject* Key = UnicodeFromView(Metadata.key);
			PyObject* Value = PyBytes_FromStringAndSize(
			    ByteData(Metadata.value),
			    static_cast<Py_ssize_t>(Metadata.value.size));
			if (Key == nullptr || Value == nullptr || PyDict_SetItem(Result, Key, Value) != 0)
			{
				Py_XDECREF(Key);
				Py_XDECREF(Value);
				Py_DECREF(Result);
				return nullptr;
			}
			Py_DECREF(Key);
			Py_DECREF(Value);
		}
		return Result;
	}

	PyObject* PackageGetCanonicalBytes(FPyPackage* Self, void*)
	{
		const vs_interop_byte_view Bytes =
		    vs_interop_package_get_canonical_signed_bytes(Self->Handle);
		if (Bytes.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "package data is too large for Python");
			return nullptr;
		}
		if (!IsValidByteView(Bytes))
		{
			PyErr_SetString(GError, "package returned an invalid canonical byte view");
			return nullptr;
		}
		return PyBytes_FromStringAndSize(ByteData(Bytes),
		    static_cast<Py_ssize_t>(Bytes.size));
	}

	PyObject* PackageRepr(FPyPackage* Self)
	{
		PyObject* Name = PackageGetName(Self, nullptr);
		if (Name == nullptr)
		{
			return nullptr;
		}
		PyObject* Result = PyUnicode_FromFormat("veroscript.Package(name=%R, version=%llu)",
		    Name, static_cast<unsigned long long>(vs_interop_package_get_version(Self->Handle)));
		Py_DECREF(Name);
		return Result;
	}

	PyGetSetDef PackageGetSet[] = {
	    {const_cast<char*>("kind"), reinterpret_cast<getter>(PackageGetKind), nullptr,
	        const_cast<char*>("package kind"), nullptr},
	    {const_cast<char*>("name"), reinterpret_cast<getter>(PackageGetName), nullptr,
	        const_cast<char*>("package name"), nullptr},
	    {const_cast<char*>("version"), reinterpret_cast<getter>(PackageGetVersion), nullptr,
	        const_cast<char*>("package version"), nullptr},
	    {const_cast<char*>("metadata"), reinterpret_cast<getter>(PackageGetMetadata), nullptr,
	        const_cast<char*>("host metadata mapping"), nullptr},
	    {const_cast<char*>("canonical_signed_bytes"),
	        reinterpret_cast<getter>(PackageGetCanonicalBytes), nullptr,
	        const_cast<char*>("canonical bytes covered by package signatures"), nullptr},
	    {nullptr, nullptr, nullptr, nullptr, nullptr}};

	struct FConfigStorage
	{
		std::vector<std::string> ImportPaths;
		std::vector<std::string> ImplicitImportPaths;
		std::vector<std::string> ScriptArgs;
		std::vector<std::string> Defines;
		std::vector<std::string> StdlibSearchPaths;
		std::vector<vs_interop_string_view> ImportViews;
		std::vector<vs_interop_string_view> ImplicitImportViews;
		std::vector<vs_interop_string_view> ScriptArgViews;
		std::vector<vs_interop_string_view> DefineViews;
		std::vector<vs_interop_string_view> StdlibSearchViews;
		std::vector<std::string> SourceOverlayPaths;
		std::vector<std::string> SourceOverlaySources;
		std::vector<vs_interop_source_overlay> SourceOverlays;
		std::string BytecodeCacheDirectory;
	};

	bool ParseStringList(PyObject* Config, const char* Name, std::vector<std::string>& Output)
	{
		PyObject* Item = PyDict_GetItemString(Config, Name);
		if (Item == nullptr)
		{
			return true;
		}
		FPyOwnedRef Sequence(SnapshotSequence(Item, "configuration list must be iterable"));
		if (Sequence.Get() == nullptr)
		{
			return false;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Sequence.Get());
		Output.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			PyObject* Value = PySequence_Fast_GET_ITEM(Sequence.Get(), Index);
			Py_ssize_t Size = 0;
			const char* Text = PyUnicode_AsUTF8AndSize(Value, &Size);
			if (Text == nullptr)
			{
				return false;
			}
			Output.emplace_back(Text, static_cast<std::size_t>(Size));
		}
		return true;
	}

	vs_interop_string_list MakeStringList(const std::vector<std::string>& Strings,
	    std::vector<vs_interop_string_view>& Views)
	{
		Views.reserve(Strings.size());
		for (const std::string& String : Strings)
		{
			Views.push_back(View(String));
		}
		return {Views.data(), static_cast<std::uint64_t>(Views.size())};
	}

	bool ParseSourceOverlays(PyObject* Config, FConfigStorage& Storage)
	{
		PyObject* Mapping = PyDict_GetItemString(Config, "source_overlays");
		if (Mapping == nullptr)
		{
			return true;
		}
		FPyOwnedRef Items(CheckedMappingItems(Mapping));
		if (Items.Get() == nullptr)
		{
			return false;
		}
		const Py_ssize_t Count = PyList_GET_SIZE(Items.Get());
		Storage.SourceOverlayPaths.reserve(static_cast<std::size_t>(Count));
		Storage.SourceOverlaySources.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			PyObject* Item = PyList_GET_ITEM(Items.Get(), Index);
			Py_ssize_t PathSize = 0;
			Py_ssize_t SourceSize = 0;
			const char* Path = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Item, 0), &PathSize);
			const char* Source = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Item, 1), &SourceSize);
			if (Path == nullptr || Source == nullptr)
			{
				return false;
			}
			Storage.SourceOverlayPaths.emplace_back(
			    Path, static_cast<std::size_t>(PathSize));
			Storage.SourceOverlaySources.emplace_back(
			    Source, static_cast<std::size_t>(SourceSize));
		}
		Storage.SourceOverlays.resize(static_cast<std::size_t>(Count));
		for (std::size_t Index = 0; Index < Storage.SourceOverlays.size(); ++Index)
		{
			Storage.SourceOverlays[Index].path = View(Storage.SourceOverlayPaths[Index]);
			Storage.SourceOverlays[Index].source = View(Storage.SourceOverlaySources[Index]);
		}
		return true;
	}

	bool GetUnsignedConfig(PyObject* Config, const char* Name, std::uint64_t* Output)
	{
		PyObject* Item = PyDict_GetItemString(Config, Name);
		if (Item == nullptr)
		{
			return true;
		}
		*Output = PyLong_AsUnsignedLongLong(Item);
		return PyErr_Occurred() == nullptr;
	}

	bool GetBoolConfig(PyObject* Config, const char* Name, std::uint8_t* Output)
	{
		PyObject* Item = PyDict_GetItemString(Config, Name);
		if (Item == nullptr)
		{
			return true;
		}
		const int Truth = PyObject_IsTrue(Item);
		if (Truth < 0)
		{
			return false;
		}
		*Output = static_cast<std::uint8_t>(Truth);
		return true;
	}

	bool GetUInt32Config(PyObject* Config, const char* Name, std::uint32_t* Output)
	{
		PyObject* Item = PyDict_GetItemString(Config, Name);
		if (Item == nullptr)
		{
			return true;
		}
		const unsigned long long Parsed = PyLong_AsUnsignedLongLong(Item);
		if (PyErr_Occurred() != nullptr)
		{
			return false;
		}
		if (Parsed > std::numeric_limits<std::uint32_t>::max())
		{
			PyErr_Format(PyExc_OverflowError, "%s must fit in uint32", Name);
			return false;
		}
		*Output = static_cast<std::uint32_t>(Parsed);
		return true;
	}

	bool BuildConfig(PyObject* ConfigObject, vs_interop_config& Config, FConfigStorage& Storage)
	{
		if (!PyDict_Check(ConfigObject))
		{
			PyErr_SetString(PyExc_TypeError, "Session config must be a dict");
			return false;
		}
		Config = {};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_MIXED;
		if (!ParseStringList(ConfigObject, "import_paths", Storage.ImportPaths) || !ParseStringList(ConfigObject, "implicit_import_paths", Storage.ImplicitImportPaths) || !ParseStringList(ConfigObject, "script_args", Storage.ScriptArgs) || !ParseStringList(ConfigObject, "defines", Storage.Defines) || !ParseStringList(ConfigObject, "stdlib_search_paths", Storage.StdlibSearchPaths) || !ParseSourceOverlays(ConfigObject, Storage))
		{
			return false;
		}
		Config.import_paths = MakeStringList(Storage.ImportPaths, Storage.ImportViews);
		Config.implicit_import_paths = MakeStringList(
		    Storage.ImplicitImportPaths, Storage.ImplicitImportViews);
		Config.script_args = MakeStringList(Storage.ScriptArgs, Storage.ScriptArgViews);
		Config.defines = MakeStringList(Storage.Defines, Storage.DefineViews);
		Config.stdlib_search_paths = MakeStringList(
		    Storage.StdlibSearchPaths, Storage.StdlibSearchViews);
		Config.source_overlays = Storage.SourceOverlays.data();
		Config.source_overlay_count =
		    static_cast<std::uint64_t>(Storage.SourceOverlays.size());

		if (PyObject* Item = PyDict_GetItemString(ConfigObject, "backend"))
		{
			const long Backend = PyLong_AsLong(Item);
			if (PyErr_Occurred())
			{
				return false;
			}
			if (Backend < VS_INTEROP_BACKEND_VM || Backend > VS_INTEROP_BACKEND_JIT)
			{
				PyErr_SetString(PyExc_ValueError, "backend must be VM, MIXED, or JIT");
				return false;
			}
			Config.execution_backend = static_cast<vs_interop_execution_backend>(Backend);
		}
		if (PyObject* Item = PyDict_GetItemString(ConfigObject, "bytecode_cache_directory"))
		{
			Py_ssize_t Size = 0;
			const char* Text = PyUnicode_AsUTF8AndSize(Item, &Size);
			if (Text == nullptr)
			{
				return false;
			}
			Storage.BytecodeCacheDirectory.assign(Text, static_cast<std::size_t>(Size));
			Config.bytecode_cache_directory = View(Storage.BytecodeCacheDirectory);
		}
		return GetUnsignedConfig(ConfigObject, "max_call_depth", &Config.max_call_depth) && GetUnsignedConfig(ConfigObject, "max_instructions", &Config.max_instructions) && GetUnsignedConfig(ConfigObject, "timeout_milliseconds", &Config.timeout_milliseconds) && GetUInt32Config(ConfigObject, "allowed_script_capabilities", &Config.allowed_script_capabilities) && GetBoolConfig(ConfigObject, "use_bytecode_cache", &Config.use_bytecode_cache) && GetBoolConfig(ConfigObject, "allow_circular_imports", &Config.allow_circular_imports) && GetBoolConfig(ConfigObject, "allow_project_wide_access", &Config.allow_project_wide_access) && GetBoolConfig(ConfigObject, "enable_execution_control", &Config.enable_execution_control) && GetBoolConfig(ConfigObject, "capability_policy_enabled", &Config.capability_policy_enabled);
	}

	PyObject* BuildDiagnostics(FPySession* Self)
	{
		const std::uint64_t Count = vs_interop_session_diagnostic_count(Self->Handle);
		if (Count > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "too many VeroScript diagnostics");
			return nullptr;
		}
		PyObject* List = PyList_New(static_cast<Py_ssize_t>(Count));
		if (List == nullptr)
		{
			return nullptr;
		}
		for (std::uint64_t Index = 0; Index < Count; ++Index)
		{
			vs_interop_diagnostic Diagnostic{};
			Diagnostic.struct_size = sizeof(Diagnostic);
			const vs_interop_status Status =
			    vs_interop_session_get_diagnostic(Self->Handle, Index, &Diagnostic);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				Py_DECREF(List);
				return RaiseStatus(Self, Status, "get diagnostic");
			}
			std::uint64_t ArgumentCount = 0;
			const vs_interop_status ArgumentCountStatus =
			    vs_interop_session_diagnostic_argument_count(
			        Self->Handle, Index, &ArgumentCount);
			if (ArgumentCountStatus != VS_INTEROP_STATUS_OK || ArgumentCount > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				Py_DECREF(List);
				if (ArgumentCountStatus != VS_INTEROP_STATUS_OK)
				{
					return RaiseStatus(Self, ArgumentCountStatus,
					    "get diagnostic argument count");
				}
				PyErr_SetString(PyExc_OverflowError,
				    "diagnostic has too many arguments for Python");
				return nullptr;
			}
			FPyOwnedRef Arguments(PyTuple_New(static_cast<Py_ssize_t>(ArgumentCount)));
			if (Arguments.Get() == nullptr)
			{
				Py_DECREF(List);
				return nullptr;
			}
			for (std::uint64_t ArgumentIndex = 0;
			    ArgumentIndex < ArgumentCount; ++ArgumentIndex)
			{
				vs_interop_string_view ArgumentView{};
				const vs_interop_status ArgumentStatus =
				    vs_interop_session_get_diagnostic_argument(
				        Self->Handle, Index, ArgumentIndex, &ArgumentView);
				PyObject* Argument = ArgumentStatus == VS_INTEROP_STATUS_OK
				    ? UnicodeFromView(ArgumentView)
				    : nullptr;
				if (Argument == nullptr)
				{
					Py_DECREF(List);
					if (ArgumentStatus != VS_INTEROP_STATUS_OK)
					{
						RaiseStatus(Self, ArgumentStatus, "get diagnostic argument");
					}
					return nullptr;
				}
				PyTuple_SET_ITEM(Arguments.Get(),
				    static_cast<Py_ssize_t>(ArgumentIndex), Argument);
			}
			// The core formatter selects the active catalog and falls back to the
			// diagnostic's authoritative English text when a translation is absent.
			vs_interop_buffer* Formatted = nullptr;
			PyObject* Message = nullptr;
			if (vs_interop_session_format_diagnostic(Self->Handle, Index, &Formatted) == VS_INTEROP_STATUS_OK && Formatted != nullptr)
			{
				const vs_interop_byte_view Bytes = vs_interop_buffer_view(Formatted);
				if (Bytes.size <= static_cast<std::uint64_t>(PY_SSIZE_T_MAX) && IsValidByteView(Bytes))
				{
					Message = PyUnicode_DecodeUTF8(ByteData(Bytes),
					    static_cast<Py_ssize_t>(Bytes.size), "strict");
				}
				vs_interop_buffer_destroy(Formatted);
			}
			if (Message != nullptr && Diagnostic.message_id.size != 0)
			{
				PyObject* MessageIdForComparison = UnicodeFromView(Diagnostic.message_id);
				if (MessageIdForComparison == nullptr)
				{
					Py_CLEAR(Message);
					PyErr_Clear();
				}
				else
				{
					const int IsUntranslatedKey = PyObject_RichCompareBool(
					    Message, MessageIdForComparison, Py_EQ);
					Py_DECREF(MessageIdForComparison);
					if (IsUntranslatedKey == 1)
					{
						Py_CLEAR(Message);
					}
					else if (IsUntranslatedKey < 0)
					{
						Py_CLEAR(Message);
						PyErr_Clear();
					}
				}
			}
			if (Message == nullptr)
			{
				PyErr_Clear();
				const bool RawSizeIsRepresentable = Diagnostic.message.size <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
				const bool RawIsKey = RawSizeIsRepresentable && Diagnostic.message.size == Diagnostic.message_id.size && Diagnostic.message.size != 0 && Diagnostic.message.data != nullptr && Diagnostic.message_id.data != nullptr && std::memcmp(Diagnostic.message.data, Diagnostic.message_id.data, static_cast<std::size_t>(Diagnostic.message.size)) == 0;
				if (Diagnostic.message.size != 0 && !RawIsKey)
				{
					Message = UnicodeFromView(Diagnostic.message);
				}
				else
				{
					PyObject* Key = UnicodeFromView(Diagnostic.message_id);
					Message = Key != nullptr
					    ? PyUnicode_FromFormat("Unlocalized VeroScript diagnostic (%U)", Key)
					    : PyUnicode_FromString("Unlocalized VeroScript diagnostic");
					Py_XDECREF(Key);
				}
			}
			PyObject* MessageId = UnicodeFromView(Diagnostic.message_id);
			PyObject* FileLabel = UnicodeFromView(Diagnostic.file_label);
			if (Message == nullptr || MessageId == nullptr || FileLabel == nullptr)
			{
				Py_XDECREF(Message);
				Py_XDECREF(MessageId);
				Py_XDECREF(FileLabel);
				Py_DECREF(List);
				return nullptr;
			}
			std::uint32_t Severity = VS_INTEROP_DIAGNOSTIC_SEVERITY_ERROR;
			const auto SeverityStatus =
			    vs_interop_session_get_diagnostic_severity(Self->Handle, Index, &Severity);
			if (SeverityStatus != VS_INTEROP_STATUS_OK)
			{
				Py_DECREF(Message);
				Py_DECREF(MessageId);
				Py_DECREF(FileLabel);
				Py_DECREF(List);
				return RaiseStatus(Self, SeverityStatus, "read diagnostic severity");
			}
			PyObject* Item = Py_BuildValue("{s:O,s:O,s:O,s:I,s:I,s:O,s:I}",
			    "message", Message, "message_id", MessageId, "file_label", FileLabel,
			    "line", Diagnostic.line, "column", Diagnostic.column,
			    "arguments", Arguments.Get(), "severity", Severity);
			Py_DECREF(Message);
			Py_DECREF(MessageId);
			Py_DECREF(FileLabel);
			if (Item == nullptr)
			{
				Py_DECREF(List);
				return nullptr;
			}
			PyList_SET_ITEM(List, static_cast<Py_ssize_t>(Index), Item);
		}
		return List;
	}

	PyObject* BuildRunResult(FPySession* Self, const vs_interop_run_result& Result)
	{
		PyObject* Diagnostics = BuildDiagnostics(Self);
		if (Diagnostics == nullptr)
		{
			return nullptr;
		}
		PyObject* Dictionary = Py_BuildValue("{s:O,s:O,s:i,s:O}",
		    "success", Result.success ? Py_True : Py_False,
		    "exit_requested", Result.exit_requested ? Py_True : Py_False,
		    "exit_code", Result.exit_code,
		    "diagnostics", Diagnostics);
		Py_DECREF(Diagnostics);
		return Dictionary;
	}

	PyObject* FinishRunOperation(FPySession* Self, const vs_interop_status Status,
	    const vs_interop_run_result& Result, const char* Operation)
	{
		if (RaiseCallbackError(Self))
		{
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK && Status != VS_INTEROP_STATUS_ENGINE_ERROR)
		{
			return RaiseStatus(Self, Status, Operation);
		}
		return BuildRunResult(Self, Result);
	}

	PyObject* SessionNew(PyTypeObject* Type, PyObject*, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		auto* Self = reinterpret_cast<FPySession*>(Type->tp_alloc(Type, 0));
		if (Self != nullptr)
		{
			Self->Handle = nullptr;
			Self->Callbacks = nullptr;
			Self->OwnerThread = 0;
		}
		return reinterpret_cast<PyObject*>(Self);
	}

	bool ParseNativeHandle(PyObject* Object, const char* Name, std::uintptr_t& Address)
	{
		Address = 0;
		if (Object == Py_None) return true;
		if (!PyLong_Check(Object) || PyBool_Check(Object))
		{
			PyErr_Format(PyExc_TypeError, "%s handle must be an integer or None", Name);
			return false;
		}
		const auto Number = PyLong_AsUnsignedLongLong(Object);
		if (PyErr_Occurred()) return false;
		if (Number > std::numeric_limits<std::uintptr_t>::max())
		{
			PyErr_Format(PyExc_OverflowError, "%s handle exceeds the pointer range", Name);
			return false;
		}
		Address = static_cast<std::uintptr_t>(Number);
		return true;
	}

	int SessionInit(FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		PyObject* ConfigObject = nullptr;
		PyObject* PrintCallback = Py_None;
		PyObject* DiagnosticCallback = Py_None;
		PyObject* ConsoleQueryCallback = Py_None;
		PyObject* ConsoleControlCallback = Py_None;
		PyObject* ConsoleWriteCallback = Py_None;
		PyObject* ModuleEnvironment = Py_None;
		static const char* Names[] = {
		    "config", "print_callback", "diagnostic_callback", "console_query", "console_control", "console_write", "module_environment", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "O|OOOOOO:Session",
		        const_cast<char**>(Names), &ConfigObject, &PrintCallback, &DiagnosticCallback, &ConsoleQueryCallback, &ConsoleControlCallback, &ConsoleWriteCallback, &ModuleEnvironment))
		{
			return -1;
		}
		if ((PrintCallback != Py_None && !PyCallable_Check(PrintCallback)) || (DiagnosticCallback != Py_None && !PyCallable_Check(DiagnosticCallback)) || (ConsoleQueryCallback != Py_None && !PyCallable_Check(ConsoleQueryCallback)) || (ConsoleControlCallback != Py_None && !PyCallable_Check(ConsoleControlCallback)) || (ConsoleWriteCallback != Py_None && !PyCallable_Check(ConsoleWriteCallback)))
		{
			PyErr_SetString(PyExc_TypeError, "session callbacks must be callable or None");
			return -1;
		}
		if (Self->Handle != nullptr || Self->OwnerThread != 0)
		{
			PyErr_SetString(GError, "VeroScript Session cannot be reinitialized");
			return -1;
		}

		vs_interop_config Config{};
		FConfigStorage Storage;
		if (!BuildConfig(ConfigObject, Config, Storage))
		{
			return -1;
		}
		if (Self->Handle != nullptr || Self->OwnerThread != 0)
		{
			PyErr_SetString(GError, "VeroScript Session cannot be reinitialized");
			return -1;
		}
		std::uintptr_t EnvironmentAddress = 0;
		if (!ParseNativeHandle(ModuleEnvironment, "module environment", EnvironmentAddress)) return -1;
		auto* State = new (std::nothrow) FCallbackState;
		if (State == nullptr)
		{
			PyErr_NoMemory();
			return -1;
		}
		if (PrintCallback != Py_None)
		{
			Py_INCREF(PrintCallback);
			State->PrintCallback = PrintCallback;
		}
		if (DiagnosticCallback != Py_None)
		{
			Py_INCREF(DiagnosticCallback);
			State->DiagnosticCallback = DiagnosticCallback;
		}

		vs_interop_session_options Options{};
		Options.struct_size = sizeof(Options);
		Options.print_callback = State->PrintCallback != nullptr ? &OnPrint : nullptr;
		Options.diagnostic_callback = State->DiagnosticCallback != nullptr ? &OnDiagnostic : nullptr;
		if (ConsoleQueryCallback != Py_None)
		{
			Py_INCREF(ConsoleQueryCallback);
			State->ConsoleQueryCallback = ConsoleQueryCallback;
		}
		if (ConsoleControlCallback != Py_None)
		{
			Py_INCREF(ConsoleControlCallback);
			State->ConsoleControlCallback = ConsoleControlCallback;
		}
		if (ConsoleWriteCallback != Py_None)
		{
			Py_INCREF(ConsoleWriteCallback);
			State->ConsoleWriteCallback = ConsoleWriteCallback;
		}
		Options.console_query_callback = State->ConsoleQueryCallback ? &OnConsoleQuery : nullptr;
		Options.console_control_callback = State->ConsoleControlCallback ? &OnConsoleControl : nullptr;
		Options.console_write_callback = State->ConsoleWriteCallback ? &OnConsoleWrite : nullptr;
		Options.user_data = State;
		vs_interop_status Status = vs_interop_session_create(&Options, &Self->Handle);
		if (Status == VS_INTEROP_STATUS_OK)
		{
			Self->Callbacks = State;
			Self->OwnerThread = PyThread_get_thread_ident();
			const FCallbackActivity Activity(Self->Callbacks);
			Py_BEGIN_ALLOW_THREADS
			    Status = vs_interop_session_apply_config_with_environment(Self->Handle, &Config,
			        reinterpret_cast<const vs_interop_module_environment*>(EnvironmentAddress));
			Py_END_ALLOW_THREADS
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(Self, Status, "create session");
			if (Self->Handle != nullptr)
			{
				(void)vs_interop_session_destroy(Self->Handle);
			}
			Self->Handle = nullptr;
			Self->Callbacks = nullptr;
			Py_XDECREF(State->PrintCallback);
			Py_XDECREF(State->DiagnosticCallback);
			Py_XDECREF(State->ConsoleQueryCallback);
			Py_XDECREF(State->ConsoleControlCallback);
			Py_XDECREF(State->ConsoleWriteCallback);
			delete State;
			return -1;
		}
		return 0;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return -1;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while creating VeroScript session");
		return -1;
	}

	int SessionTraverse(FPySession* Self, visitproc visit, void* arg)
	{
		if (Self->Callbacks != nullptr)
		{
			Py_VISIT(Self->Callbacks->PrintCallback);
			Py_VISIT(Self->Callbacks->DiagnosticCallback);
			Py_VISIT(Self->Callbacks->ConsoleQueryCallback);
			Py_VISIT(Self->Callbacks->ConsoleControlCallback);
			Py_VISIT(Self->Callbacks->ConsoleWriteCallback);
			Py_VISIT(Self->Callbacks->InstanceInterceptor);
			Py_VISIT(Self->Callbacks->MutationCallback);
			Py_VISIT(Self->Callbacks->CallbackError);
			for (const FHostFunctionState* Function : Self->Callbacks->HostFunctions)
			{
				Py_VISIT(Function->Callable);
			}
			for (const FHostTypeState* Type : Self->Callbacks->HostTypes)
			{
				Py_VISIT(Type->PythonType);
				Py_VISIT(Type->ProtocolCallbacks.Object);
			}
			for (const FHostMemberState* Member : Self->Callbacks->HostMembers)
			{
				Py_VISIT(Member->Callable);
			}
			for (const FHostProtocolCallbackState* Protocol :
			    Self->Callbacks->HostProtocolCallbacks)
			{
				Py_VISIT(Protocol->Callable);
			}
			for (const FGenericHostTypeState* Generic : Self->Callbacks->GenericHostTypes)
			{
				Py_VISIT(Generic->Factory);
			}
			for (const FSerializerState* Serializer : Self->Callbacks->Serializers)
			{
				Py_VISIT(Serializer->Serialize);
				Py_VISIT(Serializer->Deserialize);
			}
		}
		return 0;
	}

	int SessionClearReferences(FPySession* Self)
	{
		const FCallbackActivity Activity(Self->Callbacks);
		if (Self->Callbacks != nullptr)
		{
			Py_CLEAR(Self->Callbacks->PrintCallback);
			Py_CLEAR(Self->Callbacks->DiagnosticCallback);
			Py_CLEAR(Self->Callbacks->ConsoleQueryCallback);
			Py_CLEAR(Self->Callbacks->ConsoleControlCallback);
			Py_CLEAR(Self->Callbacks->ConsoleWriteCallback);
			Py_CLEAR(Self->Callbacks->InstanceInterceptor);
			Py_CLEAR(Self->Callbacks->MutationCallback);
			Py_CLEAR(Self->Callbacks->CallbackError);
			for (FHostFunctionState* Function : Self->Callbacks->HostFunctions)
			{
				Py_CLEAR(Function->Callable);
			}
			for (FHostTypeState* Type : Self->Callbacks->HostTypes)
			{
				Py_CLEAR(Type->PythonType);
				Py_CLEAR(Type->ProtocolCallbacks.Object);
			}
			for (FHostMemberState* Member : Self->Callbacks->HostMembers)
			{
				Py_CLEAR(Member->Callable);
			}
			for (FHostProtocolCallbackState* Protocol :
			    Self->Callbacks->HostProtocolCallbacks)
			{
				Py_CLEAR(Protocol->Callable);
			}
			for (FGenericHostTypeState* Generic : Self->Callbacks->GenericHostTypes)
			{
				Py_CLEAR(Generic->Factory);
			}
			for (FSerializerState* Serializer : Self->Callbacks->Serializers)
			{
				Py_CLEAR(Serializer->Serialize);
				Py_CLEAR(Serializer->Deserialize);
			}
		}
		return 0;
	}

	void DeleteCallbackState(FCallbackState* State)
	{
		if (State == nullptr)
		{
			return;
		}
		if (--State->Owners != 0) return;
		Py_XDECREF(State->PrintCallback);
		Py_XDECREF(State->DiagnosticCallback);
		Py_XDECREF(State->ConsoleQueryCallback);
		Py_XDECREF(State->ConsoleControlCallback);
		Py_XDECREF(State->ConsoleWriteCallback);
		Py_XDECREF(State->InstanceInterceptor);
		Py_XDECREF(State->MutationCallback);
		Py_XDECREF(State->CallbackError);
		for (FHostFunctionState* Function : State->HostFunctions)
		{
			delete Function;
		}
		for (FHostMemberState* Member : State->HostMembers)
		{
			delete Member;
		}
		for (FHostProtocolCallbackState* Protocol : State->HostProtocolCallbacks)
		{
			delete Protocol;
		}
		for (FGenericHostTypeState* Generic : State->GenericHostTypes)
		{
			delete Generic;
		}
		for (FHostTypeState* Type : State->HostTypes)
		{
			delete Type;
		}
		for (FSerializerState* Serializer : State->Serializers)
		{
			delete Serializer;
		}
		delete State;
	}

	PyObject* SessionClose(FPySession* Self, PyObject*);

	void SessionFinalize(FPySession* Self)
	{
		if (Self->Handle == nullptr || Self->OwnerThread != PyThread_get_thread_ident() || (Self->Callbacks && Self->Callbacks->ActiveOperations.load(std::memory_order_relaxed) != 0))
		{
			return;
		}
		PyObject* ErrorType = nullptr;
		PyObject* ErrorValue = nullptr;
		PyObject* ErrorTraceback = nullptr;
		PyErr_Fetch(&ErrorType, &ErrorValue, &ErrorTraceback);
		// Release native ownership before GC clears a callback cycle. A graph
		// retained by escaped protocol values then becomes an external GC root.
		PyObject* Result = SessionClose(Self, nullptr);
		if (Result == nullptr)
		{
			PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(Self));
		}

		else
		{
			Py_DECREF(Result);
		}

		PyErr_Restore(ErrorType, ErrorValue, ErrorTraceback);
	}

	void SessionDealloc(FPySession* Self)
	{
		if (PyObject_CallFinalizerFromDealloc(reinterpret_cast<PyObject*>(Self)) < 0) return;
		PyObject_GC_UnTrack(Self);
		auto* Handle = std::exchange(Self->Handle, nullptr);
		bool Destroyed = Handle == nullptr;
		if (Handle != nullptr && Self->OwnerThread == PyThread_get_thread_ident())
		{
			vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
			Py_BEGIN_ALLOW_THREADS
			    Status = vs_interop_session_destroy(Handle);
			Py_END_ALLOW_THREADS
			    Destroyed = Status == VS_INTEROP_STATUS_OK;
		}
		else if (Handle != nullptr)
		{
			(void)PyErr_WarnEx(PyExc_ResourceWarning,
			    "VeroScript Session finalized on a foreign thread; native session was retained for safety", 1);
			PyErr_Clear();
		}
		if (Destroyed)
		{
			DeleteCallbackState(std::exchange(Self->Callbacks, nullptr));
		}
		// If destruction was rejected, the runtime still owns user_data. Leaking the
		// small callback state is deliberate and prevents a use-after-free.
		Self->Callbacks = nullptr;
		Py_TYPE(Self)->tp_free(reinterpret_cast<PyObject*>(Self));
	}

	PyObject* SessionClose(FPySession* Self, PyObject*)
	{
		if (Self->Handle == nullptr)
		{
			Py_RETURN_NONE;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		if (Self->Callbacks != nullptr && Self->Callbacks->ActiveOperations.load(std::memory_order_relaxed) != 0)
		{
			PyErr_SetString(GError, "VeroScript session cannot be closed during an active operation or callback");
			return nullptr;
		}
		const FCallbackErrorScope ErrorScope(Self);
		if (Self->Handle == nullptr)
		{
			Py_RETURN_NONE;
		}
		// Native destruction can synchronously release Python host objects. Their
		// finalizers must observe closure and cannot enter this handle a second time.
		auto* Handle = std::exchange(Self->Handle, nullptr);
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_destroy(Handle);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			Self->Handle = Handle;
			return RaiseStatus(Self, Status, "close session");
		}
		if (RaiseCallbackError(Self))
		{
			DeleteCallbackState(std::exchange(Self->Callbacks, nullptr));
			return nullptr;
		}
		// Callable finalizers can run GC while this object is still tracked. Hide
		// the callback graph before deleting its entries so traversal cannot see
		// a partially destroyed vector of native callback states.
		DeleteCallbackState(std::exchange(Self->Callbacks, nullptr));
		Py_RETURN_NONE;
	}

	PyObject* SessionGetClosed(FPySession* Self, void*)
	{
		return PyBool_FromLong(Self->Handle == nullptr);
	}

	PyObject* SessionGetIsLoaded(FPySession* Self, void*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		return PyBool_FromLong(vs_interop_session_is_loaded(Self->Handle) != 0);
	}

	PyObject* SessionLoadSource(FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		const char* Source = nullptr;
		Py_ssize_t SourceSize = 0;
		const char* Label = nullptr;
		Py_ssize_t LabelSize = 0;
		static const char* Names[] = {"source", "file_label", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#|s#:load_source",
		        const_cast<char**>(Names), &Source, &SourceSize, &Label, &LabelSize))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		if (Label == nullptr)
		{
			Label = "<python>";
			LabelSize = 8;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_load_source(
		        Self->Handle, View(Source, SourceSize), View(Label, LabelSize), &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "load source");
	}

	PyObject* SessionLoadFile(FPySession* Self, PyObject* Args)
	{
		const char* Path = nullptr;
		Py_ssize_t PathSize = 0;
		if (!PyArg_ParseTuple(Args, "s#:load_file", &Path, &PathSize))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_load_file(Self->Handle, View(Path, PathSize), &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "load file");
	}

	PyObject* SessionLoadApplication(FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		const char* Path = nullptr;
		Py_ssize_t PathSize = 0;
		const char* Options = "{}";
		Py_ssize_t OptionsSize = 2;
		static const char* Names[] = {"absolute_path", "options_json", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#|s#:load_application", const_cast<char**>(Names), &Path, &PathSize, &Options, &OptionsSize) || !CheckSession(Self)) return nullptr;
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_application_services* Services = nullptr;
		vs_interop_buffer* Error = nullptr;
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_application_services_create(View(Options, OptionsSize), nullptr, &Services, &Error);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_application_services_destroy(Services);
			return RaiseBufferError(Status, Error, "cannot configure application services");
		}
		vs_interop_buffer_destroy(Error);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_load_application(Self->Handle, View(Path, PathSize), Services, &Result);
		vs_interop_application_services_destroy(Services);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "load application");
	}
	PyObject* SessionLoadPackage(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		PyObject* PackageObject = nullptr;
		const char* LogicalPath = "";
		Py_ssize_t LogicalPathSize = 0;
		static const char* Names[] = {"package", "logical_path", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "O!|s#:load_package",
		        const_cast<char**>(Names), &GPackageType, &PackageObject,
		        &LogicalPath, &LogicalPathSize))
		{
			return nullptr;
		}
		if (PackageObject == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "load_package parsed no package object");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_load_package(Self->Handle,
		        reinterpret_cast<FPyPackage*>(PackageObject)->Handle,
		        View(LogicalPath, LogicalPathSize), &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "load package");
	}

	PyObject* SessionReloadPackage(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		PyObject* PackageObject = nullptr;
		const char* LogicalPath = "";
		Py_ssize_t LogicalPathSize = 0;
		static const char* Names[] = {"package", "logical_path", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "O!|s#:reload_package",
		        const_cast<char**>(Names), &GPackageType, &PackageObject,
		        &LogicalPath, &LogicalPathSize))
		{
			return nullptr;
		}
		if (PackageObject == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "reload_package parsed no package object");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_reload_package(Self->Handle,
		        reinterpret_cast<FPyPackage*>(PackageObject)->Handle,
		        View(LogicalPath, LogicalPathSize), &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "reload package");
	}

	PyObject* SessionBuildPackageFile(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* SourcePath = nullptr;
		Py_ssize_t SourcePathSize = 0;
		unsigned int Kind = 0;
		const char* PackageName = nullptr;
		Py_ssize_t PackageNameSize = 0;
		unsigned long long PackageVersion = 0;
		PyObject* MetadataObject = Py_None;
		int StripDebugInfo = 0;
		static const char* Names[] = {"source_path", "kind", "package_name",
		    "package_version", "metadata", "strip_debug_info", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#Is#K|Op:build_package_file",
		        const_cast<char**>(Names), &SourcePath, &SourcePathSize, &Kind,
		        &PackageName, &PackageNameSize, &PackageVersion,
		        &MetadataObject, &StripDebugInfo) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		if (Kind != VS_INTEROP_PACKAGE_APPLICATION && Kind != VS_INTEROP_PACKAGE_LIBRARY)
		{
			PyErr_SetString(PyExc_ValueError, "package kind must be APPLICATION or LIBRARY");
			return nullptr;
		}

		std::vector<std::string> MetadataKeys;
		std::vector<std::string> MetadataValues;
		if (MetadataObject != Py_None)
		{
			FPyOwnedRef Items(CheckedMappingItems(MetadataObject));
			if (Items.Get() == nullptr)
			{
				return nullptr;
			}
			const Py_ssize_t Count = PyList_GET_SIZE(Items.Get());
			MetadataKeys.reserve(static_cast<std::size_t>(Count));
			MetadataValues.reserve(static_cast<std::size_t>(Count));
			for (Py_ssize_t Index = 0; Index < Count; ++Index)
			{
				PyObject* Item = PyList_GET_ITEM(Items.Get(), Index);
				PyObject* KeyObject = PyTuple_GET_ITEM(Item, 0);
				PyObject* ValueObject = PyTuple_GET_ITEM(Item, 1);
				Py_ssize_t KeySize = 0;
				const char* Key = PyUnicode_AsUTF8AndSize(KeyObject, &KeySize);
				char* Value = nullptr;
				Py_ssize_t ValueSize = 0;
				if (Key == nullptr || PyBytes_AsStringAndSize(ValueObject, &Value, &ValueSize) != 0)
				{
					if (Key != nullptr && !PyErr_Occurred())
					{
						PyErr_SetString(PyExc_TypeError,
						    "package metadata values must be bytes");
					}
					return nullptr;
				}
				MetadataKeys.emplace_back(Key, static_cast<std::size_t>(KeySize));
				MetadataValues.emplace_back(Value, static_cast<std::size_t>(ValueSize));
			}
		}
		std::vector<vs_interop_package_metadata> Metadata(MetadataKeys.size());
		for (std::size_t Index = 0; Index < Metadata.size(); ++Index)
		{
			Metadata[Index].key = View(MetadataKeys[Index]);
			Metadata[Index].value = {
			    reinterpret_cast<const std::uint8_t*>(MetadataValues[Index].data()),
			    static_cast<std::uint64_t>(MetadataValues[Index].size())};
		}
		vs_interop_package_build_options Options{};
		Options.struct_size = sizeof(Options);
		Options.kind = Kind;
		Options.package_name = View(PackageName, PackageNameSize);
		Options.package_version = PackageVersion;
		Options.host_metadata = Metadata.data();
		Options.host_metadata_count = static_cast<std::uint64_t>(Metadata.size());
		Options.strip_debug_info = static_cast<std::uint8_t>(StripDebugInfo);
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_buffer* PackageBytes = nullptr;
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_build_package_file(Self->Handle,
		        View(SourcePath, SourcePathSize), &Options, &PackageBytes, &Result);
		Py_END_ALLOW_THREADS if (RaiseCallbackError(Self))
		{
			vs_interop_buffer_destroy(PackageBytes);
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK && Status != VS_INTEROP_STATUS_ENGINE_ERROR)
		{
			vs_interop_buffer_destroy(PackageBytes);
			return RaiseStatus(Self, Status, "build package file");
		}
		PyObject* RunResult = BuildRunResult(Self, Result);
		if (RunResult == nullptr)
		{
			vs_interop_buffer_destroy(PackageBytes);
			return nullptr;
		}
		PyObject* Bytes = nullptr;
		if (PackageBytes != nullptr)
		{
			const vs_interop_byte_view View = vs_interop_buffer_view(PackageBytes);
			if (View.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(PyExc_OverflowError, "package output is too large for Python");
			}
			else if (!IsValidByteView(View))
			{
				PyErr_SetString(GError, "package builder returned an invalid byte view");
			}
			else
			{
				Bytes = PyBytes_FromStringAndSize(
				    ByteData(View),
				    static_cast<Py_ssize_t>(View.size));
			}
		}
		else
		{
			Bytes = PyBytes_FromStringAndSize("", 0);
		}
		vs_interop_buffer_destroy(PackageBytes);
		if (Bytes == nullptr)
		{
			Py_DECREF(RunResult);
			return nullptr;
		}
		PyObject* Output = PyTuple_Pack(2, Bytes, RunResult);
		Py_DECREF(Bytes);
		Py_DECREF(RunResult);
		return Output;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected package build error");
		return nullptr;
	}

	PyObject* SessionRun(FPySession* Self, PyObject* Args)
	{
		int Mode = VS_INTEROP_RUN_SCRIPT;
		if (!PyArg_ParseTuple(Args, "|i:run", &Mode))
		{
			return nullptr;
		}
		if (Mode != VS_INTEROP_RUN_EVAL && Mode != VS_INTEROP_RUN_SCRIPT && Mode != VS_INTEROP_RUN_EVAL_ONCE)
		{
			PyErr_SetString(PyExc_ValueError, "run mode must be EVAL, SCRIPT, or EVAL_ONCE");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_run(Self->Handle,
		        static_cast<vs_interop_run_mode>(Mode), &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "run session");
	}

	PyObject* SessionClear(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_clear(Self->Handle);
		Py_END_ALLOW_THREADS if (RaiseCallbackError(Self)) return nullptr;
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "clear session");
		Py_RETURN_NONE;
	}

	PyObject* SessionRequestCancellation(FPySession* Self, PyObject*)
	{
		// Cancellation is the sole Session operation explicitly safe from another
		// Python thread. The caller retains the Session reference, so its native
		// handle remains alive for the duration of this call.
		if (Self->Handle == nullptr)
		{
			PyErr_SetString(GError, "VeroScript session is closed");
			return nullptr;
		}
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		const FCallbackActivity Activity(Self->Callbacks);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_request_cancel(Self->Handle);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "request cancellation");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionDiagnostics(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		return BuildDiagnostics(Self);
	}

	PyObject* SessionSetInput(FPySession* Self, PyObject* Args)
	{
		const char* Text = nullptr;
		Py_ssize_t Size = 0;
		if (!PyArg_ParseTuple(Args, "s#:set_input", &Text, &Size))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const vs_interop_status Status = vs_interop_session_set_input(Self->Handle, View(Text, Size));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set input");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionSetHotReloadEnabled(FPySession* Self, PyObject* Args)
	{
		int Enabled = 0;
		if (!PyArg_ParseTuple(Args, "p:set_hot_reload_enabled", &Enabled) || !CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_set_hot_reload_enabled(
		        Self->Handle, static_cast<std::uint8_t>(Enabled));
		Py_END_ALLOW_THREADS if (RaiseCallbackError(Self)) return nullptr;
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set hot reload state");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionGetHotReloadEnabled(FPySession* Self, void*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		return PyBool_FromLong(vs_interop_session_is_hot_reload_enabled(Self->Handle) != 0);
	}

	PyObject* SessionGetReloadStatus(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self)) return nullptr;
		vs_interop_reload_status Reload{};
		Reload.struct_size = sizeof(Reload);
		const auto Status = vs_interop_session_get_reload_status(Self->Handle, &Reload);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "get reload status");
		return Py_BuildValue("{s:I,s:K,s:K,s:K,s:K,s:K}",
		    "state", static_cast<unsigned int>(Reload.state),
		    "operation_id", static_cast<unsigned long long>(Reload.operation_id),
		    "previous_generation", static_cast<unsigned long long>(Reload.previous_generation),
		    "candidate_generation", static_cast<unsigned long long>(Reload.candidate_generation),
		    "current_generation", static_cast<unsigned long long>(Reload.current_generation),
		    "committed_generation", static_cast<unsigned long long>(Reload.committed_generation));
	}

	PyObject* SessionReload(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_reload(Self->Handle, &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "reload session");
	}

	PyObject* SessionSetAnnotationService(FPySession* Self, PyObject* Arg)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		std::uintptr_t Address = 0;
		if (Arg != Py_None)
		{
			if (!PyLong_Check(Arg) || PyBool_Check(Arg))
			{
				PyErr_SetString(PyExc_TypeError, "annotation service handle must be an integer or None");
				return nullptr;
			}
			const auto Number = PyLong_AsUnsignedLongLong(Arg);
			if (PyErr_Occurred())
			{
				return nullptr;
			}
			if (Number > std::numeric_limits<std::uintptr_t>::max())
			{
				PyErr_SetString(PyExc_OverflowError, "annotation service handle exceeds the pointer range");
				return nullptr;
			}
			Address = static_cast<std::uintptr_t>(Number);
		}
		const auto Status = vs_interop_session_set_annotation_service(
		    Self->Handle, reinterpret_cast<const vs_interop_annotation_service*>(Address));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set annotation processing service");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionSetCompileProcessingService(FPySession* Self, PyObject* Arg)
	{
		if (!CheckSession(Self)) return nullptr;
		std::uintptr_t Address = 0;
		if (!ParseNativeHandle(Arg, "compile processing service", Address)) return nullptr;
		const FCallbackActivity Activity(Self->Callbacks);
		const auto Status = vs_interop_session_set_compile_processing_service(
		    Self->Handle, reinterpret_cast<const vs_interop_compile_processing_service*>(Address));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set compile processing service");
		}

		Py_RETURN_NONE;
	}

	PyObject* SessionSetScriptCompileOptions(FPySession* Self, PyObject* Args)
	try
	{
		if (!CheckSession(Self)) return nullptr;
		if (Self->Callbacks->ActiveOperations.load(std::memory_order_relaxed) != 0)
		{
			PyErr_SetString(GError, "cannot configure compile extensions during an active session operation");
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		PyObject *Roots = nullptr, *Configuration = nullptr, *AllowedRoots = nullptr;
		PyObject *InstructionObject = nullptr, *TimeoutObject = nullptr;
		int AllowImports = 1;
		if (!PyArg_ParseTuple(Args, "OOOpOO", &Roots, &Configuration, &AllowedRoots, &AllowImports, &InstructionObject, &TimeoutObject)) return nullptr;
		if (!PyLong_Check(InstructionObject) || PyBool_Check(InstructionObject) || !PyLong_Check(TimeoutObject) || PyBool_Check(TimeoutObject))
		{
			PyErr_SetString(PyExc_TypeError, "compile extension budgets must be integers");
			return nullptr;
		}
		const auto Instructions = PyLong_AsUnsignedLongLong(InstructionObject);
		if (PyErr_Occurred()) return nullptr;
		const auto Timeout = PyLong_AsUnsignedLongLong(TimeoutObject);
		if (PyErr_Occurred()) return nullptr;
		if (!PyList_Check(Roots) || !PyDict_Check(Configuration) || !PyList_Check(AllowedRoots))
		{
			PyErr_SetString(PyExc_TypeError,
			    "compile options require root/allowed-root lists and a configuration dictionary");
			return nullptr;
		}
		if (PyList_Size(Roots) > 64 || PyDict_Size(Configuration) > 4096 || PyList_Size(AllowedRoots) > 4096)
		{
			PyErr_SetString(PyExc_ValueError, "compile extension configuration exceeds entry limits");
			return nullptr;
		}
		auto String = [](PyObject* Object, vs_interop_string_view& Result)
		{
			Py_ssize_t Size = 0;
			const char* Data = PyUnicode_AsUTF8AndSize(Object, &Size);
			if (!Data) return false;
			Result = {Data, static_cast<std::uint64_t>(Size)};
			return true;
		};
		std::vector<vs_interop_compile_extension_root> RootValues;
		for (Py_ssize_t I = 0; I < PyList_Size(Roots); ++I)
		{
			auto Value = PyList_GetItem(Roots, I);
			if (!PyTuple_Check(Value) || PyTuple_Size(Value) != 2)
			{
				PyErr_SetString(PyExc_TypeError, "compile root must be a (path, registration_entry) tuple");
				return nullptr;
			}
			vs_interop_compile_extension_root Root{};
			if (!String(PyTuple_GetItem(Value, 0), Root.path) ||
			    !String(PyTuple_GetItem(Value, 1), Root.registration_entry))
			{
				return nullptr;
			}
			RootValues.push_back(Root);
		}
		std::vector<vs_interop_compile_configuration> ConfigValues;
		Py_ssize_t Position = 0;
		PyObject *Key = nullptr, *Value = nullptr;
		while (PyDict_Next(Configuration, &Position, &Key, &Value))
		{
			vs_interop_compile_configuration Item{};
			if (!String(Key, Item.key) || !String(Value, Item.value)) return nullptr;
			ConfigValues.push_back(Item);
		}
		std::vector<vs_interop_string_view> AllowedValues;
		for (Py_ssize_t I = 0; I < PyList_Size(AllowedRoots); ++I)
		{
			vs_interop_string_view Allowed{};
			if (!String(PyList_GetItem(AllowedRoots, I), Allowed)) return nullptr;
			AllowedValues.push_back(Allowed);
		}
		vs_interop_script_compile_options Options{};
		Options.struct_size = sizeof(Options);
		Options.allow_source_imports = AllowImports ? 1 : 0;
		Options.roots = RootValues.data();
		Options.root_count = RootValues.size();
		Options.configuration = ConfigValues.data();
		Options.configuration_count = ConfigValues.size();
		Options.max_instructions = Instructions;
		Options.timeout_milliseconds = Timeout;
		Options.allowed_roots = AllowedValues.data();
		Options.allowed_root_count = AllowedValues.size();
		const auto Status = vs_interop_session_set_script_compile_options(Self->Handle, &Options);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "set script compile extensions");
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (const std::exception& Exception)
	{
		PyErr_SetString(PyExc_RuntimeError, Exception.what());
		return nullptr;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected compile extension configuration error");
		return nullptr;
	}
	PyObject* SessionSetVmBackend(FPySession* Self, PyObject* Arg)
	{
		if (!CheckSession(Self)) return nullptr;
		Py_ssize_t Size = 0;
		const char* Path = Arg == Py_None ? "" : PyUnicode_AsUTF8AndSize(Arg, &Size);
		if (!Path) return nullptr;
		const auto Status = vs_interop_session_set_vm_backend(Self->Handle, View(Path, Size));
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "set VM backend");
		Py_RETURN_NONE;
	}

	PyObject* SessionGetVmBackendInfo(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self)) return nullptr;
		vs_interop_vm_backend_info Info{};
		Info.struct_size = sizeof(Info);
		const auto Status = vs_interop_session_get_vm_backend_info(Self->Handle, &Info);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "get VM backend information");
		return Py_BuildValue("{s:s#,s:s#,s:s#,s:s#,s:K,s:O,s:O}",
		    "id", Info.id.data ? Info.id.data : "", static_cast<Py_ssize_t>(Info.id.size),
		    "version", Info.version.data ? Info.version.data : "", static_cast<Py_ssize_t>(Info.version.size),
		    "target", Info.target.data ? Info.target.data : "", static_cast<Py_ssize_t>(Info.target.size),
		    "library_path", Info.library_path.data ? Info.library_path.data : "", static_cast<Py_ssize_t>(Info.library_path.size),
		    "capabilities", static_cast<unsigned long long>(Info.capabilities),
		    "selected", (Info.flags & VS_INTEROP_VM_SELECTED) ? Py_True : Py_False,
		    "frozen", (Info.flags & VS_INTEROP_VM_FROZEN) ? Py_True : Py_False);
	}
	PyObject* SessionSetJitBackend(FPySession* Self, PyObject* Arg)
	{
		if (!CheckSession(Self)) return nullptr;
		Py_ssize_t Size = 0;
		const char* Path = Arg == Py_None ? "" : PyUnicode_AsUTF8AndSize(Arg, &Size);
		if (!Path) return nullptr;
		const auto Status = vs_interop_session_set_jit_backend(Self->Handle, View(Path, Size));
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "set JIT backend");
		Py_RETURN_NONE;
	}

	PyObject* SessionGetJitBackend(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self)) return nullptr;
		vs_interop_jit_backend Backend = VS_INTEROP_JIT_BACKEND_NONE;
		const auto Status = vs_interop_session_get_jit_backend(Self->Handle, &Backend);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "get JIT backend");
		return PyLong_FromUnsignedLong(Backend);
	}

	PyObject* SessionGetJitBackendInfo(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self)) return nullptr;
		vs_interop_jit_backend_info Info{};
		Info.struct_size = sizeof(Info);
		const auto Status = vs_interop_session_get_jit_backend_info(Self->Handle, &Info);
		if (Status != VS_INTEROP_STATUS_OK) return RaiseStatus(Self, Status, "get JIT backend information");
		return Py_BuildValue("{s:s#,s:s#,s:K,s:O}",
		    "id", Info.id.data, static_cast<Py_ssize_t>(Info.id.size),
		    "target", Info.target.data, static_cast<Py_ssize_t>(Info.target.size),
		    "capabilities", static_cast<unsigned long long>(Info.capabilities),
		    "native_code", Info.native_code ? Py_True : Py_False);
	}

	PyObject* SessionSetJitPolicy(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		unsigned int HotCallThreshold = 0;
		unsigned long long MaxCompileAttempts = 0;
		unsigned long long MaxCachedEntries = 0;
		unsigned long long EagerCompileBudget = 0;
		int ReuseSessionCode = 1;
		int EnableControlledMixed = 0;
		static const char* Names[] = {"mixed_hot_call_threshold", "max_compile_attempts",
		    "max_cached_entries", "eager_compile_budget", "reuse_session_code", "enable_controlled_mixed", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "IKKK|pp:set_jit_policy",
		        const_cast<char**>(Names), &HotCallThreshold, &MaxCompileAttempts,
		        &MaxCachedEntries, &EagerCompileBudget, &ReuseSessionCode, &EnableControlledMixed) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_jit_policy Policy{};
		Policy.struct_size = sizeof(Policy);
		Policy.mixed_hot_call_threshold = HotCallThreshold;
		Policy.max_compile_attempts = MaxCompileAttempts;
		Policy.max_cached_entries = MaxCachedEntries;
		Policy.eager_compile_budget = EagerCompileBudget;
		Policy.reuse_session_code = ReuseSessionCode ? 1 : 0;
		Policy.enable_controlled_mixed = EnableControlledMixed ? 1 : 0;
		const vs_interop_status Status =
		    vs_interop_session_set_jit_policy(Self->Handle, &Policy);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set JIT policy");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionSetJitCacheLimits(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		unsigned long long MaxNativeBytes = 0;
		unsigned long long MaxRetainedPayloadBytes = 0;
		static const char* Names[] = {
		    "max_native_bytes", "max_retained_payload_bytes", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "KK:set_jit_cache_limits",
		        const_cast<char**>(Names), &MaxNativeBytes, &MaxRetainedPayloadBytes) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_jit_cache_limits Limits{};
		Limits.struct_size = sizeof(Limits);
		Limits.max_native_bytes = MaxNativeBytes;
		Limits.max_retained_payload_bytes = MaxRetainedPayloadBytes;
		const vs_interop_status Status =
		    vs_interop_session_set_jit_cache_limits(Self->Handle, &Limits);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set JIT cache limits");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionGetJitCacheLimits(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_jit_cache_limits Limits{};
		Limits.struct_size = sizeof(Limits);
		const vs_interop_status Status =
		    vs_interop_session_get_jit_cache_limits(Self->Handle, &Limits);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "get JIT cache limits");
		}
		return Py_BuildValue("{sK,sK}",
		    "max_native_bytes", static_cast<unsigned long long>(Limits.max_native_bytes),
		    "max_retained_payload_bytes",
		    static_cast<unsigned long long>(Limits.max_retained_payload_bytes));
	}

	PyObject* SessionGetJitCacheStats(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_jit_cache_stats Stats{};
		Stats.struct_size = sizeof(Stats);
		const vs_interop_status Status =
		    vs_interop_session_get_jit_cache_stats(Self->Handle, &Stats);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "get JIT cache statistics");
		}
		return Py_BuildValue("{sK,sK,sK,sO}",
		    "compile_attempts", static_cast<unsigned long long>(Stats.compile_attempts),
		    "retained_payload_bytes",
		    static_cast<unsigned long long>(Stats.retained_payload_bytes),
		    "native_bytes", static_cast<unsigned long long>(Stats.native_bytes),
		    "resource_limit_reached", Stats.resource_limit_reached ? Py_True : Py_False);
	}

	PyObject* SessionGetJitStats(FPySession* Self, PyObject*)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_jit_stats Stats{};
		Stats.struct_size = sizeof(Stats);
		const vs_interop_status Status =
		    vs_interop_session_get_last_jit_stats(Self->Handle, &Stats);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "get JIT statistics");
		}
		PyObject* Result = PyDict_New();
		if (Result == nullptr)
		{
			return nullptr;
		}
		const auto Add = [Result](const char* Name, const std::uint64_t Value) -> bool
		{
			PyObject* Number = PyLong_FromUnsignedLongLong(Value);
			if (Number == nullptr)
			{
				return false;
			}
			const int Added = PyDict_SetItemString(Result, Name, Number);
			Py_DECREF(Number);
			return Added == 0;
		};
		#define VS_PY_ADD_JIT_STAT(Name) \
	if (!Add(#Name, Stats.Name)) \
	{ \
		Py_DECREF(Result); \
		return nullptr; \
	}
		VS_PY_ADD_JIT_STAT(compile_attempts)
		VS_PY_ADD_JIT_STAT(cache_hits)
		VS_PY_ADD_JIT_STAT(compiled_entries)
		VS_PY_ADD_JIT_STAT(native_leaf_entries)
		VS_PY_ADD_JIT_STAT(native_cfg_entries)
		VS_PY_ADD_JIT_STAT(bridge_trampoline_entries)
		VS_PY_ADD_JIT_STAT(linear_trampoline_entries)
		VS_PY_ADD_JIT_STAT(vm_trampoline_entries)
		VS_PY_ADD_JIT_STAT(unsupported_entries)
		VS_PY_ADD_JIT_STAT(dispatch_attempts)
		VS_PY_ADD_JIT_STAT(dispatches)
		VS_PY_ADD_JIT_STAT(fallbacks)
		VS_PY_ADD_JIT_STAT(failures)
		VS_PY_ADD_JIT_STAT(cache_invalidations)
		VS_PY_ADD_JIT_STAT(hotness_deferrals)
		VS_PY_ADD_JIT_STAT(hot_functions)
		VS_PY_ADD_JIT_STAT(compile_budget_skips)
		VS_PY_ADD_JIT_STAT(cache_capacity_skips)
		VS_PY_ADD_JIT_STAT(eager_compile_skips)
		VS_PY_ADD_JIT_STAT(array_fast_path_pushes)
		VS_PY_ADD_JIT_STAT(array_fast_path_gets)
		VS_PY_ADD_JIT_STAT(array_fast_path_sets)
		VS_PY_ADD_JIT_STAT(scalar_map_fast_path_gets)
		VS_PY_ADD_JIT_STAT(scalar_map_fast_path_sets)
		VS_PY_ADD_JIT_STAT(scalar_method_fast_path_i32)
		VS_PY_ADD_JIT_STAT(scalar_method_fast_path_u32)
		VS_PY_ADD_JIT_STAT(scalar_method_fast_path_f64)
		VS_PY_ADD_JIT_STAT(scalar_method_fast_path_mixed)
		VS_PY_ADD_JIT_STAT(scalar_function_fast_path_i32)
		VS_PY_ADD_JIT_STAT(scalar_function_fast_path_u32)
		VS_PY_ADD_JIT_STAT(scalar_function_fast_path_f64)
		VS_PY_ADD_JIT_STAT(scalar_function_fast_path_mixed)
		VS_PY_ADD_JIT_STAT(scalar_native_fast_path_calls)
		VS_PY_ADD_JIT_STAT(inlined_call_sites)
		VS_PY_ADD_JIT_STAT(linear_frame_reuses)
		VS_PY_ADD_JIT_STAT(linear_scratch_reuses)
		VS_PY_ADD_JIT_STAT(linear_scratch_allocations)
		VS_PY_ADD_JIT_STAT(linear_argument_scratch_leases)
		VS_PY_ADD_JIT_STAT(linear_argument_scratch_reuses)
		VS_PY_ADD_JIT_STAT(direct_native_calls)
		VS_PY_ADD_JIT_STAT(cost_model_vm_fallbacks)
		VS_PY_ADD_JIT_STAT(native_typed_loop_executions)
		VS_PY_ADD_JIT_STAT(controlled_native_executions)
		VS_PY_ADD_JIT_STAT(aot_executions)
		VS_PY_ADD_JIT_STAT(native_leaf_executions)
		VS_PY_ADD_JIT_STAT(native_cfg_executions)
		VS_PY_ADD_JIT_STAT(bridge_executions)
		VS_PY_ADD_JIT_STAT(linear_executions)
		VS_PY_ADD_JIT_STAT(vm_trampoline_executions)
		#undef VS_PY_ADD_JIT_STAT
		return Result;
	}

	PyObject* BufferToUnicode(vs_interop_buffer* Buffer, const char* Operation)
	{
		if (Buffer == nullptr)
		{
			PyErr_Format(GError, "%s returned no data", Operation);
			return nullptr;
		}
		const vs_interop_byte_view Bytes = vs_interop_buffer_view(Buffer);
		PyObject* Result = nullptr;
		if (Bytes.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
		{
			PyErr_SetString(PyExc_OverflowError, "VeroScript buffer is too large for Python");
		}
		else if (!IsValidByteView(Bytes))
		{
			PyErr_SetString(GError, "VeroScript returned an invalid buffer view");
		}
		else
		{
			Result = PyUnicode_DecodeUTF8(ByteData(Bytes),
			    static_cast<Py_ssize_t>(Bytes.size), "strict");
		}
		vs_interop_buffer_destroy(Buffer);
		return Result;
	}

	PyObject* SessionExportJson(FPySession* Self, const bool Reflection)
	{
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_buffer* Buffer = nullptr;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		const FCallbackActivity Activity(Self->Callbacks);
		Py_BEGIN_ALLOW_THREADS
		    Status = Reflection
		    ? vs_interop_session_export_reflection_json(Self->Handle, &Buffer)
		    : vs_interop_session_export_bindings_json(Self->Handle, &Buffer);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_buffer_destroy(Buffer);
			return RaiseStatus(Self, Status,
			    Reflection ? "export reflection JSON" : "export bindings JSON");
		}
		return BufferToUnicode(Buffer,
		    Reflection ? "reflection export" : "bindings export");
	}

	PyObject* SessionExportReflection(FPySession* Self, PyObject*)
	{
		return SessionExportJson(Self, true);
	}

	PyObject* SessionExportBindings(FPySession* Self, PyObject*)
	{
		return SessionExportJson(Self, false);
	}

	struct FArguments
	{
		~FArguments()
		{
			for (vs_interop_value* Value : Owned)
			{
				vs_interop_value_destroy(Value);
			}
		}

		std::vector<vs_interop_value*> Values;
		std::vector<vs_interop_value*> Owned;
	};

	bool BuildArguments(PyObject* Tuple, const Py_ssize_t First, FArguments& Out)
	{
		try
		{
			const Py_ssize_t Count = PyTuple_GET_SIZE(Tuple) - First;
			Out.Values.reserve(static_cast<std::size_t>(Count));
			Out.Owned.reserve(static_cast<std::size_t>(Count));
			for (Py_ssize_t Index = First; Index < PyTuple_GET_SIZE(Tuple); ++Index)
			{
				PyObject* Object = PyTuple_GET_ITEM(Tuple, Index);
				if (PyObject_TypeCheck(Object, &GValueType))
				{
					Out.Values.push_back(reinterpret_cast<FPyValue*>(Object)->Handle);
					continue;
				}
				vs_interop_value* Value = nullptr;
				const vs_interop_status Status = vs_interop_value_create(&Value);
				if (Status != VS_INTEROP_STATUS_OK)
				{
					RaiseStatus(nullptr, Status, "create argument");
					return false;
				}
				RememberOwnedValue(Out.Owned, Value);
				if (!SetValueFromPython(Value, Object, nullptr, nullptr))
				{
					return false;
				}
				Out.Values.push_back(Value);
			}
			return true;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
		}
		catch (...)
		{
			PyErr_SetString(GError, "unexpected error while converting arguments");
		}
		return false;
	}

	PyObject* FinishValueOperation(FPySession* Self, const vs_interop_status Status,
	    const vs_interop_run_result& Result, vs_interop_value* Output, const char* Operation)
	{
		if (RaiseCallbackError(Self))
		{
			vs_interop_value_destroy(Output);
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK && Status != VS_INTEROP_STATUS_ENGINE_ERROR)
		{
			vs_interop_value_destroy(Output);
			return RaiseStatus(Self, Status, Operation);
		}
		PyObject* RunResult = BuildRunResult(Self, Result);
		if (RunResult == nullptr)
		{
			vs_interop_value_destroy(Output);
			return nullptr;
		}
		PyObject* Value = TakeValue(Output);
		if (Value == nullptr)
		{
			Py_DECREF(RunResult);
			return nullptr;
		}
		PyObject* Tuple = PyTuple_Pack(2, Value, RunResult);
		Py_DECREF(Value);
		Py_DECREF(RunResult);
		return Tuple;
	}

	int CallableTraverse(FPyCallable* Self, visitproc visit, void* arg)
	{
		Py_VISIT(Self->SessionObject);
		return 0;
	}

	int CallableClear(FPyCallable* Self)
	{
		{
			auto* Session = reinterpret_cast<FPySession*>(Self->SessionObject);
			const FCallbackActivity Activity(Session != nullptr ? Session->Callbacks : nullptr);
			vs_interop_callable_destroy(std::exchange(Self->Handle, nullptr));
		}
		Py_CLEAR(Self->SessionObject);
		return 0;
	}

	void CallableDealloc(FPyCallable* Self)
	{
		PyObject_GC_UnTrack(Self);
		(void)CallableClear(Self);
		Py_TYPE(Self)->tp_free(reinterpret_cast<PyObject*>(Self));
	}

	PyObject* CallableClose(FPyCallable* Self, PyObject*)
	{
		if (Self->Handle == nullptr)
		{
			Py_RETURN_NONE;
		}
		if (Self->OwnerThread != PyThread_get_thread_ident())
		{
			PyErr_SetString(GThreadError,
			    "retained callable must be closed on its creator thread");
			return nullptr;
		}
		(void)CallableClear(Self);
		Py_RETURN_NONE;
	}

	PyObject* CallableGetClosed(FPyCallable* Self, void*)
	{
		return PyBool_FromLong(Self->Handle == nullptr);
	}

	PyObject* CallableGetValid(FPyCallable* Self, void*)
	{
		return PyBool_FromLong(Self->Handle != nullptr && vs_interop_callable_is_valid(Self->Handle) != 0);
	}

	PyObject* CallableInvoke(FPyCallable* Self, PyObject* Args)
	{
		if (Self->Handle == nullptr)
		{
			PyErr_SetString(GError, "retained callable is closed");
			return nullptr;
		}
		if (Self->OwnerThread != PyThread_get_thread_ident())
		{
			PyErr_SetString(GThreadError,
			    "retained callable may only be invoked on its creator thread");
			return nullptr;
		}
		auto* Session = reinterpret_cast<FPySession*>(Self->SessionObject);
		if (Session == nullptr || !CheckSession(Session))
		{
			return nullptr;
		}
		FArguments Arguments;
		if (!BuildArguments(Args, 0, Arguments))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create callable return value");
		}
		const FCallbackActivity Activity(Session->Callbacks);
		const FCallbackErrorScope ErrorScope(Session);
		vs_interop_run_result Result{sizeof(Result)};
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_callable_invoke(Self->Handle, Arguments.Values.data(),
		        static_cast<std::uint64_t>(Arguments.Values.size()), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Session, Status, Result, Output,
		    "invoke retained callable");
	}

	PyObject* CallableCall(PyObject* Object, PyObject* Args, PyObject* Keywords)
	{
		if (Keywords != nullptr && PyDict_Size(Keywords) != 0)
		{
			PyErr_SetString(PyExc_TypeError,
			    "retained VeroScript callables do not accept keyword arguments");
			return nullptr;
		}
		return CallableInvoke(reinterpret_cast<FPyCallable*>(Object), Args);
	}

	PyMethodDef CallableMethods[] = {
	    {"invoke", reinterpret_cast<PyCFunction>(CallableInvoke), METH_VARARGS,
	        "Invoke the retained VeroScript function or closure."},
	    {"close", reinterpret_cast<PyCFunction>(CallableClose), METH_NOARGS,
	        "Release the retained callable on its creator thread."},
	    {nullptr, nullptr, 0, nullptr}};

	PyGetSetDef CallableGetSet[] = {
	    {const_cast<char*>("closed"), reinterpret_cast<getter>(CallableGetClosed), nullptr,
	        const_cast<char*>("whether this callable was released"), nullptr},
	    {const_cast<char*>("is_valid"), reinterpret_cast<getter>(CallableGetValid), nullptr,
	        const_cast<char*>("whether the retained target remains valid"), nullptr},
	    {nullptr, nullptr, nullptr, nullptr, nullptr}};

	PyObject* SessionRetainCallable(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	{
		PyObject* FunctionObject = nullptr;
		int WeakThis = 0;
		static const char* Names[] = {"function_value", "weak_this", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "O!|p:retain_callable",
		        const_cast<char**>(Names), &GValueType, &FunctionObject, &WeakThis))
		{
			return nullptr;
		}
		if (FunctionObject == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "retain_callable parsed no function value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_callable* Handle = nullptr;
		const vs_interop_status Status = vs_interop_session_retain_callable(Self->Handle,
		    reinterpret_cast<FPyValue*>(FunctionObject)->Handle, 1,
		    static_cast<std::uint8_t>(WeakThis), &Handle);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "retain callable");
		}
		auto* Callable = reinterpret_cast<FPyCallable*>(GCallableType.tp_alloc(
		    &GCallableType, 0));
		if (Callable == nullptr)
		{
			vs_interop_callable_destroy(Handle);
			return nullptr;
		}
		Callable->Handle = Handle;
		Py_INCREF(Self);
		Callable->SessionObject = reinterpret_cast<PyObject*>(Self);
		Callable->OwnerThread = PyThread_get_thread_ident();
		return reinterpret_cast<PyObject*>(Callable);
	}

	PyObject* SessionCallFunction(FPySession* Self, PyObject* Args)
	{
		if (PyTuple_GET_SIZE(Args) < 1)
		{
			PyErr_SetString(PyExc_TypeError, "call_function requires a function name");
			return nullptr;
		}
		PyObject* NameObject = PyTuple_GET_ITEM(Args, 0);
		Py_ssize_t NameSize = 0;
		const char* Name = PyUnicode_AsUTF8AndSize(NameObject, &NameSize);
		if (Name == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		FArguments Arguments;
		if (!BuildArguments(Args, 1, Arguments))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create return value");
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_call_function(Self->Handle, View(Name, NameSize),
		        Arguments.Values.data(), static_cast<std::uint64_t>(Arguments.Values.size()), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Self, Status, Result, Output, "call function");
	}

	PyObject* SessionConstructObject(FPySession* Self, PyObject* Args)
	{
		if (PyTuple_GET_SIZE(Args) < 1)
		{
			PyErr_SetString(PyExc_TypeError, "construct_object requires a type name");
			return nullptr;
		}
		Py_ssize_t NameSize = 0;
		const char* Name = PyUnicode_AsUTF8AndSize(PyTuple_GET_ITEM(Args, 0), &NameSize);
		if (Name == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		FArguments Arguments;
		if (!BuildArguments(Args, 1, Arguments))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create object value");
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_construct_object(Self->Handle, View(Name, NameSize),
		        Arguments.Values.data(), static_cast<std::uint64_t>(Arguments.Values.size()), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Self, Status, Result, Output, "construct object");
	}

	bool ParseInstanceAndName(PyObject* Args, FPyValue** Instance, const char** Name,
	    Py_ssize_t* NameSize, const char* Operation)
	{
		if (PyTuple_GET_SIZE(Args) < 2 || !PyObject_TypeCheck(PyTuple_GET_ITEM(Args, 0), &GValueType))
		{
			PyErr_Format(PyExc_TypeError, "%s requires a Value instance and name", Operation);
			return false;
		}
		*Instance = reinterpret_cast<FPyValue*>(PyTuple_GET_ITEM(Args, 0));
		*Name = PyUnicode_AsUTF8AndSize(PyTuple_GET_ITEM(Args, 1), NameSize);
		return *Name != nullptr;
	}

	PyObject* SessionCallMethodCore(FPySession* Self, PyObject* Args, const bool Bypass)
	{
		FPyValue* Instance = nullptr;
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		if (!ParseInstanceAndName(Args, &Instance, &Name, &NameSize, "call_method") || !CheckSession(Self))
		{
			return nullptr;
		}
		FArguments Arguments;
		if (!BuildArguments(Args, 2, Arguments))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create return value");
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		Py_BEGIN_ALLOW_THREADS
		    Status = Bypass
		    ? vs_interop_session_call_method_bypassing_interceptor(
		          Self->Handle, Instance->Handle, View(Name, NameSize), Arguments.Values.data(),
		          static_cast<std::uint64_t>(Arguments.Values.size()), Output, &Result)
		    : vs_interop_session_call_method(
		          Self->Handle, Instance->Handle, View(Name, NameSize), Arguments.Values.data(),
		          static_cast<std::uint64_t>(Arguments.Values.size()), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Self, Status, Result, Output, "call method");
	}

	PyObject* SessionCallMethod(FPySession* Self, PyObject* Args)
	{
		return SessionCallMethodCore(Self, Args, false);
	}

	PyObject* SessionCallMethodBypassingInterceptor(FPySession* Self, PyObject* Args)
	{
		return SessionCallMethodCore(Self, Args, true);
	}

	PyObject* SessionGetField(FPySession* Self, PyObject* Args)
	{
		FPyValue* Instance = nullptr;
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		if (!ParseInstanceAndName(Args, &Instance, &Name, &NameSize, "get_field") || !CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create field value");
		}
		vs_interop_run_result Result{sizeof(Result)};
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_get_field(
		        Self->Handle, Instance->Handle, View(Name, NameSize), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Self, Status, Result, Output, "get field");
	}

	PyObject* SessionSetField(FPySession* Self, PyObject* Args)
	{
		PyObject* InstanceObject = nullptr;
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* ValueObject = nullptr;
		if (!PyArg_ParseTuple(Args, "O!s#O:set_field", &GValueType, &InstanceObject,
		        &Name, &NameSize, &ValueObject))
		{
			return nullptr;
		}
		if (InstanceObject == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "set_field parsed no instance value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		FArguments Values;
		PyObject* Single = PyTuple_Pack(1, ValueObject);
		if (Single == nullptr)
		{
			return nullptr;
		}
		const bool Built = BuildArguments(Single, 0, Values);
		Py_DECREF(Single);
		if (!Built)
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_set_field(Self->Handle,
		        reinterpret_cast<FPyValue*>(InstanceObject)->Handle, View(Name, NameSize),
		        Values.Values[0], &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "set field");
	}

	PyObject* SessionGetGlobal(FPySession* Self, PyObject* Args)
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		if (!PyArg_ParseTuple(Args, "s#:get_global", &Name, &NameSize) || !CheckSession(Self))
		{
			return nullptr;
		}
		vs_interop_value* Output = nullptr;
		vs_interop_status Status = vs_interop_value_create(&Output);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "create global value");
		}
		vs_interop_run_result Result{sizeof(Result)};
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_get_global(Self->Handle, View(Name, NameSize), Output, &Result);
		Py_END_ALLOW_THREADS return FinishValueOperation(Self, Status, Result, Output, "get global");
	}

	PyObject* SessionSetGlobal(FPySession* Self, PyObject* Args)
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* ValueObject = nullptr;
		if (!PyArg_ParseTuple(Args, "s#O:set_global", &Name, &NameSize, &ValueObject) || !CheckSession(Self))
		{
			return nullptr;
		}
		FArguments Values;
		PyObject* Single = PyTuple_Pack(1, ValueObject);
		if (Single == nullptr)
		{
			return nullptr;
		}
		const bool Built = BuildArguments(Single, 0, Values);
		Py_DECREF(Single);
		if (!Built)
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_set_global(
		        Self->Handle, View(Name, NameSize), Values.Values[0], &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "set global");
	}

	PyObject* SessionHasFunction(FPySession* Self, PyObject* Args)
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		unsigned int ArgumentCount = 0;
		if (!PyArg_ParseTuple(Args, "s#I:has_function", &Name, &NameSize, &ArgumentCount) || !CheckSession(Self))
		{
			return nullptr;
		}
		std::uint8_t HasFunction = 0;
		const vs_interop_status Status = vs_interop_session_has_function(
		    Self->Handle, View(Name, NameSize), ArgumentCount, &HasFunction);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "check function");
		}
		return PyBool_FromLong(HasFunction != 0);
	}

	PyObject* SessionHasMethod(FPySession* Self, PyObject* Args)
	{
		PyObject* InstanceObject = nullptr;
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		unsigned int ArgumentCount = 0;
		if (!PyArg_ParseTuple(Args, "O!s#I:has_method", &GValueType, &InstanceObject,
		        &Name, &NameSize, &ArgumentCount))
		{
			return nullptr;
		}
		if (InstanceObject == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "has_method parsed no instance value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		std::uint8_t HasMethod = 0;
		const vs_interop_status Status = vs_interop_session_has_method(Self->Handle,
		    reinterpret_cast<FPyValue*>(InstanceObject)->Handle, View(Name, NameSize),
		    ArgumentCount, &HasMethod);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "check method");
		}
		return PyBool_FromLong(HasMethod != 0);
	}

	struct FNativeTypeStorage
	{
		void Finalize()
		{
			Descriptor.struct_size = sizeof(Descriptor);
			Descriptor.name = View(Name);
			for (auto& Argument : Arguments)
			{
				Argument.Finalize();
			}
			ArgumentDescriptors.clear();
			ArgumentDescriptors.reserve(Arguments.size());
			for (const auto& Argument : Arguments)
			{
				ArgumentDescriptors.push_back(Argument.Descriptor);
			}
			Descriptor.type_arguments = ArgumentDescriptors.data();
			Descriptor.type_argument_count =
			    static_cast<std::uint64_t>(ArgumentDescriptors.size());
		}

		vs_interop_native_type_desc Descriptor{};
		std::string Name;
		std::vector<FNativeTypeStorage> Arguments;
		std::vector<vs_interop_native_type_desc> ArgumentDescriptors;
	};

	std::string_view TrimTypeName(std::string_view Name)
	{
		while (!Name.empty() && (Name.front() == ' ' || Name.front() == '\t' || Name.front() == '\r' || Name.front() == '\n'))
		{
			Name.remove_prefix(1);
		}
		while (!Name.empty() && (Name.back() == ' ' || Name.back() == '\t' || Name.back() == '\r' || Name.back() == '\n'))
		{
			Name.remove_suffix(1);
		}
		return Name;
	}

	bool ParseNativeTypeStorage(const std::string_view Input, FNativeTypeStorage& Out,
	    const std::size_t DescriptorDepth = 0)
	{
		// Match ConvertNativeType's 32-level C ABI limit before recursing here;
		// the ABI validator runs only after this parser and Finalize have returned.
		if (DescriptorDepth >= 32)
		{
			return false;
		}
		const std::string_view Name = TrimTypeName(Input);
		vs_interop_native_type Scalar = VS_INTEROP_NATIVE_VOID;
		if (ParseNativeTypeName(Name, &Scalar))
		{
			Out.Descriptor.type = Scalar;
			return true;
		}
		const std::size_t Open = Name.find('<');
		if (Open == std::string_view::npos)
		{
			if (Name.empty())
			{
				return false;
			}
			Out.Descriptor.type = VS_INTEROP_NATIVE_HOST_OBJECT;
			Out.Name.assign(Name);
			return true;
		}
		if (Name.back() != '>')
		{
			return false;
		}
		const std::string_view Outer = TrimTypeName(Name.substr(0, Open));
		std::vector<std::string_view> ArgumentTexts;
		std::size_t Start = Open + 1;
		unsigned int Depth = 0;
		for (std::size_t Index = Start; Index + 1 < Name.size(); ++Index)
		{
			if (Name[Index] == '<')
			{
				++Depth;
			}
			else if (Name[Index] == '>')
			{
				if (Depth == 0)
				{
					return false;
				}
				--Depth;
			}
			else if (Name[Index] == ',' && Depth == 0)
			{
				ArgumentTexts.push_back(TrimTypeName(Name.substr(Start, Index - Start)));
				Start = Index + 1;
			}
		}
		if (Depth != 0)
		{
			return false;
		}
		ArgumentTexts.push_back(TrimTypeName(Name.substr(Start, Name.size() - Start - 1)));
		auto ContainerType = [&](const std::string_view Text,
		                         const std::size_t Arity, const vs_interop_native_type Type) -> bool
		{
			if (Outer != Text)
			{
				return false;
			}
			if (ArgumentTexts.size() != Arity)
			{
				return false;
			}
			Out.Descriptor.type = Type;
			return true;
		};
		bool Matched = ContainerType("array", 1, VS_INTEROP_NATIVE_ARRAY) || ContainerType("map", 2, VS_INTEROP_NATIVE_MAP) || ContainerType("set", 1, VS_INTEROP_NATIVE_SET) || ContainerType("queue", 1, VS_INTEROP_NATIVE_QUEUE) || ContainerType("stack", 1, VS_INTEROP_NATIVE_STACK) || ContainerType("ordered_map", 2, VS_INTEROP_NATIVE_ORDERED_MAP) || ContainerType("deque", 1, VS_INTEROP_NATIVE_DEQUE) || ContainerType("channel", 1, VS_INTEROP_NATIVE_CHANNEL) || ContainerType("multi_map", 2, VS_INTEROP_NATIVE_MULTI_MAP) || ContainerType("ordered_multi_map", 2, VS_INTEROP_NATIVE_ORDERED_MULTI_MAP) || ContainerType("priority_queue", 1, VS_INTEROP_NATIVE_PRIORITY_QUEUE) || ContainerType("multi_set", 1, VS_INTEROP_NATIVE_MULTI_SET) || ContainerType("ordered_multi_set", 1, VS_INTEROP_NATIVE_ORDERED_MULTI_SET);
		if (Outer == "enum" && ArgumentTexts.size() == 1)
		{
			Out.Descriptor.type = VS_INTEROP_NATIVE_ENUM;
			Out.Name.assign(ArgumentTexts.front());
			return !Out.Name.empty();
		}
		if (!Matched)
		{
			if (Outer == "function")
			{
				return false;
			}
			Out.Descriptor.type = VS_INTEROP_NATIVE_HOST_OBJECT;
			Out.Name.assign(Name);
			return true;
		}
		Out.Arguments.resize(ArgumentTexts.size());
		for (std::size_t Index = 0; Index < ArgumentTexts.size(); ++Index)
		{
			if (!ParseNativeTypeStorage(ArgumentTexts[Index], Out.Arguments[Index], DescriptorDepth + 1) || Out.Arguments[Index].Descriptor.type == VS_INTEROP_NATIVE_VOID)
			{
				return false;
			}
		}
		return true;
	}

	PyObject* CopyCallbackArgument(const vs_interop_value* Value)
	{
		bool IsScalar = false;
		PyObject* Scalar = ScalarFromValue(Value, &IsScalar);
		if (IsScalar)
		{
			return Scalar;
		}
		vs_interop_value* Clone = nullptr;
		const vs_interop_status Status = vs_interop_value_clone(Value, &Clone);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "clone host callback argument");
		}
		return NewValueTakingOwnership(Clone);
	}

	PyObject* CopyPythonHostArgument(const vs_interop_value* Value)
	{
		if (Value != nullptr && vs_interop_value_get_kind(Value) == VS_INTEROP_VALUE_HOST_OBJECT)
		{
			std::uint32_t TypeId = 0;
			vs_interop_string_view TypeName{};
			void* Payload = nullptr;
			if (vs_interop_value_get_host_object(Value, &TypeId, &TypeName, &Payload) != VS_INTEROP_STATUS_OK)
			{
				PyErr_SetString(GTypeMismatchError,
				    "host protocol received an invalid Python host object");
				return nullptr;
			}
			// Core also publishes HOST_OBJECT values. Only unwrap payloads owned
			// by this adapter; foreign values remain independent Value wrappers.
			if (GPythonHostPayloads.find(Payload) != GPythonHostPayloads.end())
			{
				PyObject* Object = static_cast<FHostObjectPayload*>(Payload)->Object.Get();
				Py_INCREF(Object);
				return Object;
			}
		}
		return CopyCallbackArgument(Value);
	}

	bool RefreshHostTypeRegistry(FCallbackState* Owner)
	{
		if (!Owner->HostTypeRegistry)
		{
			auto Registry = std::make_shared<FHostTypeRegistry>();
			if (Registry->Types.Get() == nullptr) return false;
			Owner->HostTypeRegistry = std::move(Registry);
		}
		auto Registry = Owner->HostTypeRegistry;
		// Registration is append-only. Refresh only newly registered names so
		// object construction stays constant-time once the registry is populated.
		while (Registry->RegisteredTypeCount < Owner->HostTypes.size())
		{
			const auto* Registered = Owner->HostTypes[Registry->RegisteredTypeCount];
			FPyOwnedRef Name(PyUnicode_FromStringAndSize(Registered->Name.data(),
			    static_cast<Py_ssize_t>(Registered->Name.size())));
			FPyOwnedRef WeakType(PyWeakref_NewRef(Registered->PythonType, nullptr));
			if (Name.Get() == nullptr || WeakType.Get() == nullptr || PyDict_SetItem(Registry->Types.Get(), Name.Get(), WeakType.Get()) < 0)
			{
				return false;
			}
			++Registry->RegisteredTypeCount;
		}
		return true;
	}

	bool EnsureProtocolCallbackList(FHostTypeState* Type)
	{
		if (Type->ProtocolCallbacks.Get() != nullptr) return true;
		FPyOwnedRef List(PyList_New(0));
		if (List.Get() == nullptr) return false;
		// Allocation-triggered finalizers may construct a value of this type.
		// Preserve the list installed by that nested construction.
		if (Type->ProtocolCallbacks.Get() == nullptr)
		{
			Type->ProtocolCallbacks.Object = List.Release();
		}

		return true;
	}

	bool StorePythonHostObject(FHostTypeState* Type, PyObject* Object,
	    vs_interop_value* Output)
	try
	{
		if (!RefreshHostTypeRegistry(Type->Owner)) return false;
		if (!EnsureProtocolCallbackList(Type)) return false;
		auto Payload = std::make_unique<FHostObjectPayload>();
		Payload->Registry = Type->Owner->HostTypeRegistry;
		Py_INCREF(Object);
		Payload->Object.Object = Object;
		Py_INCREF(Type->ProtocolCallbacks.Get());
		Payload->ProtocolCallbacks.Object = Type->ProtocolCallbacks.Get();
		GPythonHostPayloads.insert(Payload.get());
		const auto Status = vs_interop_value_set_host_object_ex(Output, Type->TypeId,
		    View(Type->Name), Payload.get(), &DestroyPythonHostObject,
		    &IsPythonHostObjectType, nullptr);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			RaiseStatus(nullptr, Status, "store Python host object");
			return false;
		}
		(void)Payload.release();
		return true;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return false;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while storing Python host object");
		return false;
	}

	bool SetPythonHostCallbackReturn(FCallbackState* Owner,
	    const vs_interop_native_type ReturnType,
	    const std::string& ReturnTypeName,
	    PyObject* Result,
	    vs_interop_value* OutReturn)
	{
		if (PyObject_TypeCheck(Result, &GValueType))
		{
			return vs_interop_value_assign(OutReturn,
			           reinterpret_cast<FPyValue*>(Result)->Handle) == VS_INTEROP_STATUS_OK;
		}
		if (ReturnType == VS_INTEROP_NATIVE_HOST_OBJECT)
		{
			FHostTypeState* HostType = nullptr;
			if (Owner != nullptr)
			{
				for (FHostTypeState* Candidate : Owner->HostTypes)
				{
					if (Candidate->Name == ReturnTypeName)
					{
						HostType = Candidate;
						break;
					}
				}
			}
			if (HostType == nullptr)
			{
				PyErr_Format(GTypeMismatchError,
				    "Python host return type '%s' is not registered", ReturnTypeName.c_str());
				return false;
			}
			const int IsInstance = PyObject_IsInstance(Result, HostType->PythonType);
			if (IsInstance <= 0)
			{
				if (IsInstance == 0)
				{
					PyErr_SetString(GTypeMismatchError,
					    "Python host callback returned an incompatible object");
				}
				return false;
			}
			return StorePythonHostObject(HostType, Result, OutReturn);
		}
		if (ReturnType == VS_INTEROP_NATIVE_ENUM)
		{
			return SetValueFromPython(OutReturn, Result, "enum", ReturnTypeName.c_str());
		}
		const char* ExpectedKind = NativeTypeValueKindName(ReturnType);
		return ExpectedKind != nullptr && SetValueFromPython(OutReturn, Result, ExpectedKind, nullptr);
	}

	struct FInterceptProceedState
	{
		vs_interop_intercept_context* Context = nullptr;
		vs_interop_value* OutReturn = nullptr;
		bool Active = true;
		bool Proceeded = false;
		unsigned long OwnerThread = 0;
	};

	constexpr const char* GInterceptCapsuleName = "veroscript.InterceptContext";

	void DestroyInterceptCapsule(PyObject* Capsule)
	{
		void* Pointer = PyCapsule_GetPointer(Capsule, GInterceptCapsuleName);
		if (Pointer == nullptr)
		{
			PyErr_Clear();
			return;
		}
		delete static_cast<FInterceptProceedState*>(Pointer);
	}

	PyObject* InterceptProceed(PyObject*, PyObject* Args)
	{
		PyObject* Capsule = nullptr;
		if (!PyArg_ParseTuple(Args, "O:_intercept_proceed", &Capsule))
		{
			return nullptr;
		}
		auto* State = static_cast<FInterceptProceedState*>(
		    PyCapsule_GetPointer(Capsule, GInterceptCapsuleName));
		if (State == nullptr)
		{
			return nullptr;
		}
		if (State->OwnerThread != PyThread_get_thread_ident())
		{
			PyErr_SetString(GThreadError,
			    "intercepted call may only proceed on its callback thread");
			return nullptr;
		}
		if (!State->Active || State->Context == nullptr || State->OutReturn == nullptr)
		{
			PyErr_SetString(GError, "interceptor context has expired");
			return nullptr;
		}
		if (State->Proceeded)
		{
			PyErr_SetString(GError, "intercepted call has already proceeded");
			return nullptr;
		}
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_intercept_context_proceed(State->Context, State->OutReturn);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "proceed with intercepted call");
		}
		State->Proceeded = true;
		return CopyCallbackArgument(State->OutReturn);
	}

	void RecordInterceptorException(
	    vs_interop_intercept_context* Context, FCallbackState* State)
	{
		PyObject* Type = nullptr;
		PyObject* Value = nullptr;
		PyObject* Traceback = nullptr;
		PyErr_Fetch(&Type, &Value, &Traceback);
		PyErr_NormalizeException(&Type, &Value, &Traceback);
		if (Value == nullptr)
		{
			Value = PyObject_CallNoArgs(PyExc_RuntimeError);
		}
		FPyOwnedRef Text(Value != nullptr ? PyObject_Str(Value) : nullptr);
		if (Text.Get() != nullptr)
		{
			Py_ssize_t Size = 0;
			const char* Utf8 = PyUnicode_AsUTF8AndSize(Text.Get(), &Size);
			if (Utf8 != nullptr)
			{
				vs_interop_intercept_context_set_error(Context, View(Utf8, Size));
			}
			else
			{
				PyErr_Clear();
				static constexpr char Message[] = "Python instance interceptor failed";
				vs_interop_intercept_context_set_error(Context,
				    {Message, sizeof(Message) - 1});
			}
		}
		else
		{
			PyErr_Clear();
			static constexpr char Message[] = "Python instance interceptor failed";
			vs_interop_intercept_context_set_error(Context,
			    {Message, sizeof(Message) - 1});
		}
		StorePendingCallbackError(State, Value);
		Py_XDECREF(Type);
		Py_XDECREF(Value);
		Py_XDECREF(Traceback);
		PyErr_Clear();
	}

	vs_interop_intercept_result VS_INTEROP_CALL OnInstanceIntercept(
	    vs_interop_intercept_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		auto* Callbacks = static_cast<FCallbackState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(Callbacks);
		if (Context == nullptr || OutReturn == nullptr || Callbacks == nullptr || Callbacks->InstanceInterceptor == nullptr)
		{
			return VS_INTEROP_INTERCEPT_UNHANDLED;
		}
		try
		{
			const vs_interop_value* ReceiverValue =
			    vs_interop_intercept_context_receiver(Context);
			FPyOwnedRef Receiver(ReceiverValue != nullptr
			        ? CopyCallbackArgument(ReceiverValue)
			        : nullptr);
			vs_interop_resolved_method Target{};
			Target.struct_size = sizeof(Target);
			if (Receiver.Get() == nullptr || vs_interop_intercept_context_get_target(Context, &Target) != VS_INTEROP_STATUS_OK)
			{
				if (!PyErr_Occurred())
				{
					PyErr_SetString(GError, "failed to inspect intercepted call");
				}
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			FPyOwnedRef ModuleName(UnicodeFromView(Target.module_name));
			FPyOwnedRef DeclaringType(UnicodeFromView(Target.declaring_type_name));
			FPyOwnedRef MethodName(UnicodeFromView(Target.method_name));
			FPyOwnedRef QualifiedName(UnicodeFromView(Target.qualified_name));
			FPyOwnedRef TargetData(
			    ModuleName.Get() && DeclaringType.Get() && MethodName.Get() && QualifiedName.Get()
			        ? Py_BuildValue(
			              "{s:K,s:K,s:K,s:K,s:I,s:O,s:O,s:O,s:O,s:O,s:O,s:O}",
			              "program_generation", static_cast<unsigned long long>(Target.program_generation),
			              "module_identity", static_cast<unsigned long long>(Target.module_identity),
			              "declaring_type_identity", static_cast<unsigned long long>(Target.declaring_type_identity),
			              "method_identity", static_cast<unsigned long long>(Target.method_identity),
			              "function_index", Target.function_index,
			              "module_name", ModuleName.Get(), "declaring_type_name", DeclaringType.Get(),
			              "method_name", MethodName.Get(), "qualified_name", QualifiedName.Get(),
			              "is_virtual", Target.is_virtual ? Py_True : Py_False,
			              "is_const", Target.is_const ? Py_True : Py_False,
			              "returns_void", Target.returns_void ? Py_True : Py_False)
			        : nullptr);
			const std::uint64_t ArgumentCount =
			    vs_interop_intercept_context_argument_count(Context);
			if (ArgumentCount > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(PyExc_OverflowError, "too many intercepted arguments");
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			FPyOwnedRef ArgumentList(PyList_New(0));
			for (std::uint64_t Index = 0; ArgumentList.Get() != nullptr && Index < ArgumentCount; ++Index)
			{
				const vs_interop_value* Argument = nullptr;
				if (vs_interop_intercept_context_get_argument(Context, Index, &Argument) != VS_INTEROP_STATUS_OK || Argument == nullptr)
				{
					PyErr_SetString(GError, "failed to inspect intercepted argument");
					break;
				}
				FPyOwnedRef Copy(CopyCallbackArgument(Argument));
				if (Copy.Get() == nullptr || PyList_Append(ArgumentList.Get(), Copy.Get()) != 0)
				{
					break;
				}
			}
			FPyOwnedRef Arguments(
			    ArgumentList.Get() != nullptr && !PyErr_Occurred()
			        ? PyList_AsTuple(ArgumentList.Get())
			        : nullptr);
			auto ProceedOwner = std::make_unique<FInterceptProceedState>(
			    FInterceptProceedState{Context, OutReturn, true, false, PyThread_get_thread_ident()});
			auto* ProceedState = ProceedOwner.get();
			FPyOwnedRef Capsule(PyCapsule_New(
			    ProceedState, GInterceptCapsuleName, &DestroyInterceptCapsule));
			if (Capsule.Get() != nullptr)
			{
				(void)ProceedOwner.release();
			}
			FPyOwnedRef Data(TargetData.Get() && Arguments.Get() && Capsule.Get()
			        ? Py_BuildValue("{s:O,s:O,s:O,s:O}", "receiver", Receiver.Get(),
			              "target", TargetData.Get(), "arguments", Arguments.Get(),
			              "_context", Capsule.Get())
			        : nullptr);
			if (Data.Get() == nullptr)
			{
				if (!PyErr_Occurred())
				{
					PyErr_NoMemory();
				}
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			FPyOwnedRef Result(PyObject_CallOneArg(Callbacks->InstanceInterceptor, Data.Get()));
			ProceedState->Active = false;
			ProceedState->Context = nullptr;
			ProceedState->OutReturn = nullptr;
			if (Result.Get() == nullptr)
			{
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			PyObject* DecisionObject = Result.Get();
			PyObject* ReturnObject = nullptr;
			if (PyTuple_Check(Result.Get()) && PyTuple_GET_SIZE(Result.Get()) == 2)
			{
				DecisionObject = PyTuple_GET_ITEM(Result.Get(), 0);
				ReturnObject = PyTuple_GET_ITEM(Result.Get(), 1);
			}
			const long Decision = PyLong_AsLong(DecisionObject);
			if (PyErr_Occurred() || Decision < VS_INTEROP_INTERCEPT_UNHANDLED || Decision > VS_INTEROP_INTERCEPT_FAILED || (Decision == VS_INTEROP_INTERCEPT_UNHANDLED && ProceedState->Proceeded) || (Decision == VS_INTEROP_INTERCEPT_HANDLED && ReturnObject == nullptr && !ProceedState->Proceeded && !Target.returns_void))
			{
				if (!PyErr_Occurred())
				{
					PyErr_SetString(PyExc_ValueError,
					    "invalid Python instance interceptor result");
				}
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			if (Decision == VS_INTEROP_INTERCEPT_HANDLED && ReturnObject != nullptr && !SetValueFromPython(OutReturn, ReturnObject, nullptr, nullptr))
			{
				RecordInterceptorException(Context, Callbacks);
				return VS_INTEROP_INTERCEPT_FAILED;
			}
			if (Decision == VS_INTEROP_INTERCEPT_FAILED)
			{
				static constexpr char Message[] = "Python instance interceptor failed";
				vs_interop_intercept_context_set_error(Context,
				    {Message, sizeof(Message) - 1});
			}
			return static_cast<vs_interop_intercept_result>(Decision);
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			RecordInterceptorException(Context, Callbacks);
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python interceptor failure");
			RecordInterceptorException(Context, Callbacks);
		}
		return VS_INTEROP_INTERCEPT_FAILED;
	}

	void VS_INTEROP_CALL OnMutation(
	    const vs_interop_mutation_event* Event, void* UserData)
	{
		auto* State = static_cast<FCallbackState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(State);
		if (State == nullptr || State->MutationCallback == nullptr || Event == nullptr)
		{
			return;
		}
		FPyOwnedRef Object(Event->object != nullptr
		        ? CopyCallbackArgument(Event->object)
		        : Py_NewRef(Py_None));
		FPyOwnedRef Value(Event->value != nullptr
		        ? CopyCallbackArgument(Event->value)
		        : Py_NewRef(Py_None));
		FPyOwnedRef TypeName(UnicodeFromView(Event->object_type_name));
		FPyOwnedRef FieldName(UnicodeFromView(Event->field_name));
		if (Object.Get() == nullptr || Value.Get() == nullptr || TypeName.Get() == nullptr || FieldName.Get() == nullptr)
		{
			StoreCallbackException(State);
			return;
		}
		FPyOwnedRef Data(Py_BuildValue("{s:O,s:O,s:O,s:I,s:I,s:K,s:O}",
		    "object", Object.Get(), "object_type_name", TypeName.Get(),
		    "field_name", FieldName.Get(), "field_index", Event->field_index,
		    "kind", Event->kind, "object_mutation_version",
		    static_cast<unsigned long long>(Event->object_mutation_version),
		    "value", Value.Get()));
		if (Data.Get() == nullptr)
		{
			StoreCallbackException(State);
			return;
		}
		FPyOwnedRef Result(PyObject_CallOneArg(State->MutationCallback, Data.Get()));
		if (Result.Get() == nullptr)
		{
			StoreCallbackException(State);
		}
	}

	struct FReferenceVisitorState
	{
		FCallbackState* Owner = nullptr;
		PyObject* List = nullptr;
	};

	vs_interop_callback_result VS_INTEROP_CALL VisitObjectReference(
	    const vs_interop_external_reference_kind Kind,
	    const vs_interop_value* Value, void* UserData)
	{
		auto* State = static_cast<FReferenceVisitorState*>(UserData);
		FPyGilGuard Gil;
		if (State == nullptr || State->Owner == nullptr || State->List == nullptr)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		FPyOwnedRef PythonValue(CopyCallbackArgument(Value));
		FPyOwnedRef Item(PythonValue.Get() != nullptr
		        ? Py_BuildValue("(IO)", Kind, PythonValue.Get())
		        : nullptr);
		if (Item.Get() == nullptr || PyList_Append(State->List, Item.Get()) != 0)
		{
			StoreCallbackException(State->Owner);
			return VS_INTEROP_CALLBACK_ERROR;
		}
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL InvokePythonHostFunction(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		const FActiveMemoryCall MemoryCall(Context);
		auto* State = static_cast<FHostFunctionState*>(UserData);
		const PyGILState_STATE Gil = PyGILState_Ensure();
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		vs_interop_callback_result CallbackResult = VS_INTEROP_CALLBACK_ERROR;
		std::vector<std::pair<FPyValue*, vs_interop_value*>> MutableArguments;
		const auto ReleaseMutableCopies = [&MutableArguments]()
		{
			for (const auto& Mutable : MutableArguments)
			{
				Py_DECREF(reinterpret_cast<PyObject*>(Mutable.first));
			}
			MutableArguments.clear();
		};
		try
		{
			if (State == nullptr || State->Owner == nullptr || State->Callable == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python host function is no longer available");
				RecordHostCallbackException(Context, State != nullptr ? State->Owner : nullptr);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const std::uint64_t ArgumentCount = vs_interop_call_context_argument_count(Context);
			if (ArgumentCount != State->ParameterTypes.size() || ArgumentCount > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(GCallbackError, "Python host function received an invalid argument list");
				RecordHostCallbackException(Context, State->Owner);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}

			MutableArguments.reserve(static_cast<std::size_t>(ArgumentCount));
			PyObject* Arguments = PyTuple_New(static_cast<Py_ssize_t>(ArgumentCount));
			if (Arguments == nullptr)
			{
				throw std::bad_alloc();
			}
			bool Failed = false;
			for (std::uint64_t Index = 0; Index < ArgumentCount; ++Index)
			{
				PyObject* Argument = nullptr;
				if ((State->ParameterFlags[static_cast<std::size_t>(Index)] & VS_INTEROP_PARAM_REF) != 0)
				{
					vs_interop_value* Mutable = nullptr;
					if (vs_interop_call_context_get_mutable_argument(Context, Index, &Mutable) != VS_INTEROP_STATUS_OK)
					{
						PyErr_SetString(GTypeMismatchError, "host ref argument is not mutable");
						Failed = true;
						break;
					}
					vs_interop_value* Clone = nullptr;
					if (vs_interop_value_clone(Mutable, &Clone) != VS_INTEROP_STATUS_OK)
					{
						PyErr_SetString(GError, "failed to clone host ref argument");
						Failed = true;
						break;
					}
					Argument = NewValueTakingOwnership(Clone);
					if (Argument != nullptr)
					{
						MutableArguments.emplace_back(reinterpret_cast<FPyValue*>(Argument), Mutable);
						Py_INCREF(Argument);
					}
				}
				else
				{
					const vs_interop_value* Value = nullptr;
					if (vs_interop_call_context_get_argument(Context, Index, &Value) != VS_INTEROP_STATUS_OK)
					{
						PyErr_SetString(GError, "failed to read host callback argument");
						Failed = true;
						break;
					}
					Argument = CopyPythonHostArgument(Value);
				}
				if (Argument == nullptr)
				{
					Failed = true;
					break;
				}
				PyTuple_SET_ITEM(Arguments, static_cast<Py_ssize_t>(Index), Argument);
			}

			PyObject* Result = nullptr;
			if (!Failed)
			{
				Result = PyObject_CallObject(State->Callable, Arguments);
			}
			Py_DECREF(Arguments);
			if (Result == nullptr)
			{
				ReleaseMutableCopies();
				RecordHostCallbackException(Context, State->Owner);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}

			if (State->ReturnType == VS_INTEROP_NATIVE_VOID)
			{
				if (Result != Py_None)
				{
					Py_DECREF(Result);
					ReleaseMutableCopies();
					PyErr_SetString(GTypeMismatchError,
					    "a void Python host function must return None");
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			else
			{
				const bool ReturnWritten = SetPythonHostCallbackReturn(
				    State->Owner, State->ReturnType, State->ReturnTypeName, Result, OutReturn);
				if (!ReturnWritten)
				{
					Py_DECREF(Result);
					ReleaseMutableCopies();
					if (!PyErr_Occurred())
					{
						PyErr_SetString(GTypeMismatchError, "unsupported Python host return value");
					}
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}

			for (const auto& Mutable : MutableArguments)
			{
				if (vs_interop_value_assign(Mutable.second, Mutable.first->Handle) != VS_INTEROP_STATUS_OK)
				{
					Py_DECREF(Result);
					ReleaseMutableCopies();
					PyErr_SetString(GTypeMismatchError,
					    "Python host ref argument must remain a compatible value");
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			Py_DECREF(Result);
			ReleaseMutableCopies();
			CallbackResult = VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			ReleaseMutableCopies();
			PyErr_NoMemory();
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
			}
		}
		catch (...)
		{
			ReleaseMutableCopies();
			PyErr_SetString(GCallbackError, "unexpected Python host callback failure");
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
			}
		}
		PyGILState_Release(Gil);
		return CallbackResult;
	}

	PyObject* SessionRegisterFunction(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* Callable = nullptr;
		const char* ReturnTypeText = nullptr;
		Py_ssize_t ReturnTypeSize = 0;
		PyObject* ParameterSpecs = nullptr;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		static const char* Names[] = {
		    "name", "callback", "return_type", "parameters", "documentation", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#Os#O|s#:register_function",
		        const_cast<char**>(Names), &Name, &NameSize, &Callable,
		        &ReturnTypeText, &ReturnTypeSize, &ParameterSpecs,
		        &Documentation, &DocumentationSize))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		if (!PyCallable_Check(Callable))
		{
			PyErr_SetString(PyExc_TypeError, "host callback must be callable");
			return nullptr;
		}
		FNativeTypeStorage ReturnTypeStorage;
		if (!ParseNativeTypeStorage(
		        {ReturnTypeText, static_cast<std::size_t>(ReturnTypeSize)}, ReturnTypeStorage))
		{
			PyErr_SetString(PyExc_ValueError, "unsupported host function return type");
			return nullptr;
		}
		const vs_interop_native_type ReturnType = ReturnTypeStorage.Descriptor.type;

		FPyOwnedRef Specs(SnapshotSequence(ParameterSpecs, "parameters must be a sequence"));
		if (Specs.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t ParameterCount = PySequence_Fast_GET_SIZE(Specs.Get());
		if (ParameterCount > UINT8_MAX)
		{
			PyErr_SetString(PyExc_ValueError, "host functions support at most 255 parameters");
			return nullptr;
		}

		std::unique_ptr<FHostFunctionState> State(new (std::nothrow) FHostFunctionState);
		if (!State)
		{
			return PyErr_NoMemory();
		}
		State->Owner = Self->Callbacks;
		State->ReturnType = ReturnType;
		State->ReturnTypeName = !ReturnTypeStorage.Name.empty()
		    ? ReturnTypeStorage.Name
		    : std::string(ReturnTypeText, static_cast<std::size_t>(ReturnTypeSize));
		State->ParameterTypes.reserve(static_cast<std::size_t>(ParameterCount));
		State->ParameterFlags.reserve(static_cast<std::size_t>(ParameterCount));
		std::vector<std::string> ParameterNames;
		std::vector<std::string> ParameterDocs;
		std::vector<FNativeTypeStorage> ParameterTypeStorage;
		ParameterNames.reserve(static_cast<std::size_t>(ParameterCount));
		ParameterDocs.reserve(static_cast<std::size_t>(ParameterCount));
		ParameterTypeStorage.reserve(static_cast<std::size_t>(ParameterCount));
		bool Valid = true;
		for (Py_ssize_t Index = 0; Index < ParameterCount; ++Index)
		{
			PyObject* Spec = PySequence_Fast_GET_ITEM(Specs.Get(), Index);
			if (!PyTuple_Check(Spec) || PyTuple_GET_SIZE(Spec) != 4)
			{
				PyErr_SetString(PyExc_TypeError,
				    "each host parameter must be (type, flags, name, documentation)");
				Valid = false;
				break;
			}
			Py_ssize_t TypeSize = 0;
			const char* TypeText = PyUnicode_AsUTF8AndSize(PyTuple_GET_ITEM(Spec, 0), &TypeSize);
			const unsigned long Flags = PyLong_AsUnsignedLong(PyTuple_GET_ITEM(Spec, 1));
			Py_ssize_t ParameterNameSize = 0;
			const char* ParameterName = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Spec, 2), &ParameterNameSize);
			Py_ssize_t ParameterDocSize = 0;
			const char* ParameterDoc = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Spec, 3), &ParameterDocSize);
			FNativeTypeStorage TypeStorage;
			if (TypeText == nullptr || ParameterName == nullptr || ParameterDoc == nullptr || PyErr_Occurred() || !ParseNativeTypeStorage({TypeText, static_cast<std::size_t>(TypeSize)}, TypeStorage) || TypeStorage.Descriptor.type == VS_INTEROP_NATIVE_VOID || (Flags & ~(VS_INTEROP_PARAM_REF | VS_INTEROP_PARAM_CONST_REF)) != 0 || ((Flags & VS_INTEROP_PARAM_REF) != 0 && (Flags & VS_INTEROP_PARAM_CONST_REF) != 0))
			{
				if (!PyErr_Occurred())
				{
					PyErr_SetString(PyExc_ValueError, "invalid host function parameter specification");
				}
				Valid = false;
				break;
			}
			State->ParameterTypes.push_back(TypeStorage.Descriptor.type);
			State->ParameterFlags.push_back(static_cast<std::uint32_t>(Flags));
			ParameterTypeStorage.push_back(std::move(TypeStorage));
			ParameterNames.emplace_back(ParameterName, static_cast<std::size_t>(ParameterNameSize));
			ParameterDocs.emplace_back(ParameterDoc, static_cast<std::size_t>(ParameterDocSize));
		}
		if (!Valid)
		{
			return nullptr;
		}

		std::vector<vs_interop_native_param_desc> Parameters(
		    static_cast<std::size_t>(ParameterCount));
		std::vector<vs_interop_string_view> ParameterNameViews;
		std::vector<vs_interop_string_view> ParameterDocViews;
		ParameterNameViews.reserve(static_cast<std::size_t>(ParameterCount));
		ParameterDocViews.reserve(static_cast<std::size_t>(ParameterCount));
		ReturnTypeStorage.Finalize();
		for (auto& Type : ParameterTypeStorage)
		{
			Type.Finalize();
		}
		for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		{
			Parameters[Index].struct_size = sizeof(vs_interop_native_param_desc);
			Parameters[Index].type = ParameterTypeStorage[Index].Descriptor;
			Parameters[Index].flags = State->ParameterFlags[Index];
			ParameterNameViews.push_back(View(ParameterNames[Index]));
			ParameterDocViews.push_back(View(ParameterDocs[Index]));
		}
		vs_interop_host_function_desc Descriptor{};
		Descriptor.struct_size = sizeof(Descriptor);
		Descriptor.name = View(Name, NameSize);
		Descriptor.return_type = ReturnTypeStorage.Descriptor;
		Descriptor.parameters = Parameters.data();
		Descriptor.parameter_count = static_cast<std::uint64_t>(Parameters.size());
		Descriptor.callback = &InvokePythonHostFunction;
		Descriptor.user_data = State.get();
		Descriptor.documentation = View(Documentation, DocumentationSize);
		Descriptor.parameter_names = {
		    ParameterNameViews.data(), static_cast<std::uint64_t>(ParameterNameViews.size())};
		Descriptor.parameter_documentation = {
		    ParameterDocViews.data(), static_cast<std::uint64_t>(ParameterDocViews.size())};

		if (!CheckSession(Self))
		{
			return nullptr;
		}
		Py_INCREF(Callable);
		State->Callable = Callable;
		Self->Callbacks->HostFunctions.push_back(State.get());
		const vs_interop_status Status =
		    vs_interop_session_register_host_function(Self->Handle, &Descriptor);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			Self->Callbacks->HostFunctions.pop_back();
			return RaiseStatus(Self, Status, "register Python host function");
		}
		(void)State.release();
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return nullptr;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python host function");
		return nullptr;
	}

	PyObject* SessionRegisterAnnotation(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		const char* Detail = "";
		Py_ssize_t DetailSize = 0;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		PyObject* ArgumentSpecs = nullptr;
		int CaseSensitive = 1;
		static const char* Names[] = {
		    "name", "detail", "documentation", "arguments", "case_sensitive", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#|s#s#Op:register_annotation",
		        const_cast<char**>(Names), &Name, &NameSize, &Detail, &DetailSize,
		        &Documentation, &DocumentationSize, &ArgumentSpecs, &CaseSensitive))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}

		FPyOwnedRef EmptyArguments(
		    ArgumentSpecs == nullptr ? PyTuple_New(0) : nullptr);
		if (ArgumentSpecs == nullptr)
		{
			if (EmptyArguments.Get() == nullptr)
			{
				return nullptr;
			}
			ArgumentSpecs = EmptyArguments.Get();
		}
		FPyOwnedRef Specs(SnapshotSequence(
		    ArgumentSpecs, "annotation arguments must be a sequence"));
		if (Specs.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Specs.Get());
		std::vector<std::string> ArgumentNames;
		std::vector<std::string> ArgumentDetails;
		std::vector<std::string> ArgumentDocumentation;
		ArgumentNames.reserve(static_cast<std::size_t>(Count));
		ArgumentDetails.reserve(static_cast<std::size_t>(Count));
		ArgumentDocumentation.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			PyObject* Spec = PySequence_Fast_GET_ITEM(Specs.Get(), Index);
			if (!PyTuple_Check(Spec) || PyTuple_GET_SIZE(Spec) != 3)
			{
				PyErr_SetString(PyExc_TypeError,
				    "each annotation argument must be (name, detail, documentation)");
				return nullptr;
			}
			Py_ssize_t Sizes[3]{};
			const char* Values[3]{};
			for (Py_ssize_t Field = 0; Field < 3; ++Field)
			{
				Values[Field] = PyUnicode_AsUTF8AndSize(
				    PyTuple_GET_ITEM(Spec, Field), &Sizes[Field]);
			}
			if (Values[0] == nullptr || Values[1] == nullptr || Values[2] == nullptr)
			{
				return nullptr;
			}
			ArgumentNames.emplace_back(Values[0], static_cast<std::size_t>(Sizes[0]));
			ArgumentDetails.emplace_back(Values[1], static_cast<std::size_t>(Sizes[1]));
			ArgumentDocumentation.emplace_back(
			    Values[2], static_cast<std::size_t>(Sizes[2]));
		}
		std::vector<vs_interop_annotation_argument_desc> Descriptors(
		    static_cast<std::size_t>(Count));
		for (std::size_t Index = 0; Index < Descriptors.size(); ++Index)
		{
			Descriptors[Index].struct_size = sizeof(vs_interop_annotation_argument_desc);
			Descriptors[Index].name = View(ArgumentNames[Index]);
			Descriptors[Index].detail = View(ArgumentDetails[Index]);
			Descriptors[Index].documentation = View(ArgumentDocumentation[Index]);
		}
		vs_interop_annotation_desc Descriptor{};
		Descriptor.struct_size = sizeof(Descriptor);
		Descriptor.name = View(Name, NameSize);
		Descriptor.detail = View(Detail, DetailSize);
		Descriptor.documentation = View(Documentation, DocumentationSize);
		Descriptor.arguments = Descriptors.data();
		Descriptor.argument_count = static_cast<std::uint64_t>(Descriptors.size());
		Descriptor.name_flags = CaseSensitive ? 0u : VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE;
		const vs_interop_status Status =
		    vs_interop_session_register_annotation(Self->Handle, &Descriptor);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "register annotation");
		}
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering annotation");
		return nullptr;
	}

	PyObject* SessionRegisterTypeAlias(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* TypeParameters = nullptr;
		const char* Target = nullptr;
		Py_ssize_t TargetSize = 0;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		static const char* Names[] = {
		    "name", "type_parameters", "target_type", "documentation", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#Os#|s#:register_type_alias",
		        const_cast<char**>(Names), &Name, &NameSize, &TypeParameters,
		        &Target, &TargetSize, &Documentation, &DocumentationSize))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		FPyOwnedRef Sequence(SnapshotSequence(
		    TypeParameters, "type_parameters must be a sequence"));
		if (Sequence.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Sequence.Get());
		std::vector<std::string> Parameters;
		Parameters.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			Py_ssize_t Size = 0;
			const char* Text = PyUnicode_AsUTF8AndSize(
			    PySequence_Fast_GET_ITEM(Sequence.Get(), Index), &Size);
			if (Text == nullptr)
			{
				return nullptr;
			}
			Parameters.emplace_back(Text, static_cast<std::size_t>(Size));
		}
		std::vector<vs_interop_string_view> Views;
		const vs_interop_string_list ParameterList = MakeStringList(Parameters, Views);
		const vs_interop_status Status = vs_interop_session_register_type_alias(
		    Self->Handle, View(Name, NameSize), ParameterList, View(Target, TargetSize),
		    View(Documentation, DocumentationSize));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "register type alias");
		}
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering type alias");
		return nullptr;
	}

	void RecordGenericHostTypeException(
	    vs_interop_generic_type_context* Context, FCallbackState* State)
	{
		PyObject* Type = nullptr;
		PyObject* Value = nullptr;
		PyObject* Traceback = nullptr;
		PyErr_Fetch(&Type, &Value, &Traceback);
		PyErr_NormalizeException(&Type, &Value, &Traceback);
		if (Value == nullptr)
		{
			Value = PyObject_CallNoArgs(PyExc_RuntimeError);
		}
		FPyOwnedRef Text(Value != nullptr ? PyObject_Str(Value) : nullptr);
		if (Text.Get() != nullptr)
		{
			Py_ssize_t Size = 0;
			const char* Utf8 = PyUnicode_AsUTF8AndSize(Text.Get(), &Size);
			if (Utf8 != nullptr)
			{
				vs_interop_generic_type_context_set_error(Context, View(Utf8, Size));
			}
			else
			{
				PyErr_Clear();
			}
		}
		else
		{
			PyErr_Clear();
		}
		StorePendingCallbackError(State, Value);
		Py_XDECREF(Type);
		Py_XDECREF(Value);
		Py_XDECREF(Traceback);
		PyErr_Clear();
	}

	vs_interop_callback_result VS_INTEROP_CALL InstantiatePythonGenericHostType(
	    vs_interop_generic_type_context* Context, void* UserData)
	{
		auto* State = static_cast<FGenericHostTypeState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		try
		{
			if (Context == nullptr || State == nullptr || State->Owner == nullptr || State->Factory == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python generic host type factory is unavailable");
				RecordGenericHostTypeException(Context, State != nullptr ? State->Owner : nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const std::uint64_t Count =
			    vs_interop_generic_type_context_argument_count(Context);
			if (Count > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(PyExc_OverflowError, "too many generic host type arguments");
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FPyOwnedRef Arguments(PyTuple_New(static_cast<Py_ssize_t>(Count)));
			std::vector<std::string> TypeArguments;
			TypeArguments.reserve(static_cast<std::size_t>(Count));
			for (std::uint64_t Index = 0; Arguments.Get() != nullptr && Index < Count; ++Index)
			{
				vs_interop_string_view Argument{};
				if (vs_interop_generic_type_context_get_argument(Context, Index, &Argument) != VS_INTEROP_STATUS_OK)
				{
					PyErr_SetString(GError, "failed to read generic host type argument");
					break;
				}
				FPyOwnedRef Text(UnicodeFromView(Argument));
				if (Text.Get() == nullptr)
				{
					break;
				}
				PyTuple_SET_ITEM(Arguments.Get(), static_cast<Py_ssize_t>(Index), Text.Release());
				TypeArguments.emplace_back(Argument.data,
				    static_cast<std::size_t>(Argument.size));
			}
			if (Arguments.Get() == nullptr || PyErr_Occurred())
			{
				if (!PyErr_Occurred())
				{
					PyErr_NoMemory();
				}
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FPyOwnedRef Result(PyObject_CallObject(State->Factory, Arguments.Get()));
			if (Result.Get() == nullptr)
			{
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			PyObject* PythonType = Result.Get();
			unsigned int Kind = VS_INTEROP_HOST_CLASS;
			const char* Documentation = "";
			Py_ssize_t DocumentationSize = 0;
			int Inheritable = 0;
			if (PyTuple_Check(Result.Get()))
			{
				const Py_ssize_t Size = PyTuple_GET_SIZE(Result.Get());
				if (Size < 1 || Size > 4)
				{
					PyErr_SetString(PyExc_TypeError,
					    "generic host factory tuple must be (type[, kind, documentation, inheritable])");
					RecordGenericHostTypeException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				PythonType = PyTuple_GET_ITEM(Result.Get(), 0);
				if (Size > 1)
				{
					Kind = static_cast<unsigned int>(
					    PyLong_AsUnsignedLong(PyTuple_GET_ITEM(Result.Get(), 1)));
					if (PyErr_Occurred())
					{
						RecordGenericHostTypeException(Context, State->Owner);
						return VS_INTEROP_CALLBACK_ERROR;
					}
				}
				if (Size > 2)
				{
					Documentation = PyUnicode_AsUTF8AndSize(
					    PyTuple_GET_ITEM(Result.Get(), 2), &DocumentationSize);
					if (Documentation == nullptr)
					{
						RecordGenericHostTypeException(Context, State->Owner);
						return VS_INTEROP_CALLBACK_ERROR;
					}
				}
				if (Size > 3)
				{
					Inheritable = PyObject_IsTrue(PyTuple_GET_ITEM(Result.Get(), 3));
					if (Inheritable < 0)
					{
						RecordGenericHostTypeException(Context, State->Owner);
						return VS_INTEROP_CALLBACK_ERROR;
					}
				}
			}
			if (!PyType_Check(PythonType) || (Kind != VS_INTEROP_HOST_STRUCT && Kind != VS_INTEROP_HOST_CLASS))
			{
				PyErr_SetString(PyExc_TypeError,
				    "generic host factory must return a Python type and struct/class kind");
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			vs_interop_host_type_desc Descriptor{};
			Descriptor.struct_size = sizeof(Descriptor);
			Descriptor.name = View(State->Name);
			Descriptor.kind = Kind;
			Descriptor.documentation = View(Documentation, DocumentationSize);
			Descriptor.inheritable = static_cast<std::uint8_t>(Inheritable != 0);
			const vs_interop_status Status =
			    vs_interop_generic_type_context_set_type(Context, &Descriptor);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				PyErr_SetString(GError, "generic host type description was rejected");
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			std::string ConcreteName = State->Name;
			ConcreteName.push_back('<');
			for (std::size_t Index = 0; Index < TypeArguments.size(); ++Index)
			{
				if (Index != 0)
				{
					ConcreteName.push_back(',');
				}
				ConcreteName += TypeArguments[Index];
			}
			ConcreteName.push_back('>');
			auto HostType = std::make_unique<FHostTypeState>();
			HostType->Owner = State->Owner;
			HostType->Name = std::move(ConcreteName);
			Py_INCREF(PythonType);
			HostType->PythonType = PythonType;
			State->Owner->HostTypes.push_back(HostType.get());
			(void)HostType.release();
			if (State->Owner->HostTypeRegistry && !RefreshHostTypeRegistry(State->Owner))
			{
				RecordGenericHostTypeException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			return VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			RecordGenericHostTypeException(Context, State != nullptr ? State->Owner : nullptr);
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python generic host type failure");
			RecordGenericHostTypeException(Context, State != nullptr ? State->Owner : nullptr);
		}
		return VS_INTEROP_CALLBACK_ERROR;
	}

	PyObject* SessionRegisterGenericHostType(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		unsigned int Arity = 0;
		PyObject* Factory = nullptr;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		static const char* Names[] = {"name", "arity", "factory", "documentation", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords,
		        "s#IO|s#:register_generic_host_type", const_cast<char**>(Names),
		        &Name, &NameSize, &Arity, &Factory, &Documentation, &DocumentationSize) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		if (!PyCallable_Check(Factory))
		{
			PyErr_SetString(PyExc_TypeError, "generic host type factory must be callable");
			return nullptr;
		}
		auto State = std::make_unique<FGenericHostTypeState>();
		State->Owner = Self->Callbacks;
		State->Name.assign(Name, static_cast<std::size_t>(NameSize));
		Py_INCREF(Factory);
		State->Factory = Factory;
		const vs_interop_status Status =
		    vs_interop_session_register_generic_host_type_template(
		        Self->Handle, View(State->Name), Arity, View(Documentation, DocumentationSize),
		        &InstantiatePythonGenericHostType, State.get(), &State->TemplateId);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "register Python generic host type");
		}
		const std::uint32_t TemplateId = State->TemplateId;
		Self->Callbacks->GenericHostTypes.push_back(State.release());
		return PyLong_FromUnsignedLong(TemplateId);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python generic host type");
		return nullptr;
	}

	PyObject* SessionInstantiateGenericHostType(FPySession* Self, PyObject* Args)
	try
	{
		unsigned int TemplateId = 0;
		PyObject* ArgumentsObject = nullptr;
		if (!PyArg_ParseTuple(Args, "IO:instantiate_generic_host_type",
		        &TemplateId, &ArgumentsObject) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		FGenericHostTypeState* Template = nullptr;
		for (FGenericHostTypeState* Candidate : Self->Callbacks->GenericHostTypes)
		{
			if (Candidate->TemplateId == TemplateId)
			{
				Template = Candidate;
				break;
			}
		}
		if (Template == nullptr)
		{
			PyErr_SetString(PyExc_ValueError, "unknown Python generic host type template id");
			return nullptr;
		}
		FPyOwnedRef Sequence(SnapshotSequence(ArgumentsObject,
		    "type_arguments must be a sequence"));
		if (Sequence.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Sequence.Get());
		std::vector<std::string> TypeArguments;
		TypeArguments.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			Py_ssize_t Size = 0;
			const char* Text = PyUnicode_AsUTF8AndSize(
			    PySequence_Fast_GET_ITEM(Sequence.Get(), Index), &Size);
			if (Text == nullptr)
			{
				return nullptr;
			}
			TypeArguments.emplace_back(Text, static_cast<std::size_t>(Size));
		}
		std::vector<vs_interop_string_view> Views;
		const vs_interop_string_list List = MakeStringList(TypeArguments, Views);
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const std::size_t HostTypeCountBefore = Self->Callbacks->HostTypes.size();
		std::uint32_t TypeId = 0;
		const vs_interop_status Status = vs_interop_session_instantiate_generic_host_type(
		    Self->Handle, TemplateId, List, &TypeId);
		if (RaiseCallbackError(Self))
		{
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "instantiate Python generic host type");
		}
		FHostTypeState* HostType = nullptr;
		for (FHostTypeState* Candidate : Self->Callbacks->HostTypes)
		{
			if (Candidate->TypeId == TypeId)
			{
				HostType = Candidate;
				break;
			}
		}
		if (HostType == nullptr)
		{
			for (std::size_t Index = Self->Callbacks->HostTypes.size();
			    Index > HostTypeCountBefore; --Index)
			{
				FHostTypeState* Candidate = Self->Callbacks->HostTypes[Index - 1];
				if (Candidate->TypeId == 0 && Candidate->Name.size() > Template->Name.size() && Candidate->Name.compare(0, Template->Name.size(), Template->Name) == 0 && Candidate->Name[Template->Name.size()] == '<')
				{
					HostType = Candidate;
					break;
				}
			}
		}
		if (HostType == nullptr || HostType->PythonType == nullptr)
		{
			PyErr_SetString(GError, "generic host type factory produced no Python type");
			return nullptr;
		}
		HostType->TypeId = TypeId;
		return Py_BuildValue("(IOs#)", TypeId, HostType->PythonType,
		    HostType->Name.data(), static_cast<Py_ssize_t>(HostType->Name.size()));
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while instantiating Python generic host type");
		return nullptr;
	}

	void VS_INTEROP_CALL DestroyPythonHostObject(void* Payload)
	{
		if (Payload == nullptr || !Py_IsInitialized())
		{
			return;
		}
		const PyGILState_STATE Gil = PyGILState_Ensure();
		delete static_cast<FHostObjectPayload*>(Payload);
		PyGILState_Release(Gil);
	}

	uint8_t VS_INTEROP_CALL IsPythonHostObjectType(
	    void* const Payload,
	    void* const UserData,
	    const vs_interop_string_view ExpectedTypeName)
	{
		if (Payload == nullptr || !Py_IsInitialized() || (ExpectedTypeName.size != 0 && ExpectedTypeName.data == nullptr))
		{
			return 0;
		}
		(void)UserData;
		FPyGilGuard Gil;
		auto* const Object = static_cast<FHostObjectPayload*>(Payload);
		try
		{
			FPyOwnedRef Name(UnicodeFromView(ExpectedTypeName));
			if (Name.Get() == nullptr)
			{
				PyErr_Clear();
				return 0;
			}
			PyObject* WeakType = PyDict_GetItemWithError(Object->Registry->Types.Get(), Name.Get());
			if (WeakType == nullptr)
			{
				PyErr_Clear();
				return 0;
			}
			FPyOwnedRef Type;
			#if PY_VERSION_HEX >= 0x030D0000
			if (PyWeakref_GetRef(WeakType, &Type.Object) <= 0)
			{
				PyErr_Clear();
				return 0;
			}
			#else
			PyObject* Target = PyWeakref_GetObject(WeakType);
			if (Target == nullptr || Target == Py_None)
			{
				PyErr_Clear();
				return 0;
			}
			Py_INCREF(Target);
			Type.Object = Target;
			#endif
			const int Compatible = PyObject_IsInstance(Object->Object.Get(), Type.Get());
			if (Compatible < 0)
			{
				PyErr_Clear();
			}

			return Compatible > 0 ? 1 : 0;
		}
		catch (...)
		{
			PyErr_Clear();
		}
		return 0;
	}

	vs_interop_callback_result VS_INTEROP_CALL InvokePythonHostMember(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		FActiveMemoryCall MemoryScope(Context);
		auto* State = static_cast<FHostMemberState*>(UserData);
		const PyGILState_STATE Gil = PyGILState_Ensure();
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		vs_interop_callback_result CallbackResult = VS_INTEROP_CALLBACK_ERROR;
		try
		{
			if (State == nullptr || State->Owner == nullptr || State->HostType == nullptr || State->Callable == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python host member is no longer available");
				RecordHostCallbackException(Context, State != nullptr ? State->Owner : nullptr);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const bool IsConstructor = State->Kind == VS_INTEROP_HOST_CONSTRUCTOR;
			const bool HasReceiver = State->Kind == VS_INTEROP_HOST_METHOD || State->Kind == VS_INTEROP_HOST_PROPERTY_GETTER || State->Kind == VS_INTEROP_HOST_PROPERTY_SETTER || State->Kind == VS_INTEROP_HOST_FIELD;
			const std::uint64_t ContextArgumentCount =
			    vs_interop_call_context_argument_count(Context);
			const std::uint64_t ReceiverSlotCount = HasReceiver ? 1 : 0;
			if (ContextArgumentCount != State->ParameterTypes.size() + ReceiverSlotCount || State->ParameterTypes.size() > static_cast<std::size_t>(PY_SSIZE_T_MAX - 1))
			{
				PyErr_SetString(GCallbackError, "Python host member received an invalid argument list");
				RecordHostCallbackException(Context, State->Owner);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}

			const Py_ssize_t ReceiverCount = HasReceiver ? 1 : 0;
			PyObject* Arguments = PyTuple_New(
			    static_cast<Py_ssize_t>(State->ParameterTypes.size()) + ReceiverCount);
			if (Arguments == nullptr)
			{
				throw std::bad_alloc();
			}
			bool Failed = false;
			if (HasReceiver)
			{
				auto* Payload = static_cast<FHostObjectPayload*>(
				    vs_interop_call_context_host_this(Context));
				PyObject* Receiver = Payload != nullptr ? Payload->Object.Get() : nullptr;
				if (Receiver == nullptr)
				{
					PyErr_SetString(GCallbackError, "Python host method received a null instance");
					Failed = true;
				}
				else
				{
					Py_INCREF(Receiver);
					PyTuple_SET_ITEM(Arguments, 0, Receiver);
				}
			}
			for (std::size_t Index = 0; !Failed && Index < State->ParameterTypes.size(); ++Index)
			{
				const vs_interop_value* Value = nullptr;
				if (vs_interop_call_context_get_argument(
				        Context, static_cast<std::uint64_t>(Index) + ReceiverSlotCount, &Value) != VS_INTEROP_STATUS_OK)
				{
					PyErr_SetString(GError, "failed to read Python host member argument");
					Failed = true;
					break;
				}
				PyObject* Argument = CopyPythonHostArgument(Value);
				if (Argument == nullptr)
				{
					Failed = true;
					break;
				}
				PyTuple_SET_ITEM(Arguments,
				    static_cast<Py_ssize_t>(Index) + ReceiverCount, Argument);
			}

			PyObject* Result = Failed ? nullptr : PyObject_CallObject(State->Callable, Arguments);
			Py_DECREF(Arguments);
			if (Result == nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
				PyGILState_Release(Gil);
				return VS_INTEROP_CALLBACK_ERROR;
			}

			if (IsConstructor)
			{
				const int IsInstance = PyObject_IsInstance(Result, State->HostType->PythonType);
				if (IsInstance <= 0)
				{
					if (IsInstance == 0)
					{
						PyErr_SetString(GTypeMismatchError,
						    "Python host constructor returned an incompatible object");
					}
					Py_DECREF(Result);
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				const bool Stored = StorePythonHostObject(State->HostType, Result, OutReturn);
				Py_DECREF(Result);
				if (!Stored)
				{
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			else if (State->ReturnType == VS_INTEROP_NATIVE_VOID)
			{
				if (Result != Py_None)
				{
					Py_DECREF(Result);
					PyErr_SetString(GTypeMismatchError,
					    "a void Python host method must return None");
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				Py_DECREF(Result);
			}
			else
			{
				const bool Written = SetPythonHostCallbackReturn(
				    State->Owner, State->ReturnType, State->ReturnTypeName, Result, OutReturn);
				Py_DECREF(Result);
				if (!Written)
				{
					if (!PyErr_Occurred())
					{
						PyErr_SetString(GTypeMismatchError,
						    "unsupported Python host method return value");
					}
					RecordHostCallbackException(Context, State->Owner);
					PyGILState_Release(Gil);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			CallbackResult = VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
			}
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python host member callback failure");
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
			}
		}
		PyGILState_Release(Gil);
		return CallbackResult;
	}

	vs_interop_callback_result VS_INTEROP_CALL InvokePythonHostProtocol(
	    vs_interop_call_context* Context,
	    vs_interop_value* OutReturn,
	    void* UserData)
	{
		const FActiveMemoryCall MemoryCall(Context);
		auto* State = static_cast<FHostProtocolCallbackState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		try
		{
			if (State == nullptr || State->Owner == nullptr || State->HostType == nullptr || State->Callable == nullptr || Context == nullptr || OutReturn == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python host protocol is unavailable");
				RecordHostCallbackException(Context, State != nullptr ? State->Owner : nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const bool UsesReceiver = State->Kind == EPythonHostProtocolKind::Clone;
			const std::uint64_t ArgumentCount =
			    vs_interop_call_context_argument_count(Context);
			if (!UsesReceiver && ArgumentCount > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(GCallbackError, "Python host protocol received invalid arguments");
				RecordHostCallbackException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const Py_ssize_t PythonCount = UsesReceiver
			    ? 1
			    : static_cast<Py_ssize_t>(ArgumentCount);
			FPyOwnedRef Arguments(PyTuple_New(PythonCount));
			if (Arguments.Get() == nullptr)
			{
				throw std::bad_alloc();
			}
			if (UsesReceiver)
			{
				auto* Payload = static_cast<FHostObjectPayload*>(
				    vs_interop_call_context_host_this(Context));
				PyObject* Receiver = Payload != nullptr ? Payload->Object.Get() : nullptr;
				if (Receiver == nullptr)
				{
					PyErr_SetString(GTypeMismatchError,
					    "Python host clone received a null instance");
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				Py_INCREF(Receiver);
				PyTuple_SET_ITEM(Arguments.Get(), 0, Receiver);
			}
			for (std::uint64_t Index = 0; !UsesReceiver && Index < ArgumentCount; ++Index)
			{
				const vs_interop_value* Value = nullptr;
				if (vs_interop_call_context_get_argument(Context, Index, &Value) != VS_INTEROP_STATUS_OK)
				{
					PyErr_SetString(GError, "failed to read Python host protocol argument");
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				PyObject* Argument = CopyPythonHostArgument(Value);
				if (Argument == nullptr)
				{
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				PyTuple_SET_ITEM(Arguments.Get(), static_cast<Py_ssize_t>(Index), Argument);
			}
			FPyOwnedRef Result(PyObject_CallObject(State->Callable, Arguments.Get()));
			if (Result.Get() == nullptr)
			{
				RecordHostCallbackException(Context, State->Owner);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			if (State->Kind == EPythonHostProtocolKind::Clone || State->Kind == EPythonHostProtocolKind::SubclassFactory)
			{
				const int IsInstance = PyObject_IsInstance(
				    Result.Get(), State->HostType->PythonType);
				if (IsInstance <= 0)
				{
					if (IsInstance == 0)
					{
						PyErr_SetString(GTypeMismatchError,
						    "Python host protocol returned an incompatible object");
					}
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				if (!StorePythonHostObject(State->HostType, Result.Get(), OutReturn))
				{
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			else if (State->Kind == EPythonHostProtocolKind::Equals)
			{
				const int Truth = PyObject_IsTrue(Result.Get());
				if (Truth < 0 || vs_interop_value_set_bool(OutReturn, static_cast<std::uint8_t>(Truth != 0)) != VS_INTEROP_STATUS_OK)
				{
					if (!PyErr_Occurred())
					{
						PyErr_SetString(GTypeMismatchError,
						    "Python host equals result was rejected");
					}
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			else
			{
				const unsigned long long Hash = PyLong_AsUnsignedLongLongMask(Result.Get());
				if (PyErr_Occurred() || vs_interop_value_set_uint64(OutReturn, Hash) != VS_INTEROP_STATUS_OK)
				{
					if (!PyErr_Occurred())
					{
						PyErr_SetString(GTypeMismatchError,
						    "Python host hash result was rejected");
					}
					RecordHostCallbackException(Context, State->Owner);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			return VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			RecordHostCallbackException(Context, State != nullptr ? State->Owner : nullptr);
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python host protocol failure");
			RecordHostCallbackException(Context, State != nullptr ? State->Owner : nullptr);
		}
		return VS_INTEROP_CALLBACK_ERROR;
	}

	PyObject* SessionRegisterHostClass(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* PythonType = nullptr;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		unsigned int Kind = VS_INTEROP_HOST_CLASS;
		int Inheritable = 0;
		static const char* Names[] = {
		    "name", "python_type", "documentation", "kind", "inheritable", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#O|s#Ip:register_host_class",
		        const_cast<char**>(Names), &Name, &NameSize, &PythonType,
		        &Documentation, &DocumentationSize, &Kind, &Inheritable))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		if (Kind != VS_INTEROP_HOST_STRUCT && Kind != VS_INTEROP_HOST_CLASS)
		{
			PyErr_SetString(PyExc_ValueError,
			    "Python host types must be struct or class types");
			return nullptr;
		}
		if (!PyType_Check(PythonType))
		{
			PyErr_SetString(PyExc_TypeError, "python_type must be a Python class");
			return nullptr;
		}
		std::unique_ptr<FHostTypeState> State(new (std::nothrow) FHostTypeState);
		if (!State)
		{
			return PyErr_NoMemory();
		}
		State->Owner = Self->Callbacks;
		State->Name.assign(Name, static_cast<std::size_t>(NameSize));
		Py_INCREF(PythonType);
		State->PythonType = PythonType;
		Self->Callbacks->HostTypes.push_back(State.get());
		vs_interop_host_type_desc Descriptor{};
		Descriptor.struct_size = sizeof(Descriptor);
		Descriptor.name = View(Name, NameSize);
		Descriptor.kind = Kind;
		Descriptor.documentation = View(Documentation, DocumentationSize);
		Descriptor.inheritable = static_cast<std::uint8_t>(Inheritable != 0);
		const vs_interop_status Status = vs_interop_session_register_host_type(
		    Self->Handle, &Descriptor, &State->TypeId);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			Self->Callbacks->HostTypes.pop_back();
			return RaiseStatus(Self, Status, "register Python host class");
		}
		const std::uint32_t TypeId = State->TypeId;
		(void)State.release();
		if (Self->Callbacks->HostTypeRegistry && !RefreshHostTypeRegistry(Self->Callbacks))
		{
			return nullptr;
		}
		return PyLong_FromUnsignedLong(TypeId);
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return nullptr;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python host class");
		return nullptr;
	}

	PyObject* SessionRegisterHostMember(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		unsigned int TypeId = 0;
		unsigned int Kind = 0;
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* Callable = nullptr;
		const char* ReturnTypeText = nullptr;
		Py_ssize_t ReturnTypeSize = 0;
		PyObject* ParameterSpecs = nullptr;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		static const char* Names[] = {"type_id", "kind", "name", "callback",
		    "return_type", "parameters", "documentation", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "IIs#Os#O|s#:register_host_member",
		        const_cast<char**>(Names), &TypeId, &Kind, &Name, &NameSize, &Callable,
		        &ReturnTypeText, &ReturnTypeSize, &ParameterSpecs,
		        &Documentation, &DocumentationSize))
		{
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		if (Kind != VS_INTEROP_HOST_CONSTRUCTOR && Kind != VS_INTEROP_HOST_METHOD && Kind != VS_INTEROP_HOST_STATIC_METHOD && Kind != VS_INTEROP_HOST_PROPERTY_GETTER && Kind != VS_INTEROP_HOST_PROPERTY_SETTER && Kind != VS_INTEROP_HOST_FIELD)
		{
			PyErr_SetString(PyExc_ValueError, "unsupported Python host member kind");
			return nullptr;
		}
		if (!PyCallable_Check(Callable))
		{
			PyErr_SetString(PyExc_TypeError, "host member callback must be callable");
			return nullptr;
		}
		FHostTypeState* HostType = nullptr;
		for (FHostTypeState* Candidate : Self->Callbacks->HostTypes)
		{
			if (Candidate->TypeId == TypeId)
			{
				HostType = Candidate;
				break;
			}
		}
		if (HostType == nullptr)
		{
			PyErr_SetString(PyExc_ValueError, "unknown Python host class id");
			return nullptr;
		}
		FNativeTypeStorage ReturnTypeStorage;
		if (Kind == VS_INTEROP_HOST_CONSTRUCTOR)
		{
			ReturnTypeStorage.Descriptor.type = VS_INTEROP_NATIVE_HOST_OBJECT;
			ReturnTypeStorage.Name = HostType->Name;
		}
		else if (!ParseNativeTypeStorage(
		             {ReturnTypeText, static_cast<std::size_t>(ReturnTypeSize)}, ReturnTypeStorage))
		{
			PyErr_SetString(PyExc_ValueError, "unsupported host method return type");
			return nullptr;
		}
		const vs_interop_native_type ReturnType = ReturnTypeStorage.Descriptor.type;

		FPyOwnedRef Specs(SnapshotSequence(ParameterSpecs, "parameters must be a sequence"));
		if (Specs.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t ParameterCount = PySequence_Fast_GET_SIZE(Specs.Get());
		if (ParameterCount > UINT8_MAX)
		{
			PyErr_SetString(PyExc_ValueError, "host members support at most 255 parameters");
			return nullptr;
		}
		if ((Kind == VS_INTEROP_HOST_PROPERTY_GETTER || Kind == VS_INTEROP_HOST_FIELD) && (ReturnType == VS_INTEROP_NATIVE_VOID || ParameterCount != 0))
		{
			PyErr_SetString(PyExc_ValueError,
			    "Python host field/getter requires a non-void return type and no parameters");
			return nullptr;
		}
		if (Kind == VS_INTEROP_HOST_PROPERTY_SETTER && (ReturnType != VS_INTEROP_NATIVE_VOID || ParameterCount != 1))
		{
			PyErr_SetString(PyExc_ValueError,
			    "Python host property setter requires void and exactly one parameter");
			return nullptr;
		}
		std::unique_ptr<FHostMemberState> State(new (std::nothrow) FHostMemberState);
		if (!State)
		{
			return PyErr_NoMemory();
		}
		State->Owner = Self->Callbacks;
		State->HostType = HostType;
		State->Kind = Kind;
		State->ReturnType = ReturnType;
		State->ReturnTypeName = !ReturnTypeStorage.Name.empty()
		    ? ReturnTypeStorage.Name
		    : std::string(ReturnTypeText, static_cast<std::size_t>(ReturnTypeSize));
		State->ParameterTypes.reserve(static_cast<std::size_t>(ParameterCount));
		std::vector<std::string> ParameterNames;
		std::vector<std::string> ParameterDocs;
		std::vector<FNativeTypeStorage> ParameterTypeStorage;
		ParameterNames.reserve(static_cast<std::size_t>(ParameterCount));
		ParameterDocs.reserve(static_cast<std::size_t>(ParameterCount));
		ParameterTypeStorage.reserve(static_cast<std::size_t>(ParameterCount));
		bool Valid = true;
		for (Py_ssize_t Index = 0; Index < ParameterCount; ++Index)
		{
			PyObject* Spec = PySequence_Fast_GET_ITEM(Specs.Get(), Index);
			if (!PyTuple_Check(Spec) || PyTuple_GET_SIZE(Spec) != 4)
			{
				PyErr_SetString(PyExc_TypeError,
				    "each host parameter must be (type, flags, name, documentation)");
				Valid = false;
				break;
			}
			Py_ssize_t TypeSize = 0;
			const char* TypeText = PyUnicode_AsUTF8AndSize(PyTuple_GET_ITEM(Spec, 0), &TypeSize);
			const unsigned long Flags = PyLong_AsUnsignedLong(PyTuple_GET_ITEM(Spec, 1));
			Py_ssize_t ParameterNameSize = 0;
			const char* ParameterName = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Spec, 2), &ParameterNameSize);
			Py_ssize_t ParameterDocSize = 0;
			const char* ParameterDoc = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Spec, 3), &ParameterDocSize);
			FNativeTypeStorage ParameterTypeStorageEntry;
			if (TypeText == nullptr || ParameterName == nullptr || ParameterDoc == nullptr || PyErr_Occurred() || Flags != 0 || !ParseNativeTypeStorage({TypeText, static_cast<std::size_t>(TypeSize)}, ParameterTypeStorageEntry) || ParameterTypeStorageEntry.Descriptor.type == VS_INTEROP_NATIVE_VOID)
			{
				if (!PyErr_Occurred())
				{
					PyErr_SetString(PyExc_ValueError,
					    "invalid Python host class parameter type");
				}
				Valid = false;
				break;
			}
			State->ParameterTypes.push_back(ParameterTypeStorageEntry.Descriptor.type);
			ParameterTypeStorage.push_back(std::move(ParameterTypeStorageEntry));
			ParameterNames.emplace_back(ParameterName, static_cast<std::size_t>(ParameterNameSize));
			ParameterDocs.emplace_back(ParameterDoc, static_cast<std::size_t>(ParameterDocSize));
		}
		if (!Valid)
		{
			return nullptr;
		}

		std::vector<vs_interop_native_param_desc> Parameters(
		    static_cast<std::size_t>(ParameterCount));
		std::vector<vs_interop_string_view> ParameterNameViews;
		std::vector<vs_interop_string_view> ParameterDocViews;
		ReturnTypeStorage.Finalize();
		for (auto& Type : ParameterTypeStorage)
		{
			Type.Finalize();
		}
		for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		{
			Parameters[Index].struct_size = sizeof(vs_interop_native_param_desc);
			Parameters[Index].type = ParameterTypeStorage[Index].Descriptor;
			ParameterNameViews.push_back(View(ParameterNames[Index]));
			ParameterDocViews.push_back(View(ParameterDocs[Index]));
		}
		vs_interop_host_member_desc Descriptor{};
		Descriptor.struct_size = sizeof(Descriptor);
		Descriptor.kind = Kind;
		Descriptor.name = View(Name, NameSize);
		Descriptor.return_type = ReturnTypeStorage.Descriptor;
		Descriptor.parameters = Parameters.data();
		Descriptor.parameter_count = static_cast<std::uint64_t>(Parameters.size());
		Descriptor.callback = &InvokePythonHostMember;
		Descriptor.user_data = State.get();
		Descriptor.documentation = View(Documentation, DocumentationSize);
		Descriptor.parameter_names = {
		    ParameterNameViews.data(), static_cast<std::uint64_t>(ParameterNameViews.size())};
		Descriptor.parameter_documentation = {
		    ParameterDocViews.data(), static_cast<std::uint64_t>(ParameterDocViews.size())};
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		Py_INCREF(Callable);
		State->Callable = Callable;
		Self->Callbacks->HostMembers.push_back(State.get());
		const vs_interop_status Status = vs_interop_session_register_host_member(
		    Self->Handle, TypeId, &Descriptor);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			Self->Callbacks->HostMembers.pop_back();
			return RaiseStatus(Self, Status, "register Python host member");
		}
		(void)State.release();
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		PyErr_NoMemory();
		return nullptr;
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python host member");
		return nullptr;
	}

	void RecordSerializerException(FCallbackState* State, const char* Fallback,
	    vs_interop_external_serialize_context* SerializeContext,
	    vs_interop_external_deserialize_context* DeserializeContext)
	{
		if (!PyErr_Occurred())
		{
			PyErr_SetString(GCallbackError, Fallback);
		}
		StoreCallbackException(State);
		const vs_interop_string_view Error = {
		    Fallback, static_cast<std::uint64_t>(std::strlen(Fallback))};
		if (SerializeContext != nullptr)
		{
			vs_interop_external_serialize_context_set_error(SerializeContext, Error);
		}
		if (DeserializeContext != nullptr)
		{
			vs_interop_external_deserialize_context_set_error(DeserializeContext, Error);
		}
	}

	vs_interop_callback_result VS_INTEROP_CALL InvokePythonSerializer(
	    vs_interop_external_serialize_context* Context, void* UserData)
	{
		auto* State = static_cast<FSerializerState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		vs_interop_callback_result CallbackResult = VS_INTEROP_CALLBACK_ERROR;
		try
		{
			if (State == nullptr || State->Owner == nullptr || State->Serialize == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python external serializer is unavailable");
				RecordSerializerException(State != nullptr ? State->Owner : nullptr,
				    "Python external serializer is unavailable", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const std::uint64_t FieldCount =
			    vs_interop_external_serialize_context_field_count(Context);
			if (FieldCount > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(PyExc_OverflowError,
				    "too many object fields for Python serialization");
				RecordSerializerException(State->Owner,
				    "Python external serializer could not inspect fields", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FPyOwnedRef TypeName(UnicodeFromView(
			    vs_interop_external_serialize_context_type_name(Context)));
			FPyOwnedRef MutationVersion(PyLong_FromUnsignedLongLong(
			    vs_interop_external_serialize_context_mutation_version(Context)));
			FPyOwnedRef Fields(PyDict_New());
			if (TypeName.Get() == nullptr || MutationVersion.Get() == nullptr || Fields.Get() == nullptr)
			{
				throw std::bad_alloc();
			}
			for (std::uint64_t Index = 0; Index < FieldCount; ++Index)
			{
				vs_interop_string_view Name{};
				const vs_interop_value* Value = nullptr;
				if (vs_interop_external_serialize_context_get_field(Context, Index,
				        &Name, nullptr, nullptr, nullptr, &Value) != VS_INTEROP_STATUS_OK)
				{
					PyErr_SetString(GError, "failed to inspect serialized object field");
					RecordSerializerException(State->Owner,
					    "Python external serializer could not inspect fields", Context, nullptr);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				FPyOwnedRef FieldName(UnicodeFromView(Name));
				FPyOwnedRef FieldValue(CopyCallbackArgument(Value));
				if (FieldName.Get() == nullptr || FieldValue.Get() == nullptr || PyDict_SetItem(Fields.Get(), FieldName.Get(), FieldValue.Get()) != 0)
				{
					RecordSerializerException(State->Owner,
					    "Python external serializer could not copy fields", Context, nullptr);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			FPyOwnedRef Result(PyObject_CallFunctionObjArgs(State->Serialize,
			    TypeName.Get(), MutationVersion.Get(), Fields.Get(), nullptr));
			if (Result.Get() == nullptr)
			{
				RecordSerializerException(State->Owner,
				    "Python external serializer callback failed", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			Py_buffer Bytes{};
			if (PyObject_GetBuffer(Result.Get(), &Bytes, PyBUF_CONTIG_RO) != 0)
			{
				RecordSerializerException(State->Owner,
				    "Python external serializer must return bytes", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			if (Bytes.len < 0 || (Bytes.len != 0 && Bytes.buf == nullptr))
			{
				PyBuffer_Release(&Bytes);
				PyErr_SetString(PyExc_BufferError,
				    "Python external serializer returned an invalid buffer");
				RecordSerializerException(State->Owner,
				    "Python external serializer returned an invalid buffer", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const vs_interop_status Status =
			    vs_interop_external_serialize_context_set_bytes(Context,
			        {static_cast<const std::uint8_t*>(Bytes.buf),
			            static_cast<std::uint64_t>(Bytes.len)});
			PyBuffer_Release(&Bytes);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				PyErr_SetString(GError, "failed to store Python serializer output");
				RecordSerializerException(State->Owner,
				    "Python external serializer output was rejected", Context, nullptr);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			CallbackResult = VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordSerializerException(State->Owner,
				    "Python external serializer ran out of memory", Context, nullptr);
			}
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python serializer failure");
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordSerializerException(State->Owner,
				    "unexpected Python external serializer failure", Context, nullptr);
			}
		}
		return CallbackResult;
	}

	vs_interop_callback_result VS_INTEROP_CALL InvokePythonDeserializer(
	    vs_interop_external_deserialize_context* Context, void* UserData)
	{
		auto* State = static_cast<FSerializerState*>(UserData);
		FPyGilGuard Gil;
		const FCallbackActivity Activity(State != nullptr ? State->Owner : nullptr);
		vs_interop_callback_result CallbackResult = VS_INTEROP_CALLBACK_ERROR;
		try
		{
			if (State == nullptr || State->Owner == nullptr || State->Deserialize == nullptr)
			{
				PyErr_SetString(GCallbackError, "Python external deserializer is unavailable");
				RecordSerializerException(State != nullptr ? State->Owner : nullptr,
				    "Python external deserializer is unavailable", nullptr, Context);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const vs_interop_byte_view Source =
			    vs_interop_external_deserialize_context_bytes(Context);
			if (Source.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX))
			{
				PyErr_SetString(PyExc_OverflowError,
				    "serialized object data is too large for Python");
				RecordSerializerException(State->Owner,
				    "Python external deserializer input is too large", nullptr, Context);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			if (!IsValidByteView(Source))
			{
				PyErr_SetString(GError, "serialized object data contains an invalid byte view");
				RecordSerializerException(State->Owner,
				    "Python external deserializer input is invalid", nullptr, Context);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FPyOwnedRef Bytes(PyBytes_FromStringAndSize(
			    ByteData(Source),
			    static_cast<Py_ssize_t>(Source.size)));
			FPyOwnedRef Version(PyLong_FromUnsignedLong(
			    vs_interop_external_deserialize_context_encoded_version(Context)));
			if (Bytes.Get() == nullptr || Version.Get() == nullptr)
			{
				throw std::bad_alloc();
			}
			FPyOwnedRef Result(PyObject_CallFunctionObjArgs(
			    State->Deserialize, Bytes.Get(), Version.Get(), nullptr));
			if (Result.Get() == nullptr)
			{
				RecordSerializerException(State->Owner,
				    "Python external deserializer callback failed", nullptr, Context);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			FPyOwnedRef Items(CheckedMappingItems(Result.Get()));
			if (Items.Get() == nullptr)
			{
				RecordSerializerException(State->Owner,
				    "Python external deserializer must return a mapping", nullptr, Context);
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const Py_ssize_t Count = PyList_GET_SIZE(Items.Get());
			for (Py_ssize_t Index = 0; Index < Count; ++Index)
			{
				PyObject* Item = PyList_GET_ITEM(Items.Get(), Index);
				Py_ssize_t NameSize = 0;
				const char* Name = PyUnicode_AsUTF8AndSize(
				    PyTuple_GET_ITEM(Item, 0), &NameSize);
				if (Name == nullptr)
				{
					RecordSerializerException(State->Owner,
					    "Python deserialized field names must be strings", nullptr, Context);
					return VS_INTEROP_CALLBACK_ERROR;
				}
				PyObject* ValueObject = PyTuple_GET_ITEM(Item, 1);
				vs_interop_value* Owned = nullptr;
				const vs_interop_value* Value = nullptr;
				if (PyObject_TypeCheck(ValueObject, &GValueType))
				{
					Value = reinterpret_cast<FPyValue*>(ValueObject)->Handle;
				}
				else
				{
					if (vs_interop_value_create(&Owned) != VS_INTEROP_STATUS_OK || !SetValueFromPython(Owned, ValueObject, nullptr, nullptr))
					{
						vs_interop_value_destroy(Owned);
						RecordSerializerException(State->Owner,
						    "Python deserialized field value is unsupported", nullptr, Context);
						return VS_INTEROP_CALLBACK_ERROR;
					}
					Value = Owned;
				}
				const vs_interop_status Status =
				    vs_interop_external_deserialize_context_add_field(
				        Context, View(Name, NameSize), Value);
				vs_interop_value_destroy(Owned);
				if (Status != VS_INTEROP_STATUS_OK)
				{
					PyErr_SetString(GTypeMismatchError,
					    "deserialized field assignment was rejected");
					RecordSerializerException(State->Owner,
					    "Python deserialized field assignment was rejected", nullptr, Context);
					return VS_INTEROP_CALLBACK_ERROR;
				}
			}
			CallbackResult = VS_INTEROP_CALLBACK_OK;
		}
		catch (const std::bad_alloc&)
		{
			PyErr_NoMemory();
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordSerializerException(State->Owner,
				    "Python external deserializer ran out of memory", nullptr, Context);
			}
		}
		catch (...)
		{
			PyErr_SetString(GCallbackError, "unexpected Python deserializer failure");
			if (State != nullptr && State->Owner != nullptr)
			{
				RecordSerializerException(State->Owner,
				    "unexpected Python external deserializer failure", nullptr, Context);
			}
		}
		return CallbackResult;
	}

	PyObject* SessionRegisterSerializer(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* TypeName = nullptr;
		Py_ssize_t TypeNameSize = 0;
		unsigned int Version = 0;
		PyObject* Serialize = nullptr;
		PyObject* Deserialize = nullptr;
		static const char* Names[] = {
		    "type_name", "version", "serialize", "deserialize", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords, "s#IOO:register_serializer",
		        const_cast<char**>(Names), &TypeName, &TypeNameSize, &Version,
		        &Serialize, &Deserialize) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		if (!PyCallable_Check(Serialize) || !PyCallable_Check(Deserialize))
		{
			PyErr_SetString(PyExc_TypeError,
			    "serialize and deserialize callbacks must be callable");
			return nullptr;
		}
		std::unique_ptr<FSerializerState> State(new (std::nothrow) FSerializerState);
		if (!State)
		{
			return PyErr_NoMemory();
		}
		State->Owner = Self->Callbacks;
		Py_INCREF(Serialize);
		State->Serialize = Serialize;
		Py_INCREF(Deserialize);
		State->Deserialize = Deserialize;
		Self->Callbacks->Serializers.push_back(State.get());
		const vs_interop_status Status = vs_interop_session_register_external_serializer(
		    Self->Handle, View(TypeName, TypeNameSize), Version,
		    &InvokePythonSerializer, &InvokePythonDeserializer, State.get());
		if (Status != VS_INTEROP_STATUS_OK)
		{
			Self->Callbacks->Serializers.pop_back();
			return RaiseStatus(Self, Status, "register Python external serializer");
		}
		(void)State.release();
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected serializer registration error");
		return nullptr;
	}

	PyObject* SessionSerializeObject(FPySession* Self, PyObject* Args)
	{
		PyObject* Object = nullptr;
		if (!PyArg_ParseTuple(Args, "O!:serialize_object", &GValueType, &Object))
		{
			return nullptr;
		}
		if (Object == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "serialize_object parsed no object value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_buffer* Buffer = nullptr;
		std::uint32_t Version = 0;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_serialize_object(Self->Handle,
		        reinterpret_cast<FPyValue*>(Object)->Handle, &Buffer, &Version);
		Py_END_ALLOW_THREADS if (RaiseCallbackError(Self))
		{
			vs_interop_buffer_destroy(Buffer);
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_buffer_destroy(Buffer);
			return RaiseStatus(Self, Status, "serialize object");
		}
		if (Buffer == nullptr)
		{
			PyErr_SetString(GError, "object serializer returned no data");
			return nullptr;
		}
		const vs_interop_byte_view Bytes = vs_interop_buffer_view(Buffer);
		PyObject* Data = Bytes.size <= static_cast<std::uint64_t>(PY_SSIZE_T_MAX) && IsValidByteView(Bytes)
		    ? PyBytes_FromStringAndSize(ByteData(Bytes),
		          static_cast<Py_ssize_t>(Bytes.size))
		    : nullptr;
		vs_interop_buffer_destroy(Buffer);
		if (Data == nullptr)
		{
			if (!PyErr_Occurred())
			{
				PyErr_SetString(Bytes.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX)
				        ? PyExc_OverflowError
				        : GError,
				    Bytes.size > static_cast<std::uint64_t>(PY_SSIZE_T_MAX)
				        ? "serialized object is too large for Python"
				        : "object serializer returned an invalid byte view");
			}
			return nullptr;
		}
		PyObject* Result = Py_BuildValue("(OI)", Data, Version);
		Py_DECREF(Data);
		return Result;
	}

	PyObject* SessionDeserializeObject(FPySession* Self, PyObject* Args)
	{
		PyObject* Object = nullptr;
		PyObject* Data = nullptr;
		unsigned int Version = 0;
		if (!PyArg_ParseTuple(Args, "O!OI:deserialize_object",
		        &GValueType, &Object, &Data, &Version))
		{
			return nullptr;
		}
		if (Object == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "deserialize_object parsed no object value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		Py_buffer Buffer{};
		if (PyObject_GetBuffer(Data, &Buffer, PyBUF_CONTIG_RO) != 0)
		{
			return nullptr;
		}
		if (Buffer.len < 0 || (Buffer.len != 0 && Buffer.buf == nullptr))
		{
			PyBuffer_Release(&Buffer);
			PyErr_SetString(PyExc_BufferError, "serialized object data exported an invalid buffer");
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_deserialize_object(Self->Handle,
		        reinterpret_cast<FPyValue*>(Object)->Handle,
		        {static_cast<const std::uint8_t*>(Buffer.buf),
		            static_cast<std::uint64_t>(Buffer.len)},
		        Version);
		Py_END_ALLOW_THREADS
		    PyBuffer_Release(&Buffer);
		if (RaiseCallbackError(Self))
		{
			return nullptr;
		}
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "deserialize object");
		}
		Py_RETURN_NONE;
	}

	PyObject* SessionApplyClassDefaults(FPySession* Self, PyObject* Args)
	{
		PyObject* Object = nullptr;
		if (!PyArg_ParseTuple(Args, "O!:apply_class_defaults", &GValueType, &Object))
		{
			return nullptr;
		}
		if (Object == nullptr)
		{
			PyErr_SetString(PyExc_SystemError, "apply_class_defaults parsed no object value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		vs_interop_run_result Result{sizeof(Result)};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_apply_class_defaults(Self->Handle,
		        reinterpret_cast<FPyValue*>(Object)->Handle, &Result);
		Py_END_ALLOW_THREADS return FinishRunOperation(Self, Status, Result, "apply class defaults");
	}

	PyObject* SessionEnumerateObjectReferences(FPySession* Self, PyObject* Args)
	{
		PyObject* Object = nullptr;
		if (!PyArg_ParseTuple(Args, "O!:enumerate_object_references", &GValueType, &Object))
		{
			return nullptr;
		}
		if (Object == nullptr)
		{
			PyErr_SetString(PyExc_SystemError,
			    "enumerate_object_references parsed no object value");
			return nullptr;
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		FPyOwnedRef References(PyList_New(0));
		if (References.Get() == nullptr)
		{
			return nullptr;
		}
		const FCallbackActivity Activity(Self->Callbacks);
		const FCallbackErrorScope ErrorScope(Self);
		FReferenceVisitorState Visitor{Self->Callbacks, References.Get()};
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_session_enumerate_object_references(Self->Handle,
		        reinterpret_cast<FPyValue*>(Object)->Handle,
		        &VisitObjectReference, &Visitor);
		Py_END_ALLOW_THREADS if (RaiseCallbackError(Self)) return nullptr;
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "enumerate object references");
		}
		return References.Release();
	}

	PyObject* SessionSetMutationCallback(FPySession* Self, PyObject* Args)
	{
		PyObject* Callback = nullptr;
		if (!PyArg_ParseTuple(Args, "O:set_mutation_callback", &Callback) || !CheckSession(Self))
		{
			return nullptr;
		}
		if (Callback != Py_None && !PyCallable_Check(Callback))
		{
			PyErr_SetString(PyExc_TypeError,
			    "mutation callback must be callable or None");
			return nullptr;
		}
		const vs_interop_status Status = vs_interop_session_set_mutation_callback(
		    Self->Handle, Callback != Py_None ? &OnMutation : nullptr,
		    Callback != Py_None ? Self->Callbacks : nullptr);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set mutation callback");
		}
		PyObject* Stored = Callback != Py_None ? Callback : nullptr;
		Py_XINCREF(Stored);
		Py_XSETREF(Self->Callbacks->MutationCallback, Stored);
		Py_RETURN_NONE;
	}

	PyObject* SessionRegisterHostEnum(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		const char* Name = nullptr;
		Py_ssize_t NameSize = 0;
		PyObject* PythonType = nullptr;
		PyObject* MembersObject = nullptr;
		const char* Documentation = "";
		Py_ssize_t DocumentationSize = 0;
		const char* UnderlyingType = "int64";
		Py_ssize_t UnderlyingTypeSize = 5;
		static const char* Names[] = {"name", "python_type", "members",
		    "documentation", "underlying_type", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords,
		        "s#OO|s#s#:register_host_enum", const_cast<char**>(Names),
		        &Name, &NameSize, &PythonType, &MembersObject,
		        &Documentation, &DocumentationSize, &UnderlyingType, &UnderlyingTypeSize) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		if (!PyType_Check(PythonType))
		{
			PyErr_SetString(PyExc_TypeError, "python_type must be a Python enum class");
			return nullptr;
		}
		FPyOwnedRef Members(SnapshotSequence(MembersObject,
		    "members must be a sequence of (name, value) pairs"));
		if (Members.Get() == nullptr || !CheckSession(Self))
		{
			return nullptr;
		}
		const Py_ssize_t Count = PySequence_Fast_GET_SIZE(Members.Get());
		if (Count == 0)
		{
			PyErr_SetString(PyExc_ValueError, "a Python host enum requires members");
			return nullptr;
		}
		std::vector<std::string> MemberNames;
		std::vector<std::int64_t> MemberValues;
		MemberNames.reserve(static_cast<std::size_t>(Count));
		MemberValues.reserve(static_cast<std::size_t>(Count));
		for (Py_ssize_t Index = 0; Index < Count; ++Index)
		{
			PyObject* Item = PySequence_Fast_GET_ITEM(Members.Get(), Index);
			if (!PyTuple_Check(Item) || PyTuple_GET_SIZE(Item) != 2)
			{
				PyErr_SetString(PyExc_TypeError,
				    "each Python host enum member must be (name, value)");
				return nullptr;
			}
			Py_ssize_t MemberNameSize = 0;
			const char* MemberName = PyUnicode_AsUTF8AndSize(
			    PyTuple_GET_ITEM(Item, 0), &MemberNameSize);
			const long long MemberValue = PyLong_AsLongLong(PyTuple_GET_ITEM(Item, 1));
			if (MemberName == nullptr || PyErr_Occurred())
			{
				return nullptr;
			}
			MemberNames.emplace_back(MemberName, static_cast<std::size_t>(MemberNameSize));
			MemberValues.push_back(static_cast<std::int64_t>(MemberValue));
		}
		if (!CheckSession(Self))
		{
			return nullptr;
		}
		auto State = std::make_unique<FHostTypeState>();
		State->Owner = Self->Callbacks;
		State->Name.assign(Name, static_cast<std::size_t>(NameSize));
		Py_INCREF(PythonType);
		State->PythonType = PythonType;
		vs_interop_host_type_desc Descriptor{};
		Descriptor.struct_size = sizeof(Descriptor);
		Descriptor.name = View(State->Name);
		Descriptor.kind = VS_INTEROP_HOST_ENUM;
		Descriptor.documentation = View(Documentation, DocumentationSize);
		Descriptor.scoped_enum = 1;
		Descriptor.enum_underlying_type_name = View(UnderlyingType, UnderlyingTypeSize);
		vs_interop_status Status = vs_interop_session_register_host_type(
		    Self->Handle, &Descriptor, &State->TypeId);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "register Python host enum");
		}
		for (std::size_t Index = 0; Index < MemberNames.size(); ++Index)
		{
			vs_interop_host_member_desc Member{};
			Member.struct_size = sizeof(Member);
			Member.kind = VS_INTEROP_HOST_ENUM_MEMBER;
			Member.name = View(MemberNames[Index]);
			Member.return_type.struct_size = sizeof(Member.return_type);
			Member.return_type.type = VS_INTEROP_NATIVE_VOID;
			Member.enum_value = MemberValues[Index];
			Status = vs_interop_session_register_host_member(
			    Self->Handle, State->TypeId, &Member);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				return RaiseStatus(Self, Status, "register Python host enum member");
			}
		}
		const std::uint32_t TypeId = State->TypeId;
		Self->Callbacks->HostTypes.push_back(State.release());
		return PyLong_FromUnsignedLong(TypeId);
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python host enum");
		return nullptr;
	}

	void VS_INTEROP_CALL ReleaseProtocolUserData(void* UserData)
	{
		if (!Py_IsInitialized()) return;
		FPyGilGuard Gil;
		DeleteCallbackState(static_cast<FHostProtocolCallbackState*>(UserData)->Owner);
	}

	PyObject* SessionRegisterHostProtocol(
	    FPySession* Self, PyObject* Args, PyObject* Keywords)
	try
	{
		unsigned int TypeId = 0;
		PyObject* Clone = Py_None;
		PyObject* Equals = Py_None;
		PyObject* Hash = Py_None;
		PyObject* SubclassFactory = Py_None;
		static const char* Names[] = {
		    "type_id", "clone", "equals", "hash", "subclass_factory", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords,
		        "I|OOOO:register_host_protocol", const_cast<char**>(Names), &TypeId,
		        &Clone, &Equals, &Hash, &SubclassFactory) ||
		    !CheckSession(Self))
		{
			return nullptr;
		}
		if (Self->Callbacks->ActiveOperations.load(std::memory_order_relaxed) != 0)
		{
			PyErr_SetString(GError,
			    "VeroScript host protocols cannot be registered during an active operation or callback");
			return nullptr;
		}
		// Python allocation can run cyclic GC and arbitrary finalizers. Keep the
		// type and callback graph alive until registration and cleanup complete.
		FCallbackActivity Activity(Self->Callbacks);
		PyObject* Callbacks[] = {Clone, Equals, Hash, SubclassFactory};
		for (PyObject* Callback : Callbacks)
		{
			if (Callback != Py_None && !PyCallable_Check(Callback))
			{
				PyErr_SetString(PyExc_TypeError,
				    "host protocol callbacks must be callable or None");
				return nullptr;
			}
		}
		if (Clone == Py_None && Equals == Py_None && Hash == Py_None && SubclassFactory == Py_None)
		{
			PyErr_SetString(PyExc_ValueError,
			    "at least one host protocol callback is required");
			return nullptr;
		}
		FHostTypeState* HostType = nullptr;
		for (FHostTypeState* Candidate : Self->Callbacks->HostTypes)
		{
			if (Candidate->TypeId == TypeId)
			{
				HostType = Candidate;
				break;
			}
		}
		if (HostType == nullptr)
		{
			PyErr_SetString(PyExc_ValueError, "unknown Python host type id");
			return nullptr;
		}
		std::vector<std::unique_ptr<FHostProtocolCallbackState>> States;
		States.reserve(4);
		auto Add = [&](PyObject* Callback, const EPythonHostProtocolKind Kind,
		               vs_interop_native_callback& Native, void*& User)
		{
			if (Callback == Py_None)
			{
				return;
			}
			auto State = std::make_unique<FHostProtocolCallbackState>();
			State->Owner = Self->Callbacks;
			State->HostType = HostType;
			State->Kind = Kind;
			Py_INCREF(Callback);
			State->Callable = Callback;
			Native = &InvokePythonHostProtocol;
			User = State.get();
			States.push_back(std::move(State));
		};
		vs_interop_host_type_protocol Protocol{};
		Protocol.struct_size = sizeof(Protocol);
		Add(Clone, EPythonHostProtocolKind::Clone,
		    Protocol.clone_callback, Protocol.clone_user_data);
		Add(Equals, EPythonHostProtocolKind::Equals,
		    Protocol.equals_callback, Protocol.equals_user_data);
		Add(Hash, EPythonHostProtocolKind::Hash,
		    Protocol.hash_callback, Protocol.hash_user_data);
		Add(SubclassFactory, EPythonHostProtocolKind::SubclassFactory,
		    Protocol.subclass_factory_callback, Protocol.subclass_factory_user_data);
		if (!EnsureProtocolCallbackList(HostType)) return nullptr;
		FPyOwnedRef Anchors(PyTuple_New(static_cast<Py_ssize_t>(States.size())));
		if (Anchors.Get() == nullptr) return nullptr;
		for (std::size_t Index = 0; Index < States.size(); ++Index)
		{
			Py_INCREF(States[Index]->Callable);
			PyTuple_SET_ITEM(Anchors.Get(), static_cast<Py_ssize_t>(Index), States[Index]->Callable);
		}
		// GC during the allocations above can append other registered callbacks.
		Self->Callbacks->HostProtocolCallbacks.reserve(Self->Callbacks->HostProtocolCallbacks.size() + States.size());
		const auto FirstAnchor = PyList_GET_SIZE(HostType->ProtocolCallbacks.Get());
		// Allocate before native publication, keeping existing payloads' list identity.
		if (PyList_SetSlice(HostType->ProtocolCallbacks.Get(), FirstAnchor, FirstAnchor, Anchors.Get()) < 0)
		{
			return nullptr;
		}

		const vs_interop_status Status =
		    vs_interop_session_register_host_protocol(Self->Handle, TypeId, &Protocol);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			(void)PyList_SetSlice(HostType->ProtocolCallbacks.Get(), FirstAnchor,
			    PyList_GET_SIZE(HostType->ProtocolCallbacks.Get()), nullptr);
			return RaiseStatus(Self, Status, "register Python host protocol");
		}
		const auto FirstRegistered = Self->Callbacks->HostProtocolCallbacks.size();
		for (auto& State : States)
		{
			Self->Callbacks->HostProtocolCallbacks.push_back(State.release());
		}
		for (std::size_t Index = FirstRegistered;
		    Index < Self->Callbacks->HostProtocolCallbacks.size(); ++Index)
		{
			auto* Registered = Self->Callbacks->HostProtocolCallbacks[Index];
			++Self->Callbacks->Owners;
			const auto RetainStatus = vs_interop_session_retain_callback_user_data(
			    Self->Handle, Registered, &ReleaseProtocolUserData);
			if (RetainStatus != VS_INTEROP_STATUS_OK)
			{
				--Self->Callbacks->Owners;
				RaiseStatus(Self, RetainStatus, "retain Python host protocol state");
				PyObject* ErrorType = nullptr;
				PyObject* ErrorValue = nullptr;
				PyObject* ErrorTraceback = nullptr;
				PyErr_Fetch(&ErrorType, &ErrorValue, &ErrorTraceback);
				// Registration has committed. An allocation failure during ownership
				// attachment must not leave callable bridges with unowned user data.
				Activity.Finish();
				Py_XDECREF(SessionClose(Self, nullptr));
				PyErr_Restore(ErrorType, ErrorValue, ErrorTraceback);
				return nullptr;
			}
		}
		Py_RETURN_NONE;
	}
	catch (const std::bad_alloc&)
	{
		return PyErr_NoMemory();
	}
	catch (...)
	{
		PyErr_SetString(GError, "unexpected error while registering Python host protocol");
		return nullptr;
	}

	PyObject* SessionSetInstanceInterceptor(FPySession* Self, PyObject* Args)
	{
		PyObject* Callback = nullptr;
		if (!PyArg_ParseTuple(Args, "O:set_instance_call_interceptor", &Callback) || !CheckSession(Self))
		{
			return nullptr;
		}
		if (Callback != Py_None && !PyCallable_Check(Callback))
		{
			PyErr_SetString(PyExc_TypeError,
			    "instance interceptor must be callable or None");
			return nullptr;
		}
		const vs_interop_status Status =
		    vs_interop_session_set_instance_call_interceptor(
		        Self->Handle, Callback != Py_None ? &OnInstanceIntercept : nullptr,
		        Callback != Py_None ? Self->Callbacks : nullptr);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(Self, Status, "set instance call interceptor");
		}
		PyObject* Stored = Callback != Py_None ? Callback : nullptr;
		Py_XINCREF(Stored);
		Py_XSETREF(Self->Callbacks->InstanceInterceptor, Stored);
		Py_RETURN_NONE;
	}
	#include "VeroScriptPythonMemory.inl"
	#include "VeroScriptPythonNumerics.inl"
	PyMethodDef SessionMethods[] = {
	    {"_numerics", reinterpret_cast<PyCFunction>(SessionNumerics), METH_VARARGS,
	        "Checked Core numeric operations."},
	    {"variant", reinterpret_cast<PyCFunction>(SessionVariant), METH_VARARGS, "Box a Value using a canonical session type."},
	    {"_memory", reinterpret_cast<PyCFunction>(SessionMemory), METH_VARARGS,
	        "Checked runtime memory operations."},
	    {"close", reinterpret_cast<PyCFunction>(SessionClose), METH_NOARGS, "Close the native session."},
	    {"clear", reinterpret_cast<PyCFunction>(SessionClear), METH_NOARGS, "Clear loaded program state."},
	    {"request_cancellation", reinterpret_cast<PyCFunction>(SessionRequestCancellation), METH_NOARGS,
	        "Request cooperative cancellation of an execution-control-enabled session."},
	    {"load_source", reinterpret_cast<PyCFunction>(SessionLoadSource), METH_VARARGS | METH_KEYWORDS,
	        "Compile source text."},
	    {"load_file", reinterpret_cast<PyCFunction>(SessionLoadFile), METH_VARARGS, "Compile a source file."},
	    {"load_application", reinterpret_cast<PyCFunction>(SessionLoadApplication), METH_VARARGS | METH_KEYWORDS,
	        "Load an application container/directory with explicit absolute host options."},
	    {"load_package", reinterpret_cast<PyCFunction>(SessionLoadPackage), METH_VARARGS | METH_KEYWORDS,
	        "Load a parsed VeroScript package."},
	    {"reload_package", reinterpret_cast<PyCFunction>(SessionReloadPackage), METH_VARARGS | METH_KEYWORDS,
	        "Hot-reload a parsed VeroScript package."},
	    {"build_package_file", reinterpret_cast<PyCFunction>(SessionBuildPackageFile),
	        METH_VARARGS | METH_KEYWORDS, "Build a VeroScript package from a source file."},
	    {"run", reinterpret_cast<PyCFunction>(SessionRun), METH_VARARGS, "Run the loaded program."},
	    {"diagnostics", reinterpret_cast<PyCFunction>(SessionDiagnostics), METH_NOARGS,
	        "Return structured diagnostics from the last operation."},
	    {"set_input", reinterpret_cast<PyCFunction>(SessionSetInput), METH_VARARGS,
	        "Set the session's UTF-8 input buffer."},
	    {"set_hot_reload_enabled", reinterpret_cast<PyCFunction>(SessionSetHotReloadEnabled), METH_VARARGS,
	        "Enable or disable hot reload and mutable global access."},
	    {"reload", reinterpret_cast<PyCFunction>(SessionReload), METH_NOARGS,
	        "Reload the current source program."},
	    {"reload_status", reinterpret_cast<PyCFunction>(SessionGetReloadStatus), METH_NOARGS,
	        "Return the current reload settlement snapshot."},
	    {"set_annotation_processing_service", reinterpret_cast<PyCFunction>(SessionSetAnnotationService),
	        METH_O, "Attach a valid native annotation service handle; None or zero clears it."},
	    {"set_compile_processing_service", reinterpret_cast<PyCFunction>(SessionSetCompileProcessingService), METH_O, "Set a native SDK compile processing service; None clears it."},
	    {"set_script_compile_options", reinterpret_cast<PyCFunction>(SessionSetScriptCompileOptions), METH_VARARGS, "Set compilation extension roots, configuration and policy."},
	    {"set_vm_backend", reinterpret_cast<PyCFunction>(SessionSetVmBackend), METH_O,
	        "Select a VM file, directory, installed ID or auto before the first execution; none or None clears selection."},
	    {"get_vm_backend_info", reinterpret_cast<PyCFunction>(SessionGetVmBackendInfo), METH_NOARGS,
	        "Return selected interpreter metadata and its frozen state."},
	    {"set_jit_backend", reinterpret_cast<PyCFunction>(SessionSetJitBackend), METH_O,
	        "Select a JIT file, directory or installed ID while idle; auto resolves the environment; none or None disables JIT."},
	    {"get_jit_backend", reinterpret_cast<PyCFunction>(SessionGetJitBackend), METH_NOARGS,
	        "Return the selected JIT module."},
	    {"get_jit_backend_info", reinterpret_cast<PyCFunction>(SessionGetJitBackendInfo), METH_NOARGS,
	        "Return selected backend metadata."},
	    {"set_jit_policy", reinterpret_cast<PyCFunction>(SessionSetJitPolicy), METH_VARARGS | METH_KEYWORDS,
	        "Configure this session's JIT policy."},
	    {"set_jit_cache_limits", reinterpret_cast<PyCFunction>(SessionSetJitCacheLimits),
	        METH_VARARGS | METH_KEYWORDS, "Configure retained session JIT cache limits."},
	    {"jit_cache_limits", reinterpret_cast<PyCFunction>(SessionGetJitCacheLimits), METH_NOARGS,
	        "Return retained session JIT cache limits."},
	    {"jit_cache_stats", reinterpret_cast<PyCFunction>(SessionGetJitCacheStats), METH_NOARGS,
	        "Return retained session JIT cache resource statistics."},
	    {"jit_stats", reinterpret_cast<PyCFunction>(SessionGetJitStats), METH_NOARGS,
	        "Return statistics from the most recent JIT-backed operation."},
	    {"export_reflection_json", reinterpret_cast<PyCFunction>(SessionExportReflection), METH_NOARGS,
	        "Export loaded program reflection as UTF-8 JSON."},
	    {"export_bindings_json", reinterpret_cast<PyCFunction>(SessionExportBindings), METH_NOARGS,
	        "Export registered host bindings as UTF-8 JSON."},
	    {"call_function", reinterpret_cast<PyCFunction>(SessionCallFunction), METH_VARARGS,
	        "Call a free function."},
	    {"retain_callable", reinterpret_cast<PyCFunction>(SessionRetainCallable),
	        METH_VARARGS | METH_KEYWORDS, "Retain a function value or closure."},
	    {"construct_object", reinterpret_cast<PyCFunction>(SessionConstructObject), METH_VARARGS,
	        "Construct a script object."},
	    {"call_method", reinterpret_cast<PyCFunction>(SessionCallMethod), METH_VARARGS,
	        "Call a script object method."},
	    {"call_method_bypassing_interceptor",
	        reinterpret_cast<PyCFunction>(SessionCallMethodBypassingInterceptor), METH_VARARGS,
	        "Call a script object method without invoking the instance interceptor."},
	    {"get_field", reinterpret_cast<PyCFunction>(SessionGetField), METH_VARARGS,
	        "Read a script object field."},
	    {"set_field", reinterpret_cast<PyCFunction>(SessionSetField), METH_VARARGS,
	        "Write a script object field."},
	    {"get_global", reinterpret_cast<PyCFunction>(SessionGetGlobal), METH_VARARGS,
	        "Read an exported global."},
	    {"set_global", reinterpret_cast<PyCFunction>(SessionSetGlobal), METH_VARARGS,
	        "Write an exported global."},
	    {"has_function", reinterpret_cast<PyCFunction>(SessionHasFunction), METH_VARARGS,
	        "Check whether a function overload exists."},
	    {"has_method", reinterpret_cast<PyCFunction>(SessionHasMethod), METH_VARARGS,
	        "Check whether a method overload exists."},
	    {"register_function", reinterpret_cast<PyCFunction>(SessionRegisterFunction),
	        METH_VARARGS | METH_KEYWORDS, "Register a typed Python host function."},
	    {"register_annotation", reinterpret_cast<PyCFunction>(SessionRegisterAnnotation),
	        METH_VARARGS | METH_KEYWORDS, "Register annotation documentation; case_sensitive=False folds ASCII names only."},
	    {"register_type_alias", reinterpret_cast<PyCFunction>(SessionRegisterTypeAlias),
	        METH_VARARGS | METH_KEYWORDS, "Register a host type alias."},
	    {"register_generic_host_type", reinterpret_cast<PyCFunction>(SessionRegisterGenericHostType),
	        METH_VARARGS | METH_KEYWORDS, "Register a Python generic host type template."},
	    {"instantiate_generic_host_type", reinterpret_cast<PyCFunction>(SessionInstantiateGenericHostType),
	        METH_VARARGS, "Instantiate a Python generic host type template."},
	    {"register_host_class", reinterpret_cast<PyCFunction>(SessionRegisterHostClass),
	        METH_VARARGS | METH_KEYWORDS, "Register a Python-backed host class."},
	    {"register_host_enum", reinterpret_cast<PyCFunction>(SessionRegisterHostEnum),
	        METH_VARARGS | METH_KEYWORDS, "Register a Python-backed host enum."},
	    {"register_host_protocol", reinterpret_cast<PyCFunction>(SessionRegisterHostProtocol),
	        METH_VARARGS | METH_KEYWORDS, "Register Python host value/lifecycle protocol callbacks."},
	    {"register_host_member", reinterpret_cast<PyCFunction>(SessionRegisterHostMember),
	        METH_VARARGS | METH_KEYWORDS, "Register a Python-backed host class member."},
	    {"register_serializer", reinterpret_cast<PyCFunction>(SessionRegisterSerializer),
	        METH_VARARGS | METH_KEYWORDS, "Register a Python external object serializer."},
	    {"serialize_object", reinterpret_cast<PyCFunction>(SessionSerializeObject), METH_VARARGS,
	        "Serialize a script object through its registered external serializer."},
	    {"deserialize_object", reinterpret_cast<PyCFunction>(SessionDeserializeObject), METH_VARARGS,
	        "Apply externally serialized data to a script object."},
	    {"apply_class_defaults", reinterpret_cast<PyCFunction>(SessionApplyClassDefaults), METH_VARARGS,
	        "Invoke default field initialization for an existing script object."},
	    {"enumerate_object_references", reinterpret_cast<PyCFunction>(SessionEnumerateObjectReferences),
	        METH_VARARGS, "Return owned clones of external object references."},
	    {"set_mutation_callback", reinterpret_cast<PyCFunction>(SessionSetMutationCallback), METH_VARARGS,
	        "Set or clear the structured object mutation callback."},
	    {"set_instance_call_interceptor", reinterpret_cast<PyCFunction>(SessionSetInstanceInterceptor),
	        METH_VARARGS, "Set or clear the script instance-call interceptor."},
	    {nullptr, nullptr, 0, nullptr}};

	PyGetSetDef SessionGetSet[] = {
	    {const_cast<char*>("closed"), reinterpret_cast<getter>(SessionGetClosed), nullptr,
	        const_cast<char*>("whether the session is closed"), nullptr},
	    {const_cast<char*>("is_loaded"), reinterpret_cast<getter>(SessionGetIsLoaded), nullptr,
	        const_cast<char*>("whether a program is loaded"), nullptr},
	    {const_cast<char*>("hot_reload_enabled"),
	        reinterpret_cast<getter>(SessionGetHotReloadEnabled), nullptr,
	        const_cast<char*>("whether hot reload is enabled"), nullptr},
	    {nullptr, nullptr, nullptr, nullptr, nullptr}};

	PyObject* LoadCatalogDirectory(PyObject*, PyObject* Args)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		const char* Directory = nullptr;
		Py_ssize_t DirectorySize = 0;
		if (!PyArg_ParseTuple(Args, "s#:load_catalog_directory", &Directory, &DirectorySize))
		{
			return nullptr;
		}
		vs_interop_buffer* Error = nullptr;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_locale_load_catalog_directory(View(Directory, DirectorySize), &Error);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseBufferError(Status, Error, "load localization catalog failed");
		}
		vs_interop_buffer_destroy(Error);
		Py_RETURN_NONE;
	}

	PyObject* MergeCatalogDirectory(PyObject*, PyObject* Args, PyObject* Keywords)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		const char* Directory = nullptr;
		Py_ssize_t DirectorySize = 0;
		int OverrideExisting = 0;
		static const char* Names[] = {"directory", "override_existing", nullptr};
		if (!PyArg_ParseTupleAndKeywords(Args, Keywords,
		        "s#|p:merge_catalog_directory", const_cast<char**>(Names),
		        &Directory, &DirectorySize, &OverrideExisting))
		{
			return nullptr;
		}
		vs_interop_buffer* Error = nullptr;
		vs_interop_status Status = VS_INTEROP_STATUS_INTERNAL_ERROR;
		Py_BEGIN_ALLOW_THREADS
		    Status = vs_interop_locale_merge_catalog_directory(
		        View(Directory, DirectorySize), static_cast<std::uint8_t>(OverrideExisting), &Error);
		Py_END_ALLOW_THREADS if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseBufferError(Status, Error, "merge localization catalog failed");
		}
		vs_interop_buffer_destroy(Error);
		Py_RETURN_NONE;
	}

	PyObject* SetLanguage(PyObject*, PyObject* Args)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		const char* Language = nullptr;
		Py_ssize_t LanguageSize = 0;
		if (!PyArg_ParseTuple(Args, "s#:set_language", &Language, &LanguageSize))
		{
			return nullptr;
		}
		const vs_interop_status Status = vs_interop_locale_set_language(View(Language, LanguageSize));
		if (Status != VS_INTEROP_STATUS_OK)
		{
			return RaiseStatus(nullptr, Status, "set language");
		}
		Py_RETURN_NONE;
	}

	PyObject* GetLanguage(PyObject*, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		return UnicodeFromView(vs_interop_locale_get_language());
	}

	PyObject* GetAvailableLanguages(PyObject*, PyObject*)
	{
		if (!CheckInterpreter())
		{
			return nullptr;
		}
		vs_interop_buffer* Buffer = nullptr;
		const vs_interop_status Status =
		    vs_interop_locale_get_available_languages(&Buffer);
		if (Status != VS_INTEROP_STATUS_OK)
		{
			vs_interop_buffer_destroy(Buffer);
			return RaiseStatus(nullptr, Status, "get available languages");
		}
		return BufferToUnicode(Buffer, "available language query");
	}

	PyMethodDef ModuleMethods[] = {{"_memory_current", reinterpret_cast<PyCFunction>(CurrentMemory),
	                                   METH_VARARGS, "Memory services for the active host callback."},
	    {"_numerics_current", reinterpret_cast<PyCFunction>(CurrentNumerics),
	        METH_VARARGS, "Numeric services for the active host callback."},
	    {"load_catalog_directory", LoadCatalogDirectory, METH_VARARGS,
	        "Load a complete localization catalog directory."},
	    {"merge_catalog_directory", reinterpret_cast<PyCFunction>(MergeCatalogDirectory),
	        METH_VARARGS | METH_KEYWORDS, "Merge an application localization catalog directory."},
	    {"set_language", SetLanguage, METH_VARARGS, "Select the process-wide language."},
	    {"get_language", GetLanguage, METH_NOARGS, "Return the selected language."},
	    {"get_available_languages_json", GetAvailableLanguages, METH_NOARGS,
	        "Return available language metadata as UTF-8 JSON."},
	    {"_intercept_proceed", InterceptProceed, METH_VARARGS, "Continue an active intercepted call."},
	    {nullptr, nullptr, 0, nullptr}};

	PyModuleDef_Slot ModuleSlots[] = {
	    {Py_mod_exec, reinterpret_cast<void*>(InitializeVeroScriptModule)},
	#if PY_VERSION_HEX >= 0x030C0000
	    {Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
	#endif
	#if PY_VERSION_HEX >= 0x030D0000
	    {Py_mod_gil, Py_MOD_GIL_USED},
	#endif
	    {0, nullptr}};

	PyModuleDef Module = {
	    PyModuleDef_HEAD_INIT,
	    "_veroscript",
	    "Native CPython adapter over the stable VeroScriptInterop C ABI.",
	    0,
	    ModuleMethods,
	    ModuleSlots,
	    nullptr,
	    nullptr,
	    nullptr};

	bool AddException(PyObject* ModuleObject, const char* ShortName,
	    const char* QualifiedName, PyObject* Base, PyObject** Output)
	{
		if (*Output != nullptr)
		{
			return PyModule_AddObjectRef(ModuleObject, ShortName, *Output) == 0;
		}
		FPyOwnedRef Exception(PyErr_NewException(QualifiedName, Base, nullptr));
		if (Exception.Get() == nullptr || PyModule_AddObjectRef(ModuleObject, ShortName, Exception.Get()) < 0)
		{
			return false;
		}
		// Instances can outlive removal of the extension module from sys.modules.
		// Keep one process-lifetime reference so native methods never use a dangling
		// exception type while reporting an error from such an instance.
		*Output = Exception.Release();
		return true;
	}
}

static int InitializeVeroScriptModule(PyObject* ModuleObject)
{
	if (PyInterpreterState_Get() != PyInterpreterState_Main())
	{
		PyErr_SetString(PyExc_ImportError,
		    "VeroScriptPython currently supports only the main CPython interpreter");
		return -1;
	}
	const std::uint32_t RuntimeAbiVersion = vs_interop_get_abi_version();
	if (RuntimeAbiVersion != VS_INTEROP_ABI_VERSION)
	{
		PyErr_Format(PyExc_ImportError,
		    "VeroScriptInterop ABI mismatch: extension requires 0x%08x, runtime provides 0x%08x",
		    static_cast<unsigned int>(VS_INTEROP_ABI_VERSION),
		    static_cast<unsigned int>(RuntimeAbiVersion));
		return -1;
	}
	GMainInterpreter = PyInterpreterState_Get();
	GValueType.tp_name = "veroscript.Value";
	GValueType.tp_basicsize = sizeof(FPyValue);
	GValueType.tp_dealloc = reinterpret_cast<destructor>(ValueDealloc);
	GValueType.tp_repr = reinterpret_cast<reprfunc>(TValueCallbackBoundary<&ValueRepr>::Invoke);
	GValueMapping.mp_length = reinterpret_cast<lenfunc>(TValueCallbackBoundary<&ValueLength>::Invoke);
	GValueMapping.mp_subscript = reinterpret_cast<binaryfunc>(TValueCallbackBoundary<&ValueSubscript>::Invoke);
	GValueSequence.sq_length = reinterpret_cast<lenfunc>(TValueCallbackBoundary<&ValueLength>::Invoke);
	GValueSequence.sq_contains = reinterpret_cast<objobjproc>(TValueCallbackBoundary<&ValueContainsSlot>::Invoke);
	GValueType.tp_as_mapping = &GValueMapping;
	GValueType.tp_as_sequence = &GValueSequence;
	GValueType.tp_iter = reinterpret_cast<getiterfunc>(TValueCallbackBoundary<&ValueIter>::Invoke);
	GValueType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
	GValueType.tp_doc = "Owned VeroScript value; explicit values preserve ref/out writes.";
	GValueType.tp_methods = ValueMethods;
	GValueType.tp_getset = ValueGetSet;
	GValueType.tp_init = reinterpret_cast<initproc>(TValueCallbackBoundary<&ValueInit>::Invoke);
	GValueType.tp_new = TValueCallbackBoundary<&ValueNew>::Invoke;

	GPackageType.tp_name = "veroscript.Package";
	GPackageType.tp_basicsize = sizeof(FPyPackage);
	GPackageType.tp_dealloc = reinterpret_cast<destructor>(PackageDealloc);
	GPackageType.tp_repr = reinterpret_cast<reprfunc>(PackageRepr);
	GPackageType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
	GPackageType.tp_doc = "Owned parsed VeroScript application or library package.";
	GPackageType.tp_getset = PackageGetSet;
	GPackageType.tp_init = reinterpret_cast<initproc>(PackageInit);
	GPackageType.tp_new = PackageNew;

	GCallableType.tp_name = "veroscript.Callable";
	GCallableType.tp_basicsize = sizeof(FPyCallable);
	GCallableType.tp_dealloc = reinterpret_cast<destructor>(CallableDealloc);
	GCallableType.tp_call = CallableCall;
	GCallableType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
	GCallableType.tp_doc = "Creator-thread-confined retained VeroScript callable.";
	GCallableType.tp_traverse = reinterpret_cast<traverseproc>(CallableTraverse);
	GCallableType.tp_clear = reinterpret_cast<inquiry>(CallableClear);
	GCallableType.tp_methods = CallableMethods;
	GCallableType.tp_getset = CallableGetSet;

	GSessionType.tp_name = "veroscript._native.Session";
	GSessionType.tp_basicsize = sizeof(FPySession);
	GSessionType.tp_dealloc = reinterpret_cast<destructor>(SessionDealloc);
	GSessionType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
	GSessionType.tp_doc = "Creator-thread-confined native VeroScript session.";
	GSessionType.tp_traverse = reinterpret_cast<traverseproc>(SessionTraverse);
	GSessionType.tp_clear = reinterpret_cast<inquiry>(SessionClearReferences);
	GSessionType.tp_finalize = reinterpret_cast<destructor>(SessionFinalize);
	GSessionType.tp_methods = SessionMethods;
	GSessionType.tp_getset = SessionGetSet;
	GSessionType.tp_init = reinterpret_cast<initproc>(SessionInit);
	GSessionType.tp_new = SessionNew;

	InitializeMemoryLeaseType();
	if (PyType_Ready(&GMemoryLeaseType) < 0 || PyType_Ready(&GValueType) < 0 ||
	    PyType_Ready(&GPackageType) < 0 || PyType_Ready(&GCallableType) < 0 ||
	    PyType_Ready(&GSessionType) < 0)
	{
		return -1;
	}
	if (!AddException(ModuleObject, "Error", "veroscript.Error", PyExc_RuntimeError, &GError) || !AddException(ModuleObject, "ThreadError", "veroscript.ThreadError", GError, &GThreadError) || !AddException(ModuleObject, "TypeMismatchError", "veroscript.TypeMismatchError", GError, &GTypeMismatchError) || !AddException(ModuleObject, "CallbackError", "veroscript.CallbackError", GError, &GCallbackError))
	{
		return -1;
	}

	if (PyModule_AddObjectRef(ModuleObject, "Value", reinterpret_cast<PyObject*>(&GValueType)) < 0 ||
	    PyModule_AddObjectRef(ModuleObject, "MemoryLease", reinterpret_cast<PyObject*>(&GMemoryLeaseType)) <
	        0 ||
	    PyModule_AddObjectRef(ModuleObject, "Package", reinterpret_cast<PyObject*>(&GPackageType)) < 0 ||
	    PyModule_AddObjectRef(ModuleObject, "Callable", reinterpret_cast<PyObject*>(&GCallableType)) < 0 ||
	    PyModule_AddObjectRef(ModuleObject, "Session", reinterpret_cast<PyObject*>(&GSessionType)) < 0)
	{
		return -1;
	}
	const vs_interop_string_view RuntimeVersion = vs_interop_get_runtime_version();
	FPyOwnedRef Version(UnicodeFromView(RuntimeVersion));
	if (Version.Get() == nullptr)
	{
		return -1;
	}
	const vs_interop_string_view HostPlatform = vs_interop_get_host_platform();
	FPyOwnedRef Platform(UnicodeFromView(HostPlatform));
	if (Platform.Get() == nullptr)
	{
		return -1;
	}
	if (PyModule_AddIntConstant(ModuleObject, "ABI_VERSION", VS_INTEROP_ABI_VERSION) < 0 || PyModule_AddObjectRef(ModuleObject, "RUNTIME_VERSION", Version.Get()) < 0 || PyModule_AddObjectRef(ModuleObject, "HOST_PLATFORM", Platform.Get()) < 0 || PyModule_AddStringConstant(ModuleObject, "MOBILE_BUILD_ID", VEROSCRIPT_PYTHON_BUILD_ID) < 0)
	{
		return -1;
	}
	FPyOwnedRef Capabilities(PyLong_FromUnsignedLongLong(vs_interop_get_capabilities()));
	FPyOwnedRef CompileCapabilities(PyLong_FromUnsignedLongLong(vs_interop_get_compile_capabilities()));
	if (Capabilities.Get() == nullptr || CompileCapabilities.Get() == nullptr || PyModule_AddObjectRef(ModuleObject, "CAPABILITIES", Capabilities.Get()) < 0 || PyModule_AddObjectRef(ModuleObject, "COMPILE_CAPABILITIES", CompileCapabilities.Get()) < 0)
	{
		return -1;
	}
	return 0;
}

PyMODINIT_FUNC PyInit__veroscript()
{
	return PyModuleDef_Init(&Module);
}
