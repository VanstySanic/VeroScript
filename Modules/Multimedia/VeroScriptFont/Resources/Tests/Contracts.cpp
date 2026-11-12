// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFont.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
	using namespace VeroScript::Font;
	using FShape = std::function<bool(std::string_view, const FShapeOptions &, FShapedText &, std::string &)>;

	void Require(bool Value, const char *Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FMockFace final : public IFace
	{
	public:
		explicit FMockFace(FShape Shape) : OnShape(std::move(Shape))
		{
		}
		const FFaceInfo &GetInfo() const noexcept override
		{
			return Info;
		}
		std::uint32_t GetGlyphIndex(char32_t) const override
		{
			return 0;
		}
		bool GetMetrics(std::int32_t, FMetrics &, std::string &Error) const override
		{
			Error = "test.unused_metrics";
			return false;
		}
		bool Rasterize(std::uint32_t, std::int32_t, FBitmap &, std::string &Error) const override
		{
			Error = "test.unused_rasterize";
			return false;
		}
		bool Shape(std::string_view Text, const FShapeOptions &Options, FShapedText &Result,
		    std::string &Error) const override
		{
			++Calls;
			return OnShape(Text, Options, Result, Error);
		}
		mutable std::size_t Calls = 0;

	private:
		FShape OnShape;
		FFaceInfo Info;
	};

	FShapedGlyph Glyph(std::uint32_t Id, std::uint32_t Cluster)
	{
		const auto Position = static_cast<std::int32_t>(Cluster);
		return {Id, Cluster, (Position + 1) * 64, -Position * 64, Position + 3, -Position - 5};
	}

	std::shared_ptr<FMockFace> Face(FShapedText Value)
	{
		return std::make_shared<FMockFace>(
		    [Value = std::move(Value)](auto, const auto &, auto &Result, auto &Error)
		{
			Result = Value;
			Error.clear();
			return true;
		});
	}

	void CheckGlyph(const FResolvedGlyph &Actual, const std::shared_ptr<IFace> &ExpectedFace,
	    const FShapedGlyph &Expected)
	{
		const auto &Value = Actual.Glyph;
		Require(Actual.Face == ExpectedFace && Value.Glyph == Expected.Glyph && Value.Cluster == Expected.Cluster &&
		        Value.XAdvance == Expected.XAdvance && Value.YAdvance == Expected.YAdvance &&
		        Value.XOffset == Expected.XOffset && Value.YOffset == Expected.YOffset,
		    "resolved glyph identity, order or positions changed");
	}

	void CheckPreserved(const FResolvedText &Result, const std::shared_ptr<IFace> &Owner)
	{
		Require(Result.Direction == EDirection::RightToLeft && Result.Glyphs.size() == 1,
		    "failed shaping replaced the previous result");
		CheckGlyph(Result.Glyphs[0], Owner, Glyph(99, 0));
	}

	void CoveredRuns()
	{
		for (auto Direction : {EDirection::LeftToRight, EDirection::RightToLeft})
		{
			FShapedText Expected;
			Expected.Direction = Direction;
			for (std::uint32_t I = 0; I < 3; ++I)
			{
				const auto Cluster = Direction == EDirection::LeftToRight ? I : 2 - I;
				Expected.Glyphs.push_back(Glyph(Cluster + 10, Cluster));
			}
			auto Primary = Face(Expected);
			auto Fallback = std::make_shared<FMockFace>([](auto, const auto &, auto &, auto &) -> bool
			{
				throw std::runtime_error("a covered run invoked a fallback face");
			});
			FResolvedText Result;
			FShapeOptions Options;
			Options.Direction = Direction;
			std::string Error;
			Require(ShapeWithFallback(Primary, {Fallback}, "abc", Options, Result, Error) && Error.empty(),
			    "covered run failed");
			Require(Result.Direction == Direction && Result.Glyphs.size() == Expected.Glyphs.size() &&
			        Fallback->Calls == 0,
			    "covered run changed direction or consulted fallback");
			for (std::size_t I = 0; I < Expected.Glyphs.size(); ++I)
			{
				CheckGlyph(Result.Glyphs[I], Primary, Expected.Glyphs[I]);
			}
		}
	}

	void MissingWithoutFallback()
	{
		FShapedText Expected{{Glyph(0, 0), Glyph(8, 1), Glyph(0, 2)}, EDirection::LeftToRight};
		auto Primary = Face(Expected);
		for (const auto &Fallbacks : {std::vector<std::shared_ptr<IFace>>{},
		         std::vector<std::shared_ptr<IFace>>{nullptr, Primary}})
		{
			FResolvedText Result;
			std::string Error;
			Require(ShapeWithFallback(Primary, Fallbacks, "abc", {}, Result, Error) && Error.empty(),
			    "unresolved run failed without usable fallbacks");
			Require(Result.Glyphs.size() == Expected.Glyphs.size(), "missing glyphs were discarded");
			for (std::size_t I = 0; I < Expected.Glyphs.size(); ++I)
			{
				CheckGlyph(Result.Glyphs[I], Primary, Expected.Glyphs[I]);
			}
		}
	}

	void ProgressiveFallback()
	{
		for (auto Direction : {EDirection::LeftToRight, EDirection::RightToLeft})
		{
			const std::vector<std::uint32_t> Order = Direction == EDirection::LeftToRight
			    ? std::vector<std::uint32_t>{0, 1, 2}
			    : std::vector<std::uint32_t>{2, 1, 0};
			FShapedText Missing;
			Missing.Direction = Direction;
			for (auto Cluster : Order)
			{
				Missing.Glyphs.push_back(Glyph(0, Cluster));
			}
			auto Primary = Face(Missing);
			std::vector<std::shared_ptr<IFace>> Fallbacks;
			for (std::size_t Stage = 0; Stage < Order.size(); ++Stage)
			{
				Fallbacks.push_back(std::make_shared<FMockFace>(
				    [Direction, Order, Stage](auto Text, const auto &Options, auto &Result, auto &Error)
				{
					const auto Start = *std::min_element(Order.begin() + Stage, Order.end());
					Require(Text == "abc" && Options.ByteOffset == Start &&
					        Options.ByteLength == static_cast<std::int64_t>(Order.size() - Stage) &&
					        Options.Direction == Direction,
					    "fallback lost contextual text, range or direction");
					Result = {};
					Result.Direction = Direction;
					for (auto I = Stage; I < Order.size(); ++I)
					{
						Result.Glyphs.push_back(Glyph(I == Stage ? Order[I] + 10 : 0, Order[I]));
					}
					Error.clear();
					return true;
				}));
			}
			FResolvedText Result;
			FShapeOptions Options;
			Options.Direction = Direction;
			std::string Error;
			Require(ShapeWithFallback(Primary, Fallbacks, "abc", Options, Result, Error) && Error.empty(),
			    "progressive fallback failed");
			Require(Result.Direction == Direction && Result.Glyphs.size() == Order.size(),
			    "progressive fallback changed glyph count or direction");
			for (std::size_t I = 0; I < Order.size(); ++I)
			{
				CheckGlyph(Result.Glyphs[I], Fallbacks[I], Glyph(Order[I] + 10, Order[I]));
			}
		}
	}

	void Utf8WindowsAndFailures()
	{
		const std::string Text = "A\xc3\xa9Z";
		auto Primary = Face({{Glyph(0, 1)}, EDirection::LeftToRight});
		auto Fallback = std::make_shared<FMockFace>([&Text](auto Context, const auto &Options, auto &Result, auto &Error)
		{
			Require(Context == Text && Options.ByteOffset == 1 && Options.ByteLength == 2 &&
			        Options.PixelSize == 24 && Options.Language == "fr" && Options.Script == "Latn" &&
			        !Options.bKerning,
			    "UTF-8 fallback lost byte range, context or options");
			Result = FShapedText{{Glyph(42, 1)}, EDirection::LeftToRight};
			Error.clear();
			return true;
		});
		FShapeOptions Options;
		Options.ByteOffset = 1;
		Options.ByteLength = 2;
		Options.PixelSize = 24;
		Options.Language = "fr";
		Options.Script = "Latn";
		Options.bKerning = false;
		FResolvedText Result;
		std::string Error;
		Require(ShapeWithFallback(Primary, {Fallback}, Text, Options, Result, Error) && Error.empty() &&
		        Result.Glyphs.size() == 1,
		    "UTF-8 window failed");
		CheckGlyph(Result.Glyphs[0], Fallback, Glyph(42, 1));

		Result = FResolvedText{{{Primary, Glyph(99, 0)}}, EDirection::RightToLeft};
		const auto Calls = Primary->Calls;
		auto Invalid = Options;
		Invalid.ByteOffset = 2;
		Invalid.ByteLength = 1;
		Require(!ShapeWithFallback(Primary, {}, Text, Invalid, Result, Error) && !Error.empty() &&
		        Primary->Calls == Calls,
		    "UTF-8 continuation-byte window reached the backend");
		CheckPreserved(Result, Primary);
		Require(!ShapeWithFallback(Primary, {}, std::string("\xc0\x80", 2), {}, Result, Error) &&
		        !Error.empty() && Primary->Calls == Calls,
		    "invalid UTF-8 reached the backend");
		CheckPreserved(Result, Primary);

		for (auto Cluster : {0u, 2u, 3u})
		{
			for (auto Id : {0u, 7u})
			{
				auto Bad = Face({{Glyph(Id, Cluster)}, EDirection::LeftToRight});
				Require(!ShapeWithFallback(Bad, {}, Text, Options, Result, Error) &&
				        Error == "font.invalid_shape_result",
				    "invalid cluster bypassed result validation");
				CheckPreserved(Result, Primary);
			}
		}

		auto Partial = Face({{Glyph(7, 0), Glyph(0, 1)}, EDirection::LeftToRight});
		auto Failure = std::make_shared<FMockFace>([](auto, const auto &, auto &, auto &Error)
		{
			Error = "test.shape_failed";
			return false;
		});
		Require(!ShapeWithFallback(Partial, {Failure}, "ab", {}, Result, Error) && Error == "test.shape_failed",
		    "fallback failure was ignored");
		CheckPreserved(Result, Primary);
		auto BadFallback = Face({{Glyph(55, 0)}, EDirection::LeftToRight});
		Require(!ShapeWithFallback(Partial, {BadFallback}, "ab", {}, Result, Error) &&
		        Error == "font.invalid_shape_result",
		    "fallback cluster outside its requested range was accepted");
		CheckPreserved(Result, Primary);
	}
} // namespace

int main()
{
	try
	{
		CoveredRuns();
		MissingWithoutFallback();
		ProgressiveFallback();
		Utf8WindowsAndFailures();
		std::cout << "Font native contract tests passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
