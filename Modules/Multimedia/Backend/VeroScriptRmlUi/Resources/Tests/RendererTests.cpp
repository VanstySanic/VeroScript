// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUiRenderer.h"
#include <RmlUi/Core/Matrix4.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;

	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FTexture final : public Graphics::ITexture
	{
	public:
		bool IsOpen() const noexcept override
		{
			return bOpen;
		}
		Graphics::FTextureOptions GetOptions() const noexcept override
		{
			return Options;
		}
		bool Close(std::string &Error) override
		{
			if (bThrowClose)
			{
				throw std::runtime_error("injected texture close exception");
			}
			if (bFailClose)
			{
				Error = "injected texture close failure";
				return false;
			}
			bOpen = false;
			return true;
		}
		Graphics::FTextureOptions Options;
		bool bOpen = true, bFailClose = false, bThrowClose = false;
	};

	class FSampler final : public Graphics::ISampler
	{
	public:
		bool IsOpen() const noexcept override
		{
			return bOpen;
		}
		Graphics::FSamplerOptions GetOptions() const noexcept override
		{
			return {};
		}
		bool Close(std::string &) override
		{
			bOpen = false;
			return true;
		}
		bool bOpen = true;
	};

	class FGeometry final : public Graphics::IGeometry2D
	{
	public:
		bool IsOpen() const noexcept override
		{
			return bOpen;
		}
		std::int64_t GetVertexCount() const noexcept override
		{
			return VertexCount;
		}
		std::int64_t GetIndexCount() const noexcept override
		{
			return IndexCount;
		}
		bool Close(std::string &Error) override
		{
			if (bThrowClose)
			{
				throw std::runtime_error("injected geometry close exception");
			}
			bOpen = false;
			Error.clear();
			return true;
		}
		bool bOpen = true, bThrowClose = false;
		std::int64_t VertexCount = 0, IndexCount = 0;
	};

	class FSurface final : public Graphics::ISurface
	{
	public:
		bool IsOpen() const noexcept override
		{
			return true;
		}
		std::uint64_t GetWindowId() const noexcept override
		{
			return 1;
		}
		bool Close(std::string &) override
		{
			return true;
		}
	};

	class FRecordingRenderer final : public Graphics::IRenderer2D, public Graphics::IRenderSubmission2D
	{
	public:
		bool IsOpen() const noexcept override
		{
			return bOpen;
		}
		std::shared_ptr<Graphics::IGeometry2D> CreateGeometry(const std::vector<Graphics::FVertex2D> &Value,
		    const std::vector<std::uint32_t> &Indices,
		    std::string &Error) override
		{
			if (!Graphics::ValidateGeometry2D(Value, Indices, GeometryLimit, Error))
			{
				return {};
			}
			Vertices = Value;
			++Compiles;
			auto Geometry = std::make_shared<FGeometry>();
			Geometry->VertexCount = static_cast<std::int64_t>(Value.size());
			Geometry->IndexCount = static_cast<std::int64_t>(Indices.size());
			LastGeometry = Geometry;
			return Geometry;
		}
		bool RenderTexture(const std::shared_ptr<Graphics::ITexture> &, const Graphics::FDrawList2D &List,
		    const Graphics::FRenderOptions2D &, std::string &Error) override
		{
			if (bThrowRender)
			{
				throw std::runtime_error("injected render exception");
			}
			if (bFailRender)
			{
				Error = "injected render failure";
				return false;
			}
			for (const auto &Draw : List.GetCommands())
			{
				if (!Draw.Geometry || !Draw.Geometry->IsOpen() || (Draw.Texture && !Draw.Texture->IsOpen()))
				{
					Error = "draw references closed resources";
					return false;
				}
			}
			Draws = List.GetCommands();
			for (const auto &Draw : Draws)
			{
				if (Draw.Effect.Type == Graphics::EEffect2D::ColorMatrix)
				{
					++ColorFilters;
					bPremultipliedFilter = Draw.Effect.bSourcePremultiplied;
				}
			}
			++Submits;
			return true;
		}
		bool RenderSurface(const std::shared_ptr<Graphics::ISurface> &, const Graphics::FDrawList2D &,
		    const Graphics::FRenderOptions2D &, bool &Presented, std::string &Error) override
		{
			if (bAllowSurface)
			{
				Presented = false;
				return true;
			}
			Error = "unexpected surface";
			return false;
		}
		bool Close(std::string &Error) override
		{
			if (bThrowClose)
			{
				throw std::runtime_error("injected renderer close exception");
			}
			if (bFailClose)
			{
				Error = "injected renderer close failure";
				return false;
			}
			Draws.clear();
			bOpen = false;
			return true;
		}
		bool BeginSubmission(std::string &Error) override
		{
			if (bFailBegin || bSubmission)
			{
				Error = "injected begin failure";
				return false;
			}
			bSubmission = true;
			++Begins;
			return true;
		}
		bool EndSubmission(std::string &Error) override
		{
			if (bThrowEnd)
			{
				throw std::runtime_error("injected end exception");
			}
			if (bFailEnd || !bSubmission)
			{
				Error = "injected end failure";
				return false;
			}
			bSubmission = false;
			++Ends;
			return true;
		}
		bool CancelSubmission(std::string &) override
		{
			if (bSubmission)
			{
				++Cancels;
			}
			bSubmission = false;
			if (bThrowCancel)
			{
				throw std::runtime_error("injected cancel exception");
			}
			return true;
		}
		bool bSubmission = false, bFailBegin = false, bFailEnd = false, bFailRender = false, bThrowRender = false;
		bool bThrowCancel = false, bThrowEnd = false;
		bool bAllowSurface = false;
		int Begins = 0, Ends = 0, Cancels = 0;
		std::vector<Graphics::FVertex2D> Vertices;
		std::vector<Graphics::FDraw2D> Draws;
		std::weak_ptr<FGeometry> LastGeometry;
		int Submits = 0, Compiles = 0;
		int ColorFilters = 0;
		bool bPremultipliedFilter = false;
		std::uint64_t GeometryLimit = 1024 * 1024;
		bool bOpen = true, bFailClose = false, bThrowClose = false;
	};

	class FDevice final : public Graphics::IDevice
	{
	public:
		bool IsOpen() const noexcept override
		{
			return true;
		}
		std::string GetDriver() const override
		{
			return "recording";
		}
		Graphics::FCapabilities GetCapabilities() const noexcept override
		{
			Graphics::FCapabilities Caps;
			Caps.bGuiBasic = Caps.bGuiTransform = true;
			Caps.bClipMasks = Caps.bFilters = bComposition;
			return Caps;
		}
		std::shared_ptr<Graphics::ITexture> CreateTexture(const Graphics::FTextureOptions &Options,
		    std::string &) override
		{
			auto Value = std::make_shared<FTexture>();
			Value->Options = Options;
			LastTexture = Value;
			return Value;
		}
		std::shared_ptr<Graphics::ISurface> CreateSurface(std::shared_ptr<Platform::IWindow>, std::string &) override
		{
			return {};
		}
		bool ClearTexture(const std::shared_ptr<Graphics::ITexture> &, const Graphics::FColor &, std::string &) override
		{
			return false;
		}
		bool Present(const std::shared_ptr<Graphics::ISurface> &, const Graphics::FColor &, bool &,
		    std::string &) override
		{
			return false;
		}
		bool ReadTexture(const std::shared_ptr<Graphics::ITexture> &, std::vector<std::uint8_t> &,
		    std::string &) override
		{
			return false;
		}
		bool WaitIdle(std::string &) override
		{
			return true;
		}
		bool Close(std::string &) override
		{
			return true;
		}
		std::shared_ptr<Graphics::ISampler> CreateSampler(const Graphics::FSamplerOptions &,
		    std::string &Error) override
		{
			if (bFailSampler)
			{
				Error = "injected sampler creation failure";
				return {};
			}
			return std::make_shared<FSampler>();
		}
		bool UpdateTexture(const std::shared_ptr<Graphics::ITexture> &, std::int64_t, std::int64_t, std::int64_t,
		    std::int64_t, const std::vector<std::uint8_t> &Value, std::string &Error) override
		{
			if (bFailUpload)
			{
				LastTexture.lock()->bFailClose = true;
				Error = "injected texture upload failure";
				return false;
			}
			Pixels = Value;
			return true;
		}
		std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2D(std::string &) override
		{
			if (!Recording->IsOpen())
			{
				Recording = std::make_shared<FRecordingRenderer>();
			}
			return Recording;
		}
		std::shared_ptr<FRecordingRenderer> Recording = std::make_shared<FRecordingRenderer>();
		std::weak_ptr<FTexture> LastTexture;
		std::vector<std::uint8_t> Pixels;
		bool bFailSampler = false, bFailUpload = false, bComposition = false;
	};

	void CheckGeometryUploadRecovery()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		auto Options = GuiTestResourceOptions();
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({32, 32}, Error);
		auto Backend = RmlUi::CreateBackend(Options, Error);
		auto Context = Backend ? Gui::FContext::Create(std::move(Backend), {32, 32, 1}, Error) : nullptr;
		auto Root = Gui::FWidget::Create();
		Require(Context && Root->Set(Gui::EProperty::Width, Gui::FLength{Gui::ELengthUnit::Pixel, 32}, Error) &&
		        Root->Set(Gui::EProperty::Height, Gui::FLength{Gui::ELengthUnit::Pixel, 32}, Error) &&
		        Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 1}, Error) &&
		        Root->SetPaintCallback([](auto& Canvas, auto& Failure)
		        {
			        return Canvas.FillRect({0, 0, 8, 8}, {0, 1, 0, 1}, Failure);
		        }, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		auto Recording = Device->Recording;
		bool Presented = false;
		Recording->bFailBegin = true;
		Require(!Context->Render(Presented, Error) && !Recording->bSubmission,
		    "failed geometry batch begin left an active submission");
		Recording->bFailBegin = false;
		Recording->bFailEnd = true;
		Require(!Context->Render(Presented, Error) && !Recording->bSubmission && Recording->Cancels > 0,
		    "failed geometry upload was not cancelled");
		const auto FailedCompiles = Recording->Compiles;
		Recording->bFailEnd = false;
		Require(Context->Render(Presented, Error) && Recording->Compiles > FailedCompiles,
		    "retry retained geometry from a failed upload");
		const auto Compiles = Recording->Compiles;
		const auto Begins = Recording->Begins;
		Require(Context->Render(Presented, Error) && Recording->Compiles == Compiles && Recording->Begins == Begins,
		    "unchanged geometry created an unnecessary upload submission");
		for (bool ThrowEnd : {false, true})
		{
			Require(Root->Set(Gui::EProperty::Background,
			    ThrowEnd ? Graphics::FColor{1, 1, 0, 1} : Graphics::FColor{0, 0, 1, 1}, Error) &&
			    Context->Update(1, Error), Error);
			Recording->bFailEnd = true;
			Recording->bThrowEnd = ThrowEnd;
			Recording->bThrowCancel = true;
			Require(!Context->Render(Presented, Error) && !Recording->bSubmission &&
			    Error.find(ThrowEnd ? "injected end exception" : "injected end failure") != std::string::npos,
			    "geometry cancellation exception replaced the submission error");
			const auto CancelledCompiles = Recording->Compiles;
			Recording->bFailEnd = Recording->bThrowEnd = Recording->bThrowCancel = false;
			Require(Context->Render(Presented, Error) && Recording->Compiles > CancelledCompiles,
			    "cancellation exception retained geometry from a failed upload");
		}
		Require(Context->Close(Error), Error);
	}

	void CheckSubmissionRecovery()
	{
		auto Device = std::make_shared<FDevice>();
		Device->bComposition = true;
		auto Renderer = Device->Recording;
		std::string Error;
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({16, 16}, Error);
		RmlUi::FCompositionFrame Frame;
		Require(Frame.Initialize(Options, Renderer, Device->CreateSampler({}, Error), Error), Error);
		Frame.Begin(16, 16);
		Graphics::FDrawList2D Draws;
		Require(Draws.AddGeometry(Renderer->CreateGeometry(
		                              {{0, 0}, {16, 0}, {0, 16}}, {0, 1, 2}, Error),
		            Error) &&
		        Frame.AddDraw(Draws, Error),
		    Error);
		bool Presented = true;
		Renderer->bFailBegin = true;
		Require(!Frame.Submit(Presented, Error) && !Presented && Renderer->Begins == 0 && !Renderer->bSubmission,
		    "failed begin acquired or cancelled another submission");
		Renderer->bFailBegin = false;
		Renderer->bFailRender = true;
		Require(!Frame.Submit(Presented, Error) && Error == "injected render failure" &&
		        !Renderer->bSubmission && Renderer->Cancels == 1,
		    "failed draw left submission active");
		Renderer->bFailRender = false;
		Renderer->bThrowRender = true;
		bool Threw = false;
		try
		{
			Frame.Submit(Presented, Error);
		}
		catch (const std::exception &)
		{
			Threw = true;
		}
		Require(Threw && !Renderer->bSubmission && Renderer->Cancels == 2, "render exception left submission active");
		Renderer->bThrowRender = false;
		Renderer->bFailEnd = true;
		Require(!Frame.Submit(Presented, Error) && !Presented && !Renderer->bSubmission &&
		        Renderer->Cancels == 3,
		    "failed end lost cancellation or reported presentation");
		Renderer->bFailEnd = false;
		Require(Frame.Submit(Presented, Error) && !Renderer->bSubmission && Renderer->Ends == 1,
		    "submission could not retry after failure");
		Require(Frame.Close(Error) && Options.Texture->Close(Error), Error);

		Options.Texture.reset();
		Options.Surface = std::make_shared<FSurface>();
		Renderer = std::dynamic_pointer_cast<FRecordingRenderer>(Device->CreateRenderer2D(Error));
		Renderer->bAllowSurface = true;
		RmlUi::FCompositionFrame SurfaceFrame;
		Require(SurfaceFrame.Initialize(Options, Renderer, Device->CreateSampler({}, Error), Error), Error);
		SurfaceFrame.Begin(16, 16);
		const auto Ended = Renderer->Ends;
		const auto Cancelled = Renderer->Cancels;
		Require(SurfaceFrame.AddDraw(Draws, Error) && SurfaceFrame.Submit(Presented, Error) && !Presented &&
		        Renderer->Ends == Ended && Renderer->Cancels == Cancelled + 1,
		    "unavailable surface queued unused scratch passes");
		auto Capture = Device->CreateTexture({16, 16}, Error);
		SurfaceFrame.Begin(16, 16);
		Require(SurfaceFrame.AddDraw(Draws, Error) && SurfaceFrame.Capture(Capture, {0, 0, 16, 16}, false, {}, Error) &&
		        SurfaceFrame.Submit(Presented, Error) && !Presented && Renderer->Ends == Ended + 1,
		    "unavailable surface discarded a persistent layer capture");
		Require(SurfaceFrame.Close(Error) && Capture->Close(Error), Error);
	}

	void CheckLinearGradientVertices()
	{
		RmlUi::FGradient Gradient;
		Gradient.Kind = "linear-gradient";
		Gradient.Direction = {1, 0};
		Gradient.Stops = {{{255, 0, 0, 255}, {0, Rml::Unit::NUMBER}},
		    {{0, 0, 128, 128}, {1, Rml::Unit::NUMBER}}};
		std::vector<Graphics::FVertex2D> Vertices{{0, 0, 0, 0}, {10, 0, 1, 0}, {0, 10, 0, 1}};
		Require(RmlUi::TryLinearGradient(Gradient, Vertices) && Vertices[0].Color.R == 1 &&
		        std::abs(Vertices[1].Color.B - 128.f / 255) < .0001f &&
		        std::abs(Vertices[1].Color.A - 128.f / 255) < .0001f,
		    "linear gradient lost premultiplied stop colors");
		Vertices = {{0, 0, -1, 0}, {10, 0, 1, 0}, {0, 10, 0, 1}};
		Require(!RmlUi::TryLinearGradient(Gradient, Vertices) && Vertices[0].Color.R == 1,
		    "clamped gradient modified its raster fallback input");
		Vertices[0].U = 0;
		Vertices[1].Color.R = .5f;
		Require(!RmlUi::TryLinearGradient(Gradient, Vertices), "nonuniform modulation cannot be folded into vertex colors");
		Vertices[1].Color.R = 1;
		Gradient.bRepeating = true;
		Require(!RmlUi::TryLinearGradient(Gradient, Vertices), "repeating ramp lost its raster fallback");
	}

	void CheckInlineColorFilter()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		Device->bComposition = true;
		auto Renderer = Device->Recording;
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({16, 16}, Error);
		RmlUi::FCompositionFrame Frame;
		Require(Frame.Initialize(Options, Renderer, Device->CreateSampler({}, Error), Error), Error);
		Frame.Begin(16, 16);
		const auto Layer = Frame.Push(Error);
		Graphics::FDrawList2D Draws;
		Require(Layer != 0 && Draws.AddGeometry(Renderer->CreateGeometry({{0, 0, 0, 0}, {16, 0, 1, 0}, {0, 16, 0, 1}}, {0, 1, 2}, Error), Error) &&
		        Frame.AddDraw(Draws, Error) && Frame.Pop(Error),
		    Error);
		Graphics::FEffect2D Opacity;
		Opacity.Type = Graphics::EEffect2D::ColorMatrix;
		Opacity.Matrix[18] = 0.5f;
		bool Presented = false;
		Require(Frame.Composite(Layer, 0, false, {Opacity}, false, {}, Error) &&
		        Frame.Submit(Presented, Error),
		    Error);
		Require(Renderer->ColorFilters == 1 && Renderer->bPremultipliedFilter && Renderer->Submits <= 5,
		    "single color filter allocated an intermediate composition or lost premultiplied input");
		const auto Before = Renderer->Submits;
		Frame.Begin(16, 16);
		Require(Frame.AddDraw(Draws, Error) && Frame.Composite(0, 0, true, {Opacity}, false, {}, Error) &&
		        Frame.Submit(Presented, Error),
		    Error);
		Require(Renderer->ColorFilters == 2 && Renderer->Submits - Before <= 5,
		    "self color filtering failed to isolate its feedback in a single scratch pass");
		Require(Frame.Close(Error), Error);
	}

	void CheckRetirementRetry()
	{
		for (int Mode = 0; Mode < 5; ++Mode)
		{
			std::string Error;
			auto Device = std::make_shared<FDevice>();
			auto Recording = Device->Recording;
			const std::weak_ptr<FDevice> Weak = Device;
			RmlUi::FOptions Options;
			Options.Device = Device;
			Options.Texture = Device->CreateTexture({10, 10}, Error);
			Device->bFailSampler = Mode == 4;
			Recording->bFailClose = Mode < 2 || Mode == 4;
			Recording->bThrowClose = Mode == 2 || Mode == 3;
			auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
			Require(static_cast<bool>(Backend) == !Device->bFailSampler, "unexpected initialization result");
			Options = {};
			Device.reset();
			if (Mode % 2 == 0)
			{
				std::thread Worker([Owned = std::move(Backend)]() mutable
				{
					Owned.reset();
				});
				Worker.join();
			}
			else
			{
				Backend.reset();
			}
			Require(!RmlUi::CollectRetired(Error), "retirement close failure not reported");
			Require(!Weak.expired() && Recording->IsOpen(), "failed retirement lost resources needed for retry");
			Recording->bFailClose = false;
			Recording->bThrowClose = false;
			Require(RmlUi::CollectRetired(Error) && Weak.expired() && !Recording->IsOpen(),
			    "retirement retry did not finish cleanup");
		}
	}

	void CheckReleaseExceptions()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({10, 10}, Error);
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		Renderer.BeginFrame();
		Rml::Vertex Vertices[3]{};
		Vertices[1].position = {10, 0};
		Vertices[2].position = {0, 10};
		const int Indices[]{0, 1, 2};
		const auto Handle = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		auto Geometry = Device->Recording->LastGeometry.lock();
		Require(Handle && Geometry, "geometry for release exception");
		Geometry->bThrowClose = true;
		Renderer.ReleaseGeometry(Handle);
		bool bPresented = true;
		Require(!Renderer.Submit(bPresented, Error) && !bPresented && Geometry->IsOpen() &&
		        Error.find("resource_close_exception") != std::string::npos,
		    "geometry release exception was not retained");
		Geometry->bThrowClose = false;
		Renderer.BeginFrame();
		Require(!Geometry->IsOpen(), "geometry close exception prevented retry");
		const Rml::byte Pixel[]{255, 255, 255, 255};
		const auto TextureHandle = Renderer.GenerateTexture({Pixel, 4}, {1, 1});
		auto Texture = Device->LastTexture.lock();
		Require(TextureHandle && Texture, "texture for release exception");
		Texture->bThrowClose = true;
		Renderer.ReleaseTexture(TextureHandle);
		Require(!Renderer.Close(Error) && Texture->IsOpen() &&
		        Error.find("resource_close_exception") != std::string::npos,
		    "texture release exception was not retained");
		Texture->bThrowClose = false;
		Require(Renderer.Close(Error) && !Texture->IsOpen() && Options.Texture->IsOpen(), Error);

		auto Context = Gui::FContext::Create(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error),
		    {10, 10, 1}, Error);
		auto Root = Gui::FWidget::Create();
		Require(Context && Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 1}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error) && Context->Render(bPresented, Error),
		    Error);
		Geometry = Device->Recording->LastGeometry.lock();
		Require(Geometry != nullptr, "real RmlUi geometry for close exception");
		Geometry->bThrowClose = true;
		Require(!Context->Close(Error) && !Context->IsOpen() && Geometry->IsOpen(),
		    "RmlUi destruction did not retain failed geometry cleanup");
		Geometry->bThrowClose = false;
		Require(Context->Close(Error) && !Geometry->IsOpen() && Options.Texture->IsOpen(), Error);
	}

	void CheckFailedTextureUpload()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({10, 10}, Error);
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		Renderer.BeginFrame();
		Device->bFailUpload = true;
		const Rml::byte Pixel[]{255, 255, 255, 255};
		Require(!Renderer.GenerateTexture({Pixel, 4}, {1, 1}), "texture upload failure was ignored");
		const auto Weak = Device->LastTexture;
		Require(!Weak.expired() && !Renderer.Close(Error), "failed texture cleanup lost the resource");
		Weak.lock()->bFailClose = false;
		Require(Renderer.Close(Error) && Weak.expired(), "failed texture cleanup could not be retried");
	}

	void CheckContainedPaintClip()
	{
		auto Device = std::make_shared<FDevice>();
		std::string Error;
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({100, 100}, Error);
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error) && !Renderer.CanComposite(), Error);
		Renderer.BeginFrame();
		const auto Paint = [](Gui::FPaintContext &Canvas, std::string &Failure)
		{
			return Canvas.PushClip({0, 0, 20, 20}, Failure) &&
			    Canvas.FillRect({0, 0, 20, 20}, {1, 0, 0, 1}, Failure) && Canvas.PopClip(Failure);
		};
		Renderer.RenderPaint(1, Paint, {0, 0, 20, 20}, {7.5f, 9.5f}, 1.25);
		bool bPresented = false;
		Require(Renderer.Submit(bPresented, Error), "contained paint clip needs no composition: " + Error);
		Require(Device->Recording->Draws.size() == 1 && Device->Recording->Compiles == 1,
		    "a contained paint mesh must not allocate or draw a mask");
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 20, 20}, {8, 10}, 1.25);
		Require(Renderer.Submit(bPresented, Error) && Device->Recording->Compiles == 1,
		    "contained paint geometry remains cached across movement");
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, [](Gui::FPaintContext &Canvas, std::string &Failure)
		{
			return Canvas.PushClip({0, 0, 10, 20}, Failure) &&
			    Canvas.FillRect({0, 0, 20, 20}, {1, 0, 0, 1}, Failure) && Canvas.PopClip(Failure);
		}, {0, 0, 20, 20}, {}, 1);
		Require(!Renderer.Submit(bPresented, Error) && Error == "rmlui.transformed_clip_unavailable",
		    "paint crossing a clip boundary must retain the mask path");
		Require(Renderer.Close(Error), Error);
	}

	void CheckPaintBatching()
	{
		for (const bool SmallLimit : {false, true})
		{
			auto Device = std::make_shared<FDevice>();
			if (SmallLimit)
			{
				Device->Recording->GeometryLimit = 200;
			}
			std::string Error;
			RmlUi::FOptions Options;
			Options.Device = Device;
			Options.Texture = Device->CreateTexture({100, 100}, Error);
			RmlUi::FRenderer Renderer(Options);
			Require(Renderer.Initialize(Error), Error);
			int PaintCalls = 0;
			auto Paint = [&](Gui::FPaintContext &Canvas, std::string &Failure)
			{
				++PaintCalls;
				for (int I = 0; I < 64; ++I)
				{
					if (!Canvas.FillRect({double(I), 0, 2, 2}, {1, 0, 0, .5f}, Failure)) return false;
				}
				return true;
			};
			bool Presented = false;
			Renderer.BeginFrame();
			Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
			Require(Renderer.Submit(Presented, Error), Error);
			Require(PaintCalls == 1 && Device->Recording->Draws.size() == (SmallLimit ? 64 : 1),
			    "compatible paint was not batched or fallback repeated the callback");
			const int Compiles = Device->Recording->Compiles;
			Renderer.BeginFrame();
			Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {1, 2}, 1);
			Require(Renderer.Submit(Presented, Error) && Device->Recording->Compiles == Compiles,
			    "unchanged paint or unbatched fallback was rebuilt");
			Require(Renderer.Close(Error), Error);
		}
		// Distinct clip states remain ordered batches; changing one preserves the other.
		auto Device = std::make_shared<FDevice>();
		std::string Error;
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({100, 100}, Error);
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		float Blue = .5f;
		auto Paint = [&](Gui::FPaintContext &C, std::string &E)
		{
			return C.FillRect({0, 0, 10, 10}, {1, 0, 0, 1}, E) && C.PushClip({0, 0, 50, 50}, E) &&
			    C.FillRect({10, 0, 10, 10}, {0, 0, Blue, 1}, E) && C.PopClip(E);
		};
		bool Presented = false;
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Device->Recording->Draws.size() == 2, Error);
		const auto First = Device->Recording->Draws.front().Geometry;
		const int Compiles = Device->Recording->Compiles;
		Blue = 1;
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Device->Recording->Compiles == Compiles + 1 &&
		        Device->Recording->Draws.front().Geometry == First,
		    "unmodified paint batch was released");
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, [](Gui::FPaintContext&, std::string&) { return true; }, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Device->Recording->Draws.empty(),
		    "empty borrowed paint retained commands from the previous frame");
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Device->Recording->Draws.size() == 2 &&
		        Device->Recording->Compiles == Compiles + 3,
		    "paint did not recover after an empty frame: " + Error);
		Require(Renderer.Close(Error), Error);
	}

	void CheckPaintImageRetention()
	{
		auto Device = std::make_shared<FDevice>();
		std::string Error;
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({100, 100}, Error);
		Options.ReadResource = [](const auto &, auto &Bytes, auto &)
		{
			Bytes = {1};
			return true;
		};
		int Decodes = 0;
		Options.DecodeImage = [&](const auto &, const auto &, Gui::FDecodedImage &Image, auto &)
		{
			++Decodes;
			Image.Width = Image.Height = 1;
			Image.Rgba = {255, 255, 255, 255};
			return true;
		};
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		double X = 0;
		std::string Image = "memory:first";
		auto Paint = [&](Gui::FPaintContext &Canvas, std::string &Failure)
		{
			return Canvas.DrawImage({X, 0, 10, 10}, Image, {1, 1, 1, 1}, Failure);
		};
		bool Presented = false;
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error), Error);
		const auto First = Device->Recording->Draws.front().Texture;
		X = 20;
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Decodes == 1 &&
		        Device->Recording->Draws.front().Texture == First,
		    "geometry motion reloaded its unchanged image");
		Image = "memory:second";
		Renderer.BeginFrame();
		Renderer.RenderPaint(1, Paint, {0, 0, 100, 100}, {}, 1);
		Require(Renderer.Submit(Presented, Error) && Decodes == 2 && !First->IsOpen(),
		    "changed paint image retained its retired texture");
		Require(Renderer.Close(Error), Error);
	}

	void CheckEffectRebuildSnapshot()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		auto Options = GuiTestResourceOptions();
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({100, 100}, Error);
		auto Backend = RmlUi::CreateBackend(Options, Error);
		Require(Backend != nullptr, Error);
		std::vector<Gui::FBackendNode> Nodes(3);
		for (std::size_t I = 0; I < Nodes.size(); ++I)
		{
			Nodes[I].Id = I + 1;
			Nodes[I].Parent = I ? 1 : 0;
			Nodes[I].Properties.Height = {Gui::ELengthUnit::Pixel, 20};
		}
		Nodes[0].Properties.Height.Value = 100;
		Nodes[1].Control.Kind = Gui::EControlKind::TextBox;
		Nodes[1].Control.Text = "abc";
		Nodes[2].Properties.Background = {1, 0, 0, 1};
		Nodes[2].Properties.Filter = "blur(2px)";
		std::vector<Gui::FLayoutBox> Layout;
		bool Presented = false;
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(0, Layout, Error), Error);
		Require(!Backend->Render(Presented, Error) && Error == "rmlui.filters_unavailable",
		    "effect rebuild fixture did not fail: " + Error);
		Nodes[2].Properties.Filter.clear();
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(1, Layout, Error) &&
		        Backend->Render(Presented, Error), "same-topology effect rebuild failed: " + Error);
		Require(Backend->FocusTextControl(2, {}, Error) && Backend->SetTextSelection(2, 0, 0, Error), Error);
		Platform::FEvent Input;
		Input.Type = Platform::EEventType::TextInput;
		Input.Text = "X";
		Gui::FTextEditResult Edited;
		Require(Backend->EditText(2, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Xabc",
		    "effect-only document rebuild lost snapshot identity indices: " + Error);
		Require(Backend->Synchronize({100, 100, 1}, Nodes, Error) && Backend->Update(2, Layout, Error) &&
		        Backend->SetTextSelection(2, 0, 0, Error), Error);
		Input.Text = "Y";
		Require(Backend->EditText(2, Input, Edited, Error) && Edited.bChanged && Edited.Text == "Yabc",
		    "effect rebuild retained a stale edited-text snapshot: " + Error);
		Require(Backend->Close(Error), Error);
	}

	void CheckFrameFailures()
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Device->CreateTexture({100, 100}, Error);
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		Renderer.BeginFrame();
		Rml::Vertex Vertices[3]{};
		Vertices[1].position.x = 10;
		Vertices[2].position.y = 10;
		const int Indices[]{0, 1, 2};
		const auto Geometry = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		Require(Geometry != 0, "failure test geometry");
		Renderer.RenderGeometry(Geometry, {}, 0);
		Renderer.ReleaseGeometry(Geometry);
		Require(!Renderer.CompileFilter("blur", {}), "recording backend unexpectedly supports filters");
		Require(!Renderer.CompileShader("linear-gradient", {}), "failed frame compiled a later shader");
		Renderer.BeginFrame();
		bool Presented = false;
		Require(!Renderer.Submit(Presented, Error) && Error == "rmlui.filters_unavailable",
		    "resource cleanup erased the persistent filter error");
		Require(Renderer.Close(Error), Error);

		Options.ReadResource = [](const auto &, auto &, auto &)
		{
			return false;
		};
		auto Context = Gui::FContext::Create(RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error),
		    {100, 100, 1}, Error);
		auto Root = Gui::FWidget::Create();
		Require(Context &&
		        Root->SetPaintCallback(
		            [](auto &Canvas, auto &Failure)
		{
			return Canvas.DrawImage({0, 0, 10, 10}, "memory:missing", {1, 1, 1, 1}, Failure);
		}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		Require(!Context->Render(Presented, Error) && Error == "rmlui.image_load_failed",
		    "custom drawing silently ignored an image reader failure");
		Require(Context->Close(Error), Error);
	}

	void CheckCancelledDraws()
	{
		for (bool bCancelFrame : {true, false})
		{
			std::string Error;
			auto Device = std::make_shared<FDevice>();
			RmlUi::FOptions Options;
			Options.Device = Device;
			Options.Texture = Device->CreateTexture({10, 10}, Error);
			RmlUi::FRenderer Renderer(Options);
			Require(Renderer.Initialize(Error), Error);
			Renderer.BeginFrame();
			Rml::Vertex Vertices[3]{};
			Vertices[1].position = {10, 0};
			Vertices[2].position = {0, 10};
			const int Indices[]{0, 1, 2};
			const auto Geometry = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
			const Rml::byte Pixel[]{255, 255, 255, 255};
			const auto Texture = Renderer.GenerateTexture({Pixel, 4}, {1, 1});
			Require(Geometry && Texture, "cancelled frame resources");
			Renderer.RenderGeometry(Geometry, {}, Texture);
			Renderer.ReleaseGeometry(Geometry);
			Renderer.ReleaseTexture(Texture);
			bool bPresented = true;
			if (bCancelFrame)
			{
				Renderer.BeginFrame();
			}
			else
			{
				Renderer.EnableClipMask(true);
				Require(!Renderer.Submit(bPresented, Error) && !bPresented, "failed frame submitted");
				Renderer.BeginFrame();
			}
			Require(Device->Recording->Submits == 0, "cancelled draw reached the renderer");
			Require(Renderer.Submit(bPresented, Error) && Device->Recording->Draws.empty(),
			    "cancelled draws survived into the next frame");
			Require(Renderer.Close(Error) && Options.Texture->IsOpen(), Error);
		}
	}
} // namespace

