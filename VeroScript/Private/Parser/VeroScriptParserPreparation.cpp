// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptAst.h"
#include <algorithm>
#include <stdexcept>

namespace VeroScript
{
	void VeroScriptParser::SetSourceText(std::string_view Source, std::uint32_t Line, std::uint32_t Column,
	    std::size_t DocumentOffset)
	{
		SourceText = Source;
		SourceInitialLine = Line;
		SourceInitialColumn = Column;
		SourceDocumentOffset = DocumentOffset;
		SourceLineOffsets.clear();
		SourceLineOffsets.push_back(0);
		for (std::size_t I = 0; I < Source.size(); ++I)
		{
			if (Source[I] == '\n')
			{
				SourceLineOffsets.push_back(I + 1);
			}
		}
	}
	std::unique_ptr<FExpr> VeroScriptParser::ParseExpressionFragment(std::string Ns, const std::vector<std::string>& Params,
	    std::string InitializerType, bool VariableInitializer)
	{
		InitializeFragment(std::move(Ns), Params);
		auto R = InitializerType.empty()
		    ? ParseExpression()
		    : (VariableInitializer ? ParseTypedVarInitializer(InitializerType, true)
		                           : ParseTypedBraceInitializer(InitializerType));
		if (!IsAtEnd() && !HasError())
		{
			ReportErrorAt(Peek(), "unexpected tokens after expression");
		}

		return HasError() ? nullptr : std::move(R);
	}
	std::unique_ptr<FStmt> VeroScriptParser::ParseStatementFragment(std::string Ns, const std::vector<std::string>& Params)
	{
		InitializeFragment(std::move(Ns), Params);
		auto R = ParseStatement();
		if (!IsAtEnd() && !HasError())
		{
			ReportErrorAt(Peek(), "unexpected tokens after statement");
		}

		return HasError() ? nullptr : std::move(R);
	}
	std::string VeroScriptParser::ParseTypeFragment(std::string Ns, const std::vector<std::string>& Params)
	{
		InitializeFragment(std::move(Ns), Params);
		struct FCanonicalTypeGuard
		{
			bool& Flag;
			bool Previous;
			~FCanonicalTypeGuard()
			{
				Flag = Previous;
			}
		} Guard{bCanonicalTypeFragment, bCanonicalTypeFragment};
		bCanonicalTypeFragment = true;
		auto Type = ParseReturnTypeName();
		if (!IsAtEnd() && !HasError())
		{
			ReportErrorAt(Peek(), "unexpected tokens after type");
		}

		return HasError() ? std::string{} : Type;
	}
	FSourceRange VeroScriptParser::CaptureSourceRange(std::size_t B, std::size_t E) const
	{
		if (B >= E || E > Tokens.size())
		{
			throw std::runtime_error("invalid syntax token range");
		}

		auto Offset = [&](const FSourceLocation& L)
		{
			if (L.Line < SourceInitialLine) return std::size_t{0};
			const auto Line = static_cast<std::size_t>(L.Line - SourceInitialLine);
			if (Line >= SourceLineOffsets.size()) return SourceText.size();
			const auto Base = L.Line == SourceInitialLine ? SourceInitialColumn : 1u;
			return std::min(SourceText.size(), SourceLineOffsets[Line] + static_cast<std::size_t>(L.Column >= Base ? L.Column - Base : 0));
		};
		const auto Begin = Offset(Tokens[B].Location), End = std::min(SourceText.size(), Offset(Tokens[E - 1].Location) + Tokens[E - 1].Lexeme.size());
		FSourceRange R{std::string(Tokens[B].Location.FileLabel), SourceDocumentOffset + Begin,
		    SourceDocumentOffset + End,
		    {{std::string(Tokens[B].Location.FileLabel), Tokens[B].Location.Line, Tokens[B].Location.Column},
		        {std::string(Tokens[E - 1].Location.FileLabel), Tokens[E - 1].Location.Line,
		            Tokens[E - 1].Location.Column}}};
		for (char C : Tokens[E - 1].Lexeme)
		{
			if (C == '\n')
			{
				++R.Position.End.Line;
				R.Position.End.Column = 1;
			}
			else
			{
				++R.Position.End.Column;
			}
		}
		return R;
	}

	std::unique_ptr<FExpr> VeroScriptParser::FinishExpressionRange(std::unique_ptr<FExpr> Expression, std::size_t Begin)
	{
		if (Expression && !SourceText.empty() && Current > Begin)
		{
			const auto& First = Tokens[Begin].Location;
			const auto& Last = Tokens[Current - 1];
			const auto& Range = Expression->SourceRange;
			// A precedence layer often forwards the same leaf unchanged. Reuse its
			// owned range instead of allocating the document identity at every layer.
			if (Range.SourceId.empty() || Range.Position.Begin.Line != First.Line ||
			    Range.Position.Begin.Column != First.Column ||
			    Range.Position.End.Line != Last.Location.Line ||
			    Range.Position.End.Column != Last.Location.Column + Last.Lexeme.size())
			{
				Expression->SourceRange = CaptureSourceRange(Begin, Current);
			}
		}
		return Expression;
	}

