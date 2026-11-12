// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../Internal/VeroScriptSymbolLookupIndex.h"
#include "../Internal/VeroScriptSourceRangeIndex.h"
#include "../Internal/VeroScriptQueryLexicalSnapshot.h"
#include "../Internal/VeroScriptTypeHierarchy.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Tooling;
	using namespace VeroScript::Tooling::Detail;
	using FQueryPosition = VeroScript::Tooling::Detail::FTextPosition;

	FDocumentSymbol Symbol(std::string Name, EDocumentSymbolKind Kind = EDocumentSymbolKind::Function)
	{
		FDocumentSymbol Result;
		Result.Name = std::move(Name);
		Result.Kind = Kind;
		return Result;
	}
	void Scan(const std::vector<FDocumentSymbol>& Symbols, const std::string_view Name,
	    std::vector<FSymbolLookupIndex::FEntry>& Out, const std::string* Container = nullptr,
	    const bool DeclarationOnly = false)
	{
		for (const auto& Entry : Symbols)
		{
			if (SymbolNameMatchesIdent(Entry.Name, Name))
			{
				Out.push_back({&Entry, Container, DeclarationOnly, !Container});
			}
			Scan(Entry.Children, Name, Out, &Entry.Name, DeclarationOnly || (Entry.Kind == EDocumentSymbolKind::Enum && Entry.bScopedEnum));
		}
	}
	std::size_t ScanRanges(const std::vector<FTextRange>& Ranges, const FQueryPosition Position)
	{
		std::size_t Best = FSourceRangeIndex::NotFound;
		for (std::size_t Index = 0; Index < Ranges.size(); ++Index)
		{
			const auto& Range = Ranges[Index];
			if (Position.Line < Range.StartLine || Position.Line > Range.EndLine || (Position.Line == Range.StartLine && Position.Column < Range.StartColumn) || (Position.Line == Range.EndLine && Position.Column >= Range.EndColumn))
			{
				continue;
			}
			if (Best == FSourceRangeIndex::NotFound)
			{
				Best = Index;
				continue;
			}
			const auto& Old = Ranges[Best];
			const auto Span = Range.EndLine - Range.StartLine, OldSpan = Old.EndLine - Old.StartLine;
			if (Span < OldSpan || (Span == OldSpan && (Range.StartLine > Old.StartLine || (Range.StartLine == Old.StartLine && Range.StartColumn > Old.StartColumn))))
			{
				Best = Index;
			}
		}
		return Best;
	}
}

