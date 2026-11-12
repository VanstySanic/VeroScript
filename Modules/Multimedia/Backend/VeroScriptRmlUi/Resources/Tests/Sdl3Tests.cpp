// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUiRenderer.h"
#include "VeroScriptSdl3.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
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

	void CheckMaskPaths(const std::shared_ptr<VeroScript::Graphics::IDevice> &Device)
	{
		using namespace VeroScript;
		std::string Error;
		auto Renderer = Device->CreateRenderer2D(Error);
		const auto Mesh = [&](Graphics::FColor Color)
		{
			return Renderer->CreateGeometry({{0, 0, 0, 0, Color}, {16, 0, 1, 0, Color},
			                                    {16, 16, 1, 1, Color}, {0, 16, 0, 1, Color}},
			    {0, 1, 2, 0, 2, 3}, Error);
		};
		auto Red = Mesh({1, 0, 0, 1}), Blue = Mesh({0, 0, 1, 1});
		auto Half = Mesh({1, 1, 1, .5f}), Quarter = Mesh({1, 1, 1, .25f});
		Graphics::FDrawList2D Foreground, Background, HalfMask, QuarterMask;
		Require(Red && Blue && Half && Quarter && Foreground.AddGeometry(Red, Error) &&
		        Background.AddGeometry(Blue, Error) && HalfMask.AddGeometry(Half, Error) &&
		        QuarterMask.AddGeometry(Quarter, Error),
		    Error);
		std::vector<std::uint8_t> Reference;
		for (const bool Tight : {false, true})
		{
			RmlUi::FOptions Options;
			Options.Device = Device;
			Options.Texture = Device->CreateTexture({16, 16}, Error);
			Options.MaxCompositionBytes = Tight ? 16 * 1024 : 64 * 1024;
			RmlUi::FCompositionFrame Frame;
			Require(Options.Texture && Frame.Initialize(Options, Renderer, Device->CreateSampler({}, Error), Error), Error);
			Frame.Begin(16, 16);
			if (Tight)
			{
				for (int Index = 0; Index < 5; ++Index)
				{
					Require(Frame.Push(Error) && Frame.Pop(Error), Error);
				}
			}
			bool Presented = false;
			std::vector<std::uint8_t> Pixels;
			Require(Frame.AddDraw(Background, Error) &&
			        Frame.AddDraw(Foreground, Error, {{HalfMask, false}, {QuarterMask, true}}) &&
			        Frame.Submit(Presented, Error) && Device->ReadTexture(Options.Texture, Pixels, Error),
			    Error);
			const auto At = (8 * 16 + 8) * 4;
			Require(Pixels.size() == 16 * 16 * 4 && std::abs(Pixels[At] - 96) <= 2 &&
			        std::abs(Pixels[At + 2] - 159) <= 2 && Pixels[At + 3] == 255,
			    "nested soft/inverse masks changed source-over compositing (" + std::to_string(Tight) +
			        ": " + std::to_string(Pixels.at(At)) + ", " + std::to_string(Pixels.at(At + 2)) +
			        ", " + std::to_string(Pixels.at(At + 3)) + ")");
			if (!Tight)
			{
				Reference = Pixels;
			}
			else
			{
				Require(std::equal(Pixels.begin(), Pixels.end(), Reference.begin(),
				            [](int A, int B)
				{
					return std::abs(A - B) <= 2;
				}),
				    "mask fallback disagrees with bounded direct passes");
			}
			Require(Frame.Close(Error) && Options.Texture->Close(Error), Error);
		}
		Require(Red->Close(Error) && Blue->Close(Error) && Half->Close(Error) && Quarter->Close(Error) &&
		        Renderer->Close(Error),
		    Error);
	}

	void CheckContent(const std::shared_ptr<VeroScript::Graphics::IDevice> &Device)
	{
		using namespace VeroScript;
		std::string Error;
		auto Target = Device->CreateTexture({128, 128}, Error);
		Require(Target != nullptr, Error);
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Target;
		std::vector<std::uint8_t> Tga(18 + 16, 0);
		Tga[2] = 2;
		Tga[12] = 2;
		Tga[14] = 2;
		Tga[16] = 32;
		Tga[17] = 0x28;
		for (std::size_t I = 18; I < Tga.size(); I += 4)
		{
			Tga[I + 2] = 255;
			Tga[I + 3] = 128;
		}
		Options.ReadResource = [&](const auto &Source, auto &Bytes, auto &Failure)
		{
			if (Source == "memory:test" || Source == "memory:missing-extra")
			{
				Bytes = Tga;
				return true;
			}
			return Gui::ReadGuiResourceFile(Source, Bytes, Failure);
		};
		auto Context = Gui::FContext::Create(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error),
		    {128, 128, 1}, Error);
		Require(Context != nullptr, Error);
		auto Root = Gui::FWidget::Create();
		Require(Root->Set(Gui::EProperty::Text, std::string("office <b>& text"), Error) &&
		        Root->Set(Gui::EProperty::FontSize, 24., Error) && Context->SetRoot(Root, Error),
		    Error);
		std::vector<std::uint8_t> Pixels;
		bool Presented = false;
		double Time = 0;
		const auto Render = [&]()
		{
			const bool Updated = Context->Update(Time++, Error);
			Require(Updated, "content update: " + Error);
			const bool Rendered = Context->Render(Presented, Error);
			Require(Rendered, "content render: " + Error);
			Require(Device->ReadTexture(Target, Pixels, Error), Error);
		};
		const auto Channel = [&](int X, int Y, int C)
		{
			return Pixels.at(static_cast<std::size_t>((Y * 128 + X) * 4 + C));
		};
		Render();
		std::size_t Ink = 0;
		for (std::size_t I = 3; I < Pixels.size(); I += 4)
		{
			if (Pixels[I] > 0)
			{
				++Ink;
			}
		}
		Require(Ink > 100, "default shaped font produced no glyph pixels");
		const auto TextPixels = Pixels;
		auto TemporaryChild = Gui::FWidget::Create();
		Require(Root->AddChild(TemporaryChild, Error), Error);
		Render();
		Require(Pixels == TextPixels, "structural rebuild changed retained text pixels");
		Require(Root->RemoveChild(TemporaryChild, Error), Error);
		Render();
		Require(Pixels == TextPixels, "removing a child lost retained text pixels");
		Gui::FFontFace ReplacementFont;
		ReplacementFont.Source =
		    (std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
		        "VeroScriptFontBackend/ThirdParty/HarfBuzz/perf/fonts/Roboto-Regular.ttf")
		        .generic_string();
		ReplacementFont.Family = "Replacement font";
		Require(Context->LoadFont(ReplacementFont, Error) &&
		        Root->Set(Gui::EProperty::FontFamily, ReplacementFont.Family, Error) &&
		        Root->Set(Gui::EProperty::Text, std::string("\U000f0000"), Error),
		    Error);
		Render();
		Ink = 0;
		for (std::size_t I = 3; I < Pixels.size(); I += 4)
		{
			Ink += Pixels[I] > 0;
		}
		Require(Ink > 0, "missing glyph produced no replacement pixels");
		Require(Root->Set(Gui::EProperty::FontFamily, std::string(Font::DefaultFontFamily), Error), Error);
		Require(Root->Set(Gui::EProperty::Text, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::Image, std::string("memory:test"), Error),
		    Error);
		Render();
		Require(std::abs(Channel(0, 0, 0) - 128) <= 2 && std::abs(Channel(0, 0, 3) - 128) <= 2,
		    "decoded image alpha/color contract");
		const auto ImagePixels = Pixels;
		Require(Root->AddChild(TemporaryChild, Error), Error);
		Render();
		Require(Pixels == ImagePixels, "structural rebuild lost retained image pixels");
		Require(Root->Set(Gui::EProperty::Text, std::string("office"), Error), Error);
		Render();
		const auto MixedPixels = Pixels;
		Require(Root->RemoveChild(TemporaryChild, Error), Error);
		Render();
		Require(Pixels == MixedPixels, "text/image ordering changed after a structural rebuild");
		Require(Root->Set(Gui::EProperty::Text, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::Image, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::BackgroundImage, std::string("linear-gradient(90deg, #ff0000, #0000ff)"),
		            Error),
		    Error);
		Render();
		Require(Channel(4, 64, 0) > Channel(4, 64, 2) && Channel(123, 64, 2) > Channel(123, 64, 0),
		    "gradient direction or shader cache: left=" + std::to_string(Channel(4, 64, 0)) + "," +
		        std::to_string(Channel(4, 64, 2)) + "," + std::to_string(Channel(4, 64, 3)) +
		        " right=" + std::to_string(Channel(123, 64, 0)) + "," + std::to_string(Channel(123, 64, 2)) + "," +
		        std::to_string(Channel(123, 64, 3)));
		Render();
		Require(Channel(123, 64, 2) > Channel(123, 64, 0), "repeated gradient lost resources");
		Require(Root->Set(Gui::EProperty::BackgroundImage, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Root->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Root->Set(Gui::EProperty::ClipContent, true, Error) &&
		        Root->Set(Gui::EProperty::CornerRadius, 24., Error),
		    Error);
		auto Child = Gui::FWidget::Create();
		Require(Child->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Child->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Child->Set(Gui::EProperty::Background, Graphics::FColor{0, 1, 0, 1}, Error) &&
		        Root->AddChild(Child, Error),
		    Error);
		Render();
		Require(Channel(0, 0, 3) == 0 && Channel(32, 32, 1) > 250, "rounded child mask");
		Require(Root->RemoveChild(Child, Error) && Root->Set(Gui::EProperty::CornerRadius, 0., Error) &&
		        Root->Set(Gui::EProperty::ClipContent, false, Error),
		    Error);
		int Paints = 0;
		Require(Root->SetPaintCallback(
		            [&](Gui::FPaintContext &Canvas, std::string &Failure)
		{
			++Paints;
			return Canvas.FillRect({16, 16, 16, 16}, {0, 0, 1, 1}, Failure);
		},
		            Error),
		    Error);
		Render();
		Require(Paints == 1 && Channel(20, 20, 2) > 250, "content-local paint order or geometry");
		Render();
		Require(Paints == 2 && Channel(20, 20, 2) > 250, "cached custom paint");
		Require(Root->Set(Gui::EProperty::Filter, std::string("blur(2px)"), Error), Error);
		Render();
		Require(Channel(15, 20, 3) > 0 && Channel(15, 20, 3) < 255, "blur did not spread alpha");
		Require(Root->Set(Gui::EProperty::Opacity, 0.5, Error), Error);
		Render();
		const auto TranslucentBlur = Pixels;
		for (int Gap = 1; Gap <= 2; ++Gap)
		{
			// Synchronize an unrelated property while retaining both filter and opacity.
			Require(Root->Set(Gui::EProperty::Gap, double(Gap), Error), Error);
			Render();
			Require(Pixels == TranslucentBlur, "unrelated synchronization replaced the translucent blur");
		}
		Require(Root->Set(Gui::EProperty::Opacity, 1.0, Error) &&
		        Root->Set(Gui::EProperty::Gap, 0.0, Error),
		    Error);
		Require(Root->Set(Gui::EProperty::Filter, std::string("drop-shadow(8px 0px 0px #00ff00)"), Error), Error);
		Render();
		Require(Channel(36, 20, 1) > 240 && Channel(20, 20, 2) > 240, "drop shadow lost source or tint");
		Require(Root->Set(Gui::EProperty::Filter, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::Transform, std::string("translateX(32px)"), Error),
		    Error);
		Render();
		Require(Channel(52, 20, 2) > 240 && Channel(20, 20, 3) == 0, "transformed native paint");
		bool Routed = false;
		Root->Subscribe(Platform::EEventType::PointerDown, false, [&](auto &)
		{
			Routed = true;
		}, Error);
		Platform::FEvent Input;
		Input.Type = Platform::EEventType::PointerDown;
		Input.WindowId = 1;
		Input.Button = 1;
		Input.X = 80;
		Input.Y = 20;
		bool Handled = false;
		Require(Context->ProcessEvent(Input, Handled, Error) && Routed, "transformed hit test differs from render");
		Require(Root->SetPaintCallback(
		            [](Gui::FPaintContext &Canvas, std::string &Failure)
		{
			return Canvas.PushClip({20, 20, 8, 8}, Failure) &&
			    Canvas.FillRect({16, 16, 16, 16}, {0, 0, 1, 1}, Failure) && Canvas.PopClip(Failure);
		},
		            Error),
		    Error);
		Render();
		Require(Channel(54, 22, 2) > 240 && Channel(50, 22, 3) == 0, "custom clip did not follow widget transform");
		Root = Gui::FWidget::Create();
		Child = Gui::FWidget::Create();
		Require(Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 1}, Error) &&
		        Child->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Child->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Child->Set(Gui::EProperty::BackdropFilter, std::string("grayscale(1)"), Error) &&
		        Root->AddChild(Child, Error) && Context->SetRoot(Root, Error),
		    Error);
		Render();
		Require(std::abs(Channel(32, 32, 0) - Channel(32, 32, 1)) <= 2 &&
		        std::abs(Channel(32, 32, 1) - Channel(32, 32, 2)) <= 2 && Channel(32, 32, 3) > 250 &&
		        Channel(100, 100, 0) > 250 && Channel(100, 100, 1) == 0,
		    "backdrop layer capture or color matrix");
		Require(
		    Root->RemoveChild(Child, Error) &&
		        Root->Set(Gui::EProperty::MaskImage, std::string("linear-gradient(90deg, #ffffff, #ffffff00)"), Error),
		    Error);
		Render();
		Require(Channel(4, 64, 3) > 235 && Channel(123, 64, 3) < 20, "gradient alpha-mask layer capture");
		Require(Root->Set(Gui::EProperty::MaskImage, std::string{}, Error), Error);
		for (const auto &Gradient : {"radial-gradient(#ff0000, #0000ff)", "conic-gradient(#ff0000, #0000ff)",
		         "repeating-linear-gradient(90deg, #ff0000 0px, #0000ff 16px)"})
		{
			Require(Root->Set(Gui::EProperty::BackgroundImage, std::string(Gradient), Error), Error);
			Render();
			Require(Channel(32, 32, 3) > 250, "gradient variant generated no pixels");
		}
		Require(Root->Set(Gui::EProperty::BackgroundImage, std::string{}, Error) &&
		        Root->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Root->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 64}, Error) &&
		        Root->Set(Gui::EProperty::BoxShadow, std::string("8px 0px 0px 0px #00ff00"), Error),
		    Error);
		Render();
		Require(Channel(68, 32, 1) > 240 && Channel(32, 32, 0) > 240, "box shadow capture or inverse mask");
		Require(Root->Set(Gui::EProperty::Filter, std::string("blur(65px)"), Error) && Context->Update(Time++, Error) &&
		        !Context->Render(Presented, Error),
		    "unsupported blur budget was silently ignored");
		Require(!Context->Render(Presented, Error), "failed filter disappeared on a repeated frame");
		Require(Root->Set(Gui::EProperty::Filter, std::string{}, Error), Error);
		Render();
		Require(Root->Set(Gui::EProperty::BackgroundImage, std::string("url(\"memory:missing\")"), Error) &&
		        Context->Update(Time++, Error) && !Context->Render(Presented, Error),
		    "missing background image was silently ignored");
		Require(Root->Set(Gui::EProperty::BackgroundImage, std::string("url(\"memory:missing-extra\")"), Error), Error);
		Render();
		Require(Context->Close(Error) && Target->Close(Error), Error);
	}
} // namespace

