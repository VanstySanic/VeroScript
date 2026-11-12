// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingJson.h"

#include "VeroScriptFloatParsing.h"
#include "VeroScriptFloatFormatting.h"
#include "VeroScriptJsonString.h"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		class FJsonParser
		{
		public:
			explicit FJsonParser(std::string Text)
			    : Text(std::move(Text))
			{
			}

			FJson Parse()
			{
				SkipWs();
				FJson Value = ParseValue();
				SkipWs();
				if (Index < Text.size())
				{
					throw std::runtime_error("trailing JSON content");
				}
				return Value;
			}

		private:
			void SkipWs()
			{
				while (Index < Text.size() && std::isspace(static_cast<unsigned char>(Text[Index])))
				{
					++Index;
				}
			}

			char Peek() const
			{
				return Index < Text.size() ? Text[Index] : '\0';
			}

			char Consume()
			{
				return Index < Text.size() ? Text[Index++] : '\0';
			}

			bool Match(const char Expected)
			{
				SkipWs();
				if (Peek() != Expected)
				{
					return false;
				}
				++Index;
				return true;
			}

			[[noreturn]] void Fail(const char* Message) const
			{
				throw std::runtime_error(Message);
			}

			FJson ParseValue()
			{
				if (Depth >= MaxJsonDepth)
				{
					Fail("JSON nested too deeply");
				}
				++Depth;
				struct FDepthGuard
				{
					~FDepthGuard()
					{
						--Value;
					}

					std::size_t& Value;

				} DepthGuard{Depth};
				SkipWs();
				const char C = Peek();
				if (C == '{')
				{
					return FJson::Object(ParseObject());
				}
				if (C == '[')
				{
					return FJson::Array(ParseArray());
				}
				if (C == '"')
				{
					return FJson::String(ParseString());
				}
				if (C == 't')
				{
					ExpectLiteral("true");
					return FJson::Bool(true);
				}
				if (C == 'f')
				{
					ExpectLiteral("false");
					return FJson::Bool(false);
				}
				if (C == 'n')
				{
					ExpectLiteral("null");
					return FJson::Null();
				}
				return FJson::Number(ParseNumber());
			}

			void ExpectLiteral(const char* Literal)
			{
				for (; *Literal; ++Literal)
				{
					if (Consume() != *Literal)
					{
						Fail("invalid literal");
					}
				}
			}

			FJson::FObject ParseObject()
			{
				FJson::FObject Object;
				if (!Match('{'))
				{
					Fail("expected '{'");
				}
				SkipWs();
				if (Match('}'))
				{
					return Object;
				}
				for (;;)
				{
					SkipWs();
					const std::string Key = ParseString();
					if (!Match(':'))
					{
						Fail("expected ':'");
					}
					if (!Object.emplace(Key, ParseValue()).second)
					{
						Fail("duplicate object key");
					}
					SkipWs();
					if (Match('}'))
					{
						break;
					}
					if (!Match(','))
					{
						Fail("expected ',' or '}'");
					}
				}
				return Object;
			}

			FJson::FArray ParseArray()
			{
				FJson::FArray Array;
				if (!Match('['))
				{
					Fail("expected '['");
				}
				SkipWs();
				if (Match(']'))
				{
					return Array;
				}
				for (;;)
				{
					Array.push_back(ParseValue());
					SkipWs();
					if (Match(']'))
					{
						break;
					}
					if (!Match(','))
					{
						Fail("expected ',' or ']'");
					}
				}
				return Array;
			}

			std::string ParseString()
			{
				if (Consume() != '"')
				{
					Fail("expected string");
				}
				std::string Out;
				auto AppendUtf8 = [&](const unsigned Code)
				{
					if (Code <= 0x7Fu)
					{
						Out.push_back(static_cast<char>(Code));
					}
					else if (Code <= 0x7FFu)
					{
						Out.push_back(static_cast<char>(0xC0u | ((Code >> 6) & 0x1Fu)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3Fu)));
					}
					else if (Code <= 0xFFFFu)
					{
						Out.push_back(static_cast<char>(0xE0u | ((Code >> 12) & 0x0Fu)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 6) & 0x3Fu)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3Fu)));
					}
					else
					{
						Out.push_back(static_cast<char>(0xF0u | ((Code >> 18) & 0x07u)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 12) & 0x3Fu)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 6) & 0x3Fu)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3Fu)));
					}
				};
				auto ParseUnicodeHex = [&]()
				{
					unsigned Code = 0;
					for (int I = 0; I < 4; ++I)
					{
						const char H = Consume();
						Code <<= 4;
						if (H >= '0' && H <= '9')
						{
							Code |= static_cast<unsigned>(H - '0');
						}
						else if (H >= 'a' && H <= 'f')
						{
							Code |= static_cast<unsigned>(H - 'a' + 10);
						}
						else if (H >= 'A' && H <= 'F')
						{
							Code |= static_cast<unsigned>(H - 'A' + 10);
						}
						else
						{
							Fail("invalid unicode escape");
						}
					}
					return Code;
				};
				while (Index < Text.size())
				{
					const char C = Consume();
					if (C == '"')
					{
						return Out;
					}
					if (C != '\\')
					{
						if (static_cast<unsigned char>(C) < 0x20u)
						{
							Fail("unescaped control character in string");
						}
						Out.push_back(C);
						continue;
					}
					const char Esc = Consume();
					switch (Esc)
					{
					case '"':
					case '\\':
					case '/':
						{
							Out.push_back(Esc);
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
							unsigned Code = ParseUnicodeHex();
							if (Code >= 0xD800u && Code <= 0xDBFFu)
							{
								if (Index >= Text.size() || Text[Index] != '\\')
								{
									Fail("incomplete UTF-16 surrogate pair");
								}
								Consume();
								if (Index >= Text.size() || Text[Index] != 'u')
								{
									Fail("incomplete UTF-16 surrogate pair");
								}
								Consume();
								const unsigned Low = ParseUnicodeHex();
								if (Low < 0xDC00u || Low > 0xDFFFu)
								{
									Fail("invalid UTF-16 low surrogate");
								}
								Code = 0x10000u + ((Code - 0xD800u) << 10) + (Low - 0xDC00u);
							}
							else if (Code >= 0xDC00u && Code <= 0xDFFFu)
							{
								Fail("lone UTF-16 low surrogate");
							}
							AppendUtf8(Code);
							break;
						}
					default:
						{
							Fail("invalid escape");
						}
					}
				}
				Fail("unterminated string");
			}

			double ParseNumber()
			{
				const std::size_t Start = Index;
				if (Peek() == '-')
				{
					++Index;
				}
				if (Peek() == '0')
				{
					++Index;
					if (std::isdigit(static_cast<unsigned char>(Peek())))
					{
						Fail("invalid leading zero in number");
					}
				}
				else if (Peek() >= '1' && Peek() <= '9')
				{
					do
					{
						++Index;
					}
					while (std::isdigit(static_cast<unsigned char>(Peek())));
				}
				else
				{
					Fail("invalid number");
				}
				if (Peek() == '.')
				{
					++Index;
					if (!std::isdigit(static_cast<unsigned char>(Peek())))
					{
						Fail("invalid number fraction");
					}
					while (std::isdigit(static_cast<unsigned char>(Peek())))
					{
						++Index;
					}
				}
				if (Peek() == 'e' || Peek() == 'E')
				{
					++Index;
					if (Peek() == '+' || Peek() == '-')
					{
						++Index;
					}
					if (!std::isdigit(static_cast<unsigned char>(Peek())))
					{
						Fail("invalid number exponent");
					}
					while (std::isdigit(static_cast<unsigned char>(Peek())))
					{
						++Index;
					}
				}
				double Value = 0.0;
				const auto* Begin = Text.data() + Start;
				const auto* End = Text.data() + Index;
				const auto [Ptr, Ec] = FloatParsingDetail::FromCharsDecimal(std::string_view(Begin, End - Begin), Value);
				if (Ec != std::errc{} || Ptr != End)
				{
					Fail("invalid number");
				}
				return Value;
			}

			static constexpr std::size_t MaxJsonDepth = 256;
			std::string Text;
			std::size_t Index = 0;
			std::size_t Depth = 0;
		};

		void AppendEscaped(std::string& Out, const std::string& Text)
		{
			Out.push_back('"');
			JsonStringDetail::AppendEscaped(Out, Text, JsonStringDetail::EControlEscapeStyle::Short);
			Out.push_back('"');
		}

		void AppendJson(std::string& Out, const FJson& Value)
		{
			if (std::holds_alternative<std::nullptr_t>(Value.Value))
			{
				Out += "null";
			}
			else if (const bool* Flag = std::get_if<bool>(&Value.Value))
			{
				Out += *Flag ? "true" : "false";
			}
			else if (const double* Number = std::get_if<double>(&Value.Value))
			{
				char Buf[64];
				const auto [Ptr, Ec] =
				    FloatFormattingDetail::ToCharsGeneral(Buf, Buf + sizeof(Buf), *Number);
				if (Ec != std::errc{})
				{
					Out += "0";
				}
				else
				{
					Out.append(Buf, Ptr);
				}
			}
			else if (const std::string* Text = std::get_if<std::string>(&Value.Value))
			{
				AppendEscaped(Out, *Text);
			}
			else if (const FJson::FObject* Object = std::get_if<FJson::FObject>(&Value.Value))
			{
				Out.push_back('{');
				bool bFirst = true;
				for (const auto& Pair : *Object)
				{
					if (!bFirst)
					{
						Out.push_back(',');
					}
					bFirst = false;
					AppendEscaped(Out, Pair.first);
					Out.push_back(':');
					AppendJson(Out, Pair.second);
				}
				Out.push_back('}');
			}
			else if (const FJson::FArray* Array = std::get_if<FJson::FArray>(&Value.Value))
			{
				Out.push_back('[');
				for (std::size_t Index = 0; Index < Array->size(); ++Index)
				{
					if (Index > 0)
					{
						Out.push_back(',');
					}
					AppendJson(Out, (*Array)[Index]);
				}
				Out.push_back(']');
			}
		}
	}

	FJson ParseJson(std::string Text)
	{
		return FJsonParser(std::move(Text)).Parse();
	}

	std::string ToJsonString(const FJson& Value)
	{
		std::string Out;
		AppendJson(Out, Value);
		return Out;
	}
}
