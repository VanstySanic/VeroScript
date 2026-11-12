// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("VeroScript 2D", 800, 500, true);
	VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
	VS::Graphics::Device device = graphics.CreateDevice("");
	VS::Graphics::Surface surface = device.CreateSurface(window);
	VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
	// Each vertex contains X, Y, U, V and linear R, G, B, A components.
	array<double> vertices = {40.0, 40.0, 0.0, 0.0, 1.0, 0.2, 0.1, 1.0, 300.0, 40.0, 1.0, 0.0, 0.1, 1.0, 0.2, 1.0, 170.0, 260.0, 0.5, 1.0, 0.2, 0.1, 1.0, 1.0};
	array<int64> indices = {0, 1, 2};
	VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices, indices);
	VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
	list.PushClip(20, 20, 320, 300);
	list.AddGeometry(geometry);
	list.PopClip();
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
			renderer.RenderSurface(surface, list, true, 0.08, 0.10, 0.16, 1.0);
		}
	}
	device.WaitIdle();
	list.Clear();
	geometry.Close();
	renderer.Close();
	surface.Close();
	window.Close();
	device.Close();
	return 0;
}
