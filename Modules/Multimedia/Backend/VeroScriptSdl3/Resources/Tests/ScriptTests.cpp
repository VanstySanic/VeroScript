// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptPlatformBindings.h"
#include "VeroScriptGraphicsBindings.h"
#include "VeroScriptSdl3.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>

int main(int Count, char** Args)
{
	using namespace VeroScript;
	const bool bGpu = Count > 1 && std::string(Args[1]) == "gpu";
	std::string Error;
	if (bGpu)
	{
		auto Service = Sdl3::CreateGraphics({}, Error);
		if (!Service)
		{
			std::cerr << Error << '\n';
			return 1;
		}
		auto Device = Service->CreateDevice({}, Error);
		if (!Device)
		{
			std::cout << "GPU unavailable: " << Error << '\n';
			return 77;
		}
	}
	std::shared_ptr<Platform::IWindow> InputWindow;
	FHostBindingRegistry Registry;
	const Platform::FServiceFactory PlatformFactory =
	    [&InputWindow](const std::string& Backend, Platform::EEventPumpMode PumpMode, std::string& E)
	{
		if (Backend == "input-test")
		{
			Sdl3::FPlatformOptions External;
			External.PumpMode = Platform::EEventPumpMode::External;
			auto Service = Sdl3::CreatePlatform(External, E);
			if (!Service)
			{
				return Service;
			}
			Platform::FWindowOptions WindowOptions;
			WindowOptions.bVisible = false;
			InputWindow = Service->CreateWindow(WindowOptions, E);
			if (!InputWindow)
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Platform::FEvent Event;
			Event.Type = Platform::EEventType::KeyDown;
			Event.WindowId = InputWindow->GetId();
			Event.DeviceId = 9;
			Event.Key = Platform::EKey::A;
			Event.ScanCode = 4;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Event.Key = Platform::EKey::LeftShift;
			Event.ScanCode = 225;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Event = {};
			Event.Type = Platform::EEventType::PointerDown;
			Event.WindowId = InputWindow->GetId();
			Event.DeviceId = 9;
			Event.PointerId = 100;
			Event.PointerKind = Platform::EPointerKind::Touch;
			Event.X = 12;
			Event.Y = 24;
			Event.Pressure = 0.5;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Event = {};
			Event.Type = Platform::EEventType::InputDeviceAdded;
			Event.DeviceKind = Platform::EInputDeviceKind::Gamepad;
			Event.DeviceId = 9;
			Event.AxisCount = 6;
			Event.ButtonCount = 26;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Event.Type = Platform::EEventType::ControllerAxis;
			Event.Value = 0.5;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			Event.Type = Platform::EEventType::ControllerButtonDown;
			Event.Value = 0;
			if (!Service->SubmitEvent(Event, E))
			{
				return std::shared_ptr<Platform::IService>{};
			}
			bool Available = false;
			do
			{
				if (!Service->PollEvent(Event, Available, E))
				{
					return std::shared_ptr<Platform::IService>{};
				}
			}
			while (Available);
			return Service;
		}
		if (Backend != "sdl3")
		{
			E = "unknown platform backend";
			return std::shared_ptr<Platform::IService>{};
		}
		Sdl3::FPlatformOptions Options;
		Options.PumpMode = PumpMode;
		return Sdl3::CreatePlatform(Options, E);
	};
	const Graphics::FServiceFactory GraphicsFactory = [](const std::string& Backend, std::string& E)
	{
		if (Backend != "sdl3")
		{
			E = "unknown graphics backend";
			return std::shared_ptr<Graphics::IService>{};
		}
		return Sdl3::CreateGraphics({}, E);
	};
	if (!Platform::RegisterScriptBindings(Registry, PlatformFactory, Error) ||
	    !Graphics::RegisterScriptBindings(Registry, GraphicsFactory, Error))
	{
		std::cerr << Error << '\n';
		return 2;
	}
	const auto TypeCount = Registry.GetTypes().size();
	if (Platform::RegisterScriptBindings(Registry, PlatformFactory, Error) || Registry.GetTypes().size() != TypeCount)
	{
		return 3;
	}
	const char* PlatformScript = R"vs(
int main()
{
    VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
    VS::Platform::Window window = platform.CreateWindowForGraphics("VeroScript", 96, 64, false, "");
    platform.CollectRetired();
    assert(!platform.HasPendingRetirements());
    assert(window.IsOpen());
    assert(!window.IsBorrowed() && !window.HasMouseCapture());
    assert(platform.SupportsBorrowedWindows());
    if (platform.SupportsMouseCapture()) { window.SetMouseCapture(false); }
    if (platform.SupportsSystemCursor()) { window.SetCursor(VS::Platform::Cursor::Text, true); }
    int64 width = window.GetWidth();
    assert(width == 96);
    assert(window.GetDisplayScale() > 0.0);
    window.SetTitle("Script window");
    window.SetSize(128, 96);
    window.SetTextInputArea(0.0, 0.0, 30.0, 20.0, 0);
    VS::Platform::Event input = platform.PollEvent();
    if (input.Type == VS::Platform::EventType::None) { assert(input.Text == ""); }
    assert(platform.SupportsInputDevices() && platform.SupportsInputState() && platform.SupportsDeviceEvents());
    VS::Platform::InputDevices initial = platform.GetInputDevices();
    assert(initial.Count >= 2);
    VS::Platform::InputDevice aggregate = initial.Get(0);
    assert(aggregate.Kind == VS::Platform::InputDeviceKind::Keyboard && aggregate.Aggregate && aggregate.Connected);
    VS::Platform::InputState empty = platform.GetInputState(aggregate.Kind, aggregate.DeviceId);
    assert(empty.KeyCount == 0 && empty.PointerCount == 0);
    VS::Platform::Service host = VS::Platform::Service::CreateExternal("input-test");
    VS::Platform::InputDevices devices = host.GetInputDevices();
    assert(devices.Count == 5);
    assert(host.SupportsControllers() && !host.SupportsRumble());
    VS::Platform::InputState controls = host.GetInputState(VS::Platform::InputDeviceKind::Gamepad, 9u64);
    assert(controls.AxisCount == 6 && controls.ButtonCount == 26 && controls.HatCount == 0);
    assert(controls.GetGamepadAxis(VS::Platform::GamepadAxis::LeftX) == 0.5 && controls.IsGamepadButtonDown(VS::Platform::GamepadButton::South));
    VS::Platform::InputState held = host.GetInputState(VS::Platform::InputDeviceKind::Keyboard, 9u64);
    assert(held.KeyCount == 2 && held.IsKeyDown(VS::Platform::Key::A) && held.IsScanCodeDown(4));
    assert(!held.IsScanCodeDown(0) && held.Modifiers == 1u);
    VS::Platform::PressedKey key = held.GetKey(0);
    assert(key.Key == VS::Platform::Key::A && key.ScanCode == 4 && key.WindowId != 0u64);
    VS::Platform::InputDevice keyboard = held.Device;
    assert(keyboard.DeviceId == 9u64 && keyboard.Name == "Keyboard" && !keyboard.Aggregate && keyboard.Connected);
    VS::Platform::InputState fingers = host.GetInputState(VS::Platform::InputDeviceKind::Touch, 9u64);
    assert(fingers.PointerCount == 1 && fingers.KeyCount == 0);
    VS::Platform::PointerState finger = fingers.GetPointer(0);
    assert(finger.PointerId == 100u64 && finger.WindowId == key.WindowId && finger.X == 12.0 &&
           finger.Y == 24.0 && finger.Pressure == 0.5 && finger.Buttons == 1u);
    host.ClearInputState(0u64);
    VS::Platform::InputState cleared = host.GetInputState(VS::Platform::InputDeviceKind::Keyboard, 9u64);
    assert(cleared.KeyCount == 0 && held.KeyCount == 2 && held.IsKeyDown(VS::Platform::Key::A));
    VS::Platform::Event released = host.PollEvent();
    assert(released.Type == VS::Platform::EventType::KeyUp && released.Synthetic && !released.AggregateDevice &&
           released.DeviceKind == VS::Platform::InputDeviceKind::Keyboard && released.DeviceId == 9u64);
    VS::Platform::Event shift = host.PollEvent();
    VS::Platform::Event cancelled = host.PollEvent();
    assert(cancelled.Type == VS::Platform::EventType::PointerCancelled && cancelled.Synthetic);
    VS::Platform::Event axis = host.PollEvent();
    VS::Platform::Event button = host.PollEvent();
    assert(axis.Type == VS::Platform::EventType::ControllerAxis && axis.Control == 0 && axis.Value == 0.0 && axis.Synthetic);
    assert(button.Type == VS::Platform::EventType::ControllerButtonUp && button.Control == 0 && button.Synthetic);
    assert(!host.GetInputState(VS::Platform::InputDeviceKind::Gamepad, 9u64).IsGamepadButtonDown(VS::Platform::GamepadButton::South));
    VS::Platform::Event reset = host.PollEvent();
    assert(reset.Type == VS::Platform::EventType::InputStateReset && reset.DeviceName == "");
    window.Close();
    assert(!window.IsOpen());
    platform.CollectRetired();
    return 0;
}
)vs";
	const char* GpuScript = R"vs(
