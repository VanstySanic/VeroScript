// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemoryInternal.h"

#include <limits>
#include <chrono>
#include <random>
#include <unordered_map>

namespace VeroScript::Memory
{
	using namespace Detail;
	namespace
	{
		struct FIdentity
		{
			std::weak_ptr<const void> Owner;
			std::uint64_t Id = 0;
		};
		struct FIdentities
		{
			std::mutex Mutex;
			std::map<const void*, FIdentity, std::less<const void*>> Entries;
			const void* Cursor = nullptr;
			std::uint64_t Next = 1;
		};
		FIdentities& Identities()
		{
			static FIdentities R;
			return R;
		}
		FMemoryResult IdentityOwner(const FValue& Value, std::shared_ptr<const void>& Out, bool HostClass)
		{
			bool Allowed = false;
			std::visit(
				[&](const auto& V)
				{
					using T = std::decay_t<decltype(V)>;
					if constexpr (std::is_same_v<T, FObjectHandle>)
					{
						if (!V || V->Kind == EScriptObjectKind::Class)
						{
							Out = V;
							Allowed = true;
						}
					}
					else if constexpr (std::is_same_v<T, FHostObjectHandle>)
					{
						if (!V)
						{
							Allowed = true;
						}
						else if (auto D = FindDescriptor(Value))
						{
							if (D->Kind == EKind::Buffer)
							{
								Out = V;
								Allowed = true;
							}
						}
						else if (HostClass && !IsTypeName(V->TypeName))
						{
							Out = V;
							Allowed = true;
						}
					}
					else if constexpr (std::is_same_v<T, FDynamicArrayHandle> ||
										 std::is_same_v<T, FMapHandle> || std::is_same_v<T, FSetHandle> ||
										 std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> ||
										 std::is_same_v<T, FOrderedMapHandle> ||
										 std::is_same_v<T, FDequeHandle> ||
										 std::is_same_v<T, FChannelHandle> ||
										 std::is_same_v<T, FFunctionHandle>)
					{
						Out = V;
						Allowed = true;
					}
				},
				Value);
			return Allowed ? FMemoryResult{} : Fail(EMemoryStatus::TypeMismatch, "diag.memory_object_type");
		}
		struct FLeaseRecord
		{
			std::shared_ptr<FOwner> Owner;
			bool Write = false;
		};
		struct FLeases
		{
			std::mutex Mutex;
			std::unordered_map<std::uint64_t, FLeaseRecord> Entries;
			std::uint64_t Next = 1;
		};
		FLeases& Leases()
		{
			static FLeases R;
			return R;
		}
		std::uint64_t RuntimeId()
		{
			// An opaque core-instance nonce; no native address is exported in a token.
			static const std::uint64_t Id = []
			{
				std::uint64_t Seed =
					static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
				try
				{
					std::random_device Random;
					Seed ^= (static_cast<std::uint64_t>(Random()) << 32) ^ Random();
				}
				catch (...)
				{
				}
				Seed ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&Leases()));
				Seed ^= Seed >> 30;
				Seed *= 0xbf58476d1ce4e5b9ULL;
				Seed ^= Seed >> 27;
				Seed *= 0x94d049bb133111ebULL;
				Seed ^= Seed >> 31;
				return Seed ? Seed : std::uint64_t{1};
			}();
			return Id;
		}
	} // namespace
	FMemoryResult Detail::ObjectIdentity(
		const FValue& Value, bool Address, bool HostClass, std::uint64_t& Out)
	{
		if (Address && !NativeAddressesAvailable())
		{
			return Fail(EMemoryStatus::Unsupported, "diag.memory_addresses_unsupported");
		}
		std::shared_ptr<const void> Owner;
		if (auto R = IdentityOwner(Value, Owner, HostClass); !R)
		{
			return R;
		}
		if (!Owner)
		{
			Out = 0;
			return {};
		}
		if (Address)
		{
			Out = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Owner.get()));
			return {};
		}
		try
		{
			auto& R = Identities();
			std::lock_guard<std::mutex> L(R.Mutex);
			auto It = R.Entries.upper_bound(R.Cursor);
			if (It == R.Entries.end())
			{
				It = R.Entries.begin();
			}
			for (unsigned I = 0; I < 8 && It != R.Entries.end(); ++I)
			{
				R.Cursor = It->first;
				if (It->second.Owner.expired())
				{
					It = R.Entries.erase(It);
				}
				else
				{
					++It;
				}
			}
			auto Existing = R.Entries.find(Owner.get());
			if (Existing != R.Entries.end() && !Existing->second.Owner.owner_before(Owner) &&
				!Owner.owner_before(Existing->second.Owner))
			{
				Out = Existing->second.Id;
				return {};
			}
			if (!R.Next)
			{
				return Fail(EMemoryStatus::LimitExceeded, "diag.memory_identity_exhausted");
			}
			const auto Id = R.Next;
			R.Entries[Owner.get()] = {Owner, Id};
			++R.Next;
			Out = Id;
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	namespace
	{
		bool IsHostClass(const FValue& V, const IHostBindingCatalog* Catalog)
		{
			const auto* H = std::get_if<FHostObjectHandle>(&V);
			const auto* T = H && *H && Catalog ? Catalog->FindType((*H)->TypeName) : nullptr;
			return T && T->Kind == EHostTypeKind::Class && T->Id.Value == (*H)->TypeId;
		}
	} // namespace
	FMemoryResult ObjectId(const FValue& V, std::uint64_t& Out, const IHostBindingCatalog* Catalog)
	{
		return Detail::ObjectIdentity(V, false, IsHostClass(V, Catalog), Out);
	}

	FMemoryResult ObjectAddress(const FValue& V, std::uint64_t& Out, const IHostBindingCatalog* Catalog)
	{
		return Detail::ObjectIdentity(V, true, IsHostClass(V, Catalog), Out);
	}

	FMemoryResult WrapExternalRegion(FMemoryContext& C, void* Data, std::uint64_t Length, bool ReadOnly,
		const std::shared_ptr<void>& Owner, FMemoryReleaseCallback Release, void* UserData, FValue& Out)
	{
		if (auto R = CheckContext(C); !R)
		{
			return R;
		}
		const auto Address = reinterpret_cast<std::uintptr_t>(Data);
		if ((!Data && Length) || !Owner ||
			Length > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
			Length > std::numeric_limits<std::size_t>::max() ||
			Length > std::numeric_limits<std::uintptr_t>::max() - Address)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_external_region");
		}
		try
		{
			auto O = std::make_shared<FOwner>();
			O->State = FMemoryAccess::State(C);
			if (auto R = O->ExternalCharge.Reserve(O->State->Budget, Length, true); !R)
			{
				return R;
			}
			O->External = true;
			O->ExternalData = static_cast<std::uint8_t*>(Data);
			O->Size = static_cast<std::int64_t>(Length);
			O->ExternalReadOnly = ReadOnly;
			auto D = std::make_shared<FDescriptor>();
			D->Owner = O;
			D->Length = O->Size;
			D->Generation = O->Generation;
			D->Kind = ReadOnly ? EKind::ReadOnlyView : EKind::View;
			FValue ResultValue;
			if (auto R = MakeValue(std::move(D), ResultValue); !R)
			{
				return R;
			}
			O->ExternalCharge.Commit();
			O->ExternalOwner = Owner;
			O->Release = Release;
			O->ReleaseUserData = UserData;
			O->Adopted = true;
			Out = std::move(ResultValue);
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult InvalidateExternalRegion(FMemoryContext& C, const FValue& Value)
	{
		if (auto R = CheckContext(C); !R)
		{
			return R;
		}
		auto D = FindDescriptor(Value);
		if (!D || !D->Owner || !D->Owner->External)
		{
			return Fail(EMemoryStatus::TypeMismatch, "diag.memory_type");
		}
		auto O = D->Owner;
		std::lock_guard<std::mutex> L(O->Mutex);
		if (O->State->Domain != FMemoryAccess::State(C)->Domain)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_domain");
		}
		if (!O->Valid)
		{
			return {};
		}
		if (O->Writer || O->Readers)
		{
			return Fail(EMemoryStatus::Busy, "diag.memory_busy");
		}
		O->Valid = false;
		return {};
	}

	FMemoryResult AcquireLease(FMemoryContext& C, const FValue& Value, bool Write, FMemoryLease& Out)
	{
		FRegion Region;
		if (auto R = Resolve(C, Value, Region, Write); !R)
		{
			return R;
		}
		FGuard Guard;
		if (auto R = Guard.Acquire(Region, Write); !R)
		{
			return R;
		}
		try
		{
			auto& R = Leases();
			std::lock_guard<std::mutex> L(R.Mutex);
			if (!R.Next)
			{
				return Fail(EMemoryStatus::LimitExceeded, "diag.memory_identity_exhausted");
			}
			const auto Serial = R.Next;
			R.Entries.emplace(Serial, FLeaseRecord{Guard.Owner, Write});
			++R.Next;
			Out = {Region.Data, Write ? Region.Data : nullptr, static_cast<std::uint64_t>(Region.Size),
				{RuntimeId(), Serial}};
			Guard.Owner.reset();
			return {};
		}
		catch (const std::bad_alloc&)
		{
			return Fail(EMemoryStatus::OutOfMemory, "diag.memory_out_of_memory");
		}
	}

	FMemoryResult ReleaseLease(FMemoryLeaseToken Token)
	{
		if (Token.Runtime != RuntimeId() || !Token.Serial)
		{
			return Fail(EMemoryStatus::InvalidArgument, "diag.memory_lease_token");
		}
		FLeaseRecord Record;
		{
			auto& R = Leases();
			std::lock_guard<std::mutex> L(R.Mutex);
			auto It = R.Entries.find(Token.Serial);
			if (It == R.Entries.end())
			{
				return Fail(EMemoryStatus::Expired, "diag.memory_lease_token");
			}
			Record = std::move(It->second);
			R.Entries.erase(It);
		}
		if (Record.Owner)
		{
			std::lock_guard<std::mutex> L(Record.Owner->Mutex);
			if (Record.Write)
			{
				if (Record.Owner->Version != std::numeric_limits<std::uint64_t>::max())
				{
					++Record.Owner->Version;
				}
				Record.Owner->Writer = false;
			}
			else
			{
				--Record.Owner->Readers;
			}
		}
		return {};
	}
} // namespace VeroScript::Memory

