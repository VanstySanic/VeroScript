// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTypeHierarchy.h"
#include "VeroScriptTemplateQuery.h"

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptParser.h"
#include "VeroScriptLexer.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		bool NameMatchesSymbol(const std::string& SymbolName, const std::string_view Ident)
		{
			return SymbolNameMatchesIdent(SymbolName, Ident);
		}

		std::string CompletionLabel(const std::string& SymbolName)
		{
			return StripGenericArgs(SymbolName);
		}

		bool PrefixMatches(const std::string& Label, const std::string_view Prefix)
		{
			return FuzzyIdentifierPrefixMatches(Label, Prefix);
		}

		bool IsConcreteTypeSymbolKind(const EDocumentSymbolKind Kind)
		{
			return Kind == EDocumentSymbolKind::Struct || Kind == EDocumentSymbolKind::Class || Kind == EDocumentSymbolKind::Interface || Kind == EDocumentSymbolKind::Enum;
		}

		bool IsTypeSymbolKind(const EDocumentSymbolKind Kind)
		{
			return IsConcreteTypeSymbolKind(Kind) || Kind == EDocumentSymbolKind::TypeAlias;
		}

		std::string TypeNameLeaf(const std::string_view TypeName)
		{
			const std::string Simple = StripGenericArgs(TypeName);
			const std::size_t Dot = Simple.rfind('.');
			if (Dot == std::string::npos || Dot + 1 >= Simple.size())
			{
				return Simple;
			}
			return Simple.substr(Dot + 1);
		}
		bool SameAccessOwner(const FProgram& Program, std::string_view Left, std::string_view Right)
		{
			const auto Normalize = [](std::string_view Name)
			{
				auto Head = StripGenericArgs(Name);
				for (std::size_t P = 0; (P = Head.find("::", P)) != std::string::npos;)
				{
					Head.replace(P, 2, ".");
				}

				if (!Head.empty() && Head.front() == '.')
				{
					Head.erase(0, 1);
				}

				return Head;
			};
			const auto A = Normalize(Left), B = Normalize(Right);
			if (A.empty() || B.empty()) return false;
			if (A == B) return true;
			const bool ShortA = A.find('.') == std::string::npos, ShortB = B.find('.') == std::string::npos;
			if (ShortA == ShortB) return false;
			const auto& Short = ShortA ? A : B;
			const auto& Qualified = ShortA ? B : A;
			if (Qualified.substr(Qualified.rfind('.') + 1) != Short) return false;
			// Name lookup may accept a short spelling; access checks require the
			// actual declaration. A global type wins over a namespaced homonym.
			bool Global = false;
			std::unordered_set<std::string> Candidates;
			const auto Collect = [&](const auto& Declarations)
			{
				for (const auto& D : Declarations)
				{
					const auto Name = Normalize(D.Name);
					if (Name == Short)
					{
						Global = true;
					}

					else if (Name.substr(Name.rfind('.') + 1) == Short)
					{
						Candidates.insert(Name);
					}
				}
			};
			Collect(Program.Classes);
			Collect(Program.Structs);
			Collect(Program.Interfaces);
			return !Global && Candidates.size() == 1 && Candidates.count(Qualified);
		}

		bool DeclNameMatches(const std::string& DeclName, const std::string_view Query)
		{
			const std::string Simple = StripGenericArgs(Query);
			if (SymbolNameMatchesIdent(DeclName, Simple))
			{
				return true;
			}
			const std::string Leaf = TypeNameLeaf(Query);
			return !Leaf.empty() && Leaf != Simple && SymbolNameMatchesIdent(DeclName, Leaf);
		}

		std::string TypeLookupHead(const std::string_view TypeName)
		{
			const auto SourceHead = TypeName.substr(0, TypeName.find('<'));
			std::string Head;
			Head.reserve(SourceHead.size());
			for (std::size_t Index = 0; Index < SourceHead.size(); ++Index)
			{
				if (SourceHead[Index] == ':' && Index + 1 < SourceHead.size() && SourceHead[Index + 1] == ':')
				{
					Head.push_back('.');
					++Index;
				}
				else
				{
					Head.push_back(SourceHead[Index]);
				}
			}
			if (!Head.empty() && Head.front() == '.')
			{
				Head.erase(0, 1);
			}
			return Head;
		}

		bool RequiresExactTypeLookup(const FProgram& Program, const std::string& Head,
		    const bool bExplicitGlobal = false)
		{
			if (bExplicitGlobal || Head.find('.') != std::string::npos) return true;
			const auto HasExact = [&](const auto& Declarations)
			{
				return std::any_of(Declarations.begin(), Declarations.end(),
				    [&](const auto& Declaration)
				{
					return Declaration.Name == Head;
				});
			};
			// A real global declaration wins over namespaced leaf-name recovery,
			// including when its generic arity does not match this application.
			return HasExact(Program.Structs) || HasExact(Program.Classes) || HasExact(Program.Interfaces);
		}

		// Split `Foo<A, B<C>>` into head + top-level type arguments.
		bool SplitTypeApp(
		    const std::string_view TypeName,
		    std::string& OutHead,
		    std::vector<std::string>& OutArgs)
		{
			OutHead.clear();
			OutArgs.clear();
			const std::size_t Open = TypeName.find('<');
			if (Open == std::string_view::npos || TypeName.back() != '>')
			{
				OutHead = std::string(TypeName);
				return false;
			}
			OutHead = std::string(TypeName.substr(0, Open));
			int Depth = 0;
			int ParenthesisDepth = 0;
			std::size_t ArgStart = Open + 1;
			for (std::size_t Index = Open + 1; Index + 1 < TypeName.size(); ++Index)
			{
				const char Char = TypeName[Index];
				if (Char == '<')
				{
					++Depth;
				}
				else if (Char == '>' && (Index == 0 || TypeName[Index - 1] != '-'))
				{
					--Depth;
				}
				else if (Char == '(')
				{
					++ParenthesisDepth;
				}
				else if (Char == ')' && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Char == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					std::string Arg(TypeName.substr(ArgStart, Index - ArgStart));
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.front())))
					{
						Arg.erase(Arg.begin());
					}
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.back())))
					{
						Arg.pop_back();
					}
					if (!Arg.empty())
					{
						OutArgs.push_back(std::move(Arg));
					}
					ArgStart = Index + 1;
				}
			}
			std::string Last(TypeName.substr(ArgStart, TypeName.size() - 1 - ArgStart));
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.front())))
			{
				Last.erase(Last.begin());
			}
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.back())))
			{
				Last.pop_back();
			}
			if (!Last.empty())
			{
				OutArgs.push_back(std::move(Last));
			}
			// `Box<>` is an explicit application that can use default arguments.
			// Keep it distinct from the bare declaration name `Box`.
			return true;
		}

		std::size_t FindTypeArraySuffix(const std::string_view Type)
		{
			int Angles = 0, Parens = 0;
			for (std::size_t Index = 0; Index < Type.size(); ++Index)
			{
				const char C = Type[Index];
				if (C == '<')
				{
					++Angles;
				}

				else if (C == '>' && Angles > 0 && (Index == 0 || Type[Index - 1] != '-'))
				{
					--Angles;
				}

				else if (C == '(')
				{
					++Parens;
				}

				else if (C == ')' && Parens > 0)
				{
					--Parens;
				}

				else if (C == '[' && Angles == 0 && Parens == 0)
				{
					return Index;
				}
			}
			return std::string_view::npos;
		}

		bool LookupTypeParams(const FProgram& Program, std::string_view TypeName,
		    std::vector<std::string>& OutTypeParams, std::vector<std::string>* InOutTypeArgs);
		std::string SubstituteTypeParamsInText(std::string_view Text,
		    const std::vector<std::string>& TypeParams, const std::vector<std::string>& TypeArgs);

		std::string ResolveOverrideType(const FProgram& Program, const std::string_view Owner,
		    const std::string_view ModulePath, const std::string_view TypeName, const int Depth = 0,
		    const std::vector<std::pair<std::string, std::string>>* Bindings = nullptr,
		    const bool bResolveBindings = false)
		{
			if (Depth >= 64) return {};
			std::string CurrentOwner(Owner), CurrentModule(ModulePath), CurrentType(TypeName);
			std::unordered_set<const FTypeAliasDecl*> Visited;
			for (;;)
			{
				CheckAnalysisCancellation();
				for (const auto Prefix : {std::string_view("const ref "), std::string_view("ref ")})
				{
					if (CurrentType.rfind(Prefix, 0) == 0)
					{
						const auto Inner = ResolveOverrideType(Program, CurrentOwner, CurrentModule,
						    std::string_view(CurrentType).substr(Prefix.size()), Depth + 1, Bindings, bResolveBindings);
						return Inner.empty() ? std::string{} : std::string(Prefix) + Inner;
					}
				}

				std::vector<std::string> FunctionParameters;
				std::string FunctionReturn;
				if (Runtime::ParseFunctionType(CurrentType, FunctionParameters, FunctionReturn))
				{
					for (auto& Parameter : FunctionParameters)
					{
						Parameter = ResolveOverrideType(Program, CurrentOwner, CurrentModule, Parameter, Depth + 1, Bindings, bResolveBindings);
						if (Parameter.empty()) return {};
					}
					FunctionReturn = ResolveOverrideType(Program, CurrentOwner, CurrentModule, FunctionReturn, Depth + 1, Bindings, bResolveBindings);
					return FunctionReturn.empty() ? std::string{} : Runtime::MakeFunctionTypeName(FunctionParameters, FunctionReturn);
				}
				if (const auto Bracket = FindTypeArraySuffix(CurrentType); Bracket != std::string::npos)
				{
					const auto Element = ResolveOverrideType(Program, CurrentOwner, CurrentModule,
					    std::string_view(CurrentType).substr(0, Bracket), Depth + 1, Bindings, bResolveBindings);
					std::string Suffix = CurrentType.substr(Bracket);
					if (Bindings)
					{
						std::vector<std::string> Names, Arguments;
						for (const auto& Binding : *Bindings)
						{
							Names.push_back(Binding.first);
							Arguments.push_back(Binding.second);
						}
						Suffix = SubstituteTypeParamsInText(Suffix, Names, Arguments);
					}
					return Element.empty() ? std::string{} : Element + Suffix;
				}
				std::string Head;
				std::vector<std::string> Arguments;
				const bool bTypeApplication = SplitTypeApp(CurrentType, Head, Arguments);
				// Arguments supplied by a derived declaration have already been
				// resolved in that declaration's module. Never reinterpret them as
				// an identically named private alias in the generic base's module.
				if (Bindings && Arguments.empty())
				{
					for (const auto& Binding : *Bindings)
					{
						if (Head == Binding.first)
						{
							return bResolveBindings
							    ? ResolveOverrideType(Program, CurrentOwner, CurrentModule, Binding.second,
							          Depth + 1, Bindings, true)
							    : Binding.second;
						}
					}
				}

				const bool bGlobalQualified = Head.rfind("::", 0) == 0;
				const std::string LookupHead = bGlobalQualified ? Head.substr(2) : Head;
				const auto FindAlias = [&](const std::string& Name) -> const FTypeAliasDecl*
				{
					const FTypeAliasDecl* Exported = nullptr;
					bool bAmbiguous = false;
					for (const auto& Alias : Program.TypeAliases)
					{
						if (Alias.Name != Name)
						{
							continue;
						}

						if (Alias.ModulePath == CurrentModule) return &Alias;
						if (Alias.bExported)
						{
							if (Exported)
							{
								bAmbiguous = true;
							}

							Exported = &Alias;
						}
					}
					return bAmbiguous ? nullptr : Exported;
				};
				const FTypeAliasDecl* Alias = nullptr;
				if (!bGlobalQualified && Head.find('.') == std::string::npos && Head.find("::") == std::string::npos)
				{
					std::string Scope = StripGenericArgs(CurrentOwner);
					for (std::size_t Dot = Scope.rfind('.'); Dot != std::string::npos; Dot = Scope.rfind('.'))
					{
						Scope.resize(Dot);
						if ((Alias = FindAlias(Scope + "." + Head)))
						{
							break;
						}
					}
				}
				if (!Alias)
				{
					Alias = FindAlias(LookupHead);
				}

				if (Alias)
				{
					if (!Visited.insert(Alias).second) return {};
					CurrentOwner = Alias->Name;
					CurrentModule = Alias->ModulePath;
					CurrentType = Alias->TypeName;
					Bindings = nullptr;
					continue;
				}
				std::string Result = Head;
				if (bTypeApplication)
				{
					Result += '<';
					for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
					{
						if (Index)
						{
							Result += ',';
						}

						const auto Argument = ResolveOverrideType(Program, CurrentOwner, CurrentModule,
						    Arguments[Index], Depth + 1, Bindings, bResolveBindings);
						if (Argument.empty()) return {};
						Result += Argument;
					}
					Result += '>';
				}
				return Result;
			}
		}

		std::string FormatOverrideSourceType(const std::string_view Type, const int Depth = 0)
		{
			if (Depth >= 64) return {};
			CheckAnalysisCancellation();
			for (const auto Prefix : {std::string_view("const ref "), std::string_view("ref ")})
			{
				if (Type.rfind(Prefix, 0) == 0)
				{
					const auto Inner = FormatOverrideSourceType(Type.substr(Prefix.size()), Depth + 1);
					return Inner.empty() ? std::string{} : (Type.front() == 'c' ? "const " : "") + Inner + '&';
				}
			}

			std::vector<std::string> Parameters;
			std::string Return;
			if (Runtime::ParseFunctionType(Type, Parameters, Return))
			{
				for (auto& Parameter : Parameters)
				{
					Parameter = FormatOverrideSourceType(Parameter, Depth + 1);
					if (Parameter.empty()) return {};
				}
				Return = FormatOverrideSourceType(Return, Depth + 1);
				return Return.empty() ? std::string{} : Runtime::MakeFunctionTypeName(Parameters, Return);
			}
			if (const auto Bracket = FindTypeArraySuffix(Type); Bracket != std::string_view::npos)
			{
				const auto Element = FormatOverrideSourceType(Type.substr(0, Bracket), Depth + 1);
				return Element.empty() ? std::string{} : Element + std::string(Type.substr(Bracket));
			}
			std::string Head;
			std::vector<std::string> Arguments;
			if (SplitTypeApp(Type, Head, Arguments))
			{
				Head += '<';
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					const auto Argument = FormatOverrideSourceType(Arguments[Index], Depth + 1);
					if (Argument.empty()) return {};
					if (Index)
					{
						Head += ',';
					}

					Head += Argument;
				}
				return Head + '>';
			}
			return std::string(Type);
		}

		// Match normalized compiler method signatures without changing source spellings.
		std::string CanonicalOverrideSlotType(const FProgram& Program, const std::string_view Type, const int Depth = 0)
		{
			if (Depth >= 64) return {};
			CheckAnalysisCancellation();
			if (Type == "long") return "int64";
			if (Type == "unsigned long") return "uint64";
			if (Type.rfind("::", 0) == 0) return CanonicalOverrideSlotType(Program, Type.substr(2), Depth + 1);
			for (const auto Prefix : {std::string_view("const ref "), std::string_view("ref ")})
			{
				if (Type.rfind(Prefix, 0) == 0)
				{
					const auto Inner = CanonicalOverrideSlotType(Program, Type.substr(Prefix.size()), Depth + 1);
					return Inner.empty() ? std::string{} : std::string(Prefix) + Inner;
				}
			}

			std::vector<std::string> Parameters;
			std::string Return;
			if (Runtime::ParseFunctionType(Type, Parameters, Return))
			{
				for (auto& Parameter : Parameters)
				{
					Parameter = CanonicalOverrideSlotType(Program, Parameter, Depth + 1);
					if (Parameter.empty()) return {};
				}
				Return = CanonicalOverrideSlotType(Program, Return, Depth + 1);
				return Return.empty() ? std::string{} : Runtime::MakeFunctionTypeName(Parameters, Return);
			}
			if (const auto Bracket = FindTypeArraySuffix(Type); Bracket != std::string_view::npos)
			{
				const auto Element = CanonicalOverrideSlotType(Program, Type.substr(0, Bracket), Depth + 1);
				return Element.empty() ? std::string{} : Element + std::string(Type.substr(Bracket));
			}
			std::string Head;
			std::vector<std::string> Arguments;
			if (SplitTypeApp(Type, Head, Arguments))
			{
				const bool bValueContainer = Head == "array" || Head == "queue" || Head == "priority_queue" ||
				    Head == "stack" || Head == "deque" || Head == "multiset" || Head == "ordered_multiset";
				const bool bMap = Head == "map" || Head == "multimap" || Head == "ordered_map" || Head == "ordered_multimap";
				std::vector<std::string> Parameters;
				const bool bScriptTemplate = LookupTypeParams(Program, Type, Parameters, &Arguments);
				if (bScriptTemplate && Parameters.empty() && Arguments.empty()) return Head;
				Head += '<';
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					const auto Argument = bValueContainer || bScriptTemplate || bMap
					    ? CanonicalOverrideSlotType(Program, Arguments[Index], Depth + 1)
					    : Arguments[Index];
					if (Argument.empty()) return {};
					if (Index)
					{
						Head += ',';
					}

					Head += Argument;
				}
				return Head + '>';
			}
			return std::string(Type);
		}

		std::string SubstituteTypeParamsInText(
		    const std::string_view Text,
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& TypeArgs)
		{
			if (TypeParams.empty() || TypeArgs.empty() || Text.empty())
			{
				return std::string(Text);
			}
			const std::size_t Count =
			    TypeParams.size() < TypeArgs.size() ? TypeParams.size() : TypeArgs.size();

			const auto IsIdentChar = [](const char C)
			{
				return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
			};

			std::string Out;
			Out.reserve(Text.size() + 8);
			for (std::size_t Index = 0; Index < Text.size();)
			{
				if (!IsIdentChar(Text[Index]))
				{
					Out.push_back(Text[Index]);
					++Index;
					continue;
				}
				const std::size_t Start = Index;
				while (Index < Text.size() && IsIdentChar(Text[Index]))
				{
					++Index;
				}
				const std::string_view Word = Text.substr(Start, Index - Start);
				bool bReplaced = false;
				for (std::size_t ParamIndex = 0; ParamIndex < Count; ++ParamIndex)
				{
					if (Word == TypeParams[ParamIndex] && !TypeArgs[ParamIndex].empty())
					{
						Out += FormatTemplateArgumentName(TypeArgs[ParamIndex]);
						bReplaced = true;
						break;
					}
				}
				if (!bReplaced)
				{
					Out.append(Word);
				}
			}
			return Out;
		}

		std::size_t CountGenericArityInSymbolName(const std::string_view SymbolName)
		{
			const std::size_t Lt = SymbolName.find('<');
			if (Lt == std::string_view::npos || Lt + 1 >= SymbolName.size())
			{
				return 0;
			}
			std::size_t Depth = 0;
			std::size_t ParenthesisDepth = 0;
			std::size_t Count = 0;
			bool bArgStarted = false;
			for (std::size_t Index = Lt + 1; Index < SymbolName.size(); ++Index)
			{
				const char Ch = SymbolName[Index];
				if (Ch == '<')
				{
					++Depth;
					bArgStarted = true;
				}
				else if (Ch == '>' && (Index == 0 || SymbolName[Index - 1] != '-'))
				{
					if (Depth == 0)
					{
						if (bArgStarted)
						{
							++Count;
						}
						return Count;
					}
					--Depth;
				}
				else if (Ch == '(')
				{
					++ParenthesisDepth;
					bArgStarted = true;
				}
				else if (Ch == ')' && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Ch == ',' && Depth == 0 && ParenthesisDepth == 0)
				{
					if (bArgStarted)
					{
						++Count;
					}
					bArgStarted = false;
				}
				else if (!std::isspace(static_cast<unsigned char>(Ch)))
				{
					bArgStarted = true;
				}
			}
			return 0;
		}

		bool LookupTypeParams(
		    const FProgram& Program,
		    const std::string_view TypeName,
		    std::vector<std::string>& OutTypeParams,
		    std::vector<std::string>* InOutTypeArgs)
		{
			OutTypeParams.clear();
			const std::string Head = TypeLookupHead(TypeName);
			const bool bRequireExact = RequiresExactTypeLookup(Program, Head, TypeName.rfind("::", 0) == 0);
			const auto TryDecl = [&](const auto& Decl, const bool bUseDefaults) -> bool
			{
				if (bRequireExact ? Decl.Name != Head : !DeclNameMatches(Decl.Name, Head))
				{
					return false;
				}
				const std::size_t Provided = InOutTypeArgs
				    ? InOutTypeArgs->size()
				    : static_cast<std::size_t>(-1);
				std::size_t Required = TemplateParameterNames(Decl).size();
				if (TemplateParameterDefaults(Decl).size() == TemplateParameterNames(Decl).size())
				{
					while (Required > 0 && !TemplateParameterDefaults(Decl)[Required - 1].empty())
					{
						--Required;
					}
				}
				if (InOutTypeArgs && (!bUseDefaults ? Provided != TemplateParameterNames(Decl).size() : (Provided < Required || Provided >= TemplateParameterNames(Decl).size())))
				{
					return false;
				}
				OutTypeParams = TemplateParameterNames(Decl);
				if (InOutTypeArgs && Provided < TemplateParameterNames(Decl).size())
				{
					while (InOutTypeArgs->size() < TemplateParameterNames(Decl).size())
					{
						const std::size_t Index = InOutTypeArgs->size();
						InOutTypeArgs->push_back(SubstituteTypeParamsInText(
						    TemplateParameterDefaults(Decl)[Index],
						    TemplateParameterNames(Decl),
						    *InOutTypeArgs));
					}
				}
				return true;
			};
			const auto TryAll = [&](const bool bUseDefaults) -> bool
			{
				for (const FStructDecl& Decl : Program.Structs)
				{
					if (TryDecl(Decl, bUseDefaults))
					{
						return true;
					}
				}
				for (const FClassDecl& Decl : Program.Classes)
				{
					if (TryDecl(Decl, bUseDefaults))
					{
						return true;
					}
				}
				for (const FInterfaceDecl& Decl : Program.Interfaces)
				{
					if (TryDecl(Decl, bUseDefaults))
					{
						return true;
					}
				}
				return false;
			};
			return TryAll(false) || TryAll(true);
		}

		struct FBaseEdge
		{
			std::string Name;
			EMemberAccess InheritAccess = EMemberAccess::Public;
		};

		void AppendDirectBaseEdges(
		    const FProgram& Program,
		    const std::string_view TypeName,
		    std::vector<FBaseEdge>& OutEdges,
		    const bool bResolveOverrideTypes = false)
		{
			std::string Head;
			std::vector<std::string> TypeArgs;
			(void)SplitTypeApp(TypeName, Head, TypeArgs);
			const std::string Simple = TypeLookupHead(Head);
			const bool bRequireExact = RequiresExactTypeLookup(Program, Simple, Head.rfind("::", 0) == 0);
			std::vector<std::string> TypeParams;
			(void)LookupTypeParams(Program, TypeName, TypeParams, &TypeArgs);

			const auto SpecializeBase = [&](const std::string& BaseAsWritten, const auto& Decl) -> std::string
			{
				if (BaseAsWritten.empty())
				{
					return {};
				}
				if (!bResolveOverrideTypes)
				{
					return SubstituteTypeParamsInText(BaseAsWritten, TypeParams, TypeArgs);
				}

				std::vector<std::pair<std::string, std::string>> Bindings;
				for (std::size_t Index = 0; Index < TypeParams.size() && Index < TypeArgs.size(); ++Index)
				{
					Bindings.emplace_back(TypeParams[Index], TypeArgs[Index]);
				}

				return ResolveOverrideType(Program, Decl.Name, Decl.ModulePath, BaseAsWritten, 0, &Bindings);
			};

			for (const FStructDecl& Decl : Program.Structs)
			{
				if ((bRequireExact ? Decl.Name != Simple : !DeclNameMatches(Decl.Name, Simple)) || TemplateParameterNames(Decl) != TypeParams)
				{
					continue;
				}
				if (!Decl.BaseTypeName.empty())
				{
					OutEdges.push_back(FBaseEdge{
					    SpecializeBase(Decl.BaseTypeName, Decl),
					    Decl.BaseInheritanceAccess});
				}
				for (const std::string& InterfaceName : Decl.InterfaceTypeNames)
				{
					OutEdges.push_back(FBaseEdge{
					    SpecializeBase(InterfaceName, Decl),
					    EMemberAccess::Public});
				}
				return;
			}
			for (const FClassDecl& Decl : Program.Classes)
			{
				if ((bRequireExact ? Decl.Name != Simple : !DeclNameMatches(Decl.Name, Simple)) || TemplateParameterNames(Decl) != TypeParams)
				{
					continue;
				}
				for (std::size_t BaseIndex = 0; BaseIndex < Decl.BaseTypeNames.size(); ++BaseIndex)
				{
					const EMemberAccess InheritAccess =
					    BaseIndex < Decl.BaseInheritanceAccess.size()
					    ? Decl.BaseInheritanceAccess[BaseIndex]
					    : EMemberAccess::Private;
					OutEdges.push_back(FBaseEdge{
					    SpecializeBase(Decl.BaseTypeNames[BaseIndex], Decl),
					    InheritAccess});
				}
				for (const std::string& InterfaceName : Decl.InterfaceTypeNames)
				{
					OutEdges.push_back(FBaseEdge{
					    SpecializeBase(InterfaceName, Decl),
					    EMemberAccess::Public});
				}
				return;
			}
			for (const FInterfaceDecl& Decl : Program.Interfaces)
			{
				if ((bRequireExact ? Decl.Name != Simple : !DeclNameMatches(Decl.Name, Simple)) || TemplateParameterNames(Decl) != TypeParams)
				{
					continue;
				}
				for (const std::string& BaseName : Decl.BaseInterfaceNames)
				{
					OutEdges.push_back(FBaseEdge{
					    SpecializeBase(BaseName, Decl),
					    EMemberAccess::Public});
				}
				return;
			}
		}

		template <typename TVisit>
		bool WalkTypeHierarchy(
		    const FProgram& Program,
		    const std::string_view RootTypeName,
		    std::vector<std::string>& OutClosestFirst,
		    TVisit&& Visit,
		    const bool bResolveOverrideTypes = false)
		{
			struct FPendingType
			{
				std::string Name;
				std::string Parent;
				EMemberAccess PathAccess = EMemberAccess::Public;
				EMemberAccess InheritAccess = EMemberAccess::Public;
			};
			// Override roots are actual enclosing declarations, and their base
			// edges have already been resolved in the declaring module above.
			// The legacy global alias lookup would undo that scoped resolution.
			std::vector<FPendingType> Pending{{bResolveOverrideTypes ? std::string(RootTypeName)
			                                                         : ResolveTypeAlias(Program, RootTypeName),
			    {}}};
			std::unordered_set<std::string> Seen;
			std::size_t TypeTextBytes = 0;
			constexpr std::size_t MaxTypeTextBytes = 32u * 1024u * 1024u;
			OutClosestFirst.clear();
			while (!Pending.empty())
			{
				CheckAnalysisCancellation();
				FPendingType Current = std::move(Pending.back());
				Pending.pop_back();
				if (Current.Name.empty() || !Seen.insert(Current.Name).second)
				{
					continue;
				}
				// Callable return chains (A<fn()->T>) can grow without adding
				// generic brackets. Bound cumulative spelling/storage work as well
				// as nesting and type count, including when no cancel scope exists.
				if (Current.Name.size() > MaxTypeTextBytes - TypeTextBytes)
				{
					OutClosestFirst.clear();
					return false;
				}
				TypeTextBytes += Current.Name.size();
				// The compiler's type table is indexed by uint16_t. Do not let
				// invalid generic diamonds synthesize an unbounded number of types.
				if (Seen.size() > 65536u)
				{
					OutClosestFirst.clear();
					return false;
				}
				// Recovery may expose invalid A<T> : A<array<T>> inheritance.
				// Every concrete spelling differs, so a visited-name set alone
				// cannot stop it. Bound synthetic generic growth independently of
				// inheritance depth; long ordinary chains remain supported.
				unsigned GenericDepth = 0;
				for (std::size_t Index = 0; Index < Current.Name.size(); ++Index)
				{
					const char Character = Current.Name[Index];
					if (Character == '<' && ++GenericDepth > 128u)
					{
						OutClosestFirst.clear();
						return false;
					}
					if (Character == '>' && GenericDepth && (Index == 0 || Current.Name[Index - 1] != '-'))
					{
						--GenericDepth;
					}
				}
				OutClosestFirst.push_back(Current.Name);
				Visit(Current.Name, Current.Parent, Current.PathAccess, Current.InheritAccess);
				std::vector<FBaseEdge> Edges;
				AppendDirectBaseEdges(Program, Current.Name, Edges, bResolveOverrideTypes);
				// Reverse push preserves declaration-order DFS and its existing
				// first-path access semantics for shared ancestors.
				for (auto Edge = Edges.rbegin(); Edge != Edges.rend(); ++Edge)
				{
					Pending.push_back({bResolveOverrideTypes ? Edge->Name : ResolveTypeAlias(Program, Edge->Name), Current.Name,
					    ApplyInheritanceAccess(Current.PathAccess, Edge->InheritAccess), Edge->InheritAccess});
				}
			}
			return true;
		}

		/**
		 * Closest-first hierarchy via declaration-order DFS (matches compiler
		 * LookupMemberAccess: first declared base's subtree wins for a shared ancestor).
		 * Parent[T] / InheritFromParent[T] describe how the discovering derived type
		 * inherits T, so per-member InheritFromBase re-homing can be replayed.
		 */
		void CollectTypeHierarchyWithPathAccess(
		    const FProgram& Program,
		    const std::string_view RootTypeName,
		    std::vector<std::string>& OutClosestFirst,
		    std::unordered_map<std::string, EMemberAccess>& OutPathAccessByType,
		    std::unordered_map<std::string, std::string>& OutParentByType,
		    std::unordered_map<std::string, EMemberAccess>& OutInheritFromParentByType)
		{
			OutClosestFirst.clear();
			OutPathAccessByType.clear();
			OutParentByType.clear();
			OutInheritFromParentByType.clear();
			if (RootTypeName.empty())
			{
				return;
			}

			const bool bComplete = WalkTypeHierarchy(Program, RootTypeName, OutClosestFirst,
			    [&](const std::string& Current, const std::string& Parent,
			        const EMemberAccess PathAccess, const EMemberAccess InheritAccess)
			{
				OutPathAccessByType.emplace(Current, PathAccess);
				if (!Parent.empty())
				{
					OutParentByType.emplace(Current, Parent);
					OutInheritFromParentByType.emplace(Current, InheritAccess);
				}
			});
			if (!bComplete)
			{
				OutPathAccessByType.clear();
				OutParentByType.clear();
				OutInheritFromParentByType.clear();
			}
		}

		/**
		 * Replay compiler InheritFromBase from DeclaringType up to Root along the
		 * DFS-chosen parent edges. OutEffective / OutAccessOwner match LookupMemberAccess.
		 */
		void ResolveInheritedMemberAccess(
		    const std::string_view DeclaringTypeName,
		    const EMemberAccess DeclaredAccess,
		    const std::string_view RootTypeName,
		    const std::unordered_map<std::string, std::string>& ParentByType,
		    const std::unordered_map<std::string, EMemberAccess>& InheritFromParentByType,
		    EMemberAccess& OutEffective,
		    std::string& OutAccessOwner)
		{
			OutEffective = DeclaredAccess;
			OutAccessOwner.assign(DeclaringTypeName.data(), DeclaringTypeName.size());
			std::string Current(DeclaringTypeName);
			const std::string Root(RootTypeName);
			while (!Current.empty() && Current != Root)
			{
				const auto ParentFound = ParentByType.find(Current);
				const auto InheritFound = InheritFromParentByType.find(Current);
				if (ParentFound == ParentByType.end() || InheritFound == InheritFromParentByType.end())
				{
					break;
				}
				const EMemberAccess NewEffective =
				    ApplyInheritanceAccess(OutEffective, InheritFound->second);
				if (IsAccessMoreRestricted(NewEffective, OutEffective))
				{
					OutAccessOwner = ParentFound->second;
				}
				OutEffective = NewEffective;
				Current = ParentFound->second;
			}
		}
	}

	std::string StripGenericArgs(const std::string_view TypeNameAsWritten)
	{
		const std::size_t Generic = TypeNameAsWritten.find('<');
		if (Generic == std::string_view::npos)
		{
			return std::string(TypeNameAsWritten);
		}
		return std::string(TypeNameAsWritten.substr(0, Generic));
	}

	std::string ResolveDeclarationTypeName(const FProgram& Program, const std::string_view Owner,
	    const std::string_view ModulePath, const std::string_view TypeName,
	    const std::vector<std::pair<std::string, std::string>>& Bindings, const bool bResolveBindings)
	{
		return ResolveOverrideType(Program, Owner, ModulePath, TypeName, 0, &Bindings, bResolveBindings);
	}

	std::string SpecializeMemberDetail(
	    const FProgram& Program,
	    const std::string_view TypeNameAsWritten,
	    const std::string_view DetailTemplate,
	    const FDocumentSymbol* const TypeSymbol,
	    const std::vector<std::pair<std::string, std::string>>& AdditionalBindings)
	{
		std::string Head;
		std::vector<std::string> TypeArgs;
		const bool bTypeApplication = SplitTypeApp(TypeNameAsWritten, Head, TypeArgs);
		std::vector<std::string> TypeParams;
		std::string DeclarationHead;
		const bool bSoftDeclaration = bTypeApplication && TypeSymbol && TypeSymbol->bSoftBuiltin && SplitTypeApp(TypeSymbol->Name, DeclarationHead, TypeParams) && TypeParams.size() == TypeArgs.size() && std::all_of(TypeParams.begin(), TypeParams.end(), [](const std::string& Param)
		{
			return !Param.empty() && std::all_of(Param.begin(), Param.end(), [](const char C)
			{
				return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
			});
		});
		if (!bSoftDeclaration && (!bTypeApplication || !LookupTypeParams(Program, TypeNameAsWritten, TypeParams, &TypeArgs)))
		{
			TypeParams.clear();
			TypeArgs.clear();
		}
		for (const auto& Binding : AdditionalBindings)
		{
			const auto Found = std::find(TypeParams.begin(), TypeParams.end(), Binding.first);
			if (Found != TypeParams.end())
			{
				TypeArgs[static_cast<std::size_t>(Found - TypeParams.begin())] = Binding.second;
			}
			else
			{
				TypeParams.push_back(Binding.first);
				TypeArgs.push_back(Binding.second);
			}
		}
		return SubstituteTypeParamsInText(DetailTemplate, TypeParams, TypeArgs);
	}

	std::string SpecializeFunctionDetail(
	    const FProgram& Program,
	    const std::string_view FunctionLabel,
	    const std::vector<std::string>& ExplicitTypeArgs,
	    const std::vector<std::string>& ArgumentTypeHints,
	    const std::string_view DetailTemplate)
	{
		for (const FFunctionDecl& Function : Program.Functions)
		{
			std::string DeclarationLabel = Function.Name;
			if (!TemplateParameterNames(Function).empty())
			{
				DeclarationLabel.push_back('<');
				for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
				{
					if (Index > 0)
					{
						DeclarationLabel += ", ";
					}
					DeclarationLabel += TemplateParameterNames(Function)[Index];
					if (Index < TemplateParameterDefaults(Function).size() && !TemplateParameterDefaults(Function)[Index].empty())
					{
						DeclarationLabel += " = ";
						DeclarationLabel += TemplateParameterDefaults(Function)[Index];
					}
				}
				DeclarationLabel.push_back('>');
			}
			if (DeclarationLabel != FunctionLabel || TemplateParameterNames(Function).empty() || ExplicitTypeArgs.size() > TemplateParameterNames(Function).size())
			{
				continue;
			}

			std::vector<std::string> TypeArgs(TemplateParameterNames(Function).size());
			for (std::size_t Index = 0; Index < ExplicitTypeArgs.size(); ++Index)
			{
				TypeArgs[Index] = ExplicitTypeArgs[Index];
			}

			std::function<void(std::string_view, std::string_view)> BindTypeParam;
			BindTypeParam = [&Function, &TypeArgs, &BindTypeParam](
			                    const std::string_view Pattern,
			                    const std::string_view Actual)
			{
				if (Actual.empty())
				{
					return;
				}
				for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
				{
					if (Pattern != TemplateParameterNames(Function)[Index])
					{
						continue;
					}
					// Explicit arguments win. Otherwise infer from values first; a
					// declaration default is applied below only when inference leaves the
					// parameter unresolved, matching compiler template semantics.
					if (TypeArgs[Index].empty())
					{
						TypeArgs[Index] = std::string(Actual);
					}
					return;
				}

				std::string PatternHead;
				std::string ActualHead;
				std::vector<std::string> PatternArgs;
				std::vector<std::string> ActualArgs;
				if (!SplitTypeApp(Pattern, PatternHead, PatternArgs) || !SplitTypeApp(Actual, ActualHead, ActualArgs) || PatternHead != ActualHead || PatternArgs.size() != ActualArgs.size())
				{
					return;
				}
				for (std::size_t Index = 0; Index < PatternArgs.size(); ++Index)
				{
					BindTypeParam(PatternArgs[Index], ActualArgs[Index]);
				}
			};

			for (std::size_t ArgIndex = 0;
			    ArgIndex < ArgumentTypeHints.size() && !Function.Parameters.empty();
			    ++ArgIndex)
			{
				const std::size_t ParamIndex = ArgIndex < Function.Parameters.size()
				    ? ArgIndex
				    : Function.Parameters.size() - 1;
				const FFunctionParam& Param = Function.Parameters[ParamIndex];
				if (ArgIndex >= Function.Parameters.size() && !Param.bIsVariadic)
				{
					break;
				}
				std::string Actual = ArgumentTypeHints[ArgIndex];
				if (Param.bIsVariadic)
				{
					std::string Head;
					std::vector<std::string> Elements;
					if (SplitTypeApp(Actual, Head, Elements) && (Head == "array" || Head == "TArray") && Elements.size() == 1)
					{
						Actual = Elements.front();
					}
				}
				BindTypeParam(Param.TypeName, Actual);
			}

			for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
			{
				if (!TypeArgs[Index].empty())
				{
					continue;
				}
				if (Index >= TemplateParameterDefaults(Function).size() || TemplateParameterDefaults(Function)[Index].empty())
				{
					continue;
				}
				TypeArgs[Index] = SubstituteTypeParamsInText(
				    TemplateParameterDefaults(Function)[Index],
				    TemplateParameterNames(Function),
				    TypeArgs);
			}
			bool bHasConcreteType = false;
			for (const std::string& TypeArg : TypeArgs)
			{
				bHasConcreteType = bHasConcreteType || !TypeArg.empty();
			}
			if (!bHasConcreteType)
			{
				return std::string(DetailTemplate);
			}
			return SubstituteTypeParamsInText(
			    DetailTemplate, TemplateParameterNames(Function), TypeArgs);
		}
		return std::string(DetailTemplate);
	}

	bool SymbolNameMatchesIdent(const std::string_view SymbolName, const std::string_view Ident)
	{
		if (Ident.empty())
		{
			return false;
		}
		if (SymbolName == Ident)
		{
			return true;
		}
		if (SymbolName.size() > Ident.size() && SymbolName.compare(0, Ident.size(), Ident) == 0 && SymbolName[Ident.size()] == '<')
		{
			return true;
		}

		const std::string Simple = StripGenericArgs(SymbolName);
		if (Simple == Ident)
		{
			return true;
		}
		if (Simple.size() > Ident.size() && Simple.compare(Simple.size() - Ident.size(), Ident.size(), Ident) == 0 && Simple[Simple.size() - Ident.size() - 1] == '.')
		{
			return true;
		}
		// Qualified query (`ns.IA`) vs short decl/symbol name (`IA` / `IA<T>`).
		if (Ident.size() > Simple.size() && Ident.compare(Ident.size() - Simple.size(), Simple.size(), Simple) == 0 && Ident[Ident.size() - Simple.size() - 1] == '.')
		{
			return true;
		}
		return false;
	}

	namespace
	{
		char AsciiLower(const char Ch)
		{
			return (Ch >= 'A' && Ch <= 'Z') ? static_cast<char>(Ch - 'A' + 'a') : Ch;
		}

		bool AsciiEqualIgnoreCase(const std::string_view Left, const std::string_view Right)
		{
			if (Left.size() != Right.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Left.size(); ++Index)
			{
				if (AsciiLower(Left[Index]) != AsciiLower(Right[Index]))
				{
					return false;
				}
			}
			return true;
		}

		bool AsciiStartsWithIgnoreCase(const std::string_view Text, const std::string_view Prefix)
		{
			if (Prefix.size() > Text.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Prefix.size(); ++Index)
			{
				if (AsciiLower(Text[Index]) != AsciiLower(Prefix[Index]))
				{
					return false;
				}
			}
			return true;
		}

		std::string AlnumFoldLower(const std::string_view Text)
		{
			std::string Out;
			Out.reserve(Text.size());
			for (const char Ch : Text)
			{
				if (Ch == '_' || Ch == '.')
				{
					continue;
				}
				Out.push_back(AsciiLower(Ch));
			}
			return Out;
		}

		void AppendIdentifierSegments(const std::string_view Name, std::vector<std::string>& Out)
		{
			std::size_t Index = 0;
			while (Index < Name.size())
			{
				if (Name[Index] == '_' || Name[Index] == '.')
				{
					++Index;
					continue;
				}
				const std::size_t Start = Index;
				if (std::isupper(static_cast<unsigned char>(Name[Index])) != 0)
				{
					++Index;
					while (Index < Name.size() && std::isupper(static_cast<unsigned char>(Name[Index])) != 0)
					{
						++Index;
					}
					if (Index < Name.size() && std::islower(static_cast<unsigned char>(Name[Index])) != 0 && Index - Start > 1)
					{
						--Index;
					}
					while (Index < Name.size() && (std::islower(static_cast<unsigned char>(Name[Index])) != 0 || std::isdigit(static_cast<unsigned char>(Name[Index])) != 0))
					{
						++Index;
					}
				}
				else
				{
					while (Index < Name.size() && Name[Index] != '_' && Name[Index] != '.' && std::isupper(static_cast<unsigned char>(Name[Index])) == 0)
					{
						++Index;
					}
				}
				if (Index > Start)
				{
					Out.emplace_back(Name.substr(Start, Index - Start));
				}
			}
		}

		bool CamelSegmentSubsequenceMatches(
		    const std::string_view Label,
		    const std::string_view Prefix)
		{
			std::vector<std::string> LabelSegs;
			std::vector<std::string> PrefixSegs;
			AppendIdentifierSegments(Label, LabelSegs);
			AppendIdentifierSegments(Prefix, PrefixSegs);
			if (PrefixSegs.empty())
			{
				return true;
			}
			if (LabelSegs.empty())
			{
				return false;
			}

			std::size_t QueryIndex = 0;
			for (std::size_t LabelIndex = 0;
			    LabelIndex < LabelSegs.size() && QueryIndex < PrefixSegs.size();
			    ++LabelIndex)
			{
				const std::string_view LabelSeg = LabelSegs[LabelIndex];
				const std::string_view QuerySeg = PrefixSegs[QueryIndex];
				if (QueryIndex + 1 == PrefixSegs.size())
				{
					if (AsciiStartsWithIgnoreCase(LabelSeg, QuerySeg))
					{
						++QueryIndex;
						break;
					}
				}
				else if (AsciiEqualIgnoreCase(LabelSeg, QuerySeg))
				{
					++QueryIndex;
				}
			}
			return QueryIndex == PrefixSegs.size();
		}

		bool FuzzyMatchLeaf(const std::string_view Leaf, const std::string_view Prefix)
		{
			if (Prefix.empty())
			{
				return true;
			}
			if (AsciiStartsWithIgnoreCase(Leaf, Prefix))
			{
				return true;
			}
			const std::string FoldLeaf = AlnumFoldLower(Leaf);
			const std::string FoldPrefix = AlnumFoldLower(Prefix);
			if (!FoldPrefix.empty() && AsciiStartsWithIgnoreCase(FoldLeaf, FoldPrefix))
			{
				return true;
			}
			return CamelSegmentSubsequenceMatches(Leaf, Prefix);
		}
	} // namespace

	bool FuzzyIdentifierPrefixMatches(const std::string_view Label, const std::string_view Prefix)
	{
		if (Prefix.empty())
		{
			return true;
		}
		if (Label.empty())
		{
			return false;
		}

		const std::string Simple = StripGenericArgs(Label);
		if (FuzzyMatchLeaf(Simple, Prefix))
		{
			return true;
		}

		const std::size_t Dot = Simple.rfind('.');
		if (Dot != std::string::npos && Dot + 1 < Simple.size())
		{
			return FuzzyMatchLeaf(Simple.substr(Dot + 1), Prefix);
		}
		return false;
	}

	std::string ResolveTypeAlias(const FProgram& Program, const std::string_view TypeName)
	{
		if (TypeName.find("@default:") != std::string_view::npos)
		{
			// Nested applications retain captured defaults with their declaration
			// provenance. Let Core complete them before matching or substituting
			// formal parameters; the marker itself is not a type argument.
			FTemplateCallTypeQuery Query(Program, nullptr, Program.EntryModulePath, {});
			FCallExpr Constructor(std::string(TypeName), {});
			const auto Resolved = Query.ResolveExpressionType(Constructor);
			return Resolved.empty() ? std::string{} : Resolved;
		}
		// Resolve an explicit global anchor with the scoped resolver. Merely
		// stripping it before legacy fuzzy alias lookup could select A.Pick for
		// ::Pick even when the source explicitly names the global alias.
		if (TypeName.rfind("::", 0) == 0)
		{
			auto Resolved = ResolveDeclarationTypeName(Program, {}, Program.EntryModulePath, TypeName);
			return Resolved.rfind("::", 0) == 0 ? Resolved.substr(2) : Resolved;
		}
		std::vector<FTypePathSegment> Segments;
		if (ParseTypePath(TypeName, Segments) && Segments.size() > 1)
		{
			VeroScriptLexer Lexer(TypeName, Program.EntryModulePath);
			auto Tokens = Lexer.Tokenize();
			if (!Lexer.HasError())
			{
				VeroScriptParser Parser(Tokens, Program.EntryModulePath, &Program);
				const auto Normalized = Parser.ParseTypeFragment(std::string{}, std::vector<std::string>{});
				if (!Parser.HasError() && !Normalized.empty() && Normalized != TypeName)
				{
					return ResolveTypeAlias(Program, Normalized);
				}
			}
		}
		const std::string Original(TypeName);
		std::string Head;
		std::vector<std::string> TypeArgs;
		bool bHadArgs = SplitTypeApp(TypeName, Head, TypeArgs);
		std::string Current = StripGenericArgs(Head);
		bool bEverResolved = false;
		for (int Depth = 0; Depth < 8; ++Depth)
		{
			// A canonical declaration identity cannot be replaced by a private
			// alias with the same spelling from a different imported module.
			const auto HasType = [&Program, &Current](const auto& Types)
			{
				return std::any_of(Types.begin(), Types.end(),
				    [&Program, &Current](const auto& Decl)
				{
					return Decl.Name == Current && Decl.ModulePath == Program.EntryModulePath;
				});
			};
			if (HasType(Program.Classes) || HasType(Program.Structs) || HasType(Program.Interfaces) ||
			    HasType(Program.Enums))
			{
				break;
			}

			bool bResolved = false;
			for (const FTypeAliasDecl& Alias : Program.TypeAliases)
			{
				if (!SymbolNameMatchesIdent(Alias.Name, Current) && !DeclNameMatches(Alias.Name, Current))
				{
					continue;
				}
				std::string AliasHead;
				std::vector<std::string> AliasArgs;
				if (SplitTypeApp(Alias.TypeName, AliasHead, AliasArgs))
				{
					// Alias expands to a type application — continue peeling its head.
					Current = StripGenericArgs(AliasHead);
					TypeArgs = std::move(AliasArgs);
					bHadArgs = true;
				}
				else
				{
					Current = StripGenericArgs(Alias.TypeName);
				}
				bResolved = true;
				bEverResolved = true;
				break;
			}
			if (!bResolved)
			{
				break;
			}
		}
		if (!bEverResolved)
		{
			return Original;
		}
		if (bHadArgs)
		{
			std::string Rebuilt = Current;
			Rebuilt.push_back('<');
			for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
			{
				if (Index > 0)
				{
					Rebuilt += ", ";
				}
				Rebuilt += TypeArgs[Index];
			}
			Rebuilt.push_back('>');
			return Rebuilt;
		}
		return Current;
	}

	void CollectTypeHierarchy(
	    const FProgram& Program,
	    const std::string_view RootTypeName,
	    std::vector<std::string>& OutClosestFirst)
	{
		OutClosestFirst.clear();
		if (RootTypeName.empty())
		{
			return;
		}

		(void)WalkTypeHierarchy(Program, RootTypeName, OutClosestFirst,
		    [](const std::string&, const std::string&, EMemberAccess, EMemberAccess)
		{
		});
	}

	void CollectOverrideMemberCandidates(
	    const FProgram& Program,
	    const std::string_view RootTypeName,
	    std::vector<FOverrideMemberCandidate>& OutCandidates)
	{
		std::vector<std::string> Hierarchy;
		(void)WalkTypeHierarchy(
		    Program, RootTypeName, Hierarchy,
		    [](const std::string&, const std::string&, EMemberAccess, EMemberAccess)
		{
		},
		    true);
		if (Hierarchy.size() < 2)
		{
			return;
		}
		// A nearer declaration (including a final or nonvirtual one) occupies
		// its full method slot and hides any farther ancestor declaration.
		std::unordered_set<std::string> Occupied;
		for (std::size_t TypeIndex = 0; TypeIndex < Hierarchy.size(); ++TypeIndex)
		{
			const std::string& TypeName = Hierarchy[TypeIndex];
			const std::string Simple = TypeLookupHead(TypeName);
			const bool bRequireExact = RequiresExactTypeLookup(Program, Simple, TypeName.rfind("::", 0) == 0);
			const std::vector<FMethodDecl>* Methods = nullptr;
			std::string TypeHead;
			std::vector<std::string> TypeArguments, SelectedParameters;
			const bool bTypeApplication = SplitTypeApp(TypeName, TypeHead, TypeArguments);
			if (bTypeApplication && !LookupTypeParams(Program, TypeName, SelectedParameters, &TypeArguments))
			{
				continue;
			}
			const std::vector<std::string>* MethodTypeParams = nullptr;
			std::string MethodsModulePath;
			bool bInterface = false;
			const auto FindMethods = [&](const auto& Decls) -> const std::vector<FMethodDecl>*
			{
				const std::vector<FMethodDecl>* LeafMatch = nullptr;
				const std::vector<std::string>* LeafTypeParams = nullptr;
				std::string LeafModulePath;
				bool bAmbiguousLeaf = false;
				for (const auto& Decl : Decls)
				{
					if (bTypeApplication && TemplateParameterNames(Decl) != SelectedParameters)
					{
						continue;
					}
					if (StripGenericArgs(Decl.Name) == Simple)
					{
						MethodsModulePath = Decl.ModulePath;
						MethodTypeParams = &TemplateParameterNames(Decl);
						return &Decl.Methods;
					}
					if (!bRequireExact && DeclNameMatches(Decl.Name, Simple))
					{
						if (LeafMatch)
						{
							bAmbiguousLeaf = true;
						}
						else
						{
							LeafMatch = &Decl.Methods;
							LeafModulePath = Decl.ModulePath;
							LeafTypeParams = &TemplateParameterNames(Decl);
						}
					}
				}
				if (!bAmbiguousLeaf && LeafMatch)
				{
					MethodsModulePath = std::move(LeafModulePath);
					MethodTypeParams = LeafTypeParams;
				}
				return bAmbiguousLeaf ? nullptr : LeafMatch;
			};
			Methods = FindMethods(Program.Classes);
			if (!Methods)
			{
				Methods = FindMethods(Program.Structs);
			}
			if (!Methods)
			{
				Methods = FindMethods(Program.Interfaces);
				bInterface = Methods != nullptr;
			}
			if (!Methods)
			{
				continue;
			}
			std::vector<std::pair<std::string, std::string>> MemberBindings;
			for (std::size_t Index = 0; Index < MethodTypeParams->size() && Index < TypeArguments.size();
			    ++Index)
			{
				MemberBindings.emplace_back((*MethodTypeParams)[Index], TypeArguments[Index]);
			}

			for (const FMethodDecl& Method : *Methods)
			{
				if (!TemplateParameterNames(Method).empty())
				{
					continue;
				}
				const bool bConversion = Method.Name.rfind("operator conv ", 0) == 0;
				const auto ReturnType = ResolveOverrideType(Program, TypeName, MethodsModulePath,
				    Method.ReturnType, 0, &MemberBindings);
				if (ReturnType.empty())
				{
					OutCandidates.clear();
					return;
				}
				// Conversion names contain their target type in the AST. Substitute
				// it for source insertion, and use the canonical target for slot
				// identity so an override written with a different alias occupies it.
				const auto SourceReturnType = FormatOverrideSourceType(ReturnType);
				if (SourceReturnType.empty())
				{
					OutCandidates.clear();
					return;
				}
				const std::string SourceName = bConversion ? "operator " + SourceReturnType : Method.Name;
				const std::string DeclarationHead = SourceReturnType + " " + SourceName;
				std::string Signature = DeclarationHead + "(";
				const auto ConversionSlotType = bConversion ? CanonicalOverrideSlotType(Program, ReturnType) : std::string{};
				if (bConversion && ConversionSlotType.empty())
				{
					OutCandidates.clear();
					return;
				}
				std::string Slot = (bConversion ? "operator conv " + ConversionSlotType
				                                : Method.Name) +
				    "(";
				std::string Parameters;
				for (std::size_t ParamIndex = 0; ParamIndex < Method.Parameters.size(); ++ParamIndex)
				{
					const FFunctionParam& Param = Method.Parameters[ParamIndex];
					if (ParamIndex > 0)
					{
						Signature += ", ";
						Slot += ',';
						Parameters += ", ";
					}
					const auto ParamType = ResolveOverrideType(Program, TypeName, MethodsModulePath,
					    Param.TypeName, 0, &MemberBindings);
					const auto SlotType = CanonicalOverrideSlotType(Program, ParamType);
					const auto SourceParamType = FormatOverrideSourceType(ParamType);
					if (ParamType.empty() || SlotType.empty() || SourceParamType.empty())
					{
						OutCandidates.clear();
						return;
					}
					const std::string Qualifier = std::string(Param.bIsConst ? "const " : "") + (Param.bIsRef ? "ref " : "");
					const std::string ParamSignature = Qualifier + SourceParamType + (Param.bIsVariadic ? "..." : "");
					// Match the compiler's parameter identity: top-level const on a
					// value parameter affects its declaration, not the override slot.
					const std::string SlotQualifier = Param.bIsRef && !Param.bIsVariadic
					    ? (Param.bIsConst ? "const ref " : "ref ")
					    : "";
					Slot += SlotQualifier + SlotType + (Param.bIsVariadic ? "..." : "");
					const std::string ParamName = Param.Name.empty()
					    ? "arg" + std::to_string(ParamIndex + 1)
					    : Param.Name;
					const std::string Written = ParamSignature + " " + ParamName;
					Signature += Written;
					Parameters += Written;
				}
				Signature += ')';
				Slot += Method.bIsConst ? ") const" : ")";
				if (Method.bIsConst)
				{
					Signature += " const";
				}
				if (!Occupied.insert(Slot).second || TypeIndex == 0)
				{
					continue;
				}
				const bool bOfferMethod = Method.bVirtual || Method.bOverride || Method.bPureVirtual;
				if (Method.bIsStatic || Method.bFinal || Method.bDeleted || !bOfferMethod || !IsHierarchyMemberAccessible(Program, RootTypeName, TypeName, Method.Access, RootTypeName))
				{
					continue;
				}
				FOverrideMemberCandidate Candidate;
				Candidate.Name = SourceName;
				Candidate.Signature = std::move(Signature);
				Candidate.bInterfaceImplementation = bInterface && Method.bPureVirtual;
				Candidate.Declaration = DeclarationHead + "(" + Parameters + ")" + (Method.bIsConst ? " const" : "") + (Candidate.bInterfaceImplementation ? "" : " override") + " {\n    throw 0;\n}";
				OutCandidates.push_back(std::move(Candidate));
			}
		}
	}

	const FDocumentSymbol* FindTypeSymbolInTree(
	    const std::vector<FDocumentSymbol>& Symbols,
	    const std::string_view TypeName)
	{
		const std::string Simple = StripGenericArgs(TypeName);
		if (Simple.empty())
		{
			return nullptr;
		}
		std::string Head;
		std::vector<std::string> TypeArgs;
		const bool bTypeApplication = SplitTypeApp(TypeName, Head, TypeArgs);
		const std::size_t WantedArity = TypeArgs.size();
		const std::string Leaf = TypeNameLeaf(TypeName);
		const bool bDelegate = Leaf == "delegate" || Leaf == "multicast_delegate";
		// Prefer the resolved, fully qualified identity before considering leaf
		// names. Otherwise `using Pack=Util.Pack` can select the alias `Pack`
		// instead of the actual class, hiding all of the class's members.
		for (unsigned MatchPass = 0; MatchPass < 2; ++MatchPass)
		{
			const FDocumentSymbol* Fallback = nullptr;
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				CheckAnalysisCancellation();
				if ((Symbol.TypeScope.bLocal || !Symbol.TypeScope.OwnerTypeName.empty()) &&
				    Symbol.TypeScope.QualifiedName != Simple)
				{
					continue;
				}
				if (!IsTypeSymbolKind(Symbol.Kind) || (StripGenericArgs(Symbol.Name) == Simple) != (MatchPass == 0))
				{
					continue;
				}
				if (!(SymbolNameMatchesIdent(Symbol.Name, Simple) || (!Leaf.empty() && SymbolNameMatchesIdent(Symbol.Name, Leaf))))
				{
					continue;
				}
				if (CountGenericArityInSymbolName(Symbol.Name) == WantedArity)
				{
					return &Symbol;
				}
				if (bTypeApplication && !bDelegate)
				{
					std::string DeclarationHead;
					std::vector<std::string> Parameters;
					if (!SplitTypeApp(Symbol.Name, DeclarationHead, Parameters) || Parameters.empty() || WantedArity > Parameters.size() || !std::all_of(Parameters.begin() + WantedArity, Parameters.end(), [](const std::string& Parameter)
					{
						return Parameter.find('=') != std::string::npos;
					}))
					{
						continue;
					}
				}
				if (bDelegate)
				{
					// The synthetic event type represents several arities. A real
					// declaration (or an alias copied from one) must accept this arity.
					std::string DeclarationHead;
					std::vector<std::string> Parameters;
					(void)SplitTypeApp(Symbol.Name, DeclarationHead, Parameters);
					if (!Symbol.bArityOverloadedEventType &&
					    (WantedArity > Parameters.size() ||
					        !std::all_of(Parameters.begin() + WantedArity, Parameters.end(),
					            [](const std::string& Parameter)
					{
						return Parameter.find('=') != std::string::npos;
					})))
					{
						continue;
					}
				}
				if (!Fallback)
				{
					Fallback = &Symbol;
				}
			}
			if (Fallback)
			{
				return Fallback;
			}
		}
		return nullptr;
	}

	std::string CallableParameterTypeName(const FFunctionParam& Parameter)
	{
		if (Parameter.bIsVariadic)
		{
			return Parameter.TypeName + "...";
		}
		if (Parameter.bIsRef)
		{
			return (Parameter.bIsConst ? "const ref " : "ref ") + Parameter.TypeName;
		}
		return Parameter.TypeName;
	}

	bool IsMemberAccessibleFromContext(
	    const FProgram& Program,
	    const std::string_view DeclaringTypeName,
	    const EMemberAccess Access,
	    const std::string_view AccessContextType,
	    const FCallableAccessContext* const CallableContext)
	{
		if (Access == EMemberAccess::Public || DeclaringTypeName.empty())
		{
			return true;
		}
		if (AccessContextType.empty() && !CallableContext)
		{
			return false;
		}
		if (SameAccessOwner(Program, DeclaringTypeName, AccessContextType))
		{
			return true;
		}
		const auto AccessIdentity = [&](const std::string_view Name)
		{
			std::string Result;
			const auto Find = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (StripGenericArgs(Name) != Declaration.Name)
					{
						continue;
					}
					Result = Declaration.bExported ? Declaration.Name
					                               : Declaration.ModulePath + '\x1F' + Declaration.Name;
					for (std::size_t I = 0; I < TemplateParameterNames(Declaration).size(); ++I)
					{
						Result += (I == 0 ? "#" : "") + std::string("1:T");
					}
				}
			};
			Find(Program.Classes);
			Find(Program.Structs);
			Find(Program.Interfaces);
			return Result;
		};
		FMemberAccessInfo ScopeAccess;
		ScopeAccess.bFound = true;
		ScopeAccess.EffectiveAccess = Access;
		ScopeAccess.DeclaringOwnerLinkage = AccessIdentity(DeclaringTypeName);
		const auto ContextIdentity = AccessIdentity(AccessContextType);
		if (!ScopeAccess.DeclaringOwnerLinkage.empty() && !ContextIdentity.empty())
		{
			const auto Marker = ScopeAccess.DeclaringOwnerLinkage.find('#');
			ScopeAccess.DeclaringArity =
			    Marker == std::string::npos
			    ? 0
			    : static_cast<std::size_t>(std::count(ScopeAccess.DeclaringOwnerLinkage.begin() + Marker,
			          ScopeAccess.DeclaringOwnerLinkage.end(), ':'));
			if (CanAccessMemberFromContext(ScopeAccess, ContextIdentity, Program, {}, true))
			{
				return true;
			}
		}
		const auto IsFriend = [&](const auto& Decls) -> bool
		{
			for (const auto& Decl : Decls)
			{
				if (!SameAccessOwner(Program, Decl.Name, DeclaringTypeName))
				{
					continue;
				}
				for (const FFriendDecl& Friend : Decl.Friends)
				{
					if (Friend.Kind != FFriendDecl::EKind::Type)
					{
						continue;
					}
					const std::string FriendType = ResolveTypeAlias(Program, Friend.TypeName);
					const std::string ContextType = ResolveTypeAlias(
					    Program, AccessContextType);
					if (FriendType == ContextType)
					{
						return true;
					}
					if (FriendType.find('<') == std::string::npos && ContextType.find('<') == std::string::npos && SameAccessOwner(Program, FriendType, ContextType))
					{
						return true;
					}
				}
				return false;
			}
			return false;
		};
		if (IsFriend(Program.Structs) || IsFriend(Program.Classes) || IsFriend(Program.Interfaces))
		{
			return true;
		}
		const auto NormalizeSignatureType = [&Program](const std::string_view TypeName,
		                                        const std::string_view Owner, const std::string_view ModulePath)
		{
			std::string_view Type = TypeName;
			std::string_view Prefix;
			for (const auto Qualifier : {std::string_view("const ref "), std::string_view("ref ")})
			{
				if (Type.rfind(Qualifier, 0) == 0)
				{
					Prefix = Qualifier;
					Type.remove_prefix(Qualifier.size());
					break;
				}
			}
			const bool bVariadic = Type.size() >= 3 && Type.substr(Type.size() - 3) == "...";
			if (bVariadic)
			{
				Type.remove_suffix(3);
			}
			// Preparation binds ordinary callable types, while friend declarations
			// retain source spellings. Resolve each in its declaration's scope.
			std::string Resolved = ResolveDeclarationTypeName(Program, Owner, ModulePath, Type);
			VeroScriptLexer TypeLexer(Resolved, ModulePath);
			const auto& TypeTokens = TypeLexer.Tokenize();
			if (!TypeLexer.HasError())
			{
				VeroScriptParser Parser(TypeTokens, ModulePath, &Program);
				const auto Separator = Owner.rfind('.');
				const auto Namespace = Separator == std::string_view::npos
				    ? std::string{}
				    : std::string(Owner.substr(0, Separator));
				auto Qualified = Parser.ParseTypeFragment(Namespace, {});
				if (!Parser.HasError() && !Qualified.empty())
				{
					Resolved = std::move(Qualified);
				}
			}
			const std::string Result = std::string(Prefix) + ResolveTypeAlias(Program, Resolved) + (bVariadic ? "..." : "");
			// Compare tokens rather than deleting whitespace: `ref int` and
			// the identifier `refint` are different types, including inside fn types.
			VeroScriptLexer Lexer(Result, "<access-signature>");
			const auto& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				// Internal type identities can contain non-source characters. Keep
				// their exact spelling; do not guess equivalence after a lexer error.
				return "raw:" + Result;
			}
			std::string Identity = "tokens:";
			for (const auto& Token : Tokens)
			{
				if (Token.Kind == ETokenKind::EndOfFile)
				{
					continue;
				}
				const std::string_view Spelling = Token.Kind == ETokenKind::Symbol && Token.Lexeme == "::"
				    ? std::string_view(".")
				    : Token.Lexeme;
				Identity += std::to_string(Spelling.size()) + ":";
				Identity.append(Spelling.data(), Spelling.size());
			}
			return Identity;
		};
		const auto CallableIsFriend = [&](const auto& Decls) -> bool
		{
			if (!CallableContext)
			{
				return false;
			}
			const std::string_view CallableOwner = CallableContext->OwnerTypeName.empty()
			    ? CallableContext->Name
			    : CallableContext->OwnerTypeName;
			const std::string_view CallableModule = CallableContext->OwnerTypeName.empty() && !CallableContext->ModulePath.empty()
			    ? CallableContext->ModulePath
			    : Program.EntryModulePath;
			for (const auto& Decl : Decls)
			{
				if (!SameAccessOwner(Program, Decl.Name, DeclaringTypeName))
				{
					continue;
				}
				// The granting declaration must see this free function, as in the
				// compiler. This is invariant across that declaration's friend list.
				if (CallableContext->OwnerTypeName.empty() && !ModuleVisibility::CanAccessSymbol(Program, CallableContext->ModulePath, CallableContext->bExported, Decl.ModulePath))
				{
					return false;
				}
				for (const FFriendDecl& Friend : Decl.Friends)
				{
					if (Friend.Kind != FFriendDecl::EKind::Function || Friend.bIsConstMethod != CallableContext->bIsConstMethod || Friend.Parameters.size() != CallableContext->ParameterTypes.size())
					{
						continue;
					}
					std::string FriendOwner;
					std::string FriendLeaf = Friend.FunctionName;
					int Depth = 0;
					std::size_t Separator = std::string::npos;
					for (std::size_t Index = 0; Index < Friend.FunctionName.size(); ++Index)
					{
						if (Friend.FunctionName[Index] == '<')
						{
							++Depth;
						}
						else if (Friend.FunctionName[Index] == '>' && (Index == 0 || Friend.FunctionName[Index - 1] != '-'))
						{
							--Depth;
						}
						else if (Friend.FunctionName[Index] == '.' && Depth == 0)
						{
							Separator = Index;
						}
					}
					if (Separator != std::string::npos)
					{
						FriendOwner = Friend.FunctionName.substr(0, Separator);
						FriendLeaf = Friend.FunctionName.substr(Separator + 1);
						FriendOwner = ResolveTypeAlias(Program, FriendOwner);
					}
					if (CallableContext->OwnerTypeName.empty())
					{
						// The compiler considers the declaration's namespace and the
						// written name. An unrelated namespace with the same leaf is
						// a different function and must not inherit friendship.
						const auto DeclaringSeparator = Decl.Name.rfind('.');
						const std::string DeclaringNamespace = DeclaringSeparator == std::string::npos
						    ? std::string{}
						    : Decl.Name.substr(0, DeclaringSeparator);
						const bool bScopedMatch = !DeclaringNamespace.empty() && Friend.FunctionName.rfind(DeclaringNamespace + ".", 0) != 0 && DeclaringNamespace + "." + Friend.FunctionName == CallableContext->Name;
						if (Friend.FunctionName != CallableContext->Name && !bScopedMatch)
						{
							continue;
						}
					}
					else if (FriendOwner.empty() || FriendLeaf != CallableContext->Name || !SameAccessOwner(Program, FriendOwner, CallableContext->OwnerTypeName))
					{
						continue;
					}
					if (NormalizeSignatureType(Friend.ReturnType, Decl.Name, Decl.ModulePath) != NormalizeSignatureType(CallableContext->ReturnType, CallableOwner, CallableModule))
					{
						continue;
					}
					bool bParametersMatch = true;
					for (std::size_t Index = 0; Index < Friend.Parameters.size(); ++Index)
					{
						const std::string FriendParam = CallableParameterTypeName(Friend.Parameters[Index]);
						if (NormalizeSignatureType(FriendParam, Decl.Name, Decl.ModulePath) != NormalizeSignatureType(CallableContext->ParameterTypes[Index], CallableOwner, CallableModule))
						{
							bParametersMatch = false;
							break;
						}
					}
					if (bParametersMatch)
					{
						return true;
					}
				}
				return false;
			}
			return false;
		};
		if (CallableIsFriend(Program.Structs) || CallableIsFriend(Program.Classes) || CallableIsFriend(Program.Interfaces))
		{
			return true;
		}
		if (Access == EMemberAccess::Private)
		{
			return false;
		}
		// Protected: visible in derived types.
		std::vector<std::string> Hierarchy;
		CollectTypeHierarchy(Program, AccessContextType, Hierarchy);
		for (const std::string& Ancestor : Hierarchy)
		{
			if (SameAccessOwner(Program, Ancestor, DeclaringTypeName))
			{
				return true;
			}
		}
		return false;
	}

	bool IsHierarchyMemberAccessible(
	    const FProgram& Program,
	    const std::string_view RootTypeName,
	    const std::string_view DeclaringTypeName,
	    const EMemberAccess DeclaredAccess,
	    const std::string_view AccessContextType,
	    const FCallableAccessContext* const CallableContext)
	{
		if (RootTypeName.empty() || DeclaringTypeName.empty())
		{
			return IsMemberAccessibleFromContext(
			    Program,
			    DeclaringTypeName,
			    DeclaredAccess,
			    AccessContextType,
			    CallableContext);
		}
		std::vector<std::string> Hierarchy;
		std::unordered_map<std::string, EMemberAccess> PathAccessByType;
		std::unordered_map<std::string, std::string> ParentByType;
		std::unordered_map<std::string, EMemberAccess> InheritFromParentByType;
		CollectTypeHierarchyWithPathAccess(
		    Program,
		    RootTypeName,
		    Hierarchy,
		    PathAccessByType,
		    ParentByType,
		    InheritFromParentByType);
		(void)PathAccessByType;
		if (Hierarchy.empty())
		{
			return false;
		}
		const std::string& ResolvedRoot = Hierarchy.front();

		std::string MatchedDeclaring;
		for (const std::string& TypeName : Hierarchy)
		{
			if (SameAccessOwner(Program, TypeName, DeclaringTypeName))
			{
				MatchedDeclaring = TypeName;
				break;
			}
		}
		if (MatchedDeclaring.empty())
		{
			return IsMemberAccessibleFromContext(
			    Program,
			    DeclaringTypeName,
			    DeclaredAccess,
			    AccessContextType,
			    CallableContext);
		}

		EMemberAccess EffectiveAccess = DeclaredAccess;
		std::string AccessOwner;
		ResolveInheritedMemberAccess(
		    MatchedDeclaring,
		    DeclaredAccess,
		    ResolvedRoot,
		    ParentByType,
		    InheritFromParentByType,
		    EffectiveAccess,
		    AccessOwner);
		if (EffectiveAccess == EMemberAccess::Protected)
		{
			// A friend declared by the receiver type shares that type's protected
			// access to inherited state. Checking the resolved root also preserves
			// the C++ object-type restriction instead of granting access through an
			// arbitrary base object.
			if (IsMemberAccessibleFromContext(
			        Program,
			        ResolvedRoot,
			        EMemberAccess::Protected,
			        AccessContextType,
			        CallableContext))
			{
				return true;
			}
		}
		return IsMemberAccessibleFromContext(
		    Program,
		    AccessOwner,
		    EffectiveAccess,
		    AccessContextType,
		    CallableContext);
	}

	namespace
	{
		std::vector<const FDocumentSymbol*> ScopedTypeMembers(const std::vector<FDocumentSymbol>& Symbols,
		    const FDocumentSymbol& Type)
		{
			std::vector<const FDocumentSymbol*> Result;
			for (const auto& Member : Type.Children)
			{
				Result.push_back(&Member);
			}
			for (const auto& Member : Symbols)
			{
				if (!Member.TypeScope.OwnerTypeName.empty() && Member.TypeScope.ScopeId == Type.TypeScope.Id &&
				    Member.TypeScope.OwnerTypeName == Type.TypeScope.QualifiedName &&
				    Member.TypeScope.ModulePath == Type.TypeScope.ModulePath)
				{
					Result.push_back(&Member);
				}
			}
			return Result;
		}
	} // namespace

	void AppendCompletionMembersAlongHierarchy(
	    const FProgram& Program,
	    const std::vector<FDocumentSymbol>& Symbols,
	    const std::string_view RootTypeName,
	    const std::string_view Prefix,
	    std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel,
	    const std::string_view AccessContextType,
	    const FCallableAccessContext* const CallableContext,
	    const bool bResolveValueTypes)
	{
		std::vector<std::string> Hierarchy;
		std::unordered_map<std::string, EMemberAccess> PathAccessByType;
		std::unordered_map<std::string, std::string> ParentByType;
		std::unordered_map<std::string, EMemberAccess> InheritFromParentByType;
		CollectTypeHierarchyWithPathAccess(
		    Program,
		    RootTypeName,
		    Hierarchy,
		    PathAccessByType,
		    ParentByType,
		    InheritFromParentByType);
		(void)PathAccessByType;
		if (Hierarchy.empty())
		{
			return;
		}
		const std::string& ResolvedRoot = Hierarchy.front();
		std::unordered_set<std::string> NamesHiddenByCloserType;
		for (const std::string& TypeName : Hierarchy)
		{
			const FDocumentSymbol* TypeSymbol = FindTypeSymbolInTree(Symbols, TypeName);
			if (!TypeSymbol || TypeSymbol->Kind == EDocumentSymbolKind::TypeAlias)
			{
				continue;
			}
			std::unordered_set<std::string> NamesDeclaredOnThisType;
			for (const auto* MemberSymbol : ScopedTypeMembers(Symbols, *TypeSymbol))
			{
				const auto& Member = *MemberSymbol;
				if (Member.Kind == EDocumentSymbolKind::Constructor || Member.Kind == EDocumentSymbolKind::Destructor)
				{
					continue;
				}
				// Lookup stops at the closest declaration even when that declaration
				// is inaccessible. Visibility must not expose a hidden base member.
				const std::string Label =
				    Member.TypeScope.Id.empty() ? CompletionLabel(Member.Name) : Member.TypeScope.SimpleName;
				NamesDeclaredOnThisType.insert(Label);
				EMemberAccess EffectiveAccess = Member.Access;
				std::string AccessOwner;
				ResolveInheritedMemberAccess(
				    TypeName,
				    Member.Access,
				    ResolvedRoot,
				    ParentByType,
				    InheritFromParentByType,
				    EffectiveAccess,
				    AccessOwner);
				bool bAccessible = IsMemberAccessibleFromContext(
				    Program,
				    AccessOwner,
				    EffectiveAccess,
				    AccessContextType,
				    CallableContext);
				if (!bAccessible && EffectiveAccess == EMemberAccess::Protected)
				{
					bAccessible = IsMemberAccessibleFromContext(
					    Program,
					    ResolvedRoot,
					    EMemberAccess::Protected,
					    AccessContextType,
					    CallableContext);
				}
				if (!bAccessible)
				{
					continue;
				}
				if (!PrefixMatches(Label, Prefix) || Label.empty())
				{
					continue;
				}
				if (NamesHiddenByCloserType.find(Label) != NamesHiddenByCloserType.end())
				{
					continue;
				}
				FInheritedMemberCompletion Entry;
				Entry.Kind = Member.Kind;
				Entry.Label = Label;
				Entry.bIsStatic = Member.bIsStatic;
				Entry.Detail = SpecializeMemberDetail(Program, TypeName, Member.Detail, TypeSymbol);
				if (bResolveValueTypes && !Member.TypeScope.Id.empty())
				{
					Entry.ValueType = Member.TypeScope.QualifiedName;
				}
				else if (bResolveValueTypes && Member.bSoftBuiltin)
				{
					Entry.ValueType = TypeNameFromDetail(Entry.Detail);
				}
				else if (bResolveValueTypes)
				{
					std::string Head;
					std::vector<std::string> Args, Params;
					std::vector<std::pair<std::string, std::string>> Bindings;
					if (SplitTypeApp(TypeName, Head, Args) && LookupTypeParams(Program, TypeName, Params, &Args))
					{
						for (std::size_t Index = 0; Index < Params.size() && Index < Args.size(); ++Index)
						{
							Bindings.emplace_back(Params[Index], Args[Index]);
						}
					}

					Entry.ValueType = ResolveDeclarationTypeName(Program, TypeName, TypeSymbol->FileLabel,
					    TypeNameFromDetail(Member.Detail), Bindings);
				}
				Entry.Documentation = Member.Documentation;
				Entry.bSoftBuiltin = Member.bSoftBuiltin;
				const auto Existing = OutMembersByLabel.find(Label);
				const bool bDistinctCallableOverload = Existing != OutMembersByLabel.end() && (Entry.Kind == EDocumentSymbolKind::Method || Entry.Kind == EDocumentSymbolKind::Function) && (Existing->second.Kind == EDocumentSymbolKind::Method || Existing->second.Kind == EDocumentSymbolKind::Function) && Existing->second.Detail != Entry.Detail;
				const std::string Key = bDistinctCallableOverload
				    ? Label + '\x1f' + Entry.Detail
				    : Label;
				OutMembersByLabel.emplace(Key, std::move(Entry));
			}
			NamesHiddenByCloserType.insert(
			    NamesDeclaredOnThisType.begin(), NamesDeclaredOnThisType.end());
		}
	}

	bool CollectMemberHitsAlongHierarchy(
	    const FProgram& Program,
	    const std::vector<FDocumentSymbol>& Symbols,
	    const std::string_view RootTypeName,
	    const std::string_view MemberName,
	    std::vector<FSymbolQueryHit>& OutHits)
	{
		std::vector<std::string> Hierarchy;
		CollectTypeHierarchy(Program, RootTypeName, Hierarchy);
		for (const std::string& TypeName : Hierarchy)
		{
			const FDocumentSymbol* TypeSymbol = FindTypeSymbolInTree(Symbols, TypeName);
			if (!TypeSymbol || TypeSymbol->Kind == EDocumentSymbolKind::TypeAlias)
			{
				continue;
			}
			bool bFoundOnThisType = false;
			for (const auto* MemberSymbol : ScopedTypeMembers(Symbols, *TypeSymbol))
			{
				const auto& Member = *MemberSymbol;
				if (Member.Kind == EDocumentSymbolKind::Constructor || Member.Kind == EDocumentSymbolKind::Destructor)
				{
					continue;
				}
				if (!NameMatchesSymbol(Member.Name, MemberName))
				{
					continue;
				}
				FSymbolQueryHit Hit;
				Hit.Kind = Member.Kind;
				Hit.Name = Member.Name;
				Hit.Detail = SpecializeMemberDetail(Program, TypeName, Member.Detail, TypeSymbol);
				Hit.Documentation = Member.Documentation;
				Hit.bHasSourceDocumentation = Member.bHasSourceDocumentation;
				// Prefer specialized instance name (`IA<int>`) over open decl (`IA<T>`).
				Hit.ContainerName = TypeName;
				Hit.FileLabel = Member.FileLabel;
				Hit.DefLine = Member.StartLine;
				Hit.DefColumn = Member.StartColumn;
				Hit.SourceImplementation = Member.SourceImplementation;
				Hit.bExported = Member.bExported;
				Hit.bSoftBuiltin = Member.bSoftBuiltin;
				Hit.bReadOnly = Member.bReadOnly;
				OutHits.push_back(std::move(Hit));
				bFoundOnThisType = true;
			}
			if (bFoundOnThisType)
			{
				return true;
			}
		}
		return false;
	}
}