	std::shared_ptr<const FDeferredSyntax> VeroScriptParser::CaptureDeferredSyntax(std::size_t B, std::size_t E) const
	{
		if (SourceText.empty())
		{
			throw std::runtime_error("outline parsing requires the source document");
		}
		auto F = CaptureDeclarationContext();
		F->Range = CaptureSourceRange(B, E);
		F->Source = std::string(SourceText.substr(F->Range.Begin - SourceDocumentOffset, F->Range.End - F->Range.Begin));
		return F;
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseDeferredExpression(const bool StopAtColon)
	{
		const auto Begin = Current;
		int Parens = 0, Brackets = 0, Braces = 0;
		std::size_t Conditionals = 0;
		while (!IsAtEnd())
		{
			if (StopAtColon && !Parens && !Brackets && !Braces)
			{
				if (CheckSymbol(":"))
				{
					if (Conditionals == 0)
					{
						break;
					}
					--Conditionals;
				}
				else if (CheckSymbol("?"))
				{
					++Conditionals;
				}
			}
			const bool Prefix = Current == Begin || Tokens[Current - 1].Lexeme == "=" || Tokens[Current - 1].Lexeme == "(" || Tokens[Current - 1].Lexeme == "," || Tokens[Current - 1].Lexeme == ":" || Tokens[Current - 1].Lexeme == "?" || Tokens[Current - 1].Lexeme == "{";
			bool Lambda = Prefix && Check(ETokenKind::Keyword) && (Peek().Lexeme == "fn" || Peek().Lexeme == "function");
			if (Prefix && CheckSymbol("["))
			{
				auto End = Current + 1;
				while (End < Tokens.size() && (Tokens[End].Kind == ETokenKind::Identifier || Tokens[End].Lexeme == "=" || Tokens[End].Lexeme == "&" || Tokens[End].Lexeme == ","))
				{
					++End;
				}
				Lambda = End + 1 < Tokens.size() && Tokens[End].Lexeme == "]" && Tokens[End + 1].Lexeme == "(";
			}
			if (Lambda)
			{
				if (!ParsePrimary()) return nullptr;
				continue;
			}
			// A syntactically complete generic type/callable application can contain
			// commas at expression depth zero. Its semantic identity is checked once
			// generated declarations are registered, during the normal fragment parse.
			if (Peek().Kind == ETokenKind::Identifier || Peek().Kind == ETokenKind::Keyword)
			{
				auto End = Current;
				if (TrySkipTypeNameTokens(End) && End > Current + 1 && End < Tokens.size())
				{
					bool Angles = false;
					for (auto I = Current; I < End; ++I)
					{
						if (Tokens[I].Lexeme == "<")
						{
							Angles = true;
							break;
						}
					}

					if (Angles && Tokens[End].Kind != ETokenKind::Identifier &&
					    Tokens[End].Kind != ETokenKind::IntegerLiteral)
					{
						Current = End;
						continue;
					}
				}
			}
			if (!Parens && !Brackets && !Braces &&
			    (CheckSymbol(";") || CheckSymbol(",") || CheckSymbol(")") || CheckSymbol("}")))
			{
				break;
			}

			if (CheckSymbol("("))
			{
				++Parens;
			}

			else if (CheckSymbol(")"))
			{
				if (!Parens)
				{
					break;
				}
				--Parens;
			}
			else if (CheckSymbol("["))
			{
				++Brackets;
			}

			else if (CheckSymbol("]"))
			{
				if (!Brackets)
				{
					break;
				}
				--Brackets;
			}
			else if (CheckSymbol("{"))
			{
				++Braces;
			}

			else if (CheckSymbol("}"))
			{
				if (!Braces)
				{
					break;
				}
				--Braces;
			}
			Advance();
		}
		if (Begin == Current || Parens || Brackets || Braces)
		{
			ReportErrorAt(Peek(), "invalid deferred expression");
			return nullptr;
		}
		auto R = std::make_unique<FIdentifierExpr>("");
		R->Location = Tokens[Begin].Location;
		R->DeferredSyntax = CaptureDeferredSyntax(Begin, Current);
		R->SourceRange = R->DeferredSyntax->Range;
		return R;
	}

	std::shared_ptr<FDeferredSyntax> VeroScriptParser::CaptureDeclarationContext() const
	{
		auto F = std::make_shared<FDeferredSyntax>();
		F->TypeOwner = TypeFrames.empty() ? std::string{} : TypeFrames.back().Name;
		F->TypeScope = TypeFrames.empty() ? nullptr : TypeFrames.back().Snapshot;
		F->ScopeId = BlockScopes.empty() ? FragmentScopeId : BlockScopes.back();
		F->ParentScopeId = BlockScopes.size() > 1 ? BlockScopes[BlockScopes.size() - 2] : FragmentScopeId;
		F->FunctionOwner = FunctionOwner;
		F->FunctionTypeParameters = FunctionTypeParameters;
		F->CallableTemplateParameters = CallableTemplateParameters;
		F->Bindings = CaptureBindings();
		for (const auto& Frame : TypeFrames)
		{
			F->TypeAliasBoundaries.push_back(Frame.AliasBoundary);
		}
		F->ModulePath = std::string(FileLabel);
		F->Namespace = ActiveNamespacePrefix;
		F->bAllowInternalBuiltins = bAllowInternalBuiltins;
		F->ValueParameters = ActiveValueParams;
		F->TypeParameters.assign(ActiveTypeParams.begin(), ActiveTypeParams.end());
		std::sort(F->TypeParameters.begin(), F->TypeParameters.end());
		return F;
	}
}
