// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSdl3.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace VeroScript::Sdl3
{
	class FRuntime;
	class FWindowState;
	class FInputStore;
	/** Allocated with its resource, so retirement never allocates on a destructor path. */
	struct FRetirement
	{
		virtual ~FRetirement() = default;
		virtual void Release() noexcept = 0;
		std::shared_ptr<FRuntime> Runtime;
		FRetirement* Next = nullptr;
	};

	class INativePump
	{
	public:
		virtual ~INativePump() = default;
		virtual void PollNativeCallbacks() noexcept = 0;
	};

	class FRuntime final
	{
	public:
		~FRuntime();
		bool CheckThread(std::string& Error) const;
		bool EnsureVideo(std::string& Error);
		bool EnsureControllers(std::string& Error);
		bool EnsureAudio(std::string& Error);
		void Retire(std::unique_ptr<FRetirement> Record) noexcept;
		// Callback cleanup must wait until native callback stacks have unwound, including on the owner thread.
		void Defer(std::unique_ptr<FRetirement> Record) noexcept;
		void Drain() noexcept;
		bool HasPendingRetirements();
		std::uint64_t NextWindowId(std::string& Error);
		std::uint64_t NextDeviceGeneration(std::string& Error);

		std::thread::id OwnerThread = std::this_thread::get_id();
		std::shared_ptr<const void> CodeOwner;
		std::weak_ptr<Platform::IService> PumpOwner;
		std::weak_ptr<FWindowState> CaptureOwner;
		std::unordered_map<SDL_WindowID, std::weak_ptr<FWindowState>> Windows;
		std::vector<std::weak_ptr<FInputStore>> Inputs;
		std::vector<std::weak_ptr<INativePump>> NativePumps;
		std::atomic<std::uint64_t> PendingCallbacks{0};
		bool bEventsInitialized = false, bVideoInitialized = false, bControllersInitialized = false,
			 bAudioInitialized = false;

	private:
		std::mutex RetirementMutex;
		FRetirement* Retired = nullptr;
	};

	std::shared_ptr<FRuntime> AcquireRuntime(std::shared_ptr<const void> CodeOwner, std::string& Error);
	std::unique_ptr<FRetirement> MakeServiceLease(const std::shared_ptr<FRuntime>& Runtime);
	bool SdlError(const char* Operation, std::string& Error);
	std::shared_ptr<Audio::IService> CreateAudioImplementation(std::shared_ptr<const void> CodeOwner,
															   std::string& Error);
	bool CollectRetiredImplementation(std::string& Error);
	bool QueryRetiredImplementation(bool& bPending, std::string& Error);
	bool SDLCALL WatchWindowLifecycle(void* UserData, SDL_Event* Event) noexcept;
	std::shared_ptr<Platform::IService> CreatePlatformImplementation(const FPlatformOptions& Options,
																	 std::string& Error);
	std::shared_ptr<Graphics::IService> CreateGraphicsImplementation(std::shared_ptr<const void> CodeOwner,
																	 std::string& Error);
}
