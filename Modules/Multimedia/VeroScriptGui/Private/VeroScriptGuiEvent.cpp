// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiEvent.h"

namespace VeroScript::Gui
{
	const std::vector<FEventDefinition>& GetEventDefinitions()
	{
		static const std::vector<FEventDefinition> Definitions{
		    {Platform::EEventType::Quit, "Quit", "quit", EEventDispatch::Routed, 7, true,
		        EEventDefaultAction::CloseApplication},
		    {Platform::EEventType::CloseRequested, "CloseRequested", "close-requested", EEventDispatch::Routed, 7, true,
		        EEventDefaultAction::CloseApplication},
		    {Platform::EEventType::Resized, "Resized", "resized"},
		    {Platform::EEventType::PixelSizeChanged, "PixelSizeChanged", "pixel-size-changed"},
		    {Platform::EEventType::ScaleChanged, "ScaleChanged", "scale-changed"},
		    {Platform::EEventType::Shown, "Shown", "shown"},
		    {Platform::EEventType::Hidden, "Hidden", "hidden"},
		    {Platform::EEventType::Minimized, "Minimized", "minimized"},
		    {Platform::EEventType::Restored, "Restored", "restored"},
		    {Platform::EEventType::FocusGained, "FocusGained", "focus-gained"},
		    {Platform::EEventType::FocusLost, "FocusLost", "focus-lost"},
		    {Platform::EEventType::KeyDown, "KeyDown", "key-down"},
		    {Platform::EEventType::KeyUp, "KeyUp", "key-up"},
		    {Platform::EEventType::PointerMove, "PointerMove", "pointer-move"},
		    {Platform::EEventType::PointerDown, "PointerDown", "pointer-down"},
		    {Platform::EEventType::PointerUp, "PointerUp", "pointer-up"},
		    {Platform::EEventType::Wheel, "Wheel", "wheel"},
		    {Platform::EEventType::TextInput, "TextInput", "text-input"},
		    {Platform::EEventType::TextEditing, "TextEditing", "text-editing"},
		    {Platform::EEventType::PointerCancelled, "PointerCancelled", "pointer-cancelled"},
		    {Platform::EEventType::Background, "Background", "background"},
		    {Platform::EEventType::Foreground, "Foreground", "foreground"},
		    {Platform::EEventType::InputDeviceAdded, "InputDeviceAdded", "input-device-added"},
		    {Platform::EEventType::InputDeviceRemoved, "InputDeviceRemoved", "input-device-removed"},
		    {Platform::EEventType::InputStateReset, "InputStateReset", "input-state-reset"},
		    {Platform::EEventType::MouseEntered, "MouseEntered", "mouse-entered"},
		    {Platform::EEventType::MouseLeft, "MouseLeft", "mouse-left"}};
		return Definitions;
	}
	const FEventDefinition* FindEventDefinition(std::string_view Name)
	{
		for (const auto& Definition : GetEventDefinitions())
		{
			if (Name == Definition.Name || Name == Definition.Mark)
			{
				return &Definition;
			}
		}
		return nullptr;
	}

	void FEvent::StopPropagation() noexcept
	{
		bPropagationStopped = true;
	}

	void FEvent::StopImmediatePropagation() noexcept
	{
		bPropagationStopped = true;
		bImmediateStopped = true;
	}

	void FEvent::PreventDefault() noexcept
	{
		bDefaultPrevented = true;
	}

	FSubscription::FSubscription(std::weak_ptr<FEventConnection> InConnection) : Connection(std::move(InConnection))
	{
	}

	bool FSubscription::Disconnect(std::string& Error)
	{
		Error.clear();
		auto Value = Connection.lock();
		if (!Value)
		{
			return true;
		}
		if (Value->Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread: disconnect on the GUI owner thread";
			return false;
		}
		Value->bActive = false;
		Value->Callback = {};
		return true;
	}

	bool FSubscription::IsConnected() const noexcept
	{
		const auto Value = Connection.lock();
		return Value && Value->Thread == std::this_thread::get_id() && Value->bActive;
	}
} // namespace VeroScript::Gui
