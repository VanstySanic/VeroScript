# CSharp — Mobile SDK

本頁面向建置與發佈 C# 行動 SDK 的維護者。桌面與行動共用 `VeroScriptCSharp.csproj` 及 NuGet 識別碼 `VeroScript.CSharp`。預設目標為 .NET 8/9；`VeroScriptIncludeMobile=true` 加入 .NET 10 Android/iOS 兩個目標。Android 要求 API 26、arm64-v8a/x86_64；iOS 要求 15.0、arm64 真機與 arm64/x86_64 模擬器。包含行動的封裝必須涵蓋四個受控目標、相符的工作負載及完整且已驗證的原生根目錄。

`prepare-native.py` 使用同一原始碼版本的 Android AAR 與靜態 `VeroScriptIOS.xcframework` SDK，輸入包含 Core/C++/Interop。C# 應用不需要 CPython SDK 或 Python 行動套件建置工具，也不使用 Java JNI 包裝器或原生 JIT 模組。在新目錄準備，驗證後封裝。

## 原生 SDK 準備

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Gradle 若輸出其他檔名，請使用實際 AAR 名稱。Android 需要已設定的 SDK／NDK／Gradle 工具鏈；iOS 需要 macOS 與完整 Xcode。行動受控目標需要 .NET 10 Android/iOS 工作負載。準備用直譯器不叫 `python3` 時，以 `VeroScriptMobilePython` 指定。準備流程不下載 SDK，也不簽署應用。

## 整合契約

準備階段驗證正本標頭檔、標準函式庫、六語言目錄、再散發聲明與 SHA-256 檔案清單。檢查 Android 識別碼／API 和 16 KB ELF LOAD 對齊，保留兩個 ABI 的 `libVeroScriptInterop.so` 與相符的 `libc++_shared.so`；稽核靜態 iOS 封存物件的真機／模擬器架構及最低 OS。iOS SDK 需要 Foundation/libc++。拒絕已有輸出目錄。

NuGet 自動匯入 `buildTransitive/VeroScript.CSharp.targets`。原始碼 `ProjectReference` 使用者必須匯入該檔案並提供 `VeroScriptMobileNativeRoot`。保留 Android 原生程式庫／資產，以及 iOS 的 `__Internal`、`NativeReference`、`ForceLoad` 和產生的 `ReferenceNativeSymbol` 符號保留項目。參考解析後檢查實際 iOS 繫結組件，包括轉移參考；拒絕桌面／Android 繫結組件。標準函式庫與本地化目錄屬於應用資源；位元組碼快取使用應用管理的可寫入目錄。

NuGet 在 `native/` 下保留已準備根目錄的相對布局，包括 `android/<abi>`、`ios`、`assets` 與 `licenses`，打包時不重複這些路徑段。MSBuild 目標透過 `OutputType=Exe`，或 Android SDK 推斷後的 `AndroidApplication=true` 識別應用。Android 應用的推斷輸出型別即使是 `Library`，仍須選入原生程式庫與 MAUI 資產；一般類別庫使用端不觸發這些部署項目。

資源安裝驗證路徑與摘要，要求大小寫精確的 `stdlib/core.vs`，釋放串流並在重試時修復舊檔案。失敗後可能留下已驗證檔案，執行前應重試完成安裝。非同步資源安裝完成後，在同一工作執行緒建立、設定、使用與釋放工作階段。明確選擇 VM；套件不提供原生 JIT。

[應用整合](../../User/zh-TW/mobile.md)

## 發行驗收

可攜式 C# 測試將動態與 `IOS` 靜態匯入設定同原生匯出清單核對，涵蓋資源損毀、路徑越界、取消、串流釋放、舊檔案修復，以及真實標準函式庫／目錄安裝。Python 測試涵蓋 SDK 準備、MSBuild 資產選擇、不相容 RID／最低 OS、不完整目標矩陣及已解析 iOS 組件的拒絕。這些屬於主機測試。

穩定行動發行前，在 Android 模擬器／真機及 iOS 模擬器／真機以 Release 建置並執行 MAUI 範例。涵蓋 Android 16 KB 頁環境、相依性與 APK ZIP 對齊、iOS 簽署／連結、AOT／裁剪、取消、回呼、資源匯入及冷啟動；記錄工具鏈版本、應用套件檢查與真實結果。目標平台驗收完成前，行動支援仍為實驗性。
## 出版文件

行動套件僅收錄功能檔案、執行資源與授權聲明，不攜帶 README、手冊或範例；集成 User 文件由獨立 Resources 發行物提供。原生資源路徑與裝置驗收要求維持不變。

## 桌面與行動端共用的 NuGet 來源

上述命令準備啟用行動支援的 `VeroScript.CSharp` 套件。在桌面或行動集合中發佈 CSharp 業務時，還須備妥一份正本套件來源，收齊已登錄的六個 C# 套件 ID 及各自對應的 `.nupkg`、`.snupkg`。以 `VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY` 選定套件來源。透過 `package-veroscript-mobile-release.py` 重用已產生的套件時，將同一份啟用行動支援的 `VeroScript.CSharp` 檔案傳給 `--csharp-package`。各目標重用相同位元組。完整產生端輸入與範例見[行動業務發佈步驟](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling)。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](mobile.md)
