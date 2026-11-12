# VeroScriptGraphics

請參閱[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md#provider-boundary)。

## 建置原生契約

從儲存庫根目錄執行以下命令，建置不含指令碼配接器的公共契約。執行指令碼還需要提供者，以及透過對應 RegisterScriptBindings 配接器和相符 Core 完成註冊的宿主。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## 維護與驗證

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md)

[README](../../../../README.md) · [User](../../User/zh-TW/index.md)

## 可選原生提交範圍

IRenderSubmission2D 是透過 IRenderer2D 的 dynamic_cast 查詢的可選原生介面，不改變既有渲染器虛擬表或指令碼 API。SDL_GPU 支援此介面，SDL3 模組會轉發此能力。BeginSubmission 拒絕巢狀呼叫，EndSubmission 集中提交同一渲染器的連續通道；CancelSubmission 可重複呼叫，失敗或例外時也須清理。所有呼叫維持在所屬執行緒，資源在範圍結束前保持開啟。其他渲染器讀取輸出前須先結束範圍。範圍內 RenderSurface 的 bPresented 表示已取得影像，EndSubmission 成功僅確認提交，不代表 GPU 已完成或螢幕已顯示。取得交換鏈影像後取消仍須遵守提供者的強制提交規則。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