namespace VeroScript::Memory::Detail
{
	bool CloneDescriptor(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		auto D = C.ArgCount == 1 ? FindDescriptor(C.Args[0]) : nullptr;
		if (!D || D->Kind == EKind::Buffer)
		{
			Error = "diag.memory_type";
			return false;
		}
		auto R = MakeValue(std::make_shared<FDescriptor>(*D), Out);
		if (!R)
		{
			Error = R.MessageId;
		}
		return static_cast<bool>(R);
	}

	bool EqualDescriptor(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		auto A = C.ArgCount == 2 ? FindDescriptor(C.Args[0]) : nullptr,
			 B = C.ArgCount == 2 ? FindDescriptor(C.Args[1]) : nullptr;
		if (!A || !B)
		{
			Error = "diag.memory_type";
			return false;
		}
		Out = A->Kind == B->Kind && A->Owner == B->Owner && A->Offset == B->Offset &&
			  A->Length == B->Length && A->Generation == B->Generation;
		return true;
	}

	bool HashDescriptor(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		auto D = C.ArgCount == 1 ? FindDescriptor(C.Args[0]) : nullptr;
		if (!D)
		{
			Error = "diag.memory_type";
			return false;
		}
		std::uint64_t H = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(D->Owner.get()));
		for (const auto X : {static_cast<std::uint64_t>(D->Kind), static_cast<std::uint64_t>(D->Offset),
				 static_cast<std::uint64_t>(D->Length), D->Generation})
		{
			H ^= X + 0x9e3779b97f4a7c15ull + (H << 6) + (H >> 2);
		}
		Out = H;
		return true;
	}
} // namespace VeroScript::Memory::Detail
