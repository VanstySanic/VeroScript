# VeroScriptAudio — 音訊服務

Audio 提供獨立原生契約及可選 `VS::Audio` 繫結。使用 `find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)` 並連結 `VeroScript::Audio`，或使用 `VeroScriptVisual` 聚合套件的 `COMPONENTS Audio`。基本契約不依賴 SDL、Threads、Platform 或 Core。指令碼繫結需要匹配 Core 和主程式提供的服務工廠。硬體存取還須安裝所選提供者；SDL 的服務為 `audio/sdl3`，契約 `veroscript.audio`，版本 0.1.1。

腳本中的能力、裝置、串流狀態與不可變緩衝區查詢標記為 const 方法，可從唯讀的傳值擷取呼叫。排入樣本、消費錄音資料、修改或關閉串流仍需要可寫接收物件；此類回呼應使用 `mutable` 或可寫參考擷取。

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## 緩衝與播放

應用資料為交錯 float32，取樣率 1..384000 Hz、1..8 聲道；腳本 `array<double>` 轉換前檢查範圍與有限性。一幀含每聲道一個樣本，緩衝最多 64 MiB，幀數、索引與預算使用 `int64`。`Buffer::Create(rate,channels,samples)` 建立不可變副本，提供 `FrameCount/SampleRate/Channels`、`GetSample(index)`、`GetSamples()`。`Buffer::FromWav(array<uint8>)` 無需裝置，支援小端 RIFF/WAVE PCM 8/16/24/32 與 IEEE 浮點 32/64。保留檔案聲道順序，不推測揚聲器對應；壓縮 WAV、extensible/RF64/RIFX、MP3、Ogg 明確不支援。

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

串流初始暫停。裝置 ID 零跟隨系統預設路由；非零指定方向相符的實體連線。SDL 轉換應用與裝置取樣率／聲道，並混合獨立串流。`Queue(buffer)` 要求相同應用格式，`QueueSamples(samples)` 直接排入陣列；播放緩衝滿時於提交前失敗。`Flush()` 釋放重取樣前瞻，不等待硬體播完。`SetGain(0..16)`、`SetPaused(bool)`、`Clear()`、冪等 `Close()` 只影響該串流。

## 錄音、裝置與生命週期

以 `Direction::Recording` 開啟並恢復後，非阻塞 `Read(maxFrames)` 回傳不可變緩衝，幀數為 0..串流預算；暫無輸入時回傳空緩衝。原生錄音溢位清空未讀資料，`Overflowed` 保留至 `Clear()`，即使腳本停止讀取也限制轉換後及原始資料。音訊執行緒不執行腳本回呼。`GetState()` 回傳 `PendingFrames/Gain/Paused/Connected/Overflowed` 快照；播放佇列不含硬體及重取樣延遲。

`GetDevices()` 快照有 `Count`、`Get(index)`；裝置具有 `Direction/DeviceId/Name/SampleRate/Channels`。`PollEvent()` 回傳 `Type/Device`，類型為 None、DeviceAdded、DeviceRemoved、DeviceChanged、TopologyReset。輪詢間變更可能合併；初始清單僅經列舉提供，收到 TopologyReset 後重新列舉。音訊不消費 Platform 事件，不初始化視訊或手把；指定裝置斷開後關閉重開，預設路由依主機後端處理。

在應用程式擁有者執行緒執行操作，保留執行階段／提供者至最後的 `CollectRetired()` 完成。異執行緒最終釋放會被延遲。部署 SDK 須包含實際音訊驅動；麥克風權限、瀏覽器使用者手勢及裝置可用性仍須符合目標平台需求。

請參閱[VeroScriptSdl3 — 基礎實作 — 音訊](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/zh-TW/index.md#audio-event-pumping)。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
