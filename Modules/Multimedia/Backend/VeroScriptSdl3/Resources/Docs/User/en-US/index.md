# VeroScriptSdl3 — Foundation

Script hosts must explicitly select a VM backend; registering Platform, Graphics or Audio bindings does not select one. Run the source example host with `VeroScriptVisualExample --vm-backend <VM-library-or-directory> program.vs`. `--check program.vs` validates ordinary scripts without a VM. When a VM is supplied, the example passes the same factory to compilation and execution so script compile processors use the selected backend. Native service SDKs remain independent of VM implementations.

The SDL3 backend uses SDL 3.4.18 and the `sdl3` provider ID. It supplies `platform/sdl3`, plus `graphics/sdl3` and `audio/sdl3` when those capabilities are included in the installed package. Platform, Graphics and Audio share the SDL runtime.

Registration does not initialize SDL. Create and operate services on the application main/owner thread. Offscreen rendering still needs a usable video/graphics driver. Retained resources keep the provider alive; drain owner-thread retirement before shutdown. Availability depends on the installed capabilities, operating system and device.

## Use the installed native SDK

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

See [VeroScriptGraphics — Resources and 2D drawing](../../../../../../VeroScriptGraphics/Resources/Docs/User/en-US/index.md#graphics-service-contract).

See [VeroScriptGraphics — Resources and 2D drawing — Use the installed native SDK](../../../../../../VeroScriptGraphics/Resources/Docs/User/en-US/index.md#vertex-color-mode).

## Input devices

[Platform: Multiple input devices](../../../../../../VeroScriptPlatform/Resources/Docs/User/en-US/index.md)

See [VeroScriptPlatform — Foundation — Multiple input devices](../../../../../../VeroScriptPlatform/Resources/Docs/User/en-US/index.md#input-device-snapshots).

Disconnection, focus/lifecycle changes and window destruction clear affected keys/contacts, emitting synthetic releases/cancellations before the reason event. `ClearInputState(windowId)` clears a window, or all for zero, discards queued normalized input for that scope and emits `InputStateReset`. A host-submitted reset preserves subsequent event order. A synthetic global reset also signals notification loss: clear local captures/pressed state and re-enumerate. External services have independent host-fed state. SDL reconciles touch topology during pumping/enumeration every 250 ms; keyboard/mouse changes follow ordered SDL events. Dedicated pen devices are outside this implementation. Versions remain unchanged; rebuild native hosts and providers together.

See [VeroScriptPlatform — Foundation — Multiple input devices](../../../../../../VeroScriptPlatform/Resources/Docs/User/en-US/index.md#input-device-snapshots).

The pinned SDL deduplicates keyboard transitions using global scan-code state. Multiple physical keyboards holding the same key can produce an apparent repeat and a missing release. Source IDs or raw keyboard mode do not guarantee complete per-keyboard physical state. Platform snapshots accurately track the delivered event stream; use an external host with complete device transitions when that physical guarantee is required.

## Host windows, cursors and capture

Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture() and IsBorrowed() are available through the common adapter. Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows advertise API support; a native driver can still reject an operation. SDL capture requires this window to own both mouse and keyboard focus. Focus loss, hiding, minimization, backgrounding, quitting and input reset release capture. The cursor configuration applies while the window has mouse focus or owns capture; leaving restores the default visible cursor unless dragging retains capture. Hosts must coordinate global SDL cursor/capture ownership. C++ IService.AttachWindow accepts FBorrowedWindowOptions with HandleKind="sdl3.window", the trusted SDL_Window address and a nonnull shared Owner. Addresses are not exposed to scripts. Close unregisters the wrapper without destroying the native window; native destruction invalidates the wrapper and clears observed input. Close graphics surfaces before the host destroys its window. Owner is retained until wrapper retirement is destroyed; it cannot prevent a host from explicitly destroying the native window early.

System cursors require a native default cursor. The bundled SDL advertises explicit capture for windows, cocoa and x11 only. Other drivers keep GUI logical capture without requesting unsupported SDL capture.

<a id="host-window-retirement"></a>

The SDL backend releases its GPU window claim during the native destruction event, including pending foreign-thread surface retirement; later Close remains idempotent. This observation requires SDL_EVENT_WINDOW_DESTROYED to stay enabled and unfiltered. Hosts suppressing that event must close surfaces and wrappers before destroying the native window.

## Graphics API selection and asynchronous work

<a id="sdl-graphics-drivers"></a>

Select a driver available in the installed provider: `direct3d11` or `direct3d11-warp` on Windows, `opengl` (3.3 core), `opengles` (ES 3.0), or optional `webgpu`. SDL_GPU supplies Direct3D 12, Vulkan or Metal where supported. Query the selected service and handle an unavailable driver as an error.

Create compatible windows with FWindowOptions.GraphicsApi or Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi). The hint must match the chosen API; borrowed windows must already be compatible.

For Android and the optional iOS/iPadOS/tvOS `opengles` driver, create exactly one compatible Platform window with `GraphicsApi="opengles"` before creating the device. Android EGL contexts can retain that window independently, so recovery may prepare a replacement while the old device remains alive; only one surface can own presentation at a time. UIKit retains one context for the window. Devices accept surfaces only for their retained window. Close surfaces and resources before devices/windows; closing the window invalidates its devices. The default SDL_GPU driver has a separate hosting path.

See [VeroScriptGraphics — Resources and 2D drawing — Graphics API selection and asynchronous work](../../../../../../VeroScriptGraphics/Resources/Docs/User/en-US/index.md#asynchronous-requests).

See [VeroScriptGraphics — Resources and 2D drawing — Graphics API selection and asynchronous work](../../../../../../VeroScriptGraphics/Resources/Docs/User/en-US/index.md#resource-retirement).

WebGPU requires the matching native runtime or browser port and their original notices. Browser work requires asynchronous creation and frame yielding; native and browser support must be evaluated separately for the selected SDK and target.

Default API selection is shared by CreateDevice and CreateDeviceAsync. A native WebGPU-only build with an empty driver uses the asynchronous request path. DX11 Device.Poll detects removal and invalidates device resources; recovery still requires rebuilding assets. WebGPU cancellation is safe before/after completion and when repeated; failed GetPixels preserves its output.

## Controllers and feedback

See [VeroScriptPlatform — Foundation — Controllers and feedback](../../../../../../VeroScriptPlatform/Resources/Docs/User/en-US/index.md#controllers-and-feedback).

## Audio

When audio is included, the SDL provider supplies `audio/sdl3` (`veroscript.audio`, 0.1.1). `CreateAudio` initializes audio only, sharing the existing SDL runtime. `VS::Audio` supplies playback/recording streams, device snapshots and coalesced topology changes, bounded PCM queues and gain/pause control. Native stream conversion handles sample rates and channels; the public Audio module decodes uncompressed PCM/float RIFF/WAVE without a device.

Streams start paused and are independent. Recording overflow clears unread data and retains its flag; the audio thread runs no script callbacks. Close streams and drain retirement on the owner thread. A deployed Linux SDK needs a real driver such as ALSA/PulseAudio. Microphone permissions and browser user gestures belong to the host; device availability must be checked on the actual target.

<a id="audio-event-pumping"></a>

Call `PollEvent()` or `GetDevices()` regularly on the owner thread. The SDL3 adapter pumps main-thread callbacks and native notifications without removing the host's SDL events, including for audio-only applications. Connection state uses live device enumeration; a format retained by an old stream does not prove that its device is online.

<a id="uikit-device-recovery"></a>

On iOS/iPadOS/tvOS, the `opengles` device reports `bDeviceRecovery == false` (`SupportsDeviceRecovery()` in scripts). Its window permits one retained EAGL context, so do not prepare a replacement beside the old device with RecoverDevice/RecoverDeviceAsync. Close and release the old surfaces, resources and device, drain owner-thread retirement, then create a fresh device and upload assets again. Other drivers have their own device capabilities; Android EGL replacement rules do not establish UIKit recovery support.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
