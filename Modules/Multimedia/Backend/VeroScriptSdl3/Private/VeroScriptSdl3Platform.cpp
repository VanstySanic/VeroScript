// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3Platform.h"
#include "VeroScriptSdl3Input.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace VeroScript::Sdl3
{
	namespace
	{
		using Platform::EEventType;

		Platform::EKey TranslateKey(SDL_Keycode Key)
		{
			using Platform::EKey;
			if (Key >= 'A' && Key <= 'Z')
			{
				return static_cast<EKey>(static_cast<unsigned>(EKey::A) + Key - 'A');
			}
			if (Key >= SDLK_A && Key <= SDLK_Z)
			{
				return static_cast<EKey>(static_cast<unsigned>(EKey::A) + Key - SDLK_A);
			}
			if (Key >= SDLK_0 && Key <= SDLK_9)
			{
				return static_cast<EKey>(static_cast<unsigned>(EKey::Digit0) + Key - SDLK_0);
			}
			if (Key >= SDLK_F1 && Key <= SDLK_F12)
			{
				return static_cast<EKey>(static_cast<unsigned>(EKey::F1) + Key - SDLK_F1);
			}
			if (Key >= SDLK_F13 && Key <= SDLK_F24)
			{
				return static_cast<EKey>(static_cast<unsigned>(EKey::F13) + Key - SDLK_F13);
			}
			switch (Key)
			{
			case SDLK_BACKSPACE:
				return EKey::Backspace;
			case SDLK_TAB:
				return EKey::Tab;
			case SDLK_RETURN:
				return EKey::Enter;
			case SDLK_KP_ENTER:
				return EKey::Enter;
			case SDLK_ESCAPE:
				return EKey::Escape;
			case SDLK_SPACE:
				return EKey::Space;
			case SDLK_LEFT:
				return EKey::Left;
			case SDLK_RIGHT:
				return EKey::Right;
			case SDLK_UP:
				return EKey::Up;
			case SDLK_DOWN:
				return EKey::Down;
			case SDLK_HOME:
				return EKey::Home;
			case SDLK_END:
				return EKey::End;
			case SDLK_PAGEUP:
				return EKey::PageUp;
			case SDLK_PAGEDOWN:
				return EKey::PageDown;
			case SDLK_INSERT:
				return EKey::Insert;
			case SDLK_DELETE:
				return EKey::Delete;
			case SDLK_LSHIFT:
				return EKey::LeftShift;
			case SDLK_RSHIFT:
				return EKey::RightShift;
			case SDLK_LCTRL:
				return EKey::LeftControl;
			case SDLK_RCTRL:
				return EKey::RightControl;
			case SDLK_LALT:
				return EKey::LeftAlt;
			case SDLK_RALT:
				return EKey::RightAlt;
			case SDLK_LGUI:
				return EKey::LeftSuper;
			case SDLK_RGUI:
				return EKey::RightSuper;
			default:
				return EKey::Unknown;
			}
		}

		std::uint32_t TranslateModifiers(SDL_Keymod Value)
		{
			std::uint32_t Result = 0;
			if (Value & SDL_KMOD_SHIFT)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::Shift);
			}
			if (Value & SDL_KMOD_CTRL)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::Control);
			}
			if (Value & SDL_KMOD_ALT)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::Alt);
			}
			if (Value & SDL_KMOD_GUI)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::Super);
			}
			if (Value & SDL_KMOD_CAPS)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::CapsLock);
			}
			if (Value & SDL_KMOD_NUM)
			{
				Result |= static_cast<unsigned>(Platform::EModifier::NumLock);
			}
			return Result;
		}

		bool Translate(FRuntime& Runtime, const SDL_Event& Native, Platform::FEvent& Result)
		{
			Platform::FEvent Event;
			Event.TimestampNanoseconds = Native.common.timestamp;
			std::shared_ptr<FWindowState> Window;
			if (auto* NativeWindow = SDL_GetWindowFromEvent(&Native))
			{
				const auto Entry = Runtime.Windows.find(SDL_GetWindowID(NativeWindow));
				if (Entry == Runtime.Windows.end())
				{
					return false;
				}
				Window = Entry->second.lock();
				if (!Window || !Window->bOpen.load())
				{
					return false;
				}
				if (Native.common.timestamp && Native.common.timestamp < Window->CreatedNanoseconds)
				{
					return false;
				}
				Event.WindowId = Window->Id;
			}
			switch (Native.type)
			{
			case SDL_EVENT_QUIT:
				{
					Event.Type = EEventType::Quit;
					break;
				}
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				{
					Event.Type = EEventType::CloseRequested;
					break;
				}
			case SDL_EVENT_WINDOW_RESIZED:
				{
					Event.Type = EEventType::Resized;
					break;
				}
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				{
					Event.Type = EEventType::PixelSizeChanged;
					break;
				}
			case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
				{
					Event.Type = EEventType::ScaleChanged;
					break;
				}
			case SDL_EVENT_WINDOW_SHOWN:
				{
					Event.Type = EEventType::Shown;
					break;
				}
			case SDL_EVENT_WINDOW_HIDDEN:
				{
					Event.Type = EEventType::Hidden;
					break;
				}
			case SDL_EVENT_WINDOW_MINIMIZED:
				{
					Event.Type = EEventType::Minimized;
					break;
				}
			case SDL_EVENT_WINDOW_RESTORED:
				{
					Event.Type = EEventType::Restored;
					break;
				}
			case SDL_EVENT_WINDOW_MOUSE_ENTER:
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				{
					if (!Window)
					{
						return false;
					}
					Event.Type =
					    Native.type == SDL_EVENT_WINDOW_MOUSE_ENTER ? EEventType::MouseEntered : EEventType::MouseLeft;
					Event.DeviceKind = Platform::EInputDeviceKind::Mouse;
					Event.bAggregateDevice = true;
					if (Event.Type == EEventType::MouseEntered)
					{
						// The mouse may already have moved to another window while this notification was queued.
						float X = -1, Y = -1;
						if (SDL_GetMouseFocus() == Window->Retirement->Native)
						{
							SDL_GetMouseState(&X, &Y);
						}
						Event.X = X;
						Event.Y = Y;
					}
					break;
				}
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				{
					Event.Type = EEventType::FocusGained;
					break;
				}
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				{
					Event.Type = EEventType::FocusLost;
					break;
				}
			case SDL_EVENT_DID_ENTER_BACKGROUND:
				{
					Event.Type = EEventType::Background;
					break;
				}
			case SDL_EVENT_DID_ENTER_FOREGROUND:
				{
					Event.Type = EEventType::Foreground;
					break;
				}
			case SDL_EVENT_KEYBOARD_ADDED:
			case SDL_EVENT_KEYBOARD_REMOVED:
				{
					Event.Type = Native.type == SDL_EVENT_KEYBOARD_ADDED ? EEventType::InputDeviceAdded
					                                                     : EEventType::InputDeviceRemoved;
					Event.DeviceKind = Platform::EInputDeviceKind::Keyboard;
					Event.DeviceId = Native.kdevice.which;
					const char* Name = SDL_GetKeyboardNameForID(Native.kdevice.which);
					Event.DeviceName = Name ? Name : "Keyboard";
					break;
				}
			case SDL_EVENT_MOUSE_ADDED:
			case SDL_EVENT_MOUSE_REMOVED:
				{
					if (Native.mdevice.which == SDL_TOUCH_MOUSEID)
					{
						return false;
					}
					Event.Type = Native.type == SDL_EVENT_MOUSE_ADDED ? EEventType::InputDeviceAdded
					                                                  : EEventType::InputDeviceRemoved;
					Event.DeviceKind = Platform::EInputDeviceKind::Mouse;
					Event.DeviceId = Native.mdevice.which;
					Event.bAggregateDevice = Native.mdevice.which == SDL_PEN_MOUSEID;
					const char* Name = SDL_GetMouseNameForID(Native.mdevice.which);
					Event.DeviceName = Name ? Name : "Mouse";
					break;
				}
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
				{
					Event.Type = Native.type == SDL_EVENT_KEY_DOWN ? EEventType::KeyDown : EEventType::KeyUp;
					Event.Key = TranslateKey(Native.key.key);
					Event.ScanCode = Native.key.scancode >= SDL_SCANCODE_A && Native.key.scancode <= SDL_SCANCODE_RGUI
					    ? Native.key.scancode
					    : 0;
					Event.Modifiers = TranslateModifiers(Native.key.mod);
					Event.DeviceId = Native.key.which;
					Event.bRepeat = Native.key.repeat;
					break;
				}
			case SDL_EVENT_MOUSE_MOTION:
				{
					if (Native.motion.which == SDL_TOUCH_MOUSEID)
					{
						return false;
					}
					Event.Type = EEventType::PointerMove;
					Event.DeviceId = Native.motion.which;
					Event.X = Native.motion.x;
					Event.Y = Native.motion.y;
					Event.DeltaX = Native.motion.xrel;
					Event.DeltaY = Native.motion.yrel;
					Event.Buttons = Native.motion.state;
					break;
				}
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					if (Native.button.which == SDL_TOUCH_MOUSEID)
					{
						return false;
					}
					Event.Type =
					    Native.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EEventType::PointerDown : EEventType::PointerUp;
					Event.DeviceId = Native.button.which;
					Event.Button = Native.button.button;
					Event.X = Native.button.x;
					Event.Y = Native.button.y;
					break;
				}
			case SDL_EVENT_MOUSE_WHEEL:
				{
					if (Native.wheel.which == SDL_TOUCH_MOUSEID)
					{
						return false;
					}
					Event.Type = EEventType::Wheel;
					Event.DeviceId = Native.wheel.which;
					Event.X = Native.wheel.mouse_x;
					Event.Y = Native.wheel.mouse_y;
					const double Direction = Native.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
					Event.DeltaX = Direction * Native.wheel.x;
					Event.DeltaY = Direction * Native.wheel.y;
					break;
				}
			case SDL_EVENT_FINGER_DOWN:
			case SDL_EVENT_FINGER_MOTION:
			case SDL_EVENT_FINGER_UP:
			case SDL_EVENT_FINGER_CANCELED:
				{
					if (!Window || Native.tfinger.touchID == SDL_MOUSE_TOUCHID)
					{
						return false;
					}
					int Width = 0, Height = 0;
					if (!SDL_GetWindowSize(Window->Retirement->Native, &Width, &Height))
					{
						return false;
					}
					Event.Type = Native.type == SDL_EVENT_FINGER_DOWN ? EEventType::PointerDown
					    : Native.type == SDL_EVENT_FINGER_UP          ? EEventType::PointerUp
					    : Native.type == SDL_EVENT_FINGER_CANCELED    ? EEventType::PointerCancelled
					                                                  : EEventType::PointerMove;
					Event.PointerKind = Platform::EPointerKind::Touch;
					Event.PointerId = Native.tfinger.fingerID;
					Event.DeviceId = Native.tfinger.touchID;
					Event.X = Native.tfinger.x * Width;
					Event.Y = Native.tfinger.y * Height;
					Event.DeltaX = Native.tfinger.dx * Width;
					Event.DeltaY = Native.tfinger.dy * Height;
					Event.Pressure = Native.tfinger.pressure;
					break;
				}
			case SDL_EVENT_TEXT_INPUT:
				{
					Event.Type = EEventType::TextInput;
					Event.Text = Native.text.text ? Native.text.text : "";
					break;
				}
			case SDL_EVENT_TEXT_EDITING:
				{
					Event.Type = EEventType::TextEditing;
					Event.Text = Native.edit.text ? Native.edit.text : "";
					Event.SelectionStart = Native.edit.start;
					Event.SelectionLength = Native.edit.length;
					break;
				}
			default:
				{
					return false;
				}
			}
			if (!Event.WindowId && Event.Type != EEventType::Quit && Event.Type != EEventType::Background &&
			    Event.Type != EEventType::Foreground && Event.Type != EEventType::InputDeviceAdded &&
			    Event.Type != EEventType::InputDeviceRemoved)
			{
				return false;
			}
			Event.DeviceKind = Platform::GetInputDeviceKind(Event);
			Event.bAggregateDevice =
			    Event.bAggregateDevice ||
			    (Event.DeviceKind != Platform::EInputDeviceKind::Unknown && Event.DeviceId == 0) ||
			    (Event.DeviceKind == Platform::EInputDeviceKind::Mouse && Event.DeviceId == SDL_PEN_MOUSEID);
			Result = std::move(Event);
			return true;
		}

		class FPlatformService final : public Platform::IService
		{
		public:
			~FPlatformService() override
			{
				Runtime->Retire(std::move(Lease));
			}
			//~ Begin Platform::IService Interface
			Platform::FCapabilities GetCapabilities() const noexcept override
			{
				Platform::FCapabilities Result;
				Result.bWindows = true;
				Result.bTextInput = true;
				Result.bClipboard = true;
				Result.bTouch = true;
				Result.bOwnsEventPump = Mode == Platform::EEventPumpMode::Owned;
				Result.bInputDevices = true;
				Result.bInputState = true;
				Result.bDeviceEvents = true;
				Result.bSystemCursor = bSystemCursorSupported;
				// The bundled SDL implements explicit capture only in these native video drivers.
				const auto* Driver = SDL_GetCurrentVideoDriver();
				Result.bMouseCapture = Driver && (!SDL_strcmp(Driver, "windows") || !SDL_strcmp(Driver, "cocoa") || !SDL_strcmp(Driver, "x11"));
				Result.bBorrowedWindows = true;
				Result.bControllers = Mode == Platform::EEventPumpMode::External || VEROSCRIPT_SDL3_CONTROLLERS;
				Result.bRumble = Mode == Platform::EEventPumpMode::Owned && VEROSCRIPT_SDL3_CONTROLLERS;
				return Result;
			}

			std::shared_ptr<Platform::IWindow> CreateWindow(const Platform::FWindowOptions& Options,
			    std::string& Error) override
			{
				if (!Runtime->CheckThread(Error) || !Platform::ValidateWindowOptions(Options, Error))
				{
					return {};
				}
				Runtime->Drain();
				const auto Id = Runtime->NextWindowId(Error);
				if (!Id)
				{
					return {};
				}
				auto State = std::make_shared<FWindowState>(Runtime, Id);
				SDL_WindowFlags Flags = Options.bVisible ? 0 : SDL_WINDOW_HIDDEN;
				if (Options.bResizable)
				{
					Flags |= SDL_WINDOW_RESIZABLE;
				}
				if (Options.bHighPixelDensity)
				{
					Flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
				}
				State->Retirement->Native = CreateNativeWindow(Options, Flags, Error);
				if (!State->Retirement->Native)
				{
					return {};
				}
				State->bOpen.store(true);
				State->Retirement->NativeId = SDL_GetWindowID(State->Retirement->Native);
				Runtime->Windows.emplace(State->Retirement->NativeId, State);
				Error.clear();
				return std::make_shared<FWindow>(std::move(State));
			}

			bool PollEvent(Platform::FEvent& Result, bool& bAvailable, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				Platform::FEvent Event;
				const auto Complete = [&]
				{
					if (Event.Type == EEventType::InputStateReset)
					{
						ReleaseCaptureForWindow(Event.WindowId);
					}
					bAvailable = Event.Type != EEventType::None;
					Result = std::move(Event);
					Error.clear();
					return true;
				};
				if (Input->TakeReady(Event))
				{
					return Complete();
				}
				if (!Input->RefreshNative(false, Error))
				{
					return false;
				}
				if (Input->TakeReady(Event))
				{
					return Complete();
				}
				Platform::FEvent Pending;
				while (Input->TakeSubmitted(Pending))
				{
					if (!IsLiveWindow(Pending.WindowId))
					{
						continue;
					}
					if (!Observe(std::move(Pending), Error))
					{
						return false;
					}
					if (Input->TakeReady(Event))
					{
						return Complete();
					}
				}
				if (Mode == Platform::EEventPumpMode::Owned)
				{
					SDL_Event Native;
					for (unsigned Work = 0; Work < 1024 && SDL_PollEvent(&Native); ++Work)
					{
						if (!ObserveNative(Native, Error))
						{
							return false;
						}
						if (Input->TakeReady(Event))
						{
							return Complete();
						}
					}
				}
				return Complete();
			}

			bool WaitEvent(std::int64_t Milliseconds, Platform::FEvent& Result, bool& bAvailable,
			    std::string& Error) override
			{
				if (!Runtime->CheckThread(Error) || !Platform::ValidateWait(Milliseconds, Error))
				{
					return false;
				}
				if (!PollEvent(Result, bAvailable, Error))
				{
					return false;
				}
				if (bAvailable || !Milliseconds)
				{
					return true;
				}
				if (Mode == Platform::EEventPumpMode::External)
				{
					Error = "platform.external_pump: waiting is owned by the embedding host";
					return false;
				}
				SDL_Event Native;
				SDL_ClearError();
				if (SDL_WaitEventTimeout(&Native, static_cast<int>(Milliseconds)))
				{
					if (!ObserveNative(Native, Error))
					{
						return false;
					}
					Result = {};
					bAvailable = Input->TakeReady(Result);
					if (Result.Type == EEventType::InputStateReset)
					{
						ReleaseCaptureForWindow(Result.WindowId);
					}
					return true;
				}
				if (*SDL_GetError())
				{
					return SdlError("wait for event", Error);
				}
				return true;
			}

			bool SubmitEvent(const Platform::FEvent& Event, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error) || !Platform::ValidateEvent(Event, Error))
				{
					return false;
				}
				if (!IsLiveWindow(Event.WindowId))
				{
					Error = "platform.invalid_window: event refers to a closed or foreign window";
					return false;
				}
				return Input->Submit(Event, Error);
			}

			bool SetClipboardText(const std::string& Text, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				if (Text.find('\0') != std::string::npos)
				{
					Error = "platform.invalid_text: clipboard text contains NUL";
					return false;
				}
				if (!SDL_SetClipboardText(Text.c_str()))
				{
					return SdlError("set clipboard", Error);
				}
				Error.clear();
				return true;
			}

			bool GetClipboardText(std::string& Text, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				std::unique_ptr<char, decltype(&SDL_free)> Value(SDL_GetClipboardText(), SDL_free);
				if (!Value)
				{
					return SdlError("get clipboard", Error);
				}
				std::string Copy(Value.get());
				Text.swap(Copy);
				Error.clear();
				return true;
			}

			bool CollectRetired(std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				Error.clear();
				return true;
			}
			bool HasPendingRetirements(bool& bPending, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				bPending = Runtime->HasPendingRetirements();
				Error.clear();
				return true;
			}
			bool GetInputDevices(std::vector<Platform::FInputDevice>& Result, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				return Input->GetDevices(Result, Error);
			}
			bool GetInputState(Platform::EInputDeviceKind Kind, std::uint64_t Id, Platform::FInputState& Result,
			    std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				return Input->GetState(Kind, Id, Result, Error);
			}
			bool ClearInputState(std::uint64_t WindowId, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				if (!IsLiveWindow(WindowId))
				{
					Error = "platform.invalid_window: reset refers to a closed or foreign window";
					return false;
				}
				Input->Clear(WindowId, true, true, SDL_GetTicksNS());
				ReleaseCaptureForWindow(WindowId);
				Error.clear();
				return true;
			}
			bool Rumble(Platform::EInputDeviceKind Kind, std::uint64_t Id, double Low, double High,
			    std::int64_t Milliseconds, std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return false;
				}
				Runtime->Drain();
				return Input->Rumble(Kind, Id, Low, High, Milliseconds, Error);
			}
			std::shared_ptr<Platform::IWindow> AttachWindow(const Platform::FBorrowedWindowOptions& Options,
			    std::string& Error) override
			{
				if (!Runtime->CheckThread(Error))
				{
					return {};
				}
				Runtime->Drain();
				if (Options.HandleKind != "sdl3.window" || !Options.Handle || !Options.Owner)
				{
					Error = "platform.invalid_window: expected an sdl3.window handle and its lifetime owner";
					return {};
				}
				auto* Native = reinterpret_cast<SDL_Window*>(Options.Handle);
				const auto NativeId = SDL_GetWindowID(Native);
				if (!NativeId || Runtime->Windows.count(NativeId))
				{
					Error = "platform.invalid_window: native window is invalid or already attached";
					return {};
				}
				const auto Id = Runtime->NextWindowId(Error);
				if (!Id)
				{
					return {};
				}
				auto State = std::make_shared<FWindowState>(Runtime, Id);
				State->Retirement->Native = Native;
				State->Retirement->NativeId = NativeId;
				State->Retirement->bBorrowed = true;
				State->Retirement->HostOwner = Options.Owner;
				State->bOpen.store(true);
				Runtime->Windows.emplace(NativeId, State);
				Error.clear();
				return std::make_shared<FWindow>(std::move(State));
			}

			//~ End Platform::IService Interface

			FPlatformService(std::shared_ptr<FRuntime> InRuntime, Platform::EEventPumpMode InMode)
			    : Runtime(std::move(InRuntime)), Lease(MakeServiceLease(Runtime)), Mode(InMode),
			      Input(std::make_shared<FInputStore>(Runtime, InMode == Platform::EEventPumpMode::Owned))
			{
			}

			bool Initialize(std::string& Error)
			{
				if (!Input->Initialize(Error))
				{
					return false;
				}
				// Probe once without applying a cursor; headless SDL may expose a software default only.
				auto* Cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
				bSystemCursorSupported = Cursor != nullptr;
				if (Cursor)
				{
					SDL_DestroyCursor(Cursor);
				}
				else
				{
					SDL_ClearError();
				}
				Runtime->Inputs.erase(std::remove_if(Runtime->Inputs.begin(), Runtime->Inputs.end(),
				                          [](const auto& Weak)
				{
					return Weak.expired();
				}),
				    Runtime->Inputs.end());
				Runtime->Inputs.emplace_back(Input);
				return true;
			}

		private:
			bool ObserveNative(const SDL_Event& Native, std::string& Error)
			{
				bool Handled = false;
				if (!Input->ObserveController(Native, Handled, Error))
				{
					return false;
				}
				if (Handled)
				{
					return true;
				}
				Platform::FEvent Event;
				return !Translate(*Runtime, Native, Event) || Observe(std::move(Event), Error);
			}
			bool Observe(Platform::FEvent Event, std::string& Error)
			{
				const auto Type = Event.Type;
				const auto WindowId = Event.WindowId;
				if (!Input->Observe(std::move(Event), Error))
				{
					return false;
				}
				if (Type == EEventType::FocusLost || Type == EEventType::Hidden || Type == EEventType::Minimized ||
				    Type == EEventType::Background || Type == EEventType::Quit || Type == EEventType::InputStateReset)
				{
					ReleaseCaptureForWindow(WindowId);
				}
				return true;
			}
			void ReleaseCaptureForWindow(std::uint64_t WindowId)
			{
				if (const auto Captured = Runtime->CaptureOwner.lock();
				    Captured && (!WindowId || Captured->Id == WindowId))
				{
					Captured->ReleaseCapture();
				}
			}

			bool IsLiveWindow(std::uint64_t Id) const
			{
				if (!Id)
				{
					return true;
				}
				for (const auto& Entry : Runtime->Windows)
				{
					const auto Window = Entry.second.lock();
					if (Window && Window->bOpen.load() && Window->Id == Id)
					{
						return true;
					}
				}
				return false;
			}

			std::shared_ptr<FRuntime> Runtime;
			std::unique_ptr<FRetirement> Lease;
			Platform::EEventPumpMode Mode;
			std::shared_ptr<FInputStore> Input;
			bool bSystemCursorSupported = false;
		};
	}

	void FWindowRetirement::Release() noexcept
	{
		if (bCaptureRequested)
		{
			const auto Current = Runtime->CaptureOwner.lock();
			if (!Current || Current->Id == WindowId)
			{
				SDL_CaptureMouse(false);
			}
		}
		bCaptureRequested = false;
		if (NativeId)
		{
			for (const auto& Weak : Runtime->Inputs)
			{
				if (const auto Input = Weak.lock())
				{
					Input->WindowClosed(WindowId);
				}
			}
			Runtime->Windows.erase(NativeId);
			NativeId = 0;
		}
		if (Cursor)
		{
			if (SDL_GetCursor() == Cursor)
			{
				SDL_SetCursor(SDL_GetDefaultCursor());
				SDL_ShowCursor();
			}
			SDL_DestroyCursor(Cursor);
			Cursor = nullptr;
		}
		if (Native)
		{
			if (!bBorrowed)
			{
				SDL_DestroyWindow(Native);
			}
			Native = nullptr;
		}
	}

	FWindowState::FWindowState(std::shared_ptr<FRuntime> InRuntime, std::uint64_t InId)
	    : Runtime(std::move(InRuntime)), Retirement(std::make_unique<FWindowRetirement>()), Id(InId)
	{
		Retirement->Runtime = Runtime;
		Retirement->WindowId = Id;
		CreatedNanoseconds = SDL_GetTicksNS();
	}

	FWindowState::~FWindowState()
	{
		Runtime->Retire(std::move(Retirement));
	}

	bool FWindowState::Check(std::string& Error) const
	{
		if (!Runtime->CheckThread(Error))
		{
			return false;
		}
		if (!bOpen.load())
		{
			Error = "platform.closed_window: window is closed";
			return false;
		}
		return true;
	}

	bool FWindowState::Close(std::string& Error)
	{
		if (!Runtime->CheckThread(Error))
		{
			return false;
		}
		if (Claims)
		{
			Error = "platform.window_in_use: close graphics surfaces before closing the window";
			return false;
		}
		ReleaseCapture();
		Retirement->Release();
		bOpen.store(false);
		Error.clear();
		return true;
	}

	std::uint64_t FWindow::GetId() const noexcept
	{
		return State->Id;
	}
	bool FWindow::IsOpen() const noexcept
	{
		return State->bOpen.load();
	}

	bool FWindow::GetMetrics(Platform::FWindowMetrics& Result, std::string& Error) const
	{
		if (!State->Check(Error))
		{
			return false;
		}
		int Width = 0, Height = 0, PixelWidth = 0, PixelHeight = 0;
		if (!SDL_GetWindowSize(State->Retirement->Native, &Width, &Height) ||
		    !SDL_GetWindowSizeInPixels(State->Retirement->Native, &PixelWidth, &PixelHeight))
		{
			return SdlError("query window dimensions", Error);
		}
		const double Scale = SDL_GetWindowDisplayScale(State->Retirement->Native);
		if (!std::isfinite(Scale) || Scale <= 0)
		{
			return SdlError("query display scale", Error);
		}
		const double Density = SDL_GetWindowPixelDensity(State->Retirement->Native);
		if (!std::isfinite(Density) || Density <= 0)
		{
			return SdlError("query pixel density", Error);
		}
		Result = {Width,
		    Height,
		    PixelWidth,
		    PixelHeight,
		    Width > 0 && PixelWidth > 0 ? static_cast<double>(PixelWidth) / Width : Density,
		    Height > 0 && PixelHeight > 0 ? static_cast<double>(PixelHeight) / Height : Density,
		    Scale};
		Error.clear();
		return true;
	}

	bool FWindow::SetTitle(const std::string& Title, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		Platform::FWindowOptions Options;
		Options.Title = Title;
		if (!Platform::ValidateWindowOptions(Options, Error))
		{
			return false;
		}
		if (!SDL_SetWindowTitle(State->Retirement->Native, Title.c_str()))
		{
			return SdlError("set title", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindow::SetSize(std::int64_t Width, std::int64_t Height, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		Platform::FWindowOptions Options;
		Options.Width = Width;
		Options.Height = Height;
		if (!Platform::ValidateWindowOptions(Options, Error))
		{
			return false;
		}
		if (!SDL_SetWindowSize(State->Retirement->Native, static_cast<int>(Width), static_cast<int>(Height)))
		{
			return SdlError("set size", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindow::SetVisible(bool bVisible, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		if (!(bVisible ? SDL_ShowWindow(State->Retirement->Native) : SDL_HideWindow(State->Retirement->Native)))
		{
			return SdlError("set visibility", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindow::SetTextInputEnabled(bool bEnabled, std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		if (!(bEnabled ? SDL_StartTextInput(State->Retirement->Native) : SDL_StopTextInput(State->Retirement->Native)))
		{
			return SdlError("set text input", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindow::SetTextInputArea(double X, double Y, double Width, double Height, std::int64_t Cursor,
	    std::string& Error)
	{
		if (!State->Check(Error))
		{
			return false;
		}
		const double Min = std::numeric_limits<int>::min(), Max = std::numeric_limits<int>::max();
		if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Width) || !std::isfinite(Height) || X < Min ||
		    Y < Min || X > Max || Y > Max || Width < 0 || Height < 0 || Width > Max || Height > Max ||
		    X + Width > Max || Y + Height > Max || Cursor < 0 || Cursor > Max || X + Cursor > Max)
		{
			Error = "platform.invalid_text_area: coordinates and sizes must fit int32";
			return false;
		}
		SDL_Rect Area{static_cast<int>(std::floor(X)), static_cast<int>(std::floor(Y)),
		    static_cast<int>(std::ceil(Width)), static_cast<int>(std::ceil(Height))};
		if (!SDL_SetTextInputArea(State->Retirement->Native, &Area, static_cast<int>(Cursor)))
		{
			return SdlError("set text input area", Error);
		}
		Error.clear();
		return true;
	}

	bool FWindow::Close(std::string& Error)
	{
		return State->Close(Error);
	}
	FWindow::FWindow(std::shared_ptr<FWindowState> InState) : State(std::move(InState))
	{
	}
	std::shared_ptr<FWindowState> FWindow::GetState() const
	{
		return State;
	}

	std::shared_ptr<Platform::IService> CreatePlatformImplementation(const FPlatformOptions& Options,
	    std::string& Error)
	{
		if (Options.PumpMode != Platform::EEventPumpMode::Owned &&
		    Options.PumpMode != Platform::EEventPumpMode::External)
		{
			Error = "platform.invalid_pump: unknown event pump mode";
			return {};
		}
		auto Runtime = AcquireRuntime(Options.CodeOwner, Error);
		if (!Runtime || !Runtime->EnsureVideo(Error))
		{
			return {};
		}
		if (Options.PumpMode == Platform::EEventPumpMode::Owned && !Runtime->PumpOwner.expired())
		{
			Error = "platform.pump_in_use: this SDL runtime already has an event pump owner";
			return {};
		}
		auto Service = std::make_shared<FPlatformService>(Runtime, Options.PumpMode);
		if (!Service->Initialize(Error))
		{
			return {};
		}
		if (Options.PumpMode == Platform::EEventPumpMode::Owned)
		{
			Runtime->PumpOwner = Service;
		}
		Error.clear();
		return Service;
	}
}
