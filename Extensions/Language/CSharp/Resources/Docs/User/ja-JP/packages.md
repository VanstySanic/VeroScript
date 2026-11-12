# C# パッケージ、リフレクション、ローカライズ

## パッケージのメタデータ

[相互運用 SDK — パッケージとホットリロード](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#package-container-format) を参照してください。

`GetMetadata()` は所有されたメタデータを返し、`HasCompiledModule` は
モジュールを転送できるかを示します。`GetCanonicalSignedBytes()` は署名対象の
バイト列を返します。カタログは `VeroScriptRuntime.LoadCatalogDirectory` または
`VeroScriptRuntime.MergeCatalogDirectory` で読み込み・マージします。

`VeroScriptPackage.Read` でパッケージバイト列を解析します。`LoadPackage`、`ReloadPackage`、`BuildPackageFile` がパッケージを扱い、`VeroScriptPackageKind` と `VeroScriptPackageMetadata` が種類とメタデータを表します。パッケージは転送とロードの境界ですが、サンドボックスではありません。

`ExportReflectionJson` はスクリプトの型、関数、フィールド、メソッド、引数を、`ExportBindingsJson` はホストバインディングを出力します。反射情報はツールや検証に使えますが、実行時の権限確認の代わりにはなりません。`EnumerateObjectReferences` の値は借用規則に従います。

`VeroScriptRuntime` は現在の言語、利用可能言語の JSON、カタログのロードと統合を提供します。`GetAvailableLanguagesJson` で実際に組み込まれた BCP 47 タグを確認してください。言語は診断と書式だけを変更します。

`VeroScriptRuntime.Capabilities` を確認して JIT、パッケージ、反射、reload、シリアライズ、実行ガバナンスを利用してください。実行ガバナンスには協調的な命令/時間制限、キャンセル、標準ライブラリ機能ポリシーが含まれます。性能は起動、hot call、キャッシュ、値変換、callback を分けて測定します。

`VeroScriptRuntime.AbiVersion` はアダプターの ABI `0x00000101`（0.1.1）と完全に一致する必要があります。マネージド パッケージ、ネイティブ Interop ライブラリ、ランタイム カタログは対応するリリースから配置してください。

## モジュールの所有権と署名

`LoadPackage` または `ReloadPackage` が成功すると、コンパイル済みモジュールはセッションへ転送されます。メタデータと正規化した署名対象バイト列は引き続き読めますが、同じインスタンスからモジュールを再転送できません。もう一度転送するには元のバイト列を再解析してください。`Read` はコンテナ形式を検証し、発行者の真正性は検証しません。信頼できないパッケージをロードする前に、`GetCanonicalSignedBytes()` に対する署名を検証してください。`BuildPackageFile` はパッケージのバイト列と `VeroScriptRunResult` を返し、オプションでアプリケーション／ライブラリ、名前、バージョン、メタデータ、デバッグ情報の除去を設定します。

`EnumerateObjectReferences` の値はコールバック中の借用値です。コールバック終了後に保持しないでください。プロセス全体の言語をいつ変更できるかを定め、並行処理の診断言語が混在しないようにしてください。SDK 自身の引数・寿命チェックで発生する例外は英語の場合があります。言語選択はランタイム診断に適用されます。

[C# の値と所有権](values.md#managed-call-lifetime) を参照してください。

## 診断言語の選択

選択したタグを `VeroScriptRuntime.CurrentLanguage` に設定します。選択肢を提示する前に `VeroScriptRuntime.GetAvailableLanguagesJson()` で実際に利用できるタグを取得してください。このプロパティはプロセス全体の診断言語を変更するため、他のセッションと切り替えを調整します。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
