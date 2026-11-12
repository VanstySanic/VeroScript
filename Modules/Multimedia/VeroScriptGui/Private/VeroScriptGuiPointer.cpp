// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include <algorithm>
#include <exception>

namespace VeroScript::Gui
{
	namespace
	{
		struct FPointerGuard
		{
			explicit FPointerGuard(bool &Flag) : Flag(Flag)
			{
				Flag = true;
			}
			~FPointerGuard()
			{
				Flag = false;
			}
			bool &Flag;
		};
		Platform::ECursor SystemCursor(ECursor Cursor)
		{
			switch (Cursor)
			{
				case ECursor::Text:
					return Platform::ECursor::Text;
				case ECursor::Wait:
					return Platform::ECursor::Wait;
				case ECursor::Crosshair:
					return Platform::ECursor::Crosshair;
				case ECursor::Progress:
					return Platform::ECursor::Progress;
				case ECursor::ResizeNWSE:
					return Platform::ECursor::ResizeNWSE;
				case ECursor::ResizeNESW:
					return Platform::ECursor::ResizeNESW;
				case ECursor::ResizeEW:
					return Platform::ECursor::ResizeEW;
				case ECursor::ResizeNS:
					return Platform::ECursor::ResizeNS;
				case ECursor::Move:
					return Platform::ECursor::Move;
				case ECursor::NotAllowed:
					return Platform::ECursor::NotAllowed;
				case ECursor::Pointer:
					return Platform::ECursor::Pointer;
				default:
					return Platform::ECursor::Default;
			}
		}
	} // namespace

	bool FContext::SetPointerServices(FPointerServices Services, std::string &Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || bDispatching || bDraining || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		if (!ReleasePointerServices(Error))
		{
			return false;
		}
		{
			FPointerGuard Guard(bRefreshingPointerServices);
			PointerServices = std::move(Services);
		}
		bCursorApplied = false;
		return RefreshPointerServices(Error);
	}

