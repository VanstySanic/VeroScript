// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#if VS_TEST_GPU
#include "VeroScriptGraphics.h"
#endif
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
	using namespace VeroScript;
	int Failures = 0;
	void Check(bool bPassed, const char* Message, const std::string& Error = {})
	{
		if (!bPassed)
		{
			++Failures;
			std::cerr << Message << ": " << Error << '\n';
		}
	}

	int PlatformTests(bool bHiDpi = false)
	{
		std::string Error;
		auto Platform = Sdl3::CreatePlatform({}, Error);
		if (!Platform)
		{
			std::cerr << Error << '\n';
			return bHiDpi ? 77 : 1;
		}
		Check(!Sdl3::CreatePlatform({}, Error), "second owned pump rejected");
		const auto Capabilities = Platform->GetCapabilities();
		const auto *Driver = SDL_GetCurrentVideoDriver();
		if (Driver && !SDL_strcmp(Driver, "dummy"))
		{
			Check(!Capabilities.bMouseCapture && !Capabilities.bSystemCursor,
				  "headless windows do not advertise unsupported mouse services");
		}
		else if (Driver && !SDL_strcmp(Driver, "wayland"))
		{
			Check(!Capabilities.bMouseCapture, "Wayland keeps implicit pointer grabs without explicit SDL capture");
		}
		else if (Driver && !SDL_strcmp(Driver, "windows"))
		{
			Check(Capabilities.bMouseCapture && Capabilities.bSystemCursor, "Windows native mouse services");
		}
		Platform::FWindowOptions Options;
		Options.Title = "VeroScript platform test";
		Options.Width = 96;
		Options.Height = 64;
		Options.bVisible = false;
		auto Window = Platform->CreateWindow(Options, Error);
		if (!Window)
		{
			std::cerr << Error << '\n';
			return 1;
		}
		Platform::FWindowMetrics Metrics;
		Check(Window->GetMetrics(Metrics, Error) && Metrics.Width == 96 && Metrics.Height == 64, "window metrics",
			  Error);
		Check(std::isfinite(Metrics.DisplayScale) && Metrics.DisplayScale > 0, "positive UI display scale");
		if (bHiDpi)
		{
			Check(std::abs(Metrics.DisplayScale - 2) < 0.01 && std::abs(Metrics.ScaleX - 1) < 0.01 &&
					  std::abs(Metrics.ScaleY - 1) < 0.01,
				  "200% UI scaling remains distinct from pixel density");
		}
		Check(!Window->SetSize(std::numeric_limits<std::int64_t>::max(), 1, Error), "checked size narrowing");
		bool bWrongThreadAccepted = true;
		std::thread Worker(
			[&]
			{
				std::string Local;
				bWrongThreadAccepted = Window->Close(Local);
			});
		Worker.join();
		Check(!bWrongThreadAccepted && Window->IsOpen(), "wrong-thread close preserves window");
		Platform::FEvent Event;
		bool bAvailable = false;
		for (unsigned Index = 0; Index < 100; ++Index)
		{
			Check(Platform->PollEvent(Event, bAvailable, Error), "initial event drain", Error);
			if (!bAvailable)
			{
				break;
			}
		}
		int Count = 0;
		SDL_Window** NativeWindows = SDL_GetWindows(&Count);
		Check(Count == 1 && NativeWindows, "single native runtime window");
		const auto NativeId = NativeWindows && Count ? SDL_GetWindowID(NativeWindows[0]) : 0;
		SDL_free(NativeWindows);
		for (auto NativeType : {SDL_EVENT_WINDOW_MOUSE_ENTER, SDL_EVENT_WINDOW_MOUSE_LEAVE})
		{
			SDL_Event Boundary{};
			Boundary.type = NativeType;
			Boundary.window.windowID = NativeId;
			Check(SDL_PushEvent(&Boundary), "inject mouse boundary event");
			bool bSawBoundary = false;
			const auto Expected = NativeType == SDL_EVENT_WINDOW_MOUSE_ENTER ? Platform::EEventType::MouseEntered
																			 : Platform::EEventType::MouseLeft;
			for (unsigned Index = 0; Index < 100; ++Index)
			{
				Check(Platform->PollEvent(Event, bAvailable, Error), "read mouse boundary", Error);
				if (bAvailable && Event.Type == Expected)
				{
					bSawBoundary = Event.WindowId == Window->GetId() &&
								   Event.DeviceKind == Platform::EInputDeviceKind::Mouse && Event.DeviceId == 0;
					break;
				}
			}
			Check(bSawBoundary, "native mouse boundary translation");
		}
		SDL_Event Native{};
		Native.type = SDL_EVENT_TEXT_EDITING;
		Native.edit.windowID = NativeId;
		Native.edit.text = "UTF-8 \xe6\x96\x87\xe5\xad\x97";
		Native.edit.start = 1;
		Native.edit.length = 2;
		Check(SDL_PushEvent(&Native), "inject IME event");
		bool bSawEditing = false;
		for (unsigned Index = 0; Index < 100; ++Index)
		{
			Check(Platform->PollEvent(Event, bAvailable, Error), "read IME", Error);
			if (bAvailable && Event.Type == Platform::EEventType::TextEditing)
			{
				bSawEditing = Event.WindowId == Window->GetId() && Event.SelectionStart == 1 &&
							  Event.SelectionLength == 2 && Event.Text == Native.edit.text;
				break;
			}
		}
		Check(bSawEditing, "owned UTF-8 text and character selection");
		Native = {};
		Native.type = SDL_EVENT_KEY_DOWN;
		Native.key.windowID = NativeId;
		Native.key.key = SDLK_A;
		Native.key.scancode = SDL_SCANCODE_A;
		Native.key.mod = SDL_KMOD_LSHIFT | SDL_KMOD_RCTRL;
		Native.key.repeat = true;
		Check(SDL_PushEvent(&Native), "inject key event");
		bool bSawKey = false;
		for (unsigned Index = 0; Index < 100; ++Index)
		{
			Check(Platform->PollEvent(Event, bAvailable, Error), "read key", Error);
			if (bAvailable && Event.Type == Platform::EEventType::KeyDown)
			{
				bSawKey = Event.Key == Platform::EKey::A && Event.ScanCode == 4 && Event.bRepeat &&
						  Event.Modifiers == (static_cast<unsigned>(Platform::EModifier::Shift) |
											  static_cast<unsigned>(Platform::EModifier::Control));
				break;
			}
		}
		Check(bSawKey, "backend-independent key and modifier mapping");
		Native.key.key = 'A';
		Native.common.timestamp = 0;
		Check(SDL_PushEvent(&Native), "inject uppercase key event");
		bSawKey = false;
		for (unsigned Index = 0; Index < 100; ++Index)
		{
			Check(Platform->PollEvent(Event, bAvailable, Error), "read uppercase key", Error);
			if (bAvailable && Event.Type == Platform::EEventType::KeyDown)
			{
				bSawKey = Event.Key == Platform::EKey::A;
				break;
			}
		}
		Check(bSawKey, "uppercase native hint preserves logical key identity");
		#if !VS_TEST_GPU
		Check(!Sdl3::CreateGraphics({}, Error) && Error.find("graphics.unsupported:") == 0,
			  "disabled GPU reports unsupported");
		#endif
		Sdl3::FPlatformOptions ExternalOptions;
		ExternalOptions.PumpMode = Platform::EEventPumpMode::External;
		auto External = Sdl3::CreatePlatform(ExternalOptions, Error);
		Check(External && !External->GetCapabilities().bOwnsEventPump, "external pump shares runtime");
		Platform::FEvent Injected;
		Injected.Type = Platform::EEventType::PointerMove;
		Injected.WindowId = Window->GetId();
		Injected.X = 12.5;
		Check(External->SubmitEvent(Injected, Error) && External->PollEvent(Event, bAvailable, Error) && bAvailable &&
				  Event.X == 12.5,
			  "external normalized input", Error);
		Check(!External->WaitEvent(10, Event, bAvailable, Error), "external host owns waiting");
		auto Retired = Platform->CreateWindow(Options, Error);
		std::thread Destroyer(
			[Retired = std::move(Retired)]() mutable
			{
				Retired.reset();
			});
		Destroyer.join();
		Check(Platform->CollectRetired(Error), "owner-thread deferred destruction", Error);
		Check(External->SubmitEvent(Injected, Error), "queue input before close", Error);
		Check(Window->Close(Error) && Window->Close(Error) && !Window->IsOpen(), "idempotent close", Error);
		Check(External->PollEvent(Event, bAvailable, Error) && !bAvailable, "discard queued input for closed window",
			  Error);
		Check(!External->SubmitEvent(Injected, Error), "closed window input rejected");
		Check(!Window->GetMetrics(Metrics, Error), "closed handle rejected");
		External.reset();
		Window.reset();
		std::thread LastService(
			[Platform = std::move(Platform)]() mutable
			{
				Platform.reset();
			});
		LastService.join();
		Check(Sdl3::CollectRetired(Error), "final retirement drain", Error);
		Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0, "owned subsystem references released");
		return Failures ? 1 : 0;
	}

	#if VS_TEST_GPU
	int GpuTests()
	{
		std::string Error;
		auto Service = Sdl3::CreateGraphics({}, Error);
		if (!Service)
		{
			std::cerr << Error << '\n';
			return 1;
		}
		auto Device = Service->CreateDevice({}, Error);
		if (!Device)
		{
			std::cout << "GPU unavailable: " << Error << '\n';
			return 77;
		}
		std::cout << "GPU driver: " << Device->GetDriver() << '\n';
		Check(Device->GetCapabilities().bGuiBasic && Device->GetCapabilities().bGuiTransform &&
				  Device->GetCapabilities().bClipMasks && Device->GetCapabilities().bFilters,
			  "honest capabilities");
		Graphics::FTextureOptions Options;
		Options.Width = 17;
		Options.Height = 9;
		auto Texture = Device->CreateTexture(Options, Error);
		if (!Texture)
		{
			std::cerr << Error << '\n';
			return 1;
		}
		Check(Device->ClearTexture(Texture, {1, 0.25f, 0, 1}, Error), "offscreen clear", Error);
		std::vector<std::uint8_t> Pixels;
		Check(Device->ReadTexture(Texture, Pixels, Error) && Pixels.size() == 17 * 9 * 4, "odd-width GPU readback",
			  Error);
		for (std::size_t Index = 0; Index + 3 < Pixels.size(); Index += 4)
		{
			if (Pixels[Index] != 255 || std::abs(static_cast<int>(Pixels[Index + 1]) - 64) > 1 ||
				Pixels[Index + 2] != 0 || Pixels[Index + 3] != 255)
			{
				Check(false, "actual GPU pixel values");
				break;
			}
		}
		Graphics::FTextureOptions SrgbOptions;
		SrgbOptions.Format = Graphics::ETextureFormat::Rgba8Srgb;
		auto Srgb = Device->CreateTexture(SrgbOptions, Error);
		std::vector<std::uint8_t> Encoded;
		Check(Srgb && Device->ClearTexture(Srgb, {0.25f, 0.5f, 0.75f, 0.5f}, Error) &&
				  Device->ReadTexture(Srgb, Encoded, Error) && Encoded.size() == 4 &&
				  std::abs(static_cast<int>(Encoded[0]) - 137) <= 1 &&
				  std::abs(static_cast<int>(Encoded[1]) - 188) <= 1 &&
				  std::abs(static_cast<int>(Encoded[2]) - 225) <= 1 &&
				  std::abs(static_cast<int>(Encoded[3]) - 128) <= 1,
			  "sRGB clear storage and unpremultiplied alpha", Error);
		if (Srgb)
		{
			Check(Srgb->Close(Error), "sRGB close", Error);
		}
		Check(!Device->Close(Error), "device protects live textures");
		auto Other = Service->CreateDevice({}, Error);
		Check(Other && !Other->ClearTexture(Texture, {}, Error), "cross-device resource rejected");
		if (Other)
		{
			Check(Other->Close(Error), "close secondary device", Error);
		}
		Sdl3::FPlatformOptions PlatformOptions;
		auto Platform = Sdl3::CreatePlatform(PlatformOptions, Error);
		Check(static_cast<bool>(Platform), "platform after offscreen GPU initialization", Error);
		if (Platform)
		{
			Platform::FWindowOptions WindowOptions;
			WindowOptions.Width = 96;
			WindowOptions.Height = 64;
			WindowOptions.GraphicsApi = Device->GetDriver();
			auto Window = Platform->CreateWindow(WindowOptions, Error);
			Check(static_cast<bool>(Window), "present window", Error);
			if (Window)
			{
				auto Surface = Device->CreateSurface(Window, Error);
				Check(static_cast<bool>(Surface), "compatible surface", Error);
				if (Surface)
				{
					Check(!Window->Close(Error), "surface retains window claim");
					bool bPresented = false, bSawFrame = false;
					for (int Frame = 0; Frame < 10; ++Frame)
					{
						Check(Device->Present(Surface, {0.1f, 0.2f, 0.4f, 1}, bPresented, Error),
							  "window GPU submission", Error);
						bSawFrame = bSawFrame || bPresented;
						SDL_Delay(2);
					}
					Check(bSawFrame, "at least one swapchain frame submitted");
					Check(Surface->Close(Error), "release swapchain", Error);
				}
				Check(Window->Close(Error), "close unclaimed window", Error);
			}
			for (const bool bForeignRetirement : {false, true})
			{
				const auto Driver = Device->GetDriver();
				const SDL_WindowFlags Flags = Driver == "opengl" || Driver == "opengles" ? SDL_WINDOW_OPENGL : 0;
				auto* Native = SDL_CreateWindow("host GPU lifetime", 96, 64, Flags);
				Check(Native != nullptr, "create borrowed GPU window", SDL_GetError());
				if (!Native)
				{
					continue;
				}
				Platform::FBorrowedWindowOptions Borrowed{"sdl3.window", reinterpret_cast<std::uintptr_t>(Native),
														  std::make_shared<int>(1)};
				auto Hosted = Platform->AttachWindow(Borrowed, Error);
				auto Surface = Hosted ? Device->CreateSurface(Hosted, Error) : nullptr;
				Check(static_cast<bool>(Surface), "claim borrowed GPU window", Error);
				if (Surface)
				{
					bool bPresented = false;
					Check(Device->Present(Surface, {}, bPresented, Error), "present borrowed GPU window", Error);
					if (bForeignRetirement)
					{
						std::thread Worker(
							[Surface = std::move(Surface)]() mutable
							{
								Surface.reset();
							});
						Worker.join();
					}
				}
				SDL_DestroyWindow(Native);
				Check(Hosted && !Hosted->IsOpen(), "native destruction invalidates borrowed GPU window");
				if (Surface)
				{
					bool bPresented = true;
					Check(!Surface->IsOpen() && !Device->Present(Surface, {}, bPresented, Error) && !bPresented &&
							  Surface->Close(Error) && Surface->Close(Error),
						  "invalidated surface cleanup", Error);
				}
				Check(Hosted && Hosted->Close(Error) && Platform->CollectRetired(Error),
					  "destroyed borrowed window claim retirement", Error);
			}
		}
		Check(Texture->Close(Error) && Texture->Close(Error), "texture close", Error);
		const auto Saved = Pixels;
		Check(!Device->ReadTexture(Texture, Pixels, Error) && Pixels == Saved, "readback failure preserves output");
		Check(Device->WaitIdle(Error) && Device->Close(Error), "GPU completion and close", Error);
		return Failures ? 1 : 0;
	}
	#endif
}

int main(int Count, char** Args)
{
	#if VS_TEST_GPU
	if (Count > 1 && std::string(Args[1]) == "gpu")
	{
		return GpuTests();
	}
	#endif
	return PlatformTests(Count > 1 && std::string(Args[1]) == "hidpi");
}
