// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptPlatformBindings.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include <iostream>
#include <sstream>

namespace
{
	using namespace VeroScript;
	using namespace Platform;
	class FWindow final : public IWindow
	{
	public:
		//~ Begin IWindow Interface
		std::uint64_t GetId() const noexcept override
		{
			return 17;
		}
		bool IsOpen() const noexcept override
		{
			return bOpen;
		}
		bool GetMetrics(FWindowMetrics& Out, std::string&) const override
		{
			Out = {640, 480, 640, 480, 1, 1, 1};
			return true;
		}
		bool SetTitle(const std::string&, std::string&) override
		{
			return true;
		}
		bool SetSize(std::int64_t, std::int64_t, std::string&) override
		{
			return true;
		}
		bool SetVisible(bool, std::string&) override
		{
			return true;
		}
		bool SetTextInputEnabled(bool, std::string&) override
		{
			return true;
		}
		bool SetTextInputArea(double, double, double, double, std::int64_t, std::string&) override
		{
			return true;
		}
		bool Close(std::string&) override
		{
			bOpen = false;
			return true;
		}

		//~ End IWindow Interface

	private:
		bool bOpen = true;
	};
	class FService : public IService
	{
	public:
		//~ Begin IService Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			return {};
		}
		std::shared_ptr<IWindow> CreateWindow(const FWindowOptions&, std::string&) override
		{
			return std::make_shared<FWindow>();
		}
		bool PollEvent(FEvent& Out, bool& Available, std::string&) override
		{
			Out = {};
			Available = false;
			return true;
		}
		bool WaitEvent(std::int64_t, FEvent& Out, bool& Available, std::string& E) override
		{
			return PollEvent(Out, Available, E);
		}
		bool SubmitEvent(const FEvent&, std::string&) override
		{
			return true;
		}
		bool SetClipboardText(const std::string&, std::string&) override
		{
			return true;
		}
		bool GetClipboardText(std::string& Out, std::string&) override
		{
			Out.clear();
			return true;
		}
		bool CollectRetired(std::string&) override
		{
			return true;
		}
		//~ End IService Interface
	};
	class FInputService final : public FService
	{
	public:
		//~ Begin IService Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			FCapabilities Caps{true, false, false, false, false, true, true, true};
			Caps.bControllers = Caps.bRumble = true;
			return Caps;
		}
		bool GetInputDevices(std::vector<FInputDevice>& Out, std::string&) override
		{
			FInputDevice Pad{EInputDeviceKind::Gamepad, 7, "memory gamepad", false};
			Pad.AxisCount = 6;
			Pad.ButtonCount = 26;
			Pad.bRumble = true;
			Out = {Device, Pad};
			return true;
		}
		bool GetInputState(EInputDeviceKind Kind, std::uint64_t Id, FInputState& Out, std::string& E) override
		{
			if (Kind == EInputDeviceKind::Gamepad && Id == 7)
			{
				Out = {};
				Out.Device = {Kind, Id, "memory gamepad", false};
				Out.Device.AxisCount = 6;
				Out.Device.ButtonCount = 26;
				Out.Device.bRumble = true;
				Out.Axes.resize(6);
				Out.ControllerButtons.resize(26);
				if (!bCleared)
				{
					Out.Axes[0] = -0.5;
					Out.ControllerButtons[0] = 1;
				}
				return true;
			}
			if (Kind != Device.Kind || Id != Device.DeviceId)
			{
				E = "test.unknown_device";
				return false;
			}
			Out = {};
			Out.Device = Device;
			if (!bCleared)
			{
				Out.Keys.push_back({EKey::A, 4, 17});
			}
			return true;
		}
		bool Rumble(EInputDeviceKind Kind, std::uint64_t Id, double Low, double High, std::int64_t Milliseconds,
					std::string& E) override
		{
			return Kind == EInputDeviceKind::Gamepad && Id == 7 && Low == 0.5 && High == 1 && Milliseconds == 100 &&
				   ValidateRumble(Low, High, Milliseconds, E);
		}
		bool ClearInputState(std::uint64_t, std::string&) override
		{
			bCleared = true;
			return true;
		}

		//~ End IService Interface

	private:
		FInputDevice Device{EInputDeviceKind::Keyboard, std::uint64_t{1} << 40, "memory keyboard", false};
		bool bCleared = false;
	};
}

