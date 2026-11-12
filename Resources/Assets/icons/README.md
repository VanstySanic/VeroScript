# VeroScript icon assets

> **Documentation:** [English](../../Docs/User/en-US/index.md) · [Français](../../Docs/User/fr-FR/index.md) · [日本語](../../Docs/User/ja-JP/index.md) · [한국어](../../Docs/User/ko-KR/index.md) · [简体中文](../../Docs/User/zh-CN/index.md) · [繁體中文](../../Docs/User/zh-TW/index.md)

The default VeroScript application icon uses one symmetric `V`
with a solid bright blue fill (`#60A5FA`). The canvas outside the mark is fully
transparent. The arms follow gentle curves and taper from broad upper ends.
Both the inner valley and the outer lower tip meet at sharp corners.
The [source, application and package file icons](../file-associations/README.md)
use the same `V` outline, scaled uniformly into their file silhouettes,
with a muted blue fill (`#5D75AE`). Editor icons share this outline and keep
their light- and dark-theme colors and native alignment.

PNG resources are provided at 16, 24, 32, 48, 64, 128, 256, 512, and 1024
pixels. `veroscript-icon.ico` contains the 16, 24, 32, 48, 64, 128, and 256
pixel PNG representations for Windows consumers. `veroscript-icon.icns`
provides 16 through 1024 pixel representations for macOS consumers.

`veroscript-icon.svg` is the vector resource. Keep the exterior transparent
when deriving additional formats or sizes.

The Windows CLI, Runtime, launcher, LSP and DAP executables embed the product
ICO through the shared CMake helper. Windows builds of these tools require
a resource compiler. The installer executable and window use the ICO and PNG
resources, and the macOS installer application bundle uses the ICNS.

## Regeneration

In a source checkout, `Resources/Tools/Assets/generate-icons.cjs` maintains
one shared `V` outline, the separate palettes and file silhouettes.
It regenerates the brand, `.vs` source, `.vsapp` application and `.vspkg`
package SVG, PNG, ICO and ICNS files, plus the ten `V`-bearing SVGs in
the VSCode and JetBrains editor modules. GUI window frames and the other
editor symbols retain their own designs. Run the command from the source
checkout root. Node.js and the development-only `sharp` package are required:

```sh
node Resources/Tools/Assets/generate-icons.cjs
```

To regenerate only the product icon, leaving file and editor icons untouched,
pass `--brand-only`:

```sh
node Resources/Tools/Assets/generate-icons.cjs --brand-only
```

If `sharp` is installed outside the source tree, pass
`--sharp-module /absolute/path/to/sharp` to the command. Icon generation is
independent of building or running VeroScript.

