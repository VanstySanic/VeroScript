// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"
#include <limits>
#include <type_traits>

namespace VeroScript::Json::Binding
{
	enum class ETryAction
	{
		Write,
		Resolve,
		Patch,
		MergePatch,
		Number,
		Int64,
		Double,
		DoubleExact,
		Boolean,
		String,
		CompilePointer,
		PointerResolve,
		DocumentGet,
		UInt64,
		FromString,
		FromDouble
	};

	template <class T, class V, V T::* Member, bool bSet>
	bool OptionProperty(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		auto& Value = static_cast<T*>(C.HostThis)->*Member;
		if constexpr (bSet)
		{
			if constexpr (std::is_same_v<V, bool>)
			{
				Value = std::get<bool>(C.Args[1]);
			}
			else
			{
				const auto Number = std::get<std::int64_t>(C.Args[1]);
				const auto Maximum = std::is_same_v<V, EDuplicateKeys>	? 3ull
									 : std::is_same_v<V, std::uint32_t> ? 16ull
																		: static_cast<std::uint64_t>(INT64_MAX);
				if (Number < 0 || static_cast<std::uint64_t>(Number) > Maximum)
				{
					Error = "JSON option is out of range";
					return false;
				}
				Value = static_cast<V>(Number);
			}
		}
		else if constexpr (std::is_same_v<V, bool>)
		{
			Out = Value;
		}
		else
		{
			if constexpr (std::is_unsigned_v<V>)
			{
				if (static_cast<std::uint64_t>(Value) > INT64_MAX)
				{
					Error = "JSON option exceeds the script int64 range";
					return false;
				}
			}
			Out = static_cast<std::int64_t>(Value);
		}
		return true;
	}

	template <class T, class V, V T::* Member>
	bool RegisterProperty(FState& S, FHostTypeId Id, const char* Name, std::string& Error)
	{
		constexpr auto Native = std::is_same_v<V, bool> ? ENativeType::Bool : ENativeType::Int64;
		const auto Setter = std::string("set_") + Name;
		return Method<OptionProperty<T, V, Member, false>>(S, Id, Name, Native, {}, Error) &&
			   Method<OptionProperty<T, V, Member, true>>(
				   S, Id, Setter.c_str(), ENativeType::Void, {Native}, Error, true);
	}

	template <class T, bool bSet>
	bool OptionLimits(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		auto& Value = static_cast<T*>(C.HostThis)->Limits;
		if constexpr (bSet)
		{
			Value = Object<FLimits>(C, 1);
			return true;
		}
		else
		{
			return Wrap(*static_cast<FState*>(C.UserData), Value, Out, Error);
		}
	}

	template <class T>
	bool RegisterLimits(FState& S, FHostTypeId Id, std::string& Error)
	{
		return Method<OptionLimits<T, false>>(
				   S, Id, "limits", ENativeType::HostObject, {}, Error, false, "json::limits") &&
			   Method<OptionLimits<T, true>>(S,
				   Id,
				   "set_limits",
				   ENativeType::Void,
				   {ENativeType::HostObject},
				   Error,
				   true,
				   "",
				   {"json::limits"});
	}

	template <class T>
	bool CommitTry(FState& S, const FNativeCallContext& C, std::size_t Index, bool Success, T Value, FError Failure,
		VeroScript::FValue& Out, std::string& Error)
	{
		if (Failure.Code == EError::Cancelled)
		{
			Error = Failure.Message;
			Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
			return false;
		}
		VeroScript::FValue Result, ErrorValue;
		if (!Wrap(S, std::move(Failure), ErrorValue, Error))
		{
			return false;
		}
		if (Success)
		{
			if constexpr (std::is_same_v<T, FJsonValue> || std::is_same_v<T, FDocument> || std::is_same_v<T, FPointer>)
			{
				if (!Wrap(S, std::move(Value), Result, Error))
				{
					return false;
				}
			}
			else
			{
				Result = std::move(Value);
			}
			C.AssignValue(*C.MutableArg(Index), std::move(Result));
		}
		C.AssignValue(*C.MutableArg(Index + 1), std::move(ErrorValue));
		Out = Success;
		return true;
	}

