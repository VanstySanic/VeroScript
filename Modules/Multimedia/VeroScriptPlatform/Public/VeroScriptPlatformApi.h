// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::Platform
{
	inline constexpr const char* ServiceId = "platform";
	inline constexpr const char* ContractId = "veroscript.platform";
	inline constexpr char ModuleVersion[] = "0.1.1";
	inline constexpr const char* ContractVersion = ModuleVersion;
	inline constexpr std::int64_t MaxWaitMilliseconds = 50;
	inline constexpr std::int64_t MaxRumbleMilliseconds = 65535;

	enum class EEventType : std::int32_t
	{
		None,
		Quit,
		CloseRequested,
		Resized,
		PixelSizeChanged,
		ScaleChanged,
		Shown,
		Hidden,
		Minimized,
		Restored,
		FocusGained,
		FocusLost,
		KeyDown,
		KeyUp,
		PointerMove,
		PointerDown,
		PointerUp,
		Wheel,
		TextInput,
		TextEditing,
		PointerCancelled,
		Background,
		Foreground,
		InputDeviceAdded,
		InputDeviceRemoved,
		InputStateReset,
		ControllerAxis,
		ControllerButtonDown,
		ControllerButtonUp,
		ControllerHat,
		// Window-wide mouse boundary notifications; no physical device identity.
		MouseEntered,
		MouseLeft
	};
	enum class EInputDeviceKind : std::uint8_t
	{
		Unknown,
		Keyboard,
		Mouse,
		Touch,
		Joystick,
		Gamepad
	};

	// Gamepad controls are standardized; Joystick controls use the device's raw zero-based indices.
	enum class EGamepadAxis : std::int32_t
	{
		LeftX,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger
	};
	enum class EGamepadButton : std::int32_t
	{
		South,
		East,
		West,
		North,
		Back,
		Guide,
		Start,
		LeftStick,
		RightStick,
		LeftShoulder,
		RightShoulder,
		DpadUp,
		DpadDown,
		DpadLeft,
		DpadRight,
		Misc1,
		RightPaddle1,
		LeftPaddle1,
		RightPaddle2,
		LeftPaddle2,
		Touchpad,
		Misc2,
		Misc3,
		Misc4,
		Misc5,
		Misc6
	};
	enum class EHat : std::uint8_t
	{
		Centered = 0,
		Up = 1,
		Right = 2,
		Down = 4,
		Left = 8,
		RightUp = 3,
		RightDown = 6,
		LeftUp = 9,
		LeftDown = 12
	};

	enum class EPointerKind : std::int32_t
	{
		Mouse,
		Touch
	};

	enum class EKey : std::uint32_t
	{
		Unknown = 0,
		Backspace = 8,
		Tab = 9,
		Enter = 13,
		Escape = 27,
		Space = 32,
		Digit0 = 48,
		Digit1,
		Digit2,
		Digit3,
		Digit4,
		Digit5,
		Digit6,
		Digit7,
		Digit8,
		Digit9,
		A = 65,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,
		Left = 256,
		Right,
		Up,
		Down,
		Home,
		End,
		PageUp,
		PageDown,
		Insert,
		Delete,
		F1 = 512,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,
		F13,
		F14,
		F15,
		F16,
		F17,
		F18,
		F19,
		F20,
		F21,
		F22,
		F23,
		F24,
		LeftShift = 768,
		RightShift,
		LeftControl,
		RightControl,
		LeftAlt,
		RightAlt,
		LeftSuper,
		RightSuper
	};

	enum class EModifier : std::uint32_t
	{
		None = 0,
		Shift = 1,
		Control = 2,
		Alt = 4,
		Super = 8,
		CapsLock = 16,
		NumLock = 32
	};

	enum class EEventPumpMode : std::uint8_t
	{
		Owned,
		External
	};
	enum class ECursor : std::uint8_t
	{
		Default,
		Text,
		Wait,
		Crosshair,
		Progress,
		ResizeNWSE,
		ResizeNESW,
		ResizeEW,
		ResizeNS,
		Move,
		NotAllowed,
		Pointer
	};

	/** Native hosting only. HandleKind identifies the backend's handle ABI; no address enters scripts. */
	struct FBorrowedWindowOptions
	{
		std::string HandleKind;
		std::uintptr_t Handle = 0;
		std::shared_ptr<const void> Owner;
	};

	struct FWindowOptions
	{
		std::string Title = "VeroScript";
		// Sizes use native window coordinates, which are distinct from drawable pixels and GUI layout units.
		std::int64_t Width = 960, Height = 640;
		bool bVisible = true, bResizable = true, bHighPixelDensity = true;
		// Optional window/context compatibility hint; it does not select a graphics device.
		std::string GraphicsApi;
	};
	struct FInputDevice
	{
		EInputDeviceKind Kind = EInputDeviceKind::Unknown;
		std::uint64_t DeviceId = 0;
		std::string Name;
		bool bAggregate = false, bConnected = true;
		std::int32_t AxisCount = 0, ButtonCount = 0, HatCount = 0;
		bool bRumble = false;
	};
	struct FPressedKey
	{
		EKey Key = EKey::Unknown;
		std::int32_t ScanCode = 0;
		std::uint64_t WindowId = 0;
	};
	struct FPointerState
	{
		std::uint64_t PointerId = 0, WindowId = 0;
		double X = 0, Y = 0, Pressure = 0;
		std::uint32_t Buttons = 0;
	};
	/** Immutable-by-value snapshot of input observed on this service's event stream. */
	struct FInputState
	{
		FInputDevice Device;
		std::vector<FPressedKey> Keys;
		std::vector<FPointerState> Pointers;
		// Held modifier keys tracked for this device, excluding system-wide lock states.
		std::uint32_t Modifiers = 0;
		// Dense, zero-based controller snapshots. Unobserved/released controls are neutral.
		std::vector<double> Axes;
		std::vector<std::uint8_t> ControllerButtons, Hats;
	};

	struct FWindowMetrics
	{
		std::int64_t Width = 0, Height = 0, PixelWidth = 0, PixelHeight = 0;
		// Drawable pixels per native window coordinate; distinct from the preferred UI scale.
		double ScaleX = 1, ScaleY = 1;
		// Drawable pixels per density-independent UI unit, including the user's display scaling.
		double DisplayScale = 1;
	};

	/** Events own their text. Coordinates use native window units; time is monotonic nanoseconds. */
	struct FEvent
	{
		EEventType Type = EEventType::None;
		EPointerKind PointerKind = EPointerKind::Mouse;
		std::uint64_t WindowId = 0, TimestampNanoseconds = 0, PointerId = 0, DeviceId = 0;
		double X = 0, Y = 0, DeltaX = 0, DeltaY = 0, Pressure = 0;
		EKey Key = EKey::Unknown;
		// Buttons use 1=left, 2=middle, 3=right, 4=back, 5=forward; bit N-1 denotes button N.
		std::uint32_t Modifiers = 0, Buttons = 0;
		// Physical keyboard-page USB HID usage, or zero for an unmapped native extension.
		std::int32_t ScanCode = 0, Button = 0;
		// Text-editing offsets count Unicode characters, not UTF-8 bytes; -1 denotes unknown.
		std::int64_t SelectionStart = -1, SelectionLength = -1;
		bool bRepeat = false;
		std::string Text;
		EInputDeviceKind DeviceKind = EInputDeviceKind::Unknown;
		std::string DeviceName;
		bool bAggregateDevice = false, bSynthetic = false;
		// Controller input is global (WindowId=0). Axes use [-1,1], gamepad triggers [0,1].
		std::int32_t Control = 0;
		double Value = 0;
		// Connection metadata; zero counts permit incremental discovery in an external stream.
		std::int32_t AxisCount = 0, ButtonCount = 0, HatCount = 0;
		bool bRumble = false;
	};

	struct FCapabilities
	{
		bool bWindows = false, bTextInput = false, bClipboard = false, bTouch = false;
		bool bOwnsEventPump = false;
		bool bInputDevices = false, bInputState = false, bDeviceEvents = false;
		bool bSystemCursor = false, bMouseCapture = false, bBorrowedWindows = false;
		bool bControllers = false, bRumble = false;
	};

	bool ValidateWindowOptions(const FWindowOptions& Options, std::string& Error);
	bool ValidateEvent(const FEvent& Event, std::string& Error);
	EInputDeviceKind GetInputDeviceKind(const FEvent& Event) noexcept;
	bool IsController(EInputDeviceKind Kind) noexcept;
	bool ValidateRumble(double LowFrequency, double HighFrequency, std::int64_t Milliseconds, std::string& Error);
	bool ValidateWait(std::int64_t Milliseconds, std::string& Error);

	/** Methods are owner-thread operations. Close is explicit and idempotent. */
	class IWindow
	{
	public:
		virtual ~IWindow() = default;
		virtual std::uint64_t GetId() const noexcept = 0;
		virtual bool IsOpen() const noexcept = 0;
		virtual bool GetMetrics(FWindowMetrics& Result, std::string& Error) const = 0;
		virtual bool SetTitle(const std::string& Title, std::string& Error) = 0;
		virtual bool SetSize(std::int64_t Width, std::int64_t Height, std::string& Error) = 0;
		virtual bool SetVisible(bool bVisible, std::string& Error) = 0;
		virtual bool SetTextInputEnabled(bool bEnabled, std::string& Error) = 0;
		// Cursor is a horizontal offset from X in native window units, not a text character index.
		virtual bool SetTextInputArea(double X, double Y, double Width, double Height, std::int64_t Cursor,
									  std::string& Error) = 0;
		// A claimed graphics surface must be closed before its window.
		virtual bool Close(std::string& Error) = 0;
		virtual bool IsBorrowed() const noexcept;
		virtual bool SetCursor(ECursor Cursor, bool bVisible, std::string& Error);
		virtual bool SetMouseCapture(bool bEnabled, std::string& Error);
		virtual bool HasMouseCapture() const noexcept;
	};

	class IService
	{
	public:
		virtual ~IService() = default;
		virtual FCapabilities GetCapabilities() const noexcept = 0;
		virtual std::shared_ptr<IWindow> CreateWindow(const FWindowOptions& Options, std::string& Error) = 0;
		// Success and event availability are independent. No event sets Result to None.
		virtual bool PollEvent(FEvent& Result, bool& bAvailable, std::string& Error) = 0;
		virtual bool WaitEvent(std::int64_t Milliseconds, FEvent& Result, bool& bAvailable, std::string& Error) = 0;
		// External hosts submit normalized events; this does not pump a native queue.
		virtual bool SubmitEvent(const FEvent& Event, std::string& Error) = 0;
		virtual bool SetClipboardText(const std::string& Text, std::string& Error) = 0;
		virtual bool GetClipboardText(std::string& Text, std::string& Error) = 0;
		virtual bool CollectRetired(std::string& Error) = 0;
		// Enumeration is a snapshot; zero-ID entries explicitly represent aggregate sources.
		virtual bool GetInputDevices(std::vector<FInputDevice>& Result, std::string& Error);
		virtual bool GetInputState(EInputDeviceKind Kind, std::uint64_t DeviceId, FInputState& Result,
								   std::string& Error);
		// Zero resets all windows. Notifications are read through PollEvent/WaitEvent.
		virtual bool ClearInputState(std::uint64_t WindowId, std::string& Error);
		// Closing the wrapper releases its registration, never the host-owned native window.
		virtual std::shared_ptr<IWindow> AttachWindow(const FBorrowedWindowOptions& Options, std::string& Error);
		virtual bool HasPendingRetirements(bool& bPending, std::string& Error);
		// Amplitudes are 0..1, duration 0..MaxRumbleMilliseconds ms. Zero duration or two zero amplitudes stop
		// feedback.
		virtual bool Rumble(EInputDeviceKind Kind, std::uint64_t DeviceId, double LowFrequency, double HighFrequency,
							std::int64_t Milliseconds, std::string& Error);
	};
}
