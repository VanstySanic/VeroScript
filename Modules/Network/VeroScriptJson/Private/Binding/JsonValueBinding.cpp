// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"

namespace VeroScript::Json::Binding
{
	enum class EQuery
	{
		Get,
		At,
		Set,
		Push,
		Replace,
		Insert,
		Erase,
		Remove,
		Size,
		UInt64,
		Equals,
		Contains,
		Keys
	};

	template <EQuery Query>
	bool TryValue(const FNativeCallContext& Original, VeroScript::FValue& Out, std::string& Error)
	{
		auto C = Original;
		++C.Args;
		if (C.MutableArgs)
		{
			++C.MutableArgs;
		}
		--C.ArgCount;
		auto& S = *static_cast<FState*>(C.UserData);
		auto& Value = *static_cast<FJsonValue*>(C.HostThis);
		constexpr std::size_t ErrorIndex =
			Query == EQuery::Get ? 3
			: (Query == EQuery::Set || Query == EQuery::Replace || Query == EQuery::Insert || Query == EQuery::Equals ||
				  Query == EQuery::At || Query == EQuery::Contains)
				? 2
				: 1;
		if (!C.MutableArg(ErrorIndex))
		{
			Error = "JSON try API requires ref error";
			return false;
		}
		const auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
		if (Work.Check && !Work.Check(Work.UserData, 1))
		{
			Error = "JSON operation interrupted";
			return false;
		}
		FError Failure;
		FJsonValue Result;
		bool Success = false, Found = false, Boolean = false;
		std::int64_t Size = 0;
		std::uint64_t Unsigned = 0;
		if constexpr (Query == EQuery::Get)
		{
			Success = Value.TryGet(std::get<std::string>(C.Args[0]), Result, Failure);
			Found = Success;
			if (Failure.Code == EError::Missing)
			{
				Success = true;
				Failure = {};
			}
		}
		else if constexpr (Query == EQuery::At)
		{
			Success = Value.TryAt(std::get<std::int64_t>(C.Args[0]), Result, Failure);
		}
		else if constexpr (Query == EQuery::Set)
		{
			Success = Value.TrySet(std::get<std::string>(C.Args[0]), Object<FJsonValue>(C, 1), Failure);
		}
		else if constexpr (Query == EQuery::Push)
		{
			Success = Value.TryPush(Object<FJsonValue>(C, 0), Failure);
		}
		else if constexpr (Query == EQuery::Replace)
		{
			Success = Value.TryReplace(std::get<std::int64_t>(C.Args[0]), Object<FJsonValue>(C, 1), Failure);
		}
		else if constexpr (Query == EQuery::Insert)
		{
			Success = Value.TryInsert(std::get<std::int64_t>(C.Args[0]), Object<FJsonValue>(C, 1), Failure);
		}
		else if constexpr (Query == EQuery::Erase)
		{
			Success = Value.TryErase(std::get<std::string>(C.Args[0]), Failure);
		}
		else if constexpr (Query == EQuery::Remove)
		{
			Success = Value.TryErase(std::get<std::int64_t>(C.Args[0]), Failure);
		}
		else if constexpr (Query == EQuery::Size)
		{
			Success = Value.TrySize(Size, Failure);
		}
		else if constexpr (Query == EQuery::UInt64)
		{
			Success = Value.TryUInt64(Unsigned, Failure);
		}
		else if constexpr (Query == EQuery::Contains)
		{
			Success = Value.TryContains(std::get<std::string>(C.Args[0]), Boolean, Failure);
		}
		else if constexpr (Query == EQuery::Keys)
		{
			std::vector<std::string> Keys;
			Success = Value.TryKeys(Keys, Failure);
			if (Success)
			{
				FBuilder Builder(EKind::Array);
				for (const auto& Key : Keys)
				{
					if (Work.Check && !Work.Check(Work.UserData, Key.size() + 1))
					{
						Error = "JSON keys operation interrupted";
						return false;
					}
					FJsonValue Item;
					if (!FJsonValue::TryString(Key, Item, Failure) || !Builder.TryPush(Item, Failure))
					{
						Success = false;
						break;
					}
				}
				if (Success)
				{
					Success = Builder.TryFinish(Result, Failure);
				}
			}
		}
		else if constexpr (Query == EQuery::Equals)
		{
			Success = Value.TryEquals(Object<FJsonValue>(C, 0), Boolean, Failure, {}, FWork{Work.Check, Work.UserData});
		}
		if (Failure.Code == EError::Cancelled)
		{
			Error = Failure.Message;
			Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
			return false;
		}
		VeroScript::FValue Diagnostic, Wrapped;
		if (!Wrap(S, std::move(Failure), Diagnostic, Error))
		{
			return false;
		}
		if constexpr (Query == EQuery::Get || Query == EQuery::At || Query == EQuery::Keys)
		{
			constexpr std::size_t Index = Query == EQuery::Get ? 2 : Query == EQuery::Keys ? 0 : 1;
			if (!C.MutableArg(Index) || (Query == EQuery::Get && !C.MutableArg(1)))
			{
				Error = "JSON try API requires ref outputs";
				return false;
			}
			if (Success && (Query != EQuery::Get || Found) && !Wrap(S, std::move(Result), Wrapped, Error))
			{
				return false;
			}
			if (Success)
			{
				if constexpr (Query == EQuery::Get)
				{
					C.AssignValue(*C.MutableArg(1), Found);
				}
				if (Query != EQuery::Get || Found)
				{
					C.AssignValue(*C.MutableArg(Index), std::move(Wrapped));
				}
			}
		}
		if constexpr (Query == EQuery::Size || Query == EQuery::UInt64 || Query == EQuery::Equals ||
					  Query == EQuery::Contains)
		{
			constexpr std::size_t Index = Query == EQuery::Equals || Query == EQuery::Contains ? 1 : 0;
			if (!C.MutableArg(Index))
			{
				Error = "JSON try API requires ref output";
				return false;
			}
			if (Success)
			{
				if constexpr (Query == EQuery::Size)
				{
					C.AssignValue(*C.MutableArg(Index), Size);
				}
				else if constexpr (Query == EQuery::UInt64)
				{
					C.AssignValue(*C.MutableArg(Index), Unsigned);
				}
				else
				{
					C.AssignValue(*C.MutableArg(Index), Boolean);
				}
			}
		}
		C.AssignValue(*C.MutableArg(ErrorIndex), std::move(Diagnostic));
		Out = Success;
		return true;
	}

