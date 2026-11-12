// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptJsonMappingScript.h"
#include "VeroScriptAst.h"
#include <sstream>
#include <map>
#include <set>
#include <cstring>
#include <utility>

namespace VeroScript::Json::Binding
{
	static std::string Literal(std::string_view Value)
	{
		FJsonValue Text;
		FError Error;
		std::string Out;
		if (!FJsonValue::TryString(Value, Text, Error) || !TryWrite(Text, Out, Error))
		{
			throw std::runtime_error("Invalid JSON mapping string literal");
		}
		return Out;
	}
	static const FDeclarationAnnotation* Annotation(const FDeclMeta& Metadata, std::string_view Name)
	{
		for (const auto& Item : Metadata.Annotations)
		{
			std::string_view N = Item.Name;
			if (!N.empty() && N.front() == '@')
			{
				N.remove_prefix(1);
			}
			if (N == Name)
			{
				return &Item;
			}
		}
		return nullptr;
	}
	static std::string Argument(const FDeclarationAnnotation& A, const char* Name)
	{
		for (const auto& P : A.Arguments)
		{
			if (P.Name == Name && P.Kind == EAnnotationArgumentKind::Scalar &&
			    P.ScalarKind == EAnnotationScalarKind::String)
			{
				return P.Value;
			}
		}
		throw std::runtime_error("JSON annotation requires a string argument named " + std::string(Name));
	}
	static std::string Callable(std::string Name)
	{
		if (Name.empty())
		{
			throw std::runtime_error("JSON converter or factory name cannot be empty");
		}
		for (const unsigned char C : Name)
		{
			if (!(C == '_' || C == ':' || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')))
			{
				throw std::runtime_error("JSON converter or factory must name a qualified script type");
			}
		}
		return Name;
	}
	struct FMappingField
	{
		const FAnnotationDeclarationView* Declaration;
		std::string Name, Converter, Setter, TypeName;
		bool Ignore = false, Required = false, SerializeOnly = false, DeserializeOnly = false;
	};
	struct FMappingSignature
	{
		std::string Type, Parameters = "JsonGuard=int";
		std::map<std::string, std::string> Bindings;
	};
	class FMappingProcessor final : public ICompileProcessor
	{
	public:
		explicit FMappingProcessor(INativeExtensionHost& Value) : Host(Value)
		{
		}
		static bool HasModuleScope(const FAnnotationDeclarationView& D)
		{
			// These helpers are module-level free functions. Do not emit internal
			// identities of local or nested types into their signatures.
			return !D.bLocal && !D.bNested;
		}
		static std::vector<FAnnotationTemplateParameterView> TemplateParameters(const FAnnotationDeclarationView& D)
		{
			if (!D.TemplateParameters.empty())
			{
				return D.TemplateParameters;
			}
			// Type-only AST producers can still supply the legacy transport view.
			std::vector<FAnnotationTemplateParameterView> Result;
			for (std::size_t I = 0; I < D.TypeParameters.size(); ++I)
			{
				FAnnotationTemplateParameterView P;
				P.Name = D.TypeParameters[I];
				if (I < D.TypeParameterDefaults.size())
				{
					P.DefaultSource = D.TypeParameterDefaults[I];
					P.bHasDefault = !P.DefaultSource.empty();
				}
				Result.push_back(std::move(P));
			}
			return Result;
		}
		static bool IdentifierPart(unsigned char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
		}
		static std::size_t QuotedEnd(std::string_view Text, std::size_t Start)
		{
			const char Quote = Text[Start++];
			while (Start < Text.size())
			{
				const char Ch = Text[Start++];
				if (Ch == '\\' && Start < Text.size())
				{
					++Start;
				}
				else if (Ch == Quote)
				{
					break;
				}
			}
			return Start;
		}
		template <typename TTransform>
		static std::string TransformTypeNames(std::string_view Text, TTransform Transform)
		{
			std::string Result;
			Result.reserve(Text.size());
			for (std::size_t At = 0; At < Text.size();)
			{
				const auto Start = At;
				if (Text[At] == '\'' || Text[At] == '"')
				{
					At = QuotedEnd(Text, At);
					Result.append(Text.substr(Start, At - Start));
				}
				else if (IdentifierPart(static_cast<unsigned char>(Text[At])) || Text[At] == '.' ||
				    Text.substr(At, 2) == "::")
				{
					auto Previous = Start;
					while (Previous != 0 && std::string_view(" \t\r\n").find(Text[Previous - 1]) != Text.npos)
					{
						--Previous;
					}
					const bool StartsScope = Text[At] == '.' || Text[At] == ':';
					const bool FollowsScope = (Previous != 0 && Text[Previous - 1] == '.') ||
					    (Previous >= 2 && Text.substr(Previous - 2, 2) == "::");
					auto OwnerEnd = Previous;
					if (!StartsScope && FollowsScope)
					{
						while (OwnerEnd != 0 &&
						    std::string_view(".: \t\r\n").find(Text[OwnerEnd - 1]) != Text.npos)
						{
							--OwnerEnd;
						}
					}
					const bool Member = (StartsScope || FollowsScope) && OwnerEnd != 0 &&
					    (IdentifierPart(static_cast<unsigned char>(Text[OwnerEnd - 1])) ||
					        std::string_view(">)]").find(Text[OwnerEnd - 1]) != Text.npos);
					while (At < Text.size())
					{
						if (IdentifierPart(static_cast<unsigned char>(Text[At])) || Text[At] == '.')
						{
							++At;
						}
						else if (Text.substr(At, 2) == "::")
						{
							At += 2;
						}
						else
						{
							break;
						}
					}
					const std::string Token(Text.substr(Start, At - Start));
					auto Next = At;
					while (Next < Text.size() && std::string_view(" \t\r\n").find(Text[Next]) != Text.npos)
					{
						++Next;
					}
					const bool Prefix = Text.substr(Next, 2) == "::" || (Next < Text.size() && Text[Next] == '.');
					if (Member || Token == "::" || Token == ".")
					{
						// A tail such as Outer<T>::Member belongs to its preceding
						// type path; it is not a new lookup in the helper namespace.
						Result += Token;
					}
					else if (FollowsScope)
					{
						const auto Name = Transform("::" + Token, Prefix);
						Result += Name.rfind("::", 0) == 0 ? Name.substr(2) : Name;
					}
					else
					{
						Result += Transform(Token, Prefix);
					}
				}
				else
				{
					Result += Text[At++];
				}
			}
			return Result;
		}
		static FMappingSignature MappingSignature(
		    const FCompileProcessingContext& C, const FAnnotationDeclarationView& D)
		{
			FMappingSignature Result;
			Result.Type = "::" + D.Name;
			const auto Declared = TemplateParameters(D);
			if (!Declared.empty())
			{
				// Never reuse source parameter names as helper bindings: a value
				// parameter named input/context/etc. would shadow a runtime local.
				constexpr std::string_view Prefix = "_VeroJsonParameter";
				std::set<std::string> Reserved;
				auto Reserve = [&](std::string_view Text)
				{
					for (auto At = Text.find(Prefix); At != Text.npos; At = Text.find(Prefix, At + 1))
					{
						if (At != 0 && IdentifierPart(static_cast<unsigned char>(Text[At - 1])))
						{
							continue;
						}
						auto End = At + Prefix.size();
						while (End < Text.size() && IdentifierPart(static_cast<unsigned char>(Text[End])))
						{
							++End;
						}
						Reserved.emplace(Text.substr(At, End - At));
					}
				};
				for (const auto& P : Declared)
				{
					Reserved.insert(P.Name);
				}
				for (const auto& Declaration : C.Declarations)
				{
					if (C.CheckCancelled)
					{
						C.CheckCancelled();
					}
					Reserve(Declaration.Name);
					Reserve(Declaration.TypeName);
					for (const auto& Base : Declaration.BaseTypes)
					{
						Reserve(Base);
					}
					for (const auto& P : Declaration.TemplateParameters)
					{
						Reserve(P.ValueTypeName);
					}
					for (const auto& A : Declaration.Metadata.Annotations)
					{
						for (const auto& Argument : A.Arguments)
						{
							if (Argument.Name == "type")
							{
								Reserve(Argument.Value);
							}
						}
					}
				}
				std::size_t Next = 0;
				for (const auto& P : Declared)
				{
					std::string Name;
					do
					{
						Name = std::string(Prefix) + std::to_string(Next++);
					}
					while (!Reserved.insert(Name).second);
					Result.Bindings.emplace(P.Name, std::move(Name));
				}
				Result.Type += '<';
				for (const auto& P : Declared)
				{
					if (Result.Type.back() != '<')
					{
						Result.Type += ',';
					}
					const auto& Name = Result.Bindings.at(P.Name);
					Result.Type += Name;
					if (P.bValue)
					{
						const auto ValueType = QualifyType(C, D, P.ValueTypeName, &Result.Bindings);
						// JsonGuard has a default, so every following parameter also
						// needs one. Calls deduce the actual value from the typed input.
						Result.Parameters += "," + ValueType + " " + Name + "=cast<" + ValueType + ">(0)";
					}
					else
					{
						Result.Parameters += "," + Name + "=int";
					}
				}
				Result.Type += '>';
			}
			return Result;
		}
		static std::string Identity(const FAnnotationDeclarationView& D)
		{

			if (D.bFromPackage)
			{
				if (const auto* A = Annotation(D.Metadata, "VeroJsonMappingIdentity"))
				{
					const auto Name = Argument(*A, "value");
					if (Name.empty() || Name.size() > 1024 * 1024 || Name.size() % 2 ||
					    std::any_of(Name.begin(),
					        Name.end(),
					        [](unsigned char Ch)
					{
						return !((Ch >= '0' && Ch <= '9') || (Ch >= 'a' && Ch <= 'f'));
					}))
					{
						throw std::runtime_error("Invalid packaged JSON mapping identity");
					}
					return Name;
				}
			}

			static constexpr char Digits[] = "0123456789abcdef";
			std::string Result;
			for (const unsigned char C : D.Id)
			{
				Result += Digits[C >> 4];
				Result += Digits[C & 15];
			}
			return Result;
		}

		static std::string Lookup(
		    const FCompileProcessingContext& C, const FAnnotationDeclarationView& Owner, std::string Name,
		    bool bNamespacePrefix = false)
		{
			// Declaration snapshots store namespace separators as dots.
			for (auto At = Name.find("::"); At != Name.npos; At = Name.find("::", At + 1))
			{
				Name.replace(At, 2, ".");
			}
			const bool Global = !Name.empty() && Name.front() == '.';
			if (Global)
			{
				Name.erase(0, 1);
			}
			const auto Find = [&](const std::string& Candidate) -> const FAnnotationDeclarationView*
			{
				const FAnnotationDeclarationView* Imported = nullptr;
				for (const auto& D : C.Declarations)
				{
					if (C.CheckCancelled)
					{
						C.CheckCancelled();
					}
					// Value arguments also carry names from the declaration's scope.
					if (D.Name != Candidate ||
					    (D.Target != EAnnotationTarget::Struct && D.Target != EAnnotationTarget::Class &&
					        D.Target != EAnnotationTarget::Enum && D.Target != EAnnotationTarget::Interface &&
					        D.Target != EAnnotationTarget::TypeAlias &&
					        !(bNamespacePrefix && D.Target == EAnnotationTarget::Namespace) &&
					        !(D.Target == EAnnotationTarget::GlobalVariable && (D.bIsConst || D.bConstexpr)) &&
					        !(D.Target == EAnnotationTarget::Function && D.bConstexpr)))
					{
						continue;
					}
					if (D.ModulePath == Owner.ModulePath)
					{
						return &D;
					}
					if (IsVisible(C, Owner, D))
					{
						Imported = &D;
					}
				}
				return Imported;
			};
			// Snapshot type paths are already qualified. Value expressions and
			// namespace prefixes retain the declaration's lexical lookup order.
			if (!Global && Name.find('.') != Name.npos)
			{
				if (const auto* Declaration = Find(Name);
				    Declaration && Declaration->Target != EAnnotationTarget::Namespace &&
				    Declaration->Target != EAnnotationTarget::GlobalVariable &&
				    Declaration->Target != EAnnotationTarget::Function)
				{
					return "::" + Declaration->Name;
				}
			}
			std::string Scope =
			    Global ? std::string{}
			           : Owner.Name.substr(0, Owner.Name.rfind('.') == Owner.Name.npos ? 0 : Owner.Name.rfind('.'));
			for (;;)
			{
				const auto Candidate = Scope.empty() ? Name : Scope + "." + Name;
				if (const auto* Declaration = Find(Candidate))
				{
					return "::" + Declaration->Name;
				}
				if (Scope.empty())
				{
					break;
				}
				const auto At = Scope.rfind('.');
				Scope = At == Scope.npos ? std::string{} : Scope.substr(0, At);
			}
			if (!Global)
			{
				const auto Dot = Name.rfind('.');
				if (Dot != Name.npos)
				{
					const auto Prefix = Lookup(C, Owner, Name.substr(0, Dot), true);
					if (Prefix.rfind("::", 0) == 0)
					{
						return Prefix + Name.substr(Dot);
					}
				}
			}
			return Global ? "::" + Name : Name;
		}
		static std::string QualifyType(
		    const FCompileProcessingContext& C, const FAnnotationDeclarationView& Owner, std::string_view Text,
		    const std::map<std::string, std::string>* Bindings = nullptr)
		{
			return TransformTypeNames(Text, [&](const std::string& Token, bool Prefix)
			{
				const auto Head = Token.substr(0, Token.find_first_of(".:"));
				const bool IsParameter =
				    std::find(Owner.TypeParameters.begin(), Owner.TypeParameters.end(), Head) != Owner.TypeParameters.end() ||
				    std::any_of(Owner.TemplateParameters.begin(), Owner.TemplateParameters.end(),
				        [&](const auto& P)
				{
					return P.Name == Head;
				});
				if (IsParameter && Bindings)
				{
					if (const auto Found = Bindings->find(Head); Found != Bindings->end())
					{
						return Found->second + Token.substr(Head.size());
					}
				}
				return IsParameter ? Token : Lookup(C, Owner, Token, Prefix);
			});
		}
		static const FAnnotationDeclarationView* FindTypeAlias(
		    const FCompileProcessingContext& C, const FAnnotationDeclarationView& Owner, const std::string& Type)
		{
			const auto Name = Type.rfind("::", 0) == 0 ? Type.substr(2) : Type;
			const FAnnotationDeclarationView* Imported = nullptr;
			for (const auto& D : C.Declarations)
			{
				if (C.CheckCancelled)
				{
					C.CheckCancelled();
				}
				if (D.Target != EAnnotationTarget::TypeAlias || D.Name != Name)
				{
					continue;
				}
				if (D.ModulePath == Owner.ModulePath)
				{
					return &D;
				}
				if (IsVisible(C, Owner, D))
				{
					Imported = &D;
				}
			}
			return Imported;
		}

		// A leaf containing only primitive-field structs needs no public converter
		// facade or container overloads. Keep the full support set for arbitrary
		// source, imported aliases, generics, annotations and executable members.
		static std::optional<std::set<std::string>> PrimitiveLeaf(const FCompileProcessingContext& C)
		{
			if (!C.Program || C.Program->EntryModulePath == C.ModuleId)
			{
				return {};
			}
			for (const auto& Import : C.Program->Imports)
			{
				if (Import.ImporterModulePath == C.ModuleId)
				{
					return {};
				}
			}
			std::set<std::string> Required;
			for (const auto& D : C.Declarations)
			{
				if (C.CheckCancelled)
				{
					C.CheckCancelled();
				}
				if (D.ModulePath != C.ModuleId || D.bGenerated)
				{
					continue;
				}
				if (!HasModuleScope(D) || !D.TemplateParameters.empty() || !D.TypeParameters.empty() ||
				    !D.BaseTypes.empty())
				{
					return {};
				}
				for (const auto& A : D.Metadata.Annotations)
				{
					if (A.Name != "VeroJsonMappingIdentity" && A.Name != "@VeroJsonMappingIdentity")
					{
						return {};
					}
				}
				if (D.Target == EAnnotationTarget::Struct || D.Target == EAnnotationTarget::Module ||
				    D.Target == EAnnotationTarget::Namespace)
				{
					continue;
				}
				if (D.Target != EAnnotationTarget::Field)
				{
					return {};
				}
				if (!D.DefaultValueSource.empty())
				{
					if (D.DefaultValueSource.size() > 1024)
					{
						return {};
					}
					FJsonValue LiteralValue;
					FError Error;
					if (!TryParse(D.DefaultValueSource, LiteralValue, Error))
					{
						return {};
					}
				}
				const auto& T = D.TypeName;
				if (T != "bool" && T != "int8" && T != "uint8" && T != "int16" && T != "uint16" && T != "int" &&
				    T != "uint32" && T != "int64" && T != "uint64" && T != "float" && T != "double" && T != "string")
				{
					return {};
				}
				Required.insert(T);
			}
			return Required;
		}
		static bool IsVisible(const FCompileProcessingContext& C, const FAnnotationDeclarationView& Owner,
		    const FAnnotationDeclarationView& D)
		{
			if (D.ModulePath == Owner.ModulePath || D.ModulePath.empty() || Owner.ModulePath.empty())
			{
				return true;
			}
			if (!C.Program)
			{
				return D.bExported;
			}
			if (C.Program->ProjectVisibleModulePaths.count(D.ModulePath) &&
			    C.Program->ProjectVisibleModulePaths.count(Owner.ModulePath))
			{
				return true;
			}
			// Namespace declarations are containers, not exported values or types.
			return (D.bExported || D.Target == EAnnotationTarget::Namespace) &&
			    VisibleModules(C, Owner.ModulePath).count(D.ModulePath);
		}
		static std::set<std::string> VisibleModules(const FCompileProcessingContext& C, std::string Module = {})
		{
			if (Module.empty())
			{
				Module = C.ModuleId;
			}
			std::set<std::string> Visible{Module};
			if (!C.Program)
			{
				return Visible;
			}
			for (const auto& Import : C.Program->Imports)
			{
				if (Import.ImporterModulePath == Module && !Import.ResolvedPath.empty())
				{
					Visible.insert(Import.ResolvedPath);
				}
			}
			bool Changed;
			do
			{
				Changed = false;
				for (const auto& Import : C.Program->Imports)
				{
					if (C.CheckCancelled)
					{
						C.CheckCancelled();
					}
					if (Import.bReExport && !Import.ResolvedPath.empty() && Visible.count(Import.ImporterModulePath))
					{
						Changed = Visible.insert(Import.ResolvedPath).second || Changed;
					}
				}
			}
			while (Changed);
			return Visible;
		}

		void Forward(const FCompileProcessingContext& Original, const FAnnotationDeclarationView& D,
		    FCompileProcessingOutput& Out)
		{
			auto C = Original;
			C.Declaration = nullptr;
			const auto Signature = MappingSignature(C, D);
			const auto& Type = Signature.Type;
			const auto& Parameters = Signature.Parameters;
			const auto Suffix = Identity(D);
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> bool mapping_encode(const " + Type +
			        "& input,json::value& output,json::mapping_context context,JsonGuard guard=0){return "
			        "veroscript_json_mapping::mapping_encode_" +
			        Suffix + "<int>(ref input,ref output,context);}}");
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters +
			        "> bool mapping_decode(json::value input," + Type +
			        "& output,json::mapping_context context,JsonGuard guard=0){return "
			        "veroscript_json_mapping::mapping_decode_" +
			        Suffix + "<int>(input,ref output,context);}}");
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> json::value mapping_schema(type<" +
			        Type +
			        "> tag,json::mapping_context context,JsonGuard guard=0){return "
			        "veroscript_json_mapping::mapping_schema_" +
			        Suffix + "<int>(tag,context);}}");
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> bool mapping_validate(type<" + Type +
			        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return "
			        "veroscript_json_mapping::mapping_validate_" +
			        Suffix + "<int>(tag,input,context);}}");
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> " + Type + " mapping_initial(type<" +
			        Type +
			        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return "
			        "veroscript_json_mapping::mapping_initial_" +
			        Suffix + "<int>(tag,input,context);}}");
		}
		void Emit(const FCompileProcessingContext& C, FCompileProcessingOutput& Out, std::string Source,
		    std::string Owner = {}, std::string Namespace = {})
		{
			if (C.Declaration)
			{
				const auto HeaderEnd = Source.find("guard=0){");
				for (const char* Name :
				    {"mapping_encode", "mapping_decode", "mapping_schema", "mapping_validate", "mapping_initial"})
				{
					const auto At = Source.find(std::string(" ") + Name + "(");
					if (At == Source.npos || At > HeaderEnd)
					{
						continue;
					}
					if (C.Declaration->bExported)
					{
						Source.insert(At + 1 + std::strlen(Name), "_" + Identity(*C.Declaration));
					}
					else if (const auto Export = Source.find(" export "); Export != Source.npos && Export < At)
					{
						Source.erase(Export + 1, 7);
					}
					break;
				}
			}

			FCompileGenerationOperation O;
			O.Kind = ECompileOperation::AddDeclaration;
			O.Phase = C.Phase;
			O.Scope = {C.ModuleId, std::move(Namespace), std::move(Owner), C.Snapshot};
			O.Source = std::move(Source);
			Host.SubmitCompileOperation(Out, std::move(O));
		}
		void Error(const FCompileProcessingContext& C, FCompileProcessingOutput& Out, std::string Message)
		{
			FScriptError Error;
			Error.Message = std::move(Message);
			Error.MessageId = "json.mapping.declaration";
			Error.MessageArgs = {Error.Message};
			if (C.Declaration)
			{
				Error.Location.FileLabel = C.Declaration->Location.FileLabel;
				Error.Location.Line = C.Declaration->Location.Line;
				Error.Location.Column = C.Declaration->Location.Column;
			}
			Host.ReportCompileDiagnostic(Out, std::move(Error));
		}
		#include "JsonPolymorphicMapping.inl"

		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& Out) override
		{
			if (C.CheckCancelled)
			{
				C.CheckCancelled();
			}
			if (C.Event == ECompileProcessingEvent::Module)
			{
				const auto Leaf = PrimitiveLeaf(C);
				const std::string_view Script = Leaf ? std::string_view{} : MappingScript,
				                       Marker = "// @json-declaration:";
				for (auto At = Script.find(Marker); At != Script.npos;)
				{
					const auto Begin = Script.find('\n', At), End = Script.find(Marker, Begin);
					const bool IsType = Script.substr(At + Marker.size(), Begin - At - Marker.size()) == "types";
					if (IsType == (C.Phase == ECompileProcessingPhase::Types))
					{
						Emit(C, Out, std::string(Script.substr(Begin + 1, End == Script.npos ? End : End - Begin - 1)));
					}
					At = End;
				}
				if (C.Phase == ECompileProcessingPhase::Types)
				{
					return;
				}
				std::ostringstream Source;
				for (const auto& Type : {"bool",
				         "int8",
				         "uint8",
				         "int16",
				         "uint16",
				         "int",
				         "uint32",
				         "int64",
				         "uint64",
				         "float",
				         "double",
				         "string"})
				{
					Source.str({});
					Source.clear();
					const std::string T = Type;
					if (Leaf && !Leaf->count(T))
					{
						continue;
					}
					const bool Signed = T == "int8" || T == "int16" || T == "int" || T == "int64";
					const bool Unsigned = T == "uint8" || T == "uint16" || T == "uint32" || T == "uint64";
					const auto Native = Signed ? "int64" : Unsigned ? "uint64"
					                                                : Type;
					Source << "namespace veroscript_json_mapping {template<JsonGuard=int> bool mapping_encode(const "
					       << T
					       << "& input,json::value& output,json::mapping_context context,JsonGuard guard=0){return "
					          "context.write_"
					       << Native << "(input,ref output);}}\n";
					Emit(C, Out, Source.str());
					Source.str({});
					Source.clear();
					Source << "namespace veroscript_json_mapping {template<JsonGuard=int> bool "
					          "mapping_decode(json::value input,"
					       << T << "& output,json::mapping_context context,JsonGuard guard=0){" << Native << " scalar="
					       << (T == "bool"            ? "false"
					                  : T == "string" ? "\"\""
					                                  : "0")
					       << ";"
					       << "if(!context.read_" << Native << "(input,ref scalar))return false;";
					if (T == "int8")
					{
						Source << "if(scalar < -128 || scalar > 127)return context.fail_range(\"JSON integer outside "
						          "int8\");";
					}
					if (T == "int16")
					{
						Source
						    << "if(scalar < -32768 || scalar > 32767)return context.fail_range(\"JSON integer outside "
						       "int16\");";
					}
					if (T == "int")
					{
						Source << "if(scalar < -2147483648i64 || scalar > 2147483647i64)return context.fail(\"JSON "
						          "integer outside int\");";
					}
					if (T == "uint8")
					{
						Source << "if(scalar > 255u64)return context.fail_range(\"JSON integer outside uint8\");";
					}
					if (T == "uint16")
					{
						Source << "if(scalar > 65535u64)return context.fail_range(\"JSON integer outside uint16\");";
					}
					if (T == "uint32")
					{
						Source << "if(scalar > 4294967295u64)return context.fail_range(\"JSON integer outside uint\");";
					}
					Source << "output=scalar;return true;}}\n";
					Emit(C, Out, Source.str());
					const std::map<std::string, std::pair<std::string, std::string>> Ranges = {
					    {"int8", {"-128", "127"}},
					    {"uint8", {"0", "255"}},
					    {"int16", {"-32768", "32767"}},
					    {"uint16", {"0", "65535"}},
					    {"int", {"-2147483648", "2147483647"}},
					    {"uint32", {"0", "4294967295"}},
					    {"int64", {"-9223372036854775808", "9223372036854775807"}},
					    {"uint64", {"0", "18446744073709551615"}}};
					std::string Schema = "{\"type\":\"" +
					    std::string(Signed || Unsigned ? "integer"
					            : T == "bool"          ? "boolean"
					            : T == "string"        ? "string"
					                                   : "number") +
					    "\"";
					if (const auto R = Ranges.find(T); R != Ranges.end())
					{
						Schema += ",\"minimum\":" + R->second.first + ",\"maximum\":" + R->second.second;
					}
					const auto Initial = T == "bool" ? "false" : T == "string" ? "\"\""
					                                                           : "0";
					Emit(C,
					    Out,
					    "namespace veroscript_json_mapping {template<JsonGuard=int> " + T + " mapping_initial(type<" +
					        T + "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return " +
					        Initial + ";}}");
					Emit(C,
					    Out,
					    "namespace veroscript_json_mapping {template<JsonGuard=int> bool mapping_validate(type<" + T +
					        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return true;}}");

					Schema += '}';
					Emit(C,
					    Out,
					    "namespace veroscript_json_mapping {template<JsonGuard=int> json::value mapping_schema(type<" +
					        T + "> tag,json::mapping_context context,JsonGuard guard=0){return json::parse(" +
					        Literal(Schema) + ");}}");
				}
				const auto Visible = VisibleModules(C);
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath != C.ModuleId && Visible.count(D.ModulePath) && D.bExported && HasModuleScope(D) &&
					    !Annotation(D.Metadata, "JsonPolymorphic") &&
					    (D.Target == EAnnotationTarget::Struct || D.Target == EAnnotationTarget::Class ||
					        D.Target == EAnnotationTarget::Enum))
					{
						Forward(C, D, Out);
						if (D.bFromPackage && !Annotation(D.Metadata, "VeroJsonMappingIdentity"))
						{
							auto Local = C;
							Local.Event = ECompileProcessingEvent::Declaration;
							Local.Declaration = &D;
							Process(Local, Out);
						}
					}
				}
				return;
			}

			if (C.Phase == ECompileProcessingPhase::Members && C.Declaration &&
			    Annotation(C.Declaration->Metadata, "JsonPolymorphic"))
			{
				// Polymorphic tables receive owned static members, so their own
				// lexical scope is retained rather than emitted into module helpers.
				const auto& D = *C.Declaration;
				if (!D.bGenerated && !D.bFromPackage)
				{
					Polymorphic(C, D, Out);
				}
				return;
			}
			if (C.Declaration && !HasModuleScope(*C.Declaration))
			{
				const auto& D = *C.Declaration;
				if (C.Phase == ECompileProcessingPhase::Members && !D.bGenerated && !D.bFromPackage &&
				    (Annotation(D.Metadata, "JsonConverter") || Annotation(D.Metadata, "JsonFactory")))
				{
					Error(C, Out, "Automatic JSON mapping requires a module- or namespace-scope type; "
					              "use json::custom_converter for local or nested types");
				}
				return;
			}

			if (C.Phase == ECompileProcessingPhase::Types && C.Declaration)
			{
				const auto& D = *C.Declaration;
				if (!D.bGenerated && !D.bFromPackage && D.bExported)
				{
					FDeclarationAnnotation A;
					A.Name = "@VeroJsonMappingIdentity";
					FAnnotationArgument Value;
					Value.Name = "value";
					Value.Kind = EAnnotationArgumentKind::Scalar;
					Value.ScalarKind = EAnnotationScalarKind::String;
					Value.Value = Identity(D);
					A.Arguments.push_back(std::move(Value));
					auto Annotations = D.Metadata.Annotations;
					Annotations.erase(std::remove_if(Annotations.begin(),
					                      Annotations.end(),
					                      [](const auto& Item)
					{
						return Item.Name == "VeroJsonMappingIdentity" ||
						    Item.Name == "@VeroJsonMappingIdentity";
					}),
					    Annotations.end());
					Annotations.push_back(std::move(A));
					FCompileGenerationOperation O;
					O.Kind = ECompileOperation::SetMetadata;
					O.Phase = C.Phase;
					O.TargetId = D.Id;
					O.Snapshot = C.Snapshot;
					O.Scope.ModuleId = C.ModuleId;
					O.Scope.Snapshot = C.Snapshot;
					O.Metadata.Annotations = std::move(Annotations);
					Host.SubmitCompileOperation(Out, std::move(O));
				}
				return;
			}
			if (C.Phase != ECompileProcessingPhase::Members || !C.Declaration)
			{
				return;
			}
			const auto& D = *C.Declaration;
			if (!D.bFromPackage && !D.bGenerated && D.bExported)
			{
				Forward(C, D, Out);
			}
			const bool LocalPackage = D.bFromPackage && !Annotation(D.Metadata, "VeroJsonMappingIdentity");
			if (D.Target == EAnnotationTarget::Enum && (!D.bFromPackage || LocalPackage) && !D.bGenerated &&
			    !Annotation(D.Metadata, "JsonConverter"))
			{
				std::vector<std::pair<std::string, std::string>> Members;
				std::set<std::string> Unique;
				for (const auto& M : C.Declarations)
				{
					if (M.OwnerId == D.Id && M.Target == EAnnotationTarget::EnumMember)
					{
						const auto* A = Annotation(M.Metadata, "JsonName");
						const auto Name = A ? Argument(*A, "value") : M.Name;
						if (!Unique.insert(Name).second)
						{
							Error(C, Out, "Duplicate JSON enum name: " + Name);
							return;
						}
						Members.push_back({M.Name, Name});
					}
				}
				bool Unsigned = false;
				if (C.Program)
				{
					for (const auto& E : C.Program->Enums)
					{
						if (E.Name == D.Name && E.ModulePath == D.ModulePath)
						{
							Unsigned = E.UnderlyingTypeName.rfind("uint", 0) == 0 ||
							    E.UnderlyingTypeName.rfind("unsigned", 0) == 0;
						}
					}
				}
				const std::string Number = Unsigned ? "uint64" : "int64";
				const std::string EnumType = "::" + D.Name;
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<JsonGuard=int> export " + EnumType +
				        " mapping_initial(type<" + EnumType +
				        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return cast<" +
				        EnumType + ">(0);}}");
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<JsonGuard=int> export bool mapping_validate(type<" +
				        EnumType +
				        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return true;}}");

				std::ostringstream S;
				S << "namespace veroscript_json_mapping {template<JsonGuard=int> export bool mapping_encode(const "
				  << EnumType << "& input,json::value& output,json::mapping_context context,JsonGuard guard=0){";
				for (const auto& M : Members)
				{
					S << "if(input==" << EnumType << "::" << M.first
					  << "){if(context.numeric_enums())return context.write_" << Number << "(cast<" << Number
					  << ">(input),ref output);return context.write_string(" << Literal(M.second) << ",ref output);}";
				}
				S << "return context.fail_range(\"Undefined JSON enum value\");}}";
				Emit(C, Out, S.str());
				S.str({});
				S.clear();
				S << "namespace veroscript_json_mapping {template<JsonGuard=int> export bool "
				     "mapping_decode(json::value input,"
				  << EnumType << "& output,json::mapping_context context,JsonGuard guard=0){if(context.numeric_enums()){"
				  << Number << " scalar=0;if(!context.read_" << Number << "(input,ref scalar))return false;";
				for (const auto& M : Members)
				{
					S << "if(scalar==cast<" << Number << ">(" << EnumType << "::" << M.first << ")){output=" << EnumType
					  << "::" << M.first << ";return true;}";
				}
				S << "}else{string scalar=\"\";if(!context.read_string(input,ref scalar))return false;";
				for (const auto& M : Members)
				{
					S << "if(scalar==" << Literal(M.second) << "){output=" << EnumType << "::" << M.first
					  << ";return true;}";
				}
				S << "}return context.fail_range(\"Undefined JSON enum value\");}}";
				Emit(C, Out, S.str());
				S.str({});
				S.clear();
				S << "namespace veroscript_json_mapping {template<JsonGuard=int> export json::value "
				     "mapping_schema(type<"
				  << EnumType
				  << "> tag,json::mapping_context context,JsonGuard guard=0){json::builder "
				     "choices=json::array_builder();json::value item=json::value();map<string,bool> "
				     "seen=map<string,bool>();";
				for (const auto& M : Members)
				{
					S << "{string identity=to_string(cast<" << Number << ">(" << EnumType << "::" << M.first << "));"
					  << "if(context.numeric_enums()){if(!seen.has(identity)){context.require(context.write_" << Number
					  << "(cast<" << Number << ">(" << EnumType << "::" << M.first
					  << "),ref item));choices.push(item);}}else if(context.schema_input() || "
					     "!seen.has(identity))choices.push(json::from_string("
					  << Literal(M.second) << "));seen[identity]=true;}";
				}
				S << "json::value values=choices.finish();if(values.size()==0)return "
				     "json::from_bool(false);json::value result=json::object();result.set(\"enum\",values);return "
				     "result;}}";
				Emit(C, Out, S.str());
				return;
			}
			if (D.Target != EAnnotationTarget::Struct && D.Target != EAnnotationTarget::Class &&
			    D.Target != EAnnotationTarget::Enum)
			{
				return;
			}
			if ((D.bFromPackage && !LocalPackage) || D.bGenerated)
			{
				return;
			}
			std::vector<FMappingField> Fields;
			std::set<std::string> Names;
			using FSubstitutions = std::map<std::string, std::string>;
			const auto Signature = MappingSignature(C, D);
			const auto& ThisType = Signature.Type;
			const auto& Parameters = Signature.Parameters;
			struct FOwner
			{
				const FAnnotationDeclarationView* Declaration;
				FSubstitutions Substitutions;
			};
			std::vector<FOwner> Owners;
			std::set<std::string> Visited;
			std::function<void(const FAnnotationDeclarationView&, FSubstitutions)> Collect =
			    [&](const auto& Owner, FSubstitutions Substitutions)
			{
				std::string Identity = Owner.Id;
				for (const auto& Pair : Substitutions)
				{
					Identity += "/" + Pair.first + "=" + Pair.second;
				}
				if (!Visited.insert(Identity).second)
				{
					return;
				}
				for (std::size_t Index = 0; Index < Owner.BaseTypes.size(); ++Index)
				{
					if (Index < Owner.BaseAccess.size() && Owner.BaseAccess[Index] != EMemberAccess::Public)
					{
						continue;
					}
					auto Base = QualifyType(C, Owner, Owner.BaseTypes[Index], &Substitutions);
					const auto* BaseOwner = &Owner;
					std::set<std::string> Aliases;
					for (;;)
					{
						const auto* Alias = FindTypeAlias(C, *BaseOwner, Base);
						if (!Alias)
						{
							break;
						}
						if (!Aliases.insert(Alias->Id).second)
						{
							Error(C, Out, "Cyclic JSON mapping base alias");
							return;
						}
						// Alias targets belong to their own module and namespace.
						Base = QualifyType(C, *Alias, Alias->TypeName);
						BaseOwner = Alias;
					}
					const auto Open = Base.find('<');
					auto BaseName = Base.substr(0, Open);
					if (BaseName.rfind("::", 0) == 0)
					{
						BaseName.erase(0, 2);
					}
					std::vector<std::string> Arguments;
					if (Open != Base.npos)
					{
						std::size_t Start = Open + 1;
						int AngleDepth = 0, GroupDepth = 0;
						for (std::size_t At = Start; At < Base.size(); ++At)
						{
							const char Ch = Base[At];
							if (Ch == '\'' || Ch == '"')
							{
								At = QuotedEnd(Base, At) - 1;
								continue;
							}
							if (Ch == '(' || Ch == '[')
							{
								++GroupDepth;
								continue;
							}
							if (Ch == ')' || Ch == ']')
							{
								--GroupDepth;
								continue;
							}
							if (GroupDepth != 0)
							{
								continue;
							}
							const bool Close = Ch == '>' && Base[At - 1] != '-';
							if ((Ch == ',' && AngleDepth == 0) || (Close && AngleDepth == 0))
							{
								const auto Argument = Base.substr(Start, At - Start);
								if (Argument.find_first_not_of(" \t\r\n") != Argument.npos || !Arguments.empty())
								{
									Arguments.push_back(Argument);
								}
								Start = At + 1;
								if (Ch == '>')
								{
									break;
								}
							}
							if (Ch == '<')
							{
								++AngleDepth;
							}
							else if (Close)
							{
								--AngleDepth;
							}
						}
					}
					const FAnnotationDeclarationView* Selected = nullptr;
					int BestRank = -1;
					bool Ambiguous = false;
					for (const auto& Candidate : C.Declarations)
					{
						if (C.CheckCancelled)
						{
							C.CheckCancelled();
						}
						const bool Local = Candidate.ModulePath == BaseOwner->ModulePath;
						if (Candidate.Name != BaseName || !IsVisible(C, *BaseOwner, Candidate) ||
						    (Candidate.Target != EAnnotationTarget::Struct && Candidate.Target != EAnnotationTarget::Class &&
						        Candidate.Target != EAnnotationTarget::Interface && Candidate.Target != EAnnotationTarget::Enum))
						{
							continue;
						}
						const auto Declared = TemplateParameters(Candidate);
						auto Required = Declared.size();
						while (Required != 0 && Declared[Required - 1].bHasDefault)
						{
							--Required;
						}
						if (Arguments.size() < Required || Arguments.size() > Declared.size())
						{
							continue;
						}
						// Match Core: prefer the same module, then exact parameter count,
						// across all type declaration kinds. Never select by source order.
						const int Rank = (Local ? 2 : 0) + (Arguments.size() == Declared.size() ? 1 : 0);
						if (Rank > BestRank)
						{
							Selected = &Candidate;
							BestRank = Rank;
							Ambiguous = false;
						}
						else if (Rank == BestRank)
						{
							Ambiguous = true;
						}
					}
					if (Ambiguous)
					{
						Error(C, Out, "Ambiguous JSON mapping base type");
						return;
					}
					if (!Selected || (Selected->Target != EAnnotationTarget::Struct && Selected->Target != EAnnotationTarget::Class))
					{
						continue;
					}
					FSubstitutions Next;
					const auto BaseParameters = TemplateParameters(*Selected);
					for (std::size_t I = 0; I < BaseParameters.size(); ++I)
					{
						const auto& P = BaseParameters[I];
						std::string Value = I < Arguments.size() ? Arguments[I]
						    : P.bHasDefault                      ? QualifyType(C, *Selected, P.DefaultSource, &Next)
						                                         : P.Name;
						if (P.bValue)
						{
							Value = "(" + Value + ")";
						}
						Next.emplace(P.Name, std::move(Value));
					}
					Collect(*Selected, std::move(Next));
				}
				Owners.push_back({&Owner, std::move(Substitutions)});
			};
			Collect(D, Signature.Bindings);

			for (const auto& Owner : Owners)
			{
				for (const auto& Field : C.Declarations)
				{
					if (C.CheckCancelled)
					{
						C.CheckCancelled();
					}
					const auto* Property = Annotation(Field.Metadata, "JsonProperty");
					if (Field.OwnerId != Owner.Declaration->Id ||
					    (Field.Target != EAnnotationTarget::Field &&
					        !(Field.Target == EAnnotationTarget::Method && Property)) ||
					    Field.Access != EMemberAccess::Public || Field.bIsStatic)
					{
						continue;
					}
					FMappingField F{&Field,
					    Field.Name,
					    {},
					    {},
					    QualifyType(C, *Owner.Declaration, Field.TypeName, &Owner.Substitutions)};
					F.Ignore = Annotation(Field.Metadata, "JsonIgnore") != nullptr;
					F.Required = Annotation(Field.Metadata, "JsonRequired") != nullptr;
					F.SerializeOnly = Annotation(Field.Metadata, "JsonSerializeOnly") != nullptr ||
					    (!F.Ignore && Field.Target == EAnnotationTarget::Field && Field.bIsConst);
					F.DeserializeOnly = Annotation(Field.Metadata, "JsonDeserializeOnly") != nullptr;
					if (const auto* A = Annotation(Field.Metadata, "JsonConverter"))
					{
						F.Converter = Lookup(C, *Owner.Declaration, Callable(Argument(*A, "type")));
					}
					if (Property)
					{
						if (!Field.Parameters.empty() || !Field.bIsConst || Field.TypeName == "void")
						{
							Error(C, Out, "JsonProperty requires a public const getter with no parameters");
							return;
						}
						F.Name = Argument(*Property, "name");
						for (const auto& P : Property->Arguments)
						{
							if (P.Name == "setter")
							{
								F.Setter = Callable(Argument(*Property, "setter"));
							}
						}
						if (F.Setter.empty())
						{
							F.SerializeOnly = true;
						}
					}
					if (const auto* A = Annotation(Field.Metadata, "JsonName"))
					{
						if (A->Arguments.size() != 1 || A->Arguments[0].Name != "value" ||
						    A->Arguments[0].Kind != EAnnotationArgumentKind::Scalar ||
						    A->Arguments[0].ScalarKind != EAnnotationScalarKind::String)
						{
							Error(C, Out, "JsonName requires one string argument");
							return;
						}
						F.Name = A->Arguments[0].Value;
					}
					if ((F.Required && (F.Ignore || F.SerializeOnly)) || (F.SerializeOnly && F.DeserializeOnly))
					{
						Error(C, Out, "Conflicting JSON field directions or required annotation");
						return;
					}
					if (!Names.insert(F.Name).second)
					{
						Error(C, Out, "Duplicate JSON field name: " + F.Name);
						return;
					}
					Fields.push_back(std::move(F));
				}
			}
			std::ostringstream Source;
			if (const auto* A = Annotation(D.Metadata, "JsonConverter"))
			{
				const auto Adapter = Lookup(C, D, Callable(Argument(*A, "type")));
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<" + Parameters +
				        "> export bool mapping_encode(const " + ThisType +
				        "& input,json::value& output,json::mapping_context context,JsonGuard guard=0){return "
				        "context.callback_result(" +
				        Adapter + "::encode(ref input,ref output,context));}}");
				const auto* Factory = Annotation(D.Metadata, "JsonFactory");
				const auto Initial =
				    Factory                                ? Lookup(C, D, Callable(Argument(*Factory, "type"))) + "::create(input,context)"
				    : D.Target == EAnnotationTarget::Class ? "nullptr"
				                                           : Adapter + "::create(input,context)";
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<" + Parameters + "> export " + ThisType +
				        " mapping_initial(type<" + ThisType +
				        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return " + Initial +
				        ";}}");
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<" + Parameters +
				        "> export bool mapping_validate(type<" + ThisType +
				        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){return true;}}");
				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<" + Parameters +
				        "> export bool mapping_decode(json::value input," + ThisType +
				        "& output,json::mapping_context context,JsonGuard guard=0){bool "
				        "fresh=context.take_fresh(type<" +
				        ThisType + ">().qualified_name);" + ThisType +
				        " candidate=output;if(!fresh)candidate=" + Initial + ";if(!context.callback_result(" + Adapter +
				        "::decode(input,ref candidate,context)))return false;output=candidate;return true;}}");

				Emit(C,
				    Out,
				    "namespace veroscript_json_mapping {template<" + Parameters +
				        "> export json::value mapping_schema(type<" + ThisType +
				        "> tag,json::mapping_context context,JsonGuard guard=0){return " + Adapter +
				        "::schema(context);}}");
				return;
			}
			Source << "namespace veroscript_json_mapping {template<" << Parameters
			       << "> export bool mapping_encode(const " << ThisType
			       << "& input,json::value& output,json::mapping_context context,JsonGuard guard=0){";
			if (D.Target == EAnnotationTarget::Class)
			{
				Source << "if(input==nullptr){output=json::from_null();return context.visit();}";
			}
			Source << "if(!context.enter("
			       << (D.Target == EAnnotationTarget::Class ? "std::memory::object_id(input)" : "0u64")
			       << "))return false;"
			       << "json::builder result=json::object_builder();json::value item=json::value();bool success=true;";
			for (const auto& F : Fields)
			{
				if (F.Ignore || F.DeserializeOnly)
				{
					continue;
				}
				Source << "if(success){if(!context.push(" << Literal(F.Name) << ")){context.leave();return false;}";
				std::string Operand = "input." + F.Declaration->Name;
				if (F.Declaration->Target == EAnnotationTarget::Method)
				{
					Source << "auto property=" << Operand << "();";
					Operand = "property";
				}
				Source << "success=context.callback_result("
				       << (F.Converter.empty() ? "veroscript_json_mapping::mapping_encode<int>"
				                               : F.Converter + "::encode")
				       << "(ref " << Operand << ",ref item,context));context.pop();"
				       << "if(success)result.set(" << Literal(F.Name) << ",item);}";
			}
			Source << "context.leave();if(success)output=result.finish();return success;}}";
			Emit(C, Out, Source.str());
			Source.str({});
			Source.clear();
			Source << "namespace veroscript_json_mapping {template<" << Parameters
			       << "> export bool mapping_decode(json::value input," << ThisType
			       << "& output,json::mapping_context context,JsonGuard guard=0){";
			if (D.Target == EAnnotationTarget::Class)
			{
				Source << "if(input.kind()==0){output=nullptr;return context.visit();}";
			}
			Source << "if(input.kind()!=5)return context.fail(\"Expected a JSON "
			          "object\");if(!context.enter(0u64))return false;map<string,int> rules=map<string,int>();";
			for (const auto& F : Fields)
			{
				Source << "rules[" << Literal(F.Name) << "]=" << (F.SerializeOnly ? 1 : 0) << ';';
			}
			Source << "json::value keys=input.keys();for(int64 i=0;i<keys.size();i++){string "
			          "key=keys.at(i).as_string();bool known=rules.has(key);"
			          "if(known && rules[key]==1){context.push(key);context.fail(\"Output-only JSON "
			          "field\");context.pop();context.leave();return false;}"
			          "if(!known && !context.ignore_unknown()){context.push(key);context.fail(\"Unknown JSON "
			          "field\");context.pop();context.leave();return false;}}";
			for (const auto& F : Fields)
			{
				if (F.Required)
				{
					Source << "if(!input.contains(" << Literal(F.Name) << ")){context.push(" << Literal(F.Name)
					       << ");context.fail(\"Required JSON field is missing\");context.pop();context.leave();return "
					          "false;}";
				}
			}
			std::string Validation = Source.str();
			Validation = Validation.substr(Validation.find("guard=0){") + 9);
			const std::string NullCase = "if(input.kind()==0){output=nullptr;return context.visit();}";
			if (const auto At = Validation.find(NullCase); At != Validation.npos)
			{
				Validation.replace(At, NullCase.size(), "if(input.kind()==0)return true;");
			}
			for (const std::string Remove : {"if(!context.enter(0u64))return false;", "context.leave();"})
			{
				for (auto At = Validation.find(Remove); At != Validation.npos; At = Validation.find(Remove))
				{
					Validation.erase(At, Remove.size());
				}
			}
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> export bool mapping_validate(type<" +
			        ThisType + "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){" +
			        Validation + "return true;}}");

			const auto* Factory = Annotation(D.Metadata, "JsonFactory");
			const auto Initial = Factory
			    ? Lookup(C, D, Callable(Argument(*Factory, "type"))) + "::create(input,context)"
			    : ThisType + "()";
			Emit(C,
			    Out,
			    "namespace veroscript_json_mapping {template<" + Parameters + "> export " + ThisType +
			        " mapping_initial(type<" + ThisType +
			        "> tag,json::value input,json::mapping_context context,JsonGuard guard=0){" +
			        (D.Target == EAnnotationTarget::Class ? "if(input.kind()==0)return nullptr;" : "") + "return " +
			        Initial + ";}}");
			Source << "bool fresh=context.take_fresh(type<" << ThisType << ">().qualified_name);" << ThisType
			       << " candidate=fresh?output:(" << Initial << ");bool success=true;";

			for (const auto& F : Fields)
			{
				if (F.Ignore || F.SerializeOnly)
				{
					continue;
				}
				Source << "if(success){bool present=input.contains(" << Literal(F.Name) << ");";
				Source << "if(present){if(!context.push(" << Literal(F.Name) << ")){context.leave();return false;}";
				std::string Operand = "candidate." + F.Declaration->Name;
				if (!F.Setter.empty())
				{
					Source << "auto property="
					       << (F.Converter.empty()
					                  ? "veroscript_json_mapping::mapping_initial<int>(type<" + F.TypeName +
					                      ">(),input.get(" + Literal(F.Name) + "),context)"
					                  : F.Converter + "::create(input.get(" + Literal(F.Name) + "),context)")
					       << ";context.use_fresh(type<" << F.TypeName << ">().qualified_name);";
					Operand = "property";
				}
				Source << "success=context.callback_result("
				       << (F.Converter.empty() ? "veroscript_json_mapping::mapping_decode<int>"
				                               : F.Converter + "::decode")
				       << "(input.get(" << Literal(F.Name) << "),ref " << Operand << ",context));";
				if (!F.Setter.empty())
				{
					Source << "if(success)candidate." << F.Setter << "(property);";
				}
				Source << "context.pop();}}";
			}
			Source << "context.leave();if(success)output=candidate;return success;}}";
			Emit(C, Out, Source.str());
			Source.str({});
			Source.clear();
			Source << "namespace veroscript_json_mapping {template<" << Parameters
			       << "> export json::value mapping_schema(type<" << ThisType
			       << "> tag,json::mapping_context context,JsonGuard guard=0){string name=" << Literal(D.Id)
			       << "+\"/\"+type<" << ThisType << ">().qualified_name";
			Source << ";json::value reference=json::value();if(context.schema_reference(name,ref reference))return "
			          "reference;json::builder properties=json::object_builder();json::builder "
			          "required=json::array_builder();";
			for (const auto& F : Fields)
			{
				Source << "if(context.schema_input()){properties.set(" << Literal(F.Name) << ',';
				if (F.SerializeOnly)
				{
					Source << "json::from_bool(false)";
				}
				else if (F.Ignore)
				{
					Source << "json::object()";
				}
				else if (!F.Converter.empty())
				{
					Source << F.Converter << "::schema(context)";
				}
				else
				{
					Source << "veroscript_json_mapping::mapping_schema<int>(type<" << F.TypeName << ">(),context)";
				}
				Source << ");";
				if (F.Required)
				{
					Source << "required.push(json::from_string(" << Literal(F.Name) << "));";
				}
				Source << "}";
				if (!F.Ignore && !F.DeserializeOnly)
				{
					Source << "else{properties.set(" << Literal(F.Name) << ','
					       << (F.Converter.empty() ? "veroscript_json_mapping::mapping_schema<int>(type<" + F.TypeName +
					                      ">(),context)"
					                               : F.Converter + "::schema(context)")
					       << ");required.push(json::from_string(" << Literal(F.Name) << "));}";
				}
			}
			Source
			    << "json::value "
			       "definition=json::object();definition.set(\"type\",json::from_string(\"object\"));definition.set("
			       "\"properties\",properties.finish());definition.set(\"required\",required.finish());definition.set("
			       "\"additionalProperties\",json::from_bool(context.schema_input() && context.ignore_unknown()));";
			if (D.Target == EAnnotationTarget::Class)
			{
				Source << "json::builder "
				          "alternatives=json::array_builder();alternatives.push(definition);alternatives.push(json::"
				          "parse(\"{\\\"type\\\":\\\"null\\\"}\"));definition=json::object();definition.set(\"anyOf\","
				          "alternatives.finish());";
			}
			Source << "context.schema_define(name,definition);return reference;}}";
			Emit(C, Out, Source.str());
		}

	private:
		INativeExtensionHost& Host;
	};
	bool RegisterMappingProcessor(FState& S, std::string& Error)
	{
		for (const char* Name : {"JsonName",
		         "JsonIgnore",
		         "JsonRequired",
		         "JsonSerializeOnly",
		         "JsonDeserializeOnly",
		         "JsonConverter",
		         "JsonProperty",
		         "JsonFactory",
		         "JsonPolymorphic",
		         "VeroJsonMappingIdentity"})
		{
			FHostAnnotationDesc A;
			A.Name = "@" + std::string(Name);
			A.Targets =
			    AnnotationTargetMask(EAnnotationTarget::Field) | AnnotationTargetMask(EAnnotationTarget::Method);
			if (std::string_view(Name) == "JsonName")
			{
				A.Targets |= AnnotationTargetMask(EAnnotationTarget::EnumMember);
			}
			if (std::string_view(Name) == "JsonConverter")
			{
				A.Targets |= AnnotationTargetMask(EAnnotationTarget::Struct) |
				    AnnotationTargetMask(EAnnotationTarget::Class) |
				    AnnotationTargetMask(EAnnotationTarget::Enum);
			}
			if (std::string_view(Name) == "VeroJsonMappingIdentity")
			{
				A.Targets = AnnotationTargetMask(EAnnotationTarget::Struct) |
				    AnnotationTargetMask(EAnnotationTarget::Class) |
				    AnnotationTargetMask(EAnnotationTarget::Enum);
			}
			if (std::string_view(Name) == "JsonFactory")
			{
				A.Targets =
				    AnnotationTargetMask(EAnnotationTarget::Struct) | AnnotationTargetMask(EAnnotationTarget::Class);
			}
			if (std::string_view(Name) == "JsonPolymorphic")
			{
				A.Targets = AnnotationTargetMask(EAnnotationTarget::Class);
			}
			if (std::string_view(Name) == "JsonProperty")
			{
				A.Targets = AnnotationTargetMask(EAnnotationTarget::Method);
			}
			A.Documentation = "JSON object mapping field rule";
			if (!S.Host->RegisterAnnotation(std::move(A), Error))
			{
				return false;
			}
		}
		FCompileProcessorDesc D;
		D.ProcessorId = "veroscript.json.mapping";
		D.Version = "0.1.1";
		D.ConfigurationKey = "typed-source-v7-qualified-identities";
		D.Phases =
		    CompilePhaseMask(ECompileProcessingPhase::Types) | CompilePhaseMask(ECompileProcessingPhase::Members);
		D.Operations = CompileOperationMask(ECompileOperation::AddDeclaration) |
		    CompileOperationMask(ECompileOperation::SetMetadata);
		FCompileSubscription Module;
		Module.Event = ECompileProcessingEvent::Module;
		Module.Phases =
		    CompilePhaseMask(ECompileProcessingPhase::Types) | CompilePhaseMask(ECompileProcessingPhase::Members);
		FCompileSubscription Types;
		Types.Event = ECompileProcessingEvent::Declaration;
		Types.Phases =
		    CompilePhaseMask(ECompileProcessingPhase::Types) | CompilePhaseMask(ECompileProcessingPhase::Members);
		Types.Targets = AnnotationTargetMask(EAnnotationTarget::Struct) |
		    AnnotationTargetMask(EAnnotationTarget::Class) | AnnotationTargetMask(EAnnotationTarget::Enum);
		D.Subscriptions = {Module, Types};
		D.Factory = [Host = S.Host]
		{
			return std::make_unique<FMappingProcessor>(*Host);
		};
		return S.Host->RegisterProcessor(std::move(D), Error);
	}
} // namespace VeroScript::Json::Binding
