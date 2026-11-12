# Android AAR 目標驗收

## 前提與建置

此獨立測試程式透過 Java／JNI 及 C11／C++17 Prefab 使用已建置的基礎 AAR，不另編譯第二份 Core。預設 release APK 啟用 R8，僅為此測試應用程式使用 debug 簽章金鑰。使用 Python 3.11+ 及 [Android 建置指南](index.md)固定的 JDK、Gradle、SDK、NDK、CMake 版本；先在新的輸出目錄建置匹配 SDK 產物。`--with-asmjit` 同時準備選用 JIT AAR。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## 在明確目標上執行

啟動模擬器或連接已開啟偵錯的裝置，從 `adb devices` 選取確切序號。提供匹配的基礎 AAR；範例明確要求 x86_64 安裝 ABI。

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

加入 `--jit-aar <absolute-jit.aar>` 驗證選用後端與受治理原生執行計數；ARM64 使用 `--abi arm64-v8a`。`--page-size 4096`／`--page-size 16384` 核對 Bionic `sysconf` 回報的原生頁面大小；`--debug` 選擇未縮減變體。涵蓋 API 26 及後續版本、兩種頁面大小，每個設定使用獨立輸出目錄。驗收 JNI 時另在測試模擬器啟用 CheckJNI。

## 結果與驗收範圍

執行器僅解除安裝／替換 [建置設定](../../../../Resources/Tests/Device/build.gradle.kts) 中宣告的測試應用程式 及其資料，不關閉所選裝置／模擬器。保留 APK、命令記錄、`commands.json` 和 `device-smoke.json`；校驗最終 APK 的 16 KB ZIP 對齊、各案例結果及 `INSTRUMENTATION_CODE: -1`，不能僅憑 adb 處理程序結束碼判定成功。每次嘗試先將 `acceptancePassed` 設為 false；包括指定 ABI、頁面大小在內的全部檢查通過後才設為 true。

案例涵蓋資產匯入／Unicode 名稱、UTF-8／NUL 輸出、float32 捨入、記憶體／執行／輸出預算、快取、能力拒絕、復原、明確後端、取消、並行拒絕、關閉、本地化和 JNI AOT 目錄生命週期。目錄案例不執行產生的 AOT 業務程式碼。模擬器原生轉譯下執行 ARM64 函式庫不等於實體 ARM64 硬體驗收；報告須保留實際裝置／模擬器識別和產物雜湊。Python、C#、圖形及最終應用程式仍需各自驗收。

以下純 Python 回歸模擬原生命令，只校驗執行器邏輯，不驗收 Android 執行階段。測試程式存在、主機檢查或僅建置成功都不等於目標驗收完成。

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/device-validation.md) · [fr-FR](../fr-FR/device-validation.md) · [ja-JP](../ja-JP/device-validation.md) · [ko-KR](../ko-KR/device-validation.md) · [zh-CN](../zh-CN/device-validation.md) · [zh-TW](device-validation.md)
