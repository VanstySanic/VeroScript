// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMobile.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(__APPLE__) || defined(__ANDROID__)
#include <exception>
#include <pthread.h>
#include <system_error>
#endif

namespace VeroScript::Mobile
{
	std::shared_ptr<vs_interop_aot_program> RetainAotProgram(const vs_interop_aot_program* Program)
	{
		vs_interop_aot_program* Copy = nullptr;
		if (vs_interop_aot_program_retain(Program, &Copy) != VS_INTEROP_STATUS_OK)
		{
			throw std::invalid_argument(TranslateMessage("cannot retain an AOT program"));
		}
		return {Copy, &vs_interop_aot_program_release};
	}

	FAotProgramCatalog::FAotProgramCatalog(std::vector<FAotProgramRegistration> Input) : Programs(std::move(Input))
	{
		std::sort(Programs.begin(), Programs.end(), [](const auto& A, const auto& B)
		{
			return A.Id < B.Id;
		});
		for (std::size_t I = 0; I < Programs.size(); ++I)
		{
			if (Programs[I].Id.empty() || Programs[I].Id.find('\0') != std::string::npos || !Programs[I].Program ||
			    (I && Programs[I - 1].Id == Programs[I].Id))
			{
				throw std::invalid_argument(TranslateMessage("invalid or duplicate AOT program ID"));
			}
		}
	}
	const vs_interop_aot_program* FAotProgramCatalog::Find(const std::string_view Id) const noexcept
	{
		const auto Found = std::lower_bound(Programs.begin(), Programs.end(), Id,
		    [](const auto& P, auto Value)
		{
			return P.Id < Value;
		});
		return Found != Programs.end() && Found->Id == Id ? Found->Program.get() : nullptr;
	}

	std::string TranslateMessage(const std::string_view EnglishMessage)
	{
		vs_interop_buffer* Buffer = nullptr;
		if (vs_interop_locale_translate({EnglishMessage.data(), EnglishMessage.size()}, &Buffer) != VS_INTEROP_STATUS_OK)
		{
			return std::string(EnglishMessage);
		}
		const std::unique_ptr<vs_interop_buffer, decltype(&vs_interop_buffer_destroy)> Owner(
		    Buffer, &vs_interop_buffer_destroy);
		const auto Bytes = vs_interop_buffer_view(Buffer);
		return Bytes.size == 0 ? std::string{} : std::string(reinterpret_cast<const char*>(Bytes.data), static_cast<std::size_t>(Bytes.size));
	}

	namespace
	{
		#if defined(__APPLE__) || defined(__ANDROID__)
		// Darwin queues and Android Java/native workers may have small stacks.
		// Keep the compiler's desktop stack budget without changing language limits.
		template <class F>
		FExecutionResult WithCompilerStack(F&& Execute)
		{
			constexpr std::size_t StackBytes = 8 * 1024 * 1024;
			#if defined(__APPLE__)
			const std::size_t CallerStackBytes = pthread_get_stacksize_np(pthread_self());
			#else
			pthread_attr_t CallerAttributes;
			int StackError = pthread_getattr_np(pthread_self(), &CallerAttributes);
			if (StackError)
			{
				throw std::system_error(StackError, std::generic_category(), TranslateMessage("mobile caller thread attributes"));
			}
			std::size_t CallerStackBytes = 0;
			StackError = pthread_attr_getstacksize(&CallerAttributes, &CallerStackBytes);
			pthread_attr_destroy(&CallerAttributes);
			if (StackError)
			{
				throw std::system_error(StackError, std::generic_category(), TranslateMessage("mobile caller thread stack"));
			}
			#endif
			if (CallerStackBytes >= StackBytes) return Execute();
			struct FWork
			{
				F& Function;
				FExecutionResult Result;
				std::exception_ptr Failure;
			} Work{Execute, {}, {}};
			pthread_attr_t Attributes;
			int Error = pthread_attr_init(&Attributes);
			if (Error)
			{
				throw std::system_error(Error, std::generic_category(), TranslateMessage("mobile execution thread attributes"));
			}
			Error = pthread_attr_setstacksize(&Attributes, StackBytes);
			pthread_t Thread{};
			if (!Error)
			{
				Error = pthread_create(&Thread, &Attributes, [](void* Data) noexcept -> void*
				{
					auto& Task = *static_cast<FWork*>(Data);
					try
					{
						Task.Result = Task.Function();
					}
					catch (...)
					{
						Task.Failure = std::current_exception();
					}
					return nullptr;
				}, &Work);
			}
			pthread_attr_destroy(&Attributes);
			if (Error)
			{
				throw std::system_error(Error, std::generic_category(), TranslateMessage("mobile execution thread creation"));
			}
			// The joined thread alone owns its session and borrowed input views.
			// A failed join cannot safely unwind their storage while it runs.
			if (pthread_join(Thread, nullptr))
			{
				std::terminate();
			}
			if (Work.Failure)
			{
				std::rethrow_exception(Work.Failure);
			}
			return std::move(Work.Result);
		}
		#endif

