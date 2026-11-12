// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run using VeroScriptVisualExample, which installs the public module adapters.
int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("VeroScript Platform", 800, 500, true);
	window.SetTextInputEnabled(true);
	bool running = true;
	while (running)
	{
		VS::Platform::Event input = platform.WaitEvent(16);
		if (input.Type == VS::Platform::EventType::Quit || (input.WindowId == window.GetId() && input.Type == VS::Platform::EventType::CloseRequested))
		{
			running = false;
		}
		if (input.Type == VS::Platform::EventType::TextInput)
		{
			print(input.Text);
		}
	}
	window.Close();
	platform.CollectRetired();
	return 0;
}
