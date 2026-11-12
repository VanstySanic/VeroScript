# Android — 実験的なモバイル埋め込み

実験的 AAR の対象は `arm64-v8a` と `x86_64`、API 26 以上で、VM と NDK `c++_shared` を使います。AAR と互換なネイティブ依存をアプリへ追加します。`assets/veroscript/stdlib`、C ABI Prefab ヘッダー、ライセンスを含み、アプリのスクリプトは `src/main/assets/scripts` に置きます。`scripts/helper.vs` は `/veroscript/scripts/helper.vs` に対応します。生成・実行・終了はワーカーで行い、`requestCancel()` 用に engine を保持します。`executeAsset` または `execute(source, absoluteLabel)` を使います。毎回新規状態で並行実行は禁止です。`close()` は終了を待ち繰り返せますが自身のコールバックから同期呼び出ししません。UTF-8 バイト配列は NUL と補助文字を保持し、`outputTruncated` を検査します。

SDK の [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) 型をインポートします。次のメソッド内の断片はワーカースレッドで実行し、Android の `context` が利用できることを前提とします。

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[実験的なモバイル埋め込み](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md)

## モバイル言語の明示設定

[モバイル言語の明示設定](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md)

## 管理バイト領域の上限

Android は完全な `VeroScriptEngine` コンストラクターの末尾 `maxBufferBytes` / `maxRetainedBytes` で設定します。Java `long` は uint64 のビット列を運び、通常は正のバイト数を使います。 既定は 16 MiB / 32 MiB、0 は無制限です。

## 実行制御付きネイティブ実行と静的 AOT

[実験的なモバイル埋め込み — 実行制御付きネイティブ実行と静的 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ja-JP/index.md#mobile-jit-selection) を参照してください。

<a id="android-native-selection"></a>

Android は `ExecutionBackend`、`jitBackendPath`、アプリケーションが作成する `AotPrograms` カタログ、`aotProgramId` を受け付けます。生成アプリケーションには `executePackage` を使い、ネイティブ初期化には `VeroScriptAndroid.h` を使います。カタログはプログラムを保持し、生成エントリーコードは静的リンクを維持します。ID に関数アドレスを入れてはいけません。

<!-- docs-navigation -->

---

[ユーザー文書](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
