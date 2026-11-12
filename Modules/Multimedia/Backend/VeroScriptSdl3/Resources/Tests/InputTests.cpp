// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <iostream>
#include <thread>

namespace
{
	using namespace VeroScript;
	using namespace Platform;
	int Failures = 0;
	std::string Error;
	void Check(bool Passed, const char* Message)
	{
		if (!Passed)
		{
			++Failures;
			std::cerr << Message << ": " << Error << '\n';
		}
	}
	std::vector<FEvent> Drain(IService& Service)
	{
		std::vector<FEvent> Events;
		for (unsigned I = 0; I < 10000; ++I)
		{
			FEvent Event;
			bool Available = false;
			if (!Service.PollEvent(Event, Available, Error))
			{
				Check(false, "poll input");
				break;
			}
			if (!Available)
			{
				return Events;
			}
			Check(ValidateEvent(Event, Error), "normalized event contract");
			Events.push_back(std::move(Event));
		}
		Check(false, "bounded event drain");
		return Events;
	}
	FInputState State(IService& Service, EInputDeviceKind Kind, std::uint64_t Id)
	{
		FInputState Result;
		Check(Service.GetInputState(Kind, Id, Result, Error), "input snapshot");
		return Result;
	}
	std::vector<FEvent> Send(IService& Service, FEvent Event)
	{
		Check(Service.SubmitEvent(Event, Error), "submit normalized input");
		return Drain(Service);
	}
	FEvent Key(std::uint64_t Window, std::uint64_t Device, EKey Code = EKey::A, int Scan = 4)
	{
		FEvent Event;
		Event.Type = EEventType::KeyDown;
		Event.WindowId = Window;
		Event.DeviceId = Device;
		Event.Key = Code;
		Event.ScanCode = Scan;
		return Event;
	}
	FEvent Pointer(std::uint64_t Window, EInputDeviceKind Kind, std::uint64_t Device, std::uint64_t Contact = 0)
	{
		FEvent Event;
		Event.Type = EEventType::PointerDown;
		Event.WindowId = Window;
		Event.DeviceKind = Kind;
		Event.DeviceId = Device;
		Event.PointerKind = Kind == EInputDeviceKind::Touch ? EPointerKind::Touch : EPointerKind::Mouse;
		Event.PointerId = Contact;
		Event.Button = 1;
		Event.X = 12;
		Event.Y = 24;
		Event.Pressure = 0.75;
		return Event;
	}
}

