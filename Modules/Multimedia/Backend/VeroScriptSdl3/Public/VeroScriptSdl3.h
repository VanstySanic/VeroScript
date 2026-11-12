// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformApi.h"

namespace VeroScript::Audio
{
	class IService;
}

namespace VeroScript::Graphics
{
	class IService;
}

#if defined(_WIN32) && !defined(VEROSCRIPT_SDL3_STATIC)
#if defined(VEROSCRIPT_SDL3_EXPORTS)
#define VEROSCRIPT_SDL3_API __declspec(dllexport)
#else
#define VEROSCRIPT_SDL3_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && !defined(VEROSCRIPT_SDL3_STATIC)
#define VEROSCRIPT_SDL3_API __attribute__((visibility("default")))
#else
#define VEROSCRIPT_SDL3_API
#endif

namespace VeroScript::Sdl3
{
	inline constexpr char ModuleVersion[] = "0.1.1";

	struct FPlatformOptions
	{
		Platform::EEventPumpMode PumpMode = Platform::EEventPumpMode::Owned;
		// Dynamic extension callers retain their service/environment code lease here.
		std::shared_ptr<const void> CodeOwner;
	};

	/** Call on the application main thread. Initialization happens only in these explicit factories. */
	VEROSCRIPT_SDL3_API std::shared_ptr<Platform::IService> CreatePlatform(const FPlatformOptions& Options,
																		   std::string& Error);
	VEROSCRIPT_SDL3_API std::shared_ptr<Graphics::IService> CreateGraphics(std::shared_ptr<const void> CodeOwner,
																		   std::string& Error);
	VEROSCRIPT_SDL3_API std::shared_ptr<Audio::IService> CreateAudio(std::shared_ptr<const void> CodeOwner,
																	 std::string& Error);
	// Drain deferred native destruction on the owner thread before final host shutdown.
	VEROSCRIPT_SDL3_API bool CollectRetired(std::string& Error);
	// Close resources and release requests, then keep yielding/polling until asynchronous cleanup finishes.
	VEROSCRIPT_SDL3_API bool HasPendingRetirements(bool& bPending, std::string& Error);
}
