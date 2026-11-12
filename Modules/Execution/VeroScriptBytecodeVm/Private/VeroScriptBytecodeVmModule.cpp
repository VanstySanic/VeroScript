// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmModule.h"

#if VEROSCRIPT_BYTECODE_VM_DYNAMIC
#include "VeroScriptVmBuild.h"
#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:veroscript_vm_query=_veroscript_vm_query")
#endif
#include <algorithm>
#include <cstring>
#include <exception>

namespace VeroScript::BytecodeVm
{
	IVmBackendFactory* CreateRawFactory(const FVmHostServices& Host);
	namespace
	{
		void* VEROSCRIPT_JIT_MODULE_CALL Create(const void* Host, char* Error, std::uint32_t Capacity) noexcept
		{
			try
			{
				return Host ? CreateRawFactory(*static_cast<const FVmHostServices*>(Host)) : nullptr;
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
			delete static_cast<IVmBackendFactory*>(Factory);
		}
	}
}

extern "C" VEROSCRIPT_JIT_MODULE_EXPORT std::uint32_t VEROSCRIPT_JIT_MODULE_CALL veroscript_vm_query(
    std::uint32_t CoreVersion, std::uint32_t DescriptorSize, void* OutDescriptor) noexcept
{
	if (CoreVersion != VeroScript::VEROSCRIPT_VERSION_CODE || DescriptorSize != sizeof(VeroScript::FVmModuleDescriptor) || !OutDescriptor)
	{
		return 0;
	}
	try
	{
		VeroScript::FVmModuleDescriptor Descriptor;
		const auto Abi = VeroScript::BuildVmModuleAbi();
		if (Abi.size() >= sizeof(Descriptor.Abi)) return 0;
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.Id, "vs-bytecode", sizeof("vs-bytecode"));
		static_assert(sizeof(VEROSCRIPT_VM_VERSION) <= sizeof(Descriptor.Version), "VM product version is too long");
		std::memcpy(Descriptor.Version, VEROSCRIPT_VM_VERSION, sizeof(VEROSCRIPT_VM_VERSION));
		Descriptor.CreateFactory = &VeroScript::BytecodeVm::Create;
		Descriptor.DestroyFactory = &VeroScript::BytecodeVm::Destroy;
		std::memcpy(OutDescriptor, &Descriptor, sizeof(Descriptor));
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}
#endif