int main(int Count, char **Args)
{
	using namespace VeroScript;
	try
	{
		std::string Error;
		auto Graphics = Sdl3::CreateGraphics({}, Error);
		Require(Graphics != nullptr, Error);
		Graphics::FDeviceOptions DeviceOptions;
		DeviceOptions.Driver = Count > 1 ? Args[1] : "";
		auto Device = Graphics->CreateDevice(DeviceOptions, Error);
		if (!Device)
		{
			std::cout << "GPU unavailable: " << Error << '\n';
			return Count > 1 ? 1 : 77;
		}
		auto Target = Device->CreateTexture({32, 32}, Error);
		Require(Target != nullptr, Error);
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Target;
		std::vector<std::uint8_t> Pixels;
		const auto Pixel = [&](int X, int Y, int R, int G, int B, int A)
		{
			const auto Offset = static_cast<std::size_t>((Y * 32 + X) * 4);
			return Pixels.size() == 32 * 32 * 4 && std::abs(Pixels[Offset] - R) <= 2 &&
			    std::abs(Pixels[Offset + 1] - G) <= 2 && std::abs(Pixels[Offset + 2] - B) <= 2 &&
			    std::abs(Pixels[Offset + 3] - A) <= 2;
		};
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		Renderer.BeginFrame();
		Rml::Vertex Vertices[3]{};
		Vertices[0].position = {0, 0};
		Vertices[1].position = {32, 0};
		Vertices[2].position = {0, 32};
		Vertices[0].colour = {255, 0, 0, 255};
		Vertices[1].colour = Vertices[2].colour = {0, 0, 0, 0};
		const int Indices[]{0, 1, 2};
		const auto Geometry = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		Require(Geometry != 0, "gradient geometry");
		Renderer.RenderGeometry(Geometry, {0, 0}, 0);
		bool bPresented = false;
		Require(Renderer.Submit(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error), Error);
		if (!Pixel(8, 8, 120, 0, 0, 120))
		{
			const auto Offset = (8 * 32 + 8) * 4;
			throw std::runtime_error("premultiplied gradient: expected red/alpha 120/120, got " +
			    std::to_string(Pixels.at(Offset)) + "/" + std::to_string(Pixels.at(Offset + 3)));
		}
		Renderer.ReleaseGeometry(Geometry);
		Renderer.BeginFrame();
		for (auto &Vertex : Vertices)
		{
			Vertex.colour = {255, 255, 255, 255};
		}
		const auto White = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		const Rml::byte GrayPixel[]{64, 64, 64, 128};
		const auto Image = Renderer.GenerateTexture({GrayPixel, 4}, {1, 1});
		Require(White && Image, "generated texture and geometry");
		Renderer.SetScissorRegion(Rml::Rectanglei::FromPositionSize({4, 4}, {8, 8}));
		Renderer.EnableScissorRegion(true);
		Renderer.RenderGeometry(White, {2, 3}, Image);
		// RmlUi may release handles after its last draw call, before our deferred submission.
		Renderer.ReleaseGeometry(White);
		Renderer.ReleaseTexture(Image);
		Require(Renderer.Submit(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error) &&
		        Pixel(4, 4, 27, 27, 27, 128) && Pixel(3, 4, 0, 0, 0, 0) && Pixel(12, 8, 0, 0, 0, 0),
		    "generated linear texture, translation and scissor readback");
		Require(Renderer.Close(Error), Error);

		auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Require(Backend != nullptr, Error);
		auto Context = Gui::FContext::Create(std::move(Backend), {32, 32, 2}, Error);
		auto Root = Gui::FWidget::Create();
		auto Child = Gui::FWidget::Create();
		Require(Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 0.5f}, Error) &&
		        Root->Set(Gui::EProperty::Padding, Gui::FInsets{2, 2, 2, 2}, Error) &&
		        Child->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 8}, Error) &&
		        Child->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 6}, Error) &&
		        Child->Set(Gui::EProperty::Background, Graphics::FColor{0, 1, 0, 1}, Error) &&
		        Root->AddChild(Child, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error),
		    Error);
		Require(!bPresented && Pixel(1, 1, 128, 0, 0, 128) && Pixel(4, 4, 0, 255, 0, 255) &&
		        Pixel(19, 15, 0, 255, 0, 255) && Pixel(20, 16, 128, 0, 0, 128),
		    "RmlUi layout, scale, background and alpha readback");
		Require(Child->Set(Gui::EProperty::Visibility, Gui::EVisibility::Hidden, Error) && Context->Update(1, Error) &&
		        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error) &&
		        Pixel(4, 4, 128, 0, 0, 128),
		    "visibility readback");
		Require(Context->Close(Error) && Target->IsOpen() && Device->IsOpen(), "borrowed resource ownership");
		Gui::FMarkupDiagnostic MarkupError;
		auto Markup =
		    Gui::FMarkupProgram::Compile({"pixels.vswml", R"ML(<Widget Background="rgba(255, 0, 0, 0.5)" Padding="2px">
            <style>.part { width: 8px; height: 6px; background-color: #00ff00; } .part:hover { background-color: #0000ff; }</style>
            <Widget id="part" class="part" />
        </Widget>)ML"},
		        {}, MarkupError);
		Require(Markup != nullptr, MarkupError.Format());
		Root = Markup->Create({}, MarkupError);
		Require(Root != nullptr, MarkupError.Format());
		Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Context = Gui::FContext::Create(std::move(Backend), {32, 32, 2}, Error);
		Require(Context && Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error) &&
		        Pixel(4, 4, 0, 255, 0, 255),
		    "ML/CSS to RmlUi pixels: " + Error);
		Platform::FEvent Hover;
		Hover.Type = Platform::EEventType::PointerMove;
		Hover.WindowId = 1;
		Hover.X = Hover.Y = 6;
		bool bHoverHandled = false;
		Require(Context->ProcessEvent(Hover, bHoverHandled, Error) && Context->Update(1, Error) &&
		        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error) &&
		        Pixel(4, 4, 0, 0, 255, 255),
		    "live CSS state to GPU: " + Error);
		Require(Context->Close(Error), Error);
		Require(Target->Close(Error), Error);

		Target = Device->CreateTexture({32, 32}, Error);
		Require(Target != nullptr, Error);
		Options.Texture = Target;
		Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Require(Backend != nullptr, Error);
		Context = Gui::FContext::Create(std::move(Backend), {32, 32, 1}, Error);
		Root = Gui::FWidget::Create();
		auto GroupChild = Gui::FWidget::Create();
		Require(Context && Context->GetCapabilities().bGroupOpacity &&
		        Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 1}, Error) &&
		        Root->Set(Gui::EProperty::Opacity, 0.5, Error) &&
		        GroupChild->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 16}, Error) &&
		        GroupChild->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 16}, Error) &&
		        GroupChild->Set(Gui::EProperty::Background, Graphics::FColor{0, 0, 1, 1}, Error) &&
		        Root->AddChild(GroupChild, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
		        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error),
		    Error);
		Require(Pixel(4, 4, 0, 0, 128, 128) && Pixel(24, 24, 128, 0, 0, 128),
		    "group opacity blended each overlapping widget independently");
		Require(Context->Close(Error) && Target->Close(Error), Error);

		for (const auto Format : {Graphics::ETextureFormat::Rgba8Unorm, Graphics::ETextureFormat::Rgba8Srgb})
		{
			Target = Device->CreateTexture({32, 32, Format}, Error);
			Require(Target != nullptr, Error);
			Options.Texture = Target;
			Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
			Require(Backend != nullptr, Error);
			Context = Gui::FContext::Create(std::move(Backend), {32, 32, 1}, Error);
			Root = Gui::FWidget::Create();
			Require(Root->Set(Gui::EProperty::Background, Graphics::FColor{0.5f, 0.5f, 0.5f, 1}, Error) &&
			        Context->SetRoot(Root, Error) && Context->Update(0, Error) &&
			        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error),
			    Error);
			const int Gray = Format == Graphics::ETextureFormat::Rgba8Srgb ? 188 : 128;
			Require(Pixel(8, 8, Gray, Gray, Gray, 255), "native linear color disagrees with target encoding");
			Require(Root->Set(Gui::EProperty::Opacity, 0.5, Error) && Context->Update(1, Error) &&
			        Context->Render(bPresented, Error) && Device->ReadTexture(Target, Pixels, Error),
			    Error);
			const int HalfGray = Format == Graphics::ETextureFormat::Rgba8Srgb ? 137 : 64;
			Require(Pixel(8, 8, HalfGray, HalfGray, HalfGray, 128), "linear color with opacity was double encoded");
			Require(Context->Close(Error) && Target->Close(Error), Error);
		}

		CheckContent(Device);
		CheckMaskPaths(Device);
		auto Platform = Sdl3::CreatePlatform({}, Error);
		Require(Platform != nullptr, Error);
		Platform::FWindowOptions WindowOptions;
		WindowOptions.Title = "VeroScript GUI integration test";
		WindowOptions.Width = 128;
		WindowOptions.Height = 96;
		if (Device->GetDriver() == "opengl" || Device->GetDriver() == "opengles")
		{
			WindowOptions.GraphicsApi = Device->GetDriver();
		}
		auto Window = Platform->CreateWindow(WindowOptions, Error);
		Require(Window != nullptr, Error);
		auto Surface = Device->CreateSurface(Window, Error);
		Require(Surface != nullptr, Error);
		Options.Texture.reset();
		Options.Surface = Surface;
		Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Require(Backend != nullptr, Error);
		Platform::FWindowMetrics Metrics;
		Require(Window->GetMetrics(Metrics, Error), Error);
		Context = Gui::FContext::Create(std::move(Backend),
		    {Metrics.PixelWidth, Metrics.PixelHeight, Metrics.DisplayScale}, Error);
		Root = Gui::FWidget::Create();
		Require(Context && Root->Set(Gui::EProperty::Background, Graphics::FColor{0.5f, 0.5f, 0.5f, 1}, Error) &&
		        Context->SetRoot(Root, Error) && Context->SetWindowMetrics(Window->GetId(), Metrics, Error) &&
		        Context->Update(0, Error) && Context->Render(bPresented, Error) && bPresented,
		    "window surface render: " + Error);
		bool bRouted = false;
		Root->Subscribe(Platform::EEventType::PointerDown, false, [&](auto &)
		{
			bRouted = true;
		}, Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = Window->GetId();
		Event.X = Event.Y = 10;
		Event.Button = 1;
		bool bHandled = false;
		Require(Context->ProcessEvent(Event, bHandled, Error) && bRouted, "window input routing: " + Error);
		Require(Context->Close(Error) && Surface->Close(Error) && Window->Close(Error) && Device->Close(Error) &&
		        RmlUi::CollectRetired(Error) && Sdl3::CollectRetired(Error),
		    Error);
		std::cout << "RmlUi SDL_GPU pixels passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
