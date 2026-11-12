// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"
#include "VeroScriptVariant.h"
#include "VeroScriptFloatFormatting.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <thread>
#include <unordered_set>
#include <unordered_map>

namespace VeroScript::Json::Binding
{
	struct FScriptMappingOptions
	{
		bool IgnoreUnknown = false, NumericEnums = false, KeyValuePairs = false, ExactFloating = false;
		std::uint64_t MaxDepth = 256, MaxNodes = 2 * 1024 * 1024;
	};
	struct FScriptMappingContext
	{
		FScriptMappingOptions Options;
		FError Error;
		std::vector<std::string> Path;
		std::vector<std::uint64_t> Objects;
		std::unordered_set<std::uint64_t> Active;
		std::unordered_map<std::string, std::string> SchemaNames;
		std::optional<FBuilder> SchemaDefinitions;
		bool InputSchema = true;
		std::string FreshType;
		std::uint64_t Nodes = 0, PathBytes = 0;
		std::thread::id Thread = std::this_thread::get_id();
		std::string SchemaKey(const std::string& Name) const
		{
			return std::string(InputSchema ? "input:" : "output:") + Name;
		}
		bool Fail(EError Code, std::string Message)
		{
			if (!Error)
			{
				Error.Code = Code;
				Error.Message = std::move(Message);
				for (const auto& Item : Path)
				{
					Error.InstancePointer += '/';
					for (const char C : Item)
					{
						Error.InstancePointer += C == '~' ? "~0" : C == '/' ? "~1" : std::string(1, C);
					}
				}
			}
			return false;
		}
		bool Adopt(FError Value)
		{
			if (!Error && Value)
			{
				Fail(Value.Code, {});
				Value.InstancePointer.insert(0, Error.InstancePointer);
				Error = std::move(Value);
			}
			return false;
		}
		bool Visit()
		{
			if (Error)
			{
				return false;
			}
			if (Nodes >= Options.MaxNodes)
			{
				return Fail(EError::Limit, "JSON mapping node limit exceeded");
			}
			if (Path.size() >= Options.MaxDepth)
			{
				return Fail(EError::Limit, "JSON mapping depth exceeded");
			}
			++Nodes;
			return true;
		}
	};
	template <>
	inline const char* TypeName<FScriptMappingOptions>()
	{
		return "json::mapping_options";
	}
	template <>
	inline const char* TypeName<FScriptMappingContext>()
	{
		return "json::mapping_context";
	}

	static bool InvokeMapping(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		return (*static_cast<const FBindingCall*>(C.UserData))(C, Out, Error);
	}

	static bool MappingMember(FState& S, FHostTypeId Id, std::string Name, ENativeType Result,
		std::vector<ENativeType> Params, FBindingCall Callback, std::string& Error, std::vector<std::string> Names = {},
		std::vector<std::uint8_t> Flags = {}, const char* ResultName = "")
	{
		FHostMemberDesc M;
		M.Name = std::move(Name);
		M.ReturnType = Result;
		M.ParamTypes = std::move(Params);
		M.ParamHostTypeNames = std::move(Names);
		M.ParamFlags = std::move(Flags);
		M.ReturnHostTypeName = ResultName;
		M.bMutableThis = M.Name.rfind("set_", 0) == 0;
		M.bConstMethod = !M.bMutableThis;
		auto Call = std::make_unique<FBindingCall>(
			[Callback = std::move(Callback), State = &S](const auto& Original, auto& Out, auto& E)
			{
				auto C = Original;
				C.UserData = State;
				if (C.HostThis)
				{
					++C.Args;
					if (C.MutableArgs)
					{
						++C.MutableArgs;
					}
					--C.ArgCount;
				}
				return Callback(C, Out, E);
			});
		M.UserData = Call.get();
		M.Callback = DiagnosticCall<InvokeMapping>;
		S.Calls.push_back(std::move(Call));
		return S.Host->RegisterMember(Id, std::move(M), Error);
	}

