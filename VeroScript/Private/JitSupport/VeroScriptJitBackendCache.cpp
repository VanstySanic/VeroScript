// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitBackend.h"
#include "VeroScriptJitDiagnostics.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <new>
#include <utility>
#include <vector>

namespace VeroScript
{
	namespace
	{
		std::size_t SaturatingAdd(const std::size_t Left, const std::size_t Right) noexcept
		{
			const auto Maximum = std::numeric_limits<std::size_t>::max();
			return Right > Maximum - Left ? Maximum : Left + Right;
		}

		void SetCreationError(std::string& OutError, const std::string_view Message) noexcept
		{
			try
			{
				OutError.assign(Message);
			}
			catch (...)
			{
				OutError.clear();
			}
		}

		std::size_t HeapStringBytes(const std::string& Value) noexcept
		{
			if (Value.capacity() == 0) return 0;
			const auto Storage = reinterpret_cast<std::uintptr_t>(Value.data());
			const auto Object = reinterpret_cast<std::uintptr_t>(&Value);
			return Storage >= Object && Storage - Object < sizeof(Value)
			    ? 0
			    : SaturatingAdd(Value.capacity(), 1);
		}

		// A bounded scan lets a named scalar entry leave unrelated module bodies
		// to demand compilation. Unknown execution edges keep full preparation.
		struct FDirectPreparation
		{
			static bool HasScalarSignature(const FBytecodeFunction& Function) noexcept
			{
				// Match the flat primitive subset of the canonical function type.
				// Object arguments and results can invoke implicit lifetime hooks.
				std::string_view Type = Function.ClosureTypeName;
				if (Type.substr(0, 3) != "fn(") return false;
				Type.remove_prefix(3);
				const auto Close = Type.find(")->");
				if (Close == std::string_view::npos) return false;
				const auto Result = Type.substr(Close + 3);
				if (Result != "void" && !Runtime::IsPrimitiveValueTypeName(Result)) return false;
				Type = Type.substr(0, Close);
				for (std::size_t I = 0; I < Function.ParamCount; ++I)
				{
					const auto Comma = Type.find(',');
					if (!Runtime::IsPrimitiveValueTypeName(Type.substr(0, Comma))) return false;
					if (Comma == std::string_view::npos) return I + 1 == Function.ParamCount;
					Type.remove_prefix(Comma + 1);
				}
				return Function.ParamCount == 0 && Type.empty();
			}

			bool Scan(const FBytecodeModule& Module, const std::vector<std::uint8_t>& Code,
			    std::size_t Limit) noexcept
			{
				if (Code.size() > RemainingBytes) return false;
				RemainingBytes -= Code.size();
				for (std::size_t Ip = 0; Ip < Code.size();)
				{
					const auto Op = static_cast<EOpcode>(Code[Ip]);
					const auto Size = Bytecode::InstructionSize(Op);
					if (Size == 0 || Size > Code.size() - Ip) return false;
					switch (Op)
					{
					case EOpcode::Nop:
					case EOpcode::ConstI32:
					case EOpcode::ConstF64:
					case EOpcode::ConstBool:
					case EOpcode::LoadLocal:
					case EOpcode::StoreLocal:
					case EOpcode::IncLocal:
					case EOpcode::AddLocalImm:
					case EOpcode::AddLocalLocal:
					case EOpcode::JmpIfLocalGeImm:
					case EOpcode::Add:
					case EOpcode::Sub:
					case EOpcode::Mul:
					case EOpcode::Div:
					case EOpcode::Mod:
					case EOpcode::Eq:
					case EOpcode::Ne:
					case EOpcode::Lt:
					case EOpcode::Le:
					case EOpcode::Gt:
					case EOpcode::Ge:
					case EOpcode::Not:
					case EOpcode::Neg:
					case EOpcode::BitAnd:
					case EOpcode::BitOr:
					case EOpcode::BitXor:
					case EOpcode::BitNot:
					case EOpcode::Shl:
					case EOpcode::Shr:
					case EOpcode::Jmp:
					case EOpcode::JmpIfFalse:
					case EOpcode::Dup:
					case EOpcode::Pop:
					case EOpcode::Return:
						{
							break;
						}
					case EOpcode::Call:
						{
							const auto Target = Bytecode::ReadU16LE(Code.data() + Ip + 1);
							if (Target >= Module.Functions.size()) return false;
							const auto End = Indices.begin() + Count;
							if (std::find(Indices.begin(), End, Target) == End)
							{
								if (Count >= Limit || Count == Indices.size()) return false;
								Indices[Count++] = Target;
							}
							break;
						}
					default:
						return false;
					}
					Ip += Size;
				}
				return true;
			}

			bool Collect(const FBytecodeModule& Module, std::uint16_t Entry, std::size_t Limit) noexcept
			{
				Indices[Count++] = Entry;
				if (!Scan(Module, Module.InitCode, Limit)) return false;
				for (std::size_t I = 0; I < Count; ++I)
				{
					const auto& Function = Module.Functions[Indices[I]];
					if (Function.bIsMethod || Function.bReturnsRef || !Function.Captures.empty() ||
						!Function.ExceptionRegions.empty() || !HasScalarSignature(Function) ||
						!Scan(Module, Function.Code, Limit))
					{
						return false;
					}
				}
				return true;
			}

			std::array<std::uint16_t, 32> Indices;
			std::size_t Count = 0;
			std::size_t RemainingBytes = 16 * 1024;
		};
	}

	std::uint64_t AllocateJitProgramIdentity() noexcept
	{
		static std::atomic<std::uint64_t> Next{1};
		const std::uint64_t Identity = Next.fetch_add(1, std::memory_order_relaxed);
		// Reusing a program identity would turn a stale artifact into a valid one.
		if (Identity == 0)
		{
			std::terminate();
		}
		return Identity;
	}

	std::string_view GetJitHostTarget() noexcept
	{
		#if VEROSCRIPT_PLATFORM_ANDROID || VEROSCRIPT_PLATFORM_IOS
		return GetHostPlatformInfo().Platform;
		#else
		#if defined(_M_X64) || defined(__x86_64__)
		#if defined(_WIN32)
		return "windows-x86_64";
		#elif defined(__APPLE__)
		return "macos-x86_64";
		#else
		return "unix-x86_64";
		#endif
		#elif defined(_M_ARM64) || defined(__aarch64__)
		#if defined(_WIN32)
		return "windows-aarch64";
		#elif defined(__APPLE__)
		return "macos-aarch64";
		#else
		return "unix-aarch64";
		#endif
		#elif defined(_M_IX86) || defined(__i386__)
		#if defined(_WIN32)
		return "windows-x86";
		#else
		return "unix-x86";
		#endif
		#elif defined(__wasm32__)
		return "wasm32";
		#else
		return "unknown";
		#endif
		#endif
	}