int main()
{
	using namespace VeroScript;
	try
	{
		std::string Error;
		auto Device = std::make_shared<FDevice>();
		auto Target = Device->CreateTexture({100, 100}, Error);
		RmlUi::FOptions Options;
		Options.Device = Device;
		Options.Texture = Target;
		RmlUi::FRenderer Renderer(Options);
		Require(Renderer.Initialize(Error), Error);
		Renderer.BeginFrame();
		Rml::Vertex Vertices[3]{};
		Vertices[0].position = {0, 0};
		Vertices[1].position = {20, 0};
		Vertices[2].position = {0, 20};
		for (auto &Vertex : Vertices)
		{
			Vertex.colour = {128, 0, 0, 128};
		}
		const int Indices[]{0, 1, 2};
		const auto Geometry = Renderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		Require(Geometry != 0, "geometry compilation");
		Require(std::abs(Device->Recording->Vertices[0].Color.R - 128.0 / 255) < 0.001 &&
		        std::abs(Device->Recording->Vertices[0].Color.A - 128.0 / 255) < 0.001,
		    "double premultiplication");
		const Rml::byte Pixel[]{128, 0, 0, 128};
		const auto Texture = Renderer.GenerateTexture({Pixel, 4}, {1, 1});
		Require(Texture && Device->Pixels == std::vector<std::uint8_t>({128, 0, 0, 128}), "texture premultiplication");
		Renderer.SetScissorRegion(Rml::Rectanglei::FromPositionSize({2, 3}, {12, 13}));
		Renderer.EnableScissorRegion(true);
		Renderer.RenderGeometry(Geometry, {4, 5}, Texture);
		Renderer.ReleaseGeometry(Geometry);
		Renderer.ReleaseTexture(Texture);
		bool bPresented = true;
		Require(Renderer.Submit(bPresented, Error) && !bPresented, Error);
		Require(Device->Recording->Draws.size() == 1, "draw count");
		const auto &Draw = Device->Recording->Draws[0];
		Require(!Draw.Geometry->IsOpen() && !Draw.Texture->IsOpen(), "released resources retained after submission");
		Require(Draw.bClip && Draw.Clip.X == 2 && Draw.Clip.Height == 13 && Draw.Transform.Values[12] == 4 &&
		        Draw.Transform.Values[13] == 5 && Draw.Blend == Graphics::EBlendMode::PremultipliedAlpha &&
		        Draw.VertexColorMode == Graphics::EVertexColorMode::Premultiplied,
		    "draw state mapping");
		Renderer.BeginFrame();
		Renderer.EnableClipMask(true);
		const int Submits = Device->Recording->Submits;
		Require(!Renderer.Submit(bPresented, Error) && Device->Recording->Submits == Submits,
		    "unsupported feature submitted a partial frame");
		Renderer.ReleaseTexture(Texture);
		Renderer.ReleaseGeometry(Geometry);
		Require(Renderer.Close(Error) && Target->IsOpen(), "borrowed target closed");

		Device = std::make_shared<FDevice>();
		Target = Device->CreateTexture({100, 100}, Error);
		Options.Device = Device;
		Options.Texture = Target;
		auto Backend = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Require(Backend != nullptr, Error);
		auto Context = Gui::FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = Gui::FWidget::Create();
		Require(Root->Set(Gui::EProperty::Background, Graphics::FColor{1, 0, 0, 0.5f}, Error), Error);
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) && Context->Render(bPresented, Error),
		    Error);
		Require(!Device->Recording->Draws.empty(), "real RmlUi produced no draw commands");
		Require(std::abs(Device->Recording->Vertices[0].Color.R - 0.5) < 0.01 &&
		        std::abs(Device->Recording->Vertices[0].Color.A - 0.5) < 0.01,
		    "background alpha mapping");
		const int Compiles = Device->Recording->Compiles;
		Require(Context->Update(1, Error) && Context->Render(bPresented, Error), Error);
		Require(Device->Recording->Compiles == Compiles, "unchanged frame rebuilt geometry");
		Require(Root->Set(Gui::EProperty::Opacity, 0.5, Error) && Context->Update(2, Error) &&
		        Context->Render(bPresented, Error),
		    Error);
		Require(std::abs(Device->Recording->Vertices[0].Color.A - 0.25) < 0.01, "opacity mapping");
		auto Child = Gui::FWidget::Create();
		Require(Root->AddChild(Child, Error) && Context->Update(3, Error), Error);
		const int BeforeGroup = Device->Recording->Submits;
		Require(!Context->GetCapabilities().bGroupOpacity && !Context->Render(bPresented, Error) &&
		        Error.find("group_opacity_unavailable") != std::string::npos &&
		        Device->Recording->Submits == BeforeGroup,
		    "unsupported group opacity rendered an incorrect frame");
		Require(Root->Set(Gui::EProperty::Opacity, 1.0, Error) && Context->Update(4, Error) &&
		        Context->Render(bPresented, Error),
		    Error);
		Require(Root->Set(Gui::EProperty::Opacity, 0.5, Error) && Context->Update(5, Error) &&
		        !Context->Render(bPresented, Error) && Error.find("group_opacity_unavailable") != std::string::npos,
		    "unchanged topology did not use the current parent's opacity");
		Require(Root->Set(Gui::EProperty::Opacity, 1.0, Error) && Context->Update(6, Error) &&
		        Context->Render(bPresented, Error),
		    "unchanged topology retained the previous parent's opacity: " + Error);
		Require(Context->Close(Error) && Target->IsOpen(), Error);

		Target = Device->CreateTexture({100, 100, Graphics::ETextureFormat::Rgba8Srgb}, Error);
		Options.Texture = Target;
		RmlUi::FRenderer LinearRenderer(Options);
		Require(LinearRenderer.Initialize(Error), Error);
		LinearRenderer.BeginFrame();
		for (auto &Vertex : Vertices)
		{
			Vertex.colour = {64, 64, 64, 128};
		}
		const auto LinearGeometry = LinearRenderer.CompileGeometry({Vertices, 3}, {Indices, 3});
		Require(LinearGeometry && std::abs(Device->Recording->Vertices[0].Color.R - 0.107f) < 0.002f,
		    "sRGB target did not decode straight color before premultiplication");
		const Rml::byte GrayPixel[]{64, 64, 64, 128};
		const auto LinearTexture = LinearRenderer.GenerateTexture({GrayPixel, 4}, {1, 1});
		Require(LinearTexture && Device->Pixels == std::vector<std::uint8_t>({27, 27, 27, 128}),
		    "generated texture did not convert premultiplied sRGB to linear");
		LinearRenderer.ReleaseGeometry(LinearGeometry);
		LinearRenderer.ReleaseTexture(LinearTexture);
		Require(LinearRenderer.Close(Error), Error);

		auto Direct = RmlUi::CreateBackend(VeroScript::GuiTestResourceOptions(Options), Error);
		Require(Direct != nullptr, Error);
		const int BeforeUpdate = Device->Recording->Submits;
		Require(!Direct->Render(bPresented, Error) && !bPresented && Device->Recording->Submits == BeforeUpdate,
		    "backend rendered before successful update");
		std::vector<Gui::FBackendNode> Nodes(1);
		Nodes[0].Id = 1;
		std::vector<Gui::FLayoutBox> Boxes;
		Require(Direct->Synchronize({100, 100, 1}, Nodes, Error), Error);
		Require(!Direct->Render(bPresented, Error), "backend rendered pending layout");
		Require(Direct->Update(0, Boxes, Error) && Direct->Render(bPresented, Error), Error);
		Nodes[0].Properties.Opacity = 2;
		Require(!Direct->Synchronize({100, 100, 1}, Nodes, Error) && !Direct->Render(bPresented, Error) &&
		        !Direct->Update(1, Boxes, Error),
		    "failed synchronization did not invalidate the frame");
		Nodes[0].Properties.Opacity = 1;
		Require(Direct->Synchronize({100, 100, 1}, Nodes, Error) && Direct->Update(1, Boxes, Error) &&
		        Direct->Render(bPresented, Error) && Direct->Close(Error),
		    Error);
		CheckReleaseExceptions();
		CheckRetirementRetry();
		CheckCancelledDraws();
		CheckFailedTextureUpload();
		CheckFrameFailures();
		CheckEffectRebuildSnapshot();
		CheckContainedPaintClip();
		CheckPaintBatching();
		CheckPaintImageRetention();
		CheckInlineColorFilter();
		CheckLinearGradientVertices();
		CheckSubmissionRecovery();
		CheckGeometryUploadRecovery();
		std::cout << "RmlUi graphics adapter contracts passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
