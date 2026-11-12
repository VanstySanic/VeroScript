// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptRuntime.h"

#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::Interop
{
	namespace
	{
		bool SameOwner(const std::weak_ptr<FSessionState>& Left, const std::weak_ptr<FSessionState>& Right) noexcept
		{
			return !Left.owner_before(Right) && !Right.owner_before(Left);
		}

		// A declaration describes future contents as well as the current graph.
		// Unknown names need tracking without a module, but do not acquire a
		// session merely because they might be ordinary host/enum types.
		bool DeclarationNeedsOwner(std::string_view Type, const FBytecodeModule* Module,
		    const bool TrackUnknown, const std::size_t Depth = 0)
		{
			if (Depth > 64) return true;
			Type = TrimNativeTypeName(Runtime::StripReferenceTypeName(Type));
			if (Runtime::IsPrimitiveValueTypeName(Type) || Type == "void") return false;
			if (Type == "variant" || Type == "std::variant" || Type == "std.variant" || Runtime::IsFunctionTypeName(Type))
			{
				return true;
			}
			if (Runtime::IsSetTypeName(Type) || Runtime::IsMultiSetTypeName(Type) || Runtime::IsOrderedMultiSetTypeName(Type) || Runtime::IsChannelTypeName(Type))
			{
				return false;
			}
			std::string First, Value;
			if (Runtime::IsDynamicArrayTypeName(Type))
			{
				return !Runtime::ParseDynamicArrayElementType(Type, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsMapTypeName(Type))
			{
				return !Runtime::ParseMapType(Type, First, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsOrderedMapTypeName(Type))
			{
				return !Runtime::ParseOrderedMapType(Type, First, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsQueueTypeName(Type))
			{
				return !Runtime::ParseQueueElementType(Type, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsStackTypeName(Type))
			{
				return !Runtime::ParseStackElementType(Type, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsDequeTypeName(Type))
			{
				return !Runtime::ParseDequeElementType(Type, Value) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (Runtime::IsNativeArrayTypeName(Type))
			{
				std::uint32_t Length = 0;
				return !Runtime::ParseNativeArrayType(Type, Value, Length) || DeclarationNeedsOwner(Value, Module, TrackUnknown, Depth + 1);
			}
			if (!Module) return TrackUnknown;
			if (Runtime::IsPairTypeName(Type)) return true; // builtin script struct
			// Container declarations already contain canonical runtime names. Leaf
			// lookup could mistake an independent host type for a private script
			// type with the same source-level name.
			for (const auto& Declared : Module->Types)
			{
				if (std::string_view(Declared.Name) == Type) return true;
			}
			for (const auto& Declared : Module->Interfaces)
			{
				if (std::string_view(Declared.Name) == Type) return true;
			}
			return false;
		}

		std::string_view ValueSlotDeclaration(const FValue& Data)
		{
			if (const auto* Value = std::get_if<FNativeArray>(&Data)) return Value->ElementTypeName();
			return std::visit([](const auto& Value) -> std::string_view
			{
				using T = std::decay_t<decltype(Value)>;
				if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle>)
				{
					return Value ? std::string_view(Value->ElementTypeName) : std::string_view{};
				}
				else if constexpr (std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle>)
				{
					return Value ? std::string_view(Value->ValueTypeName) : std::string_view{};
				}
				else
				{
					return {};
				}
			}, Data);
		}

		const void* SharedBackingIdentity(const FValue& Data) noexcept
		{
			return std::visit([](const auto& Value) -> const void*
			{
				using T = std::decay_t<decltype(Value)>;
				if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle>)
				{
					return Value.get();
				}
				else
				{
					return nullptr;
				}
			}, Data);
		}

		bool CanContainSharedBacking(const FValue& Data) noexcept
		{
			return SharedBackingIdentity(Data) || std::holds_alternative<FNativeArray>(Data) || std::holds_alternative<FObjectHandle>(Data) || std::holds_alternative<FFunctionHandle>(Data) || std::holds_alternative<FScriptVariantHandle>(Data);
		}

		struct FSharedContainerOwnership;
		struct FOwnershipRegistry
		{
			std::mutex Mutex;
			std::unordered_map<const void*, std::weak_ptr<FSharedContainerOwnership>> Entries;
		};

		std::shared_ptr<FOwnershipRegistry> OwnershipRegistry(const bool Create)
		{
			static std::mutex Mutex;
			static std::shared_ptr<FOwnershipRegistry> Registry;
			const std::lock_guard<std::mutex> Lock(Mutex);
			if (!Registry && Create)
			{
				Registry = std::make_shared<FOwnershipRegistry>();
			}
			return Registry;
		}

		struct FSharedContainerOwnership
		{
			FSharedContainerOwnership(std::shared_ptr<FOwnershipRegistry> InRegistry,
			    const void* InIdentity, const FValue& Data)
			    : Registry(std::move(InRegistry)), Identity(InIdentity), Snapshot(Data)
			{
			}
			~FSharedContainerOwnership()
			{
				// All payload/root members are destroyed after this lock is released.
				const std::lock_guard<std::mutex> Lock(Registry->Mutex);
				const auto Found = Registry->Entries.find(Identity);
				if (Found != Registry->Entries.end() && Found->second.expired())
				{
					Registry->Entries.erase(Found);
				}
			}
			std::shared_ptr<FOwnershipRegistry> Registry;
			const void* Identity;
			FValue Snapshot;
			std::mutex Mutex;
			std::weak_ptr<FSessionState> Origin;
			FValueRoot Root;
			bool ModuleBound = false;
		};

		std::shared_ptr<FSharedContainerOwnership> FindOwnership(
		    const std::shared_ptr<FOwnershipRegistry>& Registry, const void* Identity)
		{
			const std::lock_guard<std::mutex> Lock(Registry->Mutex);
			const auto Found = Registry->Entries.find(Identity);
			return Found == Registry->Entries.end() ? nullptr : Found->second.lock();
		}

		std::shared_ptr<FSharedContainerOwnership> GetOwnership(
		    const std::shared_ptr<FOwnershipRegistry>& Registry, const FValue& Data, const void* Identity)
		{
			if (auto Existing = FindOwnership(Registry, Identity)) return Existing;
			// Snapshot/candidate destruction must never run while holding the map lock.
			auto Candidate = std::make_shared<FSharedContainerOwnership>(Registry, Identity, Data);
			const std::lock_guard<std::mutex> Lock(Registry->Mutex);
			auto& Slot = Registry->Entries[Identity];
			if (auto Existing = Slot.lock()) return Existing;
			Slot = Candidate;
			return Candidate;
		}
	}

	struct FSharedValueOwnership
	{
		std::mutex Mutex;
		std::shared_ptr<FSharedContainerOwnership> Top;
		std::vector<std::shared_ptr<FSharedContainerOwnership>> Nested;
	};

	bool ValueNeedsSessionOwner(const FValue& Data, const FSessionState* State,
	    const FNativeCallContext* Native)
	{
		// A worker has no Session, and an active callback can belong to an older
		// generation than the Session's current candidate. Its VM owns the exact
		// semantic metadata needed to distinguish empty class and enum containers.
		const auto* Module = Native && Native->ScriptInvoker ? Native->ScriptInvoker->GetActiveModule() : nullptr;
		if (!Module && State)
		{
			Module = State->Session.GetLoadedModule();
		}
		const auto Slot = ValueSlotDeclaration(Data);
		if ((Module || Native) && !Slot.empty() && DeclarationNeedsOwner(Slot, Module, false)) return true;
		if (ValueRequiresOwner(Data)) return true;
		if (!Module && !Native) return false;
		bool Required = false;
		const bool Completed = VisitValueGraph(Data, [&](const FValue& Value, const void*)
		{
			const auto Type = ValueSlotDeclaration(Value);
			if (!Type.empty() && DeclarationNeedsOwner(Type, Module, false))
			{
				Required = true;
			}
			return !Required;
		});
		return Required || !Completed;
	}

	void RejectUnownedModuleStorage(const FValue& Data, const FNativeCallContext* Native)
	{
		// Worker callbacks have no Session root registry. They may forward their
		// private script values, but external C storage must not acquire a graph
		// through a mutable variant/function slot on that worker.
		if (!Native || !CanContainSharedBacking(Data)) return;
		if (SharedBackingIdentity(Data) && !DeclarationNeedsOwner(ValueSlotDeclaration(Data), nullptr, true))
		{
			return;
		}
		const auto Registry = OwnershipRegistry(false);
		if (!Registry) return;
		{
			const std::lock_guard<std::mutex> Lock(Registry->Mutex);
			if (Registry->Entries.empty()) return;
		}
		std::unordered_set<const void*> Seen;
		if (!VisitValueGraph(Data, [&](const FValue& Value, const void*)
		{
			const auto Identity = SharedBackingIdentity(Value);
			if (Identity && Seen.insert(Identity).second && FindOwnership(Registry, Identity) && ValueNeedsSessionOwner(Value, nullptr, Native))
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR,
				    "VeroScriptInterop worker cannot acquire external module-value storage"};
			}
			return true;
		}))
		{
			throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
		}
	}

	std::shared_ptr<FSharedValueOwnership> GetValueSharedOwnership(const FValue& Data, const bool CreateTop)
	{
		if (!CanContainSharedBacking(Data)) return nullptr;
		const auto TopIdentity = SharedBackingIdentity(Data);
		if (TopIdentity && !DeclarationNeedsOwner(ValueSlotDeclaration(Data), nullptr, true)) return nullptr;
		std::shared_ptr<FSharedValueOwnership> Result;
		const auto Registry = OwnershipRegistry(TopIdentity != nullptr && CreateTop);
		if (!Registry) return nullptr;
		if (!TopIdentity || !CreateTop)
		{
			const std::lock_guard<std::mutex> Lock(Registry->Mutex);
			if (Registry->Entries.empty()) return nullptr;
		}
		std::unordered_set<const void*> Seen;
		if (!VisitValueGraph(Data, [&](const FValue& Value, const void*)
		{
			const auto Identity = SharedBackingIdentity(Value);
			if (!Identity || !Seen.insert(Identity).second) return true;
			// Only a backing directly exposed as a C value needs a new cohort.
			// Existing nested C aliases must survive, but manufacturing cohorts for
			// every node of a script graph would duplicate roots and graph walks.
			auto Cohort = Identity == TopIdentity && CreateTop
			    ? GetOwnership(Registry, Value, Identity)
			    : FindOwnership(Registry, Identity);
			if (!Cohort) return true;
			if (!Result)
			{
				Result = std::make_shared<FSharedValueOwnership>();
			}
			if (Identity == TopIdentity)
			{
				Result->Top = std::move(Cohort);
			}
			else
			{
				Result->Nested.push_back(std::move(Cohort));
			}
			return true;
		}))
		{
			throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
		}
		return Result;
	}

	void BindValueOwnership(const FValue& Data, FSessionState& State, const FNativeCallContext* Native)
	{
		// A private borrowed script result creates no cohort. In particular it
		// must remain forwardable during replacement initialization.
		if (!CanContainSharedBacking(Data)) return;
		if (SharedBackingIdentity(Data) && !DeclarationNeedsOwner(ValueSlotDeclaration(Data), nullptr, true))
		{
			return;
		}
		const auto Registry = OwnershipRegistry(false);
		if (!Registry) return;
		{
			const std::lock_guard<std::mutex> Lock(Registry->Mutex);
			if (Registry->Entries.empty()) return;
		}
		std::vector<std::shared_ptr<FSharedContainerOwnership>> Cohorts;
		std::unordered_set<const void*> Seen;
		if (!VisitValueGraph(Data, [&](const FValue& Value, const void*)
		{
			const auto Identity = SharedBackingIdentity(Value);
			if (Identity && Seen.insert(Identity).second)
			{
				if (auto Cohort = FindOwnership(Registry, Identity))
				{
					Cohorts.push_back(std::move(Cohort));
				}
			}
			return true;
		}))
		{
			throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
		}
		if (Cohorts.empty()) return;
		const auto Target = State.weak_from_this();
		for (const auto& Cohort : Cohorts)
		{
			bool AlreadyBound = false;
			{
				const std::lock_guard<std::mutex> Lock(Cohort->Mutex);
				AlreadyBound = Cohort->ModuleBound && Cohort->Root.IsRegistered() && SameOwner(Cohort->Origin, Target);
			}
			if (AlreadyBound)
			{
				if (State.OwnerThread != std::this_thread::get_id())
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_WRONG_THREAD};
				}
				if (!State.Session.CanExportModuleValues() || !State.Session.ValuesOpen())
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
				continue;
			}
			const bool NeedsOwner = ValueNeedsSessionOwner(Cohort->Snapshot, &State, Native);
			FValueRoot Prepared;
			std::string Error;
			const auto CheckOrigin = [&]
			{
				const std::weak_ptr<FSessionState> Empty;
				if (!SameOwner(Cohort->Origin, Empty) && !SameOwner(Cohort->Origin, Target) && (Cohort->ModuleBound || NeedsOwner))
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR,
					    "VeroScriptInterop value belongs to a foreign session"};
				}
			};
			bool ReserveRoot = false;
			{
				const std::lock_guard<std::mutex> Lock(Cohort->Mutex);
				CheckOrigin();
				ReserveRoot = NeedsOwner && !Cohort->Root.IsRegistered();
			}
			if (NeedsOwner)
			{
				if (State.OwnerThread != std::this_thread::get_id())
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_WRONG_THREAD};
				}
				if (!State.Session.CanExportModuleValues() || !State.Session.ValuesOpen())
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
				if (ReserveRoot && !State.Session.RetainValue(Cohort->Snapshot, Prepared, Error))
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
			}
			{
				const std::lock_guard<std::mutex> Lock(Cohort->Mutex);
				CheckOrigin();
				if (Prepared.IsRegistered() && !Cohort->Root.IsRegistered())
				{
					Cohort->Root = std::move(Prepared);
				}
				if (NeedsOwner)
				{
					Cohort->ModuleBound = true;
				}
				Cohort->Origin = Target;
			}
		}
	}

	void SynchronizeValueOwnership(const ::vs_interop_value& Value)
	{
		const auto Holder = Value.SharedOwnership;
		if (!Holder) return;
		FValueRoot TopRoot;
		std::weak_ptr<FSessionState> Origin;
		bool Bound = false;
		{
			const std::lock_guard<std::mutex> HolderLock(Holder->Mutex);
			const auto Inspect = [&](const std::shared_ptr<FSharedContainerOwnership>& Cohort, const bool Top)
			{
				if (!Cohort) return;
				const std::lock_guard<std::mutex> Lock(Cohort->Mutex);
				if (!Cohort->Root.IsRegistered()) return;
				if (!Cohort->Root.Read())
				{
					const auto Owner = Cohort->Root.Owner();
					throw FValueAccessFailure{Owner && !Owner->IsOwnerThread()
					        ? VS_INTEROP_STATUS_WRONG_THREAD
					        : VS_INTEROP_STATUS_ENGINE_ERROR};
				}
				if (Bound && !SameOwner(Origin, Cohort->Origin))
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
				Origin = Cohort->Origin;
				Bound = true;
				if (Top)
				{
					TopRoot = Cohort->Root;
				}
			};
			Inspect(Holder->Top, true);
			for (const auto& Cohort : Holder->Nested)
			{
				Inspect(Cohort, false);
			}
		}
		if (!Bound) return;
		if (!TopRoot.IsRegistered() && !Value.Root.IsRegistered())
		{
			// Fixed arrays/immutable boxes have no shared top backing. A complete
			// wrapper root replaces their temporary collection of child leases.
			const auto Owner = Origin.lock();
			const auto* Data = Value.Storage ? &*Value.Storage : Value.BorrowedRead;
			std::string Error;
			if (!Owner || !Data || !Owner->Session.CanExportModuleValues() || !Owner->Session.RetainValue(*Data, TopRoot, Error))
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
			}
		}
		if (TopRoot.IsRegistered())
		{
			Value.Root = std::move(TopRoot);
		}
		Value.Owner = Origin;
		// Once an ancestor root covers the current graph, redundant child cohorts
		// must not keep elements alive after script removes them from that graph.
		std::vector<std::shared_ptr<FSharedContainerOwnership>> RetiredChildren;
		if (Value.Root.IsRegistered())
		{
			const std::lock_guard<std::mutex> Lock(Holder->Mutex);
			RetiredChildren.swap(Holder->Nested);
		}
		// Retire roots and release payload snapshots outside every registry/holder lock.
	}
}
