// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace VeroScript
{
	std::unique_ptr<FExpr> VeroScriptParser::ParseExpression()
	{
		const auto Begin = Current;
		if (bDeclarationOutline) return ParseDeferredExpression();
		const bool bOutermostExpression = ExpressionParseDepth == 0;
		if (bOutermostExpression)
		{
			ExpressionChainNodeCount = 0;
		}
		if (!EnterExpressionNesting(Peek().Location))
		{
			return nullptr;
		}
		struct FExpressionDepthGuard
		{
			~FExpressionDepthGuard()
			{
				Parser.LeaveExpressionNesting();
			}

			VeroScriptParser& Parser;

		} DepthGuard{*this};
		auto Result = ParseAssignment();
		if (Result && !SourceText.empty() && Current > Begin)
		{
			Result->SourceRange = CaptureSourceRange(Begin, Current);
		}

		return Result;
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseAssignment()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseConditional();
		if (!Left)
		{
			return nullptr;
		}

		if (CheckSymbol("+=") || CheckSymbol("-=") || CheckSymbol("*=") || CheckSymbol("/=") || CheckSymbol("%=") || CheckSymbol("&=") || CheckSymbol("|=") || CheckSymbol("^=") || CheckSymbol("<<=") || CheckSymbol(">>="))
		{
			const std::string CompoundOp(Peek().Lexeme);
			Advance();
			if (!ParserDetail::IsValidAssignmentTarget(*Left))
			{
				ReportErrorAt(Previous(), "invalid assignment target");
				return nullptr;
			}

			// Reserve the AST node before descending: a right-associative chain
			// otherwise reaches the native stack limit before any node is counted.
			if (!ConsumeExpressionChainNode(Previous().Location))
			{
				return nullptr;
			}

			std::unique_ptr<FExpr> Right = ParseAssignment();
			if (!Right)
			{
				return nullptr;
			}
			auto Assign = std::make_unique<FAssignExpr>(
			    std::move(Left),
			    std::move(Right),
			    ParserDetail::CompoundAssignOp(CompoundOp));
			Assign->Location = Previous().Location;
			return FinishExpressionRange(std::move(Assign), Begin);
		}

		if (MatchSymbol("="))
		{
			if (!ParserDetail::IsValidAssignmentTarget(*Left))
			{
				ReportErrorAt(Previous(), "invalid assignment target");
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(Previous().Location))
			{
				return nullptr;
			}

			std::unique_ptr<FExpr> Right = ParseAssignment();
			if (!Right)
			{
				return nullptr;
			}
			auto Assign = std::make_unique<FAssignExpr>(std::move(Left), std::move(Right));
			Assign->Location = Previous().Location;
			return FinishExpressionRange(std::move(Assign), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseConditional()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Condition = ParseLogicalOr();
		if (!Condition)
		{
			return nullptr;
		}

		if (!MatchSymbol("?"))
		{
			return FinishExpressionRange(std::move(Condition), Begin);
		}

		const FSourceLocation OpLocation = Previous().Location;
		if (!ConsumeExpressionChainNode(OpLocation))
		{
			return nullptr;
		}
		std::unique_ptr<FExpr> ThenExpr = ParseExpression();
		if (!ThenExpr)
		{
			return nullptr;
		}
		if (!MatchSymbol(":"))
		{
			ReportErrorAt(Peek(), "expected ':' in conditional expression");
			return nullptr;
		}
		std::unique_ptr<FExpr> ElseExpr = ParseConditional();
		if (!ElseExpr)
		{
			return nullptr;
		}

		auto Conditional = std::make_unique<FConditionalExpr>(
		    std::move(Condition),
		    std::move(ThenExpr),
		    std::move(ElseExpr));
		Conditional->Location = OpLocation;
		return FinishExpressionRange(std::move(Conditional), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseLogicalOr()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseLogicalAnd();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("||"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseLogicalAnd();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(EBinaryOp::LogicalOr, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseLogicalAnd()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseBitOr();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("&&"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseBitOr();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(EBinaryOp::LogicalAnd, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseBitOr()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseBitXor();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("|"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseBitXor();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(EBinaryOp::BitOr, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseBitXor()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseBitAnd();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("^"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseBitAnd();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(EBinaryOp::BitXor, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseBitAnd()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseComparison();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("&"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseComparison();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(EBinaryOp::BitAnd, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseComparison()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseShift();
		if (!Left)
		{
			return nullptr;
		}

		while (true)
		{
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "in")
			{
				const FSourceLocation OpLocation = Peek().Location;
				Advance();
				std::unique_ptr<FExpr> Right = ParseShift();
				if (!Right)
				{
					return nullptr;
				}
				if (!ConsumeExpressionChainNode(OpLocation))
				{
					return nullptr;
				}
				auto Binary = std::make_unique<FBinaryExpr>(
				    EBinaryOp::In,
				    std::move(Left),
				    std::move(Right));
				Binary->Location = OpLocation;
				Left = FinishExpressionRange(std::move(Binary), Begin);
				continue;
			}

			if (!(CheckSymbol("==") || CheckSymbol("!=") || CheckSymbol("<") || CheckSymbol("<=") || CheckSymbol(">") || CheckSymbol(">=")))
			{
				break;
			}

			EBinaryOp Op = EBinaryOp::Equal;
			if (CheckSymbol("!="))
			{
				Op = EBinaryOp::NotEqual;
			}
			else if (CheckSymbol("<="))
			{
				Op = EBinaryOp::LessEqual;
			}
			else if (CheckSymbol(">="))
			{
				Op = EBinaryOp::GreaterEqual;
			}
			else if (CheckSymbol("<"))
			{
				Op = EBinaryOp::Less;
			}
			else if (CheckSymbol(">"))
			{
				Op = EBinaryOp::Greater;
			}

			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseShift();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(Op, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseShift()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseAdditive();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("<<") || CheckSymbol(">>"))
		{
			const EBinaryOp Op = CheckSymbol("<<") ? EBinaryOp::ShiftLeft : EBinaryOp::ShiftRight;
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseAdditive();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(Op, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseAdditive()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseMultiplicative();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("+") || CheckSymbol("-"))
		{
			const EBinaryOp Op = CheckSymbol("+") ? EBinaryOp::Add : EBinaryOp::Subtract;
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseMultiplicative();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(Op, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseMultiplicative()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Left = ParseUnary();
		if (!Left)
		{
			return nullptr;
		}

		while (CheckSymbol("*") || CheckSymbol("/") || CheckSymbol("%"))
		{
			EBinaryOp Op = EBinaryOp::Multiply;
			if (CheckSymbol("/"))
			{
				Op = EBinaryOp::Divide;
			}
			else if (CheckSymbol("%"))
			{
				Op = EBinaryOp::Modulo;
			}

			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Right = ParseUnary();
			if (!Right)
			{
				return nullptr;
			}

			if (!ConsumeExpressionChainNode(OpLocation))
			{
				return nullptr;
			}
			auto Binary = std::make_unique<FBinaryExpr>(Op, std::move(Left), std::move(Right));
			Binary->Location = OpLocation;
			Left = FinishExpressionRange(std::move(Binary), Begin);
		}

		return FinishExpressionRange(std::move(Left), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseUnary()
	{
		const auto Begin = Current;
		if (ExpressionParseDepth == 0 && UnaryParseDepth == 0)
		{
			ExpressionChainNodeCount = 0;
		}
		if (!EnterUnaryNesting(Peek().Location))
		{
			return nullptr;
		}
		struct FUnaryDepthGuard
		{
			~FUnaryDepthGuard()
			{
				Parser.LeaveUnaryNesting();
			}

			VeroScriptParser& Parser;

		} DepthGuard{*this};
		if (CheckSymbol("("))
		{
			std::string CastTypeName;
			const FSourceLocation CastLocation = Peek().Location;
			if (TryMatchCStyleCastType(CastTypeName))
			{
				if (CastTypeName == "void")
				{
					ReportErrorAt(CastLocation, "cannot use void value in expression");
					return nullptr;
				}

				std::unique_ptr<FExpr> Operand = ParseUnary();
				if (!Operand)
				{
					return nullptr;
				}
				std::vector<std::unique_ptr<FExpr>> Arguments;
				Arguments.push_back(std::move(Operand));
				auto Cast = std::make_unique<FCallExpr>(
				    "cast",
				    std::move(Arguments),
				    std::vector<std::string>{std::move(CastTypeName)});
				Cast->Location = CastLocation;
				return FinishExpressionRange(std::move(Cast), Begin);
			}
		}

		if (CheckSymbol("++") || CheckSymbol("--"))
		{
			const bool bDecrement = Peek().Lexeme == "--";
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Operand = ParseUnary();
			if (!Operand)
			{
				return nullptr;
			}
			if (!ParserDetail::IsValidAssignmentTarget(*Operand))
			{
				ReportErrorAt(Previous(), "invalid increment/decrement target");
				return nullptr;
			}

			auto One = std::make_unique<FIntegerLiteralExpr>(1);
			One->Location = OpLocation;
			auto Assign = std::make_unique<FAssignExpr>(
			    std::move(Operand),
			    std::move(One),
			    bDecrement ? EBinaryOp::Subtract : EBinaryOp::Add,
			    true);
			Assign->Location = OpLocation;
			return FinishExpressionRange(std::move(Assign), Begin);
		}

		if (CheckSymbol("-"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			// The magnitude of the most-negative signed value is one larger than the
			// positive maximum. Parse it together with its leading minus so INT32_MIN
			// and INT64_MIN remain expressible without admitting that magnitude as a
			// standalone positive literal.
			if (Check(ETokenKind::IntegerLiteral))
			{
				std::int32_t SignedValue = 0;
				std::uint32_t UnsignedValue = 0;
				std::int64_t SignedValue64 = 0;
				std::uint64_t UnsignedValue64 = 0;
				bool bUnsigned = false;
				bool bIs64 = false;
				bool bParsedSignedMin = false;
				std::string ParseError;
				if (ParserDetail::ParseIntegerLiteral(
				        Peek().Lexeme,
				        SignedValue,
				        UnsignedValue,
				        bUnsigned,
				        ParseError,
				        &bIs64,
				        &SignedValue64,
				        &UnsignedValue64,
				        true,
				        &bParsedSignedMin) &&
				    bParsedSignedMin && !bUnsigned)
				{
					Advance();
					auto Literal = bIs64
					    ? std::make_unique<FIntegerLiteralExpr>(SignedValue64)
					    : std::make_unique<FIntegerLiteralExpr>(SignedValue);
					Literal->Location = OpLocation;
					return FinishExpressionRange(std::move(Literal), Begin);
				}
			}
			std::unique_ptr<FExpr> Operand = ParseUnary();
			if (!Operand)
			{
				return nullptr;
			}

			auto Unary = std::make_unique<FUnaryExpr>(EUnaryOp::Negate, std::move(Operand));
			Unary->Location = OpLocation;
			return FinishExpressionRange(std::move(Unary), Begin);
		}

		if ((CheckSymbol("!") || CheckSymbol("~")) && Current + 1 < Tokens.size() &&
		    (Tokens[Current + 1].Lexeme == "!" || Tokens[Current + 1].Lexeme == "~"))
		{
			// Long prefix chains need one AST node per operator, not one native
			// stack frame. Preserve the existing combined unary depth budget.
			std::vector<std::size_t> Operators;
			struct FChainDepthGuard
			{
				~FChainDepthGuard()
				{
					while (AdditionalDepth != 0)
					{
						--AdditionalDepth;
						Parser.LeaveUnaryNesting();
					}
				}
				VeroScriptParser& Parser;
				std::size_t AdditionalDepth = 0;
			} ChainGuard{*this};
			do
			{
				CheckAnalysisCancellation();
				Operators.push_back(Current);
				Advance();
				if (!CheckSymbol("!") && !CheckSymbol("~"))
				{
					break;
				}
				if (!EnterUnaryNesting(Peek().Location)) return nullptr;
				++ChainGuard.AdditionalDepth;
			}
			while (true);
			auto Operand = ParseUnary();
			if (!Operand) return nullptr;
			for (auto Operator = Operators.rbegin(); Operator != Operators.rend(); ++Operator)
			{
				auto Unary = std::make_unique<FUnaryExpr>(Tokens[*Operator].Lexeme == "!" ? EUnaryOp::Not : EUnaryOp::BitNot,
				    std::move(Operand));
				Unary->Location = Tokens[*Operator].Location;
				Operand = FinishExpressionRange(std::move(Unary), *Operator);
			}
			return Operand;
		}

		if (CheckSymbol("!"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Operand = ParseUnary();
			if (!Operand)
			{
				return nullptr;
			}

			auto Unary = std::make_unique<FUnaryExpr>(EUnaryOp::Not, std::move(Operand));
			Unary->Location = OpLocation;
			return FinishExpressionRange(std::move(Unary), Begin);
		}

		if (CheckSymbol("~"))
		{
			const FSourceLocation OpLocation = Peek().Location;
			Advance();
			std::unique_ptr<FExpr> Operand = ParseUnary();
			if (!Operand)
			{
				return nullptr;
			}

			auto Unary = std::make_unique<FUnaryExpr>(EUnaryOp::BitNot, std::move(Operand));
			Unary->Location = OpLocation;
			return FinishExpressionRange(std::move(Unary), Begin);
		}

		return FinishExpressionRange(ParsePostfix(), Begin);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParsePrimary()
	{
		const auto Begin = Current;
		if (CheckSymbol("{"))
		{
			return FinishExpressionRange(ParseArrayInitializer({}, false), Begin);
		}
		if (MatchSymbol("::"))
		{
			const FSourceLocation Location = Previous().Location;
			if (!MatchName())
			{
				ReportErrorAt(Peek(), "expected name after global scope operator '::'");
				return nullptr;
			}
			auto Identifier = std::make_unique<FIdentifierExpr>(
			    "::" + std::string(Previous().Lexeme));
			Identifier->Location = Location;
			return FinishExpressionRange(std::move(Identifier), Begin);
		}

		if (Match(ETokenKind::IntegerLiteral))
		{
			std::int32_t SignedValue = 0;
			std::uint32_t UnsignedValue = 0;
			std::int64_t SignedValue64 = 0;
			std::uint64_t UnsignedValue64 = 0;
			bool bUnsigned = false;
			bool bIs64 = false;
			std::string ParseError;
			if (!ParserDetail::ParseIntegerLiteral(
			        Previous().Lexeme,
			        SignedValue,
			        UnsignedValue,
			        bUnsigned,
			        ParseError,
			        &bIs64,
			        &SignedValue64,
			        &UnsignedValue64))
			{
				ReportErrorAt(Previous(), ParseError);
				return nullptr;
			}

			std::unique_ptr<FIntegerLiteralExpr> Literal;
			if (bIs64 && bUnsigned)
			{
				Literal = std::make_unique<FIntegerLiteralExpr>(UnsignedValue64, true);
			}
			else if (bIs64)
			{
				Literal = std::make_unique<FIntegerLiteralExpr>(SignedValue64);
			}
			else if (bUnsigned)
			{
				Literal = std::make_unique<FIntegerLiteralExpr>(UnsignedValue, true);
			}
			else
			{
				Literal = std::make_unique<FIntegerLiteralExpr>(SignedValue);
			}
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::CharacterLiteral))
		{
			std::int8_t Value = 0;
			std::string ParseError;
			std::uint32_t ErrorOffset = 0;
			if (!ParserDetail::DecodeCharacterLiteral(
			        Previous().Lexeme, Value, ParseError, &ErrorOffset))
			{
				FToken ErrorToken = Previous();
				ErrorToken.Location.Column += ErrorOffset;
				ReportErrorAt(ErrorToken, ParseError);
				return nullptr;
			}
			auto Literal = std::make_unique<FCharacterLiteralExpr>(Value);
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::FloatLiteral))
		{
			const std::string_view Lexeme = Previous().Lexeme;
			double Value = 0.0;
			std::string ParseError;
			if (!ParserDetail::ParseFloatLiteral(Lexeme, Value, ParseError))
			{
				ReportErrorAt(Previous(), ParseError);
				return nullptr;
			}

			const bool bFloat32 = !Lexeme.empty() && (Lexeme.back() == 'f' || Lexeme.back() == 'F');
			auto Literal = std::make_unique<FFloatLiteralExpr>(Value, bFloat32);
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::StringLiteral))
		{
			std::string Value;
			std::string ParseError;
			std::uint32_t ErrorOffset = 0;
			if (!ParserDetail::DecodeStringLiteral(
			        Previous().Lexeme, Value, ParseError, &ErrorOffset))
			{
				FToken ErrorToken = Previous();
				ErrorToken.Location.Column += ErrorOffset;
				ReportErrorAt(ErrorToken, ParseError);
				return nullptr;
			}

			auto Literal = std::make_unique<FStringLiteralExpr>(std::move(Value));
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::WStringLiteral))
		{
			std::u16string Value;
			std::string ParseError;
			std::uint32_t ErrorOffset = 0;
			if (!ParserDetail::DecodeWStringLiteral(
			        Previous().Lexeme, Value, ParseError, &ErrorOffset))
			{
				FToken ErrorToken = Previous();
				ErrorToken.Location.Column += ErrorOffset;
				ReportErrorAt(ErrorToken, ParseError);
				return nullptr;
			}

			auto Literal = std::make_unique<FWStringLiteralExpr>(std::move(Value));
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::BooleanLiteral))
		{
			const bool Value = Previous().Lexeme == "true";
			auto Literal = std::make_unique<FBooleanLiteralExpr>(Value);
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Match(ETokenKind::NullptrLiteral))
		{
			auto Literal = std::make_unique<FNullptrLiteralExpr>();
			Literal->Location = Previous().Location;
			return FinishExpressionRange(std::move(Literal), Begin);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "this")
		{
			Advance();
			auto ThisExpr = std::make_unique<FThisExpr>();
			ThisExpr->Location = Previous().Location;
			return FinishExpressionRange(std::move(ThisExpr), Begin);
		}

		// Keep capture-specific objects off ordinary expression recursion frames.
		if (CheckSymbol("["))
		{
			return FinishExpressionRange(ParseCaptureLambda(), Begin);
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "fn" || Peek().Lexeme == "function"))
		{
			const FSourceLocation Location = Peek().Location;
			const std::string FunctionKeyword(Peek().Lexeme);
			Advance();
			if (!MatchSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after '" + FunctionKeyword + "'");
				return nullptr;
			}

			auto Lambda = std::make_unique<FLambdaExpr>();
			Lambda->CaptureKind = ELambdaCaptureKind::Implicit;
			return FinishExpressionRange(ParseLambdaAfterOpenParen(std::move(Lambda), Location), Begin);
		}

		if (MatchName())
		{
			const FSourceLocation Location = Previous().Location;
			std::string Name(Previous().Lexeme);
			auto Identifier = std::make_unique<FIdentifierExpr>(std::move(Name));
			Identifier->Location = Location;
			return FinishExpressionRange(std::move(Identifier), Begin);
		}

		if (MatchSymbol("("))
		{
			std::unique_ptr<FExpr> Inner = ParseExpression();
			if (!Inner)
			{
				return nullptr;
			}

			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after expression");
				return nullptr;
			}

			return FinishExpressionRange(std::move(Inner), Begin);
		}

		ReportErrorAt(Peek(), "expected expression");
		return nullptr;
	}

	std::unique_ptr<FExpr> VeroScriptParser::FinishCall(
	    std::string Callee,
	    const FSourceLocation& Location,
	    std::vector<std::string> TypeArgs,
	    const bool bStandardLibraryQualified)
	{
		if (!bAllowInternalBuiltins && !bStandardLibraryQualified && Callee.rfind("__", 0) == 0)
		{
			ReportErrorAt(
			    Location,
			    "internal builtin is reserved for standard library implementation: " + Callee);
			return nullptr;
		}
		Advance();

		std::vector<std::unique_ptr<FExpr>> Arguments;
		if (!CheckSymbol(")"))
		{
			do
			{
				std::unique_ptr<FExpr> Argument = ParseCallArgument();
				if (!Argument)
				{
					return nullptr;
				}
				Arguments.push_back(std::move(Argument));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after arguments");
			return nullptr;
		}

		auto Call = std::make_unique<FCallExpr>(
		    std::move(Callee),
		    std::move(Arguments),
		    std::move(TypeArgs));
		Call->bStandardLibraryQualified = bStandardLibraryQualified;
		Call->bImplementationBuiltinAccess = bAllowInternalBuiltins;
		Call->Location = Location;
		return Call;
	}

	std::unique_ptr<FExpr> VeroScriptParser::FinishBraceCall(
	    std::string Callee,
	    const FSourceLocation& Location)
	{
		Advance();

		std::vector<std::unique_ptr<FExpr>> Arguments;
		if (!CheckSymbol("}"))
		{
			do
			{
				std::unique_ptr<FExpr> Argument = ParseCallArgument();
				if (!Argument)
				{
					return nullptr;
				}
				Arguments.push_back(std::move(Argument));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}' after constructor arguments");
			return nullptr;
		}

		auto Call = std::make_unique<FCallExpr>(
		    std::move(Callee),
		    std::move(Arguments));
		Call->Location = Location;
		return Call;
	}

	std::unique_ptr<FExpr> VeroScriptParser::FinishCallExpr(
	    std::unique_ptr<FExpr> CalleeExpr,
	    const FSourceLocation& Location)
	{
		Advance();

		std::vector<std::unique_ptr<FExpr>> Arguments;
		if (!CheckSymbol(")"))
		{
			do
			{
				std::unique_ptr<FExpr> Argument = ParseCallArgument();
				if (!Argument)
				{
					return nullptr;
				}
				Arguments.push_back(std::move(Argument));
			}
			while (MatchSymbol(","));
		}

		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after arguments");
			return nullptr;
		}

		if (!ConsumeExpressionChainNode(Location))
		{
			return nullptr;
		}
		auto Call = std::make_unique<FCallExpr>(
		    std::string{},
		    std::move(Arguments));
		Call->CalleeExpr = std::move(CalleeExpr);
		Call->Location = Location;
		return Call;
	}
}