	template <ETryAction Action>
	bool TryCall(const FNativeCallContext& Original, VeroScript::FValue& Out, std::string& Error)
	{
		auto C = Original;
		if (C.HostThis)
		{
			++C.Args;
			if (C.MutableArgs)
			{
				++C.MutableArgs;
			}
			--C.ArgCount;
		}

		constexpr bool bTwoInputs = Action == ETryAction::Resolve || Action == ETryAction::Patch ||
									Action == ETryAction::MergePatch || Action == ETryAction::CompilePointer;
		constexpr std::size_t Index = Action == ETryAction::DocumentGet ? 0 : bTwoInputs ? 2 : 1;
		if (!C.MutableArg(Index) || !C.MutableArg(Index + 1))
		{
			Error = "JSON try operation requires ref outputs";
			return false;
		}
		auto& S = *static_cast<FState*>(C.UserData);
		const auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
		const FWork W{Work.Check, Work.UserData};
		FError E;

		if constexpr (Action == ETryAction::CompilePointer)
		{
			FPointer Result;
			const bool Success = FPointer::TryCompile(std::get<std::string>(C.Args[0]),
				Result,
				E,
				std::get<bool>(C.Args[1]),
				C.ArgCount > 4 ? Object<FLimits>(C, 4) : FLimits{},
				W);
			return CommitTry(S, C, Index, Success, std::move(Result), std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::PointerResolve || Action == ETryAction::DocumentGet ||
						   Action == ETryAction::FromString || Action == ETryAction::FromDouble)
		{
			FJsonValue Result;
			bool Success;
			if constexpr (Action == ETryAction::PointerResolve)
			{
				Success =
					static_cast<FPointer*>(C.HostThis)
						->TryResolve(
							Object<FDocument>(C, 0), Result, E, C.ArgCount > 3 ? Object<FLimits>(C, 3) : FLimits{}, W);
			}
			else if constexpr (Action == ETryAction::FromString)
			{
				Success = FJsonValue::TryString(std::get<std::string>(C.Args[0]), Result, E);
			}
			else if constexpr (Action == ETryAction::FromDouble)
			{
				Success = FJsonValue::TryDouble(std::get<double>(C.Args[0]), Result, E);
			}
			else
			{
				const auto& Document = *static_cast<FDocument*>(C.HostThis);
				Success = Document.bHasValue;
				if (Success)
				{
					Result = Document.Value;
				}
				else
				{
					E.Code = EError::Missing;
					E.Message = "JSON document has no root value";
				}
			}
			return CommitTry(S, C, Index, Success, std::move(Result), std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::UInt64)
		{
			std::uint64_t Result = 0;
			const bool Success = Object<FJsonValue>(C, 0).TryUInt64(Result, E);
			return CommitTry(S, C, Index, Success, Result, std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::Write || Action == ETryAction::String)
		{
			std::string Text;
			bool Success;
			if constexpr (Action == ETryAction::Write)
			{
				Success = TryWrite(Object<FJsonValue>(C, 0),
					Text,
					E,
					C.ArgCount > 3 ? Object<FWriteOptions>(C, 3) : FWriteOptions{},
					W);
			}
			else
			{
				Success = Object<FJsonValue>(C, 0).TryString(Text, E);
			}
			return CommitTry(S, C, Index, Success, std::move(Text), std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::Patch)
		{
			FDocument Result;
			const bool Success = TryPatch(Object<FDocument>(C, 0), Object<FJsonValue>(C, 1), Result, E, {}, W);
			return CommitTry(S, C, Index, Success, std::move(Result), std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::Int64)
		{
			std::int64_t Result = 0;
			const bool Success = Object<FJsonValue>(C, 0).TryInt64(Result, E);
			return CommitTry(S, C, Index, Success, Result, std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::Double || Action == ETryAction::DoubleExact)
		{
			double Result = 0;
			const bool Success = Object<FJsonValue>(C, 0).TryDouble(Result, E, Action == ETryAction::DoubleExact);
			return CommitTry(S, C, Index, Success, Result, std::move(E), Out, Error);
		}
		else if constexpr (Action == ETryAction::Boolean)
		{
			bool Result = false;
			const bool Success = Object<FJsonValue>(C, 0).TryBoolean(Result, E);
			return CommitTry(S, C, Index, Success, Result, std::move(E), Out, Error);
		}
		else
		{
			FJsonValue Result;
			bool Success;
			if constexpr (Action == ETryAction::Resolve)
			{
				Success = TryResolve(Object<FDocument>(C, 0), std::get<std::string>(C.Args[1]), Result, E, {}, W);
			}
			else if constexpr (Action == ETryAction::MergePatch)
			{
				Success = TryMergePatch(Object<FJsonValue>(C, 0), Object<FJsonValue>(C, 1), Result, E, {}, W);
			}
			else
			{
				Success = FJsonValue::TryNumber(std::get<std::string>(C.Args[0]), Result, E);
			}
			return CommitTry(S, C, Index, Success, std::move(Result), std::move(E), Out, Error);
		}
	}

	template <ETryAction Action>
	bool RegisterTry(FState& S, const char* Name, ENativeType ResultType, const char* ResultName, std::string& Error,
		const char* OptionName = nullptr)
	{
		using T = ENativeType;
		constexpr bool bTwoInputs = Action == ETryAction::Resolve || Action == ETryAction::Patch ||
									Action == ETryAction::MergePatch || Action == ETryAction::CompilePointer;
		constexpr bool StringInput =
			Action == ETryAction::Number || Action == ETryAction::CompilePointer || Action == ETryAction::FromString;
		std::vector<T> Params{StringInput ? T::String : Action == ETryAction::FromDouble ? T::Double : T::HostObject};
		std::vector<std::string> Names{StringInput || Action == ETryAction::FromDouble				  ? ""
									   : Action == ETryAction::Resolve || Action == ETryAction::Patch ? "json::document"
																									  : "json::value"};
		if constexpr (bTwoInputs)
		{
			Params.push_back(Action == ETryAction::CompilePointer ? T::Bool
							 : Action == ETryAction::Resolve	  ? T::String
																  : T::HostObject);
			Names.push_back(Action == ETryAction::Resolve || Action == ETryAction::CompilePointer ? "" : "json::value");
		}
		std::vector<std::uint8_t> Flags(Params.size(), 0);
		Params.insert(Params.end(), {ResultType, T::HostObject});
		Names.insert(Names.end(), {ResultName, "json::error"});
		Flags.insert(Flags.end(), {NativeParamFlagRef, NativeParamFlagRef});
		if (OptionName)
		{
			Params.push_back(T::HostObject);
			Names.emplace_back(OptionName);
			Flags.push_back(0);
		}
		return Function<TryCall<Action>>(
			S, Name, T::Bool, std::move(Params), Error, std::move(Names), std::move(Flags));
	}

	bool RegisterReferences(FState& S, FHostTypeId Pointer, FHostTypeId Document, std::string& Error)
	{
		using T = ENativeType;
		constexpr auto R = NativeParamFlagRef;
		return Method<TryCall<ETryAction::PointerResolve>>(S,
				   Pointer,
				   "try_resolve",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   false,
				   "",
				   {"json::document", "json::value", "json::error"},
				   {0, R, R}) &&
			   Method<TryCall<ETryAction::PointerResolve>>(S,
				   Pointer,
				   "try_resolve_with_limits",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   false,
				   "",
				   {"json::document", "json::value", "json::error", "json::limits"},
				   {0, R, R, 0}) &&
			   Method<TryCall<ETryAction::DocumentGet>>(S,
				   Document,
				   "try_get",
				   T::Bool,
				   {T::HostObject, T::HostObject},
				   Error,
				   false,
				   "",
				   {"json::value", "json::error"},
				   {R, R});
	}

	bool RegisterOptions(FState& S, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId L, P, W;
		return Type<FLimits>(S, L, Error) && Type<FParseOptions>(S, P, Error) && Type<FWriteOptions>(S, W, Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::InputBytes>(S, L, "input_bytes", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::OutputBytes>(S, L, "output_bytes", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::TokenBytes>(S, L, "token_bytes", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::Nodes>(S, L, "nodes", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::Depth>(S, L, "depth", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::NumberBytes>(S, L, "number_bytes", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::Exponent>(S, L, "exponent", Error) &&
			   RegisterProperty<FLimits, std::uint64_t, &FLimits::WorkUnits>(S, L, "work_units", Error) &&
			   RegisterLimits<FParseOptions>(S, P, Error) && RegisterLimits<FWriteOptions>(S, W, Error) &&
			   RegisterProperty<FParseOptions, EDuplicateKeys, &FParseOptions::DuplicateKeys>(
				   S, P, "duplicate_keys", Error) &&
			   RegisterProperty<FParseOptions, bool, &FParseOptions::bAllowLeadingBom>(
				   S, P, "allow_leading_bom", Error) &&
			   RegisterProperty<FWriteOptions, bool, &FWriteOptions::bPretty>(S, W, "pretty", Error) &&
			   RegisterProperty<FWriteOptions, bool, &FWriteOptions::bSortKeys>(S, W, "sort_keys", Error) &&
			   RegisterProperty<FWriteOptions, std::uint32_t, &FWriteOptions::Indent>(S, W, "indent", Error) &&

			   RegisterTry<ETryAction::CompilePointer>(
				   S, "json::try_compile_pointer", T::HostObject, "json::pointer", Error) &&
			   RegisterTry<ETryAction::CompilePointer>(
				   S, "json::try_compile_pointer_with_limits", T::HostObject, "json::pointer", Error, "json::limits") &&
			   RegisterTry<ETryAction::UInt64>(S, "json::try_as_uint64", T::UInt64, "", Error) &&
			   RegisterTry<ETryAction::FromString>(S, "json::try_from_string", T::HostObject, "json::value", Error) &&
			   RegisterTry<ETryAction::FromDouble>(S, "json::try_from_double", T::HostObject, "json::value", Error) &&
			   RegisterTry<ETryAction::Write>(S, "json::try_write", T::String, "", Error) &&
			   RegisterTry<ETryAction::Write>(
				   S, "json::try_write_with_options", T::String, "", Error, "json::write_options") &&
			   RegisterTry<ETryAction::Resolve>(S, "json::try_resolve", T::HostObject, "json::value", Error) &&
			   RegisterTry<ETryAction::Patch>(S, "json::try_patch", T::HostObject, "json::document", Error) &&
			   RegisterTry<ETryAction::MergePatch>(S, "json::try_merge_patch", T::HostObject, "json::value", Error) &&
			   RegisterTry<ETryAction::Number>(S, "json::try_number", T::HostObject, "json::value", Error) &&
			   RegisterTry<ETryAction::Int64>(S, "json::try_as_int64", T::Int64, "", Error) &&
			   RegisterTry<ETryAction::Double>(S, "json::try_as_double", T::Double, "", Error) &&
			   RegisterTry<ETryAction::DoubleExact>(S, "json::try_as_double_exact", T::Double, "", Error) &&
			   RegisterTry<ETryAction::Boolean>(S, "json::try_as_bool", T::Bool, "", Error) &&
			   RegisterTry<ETryAction::String>(S, "json::try_as_string", T::String, "", Error);
	}
} // namespace VeroScript::Json::Binding
