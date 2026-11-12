// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemory.h"
#include "VeroScriptMemoryInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace VeroScript
{
	FMemoryContext::FMemoryContext(FMemoryLimits Limits) : State(std::make_shared<FMemoryContextState>())
	{
		State->Budget = std::make_shared<FMemoryBudget>();
		State->Budget->Limits = Limits;
		State->Domain = std::make_shared<FMemoryDomain>();
	}

	FMemoryContext::~FMemoryContext()
	{
		Close();
	}

	std::shared_ptr<FMemoryContext> FMemoryContext::CreateWorker() const
	{
		auto Worker = std::make_shared<FMemoryContextState>();
		Worker->Budget = State->Budget;
		Worker->Domain = std::make_shared<FMemoryDomain>();
		return std::shared_ptr<FMemoryContext>(new FMemoryContext(std::move(Worker)));
	}

	FMemoryResult FMemoryContext::SetLimits(FMemoryLimits Limits)
	{
		if (auto R = Memory::Detail::CheckContext(*this); !R)
		{
			return R;
		}
		auto& B = *State->Budget;
		std::lock_guard<std::mutex> Lock(B.Mutex);
		if (B.Reserved || B.Executions)
		{
			return Memory::Detail::Fail(EMemoryStatus::Busy, "diag.memory_busy");
		}
		if ((Limits.MaxRetainedBytes &&
				B.Stats.OwnedBytes + B.Stats.BorrowedBytes > Limits.MaxRetainedBytes) ||
			(Limits.MaxBufferBytes && !B.Sizes.empty() && *B.Sizes.rbegin() > Limits.MaxBufferBytes))
		{
			return Memory::Detail::Fail(EMemoryStatus::LimitExceeded, "diag.memory_limit");
		}
		B.Limits = Limits;
		return {};
	}

	FMemoryLimits FMemoryContext::GetLimits() const
	{
		std::lock_guard<std::mutex> L(State->Budget->Mutex);
		return State->Budget->Limits;
	}

	FMemoryStats FMemoryContext::GetStats() const
	{
		std::lock_guard<std::mutex> L(State->Budget->Mutex);
		return State->Budget->Stats;
	}

	void FMemoryContext::Close() noexcept
	{
		State->Domain->Active.store(false, std::memory_order_release);
	}

	void FMemoryContext::BeginExecution()
	{
		std::lock_guard<std::mutex> L(State->Budget->Mutex);
		++State->Budget->Executions;
	}

	void FMemoryContext::EndExecution() noexcept
	{
		std::lock_guard<std::mutex> L(State->Budget->Mutex);
		--State->Budget->Executions;
	}

	FMemoryContext::FMemoryContext(std::shared_ptr<FMemoryContextState> InState) : State(std::move(InState))
	{
	}
} // namespace VeroScript

namespace VeroScript::Memory::Detail
{
	namespace
	{
		struct FDescriptorOwner
		{
			void operator()(FHostObject* Object) noexcept
			{
				delete Object;
				// Weak handles may retain the control block after the value dies.
				Descriptor.reset();
			}

			std::shared_ptr<FDescriptor> Descriptor;
			const FHostObject* Identity = nullptr;
			const void* RegistryIdentity = nullptr;
		};

		struct FEntry
		{
			std::weak_ptr<FHostObject> Handle;
			std::weak_ptr<FDescriptor> Descriptor;
		};
		struct FRegistry
		{
			std::mutex Mutex;
			std::map<const FHostObject*, FEntry, std::less<const FHostObject*>> Entries;
			const FHostObject* Cursor = nullptr;
		};
		FRegistry& Registry()
		{
			static FRegistry R;
			return R;
		}
		void Prune(FRegistry& R)
		{
			auto It = R.Entries.upper_bound(R.Cursor);
			if (It == R.Entries.end())
			{
				It = R.Entries.begin();
			}
			for (unsigned I = 0; I < 8 && It != R.Entries.end(); ++I)
			{
				R.Cursor = It->first;
				if (It->second.Handle.expired())
				{
					It = R.Entries.erase(It);
				}
				else
				{
					++It;
				}
			}
		}
	} // namespace
	FMemoryResult CheckContext(const FMemoryContext& Context)
	{
		const auto& S = FMemoryAccess::State(Context);
		if (!S->Domain->Active.load(std::memory_order_acquire))
		{
			return Fail(EMemoryStatus::Expired, "diag.memory_expired");
		}
		if (S->Domain->Thread != std::this_thread::get_id())
		{
			return Fail(EMemoryStatus::WrongThread, "diag.memory_wrong_thread");
		}
		return {};
	}

