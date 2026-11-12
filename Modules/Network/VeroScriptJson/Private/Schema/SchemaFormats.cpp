// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Syntax validation only; no DNS, network or clock access.
#include "SchemaFormats.h"
#include "SchemaIdna.h"
#include "SchemaSemantics.h"
#include <array>

namespace VeroScript::Json::Detail::Formats
{
	static bool Digit(char C)
	{
		return C >= '0' && C <= '9';
	}
	static bool Alpha(char C)
	{
		return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
	}
	static bool Hex(char C)
	{
		return Digit(C) || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F');
	}
	static bool Has(std::string_view Set, char C)
	{
		return Set.find(C) != std::string_view::npos;
	}
	static int Pair(std::string_view S, std::size_t P)
	{
		return P + 2 <= S.size() && Digit(S[P]) && Digit(S[P + 1]) ? (S[P] - '0') * 10 + S[P + 1] - '0' : -1;
	}
	static bool Date(std::string_view S)
	{
		if (S.size() != 10 || S[4] != '-' || S[7] != '-' || !Digit(S[0]) || !Digit(S[1]) || !Digit(S[2]) ||
			!Digit(S[3]))
		{
			return false;
		}
		const auto Year = (S[0] - '0') * 1000 + (S[1] - '0') * 100 + (S[2] - '0') * 10 + S[3] - '0';
		const auto Month = Pair(S, 5), Day = Pair(S, 8);
		if (Month < 1 || Month > 12 || Day < 1)
		{
			return false;
		}
		constexpr int Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		const bool Leap = Year % 4 == 0 && (Year % 100 != 0 || Year % 400 == 0);
		return Day <= Days[Month - 1] + (Month == 2 && Leap ? 1 : 0);
	}
	static bool Time(std::string_view S)
	{
		if (S.size() < 9 || S[2] != ':' || S[5] != ':')
		{
			return false;
		}
		const auto H = Pair(S, 0), M = Pair(S, 3), Seconds = Pair(S, 6);
		if (H < 0 || H > 23 || M < 0 || M > 59 || Seconds < 0 || Seconds > 60)
		{
			return false;
		}
		std::size_t P = 8;
		if (P < S.size() && S[P] == '.')
		{
			const auto Start = ++P;
			while (P < S.size() && Digit(S[P]))
			{
				FSchemaSemantics::Check();
				++P;
			}
			if (P == Start)
			{
				return false;
			}
		}
		int Offset = 0;
		if (P + 1 == S.size() && (S[P] == 'Z' || S[P] == 'z'))
		{
		}
		else if (P + 6 == S.size() && (S[P] == '+' || S[P] == '-') && S[P + 3] == ':')
		{
			const auto Hours = Pair(S, P + 1), Minutes = Pair(S, P + 4);
			if (Hours < 0 || Hours > 23 || Minutes < 0 || Minutes > 59)
			{
				return false;
			}
			Offset = (Hours * 60 + Minutes) * (S[P] == '-' ? -1 : 1);
		}
		else
		{
			return false;
		}
		return Seconds < 60 || ((H * 60 + M - Offset) % 1440 + 1440) % 1440 == 1439;
	}
	static bool Duration(std::string_view S)
	{
		if (S.empty() || S.front() != 'P')
		{
			return false;
		}
		std::size_t P = 1;
		bool Any = false, TimePart = false, TimeAny = false;
		int Previous = -1;
		while (P < S.size())
		{
			FSchemaSemantics::Check();
			if (S[P] == 'T')
			{
				if (TimePart)
				{
					return false;
				}
				TimePart = true;
				Previous = 2;
				++P;
				continue;
			}
			const auto Start = P;
			while (P < S.size() && Digit(S[P]))
			{
				FSchemaSemantics::Check();
				++P;
			}
			if (P == Start || P == S.size())
			{
				return false;
			}
			const char Unit = S[P++];
			if (Unit == 'W')
			{
				return !Any && !TimePart && P == S.size();
			}
			int Order = -1;
			if (!TimePart)
			{
				Order = Unit == 'Y' ? 0 : Unit == 'M' ? 1 : Unit == 'D' ? 2 : -1;
			}
			else
			{
				Order = Unit == 'H' ? 3 : Unit == 'M' ? 4 : Unit == 'S' ? 5 : -1;
			}
			if (Order <= Previous || (((TimePart && TimeAny) || (!TimePart && Any)) && Order != Previous + 1))
			{
				return false;
			}
			Previous = Order;
			Any = true;
			TimeAny |= TimePart;
		}
		return Any && (!TimePart || TimeAny);
	}
	static bool Ipv4(std::string_view S)
	{
		std::size_t P = 0;
		for (unsigned Part = 0; Part < 4; ++Part)
		{
			FSchemaSemantics::Check();
			const auto Start = P;
			unsigned Value = 0;
			while (P < S.size() && Digit(S[P]))
			{
				if (P - Start >= 3)
				{
					return false;
				}
				Value = Value * 10 + S[P++] - '0';
			}
			if (P == Start || Value > 255 || (P - Start > 1 && S[Start] == '0'))
			{
				return false;
			}
			if (Part < 3)
			{
				if (P == S.size() || S[P++] != '.')
				{
					return false;
				}
			}
		}
		return P == S.size();
	}
	static bool Ipv6(std::string_view S)
	{
		if (S.empty() || S.size() > 45)
		{
			return false;
		}
		const auto Compression = S.find("::");
		if (Compression != std::string_view::npos && S.find("::", Compression + 2) != std::string_view::npos)
		{
			return false;
		}
		unsigned Groups = 0;
		auto Side = [&](std::string_view Part, bool Last)
		{
			if (Part.empty())
			{
				return true;
			}
			std::size_t P = 0;
			while (P < Part.size())
			{
				FSchemaSemantics::Check();
				const auto Colon = Part.find(':', P), End = Colon == std::string_view::npos ? Part.size() : Colon;
				const auto Token = Part.substr(P, End - P);
				if (Token.empty())
				{
					return false;
				}
				if (Token.find('.') != std::string_view::npos)
				{
					if (!Last || End != Part.size() || !Ipv4(Token))
					{
						return false;
					}
					Groups += 2;
				}
				else
				{
					if (Token.size() > 4 || !std::all_of(Token.begin(), Token.end(), Hex))
					{
						return false;
					}
					++Groups;
				}
				if (Groups > 8)
				{
					return false;
				}
				if (Colon == std::string_view::npos)
				{
					break;
				}
				P = Colon + 1;
				if (P == Part.size())
				{
					return false;
				}
			}
			return true;
		};
		if (Compression == std::string_view::npos)
		{
			return Side(S, true) && Groups == 8;
		}
		return Side(S.substr(0, Compression), false) && Side(S.substr(Compression + 2), true) && Groups < 8;
	}
	static bool Point(std::string_view S, std::size_t& P, std::uint32_t& C)
	{
		if (P == S.size())
		{
			return false;
		}
		const auto Lead = static_cast<unsigned char>(S[P++]);
		unsigned Count = 0;
		C = Lead;
		if (Lead < 128)
		{
			return true;
		}
		if (Lead >= 0xc2 && Lead <= 0xdf)
		{
			Count = 1;
			C = Lead & 31;
		}
		else if (Lead >= 0xe0 && Lead <= 0xef)
		{
			Count = 2;
			C = Lead & 15;
		}
		else if (Lead >= 0xf0 && Lead <= 0xf4)
		{
			Count = 3;
			C = Lead & 7;
		}
		else
		{
			return false;
		}
		if (Count > S.size() - P)
		{
			return false;
		}
		for (unsigned I = 0; I < Count; ++I)
		{
			const auto N = static_cast<unsigned char>(S[P++]);
			if ((N & 0xc0) != 0x80)
			{
				return false;
			}
			C = (C << 6) | (N & 63);
		}
		return !(C > 0x10ffff || (C >= 0xd800 && C <= 0xdfff) || (Count == 1 && C < 0x80) ||
				 (Count == 2 && C < 0x800) || (Count == 3 && C < 0x10000));
	}
	static bool Ucs(std::uint32_t C, bool Private)
	{
		if (Private &&
			((C >= 0xe000 && C <= 0xf8ff) || (C >= 0xf0000 && C <= 0xffffd) || (C >= 0x100000 && C <= 0x10fffd)))
		{
			return true;
		}
		return (C >= 0xa0 && C <= 0xd7ff) || (C >= 0xf900 && C <= 0xfdcf) || (C >= 0xfdf0 && C <= 0xffef) ||
			   (C >= 0x10000 && C <= 0xdfffd && (C & 0xffff) < 0xfffe) || (C >= 0xe1000 && C <= 0xefffd);
	}
	static constexpr std::string_view Unreserved = "-._~", SubDelims = "!$&'()*+,;=";
	static bool Chars(std::string_view S, std::string_view Extra, bool International, bool Private = false)
	{
		for (std::size_t P = 0; P < S.size();)
		{
			FSchemaSemantics::Check();
			const auto C = static_cast<unsigned char>(S[P]);
			if (C == '%')
			{
				if (P + 2 >= S.size() || !Hex(S[P + 1]) || !Hex(S[P + 2]))
				{
					return false;
				}
				P += 3;
			}
			else if (C >= 128)
			{
				std::uint32_t Cp;
				if (!International || !Point(S, P, Cp) || !Ucs(Cp, Private))
				{
					return false;
				}
			}
			else
			{
				++P;
				if (!Alpha(C) && !Digit(C) && !Has(Unreserved, C) && !Has(SubDelims, C) && !Has(Extra, C))
				{
					return false;
				}
			}
		}
		return true;
	}
	static bool IpLiteral(std::string_view S)
	{
		if (!S.empty() && (S.front() == 'v' || S.front() == 'V'))
		{
			const auto Dot = S.find('.');
			if (Dot == std::string_view::npos || Dot < 2 || Dot + 1 == S.size() ||
				!std::all_of(S.begin() + 1, S.begin() + Dot, Hex))
			{
				return false;
			}
			for (auto C : S.substr(Dot + 1))
			{
				if (!Alpha(C) && !Digit(C) && !Has(Unreserved, C) && !Has(SubDelims, C) && C != ':')
				{
					return false;
				}
			}
			return true;
		}
		return Ipv6(S);
	}
	static bool Authority(std::string_view S, bool International)
	{
		const auto At = S.rfind('@');
		if (At != std::string_view::npos)
		{
			if (!Chars(S.substr(0, At), ":", International))
			{
				return false;
			}
			S.remove_prefix(At + 1);
		}
		std::string_view Port;
		if (!S.empty() && S.front() == '[')
		{
			const auto End = S.find(']');
			if (End == std::string_view::npos || !IpLiteral(S.substr(1, End - 1)))
			{
				return false;
			}
			S.remove_prefix(End + 1);
			if (!S.empty())
			{
				if (S.front() != ':')
				{
					return false;
				}
				Port = S.substr(1);
			}
		}
		else
		{
			const auto Colon = S.find(':');
			if (Colon != std::string_view::npos)
			{
				Port = S.substr(Colon + 1);
				S = S.substr(0, Colon);
			}
			if (!Chars(S, "", International))
			{
				return false;
			}
		}
		return std::all_of(Port.begin(), Port.end(), Digit);
	}
	static bool Uri(std::string_view S, bool Reference, bool International)
	{
		const auto Hash = S.find('#');
		if (Hash != std::string_view::npos)
		{
			if (!Chars(S.substr(Hash + 1), ":@/?", International))
			{
				return false;
			}
			S = S.substr(0, Hash);
		}
		const auto Question = S.find('?');
		if (Question != std::string_view::npos)
		{
			if (!Chars(S.substr(Question + 1), ":@/?", International, true))
			{
				return false;
			}
			S = S.substr(0, Question);
		}
		const auto Colon = S.find(':'), Slash = S.find('/');
		bool Scheme = false;
		if (Colon != std::string_view::npos && (Slash == std::string_view::npos || Colon < Slash))
		{
			if (!Colon || !Alpha(S.front()))
			{
				return false;
			}
			for (auto C : S.substr(1, Colon - 1))
			{
				if (!Alpha(C) && !Digit(C) && !Has("+.-", C))
				{
					return false;
				}
			}
			Scheme = true;
			S.remove_prefix(Colon + 1);
		}
		if (!Scheme && !Reference)
		{
			return false;
		}
		if (S.substr(0, 2) == "//")
		{
			S.remove_prefix(2);
			const auto End = S.find('/');
			if (!Authority(S.substr(0, End), International))
			{
				return false;
			}
			S = End == std::string_view::npos ? std::string_view{} : S.substr(End);
		}
		return Chars(S, ":@/", International);
	}
	static bool Pointer(std::string_view S)
	{
		if (!S.empty() && S.front() != '/')
		{
			return false;
		}
		for (std::size_t P = 0; P < S.size(); ++P)
		{
			FSchemaSemantics::Check();
			if (S[P] == '~' && (++P == S.size() || (S[P] != '0' && S[P] != '1')))
			{
				return false;
			}
		}
		return true;
	}
	static bool RelativePointer(std::string_view S)
	{
		std::size_t P = 0;
		while (P < S.size() && Digit(S[P]))
		{
			FSchemaSemantics::Check();
			++P;
		}
		if (!P || (P > 1 && S.front() == '0'))
		{
			return false;
		}
		S.remove_prefix(P);
		return S == "#" || Pointer(S);
	}
	static bool Template(std::string_view S)
	{
		for (std::size_t P = 0; P < S.size();)
		{
			FSchemaSemantics::Check();
			if (S[P] == '{')
			{
				++P;
				if (P < S.size() && Has("+#./;?&", S[P]))
				{
					++P;
				}
				for (;;)
				{
					bool Segment = false;
					while (P < S.size())
					{
						FSchemaSemantics::Check();
						const char C = S[P];
						if (Alpha(C) || Digit(C) || C == '_')
						{
							Segment = true;
							++P;
						}
						else if (C == '%')
						{
							if (P + 2 >= S.size() || !Hex(S[P + 1]) || !Hex(S[P + 2]))
							{
								return false;
							}
							Segment = true;
							P += 3;
						}
						else if (C == '.')
						{
							if (!Segment)
							{
								return false;
							}
							Segment = false;
							++P;
						}
						else
						{
							break;
						}
					}
					if (!Segment || P == S.size())
					{
						return false;
					}
					if (S[P] == '*')
					{
						++P;
					}
					else if (S[P] == ':')
					{
						++P;
						const auto Start = P;
						if (P == S.size() || S[P] < '1' || S[P] > '9')
						{
							return false;
						}
						while (P < S.size() && Digit(S[P]))
						{
							FSchemaSemantics::Check();
							++P;
						}
						if (P - Start > 4)
						{
							return false;
						}
					}
					if (P == S.size())
					{
						return false;
					}
					if (S[P] == '}')
					{
						++P;
						break;
					}
					if (S[P++] != ',')
					{
						return false;
					}
				}
			}
			else if (S[P] == '}')
			{
				return false;
			}
			else if (S[P] == '%')
			{
				if (P + 2 >= S.size() || !Hex(S[P + 1]) || !Hex(S[P + 2]))
				{
					return false;
				}
				P += 3;
			}
			else if (static_cast<unsigned char>(S[P]) >= 128)
			{
				std::uint32_t C;
				if (!Point(S, P, C) || !Ucs(C, true))
				{
					return false;
				}
			}
			else
			{
				const auto C = static_cast<unsigned char>(S[P++]);
				if (C <= 32 || C == 127 || Has("\"<>\\^`|", C))
				{
					return false;
				}
			}
		}
		return true;
	}
	static bool Ldh(std::string_view S)
	{
		return !S.empty() && S.size() <= 63 && (Alpha(S.front()) || Digit(S.front())) &&
			   (Alpha(S.back()) || Digit(S.back())) &&
			   std::all_of(S.begin(),
				   S.end(),
				   [](char C)
				   {
					   return Alpha(C) || Digit(C) || C == '-';
				   });
	}
	static bool EmailDomain(std::string_view S, bool International)
	{
		if (S.empty() || S.size() > 253 || S.back() == '.')
		{
			return false;
		}
		if (S.front() == '[')
		{
			if (S.size() < 3 || S.back() != ']')
			{
				return false;
			}
			S = S.substr(1, S.size() - 2);
			if (Ipv4(S))
			{
				return true;
			}
			if (S.size() >= 5 && (S[0] == 'I' || S[0] == 'i') && (S[1] == 'P' || S[1] == 'p') &&
				(S[2] == 'V' || S[2] == 'v') && S[3] == '6' && S[4] == ':')
			{
				return Ipv6(S.substr(5));
			}
			const auto Colon = S.find(':');
			if (Colon == std::string_view::npos || !Ldh(S.substr(0, Colon)) || Colon + 1 == S.size())
			{
				return false;
			}
			for (unsigned char C : S.substr(Colon + 1))
			{
				if (C < 33 || C > 126 || C == '[' || C == ']' || C == '\\')
				{
					return false;
				}
			}
			return true;
		}
		if (International && std::any_of(S.begin(),
								 S.end(),
								 [](unsigned char C)
								 {
									 return C >= 128;
								 }))
		{
			return Hostname(S, true);
		}
		std::size_t P = 0;
		while (P < S.size())
		{
			const auto Dot = S.find('.', P), End = Dot == std::string_view::npos ? S.size() : Dot;
			const auto Label = S.substr(P, End - P);
			if (!Ldh(Label))
			{
				return false;
			}
			if (Dot == std::string_view::npos)
			{
				break;
			}
			P = End + 1;
		}
		return true;
	}
	static bool Email(std::string_view S, bool International)
	{
		if (S.empty() || S.size() > 254)
		{
			return false;
		}
		std::size_t P = 0;
		if (S.front() == '"')
		{
			++P;
			bool Closed = false;
			while (P < S.size())
			{
				FSchemaSemantics::Check();
				const auto C = static_cast<unsigned char>(S[P]);
				if (C == '"')
				{
					++P;
					Closed = true;
					break;
				}
				if (C == '\\')
				{
					if (++P == S.size() || static_cast<unsigned char>(S[P]) < 32 ||
						static_cast<unsigned char>(S[P]) > 126)
					{
						return false;
					}
					++P;
				}
				else if (C >= 128)
				{
					std::uint32_t Cp;
					if (!International || !Point(S, P, Cp))
					{
						return false;
					}
				}
				else
				{
					if (C < 32 || C > 126)
					{
						return false;
					}
					++P;
				}
			}
			if (!Closed)
			{
				return false;
			}
		}
		else
		{
			bool Atom = false;
			while (P < S.size() && S[P] != '@')
			{
				FSchemaSemantics::Check();
				const auto C = static_cast<unsigned char>(S[P]);
				if (C == '.')
				{
					if (!Atom)
					{
						return false;
					}
					Atom = false;
					++P;
				}
				else if (C >= 128)
				{
					std::uint32_t Cp;
					if (!International || !Point(S, P, Cp))
					{
						return false;
					}
					Atom = true;
				}
				else
				{
					if (!Alpha(C) && !Digit(C) && !Has("!#$%&'*+-/=?^_`{|}~", C))
					{
						return false;
					}
					Atom = true;
					++P;
				}
			}
			if (!Atom)
			{
				return false;
			}
		}
		return P <= 64 && P < S.size() && S[P] == '@' && EmailDomain(S.substr(P + 1), International);
	}
	bool Known(std::string_view Name)
	{
		constexpr std::string_view Names[] = {"date-time",
			"date",
			"time",
			"duration",
			"email",
			"idn-email",
			"hostname",
			"idn-hostname",
			"ipv4",
			"ipv6",
			"uri",
			"uri-reference",
			"iri",
			"iri-reference",
			"uuid",
			"uri-template",
			"json-pointer",
			"relative-json-pointer",
			"regex"};
		return std::find(std::begin(Names), std::end(Names), Name) != std::end(Names);
	}
	bool Validate(std::string_view Format, std::string_view Text)
	{
		FSchemaSemantics::Check(Text.size() + 1);
		if (Format == "date-time")
		{
			return Text.size() >= 20 && (Text[10] == 'T' || Text[10] == 't') && Date(Text.substr(0, 10)) &&
				   Time(Text.substr(11));
		}
		if (Format == "date")
		{
			return Date(Text);
		}
		if (Format == "time")
		{
			return Time(Text);
		}
		if (Format == "duration")
		{
			return Duration(Text);
		}
		if (Format == "email" || Format == "idn-email")
		{
			return Email(Text, Format == "idn-email");
		}
		if (Format == "hostname" || Format == "idn-hostname")
		{
			return Hostname(Text, Format == "idn-hostname");
		}
		if (Format == "ipv4")
		{
			return Ipv4(Text);
		}
		if (Format == "ipv6")
		{
			return Ipv6(Text);
		}
		if (Format == "uri")
		{
			return Uri(Text, false, false);
		}
		if (Format == "uri-reference")
		{
			return Uri(Text, true, false);
		}
		if (Format == "iri")
		{
			return Uri(Text, false, true);
		}
		if (Format == "iri-reference")
		{
			return Uri(Text, true, true);
		}
		if (Format == "json-pointer")
		{
			return Pointer(Text);
		}
		if (Format == "relative-json-pointer")
		{
			return RelativePointer(Text);
		}
		if (Format == "uri-template")
		{
			return Template(Text);
		}
		if (Format == "regex")
		{
			return FSchemaSemantics::RegexValid(Text);
		}
		if (Format == "uuid")
		{
			if (Text.size() != 36)
			{
				return false;
			}
			for (std::size_t I = 0; I < Text.size(); ++I)
			{
				if (I == 8 || I == 13 || I == 18 || I == 23)
				{
					if (Text[I] != '-')
					{
						return false;
					}
				}
				else if (!Hex(Text[I]))
				{
					return false;
				}
			}
			return true;
		}
		return false;
	}
} // namespace VeroScript::Json::Detail::Formats
