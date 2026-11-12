# VeroScriptPlatform — 基礎實作

Platform 提供視窗、邏輯／像素尺寸、鍵盤、滑鼠、觸控、UTF-8 文字／輸入法及剪貼簿契約。原生介面不依賴核心或 SDL；選用的指令碼轉接器提供 `VS::Platform`。

腳本中的能力、視窗指標與輸入快照查詢標記為 const 方法，可從唯讀的傳值擷取呼叫。消費事件、重設輸入狀態與修改視窗仍需要可寫接收物件；此類回呼應使用 `mutable` 或可寫參考擷取。

在應用程式主執行緒建立服務。共用原生事件佇列只處理一次，也可由外部宿主驅動並提交正規化事件。先關閉圖形呈現表面，再關閉視窗；結束前必須在擁有者執行緒回收其他執行緒延遲釋放的資源。

## 使用已安裝的原生 SDK

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

視窗座標遵循原生平台。`ScaleX/ScaleY` 將視窗座標轉換為繪製像素；`DisplayScale` 是建議的介面像素縮放比例，兩者可能不同。密度無關的 GUI 輸入使用 `guiX = windowX * ScaleX / DisplayScale`，繪製使用 `pixelX = guiX * DisplayScale`。指令碼可呼叫 `window.GetDisplayScale()`；像素尺寸或介面縮放改變時重新讀取尺寸資訊。

## 多輸入裝置

<a id="input-device-snapshots"></a>

`service.GetInputDevices()` 傳回快照，包含 `int64 Count` 與 `Get(index)`；裝置欄位為 `Kind`、`DeviceId`、`Name`、`Aggregate`、`Connected`。`service.GetInputState(kind,id)` 傳回不可變快照：`Device`、`KeyCount`、`PointerCount`、`Modifiers`、`GetKey(index)`、`GetPointer(index)`、`IsKeyDown(Key)`、`IsScanCodeDown(int)`。按鍵欄位為 `Key/ScanCode/WindowId`；指標欄位為 `PointerId/WindowId/X/Y/Pressure/Buttons`。索引會檢查範圍。能力查詢為 `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()`。

裝置識別為 `(InputDeviceKind,DeviceId)`，接觸點編號只在裝置內有效。類型為 `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`。零編號明確表示彙總輸入；可列舉實體滑鼠不代表系統會提供各滑鼠的事件識別。狀態來自本服務已消費的事件，查詢前應先泵取事件；修飾鍵只反映該裝置已觀察的按鍵，不包含系統鎖定狀態。連接事件為 `InputDeviceAdded/Removed`，事件新增 `DeviceKind`、`DeviceName`、`AggregateDevice`、`Synthetic`。

斷線、失焦、生命週期變更與視窗銷毀會清理對應狀態，先發出合成釋放／取消通知，再發出原因事件。`ClearInputState(windowId)` 清理指定視窗，零表示全部，丟棄相同範圍排隊的正規化輸入並發出 `InputStateReset`；主機提交的重設保留後續事件順序。合成全域重設亦表示通知遺失後需重新同步：清除呼叫端擷取／按下狀態並重新列舉。外部服務維持獨立的主機輸入狀態。原生裝置探索與具體能力由所選後端提供，實體事件限制請參閱後端文件。版本未變更；原生主機與提供者應配套重建。

非零編號裝置斷線時，亦會保守釋放同類彙總輸入的按下狀態，因為無法確定其實體來源；仍按住的彙總輸入需等待後續事件重新觀察。釋放路由或鍵盤配置變更時，亦會以合成釋放／取消清理原先狀態。

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## 宿主視窗、游標與擷取

公共腳本介面提供 Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture() 與 IsBorrowed()。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows 表示介面支援，原生驅動仍可拒絕操作。SDL 擷取要求視窗同時具有滑鼠及鍵盤焦點；失焦、隱藏、最小化、背景、結束及輸入重設會釋放擷取。游標設定在視窗具有滑鼠焦點時生效，離開時恢復預設可見游標；宿主須協調 SDL 全域游標／擷取的擁有權。C++ IService.AttachWindow 接收 FBorrowedWindowOptions：HandleKind="sdl3.window"、可信的 SDL_Window 位址及非空共享 Owner，不向腳本公開位址。Close 只取消包裝的登記，不銷毀宿主視窗；原生視窗銷毀會使包裝失效並清除已觀察輸入。宿主銷毀視窗前須關閉圖形表面。Owner 保留到包裝的退休記錄銷毀，但無法阻止宿主提前明確銷毀視窗。

