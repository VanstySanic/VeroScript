// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonModule.h"
#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"
#include "VeroScriptJsonBuildInfo.h"
#include <cstring>

namespace VeroScript::Json
{
	namespace Binding
	{
		enum class EAction
		{
			Parse,
			TryParse,
			Write,
			Kind,
			Size,
			Int64,
			UInt64,
			Double,
			Boolean,
			String,
			NumberText,
			At,
			Get,
			Contains,
			Set,
			Push,
			Replace,
			Equals,
			Array,
			Object,
			FromInt,
			FromUInt,
			FromBool,
			FromString,
			FromNumber,
			FromDouble,
			ErrorCode,
			ErrorMessage,
			ErrorId,
			ErrorSource,
			ErrorPointer,
			ErrorPatch,
			ErrorOffset,
			ErrorLine,
			ErrorColumn,
			DocumentSet,
			DocumentHas,
			DocumentGet,
			Patch,
			MergePatch,
			Resolve,
			Null,
			DoubleExact,
			EraseKey,
			EraseIndex,
			Insert,
			Keys,
			PointerCompile,
			PointerResolve
		};

		template <EAction Action>
		bool Call(const FNativeCallContext& Input, VeroScript::FValue& Out, std::string& Error)
		{
			FNativeCallContext C = Input;
			if (C.HostThis)
			{
				++C.Args;
				if (C.MutableArgs)
				{
					++C.MutableArgs;
				}
				--C.ArgCount;
			}
			auto& S = *static_cast<FState*>(C.UserData);
			FError E;
			FJsonValue V;
			auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
			FWork W{Work.Check, Work.UserData};
			auto* This = static_cast<FJsonValue*>(C.HostThis);
			if constexpr (Action == EAction::Parse || Action == EAction::TryParse)
			{
				constexpr auto OptionIndex = Action == EAction::Parse ? 1u : 3u;
				const auto Options = C.ArgCount > OptionIndex ? Object<FParseOptions>(C, OptionIndex) : FParseOptions{};
				bool Success = TryParse(std::get<std::string>(C.Args[0]), V, E, Options, W);
				if constexpr (Action == EAction::TryParse)
				{
					if (!C.MutableArg(1) || !C.MutableArg(2))
					{
						Error = "json::try_parse requires ref outputs";
						return false;
					}
					if (E.Code == EError::Cancelled)
					{
						Error = E.Message;
						Diagnostic(C, E.Id(), E.Message, E.InstancePointer);
						return false;
					}
					VeroScript::FValue ErrorValue, Value;
					if (!Wrap(S, E, ErrorValue, Error) || (Success && !Wrap(S, V, Value, Error)))
					{
						return false;
					}
					C.AssignValue(*C.MutableArg(2), std::move(ErrorValue));
					if (Success)
					{
						C.AssignValue(*C.MutableArg(1), std::move(Value));
					}
					Out = Success;
					return true;
				}
			}
			else if constexpr (Action == EAction::Array)
			{
				V = FJsonValue::Array();
			}
			else if constexpr (Action == EAction::Object)
			{
				V = FJsonValue::Object();
			}
			else if constexpr (Action == EAction::FromInt)
			{
				V = FJsonValue(std::get<std::int64_t>(C.Args[0]));
			}
			else if constexpr (Action == EAction::FromBool)
			{
				V = FJsonValue(std::get<bool>(C.Args[0]));
			}
			else if constexpr (Action == EAction::FromUInt)
			{
				V = FJsonValue(std::get<std::uint64_t>(C.Args[0]));
			}
			else if constexpr (Action == EAction::FromDouble)
			{
				FJsonValue::TryDouble(std::get<double>(C.Args[0]), V, E);
			}
			else if constexpr (Action == EAction::FromString)
			{
				FJsonValue::TryString(std::get<std::string>(C.Args[0]), V, E);
			}
			else if constexpr (Action == EAction::FromNumber)
			{
				FJsonValue::TryNumber(std::get<std::string>(C.Args[0]), V, E);
			}
			else if constexpr (Action == EAction::Write || Action == EAction::String || Action == EAction::NumberText)
			{
				std::string Text;
				if constexpr (Action == EAction::Write)
				{
					TryWrite(*This, Text, E, C.ArgCount ? Object<FWriteOptions>(C, 0) : FWriteOptions{}, W);
				}
				else if constexpr (Action == EAction::String)
				{
					This->TryString(Text, E);
				}
				else
				{
					This->TryNumberText(Text, E);
				}
				Out = std::move(Text);
			}
			else if constexpr (Action == EAction::Kind)
			{
				Out = static_cast<std::int64_t>(This->Kind());
			}
			else if constexpr (Action == EAction::Size || Action == EAction::Int64)
			{
				std::int64_t Number = 0;
				if constexpr (Action == EAction::Size)
				{
					This->TrySize(Number, E);
				}
				else
				{
					This->TryInt64(Number, E);
				}
				Out = Number;
			}
			else if constexpr (Action == EAction::UInt64)
			{
				std::uint64_t Number = 0;
				This->TryUInt64(Number, E);
				Out = Number;
			}
			else if constexpr (Action == EAction::Double || Action == EAction::DoubleExact)
			{
				double Number = 0;
				This->TryDouble(Number, E, Action == EAction::DoubleExact);
				Out = Number;
			}
			else if constexpr (Action == EAction::Boolean || Action == EAction::Equals)
			{
				bool Value = false;
				if constexpr (Action == EAction::Boolean)
				{
					This->TryBoolean(Value, E);
				}
				else
				{
					This->TryEquals(Object<FJsonValue>(C, 0), Value, E, {}, W);
				}
				Out = Value;
			}
			else if constexpr (Action == EAction::At)
			{
				This->TryAt(std::get<std::int64_t>(C.Args[0]), V, E);
			}
			else if constexpr (Action == EAction::Get)
			{
				This->TryGet(std::get<std::string>(C.Args[0]), V, E);
			}
			else if constexpr (Action == EAction::Set)
			{
				This->TrySet(std::get<std::string>(C.Args[0]), Object<FJsonValue>(C, 1), E);
			}
			else if constexpr (Action == EAction::Contains)
			{
				bool Found = false;
				This->TryContains(std::get<std::string>(C.Args[0]), Found, E);
				Out = Found;
			}
			else if constexpr (Action == EAction::Push)
			{
				This->TryPush(Object<FJsonValue>(C, 0), E);
			}
			else if constexpr (Action == EAction::Replace)
			{
				This->TryReplace(std::get<std::int64_t>(C.Args[0]), Object<FJsonValue>(C, 1), E);
			}
			else if constexpr (Action == EAction::Insert)
			{
				This->TryInsert(std::get<std::int64_t>(C.Args[0]), Object<FJsonValue>(C, 1), E);
			}
			else if constexpr (Action == EAction::EraseIndex)
			{
				This->TryErase(std::get<std::int64_t>(C.Args[0]), E);
			}
			else if constexpr (Action == EAction::EraseKey)
			{
				This->TryErase(std::get<std::string>(C.Args[0]), E);
			}
			else if constexpr (Action == EAction::Keys)
			{
				std::vector<std::string> Keys;
				FBuilder B;
				if (This->TryKeys(Keys, E))
				{
					for (const auto& Key : Keys)
					{
						FJsonValue K;
						if (!FJsonValue::TryString(Key, K, E) || !B.TryPush(K, E))
						{
							break;
						}
					}
					if (!E)
					{
						B.TryFinish(V, E);
					}
				}
			}
			else if constexpr (Action == EAction::ErrorId || Action == EAction::ErrorSource ||
							   Action == EAction::ErrorPointer || Action == EAction::ErrorPatch)
			{
				const auto& Value = *static_cast<FError*>(C.HostThis);
				if constexpr (Action == EAction::ErrorId)
				{
					Out = std::string(Value.Id());
				}
				else if constexpr (Action == EAction::ErrorSource)
				{
					Out = Value.bHasSource;
				}
				else if constexpr (Action == EAction::ErrorPointer)
				{
					Out = Value.InstancePointer;
				}
				else
				{
					Out = Value.PatchOperation;
				}
			}
			else if constexpr (Action == EAction::ErrorCode || Action == EAction::ErrorOffset ||
							   Action == EAction::ErrorLine || Action == EAction::ErrorColumn ||
							   Action == EAction::ErrorMessage)
			{
				const auto& Value = *static_cast<FError*>(C.HostThis);
				if constexpr (Action == EAction::ErrorMessage)
				{
					Out = Value.Message;
				}
				else
				{
					Out =
						static_cast<std::int64_t>(Action == EAction::ErrorCode ? static_cast<std::uint64_t>(Value.Code)
												  : Action == EAction::ErrorOffset ? Value.ByteOffset
												  : Action == EAction::ErrorLine   ? Value.Line
																				   : Value.Column);
				}
			}
			else if constexpr (Action == EAction::DocumentSet)
			{
				auto& Document = *static_cast<FDocument*>(C.HostThis);
				Document.Value = Object<FJsonValue>(C, 0);
				Document.bHasValue = true;
			}
			else if constexpr (Action == EAction::DocumentHas)
			{
				Out = static_cast<FDocument*>(C.HostThis)->bHasValue;
			}
			else if constexpr (Action == EAction::DocumentGet)
			{
				auto& Document = *static_cast<FDocument*>(C.HostThis);
				if (!Document.bHasValue)
				{
					Error = "JSON document has no root value";
					return false;
				}
				V = Document.Value;
			}
			else if constexpr (Action == EAction::Patch)
			{
				FDocument Result;
				if (TryPatch(Object<FDocument>(C, 0), Object<FJsonValue>(C, 1), Result, E, {}, W))
				{
					return Wrap(S, Result, Out, Error);
				}
			}
			else if constexpr (Action == EAction::MergePatch)
			{
				TryMergePatch(Object<FJsonValue>(C, 0), Object<FJsonValue>(C, 1), V, E, {}, W);
			}
			else if constexpr (Action == EAction::Resolve)
			{
				TryResolve(Object<FDocument>(C, 0), std::get<std::string>(C.Args[1]), V, E, {}, W);
			}
			else if constexpr (Action == EAction::PointerCompile)
			{
				FPointer Pointer;
				if (FPointer::TryCompile(
						std::get<std::string>(C.Args[0]), Pointer, E, std::get<bool>(C.Args[1]), {}, W))
				{
					return Wrap(S, std::move(Pointer), Out, Error);
				}
			}
			else if constexpr (Action == EAction::PointerResolve)
			{
				static_cast<FPointer*>(C.HostThis)->TryResolve(Object<FDocument>(C, 0), V, E, {}, W);
			}
			if (E)
			{
				Error = std::string(E.Id()) + ": " + E.Message;
				Diagnostic(C, E.Id(), E.Message, E.InstancePointer);
				return false;
			}
			if constexpr (Action == EAction::Parse || Action == EAction::At || Action == EAction::Get ||
						  Action == EAction::Array || Action == EAction::Object || Action == EAction::FromInt ||
						  Action == EAction::FromUInt || Action == EAction::FromBool || Action == EAction::FromString ||
						  Action == EAction::FromNumber || Action == EAction::FromDouble ||
						  Action == EAction::DocumentGet || Action == EAction::MergePatch ||
						  Action == EAction::Resolve || Action == EAction::Keys || Action == EAction::Null ||
						  Action == EAction::PointerResolve)
			{
				return Wrap(S, std::move(V), Out, Error);
			}
			return true;
		}

