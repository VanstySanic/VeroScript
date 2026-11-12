// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptSdl3Module.h"
#include "VeroScriptNativeExtension.h"
#if VEROSCRIPT_SDL3_GRAPHICS
#include "VeroScriptGraphics2D.h"
#endif
#if VEROSCRIPT_SDL3_AUDIO
#include "VeroScriptAudio.h"
#endif
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::Sdl3
{
	namespace
	{
		struct FRegistration
		{
			INativeExtensionHost* Host = nullptr;
			FHostTypeId LeaseType;
		};

		template <class T> void DeleteLease(void* Value)
		{
			delete static_cast<std::shared_ptr<T>*>(Value);
		}

		template <class T>
		std::shared_ptr<T> Lease(FRegistration& Registration, std::shared_ptr<T> Value, std::string& Error)
		{
			if (!Value)
			{
				return {};
			}
			auto Payload = std::make_unique<std::shared_ptr<T>>(std::move(Value));
			T* Pointer = Payload->get();
			// Core owns this control block and retains module code until the foreign
			// payload deleter has returned, including destruction of its shared_ptr.
			auto Owner =
				Registration.Host->MakeHostObject(Registration.LeaseType, Payload.get(), DeleteLease<T>, Error);
			if (!Owner)
			{
				return {};
			}
			Payload.release();
			return std::shared_ptr<T>(std::move(Owner), Pointer);
		}

		class FPlatformProvider final : public Platform::IService
		{
		public:
			//~ Begin Platform::IService Interface
			Platform::FCapabilities GetCapabilities() const noexcept override
			{
				return Service->GetCapabilities();
			}
			std::shared_ptr<Platform::IWindow> CreateWindow(const Platform::FWindowOptions& O, std::string& E) override
			{
				return Lease(*Registration, Service->CreateWindow(O, E), E);
			}
			bool PollEvent(Platform::FEvent& O, bool& A, std::string& E) override
			{
				return Service->PollEvent(O, A, E);
			}
			bool WaitEvent(std::int64_t M, Platform::FEvent& O, bool& A, std::string& E) override
			{
				return Service->WaitEvent(M, O, A, E);
			}
			bool SubmitEvent(const Platform::FEvent& O, std::string& E) override
			{
				return Service->SubmitEvent(O, E);
			}
			bool SetClipboardText(const std::string& O, std::string& E) override
			{
				return Service->SetClipboardText(O, E);
			}
			bool GetClipboardText(std::string& O, std::string& E) override
			{
				return Service->GetClipboardText(O, E);
			}
			bool CollectRetired(std::string& E) override
			{
				return Service->CollectRetired(E);
			}
			bool HasPendingRetirements(bool& P, std::string& E) override
			{
				return Service->HasPendingRetirements(P, E);
			}
			bool Rumble(Platform::EInputDeviceKind Kind, std::uint64_t Id, double Low, double High,
						std::int64_t Milliseconds, std::string& Error) override
			{
				return Service->Rumble(Kind, Id, Low, High, Milliseconds, Error);
			}
			bool GetInputDevices(std::vector<Platform::FInputDevice>& O, std::string& E) override
			{
				return Service->GetInputDevices(O, E);
			}
			bool GetInputState(Platform::EInputDeviceKind K, std::uint64_t I, Platform::FInputState& O,
							   std::string& E) override
			{
				return Service->GetInputState(K, I, O, E);
			}
			bool ClearInputState(std::uint64_t W, std::string& E) override
			{
				return Service->ClearInputState(W, E);
			}
			std::shared_ptr<Platform::IWindow> AttachWindow(const Platform::FBorrowedWindowOptions& O,
															std::string& E) override
			{
				return Lease(*Registration, Service->AttachWindow(O, E), E);
			}
			//~ End Platform::IService Interface

			FPlatformProvider(std::shared_ptr<Platform::IService> InService, FRegistration* InRegistration)
				: Service(std::move(InService)), Registration(InRegistration)
			{
			}

		private:
			std::shared_ptr<Platform::IService> Service;
			FRegistration* Registration;
		};

		#if VEROSCRIPT_SDL3_AUDIO
		class FAudioProvider final : public Audio::IService
		{
		public:
			//~ Begin IService Interface
			Audio::FCapabilities GetCapabilities() const noexcept override
			{
				return Service->GetCapabilities();
			}
			std::string GetDriver() const override
			{
				return Service->GetDriver();
			}
			bool GetDevices(std::vector<Audio::FDeviceInfo>& Out, std::string& E) override
			{
				return Service->GetDevices(Out, E);
			}
			bool PollEvent(Audio::FEvent& Out, bool& Available, std::string& E) override
			{
				return Service->PollEvent(Out, Available, E);
			}
			std::shared_ptr<Audio::IStream> OpenStream(const Audio::FStreamOptions& O, std::string& E) override
			{
				return Lease(*Registration, Service->OpenStream(O, E), E);
			}
			bool CollectRetired(std::string& E) override
			{
				return Service->CollectRetired(E);
			}
			bool HasPendingRetirements(bool& Pending, std::string& E) override
			{
				return Service->HasPendingRetirements(Pending, E);
			}
			//~ End IService Interface

			FAudioProvider(std::shared_ptr<Audio::IService> S, FRegistration* R)
				: Service(std::move(S)), Registration(R)
			{
			}

		private:
			std::shared_ptr<Audio::IService> Service;
			FRegistration* Registration;
		};
		#endif

		#if VEROSCRIPT_SDL3_GRAPHICS
		class FRendererProvider : public Graphics::IRenderer2D
		{
		public:
			bool IsOpen() const noexcept override
			{
				return Renderer->IsOpen();
			}
			std::shared_ptr<Graphics::IGeometry2D> CreateGeometry(const std::vector<Graphics::FVertex2D>& V,
																  const std::vector<std::uint32_t>& I,
																  std::string& E) override
			{
				return Lease(*Registration, Renderer->CreateGeometry(V, I, E), E);
			}
			bool RenderTexture(const std::shared_ptr<Graphics::ITexture>& T, const Graphics::FDrawList2D& L,
							   const Graphics::FRenderOptions2D& O, std::string& E) override
			{
				return Renderer->RenderTexture(T, L, O, E);
			}
			bool RenderSurface(const std::shared_ptr<Graphics::ISurface>& T, const Graphics::FDrawList2D& L,
							   const Graphics::FRenderOptions2D& O, bool& P, std::string& E) override
			{
				return Renderer->RenderSurface(T, L, O, P, E);
			}
			bool Close(std::string& E) override
			{
				return Renderer->Close(E);
			}
			FRendererProvider(std::shared_ptr<Graphics::IRenderer2D> R, FRegistration* S)
				: Renderer(std::move(R)), Registration(S)
			{
			}

		private:
			std::shared_ptr<Graphics::IRenderer2D> Renderer;
			FRegistration* Registration;
		};
		class FSubmissionRendererProvider final : public FRendererProvider, public Graphics::IRenderSubmission2D
		{
		public:
			//~ Begin Graphics::IRenderSubmission2D Interface
			bool BeginSubmission(std::string& E) override
			{
				return Submission->BeginSubmission(E);
			}
			bool EndSubmission(std::string& E) override
			{
				return Submission->EndSubmission(E);
			}
			bool CancelSubmission(std::string& E) override
			{
				return Submission->CancelSubmission(E);
			}
			//~ End Graphics::IRenderSubmission2D Interface

			FSubmissionRendererProvider(std::shared_ptr<Graphics::IRenderer2D> R, FRegistration* S)
				: FRendererProvider(R, S), Submission(dynamic_cast<Graphics::IRenderSubmission2D*>(R.get()))
			{
			}

		private:
			Graphics::IRenderSubmission2D* Submission;
		};
		class FDeviceProvider final : public Graphics::IDevice
		{
		public:
			//~ Begin Graphics::IDevice Interface
			bool IsOpen() const noexcept override
			{
				return Device->IsOpen();
			}
			std::string GetDriver() const override
			{
				return Device->GetDriver();
			}
			Graphics::FCapabilities GetCapabilities() const noexcept override
			{
				return Device->GetCapabilities();
			}
			std::shared_ptr<Graphics::ITexture> CreateTexture(const Graphics::FTextureOptions& O,
															  std::string& E) override
			{
				return Lease(*Registration, Device->CreateTexture(O, E), E);
			}
			std::shared_ptr<Graphics::ISurface> CreateSurface(std::shared_ptr<Platform::IWindow> W,
															  std::string& E) override
			{
				return Lease(*Registration, Device->CreateSurface(std::move(W), E), E);
			}
			bool ClearTexture(const std::shared_ptr<Graphics::ITexture>& T, const Graphics::FColor& C,
							  std::string& E) override
			{
				return Device->ClearTexture(T, C, E);
			}
			bool Present(const std::shared_ptr<Graphics::ISurface>& S, const Graphics::FColor& C, bool& P,
						 std::string& E) override
			{
				return Device->Present(S, C, P, E);
			}
			bool ReadTexture(const std::shared_ptr<Graphics::ITexture>& T, std::vector<std::uint8_t>& O,
							 std::string& E) override
			{
				return Device->ReadTexture(T, O, E);
			}
			bool WaitIdle(std::string& E) override
			{
				return Device->WaitIdle(E);
			}
			bool Close(std::string& E) override
			{
				return Device->Close(E);
			}
			std::shared_ptr<Graphics::ISampler> CreateSampler(const Graphics::FSamplerOptions& O,
															  std::string& E) override
			{
				return Lease(*Registration, Device->CreateSampler(O, E), E);
			}
			bool UpdateTexture(const std::shared_ptr<Graphics::ITexture>& T, std::int64_t X, std::int64_t Y,
							   std::int64_t W, std::int64_t H, const std::vector<std::uint8_t>& P,
							   std::string& E) override
			{
				return Device->UpdateTexture(T, X, Y, W, H, P, E);
			}
			std::shared_ptr<Graphics::IRenderer2D> CreateRenderer2D(std::string& E) override
			{
				auto R = Device->CreateRenderer2D(E);
				if (!R)
				{
					return {};
				}
				std::shared_ptr<Graphics::IRenderer2D> Proxy;
				if (dynamic_cast<Graphics::IRenderSubmission2D*>(R.get()))
				{
					Proxy = std::make_shared<FSubmissionRendererProvider>(std::move(R), Registration);
				}
				else
				{
					Proxy = std::make_shared<FRendererProvider>(std::move(R), Registration);
				}
				return Lease(*Registration, std::move(Proxy), E);
			}
			std::uint64_t GetGeneration() const noexcept override
			{
				return Device->GetGeneration();
			}
			bool Invalidate(std::string& E) override
			{
				return Device->Invalidate(E);
			}
			std::shared_ptr<Graphics::IReadbackRequest> ReadTextureAsync(const std::shared_ptr<Graphics::ITexture>& T,
																		 std::string& E) override
			{
				return Lease(*Registration, Device->ReadTextureAsync(T, E), E);
			}
			bool Poll(std::string& E) override
			{
				return Device->Poll(E);
			}
			//~ End Graphics::IDevice Interface

			FDeviceProvider(std::shared_ptr<Graphics::IDevice> InDevice, FRegistration* InRegistration)
				: Device(std::move(InDevice)), Registration(InRegistration)
			{
			}

		private:
			std::shared_ptr<Graphics::IDevice> Device;
			FRegistration* Registration;
		};

		class FDeviceRequestProvider final : public Graphics::IDeviceRequest
		{
		public:
			Graphics::ERequestStatus Poll(std::string& E) override
			{
				return Request->Poll(E);
			}
			std::shared_ptr<Graphics::IDevice> GetDevice(std::string& E) const override
			{
				auto D = Request->GetDevice(E);
				if (!D)
				{
					return {};
				}
				std::shared_ptr<Graphics::IDevice> Proxy =
					std::make_shared<FDeviceProvider>(std::move(D), Registration);
				return Lease(*Registration, std::move(Proxy), E);
			}
			bool Cancel(std::string& E) override
			{
				return Request->Cancel(E);
			}
			FDeviceRequestProvider(std::shared_ptr<Graphics::IDeviceRequest> R, FRegistration* S)
				: Request(std::move(R)), Registration(S)
			{
			}

		private:
			std::shared_ptr<Graphics::IDeviceRequest> Request;
			FRegistration* Registration;
		};
		std::shared_ptr<Graphics::IDeviceRequest>
		WrapRequest(FRegistration& Registration, std::shared_ptr<Graphics::IDeviceRequest> Request, std::string& E)
		{
			if (!Request)
			{
				return {};
			}
			std::shared_ptr<Graphics::IDeviceRequest> Proxy =
				std::make_shared<FDeviceRequestProvider>(std::move(Request), &Registration);
			return Lease(Registration, std::move(Proxy), E);
		}
		class FGraphicsProvider final : public Graphics::IService
		{
		public:
			std::shared_ptr<Graphics::IDevice> CreateDevice(const Graphics::FDeviceOptions& O, std::string& E) override
			{
				auto Device = Service->CreateDevice(O, E);
				if (!Device)
				{
					return {};
				}
				std::shared_ptr<Graphics::IDevice> Wrapper =
					std::make_shared<FDeviceProvider>(std::move(Device), Registration);
				return Lease(*Registration, std::move(Wrapper), E);
			}
			std::shared_ptr<Graphics::IDeviceRequest> CreateDeviceAsync(const Graphics::FDeviceOptions& O,
																		std::string& E) override
			{
				return WrapRequest(*Registration, Service->CreateDeviceAsync(O, E), E);
			}
			std::shared_ptr<Graphics::IDeviceRequest> RecoverDeviceAsync(const std::shared_ptr<Graphics::IDevice>& P,
																		 const Graphics::FDeviceOptions& O,
																		 std::string& E) override
			{
				return WrapRequest(*Registration, Graphics::IService::RecoverDeviceAsync(P, O, E), E);
			}
			FGraphicsProvider(std::shared_ptr<Graphics::IService> InService, FRegistration* InRegistration)
				: Service(std::move(InService)), Registration(InRegistration)
			{
			}

		private:
			std::shared_ptr<Graphics::IService> Service;
			FRegistration* Registration;
		};
		#endif

		void* CreatePlatformProvider(const FNativeServiceCreateContext& Context, void* UserData, std::string& Error)
		{
			if (!Context.Environment)
			{
				Error = "sdl3.environment_required: native provider resources need a module environment code lease";
				return nullptr;
			}
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			FPlatformOptions Options;
			Options.CodeOwner = Context.Environment;
			for (const auto& Entry : Context.Configuration)
			{
				if (Entry.first != "pump-mode" || (Entry.second != "owned" && Entry.second != "external"))
				{
					Error = "platform.invalid_configuration: expected pump-mode=owned or external";
					return nullptr;
				}
				Options.PumpMode =
					Entry.second == "external" ? Platform::EEventPumpMode::External : Platform::EEventPumpMode::Owned;
			}
			auto Service = CreatePlatform(Options, Error);
			if (!Service)
			{
				return nullptr;
			}
			return static_cast<Platform::IService*>(
				new FPlatformProvider(std::move(Service), static_cast<FRegistration*>(UserData)));
		}

		void DestroyPlatformProvider(void* Service, void*)
		{
			delete static_cast<Platform::IService*>(Service);
		}

		#if VEROSCRIPT_SDL3_GRAPHICS
		void* CreateGraphicsProvider(const FNativeServiceCreateContext& Context, void* UserData, std::string& Error)
		{
			if (!Context.Environment || !Context.Configuration.empty())
			{
				Error = "graphics.invalid_configuration: provider requires an environment and no unrecognized options";
				return nullptr;
			}
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			auto Service = CreateGraphics(Context.Environment, Error);
			if (!Service)
			{
				return nullptr;
			}
			return static_cast<Graphics::IService*>(
				new FGraphicsProvider(std::move(Service), static_cast<FRegistration*>(UserData)));
		}

		void DestroyGraphicsProvider(void* Service, void*)
		{
			delete static_cast<Graphics::IService*>(Service);
		}
		#endif

		#if VEROSCRIPT_SDL3_AUDIO
		void* CreateAudioProvider(const FNativeServiceCreateContext& Context, void* UserData, std::string& Error)
		{
			if (!Context.Environment || !Context.Configuration.empty())
			{
				Error = "audio.invalid_configuration: provider requires an environment and no unrecognized options";
				return nullptr;
			}
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			auto Service = CreateAudio(Context.Environment, Error);
			if (!Service)
			{
				return nullptr;
			}
			return static_cast<Audio::IService*>(
				new FAudioProvider(std::move(Service), static_cast<FRegistration*>(UserData)));
		}
		void DestroyAudioProvider(void* Service, void*)
		{
			delete static_cast<Audio::IService*>(Service);
		}
		#endif

		void CopyError(char* Output, std::uint32_t Capacity, const char* Text) noexcept
		{
			if (!Output || !Capacity)
			{
				return;
			}
			const auto Count = std::min<std::size_t>(std::strlen(Text), Capacity - 1);
			std::memcpy(Output, Text, Count);
			Output[Count] = 0;
		}

		void* VEROSCRIPT_EXTENSION_CALL Register(const void* Input, char* Output, std::uint32_t Capacity)
		{
			try
			{
				if (!Input)
				{
					return nullptr;
				}
				auto& Host = *const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Input));
				std::string Error;
				auto State = std::make_unique<FRegistration>();
				State->Host = &Host;
				FHostTypeDesc LeaseType;
				LeaseType.Name = "VS.Sdl3.NativeLease";
				LeaseType.Kind = EHostTypeKind::Class;
				if (!Host.RegisterType(std::move(LeaseType), State->LeaseType, Error))
				{
					CopyError(Output, Capacity, Error.c_str());
					return nullptr;
				}
				FNativeServiceProviderDesc Provider;
				Provider.ServiceId = Platform::ServiceId;
				Provider.BackendId = "sdl3";
				Provider.ContractId = Platform::ContractId;
				Provider.ContractVersion = Platform::ContractVersion;
				Provider.ConfigurationKey = std::string("sdl3.platform.") + Platform::ModuleVersion;
				Provider.UserData = State.get();
				Provider.Create = CreatePlatformProvider;
				Provider.Destroy = DestroyPlatformProvider;
				if (!Host.RegisterProvider(Provider, Error))
				{
					CopyError(Output, Capacity, Error.c_str());
					return nullptr;
				}
				#if VEROSCRIPT_SDL3_GRAPHICS
				Provider.ServiceId = Graphics::ServiceId;
				Provider.ContractId = Graphics::ContractId;
				Provider.ContractVersion = Graphics::ContractVersion;
				Provider.ConfigurationKey = std::string("sdl3.graphics.") + Graphics::ModuleVersion;
				Provider.Create = CreateGraphicsProvider;
				Provider.Destroy = DestroyGraphicsProvider;
				if (!Host.RegisterProvider(Provider, Error))
				{
					CopyError(Output, Capacity, Error.c_str());
					return nullptr;
				}
				#endif

				#if VEROSCRIPT_SDL3_AUDIO
				Provider.ServiceId = Audio::ServiceId;
				Provider.ContractId = Audio::ContractId;
				Provider.ContractVersion = Audio::ContractVersion;
				Provider.ConfigurationKey = std::string("sdl3.audio.") + Audio::ModuleVersion;
				Provider.Create = CreateAudioProvider;
				Provider.Destroy = DestroyAudioProvider;
				if (!Host.RegisterProvider(Provider, Error))
				{
					CopyError(Output, Capacity, Error.c_str());
					return nullptr;
				}
				#endif

				return State.release();
			}
			catch (const std::exception& Exception)
			{
				CopyError(Output, Capacity, Exception.what());
			}
			catch (...)
			{
				CopyError(Output, Capacity, "sdl3.registration_failed");
			}
			return nullptr;
		}

		void VEROSCRIPT_EXTENSION_CALL Destroy(void* State)
		{
			delete static_cast<FRegistration*>(State);
		}
	}
}

