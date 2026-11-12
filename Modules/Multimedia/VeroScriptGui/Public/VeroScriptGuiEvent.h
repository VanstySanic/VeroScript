// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformApi.h"
#include <functional>
#include <thread>
#include <vector>

namespace VeroScript::Gui
{
	class FWidget;

	enum class EEventPhase : std::uint8_t
	{
		Capture,
		Target,
		Bubble
	};

	enum class EEventDispatch : std::uint8_t
	{
		Direct,
		Routed
	};
	enum class EEventDefaultAction : std::uint8_t
	{
		None,
		CloseApplication
	};
	struct FEventDefinition
	{
		Platform::EEventType Type;
		const char* Name;
		const char* Mark;
		EEventDispatch Dispatch = EEventDispatch::Routed;
		// Capture/target/bubble; all current native input routes support these phases.
		std::uint8_t PhaseMask = 7;
		bool bCanPreventDefault = true;
		EEventDefaultAction DefaultAction = EEventDefaultAction::None;
	};
	const std::vector<FEventDefinition>& GetEventDefinitions();
	const FEventDefinition* FindEventDefinition(std::string_view Name);

	struct FEventResult
	{
		bool bHandled = false, bDefaultPrevented = false, bPropagationStopped = false;
	};

	struct FEvent
	{
		void StopPropagation() noexcept;
		void StopImmediatePropagation() noexcept;
		void PreventDefault() noexcept;

		Platform::FEvent Input;
		std::shared_ptr<FWidget> Target, CurrentTarget;
		EEventPhase Phase = EEventPhase::Target;
		bool bPropagationStopped = false, bImmediateStopped = false, bDefaultPrevented = false;
	};

	struct FEventConnection
	{
		std::function<void(FEvent&)> Callback;
		std::thread::id Thread;
		Platform::EEventType Type = Platform::EEventType::None;
		bool bCapture = false, bActive = true;
	};

	/** Non-owning connection token; dropping a token does not disconnect the listener. */
	class FSubscription
	{
	public:
		FSubscription() = default;
		explicit FSubscription(std::weak_ptr<FEventConnection> InConnection);
		bool Disconnect(std::string& Error);
		bool IsConnected() const noexcept;

	private:
		std::weak_ptr<FEventConnection> Connection;
	};
} // namespace VeroScript::Gui
