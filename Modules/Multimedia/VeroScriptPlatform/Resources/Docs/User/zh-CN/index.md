# VeroScriptPlatform — 基础实现

Platform 提供窗口、逻辑／像素尺寸、键盘、鼠标、触摸、UTF-8 文本／输入法及剪贴板契约。原生接口不依赖核心或 SDL；可选脚本适配器提供 `VS::Platform`。

脚本中的能力、窗口指标和输入快照查询标记为 const 方法，可从只读的按值捕获调用。消费事件、重置输入状态和修改窗口仍需要可写接收对象；此类回调应使用 `mutable` 或可写引用捕获。

在应用主线程创建服务。共享原生事件队列只泵取一次，也可由外部宿主驱动并提交规范事件。先关闭图形呈现表面，再关闭窗口；关闭宿主前必须在所有者线程清理异线程延迟释放的资源。

## 使用已安装的原生 SDK

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

窗口坐标遵循原生平台。`ScaleX/ScaleY` 将窗口坐标转换为绘制像素；`DisplayScale` 是建议的界面像素缩放比例，两者可能不同。密度无关的 GUI 输入使用 `guiX = windowX * ScaleX / DisplayScale`，绘制使用 `pixelX = guiX * DisplayScale`。脚本可调用 `window.GetDisplayScale()`；像素尺寸或界面缩放变化时重新读取尺寸信息。

## 多输入设备

<a id="input-device-snapshots"></a>

`service.GetInputDevices()` 返回快照，含 `int64 Count` 和 `Get(index)`；设备字段为 `Kind`、`DeviceId`、`Name`、`Aggregate`、`Connected`。`service.GetInputState(kind,id)` 返回不可变快照，提供 `Device`、`KeyCount`、`PointerCount`、`Modifiers`、`GetKey(index)`、`GetPointer(index)`、`IsKeyDown(Key)`、`IsScanCodeDown(int)`。按键字段为 `Key/ScanCode/WindowId`；指针字段为 `PointerId/WindowId/X/Y/Pressure/Buttons`。索引检查范围。能力查询使用 `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()`。

设备身份为 `(InputDeviceKind,DeviceId)`，触点编号只在设备内有效。类型为 `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`。零编号明确表示聚合输入；能枚举物理鼠标不代表系统会为鼠标事件提供独立编号。状态由本服务已消费的事件产生，应先泵取事件再查询；快照修饰键仅来自本设备已观察的按键，不含系统锁定状态。连接事件为 `InputDeviceAdded/Removed`，事件新增 `DeviceKind`、`DeviceName`、`AggregateDevice`、`Synthetic`。

断开、失焦、生命周期变化和销毁窗口会清理对应按键与触点，先发出合成释放／取消通知，再发出原因事件。`ClearInputState(windowId)` 清理指定窗口，零表示全部，同时丢弃对应范围排队的规范输入并发出 `InputStateReset`；宿主提交的重置保持后续事件顺序。合成的全局重置也用于通知丢失后的重新同步：清除使用方的捕获／按下状态并重新枚举。外部泵服务的状态相互独立，由宿主提供。原生设备发现方式和具体设备能力由所选后端提供，物理事件限制见后端文档。版本号未修改；原生宿主与提供者应配套重建。

非零编号设备断开时，也会保守释放同类聚合输入的按下状态，因为无法确定其物理来源；仍然按住的聚合输入需等待后续事件重新观察。释放事件路由或键盘布局变化时，也会通过合成释放／取消清理原先按下的状态。

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## 宿主窗口、光标与捕获

公共脚本适配器提供 Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture() 和 IsBorrowed()。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows 表示接口支持，原生驱动仍可拒绝操作。SDL 捕获要求该窗口同时拥有鼠标和键盘焦点；失焦、隐藏、最小化、后台、退出及输入重置均释放捕获。光标配置在窗口拥有鼠标焦点时生效，离开时恢复默认可见光标；宿主须协调 SDL 的全局光标／捕获所有权。C++ IService.AttachWindow 接收 FBorrowedWindowOptions：HandleKind="sdl3.window"、可信的 SDL_Window 地址及非空共享 Owner，不向脚本暴露地址。Close 只注销包装，不销毁宿主窗口；原生窗口销毁会使包装失效并清理已观察输入。宿主销毁窗口前须先关闭图形表面。Owner 保留到包装的退休记录被销毁，但不能阻止宿主提前显式销毁窗口。

