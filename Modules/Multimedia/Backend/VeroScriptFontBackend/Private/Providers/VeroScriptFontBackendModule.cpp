// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFontBackendModule.h"
#include "VeroScriptFontBackend.h"
#include "VeroScriptNativeExtension.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace VeroScript::FontBackend
{
	namespace
	{
		struct FRegistration
		{
			INativeExtensionHost *Host = nullptr;
			FHostTypeId LeaseType;
		};
		template <class T>
		void DeleteShared(void *Value)
		{
			delete static_cast<std::shared_ptr<T> *>(Value);
		}
		template <class T>
		std::shared_ptr<T> Pin(FRegistration &Registration, std::shared_ptr<T> Value,
		    std::string &Error)
		{
			if (!Value)
			{
				return {};
			}
			auto Payload = std::make_unique<std::shared_ptr<T>>(std::move(Value));
			auto *Pointer = Payload->get();
			auto Owner = Registration.Host->MakeHostObject(Registration.LeaseType, Payload.get(), DeleteShared<T>, Error);
			if (!Owner)
			{
				return {};
			}
			Payload.release();
			return std::shared_ptr<T>(std::move(Owner), Pointer);
		}
		class FFontProvider final : public Font::IService
		{
		public:
			explicit FFontProvider(FRegistration *Value) : Registration(Value), Service(CreateFontService())
			{
			}
			const char *GetBackendId() const noexcept override
			{
				return BackendId;
			}
			std::shared_ptr<Font::IFace> Load(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
			    std::string &Error, std::int32_t Instance) const override
			{
				return Pin(*Registration, Service->Load(Bytes, Index, Error, Instance), Error);
			}
			bool GetVariations(const std::vector<std::uint8_t> &Bytes, std::int32_t Index,
			    std::vector<Font::FVariation> &Result, std::string &Error) const override
			{
				return Service->GetVariations(Bytes, Index, Result, Error);
			}

		private:
			FRegistration *Registration;
			std::shared_ptr<Font::IService> Service;
		};
		class FLayoutProvider final : public Font::ITextLayoutService
		{
		public:
			explicit FLayoutProvider(FRegistration *Value) : Registration(Value), Service(CreateTextLayoutService())
			{
			}
			const char *GetBackendId() const noexcept override
			{
				return BackendId;
			}
			std::shared_ptr<const Font::ITextLayoutAnalysis> Analyze(std::string Text, Font::EDirection Direction,
			    std::string &Error) const override
			{
				return Pin(*Registration, Service->Analyze(std::move(Text), Direction, Error), Error);
			}

		private:
			FRegistration *Registration;
			std::shared_ptr<Font::ITextLayoutService> Service;
		};
		void *CreateFont(const FNativeServiceCreateContext &Context, void *Data, std::string &Error)
		{
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			Error.clear();
			return static_cast<Font::IService *>(new FFontProvider(static_cast<FRegistration *>(Data)));
		}
		void DestroyFont(void *Value, void *)
		{
			delete static_cast<Font::IService *>(Value);
		}
		void *CreateLayout(const FNativeServiceCreateContext &Context, void *Data, std::string &Error)
		{
			if (Context.CheckCancelled)
			{
				Context.CheckCancelled();
			}
			Error.clear();
			return static_cast<Font::ITextLayoutService *>(new FLayoutProvider(static_cast<FRegistration *>(Data)));
		}
		void DestroyLayout(void *Value, void *)
		{
			delete static_cast<Font::ITextLayoutService *>(Value);
		}
		void *VEROSCRIPT_EXTENSION_CALL Register(const void *Input, char *Output, std::uint32_t Capacity)
		{
			std::string Error;
			try
			{
				if (!Input)
				{
					return nullptr;
				}
				auto State = std::make_unique<FRegistration>();
				State->Host = const_cast<INativeExtensionHost *>(static_cast<const INativeExtensionHost *>(Input));
				FHostTypeDesc LeaseType;
				LeaseType.Name = "VS.FontBackend.NativeLease";
				LeaseType.Kind = EHostTypeKind::Class;
				if (!State->Host->RegisterType(std::move(LeaseType), State->LeaseType, Error))
				{
					throw std::runtime_error(Error);
				}
				FNativeServiceProviderDesc Provider;
				Provider.ServiceId = Font::ServiceId;
				Provider.BackendId = BackendId;
				Provider.ContractId = Font::ContractId;
				Provider.ContractVersion = Font::ContractVersion;
				Provider.ConfigurationKey = "font-backend.Font.0.1.2";
				Provider.UserData = State.get();
				Provider.Create = CreateFont;
				Provider.Destroy = DestroyFont;
				if (!State->Host->RegisterProvider(std::move(Provider), Error))
				{
					throw std::runtime_error(Error);
				}
				FNativeServiceProviderDesc Layout;
				Layout.ServiceId = Font::TextLayoutServiceId;
				Layout.BackendId = BackendId;
				Layout.ContractId = Font::TextLayoutContractId;
				Layout.ContractVersion = Font::TextLayoutContractVersion;
				Layout.ConfigurationKey = "font-backend.TextLayout.0.1.0";
				Layout.UserData = State.get();
				Layout.Create = CreateLayout;
				Layout.Destroy = DestroyLayout;
				if (State->Host->RegisterProvider(std::move(Layout), Error))
				{
					return State.release();
				}
			}
			catch (const std::exception &Exception)
			{
				Error = Exception.what();
			}
			catch (...)
			{
				Error = "font-backend.registration_failed";
			}
			if (Output && Capacity)
			{
				auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
				std::memcpy(Output, Error.data(), Count);
				Output[Count] = 0;
			}
			return nullptr;
		}
		void VEROSCRIPT_EXTENSION_CALL Destroy(void *State)
		{
			delete static_cast<FRegistration *>(State);
		}
	} // namespace
} // namespace VeroScript::FontBackend
extern "C" VEROSCRIPT_FONT_BACKEND_MODULE_API std::uint32_t VEROSCRIPT_EXTENSION_CALL
veroscript_font_backend_query(std::uint32_t Version, std::uint32_t Size, void *Output)
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
		const auto *Platform = GetHostPlatformInfo().Platform;
		if (Abi.size() >= sizeof(Descriptor.Abi) || std::strlen(Platform) >= sizeof(Descriptor.Platform))
		{
			return 0;
		}
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, Platform, std::strlen(Platform) + 1);
		std::memcpy(Descriptor.Id, "veroscript-font-backend", sizeof("veroscript-font-backend"));
		std::memcpy(Descriptor.Version, "0.1.1", sizeof("0.1.1"));
		Descriptor.Roles =
		    NativeExtensionRole(ENativeExtensionRole::Provider) | NativeExtensionRole(ENativeExtensionRole::Bindings);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = FontBackend::Register;
		Descriptor.Destroy = FontBackend::Destroy;
		std::memcpy(Output, &Descriptor, sizeof(Descriptor));
		return Version;
	}
	catch (...)
	{
		return 0;
	}
}
#if !defined(VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER)
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t
    VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(std::uint32_t V, std::uint32_t S, void *O)
{
	return veroscript_font_backend_query(V, S, O);
}
#endif