	struct FJitBackendCache::FImpl
	{
		enum class ESlotState : std::uint8_t
		{
			Unvisited,
			Unsupported,
			Deferred,
			Ready,
		};

		struct FSlot
		{
			std::shared_ptr<const FJitCompiledFunction> Function;
			std::string Reason;
			const char* StaticReason = nullptr;
			EJitFallbackReason FallbackReason = EJitFallbackReason::None;
			EJitFallbackReason RuntimeFallback = EJitFallbackReason::None;
			std::uint64_t RetryEpoch = 0;
			std::uint32_t Heat = 0;
			ESlotState State = ESlotState::Unvisited;
			bool bHot = false;
			std::uint8_t DirectPreparationCount = 0;
		};

		void Fail(const std::string_view Message) noexcept
		{
			FailureFallback = "JIT backend failed";
			try
			{
				StoreReason(LastError, std::string(Message));
			}
			catch (...)
			{
				ClearReason(LastError);
			}
		}

		void ClearReason(std::string& Destination) noexcept
		{
			DiagnosticBytes -= HeapStringBytes(Destination);
			std::string{}.swap(Destination);
		}

		void StoreReason(std::string& Destination, std::string Reason) noexcept
		{
			DiagnosticBytes -= HeapStringBytes(Destination);
			Destination.swap(Reason);
			DiagnosticBytes += HeapStringBytes(Destination);
			// Diagnostics are retained payload too. Keep the static fallback when
			// the backend's text would exceed this generation's budget.
			if (Usage().RetainedBytes > Limits.RetainedBytes)
			{
				ClearReason(Destination);
				bResourceLimitReached = true;
			}
		}

		FJitResourceUsage Usage() const noexcept
		{
			FJitResourceUsage Result = Instance->GetResourceUsage();
			Result.bResourceLimitReached |= bResourceLimitReached;
			const std::size_t SlotBytes = Slots.capacity() > std::numeric_limits<std::size_t>::max() / sizeof(FSlot)
			    ? std::numeric_limits<std::size_t>::max()
			    : Slots.capacity() * sizeof(FSlot);
			Result.RetainedBytes = SaturatingAdd(Result.RetainedBytes,
			    SaturatingAdd(sizeof(FImpl), SaturatingAdd(SlotBytes,
			        SaturatingAdd(DiagnosticBytes, PublishedArtifacts * sizeof(FJitCompiledFunction)))));
			return Result;
		}

		std::shared_ptr<const IJitBackendFactory> Factory;
		std::unique_ptr<IJitBackendInstance> Instance;
		FJitProgramView Program;
		FJitPolicy Policy;
		FJitResourceLimits Limits;
		FJitStats Stats;
		std::vector<FSlot> Slots;
		std::string LastError;
		const char* FailureFallback = nullptr;
		std::uint64_t AvailableServices = 0;
		std::uint64_t RetryEpoch = 1;
		std::size_t CachedEntries = 0;
		std::size_t PublishedArtifacts = 0;
		std::size_t DiagnosticBytes = 0;
		std::uint64_t EagerGovernance = 0;
		bool bEagerComplete = false;
		bool bResourceLimitReached = false;
	};

	std::unique_ptr<FJitBackendCache> FJitBackendCache::Create(
	    std::shared_ptr<const IJitBackendFactory> Factory,
	    FJitProgramView Program,
	    FJitPolicy Policy,
	    const FJitResourceLimits Limits,
	    const std::uint64_t AvailableServices,
	    std::string& OutError)
	{
		OutError.clear();
		if (!Factory)
		{
			return nullptr;
		}
		if (!Program.Module || Program.Identity.Program == 0 || Program.Identity.Generation == 0 || (Program.Owner && Program.Owner.get() != Program.Module))
		{
			SetCreationError(OutError, "invalid JIT program view or identity");
			return nullptr;
		}
		const auto& Description = Factory->GetDescription();
		if (Description.bNativeCode && !GetHostPlatformInfo().bAllowsNativeJit)
		{
			SetCreationError(OutError, "native JIT execution is unavailable on this platform");
			return nullptr;
		}
		if (Description.Id.empty() || Description.Target.empty() || Description.CoreVersion != VEROSCRIPT_VERSION_CODE || Description.MaxInvocationDepth == 0 || (Description.Target != "portable-cpp" && Description.Target != GetJitHostTarget()) || (Description.bNativeCode && (Description.Target == "portable-cpp" || GetJitHostTarget() == "unknown")) || (Description.RequiredServices & ~AvailableServices) != 0)
		{
			SetCreationError(OutError, "incompatible JIT backend factory contract");
			return nullptr;
		}
		try
		{
			auto State = std::make_unique<FImpl>();
			State->Factory = std::move(Factory);
			State->Program = std::move(Program);
			State->Policy = Policy;
			State->Policy.MixedHotCallThreshold = std::max<std::uint32_t>(1, Policy.MixedHotCallThreshold);
			State->Limits = Limits;
			State->AvailableServices = AvailableServices;
			State->Instance = State->Factory->CreateInstance(State->Program, Limits, OutError);
			if (!State->Instance)
			{
				if (OutError.empty())
				{
					SetCreationError(OutError, "JIT backend initialization failed");
				}
				return nullptr;
			}
			const auto& Actual = State->Instance->GetDescription();
			if (Actual.Id != Description.Id || Actual.Target != Description.Target || Actual.CoreVersion != VEROSCRIPT_VERSION_CODE || Actual.MaxInvocationDepth == 0 || (Actual.Capabilities & ~Description.Capabilities) != 0 || (Actual.bNativeCode && !Description.bNativeCode) || (Actual.RequiredServices & ~AvailableServices) != 0)
			{
				State->Instance->Retire();
				SetCreationError(OutError, "incompatible JIT backend instance contract");
				return nullptr;
			}
			State->Slots.resize(std::min<std::size_t>(State->Program.Module->Functions.size(), 65536));
			return std::unique_ptr<FJitBackendCache>(new FJitBackendCache(std::move(State)));
		}
		catch (const std::bad_alloc&)
		{
			SetCreationError(OutError, "out of memory initializing JIT backend");
		}
		catch (const std::exception& Error)
		{
			SetCreationError(OutError, Error.what());
		}
		catch (...)
		{
			SetCreationError(OutError, "JIT backend initialization threw an unknown exception");
		}
		return nullptr;
	}

