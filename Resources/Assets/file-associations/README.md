# VeroScript file icons and source-file integration

> **Documentation:** [English](../../../VeroScriptInstaller/Resources/Docs/User/en-US/index.md) · [Français](../../../VeroScriptInstaller/Resources/Docs/User/fr-FR/index.md) · [日本語](../../../VeroScriptInstaller/Resources/Docs/User/ja-JP/index.md) · [한국어](../../../VeroScriptInstaller/Resources/Docs/User/ko-KR/index.md) · [简体中文](../../../VeroScriptInstaller/Resources/Docs/User/zh-CN/index.md) · [繁體中文](../../../VeroScriptInstaller/Resources/Docs/User/zh-TW/index.md)

This directory supplies file icons and editor-independent `.vs` document
integration templates. `.vs` identifies editable VeroScript source text.
These editor templates open source for editing and must not execute it. The
Windows Runtime installer separately offers optional `.vs` and `.vsapp` run
handlers; choosing one explicitly opts into running files with that Runtime.
Both are disabled by default. Do not reuse an editor's ProgID or this editor
template as the Runtime run-handler definition.

VeroScript does not currently designate an official IDE handler. The operating
system owns the user's default-app choice, and users may select any compatible
editor. A future IDE or installer may consume these templates to advertise
itself as an available handler, but registration must not replace an existing
default unless the user explicitly requests that change.

## File icon family

- `icons/veroscript-source.*` uses a folded document for `.vs` source files.
- `icons/veroscript-app.*` uses an application window for `.vsapp` runnable
  application containers.
- `icons/veroscript-package.*` uses a sealed carton for `.vspkg` installable
  distributions. `.vspkg` contains ZIP data with `veroscript-package.json` at
  its root; this icon represents that existing format.

The file icons use the [application icon's](../icons/README.md) shared `V`
outline, scaled uniformly into their neutral file silhouettes. The mark
keeps the file icons' muted blue fill (`#5D75AE`), tapered arms and pointed
inner and outer joins. Their SVG, PNG, ICO and ICNS resources are regenerated
together by `Resources/Tools/Assets/generate-icons.cjs` in a source checkout.
The integration templates below describe `.vs` source files.

## Stable type identity

- extension: `.vs`
- MIME type: `text/x-veroscript`
- macOS UTI: `dev.veroscript.source`
- Windows ProgID: supplied by each consuming editor and unique to that
  application, for example `Vendor.Editor.VeroScript`

## Platform resources

- `windows/VeroScript.Source.reg.in` is a parameterized, per-user registry
  template; its filename describes the source type and is not a ProgID. A future
  editor must replace every placeholder, use its own application-specific
  ProgID, and may register it under Open With; it must not write Windows'
  protected `UserChoice`.
  Render the final `.reg` as UTF-16 LE before importing it with Registry Editor.
- `linux/dev.veroscript.Source.xml` defines the shared MIME type.
  `veroscript-editor.desktop.in` is a neutral desktop-entry template for a
  future editor. Selecting the default remains a separate `xdg-mime` action.
- `macos/Info.plist.document-types.xml` is a neutral document-type fragment for
  a future `.app` bundle. The localized `InfoPlist.strings` files provide the
  document display name. Finder remains responsible for default-app choice.

All pixels outside each icon silhouette are fully transparent. PNG
resources are supplied at 16, 24, 32, 48, 64, 128, 256, 512, and 1024 pixels;
Windows uses the multi-resolution ICO and macOS uses the ICNS.

These resources ship unpacked under
`Release/Resources/any/<version>/Resources/Assets/file-associations/`.
The installers do not register these editor templates. The Windows Runtime
installer constructs its own per-user run registrations and an independent,
optional `.vspkg` icon registration from the installed Runtime's `assets/icons/`
files. Default apps remain a separate user choice in Windows; the `.vspkg` icon
option does not introduce a package-install command.
