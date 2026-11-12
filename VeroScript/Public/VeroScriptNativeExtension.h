// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptJitModule.h"
#include <map>

#if defined(_WIN32)
#define VEROSCRIPT_EXTENSION_CALL __cdecl
#define VEROSCRIPT_EXTENSION_EXPORT __declspec(dllexport)
#else
#define VEROSCRIPT_EXTENSION_CALL
#define VEROSCRIPT_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

namespace VeroScript
{
	class INativeExtensionService;

	// Packed major:16/minor:8/patch:8; the initial protocol is 0.1.1.
	inline constexpr std::uint32_t NativeExtensionApiVersion = 0x00000101u;
	inline constexpr const char* NativeExtensionQuerySymbol = "veroscript_extension_query";
	enum class ENativeExtensionRole : std::uint32_t
	{
		Bindings = 1,
		Compiler = 2,
		Provider = 4,
		CommandLine = 8
	};
	constexpr std::uint32_t NativeExtensionRole(ENativeExtensionRole R)
	{
		return static_cast<std::uint32_t>(R);
	}
	inline constexpr std::uint32_t NativeExtensionParallelRegistrations = 1;
	struct FNativeExtensionDescriptor
	{
		std::uint32_t StructSize = sizeof(FNativeExtensionDescriptor), ApiVersion = NativeExtensionApiVersion;
		char Abi[768]{}, CoreBuild[96]{}, Platform[64]{}, Id[64]{}, Version[64]{};
		std::uint32_t Roles = 0, Flags = 0;
		void* (VEROSCRIPT_EXTENSION_CALL *Create)(const void* Host, char* Error, std::uint32_t ErrorCapacity) = nullptr;
		void (VEROSCRIPT_EXTENSION_CALL *Destroy)(void*) = nullptr;
	};
	// Report this compilation's contract without interposing on another binary
	// or on Core when the host uses different compiler/runtime options.
	static inline std::string BuildNativeExtensionAbi()
	{
		std::string Result = std::string("native-extension:0.1.1:") + BuildJitModuleAbi() + ':' + VEROSCRIPT_CORE_IMPLEMENTATION_ID;
		Result += ':' + std::to_string(sizeof(FHostTypeDesc)) + ':' + std::to_string(sizeof(FHostMemberDesc)) + ':' + std::to_string(sizeof(FCompileProcessorDesc));
		#if defined(_CPPRTTI) || defined(__GXX_RTTI)
		Result += ":rtti";
		#else
		Result += ":no-rtti";
		#endif
		#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
		Result += ":exceptions";
		#else
		Result += ":no-exceptions";
		#endif
		return Result;
	}
	struct FNativeExtensionServiceBinding
	{
		std::shared_ptr<const void> Owner;
		std::shared_ptr<const INativeExtensionService> Service;
		std::string Id, Version, Identity;
		bool bDeterministic = false;
	};

	struct FNativeExtensionRegistrationOptions
	{
		FBindingOrigin Origin;
		std::string ConfigurationKey;
		std::map<std::string, std::string> Configuration;
		std::shared_ptr<const FModuleEnvironment> ResourceEnvironment;
		std::vector<FNativeExtensionServiceBinding> Services;
	};

	using FNativeExtensionQuery = std::uint32_t (VEROSCRIPT_EXTENSION_CALL *)(std::uint32_t, std::uint32_t, void*);

	class INativeExtensionService
	{
	public:
		virtual ~INativeExtensionService() = default;
	};
	class FNativeServiceInstance final
	{
	public:
		~FNativeServiceInstance();
		FNativeServiceInstance(const FNativeServiceInstance&) = delete;
		FNativeServiceInstance& operator=(const FNativeServiceInstance&) = delete;
		void* Get() const noexcept
		{
			return Handle;
		}

