// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <thread>

int main(int Count, char**)
{
	using namespace VeroScript;
	using namespace Platform;
	std::string Error;
	unsigned Failures = 0;
	const auto Check = [&](bool Value, const char* Message)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << Message << ": " << Error << '\n';
		}
	};
	Sdl3::FPlatformOptions Options;
	Options.PumpMode = EEventPumpMode::External;
	auto Service = Sdl3::CreatePlatform(Options, Error);
	if (!Service)
	{
		std::cerr << Error;
		return 1;
	}
	FWindowOptions Creation;
	Creation.bVisible = false;
	Creation.Width = 0;
	Creation.Height = 60;
	Error = "stale window error";
	Check(!Service->CreateWindow(Creation, Error) && Error.find("platform.invalid_size:") == 0,
		  "non-GL window validation replaces a stale diagnostic");
	Creation.Width = 80;
	Error = "stale window error";
	auto Created = Service->CreateWindow(Creation, Error);
	Check(Created && Error.empty(), "successful non-GL window creation clears stale diagnostics");
	if (Created)
	{
		Check(Created->Close(Error), "close non-GL diagnostic test window");
		Created.reset();
	}
	Creation.GraphicsApi = "unsupported-test-api";
	SDL_SetError("stale SDL diagnostic");
	Check(!Service->CreateWindow(Creation, Error) && Error.find("platform.unsupported_graphics_api:") == 0,
		  "unsupported graphics hint preserves its specific diagnostic");
	SDL_ClearError();
	auto* Native = SDL_CreateWindow("borrowed host window", 80, 60, SDL_WINDOW_HIDDEN);
	if (!Native)
	{
		return 1;
	}
	const auto NativeId = SDL_GetWindowID(Native);
	auto Owner = std::make_shared<int>(42);
	FBorrowedWindowOptions Borrowed{"sdl3.window", reinterpret_cast<std::uintptr_t>(Native), Owner};
	auto Window = Service->AttachWindow(Borrowed, Error);
	if (!Window)
	{
		std::cerr << Error;
		SDL_DestroyWindow(Native);
		return 1;
	}
	FWindowMetrics Metrics;
	Check(Window->IsBorrowed() && Window->GetMetrics(Metrics, Error) && Metrics.Width == 80,
		  "borrowed identity and metrics");
	Check(!Service->AttachWindow(Borrowed, Error), "duplicate registration rejected");
	Check(!Window->SetCursor(static_cast<ECursor>(255), true, Error), "invalid cursor rejected");
	Check(!Window->HasMouseCapture() && !Window->SetMouseCapture(true, Error), "capture needs mouse focus");
	Check(Window->SetMouseCapture(false, Error), "idempotent capture release");
	if (Count > 1)
	{
		for (unsigned Shape = 0; Shape <= static_cast<unsigned>(ECursor::Pointer); ++Shape)
		{
			Check(Window->SetCursor(static_cast<ECursor>(Shape), true, Error), "native cursor creation");
		}
		Check(Window->SetCursor(ECursor::Text, false, Error), "cursor visibility configuration");
	}
	if (Count > 2)
	{
		const auto Focus = [](SDL_Window* Target)
		{
			if (!SDL_ShowWindow(Target) || !SDL_RaiseWindow(Target))
			{
				return false;
			}
			SDL_WarpMouseInWindow(Target, 20, 20);
			const auto Deadline = SDL_GetTicks() + 2000;
			while ((SDL_GetMouseFocus() != Target || SDL_GetKeyboardFocus() != Target) && SDL_GetTicks() < Deadline)
			{
				SDL_PumpEvents();
				SDL_Delay(10);
			}
			return SDL_GetMouseFocus() == Target && SDL_GetKeyboardFocus() == Target;
		};
		Check(Focus(Native), "show host window for capture");
		Check(Window->SetMouseCapture(true, Error) && Window->HasMouseCapture() &&
				  (SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_CAPTURE),
			  "native mouse capture");
		Check(Window->SetCursor(ECursor::ResizeEW, true, Error), "dragging cursor configuration");
		auto *DragCursor = SDL_GetCursor();
		SDL_Event Leave{};
		Leave.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
		Leave.window.windowID = NativeId;
		Check(SDL_PushEvent(&Leave) && Window->HasMouseCapture() && SDL_GetCursor() == DragCursor,
			  "leaving a captured window preserves its dragging cursor");
		Check(Window->SetMouseCapture(false, Error) && !Window->HasMouseCapture() &&
				  !(SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_CAPTURE),
			  "native capture release");
		Check(Focus(Native) && Window->SetMouseCapture(true, Error) &&
				  Service->ClearInputState(Window->GetId(), Error) && !Window->HasMouseCapture() &&
				  !(SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_CAPTURE),
			  "explicit input reset releases native capture immediately");
		bool bQueued = true;
		for (unsigned I = 0; I < 65537 && bQueued; ++I)
		{
			bQueued = Service->ClearInputState(0, Error);
		}
		FEvent Reset;
		bool bResetAvailable = false;
		Check(bQueued && Focus(Native) && Window->SetMouseCapture(true, Error) &&
				  Service->PollEvent(Reset, bResetAvailable, Error) && bResetAvailable &&
				  Reset.Type == EEventType::InputStateReset && Reset.bSynthetic && !Window->HasMouseCapture() &&
				  !(SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_CAPTURE),
			  "notification overflow resynchronization releases native capture");
		// Consume the deliberately overflowed notification queue before observing new input below.
		while (Service->PollEvent(Reset, bResetAvailable, Error) && bResetAvailable)
		{
		}
		Check(Focus(Native) && Window->SetMouseCapture(true, Error) && SDL_HideWindow(Native) &&
				  !Window->HasMouseCapture() && !(SDL_GetWindowFlags(Native) & SDL_WINDOW_MOUSE_CAPTURE),
			  "hiding clears native capture");
		auto* SecondNative = SDL_CreateWindow("capture successor", 80, 60, SDL_WINDOW_HIDDEN);
		if (!SecondNative)
		{
			return 1;
		}
		FBorrowedWindowOptions SecondOptions{"sdl3.window", reinterpret_cast<std::uintptr_t>(SecondNative), Owner};
		auto Second = Service->AttachWindow(SecondOptions, Error);
		if (!Second)
		{
			SDL_DestroyWindow(SecondNative);
			return 1;
		}
		Check(Focus(Native) && Window->SetMouseCapture(true, Error), "capture before foreign retirement");
		std::thread RetireCaptured(
			[Window = std::move(Window)]() mutable
			{
				Window.reset();
			});
		RetireCaptured.join();
		Check(Focus(SecondNative) && Second->SetMouseCapture(true, Error) && Service->CollectRetired(Error) &&
				  Second->HasMouseCapture() && (SDL_GetWindowFlags(SecondNative) & SDL_WINDOW_MOUSE_CAPTURE),
			  "retiring an old captured window preserves its successor capture");
		Check(Second->SetMouseCapture(false, Error) && Second->Close(Error), "close capture successor");
		SDL_DestroyWindow(SecondNative);
		Window = Service->AttachWindow(Borrowed, Error);
		if (!Window)
		{
			SDL_DestroyWindow(Native);
			return 1;
		}
	}
	const auto OriginalId = Window->GetId();
	Check(Window->Close(Error) && !Window->IsOpen() && SDL_GetWindowFromID(NativeId) == Native,
		  "wrapper close preserves host window");
	Window = Service->AttachWindow(Borrowed, Error);
	Check(Window && Window->GetId() != OriginalId, "reattaching uses a fresh identity");
	if (!Window)
	{
		SDL_DestroyWindow(Native);
		return 1;
	}
	FEvent Press;
	Press.Type = EEventType::KeyDown;
	Press.WindowId = Window->GetId();
	Press.DeviceId = 91;
	Press.Key = EKey::A;
	Press.ScanCode = 4;
	Check(Service->SubmitEvent(Press, Error), "borrowed window input");
	FEvent Event;
	bool Available = false;
	for (unsigned I = 0; I < 8; ++I)
	{
		Check(Service->PollEvent(Event, Available, Error), "consume borrowed input");
		if (!Available)
		{
			break;
		}
	}
	SDL_DestroyWindow(Native);
	Check(!Window->IsOpen() && !Window->GetMetrics(Metrics, Error), "native destruction invalidates wrapper");
	FInputState State;
	Check(Service->GetInputState(EInputDeviceKind::Keyboard, 91, State, Error) && State.Keys.empty(),
		  "native destruction clears observed state");
	Check(Window->Close(Error), "invalidated wrapper closes safely");
	Native = SDL_CreateWindow("foreign retirement", 40, 40, SDL_WINDOW_HIDDEN);
	const auto RetiredId = SDL_GetWindowID(Native);
	Borrowed.Handle = reinterpret_cast<std::uintptr_t>(Native);
	Window = Service->AttachWindow(Borrowed, Error);
	Check(static_cast<bool>(Window), "borrowed retirement setup");
	std::thread Worker(
		[Window = std::move(Window)]() mutable
		{
			Window.reset();
		});
	Worker.join();
	Check(Service->CollectRetired(Error) && SDL_GetWindowFromID(RetiredId) == Native,
		  "foreign retirement preserves host ownership");
	SDL_DestroyWindow(Native);
	std::cout << "Window hosting failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
