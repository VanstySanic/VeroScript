// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptI18nInternal.h"
#include <algorithm>
#include <set>

namespace VeroScript::I18n
{
	namespace
	{
		bool Alpha(char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z');
		}
		bool Digit(char C)
		{
			return C >= '0' && C <= '9';
		}
		char Lower(char C)
		{
			return C >= 'A' && C <= 'Z' ? char(C + 'a' - 'A') : C;
		}
		char Upper(char C)
		{
			return C >= 'a' && C <= 'z' ? char(C + 'A' - 'a') : C;
		}
		bool AllAlpha(const std::string& S)
		{
			return std::all_of(S.begin(), S.end(), Alpha);
		}
		bool AllDigits(const std::string& S)
		{
			return std::all_of(S.begin(), S.end(), Digit);
		}
		std::string Fold(std::string_view S)
		{
			std::string R(S);
			for (char& C : R)
			{
				C = Lower(C);
			}
			return R;
		}
	}

	bool NormalizeLanguageTag(std::string_view Input, std::string& Out, FI18nError& E)
	{
		const bool Ok = Detail::Boundary(E,
			[&]()
			{
				const auto Invalid = [&]()
				{
					return Detail::Fail(
						E, EI18nStatus::InvalidConfiguration, "invalid language tag: " + std::string(Input));
				};
				if (Input.empty()) return Invalid();
				static constexpr const char* Grandfathered[] = {"en-GB-oed", "i-ami", "i-bnn", "i-default",
					"i-enochian", "i-hak", "i-klingon", "i-lux", "i-mingo", "i-navajo", "i-pwn", "i-tao",
					"i-tay", "i-tsu", "sgn-BE-FR", "sgn-BE-NL", "sgn-CH-DE", "art-lojban", "cel-gaulish",
					"no-bok", "no-nyn", "zh-guoyu", "zh-hakka", "zh-min", "zh-min-nan", "zh-xiang"};
				const auto Folded = Fold(Input);
				for (auto Tag : Grandfathered)
				{
					if (Fold(Tag) == Folded)
					{
						Out = Tag;
						return true;
					}
				}

				std::vector<std::string> Parts;
				for (std::size_t Start = 0; Start <= Input.size();)
				{
					const auto End = Input.find('-', Start);
					const auto S =
						Input.substr(Start, (End == std::string_view::npos ? Input.size() : End) - Start);
					if (S.empty() || S.size() > 8 ||
						!std::all_of(S.begin(), S.end(),
							[](char C)
							{
								return Alpha(C) || Digit(C);
							}))
					{
						return Invalid();
					}
					Parts.push_back(Fold(S));
					if (End == std::string_view::npos)
					{
						break;
					}

					Start = End + 1;
				}
				std::vector<std::string> Base, Private;
				std::map<char, std::vector<std::string>> Extensions;
				std::size_t I = 0;
				if (Parts[0] != "x")
				{
					if (Parts[0].size() < 2 || !AllAlpha(Parts[0])) return Invalid();
					Base.push_back(Parts[I++]);
					if (Base[0].size() <= 3)
					{
						unsigned Count = 0;
						while (I < Parts.size() && Count < 3 && Parts[I].size() == 3 && AllAlpha(Parts[I]))
						{
							Base.push_back(Parts[I++]);
							++Count;
						}
					}
					if (I < Parts.size() && Parts[I].size() == 4 && AllAlpha(Parts[I]))
					{
						auto Script = Parts[I++];
						Script[0] = Upper(Script[0]);
						Base.push_back(std::move(Script));
					}
					if (I < Parts.size() &&
						((Parts[I].size() == 2 && AllAlpha(Parts[I])) ||
							(Parts[I].size() == 3 && AllDigits(Parts[I]))))
					{
						auto Region = Parts[I++];
						for (char& C : Region)
						{
							C = Upper(C);
						}
						Base.push_back(std::move(Region));
					}
					std::set<std::string> Variants;
					while (I < Parts.size() &&
						(Parts[I].size() >= 5 || (Parts[I].size() == 4 && Digit(Parts[I][0]))))
					{
						if (!Variants.insert(Parts[I]).second) return Invalid();
						Base.push_back(Parts[I++]);
					}
					while (I < Parts.size() && Parts[I].size() == 1 && Parts[I] != "x")
					{
						const char Singleton = Parts[I++][0];
						if (Extensions.count(Singleton)) return Invalid();
						std::vector<std::string> Block;
						while (I < Parts.size() && Parts[I].size() >= 2)
						{
							Block.push_back(Parts[I++]);
						}

						if (Block.empty()) return Invalid();
						Extensions.emplace(Singleton, std::move(Block));
					}
				}
				if (I < Parts.size() && Parts[I] == "x")
				{
					++I;
					if (I == Parts.size()) return Invalid();
					while (I < Parts.size())
					{
						Private.push_back(Parts[I++]);
					}
				}
				if (I != Parts.size()) return Invalid();
				std::string Result;
				const auto Add = [&](std::string_view P)
				{
					if (!Result.empty())
					{
						Result += '-';
					}
					Result.append(P);
				};
				for (const auto& P : Base)
				{
					Add(P);
				}

				for (const auto& Block : Extensions)
				{
					const char C = Block.first;
					Add(std::string_view(&C, 1));
					for (const auto& P : Block.second)
					{
						Add(P);
					}
				}
				if (!Private.empty())
				{
					Add("x");
					for (const auto& P : Private)
					{
						Add(P);
					}
				}
				Out = std::move(Result);
				return true;
			});
		if (!Ok)
		{
			Out.clear();
		}
		return Ok;
	}

	bool NormalizeConfiguration(const FI18nConfiguration& In, FI18nConfiguration& Out, FI18nError& E)
	{
		return Detail::Boundary(E,
			[&]()
			{
				FI18nConfiguration Next;
				Next.FormatOptions = In.FormatOptions;
				if (!NormalizeLanguageTag(In.Language, Next.Language, E)) return false;
				if (!In.FormatLocale.empty() && !NormalizeLanguageTag(In.FormatLocale, Next.FormatLocale, E))
				{
					return false;
				}
				std::set<std::string> Seen;
				for (const auto& Language : In.FallbackLanguages)
				{
					std::string Tag;
					if (!NormalizeLanguageTag(Language, Tag, E)) return false;
					if (Seen.insert(Tag).second)
					{
						Next.FallbackLanguages.push_back(std::move(Tag));
					}
				}
				Out = std::move(Next);
				return true;
			});
	}
}