	private:
		friend std::shared_ptr<FNativeServiceInstance> CreateNativeService(const FNativeServiceProviderDesc&,
			const FNativeServiceCreateContext&, std::string&);
		FNativeServiceInstance() = default;
		std::shared_ptr<void> Owner;
		std::shared_ptr<const FModuleEnvironment> Environment;
		void* Handle = nullptr;
		void* UserData = nullptr;
		void (*Destroy)(void*, void*) = nullptr;
	};
	std::shared_ptr<FNativeServiceInstance> CreateNativeService(const FNativeServiceProviderDesc&,
		const FNativeServiceCreateContext&, std::string& Error);
	// The returned descriptor borrows the immutable provider vector.
	const FNativeServiceProviderDesc* FindNativeServiceProvider(
		const std::vector<FNativeServiceProviderDesc>& Providers, std::string_view Service,
		std::string_view Backend, std::string_view Contract, std::string_view Version, std::string& Error);
	std::shared_ptr<FNativeServiceInstance> CreateNativeService(
		const std::vector<FNativeServiceProviderDesc>& Providers, std::string_view Service,
		std::string_view Backend, std::string_view Contract, std::string_view Version,
		const FNativeServiceCreateContext&, std::string& Error,
		FBackendSelectionResult* Selection = nullptr);

	// After the POD handshake, only this matched C++ SDK interface crosses the
	// boundary. Its vtable is owned by Core; extensions do not link a private Core.
	class INativeExtensionHost
	{
	public:
		virtual ~INativeExtensionHost() = default;
		virtual const std::map<std::string, std::string>& GetConfiguration() const = 0;
		// Additional matched-SDK services (for example an embedding host's typed
		// i18n bridge) do not require a second Core or changes to the loader.
		virtual std::shared_ptr<const INativeExtensionService> GetService(const std::string& Id, const std::string& Version, std::string& Error) const = 0;
		virtual bool RegisterFunction(FHostFunctionDesc, FNativeCallback, void*, std::string&) = 0;
		virtual bool RegisterType(FHostTypeDesc, FHostTypeId&, std::string&) = 0;
		virtual bool RegisterMember(FHostTypeId, FHostMemberDesc, std::string&) = 0;
		virtual bool RegisterTypeProtocol(FHostTypeId, const FHostTypeDesc&, std::string&) = 0;
		virtual bool RegisterGenericType(FGenericHostTypeTemplateDesc, FGenericHostTypeTemplateId&, std::string&) = 0;
		virtual bool RegisterAlias(FHostTypeAliasDesc, std::string&) = 0;
		virtual bool RegisterAnnotation(FHostAnnotationDesc, std::string&) = 0;
		virtual bool RegisterProcessor(FCompileProcessorDesc, std::string&) = 0;
		virtual bool RegisterProvider(FNativeServiceProviderDesc, std::string&) = 0;
		virtual void SubmitCompileOperation(FCompileProcessingOutput&, FCompileGenerationOperation) const = 0;
		virtual void ReportCompileDiagnostic(FCompileProcessingOutput&, FScriptError) const = 0;
		// Payload ownership transfers only on success. Core allocates the shared
		// control block, invokes the matching payload destructor, and retains code.
		virtual FHostObjectHandle MakeHostObject(FHostTypeId, void* Payload, void (*DestroyPayload)(void*), std::string&) = 0;
		virtual FHostObjectHandle MakeHostObjectNamed(const std::string& TypeName, void* Payload, void (*DestroyPayload)(void*), std::string&) = 0;
		virtual bool ReadResource(const FResourceRequest&, const FResourceWork&, FResourceData&, std::string&) const = 0;
	};
	class FLoadedNativeExtension;
	class FNativeExtensionRegistry final
	{
	public:
		explicit FNativeExtensionRegistry(const FHostBindingRegistry* Base = nullptr,
			const FCompileProcessorRegistry* Processors = nullptr);
		~FNativeExtensionRegistry();
		FNativeExtensionRegistry(const FNativeExtensionRegistry&) = delete;
		FNativeExtensionRegistry& operator=(const FNativeExtensionRegistry&) = delete;
		bool Add(std::shared_ptr<const FLoadedNativeExtension>, FNativeExtensionRegistrationOptions, std::string& Error);
		std::shared_ptr<const FHostBindingRegistry> GetBindings() const;
		std::shared_ptr<const FCompileProcessorRegistry> GetProcessors() const;
		std::vector<FNativeServiceProviderDesc> GetProviders() const;
		std::vector<std::shared_ptr<const void>> GetOwners() const;
		// Actual loaded contracts, keyed by registration provider scope.
		std::vector<FExternalDependency> GetDependencies() const;
		std::string GetFingerprint() const;
	private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
