// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptMemory.h"
#include "VeroScriptLocale.h"
#include <atomic>
#include <limits>

using namespace VeroScript;
using namespace VeroScript::Interop;
namespace
{
	vs_interop_status Status(FMemoryResult R)
	{
		switch (R.Status)
		{
		case EMemoryStatus::Ok:
			return VS_INTEROP_STATUS_OK;
		case EMemoryStatus::InvalidArgument:
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		case EMemoryStatus::OutOfMemory:
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		case EMemoryStatus::WrongThread:
			return VS_INTEROP_STATUS_WRONG_THREAD;
		case EMemoryStatus::Unsupported:
			return VS_INTEROP_STATUS_UNSUPPORTED;
		case EMemoryStatus::TypeMismatch:
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		case EMemoryStatus::PermissionDenied:
			return VS_INTEROP_STATUS_PERMISSION_DENIED;
		case EMemoryStatus::Expired:
			return VS_INTEROP_STATUS_EXPIRED;
		case EMemoryStatus::ReadOnly:
			return VS_INTEROP_STATUS_READ_ONLY;
		case EMemoryStatus::Busy:
			return VS_INTEROP_STATUS_BUSY;
		case EMemoryStatus::LimitExceeded:
			return VS_INTEROP_STATUS_LIMIT_EXCEEDED;
		case EMemoryStatus::Interrupted:
			return VS_INTEROP_STATUS_ENGINE_ERROR;
		default:
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}
	struct FCall
	{
		FMemoryContext& Context;
		FMemoryWork Work;
		bool Addresses;
		const IHostBindingCatalog* Bindings;
	};
	template <class F>
	vs_interop_status Invoke(
	    vs_interop_session* Session, const vs_interop_call_context* Call, F&& Fn) noexcept
	{
		std::shared_ptr<FSessionState> Pin;
		try
		{
			if ((Session != nullptr) == (Call != nullptr))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (Call)
			{
				const FScopedValueSession ValueSession(Call->Owner);
				if (!Call->Native || !Call->Native->MemoryServices)
				{
					return VS_INTEROP_STATUS_UNSUPPORTED;
				}
				auto& S = *Call->Native->MemoryServices;
				return Status(Fn(FCall{S.GetMemoryContext(), S.GetMemoryWork(), S.MemoryAddressesAllowed(),
				    S.MemoryHostBindings()}));
			}
			Pin = Session->State;
			if (!Pin)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (auto R = RequireOwnerThread(*Pin); R != VS_INTEROP_STATUS_OK)
			{
				return R;
			}
			if (auto R = RequireIdleSession(*Pin, "memory operation"); R != VS_INTEROP_STATUS_OK)
			{
				// The shared guard uses ENGINE_ERROR for an active session. Preserve
				// failures in the guard itself instead of reporting them as contention.
				return R == VS_INTEROP_STATUS_ENGINE_ERROR ? VS_INTEROP_STATUS_BUSY : R;
			}
			FScopedSessionActivity Activity(*Pin);
			const auto R = Fn(FCall{Pin->Session.GetMemoryContext(), {},
			    Pin->Session.GetCapabilities().bAllowMemoryAddress, &Pin->Session.GetBindingCatalog()});
			if (!R && R.MessageId)
			{
				SetError(*Pin, Locale::FormatEnglish(R.MessageId));
			}
			return Status(R);
		}
		catch (...)
		{
			return TranslateException(Pin.get());
		}
	}
	FMemoryResult Invalid()
	{
		return {EMemoryStatus::InvalidArgument, "diag.memory_arguments"};
	}
	FMemoryResult Store(FMemoryResult R, FValue Value, vs_interop_value* Out)
	{
		if (R)
		{
			AssignValue(*Out, std::move(Value));
		}
		return R;
	}
	bool Writable(vs_interop_value* Out)
	{
		return Out && IsWritableValue(Out);
	}
	// Core adopts its region before the C output is published. Keep the foreign
	// owner's release callback conditional until publication has also succeeded.
	struct FExternalMemoryAdoption
	{
		FExternalMemoryAdoption(void* InOwner, vs_interop_memory_release_callback InRelease)
		    : Owner(InOwner), Release(InRelease)
		{
		}
		void Commit() noexcept
		{
			if (State.fetch_or(Committed, std::memory_order_acq_rel) & Released)
			{
				ReleaseOwner();
			}
		}
		static void OnRelease(void* UserData) noexcept
		{
			auto& Adoption = *static_cast<FExternalMemoryAdoption*>(UserData);
			if (Adoption.State.fetch_or(Released, std::memory_order_acq_rel) & Committed)
			{
				Adoption.ReleaseOwner();
			}
		}

	private:
		void ReleaseOwner() noexcept
		{
			try
			{
				Release(Owner);
			}
			catch (...)
			{
			}
		}
		static constexpr unsigned Committed = 1u;
		static constexpr unsigned Released = 2u;
		std::atomic<unsigned> State{0};
		void* Owner;
		vs_interop_memory_release_callback Release;
	};
	std::string_view Type(uint32_t T)
	{
		switch (T)
		{
		case VS_INTEROP_NATIVE_INT32:
			return "int";
		case VS_INTEROP_NATIVE_UINT32:
			return "uint32";
		case VS_INTEROP_NATIVE_INT64:
			return "int64";
		case VS_INTEROP_NATIVE_UINT64:
			return "uint64";
		case VS_INTEROP_NATIVE_INT8:
			return "int8";
		case VS_INTEROP_NATIVE_UINT8:
			return "uint8";
		case VS_INTEROP_NATIVE_INT16:
			return "int16";
		case VS_INTEROP_NATIVE_UINT16:
			return "uint16";
		case VS_INTEROP_NATIVE_FLOAT:
			return "float";
		case VS_INTEROP_NATIVE_DOUBLE:
			return "double";
		default:
			return {};
		}
	}
} // namespace
extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_stdlib_limits(
	    vs_interop_session* S, const vs_interop_stdlib_limits* L)
	{
		if (!L || L->struct_size < sizeof(*L) || L->reserved) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		const FStandardLibraryLimits Limits{L->max_text_bytes, L->max_read_bytes, L->max_split_parts};
		return Invoke(S, nullptr, [&](FCall)
		{
			return S->State->Session.SetStandardLibraryLimits(Limits);
		});
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_session_set_memory_limits(
	    vs_interop_session* S, const vs_interop_memory_limits* L)
	{
		if (!L || L->struct_size < sizeof(*L) || L->reserved)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return Invoke(S, nullptr,
		    [&](FCall C)
		{
			return C.Context.SetLimits({L->max_buffer_bytes, L->max_retained_bytes});
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_get_stats(
	    vs_interop_session* S, const vs_interop_call_context* C, vs_interop_memory_stats* Out)
	{
		if (!Out || Out->struct_size < sizeof(*Out))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto T = X.Context.GetStats();
			Out->reserved = 0;
			Out->owned_bytes = T.OwnedBytes;
			Out->borrowed_bytes = T.BorrowedBytes;
			Out->peak_owned_bytes = T.PeakOwnedBytes;
			return FMemoryResult{};
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_create_buffer(
	    vs_interop_session* S, const vs_interop_call_context* C, int64_t N, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			if (!Writable(Out))
			{
				return Invalid();
			}
			FValue V;
			auto R = Memory::CreateBuffer(X.Context, N, V, X.Work);
			return Store(R, std::move(V), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_create_view(
	    vs_interop_session* S, const vs_interop_call_context* C, uint8_t RO, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall)
		{
			if (!Writable(Out) || RO > 1)
			{
				return Invalid();
			}
			FValue V;
			auto R = Memory::CreateEmptyView(RO != 0, V);
			return Store(R, std::move(V), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_size(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* V, int64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(V);
			return P && Out ? Memory::Size(X.Context, *P, *Out) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_capacity(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* V, int64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(V);
			return P && Out ? Memory::Capacity(X.Context, *P, *Out) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_resize(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* V, int64_t N, uint8_t Reserve)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(V);
			return P && Reserve <= 1 ? Memory::Resize(X.Context, *P, N, Reserve != 0, X.Work) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_slice(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* V, int64_t O, int64_t N, uint8_t RO,
	    vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(V);
			if (!P || !Writable(Out) || RO > 1)
			{
				return Invalid();
			}
			FValue B;
			auto R = Memory::Slice(X.Context, *P, O, N, RO != 0, B);
			return Store(R, std::move(B), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_copy(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* D, int64_t DO, const vs_interop_value* V,
	    int64_t SO, int64_t N)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(D), Q = ReadValue(V);
			return P && Q ? Memory::Copy(X.Context, *P, DO, *Q, SO, N, X.Work) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_fill(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* D, int64_t O, int64_t N, uint8_t B)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(D);
			return P ? Memory::Fill(X.Context, *P, O, N, B, X.Work) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_equal(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, int64_t AO, const vs_interop_value* B,
	    int64_t BO, int64_t N, uint8_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A), Q = ReadValue(B);
			if (!P || !Q || !Out)
			{
				return Invalid();
			}
			bool E = false;
			auto R = Memory::Equal(X.Context, *P, AO, *Q, BO, N, E, X.Work);
			if (R)
			{
				*Out = E;
			}
			return R;
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_clone(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			if (!P || !Writable(Out))
			{
				return Invalid();
			}
			FValue B;
			auto R = Memory::Clone(X.Context, *P, B, X.Work);
			return Store(R, std::move(B), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_from_array(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			auto Q = P ? std::get_if<FDynamicArrayHandle>(P) : nullptr;
			if (!P || !Writable(Out))
			{
				return Invalid();
			}
			if (!Q)
			{
				return FMemoryResult{EMemoryStatus::TypeMismatch, "diag.memory_byte_array"};
			}
			FValue B;
			auto R = Memory::FromArray(X.Context, *Q, B, X.Work);
			return Store(R, std::move(B), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_to_array(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			if (!P || !Writable(Out))
			{
				return Invalid();
			}
			FDynamicArrayHandle B;
			auto R = Memory::ToArray(X.Context, *P, B, X.Work);
			return Store(R, FValue{std::move(B)}, Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_read(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, int64_t O, uint32_t T, uint32_t Order,
	    vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			if (!P || !Writable(Out) || Order > 1 || Type(T).empty())
			{
				return Invalid();
			}
			FValue B;
			auto R =
			    Memory::Read(X.Context, *P, O, Type(T), static_cast<EMemoryByteOrder>(Order), B, X.Work);
			return Store(R, std::move(B), Out);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_write(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, int64_t O, uint32_t T, uint32_t Order,
	    const vs_interop_value* B)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A), Q = ReadValue(B);
			if (!P || !Q || Order > 1 || Type(T).empty())
			{
				return Invalid();
			}
			return Memory::Write(
			    X.Context, *P, O, Type(T), static_cast<EMemoryByteOrder>(Order), *Q, X.Work);
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_native_addresses_available(
	    vs_interop_session* S, const vs_interop_call_context* C, uint8_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			if (!Out)
			{
				return Invalid();
			}
			*Out = X.Addresses && Memory::NativeAddressesAvailable();
			return FMemoryResult{};
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_object_id(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* A, uint64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			return P && Out ? Memory::ObjectId(*P, *Out, X.Bindings) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_object_address(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* A, uint64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			if (!X.Addresses)
			{
				return FMemoryResult{EMemoryStatus::PermissionDenied, "diag.memory_permission"};
			}
			auto P = ReadValue(A);
			return P && Out ? Memory::ObjectAddress(*P, *Out, X.Bindings) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_data_address(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* A, uint64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			if (!X.Addresses)
			{
				return FMemoryResult{EMemoryStatus::PermissionDenied, "diag.memory_permission"};
			}
			auto P = ReadValue(A);
			return P && Out ? Memory::DataAddress(X.Context, *P, *Out) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_call_argument_address(
	    const vs_interop_call_context* C, uint64_t I, uint64_t* Out)
	{
		try
		{
			if (!C || !C->Native || !C->Native->MemoryServices || !Out ||
			    I > std::numeric_limits<std::size_t>::max())
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			return Status(
			    C->Native->MemoryServices->MemoryArgumentAddress(static_cast<std::size_t>(I), *Out));
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_wrap_external(vs_interop_session* S,
	    const vs_interop_call_context* C, void* D, uint64_t N, uint8_t RO, void* Owner,
	    vs_interop_memory_release_callback Release, vs_interop_value* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			if (!Owner || !Release || !Writable(Out) || RO > 1)
			{
				return Invalid();
			}
			if (Out->Root.IsRegistered())
			{
				(void)ReadValue(Out);
			}
			auto Adoption = std::make_shared<FExternalMemoryAdoption>(Owner, Release);
			FValue B;
			auto R = Memory::WrapExternalRegion(X.Context, D, N, RO != 0, Adoption,
			    FExternalMemoryAdoption::OnRelease, Adoption.get(), B);
			R = Store(R, std::move(B), Out);
			if (R)
			{
				Adoption->Commit();
			}
			return R;
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_invalidate_external(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* A)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			return P ? Memory::InvalidateExternalRegion(X.Context, *P) : Invalid();
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_acquire_lease(vs_interop_session* S,
	    const vs_interop_call_context* C, const vs_interop_value* A, uint8_t Write,
	    vs_interop_memory_lease* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			if (!P || !Out || Out->struct_size < sizeof(*Out) || Write > 1)
			{
				return Invalid();
			}
			FMemoryLease L;
			auto R = Memory::AcquireLease(X.Context, *P, Write != 0, L);
			if (R)
			{
				Out->reserved = 0;
				Out->data = L.Data;
				Out->writable_data = L.WritableData;
				Out->size = L.Size;
				Out->token = {L.Token.Runtime, L.Token.Serial};
			}
			return R;
		});
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_release_lease(vs_interop_memory_lease_token T)
	{
		try
		{
			return Status(Memory::ReleaseLease({T.runtime, T.serial}));
		}
		catch (...)
		{
			return TranslateException(nullptr);
		}
	}
	vs_interop_status VS_INTEROP_CALL vs_interop_memory_mutation_version(
	    vs_interop_session* S, const vs_interop_call_context* C, const vs_interop_value* A, uint64_t* Out)
	{
		return Invoke(S, C,
		    [&](FCall X)
		{
			auto P = ReadValue(A);
			return P && Out ? Memory::MutationVersion(X.Context, *P, *Out) : Invalid();
		});
	}
}
