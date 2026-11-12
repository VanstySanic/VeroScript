# VeroScriptGraphics — 图形资源与二维绘制

<a id="graphics-service-contract"></a>

Graphics 通过 VS::Graphics 提供设备、纹理、采样器、索引几何与 Renderer2D，公共原生契约不包含 SDL/Core 类型。模块与原生服务契约统一使用版本 0.1.1；更新时统一重建宿主和提供者。

脚本中的资源、能力、尺寸查询以及已完成请求的结果读取（`GetDevice`、`GetPixels`）标记为 const 方法，可从只读的按值捕获调用。绘制、更新、请求轮询或取消、关闭资源仍需要可写接收对象。`GetError` 也会轮询请求；这些主动操作应使用 `mutable` 或可写引用捕获。

几何上传一次后复用。脚本顶点依次包含八个 double：X,Y,U,V,R,G,B,A；索引为经过范围检查的 int64。UpdateTexture 接受紧密排列的 array<uint8> RGBA 字节。CreateSampler 选择 Filter 和 U/V AddressMode。DrawList2D 在 AddGeometry 时保存 SetTexture、SetTint、SetTransform、SetBlendMode 的快照；PushClip 对矩形取交集，PopClip 恢复之前的裁剪。

位置采用视口内的绘制像素坐标，Y 向下；裁剪采用目标绝对像素。SetTransform 接受十六个列优先 double；GUI 布局通过 Platform DisplayScale 换算为像素。顶点颜色为线性 RGBA，默认非预乘；Tint 始终非预乘。StraightAlpha 采样非预乘纹理，PremultipliedAlpha 采样预乘纹理。在透明黑背景上绘制半透明图层会存储预乘像素，合成该图层应使用 PremultipliedAlpha。sRGB 编解码遵循纹理及目标格式。

RenderTexture 和 RenderSurface 接受 target,list,clear,R,G,B,A；带 Viewport 的方法追加 x,y,width,height。清屏作用于整个目标。保留旧内容前须先初始化纹理；窗口表面要求清屏，交换链暂不可用时可成功但不呈现。在所有者线程先关闭几何、采样器、纹理、表面和渲染器，再关闭设备；Clear 释放列表引用。拒绝已关闭、跨设备和采样当前绘制目标的资源。默认纹理及对齐传输上限 256 MiB、几何 64 MiB、命令及裁剪层数 65,536。同步回读使用 C++ API；脚本使用 `Device.ReadTextureAsync`，轮询请求并仅在成功后取得像素。

```veroscript
VS::Graphics::Service graphics = VS::Graphics::Service::Create("sdl3");
VS::Graphics::Device device = graphics.CreateDevice("");
VS::Graphics::Renderer2D renderer = device.CreateRenderer2D();
array<double> vertices = {0.0,0.0,0.0,0.0,1.0,0.0,0.0,1.0,
						 32.0,0.0,1.0,0.0,1.0,0.0,0.0,1.0,
						 0.0,32.0,0.0,1.0,1.0,0.0,0.0,1.0};
array<int64> indices = {0,1,2};
VS::Graphics::Geometry2D geometry = renderer.CreateGeometry(vertices,indices);
VS::Graphics::DrawList2D list = VS::Graphics::DrawList2D::Create();
list.PushClip(0,0,64,64);
list.AddGeometry(geometry);
list.PopClip();
VS::Graphics::Texture target = device.CreateTextureWithFormat(64,64,VS::Graphics::TextureFormat::Rgba8Unorm);
renderer.RenderTexture(target,list,true,0.0,0.0,0.0,0.0);
list.Clear();
geometry.Close();
target.Close();
renderer.Close();
device.Close();
```

SDL 窗口表面要求 SDR_LINEAR 合成，使混合与清屏保持线性色彩语义；不支持时创建表面会失败。

## 使用已安装的原生 SDK

```cmake
find_package(VeroScriptGraphics 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Graphics)
```

<a id="vertex-color-mode"></a>

SetVertexColorMode 独立于 Blend，接受 VertexColorMode::Straight（默认）或 Premultiplied。已经预乘的顶点 RGB 在插值期间保持预乘；Tint 始终非预乘；使用 PremultipliedAlpha 输出时，其 alpha 仅对 RGB 缩放一次。AddGeometry 保存此状态，Clear 恢复 Straight。顶点颜色仍须为线性值，此选项不执行 sRGB 解码。

## 合成与手动设备恢复

