// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptImportOrganizer.h"
#include "VeroScriptImportPath.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptLexer.h"
#include "VeroScriptWorkspace.h"
#include "VeroScriptOverlayUtil.h"
#include <unordered_set>

namespace VeroScript::Tooling::Detail
{
	static std::vector<FTextRange> FindDuplicateSourceImportLines(std::string_view Source)
	{
		// The preprocessor splices physical lines before lexing. Do not reinterpret
		// continued directives/comments using an unspliced token stream.
		if (Source.find("\\\n") != std::string_view::npos || Source.find("\\\r\n") != std::string_view::npos)
		{
			return {};
		}

		VeroScriptLexer Lexer(Source);
		const auto& Tokens = Lexer.Tokenize();
		if (Lexer.HasError()) return {};
		const auto Begin = [&](std::size_t I)
		{
			return static_cast<std::size_t>(Tokens[I].Lexeme.data() - Source.data());
		};
		const auto End = [&](std::size_t I)
		{
			return Begin(I) + Tokens[I].Lexeme.size();
		};
		const auto Space = [&](std::size_t B, std::size_t E)
		{
			for (; B < E; ++B)
			{
				if (Source[B] != ' ' && Source[B] != '\t' && Source[B] != '\r') return false;
			}

			return true;
		};
		std::vector<FTextRange> Result;
		std::unordered_set<std::string> Seen;
		std::size_t Offset = 0, TokenIndex = 0;
		int Nesting = 0;
		for (std::uint32_t Line = 1; Offset < Source.size(); ++Line)
		{
			CheckAnalysisCancellation();
			const auto Newline = Source.find('\n', Offset);
			const auto ContentEnd = Newline == std::string_view::npos ? Source.size() : Newline;
			const auto Next = Newline == std::string_view::npos ? Source.size() : Newline + 1;
			const auto IsToken = [&](std::size_t I)
			{
				return I < Tokens.size() && Tokens[I].Kind != ETokenKind::EndOfFile;
			};
			while (IsToken(TokenIndex) && End(TokenIndex) <= Offset)
			{
				++TokenIndex;
			}

			std::size_t Stop = TokenIndex;
			while (IsToken(Stop) && Begin(Stop) < ContentEnd)
			{
				++Stop;
			}

			const auto First = TokenIndex;
			auto Cursor = First;
			const bool Exported = Cursor < Stop && Tokens[Cursor].Lexeme == "export";
			if (Exported)
			{
				++Cursor;
			}

			bool Import = Nesting == 0 && Cursor < Stop && Tokens[Cursor].Kind == ETokenKind::Keyword &&
				Tokens[Cursor].Lexeme == "import";
			if (Import)
			{
				++Cursor;
			}

			const auto Literal = Cursor;
			Import = Import && Cursor < Stop && Tokens[Cursor].Kind == ETokenKind::StringLiteral;
			if (Import)
			{
				++Cursor;
			}

			if (Cursor < Stop && Tokens[Cursor].Lexeme == ";")
			{
				++Cursor;
			}

			Import = Import && Cursor == Stop && Begin(First) >= Offset && End(Stop - 1) <= ContentEnd;
			std::size_t Prefix = Offset;
			if (Offset == 0 && Source.substr(0, 3) == "\xef\xbb\xbf")
			{
				Prefix = 3;
			}

			if (Import)
			{
				Import = Space(Prefix, Begin(First)) && Space(End(Stop - 1), ContentEnd);
				for (std::size_t I = First + 1; Import && I < Stop; ++I)
				{
					Import = Space(End(I - 1), Begin(I));
				}
			}
			std::string Path;
			Import = Import && DecodeImportPathLiteral(Tokens[Literal].Lexeme, false, Path);
			if (Import)
			{
				Path.insert(Path.begin(), Exported ? 'E' : 'I');
				if (!Seen.insert(std::move(Path)).second)
				{
					Result.push_back({Line, 1, Newline == std::string_view::npos ? Line : Line + 1,
						Newline == std::string_view::npos
							? static_cast<std::uint32_t>(ContentEnd - Offset + 1)
							: 1});
				}

			}
			else if (!Space(Prefix, ContentEnd))
			{
				// Directives, declarations, comments and scope boundaries start another
				// block. Never deduplicate across conditional branches or macro changes.
				Seen.clear();
			}
			// Literal contents never participate in source nesting. Decline imports
			// inside a type/function/namespace or an unfinished expression.
			const bool Directive = First < Stop && Tokens[First].Lexeme == "#";
			for (std::size_t I = First; !Directive && I < Stop; ++I)
			{
				if (Tokens[I].Kind == ETokenKind::Symbol)
				{
					const auto T = Tokens[I].Lexeme;
					if (T == "{" || T == "(" || T == "[")
					{
						++Nesting;
					}

					else if (T == "}" || T == ")" || T == "]")
					{
						if (--Nesting < 0) return {};
					}
				}
			}

			Offset = Next;
		}
		return Result;
	}

