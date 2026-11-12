# VeroScriptSdl3

<a id="provider-boundary"></a>

Platform／Graphics／Audio 公共契约须保持独立于 SDL 和 Core。具体 SDL 实现、运行时、驱动编译选项和原始声明归 VeroScriptSdl3。修改提供者时核对匹配契约、安装／迁移 SDK、模块租约与所有者线程退休回收。源码检查不能代替真实设备与平台验证。

## 构建与宿主接入

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## 维护与验证

`VEROSCRIPT_SDL3_GRAPHICS` 决定图形服务注册。驱动分别由 `VEROSCRIPT_SDL3_GPU`、`VEROSCRIPT_SDL3_D3D11`、`VEROSCRIPT_SDL3_OPENGL` 和 `VEROSCRIPT_SDL3_WEBGPU` 控制，WebGPU 默认 OFF。手柄和音频使用 `VEROSCRIPT_SDL3_CONTROLLERS` 与 `VEROSCRIPT_SDL3_AUDIO`。原生 WebGPU 通过 `VEROSCRIPT_WEBGPU_ROOT` 使用固定 wgpu-native v29.0.1.1，浏览器使用固定 Emdawn port。编译支持与实际验收结果须分别记录。

<a id="sdl-audio-build"></a>

根构建通过 `VEROSCRIPT_BUILD_AUDIO=ON` 启用 Audio；SDL 实现另需 `VEROSCRIPT_BUILD_SDL3=ON` 与 `VEROSCRIPT_SDL3_AUDIO=ON`。基础模块的独立构建入口也支持 Audio。Linux 真实驱动须与空设备测试分别验证。

`VEROSCRIPT_SDL3_CONTROLLERS` 默认 ON，OFF 关闭原生手柄支持，仍可接收外部规范状态。每个原生控制器只打开一次，句柄及震动在所属线程回收，包括异线程最终释放。后端不修改宿主的后台输入策略；实际物理能力取决于系统、设备和驱动。本接口不包含陀螺仪、手柄触摸板、轨迹球或高级力反馈。音频可通过 `VEROSCRIPT_SDL3_AUDIO` 单独启用；摄像头、SDL_Renderer 和独立 Haptic 子系统保持关闭。版本不变，宿主、提供者、绑定和 SDK 使用方须一起重建。

`VeroScriptSdl3ControllerTests` 使用 SDL 虚拟手柄／摇杆验证预连接枚举、热插拔、映射切换、独立状态、规范化、震动回调、错误参数及生命周期清理；内存后端和 SDL 脚本测试覆盖公共适配。虚拟设备测试不能代替所有物理手柄及 Apple／移动端／浏览器设备驱动的实机验收。

`VeroScriptSdl3OpenGl.cpp` 在 `SDL_PLATFORM_ANDROID`、`SDL_PLATFORM_IOS`、`SDL_PLATFORM_TVOS` 使用宿主窗口路径，要求 OpenGL ES 及一个兼容 Platform 窗口，不创建隐藏辅助窗口。Android 分开统计所保留 EGL 上下文与表面呈现占用，使替换／取消在提交前保留旧设备；UIKit 保持独占单上下文规则。Android 通过宿主 SDLActivity／`SDL_main` 的原生事件线程集成初始化，Java UI 线程准备完毕不等于 SDL 拥有者线程已初始化；其他平台仍要求应用主线程初始化。保持窗口、设备、事件泵及退休回收的拥有者线程约束，另行验证目标 SDK／设备。

[README](../../../../README.md) · [User](../../User/zh-CN/index.md)

合成事件测试不经过 SDL 上游去重路径。

`VEROSCRIPT_SDL3_GRAPHICS` 由已启用的图形驱动推导；应配置具体驱动选项，不直接覆盖此派生值。

SDL_GPU 渲染器复用已退休的顶点／索引缓冲对，最多 128 对／8 MiB，并受 MaxGeometryBytes 进一步限制。上传存储也会复用；传输映射和目标上传均通过 cycling 保留排队 GPU 工作仍在引用的存储。关闭渲染器时释放空闲存储；尚存几何仍遵守原有所有权和所属线程退休规则。

<a id="uikit-recovery-capability"></a>

保持 `Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities` 与上下文所有权一致：SDL_PLATFORM_IOS／SDL_PLATFORM_TVOS 下仅 opengles 清除 bDeviceRecovery。不能从提供者 ID 或其余七个启用能力推导恢复支持。UIKit 无法在旧 EAGL 所有者旁创建替换；Android 独立保留上下文是不同契约。运行时能力报告与公开恢复文档须一并维护。

<a id="native-provider-maintenance"></a>

提供者源码拥有 SDL 专属服务描述与命名静态查询 `veroscript_sdl3_query`，原生扩展构建导出 `veroscript_extension_query`。只注册本次构建包含的服务和图形驱动，分别核对公开契约及实际能力。着色器输入、驱动依赖和提供者租约归 SDL3，不进入后端无关契约。应以匹配的消费端验证静态／动态发现、输入路由、图形、音频和所有者线程回收；合成事件或虚拟驱动检查不能认证真实设备。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
