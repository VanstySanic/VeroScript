// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptI18nInternal.h"
#include "VeroScriptHostBinding.h"
#include <stdexcept>

namespace VeroScript::I18n
{
	namespace
	{
		constexpr std::uint32_t TypeIdBase = 0xffffffc0u;
		enum class EType : unsigned
		{
			Argument,
			Arguments,
			Configuration,
			Text,
			Version,
			Template,
			Resolved,
			Snapshot,
			Context,
			Kind
		};
		constexpr const char* Names[] = {"std.i18n.argument", "std.i18n.arguments", "std.i18n.configuration",
			"std.i18n.text", "std.i18n.version", "std.i18n.message_template", "std.i18n.resolved_text",
			"std.i18n.snapshot", "std.i18n.context", "std.i18n.argument_kind"};
		template <class T> constexpr EType Type();
		#define VS_I18N_TYPE(T, K)                                                                                   \
			template <> constexpr EType Type<T>()                                                                    \
			{                                                                                                        \
				return EType::K;                                                                                     \
			}
		VS_I18N_TYPE(FI18nArgument, Argument)
		VS_I18N_TYPE(FI18nArguments, Arguments)
		VS_I18N_TYPE(FI18nConfiguration, Configuration)
		VS_I18N_TYPE(FI18nText, Text)
		VS_I18N_TYPE(FI18nVersion, Version)
		VS_I18N_TYPE(FI18nMessageTemplate, Template)
		VS_I18N_TYPE(FI18nResolvedText, Resolved)
		VS_I18N_TYPE(FI18nSnapshot, Snapshot)
		VS_I18N_TYPE(std::shared_ptr<FI18nContext>, Context)
		#undef VS_I18N_TYPE
		template <class T>
		struct FDelete
		{
			void operator()(FHostObject* H) const
			{
				delete static_cast<T*>(H->Payload);
				delete H;
			}
		};
		bool Clone(const FNativeCallContext&, FValue&, std::string&);
		bool Equals(const FNativeCallContext&, FValue&, std::string&);
		template <class T> FValue Make(T Value)
		{
			auto Payload = std::make_unique<T>(std::move(Value));
			std::unique_ptr<FHostObject> Raw = std::make_unique<FHostObject>();
			Raw->TypeId = TypeIdBase + static_cast<unsigned>(Type<T>());
			Raw->TypeName = Names[static_cast<unsigned>(Type<T>())];
			Raw->Payload = Payload.release();
			// Constructing shared_ptr can throw; its typed deleter then releases Payload.
			FHostObjectHandle H(Raw.release(), FDelete<T>{});
			if constexpr (!std::is_same_v<T, std::shared_ptr<FI18nContext>>)
			{
				H->CloneCallback = Clone;
				H->EqualsCallback = Equals;
			}
			return H;
		}
		template <class T> T* Get(const FValue& Value)
		{
			const auto* H = std::get_if<FHostObjectHandle>(&Value);
			if (!H || !*H || (*H)->TypeId != TypeIdBase + static_cast<unsigned>(Type<T>()) ||
				(*H)->TypeName != Names[static_cast<unsigned>(Type<T>())] ||
				!std::get_deleter<FDelete<T>>(*H))
			{
				return nullptr;
			}

			return static_cast<T*>((*H)->Payload);
		}
		bool ErrorOutput(const FNativeCallContext& C, const FI18nError& E, std::string& Out)
		{
			Out = E.Diagnostic.Message;
			if (C.DiagnosticOutput)
			{
				*C.DiagnosticOutput = E.Diagnostic;
			}

			return false;
		}
		bool Clone(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			FI18nError E;
			const bool Ok = Detail::Boundary(E,
				[&]()
				{
					if (C.ArgCount != 1)
					{
						return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid clone arguments");
					}
					#define VS_I18N_CLONE(T)                                                                                     \
						if (const auto* V = Get<T>(C.Args[0]))                                                                   \
						{                                                                                                        \
							Out = Make(*V);                                                                                      \
							return true;                                                                                         \
						}
					VS_I18N_CLONE(FI18nArgument)
					VS_I18N_CLONE(FI18nArguments)
					VS_I18N_CLONE(FI18nConfiguration)
					VS_I18N_CLONE(FI18nText)
					VS_I18N_CLONE(FI18nVersion)
					VS_I18N_CLONE(FI18nMessageTemplate)
					VS_I18N_CLONE(FI18nResolvedText)
					VS_I18N_CLONE( FI18nSnapshot)
					#undef VS_I18N_CLONE
					return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid i18n value protocol");
				});
			return Ok || ErrorOutput(C, E, Error);
		}
		bool Equals(const FNativeCallContext& C, FValue& Out, std::string&)
		{
			bool Equal = false;
			if (C.ArgCount == 2)
			{
				#define VS_I18N_EQUAL(T) \
					if (const auto* A = Get<T>(C.Args[0])) \
					{ \
						if (const auto* B = Get<T>(C.Args[1])) \
						{ \
							Equal = *A == *B; \
						} \
					}
				VS_I18N_EQUAL(FI18nArgument)
				VS_I18N_EQUAL(FI18nArguments)
				VS_I18N_EQUAL(FI18nConfiguration)
				VS_I18N_EQUAL(FI18nText)
				VS_I18N_EQUAL(FI18nVersion)
				#undef VS_I18N_EQUAL
			}
			Out = Equal;
			return true;
		}
		bool NotEquals(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			Equals(C, Out, Error);
			Out = !std::get<bool>(Out);
			return true;
		}

