// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"

#include "VeroScriptCpp.h"
#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptApplication.h"
#endif

#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if VEROSCRIPT_WITH_BYTECODE_VM
#include "VeroScriptBytecodeVmAssembly.h"
#endif

namespace VeroScript::Interop
{
	// Callback addresses must not be published until their ownership table has
	// room. Grow geometrically so repeated registration does not relocate every
	// existing entry on every call.
	template <typename T>
	void ReserveAdditional(std::vector<T>& Values, const std::size_t Additional)
	{
		const auto Limit = Values.max_size();
		if (Additional > Limit - Values.size())
		{
			throw std::bad_alloc{};
		}
		const auto Required = Values.size() + Additional;
		if (Required <= Values.capacity()) return;
		const auto Capacity = Values.capacity();
		const auto Grown = Capacity > Limit / 2 ? Limit : Capacity * 2;
		Values.reserve(Grown < Required ? Required : Grown);
	}

	struct FDiagnosticSnapshot
	{
		ECompileDiagnosticSeverity Severity = ECompileDiagnosticSeverity::Error;
		std::string Message;
		std::string MessageId;
		std::vector<std::string> MessageArgs;
		std::string FileLabel;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
	};

	struct FNativeThunk
	{
		vs_interop_native_callback Callback = nullptr;
		void* UserData = nullptr;
		ENativeType ReturnType = ENativeType::Void;
		std::shared_ptr<void> UserDataOwner;
		bool bHostProtocol = false;
	};

	struct FGenericHostTypeThunk
	{
		vs_interop_generic_host_type_callback Callback = nullptr;
		void* UserData = nullptr;
		// Member/protocol natives for committed concrete types. Instantiation keeps
		// raw callback pointers inside a candidate registry and rolls back newly
		// generated thunks only after a rejected candidate has been destroyed.
		std::vector<std::shared_ptr<FNativeThunk>> GeneratedNativeThunks;
	};

	struct FExternalSerializerThunk
	{
		vs_interop_external_serialize_callback Serialize = nullptr;
		vs_interop_external_deserialize_callback Deserialize = nullptr;
		void* UserData = nullptr;
	};

	// FSessionState owns FInteropHost by value, so the host definition comes first.
	class FInteropHost final : public IEngineHost
	{
	public:
		//~ Begin IEngineHost Interface
		void OnPrintLine(std::string_view Line) override;

		void OnDiagnosticLine(std::string_view Line) override;
		Console::FInfo QueryConsole() override;
		bool ExecuteConsole(Console::EOperation Operation, std::int32_t First, std::int32_t Second) override;
		bool HasConsoleTextOutput() const override
		{
			return ConsoleWriteCallback != nullptr;
		}
		bool WriteConsoleText(std::string_view Text) override;
		//~ End IEngineHost Interface

		vs_interop_line_callback PrintCallback = nullptr;
		vs_interop_line_callback DiagnosticCallback = nullptr;

		vs_interop_console_query_callback ConsoleQueryCallback = nullptr;
		vs_interop_console_control_callback ConsoleControlCallback = nullptr;
		vs_interop_console_write_callback ConsoleWriteCallback = nullptr;
		void* UserData = nullptr;
	};

	struct FSessionState;
	struct FSharedValueOwnership;
	extern thread_local FSessionState* ActiveValueSession;
	// A borrowed callback keeps its originating session even when the host enters
	// another session synchronously. Restore that context only for the borrowed call.
	class FScopedValueSession final
	{
	public:
		explicit FScopedValueSession(FSessionState* Owner) noexcept
		    : Previous(ActiveValueSession)
		{
			ActiveValueSession = Owner;
		}
		~FScopedValueSession() noexcept
		{
			ActiveValueSession = Previous;
		}
		FScopedValueSession(const FScopedValueSession&) = delete;
		FScopedValueSession& operator=(const FScopedValueSession&) = delete;

	private:
		FSessionState* Previous;
	};

	struct FSessionState : std::enable_shared_from_this<FSessionState>
	{
		FSessionState();
		~FSessionState() noexcept;

		FInteropHost Host;

		// Installed into the core only when the caller opts in, so ordinary C ABI
		// sessions retain their requested JIT backend.
		std::mutex ExecutionControlMutex;
		std::shared_ptr<FExecutionControl> ExecutionControl;

