// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformApi.h"
#include <deque>
#include <map>

union SDL_Event;

namespace VeroScript::Sdl3
{
	class FRuntime;
	struct FRetirement;
	/** One independent input stream per service. All access belongs to its runtime owner. */
	class FInputStore final
	{
	public:
		explicit FInputStore(std::shared_ptr<FRuntime> Runtime, bool bNative);
		~FInputStore();
		bool ObserveController(const SDL_Event& Event, bool& bHandled, std::string& Error);
		bool Rumble(Platform::EInputDeviceKind Kind, std::uint64_t Id, double Low, double High,
					std::int64_t Milliseconds, std::string& Error);
		bool Initialize(std::string& Error);
		bool RefreshNative(bool bAll, std::string& Error);
		bool Submit(const Platform::FEvent& Event, std::string& Error);
		bool TakeSubmitted(Platform::FEvent& Event);
		bool TakeReady(Platform::FEvent& Event);
		bool Observe(Platform::FEvent Event, std::string& Error);
		bool GetDevices(std::vector<Platform::FInputDevice>& Result, std::string& Error);
		bool GetState(Platform::EInputDeviceKind Kind, std::uint64_t Id, Platform::FInputState& Result,
					  std::string& Error) const;
		void Clear(std::uint64_t WindowId, bool bNotifyReset, bool bNotifyHover, std::uint64_t Time = 0) noexcept;
		void WindowClosed(std::uint64_t WindowId) noexcept;

	private:
		using FDeviceKey = std::pair<Platform::EInputDeviceKind, std::uint64_t>;
		struct FPointer
		{
			Platform::FPointerState Value;
			std::uint64_t PressedWindow = 0;
			double PressedX = 0, PressedY = 0;
		};
		struct FDevice
		{
			Platform::FInputDevice Info;
			std::map<std::uint64_t, Platform::FPressedKey> Keys;
			std::map<std::uint64_t, FPointer> Pointers;
			std::vector<double> Axes;
			std::vector<std::uint8_t> Buttons, Hats;
			bool bLayoutKnown = false;
		};
		bool InitializeControllers(std::string& Error);
		void StopController(std::uint64_t Id) noexcept;
		bool AddDevice(Platform::FInputDevice Info, bool bNotify, std::uint64_t Time, std::string& Error);
		void RemoveDevice(FDevice& Device, std::uint64_t Time) noexcept;
		bool ApplyInput(FDevice& Device, Platform::FEvent& Event, std::string& Error);
		bool ResetDevice(FDevice& Device, std::uint64_t WindowId, bool bNotifyHover, std::uint64_t Time) noexcept;
		void Emit(Platform::FEvent Event) noexcept;
		bool DropQueuedInput(std::uint64_t WindowId) noexcept;
		static std::uint32_t Modifiers(const FDevice& Device) noexcept;
		static bool IsInput(Platform::EEventType Type) noexcept;
		std::shared_ptr<FRuntime> Runtime;
		std::unique_ptr<FRetirement> ControllerLease;
		std::map<FDeviceKey, FDevice> Devices;
		std::deque<Platform::FEvent> Submitted, Ready;
		std::size_t ActiveRecords = 0;
		std::uint64_t LastRefresh = 0;
		bool bNative = false, bResync = false;
		// Conservative presence flag avoids rescanning queues containing only control notifications.
		bool bReadyHasInput = false;
	};
}
