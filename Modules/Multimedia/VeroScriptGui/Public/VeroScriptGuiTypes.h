// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGraphics.h"
#include <array>
#include <functional>
#include <optional>
#include <variant>

namespace VeroScript::Gui
{
	enum class EVisibility : std::uint8_t
	{
		Visible,
		Hidden,
		Collapsed
	};

	enum class EHitTestMode : std::uint8_t
	{
		SelfAndChildren,
		SelfOnly,
		ChildrenOnly,
		None
	};

	enum class ELayout : std::uint8_t
	{
		Vertical,
		Horizontal,
		Overlay
	};

	enum class ECursor : std::uint8_t
	{
		Auto,
		Default,
		Text,
		Wait,
		Crosshair,
		Progress,
		ResizeNWSE,
		ResizeNESW,
		ResizeEW,
		ResizeNS,
		Move,
		NotAllowed,
		Pointer,
		None
	};

	enum class EProperty : std::uint8_t
	{
		Width,
		Height,
		Padding,
		Gap,
		Layout,
		Visibility,
		HitTest,
		Enabled,
		Focusable,
		Opacity,
		Background,
		Text,
		Image,
		FontFamily,
		FontSize,
		FontWeight,
		FontStyle,
		TextColor,
		TextAlign,
		WhiteSpace,
		TextOverflow,
		LineHeight,
		LetterSpacing,
		Direction,
		Language,
		CornerRadius,
		ClipContent,
		Filter,
		BackdropFilter,
		BackgroundImage,
		BoxShadow,
		Transform,
		MaskImage,
		BorderWidth,
		BorderColor,
		Cursor,
		Count
	};

	enum class ELengthUnit : std::uint8_t
	{
		Auto,
		Pixel,
		Percent
	};

	struct FLength
	{
		bool operator==(const FLength& Other) const noexcept;

		ELengthUnit Unit = ELengthUnit::Auto;
		double Value = 0;
	};

	struct FInsets
	{
		bool operator==(const FInsets& Other) const noexcept;

		double Top = 0, Right = 0, Bottom = 0, Left = 0;
	};

	struct FRect
	{
		bool Contains(double PointX, double PointY) const noexcept;

		double X = 0, Y = 0, Width = 0, Height = 0;
	};

	struct FViewport
	{
		std::int64_t PixelWidth = 1, PixelHeight = 1;
		double DisplayScale = 1;
	};

	using FPropertyValue = std::variant<FLength, FInsets, double, ELayout, EVisibility, EHitTestMode, bool,
										Graphics::FColor, std::string, ECursor>;
	// A missing value falls through to the style/default source; errors never fall through.
	using FPropertyGetter = std::function<bool(std::optional<FPropertyValue>&, std::string&)>;

	struct FPropertyDefinition
	{
		const char* Name;
		const char* Mark;
		FPropertyValue DefaultValue;
		bool bStyle;
		const char* Interpolator = "";
	};

	struct FResolvedProperties
	{
		FLength Width, Height;
		FInsets Padding;
		double Gap = 0, Opacity = 1;
		ELayout Layout = ELayout::Vertical;
		EVisibility Visibility = EVisibility::Visible;
		EHitTestMode HitTest = EHitTestMode::SelfAndChildren;
		bool bEnabled = true, bFocusable = false;
		Graphics::FColor Background{0, 0, 0, 0};
		std::string Text;
		std::string Image;
		std::string FontFamily = "Vero Sans";
		double FontSize = 16;
		double FontWeight = 400;
		std::string FontStyle = "normal";
		Graphics::FColor TextColor{1, 1, 1, 1};
		std::string TextAlign = "left";
		std::string WhiteSpace = "normal";
		std::string TextOverflow = "clip";
		double LineHeight = 0;
		double LetterSpacing = 0;
		std::string Direction = "auto";
		std::string Language;
		double CornerRadius = 0;
		bool bClipContent = false;
		std::string Filter;
		std::string BackdropFilter;
		std::string BackgroundImage;
		std::string BoxShadow;
		std::string Transform;
		std::string MaskImage;
		double BorderWidth = 0;
		Graphics::FColor BorderColor{.4f, .4f, .4f, 1};
		ECursor Cursor = ECursor::Auto;
	};

	const char *GetCursorName(ECursor Value) noexcept;
	const char *GetCursorMark(ECursor Value) noexcept;
	const FPropertyDefinition& GetPropertyDefinition(EProperty Property);
	bool ValidateProperty(EProperty Property, const FPropertyValue& Value, std::string& Error);
	bool IsInheritedProperty(EProperty Property) noexcept;
	bool ValidateGuiText(const std::string& Text) noexcept;
	bool PropertyValuesEqual(const FPropertyValue& Left, const FPropertyValue& Right);
	bool ValidateViewport(const FViewport& Viewport, std::string& Error);
	bool ResolvedPropertiesEqual(const FResolvedProperties& Left, const FResolvedProperties& Right);
	bool ValidateResolvedProperties(const FResolvedProperties& Value, std::string& Error);
	bool ResolveProperties(const std::array<FPropertyValue, static_cast<std::size_t>(EProperty::Count)>& Values,
						   FResolvedProperties& Result, std::string& Error);
} // namespace VeroScript::Gui
