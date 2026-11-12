// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiStyle.h"
#include "VeroScriptGuiWidget.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"
#include "VeroScriptFloatParsing.h"
#include <charconv>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VeroScript::Gui
{
	bool FWidget::SetStyleType(std::uint64_t Scope, std::string Type, std::string Mark, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		if (!Scope || Type.empty())
		{
			Error = "gui.invalid_style_scope";
			return false;
		}
		for (auto& Membership : StyleMemberships)
		{
			if (Membership.Scope == Scope)
			{
				Membership.Type = std::move(Type);
				Membership.Mark = std::move(Mark);
				MarkDirty();
				return true;
			}
		}
		StyleMemberships.push_back({Scope, std::move(Type), std::move(Mark)});
		MarkDirty();
		return true;
	}

	bool FWidget::AttachStyleSheet(std::uint64_t Scope, std::shared_ptr<const FStyleSheet> Sheet, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		if (!Scope || !Sheet)
		{
			Error = "gui.invalid_style_scope";
			return false;
		}
		StyleSheets[Scope].push_back(std::move(Sheet));
		if (std::none_of(StyleMemberships.begin(), StyleMemberships.end(),
		        [Scope](const auto& Entry)
		{
			return Entry.Scope == Scope;
		}))
		{
			StyleMemberships.push_back({Scope, "Widget", {}});
		}
		MarkDirty();
		return true;
	}

	bool FWidget::RegisterStyleProperty(FStyleProperty Property, std::string& Error)
	{
		if (!Check(Error, true, true))
		{
			return false;
		}
		if (!IsMarkupName(Property.Name) || (!Property.Mark.empty() && !IsMarkupName(Property.Mark)) ||
		    !Property.Decode)
		{
			Error = "gui.invalid_style_property";
			return false;
		}
		EProperty Native;
		std::string Ignored;
		if (FindMarkupProperty(Property.Name, Native, false, Ignored) ||
		    (!Property.Mark.empty() && FindMarkupProperty(Property.Mark, Native, false, Ignored)))
		{
			Error = "gui.reserved_style_property: " + Property.Name;
			return false;
		}
		for (const auto& Existing : StyleProfile)
		{
			if (Property.Name == Existing.first || Property.Name == Existing.second.Mark ||
			    (!Property.Mark.empty() && (Property.Mark == Existing.first || Property.Mark == Existing.second.Mark)))
			{
				Error = "gui.duplicate_style_property: " + Property.Name;
				return false;
			}
		}
		StyleProfile.emplace(Property.Name, std::move(Property));
		if (StyleMutationToken)
		{
			++StyleMutationToken;
		}
		return true;
	}

	bool FWidget::GetStyleValue(const std::string& Name, std::optional<FStyleValue>& Value, std::string& Error) const
	{
		if (!Check(Error))
		{
			return false;
		}
		if (!StyleProfile.count(Name))
		{
			Error = "gui.unknown_style_property: " + Name;
			return false;
		}
		const auto Found = StyleValues.find(Name);
		Value = Found == StyleValues.end() ? std::optional<FStyleValue>{} : std::optional<FStyleValue>(Found->second);
		return true;
	}

	bool FWidget::Invalidate(std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		MarkDirty();
		return true;
	}

	std::uint64_t FWidget::GetSynchronizationToken() const noexcept
	{
		return SynchronizationToken;
	}

	std::uint64_t FWidget::GetMutationToken() const noexcept
	{
		return MutationToken;
	}

	std::uint64_t FWidget::GetStyleMutationToken() const noexcept
	{
		return StyleMutationToken;
	}

	bool FWidget::SetCompiledStyle(EProperty Property, std::optional<FPropertyValue> Value, std::string& Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		const auto Index = static_cast<std::size_t>(Property);
		if (Index >= Properties.size() || !GetPropertyDefinition(Property).bStyle)
		{
			Error = "gui.property_not_styleable";
			return false;
		}
		if (Value && !ValidateProperty(Property, *Value, Error))
		{
			return false;
		}
		auto& Cell = Properties[Index];
		if (Cell.CompiledStyle.has_value() == Value.has_value() &&
		    (!Value || PropertyValuesEqual(*Value, *Cell.CompiledStyle)))
		{
			return true;
		}
		Cell.CompiledStyle = std::move(Value);
		++Cell.Revision;
		MarkDirty();
		return true;
	}

	FStyleProperty MakeStyleProperty(std::string Name, std::string Mark, EStyleValueType Type,
	    std::map<std::string, std::int64_t> EnumValues)
	{
		FStyleProperty Result{std::move(Name), std::move(Mark), {}};
		Result.Decode = [Name = Result.Name, Type, EnumValues = std::move(EnumValues)](
		                    std::string_view Input, FStyleValue& Out, std::string& Error)
		{
			const auto Text = TrimMarkup(Input);
			Error.clear();
			EProperty Builtin;
			if (FindMarkupProperty(Name, Builtin, true, Error) && Builtin >= EProperty::Text)
			{
				FPropertyValue Value;
				if (!ParseMarkupProperty(Name, Input, Builtin, Value, true, Error))
				{
					return false;
				}
				if (const auto* TextValue = std::get_if<std::string>(&Value))
				{
					Out = *TextValue;
				}
				else
				{
					Out = std::move(Value);
				}
				return true;
			}
			Error.clear();
			if (Type == EStyleValueType::Raw)
			{
				Out = std::string(Text);
				return true;
			}
			if (Type == EStyleValueType::String)
			{
				if (Text.size() < 2 || (Text.front() != '\'' && Text.front() != '"') || Text.back() != Text.front())
				{
					Error = "gui.style_string_requires_quotes";
					return false;
				}
				std::string Value;
				for (std::size_t Index = 1; Index + 1 < Text.size(); ++Index)
				{
					char C = Text[Index];
					if (C == '\\')
					{
						if (++Index + 1 >= Text.size())
						{
							Error = "gui.invalid_style_escape";
							return false;
						}
						C = Text[Index];
						if (C == 'n')
						{
							C = '\n';
						}
						else if (C == 'r')
						{
							C = '\r';
						}
						else if (C == 't')
						{
							C = '\t';
						}
						else if (C != '\\' && C != '\'' && C != '"')
						{
							Error = "gui.invalid_style_escape";
							return false;
						}
					}
					else if (C == Text.front())
					{
						Error = "gui.invalid_style_string";
						return false;
					}
					Value += C;
				}
				Out = std::move(Value);
				return true;
			}
			if (Type == EStyleValueType::Enum)
			{
				const auto Found = EnumValues.find(Text);
				if (Found != EnumValues.end())
				{
					Out = Found->second;
					return true;
				}
			}
			else if (Type == EStyleValueType::Int || Type == EStyleValueType::Int64)
			{
				std::int64_t Value = 0;
				const auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
				if (Parsed.ec == std::errc{} && Parsed.ptr == Text.data() + Text.size() &&
				    (Type != EStyleValueType::Int || (Value >= INT32_MIN && Value <= INT32_MAX)))
				{
					Out = Value;
					return true;
				}
			}
			else if (Type == EStyleValueType::UInt64)
			{
				std::uint64_t Value = 0;
				const auto Parsed = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
				if (Parsed.ec == std::errc{} && Parsed.ptr == Text.data() + Text.size())
				{
					Out = Value;
					return true;
				}
			}
			else if (Type == EStyleValueType::Double || Type == EStyleValueType::Float)
			{
				double Value = 0;
				const auto Parsed = FloatParsingDetail::FromCharsDecimal(Text, Value);
				if (Parsed.ec == std::errc{} && Parsed.ptr == Text.data() + Text.size() && std::isfinite(Value) &&
				    (Type != EStyleValueType::Float || std::abs(Value) <= std::numeric_limits<float>::max()))
				{
					Out = FPropertyValue(Value);
					return true;
				}
			}
			else
			{
				const char* Native = Type == EStyleValueType::Bool ? "Enabled"
				    : Type == EStyleValueType::Length              ? "Width"
				    : Type == EStyleValueType::Insets              ? "Padding"
				                                                   : "Background";
				EProperty Property;
				FPropertyValue Value;
				if (ParseMarkupProperty(Native, Text, Property, Value, false, Error))
				{
					Out = std::move(Value);
					return true;
				}
			}
			if (Error.empty())
			{
				Error = "gui.invalid_style_value: " + Text;
			}
			return false;
		};
		return Result;
	}
} // namespace VeroScript::Gui
