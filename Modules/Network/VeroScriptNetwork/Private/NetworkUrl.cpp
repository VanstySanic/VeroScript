// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetwork.h"
#include "NetworkUrlInternal.h"
#include <charconv>

namespace VeroScript::Network
{
	using UrlDetail::Hex;

	static bool ValidIpv6(std::string_view Host)
	{
		Host = Host.substr(0, Host.find('%'));
		if (Host.empty()) return false;
		std::size_t At = 0;
		unsigned Groups = 0;
		bool Compressed = Host.substr(0, 2) == "::";
		if (Compressed)
		{
			At = 2;
		}
		while (At < Host.size())
		{
			const auto End = Host.find(':', At);
			const auto Part = Host.substr(At, End == Host.npos ? Host.size() - At : End - At);
			if (Part.empty()) return false;
			if (Part.find('.') != Part.npos)
			{
				if (End != Host.npos) return false;
				unsigned Octets = 0;
				for (std::size_t Begin = 0; Begin < Part.size();)
				{
					const auto Dot = Part.find('.', Begin);
					const auto Number = Part.substr(Begin, Dot == Part.npos ? Part.size() - Begin : Dot - Begin);
					unsigned Value = 0;
					if (Number.empty() || Number.size() > 3 || (Number.size() > 1 && Number[0] == '0')) return false;
					for (const char C : Number)
					{
						if (C < '0' || C > '9') return false;
						Value = Value * 10 + static_cast<unsigned>(C - '0');
					}
					if (Value > 255 || ++Octets > 4) return false;
					if (Dot == Part.npos)
					{
						break;
					}
					Begin = Dot + 1;
					if (Begin == Part.size()) return false;
				}
				if (Octets != 4) return false;
				Groups += 2;
				break;
			}
			if (Part.size() > 4) return false;
			for (const char C : Part)
			{
				if (Hex(C) < 0) return false;
			}
			if (++Groups > 8) return false;
			if (End == Host.npos)
			{
				break;
			}
			At = End + 1;
			if (At == Host.size()) return false;
			if (Host[At] == ':')
			{
				if (Compressed) return false;
				Compressed = true;
				++At;
			}
		}
		return Compressed ? Groups < 8 : Groups == 8;
	}