		vs_interop_string_view View(const std::string_view Text)
		{
			return {Text.data(), static_cast<std::uint64_t>(Text.size())};
		}

		void ValidatePath(const std::string_view Path, const bool bAllowVirtual = true)
		{
			if (Path.empty() || Path.size() > 4096 || Path.find('\0') != std::string_view::npos || (!std::filesystem::u8path(Path).is_absolute() && !(bAllowVirtual && Path.front() == '/')))
			{
				throw std::invalid_argument(TranslateMessage("mobile resource/cache paths must be nonempty absolute UTF-8 paths without NUL"));
			}
		}

		void ValidateOptions(const FSessionOptions& Options)
		{
			if (Options.ExecutionBackend != VS_INTEROP_BACKEND_VM && Options.ExecutionBackend != VS_INTEROP_BACKEND_MIXED &&
			    Options.ExecutionBackend != VS_INTEROP_BACKEND_JIT)
			{
				throw std::invalid_argument(TranslateMessage("invalid execution backend"));
			}
			if (!Options.JitBackendPath.empty())
			{
				ValidatePath(Options.JitBackendPath, false);
			}
			if (!Options.VmBackendPath.empty())
			{
				ValidatePath(Options.VmBackendPath, false);
			}
			if (!Options.AotProgramId.empty() && (!Options.AotPrograms || !Options.AotPrograms->Find(Options.AotProgramId)))
			{
				throw std::invalid_argument(TranslateMessage("AOT program ID is not registered for this host"));
			}
			ValidatePath(Options.StdlibDirectory);
			if (!Options.CacheDirectory.empty())
			{
				ValidatePath(Options.CacheDirectory, false);
			}
			if (Options.MaxCallDepth == 0 || Options.Sources.size() > 4096)
			{
				throw std::invalid_argument(TranslateMessage("mobile call depth must be positive and resource count cannot exceed 4096"));
			}
			std::size_t SourceBytes = 0;
			for (const auto& Resource : Options.Sources)
			{
				ValidatePath(Resource.Path);
				if (Resource.Source.size() > Options.MaxSourceBytes - SourceBytes)
				{
					throw std::invalid_argument(TranslateMessage("mobile source resources exceed the configured input budget"));
				}
				SourceBytes += Resource.Source.size();
			}
		}
	}

	struct FSession::FImpl
	{
		static void VS_INTEROP_CALL Print(vs_interop_string_view Line, void* UserData) noexcept
		{
			static_cast<FImpl*>(UserData)->Capture(Line, false);
		}

		static void VS_INTEROP_CALL Diagnose(vs_interop_string_view Line, void* UserData) noexcept
		{
			static_cast<FImpl*>(UserData)->Capture(Line, true);
		}
		static uint8_t VS_INTEROP_CALL WriteConsole(vs_interop_string_view Text, void* UserData) noexcept
		{
			auto* Self = static_cast<FImpl*>(UserData);
			Self->Capture(Text, false, false);
			return Self->bCaptureFailed ? 0 : 1;
		}

		void Capture(const vs_interop_string_view Line, const bool bDiagnostic, const bool bNewline = true) noexcept
		{
			try
			{
				const std::lock_guard<std::mutex> Lock(OutputMutex);
				auto& Destination = bDiagnostic ? Result.Diagnostics : Result.Output;
				const auto Remaining = Options.MaxCapturedOutputBytes - CapturedBytes;
				const auto Count = static_cast<std::size_t>(std::min<std::uint64_t>(Remaining, Line.size));
				if (Count)
				{
					Destination.append(Line.data, Count);
					CapturedBytes += Count;
				}
				if (bNewline && CapturedBytes < Options.MaxCapturedOutputBytes)
				{
					Destination.push_back('\n');
					++CapturedBytes;
				}
				else if (bNewline)
				{
					Result.bOutputTruncated = true;
				}
				Result.bOutputTruncated = Result.bOutputTruncated || Count < Line.size;
			}
			catch (...)
			{
				// C ABI callbacks must never propagate a foreign exception.
				bCaptureFailed = true;
			}
		}

