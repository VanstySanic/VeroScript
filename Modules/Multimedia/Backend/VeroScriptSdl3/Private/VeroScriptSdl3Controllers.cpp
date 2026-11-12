// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Input.h"
#include "VeroScriptSdl3Runtime.h"
#include <cmath>
#include <limits>

namespace VeroScript::Sdl3
{
	using namespace Platform;
	#if VEROSCRIPT_SDL3_CONTROLLERS
	namespace
	{
		static_assert(SDL_GAMEPAD_AXIS_COUNT == 6 && SDL_GAMEPAD_BUTTON_COUNT == 26);
		struct FControllerHandle
		{
			~FControllerHandle()
			{
				if (Joystick && bRumbling)
				{
					SDL_RumbleJoystick(Joystick, 0, 0, 0);
				}
				if (Gamepad)
				{
					SDL_CloseGamepad(Gamepad);
				}
				else if (Joystick)
				{
					SDL_CloseJoystick(Joystick);
				}
			}
			SDL_Gamepad* Gamepad = nullptr;
			SDL_Joystick* Joystick = nullptr;
			FInputDevice Info;
			bool bRumbling = false;
		};
		struct FControllerRetirement final : FRetirement
		{
			void Release() noexcept override
			{
				Handles.clear();
			}
			std::map<SDL_JoystickID, std::unique_ptr<FControllerHandle>> Handles;
		};
		bool OpenController(FControllerRetirement& Lease, SDL_JoystickID Id, FInputDevice& Info, std::string& Error)
		{
			auto It = Lease.Handles.find(Id);
			if (It == Lease.Handles.end())
			{
				if (Lease.Handles.size() >= 4096)
				{
					Error = "platform.resource_limit: too many open controllers";
					return false;
				}
				auto Handle = std::make_unique<FControllerHandle>();
				if (SDL_IsGamepad(Id))
				{
					Handle->Gamepad = SDL_OpenGamepad(Id);
					if (Handle->Gamepad)
					{
						Handle->Joystick = SDL_GetGamepadJoystick(Handle->Gamepad);
					}
				}
				else
				{
					Handle->Joystick = SDL_OpenJoystick(Id);
				}
				if (!Handle->Joystick)
				{
					return SdlError("open controller", Error);
				}
				const char* Name =
					Handle->Gamepad ? SDL_GetGamepadName(Handle->Gamepad) : SDL_GetJoystickName(Handle->Joystick);
				Handle->Info = {Handle->Gamepad ? EInputDeviceKind::Gamepad : EInputDeviceKind::Joystick, Id,
								Name ? Name : "Controller", false};
				auto& D = Handle->Info;
				D.AxisCount = Handle->Gamepad ? SDL_GAMEPAD_AXIS_COUNT : SDL_GetNumJoystickAxes(Handle->Joystick);
				D.ButtonCount =
					Handle->Gamepad ? SDL_GAMEPAD_BUTTON_COUNT : SDL_GetNumJoystickButtons(Handle->Joystick);
				D.HatCount = Handle->Gamepad ? 0 : SDL_GetNumJoystickHats(Handle->Joystick);
				if (D.AxisCount < 0 || D.AxisCount > 256 || D.ButtonCount < 0 || D.ButtonCount > 256 ||
					D.HatCount < 0 || D.HatCount > 256)
				{
					Error = "platform.unsupported_controller: controller layout exceeds 256 controls per category";
					return false;
				}
				D.bRumble = SDL_GetBooleanProperty(SDL_GetJoystickProperties(Handle->Joystick),
												   SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
				It = Lease.Handles.emplace(Id, std::move(Handle)).first;
			}
			Info = It->second->Info;
			Error.clear();
			return true;
		}
	}
	#endif
	bool FInputStore::ObserveController(const SDL_Event& Native, bool& bHandled, std::string& Error)
	{
		bHandled = false;
		#if VEROSCRIPT_SDL3_CONTROLLERS
		if (!ControllerLease)
		{
			Error.clear();
			return true;
		}
		const auto Type = Native.type;
		const bool Added = Type == SDL_EVENT_JOYSTICK_ADDED || Type == SDL_EVENT_GAMEPAD_ADDED;
		const bool Removed = Type == SDL_EVENT_JOYSTICK_REMOVED || Type == SDL_EVENT_GAMEPAD_REMOVED;
		bool Remapped = Type == SDL_EVENT_GAMEPAD_REMAPPED;
		const bool Axis = Type == SDL_EVENT_JOYSTICK_AXIS_MOTION || Type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
		const bool Button = Type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || Type == SDL_EVENT_JOYSTICK_BUTTON_UP ||
							Type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || Type == SDL_EVENT_GAMEPAD_BUTTON_UP;
		const bool Hat = Type == SDL_EVENT_JOYSTICK_HAT_MOTION;
		if (!Added && !Removed && !Remapped && !Axis && !Button && !Hat)
		{
			Error.clear();
			return true;
		}
		bHandled = true;
		const auto Id = Native.jdevice.which; // SDL controller payloads share timestamp and instance ID fields.
		auto& Lease = static_cast<FControllerRetirement&>(*ControllerLease);
		auto Found = Lease.Handles.find(Id);
		if ((Type == SDL_EVENT_GAMEPAD_ADDED || Type == SDL_EVENT_GAMEPAD_REMOVED) &&
			Found != Lease.Handles.end() && SDL_JoystickConnected(Found->second->Joystick))
		{
			// Mapping notifications may describe an older view when several changes precede the pump.
			const auto Kind = SDL_IsGamepad(Id) ? EInputDeviceKind::Gamepad : EInputDeviceKind::Joystick;
			if (Found->second->Info.Kind == Kind &&
				(Type == SDL_EVENT_GAMEPAD_ADDED || Kind == EInputDeviceKind::Joystick))
			{
				Error.clear();
				return true;
			}
			Remapped = true;
		}
		FEvent Event;
		Event.DeviceId = Id;
		Event.TimestampNanoseconds = Native.common.timestamp;
		if (Removed || Remapped)
		{
			if (Found != Lease.Handles.end())
			{
				Event.Type = EEventType::InputDeviceRemoved;
				Event.DeviceKind = Found->second->Info.Kind;
				if (!Observe(Event, Error))
				{
					return false;
				}
				Lease.Handles.erase(Found);
			}
			if (!Remapped)
			{
				Error.clear();
				return true;
			}
		}
		if (Added || Remapped)
		{
			// SDL may queue an add followed by a removal before the application pumps.
			// Names remain cached after disconnect, so reconcile against the live IDs.
			int Count = 0;
			std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> Ids(SDL_GetJoysticks(&Count), SDL_free);
			if (!Ids)
			{
				return SdlError("enumerate controllers", Error);
			}
			bool bConnected = false;
			for (int I = 0; I < Count; ++I)
			{
				bConnected = bConnected || Ids.get()[I] == Id;
			}
			if (!bConnected)
			{
				Error.clear();
				return true;
			}
			FInputDevice Info;
			if (!OpenController(Lease, Id, Info, Error))
			{
				return false;
			}
			Event.Type = EEventType::InputDeviceAdded;
			Event.DeviceKind = Info.Kind;
			Event.DeviceName = Info.Name;
			Event.AxisCount = Info.AxisCount;
			Event.ButtonCount = Info.ButtonCount;
			Event.HatCount = Info.HatCount;
			Event.bRumble = Info.bRumble;
			return Observe(std::move(Event), Error);
		}
		if (Found == Lease.Handles.end())
		{
			Error.clear();
			return true;
		}
		const bool MappedEvent = Type == SDL_EVENT_GAMEPAD_AXIS_MOTION || Type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
								 Type == SDL_EVENT_GAMEPAD_BUTTON_UP;
		if (MappedEvent != (Found->second->Info.Kind == EInputDeviceKind::Gamepad))
		{
			Error.clear();
			return true;
		}
		Event.DeviceKind = Found->second->Info.Kind;
		if (Axis)
		{
			Event.Type = EEventType::ControllerAxis;
			Event.Control = Native.jaxis.axis;
			const auto Value = Native.jaxis.value;
			Event.Value = Value < 0 ? Value / 32768.0 : Value / 32767.0;
		}
		else if (Hat)
		{
			Event.Type = EEventType::ControllerHat;
			Event.Control = Native.jhat.hat;
			Event.Value = Native.jhat.value;
		}
		else
		{
			Event.Type = (Type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || Type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
							 ? EEventType::ControllerButtonDown
							 : EEventType::ControllerButtonUp;
			Event.Control = Native.jbutton.button;
		}
		return Observe(std::move(Event), Error);
		#else
		(void)Native;
		Error.clear();
		return true;
		#endif
	}

	bool FInputStore::Rumble(EInputDeviceKind Kind, std::uint64_t Id, double Low, double High,
							 std::int64_t Milliseconds, std::string& Error)
	{
		if (!Runtime->CheckThread(Error) || !ValidateRumble(Low, High, Milliseconds, Error))
		{
			return false;
		}
		#if VEROSCRIPT_SDL3_CONTROLLERS
		if (ControllerLease && Id && Id <= std::numeric_limits<SDL_JoystickID>::max() && IsController(Kind))
		{
			auto& Handles = static_cast<FControllerRetirement&>(*ControllerLease).Handles;
			const auto Found = Handles.find(static_cast<SDL_JoystickID>(Id));
			const auto Device = Devices.find({Kind, Id});
			if (Device == Devices.end() || !Device->second.Info.bConnected || Found == Handles.end() ||
				Found->second->Info.Kind != Kind || !SDL_JoystickConnected(Found->second->Joystick))
			{
				Error = "platform.unknown_input_device: controller is disconnected or has another kind";
				return false;
			}
			if (!Found->second->Info.bRumble)
			{
				Error = "platform.unsupported_rumble: controller has no rumble motors";
				return false;
			}
			// SDL's duration zero disables its expiry timer; explicitly neutralize the motors.
			if (!Milliseconds)
			{
				Low = High = 0;
			}
			if (!SDL_RumbleJoystick(Found->second->Joystick, static_cast<Uint16>(std::lround(Low * 65535)),
									static_cast<Uint16>(std::lround(High * 65535)), static_cast<Uint32>(Milliseconds)))
			{
				return SdlError("rumble controller", Error);
			}
			Found->second->bRumbling = Milliseconds != 0 && (Low != 0 || High != 0);
			Error.clear();
			return true;
		}
		#else
		(void)Kind;
		(void)Id;
		#endif
		Error = "platform.unsupported_rumble: native controller feedback is unavailable on this stream";
		return false;
	}

	bool FInputStore::InitializeControllers(std::string& Error)
	{
		#if VEROSCRIPT_SDL3_CONTROLLERS
		if (!Runtime->EnsureControllers(Error))
		{
			return false;
		}
		auto Lease = std::make_unique<FControllerRetirement>();
		Lease->Runtime = Runtime;
		int Count = 0;
		std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> Ids(SDL_GetJoysticks(&Count), SDL_free);
		if (!Ids)
		{
			return SdlError("enumerate controllers", Error);
		}
		ControllerLease = std::move(Lease);
		for (int I = 0; I < Count; ++I)
		{
			FInputDevice Info;
			if (!OpenController(static_cast<FControllerRetirement&>(*ControllerLease), Ids.get()[I], Info, Error) ||
				!AddDevice(std::move(Info), false, 0, Error))
			{
				return false;
			}
		}
		#endif
		Error.clear();
		return true;
	}

	void FInputStore::StopController(std::uint64_t Id) noexcept
	{
		#if VEROSCRIPT_SDL3_CONTROLLERS
		if (ControllerLease && Id <= std::numeric_limits<SDL_JoystickID>::max())
		{
			auto& Handles = static_cast<FControllerRetirement&>(*ControllerLease).Handles;
			const auto Found = Handles.find(static_cast<SDL_JoystickID>(Id));
			if (Found != Handles.end() && Found->second->bRumbling)
			{
				if (SDL_RumbleJoystick(Found->second->Joystick, 0, 0, 0) ||
					!SDL_JoystickConnected(Found->second->Joystick))
				{
					Found->second->bRumbling = false;
				}
			}
		}
		#else
		(void)Id;
		#endif
	}
}
