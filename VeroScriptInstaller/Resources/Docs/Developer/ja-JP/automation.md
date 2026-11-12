# インストーラーの自動化

保守担当者と配布ツール作者向けです。Tool はソースチェックアウトと .NET 9 SDK を使って実行します。インストーラーバンドルはポータブル Runtime アーカイブとは異なります。GUI の操作は[インストールガイド](../../User/ja-JP/index.md)を参照してください。

移設のためにファイルを手動で移動してはいけません。安全な更新または移設では、Tool に古いインストール記録を明示します。

この例は検証、初回インストール、移設、修復、削除の全手順を示します。必要な手順だけ実行してください。`runtime` だけを選択するため、`--source-runtime` のみで作成したバンドルでも使用できます。マニフェストに含まれる場合にのみ `examples` を `--components` に追加してください。更新時に省略した任意コンポーネントは削除されます。`--root` は製品ルートで、Runtime コンポーネントはその `Runtime` サブディレクトリに配置されます。

```powershell
$tool = '.\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj'
$bundle = 'D:\Bundles\runtime-installer-bundle'
dotnet run --project $tool -c Release -- verify --bundle $bundle
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScript' --components runtime
dotnet run --project $tool -c Release -- install --bundle $bundle --root 'D:\Apps\VeroScriptMoved' --existing-record 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json' --components runtime
dotnet run --project $tool -c Release -- repair --bundle $bundle --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
dotnet run --project $tool -c Release -- uninstall --record 'D:\Apps\VeroScriptMoved\Runtime\0.1.1\.veroscript-install.json'
```

開発用 Tool が使うインストーラーバンドルは `manifest.json` と `payload/` を含み、通常の Runtime ZIP ではありません。`--add-to-path` は値を取らない明示的なスイッチです。更新/移設時に省略すると、以前のインストールが所有する PATH エントリを削除します。ダウングレードは既定で拒否され、必要な場合だけ `--allow-downgrade` を使用します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/automation.md) · [fr-FR](../fr-FR/automation.md) · [ja-JP](automation.md) · [ko-KR](../ko-KR/automation.md) · [zh-CN](../zh-CN/automation.md) · [zh-TW](../zh-TW/automation.md)