		void RequireOwner() const
		{
			if (std::this_thread::get_id() != Owner)
			{
				throw std::logic_error(TranslateMessage("mobile session operations belong to their creator thread"));
			}
		}

		std::string LastError()
		{
			std::uint64_t Required = 0;
			const auto Status = vs_interop_session_copy_last_error(Session, nullptr, 0, &Required);
			if ((Status != VS_INTEROP_STATUS_OK && Status != VS_INTEROP_STATUS_BUFFER_TOO_SMALL) || Required == 0)
			{
				return TranslateMessage("mobile session operation failed");
			}
			if (Required > 64 * 1024 + 1)
			{
				Result.bOutputTruncated = true;
				return TranslateMessage("mobile error exceeds 64 KiB; consult the captured diagnostics");
			}
			std::string Text(static_cast<std::size_t>(Required), '\0');
			if (vs_interop_session_copy_last_error(Session, Text.data(), Required, &Required) != VS_INTEROP_STATUS_OK)
			{
				return TranslateMessage("mobile session operation failed");
			}
			Text.resize(static_cast<std::size_t>(Required - 1));
			return Text;
		}

		vs_interop_status Configure()
		{
			std::vector<vs_interop_source_overlay> Overlays;
			Overlays.reserve(Options.Sources.size());
			for (const auto& Resource : Options.Sources)
			{
				Overlays.push_back({View(Resource.Path), View(Resource.Source)});
			}
			const auto Stdlib = View(Options.StdlibDirectory);
			vs_interop_config Config{};
			Config.struct_size = sizeof(Config);
			Config.stdlib_search_paths = {&Stdlib, 1};
			Config.source_overlays = Overlays.data();
			Config.source_overlay_count = Overlays.size();
			Config.bytecode_cache_directory = View(Options.CacheDirectory);
			Config.use_bytecode_cache = Options.CacheDirectory.empty() ? 0 : 1;
			Config.execution_backend = Options.ExecutionBackend;
			Config.max_call_depth = Options.MaxCallDepth;
			Config.max_instructions = Options.MaxInstructions;
			Config.timeout_milliseconds = Options.TimeoutMilliseconds;
			Config.enable_execution_control = 1;
			Config.capability_policy_enabled = 1;
			Config.allowed_script_capabilities = VS_INTEROP_SCRIPT_CAP_CONSOLE_OUTPUT;
			Config.allow_circular_imports = 1;
			const auto Status = vs_interop_session_apply_config(Session, &Config);
			if (Status != VS_INTEROP_STATUS_OK)
			{
				return Status;
			}
			if (!Options.VmBackendPath.empty())
			{
				const auto BackendStatus = vs_interop_session_set_vm_backend(Session, View(Options.VmBackendPath));
				if (BackendStatus != VS_INTEROP_STATUS_OK) return BackendStatus;
			}
			if (!Options.JitBackendPath.empty())
			{
				const auto BackendStatus = vs_interop_session_set_jit_backend(Session, View(Options.JitBackendPath));
				if (BackendStatus != VS_INTEROP_STATUS_OK) return BackendStatus;
			}
			if (!Options.AotProgramId.empty())
			{
				const auto AotStatus = vs_interop_session_set_aot_program(Session, Options.AotPrograms->Find(Options.AotProgramId));
				if (AotStatus != VS_INTEROP_STATUS_OK) return AotStatus;
			}
			const vs_interop_memory_limits Limits{
			    sizeof(Limits), 0, Options.MaxBufferBytes, Options.MaxRetainedBytes};
			return vs_interop_session_set_memory_limits(Session, &Limits);
		}

		~FImpl()
		{
			// Destruction is an owner-thread operation, as for the underlying C ABI.
			if (Session)
			{
				if (vs_interop_session_destroy(Session) != VS_INTEROP_STATUS_OK)
				{
					std::terminate();
				}
			}
		}

