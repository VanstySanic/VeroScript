# 保守: VeroScriptFontBackend

## 所有範囲と検証

このモジュールは フォントのシェーピング、ラスタライズ、双方向テキスト解析のプロバイダー を管理します。公開インターフェースは [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h)、モジュール固有の入力は [`Resources/Tests`](../../../Tests) にあります。

共通のプロバイダー境界、再ビルド要件、検証、法的リソースの扱いは [マルチメディアの保守](../../../../../../../../Resources/Docs/Developer/ja-JP/multimedia.md) で管理します。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ja-JP/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)

<a id="native-provider-maintenance"></a>

任意の `VeroScriptFontBackendModule` はフォントとレイアウトのサービスを別々に登録します。静的ホストは `veroscript_font_backend_query`、動的拡張は `veroscript_extension_query` を使います。キャスト前に各サービスを Font の対応契約と照合します。保持した face と解析は元の登録ハンドルと独立して提供者 lease を保持します。FreeType/HarfBuzz/SheenBidi の入力、ライセンス、提供者ビルドはここで保守し、公開契約、既定フォントと独立モック試験は Font が所有します。

## プロバイダーのビルドと寿命

`VEROSCRIPT_BUILD_FONT_BACKEND` で本モジュールを選択します。`VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION` は対応する Core 用プロバイダーをビルドし、`VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER` は名前付き静的エントリを選びます。汎用インターフェイスは Font が所有し、具体的なバックエンドを探索しません。FreeType/HarfBuzz のフェイス、SheenBidi の解析、固定した依存ソースと法的表示は本モジュールで管理します。バックエンド ID が同じでも font と text-layout のサービス記述は独立です。

スクリプト回帰検証は対応する `VeroScriptBytecodeVm` を明示的にリンクし、公開ファクトリーを使います。レジストリのハンドルを解放した後にフェイス、解析、レイアウトスナップショットを個別に保持し、モジュールの lease を確認します。実行可能な検証ではネイティブ単独・Core 対応ビルド、依存分離、移動後の SDK 利用側を調べ、失敗と未検証の対象を記録します。

[フォントとレイアウトの公開契約](../../../../../../VeroScriptFont/Resources/Docs/User/ja-JP/index.md) · [GUI 利用側の保守](../../../../../../VeroScriptGui/Resources/Docs/Developer/ja-JP/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
