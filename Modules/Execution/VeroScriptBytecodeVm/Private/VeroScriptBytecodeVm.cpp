// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVm.h"
#include "VeroScriptBytecodeVmExecution.h"
#include "VeroScriptVmBuild.h"
#include "VeroScriptVmDispatchSelection.h"
#include <stdexcept>

namespace VeroScript::BytecodeVm
{
	namespace
	{
		class FInstance final : public IVmBackendInstance
		{
		public:
			explicit FInstance(const FVmHostServices& InHost) : Host(InHost)
			{
			}
			bool Bind(const void* Identity, const FBytecodeModule* InProgram,
			    std::uint64_t InGeneration, std::string& Error) override
			{
				if (ActiveDepth || !Identity || !InProgram || !InGeneration)
				{
					Error = "invalid or active VM instance binding";
					return false;
				}
				bCacheBindingChecked = false;
				Context = Identity;
				Program = InProgram;
				Generation = InGeneration;
				return true;
			}
			bool SupportsIdleRebinding() const noexcept override
			{
				return true;
			}
			void Unbind() noexcept override
			{
				if (ActiveDepth) return;
				bCacheBindingChecked = false;
				if (CachedProgramOwner.expired())
				{
					DispatchSelection.Reset();
					CachedProgramOwner.reset();
					CachedProgram = nullptr;
					CachedGeneration = 0;
				}
				Context = nullptr;
				Program = nullptr;
				Generation = 0;
			}
			std::size_t GetRetainedBytes() const noexcept override
			{
				return sizeof(*this) + DispatchSelection.GetRetainedBytes();
			}
			EVmExecutionResult Execute(FVmInvocation& Invocation) override
			{
				if (Context != Invocation.ContextIdentity || Program != Invocation.Program || Generation != Invocation.ProgramGeneration || !Invocation.CallToken || Invocation.State.GetStopFrameDepth() != Invocation.FrameBoundary)
				{
					Invocation.Services.ReportError("VM invocation does not match its binding");
					return EVmExecutionResult::Failed;
				}
				if (!bCacheBindingChecked)
				{
					const auto& Owner = Invocation.State.GetImmutableProgramOwner();
					const bool Owned = Owner && Owner.get() == Program;
					const bool SameOwner = Owned && CachedProgram == Program && CachedGeneration == Generation &&
					    !Owner.owner_before(CachedProgramOwner) && !CachedProgramOwner.owner_before(Owner);
					if (!SameOwner)
					{
						DispatchSelection.Reset();
					}
					if (Owned)
					{
						CachedProgramOwner = Owner;
					}
					else
					{
						CachedProgramOwner.reset();
					}
					CachedProgram = Owned ? Program : nullptr;
					CachedGeneration = Owned ? Generation : 0;
					bCacheBindingChecked = true;
				}
				struct FDepthGuard
				{
					explicit FDepthGuard(std::size_t& InDepth) : Depth(InDepth)
					{
						++Depth;
					}
					~FDepthGuard()
					{
						--Depth;
					}
					std::size_t& Depth;
				} Guard(ActiveDepth);
				Invocation.bEntered = true;
				FBytecodeVmExecution Execution(Invocation.Services, Invocation.State, Host);
				bool Ok;
				if (Invocation.bInstrumented)
				{
					Ok = Invocation.bGoverned ? Execution.RunLoop<true, true>() : Execution.RunLoop<true, false>();
				}
				else if (Invocation.State.HasDemandGlobalInitialization())
				{
					Ok = Invocation.bGoverned ? Execution.RunLoop<false, true, false, true>()
					                         : Execution.RunLoop<false, false, false, true>();
				}
				else if (Invocation.bGoverned)
				{
					Ok = Execution.CanBatchArithmetic() ? Execution.RunLoop<false, true, true>()
					                                    : Execution.RunLoop<false, true>();
				}
				else
				{
					const auto Features = DispatchSelection.GetFeatures(*Program, Invocation.State);
					Ok = Features ? Execution.RunEnhancedLoop((Features & VmDispatchSelection::IndexedFeature) != 0u)
					              : Execution.RunLoop<false, false>();
				}
				if (Invocation.State.GetExitRequestedFlag()) return EVmExecutionResult::ControlTransfer;
				if (Ok) return EVmExecutionResult::Returned;
				return Invocation.State.GetPendingExceptionFlag() || Invocation.State.GetExitRequestedFlag()
				    ? EVmExecutionResult::ControlTransfer
				    : EVmExecutionResult::Failed;
			}

		private:
			VmDispatchSelection::FCache DispatchSelection;
			// Classification retains metadata addresses only. A weak immutable-module
			// lease prevents address reuse from validating an old declaration, without
			// keeping retired modules alive in the shared idle-instance pool.
			std::weak_ptr<const FBytecodeModule> CachedProgramOwner;
			const FBytecodeModule* CachedProgram = nullptr;
			std::uint64_t CachedGeneration = 0;
			bool bCacheBindingChecked = false;

			const FVmHostServices& Host;
			const void* Context = nullptr;
			const FBytecodeModule* Program = nullptr;
			std::uint64_t Generation = 0;
			std::size_t ActiveDepth = 0;
		};

		class FFactory final : public IVmBackendFactory
		{
		public:
			explicit FFactory(const FVmHostServices& InHost) : Host(InHost)
			{
				if (!Host.OwnerIdentity)
				{
					throw std::invalid_argument("VM requires an owning Core service table");
				}
				Description.Id = "vs-bytecode";
				Description.DisplayName = "VeroScript Bytecode VM";
				Description.Version = VEROSCRIPT_VM_VERSION;
				Description.Target = "portable-cpp";
				Description.ImplementationId = VEROSCRIPT_VM_IMPLEMENTATION_ID;
				Description.Capabilities = VmAllCapabilities;
			}
			const FVmBackendDescription& GetDescription() const noexcept override
			{
				return Description;
			}
			const void* GetHostIdentity() const noexcept override
			{
				return Host.OwnerIdentity;
			}
			IVmBackendInstance* CreateInstance(std::string&) const override
			{
				return new FInstance(Host);
			}
			void DestroyInstance(IVmBackendInstance* Instance) const noexcept override
			{
				delete Instance;
			}

		private:
			const FVmHostServices Host;
			FVmBackendDescription Description;
		};
	}

	std::shared_ptr<const IVmBackendFactory> CreateFactory(const FVmHostServices& Host)
	{
		return std::make_shared<FFactory>(Host);
	}
	IVmBackendFactory* CreateRawFactory(const FVmHostServices& Host)
	{
		return new FFactory(Host);
	}
}