		FSessionOptions Options;
		const std::thread::id Owner = std::this_thread::get_id();
		vs_interop_session* Session = nullptr;
		std::mutex OutputMutex;
		FExecutionResult Result;
		std::size_t CapturedBytes = 0;
		std::atomic<bool> bCaptureFailed{false};
		bool bExecuting = false;
	};

	FSession::FSession(FSessionOptions Options) : Impl(std::make_unique<FImpl>())
	{
		ValidateOptions(Options);
		Impl->Options = std::move(Options);
		vs_interop_session_options NativeOptions{};
		NativeOptions.struct_size = sizeof(NativeOptions);
		NativeOptions.print_callback = &FImpl::Print;
		NativeOptions.diagnostic_callback = &FImpl::Diagnose;
		NativeOptions.console_write_callback = &FImpl::WriteConsole;
		NativeOptions.user_data = Impl.get();
		if (vs_interop_session_create(&NativeOptions, &Impl->Session) != VS_INTEROP_STATUS_OK)
		{
			throw std::runtime_error(TranslateMessage("cannot create mobile VeroScript session"));
		}
		if (Impl->Configure() != VS_INTEROP_STATUS_OK)
		{
			throw std::runtime_error(Impl->LastError());
		}
	}

	FSession::~FSession() = default;

	FExecutionResult FSession::Execute(const std::string_view Source, const std::string_view FileLabel)
	{
		return ExecuteImpl(Source, FileLabel, false);
	}

	FExecutionResult FSession::ExecutePackage(vs_interop_byte_view Package, std::string_view FileLabel)
	{
		if ((Package.size && !Package.data) || Package.size > std::numeric_limits<std::size_t>::max())
		{
			throw std::invalid_argument(TranslateMessage("invalid mobile program bytes"));
		}
		return ExecuteImpl({Package.size ? reinterpret_cast<const char*>(Package.data) : "", static_cast<std::size_t>(Package.size)}, FileLabel, true);
	}

	void FSession::RequestCancel() noexcept
	{
		(void)vs_interop_session_request_cancel(Impl->Session);
	}

	vs_interop_session* FSession::GetNativeSession() const
	{
		Impl->RequireOwner();
		return Impl->Session;
	}

	FExecutionResult FSession::ExecuteImpl(const std::string_view Source, const std::string_view FileLabel, const bool bPackage)
	{
		Impl->RequireOwner();
		ValidatePath(FileLabel);
		if (Impl->bExecuting)
		{
			throw std::logic_error(TranslateMessage("mobile Execute cannot be reentered; use C ABI callback-context calls"));
		}
		if (Source.size() > (bPackage ? Impl->Options.MaxProgramBytes : Impl->Options.MaxSourceBytes))
		{
			throw std::invalid_argument(TranslateMessage("mobile entry source exceeds the configured input budget"));
		}
		Impl->Result = {};
		Impl->CapturedBytes = 0;
		Impl->bCaptureFailed = false;
		Impl->bExecuting = true;
		struct FExecutionScope
		{
			~FExecutionScope()
			{
				Impl.bExecuting = false;
			}
			FImpl& Impl;
		} Scope{*Impl};
		vs_interop_run_result NativeResult{};
		NativeResult.struct_size = sizeof(NativeResult);
		auto Status = VS_INTEROP_STATUS_OK;
		if (bPackage)
		{
			vs_interop_package* Package = nullptr;
			vs_interop_buffer* Error = nullptr;
			Status = vs_interop_package_read({reinterpret_cast<const std::uint8_t*>(Source.data()), Source.size()}, &Package, &Error);
			const std::unique_ptr<vs_interop_package, decltype(&vs_interop_package_destroy)> Owner(Package, &vs_interop_package_destroy);
			const std::unique_ptr<vs_interop_buffer, decltype(&vs_interop_buffer_destroy)> ErrorOwner(Error, &vs_interop_buffer_destroy);
			if (Status == VS_INTEROP_STATUS_OK)
			{
				Status = vs_interop_session_load_package(Impl->Session, Package, View(FileLabel), &NativeResult);
			}
			else if (Error)
			{
				const auto Bytes = vs_interop_buffer_view(Error);
				Impl->Result.Error.assign(Bytes.size ? reinterpret_cast<const char*>(Bytes.data) : "", static_cast<std::size_t>(Bytes.size));
			}
		}
		else
		{
			Status = vs_interop_session_load_source(Impl->Session, View(Source), View(FileLabel), &NativeResult);
		}
		if (Status == VS_INTEROP_STATUS_OK && NativeResult.success)
		{
			Status = vs_interop_session_run(Impl->Session, VS_INTEROP_RUN_SCRIPT, &NativeResult);
		}
		Impl->Result.bSuccess = Status == VS_INTEROP_STATUS_OK && NativeResult.success && !Impl->bCaptureFailed;
		Impl->Result.ExitCode = NativeResult.exit_code;
		if (!Impl->Result.bSuccess)
		{
			if (Impl->Result.Error.empty())
			{
				Impl->Result.Error = Impl->bCaptureFailed ? TranslateMessage("mobile output capture failed") : Impl->LastError();
			}
		}
		return std::move(Impl->Result);
	}