	static bool MakeContext(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		FScriptMappingContext Context;
		if (C.ArgCount)
		{
			Context.Options = Object<FScriptMappingOptions>(C, 0);
		}
		if (!Context.Options.MaxDepth || Context.Options.MaxDepth > 256 || !Context.Options.MaxNodes)
		{
			Error = "Invalid JSON mapping limits";
			return false;
		}
		return Wrap(*static_cast<FState*>(C.UserData), std::move(Context), Out, Error);
	}

	bool RegisterMappingContext(FState& S, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Options, Context;
		if (!Type<FScriptMappingOptions>(S, Options, Error) || !HandleType<FScriptMappingContext>(S, Context, Error) ||
			!Function<MakeContext>(
				S, "json::mapping_context_create", T::HostObject, {}, Error, {}, {}, "json::mapping_context") ||
			!Function<MakeContext>(S,
				"json::mapping_context_create_with_options",
				T::HostObject,
				{T::HostObject},
				Error,
				{"json::mapping_options"},
				{},
				"json::mapping_context"))
		{
			return false;
		}
		for (const std::string Name : {"ignore_unknown", "numeric_enums", "key_value_pairs", "exact_floating"})
		{
			auto Field = [Name](FScriptMappingOptions& O) -> bool&
			{
				return Name == "ignore_unknown"	   ? O.IgnoreUnknown
					   : Name == "numeric_enums"   ? O.NumericEnums
					   : Name == "key_value_pairs" ? O.KeyValuePairs
												   : O.ExactFloating;
			};
			if (!MappingMember(
					S,
					Options,
					Name,
					T::Bool,
					{},
					[Field](const auto& C, auto& Out, auto&)
					{
						Out = Field(*static_cast<FScriptMappingOptions*>(C.HostThis));
						return true;
					},
					Error) ||
				!MappingMember(
					S,
					Options,
					"set_" + Name,
					T::Void,
					{T::Bool},
					[Field](const auto& C, auto&, auto&)
					{
						Field(*static_cast<FScriptMappingOptions*>(C.HostThis)) = std::get<bool>(C.Args[0]);
						return true;
					},
					Error) ||
				!MappingMember(
					S,
					Context,
					Name,
					T::Bool,
					{},
					[Field](const auto& C, auto& Out, auto&)
					{
						Out = Field(static_cast<FScriptMappingContext*>(C.HostThis)->Options);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		if (!MappingMember(
				S,
				Options,
				"set_limits",
				T::Void,
				{T::Int64, T::Int64},
				[](const auto& C, auto&, auto& E)
				{
					const auto Depth = std::get<std::int64_t>(C.Args[0]), Nodes = std::get<std::int64_t>(C.Args[1]);
					if (Depth < 1 || Depth > 256 || Nodes < 1)
					{
						E = "Invalid JSON mapping limits";
						return false;
					}
					auto& O = *static_cast<FScriptMappingOptions*>(C.HostThis);
					O.MaxDepth = static_cast<std::uint64_t>(Depth);
					O.MaxNodes = static_cast<std::uint64_t>(Nodes);
					return true;
				},
				Error))
		{
			return false;
		}
		using FAction =
			std::function<bool(FScriptMappingContext&, const FNativeCallContext&, VeroScript::FValue&, std::string&)>;
		auto Add = [&](const char* Name,
					   T Result,
					   std::vector<T>
						   Params,
					   FAction Action,
					   std::vector<std::string> Names = {},
					   std::vector<std::uint8_t> Flags = {},
					   const char* ResultName = "")
		{
			return MappingMember(
				S,
				Context,
				Name,
				Result,
				std::move(Params),
				[Action = std::move(Action), Result, Recoverable = std::string_view(Name) != "schema_reference"](
					const auto& Original, auto& Out, auto& E)
				{
					const auto& C = Original;
					auto& V = *static_cast<FScriptMappingContext*>(C.HostThis);
					if (V.Thread != std::this_thread::get_id())
					{
						E = "JSON mapping context belongs to another thread";
						return false;
					}
					const auto Work = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
					std::uint64_t Units = 1;
					for (std::size_t I = 0; I < C.ArgCount; ++I)
					{
						if (const auto* Text = std::get_if<std::string>(&C.Args[I]))
						{
							Units += Text->size();
						}
					}
					if (Work.Check && !Work.Check(Work.UserData, Units))
					{
						E = "JSON mapping interrupted";
						return false;
					}
					try
					{
						return Action(V, C, Out, E);
					}
					catch (const std::bad_alloc&)
					{
						if (!V.Error)
						{
							V.Error.Code = EError::Allocation;
						}
						if (Result != T::Bool || !Recoverable)
						{
							E = "JSON mapping allocation failed";
							return false;
						}
						Out = false;
						return true;
					}
				},
				Error,
				std::move(Names),
				std::move(Flags),
				ResultName);
		};
		if (!Add("runtime_type",
				T::String,
				{T::Variant},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					Out = std::string{};
					if (V.Error)
					{
						return true;
					}
					std::string Problem;
					VeroScript::FValue Snapshot;
					if (!C.ScriptInvoker ||
						!Variant::ValidateSource(C.Args[0],
							C.ScriptInvoker->GetProgramOwner(),
							C.ScriptInvoker->GetProgramGeneration(),
							Problem) ||
						!Variant::Snapshot(C.Args[0], Snapshot, Problem))
					{
						V.Fail(EError::Type,
							Problem.empty() ? "Polymorphic mapping requires an active script value"
											: std::move(Problem));
						return true;
					}
					const auto* Object = std::get_if<FObjectHandle>(&Snapshot);
					if (!Object || !*Object || (*Object)->Kind != EScriptObjectKind::Class)
					{
						V.Fail(EError::Type, "Polymorphic mapping requires a script class instance");
						return true;
					}
					Out = (*Object)->GetTypeName();
					return true;
				}))
		{
			return false;
		}
		if (!Add(
				"error",
				T::HostObject,
				{},
				[](auto& V, const auto& C, auto& Out, auto& E)
				{
					return Wrap(*static_cast<FState*>(C.UserData), V.Error, Out, E);
				},
				{},
				{},
				"json::error") ||
			!Add("require",
				T::Void,
				{T::Bool},
				[](auto& V, const auto& C, auto&, auto& E)
				{
					if (std::get<bool>(C.Args[0]))
					{
						return true;
					}
					if (!V.Error)
					{
						V.Fail(EError::Callback, "JSON converter returned false without a diagnostic");
					}
					E = std::string(V.Error.Id()) + ": " + V.Error.Message + " at " + V.Error.InstancePointer;
					Diagnostic(C, V.Error.Id(), V.Error.Message, V.Error.InstancePointer);
					return false;
				}) ||
			!Add("fail",
				T::Bool,
				{T::String},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					Out = V.Fail(EError::Type, std::get<std::string>(C.Args[0]));
					return true;
				}) ||
			!Add("fail_range",
				T::Bool,
				{T::String},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					Out = V.Fail(EError::Range, std::get<std::string>(C.Args[0]));
					return true;
				}) ||
			!Add("callback_result",
				T::Bool,
				{T::Bool},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					const bool Success = std::get<bool>(C.Args[0]);
					if (!Success && !V.Error)
					{
						V.Fail(EError::Callback, "JSON converter returned false without a diagnostic");
					}
					Out = Success && !V.Error;
					return true;
				}) ||
			!Add("use_fresh",
				T::Void,
				{T::String},
				[](auto& V, const auto& C, auto&, auto&)
				{
					V.FreshType = std::get<std::string>(C.Args[0]);
					return true;
				}) ||
			!Add("take_fresh",
				T::Bool,
				{T::String},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					const bool Match = V.FreshType == std::get<std::string>(C.Args[0]);
					if (Match)
					{
						V.FreshType.clear();
					}
					Out = Match;
					return true;
				}) ||
			!Add("schema_input",
				T::Bool,
				{},
				[](auto& V, const auto&, auto& Out, auto&)
				{
					Out = V.InputSchema;
					return true;
				}) ||
			!Add("set_schema_input",
				T::Void,
				{T::Bool},
				[](auto& V, const auto& C, auto&, auto&)
				{
					V.InputSchema = std::get<bool>(C.Args[0]);
					return true;
				}) ||
			!Add("schema_reference",
				T::Bool,
				{T::String, T::HostObject},
				[](auto& V, const auto& C, auto& Out, auto& E)
				{
					const auto Name = V.SchemaKey(std::get<std::string>(C.Args[0]));
					const auto Existing = V.SchemaNames.find(Name);
					const bool Found = Existing != V.SchemaNames.end();
					if (!Found && !V.Visit())
					{
						E = V.Error.Message;
						return false;
					}
					const std::string Key = Found ? Existing->second : std::to_string(V.SchemaNames.size());
					FJsonValue Ref = FJsonValue::Object(), Text;
					FError Failure;
					if (!FJsonValue::TryString("#/$defs/" + Key, Text, Failure) || !Ref.TrySet("$ref", Text, Failure))
					{
						E = Failure.Message;
						return false;
					}
					VeroScript::FValue Wrapped;
					if (!Wrap(*static_cast<FState*>(C.UserData), std::move(Ref), Wrapped, E))
					{
						return false;
					}
					if (!C.MutableArg(1))
					{
						E = "JSON schema reference requires ref output";
						return false;
					}
					if (!Found)
					{
						if (V.Objects.size() >= V.Options.MaxDepth)
						{
							E = "JSON schema generation depth exceeded";
							return false;
						}
						if (!V.SchemaDefinitions)
						{
							V.SchemaDefinitions.emplace(EKind::Object);
						}
						V.SchemaNames.emplace(Name, Key);
						V.Objects.push_back(0);
					}
					C.AssignValue(*C.MutableArg(1), std::move(Wrapped));
					Out = Found;
					return true;
				},
				{"", "json::value"},
				{0, NativeParamFlagRef}) ||
			!Add("schema_define",
				T::Void,
				{T::String, T::HostObject},
				[](auto& V, const auto& C, auto&, auto& E)
				{
					const auto Found = V.SchemaNames.find(V.SchemaKey(std::get<std::string>(C.Args[0])));
					FError Failure;
					if (Found == V.SchemaNames.end() || !V.SchemaDefinitions || V.Objects.empty())
					{
						E = "Unknown JSON schema definition";
						return false;
					}
					if (!V.SchemaDefinitions->TrySet(Found->second, Object<FJsonValue>(C, 1), Failure))
					{
						E = Failure.Message;
						return false;
					}
					V.Objects.pop_back();
					return true;
				},
				{"", "json::value"}) ||
			!Add(
				"schema_finish",
				T::HostObject,
				{T::HostObject},
				[](auto& V, const auto& C, auto& Out, auto& E)
				{
					FJsonValue Root = Object<FJsonValue>(C, 0), Defs = FJsonValue::Object(), Dialect;
					FError Failure;
					if (!V.Objects.empty())
					{
						E = "JSON schema has an unfinished definition";
						return false;
					}
					if (Root.Kind() == EKind::Boolean)
					{
						FJsonValue All = FJsonValue::Array();
						if (!All.TryPush(Root, Failure))
						{
							E = Failure.Message;
							return false;
						}
						Root = FJsonValue::Object();
						if (!Root.TrySet("allOf", All, Failure))
						{
							E = Failure.Message;
							return false;
						}
					}
					if ((V.SchemaDefinitions && !V.SchemaDefinitions->TryFinish(Defs, Failure)) ||
						!FJsonValue::TryString("https://json-schema.org/draft/2020-12/schema", Dialect, Failure) ||
						!Root.TrySet("$schema", Dialect, Failure) || !Root.TrySet("$defs", Defs, Failure))
					{
						E = Failure.Message;
						return false;
					}
					return Wrap(*static_cast<FState*>(C.UserData), std::move(Root), Out, E);
				},
				{"json::value"},
				{},
				"json::value") ||
			!Add("adopt",
				T::Bool,
				{T::HostObject},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					Out = V.Adopt(Object<FError>(C, 0));
					return true;
				},
				{"json::error"}) ||
			!Add("visit",
				T::Bool,
				{},
				[](auto& V, const auto&, auto& Out, auto&)
				{
					Out = V.Visit();
					return true;
				}) ||
			!Add("push",
				T::Bool,
				{T::String},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					const auto& Key = std::get<std::string>(C.Args[0]);
					if (V.Error)
					{
						Out = false;
						return true;
					}
					if (V.Path.size() >= V.Options.MaxDepth || Key.size() > 65536 - V.PathBytes)
					{
						Out = V.Fail(EError::Limit, "JSON mapping path limit exceeded");
						return true;
					}
					V.Path.push_back(Key);
					V.PathBytes += Key.size();
					Out = true;
					return true;
				}) ||
			!Add("pop",
				T::Void,
				{},
				[](auto& V, const auto&, auto&, auto&)
				{
					if (!V.Path.empty())
					{
						V.PathBytes -= V.Path.back().size();
						V.Path.pop_back();
					}
					return true;
				}) ||
			!Add("enter",
				T::Bool,
				{T::UInt64},
				[](auto& V, const auto& C, auto& Out, auto&)
				{
					const auto Id = std::get<std::uint64_t>(C.Args[0]);
					if (V.Objects.size() >= V.Options.MaxDepth)
					{
						Out = V.Fail(EError::Limit, "JSON mapping depth exceeded");
						return true;
					}
					if (Id && V.Active.count(Id))
					{
						Out = V.Fail(EError::Cycle, "JSON mapping encountered a cycle");
						return true;
					}
					V.Objects.push_back(Id);
					try
					{
						if (Id)
						{
							V.Active.insert(Id);
						}
					}
					catch (...)
					{
						V.Objects.pop_back();
						throw;
					}
					Out = V.Visit();
					return true;
				}) ||
			!Add("leave",
				T::Void,
				{},
				[](auto& V, const auto&, auto&, auto&)
				{
					if (!V.Objects.empty())
					{
						V.Active.erase(V.Objects.back());
						V.Objects.pop_back();
					}
					return true;
				}))
		{
			return false;
		}
		for (const auto Kind : {T::Bool, T::Int64, T::UInt64, T::Double, T::Float, T::String})
		{
			const char* Name = Kind == T::Bool	   ? "write_bool"
							   : Kind == T::Int64  ? "write_int64"
							   : Kind == T::UInt64 ? "write_uint64"
							   : Kind == T::Double ? "write_double"
							   : Kind == T::Float  ? "write_float"
												   : "write_string";
			if (!Add(Name,
					T::Bool,
					{Kind, T::HostObject},
					[Kind](auto& V, const auto& C, auto& Out, auto& E)
					{
						if (!V.Visit())
						{
							Out = false;
							return true;
						}
						FJsonValue Value;
						FError Error;
						bool Ok = true;
						if (Kind == T::Bool)
						{
							Value = FJsonValue(std::get<bool>(C.Args[0]));
						}
						else if (Kind == T::Int64)
						{
							Value = FJsonValue(std::get<std::int64_t>(C.Args[0]));
						}
						else if (Kind == T::UInt64)
						{
							Value = FJsonValue(std::get<std::uint64_t>(C.Args[0]));
						}
						else if (Kind == T::String)
						{
							Ok = FJsonValue::TryString(std::get<std::string>(C.Args[0]), Value, Error);
						}
						else if (Kind == T::Double)
						{
							Ok = FJsonValue::TryDouble(std::get<double>(C.Args[0]), Value, Error);
						}
						else
						{
							const auto Number = std::get<float>(C.Args[0]);
							if (!std::isfinite(Number))
							{
								Error.Code = EError::Range;
								Ok = false;
							}
							else
							{
								char Buffer[64];
								const auto R = FloatFormattingDetail::ToCharsDecimal(Buffer, Buffer + sizeof(Buffer), Number);
								Ok = R.ec == std::errc{} &&
									 FJsonValue::TryNumber(std::string_view(Buffer, R.ptr - Buffer), Value, Error);
							}
						}
						if (!Ok)
						{
							Out = V.Adopt(std::move(Error));
							return true;
						}
						VeroScript::FValue Wrapped;
						if (!Wrap(*static_cast<FState*>(C.UserData), std::move(Value), Wrapped, E))
						{
							return false;
						}
						if (!C.MutableArg(1))
						{
							E = "JSON mapping requires ref output";
							return false;
						}
						C.AssignValue(*C.MutableArg(1), std::move(Wrapped));
						Out = true;
						return true;
					},
					{"", "json::value"},
					{0, NativeParamFlagRef}))
			{
				return false;
			}
		}
		for (const auto Kind : {T::Bool, T::Int64, T::UInt64, T::Double, T::Float, T::String})
		{
			const char* Name = Kind == T::Bool	   ? "read_bool"
							   : Kind == T::Int64  ? "read_int64"
							   : Kind == T::UInt64 ? "read_uint64"
							   : Kind == T::Double ? "read_double"
							   : Kind == T::Float  ? "read_float"
												   : "read_string";
			if (!Add(Name,
					T::Bool,
					{T::HostObject, Kind},
					[Kind](auto& V, const auto& C, auto& Out, auto& E)
					{
						if (!V.Visit())
						{
							Out = false;
							return true;
						}
						const auto& Input = Object<FJsonValue>(C, 0);
						FError Error;
						VeroScript::FValue Value;
						bool Ok = false;
						if (Kind == T::Bool)
						{
							bool B = false;
							Ok = Input.TryBoolean(B, Error);
							Value = B;
						}
						else if (Kind == T::Int64)
						{
							std::int64_t I = 0;
							Ok = Input.TryInt64(I, Error);
							Value = I;
						}
						else if (Kind == T::UInt64)
						{
							std::uint64_t I = 0;
							Ok = Input.TryUInt64(I, Error);
							Value = I;
						}
						else if (Kind == T::String)
						{
							std::string S;
							Ok = Input.TryString(S, Error);
							Value = std::move(S);
						}
						else
						{
							double D = 0;
							Ok = Input.TryDouble(D, Error, V.Options.ExactFloating);
							if (Kind == T::Double)
							{
								Value = D;
							}
							else
							{
								const auto F = static_cast<float>(D);
								if (Ok && (!std::isfinite(F) || (D != 0 && F == 0) ||
											  (V.Options.ExactFloating && static_cast<double>(F) != D)))
								{
									Ok = false;
									Error.Code = EError::Range;
									Error.Message = "JSON number cannot be represented as float";
								}
								Value = F;
							}
						}
						if (!Ok)
						{
							Out = V.Adopt(std::move(Error));
							return true;
						}
						if (!C.MutableArg(1))
						{
							E = "JSON mapping requires ref output";
							return false;
						}
						C.AssignValue(*C.MutableArg(1), std::move(Value));
						Out = true;
						return true;
					},
					{"json::value", ""},
					{0, NativeParamFlagRef}))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::Json::Binding
