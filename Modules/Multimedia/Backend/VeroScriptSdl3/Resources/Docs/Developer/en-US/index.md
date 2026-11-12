# VeroScriptSdl3

<a id="provider-boundary"></a>

Public Platform/Graphics/Audio contracts remain independent of SDL and Core. The concrete SDL implementation, runtime, driver options and notices belong to VeroScriptSdl3. Validate matched contracts, installed/relocated SDKs, module leases and owner-thread retirement when changing providers. Source checks cannot replace actual device and platform verification.

## Build and hosting

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## Maintenance and verification

`VEROSCRIPT_SDL3_GRAPHICS` selects graphics service registration. Its drivers are controlled independently by `VEROSCRIPT_SDL3_GPU`, `VEROSCRIPT_SDL3_D3D11`, `VEROSCRIPT_SDL3_OPENGL` and `VEROSCRIPT_SDL3_WEBGPU`; WebGPU defaults OFF. Controller and audio selection use `VEROSCRIPT_SDL3_CONTROLLERS` and `VEROSCRIPT_SDL3_AUDIO`. Native WebGPU uses pinned wgpu-native v29.0.1.1 through `VEROSCRIPT_WEBGPU_ROOT`; browser WebGPU uses the pinned Emdawn port. Keep actual validation results separate from build support.

<a id="sdl-audio-build"></a>

The root audio build uses `VEROSCRIPT_BUILD_AUDIO=ON`; the SDL implementation additionally uses `VEROSCRIPT_BUILD_SDL3=ON` and `VEROSCRIPT_SDL3_AUDIO=ON`. The standalone foundation entry also supports Audio. Qualify real Linux drivers separately from dummy-device fixtures.

SDL3 owns the native handles and opens each controller once. `VEROSCRIPT_SDL3_CONTROLLERS=ON` (default) enables joystick/gamepad input; OFF removes native controller support while external normalized state remains available. Audio is separately selectable with `VEROSCRIPT_SDL3_AUDIO`; camera, SDL_Renderer and the separate haptic subsystem remain disabled. The backend preserves the host's background-input hints; actual availability and vibration depend on the OS/device/driver. Gyroscopes, controller touchpads, trackballs and advanced force-feedback effects are outside this interface. Native handles and active feedback retire on the owner thread, including after foreign-thread final release. Keep draining retirement before unloading. No version bump; rebuild hosts/providers/bindings/SDK consumers together.

`VeroScriptSdl3ControllerTests` exercises SDL virtual gamepads and raw joysticks, preconnected enumeration, hotplug, mapping transitions, independent state, normalization, rumble callbacks, invalid parameters, cleanup and owner-thread retirement. Memory-backend and SDL script tests verify the same public binding without requiring a physical controller. These tests do not certify every physical controller or mobile/Apple/browser device driver.

`VeroScriptSdl3OpenGl.cpp` uses the host-window path for `SDL_PLATFORM_ANDROID`, `SDL_PLATFORM_IOS` and `SDL_PLATFORM_TVOS`; it requires OpenGL ES and exactly one compatible Platform window, without a hidden auxiliary window. Android counts retained EGL contexts separately from surface presentation claims so replacement/cancellation preserves the old device until commit. UIKit keeps the exclusive context rule. Initialize Android through the host’s SDLActivity/`SDL_main` native event-thread integration; Java UI readiness is not native SDL owner-thread initialization. Other platforms still require application-main-thread initialization. Keep window, device, pumping and retirement operations on their required owner thread. Qualify SDK/device behavior separately.

[README](../../../../README.md) · [User](../../User/en-US/index.md)

Synthetic event tests bypass SDL's upstream deduplication.

`VEROSCRIPT_SDL3_GRAPHICS` is derived from the enabled graphics drivers; configure those driver options rather than overriding this derived value.

SDL_GPU renderers reuse retired vertex/index buffer pairs within 128 pairs / 8 MiB, further bounded by MaxGeometryBytes. Upload storage is reused; both transfer mapping and destination uploads cycle storage still referenced by queued GPU work. Renderer closure releases idle storage, while outstanding geometry keeps its existing ownership and owner-thread retirement rules.

<a id="uikit-recovery-capability"></a>

Keep `Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities` aligned with retained-context ownership: under SDL_PLATFORM_IOS/SDL_PLATFORM_TVOS, only the opengles driver clears bDeviceRecovery. Do not infer recovery from the provider ID or the other seven enabled capabilities. UIKit cannot create the replacement alongside its old EAGL owner; Android’s independently retained contexts use a different contract. Runtime capability reporting and the public recovery documentation must change together.

<a id="native-provider-maintenance"></a>

The provider source owns SDL-specific service descriptors and named static query `veroscript_sdl3_query`; native-extension builds expose `veroscript_extension_query`. Register only the services and graphics drivers included by the build, checking each public contract and actual capabilities independently. Keep shader inputs, driver dependencies and provider leases with SDL3 rather than the backend-independent contracts. Validate static/dynamic discovery, input routing, graphics, audio and owner-thread retirement against matching consumers; synthetic or dummy-driver checks do not certify physical devices.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