	FJitBackendCache::~FJitBackendCache()
	{
		// Entries retain their own code owner; retirement never executes script code.
		Impl->Slots.clear();
		Impl->Instance->Retire();
	}

	void FJitBackendCache::BeginRun() noexcept
	{
		Impl->ClearReason(Impl->LastError);
		Impl->FailureFallback = nullptr;
		NotifyResourcesChanged();
	}

	void FJitBackendCache::NotifyResourcesChanged() noexcept
	{
		if (++Impl->RetryEpoch == 0)
		{
			Impl->RetryEpoch = 1;
			for (auto& Slot : Impl->Slots)
			{
				Slot.RetryEpoch = 0;
			}
		}
	}

	bool FJitBackendCache::ShouldCompileOnCall(const std::uint16_t FunctionIndex) noexcept
	{
		if (FunctionIndex >= Impl->Slots.size())
		{
			return false;
		}
		auto& Slot = Impl->Slots[FunctionIndex];
		if (Slot.Heat < Impl->Policy.MixedHotCallThreshold)
		{
			++Slot.Heat;
		}
		if (Slot.Heat < Impl->Policy.MixedHotCallThreshold)
		{
			++Impl->Stats.HotnessDeferrals;
			return false;
		}
		if (!Slot.bHot)
		{
			Slot.bHot = true;
			++Impl->Stats.HotFunctions;
		}
		return true;
	}

	EJitCompilationResult FJitBackendCache::EnsureCompiled(const std::uint16_t FunctionIndex, const FExecutionCheck Check,
	    const std::uint64_t RequiredGovernance) noexcept
	{
		FImpl& State = *Impl;
		if (!Check.Poll())
		{
			State.Fail("JIT preparation stopped by host");
			return EJitCompilationResult::Failed;
		}
		if (FunctionIndex >= State.Slots.size())
		{
			State.Fail("invalid JIT function index");
			return EJitCompilationResult::Failed;
		}
		auto& Slot = State.Slots[FunctionIndex];
		if (Slot.State == FImpl::ESlotState::Ready)
		{
			if ((RequiredGovernance & ~Slot.Function->GovernanceCapabilities) != 0)
			{
				return EJitCompilationResult::Unsupported;
			}
			++State.Stats.CacheHits;
			return EJitCompilationResult::Ready;
		}
		if (Slot.State == FImpl::ESlotState::Unsupported)
		{
			return EJitCompilationResult::Unsupported;
		}
		if (Slot.State == FImpl::ESlotState::Deferred && Slot.RetryEpoch == State.RetryEpoch)
		{
			return EJitCompilationResult::Deferred;
		}
		// A retry replaces the previous decision. Release its diagnostic before
		// computing the resources available to the new compilation attempt.
		State.bEagerComplete = false;
		State.ClearReason(Slot.Reason);
		Slot.StaticReason = nullptr;
		Slot.FallbackReason = EJitFallbackReason::None;
		Slot.RuntimeFallback = EJitFallbackReason::None;
		const auto Defer = [&](const char* Reason, EJitFallbackReason Category = EJitFallbackReason::ResourceBudget)
		{
			Slot.State = FImpl::ESlotState::Deferred;
			Slot.RetryEpoch = State.RetryEpoch;
			Slot.StaticReason = Reason;
			Slot.FallbackReason = Category;
			return EJitCompilationResult::Deferred;
		};
		if (State.Policy.MaxCompileAttempts != 0 && State.Stats.CompileAttempts >= State.Policy.MaxCompileAttempts)
		{
			++State.Stats.CompileBudgetSkips;
			return Defer("compile budget exhausted", EJitFallbackReason::CompileBudget);
		}
		if (State.Policy.MaxCachedEntries != 0 && State.CachedEntries >= State.Policy.MaxCachedEntries)
		{
			++State.Stats.CacheCapacitySkips;
			return Defer("cache capacity exhausted", EJitFallbackReason::CacheCapacity);
		}
		const FJitResourceUsage Before = State.Usage();
		if (Before.ExecutableBytes > State.Limits.ExecutableBytes || Before.RetainedBytes > State.Limits.RetainedBytes)
		{
			State.bResourceLimitReached = true;
			++State.Stats.CacheCapacitySkips;
			return Defer("retained resource budget exhausted");
		}
		FJitCompileRequest Request;
		Request.Program = State.Program;
		Request.RequiredGovernance = RequiredGovernance;
		Request.ExecutionCheck = Check;
		Request.FunctionIndex = FunctionIndex;
		Request.AvailableServices = State.AvailableServices;
		Request.AvailableResources.ExecutableBytes = State.Limits.ExecutableBytes - Before.ExecutableBytes;
		Request.AvailableResources.RetainedBytes = State.Limits.RetainedBytes - Before.RetainedBytes;
		FJitCompileResponse Response;
		++State.Stats.CompileAttempts;
		try
		{
			const EJitCompilationResult Result = State.Instance->Compile(Request, Response);
			if (!Check.Poll())
			{
				State.Instance->Discard(Response.Function);
				State.Fail("JIT preparation stopped by host");
				return EJitCompilationResult::Failed;
			}
			switch (Result)
			{
			case EJitCompilationResult::Ready:
				{
					const auto& Function = Response.Function;
					if (!Function || !Function->Executable || Function->Identity != State.Program.Identity || Function->FunctionIndex != FunctionIndex || (Function->ProgramOwner && Function->ProgramOwner.get() != State.Program.Module) || (Function->RequiredServices & ~State.AvailableServices) != 0 || (State.Program.Owner && (Function->ProgramOwner != State.Program.Owner || Function->ProgramOwner.owner_before(State.Program.Owner) || State.Program.Owner.owner_before(Function->ProgramOwner))))
					{
						State.Instance->Discard(Response.Function);
						State.Fail("JIT compiler returned an invalid artifact");
						return EJitCompilationResult::Failed;
					}
					if ((RequiredGovernance & ~Function->GovernanceCapabilities) != 0)
					{
						State.Instance->Discard(Response.Function);
						Slot.State = FImpl::ESlotState::Unsupported;
						Slot.StaticReason = "artifact does not cover execution governance";
						Slot.FallbackReason = EJitFallbackReason::Governance;
						++State.CachedEntries;
						++State.Stats.UnsupportedEntries;
						return EJitCompilationResult::Unsupported;
					}
					const FJitResourceUsage After = State.Usage();
					if (After.ExecutableBytes > State.Limits.ExecutableBytes || SaturatingAdd(After.RetainedBytes, sizeof(FJitCompiledFunction)) > State.Limits.RetainedBytes)
					{
						State.Instance->Discard(Response.Function);
						Response.Function.reset();
						State.bResourceLimitReached = true;
						++State.Stats.CacheCapacitySkips;
						return Defer("compiled artifact exceeds resource budget");
					}
					if (!State.Instance->Publish(*Function, Response.Reason))
					{
						State.Instance->Discard(Response.Function);
						State.Fail(Response.Reason.empty() ? "JIT artifact publication failed" : std::string_view(Response.Reason));
						return EJitCompilationResult::Failed;
					}
					Slot.Function = std::move(Response.Function);
					++State.PublishedArtifacts;
					Slot.FallbackReason = Slot.Function->FallbackReason == EJitFallbackReason::None ? Response.FallbackReason : Slot.Function->FallbackReason;
					if (Slot.FallbackReason == EJitFallbackReason::None && (Slot.Function->bPreferVmForExecution || Slot.Function->bPreferVmForNestedExecution))
					{
						Slot.FallbackReason = EJitFallbackReason::CostModel;
					}
					Slot.StaticReason = Slot.FallbackReason == EJitFallbackReason::None ? nullptr : JitFallbackReasonName(Slot.FallbackReason);
					// Successful native publication still drops obsolete retry diagnostics.
					// Keep a Ready diagnostic only when it describes an actual VM decision.
					if (Slot.FallbackReason != EJitFallbackReason::None)
					{
						State.StoreReason(Slot.Reason, std::move(Response.Reason));
					}
					Slot.State = FImpl::ESlotState::Ready;
					++State.CachedEntries;
					++State.Stats.CompiledEntries;
					return Result;
				}
			case EJitCompilationResult::Unsupported:
				{
					if (Response.Function)
					{
						State.Instance->Discard(Response.Function);
						State.Fail("unsupported JIT compilation returned an artifact");
						return EJitCompilationResult::Failed;
					}
					Slot.State = FImpl::ESlotState::Unsupported;
					Slot.StaticReason = "function is unsupported by this backend";
					Slot.FallbackReason = Response.FallbackReason == EJitFallbackReason::None ? EJitFallbackReason::UnsupportedOpcode : Response.FallbackReason;
					State.StoreReason(Slot.Reason, std::move(Response.Reason));
					++State.CachedEntries;
					++State.Stats.UnsupportedEntries;
					return Result;
				}
			case EJitCompilationResult::Deferred:
				{
					State.Instance->Discard(Response.Function);
					State.StoreReason(Slot.Reason, std::move(Response.Reason));
					return Defer("backend deferred compilation");
				}
			case EJitCompilationResult::Failed:
				{
					State.Instance->Discard(Response.Function);
					State.Fail(Response.Reason.empty() ? "JIT compilation failed" : std::string_view(Response.Reason));
					return Result;
				}
			default:
				{
					break;
				}
			}
			State.Fail("JIT compiler returned an invalid result");
		}
		catch (const std::bad_alloc&)
		{
			State.Fail("out of memory compiling JIT function");
		}
		catch (const std::exception& Error)
		{
			State.Fail(Error.what());
		}
		catch (...)
		{
			State.Fail("JIT compilation threw an unknown exception");
		}
		State.Instance->Discard(Response.Function);
		return EJitCompilationResult::Failed;
	}

