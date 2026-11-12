// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFont.h"
namespace VeroScript::FontBackend
{
	using namespace Font;
	class FFontBackendFace final : public Font::IFace
	{
	public:
		~FFontBackendFace() override;
		const FFaceInfo &GetInfo() const noexcept override;
		std::uint32_t GetGlyphIndex(char32_t Character) const override;
		bool GetMetrics(std::int32_t Size, FMetrics &Result, std::string &Error) const override;
		bool Rasterize(std::uint32_t Glyph, std::int32_t Size, FBitmap &Result, std::string &Error) const override;
		bool Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result,
				   std::string &Error) const override;
		static bool GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t FaceIndex,
								  std::vector<FVariation> &Result, std::string &Error);
		static std::shared_ptr<FFontBackendFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
											   std::string &Error, std::int32_t Instance = 0);

	private:
		struct FImpl;
		explicit FFontBackendFace(std::unique_ptr<FImpl> Value);
		std::unique_ptr<FImpl> Impl;
	};
} // namespace VeroScript::FontBackend
