// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceMetadata.h"
#include "VeroScriptSourceMetadataIO.h"
#include "VeroScriptHostBindingCatalog.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <functional>
#include <stdexcept>

namespace VeroScript
{
	bool MapGeneratedDiagnostic(const FSourceMetadataSnapshot& Metadata, FScriptError& Error)
	{
		if (Error.bPhysicalLocation || !Error.Location.Line ||
		    Error.Location.FileLabel.rfind("veroscript-generated:", 0) != 0)
		{
			return false;
		}
		const auto* Document = Metadata.FindDocument(Error.Location.FileLabel);
		if (!Document || Error.Location.Line > Document->LineOffsets.size())
		{
			return false;
		}
		FSourceRange Range;
		Range.SourceId = Document->Id;
		Range.Begin =
		    Document->LineOffsets[Error.Location.Line - 1] + (Error.Location.Column ? Error.Location.Column - 1 : 0);
		if (!Document->Text.empty())
		{
			Range.Begin = std::min(Document->Text.size(), Range.Begin);
		}
		Range.End = Range.Begin;
		const auto Next = std::upper_bound(Document->Mappings.begin(), Document->Mappings.end(), Range.Begin,
		    [](auto Offset, const auto& Mapping)
		{
			return Offset < Mapping.Begin;
		});
		if (Next == Document->Mappings.begin())
		{
			return false;
		}
		const auto& Mapping = *std::prev(Next);
		if (Range.Begin >= Mapping.End || !Mapping.Origin.GenerationId.empty() ||
		    Mapping.End - Mapping.Begin != Mapping.Origin.Range.End - Mapping.Origin.Range.Begin)
		{
			return false;
		}
		const auto Origins = ResolveSourceOrigins(Metadata, Range);
		if (Origins.empty())
		{
			return false;
		}
		const auto& Position = Origins.back().Range.Position.Begin;
		if (Position.FileLabel.empty() || !Position.Line)
		{
			return false;
		}
		Error.Location = FSourceLocation{Position.FileLabel, Position.Line, Position.Column};
		Error.bPhysicalLocation = true;
		return true;
	}

