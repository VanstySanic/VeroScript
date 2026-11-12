// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run with VeroScriptVisualExample. Press a gamepad button to request a short rumble.
int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("Controllers", 800, 500, true);
	bool running = true;
	while (running)
	{
		VS::Platform::Event input = platform.WaitEvent(16);
		if (input.Type == VS::Platform::EventType::Quit || input.Type == VS::Platform::EventType::CloseRequested || (input.Type == VS::Platform::EventType::KeyDown && input.Key == VS::Platform::Key::Escape))
		{
			running = false;
		}
		if (input.Type == VS::Platform::EventType::InputDeviceAdded || input.Type == VS::Platform::EventType::InputDeviceRemoved)
		{
			print(input.DeviceName);
		}
		if (input.Type == VS::Platform::EventType::ControllerButtonDown && !input.Synthetic)
		{
			VS::Platform::InputState state = platform.GetInputState(input.DeviceKind, input.DeviceId);
			if (state.Device.Kind == VS::Platform::InputDeviceKind::Gamepad)
			{
				print(state.GetGamepadAxis(VS::Platform::GamepadAxis::LeftX));
				print(state.IsGamepadButtonDown(VS::Platform::GamepadButton::South));
			}
			if (platform.SupportsRumble() && state.Device.SupportsRumble)
			{
				platform.Rumble(input.DeviceKind, input.DeviceId, 0.3, 0.7, 150);
			}
		}
	}
	platform.ClearInputState(0u64);
	window.Close();
	platform.CollectRetired();
	return 0;
}