Composition2D 记录 AddDraw 快照、BeginLayer/EndLayer、PushMask/PopMask、AddBlur、AddColorMatrix、AddShadow 与 AddBackdropBlur。Device.CreateCompositor2D() 复用临时目标；CreateCompositor2DWithLimits(bytes,depth) 配置预算，默认 256 MiB、32 层，构建器上限 64 层及 65,536 条命令／绘制。群组透明度仅在图层完成后应用一次，嵌套及反向 alpha 遮罩取交集。模糊为两个可分离的 13 次采样通道，半径 0..64 像素；阴影保留原内容，背景模糊只替换遮罩区域。颜色矩阵含二十个行优先数值。合成中间及最终像素为线性预乘 RGBA；合成器将非预乘 ClearColor 转为预乘，加载模式要求旧内容已预乘，Opaque 绘制须写入相容的预乘像素。sRGB 转换由纹理格式处理。无效作用域／预算不足不会改写最终目标。Trim 释放缓存目标；设备前先关闭合成器。DrawList2D.SetAlphaMask、SetBlur、SetColorMatrix 与 ResetEffect 提供单通道效果；Additive 对预乘源通道相加。合成器表面方法在 composition 前额外接收当前绘制像素 width/height，Viewport 方法末尾追加 x,y,width,height；呈现失败输出 false，GetPooledBytes 返回 int64。Device.GetGeneration 与 SupportsDeviceRecovery 标识手动替换支持。Service.RecoverDevice(previous,driver) 先创建替代设备，仅在成功后使旧代失效；创建失败不改变旧代状态。成功后须重新创建／上传全部资源，先关闭旧表面再重新使用窗口，然后关闭旧资源和旧设备。Invalidate 后资源 Close 仍可调用。所选驱动可通过 Device.Poll 报告设备丢失；恢复设备及重建资源仍由宿主显式执行。此 API 不提供 Renderer3D、任意用户着色器或宿主命令录制。

合成器临时纹理默认采用 RGBA8 sRGB 存储，采样与附件混合仍在线性空间进行，避免经过线性 8 位中间纹理时丢失 sRGB 暗部颜色。C++ 可显式选择 RGBA8 UNORM 并接受其线性量化；两种脚本构造入口均使用 sRGB 默认值。ColorMatrix 与 AlphaMask 对负 UV 及大于一的 UV 遵循 Clamp/Repeat/Mirror 采样规则；Blur 与 Shadow 将源矩形外视为透明，半径按源像素计量。

## 图形 API 与异步请求

参阅[VeroScriptSdl3 — 基础实现 — 图形 API 与异步请求](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-CN/index.md#sdl-graphics-drivers)。

<a id="asynchronous-requests"></a>

CreateDeviceAsync/RecoverDeviceAsync 返回 DeviceRequest；Device.ReadTextureAsync 返回 ReadbackRequest。所属线程 Poll 返回 Pending / Succeeded / Failed / Cancelled，成功后才能 GetDevice/GetPixels，脚本 GetError 读取诊断。其他后端默认实现可能同步完成。浏览器 WebGPU 的同步创建、回读、WaitIdle 返回 graphics.async_required，必须在轮询之间让出浏览器事件循环。

<a id="resource-retirement"></a>

恢复成功才使旧设备代失效，随后重建资源。Cancel 丢弃结果，底层工作可能继续。先关闭并释放资源、设备及请求，再在所属线程收集清理，直到 Service.HasPendingRetirements() 为 false；浏览器须逐帧等待。保留 Platform 所属线程服务直到收尾完成。

<a id="graphics-capabilities"></a>

能力属于实际选择的设备／驱动。C++ 使用 `IDevice::GetCapabilities()`；脚本查询 `SupportsGuiBasic`、`SupportsTransforms`、`SupportsClipMasks`、`SupportsFilters` 和 `SupportsDeviceRecovery`。请求替换前检查恢复支持并处理失败，不能仅凭提供者名称判断。SDL3 限制由 [UIKit 设备恢复](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-CN/index.md#uikit-device-recovery)维护。

脚本顶点的八个 double 必须可表示为有限 float，int64 索引转为原生 uint32 前会检查。`UpdateTexture` 对指定区域读取恰好宽 × 高 × 4 个紧密排列的 RGBA 字节，保留其编码。`ResetTexture` 恢复纯色绘制。绘制列表修改与渲染使用所有者线程，不能并发进行。原生分配异常可能经 C++ 传播，脚本适配器将回调失败转换为诊断。回读返回实际存储的 RGBA 字节，失败时保留所提供的输出；阻塞原生回读／WaitIdle 应放在普通帧循环之外。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
