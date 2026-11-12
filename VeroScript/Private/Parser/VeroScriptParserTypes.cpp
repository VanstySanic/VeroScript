// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptParserLookup.h"

#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace VeroScript
{
	namespace
	{
		template <typename TDeclarations, typename TIndex>
		const typename TDeclarations::value_type* FindVisibleTypeDeclaration(
		    const TDeclarations& Declarations, const TIndex& Index, const std::string& Name,
		    const std::string_view FileLabel, const std::unordered_set<std::string>& VisibleModulePaths)
		{
			const auto Found = Index.find(Name);
			if (Found == Index.end()) return nullptr;
			for (const auto Position : Found->second)
			{
				const auto& Declaration = Declarations[Position];
				if (ParserDetail::IsDeclVisible(Declaration.ModulePath, Declaration.bExported,
				        FileLabel, &VisibleModulePaths))
				{
					return &Declaration;
				}
			}
			return nullptr;
		}
	}

	std::string VeroScriptParser::ParseReturnTypeName()
	{
		const bool bConst = Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
		if (bConst)
		{
			Advance();
		}
		// Outlines contain `ref T`; ordinary source returns retain the `T&` grammar.
		const bool bCanonicalRef = bCanonicalTypeFragment && MatchOptionalRef();
		std::string TypeName = ParseTypeName();
		if (bHasError || TypeName.empty())
		{
			return {};
		}
		const bool bRef = bCanonicalRef || MatchSymbol("&");
		if (bRef)
		{
			if (TypeName == "void")
			{
				ReportErrorAt(Previous(), "void reference return type is invalid");
				return {};
			}
			return bConst ? "const ref " + TypeName : "ref " + TypeName;
		}
		// Top-level const on a by-value return has no observable script semantics,
		// matching C++ call sites; retain const only when it qualifies a reference.
		return TypeName;
	}

	bool VeroScriptParser::IsUserTypeName(const std::string_view Name, const std::size_t AliasDepth) const
	{
		// A dependent nested name is a valid type spelling before instantiation.
		const auto RootEnd = Name.find_first_of(".<");
		if (ActiveTypeParams.count(std::string(Name.substr(0, RootEnd)))) return true;
		if (Name == "variant" || Name == "std.variant" || Name == "std::variant") return true;
		// The current type is not added to Program until its body has been parsed.
		for (auto Frame = TypeFrames.rbegin(); Frame != TypeFrames.rend(); ++Frame)
		{
			if (Name == Frame->Name || Name == Frame->Scope.DisplayName || Name == Frame->Scope.SimpleName)
			{
				return true;
			}
		}
		for (auto ScopeIt = LocalTypeAliasScopes.rbegin();
		    ScopeIt != LocalTypeAliasScopes.rend();
		    ++ScopeIt)
		{
			if (ScopeIt->find(std::string(Name)) != ScopeIt->end())
			{
				return true;
			}
		}
		const auto CheckProgram = [&](const FProgram& TargetProgram) -> bool
		{
			const auto& Lookup = GetParsedTypeLookup(TargetProgram);
			const std::string Key(Name);
			for (auto Dot = Name.find('.'); Dot != std::string_view::npos; Dot = Name.find('.', Dot + 1))
			{
				const auto* Alias = FindVisibleTypeDeclaration(TargetProgram.TypeAliases, Lookup.AliasPrefixes,
				    Key.substr(0, Dot), FileLabel, VisibleModulePaths);
				if (!Alias || AliasDepth >= 32)
				{
					continue;
				}
				// An alias qualifier can name either a nested type or an ordinary
				// member. Only a declared type tail may enter constructor grammar.
				std::vector<FTypePathSegment> Segments;
				if (!ParseTypePath(Alias->TypeName, Segments))
				{
					continue;
				}
				std::string Target;
				for (const auto& Segment : Segments)
				{
					if (!Target.empty())
					{
						Target += '.';
					}
					Target += Segment.Name;
				}
				Target += Name.substr(Dot);
				if (IsUserTypeName(Target, AliasDepth + 1)) return true;
			}
			return FindVisibleTypeDeclaration(TargetProgram.Structs, Lookup.Structs, Key, FileLabel, VisibleModulePaths) || FindVisibleTypeDeclaration(TargetProgram.Classes, Lookup.Classes, Key, FileLabel, VisibleModulePaths) || FindVisibleTypeDeclaration(TargetProgram.Interfaces, Lookup.Interfaces, Key, FileLabel, VisibleModulePaths) || FindVisibleTypeDeclaration(TargetProgram.Enums, Lookup.Enums, Key, FileLabel, VisibleModulePaths) || FindVisibleTypeDeclaration(TargetProgram.TypeAliases, Lookup.Aliases, Key, FileLabel, VisibleModulePaths);
		};

		if (Program && CheckProgram(*Program))
		{
			return true;
		}
		if (ExternalTypeProgram && CheckProgram(*ExternalTypeProgram))
		{
			return true;
		}
		if (IsForwardTypeName(std::string(Name)))
		{
			return true;
		}
		if (Program)
		{
			const auto Separator = Name.rfind('.');
			if (Separator != std::string_view::npos &&
			    IsForwardTypeName(std::string(Name.substr(0, Separator))))
			{
				const std::string Owner(Name.substr(0, Separator));
				std::vector<std::string> Arguments;
				for (auto Frame = TypeFrames.rbegin(); Frame != TypeFrames.rend(); ++Frame)
				{
					if (Frame->Name == Owner)
					{
						Arguments = Frame->TemplateParameters.IsEmpty()
						    ? Frame->Parameters
						    : Frame->TemplateParameters.GetNames();
						break;
					}
				}
				const auto Binding = FindScopedTypeMember(Owner, Arguments, std::string(Name.substr(Separator + 1)));
				if (!Binding.Name.empty() || Binding.bAmbiguous)
				{
					return true;
				}
				const auto CheckOwner = [&](const FProgram& Target, const auto& Declarations,
				                            const ETypeMemberDeclarationKind Kind)
				{
					for (const auto Position : GetParsedTypeLookup(Target).Names(Kind, Owner))
					{
						const auto& Declaration = Declarations[Position];
						if (Declaration.Name == Name.substr(0, Separator))
						{
							// The concrete attempt above already queried this exact
							// owner/member/context; only a different symbolic schema can help.
							if (TemplateParameterNames(Declaration) == Arguments)
							{
								continue;
							}
							const auto Symbolic = FindScopedTypeMember(Declaration.Name,
							    TemplateParameterNames(Declaration), std::string(Name.substr(Separator + 1)));
							if (!Symbolic.Name.empty() || Symbolic.bAmbiguous)
							{
								return true;
							}
						}
					}
					return false;
				};
				if (CheckOwner(*Program, Program->Classes, ETypeMemberDeclarationKind::Class) || CheckOwner(*Program, Program->Structs, ETypeMemberDeclarationKind::Struct) || CheckOwner(*Program, Program->Interfaces, ETypeMemberDeclarationKind::Interface))
				{
					return true;
				}
				if (ExternalTypeProgram &&
				    (CheckOwner(*ExternalTypeProgram, ExternalTypeProgram->Classes, ETypeMemberDeclarationKind::Class) || CheckOwner(*ExternalTypeProgram, ExternalTypeProgram->Structs, ETypeMemberDeclarationKind::Struct) || CheckOwner(*ExternalTypeProgram, ExternalTypeProgram->Interfaces, ETypeMemberDeclarationKind::Interface)))
				{
					return true;
				}
			}
		}
		if (HostTypeNames.find(std::string(Name)) != HostTypeNames.end())
		{
			return true;
		}
		// Soft builtins (arity-matched soft-yield in the compiler): pair<K,V> / handle<T> /
		// delegate<R[,A[,B]]> / multicast_delegate[[A[,B]]] (delegate.vs).
		// Soft-listed so generic type apps parse even when soft injection is deferred.
		// Soft-yield is by type-param arity only — non-generic `struct pair` can coexist.
		return Name == "pair" || Name == "handle" || Name == "optional" || Name == "readonly" || Name == "type" || Name == "field_info" || Name == "method_info" || Name == "module_info" || Name == "function_info" || Name == "global_info" || Name == "enum_member_info" || Name == "source_node_info" || Name == "annotation_info" || Name == "annotation_argument_info" || Name == "comment_info" || Name == "annotation_definition_info" || Name == "weak" || Name == "delegate" || Name == "multicast_delegate";
	}

	bool VeroScriptParser::IsInterfaceName(const std::string_view Name) const
	{
		const auto CheckProgram = [&](
		                              const FProgram& TargetProgram,
		                              const std::string_view Candidate) -> bool
		{
			return FindVisibleTypeDeclaration(TargetProgram.Interfaces,
			           GetParsedTypeLookup(TargetProgram).Interfaces, std::string(Candidate), FileLabel,
			           VisibleModulePaths) != nullptr;
		};
		const auto IsDirectInterface = [&](const std::string_view Candidate) -> bool
		{
			if (Program && CheckProgram(*Program, Candidate))
			{
				return true;
			}
			if (ExternalTypeProgram && CheckProgram(*ExternalTypeProgram, Candidate))
			{
				return true;
			}
			return IsForwardInterfaceName(std::string(Candidate));
		};
		if (IsDirectInterface(Name))
		{
			return true;
		}
		// `using I = IFoo;` / `typedef` — one-level alias expand (no recursive chains).
		const auto ResolveAliasTarget = [&](const FProgram& TargetProgram) -> std::string
		{
			const auto* Alias = FindVisibleTypeDeclaration(TargetProgram.TypeAliases,
			    GetParsedTypeLookup(TargetProgram).Aliases, std::string(Name), FileLabel, VisibleModulePaths);
			return Alias ? Alias->TypeName : std::string{};
		};
		std::string AliasTarget;
		if (Program)
		{
			AliasTarget = ResolveAliasTarget(*Program);
		}
		if (AliasTarget.empty() && ExternalTypeProgram)
		{
			AliasTarget = ResolveAliasTarget(*ExternalTypeProgram);
		}
		if (AliasTarget.empty())
		{
			return false;
		}
		std::string Leaf = std::move(AliasTarget);
		const std::string::size_type Lt = Leaf.find('<');
		if (Lt != std::string::npos)
		{
			Leaf = Leaf.substr(0, Lt);
		}
		const std::string::size_type Dot = Leaf.rfind('.');
		if (Dot != std::string::npos)
		{
			Leaf = Leaf.substr(Dot + 1);
		}
		return Leaf != Name && IsDirectInterface(Leaf);
	}

	bool VeroScriptParser::IsStructOrClassName(const std::string_view Name) const
	{
		if (IsInterfaceName(Name))
		{
			return false;
		}
		const auto CheckProgram = [&](const FProgram& TargetProgram) -> bool
		{
			const auto& Lookup = GetParsedTypeLookup(TargetProgram);
			const std::string Key(Name);
			return FindVisibleTypeDeclaration(TargetProgram.Structs, Lookup.Structs, Key, FileLabel, VisibleModulePaths) || FindVisibleTypeDeclaration(TargetProgram.Classes, Lookup.Classes, Key, FileLabel, VisibleModulePaths);
		};
		if (Program && CheckProgram(*Program))
		{
			return true;
		}
		if (ExternalTypeProgram && CheckProgram(*ExternalTypeProgram))
		{
			return true;
		}
		// Soft builtins (arity-matched soft-yield): pair / handle / delegate / multicast_delegate
		// stay parseable as type names even when a non-matching-arity user type shares the name.
		if (Name == "pair" || Name == "handle" || Name == "delegate" || Name == "multicast_delegate")
		{
			return true;
		}
		// Forward-scanned struct/class/enum/interface names — exclude interfaces and enums.
		if (IsForwardInterfaceName(std::string(Name)))
		{
			return false;
		}
		if (!IsForwardTypeName(std::string(Name)))
		{
			return false;
		}
		// Enum names are in ForwardTypeNames; reject if already known as enum in program.
		if (Program && GetParsedTypeLookup(*Program).Enums.count(std::string(Name)) != 0) return false;
		return true;
	}

	bool VeroScriptParser::ParseArraySuffix(std::string& InOutTypeName)
	{
		if (Runtime::IsDynamicArrayTypeName(InOutTypeName) || Runtime::IsCommonContainerTypeName(InOutTypeName))
		{
			if (CheckSymbol("["))
			{
				ReportErrorAt(Peek(), "container type cannot have a fixed size suffix");
				return false;
			}
			return true;
		}

		// C++-style multi-dim: `int xs[2][3]` → type "int[2][3]" (leftmost = outer).
		while (CheckSymbol("["))
		{
			if (!MatchSymbol("["))
			{
				return false;
			}

			const auto ExtentLocation = Peek().Location;
			const std::string Extent = ParseTemplateValueSource();
			if (bHasError || Extent.empty()) return false;
			VeroScriptLexer Lexer(Extent, FileLabel);
			VeroScriptParser Parser(Lexer.Tokenize(), FileLabel, Program.get(), VisibleModulePaths);
			auto Expression = Parser.ParseExpressionFragment(ActiveNamespacePrefix,
			    std::vector<std::string>(ActiveTypeParams.begin(), ActiveTypeParams.end()));
			if (!Expression || Lexer.HasError() || Parser.HasError())
			{
				ReportErrorAt(Peek(), "invalid array extent expression");
				return false;
			}
			FTemplateEvaluationContext Context;
			Context.ResolveCall = [](const FExpr&, const std::vector<FTemplateEvaluationResult>&)
			{
				FConstantFunction Function;
				Function.Status = ETemplateEvaluationStatus::Dependent;
				return Function;
			};
			Context.ResolveName = [](std::string_view, const FSourceLocation&)
			{
				FTemplateConstantSymbol Symbol;
				Symbol.Status = ETemplateEvaluationStatus::Dependent;
				return Symbol;
			};
			// Function-body extents are semantic requirements of the selected
			// instance. Parse their syntax now, evaluate after constexpr pruning.
			FTemplateEvaluationResult Value;
			if (!FunctionOwner.empty() || !LocalDeclarationPrefix.empty())
			{
				Value.Status = ETemplateEvaluationStatus::Dependent;
			}
			else
			{
				Value = EvaluateTemplateConstant(*Expression, Context);
			}
			std::string SizeSource = Extent;
			if (Value.Succeeded())
			{
				FTemplateValueType Integer;
				TryGetTemplateValueType("int", Integer);
				const auto Size = ConvertTemplateConstant(Value.Value, Integer, false, ExtentLocation);
				if (!Size.Succeeded() || Value.Value.Type.bBoolean || Value.Value.Type.bEnum || Size.Value.Bits == 0 || Size.Value.Bits > std::numeric_limits<std::int32_t>::max())
				{
					ReportErrorAt(Peek(), "array size must be a positive integral constant within range");
					return false;
				}
				SizeSource = std::to_string(Size.Value.Bits);
			}
			else if (Value.Status != ETemplateEvaluationStatus::Dependent)
			{
				ReportErrorAt(Peek(), "invalid array extent: " + Value.Reason);
				return false;
			}

			if (!MatchSymbol("]"))
			{
				ReportErrorAt(Peek(), "expected ']' after array size");
				return false;
			}

			InOutTypeName.push_back('[');
			InOutTypeName.append(SizeSource);
			InOutTypeName.push_back(']');
		}
		std::string ArrayElement;
		std::uint32_t ArrayLength = 0;
		if (Runtime::ParseNativeArrayType(InOutTypeName, ArrayElement, ArrayLength))
		{
			std::string LimitError;
			if (!Runtime::ValidateNativeArrayTypeLimits(InOutTypeName, &LimitError))
			{
				ReportErrorAt(Previous(), LimitError);
				return false;
			}
		}
		return true;
	}

	bool VeroScriptParser::ParseParamArraySuffix(std::string& InOutTypeName)
	{
		// Parameter forms: `int[2][3] xs`, or script-args `string[] args` (single [] only).
		while (CheckSymbol("["))
		{
			if (!MatchSymbol("["))
			{
				return false;
			}

			if (CheckSymbol("]"))
			{
				if (!MatchSymbol("]"))
				{
					return false;
				}
				if (Runtime::IsNativeArrayTypeName(InOutTypeName) || Runtime::IsScriptArgsArrayTypeName(InOutTypeName))
				{
					ReportErrorAt(Previous(), "cannot mix fixed-size and [] array suffixes");
					return false;
				}
				InOutTypeName += "[]";
				if (CheckSymbol("["))
				{
					ReportErrorAt(Peek(), "jagged [] array types are not supported");
					return false;
				}
				return true;
			}

			const auto ExtentLocation = Peek().Location;
			const std::string Extent = ParseTemplateValueSource();
			if (bHasError || Extent.empty()) return false;
			VeroScriptLexer Lexer(Extent, FileLabel);
			VeroScriptParser Parser(Lexer.Tokenize(), FileLabel, Program.get(), VisibleModulePaths);
			auto Expression = Parser.ParseExpressionFragment(ActiveNamespacePrefix,
			    std::vector<std::string>(ActiveTypeParams.begin(), ActiveTypeParams.end()));
			if (!Expression || Lexer.HasError() || Parser.HasError())
			{
				ReportErrorAt(Peek(), "invalid array extent expression");
				return false;
			}
			FTemplateEvaluationContext Context;
			Context.ResolveCall = [](const FExpr&, const std::vector<FTemplateEvaluationResult>&)
			{
				FConstantFunction Function;
				Function.Status = ETemplateEvaluationStatus::Dependent;
				return Function;
			};
			Context.ResolveName = [](std::string_view, const FSourceLocation&)
			{
				FTemplateConstantSymbol Symbol;
				Symbol.Status = ETemplateEvaluationStatus::Dependent;
				return Symbol;
			};
			// Function-body extents are semantic requirements of the selected
			// instance. Parse their syntax now, evaluate after constexpr pruning.
			FTemplateEvaluationResult Value;
			if (!FunctionOwner.empty() || !LocalDeclarationPrefix.empty())
			{
				Value.Status = ETemplateEvaluationStatus::Dependent;
			}
			else
			{
				Value = EvaluateTemplateConstant(*Expression, Context);
			}
			std::string SizeSource = Extent;
			if (Value.Succeeded())
			{
				FTemplateValueType Integer;
				TryGetTemplateValueType("int", Integer);
				const auto Size = ConvertTemplateConstant(Value.Value, Integer, false, ExtentLocation);
				if (!Size.Succeeded() || Value.Value.Type.bBoolean || Value.Value.Type.bEnum || Size.Value.Bits == 0 || Size.Value.Bits > std::numeric_limits<std::int32_t>::max())
				{
					ReportErrorAt(Peek(), "array size must be a positive integral constant within range");
					return false;
				}
				SizeSource = std::to_string(Size.Value.Bits);
			}
			else if (Value.Status != ETemplateEvaluationStatus::Dependent)
			{
				ReportErrorAt(Peek(), "invalid array extent: " + Value.Reason);
				return false;
			}

			if (!MatchSymbol("]"))
			{
				ReportErrorAt(Peek(), "expected ']' after array size");
				return false;
			}

			InOutTypeName.push_back('[');
			InOutTypeName.append(SizeSource);
			InOutTypeName.push_back(']');
		}
		std::string ArrayElement;
		std::uint32_t ArrayLength = 0;
		if (Runtime::ParseNativeArrayType(InOutTypeName, ArrayElement, ArrayLength))
		{
			std::string LimitError;
			if (!Runtime::ValidateNativeArrayTypeLimits(InOutTypeName, &LimitError))
			{
				ReportErrorAt(Previous(), LimitError);
				return false;
			}
		}
		return true;
	}

	bool VeroScriptParser::ParseOptionalParamDefault(FFunctionParam& Param, const bool bAllowDefaults)
	{
		if (!MatchSymbol("="))
		{
			return true;
		}
		if (Param.bIsVariadic)
		{
			ReportErrorAt(Previous(), "variadic parameter cannot have a default value");
			return false;
		}
		if (!bAllowDefaults)
		{
			ReportErrorAt(Previous(), "parameter defaults are not allowed here");
			return false;
		}
		if (Param.bIsRef && !Param.bIsConst)
		{
			ReportErrorAt(Previous(), "mutable ref parameter cannot have a default value");
			return false;
		}
		const auto DefaultStart = Current;
		Param.DefaultValue = ParseExpression();
		for (auto I = DefaultStart; I < Current; ++I)
		{
			if (!Param.DefaultValueSource.empty())
			{
				Param.DefaultValueSource += " ";
			}
			Param.DefaultValueSource.append(Tokens[I].Lexeme);
		}
		if (!Param.DefaultValue)
		{
			return false;
		}
		return true;
	}

	bool VeroScriptParser::ParseOptionalParamVariadic(FFunctionParam& Param, const bool bAllowVariadic)
	{
		if (!MatchSymbol("..."))
		{
			return true;
		}
		if (!bAllowVariadic)
		{
			ReportErrorAt(Previous(), "variadic parameters are not allowed here");
			return false;
		}
		if (Param.bIsRef)
		{
			ReportErrorAt(Previous(), "ref parameter cannot be variadic");
			return false;
		}
		Param.bIsVariadic = true;
		return true;
	}

	bool VeroScriptParser::ParseOptionalWhereBounds(
	    const std::vector<std::string>& TypeParams,
	    std::vector<FTypeParamBound>& OutBounds,
	    const char* GenericKindNoun)
	{
		if (!(Check(ETokenKind::Keyword) && Peek().Lexeme == "where"))
		{
			return true;
		}
		Advance();
		if (TypeParams.empty())
		{
			ReportErrorAt(
			    Previous(),
			    std::string("'where' clause requires a generic ") + GenericKindNoun);
			return false;
		}

		auto IsDeclTypeParam = [&TypeParams](const std::string_view Name) -> bool
		{
			return std::find(TypeParams.begin(), TypeParams.end(), Name) != TypeParams.end();
		};

		do
		{
			if (!Match(ETokenKind::Identifier) || !IsDeclTypeParam(Previous().Lexeme))
			{
				ReportErrorAt(Peek(), "expected type parameter in 'where' clause");
				return false;
			}
			const std::string TypeParam(Previous().Lexeme);
			if (!MatchSymbol(":"))
			{
				ReportErrorAt(Peek(), "expected ':' after type parameter in 'where' clause");
				return false;
			}

			for (;;)
			{
				const std::string InterfaceTypeName = ParseTypeName();
				if (bHasError || InterfaceTypeName.empty())
				{
					if (!bHasError)
					{
						ReportErrorAt(Peek(), "expected interface type in 'where' clause");
					}
					return false;
				}
				FTypeParamBound Bound;
				Bound.TypeParam = TypeParam;
				Bound.InterfaceTypeName = InterfaceTypeName;
				OutBounds.push_back(std::move(Bound));

				if (!CheckSymbol(","))
				{
					break;
				}
				if (Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Identifier && IsDeclTypeParam(Tokens[Current + 1].Lexeme))
				{
					if (!(Current + 2 < Tokens.size() && Tokens[Current + 2].Kind == ETokenKind::Symbol && Tokens[Current + 2].Lexeme == ":"))
					{
						MatchSymbol(",");
						ReportErrorAt(
						    Tokens[Current],
						    "expected ':' after type parameter in 'where' clause");
						return false;
					}
					// Leave the comma for the outer `do/while (MatchSymbol(","))`.
					break;
				}
				if (!MatchSymbol(","))
				{
					break;
				}
			}
		}
		while (MatchSymbol(","));

		return true;
	}

	bool VeroScriptParser::ParseOptionalFunctionBounds(FFunctionDecl& Function)
	{
		return ParseOptionalWhereBounds(Function.TypeParams, Function.Bounds, "function");
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseArrayInitializer(
	    const std::string& TypeName,
	    const bool bExplicitTypeSyntax)
	{
		const FSourceLocation Location = Peek().Location;
		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' to begin array initializer");
			return nullptr;
		}

		std::string ElementType;
		const bool bKnowsElementType = TryGetBraceElementType(TypeName, ElementType);

		std::vector<std::unique_ptr<FExpr>> Elements;
		if (!CheckSymbol("}"))
		{
			for (;;)
			{
				std::unique_ptr<FExpr> Element;
				if (bKnowsElementType && CheckSymbol("{") && TypeSupportsBraceInitializer(ElementType))
				{
					Element = ParseTypedBraceInitializer(ElementType);
				}
				else
				{
					Element = ParseExpression();
				}
				if (!Element)
				{
					return nullptr;
				}
				Elements.push_back(std::move(Element));
				if (!MatchSymbol(",") || CheckSymbol("}"))
				{
					break;
				}
			}
		}

		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}' after array initializer");
			return nullptr;
		}

		auto Init = std::make_unique<FArrayInitExpr>(
		    std::move(Elements),
		    bExplicitTypeSyntax ? TypeName : std::string{});
		Init->Location = Location;
		return Init;
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseMapInitializer(
	    const std::string& TypeName,
	    const bool bExplicitTypeSyntax)
	{
		const FSourceLocation Location = Peek().Location;
		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' to begin map initializer");
			return nullptr;
		}

		std::string ValueType;
		const bool bKnowsValueType = TryGetBraceMapValueType(TypeName, ValueType);

		std::vector<FMapInitEntry> Entries;
		if (!CheckSymbol("}"))
		{
			for (;;)
			{
				std::unique_ptr<FExpr> Key = ParseExpression();
				if (!Key)
				{
					return nullptr;
				}
				if (!MatchSymbol(":"))
				{
					ReportErrorAt(Peek(), "expected ':' after map key in initializer");
					return nullptr;
				}
				std::unique_ptr<FExpr> Value;
				if (bKnowsValueType && CheckSymbol("{") && TypeSupportsBraceInitializer(ValueType))
				{
					Value = ParseTypedBraceInitializer(ValueType);
				}
				else
				{
					Value = ParseExpression();
				}
				if (!Value)
				{
					return nullptr;
				}
				FMapInitEntry Entry;
				Entry.Key = std::move(Key);
				Entry.Value = std::move(Value);
				Entries.push_back(std::move(Entry));
				if (!MatchSymbol(",") || CheckSymbol("}"))
				{
					break;
				}
			}
		}

		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}' after map initializer");
			return nullptr;
		}

		auto Init = std::make_unique<FMapInitExpr>(
		    std::move(Entries),
		    bExplicitTypeSyntax ? TypeName : std::string{});
		Init->Location = Location;
		return Init;
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseTypedBraceInitializer(
	    const std::string& TypeName,
	    const bool bExplicitTypeSyntax)
	{
		if (bDeclarationOutline)
		{
			auto R = ParseDeferredExpression();
			if (R)
			{
				auto D = std::make_shared<FDeferredSyntax>(*R->DeferredSyntax);
				D->InitializerType = TypeName;
				R->DeferredSyntax = std::move(D);
			}
			return R;
		}
		// Alias-based container types can recurse without adding generic syntax
		// or entering ParseExpression. Bound this path independently, so wide
		// initializers retain the existing per-element expression budget.
		struct FInitializerDepthGuard
		{
			~FInitializerDepthGuard()
			{
				--Depth;
			}
			int& Depth;
		} DepthGuard{InitializerParseDepth};
		if (++InitializerParseDepth > 128)
		{
			ReportErrorAt(Peek(), "initializer nested too deeply");
			return nullptr;
		}
		const std::string EffectiveTypeName = ResolveBraceInitializerTypeName(TypeName);
		if (Runtime::IsMapTypeName(EffectiveTypeName) || Runtime::IsOrderedMapTypeName(EffectiveTypeName))
		{
			return ParseMapInitializer(TypeName, bExplicitTypeSyntax);
		}
		return ParseArrayInitializer(TypeName, bExplicitTypeSyntax);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseTypedVarInitializer(
	    const std::string& TypeName,
	    const bool bAllowOmitEquals)
	{
		if (bDeclarationOutline && (CheckSymbol("=") || CheckSymbol("(") || CheckSymbol("{")))
		{
			auto R = ParseDeferredExpression();
			if (R)
			{
				auto D = std::make_shared<FDeferredSyntax>(*R->DeferredSyntax);
				D->InitializerType = TypeName;
				D->bVariableInitializer = true;
				R->DeferredSyntax = std::move(D);
			}
			return R;
		}
		const bool bSupportsBrace = TypeSupportsBraceInitializer(TypeName);

		if (CheckSymbol("("))
		{
			if (Runtime::IsNativeArrayTypeName(TypeName))
			{
				ReportErrorAt(
				    Peek(),
				    "native array direct initialization is not supported; use '= {...}'");
				return nullptr;
			}
			const FSourceLocation Location = Peek().Location;
			std::unique_ptr<FExpr> Initializer = FinishCall(TypeName, Location);
			if (!Initializer)
			{
				return nullptr;
			}

			const std::string ResolvedTypeName = ResolveBraceInitializerTypeName(TypeName);
			const std::string CanonicalTypeName(
			    CanonicalizePrimitiveTypeKeyword(ResolvedTypeName));
			const bool bScalarType =
			    CanonicalTypeName == "bool" || CanonicalTypeName == "char" || CanonicalTypeName == "short" || CanonicalTypeName == "int" || CanonicalTypeName == "long" || CanonicalTypeName == "float" || CanonicalTypeName == "double" || CanonicalTypeName == "int64" || CanonicalTypeName == "uint64" || CanonicalTypeName == "string" || CanonicalTypeName == "wstring" || CanonicalTypeName == "unsigned char" || CanonicalTypeName == "unsigned short" || CanonicalTypeName == "unsigned int" || CanonicalTypeName == "unsigned long";
			const bool bAutoType = CanonicalTypeName == "auto";
			if (!bScalarType && !bAutoType)
			{
				return Initializer;
			}

			FCallExpr& DirectCall = static_cast<FCallExpr&>(*Initializer);
			if (DirectCall.Arguments.size() > 1)
			{
				ReportErrorAt(
				    Peek(),
				    "direct initialization of '" + TypeName + "' accepts at most one argument");
				return nullptr;
			}
			if (DirectCall.Arguments.size() == 1)
			{
				return std::move(DirectCall.Arguments.front());
			}
			if (bAutoType)
			{
				ReportErrorAt(
				    Peek(),
				    "auto direct initialization requires exactly one argument");
				return nullptr;
			}

			std::unique_ptr<FExpr> DefaultValue;
			if (CanonicalTypeName == "bool")
			{
				DefaultValue = std::make_unique<FBooleanLiteralExpr>(false);
			}
			else if (CanonicalTypeName == "float" || CanonicalTypeName == "double")
			{
				DefaultValue = std::make_unique<FFloatLiteralExpr>(0.0);
			}
			else if (CanonicalTypeName == "string")
			{
				DefaultValue = std::make_unique<FStringLiteralExpr>(std::string{});
			}
			else if (CanonicalTypeName == "wstring")
			{
				DefaultValue = std::make_unique<FWStringLiteralExpr>(std::u16string{});
			}
			else
			{
				DefaultValue = std::make_unique<FIntegerLiteralExpr>(std::int32_t{0});
			}
			DefaultValue->Location = Location;
			return DefaultValue;
		}

		if (!MatchSymbol("="))
		{
			if (bAllowOmitEquals || ResolveBraceInitializerTypeName(TypeName) == "variant" || StandardLibrary::ResolveTypeName(ResolveBraceInitializerTypeName(TypeName)) == "variant")
			{
				return nullptr;
			}
			ReportErrorAt(Peek(), "expected '=' in variable declaration");
			return nullptr;
		}

		if (CheckSymbol("{") && bSupportsBrace)
		{
			return ParseTypedBraceInitializer(TypeName);
		}
		return ParseExpression();
	}

	std::string VeroScriptParser::ResolveBraceInitializerTypeName(std::string TypeName) const
	{
		std::unordered_set<std::string> Visited;
		while (Visited.insert(TypeName).second)
		{
			bool bResolvedLocal = false;
			for (auto ScopeIt = LocalTypeAliasScopes.rbegin();
			    ScopeIt != LocalTypeAliasScopes.rend();
			    ++ScopeIt)
			{
				const auto AliasIt = ScopeIt->find(TypeName);
				if (AliasIt != ScopeIt->end())
				{
					TypeName = AliasIt->second;
					bResolvedLocal = true;
					break;
				}
			}
			if (bResolvedLocal)
			{
				continue;
			}
			const auto FindAlias = [&](const FProgram& TargetProgram) -> const FTypeAliasDecl*
			{
				for (const FTypeAliasDecl& AliasDecl : TargetProgram.TypeAliases)
				{
					if (AliasDecl.Name == TypeName && ParserDetail::IsDeclVisible(AliasDecl.ModulePath, AliasDecl.bExported, FileLabel, &VisibleModulePaths))
					{
						return &AliasDecl;
					}
				}
				return nullptr;
			};
			const FTypeAliasDecl* AliasDecl = Program ? FindAlias(*Program) : nullptr;
			if (!AliasDecl && ExternalTypeProgram)
			{
				AliasDecl = FindAlias(*ExternalTypeProgram);
			}
			if (!AliasDecl || AliasDecl->TypeName.empty())
			{
				break;
			}
			TypeName = AliasDecl->TypeName;
		}
		return TypeName;
	}

	bool VeroScriptParser::TypeSupportsBraceInitializer(const std::string& TypeName) const
	{
		const std::string EffectiveTypeName = ResolveBraceInitializerTypeName(TypeName);
		return Runtime::IsNativeArrayTypeName(EffectiveTypeName) || Runtime::IsDynamicArrayTypeName(EffectiveTypeName) || Runtime::IsMapTypeName(EffectiveTypeName) || Runtime::IsOrderedMapTypeName(EffectiveTypeName) || Runtime::IsSetTypeName(EffectiveTypeName) || Runtime::IsMultiSetTypeName(EffectiveTypeName) || Runtime::IsOrderedMultiSetTypeName(EffectiveTypeName) || Runtime::IsDequeTypeName(EffectiveTypeName) || Runtime::IsQueueTypeName(EffectiveTypeName) || Runtime::IsStackTypeName(EffectiveTypeName);
	}

	bool VeroScriptParser::TryGetBraceElementType(const std::string& TypeName, std::string& OutElementType) const
	{
		const std::string EffectiveTypeName = ResolveBraceInitializerTypeName(TypeName);
		std::uint32_t Length = 0;
		if (Runtime::IsNativeArrayTypeName(EffectiveTypeName))
		{
			return Runtime::ParseNativeArrayType(EffectiveTypeName, OutElementType, Length);
		}
		if (Runtime::IsDynamicArrayTypeName(EffectiveTypeName))
		{
			return Runtime::ParseDynamicArrayElementType(EffectiveTypeName, OutElementType);
		}
		if (Runtime::IsSetTypeName(EffectiveTypeName))
		{
			return Runtime::SplitSetElementType(EffectiveTypeName, OutElementType);
		}
		if (Runtime::IsMultiSetTypeName(EffectiveTypeName) || Runtime::IsOrderedMultiSetTypeName(EffectiveTypeName))
		{
			return Runtime::ParseMultiSetElementType(
			    EffectiveTypeName,
			    OutElementType);
		}
		if (Runtime::IsDequeTypeName(EffectiveTypeName))
		{
			return Runtime::ParseDequeElementType(EffectiveTypeName, OutElementType);
		}
		if (Runtime::IsQueueTypeName(EffectiveTypeName))
		{
			return Runtime::ParseQueueElementType(EffectiveTypeName, OutElementType);
		}
		if (Runtime::IsStackTypeName(EffectiveTypeName))
		{
			return Runtime::ParseStackElementType(EffectiveTypeName, OutElementType);
		}
		return false;
	}

	bool VeroScriptParser::TryGetBraceMapValueType(const std::string& TypeName, std::string& OutValueType) const
	{
		const std::string EffectiveTypeName = ResolveBraceInitializerTypeName(TypeName);
		std::string KeyType;
		if (Runtime::IsMapTypeName(EffectiveTypeName))
		{
			if (!Runtime::SplitMapType(EffectiveTypeName, KeyType, OutValueType))
			{
				return false;
			}
			if (Runtime::IsMultiMapTypeName(EffectiveTypeName))
			{
				OutValueType = "array<" + OutValueType + ">";
			}
			return true;
		}
		if (Runtime::IsOrderedMapTypeName(EffectiveTypeName))
		{
			if (!Runtime::SplitOrderedMapType(EffectiveTypeName, KeyType, OutValueType))
			{
				return false;
			}
			if (Runtime::IsOrderedMultiMapTypeName(EffectiveTypeName))
			{
				OutValueType = "array<" + OutValueType + ">";
			}
			return true;
		}
		return false;
	}
}
