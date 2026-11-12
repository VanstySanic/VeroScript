// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJit.h"
#include "VeroScriptAsmJitHost.h"
#include "VeroScriptAsmJitExecutionContext.h"
#include "VeroScriptAsmJitCapabilities.h"
#include "VeroScriptJitNative.h"
#include "VeroScriptJitRawScratch.h"
#include "VeroScriptAsmJitControlled.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace VeroScript::AsmJit
{
	namespace
	{
		constexpr std::uint64_t RequiredServices = JitFrameServiceCapability | JitValueServiceCapability | JitCallServiceCapability | JitScalarServiceCapability;

		bool HasTightLocalStaticCallLoop(const FBytecodeModule& Module, const FBytecodeFunction& Function)
		{
			// These callees use Core instruction services instead of a raw scalar
			// entry. A small loop repeatedly crossing that boundary is cheaper in
			// the VM. Leave larger kernels and calls outside the loop independent.
			std::size_t Count = 0;
			std::size_t LastStateCall = Function.Code.size();
			bool HasLoop = false;
			bool HasStateCallInLoop = false;
			for (std::size_t Offset = 0; Offset < Function.Code.size();)
			{
				if (++Count > 32u) return false;
				const auto Op = static_cast<EOpcode>(Function.Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Offset) return false;
				if (Op == EOpcode::Call || Op == EOpcode::CallBoundMethod)
				{
					const auto Target = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1u);
					if (Target < Module.Functions.size() && Module.Functions[Target].bUsesLocalStaticStorage)
					{
						LastStateCall = Offset;
					}
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm)
				{
					const auto Operand = Op == EOpcode::JmpIfLocalGeImm ? 7u : 1u;
					const auto Target = static_cast<std::int64_t>(Offset + Size) + Bytecode::ReadI32LE(Function.Code.data() + Offset + Operand);
					if (Target >= 0 && static_cast<std::uint64_t>(Target) < Offset)
					{
						if (HasLoop) return false;
						HasLoop = true;
						HasStateCallInLoop = LastStateCall < Offset && LastStateCall >= static_cast<std::size_t>(Target);
					}
				}
				Offset += Size;
			}
			return HasStateCallInLoop;
		}

		bool PrefersVmForShortReadOrForwarder(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, FJitCodeCache& Cache)
		{
			if (Function.bReturnsVoid) return false;
			bool HasRead = false;
			bool HasForwardedCall = false;
			std::uint16_t Target = 0;
			std::size_t Count = 0;
			for (std::size_t Offset = 0; Offset < Function.Code.size();)
			{
				if (++Count > 8) return false;
				const auto Op = static_cast<EOpcode>(Function.Code[Offset]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0 || Size > Function.Code.size() - Offset) return false;
				if (HasForwardedCall && Op != EOpcode::Return) return false;
				switch (Op)
				{
				case EOpcode::LoadLocal:
				case EOpcode::LoadGlobal:
				case EOpcode::LoadThisField:
				case EOpcode::LoadField:
					{
						HasRead = true;
						break;
					}
				case EOpcode::Nop:
				case EOpcode::LoadThis:
				case EOpcode::ConstStr:
				case EOpcode::Pop:
					{
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (static_cast<EBuiltinId>(Function.Code[Offset + 1]) != EBuiltinId::Assert) return false;
						break;
					}
				case EOpcode::CallBoundMethod:
					{
						if (!Function.bIsMethod) return false;
						Target = Bytecode::ReadU16LE(Function.Code.data() + Offset + 1);
						HasForwardedCall = true;
						break;
					}
				case EOpcode::Return:
					{
						if (Offset + Size != Function.Code.size()) return false;
						if (!HasForwardedCall) return HasRead;
						if (Target >= Module.Functions.size() || Module.Functions[Target].Code.size() > 128) return false;
						// Analyze one bounded dependency only after proving the wrapper.
						// Preserve raw bound calls to native scalar callees; their VM bridge
						// can cost more than the original wrapper. Results do not depend on
						// which caller happened to be compiled first.
						const auto Status = Cache.EnsureCompiled(Module, Target);
						if (Status == EJitCompileStatus::Unsupported) return true;
						const auto* Callee = Cache.GetCandidate(Target);
						return Status == EJitCompileStatus::Ok && Callee && Callee->bPreferVmForExecution;
					}
				default:
					{
						return false;
					}
				}
				Offset += Size;
			}
			return false;
		}

		FJitBackendDescription DescribeBackend(const FJitHostServices& Host)
		{
			FJitBackendDescription Result;
			Result.Id = "asmjit";
			Result.DisplayName = "VeroScript AsmJit";
			Result.RequiredServices = RequiredServices;
			Result.MaxInvocationDepth = 8;
			#if VEROSCRIPT_HAS_ASMJIT && VEROSCRIPT_ASMJIT_X64
			// Raw scalar frames have bounded locals/operands and no host activation.
			// Keep ordinary C++/host bridges at their original, smaller depth.
			Result.MaxDirectScalarDepth = 64;
			#endif
			Result.bNativeCode = JitBackend::GetHostCapabilities().bNativeLeaves;
			Result.Capabilities |= JitPortableExecutionCapability;
			if (Result.bNativeCode)
			{
				Result.Capabilities |= JitNativeCodeCapability | JitDirectScalarCallsCapability;
				#if VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64
				Result.Capabilities |= JitControlledExecutionCapability;
				#endif
				#if VEROSCRIPT_ASMJIT_X64
				Result.Capabilities |= JitNativeTypedLoopsCapability;
				#endif
			}
			Result.Target = Result.bNativeCode ? std::string(Host.HostTarget) : "portable-cpp";
			if (!Result.bNativeCode)
			{
				Result.NativeUnavailableReason = "native emission is disabled or unsupported for this target; portable execution is available";
			}
			return Result;
		}

		struct FCodeStorage
		{
			const FJitHostServices* Host = nullptr;
			std::unique_ptr<FJitCodeCache> Cache;
			std::unique_ptr<FControlledCodeCache> Controlled;
			std::vector<std::vector<FValue>> Scratch;
			JitScratch::FStorage RawScratch;
			FJitStats Statistics;
			std::size_t LiveExecutables = 0;
		};

		class FExecutable final : public IJitExecutable
		{
		public:
			//~ Begin IJitExecutable Interface
			EJitInvocationResult Invoke(FJitCallContext& Context) const override
			{
				const JitHost::FScope HostScope(*Storage->Host);
				const auto* Entry = Storage->Cache ? Storage->Cache->TryGet(FunctionIndex) : nullptr;
				if (!Entry) return EJitInvocationResult::NotEntered;
				// A low language depth limit must still fail at the original call,
				// including calls hidden by scalar inlining. Let the VM follow the
				// actual branch/iteration when the conservative inline bound cannot fit.
				if (Entry->InlinedCallSites != 0 && Context.Services->GetCallServices()->GetRemainingCallDepth() <= Entry->InlinedCallSites)
				{
					Context.FallbackReason = EJitFallbackReason::InvocationDepth;
					return EJitInvocationResult::NotEntered;
				}
				if (Entry->bPreferVmForExecution)
				{
					Context.FallbackReason = EJitFallbackReason::CostModel;
					if (Entry->VmPreferenceCategory == EJitFallbackReason::CostModel)
					{
						++Storage->Statistics.CostModelVmFallbacks;
					}
					if (Entry->VmPreferenceCategory != EJitFallbackReason::None)
					{
						Context.FallbackReason = Entry->VmPreferenceCategory;
					}
					return EJitInvocationResult::NotEntered;
				}
				if (Entry->TextEntryVmTripThreshold != 0u)
				{
					std::int32_t Limit = Entry->TextEntryLimitImmediate;
					bool Known = Entry->TextEntryLimitArgument == 0xFFFFu;
					if (!Known && Context.Arguments && Entry->TextEntryLimitArgument < Context.ArgumentCount)
					{
						if (const auto* Value = std::get_if<std::int32_t>(&Context.Arguments[Entry->TextEntryLimitArgument]))
						{
							Limit = *Value; Known = true;
						}
					}
					// The compiler folds the work budget into an exclusive trip limit.
					// Invocation needs neither multiplication nor dynamic division.
					if (Known && Limit < static_cast<std::int32_t>(Entry->TextEntryVmTripThreshold))
					{
						Context.FallbackReason = EJitFallbackReason::CostModel;
						++Storage->Statistics.CostModelVmFallbacks;
						return EJitInvocationResult::NotEntered;
					}
				}
				if (Entry->bMutatesScriptFields && Context.Services->GetCallServices()->HasMutationObserver())
				{
					Context.FallbackReason = EJitFallbackReason::Observation;
					return EJitInvocationResult::NotEntered;
				}
				Context.MarkEntered();
				bool Ok = false;
				{
					const JitScratch::FStorageScope ScratchScope(Storage->RawScratch);
					FExecutionContext Invocation(Context, *Entry, *Storage->Cache, Storage->Statistics, Storage->Scratch);
					switch (Entry->Kind)
					{
					case EJitEntryKind::NativeLeaf:
						{
							break;
						}
					case EJitEntryKind::NativeCfg:
						{
							break;
						}
					case EJitEntryKind::LinearTrampoline:
						{
							Context.ExecutionKind = EJitExecutionKind::Linear;
							++Storage->Statistics.LinearExecutions;
							break;
						}
					case EJitEntryKind::BridgeTrampoline:
						{
							Context.ExecutionKind = EJitExecutionKind::Bridge;
							++Storage->Statistics.BridgeExecutions;
							break;
						}
					case EJitEntryKind::VmTrampoline:
						{
							break;
						}
					default:
						{
							break;
						}
					}
					Ok = Entry->Entry(&Invocation);
				}
				if (Context.ExecutionKind == EJitExecutionKind::Unknown && Context.FallbackReason == EJitFallbackReason::None)
				{
					Context.FallbackReason = EJitFallbackReason::RuntimeGuard;
				}
				if (Context.Services->HasPendingControlTransfer()) return EJitInvocationResult::ControlTransfer;
				if (!Ok || Context.Services->HasError()) return EJitInvocationResult::Failed;
				if (Context.Program->Module->Functions[FunctionIndex].bIsMethod && Entry->Kind == EJitEntryKind::NativeCfg)
				{
					switch (Entry->ScalarAbi)
					{
					case EJitScalarAbi::I32:
						{
							++Storage->Statistics.ScalarMethodFastPathI32;
							break;
						}
					case EJitScalarAbi::U32:
						{
							++Storage->Statistics.ScalarMethodFastPathU32;
							break;
						}
					case EJitScalarAbi::F64:
						{
							++Storage->Statistics.ScalarMethodFastPathF64;
							break;
						}
					case EJitScalarAbi::Mixed:
						{
							++Storage->Statistics.ScalarMethodFastPathMixed;
							break;
						}
					default:
						{
							break;
						}
					}
				}
				return EJitInvocationResult::Returned;
			}

			~FExecutable() override
			{
				--Storage->LiveExecutables;
			}
			//~ End IJitExecutable Interface

			FExecutable(std::shared_ptr<FCodeStorage> InStorage, const std::uint16_t Index)
			    : Storage(std::move(InStorage)), FunctionIndex(Index)
			{
				++Storage->LiveExecutables;
			}

		private:
			std::shared_ptr<FCodeStorage> Storage;
			std::uint16_t FunctionIndex;
		};

		class FInstance final : public IJitBackendInstance
		{
		public:
			//~ Begin IJitBackendInstance Interface
			const FJitBackendDescription& GetDescription() const noexcept override
			{
				return Description;
			}

			EJitCompilationResult Compile(const FJitCompileRequest& Request, FJitCompileResponse& Response) override
			{
				if (!Storage)
				{
					Response.Reason = "AsmJit instance has been retired";
					return EJitCompilationResult::Failed;
				}
				const JitHost::FScope HostScope(*Storage->Host);
				if (Request.Program.Identity != Program.Identity || Request.Program.Module != Program.Module)
				{
					Response.Reason = "AsmJit compilation belongs to a different program generation";
					return EJitCompilationResult::Failed;
				}
				const auto& SourceFunction = Program.Module->Functions[Request.FunctionIndex];
				if (Request.RequiredGovernance != 0 || SourceFunction.bUsesLocalStaticStorage || (bHasLocalStaticFunctions && HasTightLocalStaticCallLoop(*Program.Module, SourceFunction)))
				{
					if (!Storage->Controlled)
					{
						Storage->Controlled = std::make_unique<FControlledCodeCache>();
					}
					return Storage->Controlled->Compile(Request, Response);
				}
				if (!Storage->Cache)
				{
					if (FJitCodeCache::MinimumPayloadBytes(Program.Module->Functions.size()) > Request.AvailableResources.RetainedBytes)
					{
						Response.Reason = "AsmJit generation metadata exceeds the remaining retention budget";
						bResourceLimitReached = true;
						return EJitCompilationResult::Deferred;
					}
					Storage->Cache = std::make_unique<FJitCodeCache>();
					Storage->Cache->SetPolicy({1, 0, 0, 0});
					Storage->Cache->PrepareModule(*Program.Module, EExecutionBackend::Mixed);
				}
				const auto Result = Storage->Cache->EnsureCompiled(*Program.Module, Request.FunctionIndex);
				if (Result == EJitCompileStatus::Unsupported)
				{
					// Core remembers unsupported decisions for this generation.
					Storage->Cache->DiscardCandidate(Request.FunctionIndex);
					Response.Reason = "function has no verified AsmJit execution plan";
					Response.FallbackReason = BytecodeFunctionHasRefParams(SourceFunction) ? EJitFallbackReason::ReferenceParameter
						: !SourceFunction.Captures.empty() ? EJitFallbackReason::Closure : EJitFallbackReason::UnsupportedOpcode;
					return EJitCompilationResult::Unsupported;
				}
				if (Result != EJitCompileStatus::Ok)
				{
					Response.Reason = "AsmJit failed to construct an execution plan";
					return EJitCompilationResult::Failed;
				}
				auto Function = std::make_shared<FJitCompiledFunction>();
				Function->Identity = Program.Identity;
				Function->ProgramOwner = Program.Owner;
				Function->FunctionIndex = Request.FunctionIndex;
				Function->RequiredServices = RequiredServices;
				const auto *Entry = Storage->Cache->GetCandidate(Request.FunctionIndex);
				Function->bSuppressNestedBackendOnFallback = Entry->bSuppressNestedJitOnVmFallback;
				Function->bPreferVmForNestedExecution = Entry->bPreferVmForNestedExecution;
				// Short reads and single-call method wrappers cost less to interpret
				// than to marshal across this boundary. Callees keep their own plans.
				Function->bPreferVmForExecution = Entry->bPreferVmForExecution || PrefersVmForShortReadOrForwarder(*Program.Module, Program.Module->Functions[Request.FunctionIndex], *Storage->Cache);
				switch (Entry->Kind)
				{
				case EJitEntryKind::NativeLeaf:
					{
						Function->ExecutionKind = EJitExecutionKind::NativeLeaf;
						break;
					}
				case EJitEntryKind::NativeCfg:
					{
						Function->ExecutionKind = EJitExecutionKind::NativeCfg;
						break;
					}
				case EJitEntryKind::LinearTrampoline:
					{
						Function->ExecutionKind = EJitExecutionKind::Linear;
						break;
					}
				case EJitEntryKind::BridgeTrampoline:
					{
						Function->ExecutionKind = EJitExecutionKind::Bridge;
						break;
					}
				case EJitEntryKind::VmTrampoline:
					{
						Function->ExecutionKind = EJitExecutionKind::VmTrampoline;
						break;
					}
				default:
					{
						break;
					}
				}
				if (Function->bPreferVmForExecution || Function->bPreferVmForNestedExecution)
				{
					Function->FallbackReason = Entry->VmPreferenceCategory == EJitFallbackReason::None ? EJitFallbackReason::CostModel : Entry->VmPreferenceCategory;
					Response.Reason = Entry->VmPreferenceReason ? Entry->VmPreferenceReason : Function->bPreferVmForNestedExecution
						? "cost model: bounded receiver expression in an interpreter caller" : "cost model: short read or forwarding call";
				}
				Function->Executable = std::make_shared<FExecutable>(Storage, Request.FunctionIndex);
				Response.Function = std::move(Function);
				return EJitCompilationResult::Ready;
			}

			bool Publish(const FJitCompiledFunction& Function, std::string& Error) override
			{
				if (Function.GovernanceCapabilities != 0) return Storage && Function.Identity == Program.Identity;
				if (!Storage || !Storage->Cache || Function.Identity != Program.Identity || !Storage->Cache->PublishCandidate(Function.FunctionIndex))
				{
					Error = "AsmJit rejected publication of an invalid candidate";
					return false;
				}
				return true;
			}

			void Discard(const std::shared_ptr<const FJitCompiledFunction>& Function) noexcept override
			{
				if (Storage && Storage->Cache && Function && Function->GovernanceCapabilities == 0)
				{
					Storage->Cache->DiscardCandidate(Function->FunctionIndex);
				}
				// Executable allocator pages remain owned and measured by this generation.
			}

			void ResetExecutionStatistics() noexcept override
			{
				if (Storage)
				{
					Storage->Statistics = {};
					if (Storage->Controlled)
					{
						Storage->Controlled->ResetStatistics();
					}
				}
			}

			FJitStats GetStatistics() const noexcept override
			{
				if (!Storage) return {};
				auto Result = Storage->Statistics;
				if (Storage->Controlled)
				{
					const auto Controlled = Storage->Controlled->GetStatistics();
					Result.ControlledNativeExecutions = Controlled.ControlledNativeExecutions;
					Result.NativeCfgEntries += Controlled.NativeCfgEntries;
				}
				if (Storage->Cache)
				{
					Storage->Cache->AccumulatePublishedStatistics(Result);
				}
				return Result;
			}

			FJitResourceUsage GetResourceUsage() const noexcept override
			{
				if (!Storage) return {};
				FJitResourceUsage Result;
				Result.bResourceLimitReached = bResourceLimitReached;
				Result.RetainedBytes = sizeof(*this) + sizeof(FCodeStorage) + Storage->LiveExecutables * sizeof(FExecutable);
				if (Storage->Cache)
				{
					Result.ExecutableBytes = Storage->Cache->NativeArena().ReservedBytes();
					Result.RetainedBytes += Storage->Cache->RetainedPayloadBytes() - Result.ExecutableBytes;
				}
				if (Storage->Controlled)
				{
					const auto Controlled = Storage->Controlled->GetResourceUsage();
					Result.ExecutableBytes += Controlled.ExecutableBytes;
					Result.RetainedBytes += sizeof(FControlledCodeCache) + Controlled.RetainedBytes;
				}
				Result.ScratchBytes = Storage->Scratch.capacity() * sizeof(std::vector<FValue>);
				for (const auto& Scratch : Storage->Scratch)
				{
					Result.ScratchBytes += Scratch.capacity() * sizeof(FValue);
				}
				Result.ScratchBytes += Storage->RawScratch.RetainedBytes();
				Result.RetainedBytes += Result.ScratchBytes;
				// Text producer pools are already included in the code-cache payload.
				if (Storage->Cache)
				{
					Result.ScratchBytes += Storage->Cache->RetainedTextScratchBytes();
				}
				return Result;
			}

			void Retire() noexcept override
			{
				Storage.reset();
			}
			//~ End IJitBackendInstance Interface

			FInstance(FJitProgramView InProgram, FJitBackendDescription InDescription, const FJitHostServices& Host)
			    : Program(std::move(InProgram)), Description(std::move(InDescription)), Storage(std::make_shared<FCodeStorage>())
			{
				Storage->Host = &Host;
				bHasLocalStaticFunctions = Program.Module && std::any_of(Program.Module->Functions.begin(), Program.Module->Functions.end(), [](const FBytecodeFunction& Function)
				{
					return Function.bUsesLocalStaticStorage;
				});
			}

		private:
			FJitProgramView Program;
			FJitBackendDescription Description;
			std::shared_ptr<FCodeStorage> Storage;
			bool bResourceLimitReached = false;
			bool bHasLocalStaticFunctions = false;
		};

		class FFactory final : public IJitBackendFactory
		{
		public:
			//~ Begin IJitBackendFactory Interface
			const FJitBackendDescription& GetDescription() const noexcept override
			{
				return Description;
			}

			std::unique_ptr<IJitBackendInstance> CreateInstance(const FJitProgramView& Program,
			    const FJitResourceLimits&, std::string&) const override
			{
				return std::make_unique<FInstance>(Program, Description, *Host);
			}
			//~ End IJitBackendFactory Interface

			explicit FFactory(const FJitHostServices& InHost) : Host(&InHost), Description(DescribeBackend(InHost))
			{
			}

		private:
			const FJitHostServices* Host = nullptr;
			const FJitBackendDescription Description;
		};
	}

	IJitBackendFactory* CreateFactory(const FJitHostServices& Host)
	{
		return new FFactory(Host);
	}
}

#if !VEROSCRIPT_ASMJIT_DYNAMIC
namespace VeroScript
{
	std::shared_ptr<const IJitBackendFactory> CreateAsmJitBackendFactory()
	{
		return std::shared_ptr<const IJitBackendFactory>(AsmJit::CreateFactory(GetJitHostServices()));
	}
}
#endif
