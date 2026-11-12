# iOS — 開發者文件

iOS 維護 Objective-C/Objective-C++ 適配與靜態 XCFramework 整合。UI 分派和應用生命週期歸適配層／宿主，每個 slice 保持真機／模擬器身分。

公共嵌入建置、生命週期、套件輸出與本地化職責統一維護在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md)。

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS 要求 macOS/Xcode、三個 slice，逐封存物件檢查 `LC_BUILD_VERSION`／CPU／最低 iOS，拒絕 macOS 或混合平台封存。真實 Objective-C/Swift 應用連結、簽署、背景／恢復、取消和資源測量仍是發布條件。

[使用者文件](../../User/zh-TW/index.md)


## 行動資源與本地化

公共行動目錄清單、部署路徑和明確載入流程統一維護在 [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/zh-TW/index.md#mobile-localization)。

翻譯緩衝區所有權、錯誤與匯出維護契約見 [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-TW/abi-and-lifetime.md#localization-abi)。

<a id="ios-acceptance-evidence"></a>

按確切原始碼／產物識別、Xcode／SDK 版本、最低部署系統、執行系統及實機／模擬器架構記錄證據。使用 CMAKE_SYSTEM_NAME=iOS 與匹配的 iphoneos／iphonesimulator sysroot，分別建置實機 arm64、模擬器 arm64／x86_64；嵌入建置使用靜態 Interop。核對合併封存與 XCFramework，再由 Objective-C++、Swift、C ABI 和靜態 AOT 應用程式使用。實際應用程式應驗證匯入、NSError／工廠橋接、Bundle 資源、可寫快取重用、權限拒絕、取消、小堆疊剖析／復原、ARC 及背景／復原。編譯實機 slice 不等於實機執行，模擬器簽章和擷取畫面不等於裝置簽章、原生 Metal 或效能驗收；最低 iOS 15.0 編譯也不等於在 iOS 15 執行驗證。實際回執與原始碼差異放入暫存證據，不在維護手冊保留帶日期的狀態段落；既有模擬器回執不能自動核准後續建置。

<!-- docs-navigation -->

---

[開發者文件](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
