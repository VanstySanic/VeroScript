// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptGraphicsComposition2D.h"
#include <emscripten.h>
#include <iostream>
#include <cmath>

namespace
{
	using namespace VeroScript;
	struct FTest
	{
		std::shared_ptr<Platform::IService> Platform;
		std::shared_ptr<Platform::IWindow> Window;
		std::shared_ptr<Graphics::IService> Service;
		std::shared_ptr<Graphics::IDeviceRequest> Request;
		std::shared_ptr<Graphics::IDevice> Device;
		std::shared_ptr<Graphics::IRenderer2D> Renderer;
		std::shared_ptr<Graphics::ICompositor2D> Compositor;
		std::shared_ptr<Graphics::ITexture> Texture;
		std::shared_ptr<Graphics::ISurface> Surface;
		std::shared_ptr<Graphics::IGeometry2D> Quad;
		std::shared_ptr<Graphics::IReadbackRequest> Readback;
		bool Success = false;
		std::string Error;
		int Stage = 0;
		double Deadline = emscripten_get_now() + 20000;
	};
	void Finish(FTest& T, bool Success, const std::string& Error)
	{
		T.Success = Success;
		T.Error = Error;
		T.Stage = 2;
		T.Deadline = emscripten_get_now() + 10000;
		std::string Ignored;
		if (T.Compositor)
		{
			T.Compositor->Close(Ignored);
		}
		if (T.Renderer)
		{
			T.Renderer->Close(Ignored);
		}
		if (T.Quad)
		{
			T.Quad->Close(Ignored);
		}
		if (T.Surface)
		{
			T.Surface->Close(Ignored);
		}
		if (T.Texture)
		{
			T.Texture->Close(Ignored);
		}
		T.Readback.reset();
		if (T.Device)
		{
			T.Device->Close(Ignored);
		}
		if (T.Window)
		{
			T.Window->Close(Ignored);
		}
		T.Request.reset();
		T.Device.reset();
		T.Service.reset();
		T.Quad.reset();
		T.Surface.reset();
		T.Texture.reset();
		T.Compositor.reset();
		T.Renderer.reset();
		T.Window.reset();
	}
	void Complete(FTest& T)
	{
		const bool Success = T.Success;
		std::cout << (Success ? "WebGPU browser passed" : "WebGPU browser failed: " + T.Error) << std::endl;
		T.Platform.reset();
		emscripten_run_script(Success ? "document.body.dataset.result='passed'"
									  : "document.body.dataset.result='failed'");
		emscripten_cancel_main_loop();
		delete &T;
	}
	void Tick(void* Data)
	{
		auto& T = *static_cast<FTest*>(Data);
		std::string E;
		if (T.Stage == 2)
		{
			bool Pending = true;
			if (!Sdl3::HasPendingRetirements(Pending, E))
			{
				T.Success = false;
				T.Error = E;
				Complete(T);
			}
			else if (!Pending)
			{
				Complete(T);
			}
			else if (emscripten_get_now() > T.Deadline)
			{
				T.Success = false;
				T.Error = "native callback retirement deadline exceeded";
				Complete(T);
			}
			return;
		}
		if (emscripten_get_now() > T.Deadline)
		{
			Finish(T, false, "asynchronous deadline exceeded");
			return;
		}
		if (T.Stage == 0)
		{
			const auto Status = T.Request->Poll(E);
			if (Status == Graphics::ERequestStatus::Pending)
			{
				return;
			}
			T.Device = T.Request->GetDevice(E);
			if (!T.Device)
			{
				Finish(T, false, E);
				return;
			}
			T.Renderer = T.Device->CreateRenderer2D(E);
			T.Compositor = Graphics::CreateCompositor2D(T.Device, {}, E);
			T.Texture = T.Device->CreateTexture({16, 16}, E);
			T.Surface = T.Device->CreateSurface(T.Window, E);
			if (!T.Renderer || !T.Compositor || !T.Texture || !T.Surface)
			{
				Finish(T, false, E);
				return;
			}
			T.Quad = T.Renderer->CreateGeometry({{0, 0}, {16, 0}, {16, 16}, {0, 16}}, {0, 1, 2, 0, 2, 3}, E);
			Graphics::FDrawList2D Red, Mask;
			Graphics::FComposition2D Frame;
			bool Presented = false;
			if (!T.Quad || !Red.SetTint({1, 0, 0, 1}, E) || !Red.AddGeometry(T.Quad, E) ||
				!Mask.PushClip({0, 0, 8, 16}, E) || !Mask.AddGeometry(T.Quad, E) || !Frame.BeginLayer(0.5f, E) ||
				!Frame.PushMask(Mask, false, E) || !Frame.AddDraw(Red, E) || !Frame.PopMask(E) || !Frame.EndLayer(E) ||
				!T.Compositor->RenderTexture(T.Texture, Frame, {}, E) ||
				!T.Compositor->RenderSurface(T.Surface, 16, 16, Frame, {}, Presented, E) || !Presented)
			{
				Finish(T, false, E);
				return;
			}
			T.Readback = T.Device->ReadTextureAsync(T.Texture, E);
			auto Cancelled = T.Device->ReadTextureAsync(T.Texture, E);
			std::vector<std::uint8_t> Unchanged{42};
			if (!Cancelled || !Cancelled->Cancel(E) || !Cancelled->Cancel(E) ||
				Cancelled->Poll(E) != Graphics::ERequestStatus::Cancelled || Cancelled->GetPixels(Unchanged, E) ||
				Unchanged != std::vector<std::uint8_t>{42})
			{
				Finish(T, false, "pending readback cancellation failed: " + E);
				return;
			}
			Cancelled.reset();
			if (!T.Readback)
			{
				Finish(T, false, E);
				return;
			}
			T.Stage = 1;
		}
		else
		{
			const auto Status = T.Readback->Poll(E);
			if (Status == Graphics::ERequestStatus::Pending)
			{
				return;
			}
			std::vector<std::uint8_t> P;
			if (Status != Graphics::ERequestStatus::Succeeded || !T.Readback->GetPixels(P, E) || P.size() != 1024)
			{
				Finish(T, false, E);
				return;
			}
			const auto A = (4 * 16 + 4) * 4, B = (4 * 16 + 12) * 4;
			const bool Success =
				std::abs(P[A] - 128) <= 2 && std::abs(P[A + 3] - 128) <= 2 && P[B] == 0 && P[B + 3] == 0;
			Finish(T, Success, Success ? "" : "GPU pixel mismatch");
		}
	}
}
int main()
{
	using namespace VeroScript;
	std::string E;
	auto T = std::make_unique<FTest>();
	T->Platform = Sdl3::CreatePlatform({}, E);
	T->Service = Sdl3::CreateGraphics({}, E);
	Platform::FWindowOptions W;
	W.Width = W.Height = 16;
	W.GraphicsApi = "webgpu";
	if (T->Platform)
	{
		T->Window = T->Platform->CreateWindow(W, E);
	}
	Graphics::FDeviceOptions O;
	O.Driver = "webgpu";
	if (T->Service)
	{
		T->Request = T->Service->CreateDeviceAsync(O, E);
	}
	if (!T->Window || !T->Request)
	{
		std::cerr << E << std::endl;
		emscripten_run_script("document.body.dataset.result='failed'");
		return 1;
	}
	emscripten_set_main_loop_arg(Tick, T.release(), 0, true);
	return 0;
}
