// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformApi.h"
#include <cmath>
#include <limits>

namespace VeroScript::Platform
{
	bool ValidateWindowOptions(const FWindowOptions& Options, std::string& Error)
	{
		if (Options.Width <= 0 || Options.Height <= 0 || Options.Width > std::numeric_limits<std::int32_t>::max() ||
			Options.Height > std::numeric_limits<std::int32_t>::max())
		{
			Error = "platform.invalid_size: dimensions must be positive and fit int32";
			return false;
		}
		if (Options.Title.find('\0') != std::string::npos || Options.GraphicsApi.find('\0') != std::string::npos)
		{
			Error = "platform.invalid_text: title contains NUL";
			return false;
		}
		Error.clear();
		return true;
	}

	bool ValidateEvent(const FEvent& Event, std::string& Error)
	{
		const bool bDeviceEvent =
			Event.Type == EEventType::InputDeviceAdded || Event.Type == EEventType::InputDeviceRemoved;
		const bool bKeyboard = Event.Type == EEventType::KeyDown || Event.Type == EEventType::KeyUp ||
							   Event.Type == EEventType::TextInput || Event.Type == EEventType::TextEditing;
		const bool bPointer = Event.Type == EEventType::PointerMove || Event.Type == EEventType::PointerDown ||
							  Event.Type == EEventType::PointerUp || Event.Type == EEventType::PointerCancelled ||
							  Event.Type == EEventType::Wheel;
		const auto Kind = GetInputDeviceKind(Event);
		const bool bController = Event.Type >= EEventType::ControllerAxis && Event.Type <= EEventType::ControllerHat;
		if ((bController &&
			 (!IsController(Kind) || Event.WindowId || !Event.DeviceId || Event.bAggregateDevice || Event.Control < 0 ||
			  Event.Control >= 256 || !std::isfinite(Event.Value) || Event.Value < -1 ||
			  (Event.Type == EEventType::ControllerAxis &&
			   (Event.Value > 1 || (Kind == EInputDeviceKind::Gamepad &&
									(Event.Control >= 6 || (Event.Control >= 4 && Event.Value < 0))))) ||
			  (Event.Type == EEventType::ControllerHat &&
			   (Kind != EInputDeviceKind::Joystick ||
				(Event.Value != 0 && Event.Value != 1 && Event.Value != 2 && Event.Value != 3 && Event.Value != 4 &&
				 Event.Value != 6 && Event.Value != 8 && Event.Value != 9 && Event.Value != 12))) ||
			  ((Event.Type == EEventType::ControllerButtonDown || Event.Type == EEventType::ControllerButtonUp) &&
			   Kind == EInputDeviceKind::Gamepad && Event.Control >= 26))) ||
			(IsController(Kind) && bDeviceEvent && (!Event.DeviceId || Event.bAggregateDevice)) ||
			(!IsController(Kind) && (Event.AxisCount || Event.ButtonCount || Event.HatCount || Event.bRumble)) ||
			Event.AxisCount < 0 || Event.AxisCount > 256 || Event.ButtonCount < 0 || Event.ButtonCount > 256 ||
			Event.HatCount < 0 || Event.HatCount > 256 ||
			(Kind == EInputDeviceKind::Gamepad && (Event.AxisCount > 6 || Event.ButtonCount > 26 || Event.HatCount)))
		{
			Error = "platform.invalid_controller: invalid identity, control, value or layout";
			return false;
		}
		if (Event.Type <= EEventType::None || Event.Type > EEventType::MouseLeft ||
			((Event.Type == EEventType::MouseEntered || Event.Type == EEventType::MouseLeft) &&
			 (!Event.WindowId || Event.DeviceId || Event.PointerId || Kind != EInputDeviceKind::Mouse ||
			  Event.PointerKind != EPointerKind::Mouse)) ||
			Event.DeviceKind > EInputDeviceKind::Gamepad ||
			(Event.Type == EEventType::InputStateReset &&
			 (Event.DeviceKind != EInputDeviceKind::Unknown || Event.DeviceId)) ||
			(bDeviceEvent && (Kind == EInputDeviceKind::Unknown || Event.WindowId)) ||
			(bKeyboard && Kind != EInputDeviceKind::Keyboard) ||
			(bPointer &&
			 Kind != (Event.PointerKind == EPointerKind::Touch ? EInputDeviceKind::Touch : EInputDeviceKind::Mouse)) ||
			((Event.Type == EEventType::PointerDown || Event.Type == EEventType::PointerUp) &&
			 Kind == EInputDeviceKind::Mouse && (Event.Button < 1 || Event.Button > 32)) ||
			Event.ScanCode < 0 || Event.ScanCode > 65535 || Event.DeviceName.find('\0') != std::string::npos ||
			(Event.PointerKind != EPointerKind::Mouse && Event.PointerKind != EPointerKind::Touch) ||
			!std::isfinite(Event.X) || !std::isfinite(Event.Y) || !std::isfinite(Event.DeltaX) ||
			!std::isfinite(Event.DeltaY) || !std::isfinite(Event.Pressure) || Event.Pressure < 0 ||
			Event.Pressure > 1 || Event.SelectionStart < -1 || Event.SelectionLength < -1 ||
			(!Event.WindowId && Event.Type != EEventType::Quit && Event.Type != EEventType::Background &&
			 Event.Type != EEventType::Foreground && !bDeviceEvent && !bController &&
			 Event.Type != EEventType::InputStateReset))
		{
			Error = "platform.invalid_event: invalid type, coordinates, pressure, or selection";
			return false;
		}
		Error.clear();
		return true;
	}
	EInputDeviceKind GetInputDeviceKind(const FEvent& Event) noexcept
	{
		if (Event.DeviceKind != EInputDeviceKind::Unknown)
		{
			return Event.DeviceKind;
		}
		switch (Event.Type)
		{
			case EEventType::MouseEntered:
			case EEventType::MouseLeft:
				return EInputDeviceKind::Mouse;
			case EEventType::KeyDown:
			case EEventType::KeyUp:
			case EEventType::TextInput:
			case EEventType::TextEditing:
				return EInputDeviceKind::Keyboard;
			case EEventType::PointerMove:
			case EEventType::PointerDown:
			case EEventType::PointerUp:
			case EEventType::PointerCancelled:
			case EEventType::Wheel:
				return Event.PointerKind == EPointerKind::Touch ? EInputDeviceKind::Touch : EInputDeviceKind::Mouse;
			default:
				return EInputDeviceKind::Unknown;
		}
	}

