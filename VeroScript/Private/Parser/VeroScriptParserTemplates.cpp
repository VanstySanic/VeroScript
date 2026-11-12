// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptLexer.h"
#include "VeroScriptStandardLibrary.h"

namespace VeroScript
{

	namespace
	{
		bool HasTemplateCallClose(const std::vector<FToken> &Tokens, std::size_t Opening)
		{
			std::size_t Angles = 1, Parentheses = 0, Brackets = 0;
			for (std::size_t I = Opening + 1; I + 1 < Tokens.size(); ++I)
			{
				if ((I & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto Part = Tokens[I].Lexeme;
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
					if (Part == "<")
					{
						if (++Angles > 256) return false;
					}
					else if (Part == ">" || Part == ">>")
					{
						if (Angles < Part.size()) return false;
						Angles -= Part.size();
						if (!Angles) return Tokens[I + 1].Lexeme == "(";
					}
					else if (Part == ";" || Part == "{" || Part == "}")
					{
						return false;
					}
				}
			}
			return false;
		}
	}

	bool VeroScriptParser::FindFunctionTemplateHeader(std::size_t &Opening) const
	{
		std::size_t ArrayDepth = 0;
		for (std::size_t Index = Current; Index < Tokens.size(); ++Index)
		{
			if ((Index & 255u) == 0)
			{
				CheckAnalysisCancellation();
			}
			const auto Token = Tokens[Index].Lexeme;
			if (Token == ";" || Token == "{" || Token == "=" || Tokens[Index].Kind == ETokenKind::EndOfFile)
			{
				return false;
			}
			// Array extent expressions may contain casts and calls before a name.
			// Their template applications cannot introduce the callable's parameters.
			if (Token == "[")
			{
				++ArrayDepth;
				continue;
			}
			if (Token == "]" && ArrayDepth != 0)
			{
				--ArrayDepth;
				continue;
			}
			if (ArrayDepth != 0)
			{
				continue;
			}
			if (Token == "(")
			{
				// Callable return types contain parentheses before the declared name.
				if (Index == Current || (Tokens[Index - 1].Lexeme != "fn" && Tokens[Index - 1].Lexeme != "function"))
				{
					return false;
				}
				std::size_t Depth = 1;
				while (++Index < Tokens.size() && Depth)
				{
					if (Tokens[Index].Lexeme == "(")
					{
						++Depth;
					}
					else if (Tokens[Index].Lexeme == ")")
					{
						--Depth;
					}
				}
				if (Depth)
				{
					return false;
				}
				--Index;
				continue;
			}
			if (Token != "<" || Index <= Current + 1)
			{
				continue;
			}
			std::size_t Name = Index - 1;
			if (!ParserDetail::IsNameToken(Tokens[Name]))
			{
				if (Name > Current && Tokens[Name - 1].Lexeme == "operator")
				{
					--Name;
				}
				else if (Name > Current + 1 && Tokens[Name - 2].Lexeme == "operator")
				{
					Name -= 2;
				}
				else
				{
					continue;
				}
			}
			if (Name <= Current || Tokens[Name - 1].Lexeme == "." || Tokens[Name - 1].Lexeme == "::" ||
			    (Tokens[Name - 1].Lexeme == "operator" && Tokens[Name].Lexeme != "operator"))
			{
				continue;
			}
			std::size_t Angles = 1, Parentheses = 0, Brackets = 0;
			for (std::size_t End = Index + 1; End < Tokens.size(); ++End)
			{
				if ((End & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto Part = Tokens[End].Lexeme;
				if (Part == "(")
				{
					++Parentheses;
				}
				else if (Part == ")")
				{
					if (!Parentheses)
					{
						break;
					}
					--Parentheses;
				}
				else if (Part == "[")
				{
					++Brackets;
				}
				else if (Part == "]")
				{
					if (!Brackets)
					{
						break;
					}
					--Brackets;
				}
				else if (Part == ";" || Part == "{" || Tokens[End].Kind == ETokenKind::EndOfFile)
				{
					break;
				}
				else if (!Parentheses && !Brackets)
				{
					if (Part == "<" && (Tokens[End - 1].Lexeme == "cast" || CheckTypeNameAt(End - 1)))
					{
						++Angles;
					}
					else if (Part == ">" || Part == ">>")
					{
						const auto Count = Part == ">>" ? 2u : 1u;
						if (Angles < Count)
						{
							break;
						}
						Angles -= Count;
						if (!Angles)
						{
							if (End + 1 < Tokens.size() && Tokens[End + 1].Lexeme == "(")
							{
								Opening = Index;
								return true;
							}
							Index = End;
							break;
						}
					}
				}
			}
		}
		return false;
	}

	std::string VeroScriptParser::ParseTemplateArgumentName()
	{
		if (CheckSymbol("::") && CheckTypeNameAt(Current + 1))
		{
			return ParseTypeName();
		}
		std::string Qualified;
		std::size_t End = Current;
		const bool QualifiedName = TryBuildQualifiedTypeName(Current, Qualified, End) && End > Current + 1;
		const bool QualifiedType =
		    QualifiedName &&
		    (IsUserTypeName(Qualified) || ParserDetail::IsTypeKeyword(Qualified) ||
		        ParserDetail::IsContainerTypeKeyword(Qualified) || !StandardLibrary::ResolveTypeName(Qualified).empty());
		const bool QualifiedValue = QualifiedName && !QualifiedType;
		// Distinguish Owner<Args>::constant from Owner<Args>::Nested before
		// committing to the type parser. Argument expressions remain unevaluated.
		std::string MemberPath;
		std::size_t Angles = 0;
		bool HasMember = false, SawApplication = false, MemberAfterApplication = false;
		for (std::size_t Index = Current; Index < Tokens.size(); ++Index)
		{
			if ((Index & 255u) == 0)
			{
				CheckAnalysisCancellation();
			}
			const auto Part = Tokens[Index].Lexeme;
			if (Part == "<" && Index > Current && (Angles || CheckTypeNameAt(Index - 1)))
			{
				++Angles;
				SawApplication = true;
			}
			else if ((Part == ">" || Part == ">>") && Angles)
			{
				Angles = Angles > Part.size() ? Angles - Part.size() : 0;
			}
			else if (Angles)
			{
				continue;
			}
			else if (Part == "." || Part == "::")
			{
				MemberPath += '.';
				HasMember = true;
				MemberAfterApplication = MemberAfterApplication || SawApplication;
			}
			else if (ParserDetail::IsNameToken(Tokens[Index]))
			{
				MemberPath += Part;
			}
			else
			{
				break;
			}
		}
		const bool Value = !CheckTypeName() || QualifiedValue ||
		    (HasMember && !(QualifiedType && !MemberAfterApplication) && !IsUserTypeName(MemberPath) &&
		        StandardLibrary::ResolveTypeName(MemberPath).empty()) ||
		    ActiveValueParams.count(std::string(Peek().Lexeme)) != 0;
		if (Value)
		{
			return ParseTemplateValueSource();
		}
		return ParseTypeName();
	}

	std::string VeroScriptParser::ParseTemplateValueSource()
	{
		std::string Source;
		// Both callers consume their opening delimiter before collecting source.
		// In a fixed-array extent, angles are operators until the closing bracket.
		const bool ArrayExtent = Current > 0 && Tokens[Current - 1].Lexeme == "[";
		std::size_t Parentheses = 0, Brackets = 0, CastAngles = 0;
		while (!IsAtEnd())
		{
			CheckAnalysisCancellation();
			const auto Part = Peek().Lexeme;
			if (Parentheses == 0 && Brackets == 0 && CastAngles == 0 &&
			    (Part == "," || (!ArrayExtent && (Part == ">" || Part == ">>"))))
			{
				break;
			}
			if (Part == "(")
			{
				++Parentheses;
			}
			else if (Part == ")")
			{
				if (Parentheses == 0)
				{
					break;
				}
				--Parentheses;
			}
			else if (Part == "[")
			{
				++Brackets;
			}
			else if (Part == "]")
			{
				if (Brackets == 0)
				{
					break;
				}
				--Brackets;
			}
			else if (Part == "<" && Current > 0 &&
			    (Tokens[Current - 1].Lexeme == "cast" || CheckTypeNameAt(Current - 1) ||
			        (ParserDetail::IsNameToken(Tokens[Current - 1]) && HasTemplateCallClose(Tokens, Current))))
			{
				++CastAngles;
			}
			else if (Part == ">" && CastAngles != 0)
			{
				--CastAngles;
			}
			else if (Part == ">>" && CastAngles != 0 && Parentheses == 0 && Brackets == 0)
			{
				if (CastAngles == 1)
				{
					Source += " >";
					Advance();
					++PendingGenericCloseCount;
					CastAngles = 0;
					break;
				}
				CastAngles -= 2;
			}
			if (!Source.empty())
			{
				Source += ' ';
			}
			Source += Part;
			Advance();
		}
		if (Source.empty() || Parentheses != 0 || Brackets != 0 || CastAngles != 0)
		{
			ReportErrorAt(Peek(), "expected a balanced template value expression");
			return {};
		}
		return Source;
	}

	bool VeroScriptParser::ParseRemainingTemplateHeaders(const FTemplateParameterList &Prefix,
	    std::vector<FTemplateParameterList> &Headers)
	{
		if (Prefix.IsEmpty())
		{
			return true;
		}
		Headers.push_back(Prefix);
		while (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
		{
			if (Headers.size() >= 64)
			{
				ReportErrorAt(Peek(), "template header nesting limit exceeded");
				return false;
			}

			// Prefix state is restored on cancellation and allocation failure too.
			struct FRestoreEnvironment
			{
				~FRestoreEnvironment()
				{
					Types.swap(SavedTypes);
					Values.swap(SavedValues);
				}

				std::unordered_set<std::string> &Types;
				std::unordered_map<std::string, FTemplateParameter> &Values;
				std::unordered_set<std::string> SavedTypes;
				std::unordered_map<std::string, FTemplateParameter> SavedValues;
			} Restore{ActiveTypeParams, ActiveValueParams, ActiveTypeParams, ActiveValueParams};

			for (const auto &Header : Headers)
			{
				for (const auto &Parameter : Header.GetParameters())
				{
					if (Parameter.Kind == ETemplateParameterKind::Type)
					{
						ActiveTypeParams.insert(Parameter.Name);
					}
					else
					{
						ActiveValueParams.emplace(Parameter.Name, Parameter);
					}
				}
			}
			std::vector<std::string> Names, Defaults;
			FTemplateParameterList Header;
			if (!ParseTemplatePrefix(Names, Defaults, &Header))
			{
				return false;
			}
			Headers.push_back(std::move(Header));
		}
		if (Headers.size() > 1 && !LooksLikeOutOfClassDefinition())
		{
			ReportErrorAt(Peek(), "multiple template headers require a qualified method definition");
			return false;
		}
		return true;
	}

	bool VeroScriptParser::PrepareMethodTemplate(FMethodDecl &Method, FCallableTemplateHeader &Header,
	    std::vector<std::string> &Activated)
	{
		Header.bHasSuffix = FindFunctionTemplateHeader(Header.Opening);
		if (Header.bHasSuffix)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				ReportErrorAt(Tokens[Header.Opening], "cannot combine a template prefix with method-name parameters");
				return false;
			}
			const auto SavedCurrent = Current;
			const auto SavedPending = PendingGenericCloseCount;
			Current = Header.Opening + 1;
			const bool Parsed =
			    ParseTypeParameterList(Method.TypeParams, Method.TypeParamDefaults, &Method.TemplateParameters);
			Header.End = Current;
			Header.PendingCloseCount = PendingGenericCloseCount;
			Current = SavedCurrent;
			PendingGenericCloseCount = SavedPending;
			if (!Parsed)
			{
				return false;
			}
		}
		for (const auto &Parameter : Method.TemplateParameters.GetParameters())
		{
			if (Parameter.Kind == ETemplateParameterKind::Value)
			{
				if (!ActiveValueParams.emplace(Parameter.Name, Parameter).second)
				{
					ReportErrorAt(Peek(), "method template parameter shadows an enclosing parameter");
					return false;
				}
				Activated.push_back(Parameter.Name);
			}
		}
		for (const auto &Name : Method.TypeParams)
		{
			if (!ActiveTypeParams.insert(Name).second)
			{
				ReportErrorAt(Peek(), "method template parameter shadows an enclosing parameter");
				return false;
			}
			Activated.push_back(Name);
		}
		return true;
	}

	bool VeroScriptParser::ConsumeMethodTemplate(const FCallableTemplateHeader &Header)
	{
		if (!Header.bHasSuffix)
		{
			return true;
		}
		if (Current != Header.Opening)
		{
			ReportErrorAt(Peek(), "invalid method template parameter position");
			return false;
		}
		Current = Header.End;
		PendingGenericCloseCount = Header.PendingCloseCount;
		return true;
	}

	bool VeroScriptParser::ParseTemplateMember(std::vector<FMethodDecl> &Methods, const EMemberAccess Access,
	    FDeclMeta &&Meta, const bool Interface)
	{
		std::vector<std::string> Names, Defaults;
		FTemplateParameterList TemplateParameters;
		if (!ParseTemplatePrefix(Names, Defaults, &TemplateParameters))
		{
			return false;
		}
		if (!TypeFrames.empty() && LooksLikeConstructorDecl(TypeNameLeaf(TypeFrames.back().Name)))
		{
			ReportErrorAt(Peek(), "constructors cannot declare their own template parameters");
			return false;
		}
		TryParseAnnotations(Meta);
		const bool Exported = !TypeFrames.empty() && TypeFrames.back().Exported;
		if (IsScopedTypeDeclaration())
		{
			const auto SavedAccess = DeclarationAccess;
			DeclarationAccess = Access;
			bool Result = false;
			if (Peek().Lexeme == "class")
			{
				Result = ParseClassDecl(Exported, std::move(Meta), std::move(Names), std::move(Defaults),
				    EMemberAccess::Private, false, std::move(TemplateParameters));
			}
			else if (Peek().Lexeme == "struct")
			{
				Result = ParseStructDecl(Exported, std::move(Meta), std::move(Names), std::move(Defaults),
				    EMemberAccess::Public, false, std::move(TemplateParameters));
			}
			else if (Peek().Lexeme == "interface")
			{
				Result = ParseInterfaceDecl(Exported, std::move(Meta), std::move(Names), std::move(Defaults),
				    EMemberAccess::Public, std::move(TemplateParameters));
			}
			else
			{
				ReportErrorAt(Peek(), "this declaration cannot have template parameters");
			}
			DeclarationAccess = SavedAccess;
			return Result;
		}
		return Interface ? ParseInterfaceMethodDecl(Methods, Access, std::move(Meta), std::move(Names),
		                       std::move(Defaults), std::move(TemplateParameters))
		                 : ParseMethodDecl(Methods, Access, std::move(Meta), std::move(Names), std::move(Defaults),
		                       std::move(TemplateParameters));
	}

} // namespace VeroScript
