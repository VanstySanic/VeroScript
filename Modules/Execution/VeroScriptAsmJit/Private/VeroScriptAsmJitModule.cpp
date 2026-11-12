// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitModule.h"

#if VEROSCRIPT_ASMJIT_DYNAMIC
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::AsmJit
{
	IJitBackendFactory* CreateFactory(const FJitHostServices& Host);

	namespace
	{
		void* VEROSCRIPT_JIT_MODULE_CALL Create(const void* Host, char* Error, std::uint32_t Capacity) noexcept
		{
			try
			{
				if (!Host) return nullptr;
				return CreateFactory(*static_cast<const FJitHostServices*>(Host));
			}
			catch (const std::exception& Exception)
			{
				if (Error && Capacity)
				{
					const auto Size = std::min<std::size_t>(std::strlen(Exception.what()), Capacity - 1);
					std::memcpy(Error, Exception.what(), Size);
					Error[Size] = 0;
				}
			}
			catch (...)
			{
			}
			return nullptr;
		}

		void VEROSCRIPT_JIT_MODULE_CALL Destroy(void* Factory) noexcept
		{
			delete static_cast<IJitBackendFactory*>(Factory);
		}
	}
}

extern "C" VEROSCRIPT_JIT_MODULE_EXPORT std::uint32_t VEROSCRIPT_JIT_MODULE_CALL veroscript_jit_query(
	std::uint32_t CoreVersion, std::uint32_t DescriptorSize, void* OutDescriptor) noexcept
{
	if (CoreVersion != VeroScript::VEROSCRIPT_VERSION_CODE || DescriptorSize != sizeof(VeroScript::FJitModuleDescriptor) || !OutDescriptor)
	{
		return 0;
	}
	try
	{
		VeroScript::FJitModuleDescriptor Descriptor;
		const auto Abi = VeroScript::BuildJitModuleAbi();
		if (Abi.size() >= sizeof(Descriptor.Abi)) return 0;
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.Id, "asmjit", sizeof("asmjit"));
		static_assert(sizeof(VEROSCRIPT_ASMJIT_MODULE_VERSION) <= sizeof(Descriptor.Version),
			"backend version must fit the module descriptor including its NUL terminator");
		std::memcpy(Descriptor.Version, VEROSCRIPT_ASMJIT_MODULE_VERSION, sizeof(VEROSCRIPT_ASMJIT_MODULE_VERSION));
		Descriptor.CreateFactory = &VeroScript::AsmJit::Create;
		Descriptor.DestroyFactory = &VeroScript::AsmJit::Destroy;
		std::memcpy(OutDescriptor, &Descriptor, sizeof(Descriptor));
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}
#endif