		bool Register(FState& S, std::string& Error)
		{
			using T = ENativeType;
			FHostTypeDesc Functions;
			Functions.Name = "json";
			Functions.Kind = EHostTypeKind::Class;
			if (!S.Host->RegisterType(std::move(Functions), S.Functions, Error))
			{
				return false;
			}
			FHostTypeId V, E, D, P;
			return Type<FJsonValue>(S, V, Error) && Type<FError>(S, E, Error) && Type<FDocument>(S, D, Error) &&
				   Type<FPointer>(S, P, Error) && RegisterValueQueries(S, V, Error) &&
				   Function<Call<EAction::PointerCompile>>(S,
					   "json::compile_pointer",
					   T::HostObject,
					   {T::String, T::Bool},
					   Error,
					   {},
					   {},
					   "json::pointer") &&
				   Method<Call<EAction::PointerResolve>>(S,
					   P,
					   "resolve",
					   T::HostObject,
					   {T::HostObject},
					   Error,
					   false,
					   "json::value",
					   {"json::document"}) &&
				   RegisterOptions(S, Error) && RegisterReferences(S, P, D, Error) && RegisterStreams(S, Error) &&
				   RegisterSchema(S, Error) && RegisterMappingContext(S, Error) && RegisterMappingProcessor(S, Error) &&
				   Function<Call<EAction::Null>>(S, "json::from_null", T::HostObject, {}, Error) &&
				   Function<Call<EAction::Parse>>(S, "json::parse", T::HostObject, {T::String}, Error) &&
				   Function<Call<EAction::Parse>>(S,
					   "json::parse_with_options",
					   T::HostObject,
					   {T::String, T::HostObject},
					   Error,
					   {"", "json::parse_options"}) &&
				   Function<Call<EAction::TryParse>>(S,
					   "json::try_parse",
					   T::Bool,
					   {T::String, T::HostObject, T::HostObject},
					   Error,
					   {"", "json::value", "json::error"},
					   {0, NativeParamFlagRef, NativeParamFlagRef}) &&
				   Function<Call<EAction::TryParse>>(S,
					   "json::try_parse_with_options",
					   T::Bool,
					   {T::String, T::HostObject, T::HostObject, T::HostObject},
					   Error,
					   {"", "json::value", "json::error", "json::parse_options"},
					   {0, NativeParamFlagRef, NativeParamFlagRef, 0}) &&
				   Function<Call<EAction::Array>>(S, "json::array", T::HostObject, {}, Error) &&
				   Function<Call<EAction::Object>>(S, "json::object", T::HostObject, {}, Error) &&
				   Function<Call<EAction::FromInt>>(S, "json::from_int", T::HostObject, {T::Int64}, Error) &&
				   Function<Call<EAction::FromBool>>(S, "json::from_bool", T::HostObject, {T::Bool}, Error) &&
				   Function<Call<EAction::FromDouble>>(S, "json::from_double", T::HostObject, {T::Double}, Error) &&
				   Function<Call<EAction::FromString>>(S, "json::from_string", T::HostObject, {T::String}, Error) &&
				   Function<Call<EAction::FromNumber>>(S, "json::from_number", T::HostObject, {T::String}, Error) &&
				   Function<Call<EAction::Patch>>(S,
					   "json::patch",
					   T::HostObject,
					   {T::HostObject, T::HostObject},
					   Error,
					   {"json::document", "json::value"},
					   {},
					   "json::document") &&
				   Function<Call<EAction::MergePatch>>(S,
					   "json::merge_patch",
					   T::HostObject,
					   {T::HostObject, T::HostObject},
					   Error,
					   {"json::value", "json::value"}) &&
				   Function<Call<EAction::Resolve>>(
					   S, "json::resolve", T::HostObject, {T::HostObject, T::String}, Error, {"json::document", ""}) &&
				   Method<Call<EAction::Write>>(S, V, "write", T::String, {}, Error) &&
				   Method<Call<EAction::Write>>(S,
					   V,
					   "write_with_options",
					   T::String,
					   {T::HostObject},
					   Error,
					   false,
					   "",
					   {"json::write_options"}) &&
				   Method<Call<EAction::Kind>>(S, V, "kind", T::Int64, {}, Error) &&
				   Method<Call<EAction::Size>>(S, V, "size", T::Int64, {}, Error) &&
				   Method<Call<EAction::Int64>>(S, V, "as_int64", T::Int64, {}, Error) &&
				   Function<Call<EAction::FromUInt>>(S, "json::from_uint", T::HostObject, {T::UInt64}, Error) &&
				   Method<Call<EAction::UInt64>>(S, V, "as_uint64", T::UInt64, {}, Error) &&
				   Method<Call<EAction::Double>>(S, V, "as_double", T::Double, {}, Error) &&
				   Method<Call<EAction::DoubleExact>>(S, V, "as_double_exact", T::Double, {}, Error) &&
				   Method<Call<EAction::Boolean>>(S, V, "as_bool", T::Bool, {}, Error) &&
				   Method<Call<EAction::String>>(S, V, "as_string", T::String, {}, Error) &&
				   Method<Call<EAction::NumberText>>(S, V, "number_text", T::String, {}, Error) &&
				   Method<Call<EAction::At>>(S, V, "at", T::HostObject, {T::Int64}, Error) &&
				   Method<Call<EAction::Get>>(S, V, "get", T::HostObject, {T::String}, Error) &&
				   Method<Call<EAction::Contains>>(S, V, "contains", T::Bool, {T::String}, Error) &&
				   Method<Call<EAction::Set>>(S, V, "set", T::Void, {T::String, T::HostObject}, Error, true) &&
				   Method<Call<EAction::Push>>(S, V, "push", T::Void, {T::HostObject}, Error, true) &&
				   Method<Call<EAction::Replace>>(S, V, "replace", T::Void, {T::Int64, T::HostObject}, Error, true) &&
				   Method<Call<EAction::Insert>>(S, V, "insert", T::Void, {T::Int64, T::HostObject}, Error, true) &&
				   Method<Call<EAction::EraseKey>>(S, V, "erase", T::Void, {T::String}, Error, true) &&
				   Method<Call<EAction::EraseIndex>>(S, V, "remove_at", T::Void, {T::Int64}, Error, true) &&
				   Method<Call<EAction::Keys>>(S, V, "keys", T::HostObject, {}, Error) &&
				   Method<Call<EAction::Equals>>(S, V, "equals", T::Bool, {T::HostObject}, Error) &&
				   Method<Call<EAction::ErrorCode>>(S, E, "code", T::Int64, {}, Error) &&
				   Method<Call<EAction::ErrorMessage>>(S, E, "message", T::String, {}, Error) &&
				   Method<Call<EAction::ErrorId>>(S, E, "message_id", T::String, {}, Error) &&
				   Method<Call<EAction::ErrorSource>>(S, E, "has_source", T::Bool, {}, Error) &&
				   Method<Call<EAction::ErrorPointer>>(S, E, "instance_pointer", T::String, {}, Error) &&
				   Method<Call<EAction::ErrorPatch>>(S, E, "patch_operation", T::Int64, {}, Error) &&
				   Method<Call<EAction::ErrorOffset>>(S, E, "byte_offset", T::Int64, {}, Error) &&
				   Method<Call<EAction::ErrorLine>>(S, E, "line", T::Int64, {}, Error) &&
				   Method<Call<EAction::ErrorColumn>>(S, E, "column", T::Int64, {}, Error) &&
				   Method<Call<EAction::DocumentHas>>(S, D, "has_value", T::Bool, {}, Error) &&
				   Method<Call<EAction::DocumentSet>>(S, D, "set", T::Void, {T::HostObject}, Error, true) &&
				   Method<Call<EAction::DocumentGet>>(S, D, "get", T::HostObject, {}, Error);
		}

