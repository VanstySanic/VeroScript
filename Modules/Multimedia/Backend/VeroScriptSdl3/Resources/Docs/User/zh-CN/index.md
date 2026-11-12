# VeroScriptSdl3 — 基础实现

脚本宿主必须显式选择 VM 后端；注册 Platform、Graphics 或 Audio 绑定不会自动选择执行器。源码示例运行器使用 `VeroScriptVisualExample --vm-backend <VM库文件或目录> program.vs`。`--check program.vs` 可在不装配 VM 的情况下检查普通脚本。提供 VM 时，示例向编译与执行传入同一工厂，使脚本编译处理器也使用所选后端。原生服务 SDK 仍不依赖具体 VM 实现。

SDL3 后端使用 SDL 3.4.18，提供者 ID 为 `sdl3`。它提供 `platform/sdl3`；已安装包包含相应能力时，还提供 `graphics/sdl3` 和 `audio/sdl3`。Platform、Graphics 和 Audio 共用 SDL 运行时。

注册本身不初始化 SDL。服务创建与操作须在应用主线程／所有者线程执行。离屏渲染仍要求可用的视频／图形驱动。保留资源会延长提供者寿命，关闭前须在所有者线程完成延迟释放。实际能力取决于所安装 SDK、操作系统与设备。

## 使用已安装的原生 SDK

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

