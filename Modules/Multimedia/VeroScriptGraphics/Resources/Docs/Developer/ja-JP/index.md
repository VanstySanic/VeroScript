# VeroScriptGraphics

[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md#provider-boundary) を参照してください。

## ネイティブ契約のビルド

リポジトリのルートで次のコマンドを実行すると、スクリプトアダプターを含まない公開契約をビルドできます。スクリプトの実行にはプロバイダーと、対応する RegisterScriptBindings アダプターを互換性のある Core に登録するホストも必要です。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## 保守と検証

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md)

[README](../../../../README.md) · [User](../../User/ja-JP/index.md)

## 任意のネイティブ送信スコープ

IRenderSubmission2D は IRenderer2D から dynamic_cast で取得する任意のネイティブインターフェースで、既存の仮想テーブルやスクリプト API を変更しません。SDL_GPU が実装し SDL3 モジュールも転送します。BeginSubmission は入れ子を拒否し、EndSubmission は同じレンダラーの連続パスを送信します。CancelSubmission は何度でも呼び出せ、失敗や例外でも後始末に使います。所有スレッドで呼び出し、終了までリソースを開いたまま保持します。別レンダラーが出力を消費する前にスコープを終了してください。スコープ内の RenderSurface の bPresented は画像取得を表し、EndSubmission の成功は送信のみを確認します。GPU 完了や画面表示は保証しません。スワップチェーン画像の取得後に取り消す場合も、プロバイダーの必須送信規則に従います。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