	bool FJitBackendCache::Prepare(const EExecutionBackend Mode, const std::size_t PreferredFunctionIndex,
	    const FExecutionCheck Check, const std::uint64_t RequiredGovernance) noexcept
	{
		if (Mode != EExecutionBackend::Jit)
		{
			return true;
		}
		if (Impl->bEagerComplete && Impl->EagerGovernance == RequiredGovernance)
		{
			if (!Check.Poll())
			{
				Impl->Fail("JIT preparation stopped by host");
				return false;
			}
			Impl->Stats.CacheHits += Impl->CachedEntries;
			return true;
		}
		const std::size_t Count = Impl->Slots.size();
		// A short-lived function call cannot amortize compiling unrelated bodies.
		// Keep full-module entry preparation: native call plans can bind direct and
		// virtual targets while those callees are already published.
		const bool bModuleEntry = PreferredFunctionIndex >= Count || (Impl->Program.Module->MainFunctionIndex >= 0 && PreferredFunctionIndex == static_cast<std::size_t>(Impl->Program.Module->MainFunctionIndex));
		const bool bShortLived = !bModuleEntry && (!Impl->Program.Owner || !Impl->Policy.bReuseSessionCode);
		const std::size_t Requested = Impl->Policy.EagerCompileBudget == 0
		    ? Count
		    : std::min(Count, Impl->Policy.EagerCompileBudget);
		const std::size_t Limit = bShortLived ? std::min<std::size_t>(Requested, 1u) : Requested;
		if (!bModuleEntry && !bShortLived && Count >= 128 && Limit > 1 && RequiredGovernance == 0)
		{
			auto& Entry = Impl->Slots[PreferredFunctionIndex];
			if (Entry.DirectPreparationCount != 0)
			{
				if (!Check.Poll())
				{
					Impl->Fail("JIT preparation stopped by host");
					return false;
				}
				Impl->Stats.CacheHits += Entry.DirectPreparationCount;
				Impl->Stats.EagerCompileSkips += Count - Entry.DirectPreparationCount;
				return true;
			}
			FDirectPreparation Plan;
			if (Plan.Collect(*Impl->Program.Module, static_cast<std::uint16_t>(PreferredFunctionIndex), Limit))
			{
				bool bComplete = true;
				for (std::size_t I = 0; I < Plan.Count; ++I)
				{
					const auto Result = EnsureCompiled(Plan.Indices[I], Check, RequiredGovernance);
					if (Result == EJitCompilationResult::Failed) return false;
					bComplete &= Result == EJitCompilationResult::Ready || Result == EJitCompilationResult::Unsupported;
				}
				if (bComplete)
				{
					Entry.DirectPreparationCount = static_cast<std::uint8_t>(Plan.Count);
				}
				Impl->Stats.EagerCompileSkips += Count - Plan.Count;
				return true;
			}
		}
		Impl->Stats.EagerCompileSkips += Count - Limit;
		std::size_t Considered = 0;
		if (Considered < Limit && PreferredFunctionIndex < Count)
		{
			if (EnsureCompiled(static_cast<std::uint16_t>(PreferredFunctionIndex), Check, RequiredGovernance) == EJitCompilationResult::Failed)
			{
				return false;
			}
			++Considered;
		}
		for (std::size_t Index = 0; Index < Count && Considered < Limit; ++Index)
		{
			if (Index == PreferredFunctionIndex)
			{
				continue;
			}
			if (EnsureCompiled(static_cast<std::uint16_t>(Index), Check, RequiredGovernance) == EJitCompilationResult::Failed)
			{
				return false;
			}
			++Considered;
		}
		Impl->bEagerComplete = Limit == Count && std::all_of(Impl->Slots.begin(), Impl->Slots.end(), [RequiredGovernance](const auto& Slot)
		{
			return Slot.State == FImpl::ESlotState::Unsupported || (Slot.State == FImpl::ESlotState::Ready && (RequiredGovernance & ~Slot.Function->GovernanceCapabilities) == 0);
		});
		Impl->EagerGovernance = RequiredGovernance;
		return true;
	}

