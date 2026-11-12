# VeroScriptGraphics

See [VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md#provider-boundary).

## Native contract build

Run these commands from the repository root to build the public contracts without script adapters. Executing scripts also requires a provider and a host that registers the corresponding RegisterScriptBindings adapters with the matching Core.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## Maintenance and verification

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md)

[README](../../../../README.md) · [User](../../User/en-US/index.md)

## Optional native submission scopes

IRenderSubmission2D is an optional native interface, queried from IRenderer2D with dynamic_cast; it does not change the existing renderer vtable or script API. SDL_GPU supports it and the SDL3 module forwards it. BeginSubmission rejects nesting; EndSubmission submits consecutive passes on that renderer. CancelSubmission is idempotent and must also be used on failure or exceptions. All calls stay on the owner thread, and resources remain open until the scope ends. End the scope before another renderer consumes its output. During a scope, RenderSurface's bPresented reports image acquisition; successful EndSubmission confirms submission, not GPU completion or display. Cancellation after acquiring a swapchain image still follows the provider's mandatory submission rules.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
