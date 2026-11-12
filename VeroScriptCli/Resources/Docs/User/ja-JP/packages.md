# パッケージと配布

VeroScript は、他のスクリプトによってインポートされた実行可能なアプリケーションとライブラリに対して個別のパッケージ形式を使用します。

|フォーマット |目的 |
| --- | --- |
| `.vsapp` |実行可能なアプリケーション パッケージ。 |
| `.vslib` |他のスクリプトによってインポートされたバイナリ ライブラリ パッケージ。 |
| `.vsbc` |配布パッケージではなく、バイトコード キャッシュ ファイル。 |
| `.vspkg` / `.zip` | ルートに `veroscript-package.json` を持つ ZIP インストール配布物。拡張子の大文字と小文字は区別しません。 |

## アプリケーション パッケージの構築

`--emit-package` を使用して出力ファイルを選択します。

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

通常、アプリケーション パッケージには、そのエントリ モジュールとそのモジュールの依存関係が含まれています。次のように実行します。

```powershell
veroscript DemoApp.vsapp
```

## ライブラリ パッケージのビルド

ライブラリ ソースはパブリック API を明示的にエクスポートする必要があり、通常はアプリケーション エントリ ポイントを提供しません。

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

他のスクリプトは、通常の `import` 構文を使用してライブラリをインポートします。

```VeroScript
import "DemoLibrary.vslib";
```

`export` のない宣言はモジュールに対してプライベートのままであり、パッケージ API の一部ではありません。

## パッケージオプション

|オプション |目的 |
| --- | --- |
| `--package-file <file>` |ソース モジュールをパッケージに追加します。オプションは繰り返し指定できます。 |
| `--package-version <N>` |パッケージのバージョンを設定します。 |
| `--strip-package-debug` |ソースファイルとデバッグラインテーブル情報を削除します。 |
| `package inspect <package>` | `.vsapp` または `.vslib` パッケージを検査します。 |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## ソースおよびバイナリ パッケージ

開発中に `.vs` ソース モジュールを直接インポートし、配布用にライブラリを `.vslib` にコンパイルします。どちらの形式でも同じモジュール可視性ルールを使用しますが、バイナリ パッケージは、ランタイムおよびそれを使用するバインディングとの互換性を維持する必要があります。

## 拡張パッケージのインストール

オプションのパッケージ管理は、既定でコマンド起動ディレクトリのプロジェクトにインストールします。`--path` は別のプロジェクトを指定し、`--root` は CLI に属する検証済み Runtime/Core SDK、`--root --path` は別の検証済みコンポーネントを選択します。相対パスの基準は起動ディレクトリです。Runtime は `Packages`、プロジェクトと SDK は `.veroscript/packages` を使用します。実行・検査・eval・REPL はまずプロジェクトを検出し、存在しない場合は CLI 自身のコンポーネントのみを使用します。ユーザー単位のインストールや暗黙の環境統合はありません。AOT も対応範囲内で同じ選択規則を使います。

拡張配布物は標準 ZIP データを使用し、`.vspkg` と `.zip` の大文字と小文字を区別しません。`pack` の既定ファイル名は `<名前>-<バージョン>.vspkg` で、`.zip` の明示指定も可能です。`install` と `publish` は同じ内容とルートの `veroscript-package.json` を検証します。破損や無効なアーカイブは改名しても拒否されます。ダウンロード URL に拡張子は不要で、ハッシュとオフラインキャッシュの再利用は実際のバイト列に基づきます。

```sh
veroscript pack ./library
veroscript pack ./library --output ./library.zip
veroscript pack ./library --output ./library-0.1.1.vspkg
veroscript publish ./library-0.1.1.vspkg
veroscript install ./library.vspkg
veroscript install ./library.zip --path "./My App"
veroscript install ./library.vspkg --root
veroscript install ./library.vspkg --root --path "D:/VeroScript/SDK/Core/0.1.1"
veroscript --path "./My App" "./My App/main.vs"
```

[拡張パッケージのインストールと管理](../../../../../VeroScriptPackageManager/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