int main()
{
	auto Native = Sdl3::CreatePlatform({}, Error);
	Sdl3::FPlatformOptions Options;
	Options.PumpMode = EEventPumpMode::External;
	auto Host = Sdl3::CreatePlatform(Options, Error);
	auto Other = Sdl3::CreatePlatform(Options, Error);
	if (!Native || !Host || !Other)
	{
		std::cerr << Error << '\n';
		return 1;
	}
	const auto Caps = Native->GetCapabilities();
	Check(Caps.bInputDevices && Caps.bInputState && Caps.bDeviceEvents, "input capabilities");
	FWindowOptions WindowOptions;
	WindowOptions.Title = "multi-input-primary";
	WindowOptions.bVisible = false;
	auto Window = Host->CreateWindow(WindowOptions, Error);
	WindowOptions.Title = "multi-input-secondary";
	auto Second = Host->CreateWindow(WindowOptions, Error);
	if (!Window || !Second)
	{
		return 1;
	}
	const auto W = Window->GetId(), W2 = Second->GetId();
	Drain(*Native);
	std::vector<FInputDevice> Devices;
	Check(Host->GetInputDevices(Devices, Error) && Devices.size() == 2 && Devices[0].bAggregate &&
			  Devices[1].bAggregate && Devices[0].DeviceId == 0 && Devices[1].DeviceId == 0,
		  "explicit aggregate devices");
	Check(Native->GetInputDevices(Devices, Error), "native SDL enumeration");

	// Identical logical keys on separate keyboards never release each other.
	auto A = Key(W, 7);
	auto Events = Send(*Host, A);
	Check(Events.size() == 2 && Events[0].Type == EEventType::InputDeviceAdded &&
			  Events[1].DeviceKind == EInputDeviceKind::Keyboard,
		  "lazy discovery before first input");
	A.bRepeat = true;
	Send(*Host, A);
	Send(*Host, Key(W2, 8));
	Send(*Host, Key(W, 7, EKey::LeftShift, 225));
	Check(State(*Host, EInputDeviceKind::Keyboard, 7).Keys.size() == 2 &&
			  State(*Host, EInputDeviceKind::Keyboard, 7).Modifiers == static_cast<unsigned>(EModifier::Shift) &&
			  State(*Host, EInputDeviceKind::Keyboard, 8).Modifiers == 0,
		  "repeat deduplication and device-local modifiers");
	auto Saved = State(*Host, EInputDeviceKind::Keyboard, 7);
	A.Type = EEventType::KeyUp;
	Send(*Host, A);
	Check(State(*Host, EInputDeviceKind::Keyboard, 7).Keys.size() == 1 &&
			  State(*Host, EInputDeviceKind::Keyboard, 8).Keys.size() == 1 && Saved.Keys.size() == 2,
		  "independent keys and immutable snapshots");
	FInputState Unknown;
	Unknown.Device.Name = "unchanged";
	Check(!Other->GetInputState(EInputDeviceKind::Keyboard, 7, Unknown, Error) && Unknown.Device.Name == "unchanged",
		  "independent service streams and transactional errors");

	// The same numeric identity can occur in all device categories and contact spaces.
	auto Mouse = Pointer(W, EInputDeviceKind::Mouse, 7);
	Send(*Host, Mouse);
	Send(*Host, Pointer(W2, EInputDeviceKind::Mouse, 8));
	auto Touch = Pointer(W, EInputDeviceKind::Touch, 7, 100);
	Send(*Host, Touch);
	Send(*Host, Pointer(W2, EInputDeviceKind::Touch, 8, 100));
	Send(*Host, Pointer(W, EInputDeviceKind::Touch, 7, 101));
	Mouse.Type = EEventType::PointerMove;
	Mouse.X = 50;
	Mouse.Buttons = 0; // A host/global mouse mask must not clear this device's held button.
	Send(*Host, Mouse);
	Check(State(*Host, EInputDeviceKind::Mouse, 7).Pointers[0].Buttons == 1 &&
			  State(*Host, EInputDeviceKind::Mouse, 7).Pointers[0].X == 50 &&
			  State(*Host, EInputDeviceKind::Mouse, 8).Pointers[0].X == 12 &&
			  State(*Host, EInputDeviceKind::Touch, 7).Pointers.size() == 2 &&
			  State(*Host, EInputDeviceKind::Touch, 8).Pointers.size() == 1,
		  "typed identities and isolated pointer state");
	Touch.Type = EEventType::PointerUp;
	Send(*Host, Touch);
	Check(State(*Host, EInputDeviceKind::Touch, 7).Pointers.size() == 1 &&
			  State(*Host, EInputDeviceKind::Touch, 8).Pointers.size() == 1,
		  "independent touch completion");

	// Late releases/motion must not leak a host's aggregate button mask into this device.
	auto Late = Pointer(W, EInputDeviceKind::Mouse, 30);
	Late.Type = EEventType::PointerUp;
	Late.Buttons = 0xffffffffu;
	Events = Send(*Host, Late);
	Check(!Events.empty() && Events.back().Buttons == 0 && State(*Host, EInputDeviceKind::Mouse, 30).Pointers.empty(),
		  "unknown mouse release has no observed buttons");
	Late = Pointer(W, EInputDeviceKind::Touch, 30, 42);
	Late.Type = EEventType::PointerMove;
	Late.Buttons = 1;
	Events = Send(*Host, Late);
	Check(!Events.empty() && Events.back().Buttons == 0 && State(*Host, EInputDeviceKind::Touch, 30).Pointers.empty(),
		  "stale touch motion has no observed buttons");
	FEvent Lost;
	Lost.Type = EEventType::FocusLost;
	Lost.WindowId = W;
	Events = Send(*Host, Lost);
	Check(Events.size() == 4 && Events[0].Type == EEventType::KeyUp && Events[0].bSynthetic &&
			  Events[1].Type == EEventType::PointerCancelled && Events[2].Type == EEventType::PointerCancelled &&
			  Events.back().Type == EEventType::FocusLost,
		  "synthetic releases precede focus loss");
	Check(State(*Host, EInputDeviceKind::Keyboard, 7).Keys.empty() &&
			  State(*Host, EInputDeviceKind::Mouse, 7).Pointers.empty() &&
			  State(*Host, EInputDeviceKind::Touch, 7).Pointers.empty() &&
			  State(*Host, EInputDeviceKind::Keyboard, 8).Keys.size() == 1 &&
			  State(*Host, EInputDeviceKind::Touch, 8).Pointers.size() == 1,
		  "window-local focus cleanup");
	Lost.Type = EEventType::Background;
	Lost.WindowId = 0;
	Events = Send(*Host, Lost);
	Check(Events.back().Type == EEventType::Background && State(*Host, EInputDeviceKind::Keyboard, 8).Keys.empty() &&
			  State(*Host, EInputDeviceKind::Touch, 8).Pointers.empty(),
		  "background clears all windows");

	FEvent Connection;
	Connection.Type = EEventType::InputDeviceRemoved;
	Connection.DeviceKind = EInputDeviceKind::Touch;
	Connection.DeviceId = 7;
	Send(*Host, Pointer(W, EInputDeviceKind::Touch, 7, 100));
	Events = Send(*Host, Connection);
	Check(Events.size() == 2 && Events[0].Type == EEventType::PointerCancelled &&
			  Events[1].Type == EEventType::InputDeviceRemoved &&
			  !State(*Host, EInputDeviceKind::Touch, 7).Device.bConnected,
		  "disconnect clears contacts before removal");
	Check(Send(*Host, Pointer(W, EInputDeviceKind::Touch, 7, 100)).empty(),
		  "stale input cannot reconnect removed device");
	Connection.Type = EEventType::InputDeviceAdded;
	Connection.DeviceName = "Reconnected touchscreen";
	Send(*Host, Connection);
	Send(*Host, Pointer(W, EInputDeviceKind::Touch, 7, 100));
	Check(State(*Host, EInputDeviceKind::Touch, 7).Pointers.size() == 1 &&
			  State(*Host, EInputDeviceKind::Touch, 7).Device.Name == Connection.DeviceName,
		  "explicit reconnection");

	Send(*Host, Key(W, 7));
	Check(Host->SubmitEvent(Key(W, 7, EKey::B, 5), Error) && Host->ClearInputState(W, Error), "explicit reset");
	Events = Drain(*Host);
	Check(Events.back().Type == EEventType::InputStateReset &&
			  State(*Host, EInputDeviceKind::Keyboard, 7).Keys.empty() &&
			  State(*Host, EInputDeviceKind::Touch, 7).Pointers.empty(),
		  "reset discards queued input and clears state");
	// Host-submitted reset is ordered; a later key-down still belongs to the new state.
	FEvent Reset;
	Reset.Type = EEventType::InputStateReset;
	Reset.WindowId = W;
	Check(Host->SubmitEvent(Key(W, 7), Error) && Host->SubmitEvent(Reset, Error) &&
			  Host->SubmitEvent(Key(W, 7, EKey::B, 5), Error),
		  "ordered host reset submission");
	Events = Drain(*Host);
	Check(std::count_if(Events.begin(), Events.end(),
						[](const auto& E)
						{
							return E.Type == EEventType::InputStateReset;
						}) == 1 &&
			  State(*Host, EInputDeviceKind::Keyboard, 7).Keys[0].Key == EKey::B,
		  "one reset preserves subsequent input");

	// Physical identity remains stable across a logical layout change and window transfer.
	Send(*Host, Key(W, 20));
	Events = Send(*Host, Key(W2, 20, EKey::B, 4));
	Check(Events.size() == 2 && Events[0].bSynthetic && Events[0].WindowId == W &&
			  State(*Host, EInputDeviceKind::Keyboard, 20).Keys[0].WindowId == W2,
		  "key transfer releases old window");
	auto LayoutUp = Key(W2, 20, EKey::C, 4);
	LayoutUp.Type = EEventType::KeyUp;
	Send(*Host, LayoutUp);
	Check(State(*Host, EInputDeviceKind::Keyboard, 20).Keys.empty(), "scan identity survives logical layout change");
	Send(*Host, Key(W, 20));
	LayoutUp.WindowId = W2;
	Events = Send(*Host, LayoutUp);
	Check(Events.size() == 2 && Events[0].bSynthetic && Events[0].WindowId == W && Events[0].Key == EKey::A &&
			  State(*Host, EInputDeviceKind::Keyboard, 20).Keys.empty(),
		  "key-up releases original window after routing change");
	Send(*Host, Pointer(W, EInputDeviceKind::Touch, 20, 77));
	auto RoutedUp = Pointer(W2, EInputDeviceKind::Touch, 20, 77);
	RoutedUp.Type = EEventType::PointerUp;
	Events = Send(*Host, RoutedUp);
	Check(Events.size() == 2 && Events[0].bSynthetic && Events[0].WindowId == W &&
			  Events[0].Type == EEventType::PointerCancelled &&
			  State(*Host, EInputDeviceKind::Touch, 20).Pointers.empty(),
		  "touch end cancels original owner after routing change");
	auto EdgeMouse = Pointer(W, EInputDeviceKind::Mouse, 20);
	EdgeMouse.Button = 32;
	Send(*Host, EdgeMouse);
	Check(State(*Host, EInputDeviceKind::Mouse, 20).Pointers[0].Buttons == 0x80000000u, "highest representable button");
	EdgeMouse.Button = 0;
	Check(!Host->SubmitEvent(EdgeMouse, Error), "invalid mouse button rejected before shifting");
	EdgeMouse.Button = 1;
	EdgeMouse.WindowId = W2;
	Events = Send(*Host, EdgeMouse);
	Check(Events.size() == 2 && Events[0].Type == EEventType::PointerCancelled && Events[0].WindowId == W &&
			  State(*Host, EInputDeviceKind::Mouse, 20).Pointers[0].Buttons == 1,
		  "pointer transfer cancels previous ownership");
	EdgeMouse.Type = EEventType::PointerUp;
	EdgeMouse.WindowId = W;
	Events = Send(*Host, EdgeMouse);
	Check(Events.size() == 2 && Events[0].bSynthetic && Events[0].WindowId == W2 && Events[0].Buttons == 0,
		  "mouse up releases original owner after routing change");
	for (auto Type : {EEventType::Hidden, EEventType::Minimized})
	{
		Send(*Host, Key(W2, 20));
		FEvent Lifecycle;
		Lifecycle.Type = Type;
		Lifecycle.WindowId = W2;
		Events = Send(*Host, Lifecycle);
		Check(Events.back().Type == Type && State(*Host, EInputDeviceKind::Keyboard, 20).Keys.empty(),
			  "visibility lifecycle clears held input");
	}
	auto LargeContact = Pointer(W2, EInputDeviceKind::Touch, std::uint64_t{1} << 40, std::uint64_t{1} << 48);
	Send(*Host, LargeContact);
	LargeContact.Type = EEventType::PointerCancelled;
	Send(*Host, LargeContact);
	LargeContact.Type = EEventType::PointerMove;
	Send(*Host, LargeContact);
	Check(State(*Host, EInputDeviceKind::Touch, LargeContact.DeviceId).Pointers.empty(),
		  "64-bit identities and stale touch motion");
	bool WrongThreadAccepted = true;
	std::thread Worker(
		[&]
		{
			std::string Local;
			std::vector<FInputDevice> Out;
			FInputState Snapshot;
			WrongThreadAccepted = Host->GetInputDevices(Out, Local) ||
								  Host->GetInputState(EInputDeviceKind::Keyboard, 7, Snapshot, Local) ||
								  Host->ClearInputState(0, Local);
		});
	Worker.join();
	Check(!WrongThreadAccepted && State(*Host, EInputDeviceKind::Keyboard, 7).Keys.size() == 1,
		  "owner-thread checks preserve state");
	Check(!Host->ClearInputState(UINT64_MAX, Error), "unknown window reset rejected");
	Send(*Other, Key(W, 70));
	Send(*Other, Key(W2, 71));
	Check(Other->ClearInputState(0, Error) && Other->ClearInputState(W, Error) && Other->ClearInputState(W2, Error),
		  "successive scoped resets after global cleanup");
	Events = Drain(*Other);
	Check(Events.size() == 3 &&
			  std::all_of(Events.begin(), Events.end(),
						  [](const auto& Event)
						  {
							  return Event.Type == EEventType::InputStateReset;
						  }) &&
			  State(*Other, EInputDeviceKind::Keyboard, 70).Keys.empty() &&
			  State(*Other, EInputDeviceKind::Keyboard, 71).Keys.empty(),
		  "scoped cleanup retains and then discards the other window's pending releases");
	Send(*Other, Key(W, 44));
	Send(*Other, Key(W, 44, EKey::B, 5));
	FEvent PendingFocusLoss;
	PendingFocusLoss.Type = EEventType::FocusLost;
	PendingFocusLoss.WindowId = W;
	Check(Other->SubmitEvent(PendingFocusLoss, Error), "submit cleanup before closing");
	FEvent FirstRelease;
	bool Available = false;
	Check(Other->PollEvent(FirstRelease, Available, Error) && Available && FirstRelease.Type == EEventType::KeyUp,
		  "partially consume cleanup notifications");
	Check(Window->Close(Error), "close input owner window");
	Check(State(*Host, EInputDeviceKind::Keyboard, 7).Keys.empty(), "close clears state synchronously");
	Events = Drain(*Host);
	Check(!Events.empty() && Events[0].Type == EEventType::KeyUp && Events.back().Type == EEventType::InputStateReset,
		  "close cleanup notifications survive closed-window filtering");
	Events = Drain(*Other);
	Check(std::any_of(Events.begin(), Events.end(),
					  [](const auto& E)
					  {
						  return E.Type == EEventType::InputStateReset;
					  }),
		  "closing preserves reset after discarding pending releases");
	Send(*Host, Key(W2, 8));
	std::thread Destroyer(
		[Second = std::move(Second)]() mutable
		{
			Second.reset();
		});
	Destroyer.join();
	Check(Host->CollectRetired(Error) && State(*Host, EInputDeviceKind::Keyboard, 8).Keys.empty(),
		  "deferred window retirement clears input");
	Drain(*Host);

	// Native SDL hotplug follows ordered SDL events, independently of the external stream.
	WindowOptions.Title = "native-input";
	auto NativeWindow = Native->CreateWindow(WindowOptions, Error);
	if (!NativeWindow)
	{
		return 1;
	}
	int Count = 0;
	SDL_Window** Windows = SDL_GetWindows(&Count);
	SDL_WindowID Id = 0;
	for (int I = 0; I < Count; ++I)
	{
		if (std::string(SDL_GetWindowTitle(Windows[I])) == "native-input")
		{
			Id = SDL_GetWindowID(Windows[I]);
		}
	}
	SDL_free(Windows);
	Drain(*Native);
	SDL_Event E{};
	E.type = SDL_EVENT_KEYBOARD_ADDED;
	E.kdevice.which = 40001;
	Check(SDL_PushEvent(&E), "native keyboard added");
	E = {};
	E.type = SDL_EVENT_KEY_DOWN;
	E.key.windowID = Id;
	E.key.which = 40001;
	E.key.key = SDLK_A;
	E.key.scancode = SDL_SCANCODE_A;
	Check(SDL_PushEvent(&E), "native device-specific key");
	Events = Drain(*Native);
	Check(Events.size() == 2 && Events[0].Type == EEventType::InputDeviceAdded &&
			  State(*Native, EInputDeviceKind::Keyboard, 40001).Keys.size() == 1,
		  "native discovery and state");
	E = {};
	Send(*Native, Key(NativeWindow->GetId(), 0));
	E.type = SDL_EVENT_KEYBOARD_REMOVED;
	E.kdevice.which = 40001;
	Check(SDL_PushEvent(&E), "native keyboard removed");
	Events = Drain(*Native);
	Check(Events.size() == 3 && Events[0].Type == EEventType::KeyUp && Events[0].bSynthetic &&
			  Events[1].Type == EEventType::KeyUp && Events[1].bAggregateDevice &&
			  Events[2].Type == EEventType::InputDeviceRemoved &&
			  State(*Native, EInputDeviceKind::Keyboard, 0).Keys.empty() &&
			  !State(*Native, EInputDeviceKind::Keyboard, 40001).Device.bConnected,
		  "native hotplug cleanup");
	E = {};
	E.type = SDL_EVENT_MOUSE_ADDED;
	E.mdevice.which = 40002;
	Check(SDL_PushEvent(&E), "native mouse added");
	E = {};
	E.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	E.button.windowID = Id;
	E.button.which = 40002;
	E.button.button = 1;
	Check(SDL_PushEvent(&E), "native mouse press");
	Drain(*Native);
	E = {};
	Send(*Native, Pointer(NativeWindow->GetId(), EInputDeviceKind::Mouse, 0));
	E.type = SDL_EVENT_MOUSE_REMOVED;
	E.mdevice.which = 40002;
	Check(SDL_PushEvent(&E), "native mouse removed");
	Events = Drain(*Native);
	Check(Events.size() == 3 && Events[0].Type == EEventType::PointerCancelled &&
			  Events[1].Type == EEventType::PointerCancelled && Events[1].bAggregateDevice &&
			  Events[2].Type == EEventType::InputDeviceRemoved &&
			  State(*Native, EInputDeviceKind::Mouse, 0).Pointers.empty() &&
			  State(*Native, EInputDeviceKind::Mouse, 40002).Pointers.empty(),
		  "native mouse cleanup");
	Check(Native->GetInputDevices(Devices, Error) && std::none_of(Devices.begin(), Devices.end(),
																  [](const auto& D)
																  {
																	  return D.DeviceId == 40001 || D.DeviceId == 40002;
																  }),
		  "enumeration excludes disconnected devices");
	// Pen-generated mouse input can arrive without a preceding SDL mouse-added event.
	E = {};
	E.type = SDL_EVENT_MOUSE_MOTION;
	E.motion.windowID = Id;
	E.motion.which = SDL_PEN_MOUSEID;
	E.motion.x = 30;
	E.motion.y = 40;
	Check(SDL_PushEvent(&E), "unannounced pen fallback motion");
	Events = Drain(*Native);
	const auto Pen = State(*Native, EInputDeviceKind::Mouse, SDL_PEN_MOUSEID);
	Check(Events.size() == 2 && Events[0].Type == EEventType::InputDeviceAdded && Events[0].bAggregateDevice &&
			  Events[1].bAggregateDevice && Pen.Device.bAggregate && Pen.Pointers.size() == 1 &&
			  Pen.Pointers[0].X == 30,
		  "lazy pen fallback remains an aggregate mouse source");
	E = {};
	E.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	E.button.windowID = Id;
	E.button.which = SDL_PEN_MOUSEID;
	E.button.button = 1;
	Check(SDL_PushEvent(&E), "pen fallback press");
	Events = Drain(*Native);
	Check(Events.size() == 1 && Events[0].bAggregateDevice && Events[0].Buttons == 1, "pen fallback button identity");
	E = {};
	E.type = SDL_EVENT_MOUSE_WHEEL;
	E.wheel.windowID = Id;
	E.wheel.which = SDL_PEN_MOUSEID;
	E.wheel.y = 1;
	Check(SDL_PushEvent(&E), "pen fallback wheel");
	Events = Drain(*Native);
	Check(Events.size() == 1 && Events[0].bAggregateDevice && Events[0].Buttons == 1 &&
			  State(*Native, EInputDeviceKind::Mouse, SDL_PEN_MOUSEID).Device.bAggregate,
		  "pen fallback wheel preserves observed buttons");
	Check(Native->ClearInputState(NativeWindow->GetId(), Error), "clear pen fallback state");
	Drain(*Native);
	for (const auto Type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_WHEEL})
	{
		E = {};
		E.type = Type;
		if (Type == SDL_EVENT_MOUSE_MOTION)
		{
			E.motion.windowID = Id;
			E.motion.which = SDL_TOUCH_MOUSEID;
		}
		else if (Type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		{
			E.button.windowID = Id;
			E.button.which = SDL_TOUCH_MOUSEID;
			E.button.button = 1;
		}
		else
		{
			E.wheel.windowID = Id;
			E.wheel.which = SDL_TOUCH_MOUSEID;
		}
		Check(SDL_PushEvent(&E), "touch-emulated mouse input");
	}
	Check(Drain(*Native).empty() && !Native->GetInputState(EInputDeviceKind::Mouse, SDL_TOUCH_MOUSEID, Unknown, Error),
		  "touch emulation neither duplicates events nor creates a mouse device");
	// Queue saturation is explicit; resetting discards pending input without reapplying it.
	for (unsigned I = 0; I < 65536; ++I)
	{
		if (!Host->SubmitEvent(Key(NativeWindow->GetId(), 20), Error))
		{
			Check(false, "fill submitted input queue");
			break;
		}
	}
	Check(!Host->SubmitEvent(Key(NativeWindow->GetId(), 20), Error) && Error.find("platform.resource_limit") == 0,
		  "queue saturation reports an error");
	Check(Host->ClearInputState(0, Error), "clear saturated input queue");
	Events = Drain(*Host);
	Check(Events.size() == 1 && Events[0].Type == EEventType::InputStateReset && Events[0].bSynthetic &&
			  Events[0].WindowId == 0,
		  "global reset discards queued presses");
	// Notification overflow must reset consumers and recover without replaying queued presses.
	Send(*Host, Key(NativeWindow->GetId(), 20));
	for (unsigned I = 0; I <= 65536; ++I)
	{
		if (!Host->ClearInputState(NativeWindow->GetId(), Error))
		{
			Check(false, "fill cleanup notification queue");
			break;
		}
	}
	Check(Host->SubmitEvent(Key(NativeWindow->GetId(), 20, EKey::B, 5), Error),
		  "submit input before notification resynchronization");
	Events = Drain(*Host);
	Check(Events.size() == 1 && Events[0].Type == EEventType::InputStateReset && Events[0].bSynthetic &&
			  Events[0].WindowId == 0 && State(*Host, EInputDeviceKind::Keyboard, 20).Keys.empty(),
		  "notification overflow produces one global reset without stale input");
	Events = Send(*Host, Key(NativeWindow->GetId(), 20));
	Check(Events.size() == 1 && Events[0].Type == EEventType::KeyDown &&
			  State(*Host, EInputDeviceKind::Keyboard, 20).Keys.size() == 1,
		  "input resumes normally after notification overflow");
	Check(NativeWindow->Close(Error), "native window close");
	std::cout << "Multi-input tests: " << Failures << " failures\n";
	return Failures ? 1 : 0;
}
