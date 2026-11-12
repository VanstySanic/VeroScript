// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "../Memory/VeroScriptMemoryInternal.h"
#include "VeroScriptNativeBindingCache.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace VeroScript
{
	FMemoryContext& VeroScriptVm::GetMemoryContext()
	{
		if (!MemoryContext)
		{
			MemoryContext = std::make_shared<FMemoryContext>();
			bOwnMemoryContext = true;
			if (bMemoryExecutionActive)
			{
				MemoryContext->BeginExecution();
			}
		}
		return *MemoryContext;
	}

	FMemoryWork VeroScriptVm::GetMemoryWork()
	{
		return {[](void* Data, std::uint64_t Units)
		{
			auto& Vm = *static_cast<VeroScriptVm*>(Data);
			return Vm.ChargeExecutionWork(Units);
		},
		    this};
	}

	FMemoryResult VeroScriptVm::MemoryArgumentAddress(std::size_t Index, std::uint64_t& Out)
	{
		if (!Capabilities.bAllowMemoryAddress)
		{
			return {EMemoryStatus::PermissionDenied, "diag.memory_permission"};
		}
		if (!Memory::NativeAddressesAvailable())
		{
			return {EMemoryStatus::Unsupported, "diag.memory_addresses_unsupported"};
		}
		if (Index >= NativeOriginalReferenceCount || !NativeOriginalReferences)
		{
			return {EMemoryStatus::Unsupported, "diag.memory_argument_address"};
		}
		FValue Binding = NativeOriginalReferences[Index];
		if (!std::holds_alternative<FLocalRef>(Binding) && !std::holds_alternative<FGlobalRef>(Binding) &&
		    !std::holds_alternative<FThisFieldRef>(Binding) && !std::holds_alternative<FIndexRef>(Binding) &&
		    !std::holds_alternative<FMapEntryRef>(Binding) && !IsClosureReferenceCell(Binding))
		{
			return {EMemoryStatus::Unsupported, "diag.memory_argument_address"};
		}
		auto* Storage = ResolveRefStorage(Binding);
		if (!Storage)
		{
			return {EMemoryStatus::Expired, "diag.memory_argument_address"};
		}
		Out = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Storage));
		return {};
	}

	VeroScriptVm::~VeroScriptVm()
	{
		if (Executor && (Executor.use_count() == 1 || bHasError || SpawnedTasks.HasActiveWorkers()))
		{
			Executor->AbandonMutexes();
		}
		if (bOwnMemoryContext && MemoryContext)
		{
			MemoryContext->Close();
		}
		ReleaseVmBackend();
	}

	void VeroScriptVm::SetMemoryContext(std::shared_ptr<FMemoryContext> Context, bool OwnContext)
	{
		if (bMemoryExecutionActive)
		{
			throw std::logic_error("cannot replace an active memory context");
		}
		if (MemoryContext != Context && bOwnMemoryContext && MemoryContext)
		{
			MemoryContext->Close();
		}
		MemoryContext = std::move(Context);
		bOwnMemoryContext = OwnContext;
	}

	FMemoryInspection VeroScriptVm::DebugInspectMemory(const FValue& Value, FValue* Storage)
	{
		FMemoryInspection Out;
		std::uint64_t N = 0;
		const bool Addresses = Capabilities.bAllowMemoryAddress && Memory::NativeAddressesAvailable();
		if (Storage && Addresses)
		{
			if (auto* P = ResolveRefStorage(*Storage))
			{
				Out.StorageAddress = reinterpret_cast<std::uintptr_t>(P);
			}
		}
		if (Memory::ObjectId(Value, N, MemoryBindings))
		{
			Out.ObjectId = N;
		}
		if (Addresses && Memory::ObjectAddress(Value, N, MemoryBindings))
		{
			Out.ObjectAddress = N;
		}
		if (Memory::IsRegion(Value))
		{
			std::int64_t Size = 0;
			auto& C = GetMemoryContext();
			if (Memory::Size(C, Value, Size))
			{
				Out.ByteLength = Size;
			}
			if (Addresses && Memory::DataAddress(C, Value, N))
			{
				Out.DataAddress = N;
			}
		}
		return Out;
	}

	void VeroScriptVm::WatchMemoryMutation(const FValue& Value, FScriptObject& Object, std::uint16_t Field)
	{
		if (!MutationCallback)
		{
			return;
		}
		PollMemoryMutations();
		std::uint64_t Version = 0;
		if (!Memory::MutationVersion(GetMemoryContext(), Value, Version))
		{
			return;
		}
		const auto& Region = std::get<FHostObjectHandle>(Value);
		for (auto& W : MemoryMutationWatches)
		{
			if (W.Object.lock().get() == &Object && W.Field == Field)
			{
				W.Region = Region;
				W.Version = Version;
				W.Expired = false;
				return;
			}
		}
		MemoryMutationWatches.push_back({Region, Object.weak_from_this(), Field, Version, false});
	}

	void VeroScriptVm::PollMemoryMutations()
	{
		if (bPollingMemoryMutations || MemoryMutationWatches.empty())
		{
			return;
		}
		bPollingMemoryMutations = true;
		struct FReset
		{
			~FReset()
			{
				Flag = false;
			}

			bool& Flag;
		} Reset{bPollingMemoryMutations};
		std::vector<std::pair<FObjectHandle, std::uint16_t>> Changed;
		for (auto I = MemoryMutationWatches.begin(); I != MemoryMutationWatches.end();)
		{
			auto O = I->Object.lock();
			auto H = I->Region.lock();
			const auto* Current =
			    O && I->Field < O->Fields.size()
			    ? std::get_if<FHostObjectHandle>(UnwrapClosureReferenceCell(&O->Fields[I->Field]))
			    : nullptr;
			if (!O || !H || !Current || *Current != H)
			{
				I = MemoryMutationWatches.erase(I);
				continue;
			}
			std::uint64_t Version = 0;
			const auto R = Memory::MutationVersion(GetMemoryContext(), FValue{H}, Version);
			if ((R && (Version != I->Version || Version == std::numeric_limits<std::uint64_t>::max())) ||
			    (!R && R.Status == EMemoryStatus::Expired && !I->Expired))
			{
				I->Version = Version;
				I->Expired = !R;
				Changed.emplace_back(O, I->Field);
			}
			++I;
		}
		if (!Changed.empty())
		{
			InvalidateJitCache();
		}
		// All region locks have been released before entering observer code.
		for (const auto& C : Changed)
		{
			NotifyFieldMutation(*C.first, C.second, EMutationKind::ContainerSet);
		}
	}

	bool VeroScriptVm::CallMemoryBuiltin(EBuiltinId Builtin, FValue* A, std::uint8_t Count)
	{
		PollMemoryMutations();
		FMemoryResult R;
		FValue Out = std::int32_t{0};
		bool Void = false;
		if (Builtin == EBuiltinId::MemoryAvailable)
		{
			Push(Capabilities.bAllowMemoryAddress && Memory::NativeAddressesAvailable());
			return true;
		}
		const bool Address = Builtin == EBuiltinId::MemoryAddress ||
		    Builtin == EBuiltinId::MemoryObjectAddress ||
		    Builtin == EBuiltinId::MemoryDataAddress;
		if (Address && !Capabilities.bAllowMemoryAddress)
		{
			ReportErrorId("diag.memory_permission");
			return false;
		}
		if (Address && !Memory::NativeAddressesAvailable())
		{
			ReportErrorId("diag.memory_addresses_unsupported");
			return false;
		}
		std::uint64_t Number = 0;
		if (Builtin == EBuiltinId::MemoryAddress)
		{
			if (!std::holds_alternative<FLocalRef>(A[0]) && !std::holds_alternative<FGlobalRef>(A[0]) &&
			    !std::holds_alternative<FThisFieldRef>(A[0]) && !std::holds_alternative<FIndexRef>(A[0]) &&
			    !std::holds_alternative<FMapEntryRef>(A[0]) && !IsClosureReferenceCell(A[0]))
			{
				ReportErrorId("diag.memory_address_target");
				return false;
			}
			auto* Target = ResolveRefStorage(A[0]);
			if (!Target)
			{
				ReportErrorId("diag.memory_address_target");
				return false;
			}
			Push(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Target)));
			return true;
		}
		if (Builtin == EBuiltinId::MemoryObjectId || Builtin == EBuiltinId::MemoryObjectAddress)
		{
			const auto* Kind = std::get_if<bool>(&A[1]);
			R = Kind ? Memory::Detail::ObjectIdentity(
			               A[0], Builtin == EBuiltinId::MemoryObjectAddress, *Kind, Number)
			         : FMemoryResult{EMemoryStatus::TypeMismatch, "diag.memory_object_type"};
			Out = Number;
		}
		else
		{
			auto& C = GetMemoryContext();
			const auto Work = GetMemoryWork();
			const auto Integer = [&](unsigned I, std::int64_t& N)
			{
				FValue V = A[I];
				std::string Error;
				if (!Runtime::CoerceValueToPrimitiveTypeName("int64", V, Error))
				{
					return false;
				}
				N = std::get<std::int64_t>(V);
				return true;
			};
			std::int64_t X = 0, Y = 0, Z = 0;
			bool Same = false;
			if ((Builtin == EBuiltinId::MemoryCopy || Builtin == EBuiltinId::MemoryEqual) &&
			    (!Integer(1, X) || !Integer(3, Y) || !Integer(4, Z)))
			{
				R = {EMemoryStatus::InvalidArgument, "diag.memory_arguments"};
			}
			else if (Builtin == EBuiltinId::MemoryFill && (!Integer(1, X) || !Integer(2, Y)))
			{
				R = {EMemoryStatus::InvalidArgument, "diag.memory_arguments"};
			}
			else if ((Builtin == EBuiltinId::MemoryRead || Builtin == EBuiltinId::MemoryWrite) &&
			    !Integer(1, X))
			{
				R = {EMemoryStatus::InvalidArgument, "diag.memory_arguments"};
			}
			else
			{
				switch (Builtin)
				{
				case EBuiltinId::MemoryDataAddress:
					{
						R = Memory::DataAddress(C, A[0], Number);
						Out = Number;
						break;
					}
				case EBuiltinId::MemoryCopy:
					{
						R = Memory::Copy(C, A[0], X, A[2], Y, Z, Work);
						Void = true;
						break;
					}
				case EBuiltinId::MemoryEqual:
					{
						R = Memory::Equal(C, A[0], X, A[2], Y, Z, Same, Work);
						Out = Same;
						break;
					}
				case EBuiltinId::MemoryFill:
					{
						if (const auto* B = std::get_if<std::uint8_t>(&A[3]))
						{
							R = Memory::Fill(C, A[0], X, Y, *B, Work);
						}
						else
						{
							R = {EMemoryStatus::TypeMismatch, "diag.memory_type"};
						}
						Void = true;
						break;
					}
				case EBuiltinId::MemoryClone:
					{
						R = Memory::Clone(C, A[0], Out, Work);
						break;
					}
				case EBuiltinId::MemoryFromArray:
					{
						if (const auto* B = std::get_if<FDynamicArrayHandle>(&A[0]))
						{
							R = Memory::FromArray(C, *B, Out, Work);
						}
						else
						{
							R = {EMemoryStatus::TypeMismatch, "diag.memory_byte_array"};
						}
						break;
					}
				case EBuiltinId::MemoryToArray:
					{
						FDynamicArrayHandle B;
						R = Memory::ToArray(C, A[0], B, Work);
						Out = std::move(B);
						break;
					}
				case EBuiltinId::MemoryRead:
				case EBuiltinId::MemoryWrite:
					{
						const auto* Type = std::get_if<std::string>(&A[Count - 1]);
						const auto* Order = std::get_if<FEnumValue>(&A[Count - 2]);
						if (!Type || !Order || Order->TypeName != "std.memory.byte_order" ||
						    (Order->Value != 0 && Order->Value != 1))
						{
							R = {EMemoryStatus::InvalidArgument, "diag.memory_byte_order"};
						}
						else if (Builtin == EBuiltinId::MemoryRead)
						{
							R = Memory::Read(
							    C, A[0], X, *Type, static_cast<EMemoryByteOrder>(Order->Value), Out, Work);
						}
						else
						{
							R = Memory::Write(
							    C, A[0], X, *Type, static_cast<EMemoryByteOrder>(Order->Value), A[2], Work);
							Void = true;
						}
						break;
					}
				default:
					{
						R = {EMemoryStatus::Unsupported, "diag.unknown_builtin"};
						break;
					}
				}
			}
		}
		PollMemoryMutations();
		if (!R)
		{
			if (!bHasError)
			{
				ReportErrorId(R.MessageId ? R.MessageId : "diag.memory_type");
			}
			return false;
		}
		if (!Void)
		{
			Push(std::move(Out));
		}
		return true;
	}

	bool VeroScriptVm::SetActiveNatives(const std::vector<FNativeFunction>* Natives)
	{
		if (!Natives && bUseEnvironmentBindings && ModuleEnvironment &&
		    ModuleEnvironment->GetDescription().Bindings)
		{
			Natives = &ModuleEnvironment->GetDescription().Bindings->GetNativeFunctions();
		}

		// A raw host table may be modified in place. Compare complete descriptors,
		// callbacks, userdata and both ownership identities before reusing copies;
		// a vector address or a revision number alone is never a proof of equality.
		const bool bSourceUnchanged = Natives && !Natives->empty()
		    ? Natives->size() == NativeBindingSourceSnapshot.size() &&
		        std::equal(Natives->begin(), Natives->end(), NativeBindingSourceSnapshot.begin(),
		            [](const FNativeFunction& Left, const FNativeFunction& Right)
		        {
			        return StandardLibrary::NativeFunctionsEquivalent(Left, Right) &&
			            Left.CallbackOwner == Right.CallbackOwner &&
			            !Left.CallbackOwner.owner_before(Right.CallbackOwner) &&
			            !Right.CallbackOwner.owner_before(Left.CallbackOwner) &&
			            !Left.ProviderIdentity.owner_before(Right.ProviderIdentity) &&
			            !Right.ProviderIdentity.owner_before(Left.ProviderIdentity);
		        })
		    : NativeBindingSourceSnapshot.empty() && !bJitHadNativeBindings;
		const bool bKnownSourceChange = !bSourceUnchanged && !NativeBindingSourceSnapshot.empty();
		if (!Natives || Natives->empty())
		{
			ActiveNatives = &StandardLibrary::GetBindingCatalog().GetNativeFunctions();
			StandardNativeStorage.clear();
		}
		else if (bSourceUnchanged)
		{
			ActiveNatives = StandardNativeStorage.empty() ? Natives : &StandardNativeStorage;
		}
		else
		{
			ActiveNatives = StandardLibrary::MergeNativeFunctions(Natives, StandardNativeStorage);
			// A supplied complete catalog is already canonical and was returned
			// directly. Do not mistake an earlier merge buffer for that table.
			if (ActiveNatives == Natives)
			{
				StandardNativeStorage.clear();
			}
		}
		bool bRefreshSnapshot = !bSourceUnchanged;
		if (ActiveModule && !ActiveModule->NativeReferences.empty())
		{
			const bool bCached = bSourceUnchanged && BoundNativeModule == ActiveModule
			    && BoundNativeSource == Natives && BoundNativeRevision == JitNativeBindingsRevision
			    && BoundNativeOwner == ProgramOwner && !BoundNativeOwner.owner_before(ProgramOwner) &&
			    !ProgramOwner.owner_before(BoundNativeOwner) && BoundNativeReferenceSnapshot == ActiveModule->NativeReferences;
			if (!bCached)
			{
				bRefreshSnapshot = true;
				std::string BindingError;
				if (!BindBytecodeNativeReferences(*ActiveModule, *ActiveNatives, BoundNativeStorage, BindingError))
				{
					bHasError = false;
					Error = {};
					ReportError(std::move(BindingError));
					ActiveNatives = nullptr;
					BoundNativeModule = nullptr;
					NativeBindingSourceSnapshot.clear();
					BoundNativeReferenceSnapshot.clear();
					NativeBindingCacheBytes = 0;
					return false;
				}
				BoundNativeModule = ActiveModule;
				BoundNativeSource = Natives;
				BoundNativeRevision = JitNativeBindingsRevision;
				BoundNativeOwner = ProgramOwner;
			}
			ActiveNatives = &BoundNativeStorage;
		}
		else
		{
			BoundNativeStorage.clear();
			BoundNativeModule = nullptr;
			BoundNativeOwner.reset();
			BoundNativeReferenceSnapshot.clear();
		}
		if (bRefreshSnapshot)
		{
			const std::vector<FNativeFunction> EmptySource;
			const std::vector<FBytecodeNativeReference> EmptyReferences;
			const auto& Source = Natives ? *Natives : EmptySource;
			const auto& References = ActiveModule ? ActiveModule->NativeReferences : EmptyReferences;
			NativeBindingCacheBytes = NativeBindingCache::RetainedBytes(
			    StandardNativeStorage, BoundNativeStorage, Source, References);
			if (NativeBindingCacheBytes <= NativeBindingCache::MaxRetainedBytes)
			{
				try
				{
					auto SourceSnapshot = Source;
					auto ReferenceSnapshot = References;
					NativeBindingSourceSnapshot.swap(SourceSnapshot);
					BoundNativeReferenceSnapshot.swap(ReferenceSnapshot);
					NativeBindingCacheBytes = NativeBindingCache::RetainedBytes(StandardNativeStorage,
					    BoundNativeStorage, NativeBindingSourceSnapshot, BoundNativeReferenceSnapshot);
				}
				catch (const std::bad_alloc&)
				{
					// Retention is optional. The normally bound table remains usable.
					NativeBindingSourceSnapshot.clear();
					BoundNativeReferenceSnapshot.clear();
					NativeBindingCacheBytes = 0;
				}
			}
			else
			{
				NativeBindingSourceSnapshot.clear();
				BoundNativeReferenceSnapshot.clear();
			}
		}
		const bool bHasBindings =
		    ActiveNatives->size() > StandardLibrary::GetBindingCatalog().GetNativeFunctions().size();
		if (JitNativeBindingsRevision != 0)
		{
			// Session/catalog revisions promise immutable registrations for that
			// revision. Preserve this contract when the optional metadata snapshot
			// is over budget or unavailable; known snapshot changes still retire code.
			if (BackendCache && (bKnownSourceChange || BackendCache->GetProgram().Identity.NativeBindingsGeneration != JitNativeBindingsRevision))
			{
				BackendCache.reset();
			}
			JitNativeBindingsGeneration = JitNativeBindingsRevision;
		}
		else if ((bHasBindings || bJitHadNativeBindings) && !bSourceUnchanged)
		{
			// Registrations may change in place between execution domains. Complete
			// metadata equality above proves the unchanged case without pointer trust.
			// Without that proof, unversioned tables always retain conservative retirement.
			// Active invocations keep a separate lease on their code storage.
			BackendCache.reset();
			if (++JitNativeBindingsGeneration == 0)
			{
				std::terminate();
			}
		}
		bJitHadNativeBindings = bHasBindings;
		// A host may reuse the vector object while changing registrations between
		// runs. Invalidate explicitly at every execution/API entry instead of
		// trusting pointer identity.
		ActiveNativeIndexSource = nullptr;
		return true;
	}
} // namespace VeroScript