参阅[VeroScriptGraphics — 图形资源与二维绘制](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md#graphics-service-contract)。

参阅[VeroScriptGraphics — 图形资源与二维绘制 — 使用已安装的原生 SDK](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md#vertex-color-mode)。

## 输入设备

[Platform: 多输入设备](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-CN/index.md)

参阅[VeroScriptPlatform — 基础实现 — 多输入设备](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-CN/index.md#input-device-snapshots)。

断开、失焦、生命周期变化和销毁窗口会清理对应按键与触点，先发出合成释放／取消通知，再发出原因事件。`ClearInputState(windowId)` 清理指定窗口，零表示全部，同时丢弃对应范围排队的规范输入并发出 `InputStateReset`；宿主提交的重置保持后续事件顺序。合成的全局重置也用于通知丢失后的重新同步：清除使用方的捕获／按下状态并重新枚举。外部泵服务的状态相互独立，由宿主提供。SDL 在泵取／枚举时每 250 ms 核对触摸拓扑；键盘／鼠标按 SDL 有序连接事件更新。独立笔设备不在本次实现范围。版本号未修改；原生宿主与提供者应配套重建。

参阅[VeroScriptPlatform — 基础实现 — 多输入设备](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-CN/index.md#input-device-snapshots)。

当前 SDL 会按全局扫描码状态去重：多个物理键盘重叠按放同一键时，可能将第二次按下标为重复并丢弃后续释放。设备编号或 raw keyboard 模式不能保证完整的物理逐键盘状态。Platform 快照按实际收到的事件维护；如需完整物理独立状态，应由外部宿主提交完整的设备事件。

## 宿主窗口、光标与捕获

公共脚本适配器提供 Window.SetCursor(Cursor,bool visible)、SetMouseCapture(bool)、HasMouseCapture() 和 IsBorrowed()。Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows 表示接口支持，原生驱动仍可拒绝操作。SDL 捕获要求该窗口同时拥有鼠标和键盘焦点；失焦、隐藏、最小化、后台、退出及输入重置均释放捕获。光标配置在窗口拥有鼠标焦点或捕获时生效；拖动捕获会保留光标，其他离开情况恢复默认可见光标；宿主须协调 SDL 的全局光标／捕获所有权。C++ IService.AttachWindow 接收 FBorrowedWindowOptions：HandleKind="sdl3.window"、可信的 SDL_Window 地址及非空共享 Owner，不向脚本暴露地址。Close 只注销包装，不销毁宿主窗口；原生窗口销毁会使包装失效并清理已观察输入。宿主销毁窗口前须先关闭图形表面。Owner 保留到包装的退休记录被销毁，但不能阻止宿主提前显式销毁窗口。

系统光标能力要求原生默认光标可用；当前 SDL 仅在 windows、cocoa 和 x11 驱动声明显式鼠标捕获。其他驱动保留 GUI 内部逻辑捕获，不请求不支持的 SDL 捕获。

<a id="host-window-retirement"></a>

SDL 后端在原生销毁事件中解除自己的 GPU 窗口占用，包括尚未回收的异线程表面；后续 Close 保持幂等。该观察依赖 SDL_EVENT_WINDOW_DESTROYED 保持启用且不被过滤；屏蔽此事件的宿主必须在销毁原生窗口前关闭表面和包装。

## 图形 API 与异步请求

<a id="sdl-graphics-drivers"></a>

选择已安装提供者实际支持的驱动：Windows 的 `direct3d11`／`direct3d11-warp`、`opengl`（3.3 core）、`opengles`（ES 3.0）或可选 `webgpu`。SDL_GPU 在支持的目标提供 Direct3D 12、Vulkan 或 Metal。查询所选服务并明确处理驱动不可用的错误。

窗口通过 FWindowOptions.GraphicsApi 或 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi) 指定兼容 API。借用窗口必须已具有兼容的原生格式。

Android 及可选 iOS／iPadOS／tvOS `opengles` 驱动要求先创建且仅创建一个 `GraphicsApi="opengles"` 的兼容 Platform 窗口。Android EGL 上下文可分别保留该窗口，因此恢复时可在旧设备仍存活的情况下准备替代设备，但同一时刻仅一个表面能拥有呈现权。UIKit 为窗口保留单个上下文。设备仅接受其所保留窗口的表面。先关闭表面与资源，再关闭设备／窗口；关闭窗口会使关联设备失效。默认 SDL_GPU 驱动使用独立宿主路径。

参阅[VeroScriptGraphics — 图形资源与二维绘制 — 图形 API 与异步请求](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md#asynchronous-requests)。

参阅[VeroScriptGraphics — 图形资源与二维绘制 — 图形 API 与异步请求](../../../../../../VeroScriptGraphics/Resources/Docs/User/zh-CN/index.md#resource-retirement)。

WebGPU 须部署匹配的原生运行时或浏览器 port 及其原始许可声明。浏览器需要异步创建，并在帧之间交还控制权；原生和浏览器支持须按所选 SDK 与目标分别确认。

CreateDevice 与 CreateDeviceAsync 共用默认 API 选择；原生构建仅启用 WebGPU 时，空驱动名仍走异步请求。DX11 的 Device.Poll 会检测设备移除并使资源失效，恢复后仍须重建资源。WebGPU 支持完成前、完成后及重复取消；失败的 GetPixels 不改变输出。

## 手柄、摇杆与震动反馈

参阅[VeroScriptPlatform — 基础实现 — 手柄、摇杆与震动反馈](../../../../../../VeroScriptPlatform/Resources/Docs/User/zh-CN/index.md#controllers-and-feedback)。

## 音频

包含音频能力时，SDL 提供 `audio/sdl3`（`veroscript.audio`，0.1.1）。`CreateAudio` 只初始化音频并共享现有 SDL 运行时。`VS::Audio` 提供播放／录音流、设备快照与可合并的拓扑变化、有界 PCM 队列、音量和暂停控制。原生流负责采样率／声道转换，公共 Audio 模块可无设备解码未压缩 PCM／浮点 RIFF/WAVE。

流初始暂停并彼此独立。录音溢出会清空未读数据并保留标志，音频线程不执行脚本回调。关闭流后在所有者线程完成回收。部署的 Linux SDK 须包含 ALSA／PulseAudio 等真实驱动；麦克风权限和浏览器用户手势由宿主处理，设备能力须在实际目标确认。

<a id="audio-event-pumping"></a>

在所属线程的应用循环中定期调用 `PollEvent()` 或 `GetDevices()`。SDL3 实现会泵取主线程回调和原生通知，但不会取走宿主的 SDL 事件；仅音频、无窗口时也能处理设备失效及默认路由迁移。连接状态依据在线设备枚举，旧流保留的原生格式不代表设备仍在线。

<a id="uikit-device-recovery"></a>

iOS／iPadOS／tvOS 的 `opengles` 设备报告 `bDeviceRecovery == false`，脚本对应 `SupportsDeviceRecovery()`。窗口只允许保留一个 EAGL 上下文，不应在旧设备仍存活时通过 RecoverDevice／RecoverDeviceAsync 准备替换。先关闭并释放旧表面、资源和设备，在所有者线程排空延迟回收，再创建新设备并重新上传资产。其他驱动按各自设备能力处理；Android EGL 的替换规则不能视为 UIKit 支持恢复。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
