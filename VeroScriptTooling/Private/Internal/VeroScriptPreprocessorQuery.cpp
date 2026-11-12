// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreprocessorQuery.h"

#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"
#include "VeroScriptAnalysisControl.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		bool SameDocumentPath(const std::string_view Left, const std::string_view Right)
		{
			return VeroScript::PathUtil::IdentityKey(std::string(Left)) == VeroScript::PathUtil::IdentityKey(std::string(Right));
		}

		void RemapDocumentSymbol(
		    FDocumentSymbol& Symbol,
		    const VeroScript::FPreprocessLineMap& Map,
		    const std::string_view FileLabel,
		    const FPreprocessedRangeMapper& RangeMapper)
		{
			if (!Symbol.bSoftBuiltin && Symbol.StartLine != 0 && (Symbol.FileLabel.empty() || SameDocumentPath(Symbol.FileLabel, FileLabel)))
			{
				std::uint32_t PhysicalLine = 0;
				std::uint32_t PhysicalColumn = 0;
				if (TryRemapPreprocessedToPhysical(
				        Map,
				        Symbol.StartLine,
				        Symbol.StartColumn,
				        PhysicalLine,
				        PhysicalColumn))
				{
					Symbol.StartLine = PhysicalLine;
					Symbol.StartColumn = PhysicalColumn;
				}
				if (TryRemapPreprocessedToPhysical(
				        Map,
				        Symbol.EndLine,
				        Symbol.EndColumn,
				        PhysicalLine,
				        PhysicalColumn))
				{
					Symbol.EndLine = PhysicalLine;
					Symbol.EndColumn = PhysicalColumn;
				}
				if (Symbol.DeclarationRange.StartLine != 0)
				{
					FTextRange Physical;
					Symbol.DeclarationRange = RangeMapper.TryRemap(Symbol.DeclarationRange, Physical)
					    ? Physical
					    : FTextRange{0, 0, 0, 0};
				}
			}

			for (FDocumentSymbol& Child : Symbol.Children)
			{
				RemapDocumentSymbol(Child, Map, FileLabel, RangeMapper);
			}
		}

		void ClearDocumentDeclarationRanges(std::vector<FDocumentSymbol>& Symbols,
		    const std::string_view FileLabel)
		{
			for (auto& Symbol : Symbols)
			{
				CheckAnalysisCancellation();
				if (!Symbol.bSoftBuiltin && (Symbol.FileLabel.empty() || SameDocumentPath(Symbol.FileLabel, FileLabel)))
				{
					Symbol.DeclarationRange = {0, 0, 0, 0};
				}
				ClearDocumentDeclarationRanges(Symbol.Children, FileLabel);
			}
		}

		bool RemapDiagnosticRange(
		    FTextRange& Range,
		    const VeroScript::FPreprocessLineMap& Map)
		{
			std::uint32_t PhysicalLine = 0;
			std::uint32_t PhysicalColumn = 0;
			bool bMapped = false;
			if (TryRemapPreprocessedToPhysical(
			        Map,
			        Range.StartLine,
			        Range.StartColumn,
			        PhysicalLine,
			        PhysicalColumn))
			{
				Range.StartLine = PhysicalLine;
				Range.StartColumn = PhysicalColumn;
				bMapped = true;
			}
			if (TryRemapPreprocessedToPhysical(
			        Map,
			        Range.EndLine,
			        Range.EndColumn,
			        PhysicalLine,
			        PhysicalColumn))
			{
				Range.EndLine = PhysicalLine;
				Range.EndColumn = PhysicalColumn;
				bMapped = true;
			}
			return bMapped;
		}
	}
	bool BuildPreprocessorLineMap(
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const std::string_view LogicalPath,
	    VeroScript::FPreprocessLineMap& OutMap,
	    std::string& OutError)
	{
		OutMap = {};
		OutError.clear();
		if (!LogicalPath.empty())
		{
			FAnalyzeOptions Options;
			Options.Defines = Defines;
			Options.PredefinedMacros = PredefinedMacros;
			return BuildModulePreprocessorLineMap(Source, LogicalPath, Options, OutMap, OutError);
		}
		std::unordered_map<std::string, std::string> Initial;
		if (!BuildDefineMap(Defines, Initial, OutError))
		{
			return false;
		}
		std::string Preprocessed;
		std::uint32_t ErrorLine = 0;
		VeroScript::FPreprocessContext Context;
		Context.LogicalPath = LogicalPath;
		return PreprocessSource(
		    Source,
		    Initial,
		    PredefinedMacros,
		    Context,
		    Preprocessed,
		    OutError,
		    &ErrorLine,
		    &OutMap);
	}

	bool BuildPreprocessorLineMap(
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    VeroScript::FPreprocessLineMap& OutMap,
	    std::string& OutError)
	{
		return BuildPreprocessorLineMap(
		    Source,
		    Defines,
		    PredefinedMacros,
		    {},
		    OutMap,
		    OutError);
	}

	bool BuildPreprocessorLineMap(
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    VeroScript::FPreprocessLineMap& OutMap,
	    std::string& OutError)
	{
		static const std::vector<VeroScript::FPredefinedMacro> Empty;
		return BuildPreprocessorLineMap(Source, Defines, Empty, OutMap, OutError);
	}

	bool IsActiveCodeLine(
	    const VeroScript::FPreprocessLineMap& Map,
	    const std::uint32_t Line)
	{
		if (Line == 0 || Line >= Map.PhysicalLineActiveCode.size())
		{
			// No map / out of range: do not drop hits.
			return true;
		}
		return Map.PhysicalLineActiveCode[Line];
	}

	bool FindActiveMacroDef(const VeroScript::FPreprocessLineMap& Map, const std::string_view Name,
	    VeroScript::FPreprocessMacroDef& OutDef,
	    const std::uint32_t AtPhysicalLine,
	    const std::uint32_t AtPhysicalColumn)
	{
		const VeroScript::FPreprocessMacroDef* Last = nullptr;
		std::size_t QueryOffset = Map.OutByteToPhysical.size();
		bool bHasQueryOffset = false;
		const auto ResolveQueryOffset = [&]()
		{
			if (bHasQueryOffset)
			{
				return;
			}

			bHasQueryOffset = true;
			std::uint32_t Distance = UINT32_MAX;
			for (std::size_t I = 0; I < Map.OutByteToPhysical.size(); ++I)
			{
				const auto& P = Map.OutByteToPhysical[I];
				if (P.Line != AtPhysicalLine)
				{
					continue;
				}

				const auto D =
				    P.Column > AtPhysicalColumn ? P.Column - AtPhysicalColumn : AtPhysicalColumn - P.Column;
				if (D < Distance)
				{
					QueryOffset = I;
					Distance = D;
					if (D == 0)
					{
						break;
					}
				}
			}
		};
		for (const VeroScript::FPreprocessMacroDef& Macro : Map.ActiveMacros)
		{
			if (Macro.Name != Name)
			{
				continue;
			}
			// A definition-name query uses its source site, independently of when
			// the definition becomes visible to subsequent emitted tokens.
			if (!Macro.bImported && Macro.bActive && AtPhysicalLine == Macro.Line &&
			    AtPhysicalColumn >= Macro.Column &&
			    AtPhysicalColumn <= Macro.Column + Macro.Name.size())
			{
				OutDef = Macro;
				return true;
			}
			// AtPhysicalLine == 0 → end of file (all events apply).
			if (AtPhysicalLine != 0 &&
			    (Macro.ActivationLine > AtPhysicalLine ||
			        (Macro.ActivationLine == AtPhysicalLine && AtPhysicalColumn != 0 &&
			            Macro.ActivationColumn > AtPhysicalColumn)))
			{
				continue;
			}
			if (AtPhysicalLine != 0 && AtPhysicalColumn != 0 && !Map.OutByteToPhysical.empty())
			{
				// Most identifier queries are not macros. Only scan the byte map
				// when a matching, physically applicable macro event needs it.
				ResolveQueryOffset();
				if (Macro.ActivationOffset > QueryOffset)
				{
					continue;
				}
			}
			Last = &Macro;
		}
		if (!Last || !Last->bActive)
		{
			return false;
		}
		OutDef = *Last;
		return true;
	}

	bool MapCaretPhysicalToPreprocessed(
	    const VeroScript::FPreprocessLineMap& Map,
	    const std::uint32_t PhysicalLine,
	    const std::uint32_t PhysicalColumn,
	    std::uint32_t& OutPreprocessedLine,
	    std::uint32_t& OutPreprocessedColumn)
	{
		return VeroScript::MapPhysicalToPreprocessed(
		    Map,
		    PhysicalLine,
		    PhysicalColumn,
		    OutPreprocessedLine,
		    OutPreprocessedColumn);
	}

	bool MapPhysicalLineToPreprocessedLine(
	    const VeroScript::FPreprocessLineMap& Map,
	    const std::uint32_t PhysicalLine,
	    std::uint32_t& OutPreprocessedLine)
	{
		return VeroScript::MapPhysicalLineToPreprocessedLine(
		    Map,
		    PhysicalLine,
		    OutPreprocessedLine);
	}

	bool MapPreprocessedLineToPhysicalLine(
	    const VeroScript::FPreprocessLineMap& Map,
	    const std::uint32_t PreprocessedLine,
	    std::uint32_t& OutPhysicalLine)
	{
		return VeroScript::MapPreprocessedLineToPhysicalLine(
		    Map,
		    PreprocessedLine,
		    OutPhysicalLine);
	}

	bool TryRemapPreprocessedToPhysical(
	    const VeroScript::FPreprocessLineMap& Map,
	    const std::uint32_t PreprocessedLine,
	    const std::uint32_t PreprocessedColumn,
	    std::uint32_t& OutPhysicalLine,
	    std::uint32_t& OutPhysicalColumn)
	{
		if (VeroScript::MapPreprocessedToPhysical(
		        Map,
		        PreprocessedLine,
		        PreprocessedColumn,
		        OutPhysicalLine,
		        OutPhysicalColumn))
		{
			return true;
		}
		if (VeroScript::MapPreprocessedLineToPhysicalLine(
		        Map,
		        PreprocessedLine,
		        OutPhysicalLine))
		{
			OutPhysicalColumn = PreprocessedColumn;
			return true;
		}
		return false;
	}

	FPreprocessedRangeMapper::FPreprocessedRangeMapper(const FPreprocessLineMap& InMap,
	    const std::string_view InPhysicalSource) : Map(InMap), PhysicalSource(InPhysicalSource)
	{
		if (Map.OutByteToPhysical.empty() || Map.OutByteToPhysical.size() != Map.PreprocessedSource.size()) return;
		const auto IndexLines = [](const std::string_view Source, std::vector<std::size_t>& Lines)
		{
			Lines.push_back(0);
			for (std::size_t I = 0; I < Source.size(); ++I)
			{
				if ((I & 0x7ffu) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (Source[I] == '\n')
				{
					Lines.push_back(I + 1);
				}
			}
		};
		IndexLines(Map.PreprocessedSource, PreprocessedLines);
		IndexLines(PhysicalSource, PhysicalLines);
	}

	bool FPreprocessedRangeMapper::TryRemap(const FTextRange& Range, FTextRange& OutPhysical) const
	{
		if (Map.OutByteToPhysical.empty() || Map.OutByteToPhysical.size() != Map.PreprocessedSource.size())
		{
			return false;
		}
		const auto OffsetAt = [](const std::string_view Source, const std::vector<std::size_t>& Lines,
		                          const std::uint32_t Line, const std::uint32_t Column, std::size_t& Offset)
		{
			if (Line == 0 || Column == 0 || Line > Lines.size()) return false;
			const auto Start = Lines[Line - 1];
			const auto End = Line < Lines.size() ? Lines[Line] - 1 : Source.size();
			if (static_cast<std::size_t>(Column - 1) > End - Start) return false;
			Offset = Start + Column - 1;
			return true;
		};
		std::size_t Begin = 0, End = 0;
		if (!OffsetAt(Map.PreprocessedSource, PreprocessedLines, Range.StartLine, Range.StartColumn, Begin) || !OffsetAt(Map.PreprocessedSource, PreprocessedLines, Range.EndLine, Range.EndColumn, End) || Begin >= End || End > Map.OutByteToPhysical.size())
		{
			return false;
		}
		const auto Last = End - 1;
		const auto IsNewline = [](const char C)
		{
			return C == '\r' || C == '\n';
		};
		const auto SamePosition = [](const FPreprocessSourcePos& A, const FPreprocessSourcePos& B)
		{
			return A.Line == B.Line && A.Column == B.Column;
		};
		const auto CollapsedOrigin = [&](const std::size_t Offset)
		{
			// Newlines intentionally map to column 1; they are not evidence that
			// the preceding '}' at column 1 came from a macro expansion.
			return (Offset != 0 && !IsNewline(Map.PreprocessedSource[Offset - 1]) && SamePosition(Map.OutByteToPhysical[Offset], Map.OutByteToPhysical[Offset - 1])) || (Offset + 1 < Map.OutByteToPhysical.size() && !IsNewline(Map.PreprocessedSource[Offset + 1]) && SamePosition(Map.OutByteToPhysical[Offset], Map.OutByteToPhysical[Offset + 1]));
		};
		if (IsNewline(Map.PreprocessedSource[Begin]) || IsNewline(Map.PreprocessedSource[Last]) || CollapsedOrigin(Last))
		{
			return false;
		}
		const auto& PhysicalBegin = Map.OutByteToPhysical[Begin];
		const auto& PhysicalLast = Map.OutByteToPhysical[Last];
		// Some producers strip a BOM before assigning first-line byte columns.
		// Without a coordinate convention for that transformation, keep the
		// existing name anchors instead of claiming a precise declaration extent.
		if ((PhysicalBegin.Line == 1 || PhysicalLast.Line == 1) && PhysicalSource.substr(0, 3) == "\xef\xbb\xbf")
		{
			return false;
		}
		std::size_t BeginOffset = 0, LastOffset = 0;
		if (!OffsetAt(PhysicalSource, PhysicalLines, PhysicalBegin.Line, PhysicalBegin.Column, BeginOffset) || !OffsetAt(PhysicalSource, PhysicalLines, PhysicalLast.Line, PhysicalLast.Column, LastOffset) || BeginOffset > LastOffset || LastOffset >= PhysicalSource.size() || PhysicalSource[LastOffset] != Map.PreprocessedSource[Last] || PhysicalLast.Column == UINT32_MAX)
		{
			// A one-byte expansion can also have a collapsed origin. In particular
			// '#define END }' must not end a declaration inside the physical END token.
			return false;
		}
		const auto IsIdentifierByte = [](const unsigned char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C >= 0x80;
		};
		if (IsIdentifierByte(static_cast<unsigned char>(PhysicalSource[LastOffset])))
		{
			auto Next = LastOffset + 1;
			if (Next < PhysicalSource.size() && IsIdentifierByte(static_cast<unsigned char>(PhysicalSource[Next])))
			{
				return false;
			}
			while (Next < PhysicalSource.size())
			{
				const char C = PhysicalSource[Next];
				if (C != ' ' && C != '\t' && C != '\r' && C != '\n' && C != '\v' && C != '\f')
				{
					break;
				}
				if ((Next & 0x7ffu) == 0)
				{
					CheckAnalysisCancellation();
				}
				++Next;
			}
			// A same-spelling one-byte function-like expansion still cannot tell
			// us where its physical invocation ends (for example a(1) -> a).
			if (Next < PhysicalSource.size() && PhysicalSource[Next] == '(') return false;
		}
		OutPhysical = {PhysicalBegin.Line, PhysicalBegin.Column, PhysicalLast.Line, PhysicalLast.Column + 1};
		return true;
	}

	void RemapDocumentSymbolsToPhysical(
	    const std::string_view Source, const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const std::string_view FileLabel, std::vector<FDocumentSymbol>& Symbols,
	    const FPreprocessLineMap* PreparedMap)
	{
		VeroScript::FPreprocessLineMap Map;
		std::string Error;
		if (!PreparedMap &&
		    !BuildPreprocessorLineMap(Source, Defines, PredefinedMacros, FileLabel, Map, Error))
		{
			ClearDocumentDeclarationRanges(Symbols, FileLabel);
			return;
		}
		const auto& EffectiveMap = PreparedMap ? *PreparedMap : Map;
		const FPreprocessedRangeMapper RangeMapper(EffectiveMap, Source);
		for (FDocumentSymbol& Symbol : Symbols)
		{
			RemapDocumentSymbol(Symbol, EffectiveMap, FileLabel, RangeMapper);
		}
	}

	void RemapToolingDiagnosticsToPhysical(
	    const std::string_view Source,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const std::string_view FileLabel,
	    std::vector<FDiagnostic>& Diagnostics)
	{
		RemapAllToolingDiagnostics(
		    Diagnostics,
		    Defines,
		    PredefinedMacros,
		    Source,
		    FileLabel,
		    {});
	}

	void RemapAllToolingDiagnostics(
	    std::vector<FDiagnostic>& Diagnostics,
	    const std::vector<std::string_view>& Defines,
	    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
	    const std::string_view PrimarySource,
	    const std::string_view PrimaryFileLabel,
	    const std::vector<FDocumentOverlay>& SourceOverlays,
	    const FModuleLineMapResolver& Resolver)
	{
		if (Diagnostics.empty())
		{
			return;
		}

		std::unordered_map<std::string, VeroScript::FPreprocessLineMap> MapsByFile;
		const auto ResolveSource = [&](const std::string_view FileLabel, std::string& OutSource) -> bool
		{
			if (SameDocumentPath(FileLabel, PrimaryFileLabel))
			{
				OutSource.assign(PrimarySource.data(), PrimarySource.size());
				return true;
			}
			for (auto I = SourceOverlays.rbegin(); I != SourceOverlays.rend(); ++I)
			{
				const auto& Overlay = *I;
				if (SameDocumentPath(Overlay.Path, FileLabel))
				{
					OutSource = Overlay.Source;
					return true;
				}
			}
			std::ifstream Input(PathUtil::FromUtf8(FileLabel), std::ios::binary);
			if (!Input)
			{
				return false;
			}
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			OutSource = Buffer.str();
			return true;
		};

		const auto LoadMap = [&](const std::string_view FileLabel) -> const VeroScript::FPreprocessLineMap*
		{
			const std::string Key = VeroScript::PathUtil::IdentityKey(std::string(FileLabel));
			const auto Existing = MapsByFile.find(Key);
			if (Existing != MapsByFile.end())
			{
				return &Existing->second;
			}
			std::string FileSource;
			if (!ResolveSource(FileLabel, FileSource))
			{
				return nullptr;
			}
			VeroScript::FPreprocessLineMap Map;
			std::string Error;
			if (!(Resolver ? Resolver(FileSource, FileLabel, Map, Error)
			               : BuildPreprocessorLineMap(FileSource, Defines, PredefinedMacros, FileLabel, Map, Error)))
			{
				return nullptr;
			}
			const auto Inserted = MapsByFile.emplace(Key, std::move(Map));
			return &Inserted.first->second;
		};

		for (FDiagnostic& Diagnostic : Diagnostics)
		{
			if (Diagnostic.bPhysicalLocation)
			{
				continue;
			}

			const std::string FileLabel =
			    Diagnostic.FileLabel.empty() ? std::string(PrimaryFileLabel) : Diagnostic.FileLabel;
			const VeroScript::FPreprocessLineMap* Map = LoadMap(FileLabel);
			if (!Map)
			{
				continue;
			}
			Diagnostic.bPhysicalLocation = RemapDiagnosticRange(Diagnostic.Range, *Map);
		}
		// Recovery diagnostics can outlive the processed syntax that produced them.
		// Bound every publishable position to its actual document, including already
		// physical diagnostics. Cache line bounds once per file rather than per error.
		std::unordered_map<std::string, std::vector<std::uint32_t>> LineBounds;
		for (auto& Diagnostic : Diagnostics)
		{
			CheckAnalysisCancellation();
			const auto Label = Diagnostic.FileLabel.empty() ? PrimaryFileLabel : std::string_view(Diagnostic.FileLabel);
			const auto Key = PathUtil::IdentityKey(std::string(Label));
			auto [It, Inserted] = LineBounds.try_emplace(Key);
			if (Inserted)
			{
				std::string Text;
				if (ResolveSource(Label, Text))
				{
					It->second.push_back(1);
					for (std::size_t I = 0; I < Text.size(); ++I)
					{
						if ((I & 0x7ffu) == 0)
						{
							CheckAnalysisCancellation();
						}

						if (Text[I] == '\n')
						{
							It->second.push_back(1);
						}

						else if (!(Text[I] == '\r' && I + 1 < Text.size() && Text[I + 1] == '\n'))
						{
							++It->second.back();
						}
					}
				}
			}
			const auto& Bounds = It->second;
			if (Bounds.empty())
			{
				continue;
			}

			const auto Clamp = [&](std::uint32_t& Line, std::uint32_t& Column)
			{
				if (Line > Bounds.size())
				{
					Line = static_cast<std::uint32_t>(Bounds.size());
					Column = Bounds.back();
				}
				else
				{
					Line = std::max(Line, 1u);
					Column = std::clamp(Column, 1u, Bounds[Line - 1]);
				}
			};
			auto& Range = Diagnostic.Range;
			Clamp(Range.StartLine, Range.StartColumn);
			Clamp(Range.EndLine, Range.EndColumn);
			if (Range.EndLine < Range.StartLine || (Range.EndLine == Range.StartLine && Range.EndColumn < Range.StartColumn))
			{
				Range.EndLine = Range.StartLine;
				Range.EndColumn = Range.StartColumn;
			}
		}
	}
}