	bool IsController(EInputDeviceKind Kind) noexcept
	{
		return Kind == EInputDeviceKind::Joystick || Kind == EInputDeviceKind::Gamepad;
	}
	bool ValidateRumble(double Low, double High, std::int64_t Milliseconds, std::string& Error)
	{
		if (!std::isfinite(Low) || !std::isfinite(High) || Low < 0 || Low > 1 || High < 0 || High > 1 ||
			Milliseconds < 0 || Milliseconds > MaxRumbleMilliseconds)
		{
			Error = "platform.invalid_rumble: amplitudes must be finite 0..1 and duration must be 0..65535 ms";
			return false;
		}
		Error.clear();
		return true;
	}
	bool ValidateWait(std::int64_t Milliseconds, std::string& Error)
	{
		if (Milliseconds < 0 || Milliseconds > MaxWaitMilliseconds)
		{
			Error = "platform.invalid_wait: timeout must be in 0..50 milliseconds";
			return false;
		}
		Error.clear();
		return true;
	}
	bool IService::GetInputDevices(std::vector<FInputDevice>&, std::string& Error)
	{
		Error = "platform.unsupported_input: device enumeration is unavailable";
		return false;
	}
	bool IService::GetInputState(EInputDeviceKind, std::uint64_t, FInputState&, std::string& Error)
	{
		Error = "platform.unsupported_input: device state is unavailable";
		return false;
	}
	bool IService::ClearInputState(std::uint64_t, std::string& Error)
	{
		Error = "platform.unsupported_input: state reset is unavailable";
		return false;
	}
	bool IWindow::IsBorrowed() const noexcept
	{
		return false;
	}
	bool IWindow::SetCursor(ECursor, bool, std::string& Error)
	{
		Error = "platform.unsupported_cursor: system cursor control is unavailable";
		return false;
	}
	bool IWindow::SetMouseCapture(bool, std::string& Error)
	{
		Error = "platform.unsupported_capture: native mouse capture is unavailable";
		return false;
	}
	bool IWindow::HasMouseCapture() const noexcept
	{
		return false;
	}
	std::shared_ptr<IWindow> IService::AttachWindow(const FBorrowedWindowOptions&, std::string& Error)
	{
		Error = "platform.unsupported_window: borrowed native windows are unavailable";
		return {};
	}
	bool IService::HasPendingRetirements(bool& bPending, std::string& Error)
	{
		bPending = false;
		Error.clear();
		return true;
	}

	bool IService::Rumble(EInputDeviceKind, std::uint64_t, double, double, std::int64_t, std::string& Error)
	{
		Error = "platform.unsupported_rumble: controller feedback is unavailable";
		return false;
	}

}