	struct FRunner::FImpl
	{
		FSessionOptions Options;
		mutable std::mutex Mutex;
		FSession* Active = nullptr;
		bool bExecuting = false;
		bool bCancelRequested = false;
	};

	FRunner::FRunner(FSessionOptions Options) : Impl(std::make_unique<FImpl>())
	{
		ValidateOptions(Options);
		Impl->Options = std::move(Options);
	}

	FRunner::~FRunner() = default;

	FExecutionResult FRunner::Execute(const std::string_view Source, const std::string_view FileLabel)
	{
		return ExecuteImpl(Source, FileLabel, false);
	}

	FExecutionResult FRunner::ExecutePackage(vs_interop_byte_view Package, std::string_view FileLabel)
	{
		if ((Package.size && !Package.data) || Package.size > std::numeric_limits<std::size_t>::max())
		{
			throw std::invalid_argument(TranslateMessage("invalid mobile program bytes"));
		}
		return ExecuteImpl({Package.size ? reinterpret_cast<const char*>(Package.data) : "", static_cast<std::size_t>(Package.size)}, FileLabel, true);
	}

	void FRunner::RequestCancel() noexcept
	{
		const std::lock_guard<std::mutex> Lock(Impl->Mutex);
		if (!Impl->bExecuting) return;
		Impl->bCancelRequested = true;
		if (Impl->Active)
		{
			Impl->Active->RequestCancel();
		}
	}

	bool FRunner::IsExecuting() const noexcept
	{
		const std::lock_guard<std::mutex> Lock(Impl->Mutex);
		return Impl->bExecuting;
	}

	FExecutionResult FRunner::ExecuteImpl(const std::string_view Source, const std::string_view FileLabel, const bool bPackage)
	{
		{
			const std::lock_guard<std::mutex> Lock(Impl->Mutex);
			if (Impl->bExecuting)
			{
				throw std::logic_error(TranslateMessage("concurrent execution on one mobile runner is unsupported"));
			}
			Impl->bExecuting = true;
			Impl->bCancelRequested = false;
		}
		struct FExecutionScope
		{
			~FExecutionScope()
			{
				const std::lock_guard<std::mutex> Lock(Impl.Mutex);
				Impl.Active = nullptr;
				Impl.bExecuting = false;
			}
			FImpl& Impl;
		} ExecutionScope{*Impl};
		auto Execute = [&]() -> FExecutionResult
		{
			FSession Session(Impl->Options);
			// Clear the borrowed pointer before owner-thread destruction, including
			// exception unwinding, while cancellation holds the same mutex.
			struct FActiveScope
			{
				~FActiveScope()
				{
					const std::lock_guard<std::mutex> Lock(Impl.Mutex);
					Impl.Active = nullptr;
				}
				FImpl& Impl;
			} ActiveScope{*Impl};
			{
				const std::lock_guard<std::mutex> Lock(Impl->Mutex);
				Impl->Active = &Session;
				if (Impl->bCancelRequested)
				{
					Session.RequestCancel();
				}
			}
			return bPackage ? Session.ExecutePackage({reinterpret_cast<const std::uint8_t*>(Source.data()), Source.size()}, FileLabel) : Session.Execute(Source, FileLabel);
		};
		#if defined(__APPLE__) || defined(__ANDROID__)
		return WithCompilerStack(Execute);
		#else
		return Execute();
		#endif
	}
}
