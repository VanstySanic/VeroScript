// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Runtime.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <limits>

namespace VeroScript::Sdl3
{
	namespace
	{
		std::mutex RuntimeMutex;
		std::weak_ptr<FRuntime> SharedRuntime;
		std::thread::id SharedOwnerThread;
		std::uint64_t WindowIdentity = 0;
		std::uint64_t DeviceGeneration = 0;

		struct FServiceLease final : FRetirement
		{
			void Release() noexcept override
			{
			}
		};
	}

	FRuntime::~FRuntime()
	{
		Drain();
		if (bAudioInitialized)
		{
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
		}
		if (bControllersInitialized)
		{
			SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
		}
		if (bVideoInitialized)
		{
			SDL_RemoveEventWatch(WatchWindowLifecycle, this);
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		}
		if (bEventsInitialized)
		{
			SDL_QuitSubSystem(SDL_INIT_EVENTS);
		}
	}

	bool FRuntime::CheckThread(std::string& Error) const
	{
		if (std::this_thread::get_id() != OwnerThread)
		{
			Error = "sdl3.wrong_thread: operation requires the runtime owner thread";
			return false;
		}
		return true;
	}

	bool FRuntime::EnsureVideo(std::string& Error)
	{
		if (!CheckThread(Error))
		{
			return false;
		}
		if (bVideoInitialized)
		{
			return true;
		}
		if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			return SdlError("initialize video", Error);
		}
		if (!SDL_AddEventWatch(WatchWindowLifecycle, this))
		{
			SdlError("watch window lifecycle", Error);
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			return false;
		}
		bVideoInitialized = true;
		return true;
	}

	bool FRuntime::EnsureControllers(std::string& Error)
	{
		#if VEROSCRIPT_SDL3_CONTROLLERS
		if (!CheckThread(Error))
		{
			return false;
		}
		if (!bControllersInitialized)
		{
			if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
			{
				return SdlError("initialize controllers", Error);
			}
			bControllersInitialized = true;
		}
		Error.clear();
		return true;
		#else
		Error = "platform.unsupported_controller: SDL controllers were disabled at build time";
		return false;
		#endif
	}

	bool FRuntime::EnsureAudio(std::string& Error)
	{
		#if VEROSCRIPT_SDL3_AUDIO
		if (!CheckThread(Error))
		{
			return false;
		}
		if (!bAudioInitialized)
		{
			if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
			{
				return SdlError("initialize audio", Error);
			}
			bAudioInitialized = true;
		}
		Error.clear();
		return true;
		#else
		Error = "audio.unsupported: SDL audio was disabled at build time";
		return false;
		#endif
	}

	void FRuntime::Retire(std::unique_ptr<FRetirement> Record) noexcept
	{
		if (!Record)
		{
			return;
		}
		if (std::this_thread::get_id() == OwnerThread)
		{
			Record->Release();
			return;
		}
		Defer(std::move(Record));
	}

	void FRuntime::Defer(std::unique_ptr<FRetirement> Record) noexcept
	{
		if (!Record)
		{
			return;
		}
		std::lock_guard<std::mutex> Lock(RetirementMutex);
		Record->Next = Retired;
		Retired = Record.release();
	}

	void FRuntime::Drain() noexcept
	{
		if (std::this_thread::get_id() != OwnerThread)
		{
			return;
		}
		for (auto It = NativePumps.begin(); It != NativePumps.end();)
		{
			if (const auto Pump = It->lock())
			{
				Pump->PollNativeCallbacks();
				++It;
			}
			else
			{
				It = NativePumps.erase(It);
			}
		}
		for (;;)
		{
			FRetirement* List = nullptr;
			{
				std::lock_guard<std::mutex> Lock(RetirementMutex);
				List = Retired;
				Retired = nullptr;
			}
			if (!List)
			{
				return;
			}
			while (List)
			{
				std::unique_ptr<FRetirement> Record(List);
				List = Record->Next;
				Record->Release();
			}
		}
	}

	bool FRuntime::HasPendingRetirements()
	{
		std::lock_guard<std::mutex> Lock(RetirementMutex);
		return Retired != nullptr || PendingCallbacks.load() != 0;
	}

	std::uint64_t FRuntime::NextWindowId(std::string& Error)
	{
		if (WindowIdentity == std::numeric_limits<std::uint64_t>::max())
		{
			Error = "platform.identity_exhausted: window identity space exhausted";
			return 0;
		}
		return ++WindowIdentity;
	}

	std::uint64_t FRuntime::NextDeviceGeneration(std::string& Error)
	{
		if (DeviceGeneration == std::numeric_limits<std::uint64_t>::max())
		{
			Error = "graphics.identity_exhausted: device generation space exhausted";
			return 0;
		}
		return ++DeviceGeneration;
	}

	std::shared_ptr<FRuntime> AcquireRuntime(std::shared_ptr<const void> CodeOwner, std::string& Error)
	{
		std::unique_lock<std::mutex> Lock(RuntimeMutex);
		if (!SharedRuntime.expired() && SharedOwnerThread != std::this_thread::get_id())
		{
			Error = "sdl3.wrong_thread: operation requires the runtime owner thread";
			return {};
		}
		if (auto Runtime = SharedRuntime.lock())
		{
			Lock.unlock();
			Runtime->Drain();
			Error.clear();
			return Runtime;
		}
		// SDLActivity marks the Java UI thread ready before SDL_main starts.
		// Initializing events below establishes Android's native event thread.
		#if !defined(__ANDROID__)
		if (!SDL_IsMainThread())
		{
			Error = "sdl3.wrong_thread: initialize on the application main thread";
			return {};
		}
		#endif
		SDL_SetMainReady();
		auto Runtime = std::make_shared<FRuntime>();
		Runtime->CodeOwner = std::move(CodeOwner);
		if (!SDL_InitSubSystem(SDL_INIT_EVENTS))
		{
			SdlError("initialize events", Error);
			return {};
		}
		Runtime->bEventsInitialized = true;
		SharedRuntime = Runtime;
		SharedOwnerThread = Runtime->OwnerThread;
		Error.clear();
		return Runtime;
	}

	std::unique_ptr<FRetirement> MakeServiceLease(const std::shared_ptr<FRuntime>& Runtime)
	{
		auto Lease = std::make_unique<FServiceLease>();
		Lease->Runtime = Runtime;
		return Lease;
	}

	bool SdlError(const char* Operation, std::string& Error)
	{
		Error = std::string("sdl3.native_failure: ") + Operation + ": " + SDL_GetError();
		return false;
	}

	bool CollectRetiredImplementation(std::string& Error)
	{
		std::shared_ptr<FRuntime> Runtime;
		{
			std::lock_guard<std::mutex> Lock(RuntimeMutex);
			if (!SharedRuntime.expired() && SharedOwnerThread != std::this_thread::get_id())
			{
				Error = "sdl3.wrong_thread: retirement requires the runtime owner thread";
				return false;
			}
			Runtime = SharedRuntime.lock();
		}
		if (Runtime)
		{
			if (!Runtime->CheckThread(Error))
			{
				return false;
			}
			Runtime->Drain();
		}
		Error.clear();
		return true;
	}
	bool QueryRetiredImplementation(bool& bPending, std::string& Error)
	{
		std::shared_ptr<FRuntime> Runtime;
		{
			std::lock_guard<std::mutex> Lock(RuntimeMutex);
			if (!SharedRuntime.expired() && SharedOwnerThread != std::this_thread::get_id())
			{
				Error = "sdl3.wrong_thread: retirement requires the runtime owner thread";
				return false;
			}
			Runtime = SharedRuntime.lock();
		}
		if (Runtime)
		{
			Runtime->Drain();
		}
		bPending = Runtime && Runtime->HasPendingRetirements();
		Error.clear();
		return true;
	}
}
