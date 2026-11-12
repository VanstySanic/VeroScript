# VeroScriptAudio — 音频服务

Audio 提供独立原生契约及可选 `VS::Audio` 绑定。使用 `find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)` 并链接 `VeroScript::Audio`，或使用 `VeroScriptVisual` 聚合包的 `COMPONENTS Audio`。基础契约不依赖 SDL、Threads、Platform 或 Core。脚本绑定需要匹配 Core 和宿主提供的服务工厂。硬件访问还须安装所选提供者；SDL 的服务为 `audio/sdl3`，契约 `veroscript.audio`，版本 0.1.1。

脚本中的能力、设备、流状态和不可变缓冲查询标记为 const 方法，可从只读的按值捕获调用。排入样本、消费录音数据、修改或关闭流仍需要可写接收对象；此类回调应使用 `mutable` 或可写引用捕获。

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## 缓冲与播放

应用数据是交错 float32，采样率 1..384000 Hz、1..8 声道；脚本 `array<double>` 转换前检查范围和有限性。一帧包含每声道一个样本，缓冲不超过 64 MiB，帧数、索引和预算为 `int64`。`Buffer::Create(rate,channels,samples)` 生成不可变副本，提供 `FrameCount/SampleRate/Channels`、`GetSample(index)`、`GetSamples()`。`Buffer::FromWav(array<uint8>)` 无需设备，支持小端 RIFF/WAVE PCM 8/16/24/32 与 IEEE 浮点 32/64。声道保留文件顺序，不推测扬声器映射；压缩 WAV、extensible/RF64/RIFX、MP3、Ogg 明确不支持。

```veroscript
VS::Audio::Service audio = VS::Audio::Service::Create("sdl3");
array<double> samples = {0.0, 0.1, -0.1, 0.0};
VS::Audio::Buffer buffer = VS::Audio::Buffer::Create(48000, 1, samples);
VS::Audio::Stream stream = audio.OpenStream(VS::Audio::Direction::Playback, 0u64, 48000, 1, 96000);
stream.Queue(buffer);
stream.SetGain(0.5);
stream.Flush();
stream.SetPaused(false);
```

流初始暂停。设备 ID 为零时跟随系统默认设备，非零标识指定方向的物理连接。SDL 负责应用格式到设备格式的采样率／声道转换，并混合独立流。`Queue(buffer)` 要求应用格式一致，`QueueSamples(samples)` 直接排入数组；播放缓冲满时在提交前失败。`Flush()` 释放重采样前瞻，不等待硬件播放结束。`SetGain(0..16)`、`SetPaused(bool)`、`Clear()`、幂等 `Close()` 只影响该流。

## 录音、设备与寿命

用 `Direction::Recording` 打开并恢复后，非阻塞 `Read(maxFrames)` 返回不可变缓冲。帧数范围为 0..该流预算，没有可用输入时返回空缓冲。原生录音队列溢出会清空未读样本，`Overflowed` 保留至 `Clear()`，即使脚本停止读取也限制转换后和原始数据。音频线程不执行脚本回调。`GetState()` 返回 `PendingFrames/Gain/Paused/Connected/Overflowed` 快照；播放待处理帧数不包含硬件和重采样延迟。

`GetDevices()` 返回含 `Count`、`Get(index)` 的快照，设备具有 `Direction/DeviceId/Name/SampleRate/Channels`。`PollEvent()` 返回 `Type/Device`，类型为 None、DeviceAdded、DeviceRemoved、DeviceChanged、TopologyReset。两次轮询间的变化可能合并；初始设备只经枚举提供。收到 TopologyReset 后重新枚举。音频不消费 Platform 事件，不初始化视频或手柄；指定设备断开后关闭重开，默认路由遵循宿主后端。

在应用所有者线程执行操作，保留运行时／提供者至最后的 `CollectRetired()` 完成。异线程最终释放会被延迟。部署 SDK 须包含真实音频驱动；麦克风权限、浏览器用户手势及设备可用性仍须满足目标平台要求。

参阅[VeroScriptSdl3 — 基础实现 — 音频](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-CN/index.md#audio-event-pumping)。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