	static std::string Lower(std::string_view Text)
	{
		std::string Out(Text);
		for (char& C : Out)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C = static_cast<char>(C - 'A' + 'a');
			}
		}
		return Out;
	}

	bool FUrl::TryParse(std::string_view Text, FUrl& Out, FError& Error)
	{
		Error = {};
		auto Invalid = [&]
		{
			Error = {EError::InvalidArgument, "Invalid absolute network URL", 0};
			return false;
		};
		if (!UrlDetail::ValidText(Text))
		{
			return Invalid();
		}
		auto Separator = Text.find("://");
		if (Separator == std::string_view::npos || Separator == 0)
		{
			return Invalid();
		}
		FUrl Value;
		Value.Scheme = Lower(Text.substr(0, Separator));
		for (std::size_t I = 0; I < Value.Scheme.size(); ++I)
		{
			char C = Value.Scheme[I];
			if (!((C >= 'a' && C <= 'z') || (I && ((C >= '0' && C <= '9') || C == '+' || C == '-' || C == '.'))))
			{
				return Invalid();
			}
		}
		Text.remove_prefix(Separator + 3);
		auto AuthorityEnd = Text.find_first_of("/?#");
		auto Authority = Text.substr(0, AuthorityEnd);
		if (Authority.empty() || Authority.find('@') != std::string_view::npos)
		{
			return Invalid();
		}
		Text.remove_prefix(Authority.size());
		std::string_view Port;
		if (Authority.front() == '[')
		{
			auto End = Authority.find(']');
			if (End == std::string_view::npos)
			{
				return Invalid();
			}
			Value.Host = Lower(Authority.substr(1, End - 1));
			if (!ValidIpv6(Value.Host))
			{
				return Invalid();
			}
			Value.bIpv6 = true;
			if (End + 1 < Authority.size())
			{
				if (Authority[End + 1] != ':')
				{
					return Invalid();
				}
				Value.bExplicitPort = true;
				Port = Authority.substr(End + 2);
			}
		}
		else
		{
			auto Colon = Authority.find(':');
			Value.Host = Lower(Authority.substr(0, Colon));
			if (Colon != std::string_view::npos)
			{
				Value.bExplicitPort = true;
				Port = Authority.substr(Colon + 1);
			}
			if (Value.Host.empty())
			{
				return Invalid();
			}
			for (unsigned char C : Value.Host)
			{
				if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '-' || C == '.' || C == '_'))
				{
					return Invalid();
				}
			}
		}
		if (Value.bExplicitPort)
		{
			unsigned Number = 0;
			auto Result = std::from_chars(Port.data(), Port.data() + Port.size(), Number);
			if (Port.empty() || Result.ec != std::errc{} || Result.ptr != Port.data() + Port.size() || Number == 0 ||
			    Number > 65535)
			{
				return Invalid();
			}
			Value.Port = static_cast<std::uint16_t>(Number);
		}
		else
		{
			Value.Port = Value.Scheme == "https" || Value.Scheme == "wss" ? 443
			    : Value.Scheme == "http" || Value.Scheme == "ws"          ? 80
			                                                              : 0;
		}
		auto Fragment = Text.find('#');
		if (Fragment != std::string_view::npos)
		{
			Value.bHasFragment = true;
			Value.Fragment = Text.substr(Fragment + 1);
			Text = Text.substr(0, Fragment);
		}
		auto Query = Text.find('?');
		if (Query != std::string_view::npos)
		{
			Value.bHasQuery = true;
			Value.Query = Text.substr(Query + 1);
			Text = Text.substr(0, Query);
		}
		Value.Path = Text.empty() ? "/" : std::string(Text);
		Out = std::move(Value);
		return true;
	}

	std::string FUrl::Authority() const
	{
		return (bIpv6 ? "[" + Host + "]" : Host) + (bExplicitPort ? ":" + std::to_string(Port) : "");
	}
	std::string FUrl::Target() const
	{
		return Path + (bHasQuery ? "?" + Query : "");
	}
	std::string FUrl::String(bool bIncludeFragment) const
	{
		return Scheme + "://" + Authority() + Target() + (bIncludeFragment && bHasFragment ? "#" + Fragment : "");
	}
	std::string PercentEncode(std::string_view Bytes, bool bForm)
	{
		std::string Out;
		constexpr char HexDigits[] = "0123456789ABCDEF";
		for (unsigned char C : Bytes)
		{
			if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_' ||
			    C == '.' || C == (bForm ? '*' : '~'))
			{
				Out += static_cast<char>(C);
			}
			else if (bForm && C == ' ')
			{
				Out += '+';
			}
			else
			{
				Out += '%';
				Out += HexDigits[C >> 4];
				Out += HexDigits[C & 15];
			}
		}
		return Out;
	}
	bool TryPercentDecode(std::string_view Text, std::string& Out, FError& Error, bool bForm)
	{
		Error = {};
		std::string Value;
		for (std::size_t I = 0; I < Text.size(); ++I)
		{
			char C = Text[I];
			if (C == '%')
			{
				if (Text.size() - I < 3 || Hex(Text[I + 1]) < 0 || Hex(Text[I + 2]) < 0)
				{
					Error = {EError::InvalidArgument, "Invalid percent escape", 0};
					return false;
				}
				Value += static_cast<char>((Hex(Text[I + 1]) << 4) | Hex(Text[I + 2]));
				I += 2;
			}
			else
			{
				Value += bForm && C == '+' ? ' ' : C;
			}
		}
		Out = std::move(Value);
		return true;
	}
	bool TryParseQuery(std::string_view Text, std::vector<FQueryParameter>& Out, FError& Error, bool bForm)
	{
		Error = {};
		std::vector<FQueryParameter> Parameters;
		while (!Text.empty())
		{
			auto End = Text.find('&');
			auto Part = Text.substr(0, End);
			auto Equals = Part.find('=');
			FQueryParameter Parameter;
			Parameter.bHasEquals = Equals != std::string_view::npos;
			if (!TryPercentDecode(Part.substr(0, Equals), Parameter.Name, Error, bForm) ||
			    (Parameter.bHasEquals && !TryPercentDecode(Part.substr(Equals + 1), Parameter.Value, Error, bForm)))
			{
				return false;
			}
			Parameters.push_back(std::move(Parameter));
			if (End == std::string_view::npos)
			{
				break;
			}
			Text.remove_prefix(End + 1);
			if (Text.empty())
			{
				Parameters.push_back({"", "", false});
			}
		}
		Out = std::move(Parameters);
		return true;
	}
	std::string BuildQuery(const std::vector<FQueryParameter>& Parameters, bool bForm)
	{
		std::string Out;
		bool bFirst = true;
		for (const auto& Parameter : Parameters)
		{
			if (!bFirst)
			{
				Out += '&';
			}
			bFirst = false;
			Out += PercentEncode(Parameter.Name, bForm);
			if (Parameter.bHasEquals)
			{
				Out += '=';
				Out += PercentEncode(Parameter.Value, bForm);
			}
		}
		return Out;
	}
} // namespace VeroScript::Network
