// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main()
{
	VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
	VS::Platform::Window window = platform.CreateWindow("VeroScript composition", 640, 420, true);
	VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
	VS::Graphics::Device device = graphics.CreateDevice("");
	VS::Graphics::Surface surface = device.CreateSurface(window);
	VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
	VS::Graphics::Compositor2D compositor = device.CreateCompositor2D();
	array<double> vertices = {40.0, 40.0, 0.0, 0.0, 1.0, 0.2, 0.1, 1.0, 320.0, 40.0, 1.0, 0.0, 0.1, 1.0, 0.2, 1.0, 180.0, 300.0, 0.5, 1.0, 0.2, 0.1, 1.0, 1.0};
	array<int64> indices = {0, 1, 2};
	VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices, indices);
	VS::Graphics::DrawList2D drawing = VS::Graphics::DrawList2D::Create();
	drawing.AddGeometry(geometry);
	VS::Graphics::DrawList2D mask = VS::Graphics::DrawList2D::Create();
	mask.PushClip(64, 48, 240, 240);
	mask.AddGeometry(geometry);
	VS::Graphics::Composition2D frame = VS::Graphics::Composition2D::Create();
	frame.BeginLayer(0.8);
	frame.PushMask(mask, false);
	frame.AddDraw(drawing);
	frame.PopMask();
	frame.AddShadow(6.0, 8.0, 8.0, 0.0, 0.0, 0.0, 0.75);
	frame.EndLayer();
	bool running = true;
	while (running)
	{
		VS::Platform::Event input = platform.WaitEvent(16);
		if (input.Type == VS::Platform::EventType::Quit || (input.WindowId == window.GetId() && input.Type == VS::Platform::EventType::CloseRequested))
		{
			running = false;
		}
		int64 width = window.GetPixelWidth();
		int64 height = window.GetPixelHeight();
		if (running && width > 0 && height > 0)
		{
			compositor.RenderSurface(surface, width, height, frame, true, 0.08, 0.10, 0.16, 1.0);
		}
	}
	device.WaitIdle();
	frame.Clear();
	drawing.Clear();
	mask.Clear();
	compositor.Close();
	geometry.Close();
	renderer.Close();
	surface.Close();
	window.Close();
	device.Close();
	platform.CollectRetired();
	return 0;
}
