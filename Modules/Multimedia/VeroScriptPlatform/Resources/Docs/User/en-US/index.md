# VeroScriptPlatform — Foundation

Platform provides windows, logical/pixel metrics, keyboard, mouse, touch, UTF-8 text/IME and clipboard contracts. Native interfaces have no Core or SDL dependency; optional script adapters expose `VS::Platform`.

Script capability, window metric and input snapshot queries are const methods and work with read-only value captures. Event consumption, input resets and window changes require a writable receiver; use `mutable` or a writable reference capture for those callbacks.

Create services on the application main thread. Pump the shared native queue once, or use external mode and submit normalized events. Close graphics surfaces before windows. Drain foreign-thread resource retirement on the owner thread before shutdown.

## Use the installed native SDK

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

Window coordinates follow the native platform. `ScaleX/ScaleY` convert window coordinates to drawable pixels; `DisplayScale` is the preferred UI scale in pixels. They can differ. Density-independent input uses `guiX = windowX * ScaleX / DisplayScale`; drawing uses `pixelX = guiX * DisplayScale`. Scripts query `window.GetDisplayScale()`. Refresh metrics when the pixel size or display scale changes.

## Multiple input devices

<a id="input-device-snapshots"></a>

`service.GetInputDevices()` returns a snapshot with `int64 Count` and `Get(index)`. Each device exposes `Kind`, `DeviceId`, `Name`, `Aggregate` and `Connected`. `service.GetInputState(kind,id)` returns an immutable snapshot: `Device`, `KeyCount`, `PointerCount`, `Modifiers`, `GetKey(index)`, `GetPointer(index)`, `IsKeyDown(Key)` and `IsScanCodeDown(int)`. Key fields are `Key/ScanCode/WindowId`; pointer fields are `PointerId/WindowId/X/Y/Pressure/Buttons`. Indices are checked. Query capabilities with `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()`.

Use `(InputDeviceKind,DeviceId)` as identity; pointer IDs are scoped to devices. Kind is `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`. Zero IDs explicitly mean aggregate input; physical enumeration does not guarantee per-mouse native event identities. State comes from events consumed by this service; pump before querying. Snapshot modifier bits describe its own held modifier keys, without system lock state. Device changes use `InputDeviceAdded/Removed`; events expose `DeviceKind`, `DeviceName`, `AggregateDevice`, `Synthetic`.

Disconnection, focus/lifecycle changes and window destruction clear affected keys/contacts, emitting synthetic releases/cancellations before the reason event. `ClearInputState(windowId)` clears a window, or all for zero, discards queued normalized input for that scope and emits `InputStateReset`. A host-submitted reset preserves subsequent event order. A synthetic global reset also signals notification loss: clear local captures/pressed state and re-enumerate. External services have independent host-fed state. The selected backend defines native discovery and supported device categories; consult its guide for physical event limitations. Versions remain unchanged; rebuild native hosts and providers together.

When a nonzero device disconnects, held aggregate input of the same kind is conservatively released too: its physical origin cannot be identified. Other aggregate presses need subsequent input to be observed again. If release routing or key layout changes, synthetic release/cancel also clears the original press owner.

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## Host windows, cursors and capture

Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture() and IsBorrowed() are available through the common adapter. Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows advertise API support; a native driver can still reject an operation. SDL capture requires this window to own both mouse and keyboard focus. Focus loss, hiding, minimization, backgrounding, quitting and input reset release capture. The cursor configuration applies while the window has mouse focus; leaving restores the default visible cursor. Hosts must coordinate global SDL cursor/capture ownership. C++ IService.AttachWindow accepts FBorrowedWindowOptions with HandleKind="sdl3.window", the trusted SDL_Window address and a nonnull shared Owner. Addresses are not exposed to scripts. Close unregisters the wrapper without destroying the native window; native destruction invalidates the wrapper and clears observed input. Close graphics surfaces before the host destroys its window. Owner is retained until wrapper retirement is destroyed; it cannot prevent a host from explicitly destroying the native window early.