請參閱[VeroScriptSdl3 — 基礎實作 — 宿主視窗、游標與擷取](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-TW/index.md#host-window-retirement)。

## 圖形 API 與非同步請求

以 FWindowOptions.GraphicsApi 或 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) 選擇相容視窗。借用視窗須已有相容的原生格式。

請參閱[VeroScriptGraphics — 圖形資源與二維繪製 — 圖形 API 與非同步請求](../../../../../VeroScriptGraphics/Resources/Docs/User/zh-TW/index.md#resource-retirement)。

## 手把、搖桿與震動回饋

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad` 使用標準 `GamepadAxis/GamepadButton` 槽位，`Joystick` 使用原始零基軸、按鍵和方向帽。同一實體裝置只提供一組事件；映射變更會釋放舊配置再宣告新配置，請重新列舉。識別碼為非零連線識別。手把是全域輸入，`WindowId=0`，查詢 `(Kind,DeviceId)` 快照前須處理事件。

裝置與連線事件提供 `AxisCount/ButtonCount/HatCount/SupportsRumble`（C++ `bRumble`）。標準手把有 6 個軸槽、26 個按鍵槽，裝置沒有的控制項保持中立。原生快照含 `Axes/ControllerButtons/Hats`；腳本提供 `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`、`IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`、`GetHat(int64)->Hat` 及三項數量，均檢查索引。事件 `ControllerAxis/ControllerButtonDown/ControllerButtonUp/ControllerHat` 使用 `Control/Value`；按鍵以事件類型判斷。軸為 [-1,1]，映射扳機為 [0,1]，不預設死區；方向帽使用 `Hat`。

服務能力為 `SupportsControllers()/SupportsRumble()`，仍須檢查各裝置的震動能力。`Rumble(kind,id,低頻幅度,高頻幅度,int64 毫秒)` 接受有限 0..1 幅度及 0..65535 ms；新呼叫取代該裝置的回饋，零時長或兩個零幅度停止回饋。不支援、斷線、無效參數及異執行緒操作均回報錯誤。

全域重設、背景／退出、斷線、通知溢位及服務回收會清除狀態並停止本服務啟動的震動；受管理視窗失焦、隱藏或最小化亦保守清除全域手把。明確指定視窗的清理不影響全域手把。缺少的鬆鍵／歸零通知在原因事件前合成，舊快照不變。外部模式接收主機提供的配置與事件，不開啟原生手把或傳送震動；主機提供者可實作相同回饋介面。

查詢 `SupportsControllers()` 及每個裝置的回饋能力。實體可用性取決於系統、裝置與驅動；背景輸入策略由主程式決定。陀螺儀、手柄觸控板、軌跡球及進階力回饋不在此介面內。原生資源與回饋須在擁有者執行緒完成回收，再卸載提供者。

<a id="window-and-events"></a>

原生宿主包含 `VeroScriptPlatformApi.h`：`IService` 管理視窗和事件，`IWindow` 提供視窗狀態，`FInputDevice`／`FInputState` 表示輸入快照。ID 是不透明身分，不是原生位址。關閉請求只是事件，實際關閉由宿主決定。`PollEvent` 區分操作成功與事件是否可用，空讀將結果重設為 `None`；`WaitEvent` 接受 0..50 毫秒。外部泵模式由宿主等待並透過 `SubmitEvent` 提交事件。文字／IME 使用自有 UTF-8 字串，IME 選擇偏移按 Unicode 字元計數。`ScanCode` 是鍵盤頁 USB HID usage，未對應時為零。滑鼠按鈕 1=左、2=中、3=右、4=後退、5=前進，遮罩使用第 N-1 位元。文字輸入區域使用原生視窗座標，游標參數是相對 X 的水平偏移。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