	std::vector<FTextRange> FindDuplicateImportLines(std::string_view Source,
		std::string_view Path, const VeroScriptWorkspace& Workspace)
	{
		const auto Candidates = FindDuplicateSourceImportLines(Source);
		if (Candidates.empty()) return {};
		try
		{
			const auto& Options = Workspace.GetOptions();
			VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
			ApplyModuleLoadingOptions(Loader, Options);
			Loader.SetDefines(Options.Defines);
			Loader.SetPredefinedMacros(Options.PredefinedMacros);
			ApplyDocumentOverlays(Loader, Workspace.SnapshotDocuments());
			// Module preprocessing observes local, host and imported public macros.
			// A failed load may still have a usable prefix; never infer later lines.
			const auto Program = Loader.LoadSourceModule(Source, Path);
			const auto* Map = Loader.GetPreprocessLineMap(Path);
			if (!Map || Map->OutByteToPhysical.size() != Map->PreprocessedSource.size()) return {};
			const auto Lines = [](std::string_view Text)
			{
				std::vector<std::size_t> Starts{0};
				for (std::size_t I = 0; I < Text.size(); ++I)
				{
					if ((I & 255u) == 0)
					{
						CheckAnalysisCancellation();
					}

					if (Text[I] == '\n')
					{
						Starts.push_back(I + 1);
					}
				}
				return Starts;
			};
			const auto PhysicalLines = Lines(Source), ProcessedLines = Lines(Map->PreprocessedSource);
			std::unordered_set<std::uint32_t> Confirmed;
			for (const auto& Range : FindDuplicateSourceImportLines(Map->PreprocessedSource))
			{
				CheckAnalysisCancellation();
				const auto Begin = ProcessedLines[Range.StartLine - 1];
				if (Begin >= Map->OutByteToPhysical.size())
				{
					continue;
				}

				const auto Origin = Map->OutByteToPhysical[Begin];
				if (Origin.Column != 1 || Origin.Line == 0 || Origin.Line > PhysicalLines.size())
				{
					continue;
				}

				const auto OriginalBegin = PhysicalLines[Origin.Line - 1];
				const auto LineContent = [](std::string_view Text, std::size_t Start)
				{
					auto Line = Text.substr(Start, Text.find('\n', Start) - Start);
					if (!Line.empty() && Line.back() == '\r')
					{
						Line.remove_suffix(1);
					}

					return Line;
				};
				const auto Content = LineContent(Map->PreprocessedSource, Begin);
				const auto Original = LineContent(Source, OriginalBegin);
				if (Content != Original)
				{
					continue;
				}

				bool Unchanged = true;
				for (std::size_t I = 0; Unchanged && I < Content.size(); ++I)
				{
					if ((I & 255u) == 0)
					{
						CheckAnalysisCancellation();
					}

					const auto P = Map->OutByteToPhysical[Begin + I];
					Unchanged = P.Line == Origin.Line && P.Column == I + 1;
				}
				if (Unchanged)
				{
					Confirmed.insert(Origin.Line);
				}
			}
			std::vector<FTextRange> Result;
			for (const auto& Range : Candidates)
			{
				if (Confirmed.count(Range.StartLine))
				{
					Result.push_back(Range);
				}
			}

			return Result;
		}
		catch (const FAnalysisCancelled&)
		{
			throw;
		}
		catch (const std::exception&)
		{
			return {};
		}
	}
}
