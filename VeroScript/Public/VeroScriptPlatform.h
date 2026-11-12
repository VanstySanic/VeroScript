// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__)
#define VEROSCRIPT_PLATFORM_ANDROID 1
#else
#define VEROSCRIPT_PLATFORM_ANDROID 0
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
#define VEROSCRIPT_PLATFORM_IOS 1
#else
#define VEROSCRIPT_PLATFORM_IOS 0
#endif

namespace VeroScript
{
	struct FHostPlatformInfo
	{
		const char* OperatingSystem;
		const char* Architecture;
		// Android uses "runtime": device/emulator identity belongs to the host.
		// Apple uses "device", "simulator", "catalyst", or "desktop".
		const char* Environment;
		const char* Platform;
		bool bMobile;
		// Platform policy, independent of whether a backend was built or selected.
		bool bAllowsNativeJit;
		bool bAllowsDynamicJitLibraries;
	};

	// Inline so independently built backends describe their own target, without
	// linking Core. Apple device/simulator identity comes from the selected SDK.
	inline constexpr FHostPlatformInfo GetHostPlatformInfo() noexcept
	{
		#if defined(_M_ARM64) || defined(__aarch64__)
		#define VEROSCRIPT_PLATFORM_ARCH "arm64"
		#elif defined(_M_X64) || defined(__x86_64__)
		#define VEROSCRIPT_PLATFORM_ARCH "x64"
		#elif defined(_M_IX86) || defined(__i386__)
		#define VEROSCRIPT_PLATFORM_ARCH "x86"
		#elif defined(__wasm32__)
		#define VEROSCRIPT_PLATFORM_ARCH "wasm32"
		#else
		#define VEROSCRIPT_PLATFORM_ARCH "unknown"
		#endif

		#if VEROSCRIPT_PLATFORM_ANDROID
		return {"android", VEROSCRIPT_PLATFORM_ARCH, "runtime", "android-" VEROSCRIPT_PLATFORM_ARCH, true, true, true};
		#elif VEROSCRIPT_PLATFORM_IOS
		#if TARGET_OS_MACCATALYST
		return {"ios", VEROSCRIPT_PLATFORM_ARCH, "catalyst", "ios-catalyst-" VEROSCRIPT_PLATFORM_ARCH, true, false, false};
		#elif TARGET_OS_SIMULATOR
		return {"ios", VEROSCRIPT_PLATFORM_ARCH, "simulator", "ios-simulator-" VEROSCRIPT_PLATFORM_ARCH, true, false, false};
		#else
		return {"ios", VEROSCRIPT_PLATFORM_ARCH, "device", "ios-" VEROSCRIPT_PLATFORM_ARCH, true, false, false};
		#endif
		#elif defined(_WIN32)
		return {"windows", VEROSCRIPT_PLATFORM_ARCH, "desktop", "win-" VEROSCRIPT_PLATFORM_ARCH, false, true, true};
		#elif defined(__APPLE__) && TARGET_OS_OSX
		return {"macos", VEROSCRIPT_PLATFORM_ARCH, "desktop", "macos-" VEROSCRIPT_PLATFORM_ARCH, false, true, true};
		#elif defined(__EMSCRIPTEN__)
		return {"web", VEROSCRIPT_PLATFORM_ARCH, "web", "web-" VEROSCRIPT_PLATFORM_ARCH, false, false, false};
		#elif defined(__linux__)
		return {"linux", VEROSCRIPT_PLATFORM_ARCH, "desktop", "linux-" VEROSCRIPT_PLATFORM_ARCH, false, true, true};
		#else
		return {"unknown", VEROSCRIPT_PLATFORM_ARCH, "unknown", "unknown-" VEROSCRIPT_PLATFORM_ARCH, false, false, false};
		#endif
		#undef VEROSCRIPT_PLATFORM_ARCH
	}
}
