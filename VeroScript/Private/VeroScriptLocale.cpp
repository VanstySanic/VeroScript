// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptLocale.h"

#include "VeroScriptEnvironment.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptPlatform.h"

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VeroScript::Locale
{
	namespace
	{
		using FCatalog = std::unordered_map<std::string, std::string>;
		using FCatalogs = std::unordered_map<std::string, FCatalog>;
		constexpr bool IsAsciiAlpha(const char Character)
		{
			return (Character >= 'A' && Character <= 'Z')
				|| (Character >= 'a' && Character <= 'z');
		}

		constexpr bool IsAsciiDigit(const char Character)
		{
			return Character >= '0' && Character <= '9';
		}

		constexpr char ToAsciiLower(const char Character)
		{
			return Character >= 'A' && Character <= 'Z'
				? static_cast<char>(Character - 'A' + 'a')
				: Character;
		}

		constexpr char ToAsciiUpper(const char Character)
		{
			return Character >= 'a' && Character <= 'z'
				? static_cast<char>(Character - 'a' + 'A')
				: Character;
		}

		constexpr std::string_view FallbackLanguageTag = "en-US";
		std::string ActiveLanguageTag(FallbackLanguageTag);
		bool bConfigured = false;
		bool bCatalogsLoaded = false;
		std::string SourceModuleRoot;
		std::set<std::string> LoadedSourceModules;
		std::recursive_mutex LocaleMutex;

		FCatalogs Catalogs;
		FCatalog CatalogEn;
		std::unordered_map<std::string, std::string> ExactEnglishToId;
		std::vector<std::pair<std::string, std::string>> CatalogEnOrdered; // id, template
		bool bEnglishIndexDirty = true;

		bool CanonicalizeLanguageTag(std::string_view Input, std::string& OutTag)
		{
			std::string Tag(Input);
			const auto First = Tag.find_first_not_of(" \t\r\n");
			if (First == std::string::npos)
			{
				return false;
			}
			Tag.erase(0, First);
			const auto Last = Tag.find_last_not_of(" \t\r\n");
			Tag.resize(Last + 1);
			const std::size_t Encoding = Tag.find('.');
			if (Encoding != std::string::npos)
			{
				Tag.resize(Encoding);
			}
			const std::size_t Modifier = Tag.find('@');
			if (Modifier != std::string::npos)
			{
				Tag.resize(Modifier);
			}
			std::string Lower(Tag);
			std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](const char Ch)
			{
				return ToAsciiLower(Ch);
			});
			if (Lower == "c" || Lower == "posix")
			{
				OutTag = FallbackLanguageTag;
				return true;
			}
			for (char& Character : Tag)
			{
				if (Character == '_')
				{
					Character = '-';
				}
				else if (!IsAsciiAlpha(Character) && !IsAsciiDigit(Character)
					&& Character != '-')
				{
					return false;
				}
			}
			std::vector<std::string> Parts;
			std::size_t Start = 0;
			while (Start <= Tag.size())
			{
				const std::size_t End = Tag.find('-', Start);
				const std::size_t Length = (End == std::string::npos ? Tag.size() : End) - Start;
				if (Length == 0)
				{
					return false;
				}
				Parts.emplace_back(Tag.substr(Start, Length));
				if (End == std::string::npos)
				{
					break;
				}
				Start = End + 1;
			}
			if (Parts.empty() || Parts.front().size() < 2 || Parts.front().size() > 8
				|| !std::all_of(Parts.front().begin(), Parts.front().end(), [](const char Ch)
				{
					return IsAsciiAlpha(Ch);
				}))
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Parts.size(); ++Index)
			{
				std::string& Part = Parts[Index];
				std::transform(Part.begin(), Part.end(), Part.begin(), [](const char Ch)
				{
					return ToAsciiLower(Ch);
				});
				const bool bAlphabetic = std::all_of(Part.begin(), Part.end(), [](const char Ch)
				{
					return IsAsciiAlpha(Ch);
				});
				if (Index != 0 && Part.size() == 4 && bAlphabetic)
				{
					Part.front() = ToAsciiUpper(Part.front());
				}
				else if (Index != 0 && Part.size() == 2 && bAlphabetic)
				{
					std::transform(Part.begin(), Part.end(), Part.begin(), [](const char Ch)
					{
						return ToAsciiUpper(Ch);
					});
				}
			}
			OutTag.clear();
			for (const std::string& Part : Parts)
			{
				if (!OutTag.empty())
				{
					OutTag.push_back('-');
				}
				OutTag.append(Part);
			}
			return true;
		}

		std::string PrimaryLanguage(const std::string_view Tag)
		{
			const std::size_t Separator = Tag.find('-');
			return std::string(Tag.substr(0, Separator));
		}

		std::string ChineseScript(const std::string_view Tag)
		{
			std::string Inferred;
			std::size_t Separator = Tag.find('-');
			while (Separator != std::string_view::npos)
			{
				const std::size_t Start = Separator + 1;
				Separator = Tag.find('-', Start);
				const std::string_view Part = Tag.substr(Start,
					(Separator == std::string_view::npos ? Tag.size() : Separator) - Start);
				if (Part.size() == 1) // Extension/private-use subtags are not script/region.
				{
					break;
				}
				if (Part == "Hant" || Part == "Hans")
				{
					return std::string(Part); // Explicit script wins over region inference.
				}
				if (Inferred.empty())
				{
					if (Part == "TW" || Part == "HK" || Part == "MO")
					{
						Inferred = "Hant";
					}
					else if (Part == "CN" || Part == "SG")
					{
						Inferred = "Hans";
					}
				}
			}
			return Inferred;
		}

		bool ResolveLanguageTag(const std::string_view Tag, std::string& OutLanguageTag)
		{
			std::string Canonical;
			if (!CanonicalizeLanguageTag(Tag, Canonical))
			{
				return false;
			}
			if (Catalogs.find(Canonical) != Catalogs.end())
			{
				OutLanguageTag = Canonical;
				return true;
			}

			std::string Parent = Canonical;
			for (;;)
			{
				const std::size_t Separator = Parent.rfind('-');
				if (Separator == std::string::npos)
				{
					break;
				}
				Parent.resize(Separator);
				if (Catalogs.find(Parent) != Catalogs.end())
				{
					OutLanguageTag = Parent;
					return true;
				}
			}

			const std::string Language = PrimaryLanguage(Canonical);
			const std::string RequestedChineseScript = Language == "zh"
				? ChineseScript(Canonical)
				: std::string{};
			int BestScore = -1;
			std::string Best;
			for (const auto& Catalog : Catalogs)
			{
				if (PrimaryLanguage(Catalog.first) != Language)
				{
					continue;
				}
				const std::string CandidateChineseScript = Language == "zh"
					? ChineseScript(Catalog.first)
					: std::string{};
				if (!RequestedChineseScript.empty()
					&& CandidateChineseScript != RequestedChineseScript)
				{
					continue;
				}
				int Score = 0;
				if (!RequestedChineseScript.empty()
					&& CandidateChineseScript == RequestedChineseScript)
				{
					Score += 100;
				}
				if (Canonical.find(Catalog.first) == 0 || Catalog.first.find(Canonical) == 0)
				{
					Score += 10;
				}
				if (Score > BestScore || (Score == BestScore && Catalog.first < Best))
				{
					BestScore = Score;
					Best = Catalog.first;
				}
			}
			if (Best.empty())
			{
				return false;
			}
			OutLanguageTag = std::move(Best);
			return true;
		}

		std::string DetectSystemLanguageTag()
		{
			#ifdef _WIN32
			wchar_t LocaleName[LOCALE_NAME_MAX_LENGTH] = {};
			if (GetUserDefaultLocaleName(LocaleName, LOCALE_NAME_MAX_LENGTH) > 0)
			{
				char Utf8[LOCALE_NAME_MAX_LENGTH * 4] = {};
				const int Length = WideCharToMultiByte(
					CP_UTF8,
					0,
					LocaleName,
						-1,
					Utf8,
					static_cast<int>(sizeof(Utf8)),
					nullptr,
					nullptr);
				if (Length > 0)
				{
					return Utf8;
				}
			}
			#else
			std::string LocaleName;
			for (const char* const Name : {"LC_ALL", "LC_MESSAGES", "LANG"})
			{
				if (const auto Value = Environment::GetUtf8(Name); Value && !Value->empty())
				{
					LocaleName = *Value;
					break;
				}
			}
			if (LocaleName.empty())
			{
				if (const char* const SystemLocale = std::setlocale(LC_MESSAGES, ""))
				{
					LocaleName = SystemLocale;
				}
			}
			if (!LocaleName.empty())
			{
				return LocaleName;
			}
			#endif
			return std::string(FallbackLanguageTag);
		}

		void ConfigureLanguageIfNeeded()
		{
			if (bConfigured)
			{
				return;
			}
			bConfigured = true;
			if (GetHostPlatformInfo().bMobile) return;
			std::string Requested = DetectSystemLanguageTag();
			if (const auto Value = Environment::GetUtf8("VERO_LANG");
				Value && !Value->empty())
			{
				Requested = *Value;
			}
			std::string Resolved;
			ActiveLanguageTag = ResolveLanguageTag(Requested, Resolved)
				? std::move(Resolved)
				: std::string(FallbackLanguageTag);
		}

		std::size_t CountLiteralChars(const std::string_view Template)
		{
			std::size_t Count = 0;
			for (std::size_t Index = 0; Index < Template.size();)
			{
				if (Template[Index] == '{' && Index + 2 < Template.size())
				{
					const char Digit = Template[Index + 1];
					if (Digit >= '0' && Digit <= '9' && Template[Index + 2] == '}')
					{
						Index += 3;
						continue;
					}
				}
				++Count;
				++Index;
			}
			return Count;
		}

		bool TemplateHasPlaceholder(const std::string_view Template)
		{
			for (std::size_t Index = 0; Index + 2 < Template.size(); ++Index)
			{
				if (Template[Index] == '{'
					&& Template[Index + 1] >= '0'
					&& Template[Index + 1] <= '9'
					&& Template[Index + 2] == '}')
				{
					return true;
				}
			}
			return false;
		}

		void RebuildEnglishIndex()
		{
			if (!bEnglishIndexDirty) return;
			struct FTemplateOrder
			{
				const FCatalog::value_type* Entry;
				std::size_t LiteralCount;
			};
			std::vector<FTemplateOrder> Order;
			Order.reserve(CatalogEn.size());
			ExactEnglishToId.clear();
			CatalogEnOrdered.clear();
			CatalogEnOrdered.reserve(CatalogEn.size());
			for (const auto& Entry : CatalogEn)
			{
				Order.push_back({&Entry, CountLiteralChars(Entry.second)});
				// Exact O(1) lookup only for templates without `{0}`..`{9}` placeholders.
				if (!TemplateHasPlaceholder(Entry.second))
				{
					ExactEnglishToId.emplace(Entry.second, Entry.first);
				}
			}
			std::sort(
				Order.begin(),
				Order.end(),
				[](const auto& Left, const auto& Right)
				{
					const std::size_t LeftLit = Left.LiteralCount;
					const std::size_t RightLit = Right.LiteralCount;
					if (LeftLit != RightLit)
					{
						return LeftLit > RightLit;
					}
					if (Left.Entry->second.size() != Right.Entry->second.size())
					{
						return Left.Entry->second.size() > Right.Entry->second.size();
					}
					return Left.Entry->first < Right.Entry->first;
				});
			for (const auto& Template : Order)
			{
				CatalogEnOrdered.push_back(*Template.Entry);
			}
			bEnglishIndexDirty = false;
		}

		int HexDigitValue(const char Character)
		{
			if (Character >= '0' && Character <= '9')
			{
				return Character - '0';
			}
			if (Character >= 'a' && Character <= 'f')
			{
				return Character - 'a' + 10;
			}
			if (Character >= 'A' && Character <= 'F')
			{
				return Character - 'A' + 10;
			}
			return -1;
		}

		bool AppendUtf8CodePoint(std::string& Out, const std::uint32_t CodePoint)
		{
			if (CodePoint <= 0x7Fu)
			{
				Out.push_back(static_cast<char>(CodePoint));
				return true;
			}
			if (CodePoint <= 0x7FFu)
			{
				Out.push_back(static_cast<char>(0xC0u | (CodePoint >> 6u)));
				Out.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
				return true;
			}
			if (CodePoint >= 0xD800u && CodePoint <= 0xDFFFu)
			{
				return false;
			}
			if (CodePoint <= 0xFFFFu)
			{
				Out.push_back(static_cast<char>(0xE0u | (CodePoint >> 12u)));
				Out.push_back(static_cast<char>(0x80u | ((CodePoint >> 6u) & 0x3Fu)));
				Out.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
				return true;
			}
			if (CodePoint <= 0x10FFFFu)
			{
				Out.push_back(static_cast<char>(0xF0u | (CodePoint >> 18u)));
				Out.push_back(static_cast<char>(0x80u | ((CodePoint >> 12u) & 0x3Fu)));
				Out.push_back(static_cast<char>(0x80u | ((CodePoint >> 6u) & 0x3Fu)));
				Out.push_back(static_cast<char>(0x80u | (CodePoint & 0x3Fu)));
				return true;
			}
			return false;
		}

		bool IsValidUtf8(const std::string_view Text)
		{
			const auto IsContinuation = [](const unsigned char Byte)
			{
				return Byte >= 0x80u && Byte <= 0xBFu;
			};
			for (std::size_t Index = 0; Index < Text.size();)
			{
				const unsigned char First = static_cast<unsigned char>(Text[Index++]);
				if (First <= 0x7Fu)
				{
					continue;
				}
				if (First >= 0xC2u && First <= 0xDFu)
				{
					if (Index >= Text.size()
						|| !IsContinuation(static_cast<unsigned char>(Text[Index++])))
					{
						return false;
					}
					continue;
				}
				if (First >= 0xE0u && First <= 0xEFu)
				{
					if (Index + 1 >= Text.size())
					{
						return false;
					}
					const unsigned char Second = static_cast<unsigned char>(Text[Index++]);
					const unsigned char Third = static_cast<unsigned char>(Text[Index++]);
					if (!IsContinuation(Third)
						|| (First == 0xE0u && (Second < 0xA0u || Second > 0xBFu))
						|| (First == 0xEDu && (Second < 0x80u || Second > 0x9Fu))
						|| (First != 0xE0u && First != 0xEDu && !IsContinuation(Second)))
					{
						return false;
					}
					continue;
				}
				if (First >= 0xF0u && First <= 0xF4u)
				{
					if (Index + 2 >= Text.size())
					{
						return false;
					}
					const unsigned char Second = static_cast<unsigned char>(Text[Index++]);
					const unsigned char Third = static_cast<unsigned char>(Text[Index++]);
					const unsigned char Fourth = static_cast<unsigned char>(Text[Index++]);
					if (!IsContinuation(Third) || !IsContinuation(Fourth)
						|| (First == 0xF0u && (Second < 0x90u || Second > 0xBFu))
						|| (First == 0xF4u && (Second < 0x80u || Second > 0x8Fu))
						|| (First != 0xF0u && First != 0xF4u && !IsContinuation(Second)))
					{
						return false;
					}
					continue;
				}
				return false;
			}
			return true;
		}

		bool ParseFlatJsonStringMap(
			const std::string& Text,
			std::unordered_map<std::string, std::string>& OutMap,
			std::string* OutError)
		{
			OutMap.clear();
			if (!IsValidUtf8(Text))
			{
				if (OutError)
				{
					*OutError = "localization JSON: invalid UTF-8";
				}
				return false;
			}
			std::size_t Index = 0;
			auto SkipWs = [&]()
			{
				while (Index < Text.size()
					&& (Text[Index] == ' ' || Text[Index] == '\t'
						|| Text[Index] == '\n' || Text[Index] == '\r'))
				{
					++Index;
				}
			};
			auto ParseString = [&](std::string& Out) -> bool
			{
				SkipWs();
				if (Index >= Text.size() || Text[Index] != '"')
				{
					return false;
				}
				++Index;
				Out.clear();
				while (Index < Text.size())
				{
					const unsigned char Character =
						static_cast<unsigned char>(Text[Index++]);
					if (Character == '"')
					{
						return true;
					}
					if (Character < 0x20u)
					{
						return false;
					}
					if (Character != '\\')
					{
						Out.push_back(static_cast<char>(Character));
						continue;
					}
					if (Index >= Text.size())
					{
						return false;
					}
					const char Escape = Text[Index++];
					switch (Escape)
					{
					case '"':
						{
							Out.push_back('"');
							break;
						}
					case '\\':
						{
							Out.push_back('\\');
							break;
						}
					case '/':
						{
							Out.push_back('/');
							break;
						}
					case 'b':
						{
							Out.push_back('\b');
							break;
						}
					case 'f':
						{
							Out.push_back('\f');
							break;
						}
					case 'n':
						{
							Out.push_back('\n');
							break;
						}
					case 'r':
						{
							Out.push_back('\r');
							break;
						}
					case 't':
						{
							Out.push_back('\t');
							break;
						}
					case 'u':
						{
							auto ParseHexQuad = [&](std::uint32_t& OutCodeUnit) -> bool
							{
								if (Index + 4 > Text.size())
								{
									return false;
								}
								OutCodeUnit = 0;
								for (int DigitIndex = 0; DigitIndex < 4; ++DigitIndex)
								{
									const int Digit = HexDigitValue(Text[Index++]);
									if (Digit < 0)
									{
										return false;
									}
									OutCodeUnit = (OutCodeUnit << 4u)
									| static_cast<std::uint32_t>(Digit);
								}
								return true;
							};
							std::uint32_t CodePoint = 0;
							if (!ParseHexQuad(CodePoint))
							{
								return false;
							}
							if (CodePoint >= 0xD800u && CodePoint <= 0xDBFFu)
							{
								if (Index + 2 > Text.size()
								|| Text[Index] != '\\'
								|| Text[Index + 1] != 'u')
								{
									return false;
								}
								Index += 2;
								std::uint32_t LowSurrogate = 0;
								if (!ParseHexQuad(LowSurrogate)
								|| LowSurrogate < 0xDC00u
								|| LowSurrogate > 0xDFFFu)
								{
									return false;
								}
								CodePoint = 0x10000u
								+ ((CodePoint - 0xD800u) << 10u)
								+ (LowSurrogate - 0xDC00u);
							}
							else if (CodePoint >= 0xDC00u && CodePoint <= 0xDFFFu)
							{
								return false;
							}
							if (!AppendUtf8CodePoint(Out, CodePoint))
							{
								return false;
							}
							break;
						}
					default:
						return false;
					}
				}
				return false;
			};

			SkipWs();
			// UTF-8 BOM
			if (Index + 2 < Text.size()
				&& static_cast<unsigned char>(Text[Index]) == 0xEF
				&& static_cast<unsigned char>(Text[Index + 1]) == 0xBB
				&& static_cast<unsigned char>(Text[Index + 2]) == 0xBF)
			{
				Index += 3;
				SkipWs();
			}
			if (Index >= Text.size() || Text[Index] != '{')
			{
				if (OutError)
				{
					*OutError = "localization JSON: expected object";
				}
				return false;
			}
			++Index;
			SkipWs();
			if (Index < Text.size() && Text[Index] == '}')
			{
				++Index;
				SkipWs();
				if (Index == Text.size())
				{
					return true;
				}
				if (OutError)
				{
					*OutError = "localization JSON: unexpected trailing content";
				}
				return false;
			}
			while (Index < Text.size())
			{
				std::string Key;
				std::string Value;
				if (!ParseString(Key))
				{
					if (OutError)
					{
						*OutError = "localization JSON: expected key string";
					}
					return false;
				}
				SkipWs();
				if (Index >= Text.size() || Text[Index] != ':')
				{
					if (OutError)
					{
						*OutError = "localization JSON: expected ':'";
					}
					return false;
				}
				++Index;
				if (!ParseString(Value))
				{
					if (OutError)
					{
						*OutError = "localization JSON: expected value string";
					}
					return false;
				}
				if (!OutMap.emplace(Key, std::move(Value)).second)
				{
					if (OutError)
					{
						*OutError = "localization JSON: duplicate key '" + Key + "'";
					}
					return false;
				}
				SkipWs();
				if (Index < Text.size() && Text[Index] == ',')
				{
					++Index;
					SkipWs();
					continue;
				}
				if (Index < Text.size() && Text[Index] == '}')
				{
					++Index;
					SkipWs();
					if (Index == Text.size())
					{
						return true;
					}
					if (OutError)
					{
						*OutError = "localization JSON: unexpected trailing content";
					}
					return false;
				}
				if (OutError)
				{
					*OutError = "localization JSON: expected ',' or '}'";
				}
				return false;
			}
			if (OutError)
			{
				*OutError = "localization JSON: unexpected end";
			}
			return false;
		}

		bool LoadJsonFile(
			const std::string& Path,
			std::unordered_map<std::string, std::string>& OutMap,
			std::string* OutError)
		{
			std::ifstream File(PathUtil::FromUtf8(Path), std::ios::binary);
			if (!File)
			{
				if (OutError)
				{
					*OutError = "cannot open localization file: " + Path;
				}
				return false;
			}
			std::ostringstream Buffer;
			Buffer << File.rdbuf();
			if (File.bad())
			{
				if (OutError)
				{
					*OutError = "cannot read localization file: " + Path;
				}
				return false;
			}
			return ParseFlatJsonStringMap(Buffer.str(), OutMap, OutError);
		}

		std::string ApplyArgs(
			std::string_view Template,
			const std::vector<std::string_view>& Args)
		{
			std::string Result;
			Result.reserve(Template.size() + 16);
			for (std::size_t Index = 0; Index < Template.size(); ++Index)
			{
				if (Template[Index] == '{' && Index + 2 < Template.size())
				{
					const char Digit = Template[Index + 1];
					if (Digit >= '0' && Digit <= '9' && Template[Index + 2] == '}')
					{
						const std::size_t ArgIndex = static_cast<std::size_t>(Digit - '0');
						if (ArgIndex < Args.size())
						{
							Result.append(Args[ArgIndex]);
						}
						Index += 2;
						continue;
					}
				}
				Result.push_back(Template[Index]);
			}
			return Result;
		}

		const std::unordered_map<std::string, std::string>& ActiveCatalog()
		{
			const auto It = Catalogs.find(ActiveLanguageTag);
			return It != Catalogs.end() ? It->second : CatalogEn;
		}

		std::vector<std::string_view> ToArgViews(const std::vector<std::string>& Args)
		{
			std::vector<std::string_view> Views;
			Views.reserve(Args.size());
			for (const std::string& Arg : Args)
			{
				Views.emplace_back(Arg);
			}
			return Views;
		}

		bool TryMatchEnglishTemplate(
			const std::string_view Template,
			const std::string_view Message,
			std::vector<std::string>& OutArgs)
		{
			OutArgs.clear();
			struct FSegment
			{
				bool bLiteral = true;
				std::string Text;
				std::size_t ArgIndex = 0;
			};
			std::vector<FSegment> Segments;
			std::size_t MaxArg = 0;
			bool bSawPlaceholder = false;
			for (std::size_t Index = 0; Index < Template.size();)
			{
				if (Template[Index] == '{' && Index + 2 < Template.size())
				{
					const char Digit = Template[Index + 1];
					if (Digit >= '0' && Digit <= '9' && Template[Index + 2] == '}')
					{
						bSawPlaceholder = true;
						FSegment Seg;
						Seg.bLiteral = false;
						Seg.ArgIndex = static_cast<std::size_t>(Digit - '0');
						MaxArg = (std::max)(MaxArg, Seg.ArgIndex + 1);
						Segments.push_back(std::move(Seg));
						Index += 3;
						continue;
					}
				}
				const std::size_t Start = Index;
				while (Index < Template.size())
				{
					if (Template[Index] == '{' && Index + 2 < Template.size())
					{
						const char Digit = Template[Index + 1];
						if (Digit >= '0' && Digit <= '9' && Template[Index + 2] == '}')
						{
							break;
						}
					}
					++Index;
				}
				FSegment Seg;
				Seg.bLiteral = true;
				Seg.Text.assign(Template.data() + Start, Index - Start);
				Segments.push_back(std::move(Seg));
			}

			if (!bSawPlaceholder)
			{
				// Complete catalog strings match only exactly. Prefix matching here would
				// wrongly claim "division by zero in ..." as diag.division_by_zero.
				return Message == Template;
			}

			std::size_t LiteralChars = 0;
			for (const FSegment& Seg : Segments)
			{
				if (Seg.bLiteral)
				{
					LiteralChars += Seg.Text.size();
				}
			}
			// Reject pure-placeholder templates like "{0}" / "{0}{1}" — they match anything.
			if (LiteralChars == 0)
			{
				return false;
			}

			OutArgs.assign(MaxArg, std::string{});
			std::size_t Pos = 0;
			for (std::size_t SegIndex = 0; SegIndex < Segments.size(); ++SegIndex)
			{
				const FSegment& Seg = Segments[SegIndex];
				if (Seg.bLiteral)
				{
					if (Seg.Text.empty())
					{
						continue;
					}
					if (Message.size() < Pos + Seg.Text.size()
						|| Message.compare(Pos, Seg.Text.size(), Seg.Text) != 0)
					{
						return false;
					}
					Pos += Seg.Text.size();
					continue;
				}

				std::string_view NextLiteral;
				for (std::size_t Look = SegIndex + 1; Look < Segments.size(); ++Look)
				{
					if (Segments[Look].bLiteral && !Segments[Look].Text.empty())
					{
						NextLiteral = Segments[Look].Text;
						break;
					}
				}
				if (NextLiteral.empty())
				{
					if (Seg.ArgIndex >= OutArgs.size())
					{
						return false;
					}
					OutArgs[Seg.ArgIndex].assign(Message.substr(Pos));
					Pos = Message.size();
					continue;
				}
				const auto Found = Message.find(NextLiteral, Pos);
				if (Found == std::string_view::npos)
				{
					return false;
				}
				if (Seg.ArgIndex >= OutArgs.size())
				{
					return false;
				}
				OutArgs[Seg.ArgIndex].assign(Message.substr(Pos, Found - Pos));
				Pos = Found;
			}
			return Pos == Message.size();
		}

		bool TryResolveEnglishMessage(
			const std::string_view EnglishMessage,
			std::string& OutMessageId,
			std::vector<std::string>& OutArgs)
		{
			OutMessageId.clear();
			OutArgs.clear();
			if (EnglishMessage.empty())
			{
				return false;
			}

			RebuildEnglishIndex();
			const auto Exact = ExactEnglishToId.find(std::string(EnglishMessage));
			if (Exact != ExactEnglishToId.end())
			{
				OutMessageId = Exact->second;
				return true;
			}

			std::string BestId;
			std::vector<std::string> BestArgs;
			std::size_t BestScore = 0;
			for (const auto& Entry : CatalogEnOrdered)
			{
				std::vector<std::string> CandidateArgs;
				if (!TryMatchEnglishTemplate(Entry.second, EnglishMessage, CandidateArgs))
				{
					continue;
				}
				// Prefer more literal specificity, then more captured args, then length.
				const std::size_t Score =
					CountLiteralChars(Entry.second) * 10000
						+ CandidateArgs.size() * 100
						+ Entry.second.size();
				if (Score > BestScore)
				{
					BestScore = Score;
					BestId = Entry.first;
					BestArgs = std::move(CandidateArgs);
				}
			}
			if (BestId.empty())
			{
				return false;
			}
			OutMessageId = std::move(BestId);
			OutArgs = std::move(BestArgs);
			return true;
		}

		std::string FormatResolvedDiagnostic(
			const std::string_view MessageId,
			const std::vector<std::string>& MessageArgs)
		{
			const auto& Catalog = ActiveCatalog();
			const auto IdIt = Catalog.find(std::string(MessageId));
			const std::string* Template = nullptr;
			if (IdIt != Catalog.end())
			{
				Template = &IdIt->second;
			}
			else
			{
				const auto EnIt = CatalogEn.find(std::string(MessageId));
				if (EnIt != CatalogEn.end())
				{
					Template = &EnIt->second;
				}
			}
			if (Template == nullptr)
			{
				return ApplyArgs(MessageId, ToArgViews(MessageArgs));
			}
			std::string Result = ApplyArgs(*Template, ToArgViews(MessageArgs));
			// Legacy catalog entries with no `{n}` at all: append captured suffix.
			if (!MessageArgs.empty() && !TemplateHasPlaceholder(*Template))
			{
				Result.append(MessageArgs.front());
			}
			return Result;
		}

		std::string TranslatePrefixed(const std::string_view EnglishMessage)
		{
			std::string MessageId;
			std::vector<std::string> Args;
			if (!TryResolveEnglishMessage(EnglishMessage, MessageId, Args))
			{
				return std::string(EnglishMessage);
			}
			return FormatResolvedDiagnostic(MessageId, Args);
		}

		bool LoadCatalogTree(
			const std::string& Directory,
			FCatalogs& OutCatalogs,
			std::string* OutError)
		{
			namespace fs = std::filesystem;
			OutCatalogs.clear();

			// Public localization paths follow the same UTF-8 contract as module paths.
			const fs::path Root = PathUtil::FromUtf8(Directory);
			std::error_code Ec;
			if (!fs::is_directory(Root, Ec) || Ec)
			{
				if (OutError)
				{
					*OutError = "localization catalog directory not found: " + Directory;
				}
				return false;
			}

			const auto MergeUnique = [&](std::unordered_map<std::string, std::string>& Destination,
				const std::unordered_map<std::string, std::string>& Source,
				const fs::path& SourcePath) -> bool
			{
				for (const auto& Entry : Source)
				{
					if (!Destination.emplace(Entry).second)
					{
						if (OutError)
						{
							*OutError = "duplicate localization key '" + Entry.first
							+ "' in catalog: " + PathUtil::ToUtf8(SourcePath);
						}
						return false;
					}
				}
				return true;
			};

			const fs::path EnRoot = Root / std::string(FallbackLanguageTag);
			if (!fs::is_directory(EnRoot, Ec) || Ec)
			{
				if (OutError)
				{
					*OutError = "localization catalog is missing en-US directory: "
						+ Directory;
				}
				return false;
			}

			const auto EnumerateJsonFiles = [&](
				const fs::path& CultureRoot,
				std::set<fs::path>& OutRelativePaths) -> bool
			{
				OutRelativePaths.clear();
				Ec.clear();
				const bool bExists = fs::exists(CultureRoot, Ec);
				if (Ec)
				{
					if (OutError)
					{
						*OutError = "cannot inspect localization culture path: "
							+ PathUtil::ToUtf8(CultureRoot);
					}
					return false;
				}
				if (!bExists)
				{
					return true;
				}
				if (!fs::is_directory(CultureRoot, Ec) || Ec)
				{
					if (OutError)
					{
						*OutError = "localization culture path is not a directory: "
							+ PathUtil::ToUtf8(CultureRoot);
					}
					return false;
				}
				fs::recursive_directory_iterator It(
					CultureRoot,
					fs::directory_options::skip_permission_denied,
					Ec);
				const fs::recursive_directory_iterator End;
				for (; It != End && !Ec; It.increment(Ec))
				{
					if (It->is_regular_file(Ec) && !Ec
						&& It->path().extension() == ".json")
					{
						OutRelativePaths.insert(It->path().lexically_relative(CultureRoot));
					}
				}
				if (!Ec)
				{
					return true;
				}
				if (OutError)
				{
					*OutError = "cannot enumerate localization catalogs: "
						+ PathUtil::ToUtf8(CultureRoot);
				}
				return false;
			};

			std::vector<std::pair<std::string, fs::path>> CultureRoots;
			fs::directory_iterator CultureIt(
				Root,
				fs::directory_options::skip_permission_denied,
				Ec);
			const fs::directory_iterator CultureEnd;
			for (; CultureIt != CultureEnd && !Ec; CultureIt.increment(Ec))
			{
				if (!CultureIt->is_directory(Ec) || Ec)
				{
					const std::string EntryName = PathUtil::ToUtf8(
						CultureIt->path().filename());
					std::string CanonicalEntry;
					if (!Ec && CanonicalizeLanguageTag(EntryName, CanonicalEntry)
						&& CanonicalEntry == EntryName)
					{
						if (OutError)
						{
							*OutError = "localization culture path is not a directory: "
								+ PathUtil::ToUtf8(CultureIt->path());
						}
						return false;
					}
					Ec.clear();
					continue;
				}
				const std::string DirectoryName = PathUtil::ToUtf8(
					CultureIt->path().filename());
				std::string Canonical;
				if (!CanonicalizeLanguageTag(DirectoryName, Canonical)
					|| Canonical != DirectoryName
					|| Canonical.find('-') == std::string::npos)
				{
					if (OutError)
					{
						*OutError = "localization culture directory must use a qualified canonical BCP 47 tag: "
							+ DirectoryName;
					}
					return false;
				}
				if (std::any_of(CultureRoots.begin(), CultureRoots.end(), [&](const auto& Entry)
				{
					return Entry.first == Canonical;
				}))
				{
					if (OutError)
					{
						*OutError = "duplicate localization culture directory: " + Canonical;
					}
					return false;
				}
				CultureRoots.emplace_back(std::move(Canonical), CultureIt->path());
			}
			if (Ec)
			{
				if (OutError)
				{
					*OutError = "cannot enumerate localization culture directories: " + Directory;
				}
				return false;
			}
			std::sort(CultureRoots.begin(), CultureRoots.end(), [](const auto& Left, const auto& Right)
			{
				return Left.first < Right.first;
			});

			std::set<fs::path> EnFiles;
			if (!EnumerateJsonFiles(EnRoot, EnFiles))
			{
				return false;
			}
			if (EnFiles.empty())
			{
				if (OutError)
				{
					*OutError = "no en-US localization catalogs found under: " + Directory;
				}
				return false;
			}
			std::unordered_map<std::string, FCatalog> EnglishByFile;
			for (const fs::path& RelativePath : EnFiles)
			{
				const fs::path EnPath = EnRoot / RelativePath;
				FCatalog En;
				std::string Error;
				if (!LoadJsonFile(PathUtil::ToUtf8(EnPath), En, &Error))
				{
					if (OutError)
					{
						*OutError = Error;
					}
					return false;
				}
				EnglishByFile.emplace(PathUtil::ToUtf8(RelativePath), En);
				if (!MergeUnique(OutCatalogs[std::string(FallbackLanguageTag)], En, EnPath))
				{
					return false;
				}
			}

			for (const auto& Culture : CultureRoots)
			{
				if (Culture.first == FallbackLanguageTag)
				{
					continue;
				}
				std::set<fs::path> Files;
				if (!EnumerateJsonFiles(Culture.second, Files))
				{
					return false;
				}
				if (Files.empty())
				{
					if (OutError)
					{
						*OutError = "localization culture directory contains no JSON files: "
							+ PathUtil::ToUtf8(Culture.second);
					}
					return false;
				}
				FCatalog& Destination = OutCatalogs[Culture.first];
				for (const fs::path& RelativePath : Files)
				{
					const std::string Relative = PathUtil::ToUtf8(RelativePath);
					const auto EnglishFile = EnglishByFile.find(Relative);
					if (EnglishFile == EnglishByFile.end())
					{
						if (OutError)
						{
							*OutError = Culture.first
								+ " localization file has no en-US counterpart: " + Relative;
						}
						return false;
					}
					FCatalog Translation;
					std::string Error;
					const fs::path TranslationPath = Culture.second / RelativePath;
					if (!LoadJsonFile(PathUtil::ToUtf8(TranslationPath), Translation, &Error))
					{
						if (OutError)
						{
							*OutError = Error;
						}
						return false;
					}
					for (const auto& Entry : Translation)
					{
						if (EnglishFile->second.find(Entry.first) == EnglishFile->second.end())
						{
							if (OutError)
							{
								*OutError = "localization key '" + Entry.first
									+ "' has no English entry in: "
									+ PathUtil::ToUtf8(TranslationPath);
							}
							return false;
						}
					}
					if (!MergeUnique(Destination, Translation, TranslationPath))
					{
						return false;
					}
				}
			}
			return true;
		}

		bool TryLoadCatalogDirectory(const std::string& Directory)
		{
			FCatalogs LoadedCatalogs;
			if (!LoadCatalogTree(Directory, LoadedCatalogs, nullptr))
			{
				return false;
			}
			Catalogs = std::move(LoadedCatalogs);
			CatalogEn = Catalogs.at(std::string(FallbackLanguageTag));
			std::string Resolved;
			if (!ResolveLanguageTag(ActiveLanguageTag, Resolved))
			{
				Resolved = FallbackLanguageTag;
			}
			ActiveLanguageTag = std::move(Resolved);
			bEnglishIndexDirty = true;
			bCatalogsLoaded = true;
			ConfigureLanguageIfNeeded();
			return true;
		}

		bool TryLoadSourceCatalogs(const std::string& Directory)
		{
			const auto Root = PathUtil::FindSourceModuleRoot(Directory);
			if (Root.empty())
			{
				return false;
			}
			if (!TryLoadCatalogDirectory(PathUtil::ToUtf8(
					Root / "VeroScript/Resources/Localization")))
			{
				return false;
			}
			SourceModuleRoot = PathUtil::ToUtf8(Root);
			LoadedSourceModules.clear();
			return true;
		}
	}

	bool TrySetLanguageFromTag(const std::string_view Tag)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		std::string LanguageTag;
		if (!ResolveLanguageTag(Tag, LanguageTag))
		{
			return false;
		}
		ActiveLanguageTag = std::move(LanguageTag);
		bConfigured = true;
		return true;
	}

	std::string GetLanguageTag()
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		return ActiveLanguageTag;
	}

	std::vector<std::string> GetAvailableLanguageTags()
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		std::vector<std::string> Result;
		Result.reserve(Catalogs.size());
		for (const auto& Catalog : Catalogs)
		{
			Result.push_back(Catalog.first);
		}
		std::sort(Result.begin(), Result.end());
		return Result;
	}

	bool LoadCatalogDirectory(const std::string_view Directory, std::string* OutError)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		FCatalogs LoadedCatalogs;
		std::string Error;
		const std::string Dir(Directory);
		if (!LoadCatalogTree(Dir, LoadedCatalogs, &Error))
		{
			if (OutError)
			{
				*OutError = Error;
			}
			return false;
		}
		Catalogs = std::move(LoadedCatalogs);
		SourceModuleRoot.clear();
		LoadedSourceModules.clear();
		CatalogEn = Catalogs.at(std::string(FallbackLanguageTag));
		std::string Resolved;
		if (!ResolveLanguageTag(ActiveLanguageTag, Resolved))
		{
			Resolved = FallbackLanguageTag;
		}
		ActiveLanguageTag = std::move(Resolved);
		bEnglishIndexDirty = true;
		bCatalogsLoaded = true;
		ConfigureLanguageIfNeeded();
		return true;
	}

	bool MergeCatalogDirectory(
		const std::string_view Directory,
		const bool bOverride,
		std::string* OutError)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		FCatalogs LoadedCatalogs;
		std::string Error;
		const std::string Dir(Directory);
		if (!LoadCatalogTree(Dir, LoadedCatalogs, &Error))
		{
			if (OutError)
			{
				*OutError = Error;
			}
			return false;
		}
		// Merge on top of the base catalog so module keys augment (not replace) it.
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		const auto MergeInto = [bOverride](
			std::unordered_map<std::string, std::string>& Dst,
			const std::unordered_map<std::string, std::string>& Src)
		{
			for (const auto& Entry : Src)
			{
				if (bOverride)
				{
					Dst[Entry.first] = Entry.second;
				}
				else
				{
					Dst.emplace(Entry.first, Entry.second);
				}
			}
		};
		for (const auto& Loaded : LoadedCatalogs)
		{
			MergeInto(Catalogs[Loaded.first], Loaded.second);
		}
		CatalogEn = Catalogs.at(std::string(FallbackLanguageTag));
		bEnglishIndexDirty = true;
		bCatalogsLoaded = true;
		return true;
	}

	void InitializeFromEnvironment()
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (GetHostPlatformInfo().bMobile)
		{
			// Mobile hosts may explicitly load catalogs and select a language.
			// Do not walk desktop directories or process environment variables.
			bCatalogsLoaded = true;
			ConfigureLanguageIfNeeded();
			return;
		}
		if (bCatalogsLoaded)
		{
			ConfigureLanguageIfNeeded();
			return;
		}

		std::string LocalizationDirectory;
		if (const auto DirEnv = Environment::GetUtf8("VERO_LOCALIZATION_DIR"))
		{
			LocalizationDirectory = *DirEnv;
		}
		if (!LocalizationDirectory.empty()
			&& TryLoadCatalogDirectory(LocalizationDirectory))
		{
			return;
		}

		const std::string ExecutableDirectory = Runtime::GetExecutableDirectory();
		if (!ExecutableDirectory.empty()
			&& (TryLoadCatalogDirectory(ExecutableDirectory + "/localization")
				|| TryLoadCatalogDirectory(ExecutableDirectory + "/../assets/localization")))
		{
			return;
		}
		// Walk up from the executable (build/bin/Debug → repo) looking for catalogs.
		if (!ExecutableDirectory.empty())
		{
			std::string Walk = ExecutableDirectory;
			for (int Depth = 0; Depth < 8; ++Depth)
			{
				const std::string::size_type Slash = Walk.find_last_of("/\\");
				if (Slash == std::string::npos || Slash == 0)
				{
					break;
				}
				Walk.resize(Slash);
				if (TryLoadCatalogDirectory(Walk + "/localization")
					|| TryLoadSourceCatalogs(Walk))
				{
					return;
				}
			}
		}
		// Cwd convenience for source checkouts and deployed assets.
		if (TryLoadCatalogDirectory("localization"))
		{
			return;
		}
		// A source checkout can load the core's own catalogs without deployed assets.
		if (TryLoadSourceCatalogs(".")
			|| TryLoadSourceCatalogs("..")
			|| TryLoadSourceCatalogs("../.."))
		{
			// Continue below to resolve the requested language.
		}

		ConfigureLanguageIfNeeded();
	}

	void InitializeModuleFromEnvironment(const std::string_view ModulePath)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		InitializeFromEnvironment();
		if (SourceModuleRoot.empty() || ModulePath.empty())
		{
			return;
		}
		const std::string Module(ModulePath);
		if (LoadedSourceModules.find(Module) != LoadedSourceModules.end())
		{
			return;
		}
		namespace fs = std::filesystem;
		const fs::path Relative = fs::u8path(Module);
		if (Relative.has_root_path())
		{
			return;
		}
		for (const auto& Part : Relative)
		{
			if (Part == "..")
			{
				return;
			}
		}
		const fs::path Catalog = fs::u8path(SourceModuleRoot)
			/ Relative / "Resources/Localization";
		if (MergeCatalogDirectory(PathUtil::ToUtf8(Catalog), false))
		{
			LoadedSourceModules.insert(Module);
		}
	}

	std::string TranslateId(
		const std::string_view MessageId,
		const std::vector<std::string_view>& Args)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		const auto& Catalog = ActiveCatalog();
		const auto It = Catalog.find(std::string(MessageId));
		if (It == Catalog.end())
		{
			const auto EnIt = CatalogEn.find(std::string(MessageId));
			if (EnIt != CatalogEn.end())
			{
				return ApplyArgs(EnIt->second, Args);
			}
			return ApplyArgs(MessageId, Args);
		}
		return ApplyArgs(It->second, Args);
	}

	std::string FormatEnglish(
		const std::string_view MessageId,
		const std::vector<std::string_view>& Args)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		const auto EnIt = CatalogEn.find(std::string(MessageId));
		if (EnIt == CatalogEn.end())
		{
			return ApplyArgs(MessageId, Args);
		}
		return ApplyArgs(EnIt->second, Args);
	}

	std::string FormatDiagnosticMessage(
		const std::string_view MessageId,
		const std::vector<std::string>& MessageArgs,
		const std::string_view EnglishFallback)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		if (!MessageId.empty())
		{
			return FormatResolvedDiagnostic(MessageId, MessageArgs);
		}
		return Translate(EnglishFallback);
	}

	bool TryResolveEnglish(
		const std::string_view EnglishMessage,
		std::string& OutMessageId,
		std::vector<std::string>& OutArgs)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		return TryResolveEnglishMessage(EnglishMessage, OutMessageId, OutArgs);
	}

	std::string Translate(const std::string_view EnglishMessage)
	{
		const std::lock_guard<std::recursive_mutex> Lock(LocaleMutex);
		if (!bCatalogsLoaded)
		{
			InitializeFromEnvironment();
		}
		if (ActiveLanguageTag == FallbackLanguageTag || EnglishMessage.empty())
		{
			return std::string(EnglishMessage);
		}
		if (!bCatalogsLoaded)
		{
			return std::string(EnglishMessage);
		}
		RebuildEnglishIndex();
		const auto Exact = ExactEnglishToId.find(std::string(EnglishMessage));
		if (Exact != ExactEnglishToId.end())
		{
			return TranslateId(Exact->second);
		}
		return TranslatePrefixed(EnglishMessage);
	}
}