	std::vector<FAnnotationDefinition> GetAnnotationDefinitions(const IHostBindingCatalog& Catalog)
	{
		std::vector<FAnnotationDefinition> R;
		std::size_t Count = 0;
		auto Append = [](std::string& I, std::string_view V)
		{
			I += std::to_string(V.size()) + ":";
			I.append(V);
		};
		std::function<std::vector<FAnnotationArgumentDefinition>(
		    const std::vector<FHostAnnotationArgumentDesc>&, std::string&, std::size_t)>
		    Copy;
		Copy = [&](const auto& Args, std::string& Identity, std::size_t Depth)
		{
			if (Depth > 64 || Args.size() > 65536 - Count)
			{
				throw std::invalid_argument("annotation description budget exceeded");
			}

			Count += Args.size();
			Append(Identity, std::to_string(Args.size()));
			std::vector<FAnnotationArgumentDefinition> Out;
			for (const auto& A : Args)
			{
				FAnnotationArgumentDefinition D;
				D.Name = A.Name;
				D.Detail = A.Detail;
				D.Documentation = A.Documentation;
				Append(Identity, D.Name);
				Append(Identity, D.Detail);
				Append(Identity, D.Documentation);
				D.Children = Copy(A.Children, Identity, Depth + 1);
				Out.push_back(std::move(D));
			}
			return Out;
		};
		for (const auto& A : Catalog.GetAnnotations())
		{
			FAnnotationDefinition D;
			D.Name = A.Name;
			D.Detail = A.Detail;
			D.Documentation = A.Documentation;
			D.ProviderId = "host";
			D.Targets = A.Targets;
			D.CaseSensitive = A.CaseSensitive;
			std::string Identity;
			Append(Identity, D.Name);
			Append(Identity, D.Detail);
			Append(Identity, D.Documentation);
			Append(Identity, std::to_string(D.Targets));
			Append(Identity, D.CaseSensitive ? "1" : "0");
			D.Arguments = Copy(A.Arguments, Identity, 0);
			std::uint64_t Hash = 14695981039346656037ull;
			for (unsigned char C : Identity)
			{
				Hash ^= C;
				Hash *= 1099511628211ull;
			}
			D.Id = "description:" + D.ProviderId + ":" + D.Name + ":" + std::to_string(Hash);
			R.push_back(std::move(D));
		}
		return R;
	}
	const FSourceDocument* FSourceMetadataSnapshot::FindDocument(const std::string& Id) const
	{
		for (const auto& D : Documents)
		{
			if (D.Id == Id) return &D;
		}
		return nullptr;
	}
	const FSourceNodeMetadata* FSourceMetadataSnapshot::FindNode(const std::string& Id) const
	{
		for (const auto& N : Nodes)
		{
			if (N.Id == Id) return &N;
		}
		return nullptr;
	}
	const FAnnotationDefinition* FSourceMetadataSnapshot::FindDefinition(const std::string& Id) const
	{
		for (const auto& D : Definitions)
		{
			if (D.Id == Id) return &D;
		}
		return nullptr;
	}
	bool FSourceMetadataSnapshot::MatchesAnnotationName(
	    const FDeclarationAnnotation& A, std::string_view Name, bool AllowOptionalAt) const
	{
		if (A.Name.empty()) return false;
		const auto* D = A.DefinitionId.empty() ? nullptr : FindDefinition(A.DefinitionId);
		std::string_view Written = A.Name;
		if (AllowOptionalAt)
		{
			if (!Written.empty() && Written.front() == '@')
			{
				Written.remove_prefix(1);
			}

			if (!Name.empty() && Name.front() == '@')
			{
				Name.remove_prefix(1);
			}
		}
		return AnnotationNamesEqual(Written, Name, !D || D->CaseSensitive);
	}
	bool TryResolveFirstSourceOrigin(const FSourceMetadataSnapshot& Snapshot,
	    const FSourceRange& Range, FSourceOrigin& OutOrigin)
	{
		if (Range.Begin > Range.End) return false;
		const auto* Document = Snapshot.FindDocument(Range.SourceId);
		if (!Document) return false;
		const auto Position = std::upper_bound(Document->Mappings.begin(), Document->Mappings.end(), Range.Begin,
		    [](const auto Offset, const auto& Mapping)
		{
			return Offset < Mapping.Begin;
		});
		if (Position == Document->Mappings.begin()) return false;
		const auto& Mapping = *std::prev(Position);
		if (Range.Begin >= Mapping.End || Mapping.Origin.Range.Begin > Mapping.Origin.Range.End) return false;
		auto Origin = Mapping.Origin;
		if (Origin.GenerationId.empty() && Mapping.End - Mapping.Begin == Origin.Range.End - Origin.Range.Begin)
		{
			Origin.Range.Begin += Range.Begin - Mapping.Begin;
			Origin.Range.End = Origin.Range.Begin + std::min(Range.End - Range.Begin, Mapping.End - Range.Begin);
		}
		if (const auto* Input = Snapshot.FindDocument(Origin.Range.SourceId); Input && !Input->LineOffsets.empty())
		{
			// Host-created snapshots also use this query without a serialization
			// validation pass. A missing zero origin would underflow the line index.
			if (Input->LineOffsets.front() != 0) return false;
			const auto SourcePosition = [&](const std::size_t Offset)
			{
				const auto LineEnd = std::upper_bound(Input->LineOffsets.begin(), Input->LineOffsets.end(), Offset);
				const auto Line = static_cast<std::size_t>(LineEnd - Input->LineOffsets.begin() - 1);
				return FAnnotationSourcePosition{Origin.Range.Position.Begin.FileLabel.empty()
				        ? Input->ModuleId
				        : Origin.Range.Position.Begin.FileLabel,
				    static_cast<std::uint32_t>(Line + 1),
				    static_cast<std::uint32_t>(Offset - Input->LineOffsets[Line] + 1)};
			};
			Origin.Range.Position.Begin = SourcePosition(Origin.Range.Begin);
			Origin.Range.Position.End = SourcePosition(Origin.Range.End);
		}
		OutOrigin = std::move(Origin);
		return true;
	}

