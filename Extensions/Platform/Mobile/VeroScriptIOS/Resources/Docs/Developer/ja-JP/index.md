# iOS — 開発者文書

iOS は Objective-C/Objective-C++ アダプターと静的 XCFramework 統合を管理します。UI 配信とライフサイクルはアダプター／ホストに置き、各 slice の実機／シミュレーター識別を保持します。

共通の組み込みビルド、寿命、パッケージ出力、ローカライズの担当範囲は [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md) を参照してください。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS は macOS/Xcode、三 slice と全オブジェクトの `LC_BUILD_VERSION`/CPU/最低 iOS 検査が必要で、macOS や混合アーカイブを拒否します。実 Objective-C/Swift リンク、署名、バックグラウンド／復帰、取消、測定は公開条件です。

[ユーザー文書](../../User/ja-JP/index.md)


## モバイルリソースと言語

共通モバイルカタログの一覧、配備パス、明示ロードの手順は [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md#mobile-localization) で管理します。

翻訳バッファーの所有権、エラー、エクスポートの保守は [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/abi-and-lifetime.md#localization-abi) で定義します。

<a id="ios-acceptance-evidence"></a>

ソース／成果物の厳密な識別、Xcode／SDK 版、配備先の最小 OS、実行 OS、実機／シミュレーターのアーキテクチャごとに証拠を維持します。CMAKE_SYSTEM_NAME=iOS と iphoneos／iphonesimulator の対応 sysroot で実機 arm64、シミュレーター arm64／x86_64 を個別ビルドし、組込みでは静的 Interop を使います。統合アーカイブと XCFramework を確認してから Objective-C++、Swift、C ABI、静的 AOT アプリで利用します。実アプリで import、NSError／ファクトリ橋渡し、Bundle 資源、書込み可能キャッシュの再利用、権限拒否、取消、小さいスタックでの解析と復旧、ARC、バックグラウンド／復帰を確認します。実機 slice のコンパイルは実機実行ではなく、シミュレーターの署名や画像は実機署名・Metal・性能を証明しません。iOS 15.0 向けコンパイルは iOS 15 での実行検証を意味しません。記録とソース差分は日付付きの現行マニュアルではなく一時証拠へ保存し、既存のシミュレーター記録で後のビルドを自動承認してはいけません。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
