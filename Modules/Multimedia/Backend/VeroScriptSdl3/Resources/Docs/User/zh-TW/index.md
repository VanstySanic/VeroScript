# VeroScriptSdl3 — 基礎實作

腳本宿主必須明確選擇 VM 後端；註冊 Platform、Graphics 或 Audio 綁定不會自動選擇執行器。原始碼範例執行器使用 `VeroScriptVisualExample --vm-backend <VM程式庫檔案或目錄> program.vs`。`--check program.vs` 可在不裝配 VM 的情況下檢查一般腳本。提供 VM 時，範例向編譯與執行傳入同一工廠，讓腳本編譯處理器也使用所選後端。原生服務 SDK 仍不依賴具體 VM 實作。

SDL3 後端使用 SDL 3.4.18，提供者 ID 為 `sdl3`。它提供 `platform/sdl3`；已安裝套件包含相應能力時，還提供 `graphics/sdl3` 和 `audio/sdl3`。Platform、Graphics 和 Audio 共用 SDL 執行階段。

註冊本身不初始化 SDL。服務建立與操作須在應用程式主／擁有者執行緒執行。離屏繪製仍要求可用的視訊／圖形驅動。保留資源會延長提供者存續時間，關閉前須在擁有者執行緒完成延遲釋放。實際能力取決於已安裝 SDK、作業系統與裝置。

## 使用已安裝的原生 SDK

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

