// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFormat.h"

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptPath.h"
#include "VeroScriptTypes.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		bool IsClosingPunctuator(const std::string_view Lexeme)
		{
			return Lexeme == ")" || Lexeme == "]" || Lexeme == "}" || Lexeme == ";" || Lexeme == "," || Lexeme == "." || Lexeme == "::";
		}

		bool IsOpeningPunctuator(const std::string_view Lexeme)
		{
			return Lexeme == "(" || Lexeme == "[" || Lexeme == "{";
		}

		bool WantsSpaceBefore(const FToken& Token)
		{
			if (Token.Kind == ETokenKind::Symbol)
			{
				if (IsClosingPunctuator(Token.Lexeme) || Token.Lexeme == "." || Token.Lexeme == "::" || Token.Lexeme == "(" || Token.Lexeme == "[")
				{
					return false;
				}
				if (Token.Lexeme == "++" || Token.Lexeme == "--")
				{
					return false;
				}
			}
			return true;
		}

		bool WantsSpaceAfter(const FToken& Token)
		{
			if (Token.Kind == ETokenKind::Symbol)
			{
				if (IsOpeningPunctuator(Token.Lexeme) || Token.Lexeme == "." || Token.Lexeme == "::")
				{
					return false;
				}
				if (Token.Lexeme == "++" || Token.Lexeme == "--" || Token.Lexeme == "!")
				{
					return false;
				}
				// Keep magnitude tokens glued: `-1` / `+2` (binary still gets a leading space).
				if (Token.Lexeme == "-" || Token.Lexeme == "+" || Token.Lexeme == "~")
				{
					return false;
				}
				if (Token.Lexeme == ";")
				{
					return false;
				}
			}
			return true;
		}

		bool IsControlKeyword(const FToken& Token)
		{
			if (Token.Kind != ETokenKind::Keyword)
			{
				return false;
			}
			return Token.Lexeme == "if" || Token.Lexeme == "while" || Token.Lexeme == "for" || Token.Lexeme == "switch" || Token.Lexeme == "catch";
		}

		// `public:` / `protected:` / `private:` access labels (not inheritance `public Base`).
		bool IsAccessSpecifierKeyword(const FToken& Token)
		{
			if (Token.Kind != ETokenKind::Keyword)
			{
				return false;
			}
			return Token.Lexeme == "public" || Token.Lexeme == "protected" || Token.Lexeme == "private";
		}

		bool WantsSpaceBetween(const FToken& Previous, const FToken& Token)
		{
			// `print(` / `main()` — no space. `if (` / `for (` — keep a space.
			// A parenthesized expression after a comma/operator is not a call and
			// keeps the normal leading space: `print(label, (a + b))`.
			if (Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "(" || Token.Lexeme == "["))
			{
				if (IsControlKeyword(Previous))
				{
					return true;
				}
				if (Previous.Kind == ETokenKind::Identifier || Previous.Lexeme == ")" || Previous.Lexeme == "]" || Previous.Lexeme == "(" || Previous.Lexeme == "[")
				{
					return false;
				}
				if (Previous.Kind == ETokenKind::Keyword)
				{
					return Previous.Lexeme == "return" || Previous.Lexeme == "throw";
				}
				return true;
			}
			return WantsSpaceAfter(Previous) && WantsSpaceBefore(Token);
		}

		bool NeedsLexicalSeparator(const FToken& Previous, const FToken& Token)
		{
			if (Previous.Kind == ETokenKind::Symbol && Token.Kind == ETokenKind::Symbol)
			{
				// Use the language lexer, including comment openers and maximal-munch
				// operators. Removing whitespace must not turn `- -x` into `--x` or
				// change the order of the operators in `- --x`.
				const std::string Pair = std::string(Previous.Lexeme) + std::string(Token.Lexeme);
				VeroScriptLexer Lexer(Pair);
				const auto& Lexed = Lexer.Tokenize();
				return Lexer.HasError() || Lexed.size() != 3 || Lexed[0].Kind != Previous.Kind || Lexed[0].Lexeme != Previous.Lexeme || Lexed[1].Kind != Token.Kind || Lexed[1].Lexeme != Token.Lexeme;
			}
			const auto IsNumber = [](const FToken& Part)
			{
				return Part.Kind == ETokenKind::IntegerLiteral || Part.Kind == ETokenKind::FloatLiteral;
			};
			if ((IsNumber(Previous) && Token.Lexeme == ".") || (Previous.Lexeme == "." && IsNumber(Token)))
			{
				return true;
			}
			const auto IsWord = [](const FToken& Part)
			{
				return Part.Kind == ETokenKind::Identifier || Part.Kind == ETokenKind::Keyword || Part.Kind == ETokenKind::BooleanLiteral || Part.Kind == ETokenKind::NullptrLiteral || Part.Kind == ETokenKind::IntegerLiteral || Part.Kind == ETokenKind::FloatLiteral;
			};
			return IsWord(Previous) && IsWord(Token);
		}

		bool IsUnaryPrefixOperator(
		    const std::vector<FToken>& Tokens,
		    const std::size_t OperatorIndex)
		{
			if (OperatorIndex >= Tokens.size() || Tokens[OperatorIndex].Kind != ETokenKind::Symbol)
			{
				return false;
			}
			const std::string_view Operator = Tokens[OperatorIndex].Lexeme;
			if (Operator != "+" && Operator != "-" && Operator != "&" && Operator != "!" && Operator != "~" && Operator != "++" && Operator != "--")
			{
				return false;
			}
			if (Operator == "!" || Operator == "~")
			{
				return true;
			}
			if (OperatorIndex == 0)
			{
				return true;
			}

			const FToken& Before = Tokens[OperatorIndex - 1];
			if (Before.Kind == ETokenKind::Symbol)
			{
				return Before.Lexeme == "(" || Before.Lexeme == "[" || Before.Lexeme == "{" || Before.Lexeme == "," || Before.Lexeme == ";" || Before.Lexeme == ":" || Before.Lexeme == "=" || Before.Lexeme == "=>" || Before.Lexeme == "+" || Before.Lexeme == "-" || Before.Lexeme == "*" || Before.Lexeme == "/" || Before.Lexeme == "%" || Before.Lexeme == "&" || Before.Lexeme == "|" || Before.Lexeme == "^" || Before.Lexeme == "!" || Before.Lexeme == "~" || Before.Lexeme == "<" || Before.Lexeme == ">" || Before.Lexeme == "<=" || Before.Lexeme == ">=" || Before.Lexeme == "==" || Before.Lexeme == "!=" || Before.Lexeme == "&&" || Before.Lexeme == "||" || Before.Lexeme == "+=" || Before.Lexeme == "-=" || Before.Lexeme == "*=" || Before.Lexeme == "/=" || Before.Lexeme == "%=";
			}
			return Before.Kind == ETokenKind::Keyword && (Before.Lexeme == "return" || Before.Lexeme == "case" || Before.Lexeme == "throw" || Before.Lexeme == "ref");
		}

		bool IsReferenceDeclarator(
		    const std::vector<FToken>& Tokens,
		    const std::vector<bool>& TypeArgClose,
		    const std::size_t AmpersandIndex)
		{
			if (AmpersandIndex == 0 || AmpersandIndex + 1 >= Tokens.size() || Tokens[AmpersandIndex].Lexeme != "&")
			{
				return false;
			}
			const FToken& Before = Tokens[AmpersandIndex - 1];
			const FToken& After = Tokens[AmpersandIndex + 1];
			if (After.Kind != ETokenKind::Identifier && After.Lexeme != "[")
			{
				return false;
			}
			if (Before.Kind == ETokenKind::Keyword && !IsControlKeyword(Before) && Before.Lexeme != "return" && Before.Lexeme != "case")
			{
				return true;
			}
			if (AmpersandIndex - 1 < TypeArgClose.size() && TypeArgClose[AmpersandIndex - 1])
			{
				return true;
			}
			if (Before.Kind != ETokenKind::Identifier || AmpersandIndex < 2)
			{
				return false;
			}

			const FToken& BeforeType = Tokens[AmpersandIndex - 2];
			if (BeforeType.Kind == ETokenKind::Keyword && (BeforeType.Lexeme == "const" || BeforeType.Lexeme == "constexpr"))
			{
				return true;
			}
			if (BeforeType.Kind == ETokenKind::Symbol && (BeforeType.Lexeme == ";" || BeforeType.Lexeme == "{" || BeforeType.Lexeme == "}" || BeforeType.Lexeme == ":"))
			{
				return true;
			}
			if (BeforeType.Lexeme != "(" || AmpersandIndex < 3)
			{
				return false;
			}
			if (Tokens[AmpersandIndex - 3].Lexeme == "for")
			{
				return true;
			}
			return AmpersandIndex >= 4 && Tokens[AmpersandIndex - 3].Kind == ETokenKind::Identifier && (Tokens[AmpersandIndex - 4].Kind == ETokenKind::Identifier || Tokens[AmpersandIndex - 4].Kind == ETokenKind::Keyword || TypeArgClose[AmpersandIndex - 4]);
		}

		bool IsBlockBrace(
		    const std::vector<FToken>& Tokens,
		    const std::size_t BraceIndex,
		    const std::vector<bool>& BraceBlocks)
		{
			if (BraceIndex == 0)
			{
				return true;
			}
			const FToken& Previous = Tokens[BraceIndex - 1];
			if (Previous.Kind == ETokenKind::Symbol)
			{
				if (Previous.Lexeme == ")" || Previous.Lexeme == ";")
				{
					return true;
				}
				if ((Previous.Lexeme == "{" || Previous.Lexeme == ",") && !BraceBlocks.empty())
				{
					return BraceBlocks.back();
				}
				if (Previous.Lexeme == "=")
				{
					return false;
				}
			}
			if (Previous.Kind == ETokenKind::Keyword && (Previous.Lexeme == "else" || Previous.Lexeme == "do" || Previous.Lexeme == "try" || Previous.Lexeme == "finally" || Previous.Lexeme == "default"))
			{
				return true;
			}

			// A declaration keyword before the current statement boundary makes this
			// a type/namespace body. This also covers generic declarations and bounds.
			for (std::size_t Index = BraceIndex; Index-- > 0;)
			{
				const FToken& Candidate = Tokens[Index];
				if (Candidate.Kind == ETokenKind::Symbol && (Candidate.Lexeme == ";" || Candidate.Lexeme == "{" || Candidate.Lexeme == "}"))
				{
					break;
				}
				if (Candidate.Kind == ETokenKind::Symbol && Candidate.Lexeme == ")")
				{
					return true;
				}
				if (Candidate.Kind == ETokenKind::Keyword && (Candidate.Lexeme == "class" || Candidate.Lexeme == "struct" || Candidate.Lexeme == "interface" || Candidate.Lexeme == "enum" || Candidate.Lexeme == "namespace" || Candidate.Lexeme == "switch"))
				{
					return true;
				}
				if (Candidate.Lexeme == "function" || Candidate.Lexeme == "fn" || Candidate.Lexeme == "=>")
				{
					return true;
				}
			}
			return false;
		}

		bool IsEnumBodyBrace(
		    const std::vector<FToken>& Tokens,
		    const std::size_t BraceIndex)
		{
			for (std::size_t Index = BraceIndex; Index-- > 0;)
			{
				const FToken& Candidate = Tokens[Index];
				if (Candidate.Kind == ETokenKind::Symbol && (Candidate.Lexeme == ";" || Candidate.Lexeme == "{" || Candidate.Lexeme == "}"))
				{
					return false;
				}
				if (Candidate.Kind == ETokenKind::Keyword && Candidate.Lexeme == "enum")
				{
					return true;
				}
			}
			return false;
		}

		bool IsTypeBodyBrace(
		    const std::vector<FToken>& Tokens,
		    const std::size_t BraceIndex)
		{
			for (std::size_t Index = BraceIndex; Index-- > 0;)
			{
				const FToken& Candidate = Tokens[Index];
				if (Candidate.Kind == ETokenKind::Symbol && (Candidate.Lexeme == ";" || Candidate.Lexeme == "{" || Candidate.Lexeme == "}"))
				{
					return false;
				}
				if (Candidate.Kind == ETokenKind::Keyword && (Candidate.Lexeme == "class" || Candidate.Lexeme == "struct" || Candidate.Lexeme == "interface" || Candidate.Lexeme == "enum"))
				{
					return true;
				}
			}
			return false;
		}

		bool FindSourceOffset(
		    const std::string_view Source,
		    const std::uint32_t Line,
		    const std::uint32_t Column,
		    std::size_t& OutOffset)
		{
			std::uint32_t CurrentLine = 1;
			std::uint32_t CurrentColumn = 1;
			for (std::size_t Index = 0; Index <= Source.size(); ++Index)
			{
				if (CurrentLine == Line && CurrentColumn == Column)
				{
					OutOffset = Index;
					return true;
				}
				if (Index == Source.size())
				{
					break;
				}
				if (Source[Index] == '\n')
				{
					++CurrentLine;
					CurrentColumn = 1;
				}
				else
				{
					++CurrentColumn;
				}
			}
			return false;
		}

		std::size_t LineStartOffset(
		    const std::string_view Source,
		    const std::uint32_t OneBasedLine)
		{
			if (OneBasedLine <= 1)
			{
				return 0;
			}
			std::size_t Offset = 0;
			for (std::uint32_t Line = 1; Line < OneBasedLine; ++Line)
			{
				Offset = Source.find('\n', Offset);
				if (Offset == std::string_view::npos)
				{
					return Source.size();
				}
				++Offset;
			}
			return Offset;
		}

		std::size_t LineEndOffset(
		    const std::string_view Source,
		    const std::uint32_t OneBasedLine)
		{
			const std::size_t Start = LineStartOffset(Source, OneBasedLine);
			if (Start >= Source.size())
			{
				return Source.size();
			}
			const std::size_t Newline = Source.find('\n', Start);
			return Newline == std::string_view::npos ? Source.size() : Newline + 1;
		}

		std::uint32_t CountLines(const std::string_view Source)
		{
			return static_cast<std::uint32_t>(
			    1 + std::count(Source.begin(), Source.end(), '\n'));
		}

		// Recover only the indentation context required by a selection. The full
		// formatter already makes the same block-vs-initializer distinction, so a
		// synthetic prefix of block braces lets it format a line range without
		// rewriting the surrounding document.
		int EnclosingBlockDepth(
		    const std::string_view Prefix,
		    const std::string_view FileLabel)
		{
			VeroScriptLexer Lexer(Prefix, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return 0;
			}
			std::vector<bool> BraceBlocks;
			int Depth = 0;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == "{")
				{
					const bool bBlock = IsBlockBrace(Tokens, Index, BraceBlocks);
					BraceBlocks.push_back(bBlock);
					if (bBlock)
					{
						++Depth;
					}
				}
				else if (Token.Lexeme == "}" && !BraceBlocks.empty())
				{
					const bool bBlock = BraceBlocks.back();
					BraceBlocks.pop_back();
					if (bBlock && Depth > 0)
					{
						--Depth;
					}
				}
			}
			return Depth;
		}

		std::size_t TokenStartOffset(const std::string_view Source, const FToken& Token)
		{
			if (!Token.Lexeme.empty())
			{
				const char* Base = Source.data();
				const char* Ptr = Token.Lexeme.data();
				if (Ptr >= Base && Ptr <= Base + Source.size())
				{
					return static_cast<std::size_t>(Ptr - Base);
				}
			}
			std::size_t Offset = Source.size();
			FindSourceOffset(Source, Token.Location.Line, Token.Location.Column, Offset);
			return Offset;
		}

		std::vector<bool> FindTypeArgumentListOpeners(const std::vector<FToken>& Tokens)
		{
			std::vector<bool> Openers(Tokens.size(), false);
			std::vector<std::size_t> Pending;
			// Match all angle lists together. Re-scanning from every '<' is
			// quadratic for deeply nested lists, including incomplete buffers.
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::Symbol)
				{
					if (Token.Lexeme == "<")
					{
						Pending.push_back(Index);
						continue;
					}
					if (Token.Lexeme == ">" || Token.Lexeme == ">>")
					{
						const std::size_t Count = Token.Lexeme == ">>" ? 2u : 1u;
						for (std::size_t Closed = 0; Closed < Count && !Pending.empty(); ++Closed)
						{
							const std::size_t Open = Pending.back();
							Pending.pop_back();
							// Preserve the scanner's exact-token rule: '>>' closes
							// an outer list, but overshoots its innermost list.
							if (Closed + 1 != Count || Open == 0)
							{
								continue;
							}
							const FToken& Previous = Tokens[Open - 1];
							Openers[Open] = (Previous.Kind == ETokenKind::Identifier || Previous.Kind == ETokenKind::Keyword) && !IsControlKeyword(Previous) && Previous.Lexeme != "return" && Previous.Lexeme != "else" && Previous.Lexeme != "case";
						}
						continue;
					}
					if (Token.Lexeme == "," || Token.Lexeme == "..." || Token.Lexeme == "=" || Token.Lexeme == "::" || Token.Lexeme == "." || Token.Lexeme == "[" || Token.Lexeme == "]" || Token.Lexeme == "&")
					{
						continue;
					}
					Pending.clear();
					continue;
				}
				if (Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Keyword)
				{
					continue;
				}
				Pending.clear();
			}
			return Openers;
		}

		void AppendIndent(
		    std::string& Out, const std::uint32_t IndentWidth,
		    const bool bInsertSpaces, const int Level)
		{
			const int SafeLevel = Level < 0 ? 0 : Level;
			if (bInsertSpaces)
			{
				const std::uint32_t Width = IndentWidth == 0 ? 1 : IndentWidth;
				Out.append(static_cast<std::size_t>(SafeLevel) * Width, ' ');
			}
			else
			{
				Out.append(static_cast<std::size_t>(SafeLevel), '\t');
			}
		}

		bool TriviaHasBlankLine(
		    const std::string_view Source,
		    const std::size_t Begin,
		    const std::size_t End)
		{
			bool bSawNewline = false;
			for (std::size_t Index = Begin; Index < End; ++Index)
			{
				if (Source[Index] == '\n')
				{
					if (bSawNewline)
					{
						return true;
					}
					bSawNewline = true;
				}
				else if (Source[Index] != ' ' && Source[Index] != '\t' && Source[Index] != '\r')
				{
					bSawNewline = false;
				}
			}
			return false;
		}

		// Emit line/block comments from [Begin, End); returns true if anything was written.
		bool AppendCommentsInRange(
		    std::string& Out,
		    const std::string_view Source,
		    const std::size_t Begin,
		    const std::size_t End,
		    const std::uint32_t IndentWidth,
		    const bool bInsertSpaces,
		    const int Indent,
		    bool& bAtLineStart)
		{
			bool bEmitted = false;
			bool bCommentOnLine = false;
			std::size_t TriviaStart = Begin;
			const auto PreserveBlankBeforeComment = [&](const std::size_t CommentStart)
			{
				if (TriviaHasBlankLine(Source, TriviaStart, CommentStart) && bAtLineStart && !Out.empty() && Out.back() == '\n' && (Out.size() < 2 || Out[Out.size() - 2] != '\n'))
				{
					Out.push_back('\n');
				}
			};
			std::size_t Index = Begin;
			while (Index < End)
			{
				const char Char = Source[Index];
				if (Char == ' ' || Char == '\t' || Char == '\r')
				{
					++Index;
					continue;
				}
				if (Char == '\n')
				{
					if (bCommentOnLine && !bAtLineStart)
					{
						Out.push_back('\n');
						bAtLineStart = true;
					}
					bCommentOnLine = false;
					++Index;
					continue;
				}
				if (Char == '/' && Index + 1 < End && Source[Index + 1] == '/')
				{
					PreserveBlankBeforeComment(Index);
					if (!bAtLineStart)
					{
						Out.push_back(' ');
					}
					else
					{
						AppendIndent(Out, IndentWidth, bInsertSpaces, Indent);
						bAtLineStart = false;
					}
					const std::size_t CommentStart = Index;
					Index += 2;
					while (Index < End && Source[Index] != '\n')
					{
						++Index;
					}
					Out.append(Source.substr(CommentStart, Index - CommentStart));
					bEmitted = true;
					bCommentOnLine = true;
					TriviaStart = Index;
					continue;
				}
				if (Char == '/' && Index + 1 < End && Source[Index + 1] == '*')
				{
					PreserveBlankBeforeComment(Index);
					if (!bAtLineStart)
					{
						Out.push_back(' ');
					}
					else
					{
						AppendIndent(Out, IndentWidth, bInsertSpaces, Indent);
						bAtLineStart = false;
					}
					const std::size_t CommentStart = Index;
					Index += 2;
					bool bContainsNewline = false;
					while (Index + 1 < End && !(Source[Index] == '*' && Source[Index + 1] == '/'))
					{
						if (Source[Index] == '\n')
						{
							bContainsNewline = true;
						}
						++Index;
					}
					if (Index + 1 < End)
					{
						Index += 2;
					}
					Out.append(Source.substr(CommentStart, Index - CommentStart));
					bEmitted = true;
					bCommentOnLine = true;
					TriviaStart = Index;
					if (bContainsNewline)
					{
						bAtLineStart = false;
					}
					continue;
				}
				// Non-trivia between tokens should not appear; stop.
				break;
			}
			if (bEmitted && TriviaHasBlankLine(Source, TriviaStart, End) && bAtLineStart && !Out.empty() && Out.back() == '\n' && (Out.size() < 2 || Out[Out.size() - 2] != '\n'))
			{
				Out.push_back('\n');
			}
			return bEmitted;
		}

		// Same-line trailing // comment immediately after a token (before the next newline).
		std::size_t AppendTrailingLineComment(
		    std::string& Out,
		    const std::string_view Source,
		    const std::size_t Begin,
		    const std::size_t End)
		{
			std::size_t Index = Begin;
			while (Index < End && (Source[Index] == ' ' || Source[Index] == '\t'))
			{
				++Index;
			}
			if (Index + 1 >= End || Source[Index] != '/' || Source[Index + 1] != '/')
			{
				return Begin;
			}
			const std::size_t CommentStart = Index;
			Index += 2;
			while (Index < End && Source[Index] != '\n' && Source[Index] != '\r')
			{
				++Index;
			}
			Out.push_back(' ');
			Out.append(Source.substr(CommentStart, Index - CommentStart));
			return Index;
		}

		// Line-start `#` directive (after whitespace), including `\` continuations.
		bool IsPreprocessorDirectiveAt(const std::string_view Source, const std::size_t Offset)
		{
			if (Offset >= Source.size() || Source[Offset] != '#')
			{
				return false;
			}
			std::size_t LineStart = Offset;
			while (LineStart > 0 && Source[LineStart - 1] != '\n')
			{
				--LineStart;
			}
			if (LineStart == 0 && Source.substr(0, 3) == "\xef\xbb\xbf")
			{
				LineStart = 3;
			}
			for (std::size_t Index = LineStart; Index < Offset; ++Index)
			{
				const char Char = Source[Index];
				if (Char != ' ' && Char != '\t' && Char != '\r')
				{
					return false;
				}
			}
			return true;
		}

		bool IsSplicedPhysicalNewline(const std::string_view Source, const std::size_t Newline)
		{
			// Match the core preprocessor: only a backslash immediately before
			// LF (or its one preceding CR) splices physical lines. Spaces do not.
			std::size_t End = Newline;
			if (End > 0 && Source[End - 1] == '\r')
			{
				--End;
			}
			return End > 0 && Source[End - 1] == '\\';
		}

		bool HasSplicedCommentDelimiter(const std::string_view Source)
		{
			// Core splices physical lines before recognizing comments. The raw
			// formatter lexer cannot safely reflow delimiters formed by that step.
			// Conservatively preserve the document; no second lexer is needed.
			char Previous = '\0';
			bool bSpliced = false;
			std::size_t NextCancellationCheck = 0;
			for (std::size_t Index = 0; Index < Source.size();)
			{
				if (Index >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Index + 2048;
				}
				const char Character = Source[Index];
				if (Character == '\\')
				{
					std::size_t Newline = Index + 1;
					if (Newline < Source.size() && Source[Newline] == '\r')
					{
						++Newline;
					}
					if (Newline < Source.size() && Source[Newline] == '\n')
					{
						bSpliced = true;
						Index = Newline + 1;
						continue;
					}
				}
				if (bSpliced && ((Previous == '/' && (Character == '/' || Character == '*')) || (Previous == '*' && Character == '/')))
				{
					return true;
				}
				Previous = Character;
				bSpliced = false;
				++Index;
			}
			return false;
		}

		std::size_t PreprocessorDirectiveEnd(
		    const std::string_view Source,
		    const std::size_t HashOffset)
		{
			std::size_t Index = HashOffset;
			std::size_t NextCancellationCheck = Index;
			char Quote = '\0';
			bool bRaw = false, bLineComment = false;
			while (Index < Source.size())
			{
				if (Index >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Index + 2048;
				}
				if (Source[Index] == '\n')
				{
					if (!bRaw && !IsSplicedPhysicalNewline(Source, Index)) return Index + 1;
					++Index;
					continue;
				}
				const char Character = Source[Index];
				if (bLineComment)
				{
					++Index;
					continue;
				}
				if (Quote != '\0')
				{
					if (Character == Quote)
					{
						Quote = '\0';
						bRaw = false;
					}
					else if (!bRaw && Character == '\\' && Index + 1 < Source.size())
					{
						++Index;
					}
					++Index;
					continue;
				}
				if (Source.substr(Index, 2) == "//")
				{
					bLineComment = true;
					Index += 2;
					continue;
				}
				if (Source.substr(Index, 2) == "/*")
				{
					// A directive can open a comment that closes on a later line.
					// Keep that whole span so formatting never resumes inside it.
					const auto Close = Source.find("*/", Index + 2);
					if (Close == std::string_view::npos) return Source.size();
					const auto Comment = Source.substr(Index + 2, Close - Index - 2);
					bool bDirectiveEnded = false;
					for (auto Newline = Comment.find('\n'); Newline != std::string_view::npos;
					    Newline = Comment.find('\n', Newline + 1))
					{
						CheckAnalysisCancellation();
						if (!IsSplicedPhysicalNewline(Source, Index + 2 + Newline))
						{
							bDirectiveEnded = true;
							break;
						}
					}
					if (bDirectiveEnded)
					{
						// The directive ended before this physical line. Real code
						// after the comment must still update formatting context.
						auto End = Close + 2;
						while (End < Source.size() && (Source[End] == ' ' || Source[End] == '\t' || Source[End] == '\r'))
						{
							++End;
						}
						return End < Source.size() && Source[End] == '\n' ? End + 1 : Close + 2;
					}
					Index = Close + 2;
					continue;
				}
				if (Character == '"' || Character == '\'')
				{
					Quote = Character;
					const auto IsIdentifier = [](const unsigned char C)
					{
						return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C >= 0x80;
					};
					bRaw = Character == '"' && Index > 0 && Source[Index - 1] == 'r' && (Index < 2 || !IsIdentifier(static_cast<unsigned char>(Source[Index - 2])));
				}
				++Index;
			}
			return Source.size();
		}

		bool HasProtectedRangeBoundary(const std::string_view Source,
		    const std::size_t Begin, const std::size_t End)
		{
			bool bBlockComment = false;
			bool bLineComment = false;
			char Quote = '\0';
			bool bEscaped = false;
			bool bRaw = false;
			const std::size_t Limit = (std::min)(End, Source.size());
			std::size_t NextCancellationCheck = 0;
			for (std::size_t Index = 0; Index < Limit; ++Index)
			{
				if (Index == Begin && (bBlockComment || Quote != '\0')) return true;
				if (Index >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Index + 2048;
				}
				const char Character = Source[Index];
				const char Next = Index + 1 < Limit ? Source[Index + 1] : '\0';
				if (bLineComment)
				{
					bLineComment = Character != '\n';
					continue;
				}
				if (bBlockComment)
				{
					if (Character == '*' && Next == '/')
					{
						bBlockComment = false;
						++Index;
					}
					continue;
				}
				if (Quote != '\0')
				{
					if (bRaw)
					{
						if (Character == Quote)
						{
							Quote = '\0';
							bRaw = false;
						}
					}
					else if (Character == '\n' || Character == '\r')
					{
						// Ordinary literals cannot continue across a physical newline.
						Quote = '\0';
						bEscaped = false;
					}
					else if (bEscaped)
					{
						bEscaped = false;
					}
					else if (Character == '\\')
					{
						bEscaped = true;
					}
					else if (Character == Quote)
					{
						Quote = '\0';
					}
					continue;
				}
				if (Character == '#' && IsPreprocessorDirectiveAt(Source, Index))
				{
					const std::size_t DirectiveEnd = PreprocessorDirectiveEnd(Source, Index);
					if ((Begin > Index && Begin < DirectiveEnd) || End < DirectiveEnd)
					{
						return true;
					}
					Index = DirectiveEnd - 1;
					continue;
				}
				if (Character == '"' || Character == '\'')
				{
					Quote = Character;
					// The lexer recognizes r" only when r is a separate token.
					const auto IsIdentifier = [](const unsigned char C)
					{
						return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C >= 0x80;
					};
					bRaw = Character == '"' && Index > 0 && Source[Index - 1] == 'r' && (Index < 2 || !IsIdentifier(static_cast<unsigned char>(Source[Index - 2])));
				}
				else if (Character == '/' && Next == '/')
				{
					bLineComment = true;
					++Index;
				}
				else if (Character == '/' && Next == '*')
				{
					bBlockComment = true;
					++Index;
				}
			}
			return bBlockComment || Quote != '\0';
		}

		struct FDirectiveSpan
		{
			std::size_t Begin = 0;
			std::size_t End = 0;
		};

		std::vector<FDirectiveSpan> CollectPreprocessorDirectives(
		    const std::string_view Source, const std::vector<FToken>& Tokens)
		{
			std::vector<FDirectiveSpan> Spans;
			// The lexer has already excluded comments and kept literals intact.
			// A line-start '#' inside either must not split its source span.
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol || Token.Lexeme != "#")
				{
					continue;
				}
				const std::size_t Begin = TokenStartOffset(Source, Token);
				if ((!Spans.empty() && Begin < Spans.back().End) || !IsPreprocessorDirectiveAt(Source, Begin))
				{
					continue;
				}
				Spans.push_back(FDirectiveSpan{Begin, PreprocessorDirectiveEnd(Source, Begin)});
			}
			return Spans;
		}

		bool OffsetInDirective(
		    const std::vector<FDirectiveSpan>& Spans,
		    const std::size_t Offset,
		    std::size_t& OutSpanIndex)
		{
			const auto After = std::upper_bound(Spans.begin(), Spans.end(), Offset,
			    [](const std::size_t Value, const FDirectiveSpan& Span)
			{
				return Value < Span.Begin;
			});
			if (After == Spans.begin()) return false;
			const auto Found = std::prev(After);
			if (Offset >= Found->End) return false;
			OutSpanIndex = static_cast<std::size_t>(Found - Spans.begin());
			return true;
		}

		FFormatResult FormatTokens(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FFormatOptions& Options)
		{
			if (HasSplicedCommentDelimiter(Source))
			{
				FFormatResult Result;
				Result.bSuccess = true;
				Result.FormattedSource = std::string(Source);
				return Result;
			}
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				FFormatResult Result;
				FScriptError Error;
				if (Lexer.GetErrorMessage().empty())
				{
					FillDiagnostic(
					    Error,
					    "diag.format_failed_lexer_error",
					    FSourceLocation{std::string(FileLabel), 1, 1});
				}
				else
				{
					FillFromEnglishMessage(
					    Error,
					    Lexer.GetErrorMessage(),
					    FSourceLocation{std::string(FileLabel), 1, 1});
				}
				Result.Diagnostics.push_back(ToToolingDiagnostic(Error, FileLabel));
				return Result;
			}

			const std::vector<FDirectiveSpan> Directives = CollectPreprocessorDirectives(Source, Tokens);

			const std::vector<bool> TypeArgOpen = FindTypeArgumentListOpeners(Tokens);
			std::vector<bool> TypeArgClose(Tokens.size(), false);
			std::vector<bool> TemplateArgClose(Tokens.size(), false);
			std::vector<bool> PrecomputedTypeArgStack;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				if (TypeArgOpen[Index])
				{
					PrecomputedTypeArgStack.push_back(
					    Index > 0 && Tokens[Index - 1].Lexeme == "template");
				}
				else if (!PrecomputedTypeArgStack.empty() && Tokens[Index].Kind == ETokenKind::Symbol)
				{
					if (Tokens[Index].Lexeme == "<")
					{
						PrecomputedTypeArgStack.push_back(false);
					}
					else if (Tokens[Index].Lexeme == ">")
					{
						TypeArgClose[Index] = true;
						TemplateArgClose[Index] = PrecomputedTypeArgStack.back();
						PrecomputedTypeArgStack.pop_back();
					}
					else if (Tokens[Index].Lexeme == ">>")
					{
						TypeArgClose[Index] = true;
						for (int CloseCount = 0;
						    CloseCount < 2 && !PrecomputedTypeArgStack.empty();
						    ++CloseCount)
						{
							TemplateArgClose[Index] =
							    TemplateArgClose[Index] || PrecomputedTypeArgStack.back();
							PrecomputedTypeArgStack.pop_back();
						}
					}
				}
			}

			std::string Out;
			Out.reserve(Source.size() + 64);
			const std::size_t EncodingPrefix = Source.substr(0, 3) == "\xef\xbb\xbf" ? 3u : 0u;
			Out.append(Source.substr(0, EncodingPrefix));
			int Indent = 0;
			// Paren/bracket nest so `for (...; ...; ...)` keeps `;` on one line.
			int GroupDepth = 0;
			int TypeArgDepth = 0;
			std::vector<bool> BraceBlocks;
			std::vector<bool> BraceEnums;
			std::vector<bool> BraceTypes;
			bool bAtLineStart = true;
			const FToken* Previous = nullptr;
			std::size_t Cursor = EncodingPrefix;
			std::size_t NextDirective = 0;

			const auto EmitDirectivesThrough = [&](const std::size_t Limit)
			{
				while (NextDirective < Directives.size() && Directives[NextDirective].Begin < Limit)
				{
					const FDirectiveSpan& Span = Directives[NextDirective];
					if (Span.Begin < Cursor)
					{
						++NextDirective;
						continue;
					}
					AppendCommentsInRange(
					    Out,
					    Source,
					    Cursor,
					    Span.Begin,
					    Options.IndentWidth,
					    Options.bInsertSpaces,
					    Indent,
					    bAtLineStart);
					if (!bAtLineStart)
					{
						Out.push_back('\n');
						bAtLineStart = true;
					}
					Out.append(Source.substr(Span.Begin, Span.End - Span.Begin));
					Cursor = Span.End;
					Previous = nullptr;
					if (!Out.empty() && Out.back() != '\n')
					{
						Out.push_back('\n');
					}
					bAtLineStart = true;
					++NextDirective;
				}
			};

			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::EndOfFile)
				{
					EmitDirectivesThrough(Source.size());
					AppendCommentsInRange(
					    Out,
					    Source,
					    Cursor,
					    Source.size(),
					    Options.IndentWidth,
					    Options.bInsertSpaces,
					    Indent,
					    bAtLineStart);
					break;
				}

				const std::size_t TokenBegin = TokenStartOffset(Source, Token);
				const std::size_t TokenEnd = TokenBegin + Token.Lexeme.size();

				EmitDirectivesThrough(TokenBegin + 1);
				std::size_t SpanIndex = 0;
				if (OffsetInDirective(Directives, TokenBegin, SpanIndex) || TokenBegin < Cursor)
				{
					// Token is part of a pass-through directive.
					continue;
				}

				const bool bBlankBefore = TriviaHasBlankLine(Source, Cursor, TokenBegin);
				const bool bEmittedComments = AppendCommentsInRange(
				    Out,
				    Source,
				    Cursor,
				    TokenBegin,
				    Options.IndentWidth,
				    Options.bInsertSpaces,
				    Indent,
				    bAtLineStart);
				// Preserve one blank line between top-level / statement groups.
				if (bBlankBefore && !bEmittedComments && bAtLineStart && !Out.empty() && Out.back() == '\n' && (Out.size() < 2 || Out[Out.size() - 2] != '\n'))
				{
					Out.push_back('\n');
				}

				const bool bOpeningBrace = Token.Kind == ETokenKind::Symbol && Token.Lexeme == "{";
				const bool bClosingBrace = Token.Kind == ETokenKind::Symbol && Token.Lexeme == "}";
				const bool bBlockBrace = bOpeningBrace
				    ? IsBlockBrace(Tokens, Index, BraceBlocks)
				    : (bClosingBrace && !BraceBlocks.empty() ? BraceBlocks.back() : true);
				const bool bEnumBrace = bOpeningBrace
				    ? IsEnumBodyBrace(Tokens, Index)
				    : (bClosingBrace && !BraceEnums.empty() ? BraceEnums.back() : false);
				const bool bTypeBrace = bOpeningBrace
				    ? IsTypeBodyBrace(Tokens, Index)
				    : (bClosingBrace && !BraceTypes.empty() ? BraceTypes.back() : false);
				// Comments can reopen a clause's line, even when range formatting omits '}'.
				const bool bContinuationClause = Token.Kind == ETokenKind::Keyword && (Token.Lexeme == "else" || Token.Lexeme == "catch" || Token.Lexeme == "finally" || Token.Lexeme == "while");
				if ((bBlockBrace && (bOpeningBrace || bClosingBrace)) || bContinuationClause)
				{
					if (bClosingBrace)
					{
						--Indent;
					}
					if (!bAtLineStart)
					{
						Out.push_back('\n');
						bAtLineStart = true;
					}
				}

				// Access labels (`public:` / `protected:` / `private:`) outdent one level
				// relative to members (match examples / C++ style), then force a newline.
				const bool bAccessLabel = IsAccessSpecifierKeyword(Token) && Index + 1 < Tokens.size() && Tokens[Index + 1].Kind == ETokenKind::Symbol && Tokens[Index + 1].Lexeme == ":";
				if (bAccessLabel)
				{
					if (!bAtLineStart)
					{
						Out.push_back('\n');
						bAtLineStart = true;
					}
					const int AccessIndent = Indent > 0 ? Indent - 1 : 0;
					AppendIndent(Out, Options.IndentWidth, Options.bInsertSpaces, AccessIndent);
					Out.append(Token.Lexeme.data(), Token.Lexeme.size());
					Out.push_back(':');
					const FToken& Colon = Tokens[Index + 1];
					const std::size_t ColonBegin = TokenStartOffset(Source, Colon);
					const std::size_t ColonEnd = ColonBegin + Colon.Lexeme.size();
					Cursor = ColonEnd;
					Cursor = AppendTrailingLineComment(Out, Source, Cursor, Source.size());
					Out.push_back('\n');
					bAtLineStart = true;
					Previous = &Colon;
					++Index;
					continue;
				}

				const bool bTypeArgMarkup = TypeArgDepth > 0 || TypeArgOpen[Index];
				if (bAtLineStart)
				{
					AppendIndent(Out, Options.IndentWidth, Options.bInsertSpaces, Indent);
					bAtLineStart = false;
				}
				else if (Previous)
				{
					bool bSpace = false;
					if (bTypeArgMarkup)
					{
						bSpace = Previous->Lexeme == "," || Previous->Lexeme == "=" || Token.Lexeme == "=";
					}
					else
					{
						bSpace = WantsSpaceBetween(*Previous, Token);
						if (Previous->Kind == ETokenKind::Symbol && Previous->Lexeme == ";" && GroupDepth > 0)
						{
							bSpace = true;
						}
						if (Index > 0 && IsUnaryPrefixOperator(Tokens, Index - 1))
						{
							bSpace = false;
						}
						else if (Previous->Kind == ETokenKind::Symbol && (Previous->Lexeme == "+" || Previous->Lexeme == "-"))
						{
							bSpace = true;
						}
						if (Previous->Lexeme == "operator" && Token.Kind == ETokenKind::Symbol)
						{
							bSpace = false;
						}
						if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "...")
						{
							bSpace = false;
						}
						if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ":" && !BraceBlocks.empty() && !BraceBlocks.back())
						{
							bSpace = false;
						}
						if (bOpeningBrace && !bBlockBrace && (Previous->Kind == ETokenKind::Identifier || Previous->Lexeme == ">" || Previous->Lexeme == ">>"))
						{
							bSpace = false;
						}
						if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "(" && Index >= 2 && Tokens[Index - 2].Lexeme == "operator")
						{
							bSpace = false;
						}
						if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "(" && Index > 0 && TypeArgClose[Index - 1])
						{
							bSpace = false;
						}
						if (IsReferenceDeclarator(Tokens, TypeArgClose, Index))
						{
							bSpace = false;
						}
						else if (Index > 0 && IsReferenceDeclarator(Tokens, TypeArgClose, Index - 1))
						{
							bSpace = true;
						}
					}
					if (bSpace || NeedsLexicalSeparator(*Previous, Token))
					{
						Out.push_back(' ');
					}
				}

				Out.append(Token.Lexeme.data(), Token.Lexeme.size());

				if (TypeArgOpen[Index])
				{
					++TypeArgDepth;
				}
				else if (TypeArgDepth > 0 && Token.Kind == ETokenKind::Symbol)
				{
					if (Token.Lexeme == "<")
					{
						++TypeArgDepth;
					}
					else if (Token.Lexeme == ">")
					{
						--TypeArgDepth;
					}
					else if (Token.Lexeme == ">>")
					{
						TypeArgDepth -= 2;
						if (TypeArgDepth < 0)
						{
							TypeArgDepth = 0;
						}
					}
				}

				bool bNeedsNewline = false;
				if (Token.Kind == ETokenKind::Symbol)
				{
					if (Token.Lexeme == "(" || Token.Lexeme == "[")
					{
						++GroupDepth;
					}
					else if (Token.Lexeme == ")" || Token.Lexeme == "]")
					{
						if (GroupDepth > 0)
						{
							--GroupDepth;
						}
					}
					else if (Token.Lexeme == "{")
					{
						BraceBlocks.push_back(bBlockBrace);
						BraceEnums.push_back(bEnumBrace);
						BraceTypes.push_back(bTypeBrace);
						if (bBlockBrace)
						{
							++Indent;
							bNeedsNewline = true;
						}
					}
					else if (Token.Lexeme == "}")
					{
						if (!BraceBlocks.empty())
						{
							BraceBlocks.pop_back();
						}
						if (!BraceEnums.empty())
						{
							BraceEnums.pop_back();
						}
						if (!BraceTypes.empty())
						{
							BraceTypes.pop_back();
						}
						if (bBlockBrace)
						{
							const bool bContinuesOnLine = Index + 1 < Tokens.size() && (Tokens[Index + 1].Lexeme == ";" || Tokens[Index + 1].Lexeme == "," || Tokens[Index + 1].Lexeme == ")" || Tokens[Index + 1].Lexeme == "]" || (bTypeBrace && Tokens[Index + 1].Kind == ETokenKind::Identifier));
							bNeedsNewline = !bContinuesOnLine;
						}
					}
					else if (Token.Lexeme == ";" && GroupDepth == 0)
					{
						bNeedsNewline = true;
					}
					else if (Token.Lexeme == "," && GroupDepth == 0 && !BraceEnums.empty() && BraceEnums.back())
					{
						bNeedsNewline = true;
					}
					else if ((Token.Lexeme == ">" || Token.Lexeme == ">>") && TemplateArgClose[Index])
					{
						bNeedsNewline = true;
					}
				}

				Cursor = TokenEnd;
				if (bNeedsNewline)
				{
					Cursor = AppendTrailingLineComment(Out, Source, Cursor, Source.size());
					Out.push_back('\n');
					bAtLineStart = true;
				}

				Previous = &Token;
			}

			while (!Out.empty() && (Out.back() == ' ' || Out.back() == '\t'))
			{
				Out.pop_back();
			}
			if (Out.size() > EncodingPrefix && Out.back() != '\n')
			{
				Out.push_back('\n');
			}

			FFormatResult Result;
			Result.bSuccess = true;
			Result.FormattedSource = std::move(Out);
			return Result;
		}
	}

	FFormatResult VeroScriptFormat::FormatSource(
	    const std::string_view Source,
	    const FFormatOptions& Options)
	{
		return FormatTokens(Source, "<format>", Options);
	}

	FRangeFormatResult VeroScriptFormat::FormatRangeSource(
	    const std::string_view Source,
	    FTextRange Range,
	    const FFormatOptions& Options)
	{
		FRangeFormatResult Result;
		if (HasSplicedCommentDelimiter(Source))
		{
			Result.bSuccess = true;
			return Result;
		}
		const std::uint32_t LineCount = CountLines(Source);
		if (LineCount == 0)
		{
			Result.bSuccess = true;
			return Result;
		}

		Range.StartLine = (std::max)(1u, (std::min)(Range.StartLine, LineCount));
		Range.EndLine = (std::max)(Range.StartLine, (std::min)(Range.EndLine, LineCount));
		// LSP ranges are end-exclusive. An end at character zero of a later line
		// denotes the newline after the preceding line and must not pull the next
		// physical line into the formatting edit.
		if (Range.EndColumn <= 1u && Range.EndLine > Range.StartLine)
		{
			--Range.EndLine;
		}
		const std::size_t Begin = LineStartOffset(Source, Range.StartLine);
		const std::size_t End = LineEndOffset(Source, Range.EndLine);
		// A fragment of a comment or raw literal cannot be lexed independently.
		// Preserve its contents; complete spans remain available for formatting.
		if (HasProtectedRangeBoundary(Source, Begin, End))
		{
			Result.bSuccess = true;
			return Result;
		}
		const int ContextDepth = EnclosingBlockDepth(Source.substr(0, Begin), "<range-format>");

		std::string Synthetic;
		Synthetic.reserve(
		    static_cast<std::size_t>((std::max)(0, ContextDepth)) * 2u + End - Begin);
		for (int Index = 0; Index < ContextDepth; ++Index)
		{
			Synthetic += "{\n";
		}
		Synthetic.append(Source.substr(Begin, End - Begin));
		const FFormatResult Formatted = FormatTokens(Synthetic, "<range-format>", Options);
		Result.bSuccess = Formatted.bSuccess;
		Result.Diagnostics = Formatted.Diagnostics;
		if (!Formatted.bSuccess)
		{
			return Result;
		}

		std::size_t ContentOffset = 0;
		for (int Index = 0; Index < ContextDepth; ++Index)
		{
			ContentOffset = Formatted.FormattedSource.find('\n', ContentOffset);
			if (ContentOffset == std::string::npos)
			{
				Result.bSuccess = false;
				return Result;
			}
			++ContentOffset;
		}
		Result.FormattedSource = Formatted.FormattedSource.substr(ContentOffset);
		Result.FormattedRange.StartLine = Range.StartLine;
		Result.FormattedRange.StartColumn = 1;
		// FormatTokens keeps a trailing line-feed.  Include the selected line-feed
		// in the replacement range whenever it exists; otherwise a range edit would
		// insert an extra blank line on every invocation.  LSP positions can express
		// this cleanly as column 0 of the following line.
		const std::size_t LastLineEnd = LineEndOffset(Source, Range.EndLine);
		if (LastLineEnd > 0 && Source[LastLineEnd - 1] == '\n')
		{
			Result.FormattedRange.EndLine = Range.EndLine + 1u;
			Result.FormattedRange.EndColumn = 1;
		}
		else
		{
			Result.FormattedRange.EndLine = Range.EndLine;
			const std::size_t LastLineStart = LineStartOffset(Source, Range.EndLine);
			Result.FormattedRange.EndColumn = static_cast<std::uint32_t>(
			    LastLineEnd - LastLineStart + 1u);
			// FormatTokens always emits a final LF, but a replacement for an unterminated
			// final line must preserve that fact.
			if (!Result.FormattedSource.empty() && Result.FormattedSource.back() == '\n')
			{
				Result.FormattedSource.pop_back();
			}
		}
		Result.bHasFormattedRange = true;
		return Result;
	}

	FFormatResult VeroScriptFormat::FormatFile(
	    const std::string_view FilePath,
	    const FFormatOptions& Options)
	{
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			FFormatResult Result;
			FScriptError Error;
			FillDiagnostic(
			    Error,
			    "diag.cannot_open_script",
			    FSourceLocation{std::string(FilePath), 1, 1},
			    {FilePath});
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, FilePath));
			return Result;
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		return FormatTokens(Buffer.str(), FilePath, Options);
	}
}
