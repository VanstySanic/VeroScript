// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptPlatformApi.h"
#include "VeroScriptGraphicsComposition2D.h"
#if VS_TEST_AUDIO
#include "VeroScriptAudio.h"
#endif
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
int wmain(int Count, wchar_t** Args)
#else
int main(int Count, char** Args)
#endif
{
	using namespace VeroScript;
	if (Count < 2 || Count > 3)
	{
		return 1;
	}
	std::string Error;
	const bool bGpu = Count == 3 && std::filesystem::path(Args[2]) == "gpu";
	std::shared_ptr<Platform::IWindow> Window;
	std::shared_ptr<Graphics::IDevice> Device;
	std::shared_ptr<Graphics::IDeviceRequest> EscapedCreation;
	std::shared_ptr<Graphics::IReadbackRequest> EscapedReadback;
	#if VS_TEST_AUDIO
	std::shared_ptr<Audio::IStream> AudioStream;
	#endif
	std::weak_ptr<const FLoadedNativeExtension> ModuleLease;
	{
		FNativeExtensionLoadOptions Load;
		Load.ExpectedId = "veroscript-sdl3";
		Load.ExpectedVersion = "0.1.1";
		Load.ExpectedRoles =
			NativeExtensionRole(ENativeExtensionRole::Provider) | NativeExtensionRole(ENativeExtensionRole::Bindings);
		auto Module = LoadNativeExtension(PathUtil::ToUtf8(std::filesystem::absolute(Args[1])), Error, Load);
		if (!Module)
		{
			std::cerr << Error << '\n';
			return 2;
		}
		ModuleLease = Module;
		FNativeExtensionRegistry Registry;
		FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = "visual";
		if (!Registry.Add(Module, Registration, Error))
		{
			std::cerr << Error << '\n';
			return 3;
		}
		const auto Providers = Registry.GetProviders();
		if (Providers.empty() || Providers[0].BackendId != "sdl3")
		{
			return 4;
		}
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "host";
		Description.Scopes = {{"host", {"visual"}}, {"visual", {}}};
		Description.Owners = Registry.GetOwners();
		Description.AvailableDependencies = Registry.GetDependencies();
		auto Environment = FModuleEnvironment::Create(std::move(Description), Error);
		if (!Environment)
		{
			std::cerr << Error << '\n';
			return 5;
		}
		for (const auto& Provider : Providers)
		{
			if (Provider.ServiceId != Platform::ServiceId)
			{
				continue;
			}
			if (Provider.ContractId != Platform::ContractId || Provider.ContractVersion != Platform::ContractVersion)
			{
				return 6;
			}
			FNativeServiceCreateContext Context;
			Context.Environment = Environment;
			auto Instance = CreateNativeService(Provider, Context, Error);
			if (!Instance)
			{
				std::cerr << Error << '\n';
				return 7;
			}
			auto* Service = static_cast<Platform::IService*>(Instance->Get());
			std::vector<Platform::FInputDevice> Inputs;
			Platform::FInputState Input;
			if (!Service->GetCapabilities().bInputDevices || !Service->GetInputDevices(Inputs, Error) ||
				Inputs.empty() || !Service->GetInputState(Inputs[0].Kind, Inputs[0].DeviceId, Input, Error) ||
				!Service->ClearInputState(0, Error))
			{
				std::cerr << Error << '\n';
				return 20;
			}
			if (Service->Rumble(Platform::EInputDeviceKind::Gamepad, 1, -1, 0, 100, Error) ||
				Error.find("platform.invalid_rumble") != 0)
			{
				std::cerr << "Provider did not forward controller feedback: " << Error;
				return 25;
			}
			Platform::FWindowOptions Options;
			Options.bVisible = bGpu;
			Options.Width = 64;
			Options.Height = 64;
			Window = Service->CreateWindow(Options, Error);
			if (!Window)
			{
				std::cerr << Error << '\n';
				return 8;
			}
		}
		#if VS_TEST_AUDIO
		for (const auto& Provider : Providers)
		{
			if (Provider.ServiceId != Audio::ServiceId)
			{
				continue;
			}
			if (Provider.ContractId != Audio::ContractId || Provider.ContractVersion != Audio::ContractVersion)
			{
				return 26;
			}
			FNativeServiceCreateContext Context;
			Context.Environment = Environment;
			auto Instance = CreateNativeService(Provider, Context, Error);
			auto* Service = Instance ? static_cast<Audio::IService*>(Instance->Get()) : nullptr;
			AudioStream = Service ? Service->OpenStream({}, Error) : nullptr;
			if (!AudioStream)
			{
				std::cerr << Error;
				return 27;
			}
		}
		if (!AudioStream)
		{
			return 28;
		}
		#endif
		if (bGpu)
		{
			for (const auto& Provider : Providers)
			{
				if (Provider.ServiceId != Graphics::ServiceId)
				{
					continue;
				}
				if (Provider.ContractId != Graphics::ContractId ||
					Provider.ContractVersion != Graphics::ContractVersion)
				{
					return 12;
				}
				FNativeServiceCreateContext Context;
				Context.Environment = Environment;
				auto Instance = CreateNativeService(Provider, Context, Error);
				if (!Instance)
				{
					std::cerr << Error << '\n';
					return 13;
				}
				auto* Service = static_cast<Graphics::IService*>(Instance->Get());
				EscapedCreation = Service->CreateDeviceAsync({}, Error);
				Device = EscapedCreation ? EscapedCreation->GetDevice(Error) : nullptr;
				if (!Device)
				{
					std::cout << "GPU unavailable: " << Error << '\n';
					return 77;
				}
				const auto Generation = Device->GetGeneration();
				auto Replacement = Service->RecoverDevice(Device, {}, Error);
				if (!Replacement || !Generation || Replacement->GetGeneration() == Generation || Device->IsOpen() ||
					!Device->Close(Error))
				{
					std::cerr << Error;
					return 21;
				}
				Device = std::move(Replacement);
			}
			if (!Device)
			{
				return 14;
			}
		}
	}
	// No registry, provider descriptor, service instance or environment remains outside the child resource.
	if (!Window || ModuleLease.expired())
	{
		return 9;
	}
	Platform::FWindowMetrics Metrics;
	if (bGpu)
	{
		Graphics::FTextureOptions Options;
		Options.Width = 17;
		Options.Height = 9;
		auto Texture = Device->CreateTexture(Options, Error);
		auto Surface = Device->CreateSurface(Window, Error);
		std::vector<std::uint8_t> Pixels;
		bool bPresented = false;
		auto Renderer = Device->CreateRenderer2D(Error);
		auto Sampler = Device->CreateSampler({}, Error);
		auto Geometry = Renderer ? Renderer->CreateGeometry({{0, 0}, {16, 0}, {0, 16}}, {0, 1, 2}, Error) : nullptr;
		Graphics::FDrawList2D List;
		if (!Renderer || !Sampler || !Geometry || !List.AddGeometry(Geometry, Error) ||
			!Renderer->RenderTexture(Texture, List, {}, Error))
		{
			std::cerr << Error;
			return 17;
		}
		if (Device->GetDriver() == "direct3d12" || Device->GetDriver() == "vulkan" || Device->GetDriver() == "metal")
		{
			auto* Submission = dynamic_cast<Graphics::IRenderSubmission2D*>(Renderer.get());
			if (!Submission || !Submission->BeginSubmission(Error) || Submission->BeginSubmission(Error) ||
				!Renderer->RenderTexture(Texture, List, {}, Error) || !Submission->EndSubmission(Error) ||
				!Submission->CancelSubmission(Error))
			{
				std::cerr << "renderer provider lost submission scope: " << Error;
				return 24;
			}
		}
		if (!Texture || !Surface || !Device->ClearTexture(Texture, {1, 0, 0, 1}, Error) ||
			!Device->ReadTexture(Texture, Pixels, Error) || Pixels.size() != 17 * 9 * 4 || Pixels.front() != 255 ||
			!Device->Present(Surface, {}, bPresented, Error))
		{
			std::cerr << Error << '\n';
			return 15;
		}
		EscapedReadback = Device->ReadTextureAsync(Texture, Error);
		if (!EscapedReadback || EscapedReadback->Poll(Error) != Graphics::ERequestStatus::Succeeded)
		{
			std::cerr << Error;
			return 23;
		}
		auto Compositor = Graphics::CreateCompositor2D(Device, {}, Error);
		Graphics::FComposition2D Frame;
		if (!Compositor || !Frame.BeginLayer(0.5f, Error) || !Frame.AddDraw(List, Error) || !Frame.EndLayer(Error) ||
			!Frame.AddBlur(2, Error) || !Compositor->RenderTexture(Texture, Frame, {}, Error) ||
			!Compositor->Close(Error))
		{
			std::cerr << Error;
			return 22;
		}
		Compositor.reset();
		// Native children must also survive release of their provider-created device proxy.
		Device.reset();
		if (!Renderer->RenderSurface(Surface, List, {}, bPresented, Error))
		{
			std::cerr << Error;
			return 18;
		}
		List.Clear();
		if (!Renderer->Close(Error) || !Geometry->Close(Error) || !Sampler->Close(Error))
		{
			std::cerr << Error;
			return 19;
		}
		if (!Surface->Close(Error) || !Texture->Close(Error))
		{
			std::cerr << Error << '\n';
			return 16;
		}
	}
	if (!Window->SetTitle("Escaped native window", Error) || !Window->GetMetrics(Metrics, Error) ||
		Metrics.Width != 64 || !Window->Close(Error))
	{
		std::cerr << Error << '\n';
		return 10;
	}
	Window.reset();
	if (bGpu)
	{
		std::vector<std::uint8_t> Pixels;
		if (ModuleLease.expired() || !EscapedCreation->GetDevice(Error) || !EscapedReadback->GetPixels(Pixels, Error) ||
			Pixels.size() != 17 * 9 * 4 || Pixels.front() != 255)
		{
			std::cerr << "Escaped asynchronous request lost its provider lease: " << Error;
			return 24;
		}
		EscapedCreation.reset();
		EscapedReadback.reset();
	}
	#if VS_TEST_AUDIO
	Audio::FStreamState AudioState;
	if (ModuleLease.expired() || !AudioStream->Queue({0, 0}, Error) || !AudioStream->GetState(AudioState, Error) ||
		AudioState.PendingFrames != 1 || !AudioStream->Close(Error))
	{
		std::cerr << Error;
		return 29;
	}
	AudioStream.reset();
	#endif
	if (!ModuleLease.expired())
	{
		std::cerr << "Module retained after final native child release\n";
		return 11;
	}
	std::cout << "Dynamic provider registration, escaped resource and final module release passed\n";
	return 0;
}
