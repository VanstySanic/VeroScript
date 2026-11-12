// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUi.h"
#include "VeroScriptFontLayout.h"
#include "../../Private/VeroScriptRmlUiPaint.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	// Exercise RmlUi's actual font mesh generation without opening a graphics device.
	class FFontRenderInterface final : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
		{
			return Next++;
		}
		void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
		{
		}
		void ReleaseGeometry(Rml::CompiledGeometryHandle) override
		{
		}
		Rml::TextureHandle LoadTexture(Rml::Vector2i &Dimensions, const Rml::String &) override
		{
			Dimensions = {};
			return 0;
		}
		Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
		{
			return Next++;
		}
		void ReleaseTexture(Rml::TextureHandle) override
		{
		}
		void EnableScissorRegion(bool) override
		{
		}
		void SetScissorRegion(Rml::Rectanglei) override
		{
		}

	private:
		std::uintptr_t Next = 1;
	};
	struct FFontMeshScope
	{
		FFontRenderInterface Renderer;
		Rml::RenderManager Manager{&Renderer};
		~FFontMeshScope()
		{
			// Atlas callback textures must release their borrowed manager before it is destroyed,
			// including when an assertion unwinds the regression scenario.
			Rml::GetFontEngineInterface()->ReleaseFontResources();
		}
	};

	std::vector<std::pair<float, float>> MeshPositions(const Rml::TexturedMeshList &Meshes)
	{
		std::vector<std::pair<float, float>> Result;
		for (const auto &Mesh : Meshes)
		{
			for (const auto &Vertex : Mesh.mesh.vertices)
			{
				Result.emplace_back(Vertex.position.x, Vertex.position.y);
			}
		}
		std::sort(Result.begin(), Result.end());
		return Result;
	}

	void CheckFontInitializationRecovery()
	{
		using namespace VeroScript;
		class FFailingFonts final : public Font::IService
		{
		public:
			explicit FFailingFonts(bool Throw) : bThrow(Throw)
			{
			}
			const char *GetBackendId() const noexcept override
			{
				return "failing-font-test";
			}
			std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &, std::int32_t, std::string &Error,
			    std::int32_t) const override
			{
				Error = "test.font_load_failed";
				return {};
			}
			bool GetVariations(const std::vector<std::uint8_t> &, std::int32_t, std::vector<Font::FVariation> &,
			    std::string &Error) const override
			{
				if (bThrow)
				{
					throw std::runtime_error("test.font_initialization_exception");
				}
				Error = "test.font_initialization_failed";
				return false;
			}

		private:
			bool bThrow;
		};
		for (bool Throw : {false, true})
		{
			RmlUi::FOptions Options;
			Options.Fonts = std::make_shared<FFailingFonts>(Throw);
			Options.TextLayouts = FontBackend::CreateTextLayoutService();
			std::weak_ptr<Font::IService> Service = Options.Fonts;
			std::string Error;
			bool Failed = false;
			try
			{
				Failed = !RmlUi::CreateBackend(Options, Error) && !Error.empty();
			}
			catch (const std::runtime_error &Exception)
			{
				Failed = Throw && std::string(Exception.what()) == "test.font_initialization_exception";
			}
			Require(Failed, "font initialization failure was not reported");
			Options.Fonts.reset();
			Require(Service.expired(), "failed font initialization retained a service and poisoned the runtime");
			auto Recovered = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
			Require(Recovered && Recovered->Close(Error), "font initialization prevented runtime recovery: " + Error);
		}
	}

	void CheckTextLayoutServices()
	{
		using namespace VeroScript;
		class FLayouts final : public Font::ITextLayoutService
		{
		public:
			const char *GetBackendId() const noexcept override
			{
				return "alternate-layout";
			}
			std::shared_ptr<const Font::ITextLayoutAnalysis> Analyze(std::string Text, Font::EDirection Direction,
			    std::string &Error) const override
			{
				++Calls;
				return Inner->Analyze(std::move(Text), Direction, Error);
			}
			mutable int Calls = 0;

		private:
			std::shared_ptr<Font::ITextLayoutService> Inner = FontBackend::CreateTextLayoutService();
		};
		std::string Error;
		auto Options = GuiTestResourceOptions();
		Options.TextLayouts.reset();
		Require(!RmlUi::CreateBackend(Options, Error) && Error == "rmlui.text_layout_service_required",
		    "RmlUi silently supplied a fixed layout backend");
		auto First = std::make_shared<FLayouts>();
		auto Second = std::make_shared<FLayouts>();
		Options.TextLayouts = First;
		auto Backend = RmlUi::CreateBackend(Options, Error);
		auto Shared = RmlUi::CreateBackend(Options, Error);
		Require(Backend && Shared, "contexts could not share the same font/layout service instances: " + Error);
		auto Conflicting = Options;
		Conflicting.TextLayouts = Second;
		Require(!RmlUi::CreateBackend(Conflicting, Error) && Error == "rmlui.text_layout_backend_in_use",
		    "different layout instances with the same backend ID shared paragraph caches");
		Conflicting = Options;
		Conflicting.Fonts = FontBackend::CreateFontService();
		Require(!RmlUi::CreateBackend(Conflicting, Error) && Error == "rmlui.font_backend_in_use",
		    "different font instances with the same backend ID shared font families/atlases");
		auto Owner = std::make_shared<const Rml::String>("abc");
		Rml::Vector<Rml::TextBidiRun> Runs;
		for (int I = 0; I < 2; ++I)
		{
			Require(Rml::GetFontEngineInterface()->GetParagraphRuns(*Owner, Rml::Style::Direction::Auto, 0, 3, Runs, Owner),
			    "selected layout service could not analyze a paragraph");
		}
		Require(First->Calls == 1 && Second->Calls == 0, "paragraph analysis ignored its selected layout service or immutable cache");
		Require(Shared->Close(Error) && Backend->Close(Error), Error);
		Options.TextLayouts = Second;
		Backend = RmlUi::CreateBackend(Options, Error);
		Require(Backend && Rml::GetFontEngineInterface()->GetParagraphRuns(*Owner, Rml::Style::Direction::Auto, 0, 3, Runs, Owner) &&
		        Second->Calls == 1 && First->Calls == 1,
		    "restarting with another layout configuration reused a stale analysis");
		Require(Backend->Close(Error), Error);
	}

	void CheckClusterFallbackDoesNotSkipText()
	{
		using namespace VeroScript;
		class FClusterFace final : public Font::IFace
		{
		public:
			explicit FClusterFace(std::shared_ptr<Font::IFace> Value) : Face(std::move(Value))
			{
			}
			const Font::FFaceInfo &GetInfo() const noexcept override
			{
				return Face->GetInfo();
			}
			std::uint32_t GetGlyphIndex(char32_t C) const override
			{
				return Face->GetGlyphIndex(C);
			}
			bool GetMetrics(std::int32_t Size, Font::FMetrics &Out, std::string &Error) const override
			{
				return Face->GetMetrics(Size, Out, Error);
			}
			bool Rasterize(std::uint32_t Glyph, std::int32_t Size, Font::FBitmap &Out,
			    std::string &Error) const override
			{
				return Face->Rasterize(Glyph, Size, Out, Error);
			}
			bool Shape(std::string_view Text, const Font::FShapeOptions &Options, Font::FShapedText &Out,
			    std::string &Error) const override
			{
				if (!Face->Shape(Text, Options, Out, Error))
				{
					return false;
				}
				// A two-codepoint cluster can be a single missing glyph followed by valid text.
				if (Text == "a\u0301x" && Out.Glyphs.size() == 2)
				{
					Out.Glyphs[0].Glyph = 0;
				}
				return true;
			}

		private:
			std::shared_ptr<Font::IFace> Face;
		};
		class FClusterFonts final : public Font::IService
		{
		public:
			const char *GetBackendId() const noexcept override
			{
				return "cluster-test";
			}
			std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
			    std::string &Error, std::int32_t Instance) const override
			{
				auto Face = Service->Load(Bytes, Index, Error, Instance);
				return Face ? std::make_shared<FClusterFace>(std::move(Face)) : nullptr;
			}
			bool GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
			    std::vector<Font::FVariation> &Out, std::string &Error) const override
			{
				return Service->GetVariations(Bytes, Index, Out, Error);
			}

		private:
			std::shared_ptr<Font::IService> Service = FontBackend::CreateFontService();
		};
		RmlUi::FOptions Options;
		Options.Fonts = std::make_shared<FClusterFonts>();
		Options.TextLayouts = FontBackend::CreateTextLayoutService();
		Options.ReadResource = [](const auto &, auto &Bytes, auto &)
		{
			Bytes = Font::GetDefaultFontData();
			return true;
		};
		std::string Error;
		auto Backend = RmlUi::CreateBackend(Options, Error);
		Require(Backend != nullptr, Error);
		Font::FFontFace Fallback;
		Fallback.Source = "memory:cluster";
		Fallback.Family = "Cluster fallback";
		Fallback.bFallback = true;
		Require(Backend->LoadFont(Fallback, Error), Error);
		auto *Engine = Rml::GetFontEngineInterface();
		const auto Face = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
		    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
		Require(Face != 0, "missing cluster test font");
		const Rml::String Language = "en";
		Rml::TextShapingContext Context{Language, Rml::Style::Direction::Ltr, Rml::Style::FontKerning::Auto, 0};
		const int ClusterWidth = Engine->GetStringWidth(Face, "a\u0301", Context);
		const int SuffixWidth = Engine->GetStringWidth(Face, "x", Context);
		const int FullWidth = Engine->GetStringWidth(Face, "a\u0301x", Context);
		Require(ClusterWidth > 0 && SuffixWidth > 0 && FullWidth > ClusterWidth + SuffixWidth / 2,
		    "fallback skipped a valid glyph following a multi-codepoint cluster");
		Require(Backend->Close(Error), Error);
	}

	void CheckMissingGlyphReplacement()
	{
		using namespace VeroScript;
		std::string Error;
		const auto Path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
		    "VeroScriptFontBackend/ThirdParty/HarfBuzz/perf/fonts/Roboto-Regular.ttf";
		std::ifstream File(Path, std::ios::binary);
		std::vector<std::uint8_t> Bytes((std::istreambuf_iterator<char>(File)), {});
		auto Native = FontBackend::CreateFontService()->Load(Bytes, 0, Error);
		Require(Native && Native->GetGlyphIndex(U'\ufffd') != 0, "missing replacement glyph in the test font");
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(Backend != nullptr, Error);
		Gui::FFontFace Description;
		Description.Source = Path.generic_string();
		Description.Family = "Replacement font";
		Require(Backend->LoadFont(Description, Error), Error);
		auto *Engine = Rml::GetFontEngineInterface();
		const auto Face = Engine->GetFontFaceHandle("replacement font", Rml::Style::FontStyle::Normal,
		    Rml::Style::FontWeight::Normal, 24);
		Require(Face != 0, "missing replacement test font");
		const Rml::String Language = "en";
		const Rml::TextShapingContext Context{Language, Rml::Style::Direction::Ltr, Rml::Style::FontKerning::Auto, 0};
		Require(Engine->GetStringWidth(Face, "\U000f0000", Context) > 0,
		    "unsupported character disappeared instead of using the replacement glyph");
		Require(Backend->Close(Error), Error);
	}

	void CheckFontFallback()
	{
		using namespace VeroScript;
		std::string Error;
		auto Fonts = FontBackend::CreateFontService();
		std::ifstream File(std::filesystem::u8path(VS_GUI_ARABIC_FONT), std::ios::binary);
		std::vector<std::uint8_t> Bytes((std::istreambuf_iterator<char>(File)), {});
		auto Arabic = Fonts->Load(Bytes, 0, Error);
		auto Latin = Fonts->Load(Font::GetDefaultFontData(), 0, Error);
		Require(Arabic && Latin, "fallback font fixtures: " + Error);
		const std::string Text = "سلام";
		Font::FShapeOptions Options;
		Options.Direction = Font::EDirection::RightToLeft;
		Options.Language = "ar";
		Font::FShapedText Direct;
		Font::FResolvedText Resolved;
		Require(Arabic->Shape(Text, Options, Direct, Error) &&
		        Font::ShapeWithFallback(Latin, {Arabic}, Text, Options, Resolved, Error),
		    Error);
		Require(Direct.Glyphs.size() == Resolved.Glyphs.size(), "fallback lost Arabic ligatures");
		for (std::size_t I = 0; I < Direct.Glyphs.size(); ++I)
		{
			const auto &A = Direct.Glyphs[I];
			const auto &B = Resolved.Glyphs[I];
			Require(B.Face == Arabic && A.Glyph == B.Glyph.Glyph && A.Cluster == B.Glyph.Cluster &&
			        A.XAdvance == B.Glyph.XAdvance,
			    "fallback lost contextual forms or cluster positions");
		}
		Options.ByteOffset = 2;
		Options.ByteLength = 4;
		Require(Arabic->Shape(Text, Options, Direct, Error), Error);
		Require(!Direct.Glyphs.empty() && Direct.Glyphs.front().Cluster >= 2, "range lost original UTF-8 clusters");
		Options.ByteOffset = 1;
		Require(!Arabic->Shape(Text, Options, Direct, Error), "non-boundary UTF-8 range accepted");
	}

	void CheckRmlUiFontFallback()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(Backend != nullptr, Error);
		Font::FFontFace Fallback;
		Fallback.Source = VS_GUI_ARABIC_FONT;
		Fallback.Family = "Arabic fallback regression";
		Fallback.bFallback = true;
		Require(Backend->LoadFont(Fallback, Error), Error);
		auto *Engine = Rml::GetFontEngineInterface();
		const auto Primary = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
		    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
		const auto Direct = Engine->GetFontFaceHandle("arabic fallback regression", Rml::Style::FontStyle::Normal,
		    Rml::Style::FontWeight::Normal, 24);
		Require(Primary && Direct, "missing fallback regression font handles");
		{
			FFontMeshScope Rendering;
			auto &Manager = Rendering.Manager;
			const Rml::String Language = "ar";
			for (auto Direction : {Rml::Style::Direction::Ltr, Rml::Style::Direction::Rtl})
			{
				for (const Rml::String Text : {"سلام", "سَلَام", "لا"})
				{
					const Rml::TextShapingContext Context{Language, Direction, Rml::Style::FontKerning::Normal, .5f};
					Rml::TexturedMeshList Actual, Expected;
					// Generate before measuring: the first shape must populate and regenerate its atlas.
					const int Width = Engine->GenerateString(Manager, Primary, 0, Text, {0, 0}, {255, 255, 255, 255},
					    1, Context, Actual);
					const int DirectWidth = Engine->GenerateString(Manager, Direct, 0, Text, {0, 0}, {255, 255, 255, 255},
					    1, Context, Expected);
					Require(Width > 0 && Width == DirectWidth && Width == Engine->GetStringWidth(Primary, Text, Context),
					    "RmlUi fallback lost contextual advances, ligatures or letter spacing");
					const auto Positions = MeshPositions(Actual);
					Require(!Positions.empty() && Positions == MeshPositions(Expected),
					    "RmlUi fallback geometry differs from the selected font's direct geometry");
					const auto Version = Engine->GetVersion(Primary);
					Actual.clear();
					Require(Engine->GenerateString(Manager, Primary, 0, Text, {0, 0}, {255, 255, 255, 255},
					            1, Context, Actual) == Width &&
					        Engine->GetVersion(Primary) == Version,
					    "a repeated fallback shape invalidated an unchanged atlas");
				}
			}
		}
		Require(Backend->Close(Error), Error);
	}

	void CheckFontAtlasIdentityAndPositions()
	{
		using namespace VeroScript;
		struct FProbe
		{
			int PrimaryShapes = 0, FallbackMetrics = 0, FallbackShapes = 0, FallbackRasterizations = 0;
			bool bOversizedFallback = false;
		};
		class FFace final : public Font::IFace
		{
		public:
			FFace(bool Fallback, std::shared_ptr<FProbe> Counts) : bFallback(Fallback), Probe(std::move(Counts))
			{
			}
			const Font::FFaceInfo &GetInfo() const noexcept override
			{
				return Info;
			}
			std::uint32_t GetGlyphIndex(char32_t C) const override
			{
				return (bFallback ? C == U'α' : C == U'a' || C == U' ') ? 1 : 0;
			}
			bool GetMetrics(std::int32_t, Font::FMetrics &Out, std::string &Error) const override
			{
				if (bFallback)
				{
					++Probe->FallbackMetrics;
				}
				Out = {};
				Out.Ascent = 8;
				Out.Descent = 2;
				Out.LineSpacing = 10;
				Out.XHeight = 5;
				Error.clear();
				return true;
			}
			bool Rasterize(std::uint32_t Glyph, std::int32_t, Font::FBitmap &Out, std::string &Error) const override
			{
				if (bFallback)
				{
					++Probe->FallbackRasterizations;
				}
				if (Glyph != 1)
				{
					Error = "test.invalid_glyph";
					return false;
				}
				Out = {};
				Out.Width = bFallback ? (Probe->bOversizedFallback ? 1025 : 2) : 1;
				Out.Height = bFallback ? 3 : 2;
				Out.BearingY = Out.Height;
				Out.Advance = bFallback ? 7 : 2;
				Out.Pixels.assign(Out.Width * Out.Height, 255);
				Error.clear();
				return true;
			}
			bool Shape(std::string_view Text, const Font::FShapeOptions &Options, Font::FShapedText &Out,
			    std::string &Error) const override
			{
				if (bFallback)
				{
					++Probe->FallbackShapes;
				}
				else
				{
					++Probe->PrimaryShapes;
				}
				Out = {};
				Out.Direction = Options.Direction == Font::EDirection::Auto ? Font::EDirection::LeftToRight : Options.Direction;
				const auto End = Options.ByteLength < 0 ? Text.size() : Options.ByteOffset + Options.ByteLength;
				if (!bFallback && Options.ByteOffset == 0 && End == Text.size() && Text == "a\u0301a")
				{
					// Keep the combining mark attached to the first glyph's cluster.
					Out.Glyphs = {{1, 0, 128, 0, 0, 0}, {1, 0, 0, 0, -64, 128}, {1, 3, 128, 0, 0, 0}};
				}
				else
				{
					for (std::size_t I = static_cast<std::size_t>(Options.ByteOffset); I < End;)
					{
						const auto Cluster = static_cast<std::uint32_t>(I);
						const auto Lead = static_cast<unsigned char>(Text[I++]);
						const unsigned Count = Lead < 0x80 ? 0 : Lead < 0xe0 ? 1
						    : Lead < 0xf0                                    ? 2
						                                                     : 3;
						char32_t C = Count ? Lead & (0x7f >> Count) : Lead;
						for (unsigned J = 0; J < Count; ++J)
						{
							C = (C << 6) | (static_cast<unsigned char>(Text[I++]) & 63);
						}
						Out.Glyphs.push_back({GetGlyphIndex(C), Cluster, (bFallback ? 7 : 2) * 64,
						    bFallback ? 64 : 0, bFallback ? 64 : 0, bFallback ? 128 : 0});
					}
				}
				if (Out.Direction == Font::EDirection::RightToLeft)
				{
					std::reverse(Out.Glyphs.begin(), Out.Glyphs.end());
				}
				Error.clear();
				return true;
			}

		private:
			bool bFallback;
			std::shared_ptr<FProbe> Probe;
			Font::FFaceInfo Info;
		};
		class FFonts final : public Font::IService
		{
		public:
			explicit FFonts(std::shared_ptr<FProbe> Counts) : Probe(std::move(Counts))
			{
			}
			const char *GetBackendId() const noexcept override
			{
				return "atlas-regression";
			}
			std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t,
			    std::string &Error, std::int32_t) const override
			{
				Error.clear();
				return std::make_shared<FFace>(!Bytes.empty() && Bytes.front() == 0xff, Probe);
			}
			bool GetVariations(const std::vector<std::uint8_t> &, std::int32_t, std::vector<Font::FVariation> &Out,
			    std::string &Error) const override
			{
				Out.clear();
				Error.clear();
				return true;
			}

		private:
			std::shared_ptr<FProbe> Probe;
		};
		auto Probe = std::make_shared<FProbe>();
		RmlUi::FOptions Options;
		Options.Fonts = std::make_shared<FFonts>(Probe);
		Options.TextLayouts = FontBackend::CreateTextLayoutService();
		Options.ReadResource = [](const auto &, auto &Bytes, auto &)
		{
			Bytes = {0xff};
			return true;
		};
		std::string Error;
		auto Backend = RmlUi::CreateBackend(Options, Error);
		Require(Backend != nullptr, Error);
		Font::FFontFace Fallback;
		Fallback.Source = "memory:atlas-regression";
		Fallback.Family = "Atlas fallback";
		Fallback.bFallback = true;
		Require(Backend->LoadFont(Fallback, Error), Error);
		auto *Engine = Rml::GetFontEngineInterface();
		const auto Face = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
		    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
		Require(Face != 0, "missing atlas regression font");
		{
			FFontMeshScope Rendering;
			auto &Manager = Rendering.Manager;
			Rml::TexturedMeshList Meshes;
			const Rml::String Language = "en";
			const Rml::TextShapingContext Context{Language, Rml::Style::Direction::Ltr, Rml::Style::FontKerning::Normal, .5f};
			Require(Engine->GenerateString(Manager, Face, 0, "a", {0, 0}, {255, 255, 255, 255}, 1, Context, Meshes) == 3 &&
			        Probe->FallbackMetrics == 0 && Probe->FallbackShapes == 0 && Probe->FallbackRasterizations == 0,
			    "covered text initialized or used a fallback face");
			const auto Before = Engine->GetVersion(Face);
			Meshes.clear();
			Require(Engine->GenerateString(Manager, Face, 0, "aαα", {0, 0}, {255, 255, 255, 255}, 1, Context, Meshes) == 18 &&
			        Engine->GetStringWidth(Face, "aαα", Context) == 18,
			    "fallback advances or fractional letter spacing were lost");
			std::vector<std::pair<float, float>> Origins;
			for (const auto &Mesh : Meshes)
			{
				for (std::size_t I = 0; I < Mesh.mesh.vertices.size(); I += 4)
				{
					Origins.emplace_back(Mesh.mesh.vertices[I].position.x, Mesh.mesh.vertices[I].position.y);
				}
			}
			std::sort(Origins.begin(), Origins.end());
			Require(Origins == std::vector<std::pair<float, float>>{{0, -2}, {4, -5}, {11, -6}},
			    "font identity collision or shaped glyph offsets/vertical advances changed geometry");
			Require(Engine->GetVersion(Face) > Before && Probe->FallbackMetrics == 0 && Probe->FallbackRasterizations == 1,
			    "new fallback glyph did not update only the active atlas");
			const auto Stable = Engine->GetVersion(Face);
			Meshes.clear();
			Engine->GenerateString(Manager, Face, 0, "aαα", {0, 0}, {255, 255, 255, 255}, 1, Context, Meshes);
			Require(Engine->GetVersion(Face) == Stable && Probe->FallbackRasterizations == 1,
			    "repeated shaping regenerated a stable glyph atlas");
			Require(Engine->GetStringWidth(Face, "?", Context) > 0, "unresolved glyph lost its replacement advance");
			// More than eight live paragraphs must share width/layout shaping results.
			std::vector<Rml::SharedPtr<const Rml::String>> Paragraphs;
			for (int I = 0; I < 32; ++I)
			{
				Paragraphs.push_back(Rml::MakeShared<const Rml::String>("a"));
			}
			for (const auto &Paragraph : Paragraphs)
			{
				auto Cached = Context;
				Cached.paragraph_owner = Paragraph;
				Require(Engine->GetStringWidth(Face, *Paragraph, Cached) == 3, "paragraph width changed");
			}
			const int Shapes = Probe->PrimaryShapes;
			for (const auto &Paragraph : Paragraphs)
			{
				auto Cached = Context;
				Cached.paragraph_owner = Paragraph;
				Rml::TextLineLayout Layout;
				Require(Engine->GetStringLayout(Face, *Paragraph, Cached, Layout) && Layout.width == 2.5f,
				    "cached paragraph layout changed");
			}
			Require(Probe->PrimaryShapes == Shapes, "live paragraph measurement cache thrashed");

			for (const float Spacing : {2.f, 0.f, -.5f, -4.f})
			{
				auto SpacedContext = Context;
				SpacedContext.letter_spacing = Spacing;
				const Rml::String Text = "a\u0301a";
				Rml::TextLineLayout Layout;
				const auto Width = std::max(0.f, 4 + 2 * Spacing);
				Require(Engine->GetStringLayout(Face, Text, SpacedContext, Layout) && Layout.width == Width,
				    "combining-cluster layout inserted spacing between attached glyphs");
				Meshes.clear();
				Require(Engine->GenerateString(Manager, Face, 0, Text, {0, 0}, {255, 255, 255, 255}, 1,
				            SpacedContext, Meshes) == static_cast<int>(std::round(Width)) &&
				        Engine->GetStringWidth(Face, Text, SpacedContext) == static_cast<int>(std::round(Width)),
				    "combining-cluster mesh width differs from layout");
				Origins.clear();
				for (const auto &Mesh : Meshes)
				{
					for (std::size_t I = 0; I < Mesh.mesh.vertices.size(); I += 4)
					{
						Origins.emplace_back(Mesh.mesh.vertices[I].position.x, Mesh.mesh.vertices[I].position.y);
					}
				}
				std::sort(Origins.begin(), Origins.end());
				std::vector<std::pair<float, float>> Expected{{0, -2}, {1, -4}, {std::round(2 + Spacing), -2}};
				std::sort(Expected.begin(), Expected.end());
				Require(Origins == Expected, "letter spacing moved an attached glyph or the following cluster");
				for (const auto Offset : {0, 3, 4})
				{
					const auto Caret = std::find_if(Layout.carets.begin(), Layout.carets.end(),
					    [&](const auto &Value)
					{
						return Value.byte_offset == Offset;
					});
					const auto X = Offset == 0 ? 0.f : Offset == 3 ? 2 + Spacing
					                                               : 4 + 2 * Spacing;
					Require(Caret != Layout.carets.end() && Caret->x == X,
					    "combining-cluster caret differs from the rendered pen position");
				}
			}

			Probe->bOversizedFallback = true;
			const auto Limited = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
			    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 25);
			Require(Limited != 0, "missing atlas limit regression font");
			for (int Attempt = 0; Attempt < 2; ++Attempt)
			{
				const auto BeforeFailure = Engine->GetVersion(Limited);
				bool Failed = false;
				Meshes.clear();
				try
				{
					Engine->GenerateString(Manager, Limited, 0, "α", {0, 0}, {255, 255, 255, 255}, 1, Context, Meshes);
				}
				catch (const std::runtime_error &Exception)
				{
					Failed = std::string(Exception.what()) == "rmlui.font_atlas_failed";
				}
				Require(Failed && Engine->GetVersion(Limited) > BeforeFailure && Meshes.empty(),
				    "oversized fallback atlas failure was swallowed or left a clean partial atlas");
			}
			Require(Probe->FallbackRasterizations == 2, "atlas retries rasterized an unchanged fallback glyph");

			class FOversizedEffect final : public Rml::FontEffect
			{
			public:
				bool HasUniqueTexture() const override
				{
					return true;
				}
				bool GetGlyphMetrics(Rml::Vector2i &, Rml::Vector2i &Dimensions, const Rml::FontGlyph &) const override
				{
					Dimensions = {1025, 1};
					return true;
				}
			};
			auto OversizedEffect = std::make_shared<FOversizedEffect>();
			OversizedEffect->SetFingerprint(1);
			for (int Attempt = 0; Attempt < 2; ++Attempt)
			{
				bool Failed = false;
				try
				{
					Engine->PrepareFontEffects(Face, {OversizedEffect});
				}
				catch (const std::runtime_error &Exception)
				{
					Failed = std::string(Exception.what()) == "rmlui.font_atlas_failed";
				}
				Require(Failed, "failed font effect retained an incomplete layer or atlas cache entry");
			}
			Require(Engine->PrepareFontEffects(Face, {std::make_shared<Rml::FontEffect>()}) == 1,
			    "failed font effects published incomplete layer configurations");
		}
		{
			struct FTextLayoutScope
			{
				FFontRenderInterface Renderer;
				Rml::Context *Context = nullptr;
				~FTextLayoutScope()
				{
					if (Context)
					{
						Rml::GetFontEngineInterface()->ReleaseFontResources();
						Rml::RemoveContext("font-line-measure-regression");
					}
				}
			} Scope;
			Scope.Context = Rml::CreateContext("font-line-measure-regression", {20000, 100}, &Scope.Renderer);
			Require(Scope.Context != nullptr, "could not create font line measurement context");
			auto *Page = Scope.Context->LoadDocumentFromMemory("<rml><head><style>body { font-family: Vero Sans; font-size:24px; } "
			                                                   "span { white-space:nowrap; }</style></head><body><span id='measure'></span></body></rml>");
			Require(Page != nullptr, "could not create font line measurement document");
			auto *Parent = Page->GetElementById("measure");
			auto Node = Page->CreateTextNode("a");
			auto *Text = static_cast<Rml::ElementText *>(Node.get());
			Parent->AppendChild(std::move(Node));
			Page->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
			Scope.Context->Update();
			Rml::String ManyWords = "a";
			for (int I = 0; I < 2048; ++I)
			{
				ManyWords += " a";
			}
			Text->SetText(ManyWords);
			Probe->PrimaryShapes = 0;
			Rml::String Line;
			int Consumed = 0;
			float Width = 0;
			Require(Text->GenerateLine(Line, Consumed, Width, 0, std::numeric_limits<float>::max(), 0, true, true, false) &&
			        Line == ManyWords && Consumed == static_cast<int>(ManyWords.size()) && Width > 0 && Probe->PrimaryShapes <= 2,
			    "unwrapped many-word line was reshaped for every token");
			Parent->SetProperty(Rml::PropertyId::WhiteSpace, Rml::Property(Rml::Style::WhiteSpace::Normal));
			for (auto Break : {Rml::Style::WordBreak::BreakAll, Rml::Style::WordBreak::BreakWord})
			{
				Parent->SetProperty(Rml::PropertyId::WordBreak, Rml::Property(Break));
				Text->SetText("&amp;aaaa");
				Scope.Context->Update();
				Require(!Text->GenerateLine(Line, Consumed, Width, 0, 1, 0, true, true, false) && Line == "&" && Consumed == 5,
				    "narrow word wrapping split an entity or lost its normalized paragraph range");
			}
			Parent->SetProperty(Rml::PropertyId::WhiteSpace, Rml::Property(Rml::Style::WhiteSpace::Prewrap));
			Parent->SetProperty(Rml::PropertyId::WordBreak, Rml::Property(Rml::Style::WordBreak::BreakAll));
			Text->SetText("a\r\n");
			Scope.Context->Update();
			const Rml::String Language;
			const Rml::TextShapingContext Shaping{Language};
			const float GlyphWidth = float(Engine->GetStringWidth(Text->GetFontFaceHandle(), "a", Shaping));
			Require(!Text->GenerateLine(Line, Consumed, Width, 0, GlyphWidth + 1, 2, true, true, false) &&
			        Line == "a" && Consumed == 1,
			    "word-break cut between CR and LF to satisfy trailing inline spacing");
			Require(!Text->GenerateLine(Line, Consumed, Width, 1, GlyphWidth + 1, 0, false, true, false) &&
			        Line == "\r\n" && Consumed == 2,
			    "wrapped CRLF did not remain one complete terminator");
		}
		Require(Backend->Close(Error), Error);
	}

	void CheckParagraphFontLayout()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(Backend != nullptr, Error);
		Font::FFontFace Fallback;
		Fallback.Source = VS_GUI_ARABIC_FONT;
		Fallback.Family = "Bidi fallback";
		Fallback.bFallback = true;
		Require(Backend->LoadFont(Fallback, Error), Error);
		auto Fonts = FontBackend::CreateFontService();
		auto Primary = Fonts->Load(Font::GetDefaultFontData(), 0, Error);
		std::ifstream File(std::filesystem::u8path(VS_GUI_ARABIC_FONT), std::ios::binary);
		std::vector<std::uint8_t> Bytes((std::istreambuf_iterator<char>(File)), {});
		auto Arabic = Fonts->Load(Bytes, 0, Error);
		Require(Primary && Arabic, "bidi font fixtures: " + Error);
		auto *Engine = Rml::GetFontEngineInterface();
		const auto Face = Engine->GetFontFaceHandle(Rml::StringUtilities::ToLower(Font::DefaultFontFamily),
		    Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, 24);
		Require(Face != 0, "missing bidi font handle");
		const Rml::String Language = "ar";
		auto Paragraph = std::make_shared<const Rml::String>("אבג 12 (abc) سلام xyz  ");
		auto Layout = Font::FTextLayout::Create(FontBackend::CreateTextLayoutService(), *Paragraph, Font::EDirection::Auto, Error);
		Require(Layout != nullptr, Error);
		const auto Start = static_cast<int>(Paragraph->find("12"));
		const Rml::String Line = Paragraph->substr(static_cast<std::size_t>(Start));
		Rml::TextShapingContext Context{Language, Rml::Style::Direction::Auto, Rml::Style::FontKerning::Normal, .25f};
		Context.paragraph_owner = Paragraph;
		Context.paragraph_text = *Paragraph;
		Context.paragraph_offset = Start;
		Context.paragraph_length = static_cast<int>(Line.size());
		Context.line_offset = Start;
		Context.line_length = static_cast<int>(Line.size());
		Font::FShapeOptions Options;
		Options.PixelSize = 24;
		Options.Language = Language;
		Font::FTextLine Expected;
		Require(Layout->ShapeLine(Primary, {Arabic}, Options, Start, Line.size(), .25, 10, Expected, Error), Error);
		Rml::TextLineLayout Actual;
		Require(Engine->GetStringLayout(Face, Line, Context, Actual) &&
		        Actual.base_direction == Rml::Style::Direction::Rtl && std::abs(Actual.width - Expected.Width) < .001 &&
		        Actual.carets.size() == Expected.Carets.size() && Actual.spans.size() == Expected.Clusters.size(),
		    "soft line was analyzed independently of its RTL paragraph");
		for (std::size_t I = 0; I < Actual.carets.size(); ++I)
		{
			Require(Actual.carets[I].byte_offset == Expected.Carets[I].ByteOffset - Start &&
			        std::abs(Actual.carets[I].x - Expected.Carets[I].X) < .001 &&
			        (Actual.carets[I].affinity == Rml::TextAffinity::Trailing) == Expected.Carets[I].bTrailing,
			    "bidi caret offsets, affinity or visual positions were lost");
		}
		for (std::size_t I = 0; I < Actual.spans.size(); ++I)
		{
			Require(Actual.spans[I].byte_begin == Expected.Clusters[I].ByteOffset - Start &&
			        Actual.spans[I].byte_end == Expected.Clusters[I].ByteOffset + Expected.Clusters[I].ByteLength - Start &&
			        std::abs(Actual.spans[I].left - Expected.Clusters[I].Left) < .001 &&
			        std::abs(Actual.spans[I].right - Expected.Clusters[I].Right) < .001,
			    "logical selection intervals lost their visual mapping");
		}
		Rml::TextLineLayout Standalone;
		const Rml::TextShapingContext Independent{Language, Rml::Style::Direction::Auto, Rml::Style::FontKerning::Normal, .25f};
		Require(Engine->GetStringLayout(Face, Line, Independent, Standalone) &&
		        Standalone.base_direction == Rml::Style::Direction::Ltr,
		    "auto direction did not use the paragraph's first strong character");
		Rml::Vector<Rml::TextBidiRun> Runs;
		Require(Engine->GetParagraphRuns(*Paragraph, Rml::Style::Direction::Auto, Start, static_cast<int>(Line.size()), Runs, Paragraph) &&
		        !Runs.empty(),
		    "paragraph visual runs were not available to inline layout");
		const auto FragmentOffset = static_cast<int>(Paragraph->find("abc"));
		auto FragmentContext = Context;
		FragmentContext.paragraph_offset = FragmentOffset;
		FragmentContext.paragraph_length = 3;
		Font::FTextLine FragmentExpected;
		Require(Layout->ShapeLine(Primary, {Arabic}, Options, Start, Line.size(), .25, 10, FragmentExpected, Error, FragmentOffset, 3), Error);
		Rml::TextLineLayout FragmentActual;
		Require(Engine->GetStringLayout(Face, "abc", FragmentContext, FragmentActual) &&
		        std::abs(FragmentActual.width - FragmentExpected.Width) < .001 &&
		        FragmentActual.base_direction == Rml::Style::Direction::Rtl,
		    "styled fragment did not retain the complete line's bidi context");
		auto MultiParagraph = std::make_shared<const Rml::String>("אב\u2028ABC\u2029DEF");
		for (const auto &Part : {std::pair<const char *, Rml::Style::Direction>{"ABC", Rml::Style::Direction::Rtl},
		         std::pair<const char *, Rml::Style::Direction>{"DEF", Rml::Style::Direction::Ltr}})
		{
			auto PartContext = Independent;
			PartContext.paragraph_owner = MultiParagraph;
			PartContext.paragraph_text = *MultiParagraph;
			PartContext.paragraph_offset = static_cast<int>(MultiParagraph->find(Part.first));
			PartContext.paragraph_length = 3;
			Rml::TextLineLayout PartLayout;
			Require(Engine->GetStringLayout(Face, Rml::String(Part.first), PartContext, PartLayout) && PartLayout.base_direction == Part.second,
			    "Unicode line and paragraph separators did not preserve/reset paragraph direction correctly");
		}
		{
			FFontMeshScope Rendering;
			Rml::TexturedMeshList Plain, Selected;
			const auto Width = Engine->GenerateString(Rendering.Manager, Face, 0, Line, {0, 0}, {255, 255, 255, 255}, 1, Context, Plain);
			auto SelectionContext = Context;
			SelectionContext.selection_begin = 0;
			SelectionContext.selection_end = 2;
			SelectionContext.selection_colour = {255, 0, 0, 255};
			Require(Engine->GenerateString(Rendering.Manager, Face, 0, Line, {0, 0}, {255, 255, 255, 255}, 1, SelectionContext, Selected) == Width &&
			        MeshPositions(Plain) == MeshPositions(Selected),
			    "selection changed mixed-direction glyph shaping");
			bool HasSelectionColour = false;
			for (const auto &Mesh : Selected)
			{
				for (const auto &Vertex : Mesh.mesh.vertices)
				{
					HasSelectionColour |= Vertex.colour.red == 255 && Vertex.colour.green == 0 && Vertex.colour.blue == 0;
				}
			}
			Require(HasSelectionColour, "selected logical range did not color its visual glyphs");
		}
		{
			Rml::ElementText Text("#text");
			Text.SetText("  a &amp;\t b   ");
			const auto &Normalized = Text.GetLayoutText(true);
			Require(Normalized.text == " a & b " && Normalized.source_to_text.size() == Text.GetText().size() + 1 &&
			        Normalized.source_to_text[2] == 1 && Normalized.source_to_text[4] == 3 &&
			        Normalized.source_to_text[8] == 3 && Normalized.source_to_text[9] == 4 &&
			        Normalized.source_to_text[11] == 5 && Normalized.source_to_text.back() == 7,
			    "paragraph normalization lost source mappings for entities or collapsed whitespace");
			Require(Text.GetLayoutText(false).text == " a &amp; b ", "decode mode reused a stale normalization cache");
			Text.SetText("a\u2028b\u2029c\u0085");
			const auto &Separators = Text.GetLayoutText(true);
			Require(Separators.text == Text.GetText() && Separators.paragraph_breaks == Rml::Vector<int>{8, 11},
			    "normalization did not distinguish a retained line separator from P1 paragraph separators");
			Text.SetText("z");
			Require(Text.GetLayoutText(true).text == "z", "text edits reused a stale normalization cache");
			for (const Rml::String Separator : {"\n", "\r\n", "\r", "\x1c", "\x1d", "\x1e", "\u0085", "\u2028", "\u2029"})
			{
				Require(Rml::StringUtilities::GetLineBreakLength(Separator.data(), Separator.data() + Separator.size()) == Separator.size(),
				    "Unicode line terminator identification lost its UTF-8 byte length");
			}
			Rml::FontEngineInterface Legacy;
			Rml::TextLineLayout Unsupported;
			Require(!Legacy.GetStringLayout(0, "z", Independent, Unsupported), "default font engine did not preserve the legacy layout path");
		}
		Require(Backend->Close(Error), Error);
	}

	void CheckNumericLocale()
	{
		struct FLocaleGuard
		{
			std::string Previous = std::setlocale(LC_NUMERIC, nullptr);
			~FLocaleGuard()
			{
				std::setlocale(LC_NUMERIC, Previous.c_str());
			}
		} Guard;
		// The adapter must not parse generated decimal strings using the host's numeric locale.
		if (!std::setlocale(LC_NUMERIC, "French_France.1252") && !std::setlocale(LC_NUMERIC, "fr_FR.UTF-8"))
		{
			return;
		}
		std::string Error;
		auto Backend = VeroScript::RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
		Require(Backend != nullptr, Error);
		auto Context = VeroScript::Gui::FContext::Create(std::move(Backend), {100, 100, 2}, Error);
		auto Root = VeroScript::Gui::FWidget::Create();
		Require(Root->Set(VeroScript::Gui::EProperty::Width,
		            VeroScript::Gui::FLength{VeroScript::Gui::ELengthUnit::Pixel, 12.5}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		VeroScript::Gui::FLayoutBox Box;
		Require(Root->GetLayout(Box, Error) && std::abs(Box.Border.Width - 12.5) < 0.01,
		    "numeric locale changed fractional layout");
		Require(Context->Close(Error), Error);
	}

	void CheckSnapshotValidation()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(Backend != nullptr, Error);
		std::vector<Gui::FBackendNode> Nodes(3);
		for (std::size_t I = 0; I < Nodes.size(); ++I)
		{
			Nodes[I].Id = I + 1;
			Nodes[I].Parent = I ? 1 : 0;
		}
		std::vector<Gui::FLayoutBox> Layout;
		double Time = 0;
		const auto CheckFailure = [&](const auto &Bad, const std::string &Expected)
		{
			Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(Time++, Layout, Error), Error);
			Require(!Backend->Synchronize({100, 100, 1}, Bad, Error) && Error == Expected,
			    "snapshot validation lost diagnostic precedence: " + Error);
			Require(!Backend->Update(Time++, Layout, Error), "invalid snapshot retained a ready frame");
			Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(Time++, Layout, Error),
			    "snapshot validation failure could not be retried: " + Error);
		};
		auto Bad = Nodes;
		Bad[1].Properties.Opacity = 2;
		Require(!Gui::ValidateResolvedProperties(Bad[1].Properties, Error), "invalid opacity fixture");
		const auto PropertyError = Error;
		CheckFailure(Bad, PropertyError);
		Bad = Nodes;
		Bad[1].Control.Maximum = -1;
		CheckFailure(Bad, "gui.invalid_control_options");
		Bad = Nodes;
		Bad[1].Id = 0;
		CheckFailure(Bad, "rmlui.invalid_snapshot");
		Bad = Nodes;
		Bad[2].Id = 2;
		Bad[2].Properties.Opacity = 2;
		CheckFailure(Bad, "rmlui.invalid_snapshot"); // Identity precedes properties on the same node.
		Bad[0].Properties.Opacity = 2;
		CheckFailure(Bad, PropertyError); // Earlier node properties precede a later identity error.
		for (const auto Parent : {std::uint64_t(999), std::uint64_t(0), std::uint64_t(3)})
		{
			Bad = Nodes;
			Bad[1].Parent = Parent;
			CheckFailure(Bad, "rmlui.invalid_snapshot");
		}
		Bad.assign(257, {});
		for (std::size_t I = 0; I < Bad.size(); ++I)
		{
			Bad[I].Id = I + 1;
			Bad[I].Parent = I;
		}
		CheckFailure(Bad, "rmlui.tree_limit");
		Bad.back().Properties.Opacity = 2;
		CheckFailure(Bad, PropertyError); // Property validation still precedes the node's depth limit.
		Require(Backend->Synchronize({100, 100, 1}, {}, Error) && Backend->Update(Time++, Layout, Error) &&
		        Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(Time++, Layout, Error), Error);
		Gui::FFontFace Face;
		Face.Source = (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		    "ThirdParty/RmlUi/Samples/assets/LatoLatin-Regular.ttf").generic_string();
		Face.Family = "Snapshot reload";
		Require(Backend->LoadFont(Face, Error) && !Backend->Update(Time++, Layout, Error),
		    "font reload retained the previous synchronized snapshot");
		Bad = Nodes;
		Bad[2].Id = 2;
		CheckFailure(Bad, "rmlui.invalid_snapshot");
		Require(Backend->Close(Error) && !Backend->Synchronize({100, 100, 1}, Nodes, Error),
		    "closed backend accepted a snapshot");
	}

	void CheckHiddenSnapshotOrdering()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(bool(Backend), Error);
		std::vector<Gui::FBackendNode> Nodes(7);
		for (std::size_t I = 0; I < Nodes.size(); ++I)
		{
			Nodes[I].Id = I + 1;
			Nodes[I].Properties.Width = {Gui::ELengthUnit::Pixel, 80};
			Nodes[I].Properties.Height = {Gui::ELengthUnit::Pixel, 20};
		}
		Nodes[0].Properties.Width.Value = 400;
		Nodes[0].Properties.Height.Value = 300;
		Nodes[1].Parent = 1;
		Nodes[2].Parent = 2;
		Nodes[3].Parent = 2;
		Nodes[4].Parent = 1;
		Nodes[5].Parent = 5;
		Nodes[6].Parent = 5;
		Nodes[3].Control.Kind = Nodes[6].Control.Kind = Gui::EControlKind::Popup;
		Nodes[3].Control.bOpen = Nodes[6].Control.bOpen = true;
		Nodes[1].Properties.Visibility = Gui::EVisibility::Hidden;
		std::vector<Gui::FLayoutBox> Layout;
		double Time = 0;
		const auto FindNode = [&](std::uint64_t Id) -> Gui::FBackendNode &
		{
			for (auto &Node : Nodes)
			{
				if (Node.Id == Id)
				{
					return Node;
				}
			}
			throw std::runtime_error("missing test node");
		};
		const auto Check = [&](bool A, bool B)
		{
			Require(Backend->Synchronize({400, 300, 1}, Nodes, Error) && Backend->Update(++Time, Layout, Error), Error);
			Require(Rml::GetNumContexts() == 1, "visibility fixture requires one context");
			auto *Context = Rml::GetContext(0);
			auto *Document = Context->GetDocument(0);
			Require(Document != nullptr, "missing document");
			Rml::ElementList Elements;
			Document->GetElementsByTagName(Elements, "vsgui-widget");
			for (const auto Id : {std::uint64_t(3), std::uint64_t(4), std::uint64_t(6), std::uint64_t(7)})
			{
				Rml::Element *Found = nullptr;
				for (auto *Element : Elements)
				{
					if (static_cast<RmlUi::FGuiElement *>(Element)->GetIdentity() == Id)
					{
						Found = Element;
						break;
					}
				}
				Require(Found != nullptr, "missing descendant element");
				const bool Expected = Id < 5 ? A : B;
				// Test the element's own visibility, including document-level popup portals.
				Require(Found->IsVisible() == Expected,
						"parent visibility or previous hidden index was stale for " + std::to_string(Id));
			}
		};
		Check(false, true);
		FindNode(2).Properties.Visibility = Gui::EVisibility::Visible;
		Check(true, true);
		FindNode(5).Properties.Visibility = Gui::EVisibility::Hidden;
		Check(true, false);
		Check(true, false);
		const auto Before = Nodes;
		Nodes = {Before[0], Before[4], Before[5], Before[6], Before[1], Before[2], Before[3]};
		Check(true, false);
		Check(true, false);
		FindNode(2).Properties.Visibility = Gui::EVisibility::Hidden;
		FindNode(5).Properties.Visibility = Gui::EVisibility::Visible;
		Check(false, true);
		Check(false, true);
		Require(Backend->Close(Error), Error);
	}

	void CheckLayoutSnapshotOutput()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(GuiTestResourceOptions(), Error);
		Require(bool(Backend), Error);
		std::vector<Gui::FBackendNode> Nodes(7);
		for (std::size_t I = 0; I < Nodes.size(); ++I)
		{
			Nodes[I].Id = I + 1;
			Nodes[I].Properties.Width = {Gui::ELengthUnit::Pixel, 80};
			Nodes[I].Properties.Height = {Gui::ELengthUnit::Pixel, 20};
		}
		Nodes[0].Properties.Width.Value = 400;
		Nodes[0].Properties.Height.Value = 300;
		Nodes[1].Parent = 1;
		Nodes[2].Parent = 2;
		Nodes[3].Parent = 1;
		Nodes[4].Parent = 4;
		Nodes[5].Parent = 4;
		Nodes[6].Parent = 6;
		Nodes[5].Control.Kind = Gui::EControlKind::Popup;
		double Time = 0;
		std::vector<Gui::FLayoutBox> Layout;
		const auto Find = [&](std::uint64_t Id) -> Gui::FBackendNode &
		{
			for (auto &Node : Nodes)
			{
				if (Node.Id == Id)
				{
					return Node;
				}
			}
			throw std::runtime_error("missing test node");
		};
		const auto Check = [&](bool ACollapsed, bool BCollapsed, bool PopupOpen)
		{
			Require(Backend->Synchronize({400, 300, 1}, Nodes, Error) && Backend->Update(++Time, Layout, Error), Error);
			Require(Layout.size() == Nodes.size(), "layout lost a snapshot node");
			for (std::size_t I = 0; I < Nodes.size(); ++I)
			{
				const auto Id = Nodes[I].Id;
				const bool Collapsed = Id == 2 || Id == 3	? ACollapsed
									   : Id == 6 || Id == 7 ? BCollapsed || !PopupOpen
									   : Id == 4 || Id == 5 ? BCollapsed
															: false;
				Require(Layout[I].Id == Id, "layout order changed");
				Require(Collapsed ? (Layout[I].Border.Width == 0 && Layout[I].Border.Height == 0)
								  : (Layout[I].Border.Width > 0 && Layout[I].Border.Height > 0),
						"collapsed/closed-popup inheritance changed for " + std::to_string(Id));
			}
		};
		Find(2).Properties.Visibility = Gui::EVisibility::Hidden;
		Check(false, false, false); // Hidden content still takes space.
		Find(2).Properties.Visibility = Gui::EVisibility::Collapsed;
		Check(true, false, false);
		Find(2).Properties.Visibility = Gui::EVisibility::Visible;
		Find(6).Control.bOpen = true;
		Check(false, false, true);
		const auto Before = Nodes;
		Nodes = {Before[0], Before[3], Before[4], Before[5], Before[6], Before[1], Before[2]};
		Find(4).Properties.Visibility = Gui::EVisibility::Collapsed;
		Check(false, true, true);
		Check(false, true, true);

		Gui::FLayoutBox Sentinel;
		Sentinel.Id = 999;
		Sentinel.Border.Width = 123;
		std::vector<Gui::FLayoutBox> Untouched{Sentinel};
		Require(!Backend->Update(Time - 1, Untouched, Error) && Untouched.size() == 1 && Untouched[0].Id == 999 &&
					Untouched[0].Border.Width == 123,
				"failed time validation changed caller layout");
		Require(Backend->Update(Time, Layout, Error), Error);
		auto &Text = Find(3);
		const auto Family = Text.Properties.FontFamily;
		Text.Properties.Text = "X";
		Text.Properties.FontFamily = "Missing update fixture family";
		Require(Backend->Synchronize({400, 300, 1}, Nodes, Error), Error);
		Require(!Backend->Update(Time + 50, Untouched, Error) && Error.find("rmlui.font_unavailable") == 0 &&
					Untouched.size() == 1 && Untouched[0].Id == 999 && Untouched[0].Border.Width == 123,
				"late update failure published partial layout or did not fail: " + Error);
		Text.Properties.Text.clear();
		Text.Properties.FontFamily = Family;
		Require(Backend->Synchronize({400, 300, 1}, Nodes, Error) && Backend->Update(++Time, Layout, Error),
				"failed update advanced time or prevented retry: " + Error);
		Require(Backend->Synchronize({400, 300, 1}, {}, Error) && Backend->Update(++Time, Layout, Error) &&
					Layout.empty(),
				"empty snapshot did not produce empty layout");
		Require(Backend->Close(Error), Error);
	}

	void CheckSnapshotRecovery()
	{
		using namespace VeroScript;
		std::string Error;
		auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
		Require(Backend != nullptr, Error);
		std::vector<Gui::FBackendNode> Nodes(3);
		for (std::size_t I = 0; I < Nodes.size(); ++I)
		{
			Nodes[I].Id = I + 1;
			Nodes[I].Parent = I ? 1 : 0;
		}
		Nodes[1].Properties.Text = "first";
		Nodes[1].Properties.WhiteSpace = "pre";
		std::vector<Gui::FLayoutBox> Layout;
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(0, Layout, Error), Error);
		const auto TextHeight = Layout[1].Border.Height;
		Require(TextHeight > 10, "initial text metrics");
		Nodes.push_back({});
		Nodes.back().Id = 4;
		Nodes.back().Parent = 1;
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(1, Layout, Error) &&
		        std::abs(Layout[1].Border.Height - TextHeight) < 0.01,
		    "structural rebuild lost unchanged text content");
		Nodes[1].Properties.Text = "first\nsecond";
		Nodes[2].Properties.Transform = "invalid-transform(";
		Require(!Backend->Synchronize({100, 100, 1}, Nodes, Error), "invalid transform accepted");
		Nodes[1].Properties.Text = "first";
		Nodes[2].Properties.Transform.clear();
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(2, Layout, Error) &&
		        std::abs(Layout[1].Border.Height - TextHeight) < 0.01,
		    "failed synchronization retained partially changed text");
		Nodes[2].Control.Kind = Gui::EControlKind::TextBox;
		Nodes[2].Control.Text = "abc";
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(3, Layout, Error) &&
		        Backend->FocusTextControl(3, {}, Error) && Backend->SetTextSelection(3, 0, 0, Error),
		    Error);
		Platform::FEvent Input;
		Input.Type = Platform::EEventType::TextInput;
		Input.Text = "X";
		Gui::FTextEditResult Edited;
		Require(Backend->EditText(3, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Xabc",
		    "initial snapshot regression edit failed: " + Error);
		Input.Type = Platform::EEventType::TextEditing;
		Input.Text.clear();
		Require(Backend->EditText(3, Input, Edited, Error) && !Edited.bChanged,
		    "reused text-edit result retained a previous committed change");
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(4, Layout, Error) &&
		        Backend->SetTextSelection(3, 0, 0, Error),
		    Error);
		Input.Type = Platform::EEventType::TextInput;
		Input.Text = "Y";
		Require(Backend->EditText(3, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Yabc",
		    "committed text edits left a stale synchronization snapshot");
		Nodes[2].Control.Text = "replacement";
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(5, Layout, Error) &&
		        Backend->SetTextSelection(3, 0, 0, Error),
		    Error);
		Input.Text = "Z";
		Require(Backend->EditText(3, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Zreplacement",
		    "control-only synchronization lost replacement text");
		std::swap(Nodes[1], Nodes[2]);
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(6, Layout, Error) &&
		        Layout[1].Id == 3 && Backend->FocusTextControl(3, {}, Error) &&
		        Backend->SetTextSelection(3, 0, 0, Error),
		    "reordered snapshot did not rebuild identity indices: " + Error);
		Input.Text = "Q";
		Require(Backend->EditText(3, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Qreplacement",
		    "reordered control edits used the previous snapshot index");
		Nodes[0].Properties.Padding.Left = 5;
		Nodes[1].Properties.Opacity = .9;
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(7, Layout, Error) &&
		        Backend->SetTextSelection(3, 0, 0, Error),
		    Error);
		Input.Text = "R";
		Require(Backend->EditText(3, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Rreplacement",
		    "same-structure synchronization after reordering retained a stale control snapshot");
		Require(Backend->Close(Error), Error);
	}

	void CheckVerticalFlow(double Scale)
	{
		using namespace VeroScript;
		using namespace Gui;
		std::string Error;
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {200, 200, Scale}, Error);
		auto Root = FWidget::Create();
		auto Column = FWidget::Create();
		Require(Context && Root->AddChild(Column, Error) && Column->Set(EProperty::Gap, 3.0, Error), Error);
		std::vector<std::shared_ptr<FWidget>> Children;
		for (int Index = 0; Index < 3; ++Index)
		{
			auto Child = FWidget::Create();
			Require(Child->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 8}, Error) && Column->AddChild(Child, Error), Error);
			Children.push_back(Child);
		}
		double Time = 0;
		const auto Height = [&]
		{
			FLayoutBox Box;
			Require(Context->Update(Time += .01, Error) && Column->GetLayout(Box, Error), Error);
			return Box.Border.Height;
		};
		Require(Context->SetRoot(Root, Error) && std::abs(Height() - 30) < .01, "auto-height vertical gaps");
		Require(Children[1]->Set(EProperty::Visibility, EVisibility::Hidden, Error) &&
		        std::abs(Height() - 30) < .01,
		    "hidden child lost its content space");
		Require(Children[1]->Set(EProperty::Visibility, EVisibility::Collapsed, Error) &&
		        std::abs(Height() - 19) < .01,
		    "collapsed child retained a spacer");
		Require(Children[1]->Set(EProperty::Visibility, EVisibility::Visible, Error) &&
		        Column->Set(EProperty::Gap, 0.0, Error) && std::abs(Height() - 24) < .01,
		    "clearing vertical gap");
		Require(Column->Set(EProperty::Gap, 5.0, Error) && std::abs(Height() - 34) < .01, "replacing vertical gap");
		Require(Column->Set(EProperty::Layout, ELayout::Horizontal, Error) && std::abs(Height() - 8) < .01 &&
		        Column->Set(EProperty::Layout, ELayout::Vertical, Error) && std::abs(Height() - 34) < .01,
		    "switching between block and flex flow left spacers behind");
		Require(Column->RemoveChild(Children[1], Error) && std::abs(Height() - 21) < .01 &&
		        Column->GetChildCount() == 2 && Column->GetChild(1, Error) == Children[2],
		    "private spacers escaped into the widget tree");
		Require(Children[0]->Set(EProperty::Visibility, EVisibility::Collapsed, Error) &&
		        Children[2]->Set(EProperty::Visibility, EVisibility::Collapsed, Error) && std::abs(Height()) < .01,
		    "an empty vertical container retained gap height");
		Require(Context->Close(Error), Error);
	}

	void CheckContentResources()
	{
		using namespace VeroScript;
		std::string Error;
		std::vector<std::uint8_t> Tga(18 + 16, 255);
		std::fill(Tga.begin(), Tga.begin() + 18, std::uint8_t{});
		Tga[2] = 2;
		Tga[12] = 2;
		Tga[14] = 2;
		Tga[16] = 32;
		Tga[17] = 0x28;
		RmlUi::FOptions Options;
		Options.ReadResource = [&](const auto &Source, auto &Bytes, auto &Failure)
		{
			if (Source == "memory:unavailable")
			{
				return false;
			}
			if (Source == "memory:throw")
			{
				throw std::runtime_error("reader exception");
			}
			if (Source == "memory:ok" || Source == "memory:custom" || Source == "memory:invalid")
			{
				Bytes = Tga;
				return true;
			}
			if (Source == "memory:missing")
			{
				Failure = "image missing";
				return false;
			}
			return Gui::ReadGuiResourceFile(Source, Bytes, Failure);
		};
		Options.DecodeImage = [](const auto &Source, const auto &, Gui::FDecodedImage &Out, auto &)
		{
			if (Source == "memory:custom")
			{
				Out.Width = 3;
				Out.Height = 4;
				Out.Rgba.assign(3 * 4 * 4, std::uint8_t{255});
				return true;
			}
			if (Source == "memory:invalid")
			{
				Out.Width = 16385;
				Out.Height = 1;
				return true;
			}
			return false;
		};
		auto Context = Gui::FContext::Create(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error),
		    {100, 100, 1}, Error);
		Require(Context != nullptr, Error);
		auto Root = Gui::FWidget::Create(), Image = Gui::FWidget::Create(), Text = Gui::FWidget::Create();
		Require(Image->Set(Gui::EProperty::Image, std::string("memory:ok"), Error) &&
		        Text->Set(Gui::EProperty::Text, std::string("office"), Error) && Root->AddChild(Image, Error) &&
		        Root->AddChild(Text, Error) && Root->Set(Gui::EProperty::FontSize, 24., Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		Gui::FLayoutBox Box;
		Require(Image->GetLayout(Box, Error) && Box.Border.Height == 2, "layout-only image dimensions");
		Require(Text->GetLayout(Box, Error) && Box.Border.Height > 10, "text intrinsic metrics");
		Gui::FPropertyValue Font;
		Require(Text->Get(Gui::EProperty::FontSize, Font, Error) && std::get<double>(Font) == 24,
		    "inherited font size");
		Require(Image->Set(Gui::EProperty::Image, std::string("memory:custom"), Error) && Context->Update(1, Error) &&
		        Image->GetLayout(Box, Error) && Box.Border.Height == 4,
		    "custom image decoder");
		Require(Image->Set(Gui::EProperty::Image, std::string("memory:missing"), Error) && !Context->Update(2, Error) &&
		        Error == "image missing",
		    "missing image was silently ignored");
		Require(Image->Set(Gui::EProperty::Image, std::string("memory:ok"), Error) && Context->Update(3, Error),
		    "recovery after replacing an invalid image: " + Error);
		Gui::FFontFace Face;
		Face.Source = (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		    "ThirdParty/RmlUi/Samples/assets/LatoLatin-Regular.ttf")
		                  .generic_string();
		Face.Family = "Application face";
		Require(Context->LoadFont(Face, Error) && Context->LoadFont(Face, Error), "font loading/idempotence: " + Error);
		Require(Text->Set(Gui::EProperty::FontFamily, Face.Family, Error) && Context->Update(4, Error) &&
		        Text->GetLayout(Box, Error) && Box.Border.Height > 10,
		    "loaded font metrics");
		Face.Family = "Invalid face";
		Face.Source = "memory:ok";
		Require(!Context->LoadFont(Face, Error) && Error.find("invalid_font") != std::string::npos,
		    "invalid font accepted");
		Face.Source = "memory:throw";
		Require(!Context->LoadFont(Face, Error) && Error.find("reader exception") != std::string::npos,
		    "font callback exception escaped");
		Face.Source = "memory:unavailable";
		Require(!Context->LoadFont(Face, Error) && !Error.empty(), "font read failure lost its diagnostic");
		Require(Image->Set(Gui::EProperty::Image, std::string("memory:invalid"), Error) && !Context->Update(5, Error),
		    "invalid custom decoded dimensions accepted");
		Require(Context->Close(Error), Error);
		bool bReentrantCloseRejected = false;
		Options.ReadResource = [&](const auto &Source, auto &Bytes, auto &Failure)
		{
			std::string CloseError;
			bReentrantCloseRejected = !Context->Close(CloseError) && CloseError == "gui.reentrant_operation";
			Context.reset();
			return Gui::ReadGuiResourceFile(Source, Bytes, Failure);
		};
		Context = Gui::FContext::Create(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error),
		    {100, 100, 1}, Error);
		Require(Context != nullptr, Error);
		Face.Source = (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		    "ThirdParty/RmlUi/Samples/assets/LatoLatin-Regular.ttf")
		                  .generic_string();
		Face.Family = "Released owner face";
		const std::weak_ptr<Gui::FContext> WeakContext = Context;
		auto *Borrowed = Context.get();
		Require(Borrowed->LoadFont(Face, Error) && bReentrantCloseRejected && !Context && WeakContext.expired(),
		    "font reader reentrancy or final owner release: " + Error);
	}
} // namespace