		void* VEROSCRIPT_EXTENSION_CALL Create(const void* Host, char* Buffer, std::uint32_t Capacity)
		{
			auto S = std::make_unique<FState>();
			S->Host = const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Host));
			std::string Error;
			if (Register(*S, Error))
			{
				return S.release();
			}
			if (Capacity)
			{
				auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Buffer, Error.data(), Count);
				Buffer[Count] = 0;
			}
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void* State)
		{
			delete static_cast<FState*>(State);
		}
	} // namespace Binding

	const char* GetImplementationIdentity() noexcept
	{
		return VEROSCRIPT_JSON_IMPLEMENTATION_ID;
	}

	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(std::uint32_t V, std::uint32_t Size, void* Out)
	{
		if (V != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Out)
		{
			return 0;
		}
		FNativeExtensionDescriptor D;
		const auto Abi = BuildNativeExtensionAbi();
		if (Abi.size() >= sizeof(D.Abi))
		{
			return 0;
		}
		std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
		std::memcpy(D.Id, "json", sizeof("json"));
		std::memcpy(D.Version, Version, 6);
		D.Roles =
			NativeExtensionRole(ENativeExtensionRole::Bindings) | NativeExtensionRole(ENativeExtensionRole::Compiler);
		D.Flags = NativeExtensionParallelRegistrations;
		D.Create = Binding::Create;
		D.Destroy = Binding::Destroy;
		std::memcpy(Out, &D, sizeof(D));
		return NativeExtensionApiVersion;
	}
} // namespace VeroScript::Json
