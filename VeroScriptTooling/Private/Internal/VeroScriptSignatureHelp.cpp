// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreparedQuery.h"
#include "VeroScriptSourceMetadataQuery.h"
#include "VeroScriptBindingVisibility.h"

#include "VeroScriptSignatureHelp.h"

#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptMemory.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptOperatorLex.h"
#include "VeroScriptPath.h"
#include "VeroScriptSoftBuiltins.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPreprocessorQuery.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptStandardNamespaceMetadata.h"
#include "VeroScriptSymbolQuery.h"
#include "VeroScriptTypeHierarchy.h"
#include "VeroScriptTypes.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		struct FCallSite
		{
			bool bActive = false;
			bool bIsMethod = false;
			bool bInvokesCallResult = false;
			bool bLocalValue = false;
			std::string CalleeName;
			// Dot-normalized identity of a resolved namespace-qualified free call.
			// CalleeName remains the leaf so token-position queries work.
			std::string QualifiedCalleeName;
			// Exact free-function identities selected by the shared scoped query.
			std::vector<std::string> ResolvedFunctionNames;
			std::vector<std::string> ExplicitTypeArgs;
			std::string InstantiatedTypeName;
			std::string ReceiverName;
			std::uint32_t ActiveParameter = 0;
			std::uint32_t CalleeLine = 1;
			std::uint32_t CalleeColumn = 1;
			FTextPosition OpenPosition;
			std::uint32_t ReceiverLine = 1;
			std::uint32_t ReceiverColumn = 1;
			char CloseDelimiter = ')';
		};

		int ComparePosition(
		    const std::uint32_t LineA,
		    const std::uint32_t ColumnA,
		    const FTextPosition Position)
		{
			if (LineA < Position.Line)
			{
				return -1;
			}
			if (LineA > Position.Line)
			{
				return 1;
			}
			if (ColumnA < Position.Column)
			{
				return -1;
			}
			if (ColumnA > Position.Column)
			{
				return 1;
			}
			return 0;
		}

		bool TokenIsSymbol(const FToken& Token, const char* Lexeme)
		{
			return Token.Kind == ETokenKind::Symbol && Token.Lexeme == Lexeme;
		}

		// The lexer preserves shift operators. Count closing angles only while
		// examining an explicit generic argument list; never split source tokens.
		int TypeArgumentCloseCount(const FToken& Token)
		{
			return TokenIsSymbol(Token, ">") ? 1 : TokenIsSymbol(Token, ">>") ? 2
			                                                                  : 0;
		}

		bool ParseTypeArgumentTokens(
		    const std::vector<FToken>& Tokens,
		    const std::size_t OpenIndex,
		    const std::size_t CloseIndex,
		    std::vector<std::string>& Out)
		{
			Out.clear();
			if (OpenIndex >= CloseIndex || !TokenIsSymbol(Tokens[OpenIndex], "<") || TypeArgumentCloseCount(Tokens[CloseIndex]) == 0)
			{
				return false;
			}
			std::string Current;
			int Depth = 0;
			int ParenthesisDepth = 0;
			bool bPreviousWord = false;
			for (std::size_t Index = OpenIndex + 1; Index < CloseIndex; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (TokenIsSymbol(Token, "<"))
				{
					++Depth;
				}
				else if (TypeArgumentCloseCount(Token) != 0)
				{
					Depth -= TypeArgumentCloseCount(Token);
				}
				else if (TokenIsSymbol(Token, "("))
				{
					++ParenthesisDepth;
				}
				else if (TokenIsSymbol(Token, ")") && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Depth == 0 && ParenthesisDepth == 0 && TokenIsSymbol(Token, ","))
				{
					if (Current.empty())
					{
						return false;
					}
					Out.push_back(std::move(Current));
					Current.clear();
					bPreviousWord = false;
					continue;
				}
				const bool bWord = Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Keyword;
				if (bWord && bPreviousWord)
				{
					Current.push_back(' ');
				}
				if (Token.Lexeme == "::")
				{
					Current.push_back('.');
				}
				else
				{
					Current.append(Token.Lexeme.data(), Token.Lexeme.size());
				}
				bPreviousWord = bWord;
			}
			// A final >> also closes the innermost argument before the outer list.
			const int InnerCloses = TypeArgumentCloseCount(Tokens[CloseIndex]) - 1;
			if (Depth != InnerCloses || ParenthesisDepth != 0 || Current.empty())
			{
				Out.clear();
				return false;
			}
			Current.append(static_cast<std::size_t>(InnerCloses), '>');
			Out.push_back(std::move(Current));
			return true;
		}

		bool IsBraceCallOpen(const std::vector<FToken>& Tokens, const std::size_t OpenIndex)
		{
			if (OpenIndex == 0 || !TokenIsSymbol(Tokens[OpenIndex], "{"))
			{
				return false;
			}
			std::size_t BeforeIndex = OpenIndex - 1;
			if (TypeArgumentCloseCount(Tokens[BeforeIndex]) != 0)
			{
				return true;
			}
			if (Tokens[BeforeIndex].Kind != ETokenKind::Identifier)
			{
				return false;
			}
			if (BeforeIndex == 0 || Tokens[BeforeIndex - 1].Kind != ETokenKind::Keyword)
			{
				return true;
			}
			const std::string_view Keyword = Tokens[BeforeIndex - 1].Lexeme;
			return Keyword != "struct" && Keyword != "class" && Keyword != "interface" && Keyword != "enum" && Keyword != "namespace";
		}

		bool FindEnclosingDelimiter(const std::vector<FToken>& Tokens,
		    const FTextPosition Position, const bool bIndexSite,
		    std::size_t& OutOpen, std::uint32_t& OutCommas)
		{
			FAnalysisStage Stage("signature.delimiters");
			CheckAnalysisCancellation();
			struct FDelimiter
			{
				std::size_t Open;
				std::uint32_t Commas = 0;
			};
			std::vector<FDelimiter> Stack;
			// Only delimiters still open at the caret can contain this call site.
			// Scan each token once, including malformed/unclosed editor buffers.
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Token = Tokens[Index];
				const int AtCaret = ComparePosition(Token.Location.Line, Token.Location.Column, Position);
				if (AtCaret > 0)
				{
					break;
				}
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == "(" || Token.Lexeme == "[" || Token.Lexeme == "{")
				{
					if (ComparePosition(Token.Location.Line,
					        Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size()), Position) > 0)
					{
						break;
					}
					Stack.push_back({Index});
				}
				else if (Token.Lexeme == ")" || Token.Lexeme == "]" || Token.Lexeme == "}")
				{
					// Calls include the closing character; index help ends at ']'.
					if (AtCaret == 0 && !bIndexSite)
					{
						break;
					}
					if (!Stack.empty())
					{
						const auto Open = Tokens[Stack.back().Open].Lexeme;
						if ((Open == "(" && Token.Lexeme == ")") || (Open == "[" && Token.Lexeme == "]") || (Open == "{" && Token.Lexeme == "}"))
						{
							Stack.pop_back();
						}
					}
				}
				else if (Token.Lexeme == "," && !Stack.empty() && ComparePosition(Token.Location.Line, Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size()), Position) <= 0)
				{
					++Stack.back().Commas;
				}
			}
			for (std::size_t Index = Stack.size(); Index > 0; --Index)
			{
				if ((Index & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Frame = Stack[Index - 1];
				if (bIndexSite ? TokenIsSymbol(Tokens[Frame.Open], "[")
				               : TokenIsSymbol(Tokens[Frame.Open], "(") || IsBraceCallOpen(Tokens, Frame.Open))
				{
					OutOpen = Frame.Open;
					OutCommas = Frame.Commas;
					return true;
				}
			}
			return false;
		}

		bool FindCallSite(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    FCallSite& Out)
		{
			Out = {};
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError() || Tokens.empty())
			{
				return false;
			}

			std::size_t BestOpen = Tokens.size();
			std::uint32_t BestCommas = 0;

			if (!FindEnclosingDelimiter(Tokens, Position, false, BestOpen, BestCommas) || BestOpen == 0)
			{
				return false;
			}

			// Accept `Ident(` and `Ident<...>(` (skip balanced type-argument list).
			// Also retain the inner callee for immediately-invoked callable results,
			// e.g. `provider.callback()(` or `make_callback()(`.
			std::size_t CalleeIndex = BestOpen - 1;
			bool bInvokesCallResult = false;
			if (TokenIsSymbol(Tokens[CalleeIndex], ")"))
			{
				int ParenDepth = 1;
				std::size_t InnerOpen = CalleeIndex;
				while (InnerOpen > 0 && ParenDepth > 0)
				{
					--InnerOpen;
					if (TokenIsSymbol(Tokens[InnerOpen], ")"))
					{
						++ParenDepth;
					}
					else if (TokenIsSymbol(Tokens[InnerOpen], "("))
					{
						--ParenDepth;
					}
				}
				if (ParenDepth != 0 || InnerOpen == 0)
				{
					return false;
				}
				CalleeIndex = InnerOpen - 1;
				bInvokesCallResult = true;
			}
			if (TypeArgumentCloseCount(Tokens[CalleeIndex]) != 0)
			{
				const std::size_t CloseTypeArgs = CalleeIndex;
				int AngleDepth = TypeArgumentCloseCount(Tokens[CalleeIndex]);
				std::size_t Index = CalleeIndex;
				while (Index > 0 && AngleDepth > 0)
				{
					--Index;
					if (TokenIsSymbol(Tokens[Index], ";") || TokenIsSymbol(Tokens[Index], "=") || TokenIsSymbol(Tokens[Index], "{") || TokenIsSymbol(Tokens[Index], "}"))
					{
						break;
					}
					if (TypeArgumentCloseCount(Tokens[Index]) != 0)
					{
						AngleDepth += TypeArgumentCloseCount(Tokens[Index]);
					}
					else if (TokenIsSymbol(Tokens[Index], "<"))
					{
						--AngleDepth;
					}
				}
				if (AngleDepth != 0 || !TokenIsSymbol(Tokens[Index], "<") || Index == 0)
				{
					return false;
				}
				(void)ParseTypeArgumentTokens(
				    Tokens, Index, CloseTypeArgs, Out.ExplicitTypeArgs);
				CalleeIndex = Index - 1;
			}

			// In `Type variable(...)`, the token immediately before `(` is the
			// declarator name. Signature help must target the constructor type,
			// just as it does for the equivalent `Type(...)` expression.
			bool bDirectInitializer = false;
			std::size_t DisplayCalleeIndex = CalleeIndex;
			if (!bInvokesCallResult && TokenIsSymbol(Tokens[BestOpen], "(") && CalleeIndex > 0)
			{
				std::size_t TypeIndex = CalleeIndex - 1;
				if (TypeArgumentCloseCount(Tokens[TypeIndex]) != 0)
				{
					const std::size_t CloseTypeArgs = TypeIndex;
					int AngleDepth = TypeArgumentCloseCount(Tokens[TypeIndex]);
					while (TypeIndex > 0 && AngleDepth > 0)
					{
						--TypeIndex;
						if (TokenIsSymbol(Tokens[TypeIndex], ";") || TokenIsSymbol(Tokens[TypeIndex], "=") || TokenIsSymbol(Tokens[TypeIndex], "{") || TokenIsSymbol(Tokens[TypeIndex], "}"))
						{
							break;
						}
						if (TypeArgumentCloseCount(Tokens[TypeIndex]) != 0)
						{
							AngleDepth += TypeArgumentCloseCount(Tokens[TypeIndex]);
						}
						else if (TokenIsSymbol(Tokens[TypeIndex], "<"))
						{
							--AngleDepth;
						}
					}
					if (AngleDepth == 0 && TypeIndex > 0)
					{
						(void)ParseTypeArgumentTokens(
						    Tokens, TypeIndex, CloseTypeArgs, Out.ExplicitTypeArgs);
						--TypeIndex;
					}
				}
				const FToken& TypeToken = Tokens[TypeIndex];
				if (TypeToken.Kind == ETokenKind::Identifier)
				{
					bDirectInitializer = true;
					DisplayCalleeIndex = TypeIndex;
				}
			}

			const FToken& Before = Tokens[DisplayCalleeIndex];
			if (Before.Kind != ETokenKind::Identifier && !(Before.Kind == ETokenKind::Keyword && DisplayCalleeIndex > 0 && IsMemberAccessSymbol(Tokens[DisplayCalleeIndex - 1].Lexeme)))
			{
				return false;
			}

			Out.bActive = true;
			Out.OpenPosition = {Tokens[BestOpen].Location.Line, Tokens[BestOpen].Location.Column};
			Out.bInvokesCallResult = bInvokesCallResult;
			Out.ActiveParameter = BestCommas;
			Out.CloseDelimiter = TokenIsSymbol(Tokens[BestOpen], "{") ? '}' : ')';
			// `@name(...)` annotations lex as `@` + Ident; keep the catalog spelling.
			if (!bDirectInitializer && DisplayCalleeIndex >= 1 && TokenIsSymbol(Tokens[DisplayCalleeIndex - 1], "@"))
			{
				Out.CalleeName = "@";
				Out.CalleeName.append(Before.Lexeme.data(), Before.Lexeme.size());
			}
			else
			{
				Out.CalleeName.assign(Before.Lexeme.data(), Before.Lexeme.size());
			}
			if (!Out.ExplicitTypeArgs.empty())
			{
				Out.InstantiatedTypeName = Out.CalleeName;
				Out.InstantiatedTypeName.push_back('<');
				for (std::size_t Index = 0; Index < Out.ExplicitTypeArgs.size(); ++Index)
				{
					if (Index > 0)
					{
						Out.InstantiatedTypeName.push_back(',');
					}
					Out.InstantiatedTypeName += Out.ExplicitTypeArgs[Index];
				}
				Out.InstantiatedTypeName.push_back('>');
			}
			Out.CalleeLine = Before.Location.Line;
			Out.CalleeColumn = Before.Location.Column;
			if (!bDirectInitializer && DisplayCalleeIndex >= 2 && IsMemberAccessSymbol(Tokens[DisplayCalleeIndex - 1].Lexeme))
			{
				const FToken& Receiver = Tokens[DisplayCalleeIndex - 2];
				const bool bReceiverOk =
				    Receiver.Kind == ETokenKind::Identifier || (Receiver.Kind == ETokenKind::Keyword && Receiver.Lexeme == "this");
				if (bReceiverOk)
				{
					Out.bIsMethod = true;
					Out.ReceiverName.assign(Receiver.Lexeme.data(), Receiver.Lexeme.size());
					Out.ReceiverLine = Receiver.Location.Line;
					Out.ReceiverColumn = Receiver.Location.Column;
				}
				else if (TokenIsSymbol(Receiver, ")") || TokenIsSymbol(Receiver, "]"))
				{
					// Preserve method context for a postfix receiver rather than
					// treating its method name as a global free function.
					Out.bIsMethod = true;
				}
			}
			return true;
		}

		bool FindIndexSite(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    FCallSite& Out)
		{
			Out = {};
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError() || Tokens.empty())
			{
				return false;
			}

			std::size_t BestOpen = Tokens.size();
			std::uint32_t BestCommas = 0;
			if (!FindEnclosingDelimiter(Tokens, Position, true, BestOpen, BestCommas) || BestOpen == 0)
			{
				return false;
			}
			const FToken& Before = Tokens[BestOpen - 1];
			if (Before.Kind != ETokenKind::Identifier)
			{
				return false;
			}
			Out.bActive = true;
			Out.OpenPosition = {Tokens[BestOpen].Location.Line, Tokens[BestOpen].Location.Column};
			Out.bIsMethod = true;
			Out.ActiveParameter = BestCommas;
			Out.CloseDelimiter = ']';
			Out.CalleeName = "operator[]";
			Out.CalleeLine = Before.Location.Line;
			Out.CalleeColumn = Before.Location.Column;
			Out.ReceiverName.assign(Before.Lexeme.data(), Before.Lexeme.size());
			Out.ReceiverLine = Before.Location.Line;
			Out.ReceiverColumn = Before.Location.Column;
			return true;
		}

		bool NameMatchesSymbol(const std::string& SymbolName, const std::string_view Ident)
		{
			if (SymbolName == Ident)
			{
				return true;
			}
			if (SymbolName.size() > Ident.size() && SymbolName.compare(0, Ident.size(), Ident) == 0 && SymbolName[Ident.size()] == '<')
			{
				return true;
			}
			return false;
		}

		std::string CompletionLabel(const std::string& SymbolName)
		{
			const std::size_t Generic = SymbolName.find('<');
			if (Generic == std::string::npos)
			{
				return SymbolName;
			}
			return SymbolName.substr(0, Generic);
		}

		void ParseParametersFromDetail(
		    const std::string& Detail,
		    std::vector<FSignatureParameter>& OutParameters,
		    const bool bCallableType = false)
		{
			const std::string_view Clean = StripDeclMetaDisplay(Detail);
			// A method's last outer list follows its return type. A callable
			// value uses its first list, even when it returns another callable.
			VeroScriptLexer Lexer(Clean, "<signature-parameters>");
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError()) return;
			int Depth = 0;
			int BracketDepth = 0;
			std::size_t Open = Tokens.size(), Close = Tokens.size();
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}

				if (Token.Lexeme == "[")
				{
					++BracketDepth;
				}

				else if (Token.Lexeme == "]" && BracketDepth > 0)
				{
					--BracketDepth;
				}

				else if (Token.Lexeme == "(")
				{
					if (Depth++ == 0 && BracketDepth == 0)
					{
						Open = Index;
						Close = Tokens.size();
					}
				}
				else if (Token.Lexeme == ")" && Depth > 0 && --Depth == 0 && BracketDepth == 0)
				{
					Close = Index;
					if (bCallableType)
					{
						break;
					}
				}
			}
			if (Open == Tokens.size() || Close == Tokens.size() || Close <= Open) return;

			// Lexer lexemes are slices of Clean. Their byte offsets remain valid
			// across line breaks, UTF-8 text and literals containing punctuation.
			const auto Offset = [&](const FToken& Token)
			{
				return static_cast<std::size_t>(Token.Lexeme.data() - Clean.data());
			};
			std::size_t Start = Offset(Tokens[Open]) + 1;
			const auto Append = [&](std::size_t End)
			{
				auto Part = Clean.substr(Start, End - Start);
				while (!Part.empty() && std::isspace(static_cast<unsigned char>(Part.front())))
				{
					Part.remove_prefix(1);
				}

				while (!Part.empty() && std::isspace(static_cast<unsigned char>(Part.back())))
				{
					Part.remove_suffix(1);
				}

				if (!Part.empty())
				{
					OutParameters.push_back({std::string(Part)});
				}
			};
			int ParenDepth = 0, AngleDepth = 0, SquareDepth = 0, BraceDepth = 0;
			for (std::size_t Index = Open + 1; Index < Close; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}

				const auto Symbol = Token.Lexeme;
				if (Symbol == "(")
				{
					++ParenDepth;
				}

				else if (Symbol == ")" && ParenDepth > 0)
				{
					--ParenDepth;
				}

				else if (Symbol == "[")
				{
					++SquareDepth;
				}

				else if (Symbol == "]" && SquareDepth > 0)
				{
					--SquareDepth;
				}

				else if (Symbol == "{")
				{
					++BraceDepth;
				}

				else if (Symbol == "}" && BraceDepth > 0)
				{
					--BraceDepth;
				}

				else if (Symbol == "<")
				{
					++AngleDepth;
				}

				else if (Symbol == ">" && AngleDepth > 0)
				{
					--AngleDepth;
				}

				else if (Symbol == ">>" && AngleDepth > 0)
				{
					AngleDepth = std::max(0, AngleDepth - 2);
				}

				else if (Symbol == "," && ParenDepth == 0 && AngleDepth == 0 && SquareDepth == 0 &&
				    BraceDepth == 0)
				{
					Append(Offset(Token));
					Start = Offset(Token) + 1;
				}
			}
			Append(Offset(Tokens[Close]));
		}

		bool DetailLooksLikeFnType(const std::string_view Detail)
		{
			return Detail.size() >= 3 && (Detail.compare(0, 3, "fn(") == 0 || Detail.compare(0, 3, "fn<") == 0);
		}

		bool TryExtractLeadingFunctionType(
		    const std::string_view Detail,
		    std::string& OutType)
		{
			OutType.clear();
			const std::string_view View = StripDeclMetaDisplay(Detail);
			if (!DetailLooksLikeFnType(View))
			{
				return false;
			}

			const auto ParseTypeEnd = [&](const auto& Self, std::size_t Offset) -> std::size_t
			{
				while (Offset < View.size() && std::isspace(static_cast<unsigned char>(View[Offset])) != 0)
				{
					++Offset;
				}
				// Qualifiers belong to a nested function result as well as a
				// parameter. Keep the entire `->const ref T` type in the label.
				for (;;)
				{
					bool bQualifier = false;
					for (const std::string_view Prefix : {"const ", "ref ", "lvalue "})
					{
						if (View.substr(Offset, Prefix.size()) == Prefix)
						{
							Offset += Prefix.size();
							bQualifier = true;
							break;
						}
					}
					if (!bQualifier)
					{
						break;
					}
				}
				if (Offset + 2 < View.size() && View.compare(Offset, 3, "fn(") == 0)
				{
					std::size_t Index = Offset + 2;
					int Depth = 1;
					while (++Index < View.size() && Depth > 0)
					{
						if (View[Index] == '(')
						{
							++Depth;
						}
						else if (View[Index] == ')')
						{
							--Depth;
						}
					}
					while (Index < View.size() && std::isspace(static_cast<unsigned char>(View[Index])) != 0)
					{
						++Index;
					}
					if (Index + 1 >= View.size() || View.compare(Index, 2, "->") != 0)
					{
						return std::string_view::npos;
					}
					return Self(Self, Index + 2);
				}

				int AngleDepth = 0;
				for (std::size_t Index = Offset; Index < View.size(); ++Index)
				{
					const char Char = View[Index];
					if (Char == '<')
					{
						++AngleDepth;
					}
					else if (Char == '>' && AngleDepth > 0 && (Index == 0 || View[Index - 1] != '-'))
					{
						--AngleDepth;
					}
					else if (AngleDepth == 0 && (Char == '(' || std::isspace(static_cast<unsigned char>(Char)) != 0))
					{
						return Index;
					}
				}
				return View.size();
			};

			const std::size_t End = ParseTypeEnd(ParseTypeEnd, 0);
			if (End == std::string_view::npos || End == 0)
			{
				return false;
			}
			OutType.assign(View.substr(0, End));
			return true;
		}

		FSignatureInformation MakeSignature(
		    const std::string& Name,
		    const std::string& Detail,
		    const std::string& ContainerName)
		{
			const std::string CleanDetail = FormatStandardSourceSpelling(StripDeclMetaDisplay(Detail));
			FSignatureInformation Info;
			std::string FunctionType;
			if (TryExtractLeadingFunctionType(CleanDetail, FunctionType) && FunctionType.size() == CleanDetail.size())
			{
				Info.Label = CleanDetail;
				ParseParametersFromDetail(CleanDetail, Info.Parameters, true);
				return Info;
			}
			// Standard bound types store a return-type/parameter fragment, with
			// the constructor's own type as its return type. Present one source
			// name and put method return types before that name.
			if (Memory::IsTypeName(Name) || Memory::IsTypeName(ContainerName))
			{
				const std::size_t Open = CleanDetail.find('(');
				if (Open != std::string::npos)
				{
					const std::size_t ReturnEnd = CleanDetail.find_last_not_of(" \t", Open == 0 ? 0 : Open - 1);
					const std::string ReturnType = Open == 0 ? std::string{} : CleanDetail.substr(0, ReturnEnd + 1);
					const std::string DisplayName = ContainerName.empty()
					    ? FormatStandardSourceSpelling(Name)
					    : FormatStandardSourceSpelling(ContainerName) + "." + Name;
					if (!ReturnType.empty() && (!ContainerName.empty() || ReturnType != FormatStandardSourceSpelling(Name)))
					{
						Info.Label = FormatStandardSourceSpelling(ReturnType) + " ";
					}
					Info.Label += DisplayName + FormatStandardSourceSpelling(std::string_view(CleanDetail).substr(Open));
					ParseParametersFromDetail(CleanDetail, Info.Parameters);
					return Info;
				}
			}
			if (!ContainerName.empty())
			{
				Info.Label = FormatStandardSourceSpelling(ContainerName);
				Info.Label.push_back('.');
			}
			Info.Label += FormatStandardSourceSpelling(Name);
			if (!CleanDetail.empty())
			{
				if (CleanDetail.front() == '(')
				{
					Info.Label += CleanDetail;
				}
				else
				{
					Info.Label.push_back(' ');
					Info.Label += CleanDetail;
				}
			}
			ParseParametersFromDetail(CleanDetail, Info.Parameters);
			return Info;
		}

		FSignatureInformation MakeSourceSignature(const std::string& Detail)
		{
			FSignatureInformation Info;
			Info.Label = FormatStandardSourceSpelling(StripDeclMetaDisplay(Detail));
			ParseParametersFromDetail(Info.Label, Info.Parameters);
			return Info;
		}

		FSignatureInformation MakeSpecializedSourceSignature(
		    const std::string_view Detail,
		    const FCallSite& CallSite,
		    const std::string_view Placeholder)
		{
			if (CallSite.ExplicitTypeArgs.size() != 1)
			{
				return MakeSourceSignature(std::string(Detail));
			}
			std::string Specialized;
			for (std::size_t Index = 0; Index < Detail.size();)
			{
				const unsigned char Character = static_cast<unsigned char>(Detail[Index]);
				if (std::isalnum(Character) == 0 && Character != '_')
				{
					Specialized.push_back(Detail[Index++]);
					continue;
				}
				const std::size_t Start = Index++;
				while (Index < Detail.size())
				{
					const unsigned char Next = static_cast<unsigned char>(Detail[Index]);
					if (std::isalnum(Next) == 0 && Next != '_')
					{
						break;
					}
					++Index;
				}
				const std::string_view Word = Detail.substr(Start, Index - Start);
				Specialized += Word == Placeholder
				    ? CallSite.ExplicitTypeArgs.front()
				    : std::string(Word);
			}
			return MakeSourceSignature(Specialized);
		}

		void AppendOperatorCallSignatures(
		    const FProgram& Program,
		    const std::vector<FDocumentSymbol>& Symbols,
		    const std::string& TypeName,
		    std::vector<FSignatureInformation>& Out)
		{
			if (TypeName.empty())
			{
				return;
			}
			std::vector<std::string> Hierarchy;
			CollectTypeHierarchy(Program, TypeName, Hierarchy);
			for (const std::string& Candidate : Hierarchy)
			{
				const FDocumentSymbol* TypeSymbol = FindTypeSymbolInTree(Symbols, Candidate);
				if (!TypeSymbol)
				{
					continue;
				}
				bool bFound = false;
				for (const FDocumentSymbol& Child : TypeSymbol->Children)
				{
					if (Child.Kind == EDocumentSymbolKind::Method && Child.Name == "operator()")
					{
						Out.push_back(MakeSignature(
						    "operator()",
						    SpecializeMemberDetail(Program, Candidate, Child.Detail, TypeSymbol),
						    CompletionLabel(TypeSymbol->Name)));
						bFound = true;
					}
				}
				if (bFound)
				{
					return;
				}
			}
		}

		template <typename TTypeDecl>
		bool FindConstructorFieldInitializerType(
		    const std::vector<TTypeDecl>& Types,
		    const FCallSite& CallSite,
		    std::string& OutTypeName)
		{
			for (const TTypeDecl& Type : Types)
			{
				for (const FConstructorDecl& Constructor : Type.Constructors)
				{
					for (const FFieldCtorInit& Init : Constructor.FieldInits)
					{
						if (Init.FieldName != CallSite.CalleeName || Init.Location.Line != CallSite.CalleeLine || Init.Location.Column != CallSite.CalleeColumn)
						{
							continue;
						}
						const auto Field = std::find_if(
						    Type.Fields.begin(),
						    Type.Fields.end(),
						    [&](const FFieldDecl& Candidate)
						{
							return Candidate.Name == Init.FieldName;
						});
						if (Field != Type.Fields.end())
						{
							OutTypeName = Field->TypeName;
							return true;
						}
					}
				}
			}
			return false;
		}

		bool FindConstructorFieldInitializerType(
		    const FProgram& Program,
		    const FCallSite& CallSite,
		    std::string& OutTypeName)
		{
			return FindConstructorFieldInitializerType(
			           Program.Structs, CallSite, OutTypeName) ||
			    FindConstructorFieldInitializerType(
			        Program.Classes, CallSite, OutTypeName);
		}

		void RelabelConstructorFieldSignatures(
		    const std::string_view FieldName,
		    std::vector<FSignatureInformation>& Signatures)
		{
			for (FSignatureInformation& Signature : Signatures)
			{
				const std::size_t Open = Signature.Label.find('(');
				if (Open != std::string::npos)
				{
					Signature.Label = std::string(FieldName) + Signature.Label.substr(Open);
				}
			}
		}

		void CollectSignatures(
		    const FProgram& Program,
		    const std::vector<FDocumentSymbol>& Symbols,
		    const FCallSite& CallSite,
		    const std::string& ReceiverType,
		    const std::vector<FSymbolQueryHit>& CalleeHits,
		    const std::vector<std::string>& ArgumentTypeHints,
		    const std::string_view AccessContextType,
		    const FCallableAccessContext* CallableAccessContext,
		    const std::string_view ReferenceModulePath,
		    const std::string_view ActiveNamespace,
		    const FTextPosition ReferencePosition,
		    std::vector<FSignatureInformation>& Out,
		    const VeroScript::IHostBindingCatalog* Bindings = nullptr)
		{
			if (CallSite.bIsMethod)
			{
				std::vector<std::string> Hierarchy;
				CollectTypeHierarchy(Program, ReceiverType, Hierarchy);
				if (CallSite.bInvokesCallResult)
				{
					for (const std::string& TypeName : Hierarchy)
					{
						const FDocumentSymbol* TypeSymbol =
						    FindTypeSymbolInTree(Symbols, TypeName);
						if (!TypeSymbol)
						{
							continue;
						}
						for (const FDocumentSymbol& Child : TypeSymbol->Children)
						{
							if (Child.Kind != EDocumentSymbolKind::Method || !NameMatchesSymbol(Child.Name, CallSite.CalleeName) || !IsHierarchyMemberAccessible(Program, ReceiverType, TypeName, Child.Access, AccessContextType, CallableAccessContext))
							{
								continue;
							}
							std::string CallableType;
							if (TryExtractLeadingFunctionType(
							        SpecializeMemberDetail(Program, TypeName, Child.Detail, TypeSymbol), CallableType))
							{
								Out.push_back(MakeSignature(
								    Child.Name, CallableType, TypeSymbol->Name));
							}
						}
					}
					if (Out.empty() && Bindings)
					{
						for (const std::string& TypeName : Hierarchy)
						{
							const VeroScript::FHostTypeDesc* const HostType =
							    Bindings->FindType(TypeName);
							if (!HostType)
							{
								continue;
							}
							for (const VeroScript::FHostMemberDesc& Member : HostType->Members)
							{
								if (Member.Name != CallSite.CalleeName || (Member.Kind != VeroScript::EHostMemberKind::Method && Member.Kind != VeroScript::EHostMemberKind::StaticMethod) || Member.ReturnType != VeroScript::ENativeType::Function)
								{
									continue;
								}
								const std::string& CallableType =
								    Member.ReturnHostTypeName;
								if (!CallableType.empty())
								{
									Out.push_back(MakeSignature(
									    Member.Name, CallableType, HostType->Name));
								}
							}
							if (!Out.empty())
							{
								break;
							}
						}
					}
					return;
				}
				for (const std::string& TypeName : Hierarchy)
				{
					const FDocumentSymbol* TypeSymbol = FindTypeSymbolInTree(Symbols, TypeName);
					if (!TypeSymbol)
					{
						continue;
					}
					for (const FDocumentSymbol& Child : TypeSymbol->Children)
					{
						if (Child.Kind != EDocumentSymbolKind::Method || !NameMatchesSymbol(Child.Name, CallSite.CalleeName))
						{
							continue;
						}
						if (!IsHierarchyMemberAccessible(
						        Program,
						        ReceiverType,
						        TypeName,
						        Child.Access,
						        AccessContextType,
						        CallableAccessContext))
						{
							continue;
						}
						const std::string Detail = SpecializeMemberDetail(
						    Program, TypeName, Child.Detail, TypeSymbol);
						if (Child.bSoftBuiltin && (Detail.find(" get<V>(") != std::string::npos || Detail.find(" set<V>(") != std::string::npos || Detail.find(" invoke<R>(") != std::string::npos || Detail.find(" invoke_static<R>(") != std::string::npos))
						{
							std::vector<std::pair<std::string, std::string>> MethodBindings;
							if (CallSite.ExplicitTypeArgs.size() == 1)
							{
								MethodBindings.emplace_back(
								    Child.Detail.find("<V>") != std::string::npos ? "V" : "R",
								    CallSite.ExplicitTypeArgs.front());
							}
							Out.push_back(MakeSourceSignature(SpecializeMemberDetail(
							    Program, TypeName, Child.Detail, TypeSymbol, MethodBindings)));
						}
						else
						{
							Out.push_back(MakeSignature(
							    CompletionLabel(Child.Name), Detail,
							    CompletionLabel(TypeSymbol->Name)));
						}
					}
				}
				std::unordered_map<std::string, FInheritedMemberCompletion> SoftMembers;
				AppendSoftContainerMembers(ReceiverType, {}, SoftMembers, &Program);
				const auto Soft = SoftMembers.find(CallSite.CalleeName);
				if (Out.empty() && Soft != SoftMembers.end())
				{
					const std::string& Detail = Soft->second.Detail;
					if ((Detail.find(" get<V>(") != std::string::npos || Detail.find(" set<V>(") != std::string::npos || Detail.find(" invoke<R>(") != std::string::npos || Detail.find(" invoke_static<R>(") != std::string::npos))
					{
						Out.push_back(MakeSpecializedSourceSignature(
						    Detail, CallSite,
						    Detail.find("<V>") != std::string::npos ? "V" : "R"));
					}
					else
					{
						Out.push_back(MakeSignature(
						    Soft->second.Label, Detail, StripGenericArgs(ReceiverType)));
					}
				}
				if (Out.empty() && Bindings)
				{
					for (const std::string& TypeName : Hierarchy)
					{
						const VeroScript::FHostTypeDesc* const HostType =
						    Bindings->FindType(TypeName);
						if (!HostType)
						{
							continue;
						}
						for (const VeroScript::FHostMemberDesc& Member : HostType->Members)
						{
							if (Member.Name != CallSite.CalleeName)
							{
								continue;
							}
							if (Member.Kind != VeroScript::EHostMemberKind::Method && Member.Kind != VeroScript::EHostMemberKind::StaticMethod)
							{
								continue;
							}
							Out.push_back(MakeSignature(
							    Member.Name,
							    VeroScript::FormatHostMemberSignature(
							        Member, HostType->Name),
							    HostType->Name));
						}
						if (!Out.empty())
						{
							break;
						}
					}
				}
				if (Out.empty())
				{
					for (const FFunctionDecl& Function : Program.Functions)
					{
						if (Function.Parameters.empty() || Function.bDeleted || ExtensionMethodLeafName(Function.Name) != CallSite.CalleeName || !ExtensionFunctionIsVisible(Program, Function, ReferenceModulePath, ActiveNamespace, ReferencePosition, Bindings) || !ExtensionReceiverTypeMatches(Function, ReceiverType))
						{
							continue;
						}
						Out.push_back(MakeSignature(
						    CallSite.CalleeName,
						    FormatExtensionMethodDetail(Function),
						    StripGenericArgs(ReceiverType)));
					}
				}
				return;
			}

			const std::string_view FreeCalleeName = CallSite.QualifiedCalleeName.empty()
			    ? std::string_view(CallSite.CalleeName)
			    : std::string_view(CallSite.QualifiedCalleeName);
			const bool bHasUnqualifiedSymbol = std::any_of(Symbols.begin(), Symbols.end(),
			    [&](const FDocumentSymbol& Symbol)
			{
				return Symbol.Kind == EDocumentSymbolKind::Function && CompletionLabel(Symbol.Name) == CallSite.CalleeName;
			});
			const bool bResolvedFunction = !CallSite.ResolvedFunctionNames.empty();
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (!CallSite.bLocalValue && Symbol.Kind == EDocumentSymbolKind::Function && (bResolvedFunction
				        ? std::find(CallSite.ResolvedFunctionNames.begin(), CallSite.ResolvedFunctionNames.end(), Symbol.Name) != CallSite.ResolvedFunctionNames.end()
				        : NameMatchesSymbol(Symbol.Name, FreeCalleeName)))
				{
					const bool bStandardAlias = Symbol.bSoftBuiltin && Symbol.Name.rfind("std.", 0) == 0 && !StandardLibrary::ResolveFunctionName(Symbol.Name).empty();
					// Standard aliases and their canonical functions describe the same
					// callable. Keep the spelling actually used at this call site.
					if ((!bResolvedFunction && !CallSite.QualifiedCalleeName.empty() && CompletionLabel(Symbol.Name) != FreeCalleeName) || (CallSite.QualifiedCalleeName.empty() && bHasUnqualifiedSymbol && bStandardAlias))
					{
						continue;
					}
					if (CallSite.bInvokesCallResult)
					{
						std::string CallableType;
						if (TryExtractLeadingFunctionType(Symbol.Detail, CallableType))
						{
							Out.push_back(MakeSignature(
							    CompletionLabel(Symbol.Name), CallableType, {}));
						}
						continue;
					}
					if (Symbol.bSoftBuiltin && Symbol.Name == "print")
					{
						// The completion/hover symbol has one identity with a
						// multi-line detail; call help exposes structured overloads.
						Out.push_back(MakeSignature("print", "void (any value)", {}));
						Out.push_back(MakeSignature("print", "void (string fmt, ...)", {}));
						continue;
					}
					if (Symbol.bSoftBuiltin && Symbol.Name == "std.concurrent.spawn")
					{
						Out.push_back(MakeSourceSignature(
						    "int std::concurrent::spawn(string name, channel<T> ch)"));
						Out.push_back(MakeSourceSignature(
						    "int std::concurrent::spawn(fn(args...)->void callable, args...)"));
						continue;
					}
					if (Symbol.bSoftBuiltin && Symbol.Name == "std.concurrent.spawn_task")
					{
						Out.push_back(MakeSpecializedSourceSignature(
						    "task<T> std::concurrent::spawn_task<T>(string name)",
						    CallSite, "T"));
						Out.push_back(MakeSpecializedSourceSignature(
						    "task<T> std::concurrent::spawn_task<T>(fn(args...)->T callable, args...)",
						    CallSite, "T"));
						continue;
					}
					if (bStandardAlias)
					{
						// Alias details already contain complete, source-facing signatures.
						// Split overloads instead of prepending an internal dotted name or
						// parsing several parameter lists as one signature.
						std::istringstream Lines(Symbol.Detail);
						std::string Line;
						while (std::getline(Lines, Line))
						{
							const std::size_t End = Line.find_last_not_of(" \t\r;");
							if (End == std::string::npos || Line.find('(') == std::string::npos)
							{
								continue;
							}
							Line.resize(End + 1);
							Out.push_back(Symbol.Name == "std.memory.read" || Symbol.Name == "std.memory.write"
							        ? MakeSpecializedSourceSignature(Line, CallSite, "T")
							        : MakeSourceSignature(Line));
							Out.back().Documentation = Symbol.Documentation;
						}
						continue;
					}
					Out.push_back(MakeSignature(
					    FormatNamespaceQualifiedName(CompletionLabel(Symbol.Name)),
					    SpecializeFunctionDetail(
					        Program,
					        Symbol.Name,
					        CallSite.ExplicitTypeArgs,
					        ArgumentTypeHints,
					        Symbol.Detail),
					    {}));
				}
			}
			if (CallSite.bInvokesCallResult)
			{
				return;
			}

			// Constructors live as children of the type, not top-level Function symbols.
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (!CallSite.bLocalValue && (Symbol.Kind == EDocumentSymbolKind::Struct || Symbol.Kind == EDocumentSymbolKind::Class) && NameMatchesSymbol(Symbol.Name, CallSite.CalleeName))
				{
					for (const FDocumentSymbol& Child : Symbol.Children)
					{
						if (Child.Kind == EDocumentSymbolKind::Constructor)
						{
							const std::string TypeName =
							    CallSite.InstantiatedTypeName.empty()
							    ? CompletionLabel(Symbol.Name)
							    : CallSite.InstantiatedTypeName;
							// Access follows the declaration identity, independent of the
							// alias/instantiated spelling used to display the signature.
							if (!IsMemberAccessibleFromContext(Program, Child.Name, Child.Access,
							        AccessContextType, CallableAccessContext))
							{
								continue;
							}
							Out.push_back(MakeSignature(
							    CompletionLabel(Symbol.Name),
							    SpecializeMemberDetail(
							        Program, TypeName, Child.Detail, &Symbol),
							    {}));
						}
					}
				}
			}

			for (const FSymbolQueryHit& Hit : CalleeHits)
			{
				if (Hit.Name != CallSite.CalleeName && !NameMatchesSymbol(Hit.Name, CallSite.CalleeName))
				{
					continue;
				}
				const std::string TypeName = TypeNameFromDetail(Hit.Detail);
				if (DetailLooksLikeFnType(TypeName))
				{
					Out.push_back(MakeSignature(Hit.Name, TypeName, {}));
					continue;
				}
				AppendOperatorCallSignatures(Program, Symbols, TypeName, Out);
			}
			if (Out.empty() && Bindings && !CallSite.bInvokesCallResult && !CallSite.bLocalValue)
			{
				for (const auto& Annotation : Bindings->GetAnnotations())
				{
					if (!AnnotationNamesEqual(Annotation.Name, CallSite.CalleeName, Annotation.CaseSensitive))
					{
						continue;
					}

					Out.push_back(MakeAnnotationSignature(Annotation));
					break;
				}
			}
		}

		bool IsConcurrentChannelCallee(const std::string_view Name)
		{
			return Name == "spawn" || Name == "channel_send" || Name == "channel_recv" || Name == "channel_close";
		}

		std::optional<std::size_t> ConcurrentChannelArgIndex(const std::string_view CalleeName)
		{
			if (CalleeName == "spawn")
			{
				return 1;
			}
			if (CalleeName == "channel_send" || CalleeName == "channel_recv" || CalleeName == "channel_close")
			{
				return 0;
			}
			return std::nullopt;
		}

		bool IsLexTypeContinuation(const FToken& Token)
		{
			if (Token.Kind == ETokenKind::Identifier || (Token.Kind == ETokenKind::Keyword && Token.Lexeme != "operator"))
			{
				return true;
			}
			return Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "<" || Token.Lexeme == ">" || Token.Lexeme == "," || Token.Lexeme == "." || Token.Lexeme == "::");
		}

		void NormalizeTypeQualification(std::string& TypeName)
		{
			for (std::size_t Offset = 0;
			    (Offset = TypeName.find("::", Offset)) != std::string::npos;)
			{
				TypeName.replace(Offset, 2, ".");
				++Offset;
			}
		}

		bool TryLexBindingTypeName(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const std::string_view BindingName,
		    std::string& OutTypeName)
		{
			if (BindingName.empty())
			{
				return false;
			}
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Identifier || Token.Lexeme != BindingName)
				{
					continue;
				}
				if (Index == 0)
				{
					continue;
				}
				std::size_t TypeEnd = Index - 1;
				while (TypeEnd > 0 && Tokens[TypeEnd].Kind == ETokenKind::Keyword && (Tokens[TypeEnd].Lexeme == "ref" || Tokens[TypeEnd].Lexeme == "const"))
				{
					--TypeEnd;
				}
				std::size_t TypeStart = TypeEnd;
				while (TypeStart > 0 && IsLexTypeContinuation(Tokens[TypeStart - 1]))
				{
					--TypeStart;
				}
				if (TypeStart > TypeEnd)
				{
					continue;
				}
				std::string TypeName;
				for (std::size_t TokenIndex = TypeStart; TokenIndex <= TypeEnd; ++TokenIndex)
				{
					const std::string_view Lexeme = Tokens[TokenIndex].Lexeme;
					// Match parser normalization: source `Ns::Type` and `Ns.Type`
					// both use dotted qualified names in AST / symbol tables.
					if (Lexeme == "::")
					{
						TypeName.push_back('.');
					}
					else
					{
						TypeName.append(Lexeme.data(), Lexeme.size());
					}
				}
				if (TypeName.empty())
				{
					continue;
				}
				NormalizeTypeQualification(TypeName);
				OutTypeName = std::move(TypeName);
				return true;
			}
			return false;
		}

		bool TryExtractCallArgIdentifier(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FCallSite& CallSite,
		    const std::size_t ArgIndex,
		    std::string& OutIdentifier,
		    FTextPosition& OutUsePosition)
		{
			OutIdentifier.clear();
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return false;
			}

			std::size_t CalleeIndex = Tokens.size();
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::Identifier && Token.Lexeme == CallSite.CalleeName && Token.Location.Line == CallSite.CalleeLine && Token.Location.Column == CallSite.CalleeColumn)
				{
					CalleeIndex = Index;
					break;
				}
			}
			if (CalleeIndex >= Tokens.size())
			{
				return false;
			}

			std::size_t OpenIndex = CalleeIndex + 1;
			if (OpenIndex < Tokens.size() && TokenIsSymbol(Tokens[OpenIndex], "<"))
			{
				int AngleDepth = 1;
				++OpenIndex;
				while (OpenIndex < Tokens.size() && AngleDepth > 0)
				{
					if (TokenIsSymbol(Tokens[OpenIndex], "<"))
					{
						++AngleDepth;
					}
					else if (TypeArgumentCloseCount(Tokens[OpenIndex]) != 0)
					{
						AngleDepth -= TypeArgumentCloseCount(Tokens[OpenIndex]);
					}
					++OpenIndex;
				}
			}
			if (OpenIndex >= Tokens.size() || !TokenIsSymbol(Tokens[OpenIndex], "("))
			{
				return false;
			}

			std::size_t CurrentArg = 0;
			std::size_t SegmentStart = OpenIndex + 1;
			int Depth = 1;
			for (std::size_t Index = OpenIndex + 1; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (TokenIsSymbol(Token, "("))
				{
					++Depth;
					continue;
				}
				if (TokenIsSymbol(Token, ")"))
				{
					--Depth;
					if (Depth == 0)
					{
						if (CurrentArg == ArgIndex)
						{
							for (std::size_t Scan = SegmentStart; Scan < Index; ++Scan)
							{
								if (Tokens[Scan].Kind == ETokenKind::Identifier)
								{
									OutIdentifier.assign(
									    Tokens[Scan].Lexeme.data(),
									    Tokens[Scan].Lexeme.size());
									OutUsePosition.Line = Tokens[Scan].Location.Line;
									OutUsePosition.Column = Tokens[Scan].Location.Column;
									return true;
								}
							}
						}
						break;
					}
					continue;
				}
				if (Depth == 1 && TokenIsSymbol(Token, ","))
				{
					if (CurrentArg == ArgIndex)
					{
						for (std::size_t Scan = SegmentStart; Scan < Index; ++Scan)
						{
							if (Tokens[Scan].Kind == ETokenKind::Identifier)
							{
								OutIdentifier.assign(
								    Tokens[Scan].Lexeme.data(),
								    Tokens[Scan].Lexeme.size());
								OutUsePosition.Line = Tokens[Scan].Location.Line;
								OutUsePosition.Column = Tokens[Scan].Location.Column;
								return true;
							}
						}
						return false;
					}
					++CurrentArg;
					SegmentStart = Index + 1;
				}
			}
			return false;
		}

		bool StartsWithTaskCallableArgument(
		    const FProgram& Program,
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FCallSite& CallSite,
		    const FTextPosition CursorPosition)
		{
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Kind != ETokenKind::Identifier || Tokens[Index].Lexeme != CallSite.CalleeName || Tokens[Index].Location.Line != CallSite.CalleeLine || Tokens[Index].Location.Column != CallSite.CalleeColumn)
				{
					continue;
				}
				std::size_t Open = Index + 1;
				if (Open < Tokens.size() && TokenIsSymbol(Tokens[Open], "<"))
				{
					int Depth = 1;
					while (++Open < Tokens.size() && Depth > 0)
					{
						if (TokenIsSymbol(Tokens[Open], "<"))
						{
							++Depth;
						}
						else if (TypeArgumentCloseCount(Tokens[Open]) != 0)
						{
							Depth -= TypeArgumentCloseCount(Tokens[Open]);
						}
					}
				}
				if (Open >= Tokens.size() || !TokenIsSymbol(Tokens[Open], "(") || Open + 1 >= Tokens.size())
				{
					return false;
				}
				const FToken& First = Tokens[Open + 1];
				if (ComparePosition(
				        First.Location.Line, First.Location.Column, CursorPosition) >= 0)
				{
					return false;
				}
				if (TokenIsSymbol(First, "["))
				{
					return true;
				}
				if (First.Kind != ETokenKind::Identifier || Open + 3 >= Tokens.size() || (!TokenIsSymbol(Tokens[Open + 2], "::") && !TokenIsSymbol(Tokens[Open + 2], ".")) || Tokens[Open + 3].Kind != ETokenKind::Identifier)
				{
					return false;
				}
				if (ComparePosition(
				        Tokens[Open + 3].Location.Line,
				        Tokens[Open + 3].Location.Column,
				        CursorPosition) >= 0)
				{
					return false;
				}
				const std::string_view TypeName = First.Lexeme;
				const std::string_view MethodName = Tokens[Open + 3].Lexeme;
				const auto HasStaticMethod = [&](const auto& Types)
				{
					for (const auto& Type : Types)
					{
						if (!NameMatchesSymbol(Type.Name, TypeName))
						{
							continue;
						}
						for (const FMethodDecl& Method : Type.Methods)
						{
							if (Method.bIsStatic && Method.Name == MethodName)
							{
								return true;
							}
						}
					}
					return false;
				};
				return HasStaticMethod(Program.Classes) || HasStaticMethod(Program.Structs) || HasStaticMethod(Program.Interfaces);
			}
			return false;
		}

		bool ParameterLabelMentionsType(
		    const std::string_view Label,
		    const std::string_view TypeName)
		{
			if (TypeName.empty() || Label.empty())
			{
				return false;
			}
			std::string_view Rest = Label;
			auto StripPrefix = [&](const std::string_view Prefix)
			{
				if (Rest.size() >= Prefix.size() && Rest.compare(0, Prefix.size(), Prefix) == 0)
				{
					Rest.remove_prefix(Prefix.size());
					return true;
				}
				return false;
			};
			(void)StripPrefix("ref ");
			(void)StripPrefix("const ");
			(void)StripPrefix("ref ");
			const auto MatchesPrefix = [Rest](const std::string_view Candidate)
			{
				if (Rest.size() < Candidate.size() || Rest.compare(0, Candidate.size(), Candidate) != 0)
				{
					return false;
				}
				if (Rest.size() == Candidate.size())
				{
					return true;
				}
				if (Candidate == "unsigned" && Rest.size() > Candidate.size() && Rest[Candidate.size()] == ' ')
				{
					std::string_view Tail = Rest.substr(Candidate.size() + 1);
					if (Tail.rfind("long", 0) == 0 || Tail.rfind("short", 0) == 0 || Tail.rfind("char", 0) == 0)
					{
						return false;
					}
				}
				const unsigned char Next = static_cast<unsigned char>(Rest[Candidate.size()]);
				return !(std::isalnum(Next) != 0 || Next == '_');
			};
			if (MatchesPrefix(TypeName))
			{
				return true;
			}

			const auto MatchesAny = [&MatchesPrefix](
			                            const std::initializer_list<std::string_view> Candidates)
			{
				for (const std::string_view Candidate : Candidates)
				{
					if (MatchesPrefix(Candidate))
					{
						return true;
					}
				}
				return false;
			};
			if (TypeName == "int" || TypeName == "int32")
			{
				return MatchesAny({"int", "int32"});
			}
			if (TypeName == "unsigned" || TypeName == "unsigned int" || TypeName == "uint32")
			{
				return MatchesAny({"unsigned", "unsigned int", "uint32"});
			}
			if (TypeName == "int64" || TypeName == "long")
			{
				return MatchesAny({"int64", "long"});
			}
			if (TypeName == "uint64" || TypeName == "unsigned long")
			{
				return MatchesAny({"uint64", "unsigned long"});
			}
			if (TypeName == "int8" || TypeName == "char")
			{
				return MatchesAny({"int8", "char"});
			}
			if (TypeName == "uint8" || TypeName == "unsigned char")
			{
				return MatchesAny({"uint8", "unsigned char"});
			}
			if (TypeName == "int16" || TypeName == "short")
			{
				return MatchesAny({"int16", "short"});
			}
			if (TypeName == "uint16" || TypeName == "unsigned short")
			{
				return MatchesAny({"uint16", "unsigned short"});
			}
			return false;
		}

		std::string_view NumericLiteralTypeHint(const FToken& Token)
		{
			const std::string_view Lexeme = Token.Lexeme;
			if (Token.Kind == ETokenKind::FloatLiteral)
			{
				return !Lexeme.empty() && (Lexeme.back() == 'f' || Lexeme.back() == 'F')
				    ? std::string_view("float")
				    : std::string_view("double");
			}
			if (Token.Kind != ETokenKind::IntegerLiteral)
			{
				return {};
			}

			std::string Lower(Lexeme);
			for (char& Character : Lower)
			{
				Character = static_cast<char>(
				    std::tolower(static_cast<unsigned char>(Character)));
			}
			const auto EndsWith = [&Lower](const std::string_view Suffix)
			{
				return Lower.size() >= Suffix.size() && std::string_view(Lower).substr(Lower.size() - Suffix.size()) == Suffix;
			};
			if (EndsWith("u64") || EndsWith("ul") || EndsWith("lu"))
			{
				return "uint64";
			}
			if (EndsWith("i64") || EndsWith("l"))
			{
				return "int64";
			}
			if (EndsWith("u"))
			{
				return "unsigned";
			}
			return "int";
		}

		bool TryExtractCallArgTypeHint(
		    const FProgram& Program,
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FCallSite& CallSite,
		    const std::size_t ArgIndex,
		    const FTextPosition CursorPosition,
		    const FTextPosition AstPosition,
		    std::string& OutTypeName,
		    const FSourceRange* ParsedArgument = nullptr)
		{
			OutTypeName.clear();
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return false;
			}

			const auto InspectArgument = [&](const std::size_t Begin, const std::size_t End)
			{
				for (std::size_t Scan = Begin; Scan < End; ++Scan)
				{
					const FToken& ArgTok = Tokens[Scan];
					// Never infer from an argument's untyped suffix after the caret.
					if (ComparePosition(ArgTok.Location.Line, ArgTok.Location.Column, CursorPosition) >= 0)
					{
						break;
					}
					if (ArgTok.Kind == ETokenKind::StringLiteral || ArgTok.Kind == ETokenKind::WStringLiteral)
					{
						OutTypeName = "string";
						return true;
					}
					if (ArgTok.Kind == ETokenKind::CharacterLiteral)
					{
						OutTypeName = "char";
						return true;
					}
					if (ArgTok.Kind == ETokenKind::IntegerLiteral || ArgTok.Kind == ETokenKind::FloatLiteral)
					{
						OutTypeName = NumericLiteralTypeHint(ArgTok);
						return true;
					}
					if (ArgTok.Kind == ETokenKind::BooleanLiteral)
					{
						OutTypeName = "bool";
						return true;
					}
					if (ArgTok.Kind == ETokenKind::Identifier)
					{
						std::vector<FSymbolQueryHit> Locals;
						VeroScriptSymbolQuery::CollectVisibleLocalsAt(Program, AstPosition, Locals);
						for (const FSymbolQueryHit& Hit : Locals)
						{
							if (Hit.Name == ArgTok.Lexeme)
							{
								OutTypeName = TypeNameFromDetail(Hit.Detail);
								return !OutTypeName.empty();
							}
						}
						return TryLexBindingTypeName(Source, FileLabel, ArgTok.Lexeme, OutTypeName);
					}
				}
				return false;
			};
			if (ParsedArgument)
			{
				const auto At = [&](const auto& Location)
				{
					return std::lower_bound(Tokens.begin(), Tokens.end(), Location,
					    [](const FToken& Token, const auto& Bound)
					{
						return ComparePosition(Token.Location.Line, Token.Location.Column,
						           {Bound.Line, Bound.Column}) < 0;
					});
				};
				return InspectArgument(At(ParsedArgument->Position.Begin) - Tokens.begin(),
				    At(ParsedArgument->Position.End) - Tokens.begin());
			}

			std::size_t CalleeIndex = Tokens.size();
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::Identifier && Token.Lexeme == CallSite.CalleeName && Token.Location.Line == CallSite.CalleeLine && Token.Location.Column == CallSite.CalleeColumn)
				{
					CalleeIndex = Index;
					break;
				}
			}
			if (CalleeIndex >= Tokens.size())
			{
				return false;
			}

			std::size_t OpenIndex = CalleeIndex + 1;
			if (OpenIndex < Tokens.size() && TokenIsSymbol(Tokens[OpenIndex], "<"))
			{
				int AngleDepth = 1;
				++OpenIndex;
				while (OpenIndex < Tokens.size() && AngleDepth > 0)
				{
					if (TokenIsSymbol(Tokens[OpenIndex], "<"))
					{
						++AngleDepth;
					}
					else if (TypeArgumentCloseCount(Tokens[OpenIndex]) != 0)
					{
						AngleDepth -= TypeArgumentCloseCount(Tokens[OpenIndex]);
					}
					++OpenIndex;
				}
			}
			if (OpenIndex >= Tokens.size() || !TokenIsSymbol(Tokens[OpenIndex], "("))
			{
				return false;
			}

			std::size_t CurrentArg = 0;
			std::size_t SegmentStart = OpenIndex + 1;
			int Depth = 1;
			for (std::size_t Index = OpenIndex + 1; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (TokenIsSymbol(Token, "("))
				{
					++Depth;
					continue;
				}
				if (TokenIsSymbol(Token, ")") || (Depth == 1 && TokenIsSymbol(Token, ",")))
				{
					const bool bEndArg =
					    TokenIsSymbol(Token, ",") ? Depth == 1 : (--Depth == 0);
					if (!bEndArg)
					{
						continue;
					}
					if (CurrentArg == ArgIndex)
					{
						return InspectArgument(SegmentStart, Index);
					}
					if (TokenIsSymbol(Token, ","))
					{
						++CurrentArg;
						SegmentStart = Index + 1;
					}
					else
					{
						break;
					}
				}
			}
			return false;
		}

		bool TryResolveBindingChannelElement(
		    const FProgram& Program,
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const std::string_view BindingName,
		    const FTextPosition BindingAstPosition,
		    std::string& OutElementType)
		{
			if (BindingName.empty())
			{
				return false;
			}
			std::string OwnerType;
			std::vector<FSymbolQueryHit> Locals;
			VeroScriptSymbolQuery::CollectVisibleLocalsAt(
			    Program,
			    BindingAstPosition,
			    Locals);
			for (const FSymbolQueryHit& Hit : Locals)
			{
				if (Hit.Name != BindingName)
				{
					continue;
				}
				OwnerType = TypeNameFromDetail(Hit.Detail);
				break;
			}
			if (OwnerType.empty())
			{
				(void)TryLexBindingTypeName(Source, FileLabel, BindingName, OwnerType);
			}
			if (OwnerType.empty())
			{
				return false;
			}
			return Runtime::ParseChannelElementType(OwnerType, OutElementType);
		}

		bool SignatureMatchesChannelElement(
		    const std::string_view Label,
		    const std::string_view ElementType)
		{
			const std::string Needle = "channel<" + std::string(ElementType) + ">";
			return Label.find(Needle) != std::string_view::npos;
		}

		std::uint32_t PickActiveSignature(
		    const std::vector<FSignatureInformation>& Signatures,
		    const std::uint32_t ActiveParameter,
		    const std::string_view ChannelElementType = {},
		    const std::string_view ActiveParamTypeHint = {})
		{
			if (Signatures.empty())
			{
				return 0;
			}
			std::size_t BestIndex = 0;
			int BestScore = std::numeric_limits<int>::max();
			bool bBestVariadic = false;
			for (std::size_t Index = 0; Index < Signatures.size(); ++Index)
			{
				const std::size_t Count = Signatures[Index].Parameters.size();
				bool bVariadic = false;
				if (!Signatures[Index].Parameters.empty())
				{
					const std::string& Last = Signatures[Index].Parameters.back().Label;
					bVariadic = Last.find("...") != std::string::npos;
				}
				int Score = 0;
				if (ActiveParameter == 0 && Count == 0)
				{
					Score = 0;
				}
				else if (Count > ActiveParameter)
				{
					Score = static_cast<int>(Count - ActiveParameter);
				}
				else if (bVariadic && Count > 0 && ActiveParameter + 1 >= Count)
				{
					Score = 50;
				}
				else
				{
					Score = 1000 + static_cast<int>(ActiveParameter + 1 - Count);
				}
				if (!ChannelElementType.empty() && SignatureMatchesChannelElement(Signatures[Index].Label, ChannelElementType))
				{
					Score -= 200;
				}
				if (!ActiveParamTypeHint.empty() && ActiveParameter < Count && ParameterLabelMentionsType(Signatures[Index].Parameters[ActiveParameter].Label, ActiveParamTypeHint))
				{
					Score -= 150;
				}
				// Prefer host varargs (`…`) over string.vs array wrappers when tied.
				if (Score < BestScore || (Score == BestScore && bVariadic && !bBestVariadic))
				{
					BestScore = Score;
					BestIndex = Index;
					bBestVariadic = bVariadic;
				}
			}
			return static_cast<std::uint32_t>(BestIndex);
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
				if ((Index & 1023u) == 0)
				{
					CheckAnalysisCancellation();
				}
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

		std::optional<FExprArgumentView> FindParsedCallArguments(const FProgram& Program,
		    const FCallSite& Site, const FTextPosition Callee, const FTextPosition Open,
		    const FTextPosition Cursor, const std::string_view Source,
		    const std::string_view FileLabel, std::uint32_t& OutParameter)
		{
			std::size_t OpenOffset = 0, CursorOffset = 0;
			if (Site.CloseDelimiter == ']' || Source.empty() || !FindSourceOffset(Source, Open.Line, Open.Column, OpenOffset) || !FindSourceOffset(Source, Cursor.Line, Cursor.Column, CursorOffset) || OpenOffset >= Source.size() || CursorOffset <= OpenOffset || Source[OpenOffset] != (Site.CloseDelimiter == '}' ? '{' : '('))
			{
				return {};
			}
			std::optional<FExprArgumentView> Found;
			bool bAmbiguous = false;
			VisitProgramNodes(Program, [&](const FExpr& Expr)
			{
				CheckAnalysisCancellation();
				if ((Expr.Kind != EExprKind::Call && Expr.Kind != EExprKind::MethodCall) || Expr.Location.Line != Callee.Line || Expr.Location.Column != Callee.Column || (Expr.Location.FileLabel != FileLabel && Expr.Location.FileLabel != Program.EntryModulePath))
				{
					return;
				}
				const auto Valid = [&](const FSourceRange& Range)
				{
					return Range.End > Range.Begin && Range.End <= Source.size() && Range.Position.Begin.FileLabel == Expr.Location.FileLabel && Range.Position.End.FileLabel == Expr.Location.FileLabel;
				};
				if (!Valid(Expr.SourceRange) || OpenOffset < Expr.SourceRange.Begin || OpenOffset >= Expr.SourceRange.End || CursorOffset >= Expr.SourceRange.End)
				{
					return;
				}
				const auto Arguments = Expr.Kind == EExprKind::Call
				    ? static_cast<const FCallExpr&>(Expr).GetArguments()
				    : static_cast<const FMethodCallExpr&>(Expr).GetArguments();
				std::size_t PreviousEnd = OpenOffset + 1;
				for (const FExpr* Argument : Arguments)
				{
					CheckAnalysisCancellation();
					if (!Argument || !Valid(Argument->SourceRange) || Argument->SourceRange.Begin < PreviousEnd || Argument->SourceRange.End >= Expr.SourceRange.End)
					{
						return;
					}
					PreviousEnd = Argument->SourceRange.End;
				}
				if (Found)
				{
					bAmbiguous = true;
				}
				else
				{
					Found = Arguments;
				}
			}, [](const FStmt&)
			{
			});
			if (!Found || bAmbiguous) return {};
			// The parsed arguments identify generic/value arguments and comparisons
			// without guessing from '<'. Only their separating gap needs lexing.
			std::size_t Parameter = 0, GapBegin = OpenOffset + 1;
			for (; Parameter < Found->size(); ++Parameter)
			{
				if ((Parameter & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Range = (*Found)[Parameter]->SourceRange;
				if (CursorOffset < Range.Begin)
				{
					break;
				}
				if (CursorOffset < Range.End)
				{
					OutParameter = static_cast<std::uint32_t>(Parameter);
					return Found;
				}
				GapBegin = Range.End;
			}
			VeroScriptLexer GapLexer(Source.substr(GapBegin, CursorOffset - GapBegin), FileLabel);
			const auto& GapTokens = GapLexer.Tokenize();
			if (GapLexer.HasError()) return {};
			bool bComma = false;
			for (const auto& Token : GapTokens)
			{
				if (Token.Kind == ETokenKind::EndOfFile)
				{
					continue;
				}
				if (bComma || !TokenIsSymbol(Token, ",")) return {};
				bComma = true;
			}
			OutParameter = static_cast<std::uint32_t>(Parameter == 0 ? 0 : Parameter - (bComma ? 0 : 1));
			return Found;
		}

		/**
		 * Detect caret inside a constructor initializer list (`Child(...) : Base(|`).
		 * Closing with `);` alone leaves a ctor without `{ }`, so recover with `) {}`.
		 */
		bool LooksLikeCtorInitializerCall(
		    const std::string_view Source,
		    const std::size_t InsertOffset)
		{
			if (InsertOffset == 0)
			{
				return false;
			}
			int ParenDepth = 0;
			std::size_t Index = InsertOffset;
			while (Index > 0)
			{
				--Index;
				const char Char = Source[Index];
				if (Char == ')')
				{
					++ParenDepth;
					continue;
				}
				if (Char == '(')
				{
					if (ParenDepth > 0)
					{
						--ParenDepth;
						continue;
					}
					// Just before `(`, expect an initializer target. The first target
					// follows `:`; later base/field targets follow a top-level comma.
					std::size_t NameEnd = Index;
					while (NameEnd > 0 && (Source[NameEnd - 1] == ' ' || Source[NameEnd - 1] == '\t' || Source[NameEnd - 1] == '\r' || Source[NameEnd - 1] == '\n'))
					{
						--NameEnd;
					}
					std::size_t NameStart = NameEnd;
					while (NameStart > 0 && (std::isalnum(static_cast<unsigned char>(Source[NameStart - 1])) != 0 || Source[NameStart - 1] == '_'))
					{
						--NameStart;
					}
					if (NameStart >= NameEnd)
					{
						return false;
					}
					std::size_t ColonProbe = NameStart;
					while (ColonProbe > 0 && (Source[ColonProbe - 1] == ' ' || Source[ColonProbe - 1] == '\t' || Source[ColonProbe - 1] == '\r' || Source[ColonProbe - 1] == '\n'))
					{
						--ColonProbe;
					}
					if (ColonProbe > 0 && Source[ColonProbe - 1] == ':')
					{
						return true;
					}
					// A later initializer target must itself follow the list's
					// top-level comma. A comma belonging to an enclosing argument
					// call is rejected below when its unmatched '(' is reached.
					if (ColonProbe == 0 || Source[ColonProbe - 1] != ',')
					{
						return false;
					}
					int CompletedCallDepth = 0;
					for (std::size_t Probe = ColonProbe; Probe > 0;)
					{
						const char Previous = Source[--Probe];
						if (Previous == ')')
						{
							++CompletedCallDepth;
						}
						else if (Previous == '(' && CompletedCallDepth > 0)
						{
							--CompletedCallDepth;
						}
						else if (Previous == '(')
						{
							return false;
						}
						else if (CompletedCallDepth == 0)
						{
							if (Previous == ':')
							{
								return true;
							}
							if (Previous == ';' || Previous == '{' || Previous == '}')
							{
								return false;
							}
						}
					}
					return false;
				}
				if (Char == ';' || Char == '{' || Char == '}')
				{
					return false;
				}
			}
			return false;
		}

		bool TryRecoverInsertCloseCall(
		    const std::string_view Source,
		    const FTextPosition Position,
		    const char CloseDelimiter,
		    std::string& OutRecovered)
		{
			std::size_t InsertOffset = 0;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, InsertOffset))
			{
				return false;
			}

			const bool bCtorInitializer =
			    CloseDelimiter == ')' && LooksLikeCtorInitializerCall(Source, InsertOffset);
			bool bNeedsPlaceholder = false;
			if (InsertOffset > 0)
			{
				std::size_t Probe = InsertOffset;
				while (Probe > 0 && (Source[Probe - 1] == ' ' || Source[Probe - 1] == '\t' || Source[Probe - 1] == '\r' || Source[Probe - 1] == '\n'))
				{
					--Probe;
				}
				bNeedsPlaceholder =
				    Probe > 0 && (Source[Probe - 1] == '(' || Source[Probe - 1] == ',');
			}

			// Editors commonly auto-close the active initializer call. Reuse that
			// delimiter and preserve the remaining initializers and existing body.
			const auto SkipTrivia = [Source](std::size_t Offset)
			{
				while (Offset < Source.size())
				{
					CheckAnalysisCancellation();
					if (std::isspace(static_cast<unsigned char>(Source[Offset])) != 0)
					{
						++Offset;
						continue;
					}
					if (Source.substr(Offset, 2) == "//")
					{
						const auto End = Source.find('\n', Offset + 2);
						Offset = End == std::string_view::npos ? Source.size() : End;
						continue;
					}
					if (Source.substr(Offset, 2) == "/*")
					{
						const auto End = Source.find("*/", Offset + 2);
						Offset = End == std::string_view::npos ? Source.size() : End + 2;
						continue;
					}
					break;
				}
				return Offset;
			};
			const std::size_t ExistingClose = SkipTrivia(InsertOffset);
			if (bCtorInitializer && ExistingClose < Source.size() && Source[ExistingClose] == CloseDelimiter)
			{
				const std::size_t AfterClose = SkipTrivia(ExistingClose + 1);
				OutRecovered.clear();
				OutRecovered.reserve(Source.size() + 4);
				OutRecovered.append(Source.substr(0, InsertOffset));
				if (bNeedsPlaceholder)
				{
					OutRecovered.push_back('0');
				}
				OutRecovered.append(
				    Source.substr(InsertOffset, ExistingClose - InsertOffset + 1));
				if (AfterClose >= Source.size() || (Source[AfterClose] != '{' && Source[AfterClose] != ','))
				{
					OutRecovered.append(" {}");
				}
				OutRecovered.append(Source.substr(ExistingClose + 1));
				return true;
			}
			if (bNeedsPlaceholder && (CloseDelimiter == ')' || CloseDelimiter == ']') && ExistingClose < Source.size() && Source[ExistingClose] == CloseDelimiter)
			{
				// Preserve an editor-inserted closer and the rest of the document.
				// Only the missing argument is needed to parse this closed call.
				OutRecovered.assign(Source.substr(0, InsertOffset));
				OutRecovered.push_back('0');
				OutRecovered.append(Source.substr(InsertOffset));
				return true;
			}

			// Prefer a dummy argument so `foo(1,|` becomes `foo(1,0);`
			// rather than invalid `foo(1,);`.
			const std::string Terminator = ExistingClose < Source.size() && Source[ExistingClose] == ';'
			    ? ""
			    : ";";
			std::string Insertion =
			    bCtorInitializer ? ") {}" : std::string(1, CloseDelimiter) + Terminator;
			if (bNeedsPlaceholder)
			{
				Insertion = bCtorInitializer
				    ? "0) {}"
				    : "0" + std::string(1, CloseDelimiter) + Terminator;
			}

			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + Insertion.size());
			OutRecovered.append(Source.substr(0, InsertOffset));
			OutRecovered += Insertion;
			OutRecovered.append(Source.substr(InsertOffset));
			return true;
		}

		FSignatureHelpResult Fail(FScriptError Error)
		{
			FSignatureHelpResult Result;
			Result.Error = std::move(Error);
			Result.Diagnostics.push_back(Result.Error);
			return Result;
		}

		FSignatureHelpResult FailFromLoader(const VeroScriptModuleLoader& Loader)
		{
			FSignatureHelpResult Result;
			Result.Error = Loader.GetError();
			Result.Diagnostics = Loader.GetDiagnostics();
			if (Result.Diagnostics.empty())
			{
				Result.Diagnostics.push_back(Result.Error);
			}
			return Result;
		}

		FSignatureHelpResult HelpLoaded(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    const std::string_view OriginalSource,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const FSignatureHelpOptions& Options)
		{
			const auto QueryBindings = SourceMetadataQueryBindings(Options.Bindings, Program.SourceMetadata.get());
			const auto ScopedBindings = ScopedQueryBindings(Program, QueryBindings.get(), FileLabel);
			const auto* Bindings = ScopedBindings.get();
			if (!SymbolsResult.bSuccess)
			{
				FSignatureHelpResult Failed = Fail(SymbolsResult.Error);
				if (!SymbolsResult.Diagnostics.empty())
				{
					Failed.Diagnostics = SymbolsResult.Diagnostics;
				}
				return Failed;
			}

			VeroScript::FPreprocessLineMap LineMap;
			std::string PreprocessError;
			if (const auto Snapshot = ProgramPreprocessorLineMap(Program, FileLabel))
			{
				LineMap = *Snapshot;
			}

			else
			{
				BuildModulePreprocessorLineMap(OriginalSource, FileLabel, Options, LineMap, PreprocessError);
			}

			const auto ToAst = [&](const std::uint32_t Line, const std::uint32_t Column) -> FTextPosition
			{
				FTextPosition Ast{Line, Column};
				std::uint32_t AstLine = 0;
				std::uint32_t AstColumn = 0;
				if (MapCaretPhysicalToPreprocessed(
				        LineMap,
				        Line,
				        Column,
				        AstLine,
				        AstColumn))
				{
					Ast.Line = AstLine;
					Ast.Column = AstColumn;
				}
				return Ast;
			};

			FCallSite CallSite;
			if (!FindCallSite(OriginalSource, FileLabel, Position, CallSite) && !FindIndexSite(OriginalSource, FileLabel, Position, CallSite))
			{
				FSignatureHelpResult Result;
				Result.bSuccess = true;
				return Result;
			}

			const auto AstCursor = ToAst(Position.Line, Position.Column);
			const auto ParsedArguments = FindParsedCallArguments(Program, CallSite,
			    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn),
			    ToAst(CallSite.OpenPosition.Line, CallSite.OpenPosition.Column), AstCursor,
			    LineMap.PreprocessedSource, FileLabel, CallSite.ActiveParameter);
			FSignatureHelpResult Result;
			Result.bSuccess = true;
			Result.bActive = true;
			Result.ActiveParameter = CallSite.ActiveParameter;

			std::string ReceiverType;
			std::vector<FSymbolQueryHit> CalleeHits;
			if (CallSite.bIsMethod)
			{
				const auto Member = VeroScriptSymbolQuery::QueryInProgram(
				    Program, SymbolsResult, OriginalSource, FileLabel,
				    {CallSite.CalleeLine, CallSite.CalleeColumn}, Bindings, LineMap);
				if (Member.Definitions.empty())
				{
					Result.bActive = false;
					return Result;
				}
				std::unordered_set<std::string> Functions;
				for (const auto& Hit : Member.Definitions)
				{
					if (Hit.Kind == EDocumentSymbolKind::Class || Hit.Kind == EDocumentSymbolKind::Struct || Hit.Kind == EDocumentSymbolKind::TypeAlias)
					{
						// A qualified constructor follows the resolved type, including
						// aliases. Its spelling alone must not select a standard type.
						CallSite.bIsMethod = false;
					}
					if (Hit.Kind == EDocumentSymbolKind::Function)
					{
						Functions.insert(StripGenericArgs(Hit.Name));
					}
				}
				if (Functions.size() == 1 && !CallSite.ReceiverName.empty())
				{
					const auto Receiver = VeroScriptSymbolQuery::QueryInProgram(Program, SymbolsResult,
					    OriginalSource, FileLabel, {CallSite.ReceiverLine, CallSite.ReceiverColumn}, Bindings, LineMap);
					if (std::any_of(Receiver.Definitions.begin(), Receiver.Definitions.end(),
					        [&](const auto& Hit)
					{
						return Hit.Kind == EDocumentSymbolKind::Namespace && *Functions.begin() == Hit.Name + "." + CallSite.CalleeName;
					}))
					{
						// Namespace qualification uses the same syntax as member access.
						// Let resolved symbols distinguish it from methods and extensions.
						CallSite.bIsMethod = false;
						CallSite.QualifiedCalleeName = *Functions.begin();
					}
				}
			}
			if (CallSite.bIsMethod)
			{
				ReceiverType = VeroScriptSymbolQuery::ResolveMemberReceiverTypeInProgram(
				    Program, SymbolsResult, OriginalSource, FileLabel,
				    {CallSite.CalleeLine, CallSite.CalleeColumn},
				    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn), Bindings);
				if (CallSite.ReceiverName == "this")
				{
					// Locals encode the callable as Type.method (e.g. N.Actor.attach);
					// ResolveEnclosingTypeName returns the type container (N.Actor).
					ReceiverType = VeroScriptSymbolQuery::ResolveEnclosingTypeName(
					    Program,
					    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn));
				}
				else if (CallSite.ReceiverName == "Super")
				{
					// ResolveReceiverTypeInProgram maps Super → first base (after local shadowing).
					ReceiverType = VeroScriptSymbolQuery::ResolveReceiverTypeInProgram(
					    Program,
					    SymbolsResult,
					    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn),
					    "Super");
				}
				if (ReceiverType.empty())
				{
					std::vector<FSymbolQueryHit> ReceiverLocals;
					VeroScriptSymbolQuery::CollectVisibleLocalsAt(
					    Program,
					    ToAst(CallSite.ReceiverLine, CallSite.ReceiverColumn),
					    ReceiverLocals);
					for (const FSymbolQueryHit& Hit : ReceiverLocals)
					{
						if (Hit.Name != CallSite.ReceiverName)
						{
							continue;
						}
						ReceiverType = TypeNameFromDetail(Hit.Detail);
						if (ReceiverType.empty() && (Hit.Kind == EDocumentSymbolKind::Struct || Hit.Kind == EDocumentSymbolKind::Class || Hit.Kind == EDocumentSymbolKind::Interface))
						{
							ReceiverType = CompletionLabel(Hit.Name);
						}
						break;
					}
				}
				if (ReceiverType.empty())
				{
					if (const FDocumentSymbol* TypeSymbol =
					        FindTypeSymbolInTree(SymbolsResult.Symbols, CallSite.ReceiverName))
					{
						ReceiverType = CompletionLabel(TypeSymbol->Name);
					}
				}
				if (ReceiverType.empty())
				{
					// In an incomplete buffer the parser may not retain the local
					// declaration. Recover its type lexically; this also normalizes
					// `Ns::Type` to the symbol-table spelling `Ns.Type`.
					(void)TryLexBindingTypeName(
					    OriginalSource,
					    FileLabel,
					    CallSite.ReceiverName,
					    ReceiverType);
				}
				// Recovered symbol details may preserve source spelling even when the
				// parser succeeded; host and script type tables use dotted linkage.
				NormalizeTypeQualification(ReceiverType);
				if (ReceiverType == "field_info" || ReceiverType == "method_info")
				{
					std::string LexicalType;
					if (TryLexBindingTypeName(
					        OriginalSource, FileLabel, CallSite.ReceiverName, LexicalType) &&
					    LexicalType.rfind(ReceiverType + "<", 0) == 0)
					{
						ReceiverType = std::move(LexicalType);
					}
				}
			}
			else
			{
				// Reuse the same local initializer inference as hover/completion.
				// The source coordinates are physical here; QueryInProgram applies
				// the preprocessor map before inspecting the prepared AST.
				const auto Callee = VeroScriptSymbolQuery::QueryInProgram(Program, SymbolsResult,
				    OriginalSource, FileLabel, {CallSite.CalleeLine, CallSite.CalleeColumn}, Bindings, LineMap);
				if (Callee.bLocalSymbol)
				{
					CalleeHits = Callee.Definitions;
					CallSite.bLocalValue = std::any_of(CalleeHits.begin(), CalleeHits.end(), [](const auto& Hit)
					{
						return Hit.Kind == EDocumentSymbolKind::Variable || Hit.Kind == EDocumentSymbolKind::Constant
						    || Hit.Kind == EDocumentSymbolKind::Field || Hit.Kind == EDocumentSymbolKind::EnumMember;
					});
				}
				else
				{
					for (const auto& Hit : Callee.Definitions)
					{
						if (Hit.Kind == EDocumentSymbolKind::Function && Hit.ContainerName.empty())
						{
							CallSite.ResolvedFunctionNames.push_back(Hit.Name);
						}
					}
				}
			}

			const std::string AccessContext = VeroScriptSymbolQuery::ResolveEnclosingTypeName(
			    Program,
			    ToAst(Position.Line, Position.Column));
			FCallableAccessContext CallableAccessContext;
			const FCallableAccessContext* const CallableAccess =
			    VeroScriptSymbolQuery::ResolveEnclosingCallableAccessContext(
			        Program,
			        ToAst(Position.Line, Position.Column),
			        CallableAccessContext)
			    ? &CallableAccessContext
			    : nullptr;
			const std::string ActiveNamespace =
			    VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(Program, ToAst(Position.Line, Position.Column),
			        FindActiveNamespaceAt(OriginalSource, FileLabel, Position));
			const FTextPosition CallAstPosition =
			    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn);
			constexpr std::size_t MaxInferredArgumentHints = 64;
			const std::size_t RequestedHintCount =
			    static_cast<std::size_t>(CallSite.ActiveParameter) + 1;
			std::vector<std::string> ArgumentTypeHints(
			    RequestedHintCount < MaxInferredArgumentHints
			        ? RequestedHintCount
			        : MaxInferredArgumentHints);
			for (std::size_t Index = 0; Index < ArgumentTypeHints.size(); ++Index)
			{
				if (ParsedArguments && Index >= ParsedArguments->size())
				{
					continue;
				}
				const auto* ParsedRange = ParsedArguments ? &(*ParsedArguments)[Index]->SourceRange : nullptr;
				(void)TryExtractCallArgTypeHint(
				    Program,
				    ParsedRange ? std::string_view(LineMap.PreprocessedSource) : OriginalSource,
				    FileLabel,
				    CallSite,
				    Index,
				    ParsedRange ? AstCursor : Position,
				    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn),
				    ArgumentTypeHints[Index], ParsedRange);
			}

			FCallSite SignatureCallSite = CallSite;
			const bool bPotentialConstructor = std::any_of(SymbolsResult.Symbols.begin(), SymbolsResult.Symbols.end(),
			    [&](const FDocumentSymbol& Symbol)
			{
				if (Symbol.Kind != EDocumentSymbolKind::Class && Symbol.Kind != EDocumentSymbolKind::Struct && Symbol.Kind != EDocumentSymbolKind::TypeAlias)
				{
					return false;
				}
				const std::string Name = CompletionLabel(Symbol.Name);
				const std::size_t Scope = Name.rfind('.');
				return Name == CallSite.CalleeName || Name.substr(Scope == std::string::npos ? 0 : Scope + 1) == CallSite.CalleeName;
			});
			if (bPotentialConstructor && !CallSite.bIsMethod && !CallSite.bInvokesCallResult && CallSite.QualifiedCalleeName.empty())
			{
				// Reuse symbol lookup so namespace imports and aliases follow the
				// same lexical visibility and shadowing rules as hover/completion.
				const auto Callee = VeroScriptSymbolQuery::QueryInProgram(
				    Program, SymbolsResult, OriginalSource, FileLabel,
				    {CallSite.CalleeLine, CallSite.CalleeColumn}, Bindings, LineMap);
				std::unordered_set<std::string> Types;
				for (const auto& Hit : Callee.Definitions)
				{
					if (Hit.Kind == EDocumentSymbolKind::Class || Hit.Kind == EDocumentSymbolKind::Struct || Hit.Kind == EDocumentSymbolKind::TypeAlias)
					{
						const std::string_view StandardType = Hit.bSoftBuiltin
						    ? StandardLibrary::ResolveTypeName(StripGenericArgs(Hit.Name)) : std::string_view{};
						Types.insert(!StandardType.empty() ? std::string(StandardType) : Hit.Kind == EDocumentSymbolKind::TypeAlias
						        ? VeroScriptSymbolQuery::ResolveReceiverTypeInProgram(
						              Program, SymbolsResult, CallAstPosition, Hit.Name)
						        : Hit.Name);
					}
				}
				if (Types.size() == 1 && !Types.begin()->empty())
				{
					SignatureCallSite.CalleeName = StripGenericArgs(*Types.begin());
					if (!SignatureCallSite.InstantiatedTypeName.empty())
					{
						SignatureCallSite.InstantiatedTypeName.replace(0,
						    SignatureCallSite.InstantiatedTypeName.find('<'), SignatureCallSite.CalleeName);
					}
					else
					{
						SignatureCallSite.InstantiatedTypeName = *Types.begin();
					}
				}
			}
			std::string ConstructorFieldName;
			std::string ConstructorFieldType;
			{
				FCallSite AstCallSite = CallSite;
				const FTextPosition AstCallee =
				    ToAst(CallSite.CalleeLine, CallSite.CalleeColumn);
				AstCallSite.CalleeLine = AstCallee.Line;
				AstCallSite.CalleeColumn = AstCallee.Column;
				if (FindConstructorFieldInitializerType(
				        Program, AstCallSite, ConstructorFieldType))
				{
					ConstructorFieldName = CallSite.CalleeName;
					NormalizeTypeQualification(ConstructorFieldType);
					SignatureCallSite.CalleeName = StripGenericArgs(ConstructorFieldType);
					SignatureCallSite.InstantiatedTypeName = ConstructorFieldType;
					SignatureCallSite.ExplicitTypeArgs.clear();
				}
			}

			CollectSignatures(
			    Program,
			    SymbolsResult.Symbols,
			    SignatureCallSite,
			    ReceiverType,
			    ConstructorFieldName.empty() ? CalleeHits : std::vector<FSymbolQueryHit>{},
			    ArgumentTypeHints,
			    AccessContext,
			    CallableAccess,
			    FileLabel,
			    ActiveNamespace,
			    CallAstPosition,
			    Result.Signatures,
			    Bindings);
			if (!ConstructorFieldName.empty())
			{
				RelabelConstructorFieldSignatures(
				    ConstructorFieldName, Result.Signatures);
			}

			if (Result.Signatures.empty() && ConstructorFieldName.empty() && !CallSite.bIsMethod && CalleeHits.empty())
			{
				const std::string ImplicitOwner =
				    VeroScriptSymbolQuery::ResolveImplicitThisTypeName(
				        Program,
				        ToAst(CallSite.CalleeLine, CallSite.CalleeColumn));
				if (!ImplicitOwner.empty())
				{
					FCallSite ImplicitCall = CallSite;
					ImplicitCall.bIsMethod = true;
					ImplicitCall.ReceiverName = "this";
					CollectSignatures(
					    Program,
					    SymbolsResult.Symbols,
					    ImplicitCall,
					    ImplicitOwner,
					    {},
					    ArgumentTypeHints,
					    AccessContext,
					    CallableAccess,
					    FileLabel,
					    ActiveNamespace,
					    CallAstPosition,
					    Result.Signatures,
					    Bindings);
				}
			}

			// Import recovery and host catalogs can surface the same callable
			// more than once. Keep the first complete signature, including its
			// parameter metadata, before selecting the active overload.
			{
				std::unordered_set<std::string> SeenSignatures;
				auto End = std::remove_if(Result.Signatures.begin(), Result.Signatures.end(),
				    [&SeenSignatures](const FSignatureInformation& Signature)
				{
					return !SeenSignatures.insert(Signature.Label).second;
				});
				Result.Signatures.erase(End, Result.Signatures.end());
			}
			if (Result.Signatures.empty())
			{
				Result.bActive = false;
			}
			else
			{
				std::string ChannelElement;
				if (IsConcurrentChannelCallee(CallSite.CalleeName))
				{
					const std::optional<std::size_t> ChannelArgIndex =
					    ConcurrentChannelArgIndex(CallSite.CalleeName);
					if (ChannelArgIndex.has_value() && CallSite.ActiveParameter >= *ChannelArgIndex)
					{
						std::string BindingName;
						FTextPosition BindingPos;
						if (TryExtractCallArgIdentifier(
						        OriginalSource,
						        FileLabel,
						        CallSite,
						        *ChannelArgIndex,
						        BindingName,
						        BindingPos))
						{
							(void)TryResolveBindingChannelElement(
							    Program,
							    OriginalSource,
							    FileLabel,
							    BindingName,
							    ToAst(BindingPos.Line, BindingPos.Column),
							    ChannelElement);
						}
					}
				}
				const std::string ActiveParamTypeHint =
				    CallSite.ActiveParameter < ArgumentTypeHints.size()
				    ? ArgumentTypeHints[CallSite.ActiveParameter]
				    : std::string{};
				Result.ActiveSignature = PickActiveSignature(
				    Result.Signatures,
				    Result.ActiveParameter,
				    ChannelElement,
				    ActiveParamTypeHint);
				if ((CallSite.QualifiedCalleeName == "std.concurrent.spawn" || CallSite.QualifiedCalleeName == "std.concurrent.spawn_task") && Result.Signatures.size() == 2)
				{
					const std::string_view FirstArgumentType = ArgumentTypeHints.empty()
					    ? std::string_view{}
					    : std::string_view(ArgumentTypeHints.front());
					bool bCallableFirstArgument = StartsWithTaskCallableArgument(
					                                  Program, OriginalSource, FileLabel, CallSite, Position) ||
					    Runtime::IsFunctionTypeName(FirstArgumentType) || (FirstArgumentType != "string" && Result.ActiveParameter > 1);
					if (!bCallableFirstArgument && FirstArgumentType != "string")
					{
						std::string FirstArgumentName;
						FTextPosition FirstArgumentPosition;
						if (TryExtractCallArgIdentifier(
						        OriginalSource, FileLabel, CallSite, 0,
						        FirstArgumentName, FirstArgumentPosition) &&
						    ComparePosition(
						        FirstArgumentPosition.Line,
						        FirstArgumentPosition.Column,
						        Position) < 0)
						{
							bCallableFirstArgument = std::any_of(
							    Program.Functions.begin(), Program.Functions.end(),
							    [&](const FFunctionDecl& Function)
							{
								return NameMatchesSymbol(
								    Function.Name, FirstArgumentName);
							});
						}
					}
					Result.ActiveSignature = bCallableFirstArgument ? 1u : 0u;
				}
				const FSignatureInformation& Active =
				    Result.Signatures[Result.ActiveSignature];
				if (!Active.Parameters.empty() && Result.ActiveParameter >= static_cast<std::uint32_t>(Active.Parameters.size()))
				{
					const bool bVariadic =
					    Active.Parameters.back().Label.find("...") != std::string::npos;
					if (!bVariadic)
					{
						Result.ActiveParameter =
						    static_cast<std::uint32_t>(Active.Parameters.size() - 1);
					}
				}
			}
			return Result;
		}

		FSignatureHelpResult HelpWithSource(
		    const std::string_view LoadSource,
		    const std::string_view OriginalSource,
		    const FTextPosition Position,
		    const std::string_view FileLabel,
		    const FSignatureHelpOptions& Options,
		    FScriptError* OutLoadError)
		{
			FDocumentSymbolOptions SymbolOptions;
			SymbolOptions.ImportPaths = Options.ImportPaths;
			SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
			SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			SymbolOptions.Defines = Options.Defines;
			SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
			SymbolOptions.SourceOverlays = Options.SourceOverlays;
			SymbolOptions.bIncludeImports = false;
			SymbolOptions.bIncludeImportedExports = true;
			SymbolOptions.bIncludeSoftBuiltins = true;
			SymbolOptions.Bindings = Options.Bindings;
			SymbolOptions.AnnotationProcessing = Options.AnnotationProcessing;
			SymbolOptions.CompileProcessing = Options.CompileProcessing;
			SymbolOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			SymbolOptions.ModuleEnvironment = Options.ModuleEnvironment;
			SymbolOptions.bIncludeHostBindings = Options.Bindings != nullptr;

			VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(Loader, Options);
			VeroScript::Tooling::ApplyHostTypeNames(Loader, Options.Bindings);
			ApplyDefines(Loader, Options.Defines);
			ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
			for (const FSourceOverlay& Overlay : Options.SourceOverlays)
			{
				Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
			}

			std::unique_ptr<FProgram> Program = Loader.LoadSourceModule(LoadSource, FileLabel);
			if (Program)
			{
				FScriptError Error;
				if (!Detail::PrepareQueryProgram(Program, Options, &Error))
				{
					if (OutLoadError)
					{
						*OutLoadError = Error;
					}
					return Fail(std::move(Error));
				}
			}
			if (Loader.HasError() || !Program)
			{
				if (Loader.HasError())
				{
					if (OutLoadError)
					{
						*OutLoadError = Loader.GetError();
					}
					return FailFromLoader(Loader);
				}
				FScriptError Error;
				FillDiagnostic(
				    Error,
				    "diag.failed_to_load_source_module",
				    FSourceLocation{std::string(FileLabel), 1, 1});
				if (OutLoadError)
				{
					*OutLoadError = Error;
				}
				return Fail(std::move(Error));
			}

			FDocumentSymbolResult SymbolsResult =
			    VeroScriptDocumentSymbols::CollectFromProgram(*Program, SymbolOptions);
			return HelpLoaded(*Program, SymbolsResult, OriginalSource, FileLabel, Position, Options);
		}
	}

	FSignatureInformation MakeAnnotationSignature(const VeroScript::FHostAnnotationDesc& Annotation)
	{
		FSignatureInformation Info;
		Info.Label = Annotation.Name + "(";
		Info.Documentation = Annotation.Documentation;
		for (const auto& Argument : Annotation.Arguments)
		{
			if (!Info.Parameters.empty())
			{
				Info.Label += ", ";
			}

			Info.Label += Argument.Name;
			if (!Argument.Detail.empty())
			{
				Info.Label += ": " + Argument.Detail;
			}

			Info.Parameters.push_back({Argument.Name});
		}
		if (Annotation.Arguments.empty())
		{
			const auto Open = Annotation.Detail.find('(');
			const auto Close = Annotation.Detail.rfind(')');
			if (Open != std::string::npos && Close != std::string::npos && Close > Open + 1)
			{
				Info.Label += Annotation.Detail.substr(Open + 1, Close - Open - 1);
			}

			Info.Label += ')';
			ParseParametersFromDetail(Info.Label, Info.Parameters);
		}
		else
		{
			Info.Label += ')';
		}

		return Info;
	}

	FSignatureHelpResult VeroScriptSignatureHelp::HelpAtSource(const std::string_view Source,
	    const FTextPosition Position, const std::string_view FileLabel,
	    const FSignatureHelpOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FSignatureHelpResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		Options.Bindings = StandardBindings.get();
		// Prefer recovering incomplete call sites before a full module load so broken
		// buffers like `Add(1,\n}` never enter multi-error parser recovery first.
		std::string LoadSource(Source);
		{
			FCallSite Probe;
			std::string Recovered;
			if ((FindCallSite(Source, FileLabel, Position, Probe) || FindIndexSite(Source, FileLabel, Position, Probe)) && TryRecoverInsertCloseCall(Source, Position, Probe.CloseDelimiter, Recovered))
			{
				LoadSource = std::move(Recovered);
			}
		}

		FScriptError LoadError;
		FSignatureHelpResult Result =
		    HelpWithSource(LoadSource, Source, Position, FileLabel, Options, &LoadError);
		if (Result.bSuccess && Result.bActive)
		{
			return Result;
		}
		const FSignatureHelpResult PrimaryFailure = Result;

		// Fallback: if we already recovered, try the original; otherwise try recovery now.
		if (LoadSource != Source)
		{
			FScriptError OriginalError;
			Result = HelpWithSource(
			    Source,
			    Source,
			    Position,
			    FileLabel,
			    Options,
			    &OriginalError);
			if (Result.bSuccess && Result.bActive)
			{
				return Result;
			}
		}
		else
		{
			std::string Recovered;
			FCallSite Probe;
			if ((FindCallSite(Source, FileLabel, Position, Probe) || FindIndexSite(Source, FileLabel, Position, Probe)) && TryRecoverInsertCloseCall(Source, Position, Probe.CloseDelimiter, Recovered))
			{
				FScriptError RecoverError;
				Result = HelpWithSource(
				    Recovered,
				    Source,
				    Position,
				    FileLabel,
				    Options,
				    &RecoverError);
				if (Result.bSuccess && Result.bActive)
				{
					return Result;
				}
			}
		}

		// A syntax error elsewhere must not discard an independently resolved
		// local callable. Reuse symbol-query recovery without inventing a global
		// signature or relaxing a member/qualified receiver.
		FCallSite LocalCall;
		if (FindCallSite(Source, FileLabel, Position, LocalCall) && !LocalCall.bIsMethod && !LocalCall.bInvokesCallResult && LocalCall.QualifiedCalleeName.empty())
		{
			FSymbolQueryOptions QueryOptions;
			QueryOptions.ImportPaths = Options.ImportPaths;
			QueryOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			QueryOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			QueryOptions.bAllowCircularImports = Options.bAllowCircularImports;
			QueryOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			QueryOptions.Defines = Options.Defines;
			QueryOptions.PredefinedMacros = Options.PredefinedMacros;
			QueryOptions.SourceOverlays = Options.SourceOverlays;
			QueryOptions.Bindings = Options.Bindings;
			QueryOptions.AnnotationProcessing = Options.AnnotationProcessing;
			QueryOptions.CompileProcessing = Options.CompileProcessing;
			QueryOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			QueryOptions.ModuleEnvironment = Options.ModuleEnvironment;
			const auto Callee = VeroScriptSymbolQuery::QueryAtSource(Source,
			    {LocalCall.CalleeLine, LocalCall.CalleeColumn}, FileLabel, QueryOptions);
			if (Callee.bSuccess && Callee.bLocalSymbol && Callee.Definitions.size() == 1)
			{
				const auto& Hit = Callee.Definitions.front();
				const auto Type = TypeNameFromDetail(Hit.Detail);
				if ((Hit.Kind == EDocumentSymbolKind::Variable || Hit.Kind == EDocumentSymbolKind::Constant) && DetailLooksLikeFnType(Type))
				{
					FSignatureHelpResult LocalResult;
					LocalResult.bSuccess = LocalResult.bActive = true;
					LocalResult.Signatures.push_back(MakeSignature(LocalCall.CalleeName, Type, {}));
					const auto Count = LocalResult.Signatures.front().Parameters.size();
					LocalResult.ActiveParameter = Count == 0 ? 0 : std::min(LocalCall.ActiveParameter, static_cast<std::uint32_t>(Count - 1));
					return LocalResult;
				}
			}
		}

		return !PrimaryFailure.bSuccess && PrimaryFailure.Diagnostics.empty() && PrimaryFailure.Error.Message.empty()
		    ? Fail(std::move(LoadError))
		    : PrimaryFailure;
	}

	FSignatureHelpResult VeroScriptSignatureHelp::HelpAtFile(
	    const std::string_view FilePath,
	    const FTextPosition Position,
	    const FSignatureHelpOptions& Options)
	{
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			FScriptError Error;
			FillDiagnostic(
			    Error,
			    "diag.cannot_open_script",
			    FSourceLocation{std::string(FilePath), 1, 1},
			    {FilePath});
			return Fail(std::move(Error));
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		return HelpAtSource(Buffer.str(), Position, FilePath, Options);
	}
}