		// Thunks must outlive Session because its native table stores their addresses.
		std::vector<std::shared_ptr<FNativeThunk>> NativeThunks;
		std::vector<std::unique_ptr<FGenericHostTypeThunk>> GenericHostTypeThunks;
		std::vector<std::unique_ptr<FExternalSerializerThunk>> ExternalSerializerThunks;
		const std::thread::id OwnerThread = std::this_thread::get_id();
		std::vector<FDiagnosticSnapshot> Diagnostics;
		std::string LastError;
		std::mutex LifecycleMutex;
		std::size_t ActiveSessionCalls = 0;
		std::thread::id ActiveSessionThread;
		std::vector<FRetainedCallableHandle> PendingCallableReleases;
		vs_interop_instance_call_interceptor InstanceInterceptor = nullptr;
		void* InstanceInterceptorUserData = nullptr;
		vs_interop_mutation_callback MutationCallback = nullptr;
		void* MutationCallbackUserData = nullptr;
		// Destroy the consumed public shell after Session's final script cleanup.
		std::unique_ptr<::vs_interop_session> ClosingWrapper;
		// Member destruction runs Session first, then restores the previous TLS
		// context, then releases ClosingWrapper. This scope does not allocate.
		std::optional<FScopedValueSession> DestructionValueSession;
		// Core teardown can run retained script destructors and installed hooks.
		// Destroy it first, while all callback data, diagnostics and locks are alive.
		VeroScriptSession Session;
	};

	class FScopedSessionActivity final
	{
	public:
		explicit FScopedSessionActivity(FSessionState& InState,
		    vs_interop_run_result* OutResult = nullptr);
		~FScopedSessionActivity() noexcept;
		FScopedSessionActivity(const FScopedSessionActivity&) = delete;
		FScopedSessionActivity& operator=(const FScopedSessionActivity&) = delete;

	private:
		FSessionState& State;
		FSessionState* PreviousValueSession = nullptr;
	};

	struct FValueAccessFailure
	{
		vs_interop_status Status;
		const char* Message = nullptr;
	};
	// Reserve cleanup slots before invoking script or cloning an exported value.
	// Even if publishing a result subsequently fails, its language-owned payload
	// is retired at the outer session safe point rather than by a C++ destructor.
	class FStagedValueResults final
	{
	public:
		FStagedValueResults(FSessionState& State, FValue& Return, std::vector<FValue>* Arguments = nullptr);
		~FStagedValueResults() noexcept;
		FStagedValueResults(const FStagedValueResults&) = delete;
		FStagedValueResults& operator=(const FStagedValueResults&) = delete;

	private:
		FSessionState& State;
		std::vector<std::pair<FValue*, FValueRoot>> Slots;
	};

	std::string CopyString(vs_interop_string_view View);
	bool IsValidString(vs_interop_string_view View);
	bool IsValidBytes(vs_interop_byte_view View);
	bool IsValidText(vs_interop_string_view View);
	vs_interop_string_view ViewOf(std::string_view Value);
	vs_interop_status RequireOwnerThread(FSessionState& State) noexcept;
	vs_interop_status RequireIdleSession(
	    FSessionState& State,
	    std::string_view Action) noexcept;
	void ReleaseCallableOrDefer(
	    FSessionState& State,
	    FRetainedCallableHandle Callable,
	    bool bOwnerThreadOnly);
	void SetError(FSessionState& State, std::string Message);
	std::shared_ptr<const FModuleEnvironment> GetModuleEnvironment(const ::vs_interop_module_environment* Environment);
	vs_interop_status ValidateModuleEnvironment(FSessionState& State,
	    const std::shared_ptr<const FModuleEnvironment>& Environment);
	vs_interop_status RequireMutableBindings(FSessionState& State);
	void InitializeFailedRunResult(vs_interop_run_result* OutResult) noexcept;
	vs_interop_status StoreRunResult(
	    FSessionState& State,
	    const FRunResult& Result,
	    vs_interop_run_result* OutResult);
	vs_interop_status TranslateException(FSessionState* State,
	    vs_interop_run_result* OutResult = nullptr) noexcept;
	vs_interop_status MakeBuffer(
	    std::vector<std::uint8_t> Bytes,
	    ::vs_interop_buffer** OutBuffer);
	vs_interop_status MakeTextBuffer(
	    std::string Text,
	    ::vs_interop_buffer** OutBuffer);
	bool IsValidRunResultOutput(const vs_interop_run_result* Result);
	bool IsWritableValue(const ::vs_interop_value* Value);
	// A private sidecar follows shared mutable backing across independently owned
	// C wrappers. It binds before script can add module-dependent children.
	std::shared_ptr<FSharedValueOwnership> GetValueSharedOwnership(const FValue& Data,
	    bool CreateTop = true);
	void SynchronizeValueOwnership(const ::vs_interop_value& Value);
	void BindValueOwnership(const FValue& Data, FSessionState& State,
	    const FNativeCallContext* Native = nullptr);
	bool ValueNeedsSessionOwner(const FValue& Data, const FSessionState* State,
	    const FNativeCallContext* Native = nullptr);
	void RejectUnownedModuleStorage(const FValue& Data, const FNativeCallContext* Native);

	const FValue* ReadValue(const ::vs_interop_value* Value,
	    FSessionState* ExpectedOwner = nullptr);
	void AssignValue(::vs_interop_value& Value, FValue Data, const ::vs_interop_value* Source = nullptr);
	void ResetValue(::vs_interop_value& Value);
	vs_interop_value_kind KindOf(const FValue& Value);

