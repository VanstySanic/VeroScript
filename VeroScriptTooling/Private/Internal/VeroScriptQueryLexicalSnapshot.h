// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptLexer.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptSymbolQuery.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	class FQueryLexicalSnapshot;
	inline thread_local FQueryLexicalSnapshot* ActiveQueryLexicalSnapshot = nullptr;
	inline std::size_t QueryTokenLowerBound(const std::vector<FToken>& Tokens, const FTextPosition Position)
	{
		return static_cast<std::size_t>(std::lower_bound(Tokens.begin(), Tokens.end(), Position,
		                                    [](const FToken& Token, const FTextPosition Point)
		{
			return Token.Location.Line < Point.Line || (Token.Location.Line == Point.Line && Token.Location.Column < Point.Column);
		}) - Tokens.begin());
	}
	inline std::pair<std::size_t, std::size_t> QueryPointTokenRange(
	    const std::vector<FToken>& Tokens, const FTextPosition Position)
	{
		const auto At = QueryTokenLowerBound(Tokens, Position);
		auto Begin = At > 0 ? At - 1 : 0;
		// A failed lexer can leave EOF before the physical caret. Its preceding
		// separator still participates in the original incomplete-member fallback.
		if (At == Tokens.size() && Begin > 0 && Tokens.back().Kind == ETokenKind::EndOfFile)
		{
			--Begin;
		}
		return {Begin, std::min(Tokens.size(), At + 1)};
	}

	/**
	 * Owns one immutable token stream and lexical indexes for a query's source. Source/label
	 * storage and this scope must outlive borrowing FQueryLexer instances. Nested
	 * scopes restore their predecessor; other sources use independent lexers.
	 */
	class FQueryLexicalSnapshot
	{
	public:
		FQueryLexicalSnapshot(std::string_view Source, std::string_view FileLabel)
		    : Source(Source), FileLabel(FileLabel), Lexer(Source, FileLabel),
		      Previous(ActiveQueryLexicalSnapshot)
		{
			FAnalysisStage Stage("lex");
			CheckAnalysisCancellation();
			const auto& Tokens = Lexer.Tokenize();
			for (std::size_t I = 0; I < Tokens.size(); ++I)
			{
				if ((I & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				HasNamespaceKeyword = HasNamespaceKeyword || Tokens[I].Lexeme == "namespace";
				HasUsingKeyword = HasUsingKeyword || Tokens[I].Lexeme == "using";
				HasOperatorKeyword = HasOperatorKeyword || Tokens[I].Lexeme == "operator";
				HasGotoKeyword = HasGotoKeyword || Tokens[I].Lexeme == "goto";
				if (I > 0 && Tokens[I - 1].Lexeme == "using" && Tokens[I].Lexeme == "namespace")
				{
					UsingNamespaces.push_back(I);
				}
			}
			LineStarts.push_back(0);
			for (std::size_t I = 0; I < Source.size(); ++I)
			{
				if ((I & 2047u) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (Source[I] == '\n')
				{
					LineStarts.push_back(I + 1);
				}
			}
			ActiveQueryLexicalSnapshot = this;
		}
		~FQueryLexicalSnapshot()
		{
			ActiveQueryLexicalSnapshot = Previous;
		}
		FQueryLexicalSnapshot(const FQueryLexicalSnapshot&) = delete;
		FQueryLexicalSnapshot& operator=(const FQueryLexicalSnapshot&) = delete;
		bool MatchesSource(const std::string_view Candidate) const
		{
			return Source.data() == Candidate.data() && Source.size() == Candidate.size();
		}
		bool Matches(const std::string_view Candidate, const std::string_view Label) const
		{
			return MatchesSource(Candidate) && FileLabel == Label;
		}
		bool SourceOffset(const FTextPosition Position, std::size_t& Out) const
		{
			if (!Position.Line || !Position.Column || Position.Line > LineStarts.size()) return false;
			const auto Start = LineStarts[Position.Line - 1];
			const auto End = Position.Line < LineStarts.size() ? LineStarts[Position.Line] - 1 : Source.size();
			const auto Column = static_cast<std::size_t>(Position.Column - 1);
			if (Column > End - Start) return false;
			Out = Start + Column;
			return true;
		}
		std::string NamespaceAt(const FTextPosition Position)
		{
			if (!HasNamespaceKeyword) return {};
			if (!NamespacesReady)
			{
				BuildNamespaces();
			}
			const auto Key = PositionKey(Position.Line, Position.Column);
			const auto End = std::upper_bound(Namespaces.begin(), Namespaces.end(), Key,
			    [](const auto Point, const auto& Entry)
			{
				return Point < Entry.first;
			});
			return End == Namespaces.begin() ? std::string{} : std::prev(End)->second;
		}
		std::size_t LastUsingNamespaceBefore(const FTextPosition Position)
		{
			const auto& Tokens = Lexer.Tokenize();
			const auto Point = PositionKey(Position.Line, Position.Column);
			const auto End = std::upper_bound(UsingNamespaces.begin(), UsingNamespaces.end(), Point,
			    [&Tokens](const auto Key, const auto Index)
			{
				return Key < PositionKey(Tokens[Index].Location.Line,
				                 Tokens[Index].Location.Column + static_cast<std::uint32_t>(Tokens[Index].Lexeme.size()));
			});
			return End == UsingNamespaces.begin() ? Tokens.size() : *std::prev(End);
		}

		std::string_view Source;
		std::string_view FileLabel;
		VeroScriptLexer Lexer;
		bool HasNamespaceKeyword = false, HasUsingKeyword = false, HasOperatorKeyword = false,
		     HasGotoKeyword = false;

	private:
		static std::uint64_t PositionKey(const std::uint32_t Line, const std::uint32_t Column)
		{
			return (static_cast<std::uint64_t>(Line) << 32u) | Column;
		}
		void BuildNamespaces()
		{
			const auto& Tokens = Lexer.Tokenize();
			struct FFrame
			{
				bool IsNamespace;
				std::string Previous;
			};
			std::vector<FFrame> Stack;
			std::vector<std::pair<std::uint64_t, std::string>> Prepared;
			std::string Active;
			std::size_t SegmentStart = 0;
			for (std::size_t I = 0; I < Tokens.size(); ++I)
			{
				if ((I & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto& Token = Tokens[I];
				bool Changed = false;
				if (Token.Lexeme == "{")
				{
					std::size_t Keyword = I;
					for (auto J = SegmentStart; J < I; ++J)
					{
						if ((J & 127u) == 0)
						{
							CheckAnalysisCancellation();
						}
						if (Tokens[J].Lexeme == "namespace")
						{
							Keyword = J;
							break;
						}
					}
					FFrame Frame{Keyword != I, Active};
					if (Frame.IsNamespace)
					{
						std::string Declared;
						for (auto J = Keyword + 1; J < I; ++J)
						{
							if ((J & 127u) == 0)
							{
								CheckAnalysisCancellation();
							}
							if (Tokens[J].Kind == ETokenKind::Identifier)
							{
								if (!Declared.empty())
								{
									Declared.push_back('.');
								}
								Declared.append(Tokens[J].Lexeme);
							}
						}
						if (!Declared.empty())
						{
							Active = Active.empty() ? std::move(Declared) : Active + "." + Declared;
							Changed = true;
						}
					}
					Stack.push_back(std::move(Frame));
					SegmentStart = I + 1;
				}
				else if (Token.Lexeme == "}")
				{
					if (!Stack.empty())
					{
						if (Stack.back().IsNamespace)
						{
							Changed = Active != Stack.back().Previous;
							Active = std::move(Stack.back().Previous);
						}
						Stack.pop_back();
					}
					SegmentStart = I + 1;
				}
				else if (Token.Lexeme == ";")
				{
					SegmentStart = I + 1;
				}
				if (Changed)
				{
					Prepared.emplace_back(PositionKey(Token.Location.Line,
					                          Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size())),
					    Active);
				}
			}
			Namespaces = std::move(Prepared);
			NamespacesReady = true;
		}
		FQueryLexicalSnapshot* Previous;
		std::vector<std::size_t> LineStarts;
		std::vector<std::size_t> UsingNamespaces;
		std::vector<std::pair<std::uint64_t, std::string>> Namespaces;
		bool NamespacesReady = false;
	};

	class FQueryLexer
	{
	public:
		FQueryLexer(std::string_view Source, std::string_view FileLabel)
		{
			if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->Matches(Source, FileLabel))
			{
				Lexer = &ActiveQueryLexicalSnapshot->Lexer;
			}
			else
			{
				OwnedLexer = std::make_unique<VeroScriptLexer>(Source, FileLabel);
				Lexer = OwnedLexer.get();
			}
		}
		const std::vector<FToken>& Tokenize()
		{
			return Lexer->Tokenize();
		}
		bool HasError() const
		{
			return Lexer->HasError();
		}

	private:
		std::unique_ptr<VeroScriptLexer> OwnedLexer;
		VeroScriptLexer* Lexer;
	};
}