		FValue Strings(const std::vector<std::string>& V)
		{
			auto A = std::make_shared<FDynamicArray>();
			A->TypeName = "array<string>";
			A->ElementTypeName = "string";
			A->ScalarElements.emplace<std::vector<std::string>>(V);
			return A;
		}
		bool ReadStrings(const FValue& V, std::vector<std::string>& Out, FI18nError& E)
		{
			const auto* A = std::get_if<FDynamicArrayHandle>(&V);
			if (!A || !*A || (*A)->TypeName != "array<string>" || (*A)->ElementTypeName != "string")
			{
				return Detail::Fail(E, EI18nStatus::ArgumentType, "expected array<string>");
			}

			std::vector<std::string> Next;
			Next.reserve((*A)->ElementCount());
			for (std::size_t I = 0; I < (*A)->ElementCount(); ++I)
			{
				const auto* S = (*A)->BorrowString(I);
				if (!S) return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid string array element");
				Next.push_back(*S);
			}
			Out = std::move(Next);
			return true;
		}

		enum class EOp : unsigned
		{
			Argument,
			Scalar,
			Kind,
			AsString,
			AsBool,
			AsInt64,
			AsUInt64,
			AsFloat,
			AsDouble,
			Arguments,
			Set,
			Get,
			Contains,
			Remove,
			Clear,
			Size,
			Names,
			Configuration,
			Language,
			Fallback,
			Locale,
			Options,
			SetLanguageProperty,
			SetFallbackProperty,
			SetLocaleProperty,
			SetOptionsProperty,
			Text,
			LiteralText,
			Message,
			IsLiteral,
			Literal,
			Domain,
			Key,
			Params,
			Version,
			ContextId,
			ConfigRevision,
			ResourceRevision,
			FormatterRevision,
			Template,
			Resolved,
			Pattern,
			FormatId,
			MatchedLanguage,
			ResultValue,
			ResultVersion,
			Snapshot,
			ViewConfiguration,
			ViewVersion,
			Lookup,
			Resolve,
			ResolveVersioned,
			Format,
			Context,
			Configure,
			SetLanguage,
			SetFallback,
			SetLocale,
			SetOptions,
			CloneContext,
			Capture,
			Normalize
		};
		bool Member(const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			const auto Op = static_cast<EOp>(reinterpret_cast<std::uintptr_t>(C.UserData));
			FI18nError E;
			const auto Invalid = [&]()
			{
				return Detail::Fail(E, EI18nStatus::InvalidState, "invalid i18n receiver or arguments");
			};
			const bool Ok = Detail::Boundary(E,
				[&]()
				{
					const auto& A = C.Args;
					switch (Op)
					{
					case EOp::Argument:
						{

							Out = Make(FI18nArgument{});
							return true;
						}
					case EOp::Scalar:
						{
							FI18nArgument V;
							if (!MakeArgument(A[0], V, E)) return false;
							Out = Make(std::move(V));
							return true;
						}
					case EOp::Kind:
						{
							const auto* V = Get<FI18nArgument>(A[0]);
							if (!V) return Invalid();
							Out = FEnumValue{Names[9], static_cast<std::int64_t>(V->Kind())};
							return true;
						}
					case EOp::AsString:
					case EOp::AsBool:
					case EOp::AsInt64:
					case EOp::AsUInt64:
					case EOp::AsFloat:
					case EOp::AsDouble:
						{
							const auto* V = Get<FI18nArgument>(A[0]);
							if (!V) return Invalid();
							#define VS_I18N_READ(O, T, F)                                                                                \
								if (Op == EOp::O)                                                                                        \
								{                                                                                                        \
									T R{};                                                                                               \
									if (!V->F(R, E)) return false;                                                                       \
									Out = std::move(R);                                                                                  \
									return true;                                                                                         \
								}
							VS_I18N_READ(AsString, std::string, TryString)
							VS_I18N_READ(AsBool, bool, TryBool)
							VS_I18N_READ(AsInt64, std::int64_t, TryInt64)
							VS_I18N_READ(AsUInt64, std::uint64_t, TryUInt64)
							VS_I18N_READ(AsFloat, float, TryFloat)
							VS_I18N_READ(AsDouble, double, TryDouble)
							#undef VS_I18N_READ
							return Invalid();
						}
					case EOp::Arguments:
						{

							Out = Make(FI18nArguments{});
							return true;
						}
					case EOp::Set:
					case EOp::Get:
					case EOp::Contains:
					case EOp::Remove:
					case EOp::Clear:
					case EOp::Size:
					case EOp::Names:
						{
							auto* V = Get<FI18nArguments>(A[0]);
							if (!V) return Invalid();
							if (Op == EOp::Set)
							{
								const auto* P = Get<FI18nArgument>(A[2]);
								return P ? V->Set(std::get<std::string>(A[1]), *P, E) : Invalid();
							}
							if (Op == EOp::Remove) return V->Remove(std::get<std::string>(A[1]), E);
							if (Op == EOp::Clear)
							{
								V->Clear();
								return true;
							}
							if (Op == EOp::Size)
							{
								Out = V->Size();
								return true;
							}
							if (Op == EOp::Names)
							{
								std::vector<std::string> R;
								for (const auto& P : V->Values())
								{
									R.push_back(P.first);
								}
								Out = Strings(R);
								return true;
							}
							if (Op == EOp::Contains)
							{
								bool R;
								if (!V->Contains(std::get<std::string>(A[1]), R, E)) return false;
								Out = R;
								return true;
							}
							FI18nArgument R;
							if (!V->Get(std::get<std::string>(A[1]), R, E)) return false;
							Out = Make(std::move(R));
							return true;
						}
					case EOp::Configuration:
						{

							Out = Make(FI18nConfiguration{});
							return true;
						}
					case EOp::Language:
					case EOp::Fallback:
					case EOp::Locale:
					case EOp::Options:
					case EOp::SetLanguageProperty:
					case EOp::SetFallbackProperty:
					case EOp::SetLocaleProperty:
					case EOp::SetOptionsProperty:
						{
							auto* V = Get<FI18nConfiguration>(A[0]);
							if (!V) return Invalid();
							if (Op == EOp::Language)
							{
								Out = V->Language;
							}

							else if (Op == EOp::Fallback)
							{
								Out = Strings(V->FallbackLanguages);
							}

							else if (Op == EOp::Locale)
							{
								Out = V->FormatLocale;
							}

							else if (Op == EOp::Options)
							{
								Out = Make(V->FormatOptions);
							}

							else if (Op == EOp::SetLanguageProperty)
							{
								V->Language = std::get<std::string>(A[1]);
							}

							else if (Op == EOp::SetFallbackProperty)
							{
								return ReadStrings(A[1], V->FallbackLanguages, E);
							}

							else if (Op == EOp::SetLocaleProperty)
							{
								V->FormatLocale = std::get<std::string>(A[1]);
							}

							else
							{
								const auto* P = Get<FI18nArguments>(A[1]);
								if (!P) return Invalid();
								V->FormatOptions = *P;
							}
							return true;
						}
					case EOp::Text:
						{

							Out = Make(FI18nText{});
							return true;
						}
					case EOp::LiteralText:
						{

							Out = Make(FI18nText(std::get<std::string>(A[0])));
							return true;
						}
					case EOp::Message:
						{
							FI18nArguments Params;
							if (C.ArgCount == 3)
							{
								const auto* P = Get<FI18nArguments>(A[2]);
								if (!P) return Invalid();
								Params = *P;
							}
							FI18nText R;
							if (!FI18nText::Message(std::get<std::string>(A[0]), std::get<std::string>(A[1]),
									std::move(Params), R, E))
							{
								return false;
							}
							Out = Make(std::move(R));
							return true;
						}
					case EOp::IsLiteral:
					case EOp::Literal:
					case EOp::Domain:
					case EOp::Key:
					case EOp::Params:
						{
							const auto* V = Get<FI18nText>(A[0]);
							if (!V) return Invalid();
							if (Op == EOp::IsLiteral)
							{
								Out = V->IsLiteral();
								return true;
							}
							if ((Op == EOp::Literal) != V->IsLiteral())
							{
								return Detail::Fail(
									E, EI18nStatus::InvalidState, "text accessor does not match text kind");
							}
							if (Op == EOp::Literal)
							{
								Out = V->LiteralValue();
							}

							else if (Op == EOp::Domain)
							{
								Out = V->Domain();
							}

							else if (Op == EOp::Key)
							{
								Out = V->Key();
							}

							else
							{
								Out = Make(V->Params());
							}
							return true;
						}
					case EOp::Version:
						{

							Out = Make(FI18nVersion{});
							return true;
						}
					case EOp::ContextId:
					case EOp::ConfigRevision:
					case EOp::ResourceRevision:
					case EOp::FormatterRevision:
						{
							const auto* V = Get<FI18nVersion>(A[0]);
							if (!V) return Invalid();
							Out = Op == EOp::ContextId		  ? V->ContextId
								: Op == EOp::ConfigRevision	  ? V->ConfigurationRevision
								: Op == EOp::ResourceRevision ? V->ResourceRevision
															  : V->FormatterRevision;
							return true;
						}
					case EOp::Template:
						{

							Out = Make(FI18nMessageTemplate{});
							return true;
						}
					case EOp::Resolved:
						{

							Out = Make(FI18nResolvedText{});
							return true;
						}
					case EOp::Pattern:
					case EOp::FormatId:
					case EOp::MatchedLanguage:
					case EOp::ResultValue:
					case EOp::ResultVersion:
						{
							if (const auto* V = Get<FI18nMessageTemplate>(A[0]))
							{
								if (!V->Valid) return Invalid();
								if (Op == EOp::Pattern)
								{
									Out = V->Pattern;
								}
								else if (Op == EOp::FormatId)
								{
									Out = V->FormatId;
								}

								else if (Op == EOp::MatchedLanguage)
								{
									Out = V->MatchedLanguage;
								}
								else
								{
									Out = Make(V->Version);
								}
								return true;
							}
							const auto* V = Get<FI18nResolvedText>(A[0]);
							if (!V || !V->Valid) return Invalid();
							if (Op == EOp::ResultValue)
							{
								Out = V->Value;
							}
							else if (Op == EOp::MatchedLanguage)
							{
								Out = V->MatchedLanguage;
							}
							else
							{
								Out = Make(V->Version);
							}
							return true;
						}
					case EOp::Snapshot:
						{

							Out = Make(FI18nSnapshot{});
							return true;
						}
					case EOp::Context:
						{
							if (!C.I18nServices) return Invalid();
							FI18nConfiguration Config;
							if (C.ArgCount)
							{
								const auto* P = Get<FI18nConfiguration>(A[0]);
								if (!P) return Invalid();
								Config = *P;
							}
							std::shared_ptr<FI18nContext> R;
							if (!CreateContext(Config, C.I18nServices->GetI18nServices(), R, E)) return false;
							Out = Make(std::move(R));
							return true;
						}
					case EOp::Configure:
					case EOp::SetLanguage:
					case EOp::SetFallback:
					case EOp::SetLocale:
					case EOp::SetOptions:
					case EOp::CloneContext:
					case EOp::Capture:
						{
							const auto* V = Get<std::shared_ptr<FI18nContext>>(A[0]);
							if (!V || !*V) return Invalid();
							if (Op == EOp::Capture)
							{
								Out = Make((*V)->CaptureSnapshot());
								return true;
							}
							if (Op == EOp::CloneContext)
							{
								std::shared_ptr<FI18nContext> R;
								if (!(*V)->Clone(R, E)) return false;
								Out = Make(std::move(R));
								return true;
							}
							if (Op == EOp::Configure)
							{
								const auto* P = Get<FI18nConfiguration>(A[1]);
								return P ? (*V)->Configure(*P, E) : Invalid();
							}
							if (Op == EOp::SetLanguage)
							{
								return (*V)->SetLanguage(std::get<std::string>(A[1]), E);
							}
							if (Op == EOp::SetLocale)
							{
								return (*V)->SetFormatLocale(std::get<std::string>(A[1]), E);
							}
							if (Op == EOp::SetOptions)
							{
								const auto* P = Get<FI18nArguments>(A[1]);
								return P ? (*V)->SetFormatOptions(*P, E) : Invalid();
							}
							std::vector<std::string> R;
							if (!ReadStrings(A[1], R, E)) return false;
							return (*V)->SetFallbackLanguages(std::move(R), E);
						}
					case EOp::ViewConfiguration:
					case EOp::ViewVersion:
					case EOp::Lookup:
					case EOp::Resolve:
					case EOp::ResolveVersioned:
					case EOp::Format:
						{
							FI18nSnapshot View;
							if (const auto* V = Get<FI18nSnapshot>(A[0]))
							{
								View = *V;
							}

							else if (const auto* ContextValue = Get<std::shared_ptr<FI18nContext>>(A[0]);
								ContextValue && *ContextValue)
							{
								View = (*ContextValue)->CaptureSnapshot();
							}

							if (!View.IsValid()) return Invalid();
							if (Op == EOp::ViewConfiguration)
							{
								Out = Make(View.Configuration());
								return true;
							}
							if (Op == EOp::ViewVersion)
							{
								Out = Make(View.Version());
								return true;
							}
							if (!C.I18nServices) return Invalid();
							const auto Work = C.I18nServices->GetI18nWork();
							if (Op == EOp::Lookup)
							{
								FI18nMessageTemplate R;
								if (!View.TryLookup(
										std::get<std::string>(A[1]), std::get<std::string>(A[2]), R, E, Work))
								{
									return false;
								}
								Out = Make(std::move(R));
								return true;
							}
							if (Op == EOp::Format)
							{
								const auto* P = Get<FI18nArguments>(A[2]);
								if (!P) return Invalid();
								std::string R;
								if (!View.TryFormat(std::get<std::string>(A[1]), *P, R, E, Work,
										C.ArgCount == 4 ? std::string_view(std::get<std::string>(A[3]))
														: "vero.named"))
								{
									return false;
								}
								Out = std::move(R);
								return true;
							}
							const auto* P = Get<FI18nText>(A[1]);
							if (!P) return Invalid();
							FI18nResolvedText R;
							if (!View.TryResolve(*P, R, E, Work)) return false;
							if (Op == EOp::Resolve)
							{
								Out = std::move(R.Value);
							}
							else
							{
								Out = Make(std::move(R));
							}
							return true;
						}
					case EOp::Normalize:
						{
							std::string R;
							if (!NormalizeLanguageTag(std::get<std::string>(A[0]), R, E)) return false;
							Out = std::move(R);
							return true;
						}

					default:
						{
							break;
						}
					}
					return Invalid();
				});
			return Ok || ErrorOutput(C, E, Error);
		}

