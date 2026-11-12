// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3.h"
#include "VeroScriptSdl3Runtime.h"

namespace VeroScript::Sdl3
{
	std::shared_ptr<Platform::IService> CreatePlatform(const FPlatformOptions& Options, std::string& Error)
	{
		return CreatePlatformImplementation(Options, Error);
	}

	std::shared_ptr<Graphics::IService> CreateGraphics(std::shared_ptr<const void> CodeOwner, std::string& Error)
	{
		#if VEROSCRIPT_SDL3_GRAPHICS
		return CreateGraphicsImplementation(std::move(CodeOwner), Error);
		#else
		(void)CodeOwner;
		Error = "graphics.unsupported: SDL graphics providers were disabled at build time";
		return {};
		#endif
	}

	std::shared_ptr<Audio::IService> CreateAudio(std::shared_ptr<const void> CodeOwner, std::string& Error)
	{
		return CreateAudioImplementation(std::move(CodeOwner), Error);
	}

	bool CollectRetired(std::string& Error)
	{
		return CollectRetiredImplementation(Error);
	}
	bool HasPendingRetirements(bool& bPending, std::string& Error)
	{
		return QueryRetiredImplementation(bPending, Error);
	}
}