	std::vector<FSourceOrigin> ResolveSourceOrigins(const FSourceMetadataSnapshot& S, FSourceRange Range)
	{
		std::vector<FSourceOrigin> Result;
		std::vector<std::string> Visited;
		for (std::size_t Depth = 0; Depth < 64; ++Depth)
		{
			if (std::find(Visited.begin(), Visited.end(), Range.SourceId) != Visited.end())
			{
				break;
			}

			Visited.push_back(Range.SourceId);
			FSourceOrigin Origin;
			if (!TryResolveFirstSourceOrigin(S, Range, Origin))
			{
				break;
			}
			Result.push_back(Origin);
			Range = Origin.Range;
		}
		return Result;
	}

	std::vector<FSourceComment> CollectSourceComments(std::string_view S, std::string_view Id, ESourceActivity Activity)
	{
		std::vector<FSourceComment> R;
		std::size_t I = 0;
		std::uint32_t Line = 1, Column = 1;
		auto Advance = [&]
		{
			if (S[I++] == '\n')
			{
				++Line;
				Column = 1;
			}
			else
			{
				++Column;
			}
		};
		while (I < S.size())
		{
			// VeroScript raw strings end at the first quote; backslashes are literal.
			if (S[I] == 'r' && I + 1 < S.size() && S[I + 1] == '"')
			{
				Advance();
				Advance();
				while (I < S.size() && S[I] != '"')
				{
					Advance();
				}

				if (I < S.size())
				{
					Advance();
				}

				continue;
			}
			if (S[I] == '"' || S[I] == '\'')
			{
				const char Quote = S[I];
				Advance();
				while (I < S.size())
				{
					if (S[I] == '\\')
					{
						Advance();
						if (I < S.size())
						{
							Advance();
						}
					}
					else if (S[I] == Quote)
					{
						Advance();
						break;
					}
					else
					{
						Advance();
					}
				}
				continue;
			}
			if (S[I] != '/' || I + 1 >= S.size() || (S[I + 1] != '/' && S[I + 1] != '*'))
			{
				Advance();
				continue;
			}
			const auto Begin = I;
			const auto BeginLine = Line, BeginColumn = Column;
			const bool Block = S[I + 1] == '*';
			const bool Documentation = I + 2 < S.size() && (S[I + 2] == '!' || S[I + 2] == (Block ? '*' : '/'));
			Advance();
			Advance();
			const auto TextBegin = I + (Documentation ? 1 : 0);
			// In /**/ the documentation marker is also the closing delimiter.
			// Keep it visible to the block scanner so later code is not consumed.
			if (Documentation && I < S.size() &&
			    !(Block && S[I] == '*' && I + 1 < S.size() && S[I + 1] == '/'))
			{
				Advance();
			}

			std::size_t TextEnd = I;
			if (Block)
			{
				while (I < S.size() && !(S[I] == '*' && I + 1 < S.size() && S[I + 1] == '/'))
				{
					Advance();
				}

				TextEnd = I;
				if (I < S.size())
				{
					Advance();
					Advance();
				}
			}
			else
			{
				while (I < S.size() && S[I] != '\r' && S[I] != '\n')
				{
					Advance();
				}

				TextEnd = I;
			}
			FSourceComment C;
			C.ModuleId = std::string(Id);
			C.Id = C.ModuleId + ":comment:" + std::to_string(Begin);
			C.RawText = std::string(S.substr(Begin, I - Begin));
			C.Text = std::string(S.substr(std::min(TextBegin, TextEnd), TextEnd - std::min(TextBegin, TextEnd)));
			C.Kind = Block ? (Documentation ? ESourceCommentKind::DocumentationBlock : ESourceCommentKind::Block)
			               : (Documentation ? ESourceCommentKind::DocumentationLine : ESourceCommentKind::Line);
			C.Activity = Activity;
			C.Range = {std::string(Id), Begin, I, {{std::string(Id), BeginLine, BeginColumn}, {std::string(Id), Line, Column}}};
			C.Origin.Range = C.Range;
			R.push_back(std::move(C));
		}
		return R;
	}
	std::string FormatSourceDocumentation(const std::vector<FSourceComment>& Comments)
	{
		const bool HasDocs = std::any_of(Comments.begin(), Comments.end(),
		    [](const auto& C)
		{
			return C.Association != ECommentAssociation::Body &&
			    C.Association != ECommentAssociation::Scope &&
			    (C.Kind == ESourceCommentKind::DocumentationLine ||
			        C.Kind == ESourceCommentKind::DocumentationBlock);
		});
		std::string Result;
		for (const auto& C : Comments)
		{
			if (C.Association == ECommentAssociation::Body || C.Association == ECommentAssociation::Scope)
			{
				continue;
			}

			if (HasDocs && C.Kind != ESourceCommentKind::DocumentationLine &&
			    C.Kind != ESourceCommentKind::DocumentationBlock)
			{
				continue;
			}

			std::size_t B = 0;
			while (B < C.Text.size())
			{
				auto E = C.Text.find('\n', B);
				if (E == std::string::npos)
				{
					E = C.Text.size();
				}

				auto L = std::string_view(C.Text).substr(B, E - B);
				while (!L.empty() && (L.front() == ' ' || L.front() == '\t' || L.front() == '\r'))
				{
					L.remove_prefix(1);
				}

				if (!L.empty() && L.front() == '*' && C.Kind == ESourceCommentKind::DocumentationBlock)
				{
					L.remove_prefix(1);
					if (!L.empty() && L.front() == ' ')
					{
						L.remove_prefix(1);
					}
				}
				while (!L.empty() && (L.back() == ' ' || L.back() == '\t' || L.back() == '\r'))
				{
					L.remove_suffix(1);
				}

				if (!Result.empty())
				{
					Result += '\n';
				}

				Result.append(L);
				B = E + 1;
			}
		}
		return Result;
	}
	namespace
	{
		bool IntegerMagnitude(const FAnnotationArgument& A, std::uint64_t& V, bool& Negative)
		{
			if (A.Kind != EAnnotationArgumentKind::Scalar || A.ScalarKind != EAnnotationScalarKind::Integer) return false;
			auto S = std::string_view(A.Value);
			Negative = !S.empty() && S.front() == '-';
			if (!S.empty() && (S.front() == '-' || S.front() == '+'))
			{
				S.remove_prefix(1);
			}

			// Match the language's fixed-width and U/L suffix spellings. Removing
			// arbitrary repetitions would also accept malformed metadata such as 1uu.
			if (S.size() >= 3 &&
			    (S.substr(S.size() - 3) == "i64" || S.substr(S.size() - 3) == "I64" ||
			        S.substr(S.size() - 3) == "u64" || S.substr(S.size() - 3) == "U64"))
			{
				S.remove_suffix(3);
			}

			else if (!S.empty())
			{
				const char Last = S.back();
				if (Last == 'u' || Last == 'U')
				{
					S.remove_suffix(1);
					if (!S.empty() && (S.back() == 'l' || S.back() == 'L'))
					{
						S.remove_suffix(1);
					}
				}
				else if (Last == 'l' || Last == 'L')
				{
					S.remove_suffix(1);
					if (!S.empty() && (S.back() == 'u' || S.back() == 'U'))
					{
						S.remove_suffix(1);
					}
				}
			}
			int Base = 10;
			if (S.size() > 2 && S[0] == '0' && (S[1] == 'x' || S[1] == 'X'))
			{
				Base = 16;
				S.remove_prefix(2);
			}
			else if (S.size() > 2 && S[0] == '0' && (S[1] == 'b' || S[1] == 'B'))
			{
				Base = 2;
				S.remove_prefix(2);
			}
			std::string Digits;
			for (char C : S)
			{
				if (C != '_')
				{
					Digits += C;
				}
			}

			const auto P = std::from_chars(Digits.data(), Digits.data() + Digits.size(), V, Base);
			return !Digits.empty() && P.ec == std::errc{} && P.ptr == Digits.data() + Digits.size();
		}
	}
	bool AnnotationArgumentUInt64(const FAnnotationArgument& A, std::uint64_t& V)
	{
		bool N = false;
		std::uint64_t T = 0;
		if (!IntegerMagnitude(A, T, N) || N) return false;
		V = T;
		return true;
	}
	bool AnnotationArgumentInt64(const FAnnotationArgument& A, std::int64_t& V)
	{
		bool N = false;
		std::uint64_t T = 0;
		if (!IntegerMagnitude(A, T, N)) return false;
		const auto Max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
		if (T > Max + (N ? 1u : 0u)) return false;
		V = N ? (T == Max + 1 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(T)) : static_cast<std::int64_t>(T);
		return true;
	}
}
