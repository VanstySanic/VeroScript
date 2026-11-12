# VeroScriptAudio — Audio services

Audio provides independent native contracts and optional `VS::Audio` bindings. Use `find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)` and `VeroScript::Audio`, or the aggregate `VeroScriptVisual` package with `COMPONENTS Audio`. The base contract needs no SDL, Threads, Platform or Core. Script bindings require the matching Core and a supplied service factory. Install the selected provider for hardware access; SDL uses `audio/sdl3`, contract `veroscript.audio`, version 0.1.1.

Script capability, device, stream-state and immutable buffer queries are const methods and work with read-only value captures. Queueing samples, consuming recorded data, changing stream state and closing streams require a writable receiver; use `mutable` or a writable reference capture for those callbacks.

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## Buffers and playback

App samples are finite interleaved float32, 1..384000 Hz and 1..8 channels; script arrays use checked `array<double>`. A frame contains one sample per channel. Buffers are bounded to 64 MiB, while frame counts, indices and queue budgets use `int64`. `Buffer::Create(rate,channels,samples)` returns an immutable copy with `FrameCount/SampleRate/Channels`, `GetSample(index)` and `GetSamples()`. `Buffer::FromWav(array<uint8>)` decodes device-free little-endian RIFF/WAVE PCM 8/16/24/32 and IEEE float 32/64. File channel order is preserved; no speaker mapping is inferred. Compressed WAV, extensible/RF64/RIFX, MP3 and Ogg fail explicitly.

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

Streams start paused. Device ID zero follows the OS default; nonzero IDs refer to a physical connection of the selected direction. SDL converts app rate/channels to the native device format and mixes separate streams. `Queue(buffer)` requires a matching app format; `QueueSamples(samples)` accepts an array directly. Playback overflow fails before submission. `Flush()` releases resampler lookahead and does not wait for audible completion. `SetGain(0..16)`, `SetPaused(bool)`, `Clear()` and idempotent `Close()` control only that stream.

## Recording and topology

Open `Direction::Recording`, resume it, then call nonblocking `Read(maxFrames)` for an immutable buffer. Counts are 0..the stream budget; unavailable input returns empty. Native capture overflow clears unread samples and latches `Overflowed` until `Clear()`, bounding app output and raw input even when no script polls. Script callbacks never run on the audio thread. `GetState()` snapshots `PendingFrames/Gain/Paused/Connected/Overflowed`; pending playback frames exclude hardware/resampler latency.

`GetDevices()` snapshots `Count` and `Get(index)`; device fields are `Direction/DeviceId/Name/SampleRate/Channels`. `PollEvent()` snapshots `Type/Device`: None, DeviceAdded, DeviceRemoved, DeviceChanged or TopologyReset. Changes between polls can coalesce; the initial list is enumeration only. Re-enumerate after TopologyReset. Audio does not consume Platform events or initialize video/controllers. Explicit-device disconnects require close/reopen; default routing follows the host backend.

Use the application owner thread; keep the runtime/provider alive through final `CollectRetired()`. Foreign-thread final release is deferred. The deployed SDK must include an actual audio driver; microphone permissions, browser user gestures and device availability remain target requirements.

See [VeroScriptSdl3 — Foundation — Audio](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/en-US/index.md#audio-event-pumping).

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