int main()
{
	using namespace VeroScript;
	using namespace VeroScript::Tooling;
	using namespace VeroScript::Tooling::Detail;
	using FQueryPosition = VeroScript::Tooling::Detail::FTextPosition;
	std::vector<FDocumentSymbol> Symbols{
	    Symbol("N.pick<T>"), Symbol("pick"), Symbol("N.pick<U>"),
	    Symbol("Outer<T>.Inner<U>", EDocumentSymbolKind::Class),
	    Symbol("Choice", EDocumentSymbolKind::Constant), Symbol("Scope", EDocumentSymbolKind::Class),
	    Symbol("Flags", EDocumentSymbolKind::Enum), Symbol("operator+"), Symbol("A::Name")};
	Symbols[5].Children = {Symbol("pick"), Symbol("Choice", EDocumentSymbolKind::Field)};
	Symbols[6].bScopedEnum = true;
	Symbols[6].Children = {Symbol("Choice", EDocumentSymbolKind::EnumMember), Symbol("Nested", EDocumentSymbolKind::Class)};
	Symbols[6].Children[1].Children = {Symbol("pick")};
	FSymbolLookupIndex Index(Symbols);
	for (const auto Name : {"pick", "Choice", "Inner", "Outer", "Flags", "Nested", "operator+", "A::Name", "absent"})
	{
		std::vector<FSymbolLookupIndex::FEntry> Expected, Actual;
		Scan(Symbols, Name, Expected);
		if (const auto* Entries = Index.Find(Name))
		{
			for (const auto& Entry : *Entries)
			{
				if (SymbolNameMatchesIdent(Entry.Symbol->Name, Name))
				{
					Actual.push_back(Entry);
				}
			}
		}
		bool Same = Actual.size() == Expected.size();
		for (std::size_t I = 0; Same && I < Actual.size(); ++I)
		{
			Same = Actual[I].Symbol == Expected[I].Symbol && Actual[I].Container == Expected[I].Container && Actual[I].bDeclarationPositionOnly == Expected[I].bDeclarationPositionOnly && Actual[I].bTopLevel == Expected[I].bTopLevel;
		}
		if (!Same)
		{
			std::cerr << "Symbol index changed name/owner/enum order: " << Name << '\n';
			return EXIT_FAILURE;
		}
	}
	if (FSymbolLookupIndex::Supports("N.pick") || FSymbolLookupIndex::Supports("pick<int>") || FSymbolLookupIndex::Supports("") || Index.Matches(std::vector<FDocumentSymbol>{}))
	{
		return EXIT_FAILURE;
	}

	std::vector<FTextRange> Ranges{{1, 1, 4, 10}, {2, 2, 3, 8}, {2, 3, 3, 8}, {2, 3, 3, 8},
	    {4, 10, 4, 10}, {30, 1, 29, 1}, {90, 1, UINT32_MAX, UINT32_MAX}};
	for (std::uint32_t I = 0; I < 128; ++I)
	{
		const auto Line = (I * 37) % 80 + 1, Column = (I * 13) % 17 + 1;
		Ranges.push_back({Line, Column, Line + I % 8, Column + I % 5 + 1});
	}
	FSourceRangeIndex RangeIndex(Ranges);
	for (std::uint32_t Line = 0; Line <= 100; ++Line)
	{
		for (std::uint32_t Column = 0; Column <= 32; ++Column)
		{
			const FQueryPosition Position{Line, Column};
			if (RangeIndex.Find(Position) != ScanRanges(Ranges, Position))
			{
				std::cerr << "Range index changed boundary/overlap/tie selection: " << Line << ':' << Column << '\n';
				return EXIT_FAILURE;
			}
		}
	}
	if (RangeIndex.Find({UINT32_MAX, UINT32_MAX}) != ScanRanges(Ranges, {UINT32_MAX, UINT32_MAX})) return EXIT_FAILURE;

	const auto* Initial = ActiveSymbolLookupIndex;
	{
		FSymbolLookupScope Outer(Symbols);
		const auto* ActiveOuter = ActiveSymbolLookupIndex;
		try
		{
			std::vector<FDocumentSymbol> Other{Symbol("other")};
			FSymbolLookupScope Inner(Other);
			if (!ActiveSymbolLookupIndex->Matches(Other) || ActiveSymbolLookupIndex->Matches(Symbols)) return EXIT_FAILURE;
			throw std::runtime_error("unwind");
		}
		catch (const std::runtime_error&)
		{
		}
		if (ActiveSymbolLookupIndex != ActiveOuter) return EXIT_FAILURE;
		std::vector<FDocumentSymbol> Many(300, Symbol("repeated"));
		bool Cancelled = false;
		try
		{
			FAnalysisControlScope Control([]
			{
				return true;
			});
			FSymbolLookupScope CancelledScope(Many);
		}
		catch (const FAnalysisCancelled&)
		{
			Cancelled = true;
		}
		if (!Cancelled || ActiveSymbolLookupIndex != ActiveOuter) return EXIT_FAILURE;
	}
	if (ActiveSymbolLookupIndex != Initial) return EXIT_FAILURE;
	{
		const std::string Source = "namespace A {\r\nvoid f(){print(\"\xF0\x9F\x98\x80 } namespace Ghost {\");} /* } */ namespace B::C {int v=0;} }\nnamespace D {int v=0;}\n";
		const auto AOpen = Source.find("namespace A {") + std::string_view("namespace A {").size();
		const auto BOpen = Source.find("namespace B::C {") + std::string_view("namespace B::C {").size();
		const auto BClose = Source.find("int v=0;}") + std::string_view("int v=0;}").size();
		const auto AClose = Source.find(" }\n", BClose) + 2;
		const auto DOpen = Source.find("namespace D {") + std::string_view("namespace D {").size();
		const auto DClose = Source.rfind('}') + 1;
		const std::vector<std::pair<std::size_t, std::string>> Changes{{AOpen, "A"}, {BOpen, "A.B.C"},
		    {BClose, "A"}, {AClose, ""}, {DOpen, "D"}, {DClose, ""}};
		FQueryLexicalSnapshot Snapshot(Source, "namespace-index.vs");
		std::vector<FQueryPosition> Positions;
		std::uint32_t Line = 1, Column = 1;
		for (std::size_t Offset = 0; Offset <= Source.size(); ++Offset)
		{
			Positions.push_back({Line, Column});
			std::size_t Found = Source.size() + 1;
			if (!Snapshot.SourceOffset({Line, Column}, Found) || Found != Offset) return EXIT_FAILURE;
			if (Offset < Source.size())
			{
				if (Source[Offset] == '\n')
				{
					++Line;
					Column = 1;
				}
				else
				{
					++Column;
				}
			}
		}
		const auto& Tokens = Snapshot.Lexer.Tokenize();
		for (std::size_t Offset = Positions.size(); Offset > 0;)
		{
			--Offset;
			std::string Expected;
			for (const auto& Change : Changes)
			{
				if (Change.first <= Offset)
				{
					Expected = Change.second;
				}
			}
			if (Snapshot.NamespaceAt(Positions[Offset]) != Expected)
			{
				std::cerr << "Namespace index changed brace/string/comment boundary at " << Offset << '\n';
				return EXIT_FAILURE;
			}
			std::size_t ExpectedToken = 0;
			while (ExpectedToken < Tokens.size() && (Tokens[ExpectedToken].Location.Line < Positions[Offset].Line || (Tokens[ExpectedToken].Location.Line == Positions[Offset].Line && Tokens[ExpectedToken].Location.Column < Positions[Offset].Column)))
			{
				++ExpectedToken;
			}
			if (QueryTokenLowerBound(Tokens, Positions[Offset]) != ExpectedToken) return EXIT_FAILURE;
			const auto Candidates = QueryPointTokenRange(Tokens, Positions[Offset]);
			const auto CoversWord = [&](const FToken& Token)
			{
				return (Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Keyword || Token.Kind == ETokenKind::BooleanLiteral || Token.Kind == ETokenKind::NullptrLiteral) && Token.Location.Line == Positions[Offset].Line && Token.Location.Column <= Positions[Offset].Column && Token.Location.Column + Token.Lexeme.size() >= Positions[Offset].Column;
			};
			const auto ExpectedWord = std::find_if(Tokens.begin(), Tokens.end(), CoversWord);
			const auto ActualWord = std::find_if(Tokens.begin() + Candidates.first, Tokens.begin() + Candidates.second, CoversWord);
			if ((ExpectedWord == Tokens.end()) != (ActualWord == Tokens.begin() + Candidates.second) || (ExpectedWord != Tokens.end() && ExpectedWord != ActualWord))
			{
				return EXIT_FAILURE;
			}
		}
		std::size_t Ignored = 0;
		if (Snapshot.SourceOffset({0, 1}, Ignored) || Snapshot.SourceOffset({1, 0}, Ignored) || Snapshot.SourceOffset({Line, Column + 1}, Ignored))
		{
			return EXIT_FAILURE;
		}
		const std::string Other = "using namespace A; void run(){goto done;done:;}";
		{
			FQueryLexicalSnapshot Nested(Other, "other.vs");
			if (!Nested.HasUsingKeyword || !Nested.HasGotoKeyword || Nested.HasOperatorKeyword || Nested.MatchesSource(Source))
			{
				return EXIT_FAILURE;
			}
			const auto& UsingTokens = Nested.Lexer.Tokenize();
			if (Nested.LastUsingNamespaceBefore({1, 1}) != UsingTokens.size() || Nested.LastUsingNamespaceBefore({1, 16}) != 1)
			{
				return EXIT_FAILURE;
			}
		}
		if (ActiveQueryLexicalSnapshot != &Snapshot || Snapshot.HasUsingKeyword || Snapshot.HasGotoKeyword)
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Qualified-token batches share one index, while a new document revision
		// must rebuild it and cannot retain the preceding program's identities.
		for (const std::string Namespace : {"First", "Second"})
		{
			std::string Source = "namespace " + Namespace + "{int value=7;}\nusing namespace " + Namespace + ";\nvoid run(){\n";
			for (int Index = 0; Index < 200; ++Index)
			{
				Source += "print(" + Namespace + "::value);\n";
			}
			Source += "}\n";
			int NamespaceBuilds = 0;
			FAnalysisControlScope Control([]
				{
					return false;
				},
				[&](const std::string_view Stage, double)
				{
					if (Stage == "namespace-index")
					{
						++NamespaceBuilds;
					}
				});
			const auto Result = VeroScriptSymbolQuery::SemanticTokensAtSource(Source, "namespace-batch.vs");
			const auto Values = std::count_if(Result.Tokens.begin(), Result.Tokens.end(), [](const auto& Token)
			{
				return Token.Line >= 3 && Token.Kind == EDocumentSymbolKind::Variable;
			});
			if (!Result.bSuccess || NamespaceBuilds != 1 || Values != 200)
			{
				std::cerr << "Qualified semantic batch lost symbols or rebuilt its namespace index per token\n";
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}