int main()
{
    VS::Platform::Service platform = VS::Platform::Service::Create("sdl3");
    VS::Platform::Window window = platform.CreateWindow("VeroScript GPU", 96, 64, true);
    VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
    VS::Graphics::DeviceRequest creation = graphics.CreateDeviceAsync("");
    assert(creation.Poll() == VS::Graphics::RequestStatus::Succeeded);
    VS::Graphics::Device device = creation.GetDevice();
    device.Poll();
    VS::Graphics::Texture texture = device.CreateTexture(17, 9);
    assert(texture.GetWidth() == 17);
    device.ClearTexture(texture, 1.0, 0.25, 0.0, 1.0);
    VS::Graphics::Surface surface = device.CreateSurface(window);
    bool presented = device.Present(surface, 0.1, 0.2, 0.4, 1.0);
    VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
    array<double> vertices = {0.0,0.0,0.0,0.0,1.0,1.0,1.0,1.0,
                             8.0,0.0,1.0,0.0,1.0,1.0,1.0,1.0,
                             0.0,8.0,0.0,1.0,1.0,1.0,1.0,1.0};
    array<int64> indices = {0,1,2};
    VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
    assert(geometry.GetVertexCount() == 3 && geometry.GetIndexCount() == 3);
    VS::Graphics::Texture image = device.CreateTextureWithFormat(1,1,VS::Graphics::TextureFormat::Rgba8Srgb);
    assert(image.GetFormat() == VS::Graphics::TextureFormat::Rgba8Srgb);
    array<uint8> pixels = {255,128,64,255};
    device.UpdateTexture(image,0,0,1,1,pixels);
    VS::Graphics::ReadbackRequest readback = device.ReadTextureAsync(image);
    assert(readback.Poll() == VS::Graphics::RequestStatus::Succeeded);
    array<uint8> copied = readback.GetPixels();
    assert(len(copied) == 4 && copied[0] == 255u && copied[1] == 128u && copied[3] == 255u);
    auto copiedSize = [readback]() -> int64 { return len(readback.GetPixels()); };
    assert(copiedSize() == 4);
    readback.Cancel();
    assert(readback.Poll() == VS::Graphics::RequestStatus::Cancelled);
    VS::Graphics::Sampler sampler = device.CreateSampler(VS::Graphics::Filter::Nearest,VS::Graphics::AddressMode::Clamp,VS::Graphics::AddressMode::Clamp);
    VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
    array<double> matrix = {1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 1.0,2.0,0.0,1.0};
    list.SetTransform(matrix);
    list.SetTexture(image,sampler);
    list.SetTint(1.0,1.0,1.0,0.5);
    list.SetBlendMode(VS::Graphics::BlendMode::StraightAlpha);
    list.SetVertexColorMode(VS::Graphics::VertexColorMode::Premultiplied);
    list.SetVertexColorMode(VS::Graphics::VertexColorMode::Straight);
    list.PushClip(0,0,16,8);
    list.AddGeometry(geometry);
    list.PopClip();
    assert(device.SupportsGuiBasic() && device.SupportsTransforms());
    assert(device.SupportsClipMasks() && device.SupportsFilters());
    renderer.RenderTexture(texture,list,true,0.0,0.0,0.0,0.0);
    renderer.RenderTextureViewport(texture,list,false,0.0,0.0,0.0,0.0,0,0,8,8);
    renderer.RenderSurface(surface,list,true,0.0,0.0,0.0,1.0);
    array<double> colors = {1.0,0.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0,0.0,
                            0.0,0.0,1.0,0.0,0.0, 0.0,0.0,0.0,1.0,0.0};
    list.SetBlur(0.0,false,false);
    list.SetColorMatrix(colors,false);
    list.SetAlphaMask(image,false,false);
    list.ResetEffect();
    VS::Graphics::Composition2D frame = VS::Graphics::Composition2D::Create();
    VS::Graphics::Compositor2D compositor = device.CreateCompositor2D();
    auto query = [device, texture, image, surface, sampler, renderer, geometry, compositor, creation]() -> bool
    {
        return device.IsOpen() && device.GetDriver() != "" && device.SupportsGuiBasic() &&
               texture.IsOpen() && texture.GetWidth() == 17 && texture.GetHeight() == 9 &&
               image.GetFormat() == VS::Graphics::TextureFormat::Rgba8Srgb && surface.IsOpen() &&
               sampler.IsOpen() && renderer.IsOpen() && geometry.GetVertexCount() == 3 &&
               geometry.GetIndexCount() == 3 && compositor.GetPooledBytes() == 0 && creation.GetDevice().IsOpen();
    };
    assert(query());
    frame.BeginLayer(0.5);
    frame.PushMask(list,false);
    frame.AddDraw(list);
    frame.PopMask();
    frame.EndLayer();
    frame.AddBlur(1.0);
    frame.AddColorMatrix(colors);
    frame.AddShadow(1.0,2.0,1.0,0.0,0.0,0.0,0.5);
    frame.AddBackdropBlur(list,1.0);
    compositor.RenderTexture(texture,frame,true,0.0,0.0,0.0,0.0);
    compositor.RenderTextureViewport(texture,frame,false,0.0,0.0,0.0,0.0,0,0,8,8);
    assert(compositor.GetPooledBytes() > 0);
    compositor.RenderSurface(surface,window.GetPixelWidth(),window.GetPixelHeight(),frame,true,0.0,0.0,0.0,1.0);
    frame.Clear();
    compositor.Trim();
    assert(compositor.GetPooledBytes() == 0);
    compositor.Close();
    list.ResetTexture(); list.Clear();
    device.WaitIdle();
    geometry.Close(); sampler.Close(); image.Close(); renderer.Close();
    surface.Close(); texture.Close();
    assert(device.SupportsDeviceRecovery());
    uint64 generation = device.GetGeneration();
    VS::Graphics::DeviceRequest recovery = graphics.RecoverDeviceAsync(device, "");
    assert(recovery.Poll() == VS::Graphics::RequestStatus::Succeeded);
    VS::Graphics::Device replacement = recovery.GetDevice();
    assert(!device.IsOpen() && replacement.IsOpen() && replacement.GetGeneration() != generation);
    device.Close(); replacement.Close(); window.Close();
    return 0;
}
)vs";
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.Bindings = &Registry;
	if (VeroScriptCompile::CompileSource(
	        "int main(){auto change=[draw=VS::Graphics::DrawList2D::Create()]() -> void "
	        "{draw.Clear();};return 0;}",
	        "<read-only-graphics-capture>", Options)
	        .bSuccess)
	{
		std::cerr << "A read-only capture unexpectedly allowed draw list mutation\n";
		return 11;
	}
	auto Compiled = VeroScriptCompile::CompileSource(bGpu ? GpuScript : PlatformScript, "<visual-test>", Options);
	if (!Compiled.bSuccess)
	{
		std::cerr << Compiled.Error.Message << '\n';
		return 4;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetMemoryHostBindings(&Registry);
	if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()))
	{
		const auto& RuntimeError = Vm.GetError();
		std::cerr << RuntimeError.Location.FileLabel << ':' << RuntimeError.Location.Line << ':'
		          << RuntimeError.Location.Column << ": " << RuntimeError.Message << '\n';
		return 5;
	}
	if (Vm.GetProcessExitCode())
	{
		return 6;
	}
	if (!bGpu)
	{
		const char* InvalidIndex = R"vs(
int main()
{
 VS::Platform::Service service = VS::Platform::Service::CreateExternal("sdl3");
 VS::Platform::InputDevices devices = service.GetInputDevices();
 devices.Get(-1);
 return 0;
}
)vs";
		auto BadIndex = VeroScriptCompile::CompileSource(InvalidIndex, "<input-index>", Options);
		std::ostringstream Ignored;
		VeroScriptVm IndexVm(Ignored);
		VeroScript::Test::Configure(IndexVm);
		IndexVm.SetMemoryHostBindings(&Registry);
		if (!BadIndex.bSuccess ||
		    IndexVm.Execute(*BadIndex.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()) ||
		    IndexVm.GetError().Message.find("platform.invalid_index") == std::string::npos)
		{
			std::cerr << "Input index validation failed: " << IndexVm.GetError().Message << '\n';
			return 9;
		}
		const char* MutableSnapshot = R"vs(
int main()
{
 VS::Platform::Service service = VS::Platform::Service::CreateExternal("sdl3");
 VS::Platform::InputDevices devices = service.GetInputDevices();
 devices.Count = 4;
 return 0;
}
)vs";
		if (VeroScriptCompile::CompileSource(MutableSnapshot, "<input-read-only>", Options).bSuccess)
		{
			std::cerr << "Input snapshot unexpectedly writable\n";
			return 10;
		}
	}
	if (InputWindow && !InputWindow->Close(Error))
	{
		return 8;
	}
	InputWindow.reset();
	if (!Sdl3::CollectRetired(Error))
	{
		return 7;
	}
	std::cout << "Script binding integration passed\n";
	return 0;
}
