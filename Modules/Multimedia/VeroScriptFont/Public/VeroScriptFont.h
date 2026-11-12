// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Font
{
	class FTextLayout;
	enum class EDirection : std::uint8_t
	{
		Auto,
		LeftToRight,
		RightToLeft
	};

	struct FFontFace
	{
		std::string Source, Family;
		std::int32_t FaceIndex = 0, Weight = 400;
		bool bItalic = false, bFallback = false;
	};

	struct FFaceInfo
	{
		std::string Family;
		std::int32_t Weight = 400;
		bool bItalic = false;
	};

	struct FVariation
	{
		std::int32_t Weight = 400, Width = 100, NamedInstance = 0;
	};

	struct FMetrics
	{
		double Ascent = 0, Descent = 0, LineSpacing = 0, XHeight = 0;
		double UnderlinePosition = 0, UnderlineThickness = 1;
		bool bHasEllipsis = false;
	};

	struct FBitmap
	{
		std::int32_t Width = 0, Height = 0, BearingX = 0, BearingY = 0, Advance = 0;
		// A8 coverage, or premultiplied sRGB RGBA8 for color glyphs. Rows are tightly packed.
		bool bColor = false;
		std::vector<std::uint8_t> Pixels;
	};

	struct FShapeOptions
	{
		std::int32_t PixelSize = 16;
		std::string Language, Script;
		EDirection Direction = EDirection::Auto;
		bool bKerning = true;
		// Shape a UTF-8 byte range while retaining surrounding text for contextual forms.
		std::int64_t ByteOffset = 0, ByteLength = -1;
		// Optional immutable validated context. Providers may skip rescanning UTF-8 only when it owns Text.
		std::shared_ptr<const FTextLayout> Layout;
	};

	struct FShapedGlyph
	{
		std::uint32_t Glyph = 0, Cluster = 0;
		// Signed 26.6 pixel positions. Cluster is an offset into the original UTF-8 text.
		std::int32_t XAdvance = 0, YAdvance = 0, XOffset = 0, YOffset = 0;
	};

	struct FShapedText
	{
		std::vector<FShapedGlyph> Glyphs;
		EDirection Direction = EDirection::LeftToRight;
	};

	inline constexpr const char *ServiceId = "font";
	inline constexpr const char *ContractId = "veroscript.font";
	inline constexpr const char *ContractVersion = "0.1.2";
	inline constexpr const char *DefaultBackendId = "font-backend";
	class IFace
	{
	public:
		virtual ~IFace() = default;
		virtual const FFaceInfo &GetInfo() const noexcept = 0;
		virtual std::uint32_t GetGlyphIndex(char32_t Character) const = 0;
		virtual bool GetMetrics(std::int32_t PixelSize, FMetrics &Result, std::string &Error) const = 0;
		virtual bool Rasterize(std::uint32_t Glyph, std::int32_t PixelSize, FBitmap &Result,
							   std::string &Error) const = 0;
		virtual bool Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result,
						   std::string &Error) const = 0;
	};
	class IService
	{
	public:
		virtual ~IService() = default;
		virtual const char *GetBackendId() const noexcept = 0;
		virtual std::shared_ptr<IFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t FaceIndex,
											std::string &Error, std::int32_t NamedInstance = 0) const = 0;
		virtual bool GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t FaceIndex,
								   std::vector<FVariation> &Result, std::string &Error) const = 0;
	};
	// Null with an empty error means an optional service is not configured.
	// A configured provider must report failures through Error.
	using FServiceFactory = std::function<std::shared_ptr<IService>(const std::string &, std::string &)>;

	struct FResolvedGlyph
	{
		std::shared_ptr<IFace> Face;
		FShapedGlyph Glyph;
	};
	struct FResolvedText
	{
		std::vector<FResolvedGlyph> Glyphs;
		EDirection Direction = EDirection::LeftToRight;
	};
	// Retries missing UTF-8 clusters against the supplied fallback faces, retaining the selected faces.
	// Callers split paragraphs into script/direction runs before shaping mixed-direction text.
	bool ShapeWithFallback(const std::shared_ptr<IFace> &Primary, const std::vector<std::shared_ptr<IFace>> &Fallbacks,
						   std::string_view Text, const FShapeOptions &Options, FResolvedText &Result,
						   std::string &Error);

	bool ValidateFaceDescription(const FFontFace &Value, std::string &Error);
	bool Validate(const FBitmap &Value, std::string &Error);
	const std::vector<std::uint8_t> &GetDefaultFontData();
	inline constexpr const char *DefaultFontFamily = "Vero Sans";
} // namespace VeroScript::Font