extern "C" VEROSCRIPT_SDL3_API std::uint32_t VEROSCRIPT_SDL3_CALL veroscript_sdl3_query(std::uint32_t Version,
																						std::uint32_t Size,
																						void* Output)
{
	using namespace VeroScript;
	if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Output)
	{
		return 0;
	}
	try
	{
		FNativeExtensionDescriptor Descriptor;
		const auto Abi = BuildNativeExtensionAbi();
		if (Abi.size() >= sizeof(Descriptor.Abi))
		{
			return 0;
		}
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		const char* Platform = GetHostPlatformInfo().Platform;
		if (std::strlen(Platform) >= sizeof(Descriptor.Platform))
		{
			return 0;
		}
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "veroscript-sdl3", sizeof("veroscript-sdl3"));
		std::memcpy(Descriptor.Version, Sdl3::ModuleVersion, sizeof(Sdl3::ModuleVersion));
		Descriptor.Roles =
			NativeExtensionRole(ENativeExtensionRole::Provider) | NativeExtensionRole(ENativeExtensionRole::Bindings);
		Descriptor.Create = Sdl3::Register;
		Descriptor.Destroy = Sdl3::Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
}

#if !defined(VEROSCRIPT_SDL3_STATIC)
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t
	VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t Version, std::uint32_t Size, void* Output)
{
	return veroscript_sdl3_query(Version, Size, Output);
}
#endif
