// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiMarkupInternal.h"
#include "VeroScriptFloatParsing.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace VeroScript::Gui
{
	FMarkupLocator::FMarkupLocator(const FMarkupSource& InSource) : Source(InSource), Position{InSource.Identity}
	{
	}

	FMarkupLocation FMarkupLocator::At(std::size_t Offset)
	{
		if (Offset < Position.Offset)
		{
			Position = {Source.Identity};
		}
		for (std::size_t Index = Position.Offset; Index < std::min(Offset, Source.Text.size()); ++Index)
		{
			const auto Character = static_cast<unsigned char>(Source.Text[Index]);
			if (Character == '\n')
			{
				++Position.Line;
				Position.Column = 1;
			}
			else if ((Character & 0xc0) != 0x80)
			{
				++Position.Column;
			}
		}
		Position.Offset = Offset;
		return Position;
	}

	FMarkupLocation MarkupLocation(const FMarkupSource& Source, std::size_t Offset)
	{
		return FMarkupLocator(Source).At(Offset);
	}

	bool ValidateMarkupUtf8(const FMarkupSource& Source, FMarkupDiagnostic& Diagnostic)
	{
		for (std::size_t Index = 0; Index < Source.Text.size();)
		{
			const auto First = static_cast<unsigned char>(Source.Text[Index]);
			std::size_t Count = First < 128      ? 1
			    : First >= 0xc2 && First <= 0xdf ? 2
			    : First <= 0xef && First >= 0xe0 ? 3
			    : First >= 0xf0 && First <= 0xf4 ? 4
			                                     : 0;
			std::uint32_t Code = First & (Count == 1 ? 0x7f : Count == 2 ? 0x1f
			                                     : Count == 3            ? 0x0f
			                                                             : 0x07);
			bool bValid = Count != 0 && Count <= Source.Text.size() - Index;
			for (std::size_t Part = 1; bValid && Part < Count; ++Part)
			{
				const auto Byte = static_cast<unsigned char>(Source.Text[Index + Part]);
				bValid = (Byte & 0xc0) == 0x80;
				Code = (Code << 6) | (Byte & 0x3f);
			}
			bValid = bValid && !(Count == 2 && Code < 0x80) && !(Count == 3 && Code < 0x800) &&
			    !(Count == 4 && Code < 0x10000) && !(Code >= 0xd800 && Code <= 0xdfff) && Code <= 0x10ffff &&
			    (Code >= 32 || Code == 9 || Code == 10 || Code == 13);
			if (!bValid)
			{
				return MarkupFail(Diagnostic, MarkupLocation(Source, Index), "gui.invalid_utf8_or_character");
			}
			Index += Count;
		}
		return true;
	}

	bool MarkupFail(FMarkupDiagnostic& Diagnostic, const FMarkupLocation& Location, std::string Message)
	{
		Diagnostic = {Location, std::move(Message)};
		return false;
	}

	std::string TrimMarkup(std::string_view Value)
	{
		const auto Begin = Value.find_first_not_of(" \t\r\n\f");
		if (Begin == std::string_view::npos)
		{
			return {};
		}
		return std::string(Value.substr(Begin, Value.find_last_not_of(" \t\r\n\f") - Begin + 1));
	}

	bool IsMarkupName(std::string_view Value)
	{
		const auto First = [](unsigned char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_' || C >= 128;
		};
		if (Value.empty() || !First(static_cast<unsigned char>(Value.front())))
		{
			return false;
		}
		return std::all_of(Value.begin() + 1, Value.end(),
		    [&](unsigned char C)
		{
			return First(C) || (C >= '0' && C <= '9') || C == '-' || C == '.';
		});
	}

	namespace
	{
		bool Number(std::string_view Text, double& Value)
		{
			if (Text.empty())
			{
				return false;
			}
			if (Text.front() == '+')
			{
				Text.remove_prefix(1);
			}
			const auto Result = FloatParsingDetail::FromCharsDecimal(Text, Value);
			return Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() && std::isfinite(Value);
		}

		bool Length(std::string_view Text, FLength& Value)
		{
			if (Text == "auto")
			{
				Value = {};
				return true;
			}
			Value.Unit = ELengthUnit::Pixel;
			if (!Text.empty() && Text.back() == '%')
			{
				Value.Unit = ELengthUnit::Percent;
				Text.remove_suffix(1);
			}
			else if (Text.size() >= 2 && (Text.substr(Text.size() - 2) == "px" || Text.substr(Text.size() - 2) == "dp"))
			{
				Text.remove_suffix(2);
			}
			return Number(Text, Value.Value);
		}

		float Decode(float Value)
		{
			return Value <= 0.04045f ? Value / 12.92f : std::pow((Value + 0.055f) / 1.055f, 2.4f);
		}

		bool Color(std::string Text, Graphics::FColor& Value)
		{
			const std::map<std::string, std::string> Named{{"transparent", "#0000"}, {"black", "#000"},
			    {"white", "#fff"}, {"red", "#f00"},
			    {"green", "#008000"}, {"blue", "#00f"}};
			if (const auto Found = Named.find(Text); Found != Named.end())
			{
				Text = Found->second;
			}
			if (!Text.empty() && Text.front() == '#')
			{
				Text.erase(0, 1);
				if (Text.size() == 3 || Text.size() == 4)
				{
					std::string Expanded;
					for (const char C : Text)
					{
						Expanded.append(2, C);
					}
					Text = std::move(Expanded);
				}
				if (Text.size() != 6 && Text.size() != 8)
				{
					return false;
				}
				std::array<float, 4> Channels{0, 0, 0, 1};
				for (std::size_t Index = 0; Index < Text.size() / 2; ++Index)
				{
					unsigned Byte = 0;
					const auto Result = std::from_chars(Text.data() + Index * 2, Text.data() + Index * 2 + 2, Byte, 16);
					if (Result.ec != std::errc{} || Result.ptr != Text.data() + Index * 2 + 2)
					{
						return false;
					}
					Channels[Index] = Byte / 255.0f;
				}
				Value = {Decode(Channels[0]), Decode(Channels[1]), Decode(Channels[2]), Channels[3]};
				return true;
			}
			const bool bAlpha = Text.compare(0, 5, "rgba(") == 0;
			const std::size_t Begin = bAlpha ? 5 : 4;
			if ((!bAlpha && Text.compare(0, 4, "rgb(") != 0) || Text.back() != ')')
			{
				return false;
			}
			Text = Text.substr(Begin, Text.size() - Begin - 1);
			std::array<double, 4> Channels{0, 0, 0, 1};
			std::size_t Offset = 0;
			for (std::size_t Index = 0; Index < (bAlpha ? 4u : 3u); ++Index)
			{
				const auto End = Text.find(',', Offset);
				if ((Index + 1 == (bAlpha ? 4u : 3u)) != (End == std::string::npos))
				{
					return false;
				}
				if (!Number(TrimMarkup(std::string_view(Text).substr(Offset, End - Offset)), Channels[Index]) ||
				    Channels[Index] < 0 || Channels[Index] > (Index == 3 ? 1 : 255))
				{
					return false;
				}
				Offset = End == std::string::npos ? Text.size() : End + 1;
			}
			Value = {Decode(static_cast<float>(Channels[0] / 255)), Decode(static_cast<float>(Channels[1] / 255)),
			    Decode(static_cast<float>(Channels[2] / 255)), static_cast<float>(Channels[3])};
			return true;
		}
	} // namespace

	bool FindMarkupProperty(std::string_view Name, EProperty& Property, bool bStyle, std::string& Error)
	{
		for (std::size_t Index = 0; Index < static_cast<std::size_t>(EProperty::Count); ++Index)
		{
			const auto& Definition = GetPropertyDefinition(static_cast<EProperty>(Index));
			if (Name == Definition.Name || Name == Definition.Mark)
			{
				if (bStyle && !Definition.bStyle)
				{
					Error = "gui.property_not_styleable: " + std::string(Name);
					return false;
				}
				Property = static_cast<EProperty>(Index);
				return true;
			}
		}
		Error = "gui.unknown_property: " + std::string(Name);
		return false;
	}

	bool ParseMarkupProperty(std::string_view Name, std::string_view Input, EProperty& Property, FPropertyValue& Parsed,
	    bool bStyle, std::string& Error)
	{
		if (!FindMarkupProperty(Name, Property, bStyle, Error))
		{
			return false;
		}
		const std::string Text = TrimMarkup(Input);
		bool bValid = false;
		if (Property == EProperty::Width || Property == EProperty::Height)
		{
			FLength Value;
			bValid = Length(Text, Value);
			Parsed = Value;
		}
		else if (Property == EProperty::Padding)
		{
			std::istringstream Stream(Text);
			std::vector<double> Values;
			bool bValidSizes = true;
			std::string Part;
			while (Stream >> Part)
			{
				FLength Value;
				if (!Length(Part, Value) || Value.Unit != ELengthUnit::Pixel)
				{
					bValidSizes = false;
					break;
				}
				Values.push_back(Value.Value);
				if (Values.size() > 4)
				{
					break;
				}
			}
			bValid = bValidSizes && Stream.eof() && !Values.empty() && Values.size() <= 4;
			if (bValid)
			{
				Parsed = FInsets{Values[0], Values.size() > 1 ? Values[1] : Values[0],
				    Values.size() > 2 ? Values[2] : Values[0],
				    Values.size() > 3 ? Values[3] : (Values.size() > 1 ? Values[1] : Values[0])};
			}
		}
		else if (Property == EProperty::Cursor)
		{
			for (unsigned Index = 0; Index <= static_cast<unsigned>(ECursor::None); ++Index)
			{
				const auto Value = static_cast<ECursor>(Index);
				std::string Lower = GetCursorName(Value);
				std::transform(Lower.begin(), Lower.end(), Lower.begin(),
				    [](char C)
				{
					return static_cast<char>(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C);
				});
				if (Text == GetCursorName(Value) || Text == GetCursorMark(Value) || Text == Lower)
				{
					Parsed = Value;
					bValid = true;
					break;
				}
			}
		}
		else if (std::holds_alternative<std::string>(GetPropertyDefinition(Property).DefaultValue))
		{
			std::string Value = Property == EProperty::Text ? std::string(Input) : Text;
			if (bStyle && Value.size() >= 2 && (Value.front() == '\'' || Value.front() == '"') &&
			    Value.back() == Value.front())
			{
				Value = Value.substr(1, Value.size() - 2);
			}
			Parsed = std::move(Value);
			bValid = true;
		}
		else if (std::holds_alternative<double>(GetPropertyDefinition(Property).DefaultValue))
		{
			double Value = 0;
			if (Property == EProperty::LineHeight && Text == "normal")
			{
				bValid = true;
				Value = 0;
			}
			else if (Property == EProperty::FontWeight && (Text == "normal" || Text == "bold"))
			{
				bValid = true;
				Value = Text == "bold" ? 700 : 400;
			}
			else if (Property != EProperty::Opacity && Property != EProperty::FontWeight)
			{
				FLength Size;
				bValid = Length(Text, Size) && Size.Unit == ELengthUnit::Pixel;
				Value = Size.Value;
			}
			else
			{
				bValid = Number(Text, Value);
			}
			Parsed = Value;
		}
		else if (std::holds_alternative<Graphics::FColor>(GetPropertyDefinition(Property).DefaultValue))
		{
			Graphics::FColor Value;
			bValid = Color(Text, Value);
			Parsed = Value;
		}
		else if (std::holds_alternative<bool>(GetPropertyDefinition(Property).DefaultValue))
		{
			bValid = Text == "true" || Text == "false";
			Parsed = Text == "true";
		}
		else
		{
			const std::vector<std::string> Names =
			    Property == EProperty::Layout ? std::vector<std::string>{"Vertical", "Horizontal", "Overlay"}
			    : Property == EProperty::Visibility
			    ? std::vector<std::string>{"Visible", "Hidden", "Collapsed"}
			    : std::vector<std::string>{"SelfAndChildren", "SelfOnly", "ChildrenOnly", "None"};
			for (std::size_t Index = 0; Index < Names.size(); ++Index)
			{
				std::string Lower = Names[Index];
				std::transform(Lower.begin(), Lower.end(), Lower.begin(),
				    [](char C)
				{
					return static_cast<char>(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C);
				});
				if (Text == Names[Index] || Text == Lower)
				{
					bValid = true;
					if (Property == EProperty::Layout)
					{
						Parsed = static_cast<ELayout>(Index);
					}
					else if (Property == EProperty::Visibility)
					{
						Parsed = static_cast<EVisibility>(Index);
					}
					else
					{
						Parsed = static_cast<EHitTestMode>(Index);
					}
					break;
				}
			}
		}
		if (!bValid)
		{
			Error = "gui.invalid_property_literal: " + std::string(Name) + "=" + Text;
			return false;
		}
		return ValidateProperty(Property, Parsed, Error);
	}

	bool FindMarkupEvent(std::string_view Name, Platform::EEventType& Event)
	{
		if (const auto* Definition = FindEventDefinition(Name))
		{
			Event = Definition->Type;
			return true;
		}
		return false;
	}
} // namespace VeroScript::Gui
