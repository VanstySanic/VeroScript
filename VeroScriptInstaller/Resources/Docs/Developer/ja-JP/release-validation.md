# 公開の検証手順

唯一の[パッケージ化規約](../../../../../Resources/Docs/Standards/ja-JP/PACKAGING.md)に従う。旧配置との互換や変換は提供せず、旧出力と不要候補を清掃して再構築する。製品バージョンは登録情報に従います。

## 検証段階

予定するソース・設定と対象プロファイルを確定し、生成器の前提は[ビルドとバンドル](build-and-bundle.md)で確認します。候補の一覧を検証してから、対象上で必要な利用側、アプリ、インストーラーのライフサイクルを実行します。クロスコンパイルだけでは実行合格になりません。署名などのバイト変更後の最終ハッシュに結果を結び付けます。

## 一覧と現在のソースの検査

以下はリポジトリのルートから既存の集合を監査する例です。`win-x64` は実際の利用対象に置き換えます。共有の `any`/`wasm32` 索引だけではネイティブ・モバイル配布の完全性を証明できません。

```text
python Resources/Tools/Packaging/verify-veroscript-release.py --release-directory Release --target win-x64 --require-complete --check-source
```

`--require-complete` は対象の必須製品集合を確認し、部分製品を拒否しますが、実行・端末、GUI、インストーラーのライフサイクル、署名、公証の検証を実行・認証しません。`--check-source` は検証器が扱う公開ペイロードを現在の正本と比較しますが、そのソースからバイナリを再ビルドした証明ではありません。省略するのは、過去の有効な新形式集合の記録済み契約を監査する場合です。`--version` は選択されたすべての製品参照にその実版を要求し、集合共通の版ではありません。

```powershell
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -ReleaseDirectory Release -Target win-x64 -RequireComplete -CheckSource -RequireSignedInstallers
```

## 検証証拠と最終確認

`passed`、`failed`、`not-run`、`not-applicable` を理由、ソース識別、ツール、設定、対象・端末識別、最終ハッシュとともに記録します。クロスリンク、エミュレーター・シミュレーター、実機、C# AOT・トリミング、Python アプリ、インストール・修復・アンインストールは別記します。環境不足と未署名は未確認事項として残します。Windows ラッパーは `-RequireSignedInstallers` で Authenticode を要求できますが、署名の実行やライフサイクル検証の代わりにはなりません。

配布前に、実ビルドの成功とペイロード、対象識別、依存、マニフェスト、ハッシュの検査合格を要求します。環境があれば対象実行とインストーラーのライフサイクルを検証し、失敗・未実行をそのまま記録します。こうした未完了事項だけでは、構築済みの有効な成果物を配布対象から外しません。[一覧と検証記録](release-layout.md#release-qualification)で `complete` と `qualified` を区別します。配布後は最終一覧とハッシュを再確認し、入力変更時は影響する証拠を無効とします。規約だけの変更では以下の文書検査のみを行い、ビルドや製品パッケージ化は実行しません。

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

<a id="installer-download-validation"></a>

## インストーラーのダウンロードを静的に検証する

`veroscript_installer.py` はダウンロード名と対象アーキテクチャを検証し、.NET 単一ファイルのバンドルを読んで、内蔵パーツをアセンブリに記録された SHA-256 とパーツ数に照合します。別ターゲット向けの組み立てでは、準備段階で検証した ZIP のダイジェストとも比較します。Linux では `.tar.gz` 内の実行ファイルが 1 つであり実行権限を持つこと、macOS では `.dmg` 内のアプリと内蔵ペイロードを確認します。別 OS から DMG を調べるには `VEROSCRIPT_7ZIP` または PATH 上の 7-Zip が必要です。対象環境での実行とインストールのライフサイクル検証は別に記録します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/release-validation.md) · [fr-FR](../fr-FR/release-validation.md) · [ja-JP](release-validation.md) · [ko-KR](../ko-KR/release-validation.md) · [zh-CN](../zh-CN/release-validation.md) · [zh-TW](../zh-TW/release-validation.md)
