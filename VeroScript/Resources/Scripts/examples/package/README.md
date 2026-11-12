# `.vslib` library and `.vsapp` application

> **Documentation:** [English](../../../../../VeroScriptCli/Resources/Docs/User/en-US/packages.md) · [Français](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/packages.md) · [日本語](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/packages.md) · [한국어](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/packages.md) · [简体中文](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/packages.md) · [繁體中文](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/packages.md)

Run these commands from this directory with `veroscript` on `PATH`:

```text
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoLibrary.vslib
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

The application prints two `42` lines and `Operation(1)`. The library demonstrates
cross-package exported enums, interfaces, structs, open generics, functions, and globals.
`hidden_value` is intentionally absent from the public surface.

`DemoApp.vsapp` contains a closed linked VM image, so it does not need
`DemoLibrary.vslib` beside it at run time. Keep the library package available while
compiling source that imports it. Use `export import "Other.vslib";` in a library
facade when its dependency should also be visible to downstream importers.
