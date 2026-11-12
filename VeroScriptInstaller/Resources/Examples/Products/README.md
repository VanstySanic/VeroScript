# Installer product definitions

[User and Developer documentation](../../Docs/README.md)

> **Developer documentation:** [English](../../Docs/Developer/en-US/index.md) · [Français](../../Docs/Developer/fr-FR/index.md) · [日本語](../../Docs/Developer/ja-JP/index.md) · [한국어](../../Docs/Developer/ko-KR/index.md) · [简体中文](../../Docs/Developer/zh-CN/index.md) · [繁體中文](../../Docs/Developer/zh-TW/index.md)

Product definitions describe identity, default installation location, components,
selection defaults, dependencies, PATH integration and payload validation. They
are build-time inputs; the bundle tool copies the normalized definition into the
validated installer manifest consumed by the generic UI and engine. Payload
hash verification is built in; Windows executable signing is a separate,
optional packaging step, not a signature on the manifest itself.

Use `custom-product.example.json` as the starting point for an externally defined
product. Component payload sources are supplied with `--source-<component-id>`.

For a custom product, build and verify an expanded installer bundle with the
Tool. The example below runs from the repository root with the .NET 9 SDK and
runtime. It supplies the required `core` payload; its definition requires
`product.dat`. The Tool omits the optional examples component and filters auxiliary documentation content.

```powershell
New-Item -ItemType Directory -Path .tmp/custom-product-payload -Force | Out-Null
Set-Content -LiteralPath .tmp/custom-product-payload/product.dat -Value 'Sample product payload' -Encoding utf8
$tool = './VeroScriptInstaller/Tool/VeroScriptInstaller.Tool.csproj'
dotnet run --project $tool -c Release -- build-bundle --output .tmp/custom-product-bundle --version 0.1.1 --definition ./VeroScriptInstaller/Resources/Examples/Products/custom-product.example.json --source-core .tmp/custom-product-payload
dotnet run --project $tool -c Release -- verify --bundle .tmp/custom-product-bundle
```

The output contains `manifest.json` and `payload/core/product.dat`. This step does
not install the product. Follow [single-file app publishing](../../Docs/Developer/en-US/build-and-bundle.md)
to embed the verified bundle in the generic App. The PowerShell business
publisher accepts configured VeroScript installer businesses; an arbitrary
`-InstallerName SampleProductInstaller` is not a supported custom-product entry.

Use `-Profile runtime`, `core-sdk`, `interop`, `cpp`, `csharp`, `python`
or `web` instead of `-Definition` for a built-in preset. Only components present
in `ComponentSources` are embedded; required components must be supplied, while
optional components can be omitted entirely. Selection defaults and dependencies
come from the definition and are enforced again by the installer engine.

Each product needs a stable, globally unique `productId`. Changing it creates a
different install/update lifecycle. Keep it stable across versions of the same
product. Component ids and installation directories are stable ownership
boundaries: update, repair and uninstall operate only on the files recorded for
those components.

## Module distribution boundary

`InstallerProductDefinition.ReleaseGroup` is serialized as `releaseGroup`.
The canonical value `Modules` marks a functional module distribution and is
rejected by bundle/manifest producers. Jit and future Modules use archive
packages, not new standalone installers. Historical product definitions may
remain readable for existing records; their presence does not grant publication
eligibility. Use the central release routing policy for official products.
