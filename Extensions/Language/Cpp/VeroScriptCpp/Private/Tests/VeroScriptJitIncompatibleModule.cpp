// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitModule.h"

#include <cstdlib>
#include <cstring>

#if VEROSCRIPT_TEST_JIT_INVALID_FACTORY
namespace
{
	class FInvalidFactory final : public VeroScript::IJitBackendFactory
	{
	public:
		//~ Begin IJitBackendFactory Interface
		const VeroScript::FJitBackendDescription& GetDescription() const noexcept override
		{
			return Description;
		}

		std::unique_ptr<VeroScript::IJitBackendInstance> CreateInstance(
			const VeroScript::FJitProgramView&, const VeroScript::FJitResourceLimits&, std::string& Error) const override
		{
			Error = "invalid test factory must be rejected before instance creation";
			return {};
		}
		//~ End IJitBackendFactory Interface

		FInvalidFactory()
		{
			Description.Id = "test";
			Description.Target = "incompatible-test-target";
		}

	private:
		VeroScript::FJitBackendDescription Description;
	};
}
#endif

extern "C" VEROSCRIPT_JIT_MODULE_EXPORT std::uint32_t VEROSCRIPT_JIT_MODULE_CALL veroscript_jit_query(
	std::uint32_t Version, std::uint32_t Size, void* Output) noexcept
{
	if (Version != VeroScript::VEROSCRIPT_VERSION_CODE || Size != sizeof(VeroScript::FJitModuleDescriptor) || !Output) return 0;
	VeroScript::FJitModuleDescriptor Descriptor;
	#if VEROSCRIPT_TEST_JIT_INVALID_FACTORY
	const auto Abi = VeroScript::BuildJitModuleAbi();
	if (Abi.size() >= sizeof(Descriptor.Abi)) return 0;
	std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
	Descriptor.CreateFactory = [](const void *, char *, std::uint32_t) -> void *
	{
		return new FInvalidFactory();
	};
	Descriptor.DestroyFactory = [](void *Factory)
	{
		delete static_cast<FInvalidFactory *>(Factory);
	};
	#else
	std::strcpy(Descriptor.Abi, "incompatible-test-abi");
	Descriptor.CreateFactory = [](const void *, char *, std::uint32_t) -> void *
	{
		std::abort();
	};
	Descriptor.DestroyFactory = [](void *)
	{
		std::abort();
	};
	#endif
	std::strcpy(Descriptor.Id, "test");
	std::strcpy(Descriptor.Version, "0.1.1");
	std::memcpy(Output, &Descriptor, sizeof(Descriptor));
	return 1;
}
