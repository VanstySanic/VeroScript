// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "TypeSupport/VeroScriptConstantRepresentation.h"
#include "VeroScriptParser.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptAnalysisControl.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace VeroScript
{
	struct VeroScriptParser::FTypeDeclarationGuard
	{
		explicit FTypeDeclarationGuard(VeroScriptParser& Parser)
		    : Parser(Parser), Depth(Parser.TypeFrames.size() - 1)
		{
		}
		~FTypeDeclarationGuard()
		{
			// Unwind borrowed declarations before their local AST owners expire,
			// including parse errors, cancellation, and allocation failures.
			while (Parser.TypeFrames.size() > Depth)
			{
				const auto AliasBoundary = Parser.TypeFrames.back().AliasBoundary;
				while (Parser.LocalTypeAliasScopes.size() > AliasBoundary + 1)
				{
					Parser.PopAliasScope();
				}
				Parser.LeaveTypeDeclaration();
			}
		}
		VeroScriptParser& Parser;
		std::size_t Depth;
	};

	bool VeroScriptParser::TrySkipTypeNameTokens(std::size_t& Index, int& PendingCloseCount) const
	{
		const auto SkipDynamicArraySuffix = [this, &Index]()
		{
			while (Index + 1 < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "[" && Tokens[Index + 1].Kind == ETokenKind::Symbol && Tokens[Index + 1].Lexeme == "]")
			{
				Index += 2;
			}
		};
		if (Index >= Tokens.size())
		{
			return false;
		}
		while (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "const" || Tokens[Index].Lexeme == "constexpr"))
		{
			++Index;
		}
		if (Index < Tokens.size() && ((Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "ref") || (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "&")))
		{
			++Index;
		}
		if (Index >= Tokens.size())
		{
			return false;
		}
		if (Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "fn" || Tokens[Index].Lexeme == "function"))
		{
			++Index;
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "(")
			{
				return false;
			}
			++Index;
			if (Index < Tokens.size() && !(Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == ")"))
			{
				for (;;)
				{
					if (!TrySkipTypeNameTokens(Index, PendingCloseCount))
					{
						return false;
					}
					// Function type parameters accept the recommended C++-style
					// trailing reference marker: function(T&, const U&) -> R.
					if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "&")
					{
						++Index;
					}
					if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != ",")
					{
						break;
					}
					++Index;
				}
			}
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != ")")
			{
				return false;
			}
			++Index;
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "->")
			{
				return false;
			}
			++Index;
			return TrySkipTypeNameTokens(Index, PendingCloseCount);
		}
		if (Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "array" || Tokens[Index].Lexeme == "set" || Tokens[Index].Lexeme == "multiset" || Tokens[Index].Lexeme == "ordered_multiset" || Tokens[Index].Lexeme == "queue" || Tokens[Index].Lexeme == "priority_queue" || Tokens[Index].Lexeme == "stack" || Tokens[Index].Lexeme == "deque" || Tokens[Index].Lexeme == "channel"))
		{
			++Index;
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "<")
			{
				return false;
			}
			++Index;
			if (!TrySkipTypeNameTokens(Index, PendingCloseCount))
			{
				return false;
			}
			if (!TrySkipCloseGeneric(Index, PendingCloseCount))
			{
				return false;
			}
			SkipDynamicArraySuffix();
			return true;
		}
		if (Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "map" || Tokens[Index].Lexeme == "multimap" || Tokens[Index].Lexeme == "ordered_map" || Tokens[Index].Lexeme == "ordered_multimap"))
		{
			++Index;
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "<")
			{
				return false;
			}
			++Index;
			if (!TrySkipTypeNameTokens(Index, PendingCloseCount))
			{
				return false;
			}
			if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != ",")
			{
				return false;
			}
			++Index;
			if (!TrySkipTypeNameTokens(Index, PendingCloseCount))
			{
				return false;
			}
			if (!TrySkipCloseGeneric(Index, PendingCloseCount))
			{
				return false;
			}
			SkipDynamicArraySuffix();
			return true;
		}
		if (Tokens[Index].Kind == ETokenKind::Keyword && ParserDetail::IsTypeKeyword(Tokens[Index].Lexeme))
		{
			if (Tokens[Index].Lexeme == "unsigned")
			{
				++Index;
				if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && ParserDetail::IsUnsignedBaseTypeKeyword(Tokens[Index].Lexeme))
				{
					++Index;
				}
				SkipDynamicArraySuffix();
				return true;
			}
			++Index;
			SkipDynamicArraySuffix();
			return true;
		}
		if (Tokens[Index].Kind == ETokenKind::Identifier || Tokens[Index].Lexeme == "::")
		{
			std::string QualifiedTypeName;
			std::size_t EndIndex = Index;
			if (!TryBuildQualifiedTypeName(Index, QualifiedTypeName, EndIndex))
			{
				return false;
			}
			Index = EndIndex;
			for (;;)
			{
				if (Index < Tokens.size() && Tokens[Index].Lexeme == "<")
				{
					std::size_t Angles = 1, Parentheses = 0, Brackets = 0;
					++Index;
					while (Index < Tokens.size() && Angles)
					{
						if ((Index & 255u) == 0)
						{
							CheckAnalysisCancellation();
						}
						const auto Part = Tokens[Index].Lexeme;
						if (Part == "(")
						{
							++Parentheses;
						}
						else if (Part == ")")
						{
							if (!Parentheses) return false;
							--Parentheses;
						}
						else if (Part == "[")
						{
							++Brackets;
						}
						else if (Part == "]")
						{
							if (!Brackets) return false;
							--Brackets;
						}
						else if (!Parentheses && !Brackets)
						{
							if (Part == "<" && Index > 0)
							{
								auto Head = Index - 1;
								while (Head >= 2 && (Tokens[Head - 1].Lexeme == "::" || Tokens[Head - 1].Lexeme == "."))
								{
									Head -= 2;
								}
								if (Tokens[Index - 1].Lexeme == "cast" || CheckTypeNameAt(Head))
								{
									++Angles;
								}
							}
							else if (Part == ">" || Part == ">>")
							{
								const auto Count = Part.size();
								if (Count >= Angles)
								{
									PendingCloseCount += static_cast<int>(Count - Angles);
									Angles = 0;
								}
								else
								{
									Angles -= Count;
								}
							}
							else if (Part == ";" || Part == "{" || Part == "}" || Tokens[Index].Kind == ETokenKind::EndOfFile)
							{
								return false;
							}
						}
						++Index;
					}
					if (Angles || Parentheses || Brackets) return false;
				}
				if (Index + 1 >= Tokens.size() || (Tokens[Index].Lexeme != "." && Tokens[Index].Lexeme != "::") ||
				    Tokens[Index + 1].Kind != ETokenKind::Identifier)
				{
					break;
				}
				Index += 2;
			}
			SkipDynamicArraySuffix();
			return true;
		}
		return false;
	}

	bool VeroScriptParser::ParseFunctionDecl(
	    const bool bExported,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const auto PreviousValues = ActiveValueParams;
		struct FValueScope
		{
			~FValueScope()
			{
				Values = std::move(Previous);
			}
			std::unordered_map<std::string, FTemplateParameter>& Values;
			std::unordered_map<std::string, FTemplateParameter> Previous;
		} ValueScope{ActiveValueParams, PreviousValues};
		const FSourceLocation Location = Peek().Location;
		FFunctionDecl Function;
		bool HasConstexpr = false;
		for (auto Index = Current; Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword &&
		    (Tokens[Index].Lexeme == "const" || Tokens[Index].Lexeme == "constexpr");
		    ++Index)
		{
			HasConstexpr = HasConstexpr || Tokens[Index].Lexeme == "constexpr";
		}
		// A plain leading const still belongs to an existing reference return type.
		if (HasConstexpr)
		{
			MatchConstexprSpecifiers(Function.bConstexpr);
		}
		Function.Location = Location;
		Function.ModulePath.assign(FileLabel);
		Function.bExported = bExported;
		Function.DeclMeta = std::move(DeclMeta);

		Function.TypeParams = std::move(PrefixTypeParams);
		Function.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		Function.TemplateParameters = std::move(PrefixTemplateParameters);
		const bool bHasTemplatePrefix = !TemplateParameterNames(Function).empty();
		const bool bLegacyGenericIntro = !bHasTemplatePrefix &&
		    Check(ETokenKind::Identifier) && !IsUserTypeName(ResolveNamespacedTypeName(std::string(Peek().Lexeme))) && !ParserDetail::IsTypeKeyword(Peek().Lexeme) && Current + 2 < Tokens.size() && ParserDetail::IsNameToken(Tokens[Current + 1]) && Tokens[Current + 2].Kind == ETokenKind::Symbol && Tokens[Current + 2].Lexeme == "(";
		std::size_t HeaderOpening = 0, HeaderEnd = 0;
		int HeaderPending = 0;
		const bool bAngledGenericIntro = !bHasTemplatePrefix && FindFunctionTemplateHeader(HeaderOpening);
		std::vector<std::string> LookaheadTypeParams, LookaheadDefaults;
		if (bAngledGenericIntro)
		{
			const auto SavedCurrent = Current;
			const auto SavedPending = PendingGenericCloseCount;
			Current = HeaderOpening + 1;
			const bool Parsed = ParseTypeParameterList(LookaheadTypeParams, LookaheadDefaults, &Function.TemplateParameters);
			HeaderEnd = Current;
			HeaderPending = PendingGenericCloseCount;
			Current = SavedCurrent;
			PendingGenericCloseCount = SavedPending;
			if (!Parsed)
			{
				return false;
			}
			Function.TypeParamDefaults = std::move(LookaheadDefaults);
		}
		for (const auto& Parameter : Function.TemplateParameters.GetParameters())
		{
			if (Parameter.Kind == ETemplateParameterKind::Value)
			{
				ActiveValueParams.emplace(Parameter.Name, Parameter);
			}
		}
		if (bHasTemplatePrefix)
		{
			for (const std::string& TypeParam : Function.TypeParams)
			{
				ActiveTypeParams.insert(TypeParam);
			}
		}
		else if (bLegacyGenericIntro)
		{
			Function.TypeParams.emplace_back(Peek().Lexeme);
			ActiveTypeParams.insert(Function.TypeParams[0]);
		}
		else if (bAngledGenericIntro)
		{
			for (const std::string& TypeParam : LookaheadTypeParams)
			{
				if (ParserDetail::IsTypeKeyword(TypeParam) || ActiveTypeParams.find(TypeParam) != ActiveTypeParams.end() || std::find(Function.TypeParams.begin(), Function.TypeParams.end(), TypeParam) != Function.TypeParams.end())
				{
					ReportErrorAt(Peek(), "invalid type parameter name");
					return false;
				}
				Function.TypeParams.push_back(TypeParam);
				ActiveTypeParams.insert(TypeParam);
			}
		}
		const auto ClearTypeParams = [&Function, this]()
		{
			for (const std::string& TypeParam : Function.TypeParams)
			{
				ActiveTypeParams.erase(TypeParam);
			}
		};

		Function.ReturnType = ParseReturnTypeName();
		if (bHasError)
		{
			ClearTypeParams();
			return false;
		}
		if (Function.ReturnType == "auto")
		{
			ClearTypeParams();
			ReportErrorAt(
			    Previous(),
			    "function return type cannot be 'auto'; use an explicit type");
			return false;
		}

		std::string OperatorName;
		if (MatchOperatorMethodName(OperatorName))
		{
			Function.Name = QualifyDeclName(OperatorName);
		}
		else if (!MatchName())
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected function name");
			return false;
		}
		else
		{
			Function.Name = QualifyDeclName(std::string(Previous().Lexeme));
		}
		if (bHasTemplatePrefix && CheckSymbol("<"))
		{
			ClearTypeParams();
			ReportErrorAt(
			    Peek(),
			    "cannot combine a template<...> prefix with function-name type parameters");
			return false;
		}

		if (bAngledGenericIntro)
		{
			if (Current != HeaderOpening)
			{
				ClearTypeParams();
				ReportErrorAt(Peek(), "invalid function template parameter position");
				return false;
			}
			Current = HeaderEnd;
			PendingGenericCloseCount = HeaderPending;
		}

		if (!CheckSymbol("("))
		{
			ClearTypeParams();
			if (bHasTemplatePrefix)
			{
				ReportErrorAt(Peek(), "template prefix must precede a function or generic type declaration");
			}
			return false;
		}

		if (!MatchSymbol("("))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '(' after function name");
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
					ClearTypeParams();
					return false;
				}
				if (!ParseParamArraySuffix(Param.TypeName))
				{
					ClearTypeParams();
					return false;
				}
				if (!MatchOptionalTrailingRefAmpersand(Param.bIsRef))
				{
					ClearTypeParams();
					return false;
				}
				if (!ParseOptionalParamVariadic(Param, true))
				{
					ClearTypeParams();
					return false;
				}
				if (!MatchName())
				{
					ClearTypeParams();
					ReportErrorAt(Peek(), "expected parameter name");
					return false;
				}
				Param.Name.assign(Previous().Lexeme);
				if (!ParseArraySuffix(Param.TypeName))
				{
					ClearTypeParams();
					return false;
				}
				if (Param.bIsVariadic && CheckSymbol("["))
				{
					ClearTypeParams();
					ReportErrorAt(Peek(), "variadic parameter cannot have a fixed size suffix");
					return false;
				}
				if (!ParseOptionalParamDefault(Param, true))
				{
					ClearTypeParams();
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Function.Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected ')' after parameters");
			return false;
		}

		if (Function.Name.rfind("operator", 0) == 0 || Function.Name.find(".operator") != std::string::npos)
		{
			const std::string Leaf = ParserDetail::OperatorNameForValidation(Function.Name);
			std::string OperatorError;
			if (!ParserDetail::ValidateFreeOperatorSignature(
			        Leaf,
			        Function.ReturnType,
			        Function.Parameters,
			        ResolveBraceInitializerTypeName(Function.Parameters.empty() ? "" : Function.Parameters.back().TypeName),
			        OperatorError))
			{
				ClearTypeParams();
				ReportErrorAt(Previous(), OperatorError);
				return false;
			}
		}

		if (!ParserDetail::HasValidVariadicPlacement(Function.Parameters))
		{
			ClearTypeParams();
			ReportErrorAt(Previous(), "variadic parameter must be the last parameter");
			return false;
		}

		if (!ParserDetail::HasValidTrailingDefaults(Function.Parameters))
		{
			ClearTypeParams();
			ReportErrorAt(Previous(), "parameters with defaults must be trailing");
			return false;
		}

		if (!ParseOptionalFunctionBounds(Function))
		{
			ClearTypeParams();
			return false;
		}

		if (MatchSymbol("="))
		{
			if (!Check(ETokenKind::Keyword) || Peek().Lexeme != "delete")
			{
				ClearTypeParams();
				ReportErrorAt(
				    Peek(),
				    "free functions support only '= delete'; '= default' is reserved for special members");
				return false;
			}
			Advance();
			if (!MatchSymbol(";"))
			{
				ClearTypeParams();
				ReportErrorAt(Peek(), "expected ';' after deleted function declaration");
				return false;
			}
			if (Function.Name == "main")
			{
				ClearTypeParams();
				ReportErrorAt(Previous(), "main cannot be deleted");
				return false;
			}
			Function.bDeleted = true;
			Function.Body.reset();
		}
		else if (!MatchSymbol(";"))
		{
			Function.Body = ParseCallableBlock(Function.Name, Function.Parameters, false, &Function.TemplateParameters);
		}
		if (Function.bConstexpr && Function.bDeleted)
		{
			ClearTypeParams();
			ReportErrorAt(Location, "constexpr function cannot be deleted");
			return false;
		}
		ClearTypeParams();
		if (bHasError)
		{
			return false;
		}
		ParserDetail::PreserveDeclaredHostSignature(
		    Function.DeclMeta,
		    Function.ReturnType,
		    Function.Parameters,
		    HostTypeNames);

		if (ParserDetail::HasFunctionWithSignature(Program->Functions, Function) &&
		    std::none_of(Program->Functions.begin(), Program->Functions.end(),
		        [&](const auto& Existing)
		{
			return Existing.Name == Function.Name && bool(Existing.Body) != bool(Function.Body);
		}))
		{
			ReportErrorAt(
			    Previous(),
			    "duplicate function with same signature: " + Function.Name);
			return false;
		}

		Program->Functions.push_back(std::move(Function));
		return true;
	}

	bool VeroScriptParser::LooksLikeOutOfClassDefinition() const
	{
		return ParserDetail::FindQualifiedDefinitionSeparator(Tokens, Current) < Tokens.size();
	}

	bool VeroScriptParser::ParseOutOfClassDefinition(
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    std::vector<FTemplateParameterList> TemplateHeaders)
	{
		if (std::any_of(
		        PrefixTypeParamDefaults.begin(),
		        PrefixTypeParamDefaults.end(),
		        [](const std::string& DefaultType)
		{
			return !DefaultType.empty();
		}))
		{
			ReportErrorAt(Peek(), "out-of-class definitions cannot repeat default type arguments");
			return false;
		}
		// Ordinary qualified bodies are resolved after the complete source graph
		// has been parsed. Constructors/destructors retain their existing parser.
		const auto Separator = ParserDetail::FindQualifiedDefinitionSeparator(Tokens, Current);
		if (Separator >= Tokens.size())
		{
			ReportErrorAt(Peek(), "invalid qualified definition");
			return false;
		}
		const auto Sep = Separator + 1;
		std::size_t OwnerSimple = Sep >= 2 ? Sep - 2 : Current;
		if (OwnerSimple < Tokens.size() && Tokens[OwnerSimple].Lexeme == ">")
		{
			std::size_t Depth = 1;
			while (OwnerSimple > Current && Depth)
			{
				--OwnerSimple;
				if (Tokens[OwnerSimple].Lexeme == ">")
				{
					++Depth;
				}

				else if (Tokens[OwnerSimple].Lexeme == "<")
				{
					--Depth;
				}
			}
			if (OwnerSimple > Current)
			{
				--OwnerSimple;
			}
		}
		auto OwnerBegin = OwnerSimple;
		while (OwnerBegin >= Current + 2 && Tokens[OwnerBegin - 1].Lexeme == "::")
		{
			auto Previous = OwnerBegin - 2;
			if (Tokens[Previous].Lexeme == ">")
			{
				std::size_t Depth = 1;
				while (Previous > Current && Depth)
				{
					--Previous;
					if (Tokens[Previous].Lexeme == ">")
					{
						++Depth;
					}
					else if (Tokens[Previous].Lexeme == "<")
					{
						--Depth;
					}
				}
				if (Previous == Current)
				{
					break;
				}
				--Previous;
			}
			if (Tokens[Previous].Kind != ETokenKind::Identifier)
			{
				break;
			}
			OwnerBegin = Previous;
		}
		if (OwnerBegin > Current && Tokens[OwnerBegin - 1].Lexeme == "::")
		{
			--OwnerBegin;
		}
		const bool bSpecial = Sep > Current && Sep < Tokens.size() &&
		    (Tokens[Sep].Lexeme == "~" || (OwnerBegin == Current && Tokens[Sep].Lexeme == Tokens[OwnerSimple].Lexeme));
		if (!bSpecial)
		{
			FSourceDefinition Definition;
			Definition.ModulePath = std::string(FileLabel);
			Definition.Namespace = ActiveNamespacePrefix;
			Definition.PrefixTypeParams = std::move(PrefixTypeParams);
			Definition.PrefixTypeParamDefaults = std::move(PrefixTypeParamDefaults);
			Definition.TemplateHeaders = std::move(TemplateHeaders);
			const auto Begin = Current;
			while (!IsAtEnd() && !CheckSymbol("{") && !CheckSymbol(";"))
			{
				Advance();
			}

			if (!MatchSymbol("{"))
			{
				ReportErrorAt(Peek(), "qualified definition requires a body");
				return false;
			}
			std::size_t Depth = 1;
			while (!IsAtEnd() && Depth)
			{
				if (CheckSymbol("{"))
				{
					++Depth;
				}

				else if (CheckSymbol("}"))
				{
					--Depth;
				}

				Advance();
			}
			if (Depth)
			{
				ReportErrorAt(Peek(), "unterminated qualified definition body");
				return false;
			}
			Definition.Tokens.assign(Tokens.begin() + Begin, Tokens.begin() + Current);
			Definition.Tokens.push_back(FToken{ETokenKind::EndOfFile, {}, Previous().Location});
			Program->SourceDefinitions.push_back(std::move(Definition));
			return true;
		}
		const std::size_t Start = Current;
		std::size_t OpenParen = Current;
		while (OpenParen < Tokens.size() && !(Tokens[OpenParen].Kind == ETokenKind::Symbol && Tokens[OpenParen].Lexeme == "("))
		{
			++OpenParen;
		}
		std::size_t LastSeparator = OpenParen;
		while (LastSeparator > Start)
		{
			--LastSeparator;
			if (Tokens[LastSeparator].Kind == ETokenKind::Symbol && Tokens[LastSeparator].Lexeme == "::")
			{
				break;
			}
		}
		std::size_t OwnerStart = LastSeparator;
		for (;;)
		{
			std::size_t Cursor = OwnerStart;
			if (Cursor > Start && Tokens[Cursor - 1].Lexeme == ">")
			{
				std::size_t Depth = 0;
				do
				{
					--Cursor;
					if (Tokens[Cursor].Lexeme == ">")
					{
						++Depth;
					}
					else if (Tokens[Cursor].Lexeme == "<")
					{
						--Depth;
					}
				}
				while (Cursor > Start && Depth != 0);
				if (Depth != 0)
				{
					ReportErrorAt(Tokens[Cursor], "invalid qualified definition");
					return false;
				}
			}
			if (Cursor == Start || !ParserDetail::IsNameToken(Tokens[Cursor - 1]))
			{
				ReportErrorAt(Tokens[Cursor], "expected owner type before '::'");
				return false;
			}
			OwnerStart = Cursor - 1;
			if (OwnerStart < Start + 2 || Tokens[OwnerStart - 1].Lexeme != "::")
			{
				break;
			}
			OwnerStart -= 1;
		}
		const std::size_t OwnerSimpleIndex = OwnerStart;
		const bool bDestructor = Tokens[LastSeparator + 1].Lexeme == "~";
		std::string OwnerSpelling;
		for (auto I = OwnerStart; I < LastSeparator; ++I)
		{
			OwnerSpelling += Tokens[I].Lexeme;
		}
		std::vector<FTypePathSegment> OwnerParts;
		if (!ParseTypePath(OwnerSpelling, OwnerParts))
		{
			ReportErrorAt(Tokens[OwnerStart], "invalid qualified definition");
			return false;
		}
		std::string OwnerName;
		std::vector<std::string> WrittenTypeParams;
		bool bHasGenericOwnerArgs = false;
		for (const auto& Part : OwnerParts)
		{
			if (!OwnerName.empty())
			{
				OwnerName += '.';
			}
			OwnerName += Part.Name;
			bHasGenericOwnerArgs = bHasGenericOwnerArgs || Part.bApplication;
			WrittenTypeParams.insert(WrittenTypeParams.end(), Part.Arguments.begin(), Part.Arguments.end());
		}
		OwnerName = ResolveNamespacedTypeName(std::move(OwnerName));
		const std::string OwnerLeaf = OwnerParts.back().Name;
		FStructDecl* StructOwner = nullptr;
		FClassDecl* ClassOwner = nullptr;
		const auto Matches = [&](const auto& Decl)
		{
			return Decl.ModulePath == FileLabel &&
			    (Decl.Name == OwnerName || Decl.Scope.DisplayName == OwnerName) &&
			    Decl.TypeParams.size() == WrittenTypeParams.size();
		};
		for (auto& Decl : Program->Structs)
		{
			if (Matches(Decl))
			{
				StructOwner = &Decl;
				break;
			}
		}
		for (auto& Decl : Program->Classes)
		{
			if (Matches(Decl))
			{
				ClassOwner = &Decl;
				break;
			}
		}
		if (!StructOwner && !ClassOwner)
		{
			ReportErrorAt(Tokens[OwnerStart], "out-of-class definition requires an earlier type declaration in the same file: " + OwnerName);
			return false;
		}
		const auto OwnerScope = StructOwner ? StructOwner->Scope : ClassOwner->Scope;
		const auto OwnerDeclarationContext =
		    StructOwner ? StructOwner->DeclarationContext : ClassOwner->DeclarationContext;
		OwnerName = StructOwner ? StructOwner->Name : ClassOwner->Name;
		const std::vector<std::string> OwnerTypeParams =
		    StructOwner ? StructOwner->TypeParams : ClassOwner->TypeParams;
		std::string Prefix;
		std::vector<std::string> Applied;
		for (const auto& Part : OwnerParts)
		{
			if (!Prefix.empty())
			{
				Prefix += '.';
			}
			Prefix += Part.Name;
			Applied.insert(Applied.end(), Part.Arguments.begin(), Part.Arguments.end());
			const auto Check = [&](const auto& Declarations)
			{
				for (const auto& Decl : Declarations)
				{
					if ((Decl.Name == ResolveNamespacedTypeName(Prefix) ||
					        Decl.Scope.DisplayName == ResolveNamespacedTypeName(Prefix)) &&
					    Decl.ModulePath == FileLabel && Decl.TypeParams == Applied)
					{
						return true;
					}
				}
				return false;
			};
			if (Part.bApplication && !Check(Program->Classes) && !Check(Program->Structs))
			{
				ReportErrorAt(Tokens[OwnerStart],
				    "generic out-of-class owner must repeat its declared type parameters in order");
				return false;
			}
		}
		if (!PrefixTypeParams.empty() && PrefixTypeParams != OwnerTypeParams)
		{
			ReportErrorAt(
			    Tokens[OwnerSimpleIndex],
			    "template prefix on an out-of-class definition must repeat the owner type parameters in order");
			return false;
		}
		if (OwnerTypeParams.empty() && bHasGenericOwnerArgs)
		{
			ReportErrorAt(Tokens[OwnerSimpleIndex], "non-generic out-of-class owner cannot have type arguments");
			return false;
		}
		if (!OwnerTypeParams.empty() && (!bHasGenericOwnerArgs || WrittenTypeParams != OwnerTypeParams))
		{
			ReportErrorAt(
			    Tokens[OwnerSimpleIndex],
			    "generic out-of-class owner must repeat its declared type parameters in order");
			return false;
		}
		struct FActiveTypeParamScope
		{
			~FActiveTypeParamScope()
			{
				for (const std::string& Name : Added)
				{
					Active.erase(Name);
				}
			}

			std::unordered_set<std::string>& Active;

			std::vector<std::string> Added;
		} TypeParamScope{ActiveTypeParams, {}};
		for (const std::string& TypeParam : OwnerTypeParams)
		{
			if (ActiveTypeParams.insert(TypeParam).second)
			{
				TypeParamScope.Added.push_back(TypeParam);
			}
		}

		struct FParserContextGuard
		{
			VeroScriptParser& Parser;
			std::shared_ptr<FDeferredSyntax> Context;
			std::vector<FTypeFrame> Frames;
			std::string Namespace;
			~FParserContextGuard()
			{
				Parser.RestoreBindings(*Context);
				Parser.TypeFrames = std::move(Frames);
				Parser.ActiveNamespacePrefix = std::move(Namespace);
				Parser.FragmentScopeId = Context->ScopeId;
				Parser.FunctionOwner = Context->FunctionOwner;
				Parser.FunctionTypeParameters = Context->FunctionTypeParameters;
			}
		} ContextGuard{*this, CaptureDeclarationContext(), std::move(TypeFrames), ActiveNamespacePrefix};
		FDeferredSyntax OwnerContext = OwnerDeclarationContext ? *OwnerDeclarationContext : FDeferredSyntax{};
		OwnerContext.TypeOwner = OwnerName;
		OwnerContext.TypeScope = std::make_shared<FTypeDeclarationScope>(OwnerScope);
		OwnerContext.ModulePath = std::string(FileLabel);
		OwnerContext.Namespace = OwnerName;
		OwnerContext.TypeParameters = OwnerTypeParams;
		RestoreDeclarationContext(OwnerContext, false);
		ActiveNamespacePrefix = OwnerName;

		std::string ReturnType;
		if (!bDestructor)
		{
			const std::string MemberLexeme(Tokens[LastSeparator + 1].Lexeme);
			const bool bConstructor = MemberLexeme == OwnerLeaf && OwnerStart == Start;
			if (!bConstructor)
			{
				Current = Start;
				ReturnType = ParseReturnTypeName();
				if (bHasError || Current != OwnerStart)
				{
					ReportErrorAt(Tokens[OwnerStart], "invalid return type before out-of-class member definition");
					return false;
				}
			}
		}

		Current = LastSeparator + 1;
		std::string MemberName;
		bool bConstructor = false;
		if (bDestructor)
		{
			Advance();
			if (!MatchName() || Previous().Lexeme != OwnerLeaf)
			{
				ReportErrorAt(Peek(), "out-of-class destructor name must match its owner type");
				return false;
			}
			MemberName = "~" + OwnerLeaf;
		}
		else if (Check(ETokenKind::Keyword) && Peek().Lexeme == "operator")
		{
			if (!MatchOperatorMethodName(MemberName) || MemberName.empty())
			{
				return false;
			}
		}
		else
		{
			if (!MatchName())
			{
				ReportErrorAt(Peek(), "expected member name after '::'");
				return false;
			}
			MemberName.assign(Previous().Lexeme);
			bConstructor = MemberName == OwnerLeaf && OwnerStart == Start;
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after out-of-class member name");
			return false;
		}

		std::vector<FFunctionParam> Parameters;
		if (!CheckSymbol(")"))
		{
			do
			{
				const auto ParameterBegin = Current;
				FFunctionParam Param;
				Param.bIsConst = MatchOptionalConst();
				Param.bIsRef = MatchOptionalRef();
				Param.TypeName = ParseTypeName();
				if (bHasError || !ParseParamArraySuffix(Param.TypeName) || !MatchOptionalTrailingRefAmpersand(Param.bIsRef) || !ParseOptionalParamVariadic(Param, true))
				{
					return false;
				}
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected parameter name in out-of-class definition");
					return false;
				}
				Param.Name.assign(Previous().Lexeme);
				if (!ParseArraySuffix(Param.TypeName))
				{
					return false;
				}
				if (CheckSymbol("="))
				{
					ReportErrorAt(Peek(), "default arguments belong on the in-class declaration only");
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after out-of-class definition parameters");
			return false;
		}
		const bool bConst = Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
		if (bConst)
		{
			Advance();
		}
		if ((bConstructor || bDestructor) && bConst)
		{
			ReportErrorAt(Previous(), "constructor or destructor definition cannot be const");
			return false;
		}
		std::vector<FBaseCtorInit> BaseInits;
		if (bConstructor && MatchSymbol(":"))
		{
			do
			{
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected base type in constructor initializer list");
					return false;
				}
				FBaseCtorInit Init;
				Init.BaseName.assign(Previous().Lexeme);
				Init.Location = Previous().Location;
				while (MatchMemberAccessOp())
				{
					if (!MatchName())
					{
						ReportErrorAt(Peek(), "expected identifier in base type name");
						return false;
					}
					Init.BaseName.push_back('.');
					Init.BaseName.append(Previous().Lexeme);
				}
				Init.BaseName = ResolveNamespacedTypeName(std::move(Init.BaseName));
				if (!MatchSymbol("("))
				{
					ReportErrorAt(Peek(), "expected '(' after base type in constructor initializer list");
					return false;
				}
				if (!CheckSymbol(")"))
				{
					do
					{
						std::unique_ptr<FExpr> Argument = ParseCallArgument();
						if (!Argument)
						{
							return false;
						}
						Init.Args.push_back(std::move(Argument));
					}
					while (MatchSymbol(","));
				}
				if (!MatchSymbol(")"))
				{
					ReportErrorAt(Peek(), "expected ')' after base constructor arguments");
					return false;
				}
				BaseInits.push_back(std::move(Init));
			}
			while (MatchSymbol(","));
		}
		if (!CheckSymbol("{"))
		{
			ReportErrorAt(Peek(), "out-of-class definition requires a function body");
			return false;
		}
		std::unique_ptr<FBlockStmt> Body =
		    ParseCallableBlock(bDestructor ? "$destructor" : "$constructor", Parameters);
		// Local declarations may grow the flat declaration vectors while parsing.
		StructOwner = nullptr;
		ClassOwner = nullptr;
		for (auto& Decl : Program->Structs)
		{
			if (Decl.Scope.Id == OwnerScope.Id)
			{
				StructOwner = &Decl;
				break;
			}
		}
		for (auto& Decl : Program->Classes)
		{
			if (Decl.Scope.Id == OwnerScope.Id)
			{
				ClassOwner = &Decl;
				break;
			}
		}
		if (!Body)
		{
			return false;
		}

		const auto SameParams = [&](const std::vector<FFunctionParam>& Declared)
		{
			if (Declared.size() != Parameters.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Declared[Index].TypeName != Parameters[Index].TypeName || Declared[Index].bIsRef != Parameters[Index].bIsRef || Declared[Index].bIsConst != Parameters[Index].bIsConst || Declared[Index].bIsVariadic != Parameters[Index].bIsVariadic)
				{
					return false;
				}
			}
			return true;
		};
		if (bDestructor)
		{
			if (!Parameters.empty())
			{
				ReportErrorAt(Tokens[OpenParen], "destructor cannot have parameters");
				return false;
			}
			FDestructorDecl* const Decl = StructOwner
			    ? StructOwner->Destructor.get()
			    : ClassOwner->Destructor.get();
			if (!Decl)
			{
				ReportErrorAt(Tokens[LastSeparator + 1], "out-of-class destructor has no matching in-class declaration");
				return false;
			}
			if (Decl->Body)
			{
				ReportErrorAt(Tokens[LastSeparator + 1], "duplicate destructor definition");
				return false;
			}
			Decl->Body = std::move(Body);
			return true;
		}
		if (bConstructor)
		{
			auto& Constructors = StructOwner ? StructOwner->Constructors : ClassOwner->Constructors;
			for (FConstructorDecl& Decl : Constructors)
			{
				if (!SameParams(Decl.Parameters))
				{
					continue;
				}
				if (Decl.bDeleted || Decl.bDefaulted || Decl.Body)
				{
					ReportErrorAt(Tokens[LastSeparator + 1], "constructor is deleted, defaulted, or already defined");
					return false;
				}
				Decl.Body = std::move(Body);
				Decl.BaseInits = std::move(BaseInits);
				if (!ClassifyConstructorFieldInitializers(
				        Constructors,
				        StructOwner ? StructOwner->Fields : ClassOwner->Fields))
				{
					return false;
				}
				return true;
			}
			ReportErrorAt(Tokens[LastSeparator + 1], "out-of-class constructor has no matching in-class declaration");
			return false;
		}

		auto& Methods = StructOwner ? StructOwner->Methods : ClassOwner->Methods;
		for (FMethodDecl& Decl : Methods)
		{
			if (Decl.Name != MemberName || Decl.bIsConst != bConst || !SameParams(Decl.Parameters))
			{
				continue;
			}
			if (Decl.ReturnType != ReturnType)
			{
				ReportErrorAt(Tokens[LastSeparator + 1], "out-of-class method return type does not match its declaration");
				return false;
			}
			if (Decl.bDeleted || Decl.Body)
			{
				ReportErrorAt(Tokens[LastSeparator + 1], "method is deleted or already defined");
				return false;
			}
			Decl.Body = std::move(Body);
			return true;
		}
		ReportErrorAt(Tokens[LastSeparator + 1], "out-of-class method has no matching in-class declaration");
		return false;
	}

	bool VeroScriptParser::ValidateOutOfClassDefinitions()
	{
		// Source declarations without bodies are diagnosed by the compiler so
		// declaration-only package facades can reuse the same parser.
		return true;
	}

	bool VeroScriptParser::ParseStructDecl(
	    const bool bExported,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    const EMemberAccess DefaultMemberAccess,
	    const bool bStaticStruct,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "struct")
		{
			return false;
		}

		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected struct name");
			return false;
		}

		FStructDecl StructDecl;
		StructDecl.Name = QualifyTypeDeclName(std::string(Previous().Lexeme));
		StructDecl.ModulePath.assign(FileLabel);
		StructDecl.bExported = bExported;
		StructDecl.bIsStatic = bStaticStruct;
		StructDecl.DeclMeta = std::move(DeclMeta);
		StructDecl.Location = Location;
		StructDecl.Scope = MakeTypeScope(StructDecl.Name, 0);
		StructDecl.DeclarationContext = CaptureDeclarationContext();
		StructDecl.TypeParams = std::move(PrefixTypeParams);
		StructDecl.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		StructDecl.TemplateParameters = std::move(PrefixTemplateParameters);
		const auto ClearTypeParams = [&StructDecl, this]()
		{
			for (const std::string& TypeParam : StructDecl.TypeParams)
			{
				ActiveTypeParams.erase(TypeParam);
			}
		};

		for (const std::string& TypeParam : StructDecl.TypeParams)
		{
			ActiveTypeParams.insert(TypeParam);
		}
		if (!TemplateParameterNames(StructDecl).empty() && CheckSymbol("<"))
		{
			ClearTypeParams();
			ReportErrorAt(
			    Peek(),
			    "cannot combine a template<...> prefix with type-name parameters");
			return false;
		}
		if (TemplateParameterNames(StructDecl).empty() && MatchSymbol("<"))
		{
			if (!ParseTypeParameterList(
			        StructDecl.TypeParams,
			        StructDecl.TypeParamDefaults, &StructDecl.TemplateParameters))
			{
				ClearTypeParams();
				return false;
			}
			for (const std::string& TypeParam : StructDecl.TypeParams)
			{
				ActiveTypeParams.insert(TypeParam);
			}
		}
		if (StructDecl.bIsStatic && !TemplateParameterNames(StructDecl).empty())
		{
			ClearTypeParams();
			ReportErrorId(Location, "diag.static_struct_cannot_be_generic");
			return false;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "final")
		{
			StructDecl.bFinal = true;
			Advance();
		}

		if (StructDecl.bIsStatic && CheckSymbol(":"))
		{
			ClearTypeParams();
			ReportErrorId(Peek(), "diag.static_struct_cannot_inherit");
			return false;
		}
		if (MatchSymbol(":"))
		{
			std::vector<std::string> BaseTypeNames;
			std::vector<EMemberAccess> BaseInheritanceAccess;
			if (!ParseInheritanceClause(
			        BaseTypeNames,
			        BaseInheritanceAccess,
			        StructDecl.InterfaceTypeNames,
			        false,
			        EMemberAccess::Public,
			        Location))
			{
				ClearTypeParams();
				return false;
			}
			if (!BaseTypeNames.empty())
			{
				StructDecl.BaseTypeName = BaseTypeNames.front();
				StructDecl.BaseInheritanceAccess = BaseInheritanceAccess.empty()
				    ? EMemberAccess::Public
				    : BaseInheritanceAccess.front();
			}
		}

		if (!ParseOptionalWhereBounds(StructDecl.TypeParams, StructDecl.Bounds, "type"))
		{
			ClearTypeParams();
			return false;
		}

		if (!MatchSymbol("{"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '{' after struct name");
			return false;
		}

		StructDecl.Scope.Kind = "struct";
		EnterTypeDeclaration(StructDecl.Name, bExported, StructDecl.TypeParams, StructDecl.Scope, StructDecl.TemplateParameters);
		FTypeDeclarationGuard TypeScope(*this);
		TypeFrames.back().OpenStruct = &StructDecl;
		EMemberAccess CurrentMemberAccess = DefaultMemberAccess;
		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			const std::size_t Checkpoint = Current;
			if (TryParseAccessSpecifier(CurrentMemberAccess))
			{
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "friend")
			{
				if (!ParseFriendDecl(StructDecl.Friends))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			FDeclMeta MemberMeta;
			if (LooksLikeAnnotationStart())
			{
				const std::size_t AnnotCheckpoint = Current;
				const int AnnotPendingCheckpoint = PendingGenericCloseCount;
				const std::string TypeLeaf = TypeNameLeaf(StructDecl.Name);
				while (LooksLikeAnnotationStart())
				{
					// `TypeName()` is a constructor, not an annotation.
					if (LooksLikeConstructorDecl(TypeLeaf))
					{
						break;
					}
					FDeclMeta One;
					if (!TryParseOneAnnotation(One))
					{
						break;
					}
					MemberMeta.insert(MemberMeta.end(), One.begin(), One.end());
					MemberMeta.Annotations.insert(MemberMeta.Annotations.end(), One.Annotations.begin(), One.Annotations.end());
				}
				const bool bIsCtor = LooksLikeConstructorDecl(TypeLeaf);
				const bool bIsDtor =
				    CheckSymbol("~") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == "~");
				const bool bKeepAnnot =
				    IsScopedTypeDeclaration() || Peek().Lexeme == "template" ||
				    (Check(ETokenKind::Keyword) &&
				        (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private")) ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "static") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "explicit") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" ||
				    CheckTypeName() || bIsCtor || bIsDtor;
				if (!bKeepAnnot)
				{
					Current = AnnotCheckpoint;
					PendingGenericCloseCount = AnnotPendingCheckpoint;
					MemberMeta.clear();
				}
			}
			EMemberAccess MemberAccess = CurrentMemberAccess;
			const bool bHasInlineMemberAccess =
			    TryParseInlineMemberAccess(MemberAccess);
			if (bHasInlineMemberAccess && Check(ETokenKind::Keyword) && (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private"))
			{
				ClearTypeParams();
				ReportErrorId(Peek(), "diag.duplicate_member_access_modifier");
				return false;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "using" && LooksLikeUsingMemberDecl())
			{
				if (!ParseUsingMemberDecl(MemberAccess, StructDecl.UsingMembers))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
			{
				if (!ParseTemplateMember(StructDecl.Methods, MemberAccess, std::move(MemberMeta), false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (IsScopedTypeDeclaration())
			{
				if (!ParseScopedTypeDeclaration(MemberAccess, std::move(MemberMeta), false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}

			if (LooksLikeConstructorDecl(TypeNameLeaf(StructDecl.Name)))
			{
				if (bHasInlineMemberAccess)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.inline_member_access_not_supported_on_constructor");
					return false;
				}
				if (StructDecl.bIsStatic)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.static_struct_cannot_declare_constructor");
					return false;
				}
				if (!ParseConstructorDecl(
				        TypeNameLeaf(StructDecl.Name),
				        StructDecl.Constructors,
				        MemberAccess,
				        std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if ((Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == "~") || CheckSymbol("~"))
			{
				if (StructDecl.bIsStatic)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.static_struct_cannot_declare_destructor");
					return false;
				}
				if (!ParseDestructorDecl(
				        TypeNameLeaf(StructDecl.Name),
				        StructDecl.Destructor,
				        MemberAccess,
				        std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
			{
				if (!ParseMethodDecl(StructDecl.Methods, MemberAccess, std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if ((Check(ETokenKind::Keyword) && Peek().Lexeme == "static") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" || CheckTypeName() || LooksLikeGenericFunctionDecl())
			{
				if (LooksLikeMethodDecl())
				{
					if (!ParseMethodDecl(StructDecl.Methods, MemberAccess, std::move(MemberMeta)))
					{
						ClearTypeParams();
						return false;
					}
					continue;
				}
				if (!ParseFieldDecl(StructDecl.Fields, MemberAccess, std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}

			Current = Checkpoint;
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected field or method in struct");
			return false;
		}

		if (!MatchSymbol("}"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '}' after struct body");
			return false;
		}

		if (StructDecl.bIsStatic)
		{
			for (FFieldDecl& Field : StructDecl.Fields)
			{
				if (Field.bIsMutable)
				{
					ClearTypeParams();
					ReportErrorId(Field.Location, "diag.static_struct_field_cannot_be_mutable");
					return false;
				}
				Field.bIsStatic = true;
			}
			for (FMethodDecl& Method : StructDecl.Methods)
			{
				if (Method.bVirtual || Method.bOverride || Method.bFinal || Method.bPureVirtual || Method.bIsConst)
				{
					ClearTypeParams();
					ReportErrorId(Method.Location, "diag.static_struct_method_cannot_have_instance_modifiers");
					return false;
				}
				Method.bIsStatic = true;
			}
			if (!StructDecl.UsingMembers.empty())
			{
				ClearTypeParams();
				ReportErrorId(Location, "diag.static_struct_cannot_declare_using_members");
				return false;
			}
		}
		if (!ClassifyConstructorFieldInitializers(StructDecl.Constructors, StructDecl.Fields))
		{
			ClearTypeParams();
			return false;
		}

		ClearTypeParams();

		StructDecl.Scope.OwnTypeParameterCount = static_cast<std::uint32_t>(StructDecl.TypeParams.size());
		if (!StructDecl.Scope.EnclosingTypeParameters.empty())
		{
			StructDecl.TypeParams.insert(StructDecl.TypeParams.begin(),
			    StructDecl.Scope.EnclosingTypeParameters.begin(),
			    StructDecl.Scope.EnclosingTypeParameters.end());
			StructDecl.TypeParamDefaults.insert(StructDecl.TypeParamDefaults.begin(),
			    StructDecl.Scope.EnclosingTypeParameters.size(), std::string{});
		}
		LeaveTypeDeclaration();

		const std::string DeclaredTypeName = StructDecl.Name;
		const bool bGenericType = !TemplateParameterNames(StructDecl).empty();
		Program->Structs.push_back(std::move(StructDecl));
		return ParseTrailingTypeDeclarators(DeclaredTypeName, bExported, bGenericType);
	}

	bool VeroScriptParser::ParseClassDecl(
	    const bool bExported,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    const EMemberAccess DefaultMemberAccess,
	    const bool bStaticClass,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "class")
		{
			return false;
		}

		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected class name");
			return false;
		}

		FClassDecl ClassDecl;
		ClassDecl.Name = QualifyTypeDeclName(std::string(Previous().Lexeme));
		ClassDecl.ModulePath.assign(FileLabel);
		ClassDecl.bExported = bExported;
		ClassDecl.bIsStatic = bStaticClass;
		ClassDecl.DeclMeta = std::move(DeclMeta);
		ClassDecl.Location = Location;
		ClassDecl.Scope = MakeTypeScope(ClassDecl.Name, 0);
		ClassDecl.DeclarationContext = CaptureDeclarationContext();
		ClassDecl.TypeParams = std::move(PrefixTypeParams);
		ClassDecl.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		ClassDecl.TemplateParameters = std::move(PrefixTemplateParameters);
		const auto ClearTypeParams = [&ClassDecl, this]()
		{
			for (const std::string& TypeParam : ClassDecl.TypeParams)
			{
				ActiveTypeParams.erase(TypeParam);
			}
		};

		for (const std::string& TypeParam : ClassDecl.TypeParams)
		{
			ActiveTypeParams.insert(TypeParam);
		}
		if (!TemplateParameterNames(ClassDecl).empty() && CheckSymbol("<"))
		{
			ClearTypeParams();
			ReportErrorAt(
			    Peek(),
			    "cannot combine a template<...> prefix with type-name parameters");
			return false;
		}
		if (TemplateParameterNames(ClassDecl).empty() && MatchSymbol("<"))
		{
			if (!ParseTypeParameterList(
			        ClassDecl.TypeParams,
			        ClassDecl.TypeParamDefaults, &ClassDecl.TemplateParameters))
			{
				ClearTypeParams();
				return false;
			}
			for (const std::string& TypeParam : ClassDecl.TypeParams)
			{
				ActiveTypeParams.insert(TypeParam);
			}
		}
		if (ClassDecl.bIsStatic && !TemplateParameterNames(ClassDecl).empty())
		{
			ClearTypeParams();
			ReportErrorId(Location, "diag.static_class_cannot_be_generic");
			return false;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "final")
		{
			ClassDecl.bFinal = true;
			Advance();
		}

		if (ClassDecl.bIsStatic && CheckSymbol(":"))
		{
			ClearTypeParams();
			ReportErrorId(Peek(), "diag.static_class_cannot_inherit");
			return false;
		}
		if (MatchSymbol(":"))
		{
			if (!ParseInheritanceClause(
			        ClassDecl.BaseTypeNames,
			        ClassDecl.BaseInheritanceAccess,
			        ClassDecl.InterfaceTypeNames,
			        true,
			        EMemberAccess::Private,
			        Location))
			{
				ClearTypeParams();
				return false;
			}
		}

		if (!ParseOptionalWhereBounds(ClassDecl.TypeParams, ClassDecl.Bounds, "type"))
		{
			ClearTypeParams();
			return false;
		}

		if (!MatchSymbol("{"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '{' after class name");
			return false;
		}

		// C++: class members are private by default (struct stays public).
		ClassDecl.Scope.Kind = "class";
		EnterTypeDeclaration(ClassDecl.Name, bExported, ClassDecl.TypeParams, ClassDecl.Scope, ClassDecl.TemplateParameters);
		FTypeDeclarationGuard TypeScope(*this);
		TypeFrames.back().OpenClass = &ClassDecl;
		EMemberAccess CurrentMemberAccess = DefaultMemberAccess;
		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			const std::size_t Checkpoint = Current;
			if (TryParseAccessSpecifier(CurrentMemberAccess))
			{
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "friend")
			{
				if (!ParseFriendDecl(ClassDecl.Friends))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "default")
			{
				if (ClassDecl.bIsStatic)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.static_class_cannot_declare_default_initialization_block");
					return false;
				}
				if (ClassDecl.ClassDefaults)
				{
					ClearTypeParams();
					ReportErrorAt(Peek(), "duplicate class default block");
					return false;
				}
				Advance();
				if (!CheckSymbol("{"))
				{
					ClearTypeParams();
					ReportErrorAt(Peek(), "expected '{' after 'default'");
					return false;
				}
				ClassDecl.ClassDefaults = ParseCallableBlock("$defaults", {});
				if (!ClassDecl.ClassDefaults)
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			FDeclMeta MemberMeta;
			if (LooksLikeAnnotationStart())
			{
				const std::size_t AnnotCheckpoint = Current;
				const int AnnotPendingCheckpoint = PendingGenericCloseCount;
				const std::string TypeLeaf = TypeNameLeaf(ClassDecl.Name);
				while (LooksLikeAnnotationStart())
				{
					// `TypeName()` is a constructor, not an annotation.
					if (LooksLikeConstructorDecl(TypeLeaf))
					{
						break;
					}
					FDeclMeta One;
					if (!TryParseOneAnnotation(One))
					{
						break;
					}
					MemberMeta.insert(MemberMeta.end(), One.begin(), One.end());
					MemberMeta.Annotations.insert(MemberMeta.Annotations.end(), One.Annotations.begin(), One.Annotations.end());
				}
				const bool bIsCtor = LooksLikeConstructorDecl(TypeLeaf);
				const bool bIsDtor =
				    CheckSymbol("~") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == "~");
				const bool bKeepAnnot =
				    IsScopedTypeDeclaration() || Peek().Lexeme == "template" ||
				    (Check(ETokenKind::Keyword) &&
				        (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private")) ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "static") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "explicit") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" ||
				    CheckTypeName() || bIsCtor || bIsDtor;
				if (!bKeepAnnot)
				{
					Current = AnnotCheckpoint;
					PendingGenericCloseCount = AnnotPendingCheckpoint;
					MemberMeta.clear();
				}
			}
			EMemberAccess MemberAccess = CurrentMemberAccess;
			const bool bHasInlineMemberAccess =
			    TryParseInlineMemberAccess(MemberAccess);
			if (bHasInlineMemberAccess && Check(ETokenKind::Keyword) && (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private"))
			{
				ClearTypeParams();
				ReportErrorId(Peek(), "diag.duplicate_member_access_modifier");
				return false;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "using" && LooksLikeUsingMemberDecl())
			{
				if (!ParseUsingMemberDecl(MemberAccess, ClassDecl.UsingMembers))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
			{
				if (!ParseTemplateMember(ClassDecl.Methods, MemberAccess, std::move(MemberMeta), false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (IsScopedTypeDeclaration())
			{
				if (!ParseScopedTypeDeclaration(MemberAccess, std::move(MemberMeta), false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}

			if (LooksLikeConstructorDecl(TypeNameLeaf(ClassDecl.Name)))
			{
				if (bHasInlineMemberAccess)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.inline_member_access_not_supported_on_constructor");
					return false;
				}
				if (ClassDecl.bIsStatic)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.static_class_cannot_declare_constructor");
					return false;
				}
				if (!ParseConstructorDecl(
				        TypeNameLeaf(ClassDecl.Name),
				        ClassDecl.Constructors,
				        MemberAccess,
				        std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if ((Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == "~") || CheckSymbol("~"))
			{
				if (ClassDecl.bIsStatic)
				{
					ClearTypeParams();
					ReportErrorId(Peek(), "diag.static_class_cannot_declare_destructor");
					return false;
				}
				if (!ParseDestructorDecl(
				        TypeNameLeaf(ClassDecl.Name),
				        ClassDecl.Destructor,
				        MemberAccess,
				        std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
			{
				if (!ParseMethodDecl(ClassDecl.Methods, MemberAccess, std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if ((Check(ETokenKind::Keyword) && Peek().Lexeme == "static") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" || CheckTypeName() || LooksLikeGenericFunctionDecl())
			{
				if (LooksLikeMethodDecl())
				{
					if (!ParseMethodDecl(ClassDecl.Methods, MemberAccess, std::move(MemberMeta)))
					{
						ClearTypeParams();
						return false;
					}
					continue;
				}
				if (!ParseFieldDecl(ClassDecl.Fields, MemberAccess, std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}

			Current = Checkpoint;
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected field or method in class");
			return false;
		}

		if (!MatchSymbol("}"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '}' after class body");
			return false;
		}

		if (ClassDecl.bIsStatic)
		{
			for (FFieldDecl& Field : ClassDecl.Fields)
			{
				if (Field.bIsMutable)
				{
					ClearTypeParams();
					ReportErrorId(Field.Location, "diag.static_class_field_cannot_be_mutable");
					return false;
				}
				Field.bIsStatic = true;
			}
			for (FMethodDecl& Method : ClassDecl.Methods)
			{
				if (Method.bVirtual || Method.bOverride || Method.bFinal || Method.bPureVirtual || Method.bIsConst)
				{
					ClearTypeParams();
					ReportErrorId(Method.Location, "diag.static_class_method_cannot_have_instance_modifiers");
					return false;
				}
				Method.bIsStatic = true;
			}
			if (!ClassDecl.UsingMembers.empty())
			{
				ClearTypeParams();
				ReportErrorId(Location, "diag.static_class_cannot_declare_using_members");
				return false;
			}
		}
		if (!ClassifyConstructorFieldInitializers(ClassDecl.Constructors, ClassDecl.Fields))
		{
			ClearTypeParams();
			return false;
		}

		ClearTypeParams();

		ClassDecl.Scope.OwnTypeParameterCount = static_cast<std::uint32_t>(ClassDecl.TypeParams.size());
		if (!ClassDecl.Scope.EnclosingTypeParameters.empty())
		{
			ClassDecl.TypeParams.insert(ClassDecl.TypeParams.begin(),
			    ClassDecl.Scope.EnclosingTypeParameters.begin(),
			    ClassDecl.Scope.EnclosingTypeParameters.end());
			ClassDecl.TypeParamDefaults.insert(ClassDecl.TypeParamDefaults.begin(),
			    ClassDecl.Scope.EnclosingTypeParameters.size(), std::string{});
		}
		LeaveTypeDeclaration();

		const std::string DeclaredTypeName = ClassDecl.Name;
		const bool bGenericType = !TemplateParameterNames(ClassDecl).empty();
		Program->Classes.push_back(std::move(ClassDecl));
		return ParseTrailingTypeDeclarators(DeclaredTypeName, bExported, bGenericType);
	}

	bool VeroScriptParser::ParseTrailingTypeDeclarators(
	    const std::string& TypeName,
	    const bool bExported,
	    const bool bGenericType)
	{
		// VeroScript keeps the existing `class A {}` spelling, while accepting
		// the C++-compatible `class A {};` and `class A {} Value;` forms.
		const std::uint32_t TypeEndLine = Previous().Location.Line;
		if (MatchSymbol(";"))
		{
			return true;
		}
		// A semicolon-less type definition may be followed by an existing
		// top-level expression such as `print(...)`. Requiring a same-line
		// declarator makes `struct A {} Value;` unambiguous without changing the
		// meaning of those existing scripts. A declaration on the following line
		// can still use the ordinary `A Value()` or `A Value = A()` spelling.
		if (Peek().Location.Line != TypeEndLine)
		{
			return true;
		}

		const auto IsDeclaratorFollower = [](const FToken& Token)
		{
			return Token.Kind == ETokenKind::Symbol && (Token.Lexeme == ";" || Token.Lexeme == "," || Token.Lexeme == "=" || Token.Lexeme == "(" || Token.Lexeme == "[");
		};
		if (Current + 1 >= Tokens.size() || !ParserDetail::IsNameToken(Tokens[Current]) || !IsDeclaratorFollower(Tokens[Current + 1]))
		{
			return true;
		}
		if (bGenericType)
		{
			ReportErrorAt(
			    Peek(),
			    "a generic type definition cannot declare an unbound instance; use a specialized type separately");
			return false;
		}
		if (!BlockScopes.empty() || !TypeFrames.empty())
		{
			ReportErrorAt(Peek(), "declare nested or local type instances in a separate declaration");
			return false;
		}

		while (true)
		{
			if (!MatchName())
			{
				ReportErrorAt(Peek(), "expected variable name after type definition");
				return false;
			}
			const FSourceLocation Location = Previous().Location;
			const std::string VariableName = QualifyDeclName(std::string(Previous().Lexeme));
			std::string VariableTypeName = TypeName;
			if (!ParseArraySuffix(VariableTypeName))
			{
				return false;
			}

			std::unique_ptr<FExpr> Initializer;
			if (CheckSymbol(",") || CheckSymbol(";"))
			{
				if (!Runtime::IsNativeArrayTypeName(VariableTypeName))
				{
					Initializer = std::make_unique<FCallExpr>(
					    TypeName,
					    std::vector<std::unique_ptr<FExpr>>{});
					Initializer->Location = Location;
				}
			}
			else
			{
				Initializer = ParseTypedVarInitializer(
				    VariableTypeName,
				    Runtime::IsNativeArrayTypeName(VariableTypeName));
				if (bHasError)
				{
					return false;
				}
			}

			auto Variable = std::make_unique<FVarDeclStmt>(
			    std::move(VariableTypeName),
			    VariableName,
			    std::move(Initializer));
			Variable->bExported = bExported;
			Variable->ModulePath.assign(FileLabel);
			Variable->Location = Location;
			Program->TopLevelStatements.push_back(std::move(Variable));

			if (!MatchSymbol(","))
			{
				break;
			}
		}

		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after variables declared with a type definition");
			return false;
		}
		return true;
	}

	bool VeroScriptParser::ParseInterfaceDecl(
	    const bool bExported,
	    FDeclMeta&& DeclMeta,
	    std::vector<std::string> PrefixTypeParams,
	    std::vector<std::string> PrefixTypeParamDefaults,
	    const EMemberAccess DefaultMemberAccess,
	    FTemplateParameterList PrefixTemplateParameters)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "interface")
		{
			return false;
		}

		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected interface name");
			return false;
		}

		FInterfaceDecl InterfaceDecl;
		InterfaceDecl.Name = QualifyTypeDeclName(std::string(Previous().Lexeme));
		InterfaceDecl.ModulePath.assign(FileLabel);
		InterfaceDecl.bExported = bExported;
		InterfaceDecl.DeclMeta = std::move(DeclMeta);
		InterfaceDecl.Location = Location;
		InterfaceDecl.Scope = MakeTypeScope(InterfaceDecl.Name, 0);
		InterfaceDecl.DeclarationContext = CaptureDeclarationContext();
		InterfaceDecl.TypeParams = std::move(PrefixTypeParams);
		InterfaceDecl.TypeParamDefaults = std::move(PrefixTypeParamDefaults);
		InterfaceDecl.TemplateParameters = std::move(PrefixTemplateParameters);
		const auto ClearTypeParams = [&InterfaceDecl, this]()
		{
			for (const std::string& TypeParam : InterfaceDecl.TypeParams)
			{
				ActiveTypeParams.erase(TypeParam);
			}
		};

		for (const std::string& TypeParam : InterfaceDecl.TypeParams)
		{
			ActiveTypeParams.insert(TypeParam);
		}
		if (!TemplateParameterNames(InterfaceDecl).empty() && CheckSymbol("<"))
		{
			ClearTypeParams();
			ReportErrorAt(
			    Peek(),
			    "cannot combine a template<...> prefix with type-name parameters");
			return false;
		}
		if (TemplateParameterNames(InterfaceDecl).empty() && MatchSymbol("<"))
		{
			if (!ParseTypeParameterList(
			        InterfaceDecl.TypeParams,
			        InterfaceDecl.TypeParamDefaults, &InterfaceDecl.TemplateParameters))
			{
				ClearTypeParams();
				return false;
			}
			for (const std::string& TypeParam : InterfaceDecl.TypeParams)
			{
				ActiveTypeParams.insert(TypeParam);
			}
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "final")
		{
			InterfaceDecl.bFinal = true;
			Advance();
		}

		if (MatchSymbol(":"))
		{
			do
			{
				if (!Match(ETokenKind::Identifier))
				{
					ClearTypeParams();
					ReportErrorAt(Peek(), "expected parent interface name");
					return false;
				}
				std::string ParentName(Previous().Lexeme);
				while (MatchMemberAccessOp())
				{
					if (!Match(ETokenKind::Identifier))
					{
						ClearTypeParams();
						ReportErrorAt(Peek(), "expected identifier after '.' or '::' in type name");
						return false;
					}
					ParentName += '.';
					ParentName += Previous().Lexeme;
				}
				ParentName = ResolveNamespacedTypeName(std::move(ParentName));
				if (!IsInterfaceName(ParentName))
				{
					ClearTypeParams();
					ReportErrorAt(Previous(), "interface parent must be an interface");
					return false;
				}
				std::string ParentTypeName = ParentName;
				if (MatchSymbol("<"))
				{
					ParentTypeName += "<";
					bool bFirstArg = true;
					do
					{
						++TypeNameParseDepth;
						const std::string ArgType = ParseTemplateArgumentName();
						--TypeNameParseDepth;
						if (bHasError || ArgType.empty())
						{
							ClearTypeParams();
							return false;
						}
						if (!bFirstArg)
						{
							ParentTypeName += ",";
						}
						ParentTypeName += ArgType;
						bFirstArg = false;
					}
					while (MatchSymbol(","));
					if (!MatchCloseGeneric())
					{
						ClearTypeParams();
						ReportErrorAt(Peek(), "expected '>' after parent interface type argument");
						return false;
					}
					ParentTypeName += ">";
					if (PendingGenericCloseCount > 0)
					{
						ClearTypeParams();
						ReportErrorAt(Previous(), "unexpected '>' after type name");
						return false;
					}
				}
				for (const std::string& Existing : InterfaceDecl.BaseInterfaceNames)
				{
					if (Existing == ParentTypeName)
					{
						ClearTypeParams();
						ReportErrorAt(Previous(), "duplicate parent interface: " + ParentTypeName);
						return false;
					}
				}
				InterfaceDecl.BaseInterfaceNames.push_back(std::move(ParentTypeName));
			}
			while (MatchSymbol(","));
		}

		if (!ParseOptionalWhereBounds(InterfaceDecl.TypeParams, InterfaceDecl.Bounds, "type"))
		{
			ClearTypeParams();
			return false;
		}

		if (!MatchSymbol("{"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '{' after interface name");
			return false;
		}

		InterfaceDecl.Scope.Kind = "interface";
		EnterTypeDeclaration(InterfaceDecl.Name, bExported, InterfaceDecl.TypeParams, InterfaceDecl.Scope, InterfaceDecl.TemplateParameters);
		FTypeDeclarationGuard TypeScope(*this);
		TypeFrames.back().OpenInterface = &InterfaceDecl;
		EMemberAccess CurrentMemberAccess = DefaultMemberAccess;
		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			const std::size_t Checkpoint = Current;
			if (TryParseAccessSpecifier(CurrentMemberAccess))
			{
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "friend")
			{
				if (!ParseFriendDecl(InterfaceDecl.Friends))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			FDeclMeta MemberMeta;
			if (LooksLikeAnnotationStart())
			{
				const std::size_t AnnotCheckpoint = Current;
				const int AnnotPendingCheckpoint = PendingGenericCloseCount;
				TryParseAnnotations(MemberMeta);
				const bool bKeepAnnot =
				    IsScopedTypeDeclaration() || Peek().Lexeme == "template" ||
				    (Check(ETokenKind::Keyword) &&
				        (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private")) ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "static") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "explicit") ||
				    (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" ||
				    CheckTypeName();
				if (!bKeepAnnot)
				{
					Current = AnnotCheckpoint;
					PendingGenericCloseCount = AnnotPendingCheckpoint;
					MemberMeta.clear();
				}
			}
			EMemberAccess MemberAccess = CurrentMemberAccess;
			const bool bHasInlineMemberAccess =
			    TryParseInlineMemberAccess(MemberAccess);
			if (bHasInlineMemberAccess && Check(ETokenKind::Keyword) && (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private"))
			{
				ClearTypeParams();
				ReportErrorId(Peek(), "diag.duplicate_member_access_modifier");
				return false;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "virtual")
			{
				if (!ParseInterfaceMethodDecl(
				        InterfaceDecl.Methods, MemberAccess, std::move(MemberMeta)))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
			{
				if (!ParseTemplateMember(InterfaceDecl.Methods, MemberAccess, std::move(MemberMeta), true))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			if (IsScopedTypeDeclaration())
			{
				if (!ParseScopedTypeDeclaration(MemberAccess, std::move(MemberMeta), false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}

			if ((Check(ETokenKind::Keyword) && Peek().Lexeme == "static") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || Peek().Lexeme == "mutable" || CheckTypeName() || LooksLikeGenericFunctionDecl())
			{
				if (LooksLikeMethodDecl())
				{
					if (!ParseInterfaceMethodDecl(
					        InterfaceDecl.Methods, MemberAccess, std::move(MemberMeta)))
					{
						ClearTypeParams();
						return false;
					}
					continue;
				}
				if (!ParseFieldDecl(
				        InterfaceDecl.Fields,
				        MemberAccess,
				        std::move(MemberMeta),
				        /*bAllowMutable=*/false))
				{
					ClearTypeParams();
					return false;
				}
				continue;
			}
			Current = Checkpoint;
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected field or method in interface");
			return false;
		}

		if (!MatchSymbol("}"))
		{
			ClearTypeParams();
			ReportErrorAt(Peek(), "expected '}' after interface body");
			return false;
		}

		ClearTypeParams();

		InterfaceDecl.Scope.OwnTypeParameterCount = static_cast<std::uint32_t>(InterfaceDecl.TypeParams.size());
		if (!InterfaceDecl.Scope.EnclosingTypeParameters.empty())
		{
			InterfaceDecl.TypeParams.insert(InterfaceDecl.TypeParams.begin(),
			    InterfaceDecl.Scope.EnclosingTypeParameters.begin(),
			    InterfaceDecl.Scope.EnclosingTypeParameters.end());
			InterfaceDecl.TypeParamDefaults.insert(InterfaceDecl.TypeParamDefaults.begin(),
			    InterfaceDecl.Scope.EnclosingTypeParameters.size(),
			    std::string{});
		}
		LeaveTypeDeclaration();

		Program->Interfaces.push_back(std::move(InterfaceDecl));
		// Keep type-definition spelling consistent with class/struct/enum: the
		// C++-style trailing semicolon is recommended but remains optional.
		MatchSymbol(";");
		return true;
	}

	bool VeroScriptParser::ParseEnumDecl(const bool bExported, FDeclMeta&& DeclMeta)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "enum")
		{
			return false;
		}

		// Optional `class` → scoped enum (`enum class Name { ... }`).
		bool bScoped = false;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "class")
		{
			Advance();
			bScoped = true;
		}

		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected enum name");
			return false;
		}

		FEnumDecl EnumDecl;
		EnumDecl.Name = QualifyTypeDeclName(std::string(Previous().Lexeme));
		EnumDecl.ModulePath.assign(FileLabel);
		EnumDecl.bExported = bExported;
		EnumDecl.bScoped = bScoped;
		EnumDecl.DeclMeta = std::move(DeclMeta);
		EnumDecl.Location = Location;
		EnumDecl.Scope = MakeTypeScope(EnumDecl.Name, 0);
		EnumDecl.DeclarationContext = CaptureDeclarationContext();
		EnumDecl.Scope.Kind = "enum";
		EnumDecl.Scope.bExported = bExported;

		if (MatchSymbol(":"))
		{
			if (!Match(ETokenKind::Keyword) && !Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected enum underlying type after ':'");
				return false;
			}
			EnumDecl.UnderlyingTypeName.assign(
			    CanonicalizePrimitiveTypeKeyword(Previous().Lexeme));
			if (EnumDecl.UnderlyingTypeName == "unsigned")
			{
				if (!Match(ETokenKind::Keyword) || (Previous().Lexeme != "char" && Previous().Lexeme != "short" && Previous().Lexeme != "int"))
				{
					ReportErrorAt(
					    Peek(), "expected 'char', 'short', or 'int' after 'unsigned'");
					return false;
				}
				EnumDecl.UnderlyingTypeName = "unsigned " + std::string(Previous().Lexeme);
			}
			if (EnumDecl.UnderlyingTypeName != "char" && EnumDecl.UnderlyingTypeName != "short" && EnumDecl.UnderlyingTypeName != "int" && EnumDecl.UnderlyingTypeName != "unsigned char" && EnumDecl.UnderlyingTypeName != "unsigned short" && EnumDecl.UnderlyingTypeName != "unsigned int" && EnumDecl.UnderlyingTypeName != "long" && EnumDecl.UnderlyingTypeName != "int64")
			{
				ReportErrorAt(
				    Previous(),
				    "enum underlying type must be int8, uint8, int16, uint16, "
				    "int32, uint32, int64, or an equivalent integral type");
				return false;
			}
		}

		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' after enum name");
			return false;
		}

		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			// Leading member annotations (`UMETA(...) Red,`). Unambiguous: a bare
			// member name is never followed by '(' in enum bodies.
			FDeclMeta MemberMeta;
			TryParseAnnotations(MemberMeta);
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected enum member name");
				return false;
			}

			FEnumMemberDecl Member;
			Member.Name.assign(Previous().Lexeme);
			Member.DeclMeta = std::move(MemberMeta);
			Member.Location = Previous().Location;

			Member.bHasExplicitValue = MatchSymbol("=");
			if (Member.bHasExplicitValue)
			{
				const auto Begin = Current;
				Member.Initializer = ParseExpression();
				if (!Member.Initializer) return false;
				for (auto I = Begin; I < Current; ++I)
				{
					if (!Member.InitializerSource.empty())
					{
						Member.InitializerSource += " ";
					}
					Member.InitializerSource.append(Tokens[I].Lexeme);
				}
			}
			else if (EnumDecl.Members.empty())
			{
				Member.Initializer = std::make_shared<FIntegerLiteralExpr>(0);
			}
			else
			{
				auto Previous = std::make_unique<FIdentifierExpr>(EnumDecl.Members.back().Name);
				auto Cast = std::make_unique<FCallExpr>("cast", std::vector<std::unique_ptr<FExpr>>{});
				Cast->TypeArgs.push_back(EnumDecl.UnderlyingTypeName);
				Cast->Arguments.push_back(std::move(Previous));
				auto One = std::make_unique<FCallExpr>("cast", std::vector<std::unique_ptr<FExpr>>{});
				One->TypeArgs.push_back(EnumDecl.UnderlyingTypeName);
				One->Arguments.push_back(std::make_unique<FIntegerLiteralExpr>(1));
				Member.Initializer = std::make_shared<FBinaryExpr>(EBinaryOp::Add, std::move(Cast), std::move(One));
			}
			Member.Initializer->Location = Member.Location;
			// Preserve immediately known literal values for outline consumers. Bound
			// expressions are completed by the common compiler constant service.
			FTemplateEvaluationContext EnumContext;
			EnumContext.ResolveName = [&](std::string_view Name, const FSourceLocation&)
			{
				FTemplateConstantSymbol Symbol;
				for (const auto& PreviousMember : EnumDecl.Members)
				{
					if (Name == PreviousMember.Name || Name == EnumDecl.Name + "." + PreviousMember.Name)
					{
						Symbol.Status = ETemplateEvaluationStatus::Constant;
						Symbol.Identity = EnumDecl.Name + "." + PreviousMember.Name;
						Symbol.Expression = PreviousMember.Initializer.get();
						Symbol.DeclaredTypeName = EnumDecl.UnderlyingTypeName;
						break;
					}
				}
				return Symbol;
			};
			const auto Immediate = EvaluateTemplateConstant(*Member.Initializer, EnumContext);
			if (Immediate.Succeeded())
			{
				FTemplateValueType Type;
				TryGetTemplateValueType(EnumDecl.UnderlyingTypeName, Type);
				const auto Value = ConvertTemplateConstant(Immediate.Value, Type, true, Member.Location);
				if (!Value.Succeeded() && FunctionOwner.empty() && LocalDeclarationPrefix.empty())
				{
					ReportErrorAt(Previous(), "enum member value is outside underlying type range");
					return false;
				}
				auto Bits = Value.Value.Bits;
				if (!Type.bUnsigned && Type.Width < 64 && (Bits & (std::uint64_t{1} << (Type.Width - 1))))
				{
					Bits |= ~((std::uint64_t{1} << Type.Width) - 1);
				}
				Member.Value = DecodeConstantSignedBits(Bits);
			}

			EnumDecl.Members.push_back(std::move(Member));

			if (MatchSymbol(","))
			{
				continue;
			}
			break;
		}

		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}' after enum body");
			return false;
		}

		MatchSymbol(";");

		Program->Enums.push_back(std::move(EnumDecl));
		return true;
	}

	bool VeroScriptParser::ParseImportDecl(const bool bReExport)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "import")
		{
			return false;
		}

		if (!Match(ETokenKind::StringLiteral))
		{
			ReportErrorAt(Peek(), "expected string literal in import");
			return false;
		}

		std::string Path;
		std::string ParseError;
		std::uint32_t ErrorOffset = 0;
		if (!ParserDetail::DecodeModuleImportPath(Previous().Lexeme, Path, ParseError,
		        &ErrorOffset))
		{
			FToken ErrorToken = Previous();
			ErrorToken.Location.Column += ErrorOffset;
			ReportErrorAt(ErrorToken, ParseError);
			return false;
		}

		// Trailing ';' is optional (still accepted for existing style).
		MatchSymbol(";");

		FImportDecl ImportDecl;
		ImportDecl.Path = std::move(Path);
		ImportDecl.ImporterModulePath = std::string(FileLabel);
		ImportDecl.bReExport = bReExport;
		ImportDecl.Location = Location;
		Program->Imports.push_back(std::move(ImportDecl));
		return true;
	}

	bool VeroScriptParser::ParseUsingDecl(const bool bExported)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || (Previous().Lexeme != "using" && Previous().Lexeme != "typedef"))
		{
			return false;
		}
		const char* const KeywordNoun = Previous().Lexeme == "typedef" ? "typedef" : "using";

		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), std::string("expected type alias name after ") + KeywordNoun);
			return false;
		}
		const std::string AliasName = QualifyTypeDeclName(std::string(Previous().Lexeme));

		if (!MatchSymbol("="))
		{
			ReportErrorAt(Peek(), std::string("expected '=' in ") + KeywordNoun + " declaration");
			return false;
		}

		const std::string TargetTypeName = ParseTypeName();
		if (bHasError || TargetTypeName.empty())
		{
			ReportErrorAt(Peek(), std::string("expected type after '=' in ") + KeywordNoun + " declaration");
			return false;
		}

		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), std::string("expected ';' after ") + KeywordNoun + " declaration");
			return false;
		}

		FTypeAliasDecl AliasDecl;
		AliasDecl.Name = AliasName;
		AliasDecl.TypeName = TargetTypeName;
		AliasDecl.ModulePath = std::string(FileLabel);
		AliasDecl.bExported = bExported;
		AliasDecl.Location = Location;
		AliasDecl.Scope = MakeTypeScope(AliasName, 0);
		AliasDecl.DeclarationContext = CaptureDeclarationContext();
		AliasDecl.Scope.Kind = "alias";
		AliasDecl.Scope.bExported = bExported;
		Program->TypeAliases.push_back(std::move(AliasDecl));
		ForwardTypeNames.insert(AliasName);
		return true;
	}

	bool VeroScriptParser::ParseUsingNamespaceDecl()
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "using" || !Match(ETokenKind::Keyword) || Previous().Lexeme != "namespace")
		{
			return false;
		}
		const bool bGlobalQualified = MatchSymbol("::");
		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected namespace name after 'using namespace'");
			return false;
		}
		std::string NamespaceName = bGlobalQualified
		    ? "::" + std::string(Previous().Lexeme)
		    : std::string(Previous().Lexeme);
		while (MatchSymbol("::") || MatchSymbol("."))
		{
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected identifier in using namespace directive");
				return false;
			}
			NamespaceName += '.';
			NamespaceName += Previous().Lexeme;
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after using namespace directive");
			return false;
		}

		FUsingNamespaceDecl Directive;
		Directive.NamespaceName = std::move(NamespaceName);
		Directive.ScopeNamespace = ActiveNamespacePrefix;
		Directive.ModulePath.assign(FileLabel);
		Directive.Location = Location;
		Program->UsingNamespaces.push_back(std::move(Directive));
		return true;
	}

	bool VeroScriptParser::ParseNamespaceDecl()
	{
		const FSourceLocation Location = Peek().Location;
		struct FNamespaceDepthGuard
		{
			~FNamespaceDepthGuard()
			{
				--Depth;
			}
			int& Depth;
		} DepthGuard{NamespaceParseDepth};
		if (++NamespaceParseDepth > 128)
		{
			ReportErrorAt(Location, "namespace nested too deeply");
			return false;
		}
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "namespace")
		{
			return false;
		}
		// Anonymous namespaces are accepted as a transparent declaration group.
		// VeroScript has no translation-unit linkage model, so they deliberately do
		// not create a synthetic namespace or change symbol visibility.
		const bool bAnonymous = CheckSymbol("{");
		std::string QualifiedName = ActiveNamespacePrefix;
		if (!bAnonymous)
		{
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected namespace name or '{'");
				return false;
			}
			// C++17-style nested namespace: `namespace a::b::c { }` is sugar for
			// `namespace a { namespace b { namespace c { } } }` (stored as a.b.c).
			std::string NestedPath(Previous().Lexeme);
			while (MatchSymbol("::"))
			{
				if (!Match(ETokenKind::Identifier))
				{
					ReportErrorAt(Peek(), "expected identifier after '::' in namespace name");
					return false;
				}
				NestedPath += '.';
				NestedPath += Previous().Lexeme;
			}
			QualifiedName = QualifyDeclName(NestedPath);
			if (StandardLibrary::IsReservedDeclarationNamespace(QualifiedName))
			{
				ReportErrorAt(
				    Previous(),
				    "namespace 'std' is reserved for the VeroScript standard library");
				return false;
			}
		}
		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' after namespace declaration");
			return false;
		}
		if (!bAnonymous)
		{
			Program->Namespaces.push_back(FNamespaceDecl{
			    QualifiedName,
			    std::string(FileLabel),
			    Location});
		}

		const std::string PreviousPrefix = ActiveNamespacePrefix;
		ActiveNamespacePrefix = QualifiedName;

		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			auto PendingStorage = std::make_unique<FDeclMeta>();
			auto& PendingDeclMeta = *PendingStorage;
			bool bExported = false;
			std::vector<std::string> PrefixTypeParams;
			std::vector<std::string> PrefixTypeParamDefaults;
			FTemplateParameterList PrefixTemplateParameters;
			std::vector<FTemplateParameterList> TemplateHeaders;
			bool bHasTypeDefaultAccess = false;
			EMemberAccess TypeDefaultAccess = EMemberAccess::Private;
			bool bStaticClass = false;
			FSourceLocation TypeHeaderModifierLocation = {};
			TryParseAnnotations(PendingDeclMeta);
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "export")
			{
				Advance();
				bExported = true;
				TryParseAnnotations(PendingDeclMeta);
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
			{
				if (!ParseTemplatePrefix(PrefixTypeParams, PrefixTypeParamDefaults, &PrefixTemplateParameters))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				TryParseAnnotations(PendingDeclMeta);
				if (!bExported && Check(ETokenKind::Keyword) && Peek().Lexeme == "export")
				{
					Advance();
					bExported = true;
					TryParseAnnotations(PendingDeclMeta);
				}
			}

			if (!ParseRemainingTemplateHeaders(PrefixTemplateParameters, TemplateHeaders))
			{
				if (!RecoverAfterDeclarationError())
				{
					ActiveNamespacePrefix = PreviousPrefix;
					return false;
				}
				continue;
			}

			while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private" || Peek().Lexeme == "static"))
			{
				const FToken Modifier = Advance();
				if (TypeHeaderModifierLocation.FileLabel.empty())
				{
					TypeHeaderModifierLocation = Modifier.Location;
				}
				if (Modifier.Lexeme == "static")
				{
					if (bStaticClass)
					{
						ReportErrorId(Modifier, "diag.duplicate_static_type_modifier");
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					bStaticClass = true;
					continue;
				}
				if (bHasTypeDefaultAccess)
				{
					ReportErrorId(Modifier, "diag.duplicate_type_default_access_modifier");
					ActiveNamespacePrefix = PreviousPrefix;
					return false;
				}
				bHasTypeDefaultAccess = true;
				TypeDefaultAccess = Modifier.Lexeme == "public"
				    ? EMemberAccess::Public
				    : Modifier.Lexeme == "protected"
				    ? EMemberAccess::Protected
				    : EMemberAccess::Private;
			}
			if ((bHasTypeDefaultAccess || bStaticClass) && !(Check(ETokenKind::Keyword) && (Peek().Lexeme == "class" || Peek().Lexeme == "struct" || Peek().Lexeme == "interface")))
			{
				ReportErrorId(TypeHeaderModifierLocation, "diag.type_declaration_modifier_requires_type");
				if (!RecoverAfterDeclarationError())
				{
					ActiveNamespacePrefix = PreviousPrefix;
					return false;
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "namespace")
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on namespace declarations");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "annotations are not supported on namespace");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (bExported)
				{
					ReportErrorAt(Peek(), "namespace cannot be exported; export members instead");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseNamespaceDecl())
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "using" || Peek().Lexeme == "typedef"))
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on using/typedef declarations");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "annotations are not supported on using/typedef");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				const bool bUsingNamespace = Peek().Lexeme == "using" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Keyword && Tokens[Current + 1].Lexeme == "namespace";
				if (bUsingNamespace && bExported)
				{
					ReportErrorAt(Peek(), "using namespace directive cannot be exported");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!(bUsingNamespace ? ParseUsingNamespaceDecl() : ParseUsingDecl(bExported)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "struct")
			{
				if (bStaticClass && bHasTypeDefaultAccess && TypeDefaultAccess == EMemberAccess::Protected)
				{
					ReportErrorId(TypeHeaderModifierLocation, "diag.static_struct_cannot_have_protected_default_access");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseStructDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Public,
				        bStaticClass, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "class")
			{
				if (bStaticClass && bHasTypeDefaultAccess && TypeDefaultAccess == EMemberAccess::Protected)
				{
					ReportErrorId(TypeHeaderModifierLocation, "diag.static_class_cannot_have_protected_default_access");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseClassDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Private,
				        bStaticClass, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "interface")
			{
				if (bStaticClass)
				{
					ReportErrorId(TypeHeaderModifierLocation, "diag.static_only_supported_on_class_or_struct");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseInterfaceDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Public, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "enum")
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on enum declarations");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseEnumDecl(bExported, std::move(PendingDeclMeta)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (LooksLikeOutOfClassDefinition())
			{
				if (bExported || !PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "out-of-class definitions cannot repeat export or declaration annotations");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				if (!ParseOutOfClassDefinition(
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults), std::move(TemplateHeaders)) &&
				    !RecoverAfterDeclarationError())
				{
					ActiveNamespacePrefix = PreviousPrefix;
					return false;
				}
				continue;
			}

			if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()) || LooksLikeGenericFunctionDecl())
			{
				if (!ParseFunctionDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults), std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
				}
				continue;
			}

			if (CheckTypeName() || (Check(ETokenKind::Keyword) && Peek().Lexeme == "const"))
			{
				const bool bLeadingConst =
				    Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
				const std::size_t Checkpoint = Current;
				const int PendingCheckpoint = PendingGenericCloseCount;
				FDeclMeta FunctionMeta = PendingDeclMeta;
				const bool bLooksLikeVariable = LooksLikeTypedVarDecl() && !LooksLikeFunctionPrototype(bExported);
				if (!bLeadingConst && !bLooksLikeVariable && ParseFunctionDecl(bExported, std::move(FunctionMeta), PrefixTypeParams, PrefixTypeParamDefaults))
				{
					continue;
				}
				if (bHasError && !bLooksLikeVariable)
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				else
				{
					Current = Checkpoint;
					PendingGenericCloseCount = PendingCheckpoint;
				}
				bool bConstexpr = false;
				const bool bIsConst = MatchConstexprSpecifiers(bConstexpr);
				std::string TypeName = ParseTypeName();
				if (bHasError || !MatchName())
				{
					ReportErrorAt(Peek(), "expected declaration in namespace");
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				const FSourceLocation FirstDeclarationLocation = Previous().Location;
				const std::string Name = QualifyDeclName(std::string(Previous().Lexeme));
				const std::string DeclaredTypeName = TypeName;
				if (!ParseArraySuffix(TypeName))
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				const bool bFirstDeclaratorHasArraySuffix = TypeName != DeclaredTypeName;
				auto MakeDeclaration = [&](const std::string& DeclarationName,
				                           const FSourceLocation& DeclarationLocation) -> std::unique_ptr<FVarDeclStmt>
				{
					const auto InitializerStart = Current;
					std::unique_ptr<FExpr> Initializer =
					    ParseTypedVarInitializer(TypeName, Runtime::IsNativeArrayTypeName(TypeName));
					if (bHasError)
					{
						return nullptr;
					}
					auto VarDecl =
					    std::make_unique<FVarDeclStmt>(TypeName, DeclarationName, std::move(Initializer));
					for (auto I = InitializerStart; I < Current; ++I)
					{
						if (!VarDecl->InitializerSource.empty())
						{
							VarDecl->InitializerSource += " ";
						}
						VarDecl->InitializerSource.append(Tokens[I].Lexeme);
					}
					VarDecl->bIsConst = bIsConst;
					VarDecl->bConstexpr = bConstexpr;
					VarDecl->bExported = bExported;
					VarDecl->DeclMeta = PendingDeclMeta;
					VarDecl->ModulePath.assign(FileLabel);
					VarDecl->Location = DeclarationLocation;
					return VarDecl;
				};

				std::vector<std::unique_ptr<FVarDeclStmt>> Declarations;
				std::unique_ptr<FVarDeclStmt> FirstDeclaration =
				    MakeDeclaration(Name, FirstDeclarationLocation);
				if (!FirstDeclaration)
				{
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				Declarations.push_back(std::move(FirstDeclaration));
				if (MatchSymbol(","))
				{
					if (TypeName == "auto")
					{
						ReportErrorAt(
						    Previous(),
						    "auto declarations cannot declare multiple variables");
					}
					else if (bFirstDeclaratorHasArraySuffix)
					{
						ReportErrorAt(
						    Previous(),
						    "multi-variable declarations require a shared array type prefix");
					}
					while (!bHasError)
					{
						if (!MatchName())
						{
							ReportErrorAt(Peek(), "expected variable name after ','");
							break;
						}
						const FSourceLocation DeclarationLocation = Previous().Location;
						const std::string DeclarationName = QualifyDeclName(std::string(Previous().Lexeme));
						if (CheckSymbol("["))
						{
							ReportErrorAt(
							    Peek(),
							    "multi-variable declarations require a shared array type prefix");
							break;
						}
						std::unique_ptr<FVarDeclStmt> Declaration =
						    MakeDeclaration(DeclarationName, DeclarationLocation);
						if (!Declaration)
						{
							break;
						}
						Declarations.push_back(std::move(Declaration));
						if (!MatchSymbol(","))
						{
							break;
						}
					}
				}
				if (bHasError || !MatchSymbol(";"))
				{
					if (!bHasError)
					{
						ReportErrorAt(Peek(), "expected ';' after namespace variable declaration");
					}
					if (!RecoverAfterDeclarationError())
					{
						ActiveNamespacePrefix = PreviousPrefix;
						return false;
					}
					continue;
				}
				for (std::unique_ptr<FVarDeclStmt>& VarDecl : Declarations)
				{
					Program->TopLevelStatements.push_back(std::move(VarDecl));
				}
				continue;
			}

			ReportErrorAt(Peek(), "expected declaration in namespace");
			if (!RecoverAfterDeclarationError())
			{
				ActiveNamespacePrefix = PreviousPrefix;
				return false;
			}
		}

		if (!MatchSymbol("}"))
		{
			ActiveNamespacePrefix = PreviousPrefix;
			ReportErrorAt(Peek(), "expected '}' after namespace body");
			return false;
		}

		ActiveNamespacePrefix = PreviousPrefix;
		return true;
	}

	bool VeroScriptParser::ParseTemplatePrefix(
	    std::vector<std::string>& OutTypeParams,
	    std::vector<std::string>& OutTypeParamDefaults,
	    FTemplateParameterList* OutParameters)
	{
		OutTypeParams.clear();
		OutTypeParamDefaults.clear();
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "template")
		{
			return false;
		}
		if (!MatchSymbol("<"))
		{
			ReportErrorAt(Peek(), "expected '<' after 'template'");
			return false;
		}
		return ParseTypeParameterList(OutTypeParams, OutTypeParamDefaults, OutParameters);
	}

	bool VeroScriptParser::ParseTypeParameterList(
	    std::vector<std::string>& OutTypeParams,
	    std::vector<std::string>& OutTypeParamDefaults,
	    FTemplateParameterList* OutParameters)
	{
		OutTypeParams.clear();
		OutTypeParamDefaults.clear();
		std::vector<FTemplateParameter> Parameters;
		std::vector<std::string> Activated;
		struct FScope
		{
			~FScope()
			{
				for (const auto& Name : Names)
				{
					Types.erase(Name);
				}
			}
			std::unordered_set<std::string>& Types;
			std::vector<std::string>& Names;
		} Scope{ActiveTypeParams, Activated};
		bool SawDefault = false;
		if (CheckSymbol(">") || CheckSymbol(">>"))
		{
			ReportErrorAt(Peek(), "template parameter list cannot be empty");
			return false;
		}
		do
		{
			CheckAnalysisCancellation();
			FTemplateParameter Parameter;
			Parameter.Location = Peek().Location;
			Parameter.Namespace = ActiveNamespacePrefix;
			auto ValueTypeEnd = Current;
			const bool TypedValue = TrySkipTypeNameTokens(ValueTypeEnd) && ValueTypeEnd < Tokens.size() && Tokens[ValueTypeEnd].Kind == ETokenKind::Identifier;
			const bool Introducer = (Check(ETokenKind::Keyword) && Peek().Lexeme == "class") || (Check(ETokenKind::Identifier) && Peek().Lexeme == "typename");
			if (Introducer)
			{
				Advance();
			}
			else if (TypedValue)
			{
				Parameter.Kind = ETemplateParameterKind::Value;
				Parameter.ValueTypeName = ParseTypeName();
				if (bHasError) return false;
			}
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected template parameter name");
				return false;
			}
			Parameter.Name = std::string(Previous().Lexeme);
			Parameter.Id = std::string(FileLabel) + ":template:" + std::to_string(Parameter.Location.Line) + ":" + std::to_string(Parameter.Location.Column) + ":" + Parameter.Name;
			if (ActiveTypeParams.count(Parameter.Name) || ActiveValueParams.count(Parameter.Name) || std::any_of(Parameters.begin(), Parameters.end(), [&](const auto& Other)
			{
				return Other.Name == Parameter.Name;
			}))
			{
				ReportErrorAt(Previous(), "template parameter shadows an existing declaration");
				return false;
			}
			if (MatchSymbol("="))
			{
				SawDefault = true;
				if (Parameter.Kind == ETemplateParameterKind::Type)
				{
					++TypeNameParseDepth;
					Parameter.DefaultTypeName = ParseTypeName();
					--TypeNameParseDepth;
					Parameter.DefaultSource = Parameter.DefaultTypeName;
				}
				else
				{
					Parameter.DefaultSource = ParseTemplateValueSource();
					VeroScriptLexer Lexer(Parameter.DefaultSource, FileLabel);
					VeroScriptParser Parser(Lexer.Tokenize(), FileLabel, Program.get(), VisibleModulePaths);
					Parser.SetHostTypeNames(HostTypeNames);
					Parser.RestoreDeclarationContext(*CaptureDeclarationContext(), false);
					Parameter.DefaultValue = Parser.ParseExpressionFragment(ActiveNamespacePrefix,
					    std::vector<std::string>(ActiveTypeParams.begin(), ActiveTypeParams.end()));
					if (!Parameter.DefaultValue || Parser.HasError())
					{
						ReportErrorAt(Previous(), "invalid template value default: " + Parser.GetError().Message);
						return false;
					}
					if (auto Fragment = Parser.TakeFragmentDeclarations())
					{
						Program->ScopedTypeUses.insert(Program->ScopedTypeUses.end(),
						    Fragment->ScopedTypeUses.begin(), Fragment->ScopedTypeUses.end());
					}
				}
				if (bHasError || !Parameter.HasDefault()) return false;
			}
			else if (SawDefault)
			{
				ReportErrorAt(Previous(), "a required template parameter cannot follow a defaulted parameter");
				return false;
			}
			if (Parameter.Kind == ETemplateParameterKind::Type)
			{
				OutTypeParams.push_back(Parameter.Name);
				OutTypeParamDefaults.push_back(Parameter.DefaultTypeName);
				ActiveTypeParams.insert(Parameter.Name);
				Activated.push_back(Parameter.Name);
			}
			Parameters.push_back(std::move(Parameter));
		}
		while (MatchSymbol(","));
		if (!MatchCloseGeneric())
		{
			ReportErrorAt(Peek(), "expected '>' after template parameters");
			return false;
		}
		if (PendingGenericCloseCount > 0)
		{
			ReportErrorAt(Previous(), "unexpected '>' after type name");
			return false;
		}
		for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		{
			const auto& Parameter = Parameters[Index];
			const auto IsForbiddenName = [&](const std::string& Text)
			{
				for (std::size_t Later = Index; Later < Parameters.size(); ++Later)
				{
					if (SubstituteScopedTypeParameters(Text, {Parameters[Later].Name}, {"__template_invalid_dependency"}) != Text)
					{
						return true;
					}
				}
				return false;
			};
			if (Parameter.Kind == ETemplateParameterKind::Type && IsForbiddenName(Parameter.DefaultTypeName))
			{
				ReportErrorAt(Previous(), "a template default may reference only earlier or enclosing template parameters");
				return false;
			}
			if (!Parameter.DefaultValue)
			{
				continue;
			}
			FTemplateEvaluationContext Context;
			Context.ResolveCall = [](const FExpr&, const std::vector<FTemplateEvaluationResult>&)
			{
				FConstantFunction Function;
				Function.Status = ETemplateEvaluationStatus::Dependent;
				return Function;
			};
			Context.CheckWork = [](std::uint64_t)
			{
				CheckAnalysisCancellation();
				return true;
			};
			Context.ResolveName = [&](const std::string_view Name, const FSourceLocation&)
			{
				FTemplateConstantSymbol Symbol;
				Symbol.Status = ETemplateEvaluationStatus::Dependent;
				for (std::size_t Other = 0; Other < Parameters.size(); ++Other)
				{
					if (Name != Parameters[Other].Name)
					{
						continue;
					}
					if (Other >= Index || Parameters[Other].Kind != ETemplateParameterKind::Value)
					{
						Symbol.Status = ETemplateEvaluationStatus::Invalid;
					}
					else
					{
						TryGetTemplateValueType(Parameters[Other].ValueTypeName, Symbol.Value.Type);
					}
					break;
				}
				return Symbol;
			};
			Context.ResolveType = [&](const std::string_view Name, FTemplateValueType& Type)
			{
				if (IsForbiddenName(std::string(Name))) return ETemplateEvaluationStatus::Invalid;
				if (TryGetTemplateValueType(Name, Type)) return ETemplateEvaluationStatus::Constant;
				return ParserDetail::IsTypeKeyword(Name) ? ETemplateEvaluationStatus::Invalid : ETemplateEvaluationStatus::Dependent;
			};
			const auto Default = EvaluateTemplateConstant(*Parameter.DefaultValue, Context);
			if (!Default.Succeeded() && Default.Status != ETemplateEvaluationStatus::Dependent)
			{
				ReportErrorAt(Previous(), "invalid template default: " + Default.Reason);
				return false;
			}
			FTemplateValueType DeclaredType;
			if (Default.Succeeded() && TryGetTemplateValueType(Parameter.ValueTypeName, DeclaredType) && !ConvertTemplateConstant(Default.Value, DeclaredType).Succeeded())
			{
				ReportErrorAt(Previous(), "template default is outside its declared value type");
				return false;
			}
		}
		if (OutParameters)
		{
			OutParameters->Assign(std::move(Parameters));
		}
		return true;
	}

	bool VeroScriptParser::TryParseAccessSpecifier(EMemberAccess& OutAccess)
	{
		if (!Check(ETokenKind::Keyword))
		{
			return false;
		}
		const std::string_view Lexeme = Peek().Lexeme;
		EMemberAccess Parsed = EMemberAccess::Public;
		if (Lexeme == "public")
		{
			Parsed = EMemberAccess::Public;
		}
		else if (Lexeme == "protected")
		{
			Parsed = EMemberAccess::Protected;
		}
		else if (Lexeme == "private")
		{
			Parsed = EMemberAccess::Private;
		}
		else
		{
			return false;
		}
		if (Current + 1 >= Tokens.size() || Tokens[Current + 1].Lexeme != ":")
		{
			return false;
		}
		Advance();
		if (!MatchSymbol(":"))
		{
			return false;
		}
		OutAccess = Parsed;
		return true;
	}

	bool VeroScriptParser::TryParseInlineMemberAccess(EMemberAccess& OutAccess)
	{
		if (!Check(ETokenKind::Keyword))
		{
			return false;
		}
		const std::string_view Lexeme = Peek().Lexeme;
		EMemberAccess Parsed = EMemberAccess::Public;
		if (Lexeme == "public")
		{
			Parsed = EMemberAccess::Public;
		}
		else if (Lexeme == "protected")
		{
			Parsed = EMemberAccess::Protected;
		}
		else if (Lexeme == "private")
		{
			Parsed = EMemberAccess::Private;
		}
		else
		{
			return false;
		}
		if (Current + 1 < Tokens.size() && Tokens[Current + 1].Lexeme == ":")
		{
			return false;
		}
		Advance();
		OutAccess = Parsed;
		return true;
	}

	bool VeroScriptParser::TryParseInheritanceAccess(EMemberAccess& OutAccess)
	{
		if (!Check(ETokenKind::Keyword))
		{
			return false;
		}
		const std::string_view Lexeme = Peek().Lexeme;
		EMemberAccess Parsed = EMemberAccess::Public;
		if (Lexeme == "public")
		{
			Parsed = EMemberAccess::Public;
		}
		else if (Lexeme == "protected")
		{
			Parsed = EMemberAccess::Protected;
		}
		else if (Lexeme == "private")
		{
			Parsed = EMemberAccess::Private;
		}
		else
		{
			return false;
		}
		if (Current + 1 >= Tokens.size() || Tokens[Current + 1].Kind != ETokenKind::Identifier)
		{
			return false;
		}
		Advance();
		OutAccess = Parsed;
		return true;
	}

	bool VeroScriptParser::LooksLikeUsingMemberDecl() const
	{
		if (!Check(ETokenKind::Keyword) || Peek().Lexeme != "using")
		{
			return false;
		}
		if (Current + 1 >= Tokens.size() || Tokens[Current + 1].Kind != ETokenKind::Identifier)
		{
			return false;
		}
		for (std::size_t Index = Current + 2; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "=")
			{
				return false;
			}
			// `.` / `::` both mark a qualified using-member (not a type alias).
			if (Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "::" || Token.Lexeme == "."))
			{
				return true;
			}
			if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ";")
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptParser::ParseUsingMemberDecl(
	    const EMemberAccess SectionAccess,
	    std::vector<FUsingMemberDecl>& OutUsingMembers)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "using")
		{
			return false;
		}
		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), "expected base or member name after using");
			return false;
		}
		// `.` and `::` are equivalent separators. Last identifier is the member;
		// preceding identifiers (normalized to `.`) form the optional base type:
		// `using Base::Get`, `using Base.Get`, `using N::M::Base::Get`.
		std::string Path(Previous().Lexeme);
		while (MatchMemberAccessOp())
		{
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected identifier after '.' / '::' in using declaration");
				return false;
			}
			Path += '.';
			Path += Previous().Lexeme;
		}
		std::string MemberName;
		std::string BaseTypeName;
		const std::size_t LastDot = Path.rfind('.');
		if (LastDot == std::string::npos)
		{
			MemberName = std::move(Path);
		}
		else
		{
			BaseTypeName = ResolveNamespacedTypeName(Path.substr(0, LastDot));
			MemberName = Path.substr(LastDot + 1);
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after using member declaration");
			return false;
		}
		FUsingMemberDecl UsingDecl;
		UsingDecl.BaseTypeName = std::move(BaseTypeName);
		UsingDecl.MemberName = std::move(MemberName);
		UsingDecl.Access = SectionAccess;
		UsingDecl.Location = Location;
		OutUsingMembers.push_back(std::move(UsingDecl));
		return true;
	}

	bool VeroScriptParser::ParseFriendDecl(std::vector<FFriendDecl>& OutFriends)
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || Previous().Lexeme != "friend")
		{
			return false;
		}

		FFriendDecl Friend;
		Friend.Location = Location;
		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "class" || Peek().Lexeme == "struct" || Peek().Lexeme == "interface"))
		{
			Advance();
			Friend.Kind = FFriendDecl::EKind::Type;
			Friend.TypeName = ParseTypeName();
			if (bHasError || Friend.TypeName.empty())
			{
				return false;
			}
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after friend type declaration");
				return false;
			}
			OutFriends.push_back(std::move(Friend));
			return true;
		}

		Friend.Kind = FFriendDecl::EKind::Function;
		Friend.ReturnType = ParseReturnTypeName();
		if (bHasError || Friend.ReturnType.empty())
		{
			return false;
		}
		if (Friend.ReturnType == "auto")
		{
			ReportErrorAt(Previous(), "friend declaration type cannot be 'auto'");
			return false;
		}
		if (MatchSymbol(";"))
		{
			// C++11-style simple-type friend declaration (`friend Inspector;`).
			Friend.Kind = FFriendDecl::EKind::Type;
			Friend.TypeName = std::move(Friend.ReturnType);
			OutFriends.push_back(std::move(Friend));
			return true;
		}

		const auto ParseNameComponent = [&]() -> std::string
		{
			std::string Component;
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "operator")
			{
				if (!MatchOperatorMethodName(Component))
				{
					return {};
				}
				return Component;
			}
			if (!MatchName())
			{
				return {};
			}
			Component.assign(Previous().Lexeme);
			if (!CheckSymbol("<"))
			{
				return Component;
			}
			Advance();
			Component.push_back('<');
			bool bFirst = true;
			do
			{
				++TypeNameParseDepth;
				const std::string TypeArg = ParseTypeName();
				--TypeNameParseDepth;
				if (bHasError || TypeArg.empty())
				{
					return {};
				}
				if (!bFirst)
				{
					Component.push_back(',');
				}
				Component += TypeArg;
				bFirst = false;
			}
			while (MatchSymbol(","));
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' in friend function qualifier");
				return {};
			}
			if (PendingGenericCloseCount > 0)
			{
				ReportErrorAt(Previous(), "unexpected '>' after type name");
				return {};
			}
			Component.push_back('>');
			return Component;
		};

		Friend.FunctionName = ParseNameComponent();
		if (bHasError || Friend.FunctionName.empty())
		{
			if (!bHasError)
			{
				ReportErrorAt(Peek(), "expected friend function name");
			}
			return false;
		}
		while (MatchMemberAccessOp())
		{
			const std::string Component = ParseNameComponent();
			if (bHasError || Component.empty())
			{
				if (!bHasError)
				{
					ReportErrorAt(Peek(), "expected name after friend function qualifier");
				}
				return false;
			}
			Friend.FunctionName += '.';
			Friend.FunctionName += Component;
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after friend function name");
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
				if (bHasError || Param.TypeName.empty() || !ParseParamArraySuffix(Param.TypeName) || !MatchOptionalTrailingRefAmpersand(Param.bIsRef) || !ParseOptionalParamVariadic(Param, true))
				{
					return false;
				}
				if (MatchName())
				{
					Param.Name.assign(Previous().Lexeme);
					if (!ParseArraySuffix(Param.TypeName))
					{
						return false;
					}
				}
				else
				{
					Param.Name = "$friend_arg" + std::to_string(Friend.Parameters.size());
				}
				if (CheckSymbol("="))
				{
					ReportErrorAt(Peek(), "friend declarations cannot specify parameter defaults");
					return false;
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Friend.Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after friend function parameters");
			return false;
		}
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "const")
		{
			Friend.bIsConstMethod = true;
			Advance();
		}
		if (!ParserDetail::HasValidVariadicPlacement(Friend.Parameters))
		{
			ReportErrorAt(Previous(), "variadic parameter must be the last parameter");
			return false;
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after friend function declaration");
			return false;
		}
		OutFriends.push_back(std::move(Friend));
		return true;
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseLocalTypeAliasStmt()
	{
		const FSourceLocation Location = Peek().Location;
		if (!Match(ETokenKind::Keyword) || (Previous().Lexeme != "using" && Previous().Lexeme != "typedef"))
		{
			return nullptr;
		}
		const char* const KeywordNoun = Previous().Lexeme == "typedef" ? "typedef" : "using";
		if (!Match(ETokenKind::Identifier))
		{
			ReportErrorAt(Peek(), std::string("expected type alias name after ") + KeywordNoun);
			return nullptr;
		}
		const std::string AliasName(Previous().Lexeme);
		if (ActiveTypeParams.count(AliasName) != 0 || ActiveValueParams.count(AliasName) != 0)
		{
			ReportErrorAt(Previous(), "type declaration shadows an active generic parameter");
			return nullptr;
		}
		if (!MatchSymbol("="))
		{
			ReportErrorAt(Peek(), std::string("expected '=' in ") + KeywordNoun + " declaration");
			return nullptr;
		}
		const std::string TargetTypeName = ParseTypeName();
		if (bHasError || TargetTypeName.empty())
		{
			ReportErrorAt(Peek(), std::string("expected type after '=' in ") + KeywordNoun + " declaration");
			return nullptr;
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), std::string("expected ';' after ") + KeywordNoun + " declaration");
			return nullptr;
		}
		if (!LocalTypeAliasScopes.empty())
		{
			if (!AddAliasBinding(AliasName, TargetTypeName))
			{
				ReportErrorAt(Peek(), "duplicate type alias in this scope: " + AliasName);
				return nullptr;
			}
		}
		FTypeAliasDecl Declaration;
		Declaration.DeclarationContext = CaptureDeclarationContext();
		const auto Prefix =
		    MakeLocalTypePrefix(BlockScopes.empty() ? FragmentScopeId : BlockScopes.back(), FileLabel);
		const auto PreviousPrefix = std::move(LocalDeclarationPrefix);
		LocalDeclarationPrefix = Prefix;
		Declaration.Name = Prefix + '.' + AliasName;
		Declaration.Scope = MakeTypeScope(Declaration.Name, 0);
		Declaration.Scope.Id += "/arity:0";
		Declaration.Scope.Kind = "alias";
		LocalDeclarationPrefix = PreviousPrefix;
		Declaration.TypeName = TargetTypeName;
		Declaration.ModulePath = std::string(FileLabel);
		Declaration.Location = Location;
		Program->TypeAliases.push_back(std::move(Declaration));
		auto Statement = std::make_unique<FTypeAliasStmt>(AliasName, TargetTypeName);
		Statement->Location = Location;
		return Statement;
	}
}
