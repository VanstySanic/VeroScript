# GUI

Gui 負責控制項樹、建構器、屬性、事件與 ML/CSS；RmlUi 是獨立選擇的配置與繪製實作。

- [框架與 RmlUi 整合](Reference/Framework.md)
- [標記與樣式](Reference/Markup.md)

`vs-gui` / `vs-rmlui`. CMake: `VeroScriptGui` / `VeroScriptRmlUi`; VS: `VS::Gui`.

[VS Code](../../../../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/User/zh-TW/index.md) · [Rider](../../../../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/User/zh-TW/index.md)

`gui-compile` 需要 Application 支援，產生標準 ZIP `.vsapp` 應用程式容器，包含啟動描述、GUI 編譯映像、資源中繼資料及明確嵌入的資源。GUI 檢查／預覽與通用 CLI 檢查／執行／inspect 讀取相同容器；啟用 GUI 指令碼支援的 Runtime 也可執行。先前的原始 GUI `.vsapp` 檔案需要重新編譯。後端遵循已安裝環境的服務選擇規則，主程式、專案設定和命令選項可以覆寫預設值；編輯器不會寫死 RmlUi。

[教學: 集合、導覽與對話方塊](../../../../../../../Resources/Docs/Tutorials/zh-TW/toolchain/24-gui-collections.md)

自動、明確選擇及停用的統一規則請參閱 [服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