	bool FContext::RefreshPointerServices(std::string &Error)
	{
		if (bRefreshingPointerServices)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		if (!PointerServices.SetCursor && !PointerServices.SetMouseCapture)
		{
			return true;
		}
		FPointerGuard Guard(bRefreshingPointerServices);
		try
		{
			const bool bCapture = std::any_of(Captures.begin(), Captures.end(),
											  [&](const auto &Entry)
											  {
												  return Entry.first.Kind == Platform::EInputDeviceKind::Mouse &&
														 Eligible(Entry.second.lock(), true, false);
											  });
			if (PointerServices.SetMouseCapture && bCapture != bMouseCaptured)
			{
				if (!PointerServices.SetMouseCapture(bCapture, Error))
				{
					if (Error.empty())
					{
						Error = "gui.mouse_capture_failed";
					}
					return false;
				}
				bMouseCaptured = bCapture;
			}
			if (!PointerServices.SetCursor)
			{
				return true;
			}
			ECursor Cursor = ECursor::Default;
			std::shared_ptr<FWidget> Target;
			bool bCapturedTarget = false;
			double X = 0, Y = 0;
			if (LastMouse)
			{
				const auto Position = MousePositions.find(*LastMouse);
				if (Position != MousePositions.end())
				{
					X = Position->second.first * WindowScaleX / Viewport.DisplayScale;
					Y = Position->second.second * WindowScaleY / Viewport.DisplayScale;
					const auto Captured = Captures.find(*LastMouse);
					if (Captured != Captures.end() && Eligible(Captured->second.lock(), true, false))
					{
						Target = Captured->second.lock();
						bCapturedTarget = true;
					}
					else if (bMouseInside)
					{
						Target = MouseHit.lock();
						if (Target && (Target->Context.lock().get() != this || Target->bDisposed))
						{
							Target.reset();
						}
					}
				}
			}
			if (Target)
			{
				FPropertyValue Value;
				if (!Target->Get(EProperty::Cursor, Value, Error))
				{
					return false;
				}
				Cursor = std::get<ECursor>(Value);
				if (Cursor == ECursor::Auto)
				{
					Cursor = ECursor::Default;
					if (Eligible(Target, true, false))
					{
						for (auto Node = Target; Node; Node = Node->Parent.lock())
						{
							const auto Kind = Node->Control.Kind;
							if (IsFloatingControl(Kind))
							{
								break;
							}
							if (IsTextControl(Kind))
							{
								Cursor = ECursor::Text;
								break;
							}
							if (Kind == EControlKind::Button || Kind == EControlKind::CheckBox ||
								Kind == EControlKind::ToggleSwitch || Kind == EControlKind::RadioButton ||
								Kind == EControlKind::Slider || Kind == EControlKind::ScrollBar)
							{
								Cursor = ECursor::Pointer;
								break;
							}
							if (Kind == EControlKind::Splitter)
							{
								const auto &Box = Node->Layout.Content;
								const bool bVertical = Node->Control.bVertical;
								const auto At = bVertical ? Y - Box.Y : X - Box.X;
								const auto Start =
									std::max(0.0, (bVertical ? Box.Height : Box.Width) - Node->Control.DividerSize) *
									Node->Control.Value;
								if (bCapturedTarget || (At >= Start && At <= Start + Node->Control.DividerSize))
								{
									Cursor = bVertical ? ECursor::ResizeNS : ECursor::ResizeEW;
								}
								break;
							}
							if (Kind == EControlKind::ScrollView)
							{
								break;
							}
						}
					}
				}
			}
			const auto Shape = SystemCursor(Cursor);
			const bool bVisible = Cursor != ECursor::None;
			if (PointerServices.SetCursor && (LastMouse || bCursorApplied) &&
				(!bCursorApplied || Shape != AppliedCursor || bVisible != bAppliedCursorVisible))
			{
				if (!PointerServices.SetCursor(Shape, bVisible, Error))
				{
					if (Error.empty())
					{
						Error = "gui.cursor_service_failed";
					}
					return false;
				}
				AppliedCursor = Shape;
				bAppliedCursorVisible = bVisible;
				bCursorApplied = true;
			}
			return true;
		}
		catch (const std::exception &Exception)
		{
			Error = std::string("gui.pointer_service_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.pointer_service_exception";
		}
		return false;
	}

	bool FContext::ReleasePointerServices(std::string &Error)
	{
		if (bRefreshingPointerServices)
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		FPointerGuard Guard(bRefreshingPointerServices);
		try
		{
			if (bMouseCaptured && PointerServices.SetMouseCapture)
			{
				if (!PointerServices.SetMouseCapture(false, Error))
				{
					if (Error.empty())
					{
						Error = "gui.mouse_capture_failed";
					}
					return false;
				}
				bMouseCaptured = false;
			}
			if (bCursorApplied && PointerServices.SetCursor &&
				(AppliedCursor != Platform::ECursor::Default || !bAppliedCursorVisible))
			{
				if (!PointerServices.SetCursor(Platform::ECursor::Default, true, Error))
				{
					if (Error.empty())
					{
						Error = "gui.cursor_service_failed";
					}
					return false;
				}
				AppliedCursor = Platform::ECursor::Default;
				bAppliedCursorVisible = true;
			}
			return true;
		}
		catch (const std::exception &Exception)
		{
			Error = std::string("gui.pointer_service_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.pointer_service_exception";
		}
		return false;
	}

	bool FContext::RefreshInputServices(std::string &Error)
	{
		const bool bTextReady = RefreshTextFocus(Error);
		std::string PointerError;
		const bool bPointerReady = RefreshPointerServices(PointerError);
		if (bTextReady && !bPointerReady)
		{
			Error = std::move(PointerError);
		}
		return bTextReady && bPointerReady;
	}
} // namespace VeroScript::Gui