		FHostBindingRegistry BuildCatalog()
		{
			FHostBindingRegistry R;
			FHostTypeId Ids[10];
			std::string Error;
			const auto Need = [&](bool Ok)
			{
				if (!Ok)
				{
					throw std::logic_error(Error);
				}
			};
			for (unsigned I = 0; I < 10; ++I)
			{
				Need(R.RegisterType(Names[I],
					I == 8		 ? EHostTypeKind::Class
						: I == 9 ? EHostTypeKind::Enum
								 : EHostTypeKind::Struct,
					Ids[I], Error));
			}

			Need(R.SetEnumScoped(Ids[9], true, Error));
			const char* KindNames[] = {"string_value", "bool_value", "int8_value", "uint8_value",
				"int16_value", "uint16_value", "int_value", "unsigned_value", "int64_value", "uint64_value",
				"float_value", "double_value", "custom_value"};
			for (unsigned I = 0; I < 13; ++I)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = KindNames[I];
				M.EnumValue = I;
				Need(R.RegisterMember(Ids[9], std::move(M), Error));
			}
			const auto Add = [&](unsigned T, std::string Name, EOp Op, ENativeType Ret,
								 std::vector<ENativeType> Params = {}, std::string HostRet = {},
								 std::vector<std::string> HostParams = {}, bool Mutate = false,
								 EHostMemberKind Kind = EHostMemberKind::Method)
			{
				FHostMemberDesc M;
				M.Kind = Kind;
				M.Name = std::move(Name);
				M.ReturnType = Ret;
				M.ParamTypes = std::move(Params);
				M.ReturnHostTypeName = std::move(HostRet);
				M.ParamHostTypeNames = std::move(HostParams);
				M.bConstMethod = !Mutate;
				M.bMutableThis = Mutate && T != 8;
				M.Callback = Member;
				M.UserData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Op));
				M.ParamElementTypeNames.resize(M.ParamTypes.size());
				for (std::size_t I = 0; I < M.ParamTypes.size(); ++I)
				{
					if (M.ParamTypes[I] == ENativeType::Array)
					{
						M.ParamElementTypeNames[I] = "string";
					}
				}

