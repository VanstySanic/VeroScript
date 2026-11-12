// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptValueRoot.h"
#include "VeroScriptValueGraphInternal.h"
#include "VeroScriptVariantInternal.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		struct FRootNode
		{
			FValue Value = std::int32_t{0};
			std::atomic_bool Retired{false};
			bool Reserved = false;
			bool PendingCleanup = false;
			bool GenerationScoped = false;
			bool Migratable = false;
			std::size_t MigrationIndex = 0;
			std::atomic_bool MigrationPending{false};
			std::atomic_bool Invalidated{false};
			std::weak_ptr<FValueRootLease> Lease;
		};
	}
	struct FValueRootState
	{
		const std::thread::id Thread = std::this_thread::get_id();
		std::atomic<unsigned> Phase{0}; // open, closing, finalizing, closed
		std::atomic<std::size_t> Reservations{0};
		std::atomic<std::uint64_t> RetirementEpoch{0};
		std::atomic<std::uint64_t> GenerationRetirementEpoch{0};
		std::uint64_t DrainedEpoch = 0;
		std::uint64_t GenerationDrainedEpoch = 0;
		std::vector<std::shared_ptr<FRootNode>> Nodes;
		std::mutex Mutex;
	};
	struct FValueRootLease
	{
		std::weak_ptr<FValueRootOwner> Owner;
		std::shared_ptr<FValueRootState> State;
		std::shared_ptr<FRootNode> Node;
		bool Reservation = false;
		~FValueRootLease()
		{
			if (!Node) return;
			if (Reservation)
			{
				State->Reservations.fetch_sub(1, std::memory_order_release);
			}
			else
			{
				Node->Retired.store(true, std::memory_order_release);
				State->RetirementEpoch.fetch_add(1, std::memory_order_release);
				if (Node->GenerationScoped)
				{
					State->GenerationRetirementEpoch.fetch_add(1, std::memory_order_release);
				}
			}
		}
	};
	const FValue* FValueRoot::Read() const noexcept
	{
		return Lease && Lease->State->Phase.load(std::memory_order_acquire) == 0 && !Lease->Node->Invalidated.load(std::memory_order_acquire) && !Lease->Node->MigrationPending.load(std::memory_order_acquire) && Lease->State->Thread == std::this_thread::get_id()
		    ? &Lease->Node->Value
		    : nullptr;
	}
	std::shared_ptr<FValueRootOwner> FValueRoot::Owner() const noexcept
	{
		return Lease ? Lease->Owner.lock() : nullptr;
	}
	FValueRootOwner::FValueRootOwner() : State(std::make_shared<FValueRootState>())
	{
	}

	FValueRootOwner::~FValueRootOwner()
	{
		State->Phase.store(3, std::memory_order_release);
	}

	bool FValueRootOwner::Retain(FValue Value, FValueRoot& Output, std::string& Error)
	{
		if (!IsOwnerThread())
		{
			Error = "value retention requires the owner thread";
			return false;
		}
		FValueRoot Prepared;
		if (!Reserve(Prepared, Error) || !CommitReserved(Prepared, Value, Error)) return false;
		Output = std::move(Prepared);
		return true;
	}

	bool FValueRootOwner::RetainForGeneration(FValue Value, FValueRoot& Output, std::string& Error)
	{
		if (!IsOwnerThread())
		{
			Error = "value retention requires the owner thread";
			return false;
		}
		FValueRoot Prepared;
		if (!Reserve(Prepared, Error)) return false;
		Prepared.Lease->Node->GenerationScoped = true;
		if (!CommitReserved(Prepared, Value, Error)) return false;
		Output = std::move(Prepared);
		return true;
	}

	bool FValueRootOwner::RetainForMigration(FValue Value, FValueRoot& Output, std::string& Error)
	{
		if (!IsOwnerThread())
		{
			Error = "value retention requires the owner thread";
			return false;
		}
		FValueRoot Prepared;
		if (!Reserve(Prepared, Error)) return false;
		Prepared.Lease->Node->Migratable = true;
		if (!CommitReserved(Prepared, Value, Error)) return false;
		Output = std::move(Prepared);
		return true;
	}

	bool FValueRootOwner::InvalidateGeneration(std::string& Error)
	{
		if (!IsOwnerThread())
		{
			Error = "generation invalidation requires the owner thread";
			return false;
		}
		bool Changed = false;
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			for (const auto& Node : State->Nodes)
			{
				if (Node->GenerationScoped && !Node->Invalidated.exchange(true, std::memory_order_acq_rel))
				{
					Node->Retired.store(true, std::memory_order_release);
					Changed = true;
				}
			}
		}
		if (Changed)
		{
			State->RetirementEpoch.fetch_add(1, std::memory_order_release);
			State->GenerationRetirementEpoch.fetch_add(1, std::memory_order_release);
		}
		return true;
	}

	bool FValueRootOwner::HasExternalRoots(const bool IncludeMigratable) const noexcept
	{
		if (!IsOwnerThread()) return true;
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		if (State->Reservations.load(std::memory_order_acquire)) return true;
		for (const auto& Node : State->Nodes)
		{
			if (!Node->GenerationScoped && (IncludeMigratable || !Node->Migratable) && !Node->Retired.load(std::memory_order_acquire)) return true;
		}
		return false;
	}

	bool FValueRootOwner::PrepareMigration(std::vector<FValueRoot>& Roots, std::vector<FValue>& Values, std::string& Error)
	{
		if (!IsOwnerThread() || !IsOpen() || !Roots.empty() || !Values.empty())
		{
			Error = "value migration requires an idle owner and empty staging storage";
			return false;
		}
		std::vector<FValueRoot> PreparedRoots;
		std::vector<FValue> PreparedValues;
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		if (State->Reservations.load(std::memory_order_acquire))
		{
			Error = "value migration cannot overlap an exported result reservation";
			return false;
		}
		for (const auto& Node : State->Nodes)
		{
			if (Node->GenerationScoped || Node->Retired.load(std::memory_order_acquire))
			{
				continue;
			}
			if (!Node->Migratable || Node->MigrationPending.load(std::memory_order_acquire))
			{
				Error = "release ordinary external roots before reloading the module";
				return false;
			}
			if (auto Lease = Node->Lease.lock())
			{
				FValueRoot Root;
				Root.Lease = std::move(Lease);
				PreparedRoots.push_back(std::move(Root));
				PreparedValues.push_back(Node->Value);
			}
		}
		for (std::size_t Index = 0; Index < PreparedRoots.size(); ++Index)
		{
			auto& Node = *PreparedRoots[Index].Lease->Node;
			Node.MigrationIndex = Index;
			Node.MigrationPending.store(true, std::memory_order_release);
		}
		Roots = std::move(PreparedRoots);
		Values = std::move(PreparedValues);
		return true;
	}

	bool FValueRootOwner::CompleteMigration(std::vector<FValueRoot>& Roots, std::vector<FValue>* Values, std::string& Error)
	{
		static_assert(std::is_nothrow_swappable_v<FValue>, "root settlement must not partially commit");
		if (!IsOwnerThread() || !IsOpen() || (Values && Values->size() != Roots.size()))
		{
			Error = "invalid value migration settlement";
			return false;
		}
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		const auto PendingCount = static_cast<std::size_t>(std::count_if(State->Nodes.begin(), State->Nodes.end(), [](const auto& Node)
		{
			return Node->MigrationPending.load(std::memory_order_acquire);
		}));
		if (PendingCount != Roots.size())
		{
			Error = "value migration settlement omitted or duplicated a pending root";
			return false;
		}
		for (std::size_t Index = 0; Index < Roots.size(); ++Index)
		{
			const auto& Root = Roots[Index];
			if (!Root.Lease || Root.Lease->State != State || !Root.Lease->Node->MigrationPending.load(std::memory_order_acquire) ||
			    Root.Lease->Node->MigrationIndex != Index)
			{
				Error = "value migration root set changed before settlement";
				return false;
			}
		}
		for (std::size_t Index = 0; Index < Roots.size(); ++Index)
		{
			auto& Node = *Roots[Index].Lease->Node;
			if (Values)
			{
				Node.Value.swap((*Values)[Index]);
			}
			Node.MigrationPending.store(false, std::memory_order_release);
		}
		return true;
	}

	bool FValueRootOwner::Reserve(FValueRoot& Output, std::string& Error)
	{
		if (!IsOpen())
		{
			Error = "value owner is closed";
			return false;
		}
		auto Node = std::make_shared<FRootNode>();
		auto Lease = std::make_shared<FValueRootLease>();
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		if (!IsOpen())
		{
			Error = "value owner is closed";
			return false;
		}
		const auto Pending = State->Reservations.load(std::memory_order_acquire);
		if (Pending >= State->Nodes.max_size() - State->Nodes.size())
		{
			Error = "value root table is full";
			return false;
		}
		const auto Required = State->Nodes.size() + Pending + 1;
		if (Required > State->Nodes.capacity())
		{
			const auto Capacity = State->Nodes.capacity();
			const auto Grown = Capacity > State->Nodes.max_size() / 2 ? State->Nodes.max_size() : Capacity * 2;
			State->Nodes.reserve(std::max(Required, Grown));
		}
		Lease->Owner = shared_from_this();
		Lease->State = State;
		Lease->Node = Node;
		Node->Lease = Lease;
		Node->Reserved = true;
		State->Reservations.fetch_add(1, std::memory_order_release);
		Lease->Reservation = true;
		FValueRoot Prepared;
		Prepared.Lease = std::move(Lease);
		Output = std::move(Prepared);
		return true;
	}

	bool FValueRootOwner::CommitReserved(FValueRoot& Root, FValue& Prepared, std::string& Error)
	{
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		if (State->Phase.load(std::memory_order_acquire) >= 2 || !Root.Lease || Root.Lease->State != State || !Root.Lease->Node->Reserved)
		{
			Error = "invalid value result reservation";
			return false;
		}
		Root.Lease->Node->Value = std::move(Prepared);
		State->Nodes.push_back(Root.Lease->Node); // capacity was reserved before value creation
		Root.Lease->Node->Reserved = false;
		Root.Lease->Reservation = false;
		State->Reservations.fetch_sub(1, std::memory_order_release);
		return true;
	}

	bool FValueRootOwner::Drain(const FCleanup& Cleanup, std::string& Error)
	{
		return DrainImpl(Cleanup, Error, false);
	}

	bool FValueRootOwner::DrainGeneration(const FCleanup& Cleanup, std::string& Error)
	{
		return DrainImpl(Cleanup, Error, true);
	}

	std::vector<FValue> FValueRootOwner::SnapshotLiveValues() const
	{
		std::vector<FValue> Values;
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			Values.reserve(State->Nodes.size());
			for (const auto& Node : State->Nodes)
			{
				if (!Node->Retired.load(std::memory_order_acquire))
				{
					Values.push_back(Node->Value);
				}
			}
		}
		return Values;
	}

	bool FValueRootOwner::HasLiveRoots() const noexcept
	{
		if (!IsOwnerThread()) return true;
		const std::lock_guard<std::mutex> Lock(State->Mutex);
		for (const auto& N : State->Nodes)
		{
			if (!N->Retired.load(std::memory_order_acquire)) return true;
		}
		return false;
	}

	bool FValueRootOwner::IsOpen() const noexcept
	{
		return State->Phase.load(std::memory_order_acquire) == 0;
	}

	bool FValueRootOwner::IsOwnerThread() const noexcept
	{
		return State->Thread == std::this_thread::get_id();
	}

	void FValueRootOwner::BeginClose() noexcept
	{
		unsigned Open = 0;
		State->Phase.compare_exchange_strong(Open, 1, std::memory_order_acq_rel);
	}

	bool FValueRootOwner::Close(const FCleanup& Cleanup, std::string& Error)
	{
		if (!IsOwnerThread())
		{
			Error = "value owner must close on its owner thread";
			return false;
		}
		if (State->Phase.load(std::memory_order_acquire) == 3) return true;
		BeginClose();
		State->Phase.store(2, std::memory_order_release);
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			for (const auto& N : State->Nodes)
			{
				N->Retired.store(true, std::memory_order_release);
			}
		}
		State->RetirementEpoch.fetch_add(1, std::memory_order_release);
		State->GenerationRetirementEpoch.fetch_add(1, std::memory_order_release);
		const bool Ok = Drain(Cleanup, Error);
		State->Phase.store(3, std::memory_order_release);
		return Ok;
	}

	bool FValueRootOwner::DrainImpl(const FCleanup& Cleanup, std::string& Error, bool GenerationOnly)
	{
		if (!IsOwnerThread())
		{
			Error = "value cleanup requires the owner thread";
			return false;
		}
		const auto GenerationEpoch = State->GenerationRetirementEpoch.load(std::memory_order_acquire);
		const auto Epoch = GenerationOnly ? GenerationEpoch : State->RetirementEpoch.load(std::memory_order_acquire);
		auto& DrainedEpoch = GenerationOnly ? State->GenerationDrainedEpoch : State->DrainedEpoch;
		if (Epoch == DrainedEpoch) return true;
		std::vector<FValue> Retired, Keep;
		std::vector<std::shared_ptr<FRootNode>> Selected;
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			Retired.reserve(State->Nodes.size());
			Keep.reserve(State->Nodes.size());
			Selected.reserve(State->Nodes.size());
			// Reserve before moving anything. A failure leaves every root registered.
			for (const auto& N : State->Nodes)
			{
				if (N->Retired.load(std::memory_order_acquire) && (!GenerationOnly || N->GenerationScoped))
				{
					Selected.push_back(N);
				}
				else
				{
					Keep.push_back(N->Value);
				}
			}
			for (const auto& N : Selected)
			{
				N->PendingCleanup = true;
				Retired.push_back(std::move(N->Value));
			}
		}
		if (Retired.empty())
		{
			DrainedEpoch = Epoch;
			if (!GenerationOnly)
			{
				State->GenerationDrainedEpoch = GenerationEpoch;
			}
			return true;
		}
		const auto Restore = [&]()
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			for (std::size_t I = 0; I < Retired.size() && I < Selected.size(); ++I)
			{
				Selected[I]->PendingCleanup = false;
				Selected[I]->Value = std::move(Retired[I]);
			}
		};
		bool Ok = false;
		try
		{
			Ok = Cleanup(Retired, Keep);
		}
		catch (...)
		{
			Restore();
			throw;
		}
		if (!Ok)
		{
			Restore();
			if (Error.empty())
			{
				Error = "external value cleanup failed";
			}
		}
		else
		{
			const std::lock_guard<std::mutex> Lock(State->Mutex);
			State->Nodes.erase(std::remove_if(State->Nodes.begin(), State->Nodes.end(),
			                       [](const auto& N)
			{
				return N->PendingCleanup;
			}),
			    State->Nodes.end());
			DrainedEpoch = Epoch;
			if (!GenerationOnly)
			{
				State->GenerationDrainedEpoch = GenerationEpoch;
			}
		}
		return Ok;
	}

	bool VisitValueGraph(const FValue& Root, const std::function<bool(const FValue&, const void*)>& Visitor)
	{
		std::unordered_set<const void*> Seen;
		return ValueGraphInternal::Visit(Root, Visitor, Seen);
	}
	bool ValueGraphInternal::Visit(const FValue& Root,
	    const std::function<bool(const FValue&, const void*)>& Visitor, std::unordered_set<const void*>& Seen)
	{
		const auto Visit = [&](auto& Self, const FValue& V, std::size_t Depth) -> bool
		{
			if (Depth > 256) return false;
			const auto Sequence = [&](const auto& H, const auto& Values) -> bool
			{
				if (!Visitor(V, H.get())) return false;
				if (!H || !Seen.insert(H.get()).second) return true;
				for (const auto& E : Values)
				{
					if (!Self(Self, E, Depth + 1)) return false;
				}
				return true;
			};
			const auto Map = [&](const auto& H) -> bool
			{
				if (!Visitor(V, H.get())) return false;
				if (!H || !Seen.insert(H.get()).second) return true;
				const auto Entries = [&](const auto& Values)
				{
					for (const auto& E : Values)
					{
						if (!Self(Self, E.second, Depth + 1)) return false;
					}
					return true;
				};
				if (!Entries(H->IntEntries) || !Entries(H->UIntEntries) || !Entries(H->Int64Entries) || !Entries(H->UInt64Entries) || !Entries(H->StrEntries) || !Entries(H->WStrEntries) || !Entries(H->EnumEntries) || !Entries(H->HostEntries))
				{
					return false;
				}
				for (const auto& E : H->HostEntries)
				{
					if (!Self(Self, FValue(E.first), Depth + 1)) return false;
				}
				return true;
			};
			if (const auto* H = std::get_if<FScriptVariantHandle>(&V))
			{
				if (!Visitor(V, H->get())) return false;
				return !*H || !Seen.insert(H->get()).second || Self(Self, (*H)->Value, Depth + 1);
			}
			if (const auto* H = std::get_if<FObjectHandle>(&V))
			{
				if (!Visitor(V, H->get())) return false;
				if (!*H || !Seen.insert(H->get()).second) return true;
				for (const auto& E : (*H)->Fields)
				{
					if (!Self(Self, E, Depth + 1)) return false;
				}
				return true;
			}
			if (const auto* H = std::get_if<FFunctionHandle>(&V))
			{
				if (!Visitor(V, H->get())) return false;
				if (!*H || !Seen.insert(H->get()).second) return true;
				for (const auto& E : (*H)->Captures)
				{
					if (!Self(Self, E, Depth + 1)) return false;
				}
				return !(*H)->CapturedThis || Self(Self, FValue((*H)->CapturedThis), Depth + 1);
			}
			if (const auto* A = std::get_if<FNativeArray>(&V))
			{
				if (!Visitor(V, nullptr)) return false;
				for (const auto& E : A->Storage().Elements)
				{
					if (!Self(Self, E, Depth + 1)) return false;
				}
				return true;
			}
			if (const auto* H = std::get_if<FDynamicArrayHandle>(&V))
			{
				if (!*H) return Visitor(V, nullptr);
				return Sequence(*H, (*H)->Elements);
			}
			if (const auto* H = std::get_if<FQueueHandle>(&V))
			{
				if (!*H) return Visitor(V, nullptr);
				return Sequence(*H, (*H)->Elements);
			}
			if (const auto* H = std::get_if<FStackHandle>(&V))
			{
				if (!*H) return Visitor(V, nullptr);
				return Sequence(*H, (*H)->Elements);
			}
			if (const auto* H = std::get_if<FDequeHandle>(&V))
			{
				if (!*H) return Visitor(V, nullptr);
				return Sequence(*H, (*H)->Elements);
			}
			if (const auto* H = std::get_if<FMapHandle>(&V)) return Map(*H);
			if (const auto* H = std::get_if<FOrderedMapHandle>(&V)) return Map(*H);
			return Visitor(V, nullptr);
		};
		return Visit(Visit, Root, 0);
	}
	bool ValueRequiresOwner(const FValue& Value)
	{
		// A validated box's source stamp already records this decision. Primitive
		// boxes have no script-owned children; copying them needs no graph walk.
		if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value)) return *Box && (*Box)->Source != nullptr;
		bool Required = false;
		const bool Completed = VisitValueGraph(Value, [&](const FValue& V, const void*)
		{
			if (const auto* H = std::get_if<FObjectHandle>(&V); H && *H)
			{
				Required = true;
			}
			if (const auto* H = std::get_if<FFunctionHandle>(&V); H && *H)
			{
				Required = true;
			}
			if (const auto* H = std::get_if<FScriptVariantHandle>(&V); H && *H && (*H)->Source)
			{
				Required = true;
			}
			return !Required;
		});
		return Required || !Completed;
	}
}
