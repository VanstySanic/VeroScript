// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptAst.h"

namespace VeroScript
{
	// Ordinary parentheses and brace initializers must not reserve the much
	// larger capture/source-range frame, even with whole-program optimization.
	#if defined(_MSC_VER)
	__declspec(noinline)
	#elif defined(__GNUC__) || defined(__clang__)
	__attribute__((noinline))
	#endif
	std::unique_ptr<FExpr> VeroScriptParser::ParseCaptureLambda()
	{

		const FSourceLocation Location = Peek().Location;
		Advance();
		auto Lambda = std::make_unique<FLambdaExpr>();
		if (MatchSymbol("]"))
		{
			Lambda->CaptureKind = ELambdaCaptureKind::None;
		}
		else
		{
			bool bHasDefault = false;
			if (MatchSymbol("="))
			{
				Lambda->CaptureKind = ELambdaCaptureKind::Implicit;
				bHasDefault = true;
			}
			else if (CheckSymbol("&")
				&& Current + 1 < Tokens.size()
				&& Tokens[Current + 1].Kind == ETokenKind::Symbol
				&& (Tokens[Current + 1].Lexeme == "]"
					|| Tokens[Current + 1].Lexeme == ","))
			{
				Advance();
				Lambda->CaptureKind = ELambdaCaptureKind::Reference;
				bHasDefault = true;
			}
			else
			{
				Lambda->CaptureKind = ELambdaCaptureKind::Explicit;
			}

			if (bHasDefault && MatchSymbol("]"))
			{
				// Default-only capture list.
			}
			else
			{
				if (bHasDefault && !MatchSymbol(","))
				{
					ReportErrorAt(Peek(), "expected ',' or ']' after lambda capture default");
					return nullptr;
				}
				do
				{
					const auto CaptureBegin = Current;
					const bool bByReference = MatchSymbol("&");
					const auto NameBegin = Current;
					if (!MatchName())
					{
						ReportErrorAt(Peek(), "expected capture name in '[' ... ']'");
						return nullptr;
					}
					FLambdaCapture Capture;
					Capture.Name.assign(Previous().Lexeme);
					Capture.Location = Previous().Location;
					Capture.bByReference = bByReference;
					Capture.NameRange = CaptureSourceRange(NameBegin, Current);
					if (MatchSymbol("="))
					{
						Capture.Initializer = ParseExpression();
						if (!Capture.Initializer) return nullptr;
					}
					Capture.SourceRange = CaptureSourceRange(CaptureBegin, Current);
					Lambda->Captures.push_back(std::move(Capture));
				}
				while (MatchSymbol(","));
				if (!MatchSymbol("]"))
				{
					ReportErrorAt(Peek(), "expected ']' after capture list");
					return nullptr;
				}
			}
		}
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after lambda capture list");
			return nullptr;
		}
		return ParseLambdaAfterOpenParen(std::move(Lambda), Location);
	}
}