請參閱[VeroScriptGraphics — 圖形資源與二維繪製](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md#graphics-service-contract)。

請參閱[VeroScriptGraphics — 圖形資源與二維繪製 — 使用已安裝的原生 SDK](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md#vertex-color-mode)。

## 輸入裝置

[Platform: 多輸入裝置](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-TW/index.md)

請參閱[VeroScriptPlatform — 基礎實作 — 多輸入裝置](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-TW/index.md#input-device-snapshots)。

斷線、失焦、生命週期變更與視窗銷毀會清理對應狀態，先發出合成釋放／取消通知，再發出原因事件。`ClearInputState(windowId)` 清理指定視窗，零表示全部，丟棄相同範圍排隊的正規化輸入並發出 `InputStateReset`；主機提交的重設保留後續事件順序。合成全域重設亦表示通知遺失後需重新同步：清除呼叫端擷取／按下狀態並重新列舉。外部服務維持獨立的主機輸入狀態。SDL 每 250 ms 在泵取／列舉時核對觸控拓撲；鍵盤／滑鼠依 SDL 有序連接事件更新。獨立觸控筆不在本次範圍。版本未變更；原生主機與提供者應配套重建。

請參閱[VeroScriptPlatform — 基礎實作 — 多輸入裝置](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-TW/index.md#input-device-snapshots)。

目前 SDL 以全域掃描碼狀態去重：多個實體鍵盤重疊按放同一按鍵時，可能將第二次按下標為重複並丟棄後續釋放。裝置編號或 raw keyboard 模式無法保證完整的實體逐鍵盤狀態。Platform 快照依收到的事件維護；若需完整實體獨立狀態，應由外部主機提交完整事件。

## 宿主視窗、游標與擷取

公共腳本介面提供 Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture() 與 IsBorrowed()。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows 表示介面支援，原生驅動仍可拒絕操作。SDL 擷取要求視窗同時具有滑鼠及鍵盤焦點；失焦、隱藏、最小化、背景、結束及輸入重設會釋放擷取。游標設定在視窗具有滑鼠焦點或擷取時生效；拖曳擷取會保留游標，其他離開情況恢復預設可見游標；宿主須協調 SDL 全域游標／擷取的擁有權。C++ IService.AttachWindow 接收 FBorrowedWindowOptions：HandleKind="sdl3.window"、可信的 SDL_Window 位址及非空共享 Owner，不向腳本公開位址。Close 只取消包裝的登記，不銷毀宿主視窗；原生視窗銷毀會使包裝失效並清除已觀察輸入。宿主銷毀視窗前須關閉圖形表面。Owner 保留到包裝的退休記錄銷毀，但無法阻止宿主提前明確銷毀視窗。

系統游標能力要求原生預設游標可用；目前 SDL 僅在 windows、cocoa 和 x11 驅動宣告明確滑鼠擷取。其他驅動保留 GUI 內部邏輯擷取，不要求不支援的 SDL 擷取。

<a id="host-window-retirement"></a>

SDL 後端在原生銷毀事件中解除自己的 GPU 視窗占用，亦涵蓋尚未回收的異執行緒表面；之後 Close 仍具冪等性。此觀察需要 SDL_EVENT_WINDOW_DESTROYED 保持啟用且不被過濾；屏蔽該事件的宿主必須先關閉表面與包裝，再銷毀原生視窗。

## 圖形 API 與非同步請求

<a id="sdl-graphics-drivers"></a>

選擇已安裝提供者實際支援的驅動：Windows 的 `direct3d11`／`direct3d11-warp`、`opengl`（3.3 core）、`opengles`（ES 3.0）或可選 `webgpu`。SDL_GPU 在支援目標提供 Direct3D 12、Vulkan 或 Metal。查詢所選服務並明確處理驅動無法使用的錯誤。

以 FWindowOptions.GraphicsApi 或 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) 選擇相容視窗。借用視窗須已有相容的原生格式。

Android 及選用 iOS／iPadOS／tvOS `opengles` 驅動要求先建立且僅建立一個 `GraphicsApi="opengles"` 的相容 Platform 視窗。Android EGL 繪圖上下文可分別保留該視窗，因此復原時可在舊裝置仍存活的情況下準備替代裝置，但同一時刻僅一個表面能擁有呈現權。UIKit 為視窗保留單個繪圖上下文。裝置僅接受其所保留視窗的表面。先關閉表面與資源，再關閉裝置／視窗；關閉視窗會使關聯裝置失效。預設 SDL_GPU 驅動使用獨立主程式路徑。

請參閱[VeroScriptGraphics — 圖形資源與二維繪製 — 圖形 API 與非同步請求](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md#asynchronous-requests)。

請參閱[VeroScriptGraphics — 圖形資源與二維繪製 — 圖形 API 與非同步請求](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md#resource-retirement)。

WebGPU 須部署匹配的原生執行階段或瀏覽器 port 及其原始授權聲明。瀏覽器需要非同步建立，並在影格之間交還控制權；原生與瀏覽器支援須按所選 SDK 和目標分別確認。

CreateDevice 與 CreateDeviceAsync 共用預設 API 選擇；原生組建僅啟用 WebGPU 時，空驅動名稱仍使用非同步請求。DX11 的 Device.Poll 會偵測裝置移除並使資源失效，復原後仍須重建資源。WebGPU 可在完成前、完成後及重複取消；失敗的 GetPixels 不改變輸出。

## 手把、搖桿與震動回饋

請參閱[VeroScriptPlatform — 基礎實作 — 手把、搖桿與震動回饋](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-TW/index.md#controllers-and-feedback)。

## 音訊

包含音訊能力時，SDL 提供 `audio/sdl3`（`veroscript.audio`，0.1.1）。`CreateAudio` 僅初始化音訊，並共用既有 SDL 執行階段。`VS::Audio` 提供播放／錄音串流、裝置快照及可合併的拓撲變更、有界 PCM 佇列、音量與暫停控制。原生串流負責取樣率／聲道轉換；公共 Audio 模組可無裝置解碼未壓縮 PCM／浮點 RIFF/WAVE。

串流初始暫停並彼此獨立。錄音溢位會清空未讀資料並保留旗標，音訊執行緒不執行指令碼回呼。關閉串流後在擁有者執行緒完成回收。部署的 Linux SDK 須包含 ALSA／PulseAudio 等實際驅動；麥克風權限和瀏覽器使用者手勢由主程式處理，裝置能力須在實際目標確認。

<a id="audio-event-pumping"></a>

在所屬執行緒的應用程式迴圈中定期呼叫 `PollEvent()` 或 `GetDevices()`。SDL3 實作會處理主執行緒回呼與原生通知，但不會取走宿主的 SDL 事件；僅音訊、無視窗時也能處理裝置失效及預設路由遷移。連線狀態依據線上裝置列舉，舊串流保留的原生格式不代表裝置仍在線上。

<a id="uikit-device-recovery"></a>

iOS／iPadOS／tvOS 的 `opengles` 裝置回報 `bDeviceRecovery == false`，腳本對應 `SupportsDeviceRecovery()`。視窗只允許保留一個 EAGL 上下文，不應在舊裝置仍存活時透過 RecoverDevice／RecoverDeviceAsync 準備替換。先關閉並釋放舊表面、資源和裝置，在擁有者執行緒排空延遲回收，再建立新裝置並重新上傳資產。其他驅動程式按各自裝置能力處理；Android EGL 的替換規則不能視為 UIKit 支援復原。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
