// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptLspStdio.h"
#include "Internal/VeroScriptToolingJson.h"
#include "Internal/VeroScriptLspThread.h"
#include "Internal/VeroScriptTypeDetail.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptHover.h"
#include "Internal/VeroScriptMessageFraming.h"
#include "Internal/VeroScriptSymbolQuery.h"
#include "VeroScriptLexer.h"
#include "Internal/VeroScriptImportPath.h"
#include "Internal/VeroScriptImportOrganizer.h"
#include "Internal/VeroScriptOperatorLex.h"
#include "VeroScriptPath.h"
#include "VeroScriptLocale.h"
#include "VeroScriptVersion.h"
#include "VeroScriptWorkspace.h"
#include "VeroScriptOverlayUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace VeroScript::Tooling
{
	namespace
	{
		using Detail::FJson;
		using Detail::ParseJson;
		using Detail::ToJsonString;

		// ---- file:// URI <-> filesystem path (Windows drive letters + Unix abs paths)

		std::string PercentDecode(const std::string_view In)
		{
			std::string Out;
			Out.reserve(In.size());
			for (std::size_t Index = 0; Index < In.size(); ++Index)
			{
				const char C = In[Index];
				if (C == '%' && Index + 2 < In.size())
				{
					const auto HexValue = [](const char H) -> int
					{
						if (H >= '0' && H <= '9')
						{
							return H - '0';
						}
						if (H >= 'a' && H <= 'f')
						{
							return H - 'a' + 10;
						}
						if (H >= 'A' && H <= 'F')
						{
							return H - 'A' + 10;
						}
						return -1;
					};
					const int High = HexValue(In[Index + 1]);
					const int Low = HexValue(In[Index + 2]);
					if (High >= 0 && Low >= 0)
					{
						Out.push_back(static_cast<char>((High << 4) | Low));
						Index += 2;
						continue;
					}
				}
				Out.push_back(C);
			}
			return Out;
		}

		std::string PercentEncodePath(const std::string_view In)
		{
			static constexpr char Hex[] = "0123456789ABCDEF";
			std::string Out;
			Out.reserve(In.size());
			for (const unsigned char C : In)
			{
				const bool bUnreserved =
				    (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-' || C == '_' || C == '.' || C == '~' || C == '/' || C == ':';
				if (bUnreserved)
				{
					Out.push_back(static_cast<char>(C));
				}
				else
				{
					Out.push_back('%');
					Out.push_back(Hex[(C >> 4) & 0x0Fu]);
					Out.push_back(Hex[C & 0x0Fu]);
				}
			}
			return Out;
		}

		#if defined(_WIN32)
		bool LooksLikeWindowsDriveRoot(const std::string_view Path)
		{
			return Path.size() >= 2 && std::isalpha(static_cast<unsigned char>(Path[0])) && Path[1] == ':';
		}
		#endif

		std::string UriToPath(const std::string& Uri)
		{
			static constexpr std::string_view Prefix = "file://";
			std::string Rest = Uri;
			bool bUncAuthority = false;
			if (Rest.size() >= Prefix.size() && Rest.compare(0, Prefix.size(), Prefix) == 0)
			{
				Rest.erase(0, Prefix.size());
				// A file URI whose authority is non-empty represents a UNC host:
				// `file://server/share/a.vs` -> `//server/share/a.vs`.
				bUncAuthority = !Rest.empty() && Rest.front() != '/';
				#if !defined(_WIN32)
				if (bUncAuthority)
				{
					const std::size_t Slash = Rest.find('/');
					const std::string_view Authority(Rest.data(),
					    Slash == std::string::npos ? Rest.size() : Slash);
					constexpr std::string_view LocalHost = "localhost";
					if (std::equal(Authority.begin(), Authority.end(),
					        LocalHost.begin(), LocalHost.end(), [](const char Left, const char Right)
					{
						return std::tolower(static_cast<unsigned char>(Left)) == Right;
					}))
					{
						// RFC 8089 treats localhost exactly as an empty authority.
						Rest = Slash == std::string::npos ? "/" : Rest.substr(Slash);
						bUncAuthority = false;
					}
				}
				#endif
			}
			Rest = PercentDecode(Rest);
			#if defined(_WIN32)
			for (char& C : Rest)
			{
				if (C == '\\')
				{
					C = '/';
				}
			}
			// "/d:/foo/bar" (leading slash before a Windows drive letter) -> "d:/foo/bar".
			if (!Rest.empty() && Rest.front() == '/' && LooksLikeWindowsDriveRoot(std::string_view(Rest).substr(1)))
			{
				Rest.erase(Rest.begin());
			}
			#endif
			if (bUncAuthority)
			{
				Rest.insert(0, "//");
			}
			return Rest;
		}

		std::string PathToUri(const std::string& Path)
		{
			if (Path.rfind("veroscript-generated:", 0) == 0) return Path;
			if (Path.rfind("veroscript-module:/", 0) == 0)
			{
				return "veroscript-module:" + PercentEncodePath(std::string_view(Path).substr(18));
			}

			std::string Normalized = Path;
			#if defined(_WIN32)
			for (char& C : Normalized)
			{
				if (C == '\\')
				{
					C = '/';
				}
			}
			#endif
			if (Normalized.size() >= 2u && Normalized[0] == '/' && Normalized[1] == '/')
			{
				return "file:" + PercentEncodePath(Normalized);
			}
			std::string Uri = "file://";
			if (Normalized.empty() || Normalized.front() != '/')
			{
				Uri += '/';
			}
			Uri += PercentEncodePath(Normalized);
			return Uri;
		}

		// ---- Tooling <-> LSP coordinate / kind mapping

		std::string_view SourceLine(const std::string_view Source, const std::uint32_t OneBasedLine)
		{
			if (OneBasedLine == 0)
			{
				return {};
			}
			std::size_t Start = 0;
			for (std::uint32_t Line = 1; Line < OneBasedLine; ++Line)
			{
				Start = Source.find('\n', Start);
				if (Start == std::string_view::npos)
				{
					return {};
				}
				++Start;
			}
			std::size_t End = Source.find('\n', Start);
			if (End == std::string_view::npos)
			{
				End = Source.size();
			}
			if (End > Start && Source[End - 1] == '\r')
			{
				--End;
			}
			return Source.substr(Start, End - Start);
		}

		std::pair<std::uint32_t, std::size_t> DecodeUtf8(
		    const std::string_view Text,
		    const std::size_t Offset)
		{
			const auto Lead = static_cast<unsigned char>(Text[Offset]);
			std::size_t Length = 1;
			std::uint32_t CodePoint = Lead;
			if ((Lead & 0xE0u) == 0xC0u)
			{
				Length = 2;
				CodePoint = Lead & 0x1Fu;
			}
			else if ((Lead & 0xF0u) == 0xE0u)
			{
				Length = 3;
				CodePoint = Lead & 0x0Fu;
			}
			else if ((Lead & 0xF8u) == 0xF0u)
			{
				Length = 4;
				CodePoint = Lead & 0x07u;
			}
			if (Offset + Length > Text.size())
			{
				return {Lead, 1};
			}
			for (std::size_t Index = 1; Index < Length; ++Index)
			{
				const auto Continuation = static_cast<unsigned char>(Text[Offset + Index]);
				if ((Continuation & 0xC0u) != 0x80u)
				{
					return {Lead, 1};
				}
				CodePoint = (CodePoint << 6u) | (Continuation & 0x3Fu);
			}
			return {CodePoint, Length};
		}

		std::uint32_t Utf16CharacterToByteColumn(
		    const std::string_view Source,
		    const std::uint32_t OneBasedLine,
		    const std::uint32_t Utf16Character)
		{
			const std::string_view Line = SourceLine(Source, OneBasedLine);
			std::size_t ByteOffset = 0;
			std::uint32_t Units = 0;
			while (ByteOffset < Line.size() && Units < Utf16Character)
			{
				const auto [CodePoint, Length] = DecodeUtf8(Line, ByteOffset);
				const std::uint32_t NextUnits = Units + (CodePoint > 0xFFFFu ? 2u : 1u);
				if (NextUnits > Utf16Character)
				{
					break;
				}
				Units = NextUnits;
				ByteOffset += Length;
			}
			return static_cast<std::uint32_t>(ByteOffset) + 1u;
		}

		std::uint32_t ByteColumnToUtf16Character(
		    const std::string_view Source,
		    const std::uint32_t OneBasedLine,
		    const std::uint32_t OneBasedColumn)
		{
			const std::string_view Line = SourceLine(Source, OneBasedLine);
			const std::size_t ByteLimit = (std::min)(Line.size(),
			    static_cast<std::size_t>(OneBasedColumn > 0 ? OneBasedColumn - 1 : 0));
			std::size_t ByteOffset = 0;
			std::uint32_t Units = 0;
			while (ByteOffset < ByteLimit)
			{
				const auto [CodePoint, Length] = DecodeUtf8(Line, ByteOffset);
				if (ByteOffset + Length > ByteLimit)
				{
					break;
				}
				Units += CodePoint > 0xFFFFu ? 2u : 1u;
				ByteOffset += Length;
			}
			return Units;
		}

		std::uint32_t Utf16Length(const std::string_view Text)
		{
			std::uint32_t Units = 0;
			for (std::size_t Offset = 0; Offset < Text.size();)
			{
				const auto [CodePoint, Length] = DecodeUtf8(Text, Offset);
				Units += CodePoint > 0xFFFFu ? 2u : 1u;
				Offset += Length;
			}
			return Units;
		}

		void AddPresentationSpan(
		    FJson::FArray& Spans,
		    const std::string_view Text,
		    const std::size_t Start,
		    const std::size_t Length,
		    const std::string_view Classification)
		{
			if (Length == 0 || Start > Text.size() || Length > Text.size() - Start)
			{
				return;
			}
			Spans.push_back(FJson::Object({{"start", FJson::Number(Utf16Length(Text.substr(0, Start)))},
			    {"length", FJson::Number(Utf16Length(Text.substr(Start, Length)))},
			    {"classification", FJson::String(std::string(Classification))}}));
		}

		FJson BuildQualifiedCompletionPresentation(const FCompletionItem& Item)
		{
			const std::string Qualified = Item.LabelDescription + "::" + Item.Label;
			const char* LeafKind = "variable";
			switch (Item.Kind)
			{
			case EOutlineSymbolKind::Import:
			case EOutlineSymbolKind::Namespace:
				{
					LeafKind = "namespace";
					break;
				}
			case EOutlineSymbolKind::TypeAlias:
				{
					LeafKind = "type";
					break;
				}
			case EOutlineSymbolKind::Class:
				{
					LeafKind = "class";
					break;
				}
			case EOutlineSymbolKind::Struct:
				{
					LeafKind = "struct";
					break;
				}
			case EOutlineSymbolKind::Interface:
				{
					LeafKind = "interface";
					break;
				}
			case EOutlineSymbolKind::Enum:
				{
					LeafKind = "enum";
					break;
				}
			case EOutlineSymbolKind::EnumMember:
				{
					LeafKind = "enumMember";
					break;
				}
			case EOutlineSymbolKind::Function:
				{
					LeafKind = "function";
					break;
				}
			case EOutlineSymbolKind::Method:
			case EOutlineSymbolKind::Constructor:
			case EOutlineSymbolKind::Destructor:
				{
					LeafKind = "method";
					break;
				}
			case EOutlineSymbolKind::Field:
				{
					LeafKind = "property";
					break;
				}
			case EOutlineSymbolKind::Constant:
				{
					LeafKind = "constant";
					break;
				}
			case EOutlineSymbolKind::Macro:
				{
					LeafKind = "macro";
					break;
				}
			case EOutlineSymbolKind::Keyword:
				{
					LeafKind = "keyword";
					break;
				}
			case EOutlineSymbolKind::Annotation:
				{
					LeafKind = "decorator";
					break;
				}
			case EOutlineSymbolKind::AnnotationArgument:
				{
					LeafKind = "decoratorArgument";
					break;
				}
			case EOutlineSymbolKind::Label:
				{
					LeafKind = "label";
					break;
				}
			default:
				{
					break;
				}
			}
			FJson::FArray LabelSpans;
			for (std::size_t Begin = 0; Begin < Item.LabelDescription.size();)
			{
				const std::size_t End = Item.LabelDescription.find("::", Begin);
				const std::size_t SegmentEnd = End == std::string::npos
				    ? Item.LabelDescription.size()
				    : End;
				AddPresentationSpan(
				    LabelSpans, Qualified, Begin, SegmentEnd - Begin, "namespace");
				Begin = End == std::string::npos ? Item.LabelDescription.size() : End + 2;
			}
			AddPresentationSpan(
			    LabelSpans, Qualified, Item.LabelDescription.size() + 2,
			    Item.Label.size(), LeafKind);
			FJson::FObject Presentation;
			Presentation["label"] = FJson::Object({{"text", FJson::String(Qualified)},
			    {"semanticSpans", FJson::Array(std::move(LabelSpans))}});

			// A qualified declaration may already be complete (stdlib metadata) or
			// use the symbol index's short type/signature form (user declarations).
			// Keep prose and ambiguous details on the ordinary fallback path.
			const bool bCallable = Item.Kind == EOutlineSymbolKind::Function || Item.Kind == EOutlineSymbolKind::Method;
			const bool bValue = Item.Kind == EOutlineSymbolKind::Variable || Item.Kind == EOutlineSymbolKind::Field || Item.Kind == EOutlineSymbolKind::Constant;
			const auto Trim = [](std::string_view Text)
			{
				while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())) != 0)
				{
					Text.remove_prefix(1);
				}
				while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())) != 0)
				{
					Text.remove_suffix(1);
				}
				return Text;
			};
			const auto IsSimpleType = [](const std::string_view Text)
			{
				// One-token type spellings are safe to turn into declarations;
				// prose and annotated details keep their existing fallback rendering.
				return !Text.empty() && (std::isalpha(static_cast<unsigned char>(Text.front())) != 0 || Text.front() == '_') && Text.find_first_of(" \t\r\n;{}=()\"'") == std::string_view::npos;
			};
			const auto IsSimpleDeclaredType = [&](std::string_view Text)
			{
				// Symbol-index details can retain declaration modifiers. Accept only
				// known modifiers before a single type, never arbitrary prose.
				Text = Trim(Text);
				while (true)
				{
					const std::size_t Space = Text.find_first_of(" \t");
					if (Space == std::string_view::npos)
					{
						return IsSimpleType(Text);
					}
					const std::string_view Word = Text.substr(0, Space);
					if (Word != "const" && Word != "constexpr" && Word != "static" && Word != "mutable" && Word != "virtual")
					{
						return false;
					}
					Text = Trim(Text.substr(Space + 1));
				}
			};
			const std::size_t FirstLineEnd = Item.Detail.find('\n');
			const std::string_view FirstLine(
			    Item.Detail.data(), FirstLineEnd == std::string::npos ? Item.Detail.size() : FirstLineEnd);
			// A return type may itself contain a parenthesized generic argument.
			// Select the call signature's opening parenthesis at template depth zero.
			std::size_t OpenParen = std::string::npos;
			std::size_t TemplateDepth = 0;
			for (std::size_t Index = 0; Index < FirstLine.size(); ++Index)
			{
				if (FirstLine[Index] == '<')
				{
					++TemplateDepth;
				}
				else if (FirstLine[Index] == '>' && TemplateDepth != 0)
				{
					--TemplateDepth;
				}
				else if (FirstLine[Index] == '(' && TemplateDepth == 0)
				{
					OpenParen = Index;
					break;
				}
			}
			std::string Declaration;
			std::size_t NameStart = std::string::npos;
			if (bCallable && OpenParen != std::string::npos)
			{
				const std::size_t Existing = FirstLine.rfind(Qualified, OpenParen);
				if (Existing != std::string::npos && Existing + Qualified.size() <= OpenParen && (Existing == 0 || std::isspace(static_cast<unsigned char>(FirstLine[Existing - 1])) != 0) && Trim(FirstLine.substr(Existing + Qualified.size(), OpenParen - Existing - Qualified.size())).empty())
				{
					Declaration = Item.Detail;
					NameStart = Existing;
					if (FirstLineEnd == std::string::npos && !Declaration.empty() && Declaration.back() == ')')
					{
						Declaration.push_back(';');
					}
				}
				else if (FirstLineEnd == std::string::npos)
				{
					const std::size_t CloseParen = FirstLine.rfind(')');
					const std::string_view Tail = CloseParen == std::string::npos
					    ? std::string_view{}
					    : Trim(FirstLine.substr(CloseParen + 1));
					if (CloseParen != std::string::npos && CloseParen > OpenParen && (Tail.empty() || Tail == ";"))
					{
						const std::string_view Prefix = Trim(FirstLine.substr(0, OpenParen));
						const std::size_t LeafStart = Prefix.rfind(Item.Label);
						const bool bHasUnqualifiedName = LeafStart != std::string::npos && LeafStart + Item.Label.size() == Prefix.size() && (LeafStart == 0 || std::isspace(static_cast<unsigned char>(Prefix[LeafStart - 1])) != 0);
						const std::string_view ReturnType = bHasUnqualifiedName
						    ? Trim(Prefix.substr(0, LeafStart))
						    : Prefix;
						if ((bHasUnqualifiedName && (ReturnType.empty() || IsSimpleDeclaredType(ReturnType))) || (!bHasUnqualifiedName && IsSimpleDeclaredType(ReturnType)))
						{
							Declaration = ReturnType.empty()
							    ? std::string()
							    : std::string(ReturnType) + ' ';
							NameStart = Declaration.size();
							Declaration += Qualified;
							Declaration += FirstLine.substr(OpenParen, CloseParen - OpenParen + 1);
							Declaration.push_back(';');
						}
					}
				}
			}
			else if (bValue && FirstLineEnd == std::string::npos && IsSimpleDeclaredType(FirstLine))
			{
				Declaration = std::string(Trim(FirstLine)) + ' ';
				NameStart = Declaration.size();
				Declaration += Qualified;
				Declaration.push_back(';');
			}
			if (NameStart != std::string::npos)
			{
				struct FSegment
				{
					std::size_t Start;
					std::size_t Length;
					const char* Classification;
				};
				std::vector<FSegment> Segments;
				// Cover the declaration and an optional canonical signature, but not
				// later prose such as "Requires import ...". Only lexically certain
				// categories are colored; arbitrary identifiers are not presumed types.
				std::size_t SyntaxEnd = Declaration.size();
				if (const std::size_t FirstNewline = Declaration.find('\n');
				    FirstNewline != std::string::npos)
				{
					SyntaxEnd = FirstNewline;
					const std::size_t SecondNewline = Declaration.find('\n', FirstNewline + 1);
					const std::string_view SecondLine(
					    Declaration.data() + FirstNewline + 1,
					    (SecondNewline == std::string::npos
					            ? Declaration.size()
					            : SecondNewline) -
					        FirstNewline - 1);
					if (SecondLine.find('(') != std::string_view::npos && SecondLine.find(')') != std::string_view::npos)
					{
						SyntaxEnd = SecondNewline == std::string::npos
						    ? Declaration.size()
						    : SecondNewline;
					}
				}
				VeroScript::VeroScriptLexer Lexer(
				    std::string_view(Declaration).substr(0, SyntaxEnd),
				    "<completion-presentation>");
				std::size_t LineStart = 0;
				std::uint32_t Line = 1;
				for (const VeroScript::FToken& Token : Lexer.Tokenize())
				{
					while (Line < Token.Location.Line && LineStart < Declaration.size())
					{
						const std::size_t Newline = Declaration.find('\n', LineStart);
						if (Newline == std::string::npos)
						{
							break;
						}
						LineStart = Newline + 1;
						++Line;
					}
					if (Token.Location.Line != Line || Token.Location.Column == 0)
					{
						continue;
					}
					const std::size_t Start = LineStart + Token.Location.Column - 1;
					if (Token.Kind == VeroScript::ETokenKind::Keyword)
					{
						Segments.push_back({Start, Token.Lexeme.size(),
						    VeroScript::IsContainerTypeKeyword(Token.Lexeme)
						        ? "type"
						        : "keyword"});
					}
					else if (Token.Kind == VeroScript::ETokenKind::BooleanLiteral || Token.Kind == VeroScript::ETokenKind::NullptrLiteral)
					{
						Segments.push_back({Start, Token.Lexeme.size(), "keyword"});
					}
					else if (Token.Kind == VeroScript::ETokenKind::StringLiteral || Token.Kind == VeroScript::ETokenKind::WStringLiteral || Token.Kind == VeroScript::ETokenKind::CharacterLiteral)
					{
						Segments.push_back({Start, Token.Lexeme.size(), "string"});
					}
					else if (Token.Kind == VeroScript::ETokenKind::IntegerLiteral || Token.Kind == VeroScript::ETokenKind::FloatLiteral)
					{
						Segments.push_back({Start, Token.Lexeme.size(), "number"});
					}
				}
				for (std::size_t Begin = 0; Begin < Item.LabelDescription.size();)
				{
					const std::size_t End = Item.LabelDescription.find("::", Begin);
					const std::size_t SegmentEnd = End == std::string::npos
					    ? Item.LabelDescription.size()
					    : End;
					Segments.push_back({NameStart + Begin,
					    SegmentEnd - Begin, "namespace"});
					Begin = End == std::string::npos ? Item.LabelDescription.size() : End + 2;
				}
				Segments.push_back({NameStart + Item.LabelDescription.size() + 2,
				    Item.Label.size(), LeafKind});
				std::sort(Segments.begin(), Segments.end(),
				    [](const FSegment& Left, const FSegment& Right)
				{
					return Left.Start < Right.Start;
				});
				FJson::FArray DetailSpans;
				std::size_t CoveredEnd = 0;
				for (const FSegment& Segment : Segments)
				{
					if (Segment.Start < CoveredEnd || Segment.Length == 0)
					{
						continue;
					}
					AddPresentationSpan(DetailSpans, Declaration,
					    Segment.Start, Segment.Length, Segment.Classification);
					CoveredEnd = Segment.Start + Segment.Length;
				}
				Presentation["detail"] = FJson::Object({{"text", FJson::String(std::move(Declaration))},
				    {"semanticSpans", FJson::Array(std::move(DetailSpans))}});
			}
			return FJson::Object(std::move(Presentation));
		}

		FTextPosition PositionFromJson(
		    const FJson& PositionObject,
		    const std::string_view Source)
		{
			FTextPosition Position;
			const std::int64_t RawLine = PositionObject.GetInt("line", 0);
			const std::int64_t RawCharacter = PositionObject.GetInt("character", 0);
			Position.Line = static_cast<std::uint32_t>(
			                    (std::min)(RawLine < 0 ? 0 : RawLine,
			                        static_cast<std::int64_t>(
			                            (std::numeric_limits<std::uint32_t>::max)() - 1u))) +
			    1u;
			Position.Column = Utf16CharacterToByteColumn(
			    Source,
			    Position.Line,
			    static_cast<std::uint32_t>(
			        (std::min)(RawCharacter < 0 ? 0 : RawCharacter,
			            static_cast<std::int64_t>(
			                (std::numeric_limits<std::uint32_t>::max)()))));
			return Position;
		}

		std::string IdentifierAtPosition(
		    const std::string_view Source,
		    const FTextPosition Position,
		    FTextRange* const OutRange = nullptr)
		{
			const std::string_view Line = SourceLine(Source, Position.Line);
			if (Line.empty())
			{
				return {};
			}

			auto IsIdentifier = [](const unsigned char Character)
			{
				return std::isalnum(Character) != 0 || Character == '_';
			};
			std::size_t Offset = (std::min)(Line.size(),
			    static_cast<std::size_t>(Position.Column > 0 ? Position.Column - 1 : 0));
			if (Offset == Line.size() || !IsIdentifier(static_cast<unsigned char>(Line[Offset])))
			{
				if (Offset == 0 || !IsIdentifier(static_cast<unsigned char>(Line[Offset - 1])))
				{
					return {};
				}
				--Offset;
			}

			std::size_t Start = Offset;
			while (Start > 0 && IsIdentifier(static_cast<unsigned char>(Line[Start - 1])))
			{
				--Start;
			}
			std::size_t End = Offset + 1;
			while (End < Line.size() && IsIdentifier(static_cast<unsigned char>(Line[End])))
			{
				++End;
			}
			if (OutRange)
			{
				OutRange->StartLine = Position.Line;
				OutRange->StartColumn = static_cast<std::uint32_t>(Start + 1);
				OutRange->EndLine = Position.Line;
				OutRange->EndColumn = static_cast<std::uint32_t>(End + 1);
			}
			return std::string(Line.substr(Start, End - Start));
		}

		FJson RangeToJson(const FTextRange& Range, const std::string_view Source)
		{
			const auto ZeroBasedLine = [](const std::uint32_t Line)
			{
				return Line == 0 ? 0u : Line - 1u;
			};
			FJson::FObject Start;
			Start["line"] = FJson::Number(static_cast<double>(ZeroBasedLine(Range.StartLine)));
			Start["character"] = FJson::Number(static_cast<double>(
			    ByteColumnToUtf16Character(Source, Range.StartLine, Range.StartColumn)));
			FJson::FObject End;
			End["line"] = FJson::Number(static_cast<double>(ZeroBasedLine(Range.EndLine)));
			End["character"] = FJson::Number(static_cast<double>(
			    ByteColumnToUtf16Character(Source, Range.EndLine, Range.EndColumn)));
			FJson::FObject Result;
			Result["start"] = FJson::Object(std::move(Start));
			Result["end"] = FJson::Object(std::move(End));
			return FJson::Object(std::move(Result));
		}

		FTextRange SelectionRangeForOutlineSymbol(
		    const FTextRange& DeclRange,
		    const std::string_view SymbolName,
		    const EOutlineSymbolKind Kind,
		    const std::string_view Source)
		{
			// The compact outline model retains declaration starts but not every
			// declaration's complete syntactic span. For an LSP selection range, a
			// zero-width point is not useful (and violates TypeHierarchyItem's intent).
			// Recover the declared spelling on its physical line without pretending to
			// know the enclosing block's full range.
			if (DeclRange.StartLine == 0 || SymbolName.empty() || DeclRange.EndLine != DeclRange.StartLine || DeclRange.EndColumn > DeclRange.StartColumn)
			{
				return DeclRange;
			}
			// Ordinary display names may carry a canonical namespace and formatted
			// template parameters. Only the declared identifier is present at this
			// source anchor, and whitespace inside the template header is arbitrary.
			// Operator/conversion names use their own spelling and must not be split.
			std::string_view SearchName = SymbolName;
			const auto QualifiedOperator = SymbolName.find(".operator");
			const bool bOperatorName = Detail::IsOperatorMethodName(SymbolName) || (QualifiedOperator != std::string_view::npos && Detail::IsOperatorMethodName(SymbolName.substr(QualifiedOperator + 1)));
			const bool bIdentifierName = Kind != EOutlineSymbolKind::Import && !bOperatorName;
			const bool bDestructor = Kind == EOutlineSymbolKind::Destructor;
			if (bIdentifierName)
			{
				if (bDestructor && SearchName.front() == '~')
				{
					SearchName.remove_prefix(1);
				}
				SearchName = SearchName.substr(0, SearchName.find('<'));
				if (const auto Scope = SearchName.find_last_of('.'); Scope != std::string_view::npos)
				{
					SearchName.remove_prefix(Scope + 1);
				}
				if (const auto Scope = SearchName.rfind("::"); Scope != std::string_view::npos)
				{
					SearchName.remove_prefix(Scope + 2);
				}
			}
			if (SearchName.empty()) return DeclRange;
			const std::string_view Line = SourceLine(Source, DeclRange.StartLine);
			if (Line.empty())
			{
				return DeclRange;
			}
			const std::size_t Anchor = (std::min)(Line.size(), static_cast<std::size_t>(DeclRange.StartColumn > 0 ? DeclRange.StartColumn - 1u : 0u));
			std::size_t Match = Line.find(SearchName, Anchor);
			const auto IsIdentifier = [](const unsigned char Character)
			{
				return std::isalnum(Character) != 0 || Character == '_';
			};
			while (Match != std::string_view::npos)
			{
				const std::size_t End = Match + SearchName.size();
				const bool bLeftBoundary = Match == 0 || !IsIdentifier(static_cast<unsigned char>(Line[Match - 1u]));
				const bool bRightBoundary = End >= Line.size() || !IsIdentifier(static_cast<unsigned char>(Line[End]));
				if (bLeftBoundary && bRightBoundary)
				{
					if (bIdentifierName)
					{
						// Lex only through the candidate name, not the remainder of a
						// long single-line class. Comments/strings cannot supply a name.
						VeroScriptLexer Lexer(Line.substr(Anchor, End - Anchor), "<outline-name>");
						const auto& Tokens = Lexer.Tokenize();
						const auto* Token = Tokens.size() >= 2 ? &Tokens[Tokens.size() - 2] : nullptr;
						if (Lexer.HasError() || !Token || (Token->Kind != ETokenKind::Identifier && Token->Kind != ETokenKind::Keyword) || Token->Lexeme != SearchName || Token->Lexeme.data() != Line.data() + Match)
						{
							Match = Line.find(SearchName, Match + 1u);
							continue;
						}
					}
					FTextRange Selection = DeclRange;
					if (bDestructor && Match > 0 && Line[Match - 1] == '~')
					{
						--Match;
					}
					Selection.StartColumn = static_cast<std::uint32_t>(Match + 1u);
					Selection.EndColumn = static_cast<std::uint32_t>(End + 1u);
					return Selection;
				}
				Match = Line.find(SearchName, Match + 1u);
			}
			return DeclRange;
		}

		int DiagnosticSeverityToLsp(const EDiagnosticSeverity Severity)
		{
			switch (Severity)
			{
			case EDiagnosticSeverity::Error:
				return 1;
			case EDiagnosticSeverity::Warning:
				return 2;
			case EDiagnosticSeverity::Information:
				return 3;
			case EDiagnosticSeverity::Hint:
				return 4;
			default:
				{
					break;
				}
			}
			return 1;
		}

		// LSP 3.17 CompletionItemKind values.
		int OutlineKindToCompletionItemKind(const EOutlineSymbolKind Kind)
		{
			switch (Kind)
			{
			case EOutlineSymbolKind::Import:
				return 9; // Module
			case EOutlineSymbolKind::Namespace:
				return 9; // Module (CompletionItemKind has no Namespace member)
			case EOutlineSymbolKind::Function:
				return 3; // Function
			case EOutlineSymbolKind::Method:
				return 2; // Method
			case EOutlineSymbolKind::Constructor:
				return 4; // Constructor
			case EOutlineSymbolKind::Struct:
				return 22; // Struct
			case EOutlineSymbolKind::Class:
				return 7; // Class
			case EOutlineSymbolKind::Interface:
				return 8; // Interface
			case EOutlineSymbolKind::Enum:
				return 13; // Enum
			case EOutlineSymbolKind::EnumMember:
				return 20; // EnumMember
			case EOutlineSymbolKind::Field:
				return 5; // Field
			case EOutlineSymbolKind::Variable:
				return 6; // Variable
			case EOutlineSymbolKind::Constant:
				return 21; // Constant
			case EOutlineSymbolKind::TypeAlias:
				return 22; // Struct (closest fit for an aliased type)
			case EOutlineSymbolKind::Label:
				return 18; // Reference (goto-label target)
			case EOutlineSymbolKind::Destructor:
				return 4; // Constructor
			case EOutlineSymbolKind::Keyword:
				return 14; // Keyword
			case EOutlineSymbolKind::Annotation:
				return 15; // Snippet (closest standard completion kind for decorators)
			case EOutlineSymbolKind::AnnotationArgument:
				return 15; // Snippet (named decorator argument/specifier)
			case EOutlineSymbolKind::Macro:
				return 21; // Constant
			default:
				{
					break;
				}
			}
			return 1; // Text
		}

		// LSP 3.17 SymbolKind values.
		int OutlineKindToSymbolKind(const EOutlineSymbolKind Kind)
		{
			switch (Kind)
			{
			case EOutlineSymbolKind::Import:
				return 2; // Module
			case EOutlineSymbolKind::Namespace:
				return 3; // Namespace
			case EOutlineSymbolKind::Function:
				return 12; // Function
			case EOutlineSymbolKind::Method:
				return 6; // Method
			case EOutlineSymbolKind::Constructor:
				return 9; // Constructor
			case EOutlineSymbolKind::Struct:
				return 23; // Struct
			case EOutlineSymbolKind::Class:
				return 5; // Class
			case EOutlineSymbolKind::Interface:
				return 11; // Interface
			case EOutlineSymbolKind::Enum:
				return 10; // Enum
			case EOutlineSymbolKind::EnumMember:
				return 22; // EnumMember
			case EOutlineSymbolKind::Field:
				return 8; // Field
			case EOutlineSymbolKind::Variable:
				return 13; // Variable
			case EOutlineSymbolKind::Constant:
				return 14; // Constant
			case EOutlineSymbolKind::TypeAlias:
				return 5; // Class (closest fit for an aliased type)
			case EOutlineSymbolKind::Label:
				return 13; // Variable (closest fit for a goto-label target)
			case EOutlineSymbolKind::Destructor:
				return 9; // Constructor
			case EOutlineSymbolKind::Keyword:
				return 14; // Constant (no Keyword in SymbolKind; unused for outline)
			case EOutlineSymbolKind::Annotation:
				return 14; // Constant (no decorator SymbolKind)
			case EOutlineSymbolKind::AnnotationArgument:
				return 14; // Constant (no decorator-argument SymbolKind)
			case EOutlineSymbolKind::Macro:
				return 14; // Constant
			default:
				{
					break;
				}
			}
			return 13; // Variable
		}

		// LSP 3.17 DocumentHighlightKind: Text=1, Read=2, Write=3.
		int DocumentHighlightKindToLsp(const EDocumentHighlightKind Kind)
		{
			switch (Kind)
			{
			case EDocumentHighlightKind::Text:
				return 1;
			case EDocumentHighlightKind::Read:
				return 2;
			case EDocumentHighlightKind::Write:
				return 3;
			default:
				{
					break;
				}
			}
			return 1;
		}

		// Indices into the semanticTokensProvider legend emitted by HandleInitialize.
		int OutlineKindToSemanticTokenType(const EOutlineSymbolKind Kind)
		{
			switch (Kind)
			{
			case EOutlineSymbolKind::Import:
			case EOutlineSymbolKind::Namespace:
				return 0; // namespace
			case EOutlineSymbolKind::TypeAlias:
				return 1; // type
			case EOutlineSymbolKind::Class:
				return 2; // class
			case EOutlineSymbolKind::Enum:
				return 3; // enum
			case EOutlineSymbolKind::Interface:
				return 4; // interface
			case EOutlineSymbolKind::Struct:
				return 5; // struct
			case EOutlineSymbolKind::Variable:
				return 6; // variable (includes parameters until the AST exposes a distinct kind)
			case EOutlineSymbolKind::Field:
				return 7; // property
			case EOutlineSymbolKind::EnumMember:
				return 8; // enumMember
			case EOutlineSymbolKind::Function:
				return 9; // function
			case EOutlineSymbolKind::Method:
			case EOutlineSymbolKind::Constructor:
			case EOutlineSymbolKind::Destructor:
				return 10; // method
			case EOutlineSymbolKind::Constant:
				return 15; // constant (VeroScript extension)
			case EOutlineSymbolKind::Keyword:
				return 12; // keyword
			case EOutlineSymbolKind::Annotation:
				return 13; // decorator
			case EOutlineSymbolKind::AnnotationArgument:
				return 14; // decoratorArgument (VeroScript extension)
			case EOutlineSymbolKind::Macro:
				return 11; // macro
			case EOutlineSymbolKind::Label:
				return 16; // label
			default:
				{
					break;
				}
			}
			return 6;
		}

		struct FLspSemanticSpan
		{
			std::uint32_t Line = 1;
			std::uint32_t StartColumn = 1;
			std::uint32_t EndColumn = 1;
			int Type = 0;
			std::uint32_t Modifiers = SemanticTokenModifiers::None;
		};

		bool SemanticSpansOverlap(const FLspSemanticSpan& Left, const FLspSemanticSpan& Right)
		{
			return Left.Line == Right.Line && Left.StartColumn < Right.EndColumn && Right.StartColumn < Left.EndColumn;
		}

		FJson::FArray BuildSemanticTokenData(
		    const std::string_view Source,
		    const FSemanticTokensResult& Result)
		{
			FAnalysisStage Stage("encode");
			CheckAnalysisCancellation();
			std::size_t EncodingWork = 0;
			const auto CheckEncodingCancellation = [&]()
			{
				if ((++EncodingWork & 1023u) == 0u)
				{
					CheckAnalysisCancellation();
				}
			};
			std::vector<FLspSemanticSpan> Spans;
			Spans.reserve(Result.Tokens.size() + 32u);
			// Keep the union of occupied byte ranges for overlap queries. Looking
			// through every earlier span for each lexical token is quadratic on
			// ordinary documents with many identifiers and keywords.
			using FSpanIntervals = std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t>;
			FSpanIntervals Occupied;
			FSpanIntervals LiteralsAndComments;
			FSpanIntervals DirectiveMessages;
			FSpanIntervals DirectiveHeads;
			const auto RegisterInterval = [](FSpanIntervals& Intervals, const FLspSemanticSpan& Span)
			{
				std::uint32_t Begin = Span.StartColumn;
				std::uint32_t End = Span.EndColumn;
				auto It = Intervals.lower_bound({Span.Line, Begin});
				if (It != Intervals.begin())
				{
					auto Previous = std::prev(It);
					if (Previous->first.first == Span.Line && Previous->second >= Begin)
					{
						Begin = Previous->first.second;
						End = (std::max)(End, Previous->second);
						It = Intervals.erase(Previous);
					}
				}
				while (It != Intervals.end() && It->first.first == Span.Line && It->first.second <= End)
				{
					End = (std::max)(End, It->second);
					It = Intervals.erase(It);
				}
				Intervals.emplace(std::make_pair(Span.Line, Begin), End);
			};
			const auto OverlapsInterval = [](const FSpanIntervals& Intervals, const FLspSemanticSpan& Span)
			{
				const auto It = Intervals.lower_bound({Span.Line, Span.StartColumn});
				if (It != Intervals.end() && It->first.first == Span.Line && It->first.second < Span.EndColumn)
				{
					return true;
				}
				if (It != Intervals.begin())
				{
					const auto Previous = std::prev(It);
					return Previous->first.first == Span.Line && Previous->second > Span.StartColumn;
				}
				return false;
			};
			for (const FSemanticToken& Token : Result.Tokens)
			{
				CheckAnalysisCancellation();
				if (Token.Range.StartLine == 0 || Token.Range.EndLine != Token.Range.StartLine || Token.Range.EndColumn <= Token.Range.StartColumn)
				{
					continue;
				}
				const FLspSemanticSpan Span{
				    Token.Range.StartLine,
				    Token.Range.StartColumn,
				    Token.Range.EndColumn,
				    OutlineKindToSemanticTokenType(Token.Kind), Token.Modifiers};
				Spans.push_back(Span);
				RegisterInterval(Occupied, Span);
				if (Span.Type == 17 || Span.Type == 19)
				{
					RegisterInterval(LiteralsAndComments, Span);
				}
			}
			const auto AddLexical = [&Spans, &Occupied, &LiteralsAndComments,
			                            &RegisterInterval, &OverlapsInterval](
			                            const FLspSemanticSpan Span)
			{
				if (Span.EndColumn <= Span.StartColumn || OverlapsInterval(Occupied, Span))
				{
					return;
				}
				Spans.push_back(Span);
				RegisterInterval(Occupied, Span);
				if (Span.Type == 17 || Span.Type == 19)
				{
					RegisterInterval(LiteralsAndComments, Span);
				}
			};
			const auto AddLexicalToken = [&AddLexical, &CheckEncodingCancellation](
			                                 const VeroScript::FToken& Token,
			                                 const int Type)
			{
				std::uint32_t Line = Token.Location.Line;
				std::uint32_t Column = Token.Location.Column;
				std::size_t SegmentStart = 0;
				for (std::size_t Index = 0; Index < Token.Lexeme.size(); ++Index)
				{
					CheckEncodingCancellation();
					if (Token.Lexeme[Index] != '\n')
					{
						continue;
					}
					std::size_t SegmentEnd = Index;
					if (SegmentEnd > SegmentStart && Token.Lexeme[SegmentEnd - 1] == '\r')
					{
						--SegmentEnd;
					}
					if (SegmentEnd > SegmentStart)
					{
						AddLexical(FLspSemanticSpan{
						    Line,
						    Column,
						    static_cast<std::uint32_t>(Column + SegmentEnd - SegmentStart),
						    Type});
					}
					++Line;
					Column = 1;
					SegmentStart = Index + 1;
				}
				if (SegmentStart < Token.Lexeme.size())
				{
					AddLexical(FLspSemanticSpan{
					    Line,
					    Column,
					    static_cast<std::uint32_t>(Column + Token.Lexeme.size() - SegmentStart),
					    Type});
				}
			};
			const auto KeywordSemanticTokenType = [](const std::string_view Word)
			{
				// Scalar spellings (`int`, `void`, `bool`, ...) are reserved language
				// keywords. Only container spellings use the named-type color; this is
				// also the contract used by the plugin's lexical fallback.
				return VeroScript::IsContainerTypeKeyword(Word) ? 1 : 12;
			};

			VeroScript::VeroScriptLexer Lexer(Source, "<semantic-tokens>");
			const auto& LexicalTokens = Lexer.Tokenize();
			bool bRecoverLexicalTokens = Lexer.HasError();
			std::string DirectiveLexicalSource;

			// The lexer intentionally skips comments. Scan them separately while
			// skipping quoted literals so comment markers embedded in strings cannot
			// corrupt the semantic token stream. Multi-line block comments are emitted
			// as one LSP token per physical line.
			std::size_t Index = 0;
			std::uint32_t Line = 1;
			std::uint32_t Column = 1;
			bool bOnlyIndentOnLine = true;
			const auto Advance = [&Source, &Index, &Line, &Column, &bOnlyIndentOnLine]()
			{
				if ((Index & 1023u) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (Index >= Source.size())
				{
					return;
				}
				if (Source[Index++] == '\n')
				{
					++Line;
					Column = 1;
					bOnlyIndentOnLine = true;
				}
				else
				{
					++Column;
					bOnlyIndentOnLine = bOnlyIndentOnLine && (Source[Index - 1] == ' ' || Source[Index - 1] == '\t');
				}
			};
			const auto EndColumnBeforeLineFeed = [&Source, &Index, &Column]()
			{
				return Index > 0 && Index < Source.size() && Source[Index] == '\n' && Source[Index - 1] == '\r' && Column > 1
				    ? Column - 1u
				    : Column;
			};
			const auto RecoveryIdentifierStart = [](const char Character)
			{
				return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_';
			};
			const auto RecoveryIdentifierPart = [&RecoveryIdentifierStart](const char Character)
			{
				return RecoveryIdentifierStart(Character) || (Character >= '0' && Character <= '9');
			};
			while (Index < Source.size())
			{
				if (Source[Index] == '#' && bOnlyIndentOnLine)
				{
					std::size_t DirectiveStart = Index + 1;
					while (DirectiveStart < Source.size() && (Source[DirectiveStart] == ' ' || Source[DirectiveStart] == '\t'))
					{
						CheckEncodingCancellation();
						++DirectiveStart;
					}
					std::size_t DirectiveEnd = DirectiveStart;
					while (DirectiveEnd < Source.size() && RecoveryIdentifierPart(Source[DirectiveEnd]))
					{
						CheckEncodingCancellation();
						++DirectiveEnd;
					}
					const auto Directive = Source.substr(DirectiveStart, DirectiveEnd - DirectiveStart);
					if (Directive == "error" || Directive == "warning")
					{
						// Diagnostic text is opaque. In particular, '/*' or a raw-string
						// opener in it cannot change the lexical state of following lines.
						while (Index < DirectiveEnd)
						{
							Advance();
						}
						while (Index < Source.size() && (Source[Index] == ' ' || Source[Index] == '\t'))
						{
							Advance();
						}
						const auto MessageLine = Line;
						const auto MessageColumn = Column;
						if (DirectiveLexicalSource.empty())
						{
							DirectiveLexicalSource = std::string(Source);
						}
						while (Index < Source.size() && Source[Index] != '\n')
						{
							DirectiveLexicalSource[Index] = ' ';
							Advance();
						}
						if (EndColumnBeforeLineFeed() > MessageColumn)
						{
							RegisterInterval(DirectiveMessages,
							    FLspSemanticSpan{MessageLine, MessageColumn, EndColumnBeforeLineFeed(), 17});
						}
						bRecoverLexicalTokens = true;
						continue;
					}
				}
				const bool bRawString = Source[Index] == 'r' && Index + 1 < Source.size() && Source[Index + 1] == '"';
				const bool bWideString = (Source[Index] == 'L' || Source[Index] == 'l') && Index + 1 < Source.size() && Source[Index + 1] == '"';
				if (bRawString || bWideString || Source[Index] == '"' || Source[Index] == '\'')
				{
					std::uint32_t SegmentLine = Line;
					std::uint32_t SegmentColumn = Column;
					if (bRawString || bWideString)
					{
						Advance();
					}
					const char Quote = Source[Index];
					Advance();
					bool bClosed = false;
					while (Index < Source.size())
					{
						if (Source[Index] == '\n')
						{
							AddLexical(FLspSemanticSpan{
							    SegmentLine, SegmentColumn, EndColumnBeforeLineFeed(), 17}); // string
							if (!bRawString)
							{
								break;
							}
							Advance();
							SegmentLine = Line;
							SegmentColumn = Column;
							continue;
						}
						const bool bEscape = !bRawString && Source[Index] == '\\';
						if (bEscape && Index + 1 < Source.size() && Source[Index + 1] == '\n')
						{
							Advance();
							AddLexical(FLspSemanticSpan{
							    SegmentLine, SegmentColumn, Column, 17}); // string
							Advance();
							SegmentLine = Line;
							SegmentColumn = Column;
							continue;
						}
						Advance();
						if (bEscape && Index < Source.size())
						{
							Advance();
							continue;
						}
						if (Index > 0 && Source[Index - 1] == Quote)
						{
							AddLexical(FLspSemanticSpan{
							    SegmentLine, SegmentColumn, Column, 17}); // string
							bClosed = true;
							break;
						}
					}
					if (!bClosed && Index >= Source.size() && Column > SegmentColumn)
					{
						AddLexical(FLspSemanticSpan{
						    SegmentLine, SegmentColumn, Column, 17}); // string
					}
					continue;
				}
				if (bRecoverLexicalTokens && RecoveryIdentifierStart(Source[Index]))
				{
					const std::size_t Start = Index;
					const std::uint32_t StartLine = Line;
					const std::uint32_t StartColumn = Column;
					do
					{
						Advance();
					}
					while (Index < Source.size() && RecoveryIdentifierPart(Source[Index]));
					const std::string_view Word = Source.substr(Start, Index - Start);
					if (VeroScript::IsLanguageKeyword(Word))
					{
						const int Type = KeywordSemanticTokenType(Word);
						AddLexical(FLspSemanticSpan{StartLine, StartColumn, Column, Type});
					}
					continue;
				}
				if (bRecoverLexicalTokens && ((Source[Index] >= '0' && Source[Index] <= '9') || (Source[Index] == '.' && Index + 1 < Source.size() && Source[Index + 1] >= '0' && Source[Index + 1] <= '9')))
				{
					const std::uint32_t StartLine = Line;
					const std::uint32_t StartColumn = Column;
					int NumberBase = 10;
					bool bFloat = false;
					const auto ConsumeDigits = [&]()
					{
						while (Index < Source.size() && ((Source[Index] >= '0' && Source[Index] <= '9') || Source[Index] == '_'))
						{
							Advance();
						}
					};
					if (Source[Index] == '0' && Index + 1 < Source.size() && (Source[Index + 1] == 'x' || Source[Index + 1] == 'X'))
					{
						NumberBase = 16;
						Advance();
						Advance();
						while (Index < Source.size() && (std::isxdigit(static_cast<unsigned char>(Source[Index])) != 0 || Source[Index] == '_'))
						{
							Advance();
						}
					}
					else if (Source[Index] == '0' && Index + 1 < Source.size() && (Source[Index + 1] == 'b' || Source[Index + 1] == 'B'))
					{
						NumberBase = 2;
						Advance();
						Advance();
						while (Index < Source.size() && (Source[Index] == '0' || Source[Index] == '1' || Source[Index] == '_'))
						{
							Advance();
						}
					}
					else
					{
						if (Source[Index] == '.')
						{
							bFloat = true;
							Advance();
						}
						ConsumeDigits();
						if (Index < Source.size() && Source[Index] == '.')
						{
							bFloat = true;
							Advance();
							ConsumeDigits();
						}
						if (Index < Source.size() && (Source[Index] == 'e' || Source[Index] == 'E'))
						{
							bFloat = true;
							Advance();
							if (Index < Source.size() && (Source[Index] == '+' || Source[Index] == '-'))
							{
								Advance();
							}
							ConsumeDigits();
						}
					}
					if (NumberBase == 10 && Index < Source.size() && (Source[Index] == 'f' || Source[Index] == 'F'))
					{
						bFloat = true;
						Advance();
					}
					if (!bFloat)
					{
						const std::string_view Remaining = Source.substr(Index);
						if (Remaining.substr(0, 3) == "i64" || Remaining.substr(0, 3) == "I64" || Remaining.substr(0, 3) == "u64" || Remaining.substr(0, 3) == "U64")
						{
							Advance();
							Advance();
							Advance();
						}
						else if (Index < Source.size() && (Source[Index] == 'u' || Source[Index] == 'U' || Source[Index] == 'l' || Source[Index] == 'L'))
						{
							Advance();
							if (Index < Source.size() && (Source[Index] == 'u' || Source[Index] == 'U' || Source[Index] == 'l' || Source[Index] == 'L'))
							{
								Advance();
							}
						}
					}
					AddLexical(FLspSemanticSpan{StartLine, StartColumn, Column, 18}); // number
					continue;
				}
				if (Source[Index] != '/' || Index + 1 >= Source.size())
				{
					Advance();
					continue;
				}
				if (Source[Index + 1] == '/')
				{
					const std::uint32_t StartLine = Line;
					const std::uint32_t StartColumn = Column;
					while (Index < Source.size() && Source[Index] != '\n')
					{
						Advance();
					}
					AddLexical(FLspSemanticSpan{
					    StartLine, StartColumn, EndColumnBeforeLineFeed(), 19}); // comment
					continue;
				}
				if (Source[Index + 1] != '*')
				{
					Advance();
					continue;
				}
				std::uint32_t SegmentLine = Line;
				std::uint32_t SegmentColumn = Column;
				bool bClosed = false;
				Advance();
				Advance();
				while (Index < Source.size())
				{
					if (Source[Index] == '*' && Index + 1 < Source.size() && Source[Index + 1] == '/')
					{
						Advance();
						Advance();
						AddLexical(FLspSemanticSpan{SegmentLine, SegmentColumn, Column, 19});
						bClosed = true;
						break;
					}
					if (Source[Index] == '\n')
					{
						AddLexical(FLspSemanticSpan{
						    SegmentLine, SegmentColumn, EndColumnBeforeLineFeed(), 19});
						Advance();
						SegmentLine = Line;
						SegmentColumn = Column;
						continue;
					}
					Advance();
				}
				if (!bClosed && Column > SegmentColumn)
				{
					AddLexical(FLspSemanticSpan{SegmentLine, SegmentColumn, Column, 19});
				}
			}

			// Classify normal lexer tokens after identifying opaque directive text.
			// Discard a whole literal that began in a diagnostic message, even if the
			// ordinary lexer incorrectly extended it across subsequent source lines.
			for (const VeroScript::FToken& Token : LexicalTokens)
			{
				CheckEncodingCancellation();
				if (OverlapsInterval(DirectiveMessages, FLspSemanticSpan{Token.Location.Line, Token.Location.Column, Token.Location.Column + 1u, 17}))
				{
					continue;
				}
				int Type = -1;
				if (Token.Kind == VeroScript::ETokenKind::Keyword)
				{
					Type = KeywordSemanticTokenType(Token.Lexeme);
				}
				else if (Token.Kind == VeroScript::ETokenKind::BooleanLiteral || Token.Kind == VeroScript::ETokenKind::NullptrLiteral)
				{
					Type = 12;
				}
				else if (Token.Kind == VeroScript::ETokenKind::StringLiteral || Token.Kind == VeroScript::ETokenKind::WStringLiteral || Token.Kind == VeroScript::ETokenKind::CharacterLiteral)
				{
					Type = 17;
				}
				else if (Token.Kind == VeroScript::ETokenKind::IntegerLiteral || Token.Kind == VeroScript::ETokenKind::FloatLiteral)
				{
					Type = 18;
				}
				if (Type >= 0)
				{
					AddLexicalToken(Token, Type);
				}
			}

			// Directives are not part of the parsed AST, but they remain meaningful
			// while the document is incomplete. Color the directive head separately
			// from macro names. Existing semantic/string/comment spans win on overlap,
			// so a line-start '#' inside a raw string is never misclassified.
			std::unordered_set<std::string> SourceMacroNames;
			std::size_t LineOffset = 0;
			std::uint32_t PhysicalLine = 1;
			const auto IsIdentifierStart = [](const char Character)
			{
				return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_';
			};
			const auto IsIdentifierPart = [&IsIdentifierStart](const char Character)
			{
				return IsIdentifierStart(Character) || (Character >= '0' && Character <= '9');
			};
			while (LineOffset < Source.size())
			{
				CheckEncodingCancellation();
				const std::size_t Newline = Source.find('\n', LineOffset);
				const std::size_t LineEnd = Newline == std::string_view::npos
				    ? Source.size()
				    : Newline;
				std::size_t Cursor = LineOffset;
				while (Cursor < LineEnd && (Source[Cursor] == ' ' || Source[Cursor] == '\t' || Source[Cursor] == '\r'))
				{
					CheckEncodingCancellation();
					++Cursor;
				}
				if (Cursor < LineEnd && Source[Cursor] == '#')
				{
					const std::uint32_t HashColumn = static_cast<std::uint32_t>(Cursor - LineOffset + 1);
					const FLspSemanticSpan HashSpan{PhysicalLine, HashColumn, HashColumn + 1u, 20};
					const bool bInsideLiteralOrComment = OverlapsInterval(LiteralsAndComments, HashSpan);
					if (!bInsideLiteralOrComment)
					{
						std::size_t DirectiveStart = ++Cursor;
						while (DirectiveStart < LineEnd && (Source[DirectiveStart] == ' ' || Source[DirectiveStart] == '\t'))
						{
							CheckEncodingCancellation();
							++DirectiveStart;
						}
						std::size_t DirectiveEnd = DirectiveStart;
						while (DirectiveEnd < LineEnd && IsIdentifierPart(Source[DirectiveEnd]))
						{
							CheckEncodingCancellation();
							++DirectiveEnd;
						}
						const std::string_view Directive =
						    Source.substr(DirectiveStart, DirectiveEnd - DirectiveStart);
						RegisterInterval(DirectiveHeads, FLspSemanticSpan{PhysicalLine, HashColumn, static_cast<std::uint32_t>(DirectiveEnd - LineOffset + 1), 20}); // preprocessor

						Cursor = DirectiveEnd;
						while (Cursor < LineEnd && (Source[Cursor] == ' ' || Source[Cursor] == '\t'))
						{
							CheckEncodingCancellation();
							++Cursor;
						}
						if (Directive == "error" || Directive == "warning")
						{
							const std::size_t MessageEnd = LineEnd > Cursor && Source[LineEnd - 1] == '\r'
							    ? LineEnd - 1
							    : LineEnd;
							if (MessageEnd > Cursor)
							{
								RegisterInterval(DirectiveMessages, FLspSemanticSpan{PhysicalLine, static_cast<std::uint32_t>(Cursor - LineOffset + 1), static_cast<std::uint32_t>(MessageEnd - LineOffset + 1), 17});
							}
						}
						if (Directive == "define" || Directive == "undef" || Directive == "ifdef" || Directive == "ifndef")
						{
							const std::size_t NameStart = Cursor;
							if (NameStart < LineEnd && IsIdentifierStart(Source[NameStart]))
							{
								while (Cursor < LineEnd && IsIdentifierPart(Source[Cursor]))
								{
									CheckEncodingCancellation();
									++Cursor;
								}
								const std::string Name(Source.substr(NameStart, Cursor - NameStart));
								if (Directive == "define")
								{
									SourceMacroNames.insert(Name);
								}
								AddLexical(FLspSemanticSpan{
								    PhysicalLine,
								    static_cast<std::uint32_t>(NameStart - LineOffset + 1),
								    static_cast<std::uint32_t>(Cursor - LineOffset + 1),
								    11}); // macro
							}
						}
						else if (Directive == "if" || Directive == "elif")
						{
							while (Cursor < LineEnd)
							{
								CheckEncodingCancellation();
								if (!IsIdentifierStart(Source[Cursor]))
								{
									++Cursor;
									continue;
								}
								const std::size_t NameStart = Cursor++;
								while (Cursor < LineEnd && IsIdentifierPart(Source[Cursor]))
								{
									CheckEncodingCancellation();
									++Cursor;
								}
								const std::string Name(Source.substr(NameStart, Cursor - NameStart));
								if (Name == "defined")
								{
									AddLexical(FLspSemanticSpan{
									    PhysicalLine,
									    static_cast<std::uint32_t>(NameStart - LineOffset + 1),
									    static_cast<std::uint32_t>(Cursor - LineOffset + 1),
									    20});
									continue;
								}
								AddLexical(FLspSemanticSpan{
								    PhysicalLine,
								    static_cast<std::uint32_t>(NameStart - LineOffset + 1),
								    static_cast<std::uint32_t>(Cursor - LineOffset + 1),
								    11});
							}
						}
					}
				}
				if (Newline == std::string_view::npos)
				{
					break;
				}
				LineOffset = Newline + 1;
				++PhysicalLine;
			}
			if (!SourceMacroNames.empty())
			{
				// Preserve physical byte offsets while preventing textual directive
				// markers from swallowing macro references on subsequent code lines.
				VeroScript::VeroScriptLexer MacroLexer(DirectiveLexicalSource.empty()
				        ? Source
				        : std::string_view(DirectiveLexicalSource),
				    "<semantic-macros>");
				for (const VeroScript::FToken& Token : MacroLexer.Tokenize())
				{
					CheckEncodingCancellation();
					if (Token.Kind == VeroScript::ETokenKind::Identifier && SourceMacroNames.find(std::string(Token.Lexeme)) != SourceMacroNames.end())
					{
						AddLexicalToken(Token, 11); // macro
					}
				}
			}
			// The interval indexes are no longer needed while building the
			// output array; release their nodes before that allocation grows.
			// Diagnostic directive payloads are text, including words such as `this`,
			// quotes and comment markers. Override ordinary lexer/AST classifications.
			Spans.erase(std::remove_if(Spans.begin(), Spans.end(), [&](const FLspSemanticSpan& Span)
			{
				CheckEncodingCancellation();
				return OverlapsInterval(DirectiveMessages, Span) || OverlapsInterval(DirectiveHeads, Span);
			}),
			    Spans.end());
			for (const auto& Message : DirectiveMessages)
			{
				CheckEncodingCancellation();
				Spans.push_back(FLspSemanticSpan{Message.first.first, Message.first.second, Message.second, 17});
			}
			for (const auto& Head : DirectiveHeads)
			{
				CheckEncodingCancellation();
				Spans.push_back(FLspSemanticSpan{Head.first.first, Head.first.second, Head.second, 20});
			}
			Occupied.clear();
			LiteralsAndComments.clear();
			DirectiveMessages.clear();
			DirectiveHeads.clear();

			std::sort(Spans.begin(), Spans.end(), [&](const FLspSemanticSpan& Left, const FLspSemanticSpan& Right)
			{
				CheckEncodingCancellation();
				if (Left.Line != Right.Line)
				{
					return Left.Line < Right.Line;
				}
				if (Left.StartColumn != Right.StartColumn)
				{
					return Left.StartColumn < Right.StartColumn;
				}
				if (Left.EndColumn != Right.EndColumn)
				{
					return Left.EndColumn < Right.EndColumn;
				}
				return Left.Type < Right.Type;
			});
			// LSP semantic token streams may not contain overlapping records. The
			// semantic query normally emits disjoint identifiers, but recovery on an
			// incomplete declaration can conservatively classify the same span twice.
			// Keep the first (most specific after the ordering above) so a temporary
			// syntax error cannot produce an invalid delta stream at the client.
			std::size_t DisjointCount = 0;
			for (std::size_t SpanIndex = 0; SpanIndex < Spans.size(); ++SpanIndex)
			{
				CheckEncodingCancellation();
				if (DisjointCount != 0 && SemanticSpansOverlap(Spans[DisjointCount - 1u], Spans[SpanIndex]))
				{
					continue;
				}
				Spans[DisjointCount++] = Spans[SpanIndex];
			}
			Spans.resize(DisjointCount);
			FJson::FArray Data;
			Data.reserve(Spans.size() * 5u);
			std::uint32_t IndexedLine = 0;
			std::size_t NextLineStart = 0;
			std::string_view IndexedSourceLine;
			std::size_t Utf16ByteOffset = 0;
			std::uint32_t Utf16Units = 0;
			// Spans is ordered and disjoint. Advance both
			// cursors only forward, avoiding a byte-sized table for long lines.
			const auto AdvanceToLine = [&](const std::uint32_t OneBasedLine)
			{
				while (IndexedLine < OneBasedLine && NextLineStart <= Source.size())
				{
					CheckEncodingCancellation();
					const std::size_t Newline = Source.find('\n', NextLineStart);
					std::size_t End = Newline == std::string_view::npos
					    ? Source.size()
					    : Newline;
					if (End > NextLineStart && Source[End - 1u] == '\r')
					{
						--End;
					}
					IndexedSourceLine = Source.substr(NextLineStart, End - NextLineStart);
					NextLineStart = Newline == std::string_view::npos
					    ? Source.size() + 1u
					    : Newline + 1u;
					++IndexedLine;
				}
				if (IndexedLine < OneBasedLine)
				{
					IndexedSourceLine = {};
				}
				IndexedLine = OneBasedLine;
				Utf16ByteOffset = 0;
				Utf16Units = 0;
			};
			const auto Utf16Column = [&](const std::uint32_t OneBasedColumn)
			{
				const std::size_t ByteLimit = (std::min)(IndexedSourceLine.size(),
				    static_cast<std::size_t>(OneBasedColumn > 0
				            ? OneBasedColumn - 1u
				            : 0u));
				while (Utf16ByteOffset < ByteLimit)
				{
					CheckEncodingCancellation();
					const auto [CodePoint, Length] = DecodeUtf8(
					    IndexedSourceLine, Utf16ByteOffset);
					if (Utf16ByteOffset + Length > ByteLimit)
					{
						break;
					}
					Utf16Units += CodePoint > 0xFFFFu ? 2u : 1u;
					Utf16ByteOffset += Length;
				}
				return Utf16Units;
			};
			std::uint32_t PreviousLine = 0;
			std::uint32_t PreviousCharacter = 0;
			bool bFirst = true;
			for (const FLspSemanticSpan& Span : Spans)
			{
				CheckEncodingCancellation();
				if (Span.Line != IndexedLine)
				{
					AdvanceToLine(Span.Line);
				}
				const std::uint32_t LspLine = Span.Line - 1u;
				const std::uint32_t Character = Utf16Column(Span.StartColumn);
				const std::uint32_t EndCharacter = Utf16Column(Span.EndColumn);
				const std::uint32_t DeltaLine = bFirst ? LspLine : LspLine - PreviousLine;
				const std::uint32_t DeltaStart = bFirst || DeltaLine != 0
				    ? Character
				    : Character - PreviousCharacter;
				Data.push_back(FJson::Number(static_cast<double>(DeltaLine)));
				Data.push_back(FJson::Number(static_cast<double>(DeltaStart)));
				Data.push_back(FJson::Number(static_cast<double>(EndCharacter - Character)));
				Data.push_back(FJson::Number(static_cast<double>(Span.Type)));
				Data.push_back(FJson::Number(Span.Modifiers));
				PreviousLine = LspLine;
				PreviousCharacter = Character;
				bFirst = false;
			}
			return Data;
		}

		// Whole-buffer edits use LSP's physical line separators directly. Core
		// byte columns intentionally follow a different convention for lone CRs.
		FJson FullDocumentRangeToJson(const std::string_view Source)
		{
			std::uint32_t Line = 0, Character = 0;
			std::size_t NextCancellationCheck = 0;
			for (std::size_t Index = 0; Index < Source.size();)
			{
				if (Index >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Index + 2048;
				}
				if (Source[Index] == '\r' || Source[Index] == '\n')
				{
					const bool CR = Source[Index++] == '\r';
					if (CR && Index < Source.size() && Source[Index] == '\n')
					{
						++Index;
					}
					++Line;
					Character = 0;
				}
				else
				{
					const auto [CodePoint, Length] = DecodeUtf8(Source, Index);
					Character += CodePoint > 0xFFFFu ? 2u : 1u;
					Index += Length;
				}
			}
			return FJson::Object({{"start", FJson::Object({{"line", FJson::Number(0)}, {"character", FJson::Number(0)}})},
			    {"end", FJson::Object({{"line", FJson::Number(Line)}, {"character", FJson::Number(Character)}})}});
		}

		std::size_t LspPositionToSourceOffset(const std::string_view Source, const FJson& Position)
		{
			const auto TargetLine = static_cast<std::uint32_t>((std::max)(0, Position.GetInt("line", 0)));
			const auto TargetCharacter = static_cast<std::uint32_t>((std::max)(0, Position.GetInt("character", 0)));
			std::size_t Offset = 0;
			for (std::uint32_t Line = 0; Line < TargetLine && Offset < Source.size(); ++Line)
			{
				CheckAnalysisCancellation();
				const auto Newline = Source.find_first_of("\r\n", Offset);
				if (Newline == std::string_view::npos) return Source.size();
				Offset = Newline + 1;
				if (Source[Newline] == '\r' && Offset < Source.size() && Source[Offset] == '\n')
				{
					++Offset;
				}
			}
			std::uint32_t Character = 0;
			std::size_t NextCancellationCheck = Offset;
			while (Offset < Source.size() && Source[Offset] != '\r' && Source[Offset] != '\n' && Character < TargetCharacter)
			{
				if (Offset >= NextCancellationCheck)
				{
					CheckAnalysisCancellation();
					NextCancellationCheck = Offset + 2048;
				}
				const auto [CodePoint, Length] = DecodeUtf8(Source, Offset);
				const auto Units = CodePoint > 0xFFFFu ? 2u : 1u;
				if (Character + Units > TargetCharacter)
				{
					break;
				}
				Character += Units;
				Offset += Length;
			}
			return Offset;
		}

		bool HasBareCarriageReturn(const std::string_view Source)
		{
			for (auto Offset = Source.find('\r'); Offset != std::string_view::npos;
			    Offset = Source.find('\r', Offset + 1))
			{
				CheckAnalysisCancellation();
				if (Offset + 1 == Source.size() || Source[Offset + 1] != '\n') return true;
			}
			return false;
		}

		bool CaseInsensitiveContains(const std::string_view Haystack, const std::string_view Needle)
		{
			if (Needle.empty())
			{
				return true;
			}
			if (Needle.size() > Haystack.size())
			{
				return false;
			}
			const auto ToLower = [](const unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			};
			for (std::size_t Index = 0; Index + Needle.size() <= Haystack.size(); ++Index)
			{
				bool bMatch = true;
				for (std::size_t Offset = 0; Offset < Needle.size(); ++Offset)
				{
					if (ToLower(static_cast<unsigned char>(Haystack[Index + Offset])) != ToLower(static_cast<unsigned char>(Needle[Offset])))
					{
						bMatch = false;
						break;
					}
				}
				if (bMatch)
				{
					return true;
				}
			}
			return false;
		}

		bool CaseInsensitiveStartsWith(const std::string_view Value, const std::string_view Prefix)
		{
			if (Prefix.size() > Value.size())
			{
				return false;
			}
			return CaseInsensitiveContains(Value.substr(0, Prefix.size()), Prefix);
		}

		bool CaseInsensitiveSubsequence(const std::string_view Haystack, const std::string_view Needle)
		{
			if (Needle.empty())
			{
				return true;
			}
			std::size_t NeedleIndex = 0;
			for (const unsigned char Character : Haystack)
			{
				if (std::tolower(Character) == std::tolower(static_cast<unsigned char>(Needle[NeedleIndex])))
				{
					if (++NeedleIndex == Needle.size())
					{
						return true;
					}
				}
			}
			return false;
		}

		std::string NormalizeQualifiedSeparators(const std::string_view Value)
		{
			// The parser/outliner historically uses `.` for some nested namespace
			// display names while VeroScript source uses `::`. Treat both as one
			// canonical query spelling without changing the display name returned to
			// clients (which preserves existing protocol behavior).
			std::string Result;
			Result.reserve(Value.size());
			for (std::size_t Index = 0; Index < Value.size(); ++Index)
			{
				if (Value[Index] == ':' && Index + 1u < Value.size() && Value[Index + 1u] == ':')
				{
					Result.push_back('.');
					++Index;
					continue;
				}
				Result.push_back(Value[Index]);
			}
			return Result;
		}

		int WorkspaceSymbolMatchScore(const FWorkspaceSymbol& Symbol, const std::string_view Query)
		{
			if (Query.empty())
			{
				return 0;
			}
			const std::string QualifiedName = Symbol.ContainerName.empty()
			    ? Symbol.Name
			    : Symbol.ContainerName + "::" + Symbol.Name;
			const std::string LegacyDottedName = Symbol.ContainerName.empty()
			    ? Symbol.Name
			    : Symbol.ContainerName + "." + Symbol.Name;
			const std::string CanonicalName = NormalizeQualifiedSeparators(Symbol.Name);
			const std::string CanonicalQualifiedName =
			    NormalizeQualifiedSeparators(QualifiedName);
			const std::string CanonicalContainerName =
			    NormalizeQualifiedSeparators(Symbol.ContainerName);
			int Score = 0;
			std::size_t Offset = 0;
			while (Offset < Query.size())
			{
				while (Offset < Query.size() && std::isspace(static_cast<unsigned char>(Query[Offset])))
				{
					++Offset;
				}
				const std::size_t Start = Offset;
				while (Offset < Query.size() && !std::isspace(static_cast<unsigned char>(Query[Offset])))
				{
					++Offset;
				}
				if (Start == Offset)
				{
					break;
				}

				const std::string CanonicalToken =
				    NormalizeQualifiedSeparators(Query.substr(Start, Offset - Start));
				const std::string_view Token = CanonicalToken;
				// Prefer symbol-name matches before container/detail/file matches. This
				// makes Ctrl+T-like callers stable without hiding qualified matches.
				if (CanonicalName.size() == Token.size() && CaseInsensitiveStartsWith(CanonicalName, Token))
				{
					Score += 0;
				}
				else if (CaseInsensitiveStartsWith(CanonicalName, Token))
				{
					Score += 10;
				}
				else if (CaseInsensitiveStartsWith(CanonicalQualifiedName, Token))
				{
					Score += 20;
				}
				else if (CaseInsensitiveStartsWith(CanonicalContainerName, Token))
				{
					Score += 30;
				}
				else if (CaseInsensitiveContains(CanonicalName, Token))
				{
					Score += 40;
				}
				else if (CaseInsensitiveContains(CanonicalQualifiedName, Token))
				{
					Score += 50;
				}
				else if (CaseInsensitiveContains(LegacyDottedName, Token))
				{
					Score += 55;
				}
				else if (CaseInsensitiveSubsequence(CanonicalName, Token) || CaseInsensitiveSubsequence(CanonicalQualifiedName, Token))
				{
					// Fuzzy matches remain below complete text matches, but make short
					// Ctrl+T-style queries useful without discarding semantic ranking.
					Score += 70;
				}
				else
				{
					// A match that only came from detail/file metadata is useful, but a
					// fuzzy match against the semantic name is still the better result.
					Score += 80;
				}
			}
			return Score;
		}

		bool IsHierarchyTypeKind(const EOutlineSymbolKind Kind)
		{
			return Kind == EOutlineSymbolKind::Class || Kind == EOutlineSymbolKind::Struct || Kind == EOutlineSymbolKind::Interface;
		}

		std::string TypeStem(std::string_view Name)
		{
			const std::size_t Generic = Name.find('<');
			Name = Name.substr(0, Generic);
			while (!Name.empty() && std::isspace(static_cast<unsigned char>(Name.back())))
			{
				Name.remove_suffix(1);
			}
			return std::string(Name);
		}

		bool SameTypeStem(const std::string_view Left, const std::string_view Right)
		{
			return TypeStem(Left) == TypeStem(Right);
		}

		bool WorkspaceSymbolMatches(const FWorkspaceSymbol& Symbol, const std::string_view Query)
		{
			const std::string QualifiedName = Symbol.ContainerName.empty()
			    ? Symbol.Name
			    : Symbol.ContainerName + "::" + Symbol.Name;
			const std::string LegacyDottedName = Symbol.ContainerName.empty()
			    ? Symbol.Name
			    : Symbol.ContainerName + "." + Symbol.Name;
			const std::string CanonicalName = NormalizeQualifiedSeparators(Symbol.Name);
			const std::string CanonicalQualifiedName =
			    NormalizeQualifiedSeparators(QualifiedName);
			const std::string CanonicalContainerName =
			    NormalizeQualifiedSeparators(Symbol.ContainerName);
			std::size_t Offset = 0;
			while (Offset < Query.size())
			{
				while (Offset < Query.size() && std::isspace(static_cast<unsigned char>(Query[Offset])))
				{
					++Offset;
				}
				const std::size_t Start = Offset;
				while (Offset < Query.size() && !std::isspace(static_cast<unsigned char>(Query[Offset])))
				{
					++Offset;
				}
				if (Start == Offset)
				{
					break;
				}

				const std::string CanonicalToken =
				    NormalizeQualifiedSeparators(Query.substr(Start, Offset - Start));
				const std::string_view Token = CanonicalToken;
				if (!CaseInsensitiveContains(CanonicalName, Token) && !CaseInsensitiveContains(CanonicalQualifiedName, Token) && !CaseInsensitiveContains(LegacyDottedName, Token) && !CaseInsensitiveContains(CanonicalContainerName, Token) && !CaseInsensitiveContains(Symbol.Detail, Token) && !CaseInsensitiveContains(Symbol.FileLabel, Token))
				{
					if (!CaseInsensitiveSubsequence(CanonicalName, Token) && !CaseInsensitiveSubsequence(CanonicalQualifiedName, Token))
					{
						return false;
					}
				}
			}
			return true;
		}

		std::string WorkspaceSymbolId(const FWorkspaceSymbol& Symbol)
		{
			// Location plus semantic container/kind makes the id deterministic across
			// requests without relying on implementation-defined std::hash values.
			return std::to_string(static_cast<unsigned>(Symbol.Kind)) + '|' + Symbol.FileLabel + '|' + std::to_string(Symbol.Range.StartLine) + ':' + std::to_string(Symbol.Range.StartColumn) + '|' + Symbol.ContainerName + '|' + Symbol.Name;
		}

		class FLspStdioSession
		{
		public:
			FLspStdioSession(
			    std::istream& In,
			    std::ostream& Out,
			    FLspStdioRunOptions Options)
			    : In(In), Out(Out), RunOptions(std::move(Options))
			{
				FWorkspaceOptions WorkspaceOptions;
				WorkspaceOptions.WorkspaceRoots = RunOptions.DefaultWorkspaceRoots;
				WorkspaceOptions.ImportPaths = RunOptions.DefaultImportPaths;
				WorkspaceOptions.ImplicitImportPaths =
				    RunOptions.DefaultImplicitImportPaths;
				WorkspaceOptions.bAllowCircularImports =
				    RunOptions.bDefaultAllowCircularImports;
				WorkspaceOptions.bAllowProjectWideAccess =
				    RunOptions.bDefaultAllowProjectWideAccess;
				WorkspaceOptions.StdlibSearchPaths = RunOptions.DefaultStdlibSearchPaths;
				WorkspaceOptions.Defines = RunOptions.DefaultDefines;
				WorkspaceOptions.PredefinedMacros = RunOptions.DefaultPredefinedMacros;
				WorkspaceOptions.AnnotationProcessing = RunOptions.AnnotationProcessing;
				WorkspaceOptions.CompileProcessing = RunOptions.CompileProcessing;
				WorkspaceOptions.ScriptCompileExtensions = RunOptions.ScriptCompileExtensions;
				WorkspaceOptions.ModuleEnvironment = RunOptions.ModuleEnvironment;
				ReloadBindingCatalogs(RunOptions.DefaultBindingCatalogPaths);
				ApplyBindingsToOptions(WorkspaceOptions);
				Workspace.SetOptions(WorkspaceOptions);
			}

			int Run()
			{
				// Read cancellation independently. The protocol worker applies updates
				// in wire order; a separate worker computes semantic-token snapshots.
				Detail::FLspThread AnalysisWorker([this]()
				{
					RunSemanticWorker();
				});
				Detail::FLspThread Worker([this]()
				{
					RunWorker();
				});

				bool bStopReading = false;
				while (!bStopReading)
				{
					bool bInvalidFrame = false;
					std::optional<std::string> Body = ReadMessage(bInvalidFrame);
					if (!Body)
					{
						std::lock_guard<std::mutex> Lock(IncomingMutex);
						bFramingError = bInvalidFrame;
						break;
					}
					FJson Root;
					try
					{
						Root = ParseJson(*Body);
					}
					catch (const std::exception& Exception)
					{
						std::cerr << "veroscript-lsp: invalid JSON-RPC message: "
						          << Exception.what() << "\n";
						QueueIncomingMessage(FJson::Null(), true);
						continue;
					}
					bStopReading = QueueIncomingMessage(std::move(Root));
				}
				{
					std::lock_guard<std::mutex> Lock(IncomingMutex);
					bInputClosed = true;
				}
				IncomingCondition.notify_all();
				Worker.join();
				{
					std::lock_guard<std::mutex> Lock(IncomingMutex);
					bStopSemanticWorker = true;
				}
				IncomingCondition.notify_all();
				AnalysisWorker.join();
				// LSP requires a non-zero process exit when the client sends `exit`
				// without first completing the `shutdown` request.
				return bFramingError ? 1 : (bExit && !bShutdownReceived ? 1 : 0);
			}

		private:
			enum class ERequestStopReason : std::uint8_t
			{
				None,
				Cancelled,
				TimedOut,
			};

			#include "Internal/VeroScriptLspSemanticAnalysis.inl"

			struct FBindingCatalogStamp
			{
				bool operator!=(const FBindingCatalogStamp& Other) const
				{
					return WriteTime != Other.WriteTime || Size != Other.Size;
				}

				std::optional<std::filesystem::file_time_type> WriteTime;

				std::optional<std::uintmax_t> Size;
			};

			static FBindingCatalogStamp BindingCatalogStamp(const std::string& Path)
			{
				FBindingCatalogStamp Stamp;
				const auto NativePath = PathUtil::FromUtf8(Path);
				std::error_code Ec;
				const auto Time = std::filesystem::last_write_time(NativePath, Ec);
				if (!Ec)
				{
					Stamp.WriteTime = Time;
				}

				const auto Size = std::filesystem::file_size(NativePath, Ec);
				if (!Ec)
				{
					Stamp.Size = Size;
				}

				return Stamp;
			}

			bool HasBindingCatalogChange(const FJson& Params) const
			{
				const FJson* Changes = Params.Find("changes");
				if (!Changes || !Changes->ArrayPtr()) return false;
				for (const FJson& Change : *Changes->ArrayPtr())
				{
					const std::string Path = UriToPath(Change.GetString("uri"));
					for (const std::string& CatalogPath : ActiveBindingCatalogPaths)
					{
						if (PathUtil::Equal(Path, CatalogPath)) return true;
					}
				}
				return false;
			}

			void ReloadBindingCatalogs(const std::vector<std::string>& CatalogPaths)
			{
				ActiveBindingCatalogPaths.clear();
				std::unordered_set<std::string> Identities;
				for (const std::string& Path : CatalogPaths)
				{
					if (Path.empty())
					{
						continue;
					}

					std::error_code Ec;
					const auto Absolute = std::filesystem::absolute(PathUtil::FromUtf8(Path), Ec);
					const std::string Normalized = Ec ? PathUtil::Normalize(Path) : PathUtil::ToUtf8(Absolute.lexically_normal());
					if (Identities.insert(PathUtil::IdentityKey(Normalized)).second)
					{
						ActiveBindingCatalogPaths.push_back(Normalized);
					}
				}
				BindingCatalogStamps.clear();
				if (ActiveBindingCatalogPaths.empty())
				{
					BindingCatalog.Clear();
					bHasLoadedBindingCatalog = false;
					bBindingCatalogFresh = true;
					BindingCatalogError.clear();
					return;
				}
				VeroScript::FHostBindingRegistry NewCatalog;
				for (const std::string& CatalogPath : ActiveBindingCatalogPaths)
				{
					BindingCatalogStamps.emplace(CatalogPath, BindingCatalogStamp(CatalogPath));
				}
				for (std::size_t Index = 0; Index < ActiveBindingCatalogPaths.size(); ++Index)
				{
					const std::string& CatalogPath = ActiveBindingCatalogPaths[Index];
					std::string Error;
					if (!VeroScript::LoadBindingCatalogFile(
					        CatalogPath, NewCatalog, Error, Index == 0))
					{
						std::cerr << "veroscript-lsp: binding catalog '" << CatalogPath
						          << "': " << Error << "\n";
						bBindingCatalogFresh = false;
						BindingCatalogError = "binding catalog '" + CatalogPath + "': " + Error;
						return;
					}
				}
				BindingCatalog.Swap(NewCatalog);
				bHasLoadedBindingCatalog = true;
				bBindingCatalogFresh = true;
				BindingCatalogError.clear();
			}

			void PublishBindingCatalogStatus()
			{
				if (!bInitializeResponseSent) return;
				if (!LastReportedBindingFresh && bBindingCatalogFresh)
				{
					LastReportedBindingFresh = true;
					return;
				}
				if (LastReportedBindingFresh == bBindingCatalogFresh)
				{
					return;
				}
				LastReportedBindingFresh = bBindingCatalogFresh;
				if (ActiveBindingCatalogPaths.empty()) return;
				const std::string Message = bBindingCatalogFresh
				    ? Locale::TranslateId("lsp.host_bindings_current")
				    : Locale::TranslateId(
				          bHasLoadedBindingCatalog
				              ? "lsp.host_bindings_last_snapshot"
				              : "lsp.host_bindings_unavailable",
				          {BindingCatalogError});
				WriteNotification("window/logMessage", FJson::Object({{"type", FJson::Number(bBindingCatalogFresh ? 3 : 2)}, {"message", FJson::String(Message)}}));
			}

			bool EnsureBindingCatalogFresh(const bool bForce = false)
			{
				if (ActiveBindingCatalogPaths.empty())
				{
					return false;
				}
				bool bChanged = bForce || BindingCatalogStamps.size() != ActiveBindingCatalogPaths.size();
				for (const std::string& CatalogPath : ActiveBindingCatalogPaths)
				{
					const auto Existing = BindingCatalogStamps.find(CatalogPath);
					if (Existing == BindingCatalogStamps.end() || Existing->second != BindingCatalogStamp(CatalogPath))
					{
						bChanged = true;
						break;
					}
				}
				if (!bChanged)
				{
					return false;
				}
				const std::vector<std::string> Paths = ActiveBindingCatalogPaths;
				ReloadBindingCatalogs(Paths);
				PublishBindingCatalogStatus();
				FWorkspaceOptions Options = Workspace.GetOptions();
				ApplyBindingsToOptions(Options);
				Workspace.SetOptions(Options);
				InvalidateSemanticAnalysis();
				ReanalyzeAllOpenDocuments();
				return true;
			}

			bool HasBindingCatalogContent() const
			{
				return !BindingCatalog.GetFunctions().empty() || !BindingCatalog.GetTypes().empty() || !BindingCatalog.GetTypeAliases().empty() || !BindingCatalog.GetGenericTypeTemplates().empty() || !BindingCatalog.GetAnnotations().empty();
			}

			void ApplyBindingsToOptions(FWorkspaceOptions& Options)
			{
				Options.Bindings = HasBindingCatalogContent() ? &BindingCatalog : nullptr;
			}

			std::string SourceForPath(const std::string_view Path) const
			{
				FDocumentOverlay Document;
				if (Workspace.TryGetDocument(Path, Document))
				{
					return std::move(Document.Source);
				}
				if (const auto& Environment = Workspace.GetOptions().ModuleEnvironment)
				{
					if (const auto* Module = Environment->FindSource(std::string(Path));
					    Module && Module->Source)
					{
						return *Module->Source;
					}
				}

				std::ifstream File(PathUtil::FromUtf8(Path), std::ios::binary);
				if (!File)
				{
					return {};
				}
				return std::string(
				    std::istreambuf_iterator<char>(File),
				    std::istreambuf_iterator<char>());
			}

			std::string_view SourceForPath(const std::string_view Path,
			    std::unordered_map<std::string, std::string>& Sources) const
			{
				const auto [Entry, bInserted] = Sources.try_emplace(std::string(Path));
				if (bInserted)
				{
					Entry->second = SourceForPath(Path);
				}
				return Entry->second;
			}

			// ---- Framing / wire I/O

			void WriteFramed(const std::string& Body)
			{
				Out << "Content-Length: " << Body.size() << "\r\n\r\n"
				    << Body;
				Out.flush();
			}

			void WriteMessage(FJson::FObject Message)
			{
				Message["jsonrpc"] = FJson::String("2.0");
				WriteFramed(ToJsonString(FJson::Object(std::move(Message))));
			}

			void WriteResponseResultRaw(const FJson& Id, FJson Result)
			{
				FJson::FObject Message;
				Message["id"] = Id;
				Message["result"] = std::move(Result);
				WriteMessage(std::move(Message));
			}

			void WriteResponseErrorRaw(const FJson& Id, const int Code, const std::string& ErrorMessage)
			{
				FJson::FObject ErrorObject;
				ErrorObject["code"] = FJson::Number(static_cast<double>(Code));
				ErrorObject["message"] = FJson::String(ErrorMessage);
				FJson::FObject Message;
				Message["id"] = Id;
				Message["error"] = FJson::Object(std::move(ErrorObject));
				WriteMessage(std::move(Message));
			}

			static std::string RequestStopMessage(const ERequestStopReason Reason)
			{
				return Locale::TranslateId(Reason == ERequestStopReason::TimedOut
				        ? "lsp.request_timed_out"
				        : "lsp.request_cancelled");
			}

			ERequestStopReason CompleteRequestAndConsumeCancellation(const FJson& Id)
			{
				const std::string Key = ToJsonString(Id);
				std::lock_guard<std::mutex> Lock(IncomingMutex);
				const auto Deadline = ActiveRequestDeadlines.find(Key);
				const bool bTimedOut = Deadline != ActiveRequestDeadlines.end() && std::chrono::steady_clock::now() >= Deadline->second;
				const bool bCancelled = CancelledRequestIds.erase(Key) != 0;
				ActiveRequestDeadlines.erase(Key);
				ActiveRequestIds.erase(Key);
				PendingRequestIds.erase(Key);
				return bTimedOut ? ERequestStopReason::TimedOut
				                 : (bCancelled ? ERequestStopReason::Cancelled : ERequestStopReason::None);
			}

			bool IsRequestCancellationRequested(const FJson& Id)
			{
				const std::string Key = ToJsonString(Id);
				std::lock_guard<std::mutex> Lock(IncomingMutex);
				if (CancelledRequestIds.find(Key) != CancelledRequestIds.end())
				{
					return true;
				}
				const auto Deadline = ActiveRequestDeadlines.find(Key);
				return Deadline != ActiveRequestDeadlines.end() && std::chrono::steady_clock::now() >= Deadline->second;
			}

			void WriteResponseResult(const FJson& Id, FJson Result)
			{
				const ERequestStopReason Reason = CompleteRequestAndConsumeCancellation(Id);
				if (Reason != ERequestStopReason::None)
				{
					WriteResponseErrorRaw(Id,
					    Reason == ERequestStopReason::TimedOut ? -32001 : -32800,
					    RequestStopMessage(Reason));
					return;
				}
				WriteResponseResultRaw(Id, std::move(Result));
			}

			void WriteResponseError(const FJson& Id, const int Code, const std::string& ErrorMessage)
			{
				const ERequestStopReason Reason = CompleteRequestAndConsumeCancellation(Id);
				if (Reason != ERequestStopReason::None)
				{
					WriteResponseErrorRaw(Id,
					    Reason == ERequestStopReason::TimedOut ? -32001 : -32800,
					    RequestStopMessage(Reason));
					return;
				}
				WriteResponseErrorRaw(Id, Code, ErrorMessage);
			}

			void WriteNotification(const std::string& Method, FJson Params)
			{
				FJson::FObject Message;
				Message["method"] = FJson::String(Method);
				Message["params"] = std::move(Params);
				WriteMessage(std::move(Message));
			}

			void RegisterWorkspaceWatcherIfSupported()
			{
				if (!bClientSupportsWatchedFilesDynamicRegistration || (bWorkspaceWatcherRegistered && RegisteredBindingCatalogPaths == ActiveBindingCatalogPaths))
				{
					return;
				}
				FJson::FObject SourceWatcher;
				SourceWatcher["globPattern"] = FJson::String("**/*.vs");
				FJson::FObject PackageWatcher;
				PackageWatcher["globPattern"] = FJson::String("**/*.[vV][sS][lL][iI][bB]");
				FJson::FArray Watchers{FJson::Object(std::move(SourceWatcher)),
				    FJson::Object(std::move(PackageWatcher))};
				for (const std::string& Path : ActiveBindingCatalogPaths)
				{
					const auto NativePath = PathUtil::FromUtf8(Path);
					const std::string MatchPath = bClientSupportsRelativeWatchPatterns
					    ? PathUtil::ToUtf8(NativePath.filename())
					    : Path;
					std::string Pattern;
					for (const char C : MatchPath)
					{
						// Glob metacharacters in a filename must match literally.
						if (C == '*' || C == '?' || C == '[' || C == ']' || C == '{' || C == '}')
						{
							Pattern += std::string("[") + C + "]";
						}

						else
						{
							Pattern += C;
						}
					}
					FJson Glob = bClientSupportsRelativeWatchPatterns
					    ? FJson::Object({{"baseUri", FJson::String(PathToUri(PathUtil::ToUtf8(NativePath.parent_path())))},
					          {"pattern", FJson::String(std::move(Pattern))}})
					    : FJson::String(std::move(Pattern));
					Watchers.push_back(FJson::Object({{"globPattern", std::move(Glob)}}));
				}
				if (bWorkspaceWatcherRegistered)
				{
					WriteMessage({{"id", FJson::String("veroscript-unregister-workspace-watchers-" + std::to_string(NextWatcherRequestId++))},
					    {"method", FJson::String("client/unregisterCapability")},
					    {"params", FJson::Object({{"unregisterations", FJson::Array({FJson::Object({{"id", FJson::String(RegisteredWatcherId)}, {"method", FJson::String("workspace/didChangeWatchedFiles")}})})}})}});
				}
				FJson::FObject RegisterOptions;
				RegisterOptions["watchers"] = FJson::Array(std::move(Watchers));
				RegisteredWatcherId = "veroscript-workspace-vs-files-" + std::to_string(NextWatcherRequestId);
				FJson::FObject Registration;
				Registration["id"] = FJson::String(RegisteredWatcherId);
				Registration["method"] = FJson::String("workspace/didChangeWatchedFiles");
				Registration["registerOptions"] = FJson::Object(std::move(RegisterOptions));
				FJson::FObject Params;
				Params["registrations"] = FJson::Array({FJson::Object(std::move(Registration))});
				FJson::FObject Message;
				Message["id"] = FJson::String("veroscript-register-workspace-watchers-" + std::to_string(NextWatcherRequestId++));
				Message["method"] = FJson::String("client/registerCapability");
				Message["params"] = FJson::Object(std::move(Params));
				WriteMessage(std::move(Message));
				bWorkspaceWatcherRegistered = true;
				RegisteredBindingCatalogPaths = ActiveBindingCatalogPaths;
			}

			std::optional<std::string> ReadMessage(bool& OutFramingError)
			{
				Internal::FMessageReadResult Result =
				    Internal::ReadContentLengthMessage(In);
				OutFramingError = Result.Status == Internal::EMessageReadStatus::InvalidFrame;
				if (Result.Status != Internal::EMessageReadStatus::Message)
				{
					return std::nullopt;
				}
				return std::move(Result.Body);
			}

			bool QueueIncomingMessage(FJson Root, const bool bParseError = false)
			{
				const std::string Method = Root.GetString("method");
				if (Method == "$/cancelRequest")
				{
					const FJson* const Params = Root.Find("params");
					if (Params)
					{
						if (const FJson* const CancelledId = Params->Find("id"))
						{
							const std::string Key = ToJsonString(*CancelledId);
							std::lock_guard<std::mutex> Lock(IncomingMutex);
							if (PendingRequestIds.find(Key) != PendingRequestIds.end() || ActiveRequestIds.find(Key) != ActiveRequestIds.end())
							{
								CancelledRequestIds.insert(Key);
							}
						}
					}
					// Wake the protocol worker too: a cancelled queued analysis must
					// not wait for the active analysis or its original deadline.
					IncomingCondition.notify_all();
					return false;
				}

				const FJson* const Id = Root.Find("id");
				{
					std::unique_lock<std::mutex> Lock(IncomingMutex);
					// Preserve transport backpressure rather than allowing an unbounded
					// queue when a client sends requests faster than semantic analysis can
					// consume them. Cancellation does not wait for semantic work once its
					// notification has been read.
					IncomingCondition.wait(Lock, [this]()
					{
						return IncomingMessages.size() < MaxQueuedMessages;
					});
					// JSON-RPC responses to server-originated requests also carry an id,
					// but they are not client requests and must not enter cancellation state.
					if (Id && !Method.empty())
					{
						PendingRequestIds.insert(ToJsonString(*Id));
					}
					IncomingMessages.emplace_back(std::move(Root), bParseError);
				}
				IncomingCondition.notify_all();
				// `exit` is ordered after the earlier queued messages.  Stop accepting
				// new input, then let the worker perform the ordered shutdown sequence.
				return Method == "exit";
			}

			void RunWorker()
			{
				std::size_t IncomingBurst = 0;
				for (;;)
				{
					FJson Root;
					bool bParseError = false;
					std::shared_ptr<FSemanticAnalysisJob> Completed;
					{
						std::unique_lock<std::mutex> Lock(IncomingMutex);
						for (;;)
						{
							ExpireQueuedSemanticJobs();
							if (!IncomingMessages.empty() || !CompletedSemanticJobs.empty() || (bInputClosed && PendingSemanticJobs == 0))
							{
								break;
							}
							auto Wake = std::chrono::steady_clock::time_point::max();
							for (const auto& Job : SemanticJobs)
							{
								const auto Deadline = ActiveRequestDeadlines.find(ToJsonString(Job->Id));
								if (Deadline != ActiveRequestDeadlines.end())
								{
									Wake = (std::min)(Wake, Deadline->second);
								}
							}
							if (Wake == std::chrono::steady_clock::time_point::max())
							{
								IncomingCondition.wait(Lock);
							}
							else
							{
								IncomingCondition.wait_until(Lock, Wake);
							}
						}
						if (!IncomingMessages.empty() && (CompletedSemanticJobs.empty() || IncomingBurst < 8))
						{
							++IncomingBurst;
							Root = std::move(IncomingMessages.front().first);
							bParseError = IncomingMessages.front().second;
							IncomingMessages.pop_front();
						}
						else if (!CompletedSemanticJobs.empty())
						{
							IncomingBurst = 0;
							Completed = std::move(CompletedSemanticJobs.front());
							CompletedSemanticJobs.pop_front();
							--PendingSemanticJobs;
						}
						else
						{
							return;
						}
					}
					IncomingCondition.notify_all();
					if (Completed)
					{
						CompleteSemanticJob(*Completed);
					}
					else if (bParseError)
					{
						WriteResponseErrorRaw(FJson::Null(), -32700, "Parse error");
					}
					else
					{
						ProcessMessage(Root);
					}
				}
			}

			bool BeginRequest(const FJson& Id, const std::string& Method)
			{
				const std::string Key = ToJsonString(Id);
				std::lock_guard<std::mutex> Lock(IncomingMutex);
				PendingRequestIds.erase(Key);
				ActiveRequestIds.insert(Key);
				const bool bAnalysis = Method == "textDocument/semanticTokens/full" || Method == "textDocument/semanticTokens/full/delta";
				ActiveRequestDeadlines[Key] = std::chrono::steady_clock::now() + (bAnalysis ? AnalysisTimeout : RequestTimeout);
				if (CancelledRequestIds.erase(Key) != 0)
				{
					ActiveRequestIds.erase(Key);
					ActiveRequestDeadlines.erase(Key);
					return false;
				}
				return true;
			}

			// ---- Dispatch

			void ProcessMessage(const FJson& Root)
			try
			{
				const std::string Method = Root.GetString("method");
				if (Method.empty())
				{
					// Response to a client-bound registration request. Registration is a
					// best-effort cache invalidation aid and has no response payload to apply.
					return;
				}

				// Pick up bindings.json edits without requiring didChangeConfiguration.
				if (Method != "initialize" && Method != "shutdown" && Method != "exit" && Method != "$/cancelRequest" && Method != "workspace/didChangeWatchedFiles")
				{
					EnsureBindingCatalogFresh();
				}

				const FJson* IdField = Root.Find("id");
				if (Method == "workspace/didChangeWatchedFiles" || Method == "workspace/didChangeWorkspaceFolders")
				{
					InvalidateSemanticAnalysis();
				}
				const FJson* ParamsField = Root.Find("params");
				const FJson EmptyParams = FJson::Object();
				const FJson& Params = ParamsField ? *ParamsField : EmptyParams;
				const bool bIsRequest = IdField != nullptr;
				const FJson RequestId = IdField ? *IdField : FJson::Null();
				if (bIsRequest)
				{
					if (!BeginRequest(RequestId, Method))
					{
						WriteResponseErrorRaw(RequestId, -32800,
						    Locale::TranslateId("lsp.request_cancelled"));
						return;
					}
				}
				// Foreground AST queries must observe cancellation/deadlines while
				// loading and parsing, rather than only when serializing a response.
				std::optional<FAnalysisControlScope> RequestControl;
				if (bIsRequest)
				{
					RequestControl.emplace([this, RequestId]()
					{
						return IsRequestCancellationRequested(RequestId);
					});
					CheckAnalysisCancellation();
				}
				std::shared_ptr<const Detail::FSemanticQuerySnapshot> QuerySnapshot;
				if (bIsRequest)
				{
					if (const FJson* Document = Params.Find("textDocument"))
					{
						const auto Ready = ReadySemanticTokens.find(PathUtil::IdentityKey(
						    UriToPath(Document->GetString("uri"))));
						if (Ready != ReadySemanticTokens.end() &&
						    Ready->second.Revision == SemanticRevision.load() &&
						    Detail::IsSemanticQuerySnapshotCacheable(Ready->second.Snapshot) &&
						    Detail::IsSemanticQuerySnapshotCurrent(Ready->second.Snapshot))
						{
							QuerySnapshot = Ready->second.Snapshot;
						}
					}
				}
				Detail::FSemanticQueryScope QueryScope(std::move(QuerySnapshot));

				if (Method == "initialize")
				{
					HandleInitialize(RequestId, Params);
					return;
				}
				if (Method == "initialized")
				{
					RegisterWorkspaceWatcherIfSupported();
					return;
				}
				if (Method == "shutdown")
				{
					bShutdownReceived = true;
					WriteResponseResult(RequestId, FJson::Null());
					return;
				}
				if (Method == "exit")
				{
					bExit = true;
					// The client has ended the session. Retire queued snapshots and stop
					// active analysis instead of delaying process exit until its deadline.
					{
						std::lock_guard<std::mutex> Lock(IncomingMutex);
						CancelledRequestIds.insert(ActiveRequestIds.begin(), ActiveRequestIds.end());
					}
					IncomingCondition.notify_all();
					return;
				}
				if (Method == "textDocument/didOpen")
				{
					HandleDidOpen(Params);
					return;
				}
				if (Method == "textDocument/didChange")
				{
					HandleDidChange(Params);
					return;
				}
				if (Method == "textDocument/didSave")
				{
					HandleDidSave(Params);
					return;
				}
				if (Method == "textDocument/didClose")
				{
					HandleDidClose(Params);
					return;
				}
				if (Method == "textDocument/completion")
				{
					HandleCompletion(RequestId, Params);
					return;
				}
				if (Method == "completionItem/resolve")
				{
					HandleCompletionResolve(RequestId, Params);
					return;
				}
				if (Method == "textDocument/hover")
				{
					HandleHover(RequestId, Params);
					return;
				}
				if (Method == "textDocument/definition")
				{
					HandleDefinition(RequestId, Params);
					return;
				}
				if (Method == "textDocument/declaration")
				{
					// This request targets the original declaration, including prototypes.
					HandleDefinition(RequestId, Params);
					return;
				}
				if (Method == "textDocument/typeDefinition")
				{
					HandleTypeDefinition(RequestId, Params);
					return;
				}
				if (Method == "textDocument/implementation")
				{
					HandleImplementation(RequestId, Params);
					return;
				}
				if (Method == "textDocument/prepareTypeHierarchy")
				{
					HandlePrepareTypeHierarchy(RequestId, Params);
					return;
				}
				if (Method == "typeHierarchy/supertypes")
				{
					HandleTypeHierarchySupertypes(RequestId, Params);
					return;
				}
				if (Method == "typeHierarchy/subtypes")
				{
					HandleTypeHierarchySubtypes(RequestId, Params);
					return;
				}
				if (Method == "textDocument/documentSymbol")
				{
					HandleDocumentSymbol(RequestId, Params);
					return;
				}
				if (Method == "textDocument/selectionRange")
				{
					HandleSelectionRange(RequestId, Params);
					return;
				}
				if (Method == "textDocument/documentLink")
				{
					HandleDocumentLink(RequestId, Params);
					return;
				}
				if (Method == "veroscript/moduleSource")
				{
					HandleModuleSource(RequestId, Params);
					return;
				}
				if (Method == "textDocument/foldingRange")
				{
					HandleFoldingRange(RequestId, Params);
					return;
				}
				if (Method == "textDocument/semanticTokens/full")
				{
					HandleSemanticTokens(RequestId, Params);
					return;
				}
				if (Method == "textDocument/signatureHelp")
				{
					HandleSignatureHelp(RequestId, Params);
					return;
				}
				if (Method == "textDocument/references")
				{
					HandleReferences(RequestId, Params);
					return;
				}
				if (Method == "textDocument/documentHighlight")
				{
					HandleDocumentHighlight(RequestId, Params);
					return;
				}
				if (Method == "textDocument/rename")
				{
					HandleRename(RequestId, Params);
					return;
				}
				if (Method == "textDocument/semanticTokens/full/delta")
				{
					HandleSemanticTokensDelta(RequestId, Params);
					return;
				}
				if (Method == "textDocument/prepareRename")
				{
					HandlePrepareRename(RequestId, Params);
					return;
				}
				if (Method == "textDocument/formatting")
				{
					HandleFormatting(RequestId, Params);
					return;
				}
				if (Method == "workspace/symbol")
				{
					HandleWorkspaceSymbol(RequestId, Params);
					return;
				}
				if (Method == "workspaceSymbol/resolve")
				{
					HandleWorkspaceSymbolResolve(RequestId, Params);
					return;
				}
				if (Method == "workspace/didChangeConfiguration")
				{
					HandleDidChangeConfiguration(Params);
					return;
				}
				if (Method == "textDocument/rangeFormatting")
				{
					HandleRangeFormatting(RequestId, Params);
					return;
				}
				if (Method == "textDocument/onTypeFormatting")
				{
					HandleOnTypeFormatting(RequestId, Params);
					return;
				}
				if (Method == "textDocument/codeAction")
				{
					HandleCodeAction(RequestId, Params);
					return;
				}
				if (Method == "codeAction/resolve")
				{
					HandleCodeActionResolve(RequestId, Params);
					return;
				}
				if (Method == "workspace/didChangeWorkspaceFolders")
				{
					HandleDidChangeWorkspaceFolders(Params);
					return;
				}
				if (Method == "workspace/didChangeWatchedFiles")
				{
					// An explicit event is authoritative even when timestamps and size
					// were preserved by the writer. Reload once, then invalidate queries.
					if (EnsureBindingCatalogFresh(HasBindingCatalogChange(Params))) return;
					// Workspace symbols include closed files on disk. A file watcher event
					// must invalidate the incremental index even when no open overlay changed.
					Workspace.SetOptions(Workspace.GetOptions());
					ReanalyzeAllOpenDocuments();
					return;
				}

				if (bIsRequest)
				{
					WriteResponseError(
					    RequestId,
					    -32601,
					    Locale::TranslateId("lsp.protocol.method_not_found", {Method}));
				}
				// Unknown notifications are intentionally no-ops.
			}
			catch (const FAnalysisCancelled&)
			{
				if (const FJson* Id = Root.Find("id"))
				{
					WriteResponseError(*Id, -32800, Locale::TranslateId("lsp.request_cancelled"));
				}
			}
			catch (const std::exception& Error)
			{
				// A query failure must not escape the ordered worker and terminate
				// the process while its reader/analysis threads are still joinable.
				if (const FJson* Id = Root.Find("id"))
				{
					WriteResponseError(*Id, -32603, Error.what());
				}
				else
				{
					std::cerr << "veroscript-lsp: notification failed: " << Error.what() << '\n';
				}
			}

			// ---- Lifecycle

			std::vector<std::string> WorkspaceRootsFromInitialize(const FJson& Params) const
			{
				std::vector<std::string> Roots;
				bool bWorkspaceFoldersProvided = false;
				if (const FJson* const Folders = Params.Find("workspaceFolders"))
				{
					if (const FJson::FArray* const Array = Folders->ArrayPtr())
					{
						// An explicitly empty array means that the client currently has no
						// workspace folders. LSP says rootUri is the legacy fallback only
						// when workspaceFolders is absent/null, not when it is an empty list.
						bWorkspaceFoldersProvided = true;
						for (const FJson& Folder : *Array)
						{
							const std::string Path = UriToPath(Folder.GetString("uri"));
							if (!Path.empty())
							{
								Roots.push_back(Path);
							}
						}
					}
				}
				if (!bWorkspaceFoldersProvided)
				{
					const std::string Root = UriToPath(Params.GetString("rootUri"));
					if (!Root.empty())
					{
						Roots.push_back(Root);
					}
				}
				std::vector<std::string> UniqueRoots;
				for (const std::string& Root : Roots)
				{
					if (std::none_of(
					        UniqueRoots.begin(), UniqueRoots.end(), [&Root](const std::string& Existing)
					{
						return PathUtil::Equal(Existing, Root);
					}))
					{
						UniqueRoots.push_back(Root);
					}
				}
				return UniqueRoots;
			}

			void SetWorkspaceRoots(std::vector<std::string> Roots)
			{
				FWorkspaceOptions NewOptions = Workspace.GetOptions();
				NewOptions.WorkspaceRoots = std::move(Roots);
				Workspace.SetOptions(NewOptions);
			}

			void HandleDidChangeWorkspaceFolders(const FJson& Params)
			{
				const FJson* const Event = Params.Find("event");
				if (!Event)
				{
					return;
				}
				FWorkspaceOptions NewOptions = Workspace.GetOptions();
				if (const FJson* const Removed = Event->Find("removed"))
				{
					if (const FJson::FArray* const Array = Removed->ArrayPtr())
					{
						for (const FJson& Folder : *Array)
						{
							const std::string RemovedPath = UriToPath(Folder.GetString("uri"));
							NewOptions.WorkspaceRoots.erase(
							    std::remove_if(
							        NewOptions.WorkspaceRoots.begin(), NewOptions.WorkspaceRoots.end(),
							        [&RemovedPath](const std::string& Root)
							{
								return PathUtil::Equal(Root, RemovedPath);
							}),
							    NewOptions.WorkspaceRoots.end());
						}
					}
				}
				if (const FJson* const Added = Event->Find("added"))
				{
					if (const FJson::FArray* const Array = Added->ArrayPtr())
					{
						for (const FJson& Folder : *Array)
						{
							const std::string AddedPath = UriToPath(Folder.GetString("uri"));
							if (!AddedPath.empty() && std::none_of(NewOptions.WorkspaceRoots.begin(), NewOptions.WorkspaceRoots.end(), [&AddedPath](const std::string& Root)
							{
								return PathUtil::Equal(Root, AddedPath);
							}))
							{
								NewOptions.WorkspaceRoots.push_back(AddedPath);
							}
						}
					}
				}
				Workspace.SetOptions(NewOptions);
				ReanalyzeAllOpenDocuments();
			}

			void HandleInitialize(const FJson& Id, const FJson& Params)
			{
				if (const FJson* const ClientCapabilities = Params.Find("capabilities"))
				{
					if (const FJson* const TextDocument = ClientCapabilities->Find("textDocument"))
					{
						if (const FJson* const Completion = TextDocument->Find("completion"))
						{
							if (const FJson* const CompletionItem = Completion->Find("completionItem"))
							{
								bClientSupportsCompletionSnippets =
								    CompletionItem->GetBool("snippetSupport", false);
							}
						}
					}
					if (const FJson* const WorkspaceCapabilities =
					        ClientCapabilities->Find("workspace"))
					{
						if (const FJson* const WatchedFiles =
						        WorkspaceCapabilities->Find("didChangeWatchedFiles"))
						{
							bClientSupportsWatchedFilesDynamicRegistration =
							    WatchedFiles->GetBool("dynamicRegistration", false);
							bClientSupportsRelativeWatchPatterns =
							    WatchedFiles->GetBool("relativePatternSupport", false);
						}
					}
				}
				const std::string RequestedLocale = Params.GetString("locale");
				if (!RequestedLocale.empty())
				{
					// LSP 3.16 initialize.locale is a BCP 47 preference. An
					// unsupported preference leaves the environment-selected catalog intact.
					(void)Locale::TrySetLanguageFromTag(RequestedLocale);
				}
				if (const FJson* InitializationOptions = Params.Find("initializationOptions"))
				{
					ApplySettingsRoot(*InitializationOptions);
				}
				if (Params.Find("workspaceFolders") || !Params.GetString("rootUri").empty())
				{
					SetWorkspaceRoots(WorkspaceRootsFromInitialize(Params));
				}

				FJson::FObject CompletionProvider;
				CompletionProvider["resolveProvider"] = FJson::Bool(true);
				FJson::FArray CompletionTriggers;
				CompletionTriggers.push_back(FJson::String("."));
				CompletionTriggers.push_back(FJson::String(":"));
				CompletionTriggers.push_back(FJson::String("@"));
				CompletionProvider["triggerCharacters"] = FJson::Array(std::move(CompletionTriggers));

				FJson::FArray SignatureTriggers;
				SignatureTriggers.push_back(FJson::String("("));
				SignatureTriggers.push_back(FJson::String(","));
				FJson::FObject SignatureHelpProvider;
				SignatureHelpProvider["triggerCharacters"] = FJson::Array(std::move(SignatureTriggers));

				FJson::FObject TextDocumentSync;
				TextDocumentSync["openClose"] = FJson::Bool(true);
				TextDocumentSync["change"] = FJson::Number(2); // Incremental document sync.
				TextDocumentSync["save"] = FJson::Object({{"includeText", FJson::Bool(true)}});

				FJson::FObject Capabilities;
				Capabilities["textDocumentSync"] = FJson::Object(std::move(TextDocumentSync));
				Capabilities["completionProvider"] = FJson::Object(std::move(CompletionProvider));
				Capabilities["hoverProvider"] = FJson::Bool(true);
				Capabilities["definitionProvider"] = FJson::Bool(true);
				Capabilities["declarationProvider"] = FJson::Bool(true);
				Capabilities["typeDefinitionProvider"] = FJson::Bool(true);
				Capabilities["implementationProvider"] = FJson::Bool(true);
				Capabilities["typeHierarchyProvider"] = FJson::Bool(true);
				Capabilities["documentSymbolProvider"] = FJson::Bool(true);
				Capabilities["selectionRangeProvider"] = FJson::Bool(true);
				Capabilities["documentLinkProvider"] = FJson::Object(
				    {{"resolveProvider", FJson::Bool(false)}});
				Capabilities["foldingRangeProvider"] = FJson::Bool(true);
				FJson::FArray SemanticTokenTypes;
				for (const char* Type : {
				         "namespace", "type", "class", "enum", "interface", "struct",
				         "variable", "property", "enumMember", "function", "method",
				         "macro", "keyword", "decorator", "decoratorArgument",
				         "constant", "label", "string", "number", "comment", "preprocessor"})
				{
					SemanticTokenTypes.push_back(FJson::String(Type));
				}
				FJson::FObject SemanticLegend;
				SemanticLegend["tokenTypes"] = FJson::Array(std::move(SemanticTokenTypes));
				SemanticLegend["tokenModifiers"] = FJson::Array({FJson::String("readonly")});
				FJson::FObject SemanticProvider;
				SemanticProvider["legend"] = FJson::Object(std::move(SemanticLegend));
				SemanticProvider["full"] = FJson::Object({{"delta", FJson::Bool(true)}});
				SemanticProvider["range"] = FJson::Bool(false);
				Capabilities["semanticTokensProvider"] =
				    FJson::Object(std::move(SemanticProvider));
				Capabilities["signatureHelpProvider"] = FJson::Object(std::move(SignatureHelpProvider));
				Capabilities["referencesProvider"] = FJson::Bool(true);
				Capabilities["documentHighlightProvider"] = FJson::Bool(true);
				Capabilities["renameProvider"] = FJson::Object(
				    {{"prepareProvider", FJson::Bool(true)}});
				Capabilities["documentFormattingProvider"] = FJson::Bool(true);
				Capabilities["documentRangeFormattingProvider"] = FJson::Bool(true);
				Capabilities["documentOnTypeFormattingProvider"] = FJson::Object({{"firstTriggerCharacter", FJson::String(";")},
				    {"moreTriggerCharacter", FJson::Array({FJson::String("}"), FJson::String("\n")})}});
				Capabilities["workspaceSymbolProvider"] = FJson::Object(
				    {{"resolveProvider", FJson::Bool(true)}});
				Capabilities["workspace"] = FJson::Object({{"workspaceFolders", FJson::Object({{"supported", FJson::Bool(true)}, {"changeNotifications", FJson::Bool(true)}})}});
				Capabilities["codeActionProvider"] = FJson::Object(
				    {{"codeActionKinds", FJson::Array({FJson::String("source.formatDocument"), FJson::String("source.organizeImports"), FJson::String("refactor.rewrite")})},
				        {"resolveProvider", FJson::Bool(true)}});

				FJson::FObject ServerInfo;
				if (RunOptions.ModuleEnvironment)
				{
					Capabilities["experimental"] = FJson::Object({{"veroscriptModuleEnvironment", FJson::Object({{"compileFingerprint", FJson::String(RunOptions.ModuleEnvironment->GetCompileFingerprint())}, {"executionFingerprint", FJson::String(RunOptions.ModuleEnvironment->GetExecutionFingerprint())}, {"moduleSourceMethod", FJson::String("veroscript/moduleSource")}})}});
				}
				ServerInfo["name"] = FJson::String("veroscript-lsp");
				ServerInfo["version"] = FJson::String(VeroScript::GetVersionString());
				ServerInfo["platform"] = FJson::String(VeroScript::GetHostPlatformString());
				ServerInfo["jitCapability"] = FJson::String(VeroScript::GetJitCapabilityString());

				FJson::FObject Result;
				Result["capabilities"] = FJson::Object(std::move(Capabilities));
				Result["serverInfo"] = FJson::Object(std::move(ServerInfo));
				WriteResponseResult(Id, FJson::Object(std::move(Result)));
				bInitializeResponseSent = true;
				if (!bBindingCatalogFresh)
				{
					PublishBindingCatalogStatus();
				}
			}

			// ---- Document sync

			void HandleDidOpen(const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Text = TextDocument->GetString("text");
				InvalidateSemanticAnalysis(Path);
				InvalidateDiagnosticTarget(Path);
				Workspace.OpenDocument(Path, Text);
				// A repeated didOpen starts a new document lifetime; do not carry a
				// stable snapshot from a previous lifetime of the same URI.
				SemanticTokenSnapshots.erase(PathUtil::IdentityKey(Path));
				DocumentVersions[PathUtil::IdentityKey(Path)] =
				    TextDocument->GetInt("version", 0);
				PublishDiagnosticsForDocument(Path);
			}

			void HandleDidChange(const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::int64_t Version = TextDocument->GetInt("version", -1);
				const std::string VersionKey = PathUtil::IdentityKey(Path);
				if (const auto Found = DocumentVersions.find(VersionKey);
				    Version >= 0 && Found != DocumentVersions.end() && Version <= Found->second)
				{
					return;
				}
				std::string LatestText = SourceForPath(Path);
				bool bHaveText = false;
				if (const FJson* Changes = Params.Find("contentChanges"))
				{
					if (const FJson::FArray* Array = Changes->ArrayPtr())
					{
						for (const FJson& Change : *Array)
						{
							if (const FJson* TextField = Change.Find("text"))
							{
								if (const std::string* Text = std::get_if<std::string>(&TextField->Value))
								{
									if (const FJson* Range = Change.Find("range"))
									{
										const FJson* Start = Range->Find("start");
										const FJson* End = Range->Find("end");
										if (!Start || !End)
										{
											return;
										}
										const std::size_t StartOffset = LspPositionToSourceOffset(LatestText, *Start);
										const std::size_t EndOffset = LspPositionToSourceOffset(LatestText, *End);
										if (EndOffset < StartOffset)
										{
											return;
										}
										LatestText.replace(StartOffset, EndOffset - StartOffset, *Text);
									}
									else
									{
										LatestText = *Text;
									}
									bHaveText = true;
								}
							}
						}
					}
				}
				if (!bHaveText)
				{
					return;
				}
				InvalidateSemanticAnalysis(Path);
				InvalidateDiagnosticTarget(Path);
				Workspace.UpdateDocument(Path, LatestText);
				if (Version >= 0)
				{
					DocumentVersions[VersionKey] = Version;
				}
				PublishDiagnosticsForDocument(Path);
			}

			void HandleDidSave(const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				InvalidateSemanticAnalysis(Path);
				InvalidateDiagnosticTarget(Path);
				if (const FJson* TextField = Params.Find("text"))
				{
					if (const std::string* Text = std::get_if<std::string>(&TextField->Value))
					{
						Workspace.UpdateDocument(Path, *Text);
					}
				}
				PublishDiagnosticsForDocument(Path);
			}

			void HandleDidClose(const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				InvalidateSemanticAnalysis(Path);
				InvalidateDiagnosticTarget(Path);
				SemanticDocumentRevisions.erase(PathUtil::IdentityKey(Path));
				Workspace.CloseDocument(Path);
				DocumentVersions.erase(PathUtil::IdentityKey(Path));
				SemanticTokenSnapshots.erase(PathUtil::IdentityKey(Path));
				ReplaceDiagnosticContributions(Path, nullptr);
			}

			// ---- Language queries

			bool ExtractDocumentPosition(
			    const FJson& Params,
			    std::string& OutPath,
			    FTextPosition& OutPosition)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				const FJson* PositionField = Params.Find("position");
				if (!TextDocument || !PositionField)
				{
					return false;
				}
				OutPath = UriToPath(TextDocument->GetString("uri"));
				OutPosition = PositionFromJson(*PositionField, SourceForPath(OutPath));
				return true;
			}

			void HandleCompletion(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}

				const std::string Source = SourceForPath(Path);
				// LSP can only register one-character triggers, so `:` fires after both
				// colons. Suppress single-colon contexts (`public:`, `case ...:` etc.)
				// and retain automatic completion only after a complete `::`.
				if (const FJson* Context = Params.Find("context");
				    Context && Context->GetString("triggerCharacter") == ":")
				{
					const std::string_view Line = SourceLine(Source, Position.Line);
					const std::size_t CaretOffset =
					    Position.Column > 0 ? static_cast<std::size_t>(Position.Column - 1) : 0;
					if (CaretOffset < 2 || CaretOffset > Line.size() || Line.substr(CaretOffset - 2, 2) != "::")
					{
						FJson::FObject EmptyCompletionList;
						EmptyCompletionList["isIncomplete"] = FJson::Bool(false);
						EmptyCompletionList["items"] = FJson::Array();
						WriteResponseResult(Id, FJson::Object(std::move(EmptyCompletionList)));
						return;
					}
				}

				const FCompletionResult Result = Workspace.CompleteAtDocument(Path, Position);
				FJson::FArray Items;
				Items.reserve(std::min(Result.Items.size(), MaxCompletionItems));
				const bool bPreselectFirst = !Result.Prefix.empty() && !Result.Items.empty() && Result.Items.front().MatchQuality <= 2u && (Result.Items.size() == 1u || Result.Items[1].MatchQuality > Result.Items.front().MatchQuality);
				for (std::size_t ItemIndex = 0;
				    ItemIndex < Result.Items.size() && Items.size() < MaxCompletionItems;
				    ++ItemIndex)
				{
					const FCompletionItem& Item = Result.Items[ItemIndex];
					const bool bUseSnippet = Item.bInsertTextSnippet && bClientSupportsCompletionSnippets;
					std::string InsertText = Item.bInsertTextSnippet && !bUseSnippet
					    ? Item.Label
					    : Item.InsertText.empty() ? Item.Label
					                              : Item.InsertText;
					if (Item.bInsertTextSnippet && !bUseSnippet && Item.InsertText.rfind("std::", 0) == 0 && (Item.Kind == EOutlineSymbolKind::Function || Item.Kind == EOutlineSymbolKind::Method))
					{
						// Keep a required namespace when the client cannot apply snippets.
						InsertText = Item.InsertText.substr(0, Item.InsertText.find_first_of("(<"));
					}
					if (Item.bOverrideMember && !bUseSnippet)
					{
						InsertText = Item.InsertText;
						const std::size_t Stub = InsertText.find("${0:throw 0;}");
						if (Stub != std::string::npos)
						{
							InsertText.replace(Stub, 13, "throw 0;");
						}
					}

					FJson::FObject ItemObject;
					ItemObject["label"] = FJson::String(Item.Label);
					if (!Item.LabelDescription.empty())
					{
						ItemObject["labelDetails"] = FJson::Object({{"description", FJson::String(Item.LabelDescription)}});
						// Optional Valence display metadata. Standard LSP clients ignore
						// this field and still receive the leaf label and exact textEdit.
						ItemObject["$valencePresentation"] =
						    BuildQualifiedCompletionPresentation(Item);
					}
					// Keep filtering stable when label presentation contains overload/type
					// decoration.  The raw identifier is what a client should fuzzy-match.
					ItemObject["filterText"] = FJson::String(Item.FilterText.empty() ? Item.Label : Item.FilterText);
					if (ItemIndex == 0 && bPreselectFirst)
					{
						ItemObject["preselect"] = FJson::Bool(true);
					}
					const bool bAnnotation = Item.Detail.rfind("annotation", 0) == 0;
					ItemObject["kind"] = FJson::Number(static_cast<double>(
					    bAnnotation ? 15 : OutlineKindToCompletionItemKind(Item.Kind)));
					if (Item.Kind == EOutlineSymbolKind::Annotation || Item.Kind == EOutlineSymbolKind::AnnotationArgument || Item.Kind == EOutlineSymbolKind::Macro || Item.Kind == EOutlineSymbolKind::Namespace)
					{
						FJson::FObject Data;
						Data["veroscriptKind"] = FJson::String(
						    Item.Kind == EOutlineSymbolKind::AnnotationArgument
						        ? "annotationArgument"
						        : Item.Kind == EOutlineSymbolKind::Macro
						        ? "macro"
						        : Item.Kind == EOutlineSymbolKind::Namespace
						        ? "namespace"
						        : "annotation");
						ItemObject["data"] = FJson::Object(std::move(Data));
					}
					if (!Item.Detail.empty())
					{
						ItemObject["detail"] = FJson::String(Item.Detail);
					}
					if (!Item.Documentation.empty())
					{
						ItemObject["documentation"] = FJson::String(Item.Documentation);
					}
					else if (Item.bSoftBuiltin)
					{
						// Prefer richer soft-type blurbs ("soft builtin (delegate.vs…)") when
						// Detail already carries them; otherwise emit the standard tag.
						const std::string Documentation =
						    Item.Detail.rfind("soft builtin", 0) == 0 || bAnnotation
						    ? Item.Detail
						    : "soft builtin";
						ItemObject["documentation"] = FJson::String(Documentation);
					}
					ItemObject["insertText"] = FJson::String(InsertText);
					if (bUseSnippet)
					{
						ItemObject["insertTextFormat"] = FJson::Number(2); // Snippet
					}
					{
						// Match quality is the primary key and contextual priority is the
						// secondary key. Label/detail/insert text exactly mirror the final
						// server comparator so client-side sortText ordering cannot reshuffle it.
						char SortBuf[24];
						std::snprintf(
						    SortBuf,
						    sizeof(SortBuf),
						    "%02u_%04u_",
						    static_cast<unsigned>(Item.MatchQuality),
						    static_cast<unsigned>(Item.SortPriority));
						ItemObject["sortText"] = FJson::String(
						    std::string(SortBuf) + Item.Label + '\x1f' + Item.Detail + '\x1f' + InsertText);
					}
					if (Result.bHasReplaceRange || Item.bOverrideMember)
					{
						FTextRange EditRange = Result.ReplaceRange;
						if (!Result.bHasReplaceRange)
						{
							EditRange.StartLine = Position.Line;
							EditRange.StartColumn = Position.Column;
							EditRange.EndLine = Position.Line;
							EditRange.EndColumn = Position.Column;
						}
						FJson::FObject TextEdit;
						TextEdit["range"] = RangeToJson(EditRange, Source);
						TextEdit["newText"] = FJson::String(InsertText);
						ItemObject["textEdit"] = FJson::Object(std::move(TextEdit));
					}
					Items.push_back(FJson::Object(std::move(ItemObject)));
				}

				FJson::FObject CompletionList;
				CompletionList["isIncomplete"] = FJson::Bool(
				    Items.size() < Result.Items.size());
				CompletionList["items"] = FJson::Array(std::move(Items));
				WriteResponseResult(Id, FJson::Object(std::move(CompletionList)));
			}

			void HandleCompletionResolve(const FJson& Id, const FJson& Params)
			{
				// Completion is intentionally light-weight. Resolve keeps the original
				// item shape and promotes its detail into Markdown documentation only
				// when the client asks for it, so initial lists remain compact.
				FJson::FObject Item = Params.ObjectPtr() ? *Params.ObjectPtr() : FJson::FObject{};
				if (Item.find("documentation") == Item.end())
				{
					if (const auto Detail = Item.find("detail"); Detail != Item.end())
					{
						if (const std::string* const Text = std::get_if<std::string>(&Detail->second.Value))
						{
							Item["documentation"] = FJson::Object(
							    {{"kind", FJson::String("markdown")}, {"value", FJson::String(*Text)}});
						}
					}
				}
				WriteResponseResult(Id, FJson::Object(std::move(Item)));
			}

			void HandleHover(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}

				// Recovery belongs to the live query. Matching token text alone cannot
				// validate an older receiver, declaration, import or binding catalog.
				const FHoverResult Result = Workspace.HoverDocument(Path, Position);
				if (!Result.bHasToken || Result.Contents.empty())
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}

				FJson::FObject Contents;
				Contents["kind"] = FJson::String("markdown");
				Contents["value"] = FJson::String(VeroScriptHover::FormatMarkdown(Result));

				FJson::FObject HoverObject;
				HoverObject["contents"] = FJson::Object(std::move(Contents));
				HoverObject["range"] = RangeToJson(Result.TokenRange, SourceForPath(Path));
				WriteResponseResult(Id, FJson::Object(std::move(HoverObject)));
			}

			std::string NavigationUri(const std::string& Path)
			{
				// A re-export can legally target a transitive dependency. Retain only
				// targets actually returned by semantic navigation, rather than exposing
				// every module of every transitive provider to moduleSource callers.
				if (const auto& Environment = Workspace.GetOptions().ModuleEnvironment;
				    Environment && Path.rfind("veroscript-module:/", 0) == 0 && Environment->FindSource(Path))
				{
					PublishedModuleSources.insert(PathUtil::IdentityKey(Path));
				}

				return PathToUri(Path);
			}
			FJson NavigationLocations(const std::vector<FNavigationHit>& Hits)
			{
				FJson::FArray Locations;
				Locations.reserve(Hits.size());
				for (const FNavigationHit& Hit : Hits)
				{
					FJson::FObject Location;
					// Standard LSP clients cannot display arbitrary virtual source. Keep
					// navigation usable by targeting the annotation that generated it.
					// The query already retained the generating annotation from this
					// prepared program. Navigation must not recompile or rerun a host
					// processor just to find it, especially in a buffer with errors.
					const bool bGenerated = Hit.FileLabel.rfind("veroscript-generated:", 0) == 0 && !Hit.GeneratedOrigin.FileLabel.empty();
					const auto& Label = bGenerated ? Hit.GeneratedOrigin.FileLabel : Hit.FileLabel;
					const auto Range = !bGenerated ? Hit.DefinitionRange : FTextRange{Hit.GeneratedOrigin.Line, Hit.GeneratedOrigin.Column, Hit.GeneratedOrigin.Line, Hit.GeneratedOrigin.Column + 1};
					Location["uri"] = FJson::String(NavigationUri(Label));
					Location["range"] = RangeToJson(Range, SourceForPath(Label));
					Locations.push_back(FJson::Object(std::move(Location)));
				}
				return FJson::Array(std::move(Locations));
			}

			void HandleDefinition(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}

				const FNavigationResult Result = Workspace.NavigateDocument(Path, Position);
				WriteResponseResult(Id, NavigationLocations(Result.Definitions));
			}

			void HandleImplementation(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				FWorkspaceSymbolsOptions SymbolOptions;
				SymbolOptions.ShouldCancel = [this, Id]()
				{
					return IsRequestCancellationRequested(Id);
				};
				const FNavigationResult Result =
				    Workspace.ImplementationDocument(Path, Position, SymbolOptions);
				if (Result.bCancelled || IsRequestCancellationRequested(Id))
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}
				WriteResponseResult(Id, NavigationLocations(Result.Definitions));
			}

			FJson MakeTypeHierarchyItem(const FWorkspaceSymbol& Symbol)
			{
				const std::string Source = SourceForPath(Symbol.FileLabel);
				const FTextRange SelectionRange = SelectionRangeForOutlineSymbol(
				    Symbol.Range, Symbol.Name, Symbol.Kind, Source);
				const FJson Range = RangeToJson(SelectionRange, Source);
				FJson::FObject Item;
				Item["name"] = FJson::String(Symbol.Name);
				Item["kind"] = FJson::Number(
				    static_cast<double>(OutlineKindToSymbolKind(Symbol.Kind)));
				Item["detail"] = FJson::String(Symbol.Detail);
				Item["uri"] = FJson::String(NavigationUri(Symbol.FileLabel));
				Item["range"] = Range;
				Item["selectionRange"] = Range;
				Item["data"] = FJson::Object(
				    {{"veroscriptTypeSymbolId", FJson::String(WorkspaceSymbolId(Symbol))}});
				return FJson::Object(std::move(Item));
			}

			std::optional<FWorkspaceSymbol> FindWorkspaceTypeForNavigation(
			    const FNavigationHit& Hit,
			    const FWorkspaceSymbolsResult& Symbols) const
			{
				if (!IsHierarchyTypeKind(Hit.Kind))
				{
					return std::nullopt;
				}
				const auto Found = std::find_if(
				    Symbols.Symbols.begin(), Symbols.Symbols.end(), [&Hit](const FWorkspaceSymbol& Symbol)
				{
					return IsHierarchyTypeKind(Symbol.Kind) && PathUtil::Equal(Symbol.FileLabel, Hit.FileLabel) && Symbol.Range.StartLine == Hit.DefinitionRange.StartLine && Symbol.Range.StartColumn == Hit.DefinitionRange.StartColumn && SameTypeStem(Symbol.Name, Hit.Name);
				});
				return Found == Symbols.Symbols.end()
				    ? std::optional<FWorkspaceSymbol>{}
				    : std::optional<FWorkspaceSymbol>{*Found};
			}

			std::optional<FWorkspaceSymbol> FindTypeHierarchyItemSymbol(
			    const FJson& Params,
			    const FWorkspaceSymbolsResult& Symbols) const
			{
				const FJson* const Item = Params.Find("item");
				const FJson* const Data = Item ? Item->Find("data") : nullptr;
				const std::string Id = Data ? Data->GetString("veroscriptTypeSymbolId") : std::string{};
				if (Id.empty())
				{
					return std::nullopt;
				}
				const auto Found = std::find_if(
				    Symbols.Symbols.begin(), Symbols.Symbols.end(), [&Id](const FWorkspaceSymbol& Symbol)
				{
					return IsHierarchyTypeKind(Symbol.Kind) && WorkspaceSymbolId(Symbol) == Id;
				});
				return Found == Symbols.Symbols.end()
				    ? std::optional<FWorkspaceSymbol>{}
				    : std::optional<FWorkspaceSymbol>{*Found};
			}

			FWorkspaceSymbolsResult GetHierarchySymbols(const FJson& Id)
			{
				FWorkspaceSymbolsOptions Options;
				Options.ShouldCancel = [this, Id]()
				{
					return IsRequestCancellationRequested(Id);
				};
				return Workspace.WorkspaceSymbols(Options);
			}

			void HandlePrepareTypeHierarchy(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const FNavigationResult Navigation = Workspace.NavigateDocument(Path, Position);
				const FWorkspaceSymbolsResult Symbols = GetHierarchySymbols(Id);
				if (Symbols.bCancelled)
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}
				for (const FNavigationHit& Hit : Navigation.Definitions)
				{
					if (const std::optional<FWorkspaceSymbol> Symbol =
					        FindWorkspaceTypeForNavigation(Hit, Symbols))
					{
						WriteResponseResult(Id, FJson::Array({MakeTypeHierarchyItem(*Symbol)}));
						return;
					}
				}
				WriteResponseResult(Id, FJson::Array());
			}

			void HandleTypeHierarchySupertypes(const FJson& Id, const FJson& Params)
			{
				const FWorkspaceSymbolsResult Symbols = GetHierarchySymbols(Id);
				if (Symbols.bCancelled)
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}
				const std::optional<FWorkspaceSymbol> Current =
				    FindTypeHierarchyItemSymbol(Params, Symbols);
				if (!Current)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				FJson::FArray Parents;
				for (const std::string& Base : Detail::BaseTypesFromDetail(Current->Detail))
				{
					std::vector<const FWorkspaceSymbol*> Candidates;
					for (const FWorkspaceSymbol& Symbol : Symbols.Symbols)
					{
						if (IsHierarchyTypeKind(Symbol.Kind) && SameTypeStem(Symbol.Name, Base))
						{
							Candidates.push_back(&Symbol);
						}
					}
					// The compact cross-file outline does not retain full namespace identity
					// for every declaration. Never choose an arbitrary same-named base.
					if (Candidates.size() == 1u)
					{
						Parents.push_back(MakeTypeHierarchyItem(*Candidates.front()));
					}
				}
				WriteResponseResult(Id, FJson::Array(std::move(Parents)));
			}

			void HandleTypeHierarchySubtypes(const FJson& Id, const FJson& Params)
			{
				const FWorkspaceSymbolsResult Symbols = GetHierarchySymbols(Id);
				if (Symbols.bCancelled)
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}
				const std::optional<FWorkspaceSymbol> Current =
				    FindTypeHierarchyItemSymbol(Params, Symbols);
				if (!Current)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::size_t SameNameCount = static_cast<std::size_t>(std::count_if(
				    Symbols.Symbols.begin(), Symbols.Symbols.end(), [Current](const FWorkspaceSymbol& Symbol)
				{
					return IsHierarchyTypeKind(Symbol.Kind) && SameTypeStem(Symbol.Name, Current->Name);
				}));
				if (SameNameCount != 1u)
				{
					// Without a unique qualified identity there is no sound way to decide
					// which declaration a written base name targets.
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				FJson::FArray Children;
				for (const FWorkspaceSymbol& Candidate : Symbols.Symbols)
				{
					if (!IsHierarchyTypeKind(Candidate.Kind) || SameTypeStem(Candidate.Name, Current->Name))
					{
						continue;
					}
					const std::vector<std::string> Bases = Detail::BaseTypesFromDetail(Candidate.Detail);
					if (std::any_of(Bases.begin(), Bases.end(), [Current](const std::string& Base)
					{
						return SameTypeStem(Base, Current->Name);
					}))
					{
						Children.push_back(MakeTypeHierarchyItem(Candidate));
					}
				}
				WriteResponseResult(Id, FJson::Array(std::move(Children)));
			}

			void HandleTypeDefinition(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Source = SourceForPath(Path);
				const std::string Token = IdentifierAtPosition(Source, Position);
				if (Token.empty())
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const auto IsTypeKind = [](const EOutlineSymbolKind Kind)
				{
					return Kind == EOutlineSymbolKind::Class || Kind == EOutlineSymbolKind::Struct || Kind == EOutlineSymbolKind::Interface || Kind == EOutlineSymbolKind::Enum || Kind == EOutlineSymbolKind::TypeAlias;
				};
				FJson::FArray Locations;
				// Prefer semantic navigation: it preserves qualified names, imports and
				// same-name types in different namespaces. The local-outline recovery
				// below is only for temporarily broken/incomplete buffers.
				const FNavigationResult Navigation = Workspace.NavigateDocument(Path, Position);
				for (const FNavigationHit& Hit : Navigation.Definitions)
				{
					if (!IsTypeKind(Hit.Kind))
					{
						continue;
					}
					FJson::FObject Location;
					Location["uri"] = FJson::String(NavigationUri(Hit.FileLabel));
					Location["range"] = RangeToJson(Hit.DefinitionRange, SourceForPath(Hit.FileLabel));
					Locations.push_back(FJson::Object(std::move(Location)));
				}
				const auto AppendMatchingTypes = [&](const auto& Self, const std::vector<FOutlineSymbol>& Symbols) -> void
				{
					for (const FOutlineSymbol& Symbol : Symbols)
					{
						if (IsTypeKind(Symbol.Kind) && Symbol.Name == Token)
						{
							FJson::FObject Location;
							Location["uri"] = FJson::String(NavigationUri(Symbol.FileLabel.empty() ? Path : Symbol.FileLabel));
							Location["range"] = RangeToJson(
							    Symbol.Range, SourceForPath(Symbol.FileLabel.empty() ? Path : Symbol.FileLabel));
							Locations.push_back(FJson::Object(std::move(Location)));
						}
						Self(Self, Symbol.Children);
					}
				};
				if (Locations.empty())
				{
					AppendMatchingTypes(AppendMatchingTypes, Workspace.OutlineDocument(Path).Symbols);
				}
				if (Locations.empty())
				{
					// Keep type navigation available while an unrelated import or the
					// declaration body is incomplete. Use lexer tokens rather than textual
					// prefix search so a declaration-shaped string/comment cannot become a
					// false navigation target.
					VeroScript::VeroScriptLexer Lexer(Source, Path);
					const std::vector<VeroScript::FToken> Tokens = Lexer.Tokenize();
					for (std::size_t Index = 0; Index + 1u < Tokens.size(); ++Index)
					{
						const std::string_view Head = Tokens[Index].Lexeme;
						if (Head != "class" && Head != "struct" && Head != "interface" && Head != "enum" && Head != "using")
						{
							continue;
						}
						std::size_t NameIndex = Index + 1u;
						if (Head == "enum" && Tokens[NameIndex].Lexeme == "class")
						{
							++NameIndex;
						}
						if (NameIndex >= Tokens.size() || Tokens[NameIndex].Kind != VeroScript::ETokenKind::Identifier || Tokens[NameIndex].Lexeme != Token)
						{
							continue;
						}
						const VeroScript::FToken& NameToken = Tokens[NameIndex];
						FTextRange Range;
						Range.StartLine = Range.EndLine = NameToken.Location.Line;
						Range.StartColumn = NameToken.Location.Column;
						Range.EndColumn = NameToken.Location.Column + static_cast<std::uint32_t>(NameToken.Lexeme.size());
						FJson::FObject Location;
						Location["uri"] = FJson::String(PathToUri(Path));
						Location["range"] = RangeToJson(Range, Source);
						Locations.push_back(FJson::Object(std::move(Location)));
						break;
					}
				}
				WriteResponseResult(Id, FJson::Array(std::move(Locations)));
			}

			static FJson OutlineSymbolToDocumentSymbol(
			    const FOutlineSymbol& Symbol,
			    const std::string_view Source,
			    FTextRange* OutRange = nullptr)
			{
				CheckAnalysisCancellation();
				const auto Contains = [](const FTextRange& Outer, const FTextRange& Inner)
				{
					return (Outer.StartLine < Inner.StartLine || (Outer.StartLine == Inner.StartLine && Outer.StartColumn <= Inner.StartColumn)) && (Outer.EndLine > Inner.EndLine || (Outer.EndLine == Inner.EndLine && Outer.EndColumn >= Inner.EndColumn));
				};
				const FTextRange Selection = SelectionRangeForOutlineSymbol(Symbol.Range, Symbol.Name, Symbol.Kind, Source);
				const bool bHasDeclarationRange = Symbol.DeclarationRange.StartLine != 0 && Contains(Symbol.DeclarationRange, Selection);
				FTextRange Range = bHasDeclarationRange ? Symbol.DeclarationRange : Selection;

				FJson::FObject Object;
				Object["name"] = FJson::String(Symbol.Name.empty() ? std::string("<anonymous>") : Symbol.Name);
				Object["kind"] = FJson::Number(static_cast<double>(OutlineKindToSymbolKind(Symbol.Kind)));
				if (!Symbol.Detail.empty())
				{
					Object["detail"] = FJson::String(Symbol.Detail);
				}
				Object["selectionRange"] = RangeToJson(Selection, Source);
				if (!Symbol.Children.empty())
				{
					FJson::FArray Children;
					Children.reserve(Symbol.Children.size());
					for (const FOutlineSymbol& Child : Symbol.Children)
					{
						// Generated and separately supplied bodies live in other
						// documents. Their virtual ranges cannot be children in this
						// document's LSP outline; completion/navigation still expose them.
						if (Child.FileLabel != Symbol.FileLabel)
						{
							continue;
						}

						FTextRange ChildRange;
						FJson ChildObject = OutlineSymbolToDocumentSymbol(Child, Source, &ChildRange);
						// A separately defined body can be in this same document but
						// outside the parent's declaration (for example an out-of-class
						// method's labels). It is not a lexical outline child there.
						if (bHasDeclarationRange && !Contains(Range, ChildRange))
						{
							continue;
						}
						if (!bHasDeclarationRange)
						{
							if (ChildRange.StartLine < Range.StartLine || (ChildRange.StartLine == Range.StartLine && ChildRange.StartColumn < Range.StartColumn))
							{
								Range.StartLine = ChildRange.StartLine;
								Range.StartColumn = ChildRange.StartColumn;
							}
							if (ChildRange.EndLine > Range.EndLine || (ChildRange.EndLine == Range.EndLine && ChildRange.EndColumn > Range.EndColumn))
							{
								Range.EndLine = ChildRange.EndLine;
								Range.EndColumn = ChildRange.EndColumn;
							}
						}
						Children.push_back(std::move(ChildObject));
					}
					Object["children"] = FJson::Array(std::move(Children));
				}
				Object["range"] = RangeToJson(Range, Source);
				if (OutRange)
				{
					*OutRange = Range;
				}
				return FJson::Object(std::move(Object));
			}

			void HandleDocumentSymbol(const FJson& Id, const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const FOutlineResult Result = Workspace.OutlineDocument(Path);
				if (!Result.bSuccess)
				{
					// A null result distinguishes a transient parse failure from a
					// successfully parsed document that legitimately has no symbols.
					WriteResponseResult(Id, FJson::Null());
					return;
				}
				const std::string Source = SourceForPath(Path);

				FJson::FArray Symbols;
				Symbols.reserve(Result.Symbols.size());
				for (const FOutlineSymbol& Symbol : Result.Symbols)
				{
					// Generated top-level declarations belong to their virtual source,
					// just as generated members do. Their positions cannot describe
					// symbols in this physical document's outline.
					if (!PathUtil::Equal(Symbol.FileLabel, Path))
					{
						continue;
					}
					Symbols.push_back(OutlineSymbolToDocumentSymbol(Symbol, Source));
				}
				WriteResponseResult(Id, FJson::Array(std::move(Symbols)));
			}

			void HandleSelectionRange(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				const FJson* const Positions = Params.Find("positions");
				if (!TextDocument || !Positions || !Positions->ArrayPtr())
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				const FJson DocumentRange = FullDocumentRangeToJson(Source);
				FJson::FArray Results;
				for (const FJson& PositionJson : *Positions->ArrayPtr())
				{
					const FTextPosition Position = PositionFromJson(PositionJson, Source);
					FTextRange TokenRange;
					(void)IdentifierAtPosition(Source, Position, &TokenRange);
					if (TokenRange.StartLine == 0)
					{
						TokenRange.StartLine = Position.Line;
						TokenRange.EndLine = Position.Line;
						TokenRange.StartColumn = Position.Column;
						TokenRange.EndColumn = Position.Column;
					}
					const std::string_view Line = SourceLine(Source, Position.Line);
					FTextRange LineRange;
					LineRange.StartLine = Position.Line;
					LineRange.EndLine = Position.Line;
					LineRange.StartColumn = 1;
					LineRange.EndColumn = static_cast<std::uint32_t>(Line.size()) + 1u;
					FJson::FObject DocumentNode;
					DocumentNode["range"] = DocumentRange;
					FJson::FObject LineNode;
					LineNode["range"] = RangeToJson(LineRange, Source);
					LineNode["parent"] = FJson::Object(std::move(DocumentNode));
					FJson::FObject TokenNode;
					TokenNode["range"] = RangeToJson(TokenRange, Source);
					TokenNode["parent"] = FJson::Object(std::move(LineNode));
					Results.push_back(FJson::Object(std::move(TokenNode)));
				}
				WriteResponseResult(Id, FJson::Array(std::move(Results)));
			}

			void HandleModuleSource(const FJson& Id, const FJson& Params)
			{
				const auto& Environment = Workspace.GetOptions().ModuleEnvironment;
				const auto* Module = Environment ? Environment->FindSource(UriToPath(Params.GetString("uri"))) : nullptr;
				if (!Module || !Module->Source || (!Environment->CanAccessOwner(Environment->GetDescription().EntryOwnerId, Module->OwnerId) && !PublishedModuleSources.count(PathUtil::IdentityKey(Module->Path))))
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}
				// A JSON string can expand each byte to six bytes. Keep even the
				// worst escaped response within the existing 64 MiB frame budget.
				if (Module->Source->size() > (64u * 1024u * 1024u - 4096u) / 6u)
				{
					WriteResponseError(Id, -32602, "module source exceeds protocol response budget");
					return;
				}
				WriteResponseResult(Id, FJson::Object({{"uri", FJson::String(PathToUri(Module->Path))}, {"languageId", FJson::String("veroscript")}, {"text", FJson::String(*Module->Source)}, {"readOnly", FJson::Bool(true)}, {"compileFingerprint", FJson::String(Environment->GetCompileFingerprint())}}));
			}

			void HandleDocumentLink(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				VeroScriptModuleLoader Loader(Path, Workspace.GetOptions().ModuleEnvironment);
				ApplyModuleLoadingOptions(Loader, Workspace.GetOptions());
				for (std::string DocumentPath : Workspace.ListDocuments())
				{
					// Path resolution only needs overlay keys, not copies of every buffer.
					Loader.SetSourceOverlay(std::move(DocumentPath), {});
				}
				const std::string Directory = PathUtil::ToUtf8(PathUtil::FromUtf8(Path).parent_path());
				FJson::FArray Links;
				VeroScriptLexer Lexer(Source, Path);
				const auto& Tokens = Lexer.Tokenize();
				for (std::size_t I = 0; I + 1 < Tokens.size(); ++I)
				{
					CheckAnalysisCancellation();
					std::size_t Target = I + 1;
					bool CompilerExtension = false;
					if (Tokens[I].Lexeme == "import" && Tokens[I].Kind == ETokenKind::Keyword)
					{
						if (Target < Tokens.size() && Tokens[Target].Lexeme == "compiler")
						{
							CompilerExtension = true;
							++Target;
						}
					}
					else if (Tokens[I].Lexeme == "#" && Tokens[Target].Lexeme == "include")
					{
						++Target;
					}

					else
					{
						continue;
					}

					if (Target >= Tokens.size() || Tokens[Target].Kind != ETokenKind::StringLiteral)
					{
						continue;
					}

					const auto& Token = Tokens[Target];
					std::string TargetPath;
					if (!Detail::DecodeImportPathLiteral(Token.Lexeme, CompilerExtension, TargetPath))
					{
						continue;
					}

					std::string ResolvedPath;
					bool Resolved = false;
					try
					{
						Resolved = Loader.ResolveImportPath(TargetPath, Directory, ResolvedPath, Path);
					}
					catch (const FAnalysisCancelled&)
					{
						throw;
					}
					catch (const std::exception&)
					{
						continue;
					}
					if (!Resolved)
					{
						// Preserve the local link while an imported file is being created.
						ResolvedPath = PathUtil::ToUtf8((PathUtil::FromUtf8(Directory) / PathUtil::FromUtf8(TargetPath)).lexically_normal());
					}
					FTextRange Range;
					Range.StartLine = Range.EndLine = Token.Location.Line;
					// The lexer ignores the leading UTF-8 BOM when counting columns;
					// the editor buffer and its LSP positions still contain that character.
					const auto BomBytes = Token.Location.Line == 1 && Source.substr(0, 3) == "\xef\xbb\xbf" ? 3u : 0u;
					const std::size_t HeaderSize = Token.Lexeme.substr(0, 2) == "r\"" ? 2 : 1;
					Range.StartColumn = Range.EndColumn = Token.Location.Column + BomBytes + static_cast<std::uint32_t>(HeaderSize);
					for (std::size_t J = HeaderSize; J + 1 < Token.Lexeme.size(); ++J)
					{
						if ((J & 255u) == 0)
						{
							CheckAnalysisCancellation();
						}

						if (Token.Lexeme[J] == '\n')
						{
							++Range.EndLine;
							Range.EndColumn = 1;
						}
						else
						{
							++Range.EndColumn;
						}
					}
					FJson::FObject Link;
					Link["range"] = RangeToJson(Range, Source);
					Link["target"] = FJson::String(NavigationUri(ResolvedPath));
					Link["tooltip"] = FJson::String(Locale::TranslateId("lsp.document_link.open_import"));
					Links.push_back(FJson::Object(std::move(Link)));
					I = Target;
				}
				WriteResponseResult(Id, FJson::Array(std::move(Links)));
			}

			void HandleFoldingRange(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				FJson::FArray Ranges;
				const auto AddRange = [&Ranges](
				                          const std::uint32_t StartLine,
				                          const std::uint32_t EndLine,
				                          const char* const Kind)
				{
					if (EndLine <= StartLine)
					{
						return;
					}
					FJson::FObject Range;
					Range["startLine"] = FJson::Number(static_cast<double>(StartLine - 1u));
					Range["endLine"] = FJson::Number(static_cast<double>(EndLine - 1u));
					Range["kind"] = FJson::String(Kind);
					Ranges.push_back(FJson::Object(std::move(Range)));
				};

				// Lexer tokens keep braces in strings and comments out of the fold stack.
				VeroScript::VeroScriptLexer Lexer(Source, Path);
				const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
				std::vector<std::uint32_t> BlockStarts;
				for (const VeroScript::FToken& Token : Tokens)
				{
					if (Token.Kind != VeroScript::ETokenKind::Symbol)
					{
						continue;
					}
					if (Token.Lexeme == "{")
					{
						BlockStarts.push_back(Token.Location.Line);
					}
					else if (Token.Lexeme == "}" && !BlockStarts.empty())
					{
						const std::uint32_t StartLine = BlockStarts.back();
						BlockStarts.pop_back();
						AddRange(StartLine, Token.Location.Line, "region");
					}
				}

				std::size_t Offset = 0;
				std::uint32_t LineNumber = 1;
				std::uint32_t CommentStart = 0;
				std::uint32_t ImportStart = 0;
				const auto FlushLineGroups = [&]()
				{
					if (CommentStart != 0)
					{
						AddRange(CommentStart, LineNumber - 1u, "comment");
						CommentStart = 0;
					}
					if (ImportStart != 0)
					{
						AddRange(ImportStart, LineNumber - 1u, "imports");
						ImportStart = 0;
					}
				};
				while (Offset <= Source.size())
				{
					const std::size_t End = Source.find('\n', Offset);
					std::string_view Line = std::string_view(Source).substr(
					    Offset, (End == std::string::npos ? Source.size() : End) - Offset);
					while (!Line.empty() && std::isspace(static_cast<unsigned char>(Line.front())) != 0)
					{
						Line.remove_prefix(1);
					}
					const bool bComment = Line.rfind("//", 0) == 0;
					const bool bImport = Line.rfind("import ", 0) == 0 || Line.rfind("export import ", 0) == 0;
					if (bComment)
					{
						if (CommentStart == 0)
						{
							CommentStart = LineNumber;
						}
					}
					else if (CommentStart != 0)
					{
						AddRange(CommentStart, LineNumber - 1u, "comment");
						CommentStart = 0;
					}
					if (bImport)
					{
						if (ImportStart == 0)
						{
							ImportStart = LineNumber;
						}
					}
					else if (ImportStart != 0)
					{
						AddRange(ImportStart, LineNumber - 1u, "imports");
						ImportStart = 0;
					}
					if (End == std::string::npos)
					{
						if (CommentStart != 0)
						{
							AddRange(CommentStart, LineNumber, "comment");
							CommentStart = 0;
						}
						if (ImportStart != 0)
						{
							AddRange(ImportStart, LineNumber, "imports");
							ImportStart = 0;
						}
						break;
					}
					Offset = End + 1;
					++LineNumber;
				}
				FlushLineGroups();
				WriteResponseResult(Id, FJson::Array(std::move(Ranges)));
			}

			void HandleSemanticTokens(const FJson& Id, const FJson& Params)
			{
				QueueSemanticTokens(Id, Params, false);
			}

			void HandleSemanticTokensDelta(const FJson& Id, const FJson& Params)
			{
				QueueSemanticTokens(Id, Params, true);
			}

			void ReplySemanticTokens(const FJson& Id, const FJson& Params,
			    const FJson::FArray& Data, bool bDelta)
			{
				if (IsRequestCancellationRequested(Id))
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}
				const std::string Path = UriToPath(Params.Find("textDocument")->GetString("uri"));
				const std::string PreviousResultId = Params.GetString("previousResultId");
				const std::string SnapshotKey = PathUtil::IdentityKey(Path);
				const auto Previous = SemanticTokenSnapshots.find(SnapshotKey);
				const std::string ResultId = std::to_string(NextSemanticTokenResultId++);
				FJson::FObject Response;
				Response["resultId"] = FJson::String(ResultId);
				if (!bDelta || Previous == SemanticTokenSnapshots.end() || Previous->second.first != PreviousResultId)
				{
					Response["data"] = FJson::Array(Data);
				}
				else
				{
					const FJson::FArray& PreviousData = Previous->second.second;
					// Semantic-token deltas operate on the integer stream, but keeping
					// edit boundaries aligned to complete five-integer token records avoids
					// transient malformed tuples in strict clients.
					std::size_t Prefix = 0;
					while (Prefix + 5u <= PreviousData.size() && Prefix + 5u <= Data.size())
					{
						if (Prefix % 1280u == 0 && IsRequestCancellationRequested(Id))
						{
							WriteResponseResult(Id, FJson::Null());
							return;
						}
						bool bSameToken = true;
						for (std::size_t Field = 0; Field < 5u; ++Field)
						{
							bSameToken &= std::get<double>(PreviousData[Prefix + Field].Value) == std::get<double>(Data[Prefix + Field].Value);
						}
						if (!bSameToken)
						{
							break;
						}
						Prefix += 5u;
					}
					std::size_t Suffix = 0;
					while (Suffix + 5u <= PreviousData.size() - Prefix && Suffix + 5u <= Data.size() - Prefix)
					{
						if (Suffix % 1280u == 0 && IsRequestCancellationRequested(Id))
						{
							WriteResponseResult(Id, FJson::Null());
							return;
						}
						bool bSameToken = true;
						for (std::size_t Field = 0; Field < 5u; ++Field)
						{
							bSameToken &= std::get<double>(PreviousData[PreviousData.size() - 5u - Suffix + Field].Value) == std::get<double>(Data[Data.size() - 5u - Suffix + Field].Value);
						}
						if (!bSameToken)
						{
							break;
						}
						Suffix += 5u;
					}
					FJson::FArray Inserted;
					Inserted.insert(
					    Inserted.end(), Data.begin() + Prefix, Data.end() - Suffix);
					FJson::FObject Edit;
					Edit["start"] = FJson::Number(static_cast<double>(Prefix));
					Edit["deleteCount"] = FJson::Number(
					    static_cast<double>(PreviousData.size() - Prefix - Suffix));
					if (!Inserted.empty())
					{
						Edit["data"] = FJson::Array(std::move(Inserted));
					}
					if (Prefix != PreviousData.size() || Prefix != Data.size())
					{
						Response["edits"] = FJson::Array({FJson::Object(std::move(Edit))});
					}
					else
					{
						Response["edits"] = FJson::Array();
					}
				}
				const ERequestStopReason Reason = CompleteRequestAndConsumeCancellation(Id);
				if (Reason != ERequestStopReason::None)
				{
					WriteResponseErrorRaw(Id, Reason == ERequestStopReason::TimedOut ? -32001 : -32800,
					    RequestStopMessage(Reason));
					return;
				}
				SemanticTokenSnapshots[SnapshotKey] = std::make_pair(ResultId, Data);
				WriteResponseResultRaw(Id, FJson::Object(std::move(Response)));
			}

			void HandleSignatureHelp(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}

				const FSignatureHelpResult Result = Workspace.SignatureHelpDocument(Path, Position);
				if (!Result.bActive || Result.Signatures.empty())
				{
					WriteResponseResult(Id, FJson::Null());
					return;
				}

				FJson::FArray Signatures;
				Signatures.reserve(Result.Signatures.size());
				for (const FSignatureInformation& Signature : Result.Signatures)
				{
					FJson::FArray Parameters;
					Parameters.reserve(Signature.Parameters.size());
					for (const FSignatureParameter& Parameter : Signature.Parameters)
					{
						FJson::FObject ParameterObject;
						ParameterObject["label"] = FJson::String(Parameter.Label);
						Parameters.push_back(FJson::Object(std::move(ParameterObject)));
					}

					FJson::FObject SignatureObject;
					SignatureObject["label"] = FJson::String(Signature.Label);
					SignatureObject["parameters"] = FJson::Array(std::move(Parameters));
					if (!Signature.Documentation.empty())
					{
						SignatureObject["documentation"] = FJson::String(Signature.Documentation);
					}
					Signatures.push_back(FJson::Object(std::move(SignatureObject)));
				}

				FJson::FObject HelpObject;
				HelpObject["signatures"] = FJson::Array(std::move(Signatures));
				HelpObject["activeSignature"] = FJson::Number(static_cast<double>(Result.ActiveSignature));
				HelpObject["activeParameter"] = FJson::Number(static_cast<double>(Result.ActiveParameter));
				WriteResponseResult(Id, FJson::Object(std::move(HelpObject)));
			}

			void HandleReferences(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}

				bool bIncludeDeclaration = true;
				if (const FJson* Context = Params.Find("context"))
				{
					bIncludeDeclaration = Context->GetBool("includeDeclaration", true);
				}

				const FReferencesResult Result = Workspace.FindReferencesDocument(Path, Position);
				// One request owns one source snapshot per file. Re-reading/copying a
				// complete file for each reference is costly and can mix disk revisions.
				std::unordered_map<std::string, std::string> Sources;
				FJson::FArray Locations;
				Locations.reserve(Result.References.size());
				for (const FReferenceHit& Hit : Result.References)
				{
					if (Locations.size() >= MaxReferenceResults || IsRequestCancellationRequested(Id))
					{
						break;
					}
					if (!bIncludeDeclaration && Hit.bIsDefinition)
					{
						continue;
					}
					FJson::FObject Location;
					Location["uri"] = FJson::String(NavigationUri(Hit.FileLabel));
					Location["range"] = RangeToJson(Hit.Range, SourceForPath(Hit.FileLabel, Sources));
					Locations.push_back(FJson::Object(std::move(Location)));
				}
				WriteResponseResult(Id, FJson::Array(std::move(Locations)));
			}

			void HandleDocumentHighlight(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}

				const FDocumentHighlightResult Result = Workspace.HighlightDocument(Path, Position);
				const std::string Source = SourceForPath(Path);
				FJson::FArray Highlights;
				Highlights.reserve(Result.Highlights.size());
				for (const FDocumentHighlight& Highlight : Result.Highlights)
				{
					FJson::FObject Item;
					Item["range"] = RangeToJson(Highlight.Range, Source);
					Item["kind"] = FJson::Number(
					    static_cast<double>(DocumentHighlightKindToLsp(Highlight.Kind)));
					Highlights.push_back(FJson::Object(std::move(Item)));
				}
				WriteResponseResult(Id, FJson::Array(std::move(Highlights)));
			}

			void HandleRename(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseError(
					    Id, -32602, Locale::TranslateId("lsp.rename.missing_document_position"));
					return;
				}

				const std::string NewName = Params.GetString("newName");
				const FRenameResult Result = Workspace.RenameDocument(Path, Position, NewName);
				if (!Result.bCanRename)
				{
					WriteResponseError(
					    Id,
					    -32602,
					    Result.Reason.empty()
					        ? Locale::TranslateId("lsp.rename.symbol_cannot_be_renamed")
					        : Result.Reason);
					return;
				}

				std::map<std::string, FJson::FArray> EditsByUri;
				std::unordered_map<std::string, std::string> Sources;
				for (const FTextEdit& Edit : Result.Edits)
				{
					FJson::FObject TextEdit;
					TextEdit["range"] = RangeToJson(Edit.Range, SourceForPath(Edit.FileLabel, Sources));
					TextEdit["newText"] = FJson::String(Edit.NewText);
					EditsByUri[PathToUri(Edit.FileLabel)].push_back(FJson::Object(std::move(TextEdit)));
				}

				FJson::FObject Changes;
				for (auto& [Uri, Edits] : EditsByUri)
				{
					Changes[Uri] = FJson::Array(std::move(Edits));
				}

				FJson::FObject WorkspaceEdit;
				WorkspaceEdit["changes"] = FJson::Object(std::move(Changes));
				WriteResponseResult(Id, FJson::Object(std::move(WorkspaceEdit)));
			}

			void HandlePrepareRename(const FJson& Id, const FJson& Params)
			{
				std::string Path;
				FTextPosition Position;
				if (!ExtractDocumentPosition(Params, Path, Position))
				{
					WriteResponseError(
					    Id, -32602, Locale::TranslateId("lsp.rename.missing_document_position"));
					return;
				}

				const std::string Source = SourceForPath(Path);
				FTextRange TokenRange;
				const std::string Token = IdentifierAtPosition(Source, Position, &TokenRange);
				if (Token.empty())
				{
					WriteResponseError(
					    Id, -32602, Locale::TranslateId("lsp.rename.no_identifier_under_caret"));
					return;
				}
				// prepareRename is a target query, not a trial transformation.  In
				// particular, a synthetic replacement name could conflict in scope and
				// incorrectly reject an otherwise renameable symbol.
				const FRenameResult Validation = Workspace.CanRenameDocument(Path, Position);
				if (!Validation.bCanRename)
				{
					WriteResponseError(
					    Id,
					    -32602,
					    Validation.Reason.empty()
					        ? Locale::TranslateId("lsp.rename.symbol_cannot_be_renamed")
					        : Validation.Reason);
					return;
				}

				FJson::FObject Result;
				Result["range"] = RangeToJson(TokenRange, Source);
				Result["placeholder"] = FJson::String(
				    Validation.OldName.empty() ? Token : Validation.OldName);
				WriteResponseResult(Id, FJson::Object(std::move(Result)));
			}

			std::uint32_t FormattingIndentWidth(const FJson& Params) const
			{
				const FJson* const FormattingOptions = Params.Find("options");
				if (!FormattingOptions)
				{
					return 0; // preserve the workspace default for incomplete client requests.
				}
				const std::int64_t TabSize = FormattingOptions->GetInt("tabSize", 0);
				if (TabSize <= 0)
				{
					return 0;
				}
				// Bound client input so a malformed request cannot generate
				// pathological indentation allocations.
				return static_cast<std::uint32_t>((std::min)(TabSize, std::int64_t{16}));
			}

			bool FormattingInsertSpaces(const FJson& Params) const
			{
				const FJson* const Options = Params.Find("options");
				return Options ? Options->GetBool("insertSpaces", true) : true;
			}

			void HandleFormatting(const FJson& Id, const FJson& Params)
			{
				const FJson* TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}

				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const FFormatResult Result = Workspace.FormatDocument(
				    Path, FormattingIndentWidth(Params), FormattingInsertSpaces(Params));
				if (!Result.bSuccess)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}

				const std::string Source = SourceForPath(Path);
				FJson::FObject TextEdit;
				TextEdit["range"] = FullDocumentRangeToJson(Source);
				TextEdit["newText"] = FJson::String(Result.FormattedSource);

				FJson::FArray Edits;
				Edits.push_back(FJson::Object(std::move(TextEdit)));
				WriteResponseResult(Id, FJson::Array(std::move(Edits)));
			}

			void HandleRangeFormatting(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				const FJson* const RequestedRange = Params.Find("range");
				if (!TextDocument || !RequestedRange)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				const FJson* const Start = RequestedRange->Find("start");
				const FJson* const End = RequestedRange->Find("end");
				// Fragment formatting uses core LF-based coordinates. Full-document
				// formatting can safely normalize a lone CR using its physical EOF.
				if (!Start || !End || HasBareCarriageReturn(Source))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const FTextPosition StartPosition = PositionFromJson(*Start, Source);
				const FTextPosition EndPosition = PositionFromJson(*End, Source);
				FTextRange Range;
				Range.StartLine = StartPosition.Line;
				Range.StartColumn = StartPosition.Column;
				Range.EndLine = EndPosition.Line;
				Range.EndColumn = EndPosition.Column;
				const FRangeFormatResult Result = Workspace.FormatRangeDocument(
				    Path, Range, FormattingIndentWidth(Params), FormattingInsertSpaces(Params));
				if (!Result.bSuccess || !Result.bHasFormattedRange)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				FJson::FObject TextEdit;
				TextEdit["range"] = RangeToJson(Result.FormattedRange, Source);
				TextEdit["newText"] = FJson::String(Result.FormattedSource);
				WriteResponseResult(Id, FJson::Array({FJson::Object(std::move(TextEdit))}));
			}

			void HandleOnTypeFormatting(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				const FJson* const PositionNode = Params.Find("position");
				const std::string Trigger = Params.GetString("ch");
				if (!TextDocument || !PositionNode || (Trigger != ";" && Trigger != "}" && Trigger != "\n"))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				if (HasBareCarriageReturn(Source))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const FTextPosition Position = PositionFromJson(*PositionNode, Source);
				if (Trigger == "\n" && (Position.Line <= 1u || Position.Line > 1u + std::count(Source.begin(), Source.end(), '\n')))
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				const std::uint32_t TargetLine = Trigger == "\n"
				    ? Position.Line - 1u
				    : Position.Line;
				const std::string_view OriginalLine = SourceLine(Source, TargetLine);
				FTextRange LineRange;
				// Enter leaves the caret on the new line. Format only the completed
				// physical line, even when the client inserted indentation after Enter.
				LineRange.StartLine = TargetLine;
				LineRange.StartColumn = 1;
				LineRange.EndLine = TargetLine;
				LineRange.EndColumn = static_cast<std::uint32_t>(OriginalLine.size() + 1u);
				const FRangeFormatResult Result = Workspace.FormatRangeDocument(
				    Path, LineRange, FormattingIndentWidth(Params), FormattingInsertSpaces(Params));
				if (!Result.bSuccess || !Result.bHasFormattedRange)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				std::string FormattedLine = Result.FormattedSource;
				if (!FormattedLine.empty() && FormattedLine.back() == '\n')
				{
					FormattedLine.pop_back();
				}
				// On-type edits must not replace the line ending (especially CRLF) or
				// create new lines while the user is typing an incomplete statement.
				if (FormattedLine.find_first_of("\r\n") != std::string::npos || FormattedLine == OriginalLine)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				FTextRange EditRange;
				EditRange.StartLine = TargetLine;
				EditRange.StartColumn = 1;
				EditRange.EndLine = TargetLine;
				EditRange.EndColumn = static_cast<std::uint32_t>(OriginalLine.size() + 1u);
				FJson::FObject Edit;
				Edit["range"] = RangeToJson(EditRange, Source);
				Edit["newText"] = FJson::String(std::move(FormattedLine));
				WriteResponseResult(Id, FJson::Array({FJson::Object(std::move(Edit))}));
			}

			void HandleCodeAction(const FJson& Id, const FJson& Params)
			{
				const FJson* const TextDocument = Params.Find("textDocument");
				if (!TextDocument)
				{
					WriteResponseResult(Id, FJson::Array());
					return;
				}
				bool bFormatRequested = true;
				bool bOrganizeImportsRequested = true;
				bool bOverrideRequested = true;
				if (const FJson* const Context = Params.Find("context"))
				{
					if (const FJson* const Only = Context->Find("only"))
					{
						if (const FJson::FArray* const Kinds = Only->ArrayPtr(); Kinds && !Kinds->empty())
						{
							bFormatRequested = false;
							bOrganizeImportsRequested = false;
							bOverrideRequested = false;
							for (const FJson& Kind : *Kinds)
							{
								const std::string* const RequestedKind = std::get_if<std::string>(&Kind.Value);
								if (!RequestedKind)
								{
									continue;
								}
								if (*RequestedKind == "source" || *RequestedKind == "source.formatDocument")
								{
									bFormatRequested = true;
								}
								if (*RequestedKind == "source" || *RequestedKind == "source.organizeImports")
								{
									bOrganizeImportsRequested = true;
								}
								if (*RequestedKind == "refactor" || *RequestedKind == "refactor.rewrite")
								{
									bOverrideRequested = true;
								}
							}
							if (!bFormatRequested && !bOrganizeImportsRequested && !bOverrideRequested)
							{
								WriteResponseResult(Id, FJson::Array());
								return;
							}
						}
					}
				}
				const std::string Path = UriToPath(TextDocument->GetString("uri"));
				const std::string Source = SourceForPath(Path);
				FJson::FArray Actions;
				const auto MakeFullDocumentEdit = [&Path, &Source](const std::string& NewText)
				{
					FJson::FObject TextEdit;
					TextEdit["range"] = FullDocumentRangeToJson(Source);
					TextEdit["newText"] = FJson::String(NewText);
					FJson::FObject Changes;
					Changes[PathToUri(Path)] = FJson::Array({FJson::Object(std::move(TextEdit))});
					return FJson::Object({{"changes", FJson::Object(std::move(Changes))}});
				};
				if (bOverrideRequested)
				{
					const FJson* const Range = Params.Find("range");
					const FJson* const Start = Range ? Range->Find("start") : nullptr;
					const FJson* const End = Range ? Range->Find("end") : nullptr;
					if (Start && End)
					{
						const FTextPosition Position = PositionFromJson(*Start, Source);
						const FTextPosition EndPosition = PositionFromJson(*End, Source);
						if (Position.Line == EndPosition.Line && Position.Column == EndPosition.Column)
						{
							const FCompletionResult Completion =
							    Workspace.CompleteAtDocument(Path, Position);
							if (Completion.Prefix.empty())
							{
								FTextRange InsertRange;
								InsertRange.StartLine = Position.Line;
								InsertRange.StartColumn = Position.Column;
								InsertRange.EndLine = Position.Line;
								InsertRange.EndColumn = Position.Column;
								for (const FCompletionItem& Item : Completion.Items)
								{
									if (!Item.bOverrideMember)
									{
										continue;
									}
									std::string NewText = Item.InsertText;
									const std::size_t Stub = NewText.find("${0:throw 0;}");
									if (Stub != std::string::npos)
									{
										NewText.replace(Stub, 13, "throw 0;");
									}
									FJson::FObject TextEdit;
									TextEdit["range"] = RangeToJson(InsertRange, Source);
									TextEdit["newText"] = FJson::String(std::move(NewText));
									FJson::FObject Changes;
									Changes[PathToUri(Path)] = FJson::Array({FJson::Object(std::move(TextEdit))});
									FJson::FObject Action;
									Action["title"] = FJson::String(
									    Locale::TranslateId(Item.bImplementsInterfaceMember
									            ? "lsp.code_action.implement_interface_member"
									            : "lsp.code_action.override_inherited_member",
									        {Item.Detail}));
									Action["kind"] = FJson::String("refactor.rewrite");
									Action["edit"] = FJson::Object({{"changes", FJson::Object(std::move(Changes))}});
									Actions.push_back(FJson::Object(std::move(Action)));
								}
							}
						}
					}
				}
				if (bFormatRequested)
				{
					const FFormatResult Format = Workspace.FormatDocument(Path);
					if (Format.bSuccess)
					{
						FJson::FObject Action;
						Action["title"] = FJson::String(
						    Locale::TranslateId("lsp.code_action.format_document"));
						Action["kind"] = FJson::String("source.formatDocument");
						Action["edit"] = MakeFullDocumentEdit(Format.FormattedSource);
						Action["data"] = FJson::Object(
						    {{"veroscriptAction", FJson::String("formatDocument")}});
						Actions.push_back(FJson::Object(std::move(Action)));
					}
				}

				if (bOrganizeImportsRequested)
				{
					FJson::FArray Edits;
					for (const auto& Range : Detail::FindDuplicateImportLines(Source, Path, Workspace))
					{
						Edits.push_back(FJson::Object({{"range", RangeToJson(Range, Source)}, {"newText", FJson::String("")}}));
					}
					if (!Edits.empty())
					{
						FJson::FObject Action;
						Action["title"] = FJson::String(
						    Locale::TranslateId("lsp.code_action.remove_duplicate_imports"));
						Action["kind"] = FJson::String("source.organizeImports");
						FJson::FObject Changes;
						Changes[PathToUri(Path)] = FJson::Array(std::move(Edits));
						Action["edit"] = FJson::Object({{"changes", FJson::Object(std::move(Changes))}});
						Action["data"] = FJson::Object(
						    {{"veroscriptAction", FJson::String("removeDuplicateImports")}});
						Actions.push_back(FJson::Object(std::move(Action)));
					}
				}
				WriteResponseResult(Id, FJson::Array(std::move(Actions)));
			}

			void HandleCodeActionResolve(const FJson& Id, const FJson& Params)
			{
				// All currently advertised actions are safe, eagerly materialised edits.
				// Resolve remains implemented for clients that uniformly resolve their
				// action list before presenting it; it preserves the exact action payload.
				const FJson::FObject Action = Params.ObjectPtr()
				    ? *Params.ObjectPtr()
				    : FJson::FObject{};
				WriteResponseResult(Id, FJson::Object(Action));
			}

			void HandleWorkspaceSymbol(const FJson& Id, const FJson& Params)
			{
				const std::string Query = Params.GetString("query");
				FWorkspaceSymbolsOptions QueryOptions;
				QueryOptions.ShouldCancel = [this, Id]()
				{
					return IsRequestCancellationRequested(Id);
				};
				const FWorkspaceSymbolsResult Result = Workspace.WorkspaceSymbols(QueryOptions);
				if (Result.bCancelled)
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}

				struct FMatch
				{
					const FWorkspaceSymbol* Symbol;
					int Score;
					std::size_t Order;
				};
				std::vector<FMatch> MatchingSymbols;
				MatchingSymbols.reserve(Result.Symbols.size());
				std::size_t ScanIndex = 0;
				for (const FWorkspaceSymbol& Symbol : Result.Symbols)
				{
					// Matching a large in-memory index can itself dominate a symbol
					// request after filesystem scanning has finished. Poll at a bounded
					// cadence so explicit cancellation and configured deadlines do not
					// wait for the whole candidate set to be filtered.
					if ((++ScanIndex & 0x7fu) == 0u && IsRequestCancellationRequested(Id))
					{
						WriteResponseError(Id, -32800,
						    RequestStopMessage(ERequestStopReason::Cancelled));
						return;
					}
					if (WorkspaceSymbolMatches(Symbol, Query))
					{
						MatchingSymbols.push_back({&Symbol, WorkspaceSymbolMatchScore(Symbol, Query), ScanIndex});
					}
				}
				std::size_t SortWork = 0;
				const auto MatchOrder = [&SortWork](const FMatch& A, const FMatch& B)
				{
					if ((++SortWork & 0x7fu) == 0u)
					{
						CheckAnalysisCancellation();
					}
					if (A.Score != B.Score)
					{
						return A.Score < B.Score;
					}
					const auto* Left = A.Symbol;
					const auto* Right = B.Symbol;
					if (Left->Name != Right->Name)
					{
						return Left->Name < Right->Name;
					}
					if (Left->ContainerName != Right->ContainerName)
					{
						return Left->ContainerName < Right->ContainerName;
					}
					if (Left->FileLabel != Right->FileLabel)
					{
						return Left->FileLabel < Right->FileLabel;
					}
					return A.Order < B.Order;
				};
				const auto Count = (std::min)(MatchingSymbols.size(), MaxWorkspaceSymbolResults);
				std::partial_sort(MatchingSymbols.begin(), MatchingSymbols.begin() + Count,
				    MatchingSymbols.end(), MatchOrder);

				FJson::FArray Symbols;
				Symbols.reserve(Count);
				std::unordered_map<std::string, std::string> SourceSnapshots;
				for (const FMatch& Match : MatchingSymbols)
				{
					if (Symbols.size() >= MaxWorkspaceSymbolResults || IsRequestCancellationRequested(Id))
					{
						break;
					}
					const FWorkspaceSymbol& Symbol = *Match.Symbol;

					FJson::FObject Location;
					if (Symbol.FileLabel.empty())
					{
						// Soft builtins / host catalog entries have no source file.
						Location["uri"] = FJson::String("veroscript:host-bindings");
						FJson::FObject Zero;
						Zero["line"] = FJson::Number(0);
						Zero["character"] = FJson::Number(0);
						FJson::FObject Range;
						Range["start"] = FJson::Object(Zero);
						Range["end"] = FJson::Object(Zero);
						Location["range"] = FJson::Object(std::move(Range));
					}
					else
					{
						Location["uri"] = FJson::String(NavigationUri(Symbol.FileLabel));
						// One source snapshot per response: repeated symbols from a
						// closed file must not repeatedly read/copy its entire contents.
						const auto [Source, bInserted] = SourceSnapshots.try_emplace(Symbol.FileLabel);
						if (bInserted)
						{
							Source->second = SourceForPath(Symbol.FileLabel);
						}
						const std::string& SymbolSource = Source->second;
						if (Symbol.FileLabel.rfind("veroscript-generated:", 0) == 0 &&
						    !Symbol.GeneratedOrigin.FileLabel.empty())
						{
							const auto& Origin = Symbol.GeneratedOrigin;
							Location["uri"] = FJson::String(NavigationUri(Origin.FileLabel));
							Location["range"] = RangeToJson({Origin.Line, Origin.Column,
							                                    Origin.Line, Origin.Column + 1},
							    SourceForPath(Origin.FileLabel));
						}
						else
						{
							Location["range"] = RangeToJson(
							    SelectionRangeForOutlineSymbol(Symbol.Range, Symbol.Name, Symbol.Kind, SymbolSource),
							    SymbolSource);
						}
					}

					FJson::FObject Item;
					Item["name"] = FJson::String(Symbol.Name);
					Item["detail"] = FJson::String(Symbol.Detail);
					Item["kind"] = FJson::Number(
					    static_cast<double>(OutlineKindToSymbolKind(Symbol.Kind)));
					Item["containerName"] = FJson::String(Symbol.ContainerName);
					Item["location"] = FJson::Object(std::move(Location));
					Item["data"] = FJson::Object(
					    {{"veroscriptSymbolId", FJson::String(WorkspaceSymbolId(Symbol))}});
					Symbols.push_back(FJson::Object(std::move(Item)));
				}
				WriteResponseResult(Id, FJson::Array(std::move(Symbols)));
			}

			void HandleWorkspaceSymbolResolve(const FJson& Id, const FJson& Params)
			{
				FJson::FObject Item = Params.ObjectPtr() ? *Params.ObjectPtr() : FJson::FObject{};
				const FJson* const Data = Params.Find("data");
				const std::string SymbolId = Data ? Data->GetString("veroscriptSymbolId") : std::string{};
				if (SymbolId.empty())
				{
					WriteResponseResult(Id, FJson::Object(std::move(Item)));
					return;
				}
				FWorkspaceSymbolsOptions QueryOptions;
				QueryOptions.ShouldCancel = [this, Id]()
				{
					return IsRequestCancellationRequested(Id);
				};
				const FWorkspaceSymbolsResult Symbols = Workspace.WorkspaceSymbols(QueryOptions);
				if (Symbols.bCancelled)
				{
					WriteResponseError(Id, -32800,
					    RequestStopMessage(ERequestStopReason::Cancelled));
					return;
				}
				const auto Found = std::find_if(
				    Symbols.Symbols.begin(), Symbols.Symbols.end(), [&SymbolId](const FWorkspaceSymbol& Symbol)
				{
					return WorkspaceSymbolId(Symbol) == SymbolId;
				});
				if (Found != Symbols.Symbols.end())
				{
					Item["detail"] = FJson::String(Found->Detail);
					Item["containerName"] = FJson::String(Found->ContainerName);
				}
				WriteResponseResult(Id, FJson::Object(std::move(Item)));
			}

			// ---- Configuration

			bool ApplySettingsRoot(const FJson& SettingsRoot)
			{
				const FJson* VeroScriptSettings = SettingsRoot.Find("veroscript");
				if (!VeroScriptSettings)
				{
					return false;
				}
				// Request budgets, tracing and result limits do not change diagnostics.
				// Recompiling every open buffer for those settings blocks the protocol
				// worker before it can even start the next request's deadline.
				const bool bNeedsAnalysis = VeroScriptSettings->Find("importPaths") || VeroScriptSettings->Find("implicitImportPaths") || VeroScriptSettings->Find("allowCircularImports") || VeroScriptSettings->Find("allowProjectWideAccess") || VeroScriptSettings->Find("defines") || VeroScriptSettings->Find("predefinedMacros") || VeroScriptSettings->Find("stdlibSearchPaths") || VeroScriptSettings->Find("stdlibPath") || VeroScriptSettings->Find("bindingCatalogPaths");

				FWorkspaceOptions NewOptions = Workspace.GetOptions();
				// Keep request and indexing limits configurable per client while clamping
				// them to values that cannot accidentally disable server safeguards.
				const auto ReadLimit = [VeroScriptSettings](
				                           const char* const Name,
				                           const int Current,
				                           const int Minimum,
				                           const int Maximum)
				{
					return (std::max)(Minimum, (std::min)(Maximum, VeroScriptSettings->GetInt(Name, Current)));
				};
				RequestTimeout = std::chrono::milliseconds(ReadLimit(
				    "lspRequestTimeoutMs", static_cast<int>(RequestTimeout.count()), 50, 30000));
				// Preserve the legacy all-request override unless an analysis-specific
				// budget is supplied; defaults are 2s interactive / 5s analysis.
				AnalysisTimeout = std::chrono::milliseconds(ReadLimit("lspAnalysisTimeoutMs",
				    VeroScriptSettings->Find("lspRequestTimeoutMs")
				        ? static_cast<int>(RequestTimeout.count())
				        : static_cast<int>(AnalysisTimeout.count()),
				    50, 30000));
				bTraceAnalysis = VeroScriptSettings->GetBool("lspTraceAnalysis", bTraceAnalysis);
				MaxCompletionItems = static_cast<std::size_t>(ReadLimit(
				    "maxCompletionItems", static_cast<int>(MaxCompletionItems), 32, 5000));
				MaxWorkspaceSymbolResults = static_cast<std::size_t>(ReadLimit(
				    "maxWorkspaceSymbolResults", static_cast<int>(MaxWorkspaceSymbolResults), 64, 20000));
				MaxReferenceResults = static_cast<std::size_t>(ReadLimit(
				    "maxReferenceResults", static_cast<int>(MaxReferenceResults), 64, 50000));
				NewOptions.MaxWorkspaceFiles = static_cast<std::size_t>(ReadLimit(
				    "maxWorkspaceFiles", static_cast<int>(NewOptions.MaxWorkspaceFiles), 1, 100000));
				NewOptions.MaxWorkspaceFileBytes = static_cast<std::size_t>(ReadLimit(
				    "maxWorkspaceFileBytes", static_cast<int>(NewOptions.MaxWorkspaceFileBytes),
				    1024, 64 * 1024 * 1024));
				if (const FJson* ImportPaths = VeroScriptSettings->Find("importPaths"))
				{
					if (const FJson::FArray* Array = ImportPaths->ArrayPtr())
					{
						std::vector<std::string> Paths;
						Paths.reserve(Array->size());
						for (const FJson& Entry : *Array)
						{
							if (const std::string* Path = std::get_if<std::string>(&Entry.Value))
							{
								Paths.push_back(*Path);
							}
						}
						NewOptions.ImportPaths = std::move(Paths);
					}
				}
				if (const FJson* ImportPaths =
				        VeroScriptSettings->Find("implicitImportPaths"))
				{
					if (const FJson::FArray* Array = ImportPaths->ArrayPtr())
					{
						std::vector<std::string> Paths;
						Paths.reserve(Array->size());
						for (const FJson& Entry : *Array)
						{
							if (const std::string* Path =
							        std::get_if<std::string>(&Entry.Value))
							{
								Paths.push_back(*Path);
							}
						}
						NewOptions.ImplicitImportPaths = std::move(Paths);
					}
				}
				NewOptions.bAllowCircularImports = VeroScriptSettings->GetBool(
				    "allowCircularImports",
				    NewOptions.bAllowCircularImports);
				NewOptions.bAllowProjectWideAccess = VeroScriptSettings->GetBool(
				    "allowProjectWideAccess",
				    NewOptions.bAllowProjectWideAccess);
				if (const FJson* Defines = VeroScriptSettings->Find("defines"))
				{
					if (const FJson::FArray* Array = Defines->ArrayPtr())
					{
						std::vector<std::string> DefineList;
						DefineList.reserve(Array->size());
						for (const FJson& Entry : *Array)
						{
							if (const std::string* Define = std::get_if<std::string>(&Entry.Value))
							{
								DefineList.push_back(*Define);
							}
						}
						NewOptions.Defines = std::move(DefineList);
					}
				}
				if (const FJson* Macros = VeroScriptSettings->Find("predefinedMacros"))
				{
					if (const FJson::FArray* Array = Macros->ArrayPtr())
					{
						std::vector<VeroScript::FPredefinedMacro> Parsed;
						Parsed.reserve(Array->size());
						for (const FJson& Entry : *Array)
						{
							if (!Entry.ObjectPtr())
							{
								continue;
							}
							VeroScript::FPredefinedMacro Macro;
							Macro.Name = Entry.GetString("name");
							Macro.Body = Entry.GetString("body");
							Macro.SourceName = Entry.GetString("sourceName", "<host>");
							Macro.bVariadic = Entry.GetBool("variadic");
							if (const FJson* Parameters = Entry.Find("parameters"))
							{
								if (const FJson::FArray* ParamArray = Parameters->ArrayPtr())
								{
									for (const FJson& Parameter : *ParamArray)
									{
										if (const std::string* Name =
										        std::get_if<std::string>(&Parameter.Value))
										{
											Macro.Parameters.push_back(*Name);
										}
									}
								}
							}
							Parsed.push_back(std::move(Macro));
						}
						NewOptions.PredefinedMacros = std::move(Parsed);
					}
				}
				if (const FJson* StdlibPaths = VeroScriptSettings->Find("stdlibSearchPaths");
				    StdlibPaths && StdlibPaths->ArrayPtr())
				{
					NewOptions.StdlibSearchPaths.clear();
					for (const FJson& Entry : *StdlibPaths->ArrayPtr())
					{
						if (const std::string* Path = std::get_if<std::string>(&Entry.Value);
						    Path && !Path->empty())
						{
							NewOptions.StdlibSearchPaths.push_back(*Path);
						}
					}
				}
				else if (const FJson* StdlibPath = VeroScriptSettings->Find("stdlibPath"))
				{
					NewOptions.StdlibSearchPaths.clear();
					if (const std::string* Path = std::get_if<std::string>(&StdlibPath->Value);
					    Path != nullptr && !Path->empty())
					{
						NewOptions.StdlibSearchPaths.push_back(*Path);
					}
				}
				if (const FJson* CatalogPaths = VeroScriptSettings->Find("bindingCatalogPaths"))
				{
					std::vector<std::string> Paths;
					if (const FJson::FArray* Array = CatalogPaths->ArrayPtr())
					{
						for (const FJson& Entry : *Array)
						{
							if (const std::string* Path = std::get_if<std::string>(&Entry.Value);
							    Path != nullptr && !Path->empty())
							{
								Paths.push_back(*Path);
							}
						}
					}
					ReloadBindingCatalogs(Paths);
					PublishBindingCatalogStatus();
					if (bWorkspaceWatcherRegistered)
					{
						RegisterWorkspaceWatcherIfSupported();
					}
				}
				// Protocol-only settings retain both the semantic snapshots and the
				// workspace symbol index. Index limits affect discovery, not parsing.
				if (bNeedsAnalysis || NewOptions.MaxWorkspaceFiles != Workspace.GetOptions().MaxWorkspaceFiles || NewOptions.MaxWorkspaceFileBytes != Workspace.GetOptions().MaxWorkspaceFileBytes)
				{
					ApplyBindingsToOptions(NewOptions);
					Workspace.SetOptions(NewOptions);
				}
				if (bNeedsAnalysis)
				{
					InvalidateSemanticAnalysis();
				}
				return bNeedsAnalysis;
			}

			void HandleDidChangeConfiguration(const FJson& Params)
			{
				if (const FJson* Settings = Params.Find("settings"))
				{
					if (ApplySettingsRoot(*Settings))
					{
						ReanalyzeAllOpenDocuments();
					}
				}
			}

			// ---- Diagnostics

			struct FDiagnosticFileContribution
			{
				std::string Path;
				// Encode against the analysis input once. Another entry can retain this
				// contribution after an overlay closes; its UTF-16 ranges must not be
				// recomputed using unrelated or missing disk contents.
				FJson::FArray Items;
			};
			using FDiagnosticContributions = std::map<std::string, FDiagnosticFileContribution>;

			void InvalidateDiagnosticTarget(const std::string& Path)
			{
				// An importer's old locations cannot be republished with a changed
				// overlay's current version, or after that overlay returns to disk.
				const auto Key = PathUtil::IdentityKey(Path);
				for (auto& [Root, Contributions] : DiagnosticContributionsByRoot)
				{
					Contributions.erase(Key);
				}
			}

			void WritePublishDiagnostics(
			    const std::string& Uri,
			    FJson::FArray Items)
			{
				FJson::FObject Params;
				Params["uri"] = FJson::String(Uri);
				// Attach the open-buffer version whenever one is available. Clients can
				// then discard diagnostics produced for a superseded overlay rather than
				// briefly painting an old analysis result over new source.
				if (const auto Version = DocumentVersions.find(
				        PathUtil::IdentityKey(UriToPath(Uri)));
				    Version != DocumentVersions.end())
				{
					Params["version"] = FJson::Number(static_cast<double>(Version->second));
				}
				Params["diagnostics"] = FJson::Array(std::move(Items));
				WriteNotification("textDocument/publishDiagnostics", FJson::Object(std::move(Params)));
			}

			FDiagnosticContributions CollectDiagnosticContributions(
			    const FAnalyzeResult& Result, const std::string_view EntryPath)
			{
				FDiagnosticContributions Contributions;
				std::unordered_map<std::string, std::string> Sources;
				for (const FDiagnostic& Diagnostic : Result.Diagnostics)
				{
					const std::string Path = Diagnostic.FileLabel.empty() ? std::string(EntryPath) : Diagnostic.FileLabel;
					auto& File = Contributions[PathUtil::IdentityKey(Path)];
					File.Path = Path;
					FJson::FObject Item;
					Item["range"] = RangeToJson(Diagnostic.Range, SourceForPath(Path, Sources));
					Item["severity"] = FJson::Number(DiagnosticSeverityToLsp(Diagnostic.Severity));
					Item["message"] = FJson::String(Diagnostic.Message);
					if (!Diagnostic.Code.empty())
					{
						Item["code"] = FJson::String(Diagnostic.Code);
					}
					Item["source"] = FJson::String("veroscript");
					File.Items.push_back(FJson::Object(std::move(Item)));
				}
				return Contributions;
			}

			static void AddDiagnosticPaths(const FDiagnosticContributions& Contributions,
			    std::map<std::string, std::string>& Paths)
			{
				for (const auto& [Key, File] : Contributions)
				{
					Paths.try_emplace(Key, File.Path);
				}
			}

			void PublishDiagnosticPaths(const std::map<std::string, std::string>& Paths)
			{
				std::map<std::string, FJson::FArray> ItemsByPath;
				std::map<std::string, std::set<std::string>> SeenByPath;
				// Visit contributions once, rather than scanning every root for each
				// open path during a workspace refresh (quadratic even for clean files).
				for (const auto& [Root, Contributions] : DiagnosticContributionsByRoot)
				{
					for (const auto& [Key, File] : Contributions)
					{
						if (!Paths.count(Key))
						{
							continue;
						}
						auto& Items = ItemsByPath[Key];
						auto& Seen = SeenByPath[Key];
						for (const FJson& Item : File.Items)
						{
							if (Seen.insert(ToJsonString(Item)).second)
							{
								Items.push_back(Item);
							}
						}
					}
				}
				for (const auto& [Key, Path] : Paths)
				{
					const auto Items = ItemsByPath.find(Key);
					WritePublishDiagnostics(PathToUri(Path),
					    Items == ItemsByPath.end() ? FJson::FArray{} : std::move(Items->second));
				}
			}

			void ReplaceDiagnosticContributions(const std::string& Path, const FAnalyzeResult* Result)
			{
				const auto Root = PathUtil::IdentityKey(Path);
				std::map<std::string, std::string> Affected{{Root, Path}};
				auto Contributions = Result ? CollectDiagnosticContributions(*Result, Path) : FDiagnosticContributions{};
				CheckAnalysisCancellation();
				if (const auto Previous = DiagnosticContributionsByRoot.find(Root);
				    Previous != DiagnosticContributionsByRoot.end())
				{
					AddDiagnosticPaths(Previous->second, Affected);
				}
				AddDiagnosticPaths(Contributions, Affected);
				if (Result)
				{
					DiagnosticContributionsByRoot[Root] = std::move(Contributions);
				}
				else
				{
					DiagnosticContributionsByRoot.erase(Root);
				}
				PublishDiagnosticPaths(Affected);
			}

			FAnalysisControlScope MakeDiagnosticAnalysisControl() const
			{
				const auto Deadline = std::chrono::steady_clock::now() + AnalysisTimeout;
				const auto* const OuterControl = ActiveAnalysisControl;
				return FAnalysisControlScope([Deadline, OuterControl]()
				{
					return std::chrono::steady_clock::now() >= Deadline || (OuterControl && OuterControl->ShouldCancel && OuterControl->ShouldCancel());
				});
			}

			void PublishDiagnosticsForDocument(const std::string& Path)
			{
				auto Control = MakeDiagnosticAnalysisControl();
				const FAnalyzeResult Result = Workspace.AnalyzeDocument(Path);
				ReplaceDiagnosticContributions(Path, &Result);
			}

			void ReanalyzeAllOpenDocuments()
			try
			{
				auto Control = MakeDiagnosticAnalysisControl();
				std::map<std::string, FDiagnosticContributions> Updated;
				std::map<std::string, std::string> Affected;
				for (const auto& [Root, Contributions] : DiagnosticContributionsByRoot)
				{
					AddDiagnosticPaths(Contributions, Affected);
				}
				(void)Workspace.AnalyzeAll([&](const std::string_view Path, const FAnalyzeResult& Result)
				{
					const auto Key = PathUtil::IdentityKey(std::string(Path));
					Affected.try_emplace(Key, Path);
					auto Contributions = CollectDiagnosticContributions(Result, Path);
					AddDiagnosticPaths(Contributions, Affected);
					Updated.emplace(Key, std::move(Contributions));
				});
				CheckAnalysisCancellation();
				DiagnosticContributionsByRoot = std::move(Updated);
				PublishDiagnosticPaths(Affected);
			}
			catch (const FAnalysisCancelled&)
			{
				// Catalog refresh runs before dispatch: a diagnostic deadline must
				// not discard the document change or request that triggered it.
				// The diagnostic scope has unwound; preserve outer cancellation.
				CheckAnalysisCancellation();
			}

			std::istream& In;
			std::ostream& Out;
			FLspStdioRunOptions RunOptions;
			VeroScript::FHostBindingRegistry BindingCatalog;
			bool bHasLoadedBindingCatalog = false;
			bool bBindingCatalogFresh = true;
			std::string BindingCatalogError;
			std::optional<bool> LastReportedBindingFresh;
			bool bInitializeResponseSent = false;
			std::vector<std::string> ActiveBindingCatalogPaths;
			std::map<std::string, FBindingCatalogStamp> BindingCatalogStamps;
			VeroScriptWorkspace Workspace;
			bool bExit = false;
			bool bFramingError = false;
			bool bShutdownReceived = false;
			bool bClientSupportsCompletionSnippets = false;
			bool bClientSupportsWatchedFilesDynamicRegistration = false;
			bool bClientSupportsRelativeWatchPatterns = false;
			bool bWorkspaceWatcherRegistered = false;
			std::vector<std::string> RegisteredBindingCatalogPaths;
			std::string RegisteredWatcherId;
			std::uint64_t NextWatcherRequestId = 0;
			std::mutex IncomingMutex;
			std::condition_variable IncomingCondition;
			// Parse errors share the ordered protocol worker and its output stream.
			std::deque<std::pair<FJson, bool>> IncomingMessages;
			static constexpr std::size_t MaxQueuedMessages = 256;
			bool bInputClosed = false;
			// Last accepted LSP version per open document. Stale/out-of-order changes
			// are ignored so delayed IDE work cannot roll an overlay backwards.
			std::map<std::string, std::int64_t> DocumentVersions;
			// Request lifecycle is protected because the input thread may receive
			// $/cancelRequest while the worker is analysing an earlier request.
			std::set<std::string> PendingRequestIds;
			std::set<std::string> ActiveRequestIds;
			std::set<std::string> CancelledRequestIds;
			std::map<std::string, std::chrono::steady_clock::time_point> ActiveRequestDeadlines;
			std::size_t MaxCompletionItems = 512;
			std::size_t MaxWorkspaceSymbolResults = 2000;
			std::size_t MaxReferenceResults = 5000;
			std::chrono::milliseconds RequestTimeout{2000};
			// Weak ownership releases large buffers when queued work finishes.
			std::weak_ptr<const FSemanticWorkspaceSnapshot> QueuedSnapshot;
			std::uint64_t QueuedSnapshotRevision = 0;
			// semanticTokens/full/delta is lossless: on a cache hit we replace the
			// prior integer stream atomically rather than attempting fragile token
			// coordinate diffs while a buffer is incomplete.
			std::map<std::string, std::pair<std::string, FJson::FArray>> SemanticTokenSnapshots;
			std::uint64_t NextSemanticTokenResultId = 1;

			std::atomic<std::uint64_t> SemanticRevision{1};
			std::atomic<std::uint64_t> SemanticConfiguration{1};
			std::map<std::string, std::shared_ptr<std::atomic<std::uint64_t>>> SemanticDocumentRevisions;
			std::map<std::string, FReadySemanticTokens> ReadySemanticTokens;
			// These queues/counters share IncomingMutex with request lifecycle state.
			std::deque<std::shared_ptr<FSemanticAnalysisJob>> SemanticJobs;
			std::deque<std::shared_ptr<FSemanticAnalysisJob>> CompletedSemanticJobs;
			std::size_t PendingSemanticJobs = 0;
			// Keep separate storage: the worker stop flag uses IncomingMutex,
			// while tracing is written only by the protocol worker.
			bool bStopSemanticWorker = false;
			bool bTraceAnalysis = false;
			std::chrono::milliseconds AnalysisTimeout{5000};

			// Each open analysis entry owns its contribution to itself and its imports.
			// Updating one entry cannot erase another entry's independent diagnostics.
			std::map<std::string, FDiagnosticContributions> DiagnosticContributionsByRoot;
			std::set<std::string> PublishedModuleSources;
		};
	}

	int VeroScriptLspStdio::Run(
	    std::istream& In,
	    std::ostream& Out,
	    const FLspStdioRunOptions& Options)
	{
		const Internal::FScopedProtocolStreamTies StreamTies(In, Out);
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		return FLspStdioSession(In, Out, Options).Run();
	}
}