	std::shared_ptr<const FJitCompiledFunction> FJitBackendCache::Find(const std::uint16_t FunctionIndex) const noexcept
	{
		return FunctionIndex < Impl->Slots.size() ? Impl->Slots[FunctionIndex].Function : nullptr;
	}

	const FJitCompiledFunction* FJitBackendCache::FindBorrowed(const std::uint16_t FunctionIndex) const noexcept
	{
		return FunctionIndex < Impl->Slots.size() ? Impl->Slots[FunctionIndex].Function.get() : nullptr;
	}

	bool FJitBackendCache::IsUnsupported(const std::uint16_t FunctionIndex) const noexcept
	{
		return FunctionIndex < Impl->Slots.size() && Impl->Slots[FunctionIndex].State == FImpl::ESlotState::Unsupported;
	}

	EJitInvocationResult FJitBackendCache::Invoke(const std::uint16_t FunctionIndex, FJitCallContext& Context) noexcept
	{
		const auto Function = Find(FunctionIndex);
		if (!Function)
		{
			return EJitInvocationResult::NotEntered;
		}
		if (!Context.Services)
		{
			Impl->Fail("missing JIT runtime services");
			return EJitInvocationResult::Failed;
		}
		auto& Services = *Context.Services;
		const auto Fail = [&](const std::string_view Error)
		{
			Impl->Fail(Error);
			Services.ReportError(Error);
			++Impl->Stats.Failures;
			return EJitInvocationResult::Failed;
		};
		if (Context.Program && (Context.Program->Identity != Impl->Program.Identity || Context.Program->Module != Impl->Program.Module))
		{
			return Fail("JIT invocation belongs to a different program");
		}
		if (Context.FunctionIndex != FunctionIndex || Context.bEntered || Context.bHasReturn || Context.ArgumentCount != Impl->Program.Module->Functions[FunctionIndex].ParamCount || (Context.ArgumentCount != 0 && !Context.Arguments))
		{
			return Fail("invalid JIT invocation context");
		}
		if (Services.HasError())
		{
			return EJitInvocationResult::Failed;
		}
		if (Services.HasPendingControlTransfer())
		{
			return EJitInvocationResult::ControlTransfer;
		}
		const auto* Governance = Services.GetExecutionGovernanceServices();
		if (Governance && (Governance->GetRequiredCapabilities() & ~Function->GovernanceCapabilities) != 0)
		{
			Context.FallbackReason = EJitFallbackReason::Governance;
			Impl->Slots[FunctionIndex].RuntimeFallback = Context.FallbackReason;
			++Impl->Stats.Fallbacks;
			return EJitInvocationResult::NotEntered;
		}
		Context.Program = &Impl->Program;
		Impl->Slots[FunctionIndex].RuntimeFallback = EJitFallbackReason::None;
		++Impl->Stats.DispatchAttempts;
		try
		{
			const EJitInvocationResult Result = Function->Executable->Invoke(Context);
			if (Context.bEntered && Context.ExecutionKind == EJitExecutionKind::Unknown && Function->ExecutionKind != EJitExecutionKind::Unknown && Context.FallbackReason == EJitFallbackReason::None)
			{
				Context.FallbackReason = EJitFallbackReason::RuntimeGuard;
			}
			if (Context.FallbackReason != EJitFallbackReason::None)
			{
				Impl->Slots[FunctionIndex].RuntimeFallback = Context.FallbackReason;
			}
			switch (Result)
			{
			case EJitInvocationResult::NotEntered:
				{
					if (Context.bEntered || Context.bHasReturn || Services.HasError() || Services.HasPendingControlTransfer())
					{
						return Fail("JIT backend requested fallback after entering execution");
					}
					++Impl->Stats.Fallbacks;
					Impl->Slots[FunctionIndex].RuntimeFallback = Context.FallbackReason == EJitFallbackReason::None ? EJitFallbackReason::RuntimeGuard : Context.FallbackReason;
					return Result;
				}
			case EJitInvocationResult::Returned:
				{
					const auto& Signature = Impl->Program.Module->Functions[FunctionIndex];
					if (!Context.bEntered || Services.HasError() || Services.HasPendingControlTransfer() || Context.bHasReturn == Signature.bReturnsVoid)
					{
						return Fail("invalid JIT return state");
					}
					if (!Signature.bReturnsVoid && !Services.PrepareReturn(Signature, Context.ReturnValue))
					{
						return Fail("invalid JIT return value or reference");
					}
					++Impl->Stats.Dispatches;
					return Result;
				}
			case EJitInvocationResult::ControlTransfer:
				{
					if (!Context.bEntered || !Services.HasPendingControlTransfer())
					{
						return Fail("JIT backend returned control transfer without pending control flow");
					}
					return Result;
				}
			case EJitInvocationResult::Failed:
				{
					if (!Services.HasError())
					{
						return Fail("JIT backend failed without a diagnostic");
					}
					++Impl->Stats.Failures;
					return Result;
				}
			default:
				{
					break;
				}
			}
			return Fail("JIT backend returned an invalid invocation result");
		}
		catch (const std::bad_alloc&)
		{
			return Fail("out of memory executing JIT function");
		}
		catch (const std::exception& Error)
		{
			return Fail(Error.what());
		}
		catch (...)
		{
			return Fail("JIT execution threw an unknown exception");
		}
	}