int main()
{
	// A provider can omit optional input methods without depending on SDL or implementing stubs.
	FService Unsupported;
	std::string Error;
	std::vector<FInputDevice> Devices{{EInputDeviceKind::Mouse, 12, "preserved"}};
	FInputState Snapshot;
	Snapshot.Device.Name = "preserved";
	if (Unsupported.GetInputDevices(Devices, Error) || Devices[0].Name != "preserved" ||
		Unsupported.GetInputState(EInputDeviceKind::Mouse, 12, Snapshot, Error) ||
		Snapshot.Device.Name != "preserved" || Unsupported.ClearInputState(0, Error) ||
		Error.find("platform.unsupported_input") != 0 ||
		Unsupported.Rumble(EInputDeviceKind::Gamepad, 1, 1, 1, 100, Error) ||
		Error.find("platform.unsupported_rumble") != 0)
	{
		return 1;
	}
	FHostBindingRegistry Registry;
	unsigned Calls = 0;
	if (!RegisterScriptBindings(
			Registry,
			[&Calls](const std::string& Backend, EEventPumpMode Mode, std::string& E) -> std::shared_ptr<IService>
			{
				if (Backend != "memory" || Mode != EEventPumpMode::External)
				{
					E = "test.invalid_factory_arguments";
					return {};
				}
				++Calls;
				return std::make_shared<FInputService>();
			},
			Error))
	{
		std::cerr << Error;
		return 2;
	}
	const char* Source = R"vs(
int main()
{
 VS::Platform::Service service = VS::Platform::Service::CreateExternal("memory");
 VS::Platform::Window window = service.CreateWindow("memory",640,480,false);
 assert(window.IsOpen() && window.GetWidth() == 640 && window.GetId() == 17u64);
 assert(service.SupportsInputDevices() && service.SupportsInputState() && service.SupportsDeviceEvents());
 VS::Platform::InputDevices devices = service.GetInputDevices();
 assert(devices.Count == 2);
 assert(service.SupportsControllers() && service.SupportsRumble());
 VS::Platform::InputDevice pad = devices.Get(1);
 assert(pad.Kind == VS::Platform::InputDeviceKind::Gamepad && pad.AxisCount == 6 && pad.ButtonCount == 26 && pad.SupportsRumble);
 VS::Platform::InputState controls = service.GetInputState(pad.Kind, pad.DeviceId);
 assert(controls.GetGamepadAxis(VS::Platform::GamepadAxis::LeftX) == -0.5 && controls.GetAxis(0) == -0.5);
 assert(controls.IsGamepadButtonDown(VS::Platform::GamepadButton::South) && controls.IsButtonDown(0));
 service.Rumble(pad.Kind, pad.DeviceId, 0.5, 1.0, 100);
 VS::Platform::InputDevice device = devices.Get(0);
 assert(device.Kind == VS::Platform::InputDeviceKind::Keyboard && device.DeviceId == 1099511627776u64);
 assert(device.Name == "memory keyboard" && device.Connected && !device.Aggregate);
 VS::Platform::InputState state = service.GetInputState(device.Kind,device.DeviceId);
 assert(state.KeyCount == 1 && state.IsKeyDown(VS::Platform::Key::A));
 auto query = [service, window, devices, state, controls]() -> bool
 {
  return service.SupportsInputDevices() && service.GetInputDevices().Get(0).Connected &&
         window.IsOpen() && window.GetId() == 17u64 && window.GetWidth() == 640 &&
         devices.Get(0).Name == "memory keyboard" && state.IsKeyDown(VS::Platform::Key::A) &&
         state.IsScanCodeDown(4) && state.GetKey(0).WindowId == 17u64 &&
         controls.GetAxis(0) == -0.5 && controls.IsGamepadButtonDown(VS::Platform::GamepadButton::South);
 };
 assert(query());
 service.ClearInputState(0u64);
 VS::Platform::InputState cleared = service.GetInputState(device.Kind,device.DeviceId);
 assert(cleared.KeyCount == 0 && state.KeyCount == 1);
 window.Close();
 assert(!window.IsOpen());
 return 0;
}
)vs";
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.Bindings = &Registry;
	auto Result = VeroScriptCompile::CompileSource(Source, "<independent-platform-backend>", Options);
	if (!Result.bSuccess)
	{
		std::cerr << Result.Error.Message;
		return 3;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetMemoryHostBindings(&Registry);
	if (!Vm.Execute(*Result.Module, ERunMode::Script, {}, &Registry.GetNativeFunctions()) || Calls != 1)
	{
		std::cerr << Vm.GetError().Message;
		return 4;
	}
	if (VeroScriptCompile::CompileSource(
			"int main(){auto reset=[service=VS::Platform::Service::CreateExternal(\"memory\")]() -> void "
			"{service.ClearInputState(0u64);};return 0;}", "<read-only-platform-capture>", Options).bSuccess)
	{
		std::cerr << "A read-only capture unexpectedly allowed input state mutation\n";
		return 5;
	}
	std::cout << "Backend-independent Platform bindings passed\n";
	return 0;
}
