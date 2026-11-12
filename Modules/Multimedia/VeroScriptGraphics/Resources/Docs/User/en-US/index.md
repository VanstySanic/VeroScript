# VeroScriptGraphics — Resources and 2D drawing

<a id="graphics-service-contract"></a>

Graphics exposes devices, textures, samplers, indexed geometry and Renderer2D through VS::Graphics. Public native contracts contain no SDL/Core types. The module and its native service contract both use version 0.1.1. Rebuild hosts and providers together.

Script resource, capability and size queries, plus completed-request results (`GetDevice` and `GetPixels`), are const methods and work with read-only value captures. Drawing, updates, request polling/cancellation and resource closure require a writable receiver. `GetError` also polls its request; use `mutable` or a writable reference capture for these active operations.

Geometry is uploaded once and reused. Each script vertex contains eight doubles: X,Y,U,V,R,G,B,A; indices are checked int64 values. UpdateTexture accepts packed array<uint8> RGBA. CreateSampler selects Filter and U/V AddressMode. DrawList2D snapshots SetTexture, SetTint, SetTransform and SetBlendMode at AddGeometry; PushClip intersects rectangles and PopClip restores the prior clip.

Coordinates use viewport-local drawable pixels, Y downward; clips use absolute target pixels. SetTransform takes sixteen column-major doubles. GUI layout maps to pixels using Platform DisplayScale. Vertex colors are linear RGBA, straight by default; tint is always straight. StraightAlpha samples straight textures; PremultipliedAlpha samples premultiplied textures. Translucent layers drawn over transparent black store premultiplied pixels. sRGB conversion follows texture/target formats.

RenderTexture and RenderSurface take target,list,clear,R,G,B,A; Viewport variants append x,y,width,height. Clear affects the whole target. Initialize textures before loading prior contents; window surfaces require clearing. Swapchain unavailability succeeds without presenting. Close geometry, samplers, textures, surfaces and renderers before the device on its owner thread. Clear releases list references. Closed, foreign-device and feedback resources are rejected. Defaults: 256 MiB texture/aligned transfer, 64 MiB geometry, 65,536 commands/clip levels. Synchronous readback uses the C++ API; scripts use `Device.ReadTextureAsync`, then poll the request and read its pixels only after success.

```veroscript
VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
VS::Graphics::Device device = graphics.CreateDevice("");
VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
array<double> vertices = {0.0,0.0,0.0,0.0,1.0,0.0,0.0,1.0,
						 32.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,
						 0.0,32.0,0.0,1.0,1.0,0.0,0.0,1.0};
array<int64> indices = {0,1,2};
VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
list.PushClip(0,0,64,64);
list.AddGeometry(geometry);
list.PopClip();
VS::Graphics::Texture target = device.CreateTextureWithFormat(64,64,VS::Graphics::TextureFormat::Rgba8Unorm);
renderer.RenderTexture(target,list,true,0.0,0.0,0.0,0.0);
list.Clear();
geometry.Close();
target.Close();
renderer.Close();
device.Close();
```

SDL window surfaces require SDR_LINEAR composition for consistent linear blending; an unsupported composition fails surface creation.

## Use the installed native SDK

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode selects VertexColorMode::Straight (default) or Premultiplied independently of Blend. Already premultiplied vertex RGB remains premultiplied during interpolation; tint remains straight, and PremultipliedAlpha output scales RGB by tint alpha exactly once. AddGeometry snapshots this choice and Clear restores Straight. All vertex colors must still be linear; this setting does not decode sRGB.

## Composition and manual device recovery

