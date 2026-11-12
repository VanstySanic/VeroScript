// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// This uses real SDL_GPU submission, without any GUI dependency.
int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("VeroScript Graphics", 800, 500, true);
	VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
	VS::Graphics::Device device = graphics.CreateDevice("");
	VS::Graphics::Surface surface = device.CreateSurface(window);
	bool running = true;
	while (running)
	{
		VS::Platform::Event input = platform.WaitEvent(16);
		if (input.Type == VS::Platform::EventType::Quit || (input.WindowId == window.GetId() && input.Type == VS::Platform::EventType::CloseRequested))
		{
			running = false;
		}
		if (running)
		{
			device.Present(surface, 0.12, 0.20, 0.32, 1.0);
		}
	}
	device.WaitIdle();
	surface.Close();
	window.Close();
	device.Close();
	platform.CollectRetired();
	return 0;
}