	const FJitProgramView& FJitBackendCache::GetProgram() const noexcept
	{
		return Impl->Program;
	}

	const FJitBackendDescription& FJitBackendCache::GetDescription() const noexcept
	{
		return Impl->Instance->GetDescription();
	}

	const std::shared_ptr<const IJitBackendFactory>& FJitBackendCache::GetFactory() const noexcept
	{
		return Impl->Factory;
	}

	std::size_t FJitBackendCache::MinimumRetainedBytes(const std::size_t FunctionCount) noexcept
	{
		return SaturatingAdd(sizeof(FImpl), std::min<std::size_t>(FunctionCount, 65536) * sizeof(FImpl::FSlot));
	}

	FJitResourceUsage FJitBackendCache::GetResourceUsage() const noexcept
	{
		return Impl->Usage();
	}

	FJitStats FJitBackendCache::GetStats() const noexcept
	{
		auto Stats = Impl->Stats;
		const auto Backend = Impl->Instance->GetStatistics();
		Stats.ControlledNativeExecutions = SaturatingAdd(Stats.ControlledNativeExecutions, Backend.ControlledNativeExecutions);
		Stats.AotExecutions = SaturatingAdd(Stats.AotExecutions, Backend.AotExecutions);
		Stats.NativeLeafEntries = SaturatingAdd(Stats.NativeLeafEntries, Backend.NativeLeafEntries);
		Stats.NativeCfgEntries = SaturatingAdd(Stats.NativeCfgEntries, Backend.NativeCfgEntries);
		Stats.BridgeTrampolineEntries = SaturatingAdd(Stats.BridgeTrampolineEntries, Backend.BridgeTrampolineEntries);
		Stats.LinearTrampolineEntries = SaturatingAdd(Stats.LinearTrampolineEntries, Backend.LinearTrampolineEntries);
		Stats.VmTrampolineEntries = SaturatingAdd(Stats.VmTrampolineEntries, Backend.VmTrampolineEntries);
		Stats.DispatchAttempts = SaturatingAdd(Stats.DispatchAttempts, Backend.DispatchAttempts);
		Stats.Dispatches = SaturatingAdd(Stats.Dispatches, Backend.Dispatches);
		Stats.Fallbacks = SaturatingAdd(Stats.Fallbacks, Backend.Fallbacks);
		Stats.CostModelVmFallbacks = SaturatingAdd(Stats.CostModelVmFallbacks, Backend.CostModelVmFallbacks);
		Stats.Failures = SaturatingAdd(Stats.Failures, Backend.Failures);
		Stats.ArrayFastPathPushes = SaturatingAdd(Stats.ArrayFastPathPushes, Backend.ArrayFastPathPushes);
		Stats.ArrayFastPathGets = SaturatingAdd(Stats.ArrayFastPathGets, Backend.ArrayFastPathGets);
		Stats.ArrayFastPathSets = SaturatingAdd(Stats.ArrayFastPathSets, Backend.ArrayFastPathSets);
		Stats.ScalarMapFastPathGets = SaturatingAdd(Stats.ScalarMapFastPathGets, Backend.ScalarMapFastPathGets);
		Stats.ScalarMapFastPathSets = SaturatingAdd(Stats.ScalarMapFastPathSets, Backend.ScalarMapFastPathSets);
		Stats.NativeTypedLoopExecutions = SaturatingAdd(Stats.NativeTypedLoopExecutions, Backend.NativeTypedLoopExecutions);
		Stats.ScalarMethodFastPathI32 = SaturatingAdd(Stats.ScalarMethodFastPathI32, Backend.ScalarMethodFastPathI32);
		Stats.ScalarMethodFastPathU32 = SaturatingAdd(Stats.ScalarMethodFastPathU32, Backend.ScalarMethodFastPathU32);
		Stats.ScalarMethodFastPathF64 = SaturatingAdd(Stats.ScalarMethodFastPathF64, Backend.ScalarMethodFastPathF64);
		Stats.ScalarMethodFastPathMixed = SaturatingAdd(Stats.ScalarMethodFastPathMixed, Backend.ScalarMethodFastPathMixed);
		Stats.ScalarFunctionFastPathI32 = SaturatingAdd(Stats.ScalarFunctionFastPathI32, Backend.ScalarFunctionFastPathI32);
		Stats.ScalarFunctionFastPathU32 = SaturatingAdd(Stats.ScalarFunctionFastPathU32, Backend.ScalarFunctionFastPathU32);
		Stats.ScalarFunctionFastPathF64 = SaturatingAdd(Stats.ScalarFunctionFastPathF64, Backend.ScalarFunctionFastPathF64);
		Stats.ScalarFunctionFastPathMixed = SaturatingAdd(Stats.ScalarFunctionFastPathMixed, Backend.ScalarFunctionFastPathMixed);
		Stats.ScalarNativeFastPathCalls = SaturatingAdd(Stats.ScalarNativeFastPathCalls, Backend.ScalarNativeFastPathCalls);
		Stats.DirectNativeCalls = SaturatingAdd(Stats.DirectNativeCalls, Backend.DirectNativeCalls);
		Stats.InlinedCallSites = SaturatingAdd(Stats.InlinedCallSites, Backend.InlinedCallSites);
		Stats.LinearFrameReuses = SaturatingAdd(Stats.LinearFrameReuses, Backend.LinearFrameReuses);
		Stats.LinearScratchReuses = SaturatingAdd(Stats.LinearScratchReuses, Backend.LinearScratchReuses);
		Stats.LinearScratchAllocations = SaturatingAdd(Stats.LinearScratchAllocations, Backend.LinearScratchAllocations);
		Stats.LinearArgumentScratchLeases = SaturatingAdd(Stats.LinearArgumentScratchLeases, Backend.LinearArgumentScratchLeases);
		Stats.LinearArgumentScratchReuses = SaturatingAdd(Stats.LinearArgumentScratchReuses, Backend.LinearArgumentScratchReuses);
		Stats.NativeLeafExecutions = SaturatingAdd(Stats.NativeLeafExecutions, Backend.NativeLeafExecutions);
		Stats.NativeCfgExecutions = SaturatingAdd(Stats.NativeCfgExecutions, Backend.NativeCfgExecutions);
		Stats.BridgeExecutions = SaturatingAdd(Stats.BridgeExecutions, Backend.BridgeExecutions);
		Stats.LinearExecutions = SaturatingAdd(Stats.LinearExecutions, Backend.LinearExecutions);
		Stats.VmTrampolineExecutions = SaturatingAdd(Stats.VmTrampolineExecutions, Backend.VmTrampolineExecutions);
		return Stats;
	}

