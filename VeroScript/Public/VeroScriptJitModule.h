// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitBackend.h"
#include "VeroScriptJitHostServices.h"
#include "VeroScriptJitBuild.h"
#include "VeroScriptPlatform.h"

#include <string>

// Only this POD handshake is used before the matched C++ ABI is verified.
#if defined(_WIN32)
#define VEROSCRIPT_JIT_MODULE_CALL __cdecl
#define VEROSCRIPT_JIT_MODULE_EXPORT __declspec(dllexport)
#else
#define VEROSCRIPT_JIT_MODULE_CALL
#define VEROSCRIPT_JIT_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace VeroScript
{
	inline constexpr const char* JitModuleQuerySymbol = "veroscript_jit_query";

	struct FJitModuleDescriptor
	{
		std::uint32_t StructSize = sizeof(FJitModuleDescriptor);
		std::uint32_t CoreVersion = VEROSCRIPT_VERSION_CODE;
		char Abi[512]{};
		char Id[64]{};
		char Version[32]{};
		void*(VEROSCRIPT_JIT_MODULE_CALL* CreateFactory)(const void*, char*, std::uint32_t) = nullptr;
		void (VEROSCRIPT_JIT_MODULE_CALL *DestroyFactory)(void*) = nullptr;
	};

	using FJitModuleQuery = std::uint32_t (VEROSCRIPT_JIT_MODULE_CALL *)(
		std::uint32_t CoreVersion, std::uint32_t DescriptorSize, void* OutDescriptor);

	/**
	 * Header-only so each binary describes its own compiler, STL and layout.
	 * Public-header revisions are hashed by CMake, independently of product version.
	 */
	// Language mode and runtime flags can differ in a consuming executable.
	// Keep this implementation local so COMDAT folding cannot replace Core's
	// descriptor calculation with a consumer's calculation (or vice versa).
	static inline std::string BuildJitModuleAbi()
	{
		std::string Result = VEROSCRIPT_JIT_PUBLIC_ABI;
		auto Add = [&](std::uint64_t Value)
		{
			Result += ':';
			Result += std::to_string(Value);
		};
		#if defined(__clang__)
		Add(2);
		Add(__clang_major__);
		Add(__clang_minor__);
		Add(__clang_patchlevel__);
		#elif defined(_MSC_FULL_VER)
		Add(1);
		Add(_MSC_FULL_VER);
		#elif defined(__GNUC__)
		Add(3);
		Add(__GNUC__);
		Add(__GNUC_MINOR__);
		Add(__GNUC_PATCHLEVEL__);
		#endif
		#if defined(_MSVC_STL_VERSION)
		Add(_MSVC_STL_VERSION);
		Add(_MSVC_STL_UPDATE);
		#elif defined(_LIBCPP_VERSION)
		Add(_LIBCPP_VERSION);
		#elif defined(__GLIBCXX__)
		Add(__GLIBCXX__);
		#endif
		#if defined(_ITERATOR_DEBUG_LEVEL)
		Add(_ITERATOR_DEBUG_LEVEL);
		#endif
		#if defined(_GLIBCXX_USE_CXX11_ABI)
		Add(_GLIBCXX_USE_CXX11_ABI);
		#endif
		#if defined(_GLIBCXX_DEBUG)
		Result += ":glibcxx-debug";
		#endif
		#if defined(_LIBCPP_ABI_VERSION)
		Add(_LIBCPP_ABI_VERSION);
		#endif
		#if defined(_DLL)
		Result += ":dynamic-crt";
		#elif defined(_WIN32)
		Result += ":static-crt";
		#endif
		#if defined(_DEBUG)
		Result += ":debug";
		#endif
		#if defined(_MSVC_LANG)
		Add(_MSVC_LANG);
		#else
		Add(__cplusplus);
		#endif
		Result += ':';
		Result += GetHostPlatformInfo().Platform;
		#if defined(_M_X64) || defined(__x86_64__)
		Result += ":x64";
		#elif defined(_M_ARM64) || defined(__aarch64__)
		Result += ":arm64";
		#elif defined(_M_IX86) || defined(__i386__)
		Result += ":x86";
		#else
		Result += ":other-isa";
		#endif
		Add(sizeof(void*));
		Add(sizeof(FValue));
		Add(alignof(FValue));
		Add(sizeof(FBytecodeModule));
		Add(alignof(FBytecodeModule));
		Add(sizeof(FBytecodeFunction));
		Add(alignof(FBytecodeFunction));
		Add(sizeof(FJitProgramView));
		Add(sizeof(FJitCallContext));
		Add(sizeof(FJitCompileRequest));
		Add(sizeof(FJitCompileResponse));
		Add(sizeof(FJitHostServices));
		Add(VEROSCRIPT_VERSION_CODE);
		return Result;
	}
}
