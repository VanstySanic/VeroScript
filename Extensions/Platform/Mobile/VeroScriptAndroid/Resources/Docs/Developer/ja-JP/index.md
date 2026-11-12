# Android — 開発者文書

Android は Java/JNI と AAR/Prefab アダプターを管理します。UI 配信とライフサイクルはアダプター／ホストに置きます。JNI は標準 UTF-8 バイト配列と明示的なカタログ読込・言語選択を使い、翻訳例外に JNI modified UTF-8 を使いません。デスクトップ JNI smoke は実 JVM とテスト用 AssetManager を使い、Android 実機検証の代わりにはなりません。

共通の組み込みビルド、寿命、パッケージ出力、ローカライズの担当範囲は [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md) を参照してください。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android は JDK 17、Gradle 8.11.1、AGP 8.9.2、SDK 35、NDK 28.2.13676358、CMake 3.31.6 が必要です。両 ABI の JNI/Interop/libc++、Android/API 身分、16 KB ELF LOAD 整列を検査します。実機／エミュレーター、R8、最終 APK ZIP 整列も必要です。

[ユーザー文書](../../User/ja-JP/index.md)


## モバイルリソースと言語

共通モバイルカタログの一覧、配備パス、明示ロードの手順は [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md#mobile-localization) で管理します。

翻訳バッファーの所有権、エラー、エクスポートの保守は [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/abi-and-lifetime.md#localization-abi) で定義します。

[Android AAR の対象検証](device-validation.md)

基本 Android AAR は embedded-only ビルドでも Application（`VEROSCRIPT_BUILD_APPLICATION=ON`）を明示的に有効にします。LICENSE と NDK の通知に加え、`assets/veroscript/licenses/MINIZ-LICENSE.txt`、`JSON-LICENSE.txt`、`SHA256-LICENSE.txt` を保持してください。モバイル検証はこれらを必須とします。アプリケーションコンテナーの有効化でパッケージインストールクライアントやデスクトップツールが有効になることはありません。

<a id="jni-host-fixture"></a>

任意のデスクトップ JNI 検証は `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`、`BUILD_TESTING=ON` と JAVA_HOME の実 JDK を使います。VeroScriptAndroidHostSmoke をビルドし、CTest の VeroScriptAndroidJniHostSmoke を実行します。実 JNI アダプターと検証用 AssetManager を `-Xcheck:jni` で使い、UTF-8／NUL、import、例外、取消、解放を確認します。Android ABI／API、APK、実機の検証にはなりません。対象検証は [端末ガイド](device-validation.md)を参照してください。

<!-- docs-navigation -->

---

[開発者文書](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
