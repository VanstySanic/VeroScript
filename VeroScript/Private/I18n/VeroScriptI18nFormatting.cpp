// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptI18nInternal.h"
#include "VeroScriptFloatFormatting.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace VeroScript::I18n
{
	namespace
	{
		bool PlaceholderName(std::string_view S)
		{
			const auto Start = [](char C)
			{
				return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || C == '_';
			};
			if (S.empty() || !Start(S.front())) return false;
			for (char C : S)
			{
				if (!Start(C) && !(C >= '0' && C <= '9') && C != '.' && C != '-') return false;
			}

			return true;
		}
		bool Append(std::string& Out, std::string_view Text, FI18nWork Work, FI18nError& E)
		{
			if (Text.size() > Out.max_size() - Out.size()) return Detail::Fail(E, EI18nStatus::ResourceLimit, "output size overflow");
			if (!Detail::CheckSize(Out.size() + Text.size(), Work, E)) return false;
			for (std::size_t I = 0; I < Text.size();)
			{
				const auto N = std::min<std::size_t>(256, Text.size() - I);
				if (!Detail::CheckWork(Work, E, 1)) return false;
				Out.append(Text.data() + I, N);
				I += N;
			}
			return true;
		}
		bool ScalarText(const FI18nArgument& A, std::string& Out, FI18nError& E)
		{
			const auto& V = A.ScalarValue();
			char Buffer[128];
			if (A.Kind() == EI18nArgumentKind::Custom)
			{
				return Detail::Fail(
					E, EI18nStatus::ArgumentType, "custom argument requires an extension formatter");
			}
			if (const auto* P = std::get_if<std::string>(&V))
			{
				Out = *P;
				return true;
			}
			if (const auto* P = std::get_if<bool>(&V))
			{
				Out = *P ? "true" : "false";
				return true;
			}
			#define VS_I18N_PRINT(T) \
				if (const auto* P = std::get_if<T>(&V)) \
				{ \
					const auto R = std::to_chars(Buffer, Buffer + sizeof(Buffer), *P); \
					if (R.ec != std::errc{}) \
					{ \
						return Detail::Fail(E, EI18nStatus::NumericConversion, "numeric conversion failed"); \
					} \
					Out.assign(Buffer, R.ptr); \
					return true; \
				}
			VS_I18N_PRINT(std::int8_t)
			VS_I18N_PRINT(std::uint8_t)
			VS_I18N_PRINT(std::int16_t)
			VS_I18N_PRINT(std::uint16_t)
			VS_I18N_PRINT(std::int32_t)
			VS_I18N_PRINT(std::uint32_t)
			VS_I18N_PRINT(std::int64_t)
			VS_I18N_PRINT(std::uint64_t)
			#undef VS_I18N_PRINT
			#define VS_I18N_PRINT_FLOAT(T) \
				if (const auto* P = std::get_if<T>(&V)) \
				{ \
					if (!std::isfinite(*P)) return Detail::Fail(E, EI18nStatus::NumericConversion, "non-finite argument is not supported"); \
					const auto R = FloatFormattingDetail::ToCharsGeneral(Buffer, Buffer + sizeof(Buffer), *P); \
					if (R.ec != std::errc{}) return Detail::Fail(E, EI18nStatus::NumericConversion, "numeric conversion failed"); \
					Out.assign(Buffer, R.ptr); \
					return true; \
				}
			VS_I18N_PRINT_FLOAT(float)
			VS_I18N_PRINT_FLOAT(double)
			#undef VS_I18N_PRINT_FLOAT
			return Detail::Fail(E, EI18nStatus::ArgumentType, "invalid scalar argument");
		}
	}

	bool TryFormatNamed(std::string_view Pattern, const FI18nArguments& Args,
		std::string& Out, FI18nError& E, FI18nWork Work)
	{
		const bool Ok = Detail::Boundary(E,
			[&]()
			{
				const FI18nArguments StableArgs = Args;
				if (!Detail::CheckWork(Work, E)) return false;
				std::string Result;
				for (std::size_t I = 0; I < Pattern.size();)
				{
					if (!Detail::CheckWork(Work, E, 1)) return false;
					if (Pattern[I] != '{' && Pattern[I] != '}')
					{
						std::size_t End = I;
						while (End < Pattern.size() && End - I < 256 && Pattern[End] != '{' &&
							Pattern[End] != '}')
						{
							++End;
						}

						if (!Append(Result, Pattern.substr(I, End - I), Work, E)) return false;
						I = End;
						continue;
					}
					if (I + 1 < Pattern.size() && Pattern[I + 1] == Pattern[I])
					{
						if (!Append(Result, Pattern.substr(I, 1), Work, E)) return false;
						I += 2;
						continue;
					}
					if (Pattern[I] == '}')
					{
						return Detail::Fail(E, EI18nStatus::InvalidPattern, "unmatched closing brace");
					}
					std::size_t End = I + 1;
					while (End < Pattern.size() && Pattern[End] != '}')
					{
						if ((End - I) % 256 == 0 && !Detail::CheckWork(Work, E, 1)) return false;
						++End;
					}
					if (End == Pattern.size())
					{
						return Detail::Fail(E, EI18nStatus::InvalidPattern, "unclosed placeholder");
					}
					const auto Name = Pattern.substr(I + 1, End - I - 1);
					if (!PlaceholderName(Name))
					{
						return Detail::Fail(E, EI18nStatus::InvalidPattern, "invalid named placeholder");
					}
					const auto Entry = StableArgs.Values().find(Name);
					if (Entry == StableArgs.Values().end())
					{
						Detail::Fail(
							E, EI18nStatus::MissingArgument, "missing parameter: " + std::string(Name));
						Detail::Identify(E, {}, {}, {}, Name);
						return false;
					}
					const auto& Value = Entry->second;
					// Strings are appended directly in bounded chunks, without another full copy.
					if (Value.Kind() == EI18nArgumentKind::String)
					{
						if (!Append(Result, std::get<std::string>(Value.ScalarValue()), Work, E))
						{
							return false;
						}
					}
					else
					{
						std::string Rendered;
						if (!ScalarText(Value, Rendered, E))
						{
							Detail::Identify(E, {}, {}, {}, Name);
							return false;
						}
						if (!Append(Result, Rendered, Work, E)) return false;
					}
					I = End + 1;
				}
				if (!Detail::CheckWork(Work, E)) return false;
				Out = std::move(Result);
				return true;
			});
		if (!Ok)
		{
			Out.clear();
		}
		return Ok;
	}
}
