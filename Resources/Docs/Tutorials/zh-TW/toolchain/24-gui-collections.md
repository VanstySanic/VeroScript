# 24：使用集合、導覽與對話方塊

開啟隨附 GUI 範例，練習選取項目、排序虛擬表格並接收模態對話方塊結果。範例的清單與表格各有 10,000 列，僅為檢視區附近的列建立元件。

## 1. 準備

使用相符且啟用 GUI 的完整 Runtime，保留其 `Vm/`，並設定平台／圖形 `sdl3`、GUI `rmlui`、影像 `stb`、字型／文字配置 `font-backend` 提供者。在已安裝環境包含這些服務的既有 GUI 專案操作；複製原始檔不會安裝提供者。執行時保留完整 Runtime 與提供者資源。

將 [main.vs](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/main.vs)、[page.vswml](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswml)、[page.vswcss](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/CompositeControls/page.vswcss) 三個檔案複製到同一可寫專案目錄，保持檔名與相對位置，並在此目錄開啟終端機。

## 2. 檢查

下列命令將編譯處理器逾時設為 20 秒，指令數及其他主機限制仍會生效；請依專案與主機情況調整預算。檢查成功的結束碼為 0，不開啟視窗，也不執行 `main`。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-check main.vs
```

## 3. 執行與互動

使用相同選項預覽指令碼入口。本例呼叫 `Application.Run()`，須關閉視窗離開。`--frames` 僅允許用於 ML 預覽入口；對此 `.vs` 入口指定該參數會傳回用法錯誤。

```sh
veroscript --compiler-timeout 20000 --platform-backend sdl3 --graphics-backend sdl3 --gui-backend rmlui --image-backend stb --font-backend font-backend --text-layout-backend font-backend gui-preview main.vs
```

在 ListView／TreeView 選取列並用方向鍵導覽，切換 ComboBox 與 TabView。按表頭排序；排序依 UTF-8 文字順序，外觀為數字的文字也不自動使用數值排序。開啟 File 選單及對話方塊，選擇接受／取消或按 Escape，觀察狀態文字中的結果。模態對話方塊開啟時，背景元件不接收一般指標／鍵盤輸入。

## 4. 觀察虛擬化

捲動兩個 10,000 列檢視。邏輯資料保留，元件按需回收；選取狀態屬於鍵，而非回收的元件實體。本例列高固定 28 個介面單位，自訂內容也須放入該高度。不提供可變列高或樹狀虛擬化。

## 5. 練習

修改標籤或狀態字串後重新檢查。增加資料列數時保持 `count × rowHeight ≤ 16,777,216`，再次觀察捲動。自訂列工廠須完整重設回收狀態，並使用穩定、非空且唯一的鍵。`SetVirtualSource`、預取範圍、所有權與選取介面見 [集合、導覽與對話方塊](../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-TW/Reference/Framework.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/24-gui-collections.md) · [fr-FR](../../fr-FR/toolchain/24-gui-collections.md) · [ja-JP](../../ja-JP/toolchain/24-gui-collections.md) · [ko-KR](../../ko-KR/toolchain/24-gui-collections.md) · [zh-CN](../../zh-CN/toolchain/24-gui-collections.md) · [zh-TW](24-gui-collections.md)
