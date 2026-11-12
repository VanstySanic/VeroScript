// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
	using namespace VeroScript;
	using namespace Platform;
	std::string Error;
	unsigned Failures = 0;
	void Check(bool Value, const char* Name)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << Name << ": " << Error << '\n';
		}
	}
	std::vector<FEvent> Drain(IService& Service)
	{
		std::vector<FEvent> Events;
		for (unsigned I = 0; I < 10000; ++I)
		{
			FEvent E;
			bool Available = false;
			if (!Service.PollEvent(E, Available, Error))
			{
				Check(false, "poll controllers");
				break;
			}
			if (!Available)
			{
				return Events;
			}
			Check(ValidateEvent(E, Error), "controller event contract");
			Events.push_back(std::move(E));
		}
		Check(false, "bounded controller drain");
		return Events;
	}
	FInputState State(IService& Service, EInputDeviceKind Kind, std::uint64_t Id)
	{
		FInputState S;
		Check(Service.GetInputState(Kind, Id, S, Error), "controller snapshot");
		return S;
	}
	bool Neutral(const FInputState& S)
	{
		return std::all_of(S.Axes.begin(), S.Axes.end(),
						   [](double V)
						   {
							   return V == 0;
						   }) &&
			   std::all_of(S.ControllerButtons.begin(), S.ControllerButtons.end(),
						   [](auto V)
						   {
							   return V == 0;
						   }) &&
			   std::all_of(S.Hats.begin(), S.Hats.end(),
						   [](auto V)
						   {
							   return V == 0;
						   });
	}
	#if VS_TEST_CONTROLLERS
	struct FFeedback
	{
		unsigned Calls = 0;
		Uint16 Low = 0, High = 0;
	};
	bool SDLCALL Feedback(void* Data, Uint16 Low, Uint16 High)
	{
		auto& F = *static_cast<FFeedback*>(Data);
		++F.Calls;
		F.Low = Low;
		F.High = High;
		return true;
	}
	SDL_JoystickID Attach(bool Gamepad, const char* Name, FFeedback* F = nullptr)
	{
		SDL_VirtualJoystickDesc D;
		SDL_INIT_INTERFACE(&D);
		D.type = static_cast<Uint16>(Gamepad ? SDL_JOYSTICK_TYPE_GAMEPAD : SDL_JOYSTICK_TYPE_FLIGHT_STICK);
		D.name = Name;
		D.naxes = Gamepad ? SDL_GAMEPAD_AXIS_COUNT : 2;
		D.nbuttons = Gamepad ? SDL_GAMEPAD_BUTTON_COUNT : 4;
		D.nhats = Gamepad ? 0 : 1;
		D.axis_mask = Gamepad ? 0x3f : 0;
		D.button_mask = Gamepad ? (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1 : 0;
		if (F)
		{
			D.userdata = F;
			D.Rumble = Feedback;
		}
		const auto Id = SDL_AttachVirtualJoystick(&D);
		Check(Id != 0, "attach SDL virtual controller");
		return Id;
	}
	#endif
}
int main()
{
	using namespace VeroScript;
	using namespace Platform;
	Sdl3::FPlatformOptions External;
	External.PumpMode = EEventPumpMode::External;
	auto Host = Sdl3::CreatePlatform(External, Error);
	if (!Host)
	{
		std::cerr << Error;
		return 1;
	}
	Check(Host->GetCapabilities().bControllers && !Host->GetCapabilities().bRumble,
		  "external normalized controls without native feedback");
	FEvent E;
	E.Type = EEventType::InputDeviceAdded;
	E.DeviceKind = EInputDeviceKind::Gamepad;
	E.DeviceId = std::uint64_t{1} << 40;
	E.AxisCount = 6;
	E.ButtonCount = 26;
	Check(Host->SubmitEvent(E, Error), "host gamepad metadata");
	Drain(*Host);
	E.Type = EEventType::ControllerAxis;
	E.Control = 0;
	E.Value = -1;
	Check(Host->SubmitEvent(E, Error), "host axis");
	E.Type = EEventType::ControllerButtonDown;
	E.Control = 0;
	E.Value = 0;
	Check(Host->SubmitEvent(E, Error), "host button");
	Drain(*Host);
	const auto Saved = State(*Host, EInputDeviceKind::Gamepad, E.DeviceId);
	Check(Saved.Axes.size() == 6 && Saved.Axes[0] == -1 && Saved.ControllerButtons[0],
		  "host state with 64-bit device identity");
	Check(!Host->Rumble(EInputDeviceKind::Gamepad, E.DeviceId, 0.5, 1, 100, Error),
		  "external feedback is explicitly unsupported");
	E.Type = EEventType::ControllerAxis;
	E.Control = 4;
	E.Value = -0.1;
	Check(!Host->SubmitEvent(E, Error), "negative mapped trigger rejected");
	E.Control = 0;
	E.Value = std::numeric_limits<double>::quiet_NaN();
	Check(!Host->SubmitEvent(E, Error), "nonfinite axis rejected");
	E.Value = 0;
	E.DeviceId = 0;
	Check(!Host->SubmitEvent(E, Error), "aggregate controller rejected");
	FWindowOptions WindowOptions;
	WindowOptions.bVisible = false;
	auto Window = Host->CreateWindow(WindowOptions, Error);
	if (!Window)
	{
		std::cerr << Error;
		return 1;
	}
	Check(Host->ClearInputState(Window->GetId(), Error), "window-scoped reset");
	Drain(*Host);
	Check(State(*Host, EInputDeviceKind::Gamepad, Saved.Device.DeviceId).ControllerButtons[0] != 0,
		  "explicit window reset preserves global controller state");
	Check(Host->ClearInputState(0, Error), "global controller reset");
	auto Events = Drain(*Host);
	Check(Neutral(State(*Host, EInputDeviceKind::Gamepad, Saved.Device.DeviceId)) && Saved.ControllerButtons[0] &&
			  Events.size() == 3 && Events.back().Type == EEventType::InputStateReset,
		  "synthetic neutral events and immutable snapshots");
	E = {};
	E.Type = EEventType::ControllerButtonDown;
	E.DeviceKind = EInputDeviceKind::Gamepad;
	E.DeviceId = Saved.Device.DeviceId;
	Check(Host->SubmitEvent(E, Error), "host press before disconnect");
	Drain(*Host);
	E.Type = EEventType::InputDeviceRemoved;
	Check(Host->SubmitEvent(E, Error), "host disconnect");
	Events = Drain(*Host);
	Check(Events.size() == 2 && Events[0].Type == EEventType::ControllerButtonUp && Events[0].bSynthetic &&
			  Events[1].Type == EEventType::InputDeviceRemoved,
		  "host disconnect synthesizes missing releases");
	E.Type = EEventType::ControllerButtonDown;
	Check(Host->SubmitEvent(E, Error) && Drain(*Host).empty(),
		  "stale controller input cannot reconnect a removed source");
	E.Type = EEventType::InputDeviceAdded;
	Check(Host->SubmitEvent(E, Error), "host reconnect");
	Drain(*Host);
	E.Type = EEventType::ControllerButtonDown;
	Check(Host->SubmitEvent(E, Error), "controller press before focus loss");
	Drain(*Host);
	FEvent Lost;
	Lost.Type = EEventType::FocusLost;
	Lost.WindowId = Window->GetId();
	Check(Host->SubmitEvent(Lost, Error), "managed window focus loss");
	Events = Drain(*Host);
	Check(Neutral(State(*Host, EInputDeviceKind::Gamepad, E.DeviceId)) && Events.size() == 2 &&
			  Events[0].Type == EEventType::ControllerButtonUp && Events[1].Type == EEventType::FocusLost,
		  "focus loss neutralizes global controllers before the reason event");
	Check(Window->Close(Error), "close host test window");
	Window.reset();
	#if VS_TEST_CONTROLLERS
	// This test owns its background-input policy; the library must never change process hints.
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	Check(SDL_InitSubSystem(SDL_INIT_GAMEPAD), "test controller subsystem");
	FFeedback FirstFeedback, SecondFeedback;
	const auto First = Attach(true, "Vero first gamepad", &FirstFeedback);
	auto Native = Sdl3::CreatePlatform({}, Error);
	if (!Native)
	{
		std::cerr << Error;
		return 1;
	}
	Check(Native->GetCapabilities().bControllers && Native->GetCapabilities().bRumble,
		  "native controller capabilities");
	std::vector<FInputDevice> Devices;
	Check(Native->GetInputDevices(Devices, Error) && std::any_of(Devices.begin(), Devices.end(),
																 [First](const auto& D)
																 {
																	 return D.DeviceId == First &&
																			D.Kind == EInputDeviceKind::Gamepad &&
																			D.AxisCount == 6 && D.bRumble;
																 }),
		  "enumerate preconnected mapped gamepad");
	Drain(*Native);
	const auto Second = Attach(true, "Vero second gamepad", &SecondFeedback);
	const auto Raw = Attach(false, "Vero raw joystick");
	Events = Drain(*Native);
	Check(std::count_if(Events.begin(), Events.end(),
						[Second, Raw](const auto& V)
						{
							return V.Type == EEventType::InputDeviceAdded &&
								   (V.DeviceId == Second || V.DeviceId == Raw);
						}) == 2,
		  "hotplug emits one stream per physical controller");
	auto* A = SDL_OpenJoystick(First);
	auto* B = SDL_OpenJoystick(Second);
	auto* J = SDL_OpenJoystick(Raw);
	if (!A || !B || !J)
	{
		return 1;
	}
	Check(SDL_SetJoystickVirtualAxis(A, 0, -32768) && SDL_SetJoystickVirtualButton(A, 0, true) &&
			  SDL_SetJoystickVirtualAxis(B, 0, 32767) && SDL_SetJoystickVirtualButton(B, 0, true) &&
			  SDL_SetJoystickVirtualAxis(J, 1, -16384) && SDL_SetJoystickVirtualButton(J, 2, true) &&
			  SDL_SetJoystickVirtualHat(J, 0, SDL_HAT_RIGHTUP),
		  "set virtual controls");
	Events = Drain(*Native);
	const auto Before = State(*Native, EInputDeviceKind::Gamepad, First);
	const auto Other = State(*Native, EInputDeviceKind::Gamepad, Second);
	const auto RawState = State(*Native, EInputDeviceKind::Joystick, Raw);
	Check(Before.Axes[0] == -1 && Other.Axes[0] == 1 && Before.ControllerButtons[0] && Other.ControllerButtons[0] &&
			  RawState.Axes[1] == -0.5 && RawState.ControllerButtons[2] && RawState.Hats[0] == SDL_HAT_RIGHTUP,
		  "independent standard and raw state, hats and signed endpoint normalization");
	Check(std::count_if(Events.begin(), Events.end(),
						[First](const auto& V)
						{
							return V.DeviceId == First && V.Type == EEventType::ControllerButtonDown;
						}) == 1,
		  "mapped joystick button is not duplicated");
	Check(Native->Rumble(EInputDeviceKind::Gamepad, First, 0.5, 1, 10000, Error) && FirstFeedback.Low == 32768 &&
			  FirstFeedback.High == 65535,
		  "normalized rumble reaches native motors");
	Check(!Native->Rumble(EInputDeviceKind::Joystick, Raw, 0.5, 0.5, 100, Error) &&
			  Error.find("unsupported_rumble") != std::string::npos,
		  "per-device unsupported rumble");
	Check(Native->Rumble(EInputDeviceKind::Gamepad, First, 1, 1, 0, Error) && FirstFeedback.Low == 0 &&
			  FirstFeedback.High == 0,
		  "zero duration explicitly stops motors even with nonzero amplitudes");
	Check(Native->Rumble(EInputDeviceKind::Gamepad, First, 0.5, 1, 10000, Error),
		  "restart feedback after zero duration stop");
	const auto Calls = FirstFeedback.Calls;
	Check(!Native->Rumble(EInputDeviceKind::Gamepad, First, -0.1, 1, 10, Error) &&
			  !Native->Rumble(EInputDeviceKind::Gamepad, First, 1, 1, -1, Error) &&
			  !Native->Rumble(EInputDeviceKind::Gamepad, First, 1, 1, MaxRumbleMilliseconds + 1, Error) &&
			  FirstFeedback.Calls == Calls,
		  "invalid feedback never reaches the device");
	bool Accepted = true;
	std::thread Wrong(
		[&]
		{
			std::string Err;
			Accepted = Native->Rumble(EInputDeviceKind::Gamepad, First, 1, 1, 1, Err);
		});
	Wrong.join();
	Check(!Accepted, "foreign thread feedback rejected");
	Check(Native->ClearInputState(0, Error) && FirstFeedback.Low == 0 && FirstFeedback.High == 0,
		  "global reset stops rumble");
	Events = Drain(*Native);
	Check(Neutral(State(*Native, EInputDeviceKind::Gamepad, First)) &&
			  Neutral(State(*Native, EInputDeviceKind::Joystick, Raw)) && Before.ControllerButtons[0],
		  "all controller categories reset and old snapshots remain stable");
	char Guid[33]{};
	SDL_GUIDToString(SDL_GetJoystickGUID(J), Guid, sizeof(Guid));
	const auto Mapping = std::string(Guid) + ",Vero mapped joystick,a:b2,leftx:a1,lefty:a0,";
	Check(SDL_AddGamepadMapping(Mapping.c_str()) >= 0, "add a mapping to a live raw joystick");
	Events = Drain(*Native);
	Check(!State(*Native, EInputDeviceKind::Joystick, Raw).Device.bConnected &&
			  State(*Native, EInputDeviceKind::Gamepad, Raw).Device.bConnected,
		  "live mapping transitions raw joystick to one standardized gamepad stream");
	// This SDL revision clears the bindings but retains the gamepad mapping entry.
	Check(SDL_SetGamepadMapping(Raw, nullptr) && SDL_IsGamepad(Raw),
		  "clearing native bindings retains the pinned SDL gamepad classification");
	Events = Drain(*Native);
	const auto ClearedMapping = State(*Native, EInputDeviceKind::Gamepad, Raw);
	Check(ClearedMapping.Device.bConnected && Neutral(ClearedMapping) &&
			  !State(*Native, EInputDeviceKind::Joystick, Raw).Device.bConnected &&
			  std::count_if(Events.begin(), Events.end(),
						[Raw](const auto& V)
						{
							return V.DeviceId == Raw && V.DeviceKind == EInputDeviceKind::Gamepad &&
								   V.Type == EEventType::InputDeviceRemoved;
						}) == 1 &&
			  std::count_if(Events.begin(), Events.end(),
							[Raw](const auto& V)
							{
							return V.DeviceId == Raw && V.DeviceKind == EInputDeviceKind::Gamepad &&
								   V.Type == EEventType::InputDeviceAdded;
							}) == 1,
		  "clearing bindings resets and reopens one mapped stream");
	Check(SDL_SetGamepadMapping(Raw, Mapping.c_str()) && SDL_SetGamepadMapping(Raw, nullptr) &&
			  SDL_SetGamepadMapping(Raw, Mapping.c_str()) && SDL_IsGamepad(Raw),
		  "queue multiple mapping changes before pumping");
	Drain(*Native);
	Check(State(*Native, EInputDeviceKind::Gamepad, Raw).Device.bConnected &&
			  !State(*Native, EInputDeviceKind::Joystick, Raw).Device.bConnected,
		  "queued mapping changes converge on the current native layout");
	Check(SDL_SetJoystickVirtualButton(J, 2, false), "release remapped gamepad input");
	Drain(*Native);
	Check(SDL_SetJoystickVirtualButton(J, 2, true), "press remapped gamepad input");
	Drain(*Native);
	Check(State(*Native, EInputDeviceKind::Gamepad, Raw).ControllerButtons[0] != 0,
		  "remapped gamepad continues receiving independent native input");
	Check(SDL_SetGamepadMapping(Raw, Mapping.c_str()), "queue mapping update immediately before unplugging");
	Check(SDL_DetachVirtualJoystick(Raw), "unplug joystick before pumping its mapping update");
	Events = Drain(*Native);
	const auto RawRemoved = std::find_if(Events.begin(), Events.end(),
										 [Raw](const auto& V)
										 {
											 return V.DeviceId == Raw && V.Type == EEventType::InputDeviceRemoved;
										 });
	Check(RawRemoved != Events.end() && RawRemoved->DeviceKind == EInputDeviceKind::Gamepad &&
			  std::count_if(Events.begin(), Events.end(),
							[Raw](const auto& V)
							{
								return V.DeviceId == Raw && V.Type == EEventType::InputDeviceRemoved;
							}) == 1 &&
			  std::any_of(Events.begin(), RawRemoved,
						  [Raw](const auto& V)
						  {
							  return V.DeviceId == Raw && V.Type == EEventType::ControllerButtonUp;
						  }) &&
			  !State(*Native, EInputDeviceKind::Joystick, Raw).Device.bConnected &&
			  !State(*Native, EInputDeviceKind::Gamepad, Raw).Device.bConnected,
		  "unplug before queued remapping releases input once without reopening a disconnected device");
	const auto Transient = Attach(false, "Vero transient joystick");
	Check(SDL_DetachVirtualJoystick(Transient), "detach before pumping the initial add");
	Events = Drain(*Native);
	Check(std::none_of(Events.begin(), Events.end(),
					   [Transient](const auto& V)
					   {
						   return V.DeviceId == Transient && (V.Type == EEventType::InputDeviceAdded ||
																 V.Type == EEventType::InputDeviceRemoved);
					   }),
		  "queued add and detach do not open a cached-name device that is already gone");
	Check(Native->ClearInputState(0, Error), "clear after mapping change");
	Drain(*Native);
	SDL_SetJoystickVirtualButton(A, 0, false);
	SDL_SetJoystickVirtualButton(B, 0, false);
	Drain(*Native);
	SDL_SetJoystickVirtualButton(A, 0, true);
	SDL_SetJoystickVirtualButton(B, 0, true);
	Drain(*Native);
	Check(SDL_DetachVirtualJoystick(First), "unplug first gamepad");
	Events = Drain(*Native);
	const auto Removed = std::find_if(Events.begin(), Events.end(),
									  [First](const auto& V)
									  {
										  return V.DeviceId == First && V.Type == EEventType::InputDeviceRemoved;
									  });
	Check(Removed != Events.end() &&
			  std::count_if(Events.begin(), Events.end(),
							[First](const auto& V)
							{
								return V.DeviceId == First && V.Type == EEventType::InputDeviceRemoved;
							}) == 1 &&
			  std::any_of(Events.begin(), Removed,
						  [First](const auto& V)
						  {
							  return V.DeviceId == First && V.Type == EEventType::ControllerButtonUp;
						  }) &&
			  !State(*Native, EInputDeviceKind::Gamepad, First).Device.bConnected &&
			  State(*Native, EInputDeviceKind::Gamepad, Second).ControllerButtons[0],
		  "disconnect releases precede removal and preserve other devices");
	Check(!Native->Rumble(EInputDeviceKind::Gamepad, First, 1, 1, 10, Error), "removed controller feedback rejected");
	Check(Native->Rumble(EInputDeviceKind::Gamepad, Second, 1, 0.25, 10000, Error), "second device feedback");
	FEvent Background;
	Background.Type = EEventType::Background;
	Check(Native->SubmitEvent(Background, Error), "submit background lifecycle");
	Drain(*Native);
	Check(Neutral(State(*Native, EInputDeviceKind::Gamepad, Second)) && SecondFeedback.Low == 0 &&
			  SecondFeedback.High == 0,
		  "background clears controller state and feedback");
	Check(Native->Rumble(EInputDeviceKind::Gamepad, Second, 1, 1, 10000, Error), "feedback before service retirement");
	std::thread Drop(
		[P = std::move(Native)]() mutable
		{
			P.reset();
		});
	Drop.join();
	Check(Sdl3::CollectRetired(Error) && SecondFeedback.Low == 0 && SecondFeedback.High == 0,
		  "foreign service retirement stops feedback on owner");
	SDL_CloseJoystick(A);
	SDL_CloseJoystick(B);
	SDL_CloseJoystick(J);
	SDL_DetachVirtualJoystick(Second);
	auto Reopened = Sdl3::CreatePlatform({}, Error);
	Check(static_cast<bool>(Reopened), "pump owner reusable after controller shutdown");
	Drain(*Reopened);
	Reopened.reset();
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	#else
	auto Native = Sdl3::CreatePlatform({}, Error);
	Check(Native && !Native->GetCapabilities().bControllers && !Native->GetCapabilities().bRumble,
		  "disabled controller backend advertises no capability");
	#endif
	Host.reset();
	Check(Sdl3::CollectRetired(Error), "final controller retirement");
	std::cout << "Controller failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