				if (Ret == ENativeType::Array)
				{
					M.ReturnElementTypeName = "string";
				}

				Need(R.RegisterMember(Ids[T], std::move(M), Error));
			};
			const EOp Ctors[] = {EOp::Argument, EOp::Arguments, EOp::Configuration, EOp::Text, EOp::Version,
				EOp::Template, EOp::Resolved, EOp::Snapshot, EOp::Context};
			for (unsigned I = 0; I < 9; ++I)
			{
				Add(I, "", Ctors[I], ENativeType::HostObject, {}, Names[I], {}, false,
					EHostMemberKind::Constructor);
				if (I == 8)
				{
					continue;
				}

				Need(R.RegisterTypeClone(Ids[I], Clone, nullptr, Error));
				if (I <= 4)
				{
					Need(R.RegisterTypeEquals(Ids[I], Equals, nullptr, Error));
				}

				if (I == 0 || I == 1 || I == 3 || I == 4)
				{
					for (unsigned N = 0; N < 2; ++N)
					{
						FHostMemberDesc M;
						M.Name = N ? "operator!=" : "operator==";
						M.ReturnType = ENativeType::Bool;
						M.ParamTypes = {ENativeType::HostObject};
						M.ParamHostTypeNames = {Names[I]};
						M.bConstMethod = true;
						M.Callback = N ? NotEquals : Equals;
						Need(R.RegisterOperator(Ids[I], std::move(M), Error));
					}
				}
			}
			const ENativeType Scalars[] = {ENativeType::String, ENativeType::Bool, ENativeType::Int8,
				ENativeType::UInt8, ENativeType::Int16, ENativeType::UInt16, ENativeType::Int,
				ENativeType::Unsigned, ENativeType::Int64, ENativeType::UInt64, ENativeType::Float,
				ENativeType::Double};
			for (auto T : Scalars)
			{
				Add(0, "", EOp::Scalar, ENativeType::HostObject, {T}, Names[0], {}, false,
					EHostMemberKind::Constructor);
			}

			Add(0, "kind", EOp::Kind, ENativeType::Enum, {}, Names[9]);
			Add(0, "as_string", EOp::AsString, ENativeType::String);
			Add(0, "as_bool", EOp::AsBool, ENativeType::Bool);
			Add(0, "as_int64", EOp::AsInt64, ENativeType::Int64);
			Add(0, "as_uint64", EOp::AsUInt64, ENativeType::UInt64);
			Add(0, "as_float", EOp::AsFloat, ENativeType::Float);
			Add(0, "as_double", EOp::AsDouble, ENativeType::Double);
			Add(1, "set", EOp::Set, ENativeType::Void, {ENativeType::String, ENativeType::HostObject}, {},
				{"", Names[0]}, true);
			Add(1, "get", EOp::Get, ENativeType::HostObject, {ENativeType::String}, Names[0]);
			Add(1, "contains", EOp::Contains, ENativeType::Bool, {ENativeType::String});
			Add(1, "remove", EOp::Remove, ENativeType::Void, {ENativeType::String}, {}, {}, true);
			Add(1, "clear", EOp::Clear, ENativeType::Void, {}, {}, {}, true);
			Add(1, "size", EOp::Size, ENativeType::Int64);
			Add(1, "names", EOp::Names, ENativeType::Array);
			const char* Props[] = {"language", "fallback_languages", "format_locale", "format_options"};
			const EOp Getters[] = {EOp::Language, EOp::Fallback, EOp::Locale, EOp::Options};
			const EOp Setters[] = {EOp::SetLanguageProperty, EOp::SetFallbackProperty, EOp::SetLocaleProperty,
				EOp::SetOptionsProperty};
			const ENativeType PropTypes[] = {
				ENativeType::String, ENativeType::Array, ENativeType::String, ENativeType::HostObject};
			for (unsigned I = 0; I < 4; ++I)
			{
				Add(2, Props[I], Getters[I], PropTypes[I], {}, I == 3 ? Names[1] : "", {}, false,
					EHostMemberKind::PropertyGetter);
				Add(2, Props[I], Setters[I], ENativeType::Void, {PropTypes[I]}, {},
					I == 3 ? std::vector<std::string>{Names[1]} : std::vector<std::string>{}, true,
					EHostMemberKind::PropertySetter);
			}
			Add(3, "", EOp::LiteralText, ENativeType::HostObject, {ENativeType::String}, Names[3], {}, false,
				EHostMemberKind::Constructor);
			Add(3, "message_key", EOp::Message, ENativeType::HostObject,
				{ENativeType::String, ENativeType::String}, Names[3], {}, false,
				EHostMemberKind::StaticMethod);
			Add(3, "message", EOp::Message, ENativeType::HostObject,
				{ENativeType::String, ENativeType::String, ENativeType::HostObject}, Names[3],
				{"", "", Names[1]}, false, EHostMemberKind::StaticMethod);
			Add(3, "is_literal", EOp::IsLiteral, ENativeType::Bool);
			Add(3, "literal", EOp::Literal, ENativeType::String);
			Add(3, "domain", EOp::Domain, ENativeType::String);
			Add(3, "key", EOp::Key, ENativeType::String);
			Add(3, "params", EOp::Params, ENativeType::HostObject, {}, Names[1]);
			Add(4, "context_id", EOp::ContextId, ENativeType::UInt64, {}, {}, {}, false,
				EHostMemberKind::PropertyGetter);
			Add(4, "configuration_revision", EOp::ConfigRevision, ENativeType::UInt64, {}, {}, {}, false,
				EHostMemberKind::PropertyGetter);
			Add(4, "resource_revision", EOp::ResourceRevision, ENativeType::UInt64, {}, {}, {}, false,
				EHostMemberKind::PropertyGetter);
			Add(4, "formatter_revision", EOp::FormatterRevision, ENativeType::UInt64, {}, {}, {}, false,
				EHostMemberKind::PropertyGetter);
			Add(5, "pattern", EOp::Pattern, ENativeType::String);
			Add(5, "format_id", EOp::FormatId, ENativeType::String);
			for (unsigned T : {5u, 6u})
			{
				Add(T, "matched_language", EOp::MatchedLanguage, ENativeType::String);
				Add(T, "version", EOp::ResultVersion, ENativeType::HostObject, {}, Names[4]);
			}
			Add(6, "value", EOp::ResultValue, ENativeType::String);
			for (unsigned T : {7u, 8u})
			{
				Add(T, "configuration", EOp::ViewConfiguration, ENativeType::HostObject, {}, Names[2]);
				Add(T, "version", EOp::ViewVersion, ENativeType::HostObject, {}, Names[4]);
				Add(T, "resolve", EOp::Resolve, ENativeType::String, {ENativeType::HostObject}, {},
					{Names[3]});
				Add(T, "resolve_versioned", EOp::ResolveVersioned, ENativeType::HostObject,
					{ENativeType::HostObject}, Names[6], {Names[3]});
				Add(T, "format", EOp::Format, ENativeType::String,
					{ENativeType::String, ENativeType::HostObject}, {}, {"", Names[1]});
				Add(T, "format_with", EOp::Format, ENativeType::String,
					{ENativeType::String, ENativeType::HostObject, ENativeType::String}, {},
					{"", Names[1], ""});
			}
			Add(7, "lookup", EOp::Lookup, ENativeType::HostObject, {ENativeType::String, ENativeType::String},
				Names[5]);
			Add(8, "", EOp::Context, ENativeType::HostObject, {ENativeType::HostObject}, Names[8], {Names[2]},
				false, EHostMemberKind::Constructor);
			Add(8, "snapshot", EOp::Capture, ENativeType::HostObject, {}, Names[7]);
			Add(8, "clone", EOp::CloneContext, ENativeType::HostObject, {}, Names[8]);
			Add(8, "configure", EOp::Configure, ENativeType::Void, {ENativeType::HostObject}, {}, {Names[2]},
				true);
			Add(8, "set_language", EOp::SetLanguage, ENativeType::Void, {ENativeType::String}, {}, {}, true);
			Add(8, "set_fallback_languages", EOp::SetFallback, ENativeType::Void, {ENativeType::Array}, {},
				{}, true);
			Add(8, "set_format_locale", EOp::SetLocale, ENativeType::Void, {ENativeType::String}, {}, {},
				true);
			Add(8, "set_format_options", EOp::SetOptions, ENativeType::Void, {ENativeType::HostObject}, {},
				{Names[1]}, true);
			FHostFunctionDesc F;
			F.Name = "std.i18n.normalize_language_tag";
			F.ReturnType = ENativeType::String;
			F.ParamTypes = {ENativeType::String};
			Need(R.RegisterNative(std::move(F), Member,
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(EOp::Normalize)), Error));
			return R;
		}
		class FCatalog final : public IHostBindingCatalog
		{
		public:

			//~ Begin IHostBindingCatalog Interface
			const std::vector<FNativeFunction>& GetNativeFunctions() const override
			{
				return Source.GetNativeFunctions();
			}
			const std::vector<FHostFunctionDesc>& GetFunctions() const override
			{
				return Source.GetFunctions();
			}
			const std::vector<FHostTypeDesc>& GetTypes() const override
			{
				return Types;
			}
			const FHostFunctionDesc* FindFunction(std::string_view N) const override
			{
				return Source.FindFunction(N);
			}
			const FHostTypeDesc* FindType(std::string_view N) const override
			{
				for (const auto& T : Types)
				{
					if (T.Name == N) return &T;
				}
				return nullptr;
			}
			const FHostTypeDesc* FindType(FHostTypeId I) const override
			{
				for (const auto& T : Types)
				{
					if (T.Id == I) return &T;
				}
				return nullptr;
			}
			//~ End IHostBindingCatalog Interface

			FCatalog() : Source(BuildCatalog()), Types(Source.GetTypes())
			{
				for (unsigned I = 0; I < Types.size(); ++I)
				{
					Types[I].Id.Value = TypeIdBase + I;
				}
			}

		private:
			FHostBindingRegistry Source;
			std::vector<FHostTypeDesc> Types;
		};
	}
	const IHostBindingCatalog& GetBindingCatalog()
	{
		static const FCatalog Catalog;
		return Catalog;
	}
	#define VS_I18N_SCRIPT_VALUE(T)                                                                              \
		bool ToScriptValue(const T& V, FValue& Out, FI18nError& E)                                               \
		{                                                                                                        \
			return Detail::Boundary(E,                                                                           \
				[&]()                                                                                            \
				{                                                                                                \
					Out = Make(V);                                                                               \
					return true;                                                                                 \
				});                                                                                              \
		}                                                                                                        \
		bool FromScriptValue(const FValue& V, T& Out, FI18nError& E)                                             \
		{                                                                                                        \
			return Detail::Boundary(E,                                                                           \
				[&]()                                                                                            \
				{                                                                                                \
					const auto* P = Get<T>(V);                                                                   \
					if (!P) return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid i18n value protocol");    \
					Out = *P;                                                                                    \
					return true;                                                                                 \
				});                                                                                              \
		}
	VS_I18N_SCRIPT_VALUE(FI18nArgument)
	VS_I18N_SCRIPT_VALUE(FI18nArguments)
	VS_I18N_SCRIPT_VALUE(FI18nConfiguration)
	VS_I18N_SCRIPT_VALUE(FI18nVersion)
	VS_I18N_SCRIPT_VALUE(FI18nMessageTemplate)
	VS_I18N_SCRIPT_VALUE(FI18nResolvedText)
	VS_I18N_SCRIPT_VALUE(FI18nSnapshot)
	#undef VS_I18N_SCRIPT_VALUE
	bool ToScriptValue(const FI18nText& V, FValue& Out, FI18nError& E)
	{
		return Detail::Boundary(E,
			[&]()
			{
				Out = Make(V);
				return true;
			});
	}
	bool ToScriptValue(std::shared_ptr<FI18nContext> V, FValue& Out, FI18nError& E)
	{
		return Detail::Boundary(E,
			[&]()
			{
				if (!V) return Detail::Fail(E, EI18nStatus::InvalidState, "null i18n context");
				Out = Make(std::move(V));
				return true;
			});
	}
	bool FromScriptValue(const FValue& V, FI18nText& Out, FI18nError& E)
	{
		return Detail::Boundary(E,
			[&]()
			{
				const auto* P = Get<FI18nText>(V);
				if (!P) return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid text value protocol");
				Out = *P;
				return true;
			});
	}
	bool FromScriptValue(const FValue& V, std::shared_ptr<FI18nContext>& Out, FI18nError& E)
	{
		return Detail::Boundary(E,
			[&]()
			{
				const auto* P = Get<std::shared_ptr<FI18nContext>>(V);
				if (!P || !*P)
				{
					return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid context value protocol");
				}
				Out = *P;
				return true;
			});
	}
}