	vs_interop_status BuildArguments(
	    ::vs_interop_value* const* Arguments,
	    std::uint64_t ArgumentCount,
	    std::vector<FValue>& OutArguments,
	    bool bRequireWritable,
	    FSessionState* ExpectedOwner = nullptr);
	vs_interop_status CommitArguments(
	    ::vs_interop_value* const* Arguments,
	    std::uint64_t ArgumentCount,
	    const std::vector<FValue>& Values);

	bool ConvertNativeType(
	    const vs_interop_native_type_desc& Source,
	    FNativeTypeDesc& Out,
	    std::string& OutError,
	    std::size_t Depth = 0,
	    std::size_t* NodeCount = nullptr);
	bool ConvertNativeParam(
	    const vs_interop_native_param_desc& Source,
	    FNativeParamDesc& Out,
	    std::string& OutError,
	    std::size_t* NodeCount = nullptr);
	bool NativeThunkCallback(
	    const FNativeCallContext& Context,
	    FValue& OutReturn,
	    std::string& OutError) noexcept;
}

struct vs_interop_session
{
	std::shared_ptr<VeroScript::Interop::FSessionState> State;
};

struct vs_interop_value
{
	// Only handles returned by value_create/value_clone own this wrapper. Callback
	// views are stack-borrowed wrappers and destroy must leave them untouched.
	bool bOwnedHandle = false;
	mutable VeroScript::FValueRoot Root;
	mutable std::weak_ptr<VeroScript::Interop::FSessionState> Owner;
	// Callback views cannot outlive their stack frame. Keep its live session
	// context when final teardown has expired the session's weak control block.
	VeroScript::Interop::FSessionState* BorrowedOwner = nullptr;
	std::shared_ptr<VeroScript::Interop::FSharedValueOwnership> SharedOwnership;
	const VeroScript::FNativeCallContext* NativeContext = nullptr;
	std::optional<VeroScript::FValue> Storage;
	const VeroScript::FValue* BorrowedRead = nullptr;
	VeroScript::FValue* BorrowedWrite = nullptr;
	// C exposes UTF-16 as uint16_t while the core stores char16_t. Keep a copied
	// view to avoid strict-aliasing UB; opaque handles make this ABI-neutral.
	mutable std::vector<std::uint16_t> Utf16View;
	mutable bool bUtf16ViewValid = false;
};

namespace VeroScript::Interop
{
	inline FSessionState* ValueOwnerContext(const ::vs_interop_value& Value,
	    const std::shared_ptr<FSessionState>& LockedOwner) noexcept
	{
		return LockedOwner ? LockedOwner.get() : Value.bOwnedHandle ? nullptr
		                                                            : Value.BorrowedOwner;
	}
}

struct vs_interop_callable
{
	std::weak_ptr<VeroScript::Interop::FSessionState> State;
	VeroScript::FRetainedCallableHandle Handle;
	std::thread::id OwnerThread;
	bool bOwnerThreadOnly = true;
};

struct vs_interop_call_context
{
	const VeroScript::FNativeCallContext* Native = nullptr;
	VeroScript::Interop::FSessionState* Owner = nullptr;
	std::vector<vs_interop_value> Arguments;
	std::string Error;
};

struct vs_interop_buffer
{
	std::vector<std::uint8_t> Bytes;
};

struct vs_interop_package
{
	VeroScript::FPackageData Data;
	#if defined(VEROSCRIPT_WITH_APPLICATION)
	std::shared_ptr<const VeroScript::Application::FApplication> Application;
	#endif
	std::string LastError;
};

struct vs_interop_intercept_context
{
	const VeroScript::FInstanceCallInterceptContext* Native = nullptr;
	VeroScript::Interop::FSessionState* Owner = nullptr;
	VeroScript::FScriptError* ScriptError = nullptr;
	vs_interop_value Receiver;
	vs_interop_value ReturnSlot;
	std::vector<vs_interop_value> Arguments;
};

struct vs_interop_generic_type_context
{
	VeroScript::Interop::FGenericHostTypeThunk* Owner = nullptr;
	const std::vector<std::string>* TypeArguments = nullptr;
	VeroScript::FHostTypeDesc Prototype;
	std::string Error;
	bool bTypeSet = false;
};

struct vs_interop_external_serialize_context
{
	const VeroScript::FExternalObjectView* Object = nullptr;
	std::vector<vs_interop_value> Fields;
	std::vector<std::uint8_t>* Output = nullptr;
	std::string* Error = nullptr;
	bool bOutputSet = false;
};

struct vs_interop_external_deserialize_context
{
	VeroScript::Interop::FSessionState* Owner = nullptr;
	const std::uint8_t* Bytes = nullptr;
	std::size_t ByteCount = 0;
	std::uint32_t EncodedVersion = 0;
	std::vector<VeroScript::FExternalFieldAssignment>* Fields = nullptr;
	std::string* Error = nullptr;
};
