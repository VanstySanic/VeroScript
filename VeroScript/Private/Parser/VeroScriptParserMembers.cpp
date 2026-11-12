// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptNamespaceLookup.h"
#include "VeroScriptStandardLibrary.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptParser::ParseMethodFragment(std::string Namespace, const std::vector<std::string>& TypeParams,
	    const EMemberAccess Access, FMethodDecl& Method, FTemplateParameterList OwnTemplateParameters)
	{
		InitializeFragment(std::move(Namespace), TypeParams);
		FDeclMeta Meta;
		TryParseAnnotations(Meta);
		std::vector<FMethodDecl> Methods;
		std::vector<std::string> OwnTypes, OwnDefaults;
		for (const auto& Parameter : OwnTemplateParameters.GetParameters())
		{
			if (Parameter.Kind == ETemplateParameterKind::Type)
			{
				OwnTypes.push_back(Parameter.Name);
				OwnDefaults.push_back(Parameter.DefaultTypeName);
			}
		}
		if (HasError() || !ParseMethodDecl(Methods, Access, std::move(Meta), std::move(OwnTypes), std::move(OwnDefaults), std::move(OwnTemplateParameters)) || !IsAtEnd())
		{
			if (!HasError())
			{
				ReportErrorAt(Peek(), "expected exactly one complete method");
			}

			return false;
		}
		Method = std::move(Methods.front());
		return true;
	}

	std::unique_ptr<FBlockStmt> VeroScriptParser::ParseBodyFragment(std::string Namespace,
	    const std::vector<std::string>& TypeParams,
	    const std::vector<FFunctionParam>* Parameters)
	{
		InitializeFragment(std::move(Namespace), TypeParams);
		if (Parameters)
		{
			PushRuntimeScope();
			for (const auto& Parameter : *Parameters)
			{
				AddRuntimeBinding(Parameter.Name);
			}
		}
		auto Body = ParseBlock();
		if (Parameters)
		{
			PopRuntimeScope();
		}
		if (!IsAtEnd() && !HasError())
		{
			ReportErrorAt(Peek(), "unexpected tokens after method body");
		}

		return HasError() ? nullptr : std::move(Body);
	}
	bool VeroScriptParser::ParseFunctionFragment(std::string Namespace, std::vector<std::string> TypeParams,
	    FFunctionDecl& Function, FTemplateParameterList Parameters)
	{
		InitializeFragment(std::move(Namespace), TypeParams);
		if (!ParseFunctionDecl(false, {}, std::move(TypeParams), {}, std::move(Parameters)) || HasError() || !IsAtEnd())
		{
			if (!HasError())
			{
				ReportErrorAt(Peek(), "expected exactly one function definition");
			}

			return false;
		}
		Function = std::move(Program->Functions.front());
		return true;
	}
	std::unordered_set<std::string> VeroScriptParser::GetUsedCompilerBuiltinTypes() const
	{
		std::unordered_set<std::string> Result;
		for (const auto& Token : Tokens)
		{
			if (Token.Lexeme == "module_info" || Token.Lexeme == "function_info" ||
			    Token.Lexeme == "global_info")
			{
				Result.emplace(Token.Lexeme);
			}
		}

		return Result;
	}
	void VeroScriptParser::InitializeFragment(std::string Namespace, const std::vector<std::string>& TypeParams)
	{
		Program = std::make_unique<FProgram>();
		ParsedTypeLookup = {};
		bFragmentForwardLookup = true;
		ActiveNamespacePrefix = std::move(Namespace);
		ActiveTypeParams.insert(TypeParams.begin(), TypeParams.end());
		if (!ExternalTypeProgram) return;
		Program->Namespaces = ExternalTypeProgram->Namespaces;
		const auto Anchor = ResolveProgramLookupLocation(ExternalTypeProgram, Tokens.front().Location);
		for (const auto& D : ExternalTypeProgram->UsingNamespaces)
		{
			if (D.ModulePath == FileLabel &&
			    (D.Location.Line < Anchor.Line ||
			        (D.Location.Line == Anchor.Line && D.Location.Column <= Anchor.Column)))
			{
				Program->UsingNamespaces.push_back(D);
			}
		}

		(void)GetExternalForwardTypeLookup();
	}

	void VeroScriptParser::TryParseAnnotations(FDeclMeta& OutMeta)
	{
		while (LooksLikeAnnotationStart())
		{
			FDeclMeta One;
			const auto Checkpoint = Current;
			const auto PendingCheckpoint = PendingGenericCloseCount;
			const bool Explicit = CheckSymbol("@");
			if (!TryParseOneAnnotation(One))
			{
				// Identifier(...) also begins an ordinary expression. Failed
				// speculative metadata parsing must leave that expression intact.
				if (!Explicit)
				{
					Current = Checkpoint;
					PendingGenericCloseCount = PendingCheckpoint;
					bHasError = false;
					Error = {};
				}
				break;
			}
			OutMeta.insert(OutMeta.end(), One.begin(), One.end());
			OutMeta.Annotations.insert(OutMeta.Annotations.end(), One.Annotations.begin(), One.Annotations.end());
		}
	}

	bool VeroScriptParser::TryParseOneAnnotation(FDeclMeta& OutMeta)
	{
		const auto Position = [](const FToken& Token)
		{
			return FAnnotationSourcePosition{std::string(Token.Location.FileLabel), Token.Location.Line, Token.Location.Column};
		};
		FDeclarationAnnotation Annotation;
		Annotation.Ordinal = Current;
		Annotation.Range.Begin = Position(Peek());
		const bool bAt = MatchSymbol("@");
		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected annotation name");
			return false;
		}
		Annotation.NamePosition = Position(Previous());
		Annotation.Name = (bAt ? "@" : "") + std::string(Previous().Lexeme);
		OutMeta.emplace_back("__annot", Annotation.Name);
		if (MatchSymbol("("))
		{
			if (!TryParseAnnotationArgList(OutMeta, {}, Annotation.Arguments) || !MatchSymbol(")"))
			{
				if (!bHasError)
				{
					ReportErrorAt(Peek(), "invalid annotation arguments or missing ')'");
				}

				return false;
			}
		}
		else if (!bAt)
		{
			ReportErrorAt(Peek(), "expected '(' after annotation name");
			return false;
		}
		Annotation.Range.End = Position(Previous());
		Annotation.Range.End.Column += static_cast<std::uint32_t>(Previous().Lexeme.size());
		if (!SourceText.empty())
		{
			Annotation.RawText =
			    CaptureDeferredSyntax(static_cast<std::size_t>(Annotation.Ordinal), Current)->Source;
		}

		else
		{
			Annotation.RawText = Annotation.Name;
		}

		OutMeta.Annotations.push_back(std::move(Annotation));
		return true;
	}

	bool VeroScriptParser::TryParseAnnotationArgList(FDeclMeta& OutMeta, const std::string& KeyPrefix,
	    std::vector<FAnnotationArgument>& Arguments, const std::size_t Depth)
	{
		if (CheckSymbol(")")) return true;
		if (Depth >= 64)
		{
			ReportErrorAt(Peek(), "annotation group nesting limit exceeded");
			return false;
		}
		for (;;)
		{
			if (!Match(ETokenKind::Identifier)) return false;
			if (OutMeta.size() >= 4097)
			{
				ReportErrorAt(Peek(), "annotation argument limit exceeded");
				return false;
			}
			FAnnotationArgument Argument;
			Argument.Name = Previous().Lexeme;
			Argument.Range.Begin = {std::string(Previous().Location.FileLabel), Previous().Location.Line, Previous().Location.Column};
			const std::string Key = KeyPrefix.empty() ? Argument.Name : KeyPrefix + "." + Argument.Name;
			if (MatchSymbol("="))
			{
				if (MatchSymbol("("))
				{
					Argument.Kind = EAnnotationArgumentKind::Group;
					OutMeta.emplace_back(Key, "__annot");
					if (!TryParseAnnotationArgList(OutMeta, Key, Argument.Children, Depth + 1) || !MatchSymbol(")")) return false;
				}
				else
				{
					Argument.Kind = EAnnotationArgumentKind::Scalar;
					const auto First = Current;
					const auto Kind = CheckSymbol("-") && Current + 1 < Tokens.size() ? Tokens[Current + 1].Kind : Peek().Kind;
					switch (Kind)
					{
					case ETokenKind::StringLiteral:
						{
							Argument.ScalarKind = EAnnotationScalarKind::String;
							break;
						}
					case ETokenKind::IntegerLiteral:
						{
							Argument.ScalarKind = EAnnotationScalarKind::Integer;
							break;
						}
					case ETokenKind::FloatLiteral:
						{
							Argument.ScalarKind = EAnnotationScalarKind::Float;
							break;
						}
					case ETokenKind::BooleanLiteral:
						{
							Argument.ScalarKind = EAnnotationScalarKind::Boolean;
							break;
						}
					default:
						{
							break;
						}
					}
					if (!TryParseAnnotationValue(Argument.Value)) return false;
					for (auto Index = First; Index < Current; ++Index)
					{
						Argument.RawText.append(Tokens[Index].Lexeme);
					}

					OutMeta.emplace_back(Key, Argument.Value);
				}
			}
			else
			{
				OutMeta.emplace_back(Key, "");
			}

			Argument.Range.End = {std::string(Previous().Location.FileLabel), Previous().Location.Line,
			    Previous().Location.Column + static_cast<std::uint32_t>(Previous().Lexeme.size())};
			Arguments.push_back(std::move(Argument));
			if (!MatchSymbol(",") || CheckSymbol(")"))
			{
				break;
			}
		}
		return true;
	}

	bool VeroScriptParser::TryParseAnnotationValue(std::string& OutValue)
	{
		OutValue.clear();
		if (Match(ETokenKind::StringLiteral))
		{
			std::string ParseError;
			std::uint32_t ErrorOffset = 0;
			if (!ParserDetail::DecodeStringLiteral(
			        Previous().Lexeme, OutValue, ParseError, &ErrorOffset))
			{
				FToken ErrorToken = Previous();
				ErrorToken.Location.Column += ErrorOffset;
				ReportErrorAt(ErrorToken, ParseError);
				return false;
			}
			return true;
		}
		if (Match(ETokenKind::BooleanLiteral) || Match(ETokenKind::IntegerLiteral) || Match(ETokenKind::FloatLiteral) || Match(ETokenKind::Identifier))
		{
			OutValue.assign(Previous().Lexeme);
			return true;
		}
		if (MatchSymbol("-"))
		{
			if (Match(ETokenKind::IntegerLiteral) || Match(ETokenKind::FloatLiteral))
			{
				OutValue = "-";
				OutValue += Previous().Lexeme;
				return true;
			}
			return false;
		}
		return false;
	}

	bool VeroScriptParser::LooksLikeAnnotationStart() const
	{
		if (IsAtEnd())
		{
			return false;
		}
		if (CheckSymbol("@")) return true;
		if (Check(ETokenKind::Identifier))
		{
			if (Current + 1 >= Tokens.size())
			{
				return false;
			}
			return Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == "(";
		}
		return false;
	}

	std::string VeroScriptParser::QualifyDeclName(const std::string& SimpleName) const
	{
		if (FragmentOwnerScope && SimpleName == FragmentOwnerScope->SimpleName &&
		    TypeFrames.size() == FragmentOwnerDepth)
		{
			return FragmentOwnerScope->QualifiedName;
		}
		if (!LocalDeclarationPrefix.empty())
		{
			return LocalDeclarationPrefix + "." + SimpleName;
		}
		if (ActiveNamespacePrefix.empty())
		{
			return SimpleName;
		}
		return ActiveNamespacePrefix + "." + SimpleName;
	}

	std::string VeroScriptParser::TypeNameLeaf(const std::string_view Name) const
	{
		const std::size_t Dot = Name.rfind('.');
		if (Dot == std::string_view::npos)
		{
			return std::string(Name);
		}
		return std::string(Name.substr(Dot + 1));
	}

	bool VeroScriptParser::TryBuildQualifiedTypeName(
	    std::size_t Index,
	    std::string& OutTypeName,
	    std::size_t& OutEndIndex) const
	{
		const bool bGlobalQualified = Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "::";
		if (bGlobalQualified)
		{
			++Index;
		}
		if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Identifier)
		{
			return false;
		}
		std::string Built(Tokens[Index].Lexeme);
		std::string HostBuilt = Built;
		++Index;
		while (Index + 1 < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && (Tokens[Index].Lexeme == "." || Tokens[Index].Lexeme == "::") && (Tokens[Index + 1].Kind == ETokenKind::Identifier || (Tokens[Index + 1].Kind == ETokenKind::Keyword && (ParserDetail::IsTypeKeyword(Tokens[Index + 1].Lexeme) || ParserDetail::IsContainerTypeKeyword(Tokens[Index + 1].Lexeme)))))
		{
			HostBuilt += Tokens[Index].Lexeme;
			HostBuilt += Tokens[Index + 1].Lexeme;
			Built += '.';
			Built += Tokens[Index + 1].Lexeme;
			Index += 2;
		}
		OutEndIndex = Index;
		std::string Resolved;
		if (const std::string_view StandardType =
		        StandardLibrary::ResolveTypeName(Built);
		    !StandardType.empty())
		{
			Resolved.assign(StandardType);
		}
		else if (HostTypeNames.find(HostBuilt) != HostTypeNames.end())
		{
			Resolved = std::move(HostBuilt);
		}
		else if (bGlobalQualified)
		{
			// Preserve the root qualifier until compiler lookup. A bare `::T` must
			// not fall back to the current namespace, a local alias, or a using
			// directive merely because its normalized spelling is otherwise `T`.
			Resolved = (bDeclarationOutline || IsUserTypeName(Built)) ? "::" + Built : std::string{};
		}
		else
		{
			Resolved = ResolveNamespacedTypeName(std::move(Built));
		}
		const std::string_view LookupName = Resolved.rfind("::", 0) == 0
		    ? std::string_view(Resolved).substr(2)
		    : std::string_view(Resolved);
		if (!bDeclarationOutline && !IsUserTypeName(LookupName) && ActiveTypeParams.find(Resolved) == ActiveTypeParams.end() && !ParserDetail::IsTypeKeyword(Resolved) && !ParserDetail::IsContainerTypeKeyword(Resolved))
		{
			return false;
		}
		OutTypeName = Resolved;
		return true;
	}

	std::string VeroScriptParser::ResolveNamespacedTypeName(std::string TypeName) const
	{
		const auto ResolveKnownType = [this](const std::string& Candidate) -> std::string
		{
			if (const std::string_view StandardType =
			        StandardLibrary::ResolveTypeName(Candidate);
			    !StandardType.empty())
			{
				return std::string(StandardType);
			}
			if (IsUserTypeName(Candidate))
			{
				return Candidate;
			}
			// Script namespaces use '.', while host catalogs commonly preserve C++
			// namespace spelling. A using-directive must make both representations
			// reachable without changing the registered host type identity.
			std::string HostCandidate = Candidate;
			for (std::size_t Dot = 0;
			    (Dot = HostCandidate.find('.', Dot)) != std::string::npos;)
			{
				HostCandidate.replace(Dot, 1, "::");
				Dot += 2;
			}
			return HostTypeNames.find(HostCandidate) != HostTypeNames.end()
			    ? HostCandidate
			    : std::string{};
		};

		if (TypeName.rfind("::", 0) == 0)
		{
			// A leading scope operator is an exact root lookup. Qualified names are
			// already anchored by the parser's dotted internal representation; once
			// a real type is found the marker is no longer needed.
			const std::string RootName = TypeName.substr(2);
			if (const std::string Resolved = ResolveKnownType(RootName); !Resolved.empty())
			{
				return Resolved;
			}
			return TypeName;
		}

		if (ActiveTypeParams.find(TypeName) != ActiveTypeParams.end())
		{
			return TypeName;
		}
		const auto FindAlias = [&](const std::size_t Begin, const std::size_t End) -> std::string
		{
			for (std::size_t Index = End; Index > Begin; --Index)
			{
				const auto& Aliases = LocalTypeAliasScopes[Index - 1];
				if (const auto Found = Aliases.find(TypeName); Found != Aliases.end())
				{
					return Found->second.rfind("$local", 0) == 0 ? Found->second : TypeName;
				}
				for (auto Dot = TypeName.rfind('.'); Dot != std::string::npos;
				    Dot = Dot == 0 ? std::string::npos : TypeName.rfind('.', Dot - 1))
				{
					if (const auto Found = Aliases.find(TypeName.substr(0, Dot)); Found != Aliases.end())
					{
						return Found->second + TypeName.substr(Dot);
					}
				}
			}
			return {};
		};
		std::size_t AliasEnd = LocalTypeAliasScopes.size();
		for (auto Frame = TypeFrames.rbegin(); Frame != TypeFrames.rend(); ++Frame)
		{
			const auto Boundary = std::min(Frame->AliasBoundary, AliasEnd);
			if (auto Alias = FindAlias(Boundary, AliasEnd); !Alias.empty())
			{
				return Alias;
			}
			AliasEnd = Boundary;
			if (TypeName == TypeNameLeaf(Frame->Name))
			{
				return Frame->Name;
			}
			if (auto Child = ResolveKnownType(Frame->Name + "$arity" +
			        std::to_string(OwnTemplateParameterCount(Frame->Scope)) + "." + TypeName);
			    !Child.empty())
			{
				return Child;
			}
			if (auto Child = ResolveKnownType(Frame->Name + "." + TypeName); !Child.empty())
			{
				return Child;
			}
		}
		if (auto Alias = FindAlias(0, AliasEnd); !Alias.empty())
		{
			return Alias;
		}
		if (TypeName.find('.') != std::string::npos)
		{
			const auto Names = BindNamespacePath(TypeName, ActiveNamespacePrefix, [&](const std::string& Prefix)
			{
				return NamespaceExists(Prefix, std::string(FileLabel)) || !ResolveKnownType(Prefix).empty();
			});
			if (!Names.empty())
			{
				if (const auto Resolved = ResolveKnownType(Names.front()); !Resolved.empty()) return Resolved;
				// Keep the original spelling for semantic lookup instead of replacing
				// a missing member of a bound prefix with an outer/using candidate.
				return TypeName;
			}
		}
		if (!ActiveNamespacePrefix.empty())
		{
			// Walk from innermost namespace outward: N.M.T, then N.T.
			std::string Prefix = ActiveNamespacePrefix;
			for (;;)
			{
				const std::string Candidate = Prefix + "." + TypeName;
				if (const std::string Resolved = ResolveKnownType(Candidate);
				    !Resolved.empty())
				{
					return Resolved;
				}
				const std::size_t Dot = Prefix.rfind('.');
				if (Dot == std::string::npos)
				{
					break;
				}
				Prefix = Prefix.substr(0, Dot);
			}
		}
		if (IsUserTypeName(TypeName))
		{
			return TypeName;
		}
		std::vector<std::string> Matches;
		if (Program)
		{
			for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
			{
				if (Directive.ModulePath != FileLabel)
				{
					continue;
				}
				if (!Directive.ScopeNamespace.empty() && ActiveNamespacePrefix != Directive.ScopeNamespace && ActiveNamespacePrefix.rfind(Directive.ScopeNamespace + ".", 0) != 0)
				{
					continue;
				}
				for (const std::string& Target : ResolveUsingNamespaceTargets(Directive))
				{
					const std::string Candidate = Target + "." + TypeName;
					const std::string Resolved = ResolveKnownType(Candidate);
					if (Resolved.empty())
					{
						continue;
					}
					if (std::find(Matches.begin(), Matches.end(), Resolved) == Matches.end())
					{
						Matches.push_back(Resolved);
					}
					// Relative namespace lookup selects the first existing target.
					break;
				}
			}
		}
		if (Matches.size() == 1)
		{
			return Matches.front();
		}
		if (Matches.size() > 1)
		{
			const_cast<VeroScriptParser*>(this)->ReportErrorAt(
			    Peek(), "ambiguous type name from using namespace: " + TypeName);
		}
		return TypeName;
	}

	bool VeroScriptParser::NamespaceExists(
	    const std::string& Name, const std::string& ReferenceModulePath) const
	{
		if (StandardLibrary::IsNamespace(Name)) return true;
		if (!Program) return false;
		const auto Count = Program->Namespaces.size() + (ExternalTypeProgram ? ExternalTypeProgram->Namespaces.size() : 0);
		if (!NamespaceLookup || NamespaceDeclarationCount != Count)
		{
			NamespaceLookup = std::make_shared<FNamespaceLookup>(*Program);
			NamespaceDeclarationCount = Count;
			if (ExternalTypeProgram && ExternalTypeProgram != Program.get())
			{
				NamespaceLookup->AddProgram(*ExternalTypeProgram);
			}
			for (const auto &HostName : HostTypeNames)
			{
				NamespaceLookup->Add(HostName, {}, true, false);
			}
		}
		// Deferred fragments own only their local syntax, not the module import
		// graph. Use the completed program that also supplies their visible type
		// declarations when checking an imported namespace's origins.
		const auto& LookupProgram = bFragmentForwardLookup && ExternalTypeProgram
		    ? *ExternalTypeProgram
		    : *Program;
		return NamespaceLookup->Contains(LookupProgram, Name, ReferenceModulePath, nullptr, &VisibleModulePaths);
	}

	std::vector<std::string> VeroScriptParser::ResolveUsingNamespaceTargets(
	    const FUsingNamespaceDecl& Directive) const
	{
		return BindNamespacePath(Directive.NamespaceName, Directive.ScopeNamespace, [&](const std::string& Name)
		{
			return NamespaceExists(Name, Directive.ModulePath);
		});
	}

	bool VeroScriptParser::ParseFieldDecl(
	    std::vector<FFieldDecl>& OutFields,
	    const EMemberAccess Access,
	    FDeclMeta&& DeclMeta,
	    const bool bAllowMutable)
	{
		const FSourceLocation Location = Peek().Location;
		bool bIsStatic = false;
		bool bIsConst = false;
		bool bConstexpr = false;
		bool bIsMutable = false;
		// Accept field declaration specifiers in any order.
		for (;;)
		{
			if (!bIsStatic && MatchOptionalStatic())
			{
				bIsStatic = true;
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "constexpr")
			{
				if (bConstexpr)
				{
					ReportErrorAt(Peek(), "duplicate constexpr specifier");
					return false;
				}
				bConstexpr = true;
				Advance();
				continue;
			}
			if (!bIsConst && MatchOptionalConst())
			{
				bIsConst = true;
				continue;
			}
			if (!bIsMutable && MatchOptionalMutable())
			{
				bIsMutable = true;
				continue;
			}
			break;
		}
		bIsConst = bIsConst || bConstexpr;
		if (bConstexpr && !bIsStatic)
		{
			ReportErrorAt(Location, "constexpr field requires explicit static");
			return false;
		}
		if (bIsMutable && !bAllowMutable)
		{
			ReportErrorAt(Location, "mutable is only valid on struct or class instance fields");
			return false;
		}
		if (bIsMutable && bIsStatic)
		{
			ReportErrorAt(Location, "static field cannot be mutable");
			return false;
		}
		if (bIsMutable && bIsConst)
		{
			ReportErrorAt(Location, "mutable field cannot be const");
			return false;
		}
		const std::string TypeName = ParseTypeName();
		if (bHasError)
		{
			return false;
		}

		if (!MatchName())
		{
			ReportErrorAt(Peek(), "expected field name");
			return false;
		}

		FFieldDecl Field;
		Field.TypeName = TypeName;
		Field.Name.assign(Previous().Lexeme);
		if (ActiveValueParams.count(Field.Name) != 0)
		{
			ReportErrorAt(Previous(), "field cannot shadow a template value parameter");
			return false;
		}
		Field.bIsConst = bIsConst;
		Field.bConstexpr = bConstexpr;
		Field.bIsMutable = bIsMutable;
		Field.bIsStatic = bIsStatic;
		Field.Access = Access;
		Field.DeclMeta = std::move(DeclMeta);
		Field.Location = Location;
		if (!ParseArraySuffix(Field.TypeName))
		{
			return false;
		}
		if (Field.bIsMutable && Runtime::IsReferenceTypeName(Field.TypeName))
		{
			ReportErrorAt(Field.Location, "mutable field cannot have a reference type");
			return false;
		}

		if (MatchSymbol("="))
		{
			const auto DefaultStart = Current;
			if (CheckSymbol("{") && TypeSupportsBraceInitializer(Field.TypeName))
			{
				Field.DefaultValue = ParseTypedBraceInitializer(Field.TypeName);
			}
			else
			{
				Field.DefaultValue = ParseExpression();
			}
			for (auto I = DefaultStart; I < Current; ++I)
			{
				if (!Field.DefaultValueSource.empty())
				{
					Field.DefaultValueSource += " ";
				}
				Field.DefaultValueSource.append(Tokens[I].Lexeme);
			}
			if (!Field.DefaultValue)
			{
				return false;
			}
			if (bDeclarationOutline && Field.DefaultValue->DeferredSyntax &&
			    !Field.DefaultValue->DeferredSyntax->Source.empty() &&
			    Field.DefaultValue->DeferredSyntax->Source.front() == '{')
			{
				auto D = std::make_shared<FDeferredSyntax>(*Field.DefaultValue->DeferredSyntax);
				D->InitializerType = Field.TypeName;
				Field.DefaultValue->DeferredSyntax = std::move(D);
			}
		}

		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after field declaration");
			return false;
		}

		for (const FFieldDecl& Existing : OutFields)
		{
			if (Existing.Name == Field.Name)
			{
				ReportErrorAt(Previous(), "duplicate field: " + Field.Name);
				return false;
			}
		}

		OutFields.push_back(std::move(Field));
		return true;
	}

	bool VeroScriptParser::ParseMethodDecl(
	    std::vector<FMethodDecl>& OutMethods,
	    const EMemberAccess Access,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const FSourceLocation Location = Peek().Location;
		FMethodDecl Method;
		Method.TypeParams = std::move(PrefixTypeParams);
		Method.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		Method.TemplateParameters = std::move(PrefixTemplateParameters);
		struct FParameterScope
		{
			~FParameterScope()
			{
				for (const auto& Name : Added)
				{
					Active.erase(Name);
					Values.erase(Name);
				}
			}
			std::unordered_set<std::string>& Active;
			std::unordered_map<std::string, FTemplateParameter>& Values;
			std::vector<std::string> Added;
		} ParameterScope{ActiveTypeParams, ActiveValueParams, {}};
		while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "static" || Peek().Lexeme == "constexpr"))
		{
			bool& Flag = Peek().Lexeme == "static" ? Method.bIsStatic : Method.bConstexpr;
			if (Flag)
			{
				ReportErrorAt(Peek(), "duplicate method specifier");
				return false;
			}
			Flag = true;
			Advance();
		}
		FCallableTemplateHeader Header;
		if (!PrepareMethodTemplate(Method, Header, ParameterScope.Added))
		{
			return false;
		}
		Method.DeclMeta = std::move(DeclMeta);
		Method.bIsStatic = MatchOptionalStatic() || Method.bIsStatic;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be virtual");
				return false;
			}
			Method.bVirtual = true;
			Advance();
		}
		Method.ReturnType = ParseReturnTypeName();
		if (bHasError)
		{
			return false;
		}
		if (Method.ReturnType == "auto")
		{
			ReportErrorAt(
			    Previous(),
			    "method return type cannot be 'auto'; use an explicit type");
			return false;
		}

		if (!MatchOperatorMethodName(Method.Name) && !MatchName())
		{
			ReportErrorAt(Peek(), "expected method name");
			return false;
		}
		if (Method.Name.empty())
		{
			Method.Name.assign(Previous().Lexeme);
		}
		if (ActiveValueParams.count(Method.Name) != 0)
		{
			ReportErrorAt(Previous(), "method cannot shadow a template value parameter");
			return false;
		}
		Method.Access = Access;
		Method.Location = Location;

		if (!ConsumeMethodTemplate(Header))
		{
			return false;
		}
		if (!TemplateParameterNames(Method).empty() && Method.Name.rfind("operator", 0) == 0)
		{
			ReportErrorAt(Peek(), "member operators cannot declare their own template parameters");
			return false;
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after method name");
			return false;
		}

		if (!CheckSymbol(")"))
		{
			do
			{
				const auto ParameterBegin = Current;
				FFunctionParam Param;
				Param.bIsConst = MatchOptionalConst();
				Param.bIsRef = MatchOptionalRef();
				Param.TypeName = ParseTypeName();
				if (bHasError)
				{
					return false;
				}
				if (!ParseParamArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (!MatchOptionalTrailingRefAmpersand(Param.bIsRef))
				{
					return false;
				}
				if (!ParseOptionalParamVariadic(Param, true))
				{
					return false;
				}
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected parameter name");
					return false;
				}
				Param.Name.assign(Previous().Lexeme);
				if (!ParseArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (Param.bIsVariadic && CheckSymbol("["))
				{
					ReportErrorAt(Peek(), "variadic parameter cannot have a fixed size suffix");
					return false;
				}
				if (!ParseOptionalParamDefault(Param, true))
				{
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Method.Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after parameters");
			return false;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "const")
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be const");
				return false;
			}
			Method.bIsConst = true;
			Advance();
		}

		if (Method.Name.rfind("operator", 0) == 0)
		{
			std::string OperatorError;
			if (!ParserDetail::ValidateMemberOperatorSignature(
			        Method.Name,
			        Method.ReturnType,
			        Method.bIsStatic,
			        Method.Parameters,
			        ResolveBraceInitializerTypeName(Method.Parameters.empty() ? "" : Method.Parameters.back().TypeName),
			        OperatorError))
			{
				ReportErrorAt(Previous(), OperatorError);
				return false;
			}
		}

		if (!ParserDetail::HasValidVariadicPlacement(Method.Parameters))
		{
			ReportErrorAt(Previous(), "variadic parameter must be the last parameter");
			return false;
		}

		if (!ParserDetail::HasValidTrailingDefaults(Method.Parameters))
		{
			ReportErrorAt(Previous(), "parameters with defaults must be trailing");
			return false;
		}

		// Virt-specifier-seq after `)`: accept `override` and `final` in any order (C++),
		// rejecting a repeated specifier.
		while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "override" || Peek().Lexeme == "final"))
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be override or final");
				return false;
			}
			const bool bIsFinal = Peek().Lexeme == "final";
			if (bIsFinal)
			{
				if (Method.bFinal)
				{
					ReportErrorAt(Peek(), "duplicate 'final' specifier");
					return false;
				}
				Method.bFinal = true;
			}
			else
			{
				if (Method.bOverride)
				{
					ReportErrorAt(Peek(), "duplicate 'override' specifier");
					return false;
				}
				Method.bOverride = true;
			}
			Advance();
		}
		if (Method.bFinal && !Method.bVirtual && !Method.bOverride)
		{
			ReportErrorAt(
			    Previous(),
			    "final method must be marked virtual or override");
			return false;
		}

		if (!ParseOptionalWhereBounds(std::vector<std::string>(ActiveTypeParams.begin(), ActiveTypeParams.end()), Method.Bounds, "method"))
		{
			return false;
		}
		if (!TemplateParameterNames(Method).empty() && (Method.bVirtual || Method.bOverride || Method.bFinal))
		{
			ReportErrorAt(Peek(), "method templates cannot be virtual, override, or final");
			return false;
		}

		if (MatchSymbol("="))
		{
			if (Check(ETokenKind::IntegerLiteral) && Peek().Lexeme == "0")
			{
				if (Method.bIsStatic)
				{
					ReportErrorAt(Peek(), "static method cannot be pure virtual");
					return false;
				}
				if (!Method.bVirtual && !Method.bOverride)
				{
					ReportErrorAt(Peek(), "pure virtual method must be marked virtual or override");
					return false;
				}
				if (Method.bFinal)
				{
					ReportErrorAt(Peek(), "pure virtual method cannot be final");
					return false;
				}
				Advance();
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after pure virtual method declaration");
					return false;
				}
				Method.bVirtual = true;
				Method.bPureVirtual = true;
				Method.Body.reset();
			}
			else if (Check(ETokenKind::Keyword) && Peek().Lexeme == "default")
			{
				if (Method.Name != "operator=" && Method.Name != "operator==" && Method.Name != "operator!=")
				{
					ReportErrorAt(Peek(), "only copy assignment and equality special members may be defaulted");
					return false;
				}
				Advance();
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after defaulted method declaration");
					return false;
				}
				Method.bDefaulted = true;
				Method.Body = std::make_unique<FBlockStmt>();
				Method.Body->Location = Method.Location;
			}
			else if (!Check(ETokenKind::Keyword) || Peek().Lexeme != "delete")
			{
				ReportErrorAt(
				    Peek(),
				    "expected '0', 'delete', or 'default' after '='");
				return false;
			}
			else
			{
				if (Method.bVirtual || Method.bOverride || Method.bFinal)
				{
					ReportErrorAt(Peek(), "virtual, override, or final methods cannot be deleted");
					return false;
				}
				Advance();
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after deleted method declaration");
					return false;
				}
				Method.bDeleted = true;
				Method.Body.reset();
			}
		}
		else
		{
			if (MatchSymbol(";"))
			{
				Method.Body.reset();
			}
			else
			{
				Method.Body = ParseCallableBlock(Method.Name, Method.Parameters, Method.bIsConst, &Method.TemplateParameters);
			}
		}
		if (!Method.bDeleted && !Method.bPureVirtual && !Method.Body && Previous().Lexeme != ";")
		{
			return false;
		}
		ParserDetail::PreserveDeclaredHostSignature(
		    Method.DeclMeta, Method.ReturnType, Method.Parameters, HostTypeNames);

		if (ParserDetail::HasMethodWithSignature(OutMethods, Method))
		{
			ReportErrorAt(
			    Previous(),
			    "duplicate method with same signature: " + Method.Name);
			return false;
		}
		OutMethods.push_back(std::move(Method));
		return true;
	}

	bool VeroScriptParser::ParseInterfaceMethodDecl(
	    std::vector<FMethodDecl>& OutMethods,
	    const EMemberAccess Access,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const FSourceLocation Location = Peek().Location;
		FMethodDecl Method;
		Method.TypeParams = std::move(PrefixTypeParams);
		Method.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		Method.TemplateParameters = std::move(PrefixTemplateParameters);
		struct FParameterScope
		{
			~FParameterScope()
			{
				for (const auto& Name : Added)
				{
					Active.erase(Name);
					Values.erase(Name);
				}
			}
			std::unordered_set<std::string>& Active;
			std::unordered_map<std::string, FTemplateParameter>& Values;
			std::vector<std::string> Added;
		} ParameterScope{ActiveTypeParams, ActiveValueParams, {}};
		while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "static" || Peek().Lexeme == "constexpr"))
		{
			bool& Flag = Peek().Lexeme == "static" ? Method.bIsStatic : Method.bConstexpr;
			if (Flag)
			{
				ReportErrorAt(Peek(), "duplicate method specifier");
				return false;
			}
			Flag = true;
			Advance();
		}
		FCallableTemplateHeader Header;
		if (!PrepareMethodTemplate(Method, Header, ParameterScope.Added))
		{
			return false;
		}
		Method.DeclMeta = std::move(DeclMeta);
		Method.bIsStatic = MatchOptionalStatic() || Method.bIsStatic;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be virtual");
				return false;
			}
			Method.bVirtual = true;
			Advance();
		}
		Method.ReturnType = ParseReturnTypeName();
		if (bHasError)
		{
			return false;
		}
		if (Method.ReturnType == "auto")
		{
			ReportErrorAt(
			    Previous(),
			    "method return type cannot be 'auto'; use an explicit type");
			return false;
		}

		if (!MatchOperatorMethodName(Method.Name) && !MatchName())
		{
			ReportErrorAt(Peek(), "expected method name");
			return false;
		}
		if (Method.Name.empty())
		{
			Method.Name.assign(Previous().Lexeme);
		}
		if (ActiveValueParams.count(Method.Name) != 0)
		{
			ReportErrorAt(Previous(), "method cannot shadow a template value parameter");
			return false;
		}
		Method.Access = Access;
		Method.Location = Location;

		if (!ConsumeMethodTemplate(Header))
		{
			return false;
		}
		if (!TemplateParameterNames(Method).empty() && Method.Name.rfind("operator", 0) == 0)
		{
			ReportErrorAt(Peek(), "member operators cannot declare their own template parameters");
			return false;
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after method name");
			return false;
		}

		if (!CheckSymbol(")"))
		{
			do
			{
				const auto ParameterBegin = Current;
				FFunctionParam Param;
				Param.bIsConst = MatchOptionalConst();
				Param.bIsRef = MatchOptionalRef();
				Param.TypeName = ParseTypeName();
				if (bHasError)
				{
					return false;
				}
				if (!ParseParamArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (!MatchOptionalTrailingRefAmpersand(Param.bIsRef))
				{
					return false;
				}
				if (!ParseOptionalParamVariadic(Param, false))
				{
					return false;
				}
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected parameter name");
					return false;
				}
				Param.Name.assign(Previous().Lexeme);
				if (!ParseArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (!ParseOptionalParamDefault(Param, false))
				{
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Method.Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after parameters");
			return false;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "const")
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be const");
				return false;
			}
			Method.bIsConst = true;
			Advance();
		}

		if (Method.Name.rfind("operator", 0) == 0)
		{
			std::string OperatorError;
			if (!ParserDetail::ValidateMemberOperatorSignature(
			        Method.Name,
			        Method.ReturnType,
			        Method.bIsStatic,
			        Method.Parameters,
			        ResolveBraceInitializerTypeName(Method.Parameters.empty() ? "" : Method.Parameters.back().TypeName),
			        OperatorError))
			{
				ReportErrorAt(Previous(), OperatorError);
				return false;
			}
		}

		while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "override" || Peek().Lexeme == "final"))
		{
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static method cannot be override or final");
				return false;
			}
			const bool bIsFinal = Peek().Lexeme == "final";
			if ((bIsFinal && Method.bFinal) || (!bIsFinal && Method.bOverride))
			{
				ReportErrorAt(Peek(), bIsFinal ? "duplicate 'final' specifier" : "duplicate 'override' specifier");
				return false;
			}
			Method.bFinal = Method.bFinal || bIsFinal;
			Method.bOverride = Method.bOverride || !bIsFinal;
			Advance();
		}
		if (Method.bFinal && !Method.bVirtual && !Method.bOverride)
		{
			ReportErrorAt(Previous(), "final interface method must be marked virtual or override");
			return false;
		}

		if (!ParseOptionalWhereBounds(std::vector<std::string>(ActiveTypeParams.begin(), ActiveTypeParams.end()), Method.Bounds, "method"))
		{
			return false;
		}
		if (!TemplateParameterNames(Method).empty() && (Method.bVirtual || Method.bOverride || Method.bFinal))
		{
			ReportErrorAt(Peek(), "method templates cannot be virtual, override, or final");
			return false;
		}

		if (MatchSymbol(";"))
		{
			if (ParserDetail::HasMethodWithSignature(OutMethods, Method))
			{
				ReportErrorAt(Previous(), "duplicate method with same signature: " + Method.Name);
				return false;
			}
			OutMethods.push_back(std::move(Method));
			return true;
		}

		if (MatchSymbol("="))
		{
			if (!Check(ETokenKind::IntegerLiteral) || Peek().Lexeme != "0")
			{
				ReportErrorAt(Peek(), "interface methods support only explicit '= 0' pure virtual declarations");
				return false;
			}
			if (Method.bIsStatic)
			{
				ReportErrorAt(Peek(), "static interface method cannot be pure virtual");
				return false;
			}
			if (!Method.bVirtual && !Method.bOverride)
			{
				ReportErrorAt(Peek(), "pure virtual interface method must be marked virtual or override");
				return false;
			}
			if (Method.bFinal)
			{
				ReportErrorAt(Peek(), "pure virtual interface method cannot be final");
				return false;
			}
			if (Access != EMemberAccess::Public)
			{
				ReportErrorAt(Peek(), "pure virtual interface method must be public");
				return false;
			}
			Advance();
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after pure virtual interface method declaration");
				return false;
			}
			Method.bVirtual = true;
			Method.bPureVirtual = true;
			Method.Body.reset();
			ParserDetail::PreserveDeclaredHostSignature(
			    Method.DeclMeta, Method.ReturnType, Method.Parameters, HostTypeNames);
			if (ParserDetail::HasMethodWithSignature(OutMethods, Method))
			{
				ReportErrorAt(Previous(), "duplicate method with same signature: " + Method.Name);
				return false;
			}
			OutMethods.push_back(std::move(Method));
			return true;
		}

		if (!CheckSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' or '= 0;' after interface method signature");
			return false;
		}
		Method.Body = ParseCallableBlock(Method.Name, Method.Parameters, Method.bIsConst, &Method.TemplateParameters);
		if (!Method.Body)
		{
			return false;
		}
		ParserDetail::PreserveDeclaredHostSignature(
		    Method.DeclMeta, Method.ReturnType, Method.Parameters, HostTypeNames);
		if (ParserDetail::HasMethodWithSignature(OutMethods, Method))
		{
			ReportErrorAt(
			    Previous(),
			    "duplicate method with same signature: " + Method.Name);
			return false;
		}
		OutMethods.push_back(std::move(Method));
		return true;
	}

	bool VeroScriptParser::ParseConstructorDecl(
	    const std::string_view TypeName,
	    std::vector<FConstructorDecl>& OutConstructors,
	    const EMemberAccess Access,
	    FDeclMeta&& DeclMeta)
	{
		const FSourceLocation Location = Peek().Location;
		const bool bExplicit = Check(ETokenKind::Keyword) && Peek().Lexeme == "explicit";
		if (bExplicit)
		{
			Advance();
		}
		if (!Match(ETokenKind::Identifier) || Previous().Lexeme != TypeName)
		{
			ReportErrorAt(Peek(), "expected constructor name");
			return false;
		}

		FConstructorDecl Constructor;
		Constructor.Access = Access;
		Constructor.bExplicit = bExplicit;
		Constructor.DeclMeta = std::move(DeclMeta);
		Constructor.Location = Location;

		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after constructor name");
			return false;
		}

		if (!CheckSymbol(")"))
		{
			do
			{
				const auto ParameterBegin = Current;
				FFunctionParam Param;
				Param.bIsConst = MatchOptionalConst();
				Param.bIsRef = MatchOptionalRef();
				Param.TypeName = ParseTypeName();
				if (bHasError)
				{
					return false;
				}
				if (!ParseParamArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (!MatchOptionalTrailingRefAmpersand(Param.bIsRef))
				{
					return false;
				}
				if (!ParseOptionalParamVariadic(Param, false))
				{
					return false;
				}
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected parameter name");
					return false;
				}
				Param.Name.assign(Previous().Lexeme);
				if (!ParseArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (!ParseOptionalParamDefault(Param, false))
				{
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Constructor.Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after constructor parameters");
			return false;
		}

		const bool bHasInitializerList = MatchSymbol(":");
		if (bHasInitializerList)
		{
			do
			{
				if (!Match(ETokenKind::Identifier))
				{
					ReportErrorAt(Peek(), "expected base constructor name");
					return false;
				}
				FBaseCtorInit BaseInit;
				BaseInit.BaseName.assign(Previous().Lexeme);
				BaseInit.Location = Previous().Location;
				if (!MatchSymbol("("))
				{
					ReportErrorAt(Peek(), "expected '(' after base constructor name");
					return false;
				}
				if (!CheckSymbol(")"))
				{
					do
					{
						std::unique_ptr<FExpr> Arg = ParseCallArgument();
						if (!Arg)
						{
							return false;
						}
						BaseInit.Args.push_back(std::move(Arg));
					}
					while (MatchSymbol(","));
				}
				if (!MatchSymbol(")"))
				{
					ReportErrorAt(Peek(), "expected ')' after base constructor arguments");
					return false;
				}
				Constructor.BaseInits.push_back(std::move(BaseInit));
			}
			while (MatchSymbol(","));
		}

		if (MatchSymbol("="))
		{
			if (bHasInitializerList)
			{
				ReportErrorAt(
				    Previous(),
				    "constructor initializer list requires a constructor body");
				return false;
			}
			if (!Check(ETokenKind::Keyword) || (Peek().Lexeme != "default" && Peek().Lexeme != "delete"))
			{
				ReportErrorAt(Peek(), "expected 'default' or 'delete' after '='");
				return false;
			}
			const bool bDefaulted = Peek().Lexeme == "default";
			Advance();
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after constructor declaration");
				return false;
			}
			if (bDefaulted && (Constructor.Parameters.size() > 1 || !Constructor.BaseInits.empty() || !Constructor.FieldInits.empty() || (!Constructor.Parameters.empty() && (!Constructor.Parameters[0].bIsRef || !Constructor.Parameters[0].bIsConst || Constructor.Parameters[0].DefaultValue))))
			{
				ReportErrorAt(
				    Previous(),
				    "a defaulted constructor must be a default or const-reference copy constructor without initializers");
				return false;
			}
			Constructor.bDefaulted = bDefaulted;
			Constructor.bDeleted = !bDefaulted;
			if (bDefaulted)
			{
				Constructor.Body = std::make_unique<FBlockStmt>();
				Constructor.Body->Location = Location;
				Constructor.Body->ModulePath.assign(FileLabel);
			}
		}
		else
		{
			if (MatchSymbol(";"))
			{
				if (bHasInitializerList)
				{
					ReportErrorAt(
					    Previous(),
					    "constructor initializer list requires a constructor body");
					return false;
				}
				Constructor.Body.reset();
			}
			else
			{
				Constructor.Body = ParseCallableBlock("$constructor", Constructor.Parameters);
			}
		}
		if (!Constructor.bDeleted && !Constructor.Body && Previous().Lexeme != ";")
		{
			return false;
		}
		// Reject exact duplicates here so every frontend (including the module
		// loader's uninstantiated-generic path) observes the same declaration
		// semantics. The compiler still performs canonical alias-aware validation.
		if (ParserDetail::HasConstructorWithSignature(OutConstructors, Constructor))
		{
			ReportErrorAt(Previous(), "duplicate constructor: " + std::string(TypeName));
			return false;
		}

		OutConstructors.push_back(std::move(Constructor));
		return true;
	}

	bool VeroScriptParser::ClassifyConstructorFieldInitializers(
	    std::vector<FConstructorDecl>& Constructors,
	    const std::vector<FFieldDecl>& Fields)
	{
		for (FConstructorDecl& Constructor : Constructors)
		{
			std::unordered_set<std::string> UsedFieldNames;
			for (const FFieldCtorInit& Existing : Constructor.FieldInits)
			{
				UsedFieldNames.insert(Existing.FieldName);
			}

			std::vector<FBaseCtorInit> RemainingBaseInits;
			RemainingBaseInits.reserve(Constructor.BaseInits.size());
			for (FBaseCtorInit& Init : Constructor.BaseInits)
			{
				const auto FieldIt = std::find_if(
				    Fields.begin(),
				    Fields.end(),
				    [&](const FFieldDecl& Field)
				{
					return Field.Name == Init.BaseName;
				});
				if (FieldIt == Fields.end())
				{
					RemainingBaseInits.push_back(std::move(Init));
					continue;
				}

				if (FieldIt->bIsStatic)
				{
					ReportErrorId(
					    Init.Location,
					    "diag.constructor_initializer_cannot_target_static_field",
					    {Init.BaseName});
					return false;
				}
				if (FieldIt->DefaultValue)
				{
					ReportErrorId(
					    Init.Location,
					    "diag.field_already_has_declaration_initializer",
					    {Init.BaseName});
					return false;
				}
				const std::string CanonicalFieldType(
				    CanonicalizePrimitiveTypeKeyword(FieldIt->TypeName));
				const bool bScalarField = CanonicalFieldType == "bool" || CanonicalFieldType == "char" || CanonicalFieldType == "short" || CanonicalFieldType == "int" || CanonicalFieldType == "long" || CanonicalFieldType == "float" || CanonicalFieldType == "double" || CanonicalFieldType == "int64" || CanonicalFieldType == "uint64" || CanonicalFieldType == "string" || CanonicalFieldType == "wstring" || CanonicalFieldType == "unsigned char" || CanonicalFieldType == "unsigned short" || CanonicalFieldType == "unsigned int" || CanonicalFieldType == "unsigned long";
				if (bScalarField && Init.Args.size() > 1)
				{
					ReportErrorId(
					    Init.Location,
					    "diag.scalar_field_initializer_expects_at_most_one_argument",
					    {Init.BaseName});
					return false;
				}
				if (!UsedFieldNames.insert(Init.BaseName).second)
				{
					ReportErrorId(
					    Init.Location,
					    "diag.duplicate_field_constructor_initializer",
					    {Init.BaseName});
					return false;
				}

				FFieldCtorInit FieldInit;
				FieldInit.FieldName = std::move(Init.BaseName);
				FieldInit.Args = std::move(Init.Args);
				FieldInit.Location = Init.Location;
				Constructor.FieldInits.push_back(std::move(FieldInit));
			}
			Constructor.BaseInits = std::move(RemainingBaseInits);
		}
		return true;
	}

	bool VeroScriptParser::ParseDestructorDecl(
	    const std::string_view TypeName,
	    std::unique_ptr<FDestructorDecl>& OutDestructor,
	    const EMemberAccess Access,
	    FDeclMeta&& DeclMeta)
	{
		const FSourceLocation Location = Peek().Location;
		bool bVirtual = false;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
		{
			bVirtual = true;
			Advance();
		}
		if (!MatchSymbol("~"))
		{
			ReportErrorAt(Peek(), "expected '~' in destructor");
			return false;
		}
		if (!Match(ETokenKind::Identifier) || Previous().Lexeme != TypeName)
		{
			ReportErrorAt(Peek(), "expected destructor name");
			return false;
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after destructor name");
			return false;
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "destructor cannot have parameters");
			return false;
		}
		if (OutDestructor)
		{
			ReportErrorAt(Previous(), "duplicate destructor");
			return false;
		}
		// Destructors take no virt-specifiers or cv-qualifiers; give a targeted
		// message instead of the generic "expected '{'" from ParseBlock.
		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "const" || Peek().Lexeme == "final" || Peek().Lexeme == "override"))
		{
			ReportErrorAt(
			    Peek(),
			    "destructor cannot be '" + std::string(Peek().Lexeme) + "'");
			return false;
		}
		auto Destructor = std::make_unique<FDestructorDecl>();
		Destructor->bVirtual = bVirtual;
		Destructor->Access = Access;
		Destructor->DeclMeta = std::move(DeclMeta);
		Destructor->Location = Location;
		if (MatchSymbol("="))
		{
			if (Check(ETokenKind::IntegerLiteral) && Peek().Lexeme == "0")
			{
				if (!bVirtual)
				{
					ReportErrorAt(Peek(), "pure virtual destructor must be marked virtual");
					return false;
				}
				Advance();
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after pure virtual destructor declaration");
					return false;
				}
				Destructor->bPureVirtual = true;
				Destructor->Body.reset();
			}
			else if (!Check(ETokenKind::Keyword) || Peek().Lexeme != "default")
			{
				ReportErrorAt(
				    Peek(),
				    "destructors support only '= 0' or '= default'; managed values cannot have a deleted destructor");
				return false;
			}
			else
			{
				Advance();
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after defaulted destructor declaration");
					return false;
				}
				Destructor->bDefaulted = true;
				Destructor->Body = std::make_unique<FBlockStmt>();
				Destructor->Body->Location = Location;
				Destructor->Body->ModulePath.assign(FileLabel);
			}
		}
		else
		{
			if (MatchSymbol(";"))
			{
				Destructor->Body.reset();
			}
			else
			{
				Destructor->Body = ParseCallableBlock("$destructor", {});
			}
		}
		if (!Destructor->Body && !Destructor->bPureVirtual && Previous().Lexeme != ";")
		{
			return false;
		}
		OutDestructor = std::move(Destructor);
		return true;
	}

	bool VeroScriptParser::ParseInheritanceClause(
	    std::vector<std::string>& OutBaseTypeNames,
	    std::vector<EMemberAccess>& OutBaseInheritanceAccess,
	    std::vector<std::string>& OutInterfaceTypeNames,
	    const bool bAllowMultipleClassBases,
	    const EMemberAccess DefaultBaseAccess,
	    const FSourceLocation& Location)
	{
		(void)Location;
		OutBaseTypeNames.clear();
		OutBaseInheritanceAccess.clear();
		OutInterfaceTypeNames.clear();
		do
		{
			EMemberAccess BaseAccess = DefaultBaseAccess;
			TryParseInheritanceAccess(BaseAccess);

			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected base or interface name");
				return false;
			}
			std::string Name(Previous().Lexeme);
			while (MatchMemberAccessOp())
			{
				if (!Match(ETokenKind::Identifier))
				{
					ReportErrorAt(Peek(), "expected identifier after '.' or '::' in type name");
					return false;
				}
				Name += '.';
				Name += Previous().Lexeme;
			}
			Name = ResolveNamespacedTypeName(std::move(Name));
			if (IsInterfaceName(Name))
			{
				std::string InterfaceTypeName = Name;
				if (MatchSymbol("<"))
				{
					if (!MatchCloseGeneric())
					{
						InterfaceTypeName += "<";
						bool bFirstArg = true;
						do
						{
							++TypeNameParseDepth;
							const std::string ArgType = ParseTemplateArgumentName();
							--TypeNameParseDepth;
							if (bHasError || ArgType.empty())
							{
								return false;
							}
							if (!bFirstArg)
							{
								InterfaceTypeName += ",";
							}
							InterfaceTypeName += ArgType;
							bFirstArg = false;
						}
						while (MatchSymbol(","));
						if (!MatchCloseGeneric())
						{
							ReportErrorAt(Peek(), "expected '>' after interface type argument");
							return false;
						}
						InterfaceTypeName += ">";
					}
					else
					{
						InterfaceTypeName += "<>";
					}
					if (PendingGenericCloseCount > 0)
					{
						ReportErrorAt(Previous(), "unexpected '>' after type name");
						return false;
					}
				}
				OutInterfaceTypeNames.push_back(std::move(InterfaceTypeName));
				continue;
			}
			if (IsStructOrClassName(Name) || HostTypeNames.find(Name) != HostTypeNames.end())
			{
				if (!OutBaseTypeNames.empty() && !bAllowMultipleClassBases)
				{
					ReportErrorAt(
					    Previous(),
					    "struct supports only single inheritance");
					return false;
				}
				std::string BaseTypeName = Name;
				if (MatchSymbol("<"))
				{
					if (HostTypeNames.find(Name) != HostTypeNames.end() && !IsStructOrClassName(Name))
					{
						ReportErrorAt(
						    Previous(),
						    "host base type cannot be generic: " + Name);
						return false;
					}
					if (!MatchCloseGeneric())
					{
						BaseTypeName += "<";
						bool bFirstArg = true;
						do
						{
							++TypeNameParseDepth;
							const std::string ArgType = ParseTemplateArgumentName();
							--TypeNameParseDepth;
							if (bHasError || ArgType.empty())
							{
								return false;
							}
							if (!bFirstArg)
							{
								BaseTypeName += ",";
							}
							BaseTypeName += ArgType;
							bFirstArg = false;
						}
						while (MatchSymbol(","));
						if (!MatchCloseGeneric())
						{
							ReportErrorAt(Peek(), "expected '>' after base type argument");
							return false;
						}
						BaseTypeName += ">";
					}
					else
					{
						BaseTypeName += "<>";
					}
					if (PendingGenericCloseCount > 0)
					{
						ReportErrorAt(Previous(), "unexpected '>' after type name");
						return false;
					}
				}
				OutBaseTypeNames.push_back(std::move(BaseTypeName));
				OutBaseInheritanceAccess.push_back(BaseAccess);
				continue;
			}
			ReportErrorAt(Previous(), "unknown base or interface type: " + Name);
			return false;
		}
		while (MatchSymbol(","));
		return true;
	}

}