	void FJitBackendCache::ResetExecutionStatistics() noexcept
	{
		Impl->Stats.DispatchAttempts = 0;
		Impl->Stats.Dispatches = 0;
		Impl->Stats.Fallbacks = 0;
		Impl->Stats.CostModelVmFallbacks = 0;
		Impl->Stats.Failures = 0;
		Impl->Stats.ArrayFastPathPushes = 0;
		Impl->Stats.ArrayFastPathGets = 0;
		Impl->Stats.ArrayFastPathSets = 0;
		Impl->Stats.ScalarMapFastPathGets = 0;
		Impl->Stats.ScalarMapFastPathSets = 0;
		Impl->Stats.NativeTypedLoopExecutions = 0;
		Impl->Stats.ScalarMethodFastPathI32 = 0;
		Impl->Stats.ScalarMethodFastPathU32 = 0;
		Impl->Stats.ScalarMethodFastPathF64 = 0;
		Impl->Stats.ScalarMethodFastPathMixed = 0;
		Impl->Stats.ScalarFunctionFastPathI32 = 0;
		Impl->Stats.ScalarFunctionFastPathU32 = 0;
		Impl->Stats.ScalarFunctionFastPathF64 = 0;
		Impl->Stats.ScalarFunctionFastPathMixed = 0;
		Impl->Stats.ScalarNativeFastPathCalls = 0;
		Impl->Stats.DirectNativeCalls = 0;
		Impl->Stats.ControlledNativeExecutions = 0;
		Impl->Stats.AotExecutions = 0;
		Impl->Stats.LinearFrameReuses = 0;
		Impl->Stats.LinearScratchReuses = 0;
		Impl->Stats.LinearScratchAllocations = 0;
		Impl->Stats.LinearArgumentScratchLeases = 0;
		Impl->Stats.LinearArgumentScratchReuses = 0;
		Impl->Stats.NativeLeafExecutions = 0;
		Impl->Stats.NativeCfgExecutions = 0;
		Impl->Stats.BridgeExecutions = 0;
		Impl->Stats.LinearExecutions = 0;
		Impl->Stats.VmTrampolineExecutions = 0;
		Impl->Instance->ResetExecutionStatistics();
	}

