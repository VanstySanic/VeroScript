// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiTypes.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace VeroScript::Gui
{
	namespace
	{
		const std::array<FPropertyDefinition, static_cast<std::size_t>(EProperty::Count)> Definitions{
			{{"Width", "width", FLength{}, true, "length"},
			 {"Height", "height", FLength{}, true, "length"},
			 {"Padding", "padding", FInsets{}, true, "insets"},
			 {"Gap", "gap", 0.0, true, "number"},
			 {"Layout", "layout", ELayout::Vertical, true},
			 {"Visibility", "visibility", EVisibility::Visible, true},
			 {"HitTest", "hit-test", EHitTestMode::SelfAndChildren, false},
			 {"Enabled", "enabled", true, false},
			 {"Focusable", "focusable", false, false},
			 {"Opacity", "opacity", 1.0, true, "number"},
			 {"Background", "background-color", Graphics::FColor{0, 0, 0, 0}, true, "color"},
			 {"Text", "text", std::string{}, false},
			 {"Image", "image", std::string{}, false},
			 {"FontFamily", "font-family", std::string{"Vero Sans"}, true},
			 {"FontSize", "font-size", 16.0, true},
			 {"FontWeight", "font-weight", 400.0, true},
			 {"FontStyle", "font-style", std::string{"normal"}, true},
			 {"TextColor", "color", Graphics::FColor{1, 1, 1, 1}, true},
			 {"TextAlign", "text-align", std::string{"left"}, true},
			 {"WhiteSpace", "white-space", std::string{"normal"}, true},
			 {"TextOverflow", "text-overflow", std::string{"clip"}, true},
			 {"LineHeight", "line-height", 0.0, true},
			 {"LetterSpacing", "letter-spacing", 0.0, true},
			 {"Direction", "direction", std::string{"auto"}, true},
			 {"Language", "language", std::string{}, true},
			 {"CornerRadius", "border-radius", 0.0, true},
			 {"ClipContent", "clip-content", false, true},
			 {"Filter", "filter", std::string{}, true},
			 {"BackdropFilter", "backdrop-filter", std::string{}, true},
			 {"BackgroundImage", "background-image", std::string{}, true},
			 {"BoxShadow", "box-shadow", std::string{}, true},
			 {"Transform", "transform", std::string{}, true},
			 {"MaskImage", "mask-image", std::string{}, true},
			 {"BorderWidth", "border-width", 0.0, true},
			 {"BorderColor", "border-color", Graphics::FColor{.4f, .4f, .4f, 1}, true},
			 {"Cursor", "cursor", ECursor::Auto, true}}};

		bool ValidSize(double Value)
		{
			return std::isfinite(Value) && Value >= 0 && Value <= 16777216.0;
		}
	} // namespace

	const char *GetCursorName(ECursor Value) noexcept
	{
		static constexpr const char *Names[]{"Auto",	 "Default",	   "Text",		 "Wait",	 "Crosshair",
											 "Progress", "ResizeNWSE", "ResizeNESW", "ResizeEW", "ResizeNS",
											 "Move",	 "NotAllowed", "Pointer",	 "None"};
		const auto Index = static_cast<std::size_t>(Value);
		return Index < std::size(Names) ? Names[Index] : nullptr;
	}

	const char *GetCursorMark(ECursor Value) noexcept
	{
		static constexpr const char *Marks[]{"auto",	 "default",		"text",		   "wait",		"crosshair",
											 "progress", "nwse-resize", "nesw-resize", "ew-resize", "ns-resize",
											 "move",	 "not-allowed", "pointer",	   "none"};
		const auto Index = static_cast<std::size_t>(Value);
		return Index < std::size(Marks) ? Marks[Index] : nullptr;
	}

	bool FLength::operator==(const FLength& Other) const noexcept
	{
		return Unit == Other.Unit && Value == Other.Value;
	}

	bool FInsets::operator==(const FInsets& Other) const noexcept
	{
		return Top == Other.Top && Right == Other.Right && Bottom == Other.Bottom && Left == Other.Left;
	}

	bool FRect::Contains(double PointX, double PointY) const noexcept
	{
		return Width > 0 && Height > 0 && PointX >= X && PointY >= Y && PointX < X + Width && PointY < Y + Height;
	}

	const FPropertyDefinition& GetPropertyDefinition(EProperty Property)
	{
		const auto Index = static_cast<std::size_t>(Property);
		if (Index >= Definitions.size())
		{
			throw std::out_of_range("gui.invalid_property");
		}
		return Definitions[Index];
	}

	bool ValidateProperty(EProperty Property, const FPropertyValue& Value, std::string& Error)
	{
		Error.clear();
		const auto Index = static_cast<std::size_t>(Property);
		if (Index >= Definitions.size() || Value.index() != Definitions[Index].DefaultValue.index())
		{
			Error = "gui.property_type: property value has an incompatible type";
			return false;
		}
		bool bValid = true;
		if (const auto* Length = std::get_if<FLength>(&Value))
		{
			bValid = Length->Unit <= ELengthUnit::Percent && ValidSize(Length->Value) &&
					 (Length->Unit != ELengthUnit::Auto || Length->Value == 0);
		}
		else if (const auto* Insets = std::get_if<FInsets>(&Value))
		{
			bValid = ValidSize(Insets->Top) && ValidSize(Insets->Right) && ValidSize(Insets->Bottom) &&
					 ValidSize(Insets->Left);
		}
		else if (const auto* Number = std::get_if<double>(&Value))
		{
			bValid = (Property == EProperty::LetterSpacing ? std::isfinite(*Number) && std::abs(*Number) <= 16777216
														   : ValidSize(*Number)) &&
					 (Property != EProperty::Opacity || *Number <= 1) &&
					 (Property != EProperty::FontSize || (*Number > 0 && *Number <= 4096)) &&
					 (Property != EProperty::FontWeight ||
					  (*Number >= 1 && *Number <= 1000 && std::floor(*Number) == *Number));
		}
		else if (const auto* Layout = std::get_if<ELayout>(&Value))
		{
			bValid = *Layout <= ELayout::Overlay;
		}
		else if (const auto* Visibility = std::get_if<EVisibility>(&Value))
		{
			bValid = *Visibility <= EVisibility::Collapsed;
		}
		else if (const auto* HitTest = std::get_if<EHitTestMode>(&Value))
		{
			bValid = *HitTest <= EHitTestMode::None;
		}
		else if (const auto *Cursor = std::get_if<ECursor>(&Value))
		{
			bValid = GetCursorName(*Cursor) != nullptr;
		}
		else if (const auto* Color = std::get_if<Graphics::FColor>(&Value))
		{
			bValid = Graphics::ValidateColor(*Color, Error);
		}
		else if (const auto* Text = std::get_if<std::string>(&Value))
		{
			bValid =
				Text->size() <= (Property == EProperty::Text ? 16 * 1024 * 1024u : 16384u) && ValidateGuiText(*Text);
			if (Property == EProperty::FontStyle)
			{
				bValid = bValid && (*Text == "normal" || *Text == "italic");
			}
			else if (Property == EProperty::TextAlign)
			{
				bValid = bValid && (*Text == "left" || *Text == "right" || *Text == "center" || *Text == "justify");
			}
			else if (Property == EProperty::WhiteSpace)
			{
				bValid = bValid && (*Text == "normal" || *Text == "pre" || *Text == "pre-wrap" || *Text == "pre-line" ||
									*Text == "nowrap");
			}
			else if (Property == EProperty::TextOverflow)
			{
				bValid = bValid && (*Text == "clip" || *Text == "ellipsis");
			}
			else if (Property == EProperty::Direction)
			{
				bValid = bValid && (*Text == "auto" || *Text == "ltr" || *Text == "rtl");
			}
			else if (Property == EProperty::FontFamily)
			{
				bValid = bValid && !Text->empty();
			}
		}
		if (!bValid && Error.empty())
		{
			Error = std::string("gui.property_range: invalid value for ") + Definitions[Index].Name;
		}
		return bValid;
	}

	bool IsInheritedProperty(EProperty Property) noexcept
	{
		return (Property >= EProperty::FontFamily && Property <= EProperty::Language) || Property == EProperty::Cursor;
	}

	bool ValidateGuiText(const std::string& Text) noexcept
	{
		for (std::size_t I = 0; I < Text.size();)
		{
			const auto First = static_cast<unsigned char>(Text[I++]);
			if (First < 128)
			{
				if (First < 32 && First != '\n' && First != '\r' && First != '\t')
				{
					return false;
				}
				continue;
			}
			const unsigned Count = First >= 0xc2 && First < 0xe0	? 1
								   : First >= 0xe0 && First < 0xf0	? 2
								   : First >= 0xf0 && First <= 0xf4 ? 3
																	: 0;
			if (!Count || Text.size() - I < Count)
			{
				return false;
			}
			std::uint32_t Code = First & (0x7f >> (Count + 1));
			for (unsigned Part = 0; Part < Count; ++Part)
			{
				const auto Byte = static_cast<unsigned char>(Text[I++]);
				if ((Byte & 0xc0) != 0x80)
				{
					return false;
				}
				Code = (Code << 6) | (Byte & 63);
			}
			if (Code < (Count == 1	 ? 0x80u
						: Count == 2 ? 0x800u
									 : 0x10000u) ||
				Code > 0x10ffff || (Code >= 0xd800 && Code <= 0xdfff))
			{
				return false;
			}
		}
		return true;
	}

	bool PropertyValuesEqual(const FPropertyValue& Left, const FPropertyValue& Right)
	{
		if (Left.index() != Right.index())
		{
			return false;
		}
		return std::visit(
			[&](const auto& Value)
			{
				using T = std::decay_t<decltype(Value)>;
				const auto& Other = std::get<T>(Right);
				if constexpr (std::is_same_v<T, Graphics::FColor>)
				{
					return Value.R == Other.R && Value.G == Other.G && Value.B == Other.B && Value.A == Other.A;
				}
				else
				{
					return Value == Other;
				}
			},
			Left);
	}

	bool ValidateViewport(const FViewport& Viewport, std::string& Error)
	{
		Error.clear();
		if (Viewport.PixelWidth <= 0 || Viewport.PixelHeight <= 0 ||
			Viewport.PixelWidth > std::numeric_limits<int>::max() ||
			Viewport.PixelHeight > std::numeric_limits<int>::max() || !std::isfinite(Viewport.DisplayScale) ||
			Viewport.DisplayScale < 0.01 || Viewport.DisplayScale > 100)
		{
			Error = "gui.invalid_viewport: positive representable pixels and a finite display scale are required";
			return false;
		}
		return true;
	}

	bool ResolveProperties(const std::array<FPropertyValue, static_cast<std::size_t>(EProperty::Count)>& Values,
						   FResolvedProperties& Result, std::string& Error)
	{
		for (std::size_t Index = 0; Index < Values.size(); ++Index)
		{
			if (!ValidateProperty(static_cast<EProperty>(Index), Values[Index], Error))
			{
				return false;
			}
		}
		Result.Width = std::get<FLength>(Values[0]);
		Result.Height = std::get<FLength>(Values[1]);
		Result.Padding = std::get<FInsets>(Values[2]);
		Result.Gap = std::get<double>(Values[3]);
		Result.Layout = std::get<ELayout>(Values[4]);
		Result.Visibility = std::get<EVisibility>(Values[5]);
		Result.HitTest = std::get<EHitTestMode>(Values[6]);
		Result.bEnabled = std::get<bool>(Values[7]);
		Result.bFocusable = std::get<bool>(Values[8]);
		Result.Opacity = std::get<double>(Values[9]);
		Result.Background = std::get<Graphics::FColor>(Values[10]);
		Result.Text = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Text)]);
		Result.Image = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Image)]);
		Result.FontFamily = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::FontFamily)]);
		Result.FontSize = std::get<double>(Values[static_cast<std::size_t>(EProperty::FontSize)]);
		Result.FontWeight = std::get<double>(Values[static_cast<std::size_t>(EProperty::FontWeight)]);
		Result.FontStyle = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::FontStyle)]);
		Result.TextColor = std::get<Graphics::FColor>(Values[static_cast<std::size_t>(EProperty::TextColor)]);
		Result.TextAlign = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::TextAlign)]);
		Result.WhiteSpace = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::WhiteSpace)]);
		Result.TextOverflow = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::TextOverflow)]);
		Result.LineHeight = std::get<double>(Values[static_cast<std::size_t>(EProperty::LineHeight)]);
		Result.LetterSpacing = std::get<double>(Values[static_cast<std::size_t>(EProperty::LetterSpacing)]);
		Result.Direction = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Direction)]);
		Result.Language = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Language)]);
		Result.CornerRadius = std::get<double>(Values[static_cast<std::size_t>(EProperty::CornerRadius)]);
		Result.bClipContent = std::get<bool>(Values[static_cast<std::size_t>(EProperty::ClipContent)]);
		Result.Filter = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Filter)]);
		Result.BackdropFilter = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::BackdropFilter)]);
		Result.BackgroundImage = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::BackgroundImage)]);
		Result.BoxShadow = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::BoxShadow)]);
		Result.Transform = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::Transform)]);
		Result.MaskImage = std::get<std::string>(Values[static_cast<std::size_t>(EProperty::MaskImage)]);
		Result.BorderWidth = std::get<double>(Values[static_cast<std::size_t>(EProperty::BorderWidth)]);
		Result.BorderColor = std::get<Graphics::FColor>(Values[static_cast<std::size_t>(EProperty::BorderColor)]);
		Result.Cursor = std::get<ECursor>(Values[static_cast<std::size_t>(EProperty::Cursor)]);
		return true;
	}

	bool ResolvedPropertiesEqual(const FResolvedProperties& Left, const FResolvedProperties& Right)
	{
		return Left.Width == Right.Width && Left.Height == Right.Height && Left.Padding == Right.Padding &&
			   Left.Gap == Right.Gap && Left.Opacity == Right.Opacity && Left.Layout == Right.Layout &&
			   Left.Visibility == Right.Visibility && Left.HitTest == Right.HitTest &&
			   Left.bEnabled == Right.bEnabled && Left.bFocusable == Right.bFocusable &&
			   PropertyValuesEqual(Left.Background, Right.Background) && PropertyValuesEqual(Left.Text, Right.Text) &&
			   PropertyValuesEqual(Left.Image, Right.Image) && PropertyValuesEqual(Left.FontFamily, Right.FontFamily) &&
			   PropertyValuesEqual(Left.FontSize, Right.FontSize) &&
			   PropertyValuesEqual(Left.FontWeight, Right.FontWeight) &&
			   PropertyValuesEqual(Left.FontStyle, Right.FontStyle) &&
			   PropertyValuesEqual(Left.TextColor, Right.TextColor) &&
			   PropertyValuesEqual(Left.TextAlign, Right.TextAlign) &&
			   PropertyValuesEqual(Left.WhiteSpace, Right.WhiteSpace) &&
			   PropertyValuesEqual(Left.TextOverflow, Right.TextOverflow) &&
			   PropertyValuesEqual(Left.LineHeight, Right.LineHeight) &&
			   PropertyValuesEqual(Left.LetterSpacing, Right.LetterSpacing) &&
			   PropertyValuesEqual(Left.Direction, Right.Direction) &&
			   PropertyValuesEqual(Left.Language, Right.Language) &&
			   PropertyValuesEqual(Left.CornerRadius, Right.CornerRadius) &&
			   PropertyValuesEqual(Left.bClipContent, Right.bClipContent) &&
			   PropertyValuesEqual(Left.Filter, Right.Filter) &&
			   PropertyValuesEqual(Left.BackdropFilter, Right.BackdropFilter) &&
			   PropertyValuesEqual(Left.BackgroundImage, Right.BackgroundImage) &&
			   PropertyValuesEqual(Left.BoxShadow, Right.BoxShadow) &&
			   PropertyValuesEqual(Left.Transform, Right.Transform) &&
			   PropertyValuesEqual(Left.MaskImage, Right.MaskImage) &&
			   PropertyValuesEqual(Left.BorderWidth, Right.BorderWidth) &&
			   PropertyValuesEqual(Left.BorderColor, Right.BorderColor) && Left.Cursor == Right.Cursor;
	}

	bool ValidateResolvedProperties(const FResolvedProperties& Value, std::string& Error)
	{
		const std::array<FPropertyValue, static_cast<std::size_t>(EProperty::Count)> Values{
			Value.Width,		  Value.Height,			 Value.Padding,		  Value.Gap,
			Value.Layout,		  Value.Visibility,		 Value.HitTest,		  Value.bEnabled,
			Value.bFocusable,	  Value.Opacity,		 Value.Background,	  Value.Text,
			Value.Image,		  Value.FontFamily,		 Value.FontSize,	  Value.FontWeight,
			Value.FontStyle,	  Value.TextColor,		 Value.TextAlign,	  Value.WhiteSpace,
			Value.TextOverflow,	  Value.LineHeight,		 Value.LetterSpacing, Value.Direction,
			Value.Language,		  Value.CornerRadius,	 Value.bClipContent,  Value.Filter,
			Value.BackdropFilter, Value.BackgroundImage, Value.BoxShadow,	  Value.Transform,
			Value.MaskImage,	  Value.BorderWidth,	 Value.BorderColor,	  Value.Cursor};
		FResolvedProperties Checked;
		return ResolveProperties(Values, Checked, Error);
	}
} // namespace VeroScript::Gui