See [VeroScriptSdl3 — Foundation — Host windows, cursors and capture](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/en-US/index.md#host-window-retirement).

## Graphics API selection and asynchronous work

Create compatible windows with FWindowOptions.GraphicsApi or Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi). The hint must match the chosen API; borrowed windows must already be compatible.

See [VeroScriptGraphics — Resources and 2D drawing — Graphics API selection and asynchronous work](../../../../../VeroScriptGraphics/Resources/Docs/User/en-US/index.md#resource-retirement).

## Controllers and feedback

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad` exposes the standard `GamepadAxis` and `GamepadButton` slots; `Joystick` exposes raw zero-based axes/buttons/hats. A physical device has one stream, selected by its mapping. Device IDs are nonzero connection identities, never aggregate or persistent hardware IDs. Controllers are global (`WindowId=0`); use `(Kind,DeviceId)` and pump before reading snapshots. Mapping changes release the old layout and announce the replacement, so re-enumerate after topology notifications.

Devices and connection events expose `AxisCount`, `ButtonCount`, `HatCount`, `SupportsRumble` (C++ `bRumble`). Standard gamepads have 6 axis slots and 26 button slots, including controls absent on a particular model; absent controls remain neutral. Native snapshots contain dense `Axes`, `ControllerButtons`, `Hats`; scripts use `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`, `IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`, `GetHat(int64)->Hat` and the three counts. Indices are checked. `ControllerAxis`, `ControllerButtonDown/Up`, `ControllerHat` events carry `Control` and `Value`. Axes are normalized to [-1,1], mapped triggers to [0,1]; no dead zone is imposed. Hat values use the public `Hat` enum. Button state comes from the down/up event type.

`SupportsControllers()` and `SupportsRumble()` advertise service support. Check each device's `SupportsRumble` before calling `Rumble(kind,id,lowFrequency,highFrequency,int64 milliseconds)`. Amplitudes must be finite 0..1 and duration 0..65535 ms. A new call replaces that device's feedback; duration zero or two zero amplitudes stop it. Unsupported/disconnected devices and wrong-thread calls fail explicitly. The default backend-independent method reports unsupported.

Full reset (`ClearInputState(0)`), background/quit, disconnection, notification overflow and service retirement neutralize the affected controls and stop feedback started by this service. Managed-window focus loss/hide/minimize also neutralizes global controllers conservatively; a window-specific explicit reset does not clear global controllers. Missing button releases/axis or hat neutralization are synthesized before the reason event. Native SDL can already supply disconnect releases; these need not be synthetic. Snapshots remain immutable. External streams accept normalized controller metadata/input without opening native devices; this SDL external-mode provider does not send native feedback. Host providers can implement the same Rumble contract.

Check `SupportsControllers()` and each device’s feedback capabilities. Physical availability depends on the OS, device and driver. Background-input policy belongs to the host. Gyroscopes, controller touchpads, trackballs and advanced force-feedback effects are outside this interface. Native resources and feedback retire on the owner thread; keep draining retirement before unloading.

<a id="window-and-events"></a>

Native hosts include `VeroScriptPlatformApi.h`: `IService` owns window/event operations, `IWindow` exposes window state, and `FInputDevice` / `FInputState` represent input snapshots. IDs are opaque identities, not native addresses. A close request is an event; the host decides when to close. `PollEvent` separates operation success from event availability and resets an empty result to `None`. `WaitEvent` accepts 0..50 milliseconds; external-pump hosts own waiting and supply events through `SubmitEvent`. Text/IME strings are owned UTF-8, while IME selection offsets count Unicode characters. `ScanCode` is a keyboard-page USB HID usage, or zero if unmapped. Mouse button codes are 1=left, 2=middle, 3=right, 4=back, 5=forward; the button mask uses bit N-1. Text-input areas use native window coordinates; their cursor offset is horizontal from X.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
