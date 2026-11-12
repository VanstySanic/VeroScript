// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformApi.h"
#include <iostream>

int main()
{
	using namespace VeroScript::Platform;
	FEvent Event;
	Event.Type = EEventType::InputDeviceAdded;
	Event.DeviceKind = EInputDeviceKind::Keyboard;
	Event.DeviceId = std::uint64_t{1} << 40;
	Event.DeviceName = "independent backend";
	std::string Error;
	if (!ValidateEvent(Event, Error) || GetInputDeviceKind(Event) != EInputDeviceKind::Keyboard)
	{
		std::cerr << Error;
		return 1;
	}
	Event.Type = EEventType::InputStateReset;
	Event.DeviceKind = EInputDeviceKind::Unknown;
	Event.DeviceId = 0;
	if (!ValidateEvent(Event, Error) || !ValidateWait(0, Error))
	{
		return 2;
	}
	std::cout << "Platform SDK works without SDL or Core\n";
	return 0;
}
