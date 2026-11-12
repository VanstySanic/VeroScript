// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run using VeroScriptVisualExample, which registers the public script adapters.
int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("Input devices", 800, 500, true);
	VS::Platform::InputDevices devices = platform.GetInputDevices();
	for (int64 i = 0; i < devices.Count; ++i)
	{
		VS::Platform::InputDevice device = devices.Get(i);
		print(device.Name);
		if (device.Aggregate)
		{
			print("System aggregate input");
		}
	}
	bool running = true;
	while (running)
	{
		VS::Platform::Event input = platform.WaitEvent(16);
		if (input.Type == VS::Platform::EventType::Quit || (input.WindowId == window.GetId() && input.Type == VS::Platform::EventType::CloseRequested))
		{
			running = false;
		}
		if (input.Type == VS::Platform::EventType::InputDeviceAdded || input.Type == VS::Platform::EventType::InputDeviceRemoved)
		{
			print(input.DeviceName);
			devices = platform.GetInputDevices();
		}
		if (input.Type == VS::Platform::EventType::KeyDown)
		{
			VS::Platform::InputState state = platform.GetInputState(input.DeviceKind, input.DeviceId);
			if (state.IsKeyDown(VS::Platform::Key::Escape))
			{
				running = false;
			}
		}
		if (input.Type == VS::Platform::EventType::InputStateReset)
		{
			// A reset also invalidates application-owned pressed/capture state.
			devices = platform.GetInputDevices();
		}
	}
	window.Close();
	platform.CollectRetired();
	return 0;
}
