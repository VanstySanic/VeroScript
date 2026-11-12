# Installer automation

For maintainers and deployment-tool authors. Run Tool commands from a source checkout with the .NET 9 SDK. An installer bundle is not a portable runtime archive. The graphical installer’s end-user operations are documented in the [installation guide](../../User/en-US/index.md).

## Development and automation entry point

The Tool uses an expanded installer bundle containing `manifest.json` and `payload/`, not a regular Runtime ZIP. The following example targets the runtime profile; `--root` is the product root:

This example demonstrates a complete lifecycle: verification, initial installation, relocation, repair and uninstall. Run only the steps you need. It selects only `runtime`, so it also works with a bundle prepared using only `--source-runtime`. Add `examples` to `--components` only if the bundle manifest includes that component; during an update, omitted optional components are removed. `--root` is the product root, and the Runtime component is installed under its `Runtime` subdirectory.

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

`--add-to-path` is a value-less Tool switch and must be explicitly supplied when wanted. Omitting it during update or relocation clears the PATH entry owned by the previous installation. Downgrades are rejected by default; add `--allow-downgrade` only when genuinely required.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](automation.md) · [fr-FR](../fr-FR/automation.md) · [ja-JP](../ja-JP/automation.md) · [ko-KR](../ko-KR/automation.md) · [zh-CN](../zh-CN/automation.md) · [zh-TW](../zh-TW/automation.md)