参阅[VeroScriptSdl3 — 基础实现 — 宿主窗口、光标与捕获](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-CN/index.md#host-window-retirement)。

## 图形 API 与异步请求

窗口通过 FWindowOptions.GraphicsApi 或 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) 指定兼容 API。借用窗口必须已具有兼容的原生格式。

参阅[VeroScriptGraphics — 图形资源与二维绘制 — 图形 API 与异步请求](../../../../../VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md#resource-retirement)。

## 手柄、摇杆与震动反馈

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad` 提供标准 `GamepadAxis/GamepadButton` 槽位，`Joystick` 提供原始零基轴、按键和方向帽。同一物理设备只输出一套事件；映射变化先释放旧布局，再连接新布局，使用方应重新枚举。设备编号非零，仅标识本次连接。手柄事件是全局输入，`WindowId=0`，以 `(Kind,DeviceId)` 区分设备，查询快照前须泵取事件。

设备及连接事件包含 `AxisCount/ButtonCount/HatCount/SupportsRumble`（C++ 为 `bRumble`）。标准手柄固定提供 6 个轴槽和 26 个按键槽，设备没有的控件保持中立。原生快照为 `Axes/ControllerButtons/Hats`；脚本使用 `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`、`IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`、`GetHat(int64)->Hat` 和三个数量字段，索引均检查范围。事件 `ControllerAxis/ControllerButtonDown/ControllerButtonUp/ControllerHat` 携带 `Control/Value`；按键值由事件类型判断。轴为 [-1,1]，映射后的扳机为 [0,1]，不自动施加死区；方向帽使用 `Hat` 枚举。

服务以 `SupportsControllers()/SupportsRumble()` 报告接口能力，设备另行报告震动能力。调用 `Rumble(kind,id,低频幅度,高频幅度,int64 毫秒)`；幅度须为有限的 0..1，时长为 0..65535 ms。新调用替换该设备的反馈，时长为零或两个幅度均为零表示停止。不支持、断开、参数错误或异线程调用均明确报错。

全局清理 `ClearInputState(0)`、后台／退出、设备断开、通知溢出和服务回收会清除对应状态并停止本服务启动的震动。受管理窗口失焦、隐藏或最小化时，也保守清除全局手柄；显式按窗口清理不影响全局手柄。缺失的松键／轴归零／方向帽居中通知在原因事件前合成；SDL 也可能已提供原生释放。旧快照不变。外部泵可提交完整的手柄元数据和规范输入，SDL 外部模式不打开原生设备，也不发送震动；自定义宿主提供者可实现同一反馈接口。

查询 `SupportsControllers()` 及每个设备的反馈能力。物理可用性取决于系统、设备与驱动；后台输入策略由宿主决定。陀螺仪、手柄触摸板、轨迹球及高级力反馈不在此接口内。原生资源与反馈须在所有者线程完成回收，再卸载提供者。

<a id="window-and-events"></a>

原生宿主包含 `VeroScriptPlatformApi.h`：`IService` 管理窗口和事件，`IWindow` 提供窗口状态，`FInputDevice`／`FInputState` 表示输入快照。ID 是不透明身份，不是原生地址。关闭请求只是事件，实际关闭由宿主决定。`PollEvent` 区分操作成功与事件是否可用，空读将结果重置为 `None`；`WaitEvent` 接受 0..50 毫秒。外部泵模式由宿主等待并通过 `SubmitEvent` 提交事件。文本／IME 使用自有 UTF-8 字符串，IME 选择偏移按 Unicode 字符计数。`ScanCode` 是键盘页 USB HID usage，未映射时为零。鼠标按钮 1=左、2=中、3=右、4=后退、5=前进，掩码使用第 N-1 位。文本输入区域使用原生窗口坐标，光标参数是相对 X 的水平偏移。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
