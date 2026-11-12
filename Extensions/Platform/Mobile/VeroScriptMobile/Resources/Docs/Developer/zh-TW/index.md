# Mobile — 開發者文件

UI 分派和應用生命週期由適配層／宿主負責，Core 保持平台中立。embedded-only 排除桌面 CLI/tooling 和語言匯出器。靜態 Interop 屬於原始碼相依圖或合併的 iOS 封存，桌面 Interop 套件仍使用共享程式庫。平台身分須區分 Android/Linux、iOS/macOS、真機／模擬器。平台允許旗標不能證明後端已建置／選擇或裝置已驗收。驗證資源匯入、快取失效／損壞、輸入輸出上限、取消及解構。

```text
cmake -S . -B build/embedded -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF -DBUILD_TESTING=ON
cmake --build build/embedded --config Release --target VeroScriptMobileTests
```

`package-veroscript-mobile.py` 產生獨立實驗成品，拒絕既有輸出，不出版到桌面發行集合。

工具鏈版本與平台專屬驗收條件分別維護在 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/zh-TW/index.md) 和 [iOS](../../../../../VeroScriptIOS/Resources/Docs/Developer/zh-TW/index.md)。

[使用者文件](../../User/zh-TW/index.md)


## 行動資源與本地化

<a id="mobile-localization"></a>

`mobile` 本地化設定集成 Core、C++ Session、Interop 與 `Extensions/Platform/Mobile/VeroScriptMobile/Resources/Localization`，不包含桌面 Tooling/UI。Android Gradle 將同一目錄複製至 AAR，打包器驗證完整清單與精確位元組；iOS 複製至 `assets/localization`。應用在選擇語言前明確解包、載入，不得引入行動環境自動探索。行動套件僅收錄功能檔案、執行資源與授權聲明，不攜帶 README、手冊或範例；集成 User 文件由獨立 Resources 發行物提供。原生資源路徑與裝置驗收要求維持不變。

Mobile `TranslateMessage` 經公開 C ABI 委派。緩衝區所有權與錯誤行為見 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/abi-and-lifetime.md)，Android JNI 編碼要求歸 [Android](../../../../../VeroScriptAndroid/Resources/Docs/Developer/zh-TW/index.md) 維護。

Core 與目標 AOT 實作指紋納入 `Shared/Private/VeroScriptFloatParsing.h`、`VeroScriptFloatFormatting.h` 和 `VeroScriptJsonString.h`。Python 原始碼發行保留全部必要的 `Shared/Private` 輸入，包括供 Application／套件執行階段使用的 `VeroScriptDarwinPaths.h`。Android Python 原始碼摘要亦涵蓋 Application 及其 C 原始碼。這些是私有建置輸入，不是公開 SDK 標頭或裝置驗收證據。

<a id="mobile-execution-worker"></a>

`Private/VeroScriptMobile.cpp::WithCompilerStack` 在 Android／Apple 檢查呼叫堆疊大小；小於 8 MiB 時建立 8 MiB pthread 並同步 join，否則於呼叫執行緒執行。工作執行緒擁有整個 FSession 的建立、執行和銷毀，不只是剖析階段；捕捉例外後於 join 完成時重新擲出。保持取消互斥鎖保護下的作用中 Session 指標在銷毀前清空，並於所有結束路徑清理外層執行狀態。直接使用 FSession 不經過此輔助路徑，堆疊由宿主提供。該預算不提高語言巢狀、呼叫深度或執行限制。維護小堆疊合法輸入、剖析上限診斷、例外傳遞、取消和重用回歸；Android 測試程式涵蓋巢狀後復原，Apple 回歸明確固定 512 KiB 呼叫堆疊。

<a id="mobile-message-coverage"></a>

Mobile 目錄應涵蓋 C++ Session／runner、Android AOT／Java 參數驗證及 iOS 工廠／後端錯誤。C++ 與 Java 驗證使用既有翻譯入口，JNI／NSError 在邊界翻譯適配例外。std::system_error 僅翻譯專案內容脈絡，保留錯誤碼和 category；作業系統後綴遵循系統函式庫語言。資源失敗繼續使用既有後援。六語言須保持訊息鍵及預留位置一致。

純 Core／C++／C 原始碼宿主可用 `VEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF` 關閉適配器。`VEROSCRIPT_INTEROP_STATIC=ON` 是原始碼／靜態行動建置選項，不改變桌面共用 Interop 發行方式。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
