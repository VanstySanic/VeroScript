// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include "VeroScriptInteropInternal.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace VeroScript::Interop
{
	FSessionState::FSessionState() : Session(Host)
	{
		#if VEROSCRIPT_WITH_BYTECODE_VM
		BytecodeVm::Configure(Session);
		#endif
	}
	FSessionState::~FSessionState() noexcept
	{
		// Final ownership may disappear while a different session is active.
		// Core cleanup callbacks must retain this session's borrowed context.
		DestructionValueSession.emplace(this);
	}

	namespace
	{
		// Ordinary session operations cannot nest, including while host-owned
		// callbacks/functors are copied or released before Core enters the VM.
		// Callback-context invocation enters the active VM directly instead.
		struct FSessionReentry final
		{
			vs_interop_run_result* OutResult;
		};

		bool TryConvertNativeType(const vs_interop_native_type Source, ENativeType& Out)
		{
			switch (Source)
			{
			case VS_INTEROP_NATIVE_VOID:
				{
					Out = ENativeType::Void;
					return true;
				}
			case VS_INTEROP_NATIVE_INT32:
				{
					Out = ENativeType::Int;
					return true;
				}
			case VS_INTEROP_NATIVE_BOOL:
				{
					Out = ENativeType::Bool;
					return true;
				}
			case VS_INTEROP_NATIVE_DOUBLE:
				{
					Out = ENativeType::Double;
					return true;
				}
			case VS_INTEROP_NATIVE_STRING:
				{
					Out = ENativeType::String;
					return true;
				}
			case VS_INTEROP_NATIVE_UINT32:
				{
					Out = ENativeType::Unsigned;
					return true;
				}
			case VS_INTEROP_NATIVE_HOST_OBJECT:
				{
					Out = ENativeType::HostObject;
					return true;
				}
			case VS_INTEROP_NATIVE_ARRAY:
				{
					Out = ENativeType::Array;
					return true;
				}
			case VS_INTEROP_NATIVE_MAP:
				{
					Out = ENativeType::Map;
					return true;
				}
			case VS_INTEROP_NATIVE_SET:
				{
					Out = ENativeType::Set;
					return true;
				}
			case VS_INTEROP_NATIVE_INT64:
				{
					Out = ENativeType::Int64;
					return true;
				}
			case VS_INTEROP_NATIVE_UINT64:
				{
					Out = ENativeType::UInt64;
					return true;
				}
			case VS_INTEROP_NATIVE_ENUM:
				{
					Out = ENativeType::Enum;
					return true;
				}
			case VS_INTEROP_NATIVE_QUEUE:
				{
					Out = ENativeType::Queue;
					return true;
				}
			case VS_INTEROP_NATIVE_STACK:
				{
					Out = ENativeType::Stack;
					return true;
				}
			case VS_INTEROP_NATIVE_ORDERED_MAP:
				{
					Out = ENativeType::OrderedMap;
					return true;
				}
			case VS_INTEROP_NATIVE_DEQUE:
				{
					Out = ENativeType::Deque;
					return true;
				}
			case VS_INTEROP_NATIVE_CHANNEL:
				{
					Out = ENativeType::Channel;
					return true;
				}
			case VS_INTEROP_NATIVE_MULTI_MAP:
				{
					Out = ENativeType::MultiMap;
					return true;
				}
			case VS_INTEROP_NATIVE_ORDERED_MULTI_MAP:
				{
					Out = ENativeType::OrderedMultiMap;
					return true;
				}
			case VS_INTEROP_NATIVE_PRIORITY_QUEUE:
				{
					Out = ENativeType::PriorityQueue;
					return true;
				}
			case VS_INTEROP_NATIVE_MULTI_SET:
				{
					Out = ENativeType::MultiSet;
					return true;
				}
			case VS_INTEROP_NATIVE_ORDERED_MULTI_SET:
				{
					Out = ENativeType::OrderedMultiSet;
					return true;
				}
			case VS_INTEROP_NATIVE_FUNCTION:
				{
					Out = ENativeType::Function;
					return true;
				}
			case VS_INTEROP_NATIVE_FLOAT:
				{
					Out = ENativeType::Float;
					return true;
				}
			case VS_INTEROP_NATIVE_INT8:
				{
					Out = ENativeType::Int8;
					return true;
				}
			case VS_INTEROP_NATIVE_UINT8:
				{
					Out = ENativeType::UInt8;
					return true;
				}
			case VS_INTEROP_NATIVE_INT16:
				{
					Out = ENativeType::Int16;
					return true;
				}
			case VS_INTEROP_NATIVE_UINT16:
				{
					Out = ENativeType::UInt16;
					return true;
				}
			case VS_INTEROP_NATIVE_VARIANT:
				{
					Out = ENativeType::Variant;
					return true;
				}
			default:
				return false;
			}
		}

		bool TryConvertBackend(
		    const vs_interop_execution_backend Source,
		    EExecutionBackend& Out)
		{
			switch (Source)
			{
			case VS_INTEROP_BACKEND_VM:
				{
					Out = EExecutionBackend::Vm;
					return true;
				}
			case VS_INTEROP_BACKEND_MIXED:
				{
					Out = EExecutionBackend::Mixed;
					return true;
				}
			case VS_INTEROP_BACKEND_JIT:
				{
					Out = EExecutionBackend::Jit;
					return true;
				}
			default:
				return false;
			}
		}

		bool TryConvertRunMode(const vs_interop_run_mode Source, ERunMode& Out)
		{
			switch (Source)
			{
			case VS_INTEROP_RUN_EVAL:
				{
					Out = ERunMode::Eval;
					return true;
				}
			case VS_INTEROP_RUN_SCRIPT:
				{
					Out = ERunMode::Script;
					return true;
				}
			case VS_INTEROP_RUN_EVAL_ONCE:
				{
					Out = ERunMode::EvalOnce;
					return true;
				}
			default:
				return false;
			}
		}
	}

	std::string CopyString(const vs_interop_string_view View)
	{
		if (View.size == 0)
		{
			return {};
		}
		return std::string(View.data, static_cast<std::size_t>(View.size));
	}

	bool IsValidString(const vs_interop_string_view View)
	{
		return (View.data != nullptr || View.size == 0) && View.size <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) && View.size <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
	}

	bool IsValidBytes(const vs_interop_byte_view View)
	{
		return (View.data != nullptr || View.size == 0) && View.size <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
	}

	bool IsValidText(const vs_interop_string_view View)
	{
		return IsValidString(View) && (View.size == 0 || std::memchr(View.data, '\0', static_cast<std::size_t>(View.size)) == nullptr);
	}

	vs_interop_string_view ViewOf(const std::string_view Value)
	{
		return {Value.data(), static_cast<std::uint64_t>(Value.size())};
	}

	void FInteropHost::OnPrintLine(const std::string_view Line)
	{
		if (!PrintCallback)
		{
			return;
		}
		try
		{
			PrintCallback(ViewOf(Line), UserData);
		}
		catch (...)
		{
			// Foreign callbacks must not unwind into the runtime.
		}
	}

	void FInteropHost::OnDiagnosticLine(const std::string_view Line)
	{
		if (!DiagnosticCallback)
		{
			return;
		}
		try
		{
			DiagnosticCallback(ViewOf(Line), UserData);
		}
		catch (...)
		{
			// Foreign callbacks must not unwind into the runtime.
		}
	}

	Console::FInfo FInteropHost::QueryConsole()
	{
		Console::FInfo Result;
		if (!ConsoleQueryCallback) return Result;
		vs_interop_console_info Info{};
		Info.struct_size = sizeof(Info);
		try
		{
			if (ConsoleQueryCallback(&Info, UserData) != 1 || Info.struct_size != sizeof(Info) ||
			    (Info.flags & ~15u) != 0)
			{
				return Result;
			}

			Result.bOutputTerminal = (Info.flags & 1u) != 0;
			Result.bColor = ConsoleControlCallback && (Info.flags & 2u) != 0;
			Result.bCursor = ConsoleControlCallback && (Info.flags & 4u) != 0;
			Result.bClear = ConsoleControlCallback && (Info.flags & 8u) != 0;
			Result.Width = Info.width;
			Result.Height = Info.height;
		}
		catch (...)
		{ /* A foreign callback cannot unwind into the core. */
		}
		return Result;
	}
	bool FInteropHost::ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second)
	{
		if (!ConsoleControlCallback) return false;
		try
		{
			return ConsoleControlCallback(static_cast<std::uint32_t>(Operation), First, Second, UserData) ==
			    1;
		}
		catch (...)
		{
			return false;
		}
	}
	bool FInteropHost::WriteConsoleText(std::string_view Text)
	{
		try
		{
			return ConsoleWriteCallback && ConsoleWriteCallback(ViewOf(Text), UserData) == 1;
		}
		catch (...)
		{
			return false;
		}
	}

	vs_interop_status RequireOwnerThread(FSessionState& State) noexcept
	{
		if (std::this_thread::get_id() == State.OwnerThread)
		{
			return VS_INTEROP_STATUS_OK;
		}
		// Do not touch session-owned diagnostic storage from a foreign thread. The
		// status is the complete error report for this rejected operation; writing
		// LastError here could race a valid call on the creator thread.
		return VS_INTEROP_STATUS_WRONG_THREAD;
	}

	vs_interop_status RequireIdleSession(
	    FSessionState& State,
	    const std::string_view Action) noexcept
	{
		try
		{
			{
				const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
				if (State.ActiveSessionCalls == 0)
				{
					return VS_INTEROP_STATUS_OK;
				}
			}
			SetError(State, std::string(Action) + " is not allowed during script execution");
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	void ReleaseCallableOrDefer(
	    FSessionState& State,
	    const FRetainedCallableHandle Callable,
	    const bool bOwnerThreadOnly)
	{
		if (!Callable)
		{
			return;
		}
		{
			const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
			if (State.ActiveSessionCalls != 0 || (bOwnerThreadOnly && std::this_thread::get_id() != State.OwnerThread))
			{
				State.PendingCallableReleases.push_back(Callable);
				return;
			}
		}
		const FScopedSessionActivity Activity(State);
		(void)State.Session.ReleaseCallable(Callable);
	}

	FScopedSessionActivity::FScopedSessionActivity(FSessionState& InState,
	    vs_interop_run_result* const OutResult)
	    : State(InState)
	{
		{
			const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
			if (State.ActiveSessionCalls != 0)
			{
				throw FSessionReentry{OutResult};
			}
			++State.ActiveSessionCalls;
			State.ActiveSessionThread = std::this_thread::get_id();
		}
		PreviousValueSession = ActiveValueSession;
		ActiveValueSession = &State;
		try
		{
			std::string RootError;
			if (std::this_thread::get_id() == State.OwnerThread && !State.Session.DrainValueRoots(RootError))
			{
				SetError(State, std::move(RootError));
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
			}
		}
		catch (...)
		{
			InitializeFailedRunResult(OutResult);
			ActiveValueSession = PreviousValueSession;
			const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
			--State.ActiveSessionCalls;
			throw;
		}
	}

	FScopedSessionActivity::~FScopedSessionActivity() noexcept
	{
		struct FRestoreContext
		{
			FSessionState* Previous;
			~FRestoreContext()
			{
				ActiveValueSession = Previous;
			}
		} Restore{PreviousValueSession};
		if (std::this_thread::get_id() == State.OwnerThread)
		{
			try
			{
				std::string RootError;
				if (!State.Session.DrainValueRoots(RootError) && State.LastError.empty())
				{
					SetError(State, std::move(RootError));
				}
			}
			catch (...)
			{
			}
		}
		try
		{
			std::vector<FRetainedCallableHandle> Pending;
			{
				const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
				if (State.ActiveSessionCalls == 0)
				{
					return;
				}
				if (State.ActiveSessionCalls > 1)
				{
					--State.ActiveSessionCalls;
					return;
				}
				// Owner-thread-only retained entries cannot be released by a foreign
				// callable invocation. Leave all deferred work for the next owner call.
				if (std::this_thread::get_id() != State.OwnerThread)
				{
					State.ActiveSessionCalls = 0;
					return;
				}
				Pending.swap(State.PendingCallableReleases);
			}

			// Keep ActiveSessionCalls at one while closures are discarded: release
			// can run script destructors and host callbacks, which must not reenter
			// mutating session APIs. Such callbacks may queue more releases.
			for (;;)
			{
				for (const FRetainedCallableHandle Callable : Pending)
				{
					try
					{
						(void)State.Session.ReleaseCallable(Callable);
					}
					catch (...)
					{
					}
				}
				Pending.clear();
				// Callable destructors may also retire externally rooted values.
				try
				{
					std::string RootError;
					if (!State.Session.DrainValueRoots(RootError) && State.LastError.empty())
					{
						SetError(State, std::move(RootError));
					}
				}
				catch (...)
				{
				}
				const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
				if (State.PendingCallableReleases.empty())
				{
					State.ActiveSessionCalls = 0;
					return;
				}
				Pending.swap(State.PendingCallableReleases);
			}
		}
		catch (...)
		{
			try
			{
				const std::lock_guard<std::mutex> Lock(State.LifecycleMutex);
				if (State.ActiveSessionCalls == 1)
				{
					State.ActiveSessionCalls = 0;
				}
			}
			catch (...)
			{
			}
		}
	}

	void SetError(FSessionState& State, std::string Message)
	{
		State.LastError = std::move(Message);
		State.Diagnostics.clear();
	}

	void InitializeFailedRunResult(vs_interop_run_result* const OutResult) noexcept
	{
		if (OutResult)
		{
			*OutResult = {};
			OutResult->struct_size = sizeof(*OutResult);
			OutResult->exit_code = 2;
		}
	}

	vs_interop_status StoreRunResult(
	    FSessionState& State,
	    const FRunResult& Result,
	    vs_interop_run_result* OutResult)
	{
		State.Diagnostics.clear();
		State.Diagnostics.reserve(Result.Diagnostics.size());
		for (const FScriptError& Error : Result.Diagnostics)
		{
			FDiagnosticSnapshot Snapshot;
			Snapshot.Message = Error.Message;
			Snapshot.Severity = Error.Severity;
			Snapshot.MessageId = Error.MessageId;
			Snapshot.MessageArgs = Error.MessageArgs;
			Snapshot.FileLabel = Error.Location.FileLabel;
			Snapshot.Line = Error.Location.Line;
			Snapshot.Column = Error.Location.Column;
			State.Diagnostics.push_back(std::move(Snapshot));
		}
		if (State.Diagnostics.empty() && !Result.Error.Message.empty())
		{
			FDiagnosticSnapshot Snapshot;
			Snapshot.Message = Result.Error.Message;
			Snapshot.Severity = Result.Error.Severity;
			Snapshot.MessageId = Result.Error.MessageId;
			Snapshot.MessageArgs = Result.Error.MessageArgs;
			Snapshot.FileLabel = Result.Error.Location.FileLabel;
			Snapshot.Line = Result.Error.Location.Line;
			Snapshot.Column = Result.Error.Location.Column;
			State.Diagnostics.push_back(std::move(Snapshot));
		}

		if (Result.bSuccess)
		{
			State.LastError.clear();
		}
		else if (!State.Diagnostics.empty())
		{
			const auto Primary = std::find_if(State.Diagnostics.begin(), State.Diagnostics.end(),
			    [](const auto& D)
			{
				return D.Severity == ECompileDiagnosticSeverity::Error;
			});
			State.LastError = !Result.Error.Message.empty() ? Result.Error.Message
			    : Primary != State.Diagnostics.end()        ? Primary->Message
			                                                : State.Diagnostics.front().Message;
		}
		else
		{
			State.LastError = "VeroScript operation failed";
		}

		if (OutResult)
		{
			OutResult->struct_size = sizeof(vs_interop_run_result);
			OutResult->success = Result.bSuccess ? 1u : 0u;
			OutResult->exit_requested = Result.bExitRequested ? 1u : 0u;
			OutResult->reserved_u8[0] = 0;
			OutResult->reserved_u8[1] = 0;
			OutResult->exit_code = Result.ExitCode;
			OutResult->diagnostic_count = static_cast<std::uint32_t>(
			    std::min<std::size_t>(State.Diagnostics.size(), UINT32_MAX));
		}
		return Result.bSuccess ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_ENGINE_ERROR;
	}

	vs_interop_status TranslateException(FSessionState* State,
	    vs_interop_run_result* const OutResult) noexcept
	{
		InitializeFailedRunResult(OutResult);
		const auto TrySetError = [State](const char* const Message) noexcept
		{
			if (!State || !Message)
			{
				return;
			}
			try
			{
				SetError(*State, Message);
			}
			catch (...)
			{
			}
		};
		try
		{
			throw;
		}
		catch (const FValueAccessFailure& Failure)
		{
			if (State && Failure.Message)
			{
				try
				{
					FRunResult Result;
					Result.ExitCode = 2;
					Result.Error.Message = Failure.Message;
					Result.Error.Location.FileLabel = "<interop>";
					(void)StoreRunResult(*State, Result, OutResult);
				}
				catch (...)
				{
					TrySetError(Failure.Message);
				}
			}
			return Failure.Status;
		}
		catch (const FSessionReentry& Reentry)
		{
			InitializeFailedRunResult(Reentry.OutResult);
			if (State)
			{
				try
				{
					FRunResult Failure;
					Failure.ExitCode = 2;
					Failure.Error.Message = "VeroScriptInterop session reentry is not allowed during an active operation";
					Failure.Error.Location.FileLabel = "<interop>";
					return StoreRunResult(*State, Failure, Reentry.OutResult);
				}
				catch (...)
				{
					return TranslateException(State);
				}
			}
			TrySetError("VeroScriptInterop session reentry is not allowed during an active operation");
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		}
		catch (const std::bad_alloc&)
		{
			TrySetError("VeroScriptInterop allocation failed");
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (const std::exception& Error)
		{
			TrySetError(Error.what());
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
		catch (...)
		{
			TrySetError("Unknown exception at VeroScriptInterop ABI boundary");
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	bool IsValidRunResultOutput(const vs_interop_run_result* Result)
	{
		return !Result || Result->struct_size == 0 || Result->struct_size >= sizeof(vs_interop_run_result);
	}

	bool ConvertNativeType(
	    const vs_interop_native_type_desc& Source,
	    FNativeTypeDesc& Out,
	    std::string& OutError,
	    const std::size_t Depth,
	    std::size_t* NodeCount)
	{
		std::size_t RootNodeCount = 0;
		if (!NodeCount)
		{
			NodeCount = &RootNodeCount;
		}
		if (*NodeCount >= 1024)
		{
			OutError = "native type descriptors exceed 1024 total nodes";
			return false;
		}
		++*NodeCount;
		if (Source.struct_size < sizeof(vs_interop_native_type_desc))
		{
			OutError = "native type descriptor struct_size is too small";
			return false;
		}
		if (Depth >= 32)
		{
			OutError = "native type descriptor nesting exceeds 32 levels";
			return false;
		}
		if (!IsValidText(Source.name))
		{
			OutError = "invalid native type descriptor";
			return false;
		}
		if (Source.type_argument_count != 0 && Source.type_arguments == nullptr)
		{
			OutError = "native type descriptor has null type_arguments";
			return false;
		}
		if (Source.type_argument_count > 64)
		{
			OutError = "native type descriptor has too many type arguments";
			return false;
		}

		Out = {};
		if (!TryConvertNativeType(Source.type, Out.Type))
		{
			OutError = "unknown native type descriptor kind";
			return false;
		}
		const std::string Name = CopyString(Source.name);
		switch (Out.Type)
		{
		case ENativeType::HostObject:
		case ENativeType::Enum:
		case ENativeType::Function:
			{
				Out.HostTypeName = Name;
				break;
			}
		case ENativeType::Array:
		case ENativeType::Set:
		case ENativeType::Queue:
		case ENativeType::Stack:
		case ENativeType::Deque:
		case ENativeType::Channel:
		case ENativeType::PriorityQueue:
		case ENativeType::MultiSet:
		case ENativeType::OrderedMultiSet:
			{
				Out.ElementTypeName = Name;
				break;
			}
		default:
			{
				break;
			}
		}
		Out.TypeArguments.reserve(static_cast<std::size_t>(Source.type_argument_count));
		for (std::uint64_t Index = 0; Index < Source.type_argument_count; ++Index)
		{
			FNativeTypeDesc Argument;
			if (!ConvertNativeType(Source.type_arguments[Index], Argument, OutError,
			        Depth + 1, NodeCount))
			{
				return false;
			}
			Out.TypeArguments.push_back(std::move(Argument));
		}
		return true;
	}

	bool ConvertNativeParam(
	    const vs_interop_native_param_desc& Source,
	    FNativeParamDesc& Out,
	    std::string& OutError,
	    std::size_t* NodeCount)
	{
		if (Source.struct_size < sizeof(vs_interop_native_param_desc))
		{
			OutError = "native parameter descriptor struct_size is too small";
			return false;
		}
		if ((Source.flags & ~(VS_INTEROP_PARAM_REF | VS_INTEROP_PARAM_CONST_REF)) != 0)
		{
			OutError = "native parameter descriptor has unknown flags";
			return false;
		}
		if (!ConvertNativeType(Source.type, Out.Type, OutError, 0, NodeCount))
		{
			return false;
		}
		Out.Flags = 0;
		if ((Source.flags & VS_INTEROP_PARAM_REF) != 0)
		{
			Out.Flags |= NativeParamFlagRef;
		}
		if ((Source.flags & VS_INTEROP_PARAM_CONST_REF) != 0)
		{
			Out.Flags |= NativeParamFlagConstRef;
		}
		return true;
	}
}

extern "C"
{
	uint32_t VS_INTEROP_CALL vs_interop_get_abi_version(void)
	{
		return VS_INTEROP_ABI_VERSION;
	}

	vs_interop_string_view VS_INTEROP_CALL vs_interop_get_runtime_version(void)
	{
		try
		{
			return VeroScript::Interop::ViewOf(VeroScript::GetVersionString());
		}
		catch (...)
		{
			// The core lazily allocates this string. No exception may cross the C ABI.
			return {nullptr, 0};
		}
	}

	vs_interop_string_view VS_INTEROP_CALL vs_interop_get_host_platform(void)
	{
		return VeroScript::Interop::ViewOf(VeroScript::GetHostPlatformString());
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_create(
	    const vs_interop_session_options* options,
	    vs_interop_session** out_session)
	{
		if (!out_session)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_session = nullptr;
		try
		{
			// Console callbacks extend the same-ABI options prefix. Older callers
			// still provide the original line callbacks and user data.
			if (options && options->struct_size < offsetof(vs_interop_session_options, console_query_callback))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			auto Handle = std::make_unique<vs_interop_session>();
			Handle->State = std::make_shared<VeroScript::Interop::FSessionState>();
			if (options)
			{
				Handle->State->Host.PrintCallback = options->print_callback;
				Handle->State->Host.DiagnosticCallback = options->diagnostic_callback;
				Handle->State->Host.UserData = options->user_data;
				if (options->struct_size >= offsetof(vs_interop_session_options, console_query_callback) + sizeof(options->console_query_callback))
				{
					Handle->State->Host.ConsoleQueryCallback = options->console_query_callback;
				}
				if (options->struct_size >= offsetof(vs_interop_session_options, console_control_callback) + sizeof(options->console_control_callback))
				{
					Handle->State->Host.ConsoleControlCallback = options->console_control_callback;
				}
				if (options->struct_size >= offsetof(vs_interop_session_options, console_write_callback) + sizeof(options->console_write_callback))
				{
					Handle->State->Host.ConsoleWriteCallback = options->console_write_callback;
				}
			}
			*out_session = Handle.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_destroy(vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		try
		{
			{
				const std::lock_guard<std::mutex> Lock(State->LifecycleMutex);
				if (State->ActiveSessionCalls != 0 && State->ActiveSessionThread != State->OwnerThread)
				{
					return VS_INTEROP_STATUS_ENGINE_ERROR;
				} // join the opted-in foreign invocation before closing
			}
			State->Session.BeginCloseValues();
			if (State->ActiveSessionCalls != 0)
			{
				session->State.reset();
				State->ClosingWrapper.reset(session);
				return VS_INTEROP_STATUS_OK;
			}
			// Keep the opaque wrapper allocated through synchronous finalization.
			// Destructor callbacks must see a closed handle, rather than freed memory.
			session->State.reset();
			State.reset();
			delete session;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}

	static vs_interop_status ApplyConfiguration(
	    vs_interop_session* session,
	    const vs_interop_config* config,
	    const vs_interop_module_environment* environment,
	    const bool replace_environment)
	{
		if (!session || !session->State || !config || config->struct_size < sizeof(vs_interop_config))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(
		        State, "ApplyConfig");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			auto CopyList = [&](const vs_interop_string_list& List,
			                    std::vector<std::string>& Storage,
			                    std::vector<std::string_view>& Views) -> bool
			{
				if ((List.count != 0 && !List.values) || List.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
				{
					return false;
				}
				Storage.reserve(static_cast<std::size_t>(List.count));
				for (std::uint64_t Index = 0; Index < List.count; ++Index)
				{
					if (!VeroScript::Interop::IsValidText(List.values[Index]))
					{
						return false;
					}
					Storage.push_back(VeroScript::Interop::CopyString(List.values[Index]));
				}
				Views.reserve(Storage.size());
				for (const std::string& Value : Storage)
				{
					Views.emplace_back(Value);
				}
				return true;
			};

			std::vector<std::string> ImportStorage, ImplicitStorage, ArgStorage,
			    DefineStorage, StdlibStorage;
			std::vector<std::string_view> ImportViews, ImplicitViews, ArgViews,
			    DefineViews, StdlibViews;
			if (!CopyList(config->import_paths, ImportStorage, ImportViews) || !CopyList(config->implicit_import_paths, ImplicitStorage, ImplicitViews) || !CopyList(config->script_args, ArgStorage, ArgViews) || !CopyList(config->defines, DefineStorage, DefineViews) || !CopyList(config->stdlib_search_paths, StdlibStorage, StdlibViews) || !VeroScript::Interop::IsValidText(config->bytecode_cache_directory) || (config->source_overlay_count != 0 && !config->source_overlays) || (config->predefined_macro_count != 0 && !config->predefined_macros) || config->source_overlay_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || config->predefined_macro_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || config->max_call_depth > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || (config->allowed_script_capabilities & ~static_cast<std::uint32_t>(VS_INTEROP_SCRIPT_CAP_ALL)) != 0 || config->execution_backend > VS_INTEROP_BACKEND_JIT)
			{
				VeroScript::Interop::SetError(State, "invalid VeroScriptInterop configuration");
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}

			bool Configured = false;
			std::string ConfigurationError;
			{
				// Service functors can call the C ABI from their copy constructors
				// and destructors. Guard preparation, commit, and temporary cleanup.
				const VeroScript::Interop::FScopedSessionActivity Activity(State);
				// FEngineConfig holds a view; keep its storage alive through ApplyConfig.
				const std::string BytecodeCacheDirectory = VeroScript::Interop::CopyString(
				    config->bytecode_cache_directory);
				VeroScript::FEngineConfig Config;
				Config.NumericLimits = State.Session.GetNumericLimits();
				Config.StandardLibraryLimits = State.Session.GetStandardLibraryLimits();
				Config.ModuleEnvironment = replace_environment
				    ? VeroScript::Interop::GetModuleEnvironment(environment)
				    : State.Session.GetModuleEnvironment();
				if (const auto Status =
				        VeroScript::Interop::ValidateModuleEnvironment(State, Config.ModuleEnvironment);
				    Status != VS_INTEROP_STATUS_OK)
				{
					return Status;
				}

				Config.JitBackendFactory = State.Session.GetJitBackendFactory();
				Config.AotProgram = State.Session.GetAotProgram();
				Config.ImportPaths = ImportViews;
				Config.ImplicitImportPaths = ImplicitViews;
				Config.ScriptArgs = ArgViews;
				Config.Defines = DefineViews;
				Config.StdlibSearchPaths = StdlibViews;
				Config.BytecodeCacheDirectory = BytecodeCacheDirectory;
				Config.MaxCallDepth = static_cast<std::size_t>(config->max_call_depth);
				Config.ExecutionLimits.MaxInstructions = config->max_instructions;
				Config.ExecutionLimits.TimeoutMilliseconds = config->timeout_milliseconds;
				// Build a candidate token without mutating the live Session state.  The
				// rest of this function can still reject source overlays or predefined
				// macros, and a failed ApplyConfig must not make request_cancel target a
				// token different from the one the Session actually owns.
				std::shared_ptr<VeroScript::FExecutionControl> CandidateExecutionControl;
				{
					const std::lock_guard<std::mutex> Lock(State.ExecutionControlMutex);
					if (config->enable_execution_control != 0)
					{
						CandidateExecutionControl = State.ExecutionControl;
						if (!CandidateExecutionControl)
						{
							CandidateExecutionControl =
							    std::make_shared<VeroScript::FExecutionControl>();
						}
					}
				}
				Config.ExecutionControl = CandidateExecutionControl;
				if (config->capability_policy_enabled != 0)
				{
					const std::uint32_t Allowed = config->allowed_script_capabilities;
					Config.Capabilities.bAllowFileRead =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_FILE_READ) != 0;
					Config.Capabilities.bAllowFileWrite =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_FILE_WRITE) != 0;
					Config.Capabilities.bAllowFileDelete =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_FILE_DELETE) != 0;
					Config.Capabilities.bAllowEnvironmentRead =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_READ) != 0;
					Config.Capabilities.bAllowEnvironmentWrite =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_ENVIRONMENT_WRITE) != 0;
					Config.Capabilities.bAllowConsoleInput =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_CONSOLE_INPUT) != 0;
					Config.Capabilities.bAllowConsoleOutput =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT) != 0;
					Config.Capabilities.bAllowTaskSpawn =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_TASK_SPAWN) != 0;
					Config.Capabilities.bAllowProcessDirectoryChange =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_PROCESS_DIRECTORY_CHANGE) != 0;
					Config.Capabilities.bAllowMemoryAddress =
					    (Allowed & VS_INTEROP_SCRIPT_CAP_MEMORY_ADDRESS) != 0;
				}
				Config.MemoryLimits = State.Session.GetMemoryContext().GetLimits();
				if (!VeroScript::Interop::TryConvertBackend(
				        config->execution_backend, Config.ExecutionBackend))
				{
					VeroScript::Interop::SetError(State, "invalid execution backend");
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				Config.bUseBytecodeCache = config->use_bytecode_cache != 0;
				Config.bAllowCircularImports = config->allow_circular_imports != 0;
				Config.bAllowProjectWideAccess = config->allow_project_wide_access != 0;
				Config.SourceOverlays.reserve(static_cast<std::size_t>(config->source_overlay_count));
				for (std::uint64_t Index = 0; Index < config->source_overlay_count; ++Index)
				{
					const auto& Overlay = config->source_overlays[Index];
					if (!VeroScript::Interop::IsValidText(Overlay.path) || !VeroScript::Interop::IsValidText(Overlay.source))
					{
						VeroScript::Interop::SetError(State, "invalid source overlay");
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					Config.SourceOverlays.push_back({VeroScript::Interop::CopyString(Overlay.path),
					    VeroScript::Interop::CopyString(Overlay.source)});
				}
				Config.PredefinedMacros.reserve(
				    static_cast<std::size_t>(config->predefined_macro_count));
				for (std::uint64_t Index = 0; Index < config->predefined_macro_count; ++Index)
				{
					const auto& SourceMacro = config->predefined_macros[Index];
					if (SourceMacro.struct_size < sizeof(vs_interop_predefined_macro) || !VeroScript::Interop::IsValidText(SourceMacro.name) || SourceMacro.name.size == 0 || !VeroScript::Interop::IsValidText(SourceMacro.body) || !VeroScript::Interop::IsValidText(SourceMacro.source_name) || (SourceMacro.parameters.count != 0 && !SourceMacro.parameters.values) || SourceMacro.parameters.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
					{
						VeroScript::Interop::SetError(State, "invalid predefined macro");
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					VeroScript::FPredefinedMacro Macro;
					Macro.Name = VeroScript::Interop::CopyString(SourceMacro.name);
					Macro.Body = VeroScript::Interop::CopyString(SourceMacro.body);
					Macro.SourceName = VeroScript::Interop::CopyString(SourceMacro.source_name);
					if (Macro.SourceName.empty())
					{
						Macro.SourceName = "<host>";
					}
					Macro.bVariadic = SourceMacro.variadic != 0;
					Macro.Parameters.reserve(
					    static_cast<std::size_t>(SourceMacro.parameters.count));
					for (std::uint64_t ParamIndex = 0;
					    ParamIndex < SourceMacro.parameters.count;
					    ++ParamIndex)
					{
						const auto Parameter = SourceMacro.parameters.values[ParamIndex];
						if (!VeroScript::Interop::IsValidText(Parameter) || Parameter.size == 0)
						{
							VeroScript::Interop::SetError(
							    State, "invalid predefined macro parameter");
							return VS_INTEROP_STATUS_INVALID_ARGUMENT;
						}
						Macro.Parameters.push_back(VeroScript::Interop::CopyString(Parameter));
					}
					Config.PredefinedMacros.push_back(std::move(Macro));
				}
				// Decode and validate plain configuration data before copying service
				// functors. A rejected descriptor must retain its own error message.
				Config.AnnotationProcessing = State.Session.GetAnnotationProcessing();
				Config.CompileProcessing = State.Session.GetCompileProcessing();
				Config.ScriptCompileExtensions = State.Session.GetScriptCompileExtensions();
				try
				{
					Configured = State.Session.ApplyConfig(Config, &ConfigurationError);
				}
				catch (...)
				{
					// Core configuration is not transactional on allocation failure.
					// A late copy may throw after installing its control token; keep
					// request_cancel bound to the token actually owned by the Session.
					const std::lock_guard<std::mutex> Lock(State.ExecutionControlMutex);
					State.ExecutionControl = State.Session.GetExecutionControl();
					throw;
				}
				if (Configured)
				{
					const std::lock_guard<std::mutex> Lock(State.ExecutionControlMutex);
					// C ABI callers do not own the token, so a successfully applied
					// configuration is the explicit boundary that makes a previously
					// cancelled session runnable again.  Commit/reset together so
					// request_cancel always observes the token installed in the Session.
					if (CandidateExecutionControl)
					{
						CandidateExecutionControl->Reset();
					}
					State.ExecutionControl = CandidateExecutionControl;
				}
			}
			if (!Configured)
			{
				// Temporary service owners may reenter while being released. Publish
				// the original rejection only after their guarded cleanup completes.
				VeroScript::Interop::SetError(State, ConfigurationError.empty()
				        ? "VeroScript configuration was rejected"
				        : std::move(ConfigurationError));
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.LastError.clear();
			State.Diagnostics.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_config(
	    vs_interop_session* session, const vs_interop_config* config)
	{
		return ApplyConfiguration(session, config, nullptr, false);
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_apply_config_with_environment(
	    vs_interop_session* session, const vs_interop_config* config,
	    const vs_interop_module_environment* environment)
	{
		return ApplyConfiguration(session, config, environment, true);
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_request_cancel(
	    vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto State = session->State;
		try
		{
			std::shared_ptr<VeroScript::FExecutionControl> Control;
			{
				const std::lock_guard<std::mutex> Lock(State->ExecutionControlMutex);
				Control = State->ExecutionControl;
			}
			if (!Control)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			Control->RequestCancel();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(State.get());
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_load_source(
	    vs_interop_session* session,
	    const vs_interop_string_view source,
	    const vs_interop_string_view file_label,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(source) || !VeroScript::Interop::IsValidText(file_label) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.LoadSource(VeroScript::Interop::CopyString(source),
			        VeroScript::Interop::CopyString(file_label)),
			    out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_load_file(
	    vs_interop_session* session,
	    const vs_interop_string_view file_path,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidText(file_path) || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.LoadFile(VeroScript::Interop::CopyString(file_path)), out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_run(
	    vs_interop_session* session,
	    const vs_interop_run_mode mode,
	    vs_interop_run_result* out_result)
	{
		if (!session || !session->State || !VeroScript::Interop::IsValidRunResultOutput(out_result))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			VeroScript::ERunMode RunMode;
			if (!VeroScript::Interop::TryConvertRunMode(mode, RunMode))
			{
				VeroScript::Interop::SetError(State, "invalid run mode");
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const VeroScript::Interop::FScopedSessionActivity Activity(State, out_result);
			return VeroScript::Interop::StoreRunResult(State,
			    State.Session.Run(RunMode), out_result);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State, out_result);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_clear(vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "Clear");
		    Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			State.Session.Clear();
			if (State.Session.IsLoaded())
			{
				VeroScript::Interop::SetError(State,
				    State.Session.RequiresReset() && !State.Session.CanExportModuleValues()
				        ? "Hot reload rollback did not finish. Destroy and recreate the session."
				        : "release external value roots before clearing the module");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			State.LastError.clear();
			State.Diagnostics.clear();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}

	uint8_t VS_INTEROP_CALL vs_interop_session_is_loaded(const vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return 0;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return 0;
		}
		try
		{
			return State->Session.IsLoaded() ? 1u : 0u;
		}
		catch (...)
		{
			return 0;
		}
	}

	uint64_t VS_INTEROP_CALL vs_interop_session_diagnostic_count(
	    const vs_interop_session* session)
	{
		if (!session || !session->State)
		{
			return 0;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return 0;
		}
		return static_cast<uint64_t>(State->Diagnostics.size());
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic(
	    const vs_interop_session* session,
	    const uint64_t index,
	    vs_interop_diagnostic* out_diagnostic)
	{
		if (!session || !session->State || !out_diagnostic || (out_diagnostic->struct_size != 0 && out_diagnostic->struct_size < sizeof(vs_interop_diagnostic)))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		if (index >= State->Diagnostics.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto& Item = State->Diagnostics[static_cast<std::size_t>(index)];
		out_diagnostic->struct_size = sizeof(vs_interop_diagnostic);
		out_diagnostic->message = VeroScript::Interop::ViewOf(Item.Message);
		out_diagnostic->message_id = VeroScript::Interop::ViewOf(Item.MessageId);
		out_diagnostic->file_label = VeroScript::Interop::ViewOf(Item.FileLabel);
		out_diagnostic->line = Item.Line;
		out_diagnostic->column = Item.Column;
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_diagnostic_argument_count(
	    const vs_interop_session* session,
	    const uint64_t diagnostic_index,
	    uint64_t* out_count)
	{
		if (!session || !session->State || !out_count)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		if (diagnostic_index >= State->Diagnostics.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_count = static_cast<uint64_t>(
		    State->Diagnostics[static_cast<std::size_t>(diagnostic_index)].MessageArgs.size());
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_get_diagnostic_argument(
	    const vs_interop_session* session,
	    const uint64_t diagnostic_index,
	    const uint64_t argument_index,
	    vs_interop_string_view* out_argument)
	{
		if (!session || !session->State || !out_argument)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		if (diagnostic_index >= State->Diagnostics.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto& Arguments = State->Diagnostics[static_cast<std::size_t>(diagnostic_index)].MessageArgs;
		if (argument_index >= Arguments.size())
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_argument = VeroScript::Interop::ViewOf(
		    Arguments[static_cast<std::size_t>(argument_index)]);
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_copy_last_error(
	    const vs_interop_session* session,
	    char* buffer,
	    const uint64_t buffer_size,
	    uint64_t* required_size)
	{
		if (!session || !session->State || !required_size)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		const auto State = session->State;
		if (std::this_thread::get_id() != State->OwnerThread)
		{
			return VS_INTEROP_STATUS_WRONG_THREAD;
		}
		const std::string& Error = State->LastError;
		*required_size = static_cast<uint64_t>(Error.size()) + 1u;
		if (!buffer || buffer_size < *required_size)
		{
			return VS_INTEROP_STATUS_BUFFER_TOO_SMALL;
		}
		std::memcpy(buffer, Error.data(), Error.size());
		buffer[Error.size()] = '\0';
		return VS_INTEROP_STATUS_OK;
	}
}