	template <EQuery Query>
	bool AddQuery(FState& S, FHostTypeId Id, const char* Name, std::vector<ENativeType> Types,
		std::vector<std::string> Names, std::vector<std::uint8_t> Flags, std::string& Error)
	{
		FHostMemberDesc M;
		M.Name = Name;
		M.ReturnType = ENativeType::Bool;
		M.ParamTypes = std::move(Types);
		M.ParamHostTypeNames = std::move(Names);
		M.ParamFlags = std::move(Flags);
		M.Callback = TryValue<Query>;
		M.UserData = &S;
		M.bMutableThis = Query == EQuery::Set || Query == EQuery::Push || Query == EQuery::Replace ||
						 Query == EQuery::Insert || Query == EQuery::Erase || Query == EQuery::Remove;
		M.bConstMethod = !M.bMutableThis;
		return S.Host->RegisterMember(Id, std::move(M), Error);
	}

	bool RegisterValueQueries(FState& S, FHostTypeId Id, std::string& Error)
	{
		using T = ENativeType;
		constexpr auto R = NativeParamFlagRef;
		return AddQuery<EQuery::Contains>(S,
				   Id,
				   "try_contains",
				   {T::String, T::Bool, T::HostObject},
				   {"", "", "json::error"},
				   {0, R, R},
				   Error) &&
			   AddQuery<EQuery::Keys>(
				   S, Id, "try_keys", {T::HostObject, T::HostObject}, {"json::value", "json::error"}, {R, R}, Error) &&
			   AddQuery<EQuery::Get>(S,
				   Id,
				   "try_get",
				   {T::String, T::Bool, T::HostObject, T::HostObject},
				   {"", "", "json::value", "json::error"},
				   {0, R, R, R},
				   Error) &&
			   AddQuery<EQuery::At>(S,
				   Id,
				   "try_at",
				   {T::Int64, T::HostObject, T::HostObject},
				   {"", "json::value", "json::error"},
				   {0, R, R},
				   Error) &&
			   AddQuery<EQuery::Set>(S,
				   Id,
				   "try_set",
				   {T::String, T::HostObject, T::HostObject},
				   {"", "json::value", "json::error"},
				   {0, 0, R},
				   Error) &&
			   AddQuery<EQuery::Push>(
				   S, Id, "try_push", {T::HostObject, T::HostObject}, {"json::value", "json::error"}, {0, R}, Error) &&
			   AddQuery<EQuery::Replace>(S,
				   Id,
				   "try_replace",
				   {T::Int64, T::HostObject, T::HostObject},
				   {"", "json::value", "json::error"},
				   {0, 0, R},
				   Error) &&
			   AddQuery<EQuery::Insert>(S,
				   Id,
				   "try_insert",
				   {T::Int64, T::HostObject, T::HostObject},
				   {"", "json::value", "json::error"},
				   {0, 0, R},
				   Error) &&
			   AddQuery<EQuery::Erase>(
				   S, Id, "try_erase", {T::String, T::HostObject}, {"", "json::error"}, {0, R}, Error) &&
			   AddQuery<EQuery::Remove>(
				   S, Id, "try_remove_at", {T::Int64, T::HostObject}, {"", "json::error"}, {0, R}, Error) &&
			   AddQuery<EQuery::Size>(
				   S, Id, "try_size", {T::Int64, T::HostObject}, {"", "json::error"}, {R, R}, Error) &&
			   AddQuery<EQuery::UInt64>(
				   S, Id, "try_as_uint64", {T::UInt64, T::HostObject}, {"", "json::error"}, {R, R}, Error) &&
			   AddQuery<EQuery::Equals>(S,
				   Id,
				   "try_equals",
				   {T::HostObject, T::Bool, T::HostObject},
				   {"json::value", "", "json::error"},
				   {0, R, R},
				   Error);
	}
} // namespace VeroScript::Json::Binding
