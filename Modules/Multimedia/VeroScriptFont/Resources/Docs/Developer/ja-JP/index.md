# 保守: VeroScriptFont

## 所有範囲と検証

このモジュールは フォント、シェーピング、テキストレイアウトの契約 を管理します。公開インターフェースは [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h)、モジュール固有の入力は [`Resources/Tests`](../../../Tests) にあります。

共通のプロバイダー境界、再ビルド要件、検証、法的リソースの扱いは [マルチメディアの保守](../../../../../../../Resources/Docs/Developer/ja-JP/multimedia.md) で管理します。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ja-JP/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)

<a id="font-layout-maintenance"></a>

不変レイアウトは文字列と選択サービスを保持します。同期された行解析キャッシュは最大四つの不変解析を保ち、スタイル断片は交差する論理ランを選択します。検証済みレイアウトが同じバッファと長さを所有する場合だけ UTF-8 検証を省略します。フォールバックではクラスタ範囲検証とグリフ順序を保持します。`VeroScriptFontContractTests` と `VeroScriptFontLayoutContractTests` は具象提供者なしのモックを使い、コーパスとスクリプト試験は FontBackend が所有します。バインディングは Core 内部テストヘッダーに依存せず VM を明示選択します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
