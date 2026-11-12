// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Input.h"
#include "VeroScriptSdl3Runtime.h"
#include <algorithm>
#include <set>

namespace VeroScript::Sdl3
{
	using namespace Platform;
	namespace
	{
		constexpr std::size_t MaxDevices = 4096, MaxActiveRecords = 8192, MaxQueuedEvents = 65536;
		const char* DefaultName(EInputDeviceKind Kind)
		{
			switch (Kind)
			{
			case EInputDeviceKind::Keyboard:
				return "Keyboard";
			case EInputDeviceKind::Mouse:
				return "Mouse";
			case EInputDeviceKind::Touch:
				return "Touch";
			case EInputDeviceKind::Joystick:
				return "Joystick";
			case EInputDeviceKind::Gamepad:
				return "Gamepad";
			default:
				return "Unknown";
			}
		}
		std::uint64_t KeyToken(const FEvent& Event)
		{
			return Event.ScanCode ? (std::uint64_t{1} << 32) | static_cast<std::uint32_t>(Event.ScanCode)
			                      : static_cast<std::uint32_t>(Event.Key);
		}
	}
	FInputStore::FInputStore(std::shared_ptr<FRuntime> R, bool Native) : Runtime(std::move(R)), bNative(Native)
	{
	}
	FInputStore::~FInputStore()
	{
		Runtime->Retire(std::move(ControllerLease));
	}
	bool FInputStore::Initialize(std::string& Error)
	{
		if (!AddDevice({EInputDeviceKind::Keyboard, 0, "System keyboard", true}, false, 0, Error) ||
		    !AddDevice({EInputDeviceKind::Mouse, 0, "System mouse", true}, false, 0, Error))
		{
			return false;
		}
		return !bNative || (InitializeControllers(Error) && RefreshNative(true, Error));
	}
	bool FInputStore::RefreshNative(bool bAll, std::string& Error)
	{
		if (!bNative)
		{
			Error.clear();
			return true;
		}
		const auto Time = SDL_GetTicksNS();
		if (!bAll && Time - LastRefresh < 250000000)
		{
			Error.clear();
			return true;
		}
		std::vector<FInputDevice> Found;
		int Count = 0;
		if (bAll)
		{
			std::unique_ptr<SDL_KeyboardID, decltype(&SDL_free)> Keys(SDL_GetKeyboards(&Count), SDL_free);
			if (!Keys)
			{
				return SdlError("enumerate keyboards", Error);
			}
			for (int I = 0; I < Count; ++I)
			{
				const auto Id = Keys.get()[I];
				const char* Name = SDL_GetKeyboardNameForID(Id);
				Found.push_back({EInputDeviceKind::Keyboard, Id, Name ? Name : "Keyboard", Id == 0});
			}
			std::unique_ptr<SDL_MouseID, decltype(&SDL_free)> Mice(SDL_GetMice(&Count), SDL_free);
			if (!Mice)
			{
				return SdlError("enumerate mice", Error);
			}
			for (int I = 0; I < Count; ++I)
			{
				const auto Id = Mice.get()[I];
				const char* Name = SDL_GetMouseNameForID(Id);
				Found.push_back({EInputDeviceKind::Mouse, Id, Name ? Name : "Mouse", Id == 0 || Id == SDL_PEN_MOUSEID});
			}
		}
		std::unique_ptr<SDL_TouchID, decltype(&SDL_free)> Touches(SDL_GetTouchDevices(&Count), SDL_free);
		if (!Touches)
		{
			return SdlError("enumerate touch devices", Error);
		}
		for (int I = 0; I < Count; ++I)
		{
			const auto Id = Touches.get()[I];
			// Mouse emulation must not duplicate the corresponding mouse stream.
			if (Id == SDL_MOUSE_TOUCHID)
			{
				continue;
			}
			const char* Name = SDL_GetTouchDeviceName(Id);
			Found.push_back({EInputDeviceKind::Touch, Id, Name ? Name : "Touch", Id == 0});
		}
		std::set<FDeviceKey> TouchIds;
		for (auto& Info : Found)
		{
			if (Info.Kind == EInputDeviceKind::Touch)
			{
				TouchIds.emplace(Info.Kind, Info.DeviceId);
			}
			// Only initial discovery and touch reconciliation use snapshots. Keyboard/mouse
			// changes thereafter follow SDL's ordered native events.
			if (!AddDevice(std::move(Info), LastRefresh != 0, Time, Error))
			{
				return false;
			}
		}
		for (auto& Entry : Devices)
		{
			if (Entry.second.Info.Kind == EInputDeviceKind::Touch && Entry.second.Info.DeviceId &&
			    Entry.second.Info.bConnected && !TouchIds.count(Entry.first))
			{
				RemoveDevice(Entry.second, Time);
			}
		}
		LastRefresh = Time ? Time : 1;
		Error.clear();
		return true;
	}
	bool FInputStore::Submit(const FEvent& Event, std::string& Error)
	{
		if (Submitted.size() >= MaxQueuedEvents)
		{
			Error = "platform.resource_limit: submitted event queue is full";
			return false;
		}
		Submitted.push_back(Event);
		Error.clear();
		return true;
	}
	bool FInputStore::TakeSubmitted(FEvent& Event)
	{
		if (Submitted.empty())
		{
			return false;
		}
		Event = std::move(Submitted.front());
		Submitted.pop_front();
		return true;
	}
	bool FInputStore::TakeReady(FEvent& Event)
	{
		if (bResync)
		{
			Ready.clear();
			bReadyHasInput = false;
			DropQueuedInput(0);
			for (auto& Entry : Devices)
			{
				Entry.second.Keys.clear();
				Entry.second.Pointers.clear();
				std::fill(Entry.second.Axes.begin(), Entry.second.Axes.end(), 0);
				std::fill(Entry.second.Buttons.begin(), Entry.second.Buttons.end(), std::uint8_t{0});
				std::fill(Entry.second.Hats.begin(), Entry.second.Hats.end(), std::uint8_t{0});
				if (IsController(Entry.first.first))
				{
					StopController(Entry.first.second);
				}
			}
			ActiveRecords = 0;
			Event = {};
			Event.Type = EEventType::InputStateReset;
			Event.TimestampNanoseconds = SDL_GetTicksNS();
			Event.bSynthetic = true;
			bResync = false;
			return true;
		}
		if (Ready.empty())
		{
			return false;
		}
		Event = std::move(Ready.front());
		Ready.pop_front();
		if (Ready.empty())
		{
			bReadyHasInput = false;
		}
		return true;
	}
	bool FInputStore::Observe(FEvent Event, std::string& Error)
	{
		// Keep the state machine safe even for injected native events.
		if (!ValidateEvent(Event, Error))
		{
			return false;
		}
		Event.DeviceKind = GetInputDeviceKind(Event);
		Event.bAggregateDevice =
		    Event.bAggregateDevice || (Event.DeviceKind != EInputDeviceKind::Unknown && Event.DeviceId == 0);
		if (Event.Type == EEventType::InputDeviceAdded)
		{
			FInputDevice Info{Event.DeviceKind, Event.DeviceId, std::move(Event.DeviceName), Event.bAggregateDevice};
			Info.AxisCount = Event.AxisCount;
			Info.ButtonCount = Event.ButtonCount;
			Info.HatCount = Event.HatCount;
			Info.bRumble = Event.bRumble;
			return AddDevice(std::move(Info), true, Event.TimestampNanoseconds, Error);
		}
		if (Event.Type == EEventType::InputDeviceRemoved)
		{
			auto Found = Devices.find({Event.DeviceKind, Event.DeviceId});
			if (Found == Devices.end())
			{
				if (!AddDevice({Event.DeviceKind, Event.DeviceId, Event.DeviceName, Event.bAggregateDevice}, false,
				        Event.TimestampNanoseconds, Error))
				{
					return false;
				}
				Found = Devices.find({Event.DeviceKind, Event.DeviceId});
			}
			if (Found->second.Info.bConnected)
			{
				RemoveDevice(Found->second, Event.TimestampNanoseconds);
			}
			Error.clear();
			return true;
		}
		if (Event.Type == EEventType::InputStateReset)
		{
			// A submitted reset is ordered with subsequent host input. Do not discard it.
			for (auto& Entry : Devices)
			{
				ResetDevice(Entry.second, Event.WindowId, true, Event.TimestampNanoseconds);
			}
		}
		else if (Event.Type == EEventType::FocusLost || Event.Type == EEventType::Hidden ||
		    Event.Type == EEventType::Minimized || Event.Type == EEventType::Background ||
		    Event.Type == EEventType::Quit)
		{
			const bool bAll = Event.Type == EEventType::Background || Event.Type == EEventType::Quit;
			for (auto& Entry : Devices)
			{
				ResetDevice(Entry.second, bAll || IsController(Entry.first.first) ? 0 : Event.WindowId, true,
				    Event.TimestampNanoseconds);
			}
		}
		if (IsInput(Event.Type))
		{
			const FDeviceKey Key{Event.DeviceKind, Event.DeviceId};
			auto Found = Devices.find(Key);
			// A removed source cannot be resurrected by queued stale input.
			if (Found != Devices.end() && !Found->second.Info.bConnected)
			{
				Error.clear();
				return true;
			}
			if (Found == Devices.end())
			{
				if (!AddDevice({Event.DeviceKind, Event.DeviceId, Event.DeviceName, Event.bAggregateDevice}, true,
				        Event.TimestampNanoseconds, Error))
				{
					return false;
				}
				Found = Devices.find(Key);
			}
			Event.bAggregateDevice = Found->second.Info.bAggregate;
			if (!ApplyInput(Found->second, Event, Error))
			{
				return false;
			}
		}
		Emit(std::move(Event));
		Error.clear();
		return true;
	}
	bool FInputStore::GetDevices(std::vector<FInputDevice>& Result, std::string& Error)
	{
		// Preserve keyboard/mouse event ordering; refresh touch topology only.
		if (!RefreshNative(false, Error))
		{
			return false;
		}
		std::vector<FInputDevice> Copy;
		for (const auto& Entry : Devices)
		{
			if (Entry.second.Info.bConnected)
			{
				Copy.push_back(Entry.second.Info);
			}
		}
		Result = std::move(Copy);
		Error.clear();
		return true;
	}
	bool FInputStore::GetState(EInputDeviceKind Kind, std::uint64_t Id, FInputState& Result, std::string& Error) const
	{
		const auto Found = Devices.find({Kind, Id});
		if (Found == Devices.end())
		{
			Error = "platform.unknown_input_device: no state for this kind and identity";
			return false;
		}
		FInputState Copy;
		Copy.Device = Found->second.Info;
		Copy.Modifiers = Modifiers(Found->second);
		Copy.Axes = Found->second.Axes;
		Copy.ControllerButtons = Found->second.Buttons;
		Copy.Hats = Found->second.Hats;
		for (const auto& Key : Found->second.Keys)
		{
			Copy.Keys.push_back(Key.second);
		}
		for (const auto& Pointer : Found->second.Pointers)
		{
			Copy.Pointers.push_back(Pointer.second.Value);
		}
		Result = std::move(Copy);
		Error.clear();
		return true;
	}
	void FInputStore::Clear(std::uint64_t WindowId, bool bNotifyReset, bool bNotifyHover, std::uint64_t Time) noexcept
	{
		const bool bDroppedRelease = DropQueuedInput(WindowId);
		bool bActive = false;
		for (auto& Entry : Devices)
		{
			bActive |= ResetDevice(Entry.second, WindowId, bNotifyHover, Time);
		}
		if (bNotifyReset || bActive || bDroppedRelease)
		{
			FEvent Reset;
			Reset.Type = EEventType::InputStateReset;
			Reset.WindowId = WindowId;
			Reset.TimestampNanoseconds = Time;
			Reset.bSynthetic = true;
			Emit(std::move(Reset));
		}
	}
	void FInputStore::WindowClosed(std::uint64_t WindowId) noexcept
	{
		Clear(WindowId, false, false, SDL_GetTicksNS());
	}
	bool FInputStore::AddDevice(FInputDevice Info, bool bNotify, std::uint64_t Time, std::string& Error)
	{
		if (Info.Kind == EInputDeviceKind::Gamepad)
		{
			// Stable standard slots exist even when a model omits a physical control.
			Info.AxisCount = 6;
			Info.ButtonCount = 26;
			Info.HatCount = 0;
		}
		Info.bAggregate = Info.bAggregate || Info.DeviceId == 0;
		Info.bConnected = true;
		if (Info.Name.empty())
		{
			Info.Name = DefaultName(Info.Kind);
		}
		const FDeviceKey Key{Info.Kind, Info.DeviceId};
		auto Found = Devices.find(Key);
		if (Found == Devices.end())
		{
			if (Devices.size() >= MaxDevices)
			{
				const auto Old = std::find_if(Devices.begin(), Devices.end(),
				    [](const auto& Entry)
				{
					return !Entry.second.Info.bConnected;
				});
				if (Old == Devices.end())
				{
					Error = "platform.resource_limit: too many input devices";
					return false;
				}
				Devices.erase(Old);
			}
			Found = Devices.emplace(Key, FDevice{}).first;
		}
		else if (Found->second.Info.bConnected)
		{
			// Repeated discovery does not duplicate connection notifications.
			Error.clear();
			return true;
		}
		auto& Device = Found->second;
		Device.Axes.resize(static_cast<std::size_t>(Info.AxisCount));
		Device.Buttons.resize(static_cast<std::size_t>(Info.ButtonCount));
		Device.Hats.resize(static_cast<std::size_t>(Info.HatCount));
		Device.bLayoutKnown = Info.AxisCount || Info.ButtonCount || Info.HatCount;
		Found->second.Info = std::move(Info);
		if (bNotify)
		{
			FEvent Added;
			Added.Type = EEventType::InputDeviceAdded;
			Added.DeviceKind = Key.first;
			Added.DeviceId = Key.second;
			Added.DeviceName = Found->second.Info.Name;
			Added.AxisCount = Found->second.Info.AxisCount;
			Added.ButtonCount = Found->second.Info.ButtonCount;
			Added.HatCount = Found->second.Info.HatCount;
			Added.bRumble = Found->second.Info.bRumble;
			Added.bAggregateDevice = Found->second.Info.bAggregate;
			Added.TimestampNanoseconds = Time;
			Emit(std::move(Added));
		}
		Error.clear();
		return true;
	}
	void FInputStore::RemoveDevice(FDevice& Device, std::uint64_t Time) noexcept
	{
		// A topology refresh can run while notifications from the previous input remain queued.
		Ready.erase(
		    std::remove_if(
		        Ready.begin(), Ready.end(),
		        [&](const FEvent& Event)
		{
			const bool bDrop =
			    IsInput(Event.Type) && Event.DeviceKind == Device.Info.Kind &&
			    (Event.DeviceId == Device.Info.DeviceId || (Device.Info.DeviceId && Event.DeviceId == 0));
			if (bDrop &&
			    (Event.Type == EEventType::KeyUp || Event.Type == EEventType::PointerUp ||
			        Event.Type == EEventType::PointerCancelled || Event.Type == EEventType::ControllerButtonUp ||
			        ((Event.Type == EEventType::ControllerAxis || Event.Type == EEventType::ControllerHat) &&
			            Event.Value == 0)))
			{
				bResync = true;
			}
			return bDrop;
		}),
		    Ready.end());
		ResetDevice(Device, 0, true, Time);
		if (Device.Info.DeviceId)
		{
			// Unknown-origin held input may have come from the device just unplugged.
			// Conservatively release it rather than leaving aggregate keys/buttons stuck.
			const auto Aggregate = Devices.find({Device.Info.Kind, 0});
			if (Aggregate != Devices.end())
			{
				ResetDevice(Aggregate->second, 0, true, Time);
			}
		}
		Device.Info.bConnected = false;
		try
		{
			FEvent Removed;
			Removed.Type = EEventType::InputDeviceRemoved;
			Removed.DeviceKind = Device.Info.Kind;
			Removed.DeviceId = Device.Info.DeviceId;
			Removed.DeviceName = Device.Info.Name;
			Removed.bAggregateDevice = Device.Info.bAggregate;
			Removed.TimestampNanoseconds = Time;
			Emit(std::move(Removed));
		}
		catch (...)
		{
			bResync = true;
		}
	}
	bool FInputStore::ApplyInput(FDevice& Device, FEvent& Event, std::string& Error)
	{
		const auto Type = Event.Type;
		if (IsController(Device.Info.Kind))
		{
			const auto Index = static_cast<std::size_t>(Event.Control);
			const bool bAxis = Type == EEventType::ControllerAxis, bHat = Type == EEventType::ControllerHat;
			const auto Size = bAxis ? Device.Axes.size() : bHat ? Device.Hats.size()
			                                                    : Device.Buttons.size();
			if (Device.bLayoutKnown && Index >= Size)
			{
				Error = "platform.invalid_controller: control is outside the announced layout";
				return false;
			}
			const double Value = bAxis || bHat ? Event.Value : Type == EEventType::ControllerButtonDown ? 1
			                                                                                            : 0;
			const double Old = Index < Size ? (bAxis ? Device.Axes[Index]
			                                          : bHat ? Device.Hats[Index]
			                                                 : Device.Buttons[Index])
			                                : 0;
			if (!Old && Value && ActiveRecords >= MaxActiveRecords)
			{
				Error = "platform.resource_limit: too many active inputs";
				return false;
			}
			if (bAxis)
			{
				Device.Axes.resize(std::max(Device.Axes.size(), Index + 1));
				Device.Axes[Index] = Value;
			}
			else if (bHat)
			{
				Device.Hats.resize(std::max(Device.Hats.size(), Index + 1));
				Device.Hats[Index] = static_cast<std::uint8_t>(Value);
			}
			else
			{
				Device.Buttons.resize(std::max(Device.Buttons.size(), Index + 1));
				Device.Buttons[Index] = static_cast<std::uint8_t>(Value);
			}
			if (!Old && Value)
			{
				++ActiveRecords;
			}
			if (Old && !Value)
			{
				--ActiveRecords;
			}
			Device.Info.AxisCount = static_cast<std::int32_t>(Device.Axes.size());
			Device.Info.ButtonCount = static_cast<std::int32_t>(Device.Buttons.size());
			Device.Info.HatCount = static_cast<std::int32_t>(Device.Hats.size());
			return true;
		}
		if (Type == EEventType::KeyDown || Type == EEventType::KeyUp)
		{
			const auto Token = KeyToken(Event);
			if (!Token)
			{
				return true;
			}
			const auto Found = Device.Keys.find(Token);
			if (Type == EEventType::KeyUp)
			{
				if (Found != Device.Keys.end())
				{
					const auto Previous = Found->second;
					Device.Keys.erase(Found);
					--ActiveRecords;
					if (Previous.WindowId != Event.WindowId || Previous.Key != Event.Key)
					{
						FEvent Up = Event;
						Up.WindowId = Previous.WindowId;
						Up.Key = Previous.Key;
						Up.ScanCode = Previous.ScanCode;
						Up.Modifiers = Modifiers(Device);
						Up.bRepeat = false;
						Up.bSynthetic = true;
						Emit(std::move(Up));
					}
				}
			}
			else
			{
				if (Found == Device.Keys.end() && ActiveRecords >= MaxActiveRecords)
				{
					Error = "platform.resource_limit: too many active inputs";
					return false;
				}
				if (Found == Device.Keys.end())
				{
					Device.Keys.emplace(Token, FPressedKey{Event.Key, Event.ScanCode, Event.WindowId});
					++ActiveRecords;
				}
				else
				{
					if (Found->second.WindowId != Event.WindowId || Found->second.Key != Event.Key)
					{
						FEvent Up = Event;
						Up.Type = EEventType::KeyUp;
						Up.WindowId = Found->second.WindowId;
						Up.Key = Found->second.Key;
						Up.bRepeat = false;
						Up.bSynthetic = true;
						Emit(std::move(Up));
					}
					Found->second = {Event.Key, Event.ScanCode, Event.WindowId};
				}
			}
			return true;
		}
		if (Device.Info.Kind != EInputDeviceKind::Mouse && Device.Info.Kind != EInputDeviceKind::Touch)
		{
			return true;
		}
		const bool bTouch = Device.Info.Kind == EInputDeviceKind::Touch;
		auto Found = Device.Pointers.find(Event.PointerId);
		if (Type == EEventType::PointerCancelled || (bTouch && Type == EEventType::PointerUp))
		{
			if (Found != Device.Pointers.end() && Found->second.PressedWindow &&
			    Found->second.PressedWindow != Event.WindowId)
			{
				FEvent Cancel = Event;
				Cancel.Type = EEventType::PointerCancelled;
				Cancel.WindowId = Found->second.PressedWindow;
				Cancel.X = Found->second.PressedX;
				Cancel.Y = Found->second.PressedY;
				Cancel.Pressure = 0;
				Cancel.Buttons = 0;
				Cancel.bSynthetic = true;
				Emit(std::move(Cancel));
			}
			if (Found != Device.Pointers.end())
			{
				Device.Pointers.erase(Found);
				--ActiveRecords;
			}
			Event.Buttons = 0;
			return true;
		}
		if (Found == Device.Pointers.end())
		{
			if ((bTouch && Type != EEventType::PointerDown) || Type == EEventType::PointerUp)
			{
				// No locally observed press can contribute buttons to this event.
				Event.Buttons = 0;
				return true;
			}
			if (ActiveRecords >= MaxActiveRecords)
			{
				Error = "platform.resource_limit: too many active inputs";
				return false;
			}
			Found = Device.Pointers.emplace(Event.PointerId, FPointer{}).first;
			++ActiveRecords;
		}
		auto& Pointer = Found->second;
		if (Type == EEventType::PointerDown)
		{
			if (Pointer.PressedWindow && Pointer.PressedWindow != Event.WindowId)
			{
				FEvent Cancel;
				Cancel.Type = EEventType::PointerCancelled;
				Cancel.DeviceKind = Device.Info.Kind;
				Cancel.DeviceId = Device.Info.DeviceId;
				Cancel.PointerKind = Event.PointerKind;
				Cancel.PointerId = Event.PointerId;
				Cancel.WindowId = Pointer.PressedWindow;
				Cancel.X = Pointer.PressedX;
				Cancel.Y = Pointer.PressedY;
				Cancel.TimestampNanoseconds = Event.TimestampNanoseconds;
				Cancel.bAggregateDevice = Device.Info.bAggregate;
				Cancel.bSynthetic = true;
				Emit(std::move(Cancel));
				Pointer.Value.Buttons = 0;
			}
			if (!Pointer.Value.Buttons || bTouch)
			{
				Pointer.PressedWindow = Event.WindowId;
			}
			Pointer.Value.Buttons |= bTouch ? 1u : (1u << (Event.Button - 1));
		}
		else if (Type == EEventType::PointerUp)
		{
			Pointer.Value.Buttons &= ~(1u << (Event.Button - 1));
			if (Pointer.PressedWindow && Pointer.PressedWindow != Event.WindowId)
			{
				FEvent Up = Event;
				Up.WindowId = Pointer.PressedWindow;
				Up.X = Pointer.PressedX;
				Up.Y = Pointer.PressedY;
				Up.Buttons = Pointer.Value.Buttons;
				Up.bSynthetic = true;
				Emit(std::move(Up));
			}
			if (!Pointer.Value.Buttons)
			{
				Pointer.PressedWindow = 0;
			}
		}
		Pointer.Value.PointerId = Event.PointerId;
		Pointer.Value.WindowId = Event.WindowId;
		Pointer.Value.X = Event.X;
		Pointer.Value.Y = Event.Y;
		Pointer.Value.Pressure = Event.Pressure;
		if (Pointer.PressedWindow == Event.WindowId)
		{
			Pointer.PressedX = Event.X;
			Pointer.PressedY = Event.Y;
		}
		Event.Buttons = Pointer.Value.Buttons;
		return true;
	}
	bool FInputStore::ResetDevice(FDevice& Device, std::uint64_t WindowId, bool bNotifyHover,
	    std::uint64_t Time) noexcept
	{
		bool bActive = false;
		if (IsController(Device.Info.Kind) && !WindowId)
		{
			StopController(Device.Info.DeviceId);
			for (unsigned Category = 0; Category < 3; ++Category)
			{
				const auto Count = Category == 0 ? Device.Axes.size()
				    : Category == 1              ? Device.Buttons.size()
				                                 : Device.Hats.size();
				for (std::size_t I = 0; I < Count; ++I)
				{
					const double Value = Category == 0 ? Device.Axes[I]
					    : Category == 1                ? Device.Buttons[I]
					                                   : Device.Hats[I];
					if (!Value)
					{
						continue;
					}
					FEvent Reset;
					Reset.Type = Category == 0 ? EEventType::ControllerAxis
					    : Category == 1        ? EEventType::ControllerButtonUp
					                           : EEventType::ControllerHat;
					Reset.DeviceKind = Device.Info.Kind;
					Reset.DeviceId = Device.Info.DeviceId;
					Reset.Control = static_cast<std::int32_t>(I);
					Reset.TimestampNanoseconds = Time;
					Reset.bSynthetic = true;
					Emit(std::move(Reset));
					if (Category == 0)
					{
						Device.Axes[I] = 0;
					}
					else if (Category == 1)
					{
						Device.Buttons[I] = 0;
					}
					else
					{
						Device.Hats[I] = 0;
					}
					--ActiveRecords;
					bActive = true;
				}
			}
		}
		for (auto It = Device.Keys.begin(); It != Device.Keys.end();)
		{
			if (WindowId && It->second.WindowId != WindowId)
			{
				++It;
				continue;
			}
			FEvent Up;
			Up.Type = EEventType::KeyUp;
			Up.DeviceKind = Device.Info.Kind;
			Up.DeviceId = Device.Info.DeviceId;
			Up.bAggregateDevice = Device.Info.bAggregate;
			Up.WindowId = It->second.WindowId;
			Up.Key = It->second.Key;
			Up.ScanCode = It->second.ScanCode;
			Up.TimestampNanoseconds = Time;
			Up.bSynthetic = true;
			It = Device.Keys.erase(It);
			--ActiveRecords;
			Up.Modifiers = Modifiers(Device);
			Emit(std::move(Up));
			bActive = true;
		}
		for (auto It = Device.Pointers.begin(); It != Device.Pointers.end();)
		{
			const auto& Pointer = It->second;
			if (WindowId && Pointer.Value.WindowId != WindowId && Pointer.PressedWindow != WindowId)
			{
				++It;
				continue;
			}
			if (Pointer.Value.Buttons || bNotifyHover)
			{
				FEvent Cancel;
				Cancel.Type = EEventType::PointerCancelled;
				Cancel.DeviceKind = Device.Info.Kind;
				Cancel.DeviceId = Device.Info.DeviceId;
				Cancel.bAggregateDevice = Device.Info.bAggregate;
				Cancel.PointerKind =
				    Device.Info.Kind == EInputDeviceKind::Touch ? EPointerKind::Touch : EPointerKind::Mouse;
				Cancel.PointerId = It->first;
				Cancel.WindowId = Pointer.PressedWindow ? Pointer.PressedWindow : Pointer.Value.WindowId;
				const bool bCaptured = Pointer.PressedWindow && Pointer.PressedWindow != Pointer.Value.WindowId;
				Cancel.X = bCaptured ? Pointer.PressedX : Pointer.Value.X;
				Cancel.Y = bCaptured ? Pointer.PressedY : Pointer.Value.Y;
				Cancel.TimestampNanoseconds = Time;
				Cancel.bSynthetic = true;
				Emit(std::move(Cancel));
				bActive = true;
			}
			It = Device.Pointers.erase(It);
			--ActiveRecords;
		}
		return bActive;
	}
	void FInputStore::Emit(FEvent Event) noexcept
	{
		if (bResync)
		{
			return;
		}
		try
		{
			if (Ready.size() >= MaxQueuedEvents)
			{
				bResync = true;
				return;
			}
			Ready.push_back(std::move(Event));
			bReadyHasInput |= IsInput(Ready.back().Type);
		}
		catch (...)
		{
			bResync = true;
		}
	}
	bool FInputStore::DropQueuedInput(std::uint64_t WindowId) noexcept
	{
		const auto Drop = [WindowId](const FEvent& Event)
		{
			return IsInput(Event.Type) && (!WindowId || Event.WindowId == WindowId);
		};
		Submitted.erase(std::remove_if(Submitted.begin(), Submitted.end(), Drop), Submitted.end());
		if (!bReadyHasInput)
		{
			return false;
		}
		bool bDroppedRelease = false;
		bool bRemainingInput = false;
		Ready.erase(std::remove_if(Ready.begin(), Ready.end(),
		                [&](const FEvent& Event)
		{
			if (!Drop(Event))
			{
				bRemainingInput |= IsInput(Event.Type);
				return false;
			}
			bDroppedRelease |= Event.Type == EEventType::KeyUp ||
			    Event.Type == EEventType::PointerUp ||
			    Event.Type == EEventType::PointerCancelled ||
			    Event.Type == EEventType::ControllerButtonUp ||
			    Event.Type == EEventType::ControllerAxis ||
			    Event.Type == EEventType::ControllerHat;
			return true;
		}),
		    Ready.end());
		bReadyHasInput = bRemainingInput;
		return bDroppedRelease;
	}
	std::uint32_t FInputStore::Modifiers(const FDevice& Device) noexcept
	{
		std::uint32_t Result = 0;
		for (const auto& Key : Device.Keys)
		{
			switch (Key.second.Key)
			{
			case EKey::LeftShift:
			case EKey::RightShift:
				{
					Result |= static_cast<unsigned>(EModifier::Shift);
					break;
				}
			case EKey::LeftControl:
			case EKey::RightControl:
				{
					Result |= static_cast<unsigned>(EModifier::Control);
					break;
				}
			case EKey::LeftAlt:
			case EKey::RightAlt:
				{
					Result |= static_cast<unsigned>(EModifier::Alt);
					break;
				}
			case EKey::LeftSuper:
			case EKey::RightSuper:
				{
					Result |= static_cast<unsigned>(EModifier::Super);
					break;
				}
			default:
				{
					break;
				}
			}
		}
		return Result;
	}
	bool FInputStore::IsInput(EEventType Type) noexcept
	{
		return Type == EEventType::KeyDown || Type == EEventType::KeyUp || Type == EEventType::TextInput ||
		    Type == EEventType::TextEditing || Type == EEventType::PointerMove || Type == EEventType::PointerDown ||
		    Type == EEventType::PointerUp || Type == EEventType::PointerCancelled || Type == EEventType::Wheel ||
		    (Type >= EEventType::ControllerAxis && Type <= EEventType::ControllerHat);
	}
}