int main()
{
	using namespace VeroScript;
	using namespace Gui;
	try
	{
		CheckFontInitializationRecovery();
		CheckTextLayoutServices();
		CheckClusterFallbackDoesNotSkipText();
		CheckMissingGlyphReplacement();
		CheckFontFallback();
		CheckRmlUiFontFallback();
		CheckFontAtlasIdentityAndPositions();
		CheckParagraphFontLayout();
		CheckNumericLocale();
		CheckSnapshotValidation();
		CheckSnapshotRecovery();
		CheckHiddenSnapshotOrdering();
		CheckLayoutSnapshotOutput();
		CheckContentResources();
		for (const double Scale : {1.0, 1.5, 2.0})
		{
			CheckVerticalFlow(Scale);
		}
		std::string Error;
		auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
		Require(Backend != nullptr, Error);
		auto Context = FContext::Create(std::move(Backend), {600, 400, 2}, Error);
		Require(Context != nullptr, Error);
		auto Root = FWidget::Create();
		auto First = FWidget::Create();
		auto Second = FWidget::Create();
		Require(Root->Set(EProperty::Padding, FInsets{10, 10, 10, 10}, Error), Error);
		Require(Root->Set(EProperty::Gap, 5.0, Error), Error);
		Require(First->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 40}, Error), Error);
		Require(Second->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 20}, Error), Error);
		Require(Root->AddChild(First, Error) && Root->AddChild(Second, Error), Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		FLayoutBox Box;
		Require(Root->GetLayout(Box, Error), Error);
		Require(std::abs(Box.Border.Width - 300) < 0.1 && std::abs(Box.Content.Width - 280) < 0.1,
		    "DPI/padding layout");
		Require(Second->GetLayout(Box, Error), Error);
		Require(std::abs(Box.Border.Y - 55) < 0.1 && std::abs(Box.Border.Height - 20) < 0.1, "vertical gap layout");
		Require(First->Set(EProperty::Visibility, EVisibility::Collapsed, Error), Error);
		Require(Context->Update(1, Error), Error);
		Require(Second->GetLayout(Box, Error) && std::abs(Box.Border.Y - 10) < 0.1, "collapsed flow");
		Require(First->Set(EProperty::Visibility, EVisibility::Visible, Error), Error);
		Require(First->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 50}, Error), Error);
		Require(Second->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 20}, Error), Error);
		Require(Root->Set(EProperty::Layout, ELayout::Horizontal, Error) && Context->Update(2, Error), Error);
		Require(Second->GetLayout(Box, Error) && std::abs(Box.Border.X - 65) < 0.1 && std::abs(Box.Border.Y - 10) < 0.1,
		    "horizontal flow");
		Require(Root->Set(EProperty::Layout, ELayout::Overlay, Error), Error);
		Require(Second->Clear(EProperty::Width, Error) && Second->Clear(EProperty::Height, Error), Error);
		Require(Context->Update(3, Error) && Second->GetLayout(Box, Error), Error);
		Require(std::abs(Box.Border.X - 10) < 0.1 && std::abs(Box.Border.Y - 10) < 0.1 &&
		        std::abs(Box.Border.Width - 280) < 0.1 && std::abs(Box.Border.Height - 180) < 0.1,
		    "overlay content bounds");
		bool bPresented = true;
		Require(!Context->Render(bPresented, Error) && !bPresented, "layout-only render must fail");
		auto Other = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
		Require(Other != nullptr, Error);
		Require(Context->Close(Error), Error);
		std::vector<FLayoutBox> Layout;
		Require(Other->Synchronize({100, 100, 1}, {}, Error) && Other->Update(0, Layout, Error), Error);
		Require(Other->Close(Error), Error);
		Other.reset();
		Context.reset();
		auto Retired = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error);
		Require(Retired != nullptr, Error);
		std::thread Worker([Owned = std::move(Retired)]() mutable
		{
			Owned.reset();
		});
		Worker.join();
		Require(RmlUi::CollectRetired(Error), Error);
		Require(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions({}), Error) != nullptr,
		    "runtime restart failed: " + Error);
		std::cout << "RmlUi layout/lifetime contracts passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
