# 多媒體維護

本公共維護指南負責多媒體模組間的提供者邊界與驗證流程。公開行為和 API 仍歸各模組 User，具體實作細節仍歸各模組 Developer。

公開標頭不得洩漏第三方型別。通用契約歸 Media 模組，具體實作與原始聲明歸後端。更新時維持錯誤、輸出保留及所有權契約；服務契約或 Core 身分改變後同時重建提供者與使用端。

允許執行時，使用模組 CMake 入口及登記的測試，分別驗證安裝後 CMake 使用、選用腳本/原生提供者和宣稱支援的各平台。靜態文件與資源檢查不能認證繪製、字型、裝置、行動端或 IDE。臨時測量不得混入 User。

重新散布時保留實際相依的原始授權及固定來源清單，第三方法律文字維持原文。

## 模組指南

| 模組 | 使用 | 維護 |
| --- | --- | --- |
| VeroScriptPlatform | [使用者](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptGraphics | [使用者](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptAudio | [使用者](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptImage | [使用者](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptFont | [使用者](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptGui | [使用者](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptSdl3 | [使用者](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptStb | [使用者](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptFontBackend | [使用者](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/zh-TW/index.md) |
| VeroScriptRmlUi | [使用者](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/zh-TW/index.md) | [開發](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/zh-TW/index.md) |

GUI／foundation 彙總元件安裝使用 CMake 3.25+ 的 `block(SCOPE_FOR VARIABLES ...)` 作用域。還原巢狀元件選擇，同時傳遞 `CMAKE_INSTALL_MANIFEST_FILES` 與 `CMAKE_ABSOLUTE_DESTINATION_FILES`，保留 Gui／RmlUi／資源相依安裝及檔案清單。已安裝 SDK 消費端與最終發行篩選仍須分別檢查。

<!-- docs-navigation -->

---

[文件維護](index.md)

[en-US](../en-US/multimedia.md) · [fr-FR](../fr-FR/multimedia.md) · [ja-JP](../ja-JP/multimedia.md) · [ko-KR](../ko-KR/multimedia.md) · [zh-CN](../zh-CN/multimedia.md) · [zh-TW](multimedia.md)
