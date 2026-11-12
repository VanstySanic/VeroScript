# Python パッケージ、リフレクション、ローカライズ

## パッケージ種別とビルド結果

[相互運用 SDK — パッケージとホットリロード](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#package-container-format) を参照してください。

`PackageKind.APPLICATION` は実行可能パッケージを、`PackageKind.LIBRARY` は
ライブラリ パッケージを作成します。`Session.build_package_file(...)` は
`(data, RunResult)` を返すため、`Package(data)` を作る前に
`result.success` と診断を確認してください。`Package.from_file(path)` は
ファイルを解析し、`package_kind` が種類を示します。解析で検証されるのは
コンテナー構造であり発行者の真正性ではありません。信頼できないバイト列を
ロードする前に `canonical_signed_bytes` への署名を検証してください。

解析済みの `Package` は不変のスナップショットです。別のバイト列を読み込む場合は
新しいオブジェクトを作成してください。`__init__` の再呼び出しは `veroscript.Error` になります。

## 署名対象とカタログ

公開プロパティ `canonical_signed_bytes` は署名の対象となる正規化済み
バイト列を返します。`merge_catalog_directory` は runtime のカタログを
置き換えずにアプリケーションのカタログを追加します。

`Package.from_file()` はパッケージを読み込み、`package_kind` は種類を示します。セッションの `load_package`、`reload_package`、`build_package_file` でロード、再ロード、生成を行います。パッケージはロード境界でありセキュリティサンドボックスではありません。

`reflection()` は関数、型、メソッド、フィールド、引数、メタデータを返し、`bindings()` は現在のホストバインディングを返します。

`set_language`、`get_language`、`available_languages` が言語を管理し、`merge_catalog_directory` は外部カタログを統合します。信頼できる配布物だけを使い、キーと書式引数を検証してください。言語は診断と書式を変えますが、スクリプトの意味は変えません。

`ABI_VERSION`、`RUNTIME_VERSION`、`CAPABILITIES` を使ってロードされた runtime を識別できます。パッケージ、リフレクション、JIT 診断、ホットリロード、実行ガバナンスなどのオプション機能に依存する前に機能を確認してください。実行ガバナンスには協調的な命令/時間制限、キャンセル、標準ライブラリ機能ポリシーが含まれます。性能を評価するときは、起動、hot call、パッケージ、キャッシュ、Python 変換、GIL と callback のコストを分けて測定してください。JIT 統計は診断情報であり、`direct_native_calls` は VM の値ブリッジを通らず別のネイティブ エントリに到達した世代単位の JIT 呼び出し数です。

## 返されるオブジェクト参照

`enumerate_object_references(instance)` はスクリプトオブジェクトの `Value` を受け取り、セッションがアイドルの間に作成スレッドから呼び出します。戻り値は `ExternalReference` のタプルです。`kind` は `ExternalReferenceKind.SCRIPT_OBJECT` または `ExternalReferenceKind.HOST_OBJECT` で、`value` は複製された `Value` ラッパーを所有します。コピーするのは参照であり、オブジェクトの同一性は保たれ、グラフ全体の複製ではありません。タプルと値は列挙後も保持でき、借用コールバックビューではありません。通常の値、セッション、破棄とスレッドの規則は引き続き適用されます。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