Composition2D records AddDraw snapshots, BeginLayer/EndLayer, PushMask/PopMask, AddBlur, AddColorMatrix, AddShadow and AddBackdropBlur. Device.CreateCompositor2D() reuses temporary targets; CreateCompositor2DWithLimits(bytes,depth) configures their budget. Defaults are 256 MiB, 32 levels; the builder allows 64 levels and 65,536 commands/draws. Group opacity applies once to the completed layer; nested alpha masks intersect, including inverse masks. Blur uses two separable 13-tap passes, radius 0..64 pixels; shadows preserve original content and backdrop blur replaces only the masked region. Color matrices contain twenty row-major values. Intermediate and final composition pixels are premultiplied linear RGBA; the compositor premultiplies its straight ClearColor, and load mode requires premultiplied contents. Opaque draws must write compatible premultiplied pixels. Texture formats handle sRGB conversion. Invalid scopes/budgets fail before changing the final target. Trim releases pooled targets; close the compositor before the device. DrawList2D.SetAlphaMask, SetBlur, SetColorMatrix and ResetEffect expose single-pass effects; Additive adds premultiplied source channels. Compositor surface methods additionally take the current drawable width/height before the composition; Viewport variants append x,y,width,height. Failed presentation outputs false. GetPooledBytes returns int64. Device.GetGeneration and SupportsDeviceRecovery identify manual replacement support. Service.RecoverDevice(previous,driver) creates a replacement first and invalidates the previous generation only after success. Creation failure leaves that generation unchanged. After success, recreate/upload every asset, close old surfaces before reclaiming their windows, then close old resources and the old device. Resource Close remains valid after Invalidate. The selected driver can report device loss through Device.Poll; recovery and resource restoration remain explicit host actions. Renderer3D, arbitrary user shaders and host command recording are not supplied by this API.

Compositor temporary textures default to RGBA8 sRGB storage; sampling and attachment blending remain linear. This avoids losing dark sRGB colors through an 8-bit linear intermediate. C++ can explicitly select RGBA8 UNORM with its linear quantization; both script constructors use the sRGB default. ColorMatrix and AlphaMask honor Clamp/Repeat/Mirror sampling for negative UVs and UVs above one. Blur and Shadow use transparent sampling outside the source rectangle; their radius is measured in source pixels.

## Graphics API selection and asynchronous work

See [VeroScriptSdl3 — Foundation — Graphics API selection and asynchronous work](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/en-US/index.md#sdl-graphics-drivers).

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync return DeviceRequest; Device.ReadTextureAsync returns ReadbackRequest. Poll on the owner thread: Pending / Succeeded / Failed / Cancelled. GetDevice/GetPixels require success; GetError returns script diagnostics. Defaults may complete synchronously. Browser WebGPU rejects synchronous creation/readback/WaitIdle with graphics.async_required. Yield the browser loop between polls.

<a id="resource-retirement"></a>

Recovery invalidates the previous generation only after success; rebuild resources. Cancel discards results but native work may still complete. Close and release resources, devices and requests, then collect on the owner thread until Service.HasPendingRetirements() is false; yield browser frames. Keep a Platform owner service until cleanup ends.

<a id="graphics-capabilities"></a>

Capabilities belong to the selected device/driver. C++ uses `IDevice::GetCapabilities()`; scripts query `SupportsGuiBasic`, `SupportsTransforms`, `SupportsClipMasks`, `SupportsFilters` and `SupportsDeviceRecovery`. Check recovery support before requesting replacement and handle failure; a provider name alone is insufficient. The SDL3 restrictions are maintained in [UIKit device recovery](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/en-US/index.md#uikit-device-recovery).

Each script vertex’s eight doubles must fit finite float; int64 indices are checked before conversion to native uint32. `UpdateTexture` consumes exactly width × height × 4 packed RGBA bytes for the requested region and preserves their encoding. `ResetTexture` restores solid drawing. Drawing-list mutations and rendering use the owner thread and must not run concurrently. Native allocation exceptions can propagate through C++; script adapters translate callback failures into diagnostics. Readback returns stored RGBA bytes and preserves the supplied output on failure; blocking native readback/WaitIdle belong outside ordinary frame loops.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