	void FJitBackendCache::AccumulateTransientStatistics(const FJitStats& Statistics) noexcept
	{
		Impl->Stats.CompileAttempts = SaturatingAdd(Impl->Stats.CompileAttempts, Statistics.CompileAttempts);
		Impl->Stats.CacheHits = SaturatingAdd(Impl->Stats.CacheHits, Statistics.CacheHits);
		Impl->Stats.CompiledEntries = SaturatingAdd(Impl->Stats.CompiledEntries, Statistics.CompiledEntries);
		Impl->Stats.NativeLeafEntries = SaturatingAdd(Impl->Stats.NativeLeafEntries, Statistics.NativeLeafEntries);
		Impl->Stats.NativeCfgEntries = SaturatingAdd(Impl->Stats.NativeCfgEntries, Statistics.NativeCfgEntries);
		Impl->Stats.BridgeTrampolineEntries = SaturatingAdd(Impl->Stats.BridgeTrampolineEntries, Statistics.BridgeTrampolineEntries);
		Impl->Stats.LinearTrampolineEntries = SaturatingAdd(Impl->Stats.LinearTrampolineEntries, Statistics.LinearTrampolineEntries);
		Impl->Stats.VmTrampolineEntries = SaturatingAdd(Impl->Stats.VmTrampolineEntries, Statistics.VmTrampolineEntries);
		Impl->Stats.UnsupportedEntries = SaturatingAdd(Impl->Stats.UnsupportedEntries, Statistics.UnsupportedEntries);
		Impl->Stats.DispatchAttempts = SaturatingAdd(Impl->Stats.DispatchAttempts, Statistics.DispatchAttempts);
		Impl->Stats.Dispatches = SaturatingAdd(Impl->Stats.Dispatches, Statistics.Dispatches);
		Impl->Stats.Fallbacks = SaturatingAdd(Impl->Stats.Fallbacks, Statistics.Fallbacks);
		Impl->Stats.CostModelVmFallbacks = SaturatingAdd(Impl->Stats.CostModelVmFallbacks, Statistics.CostModelVmFallbacks);
		Impl->Stats.Failures = SaturatingAdd(Impl->Stats.Failures, Statistics.Failures);
		Impl->Stats.CacheInvalidations = SaturatingAdd(Impl->Stats.CacheInvalidations, Statistics.CacheInvalidations);
		Impl->Stats.HotnessDeferrals = SaturatingAdd(Impl->Stats.HotnessDeferrals, Statistics.HotnessDeferrals);
		Impl->Stats.HotFunctions = SaturatingAdd(Impl->Stats.HotFunctions, Statistics.HotFunctions);
		Impl->Stats.CompileBudgetSkips = SaturatingAdd(Impl->Stats.CompileBudgetSkips, Statistics.CompileBudgetSkips);
		Impl->Stats.CacheCapacitySkips = SaturatingAdd(Impl->Stats.CacheCapacitySkips, Statistics.CacheCapacitySkips);
		Impl->Stats.EagerCompileSkips = SaturatingAdd(Impl->Stats.EagerCompileSkips, Statistics.EagerCompileSkips);
		Impl->Stats.ArrayFastPathPushes = SaturatingAdd(Impl->Stats.ArrayFastPathPushes, Statistics.ArrayFastPathPushes);
		Impl->Stats.ArrayFastPathGets = SaturatingAdd(Impl->Stats.ArrayFastPathGets, Statistics.ArrayFastPathGets);
		Impl->Stats.ArrayFastPathSets = SaturatingAdd(Impl->Stats.ArrayFastPathSets, Statistics.ArrayFastPathSets);
		Impl->Stats.ScalarMapFastPathGets = SaturatingAdd(Impl->Stats.ScalarMapFastPathGets, Statistics.ScalarMapFastPathGets);
		Impl->Stats.ScalarMapFastPathSets = SaturatingAdd(Impl->Stats.ScalarMapFastPathSets, Statistics.ScalarMapFastPathSets);
		Impl->Stats.NativeTypedLoopExecutions = SaturatingAdd(Impl->Stats.NativeTypedLoopExecutions, Statistics.NativeTypedLoopExecutions);
		Impl->Stats.ScalarMethodFastPathI32 = SaturatingAdd(Impl->Stats.ScalarMethodFastPathI32, Statistics.ScalarMethodFastPathI32);
		Impl->Stats.ScalarMethodFastPathU32 = SaturatingAdd(Impl->Stats.ScalarMethodFastPathU32, Statistics.ScalarMethodFastPathU32);
		Impl->Stats.ScalarMethodFastPathF64 = SaturatingAdd(Impl->Stats.ScalarMethodFastPathF64, Statistics.ScalarMethodFastPathF64);
		Impl->Stats.ScalarMethodFastPathMixed = SaturatingAdd(Impl->Stats.ScalarMethodFastPathMixed, Statistics.ScalarMethodFastPathMixed);
		Impl->Stats.ScalarFunctionFastPathI32 = SaturatingAdd(Impl->Stats.ScalarFunctionFastPathI32, Statistics.ScalarFunctionFastPathI32);
		Impl->Stats.ScalarFunctionFastPathU32 = SaturatingAdd(Impl->Stats.ScalarFunctionFastPathU32, Statistics.ScalarFunctionFastPathU32);
		Impl->Stats.ScalarFunctionFastPathF64 = SaturatingAdd(Impl->Stats.ScalarFunctionFastPathF64, Statistics.ScalarFunctionFastPathF64);
		Impl->Stats.ScalarFunctionFastPathMixed = SaturatingAdd(Impl->Stats.ScalarFunctionFastPathMixed, Statistics.ScalarFunctionFastPathMixed);
		Impl->Stats.ScalarNativeFastPathCalls = SaturatingAdd(Impl->Stats.ScalarNativeFastPathCalls, Statistics.ScalarNativeFastPathCalls);
		Impl->Stats.DirectNativeCalls = SaturatingAdd(Impl->Stats.DirectNativeCalls, Statistics.DirectNativeCalls);
		Impl->Stats.ControlledNativeExecutions = SaturatingAdd(Impl->Stats.ControlledNativeExecutions, Statistics.ControlledNativeExecutions);
		Impl->Stats.AotExecutions = SaturatingAdd(Impl->Stats.AotExecutions, Statistics.AotExecutions);
		Impl->Stats.InlinedCallSites = SaturatingAdd(Impl->Stats.InlinedCallSites, Statistics.InlinedCallSites);
		Impl->Stats.LinearFrameReuses = SaturatingAdd(Impl->Stats.LinearFrameReuses, Statistics.LinearFrameReuses);
		Impl->Stats.LinearScratchReuses = SaturatingAdd(Impl->Stats.LinearScratchReuses, Statistics.LinearScratchReuses);
		Impl->Stats.LinearScratchAllocations = SaturatingAdd(Impl->Stats.LinearScratchAllocations, Statistics.LinearScratchAllocations);
		Impl->Stats.LinearArgumentScratchLeases = SaturatingAdd(Impl->Stats.LinearArgumentScratchLeases, Statistics.LinearArgumentScratchLeases);
		Impl->Stats.LinearArgumentScratchReuses = SaturatingAdd(Impl->Stats.LinearArgumentScratchReuses, Statistics.LinearArgumentScratchReuses);
		Impl->Stats.NativeLeafExecutions = SaturatingAdd(Impl->Stats.NativeLeafExecutions, Statistics.NativeLeafExecutions);
		Impl->Stats.NativeCfgExecutions = SaturatingAdd(Impl->Stats.NativeCfgExecutions, Statistics.NativeCfgExecutions);
		Impl->Stats.BridgeExecutions = SaturatingAdd(Impl->Stats.BridgeExecutions, Statistics.BridgeExecutions);
		Impl->Stats.LinearExecutions = SaturatingAdd(Impl->Stats.LinearExecutions, Statistics.LinearExecutions);
		Impl->Stats.VmTrampolineExecutions = SaturatingAdd(Impl->Stats.VmTrampolineExecutions, Statistics.VmTrampolineExecutions);
	}

	std::string_view FJitBackendCache::GetLastError() const noexcept
	{
		if (!Impl->LastError.empty())
		{
			return Impl->LastError;
		}
		return Impl->FailureFallback ? Impl->FailureFallback : "";
	}

	std::string_view FJitBackendCache::GetFunctionReason(const std::uint16_t FunctionIndex) const noexcept
	{
		if (FunctionIndex >= Impl->Slots.size())
		{
			return "invalid function index";
		}
		const auto& Slot = Impl->Slots[FunctionIndex];
		if (Slot.RuntimeFallback != EJitFallbackReason::None) return JitFallbackReasonName(Slot.RuntimeFallback);
		if (!Slot.Reason.empty())
		{
			return Slot.Reason;
		}
		return Slot.StaticReason ? Slot.StaticReason : "";
	}
	EJitFallbackReason FJitBackendCache::GetFunctionFallbackReason(const std::uint16_t FunctionIndex) const noexcept
	{
		if (FunctionIndex >= Impl->Slots.size()) return EJitFallbackReason::EntryUnavailable;
		const auto& Slot = Impl->Slots[FunctionIndex];
		if (Slot.RuntimeFallback != EJitFallbackReason::None) return Slot.RuntimeFallback;
		if (Slot.FallbackReason != EJitFallbackReason::None) return Slot.FallbackReason;
		if (Slot.State == FImpl::ESlotState::Ready) return EJitFallbackReason::None;
		return Slot.State == FImpl::ESlotState::Unvisited ? EJitFallbackReason::Hotness : EJitFallbackReason::EntryUnavailable;
	}

	FJitBackendCache::FJitBackendCache(std::unique_ptr<FImpl> InImpl) noexcept
	    : Impl(std::move(InImpl))
	{
	}
}
