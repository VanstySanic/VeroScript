# Android — 開發者文件

Android 維護 Java/JNI 與 AAR/Prefab 適配。UI 分派和應用生命週期歸適配層／宿主。JNI 使用標準 UTF-8 位元組陣列以及明確目錄載入／語言選擇，翻譯例外不能使用 JNI modified UTF-8。桌面 JNI smoke 使用真實 JVM 與測試 AssetManager，不能代替 Android 裝置驗收。

公共嵌入建置、生命週期、套件輸出與本地化職責統一維護在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md)。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android 要求 JDK 17、Gradle 8.11.1、AGP 8.9.2、SDK 35、NDK 28.2.13676358 和 CMake 3.31.6。檢查兩 ABI 的 JNI/Interop/libc++、Android/API 身分及 16 KB ELF LOAD 對齊；真機／模擬器、R8、最終 APK ZIP 對齊仍須驗證。

[使用者文件](../../User/zh-TW/index.md)


## 行動資源與本地化

公共行動目錄清單、部署路徑和明確載入流程統一維護在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md#mobile-localization)。

翻譯緩衝區所有權、錯誤與匯出維護契約見 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/abi-and-lifetime.md#localization-abi)。

[Android AAR 目標驗收](device-validation.md)

基礎 Android AAR 在 embedded-only 建置中亦明確啟用 Application（`VEROSCRIPT_BUILD_APPLICATION=ON`）。除 LICENSE 與 NDK 聲明外，保留 `assets/veroscript/licenses/MINIZ-LICENSE.txt`、`JSON-LICENSE.txt` 和 `SHA256-LICENSE.txt`；行動校驗器會要求這些檔案。應用程式容器支援不代表啟用套件安裝用戶端或桌面工具。

<a id="jni-host-fixture"></a>

選用桌面 JNI 測試程式使用 `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`、`BUILD_TESTING=ON` 及 JAVA_HOME 指定的實際 JDK。建置 VeroScriptAndroidHostSmoke 後執行 CTest 項目 VeroScriptAndroidJniHostSmoke；它透過真實 JNI 適配和測試 AssetManager，在 `-Xcheck:jni` 下驗證 UTF-8／NUL、匯入、例外、取消與釋放，不構成 Android ABI／API、APK 或裝置驗收。目標驗收由 [裝置指南](device-validation.md)維護。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