	FMemoryResult Count(std::int64_t Value)
	{
		if (Value < 0 || static_cast<std::uint64_t>(Value) > std::numeric_limits<std::size_t>::max())
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_range");
		}
		return {};
	}

	FMemoryResult CheckWork(FMemoryWork Work, std::uint64_t Bytes)
	{
		if (Work.Check && !Work.Check(Work.UserData, Bytes / 64 + (Bytes % 64 != 0)))
		{
			return Fail(EMemoryStatus::Interrupted, "diag.memory_interrupted");
		}
		return {};
	}

	FCharge::~FCharge()
	{
		if (!Budget)
		{
			return;
		}
		std::lock_guard<std::mutex> L(Budget->Mutex);
		if (Committed)
		{
			(Borrowed ? Budget->Stats.BorrowedBytes : Budget->Stats.OwnedBytes) -= Size;
			Budget->Sizes.erase(Entry);
		}
		else
		{
			Budget->Reserved -= Size;
		}
	}

	FMemoryResult FCharge::Reserve(
		std::shared_ptr<FMemoryBudget> InBudget, std::uint64_t Bytes, bool bBorrowed)
	{
		std::lock_guard<std::mutex> L(InBudget->Mutex);
		const auto& S = InBudget->Stats;
		const auto Used = S.OwnedBytes + S.BorrowedBytes;
		const auto Max = std::numeric_limits<std::uint64_t>::max();
		if (Bytes > Max - Used || InBudget->Reserved > Max - Used - Bytes ||
			(InBudget->Limits.MaxBufferBytes && Bytes > InBudget->Limits.MaxBufferBytes) ||
			(InBudget->Limits.MaxRetainedBytes &&
				(Used + InBudget->Reserved > InBudget->Limits.MaxRetainedBytes ||
					Bytes > InBudget->Limits.MaxRetainedBytes - Used - InBudget->Reserved)))
		{
			return Fail(EMemoryStatus::LimitExceeded, "diag.memory_limit");
		}
		InBudget->Reserved += Bytes;
		Budget = std::move(InBudget);
		Size = Bytes;
		Borrowed = bBorrowed;
		return {};
	}

	void FCharge::Commit()
	{
		std::lock_guard<std::mutex> L(Budget->Mutex);
		// Allocate metadata before changing accounting; failure leaves the reservation intact.
		Entry = Budget->Sizes.insert(Size);
		Budget->Reserved -= Size;
		(Borrowed ? Budget->Stats.BorrowedBytes : Budget->Stats.OwnedBytes) += Size;
		Budget->Stats.PeakOwnedBytes = std::max(Budget->Stats.PeakOwnedBytes, Budget->Stats.OwnedBytes);
		Committed = true;
	}

	FOwner::~FOwner()
	{
		// No owner, registry, or budget lock is held while entering host code.
		if (Adopted && Release)
		{
			try
			{
				Release(ReleaseUserData);
			}
			catch (...)
			{
			}
		}
	}

	std::shared_ptr<FDescriptor> FindDescriptor(const FValue& Value)
	{
		const auto* H = std::get_if<FHostObjectHandle>(&Value);
		if (!H || !*H)
		{
			return {};
		}
		// The private deleter proves both ownership and the original object address.
		// Aliasing control blocks must not turn an unrelated payload into a region.
		// Builds without RTTI keep the authoritative registry fallback below.
		if (const auto* Owner = std::get_deleter<FDescriptorOwner>(*H))
		{
			const auto& D = Owner->Descriptor;
			if (Owner->Identity != H->get() || Owner->RegistryIdentity != &Registry() || !D
				|| (*H)->TypeName != Names[static_cast<unsigned>(D->Kind)]
				|| (*H)->TypeId != TypeIdBase + static_cast<unsigned>(D->Kind))
			{
				return {};
			}
			return D;
		}
		auto& R = Registry();
		std::lock_guard<std::mutex> L(R.Mutex);
		auto It = R.Entries.find(H->get());
		if (It == R.Entries.end())
		{
			return {};
		}
		auto Handle = It->second.Handle.lock();
		if (!Handle || Handle.owner_before(*H) || H->owner_before(Handle))
		{
			return {};
		}
		auto D = It->second.Descriptor.lock();
		if (!D || Handle->TypeName != Names[static_cast<unsigned>(D->Kind)] ||
			Handle->TypeId != TypeIdBase + static_cast<unsigned>(D->Kind))
		{
			return {};
		}
		return D;
	}

	FMemoryResult MakeValue(std::shared_ptr<FDescriptor> D, FValue& OutValue)
	{
		try
		{
			// Registry initialization may allocate; finish it before creating an unowned object.
			auto& R = Registry();
			auto* Object = new FHostObject();
			auto H = FHostObjectHandle(Object, FDescriptorOwner{D, Object, &R});
			H->TypeName = Names[static_cast<unsigned>(D->Kind)];
			H->TypeId = TypeIdBase + static_cast<unsigned>(D->Kind);
			H->Payload = D.get();
			if (D->Kind != EKind::Buffer)
			{
				H->CloneCallback = CloneDescriptor;
				H->EqualsCallback = EqualDescriptor;
				H->HashCallback = HashDescriptor;
			}
			{
				std::lock_guard<std::mutex> L(R.Mutex);
				Prune(R);
				R.Entries[H.get()] = {H, D};
			}
			OutValue = std::move(H);
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult Resolve(FMemoryContext& Context, const FValue& Value, FRegion& Out, bool Write)
	{
		if (auto R = CheckContext(Context); !R)
		{
			return R;
		}
		auto D = FindDescriptor(Value);
		if (!D)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		if (Write && D->Kind == EKind::ReadOnlyView)
		{
			return Fail(EMemoryStatus::ReadOnly, "diag.memory_readonly");
		}
		Out.Descriptor = D;
		if (!D->Owner)
		{
			Out.Size = 0;
			return {};
		}
		auto& O = *D->Owner;
		std::lock_guard<std::mutex> L(O.Mutex);
		const auto& S = FMemoryAccess::State(Context);
		if (S->Domain != O.State->Domain || S->Budget != O.State->Budget)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_domain");
		}
		if (!O.Valid || (D->Kind != EKind::Buffer && D->Generation != O.Generation))
		{
			return Fail(EMemoryStatus::Expired, "diag.memory_expired");
		}
		if (Write && O.ExternalReadOnly)
		{
			return Fail(EMemoryStatus::ReadOnly, "diag.memory_readonly");
		}
		Out.Offset = D->Kind == EKind::Buffer ? 0 : D->Offset;
		Out.Size = D->Kind == EKind::Buffer ? O.Size : D->Length;
		Out.Data = Out.Size == 0 ? nullptr : O.Data() + static_cast<std::size_t>(Out.Offset);
		return {};
	}

	FMemoryResult Bounds(const FRegion& Region, std::int64_t Offset, std::int64_t Length)
	{
		if (!Count(Offset) || !Count(Length) || Offset > Region.Size || Length > Region.Size - Offset)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_range");
		}
		return {};
	}

	FGuard::~FGuard()
	{
		if (!Owner)
		{
			return;
		}
		std::lock_guard<std::mutex> L(Owner->Mutex);
		if (Write)
		{
			Owner->Writer = false;
		}
		else
		{
			--Owner->Readers;
		}
	}

	FMemoryResult FGuard::Acquire(const FRegion& Region, bool bWrite)
	{
		Owner = Region.Descriptor->Owner;
		if (!Owner)
		{
			return {};
		}
		std::lock_guard<std::mutex> L(Owner->Mutex);
		if (Owner->Writer || (bWrite && Owner->Readers))
		{
			Owner.reset();
			return Fail(EMemoryStatus::Busy, "diag.memory_busy");
		}
		Write = bWrite;
		if (Write)
		{
			Owner->Writer = true;
		}
		else
		{
			if (Owner->Readers == std::numeric_limits<std::uint64_t>::max())
			{
				Owner.reset();
				return Fail(EMemoryStatus::LimitExceeded, "diag.memory_identity_exhausted");
			}
			++Owner->Readers;
		}
		return {};
	}

	FMemoryResult Allocate(FMemoryContext& Context, std::int64_t Capacity, std::shared_ptr<FBlock>& Out)
	{
		if (auto R = Count(Capacity); !R)
		{
			return R;
		}
		try
		{
			auto B = std::make_shared<FBlock>();
			if (auto R = B->Charge.Reserve(
					FMemoryAccess::State(Context)->Budget, static_cast<std::uint64_t>(Capacity), false);
				!R)
			{
				return R;
			}
			if (Capacity)
			{
				B->Data.reset(new std::uint8_t[static_cast<std::size_t>(Capacity)]);
			}
			B->Charge.Commit();
			Out = std::move(B);
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult CreateUninitializedBuffer(FMemoryContext& Context, std::int64_t Size, FValue& Out)
	{
		if (auto R = CheckContext(Context); !R)
		{
			return R;
		}
		try
		{
			auto O = std::make_shared<FOwner>();
			O->State = FMemoryAccess::State(Context);
			if (auto R = Allocate(Context, Size, O->Block); !R)
			{
				return R;
			}
			O->Size = Size;
			auto D = std::make_shared<FDescriptor>();
			D->Owner = std::move(O);
			D->Kind = EKind::Buffer;
			return MakeValue(std::move(D), Out);
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}
} // namespace VeroScript::Memory::Detail

namespace VeroScript::Memory
{
	using namespace Detail;
	bool NativeAddressesAvailable() noexcept
	{
		#if defined(__EMSCRIPTEN__) || defined(__wasm__)
		return false;
		#else
		return sizeof(std::uintptr_t) <= sizeof(std::uint64_t);
		#endif
	}

	bool IsTypeName(std::string_view Name) noexcept
	{
		return Name == Names[0] || Name == Names[1] || Name == Names[2] || Name == "std.memory.byte_order";
	}

	bool IsRegion(const FValue& Value) noexcept
	{
		try
		{
			return static_cast<bool>(FindDescriptor(Value));
		}
		catch (...)
		{
			return false;
		}
	}

	FMemoryResult CreateBuffer(
		FMemoryContext& Context, std::int64_t SizeValue, FValue& OutValue, FMemoryWork Work)
	{
		FValue ResultValue;
		if (auto R = CreateUninitializedBuffer(Context, SizeValue, ResultValue); !R)
		{
			return R;
		}
		auto D = FindDescriptor(ResultValue);
		for (std::size_t Offset = 0, N = static_cast<std::size_t>(SizeValue); Offset < N;)
		{
			const auto Chunk = std::min(ChunkSize, N - Offset);
			if (auto R = CheckWork(Work, Chunk); !R)
			{
				return R;
			}
			std::memset(D->Owner->Data() + Offset, 0, Chunk);
			Offset += Chunk;
		}
		OutValue = std::move(ResultValue);
		return {};
	}

	FMemoryResult CreateEmptyView(bool ReadOnly, FValue& OutValue)
	{
		try
		{
			auto D = std::make_shared<FDescriptor>();
			D->Kind = ReadOnly ? EKind::ReadOnlyView : EKind::View;
			return MakeValue(std::move(D), OutValue);
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult Size(FMemoryContext& C, const FValue& V, std::int64_t& Out)
	{
		FRegion R;
		if (auto S = Resolve(C, V, R); !S)
		{
			return S;
		}
		Out = R.Size;
		return {};
	}

	FMemoryResult Capacity(FMemoryContext& C, const FValue& V, std::int64_t& Out)
	{
		FRegion R;
		if (auto S = Resolve(C, V, R); !S)
		{
			return S;
		}
		if (R.Descriptor->Kind != EKind::Buffer)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		Out = static_cast<std::int64_t>(R.Descriptor->Owner->Block->Charge.Size);
		return {};
	}

	FMemoryResult DataAddress(FMemoryContext& C, const FValue& V, std::uint64_t& Out)
	{
		if (!NativeAddressesAvailable())
		{
			return Fail(EMemoryStatus::Unsupported, "diag.memory_addresses_unsupported");
		}
		FRegion R;
		if (auto S = Resolve(C, V, R); !S)
		{
			return S;
		}
		Out = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(R.Data));
		return {};
	}

	FMemoryResult Resize(
		FMemoryContext& C, const FValue& V, std::int64_t RequestedSize, bool ReserveOnly, FMemoryWork Work)
	{
		if (auto S = Count(RequestedSize); !S)
		{
			return S;
		}
		FRegion R;
		if (auto S = Resolve(C, V, R, true); !S)
		{
			return S;
		}
		if (R.Descriptor->Kind != EKind::Buffer)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		auto O = R.Descriptor->Owner;
		const auto OldSize = O->Size;
		const auto CapacityValue = O->Block->Charge.Size;
		if ((ReserveOnly && static_cast<std::uint64_t>(RequestedSize) <= CapacityValue) ||
			(!ReserveOnly && RequestedSize == OldSize))
		{
			return {};
		}
		if (O->Generation == std::numeric_limits<std::uint64_t>::max())
		{
			return Fail(EMemoryStatus::LimitExceeded, "diag.memory_generation_exhausted");
		}
		FGuard G;
		if (auto S = G.Acquire(R, true); !S)
		{
			return S;
		}
		std::shared_ptr<FBlock> ReplacementBlock;
		if (static_cast<std::uint64_t>(RequestedSize) > CapacityValue)
		{
			if (auto S = Allocate(C, RequestedSize, ReplacementBlock); !S)
			{
				return S;
			}
			for (std::size_t P = 0, N = static_cast<std::size_t>(OldSize); P < N;)
			{
				const auto K = std::min(ChunkSize, N - P);
				if (auto S = CheckWork(Work, K); !S)
				{
					return S;
				}
				std::memcpy(ReplacementBlock->Data.get() + P, O->Data() + P, K);
				P += K;
			}
		}
		if (!ReserveOnly && RequestedSize > OldSize)
		{
			auto* Data = ReplacementBlock ? ReplacementBlock->Data.get() : O->Data();
			for (std::size_t P = static_cast<std::size_t>(OldSize),
							 N = static_cast<std::size_t>(RequestedSize);
				P < N;)
			{
				const auto K = std::min(ChunkSize, N - P);
				if (auto S = CheckWork(Work, K); !S)
				{
					return S;
				}
				std::memset(Data + P, 0, K);
				P += K;
			}
		}
		// All visible changes happen after allocation, initialization and governance checks.
		if (ReplacementBlock)
		{
			O->Block.swap(ReplacementBlock);
		}
		if (!ReserveOnly)
		{
			O->Size = RequestedSize;
		}
		++O->Generation;
		{
			std::lock_guard<std::mutex> L(O->Mutex);
			if (O->Version != std::numeric_limits<std::uint64_t>::max())
			{
				++O->Version;
			}
		}
		return {};
	}

	FMemoryResult Slice(FMemoryContext& C, const FValue& V, std::int64_t Offset, std::int64_t Length,
		bool ReadOnly, FValue& Out)
	{
		FRegion R;
		if (auto S = Resolve(C, V, R, !ReadOnly); !S)
		{
			return S;
		}
		if (auto S = Bounds(R, Offset, Length); !S)
		{
			return S;
		}
		try
		{
			auto D = std::make_shared<FDescriptor>();
			D->Owner = R.Descriptor->Owner;
			D->Offset = R.Offset + Offset;
			D->Length = Length;
			D->Kind = ReadOnly ? EKind::ReadOnlyView : EKind::View;
			D->Generation = D->Owner ? D->Owner->Generation : 0;
			return MakeValue(std::move(D), Out);
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult MutationVersion(FMemoryContext& C, const FValue& V, std::uint64_t& Out)
	{
		FRegion R;
		if (auto S = Resolve(C, V, R); !S)
		{
			return S;
		}
		if (!R.Descriptor->Owner)
		{
			Out = 0;
			return {};
		}
		std::lock_guard<std::mutex> L(R.Descriptor->Owner->Mutex);
		Out = R.Descriptor->Owner->Version;
		return {};
	}
} // namespace VeroScript::Memory
