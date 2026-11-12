# VeroScriptAudio — 音声サービス

Audio は独立したネイティブ契約と任意の `VS::Audio` バインディングを提供します。`find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)` と `VeroScript::Audio`、または統合パッケージ `VeroScriptVisual` の `COMPONENTS Audio` を使用します。基本契約は SDL、Threads、Platform、Core に依存しません。スクリプトには一致する Core とホストが渡すサービスファクトリーが必要です。機器を使うには選択したプロバイダーを導入します。SDL は `audio/sdl3`、契約 `veroscript.audio`、版 0.1.1 です。

スクリプトの機能、デバイス、ストリーム状態、不変バッファの取得メソッドは const で、読み取り専用の値キャプチャから呼び出せます。サンプルの追加、録音データの消費、ストリームの変更や終了には書き込み可能な受信側が必要です。そのようなコールバックでは `mutable` または書き込み可能な参照キャプチャを使用してください。

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## バッファーと再生

入力は有限のインターリーブ float32、1..384000 Hz、1..8 チャンネルです。スクリプトの `array<double>` は検証後に変換します。フレームは各チャンネルの標本一つずつで、上限 64 MiB、件数・添字・予算は `int64` です。`Buffer::Create(rate,channels,samples)` は不変コピーを作り、`FrameCount/SampleRate/Channels`、`GetSample(index)`、`GetSamples()` を提供します。`Buffer::FromWav(array<uint8>)` は機器なしでリトルエンディアン RIFF/WAVE PCM 8/16/24/32 と IEEE 浮動小数点 32/64 を復号します。ファイルのチャンネル順を維持し、スピーカー位置は推測しません。圧縮 WAV、extensible/RF64/RIFX、MP3、Ogg は未対応です。

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

ストリームは一時停止で開始します。ID ゼロは OS の既定経路、非ゼロは方向が一致する物理接続です。SDL は周波数とチャンネルを変換し、独立ストリームを混合します。`Queue(buffer)` は同じアプリ形式を要求し、`QueueSamples(samples)` は配列を直接送ります。予算超過は送信前に失敗します。`Flush()` は再サンプリングの先読みを解放し、可聴終了を待ちません。`SetGain(0..16)`、`SetPaused(bool)`、`Clear()`、冪等な `Close()` はそのストリームだけに作用します。

## 録音と寿命

`Direction::Recording` を開いて再開後、非ブロッキング `Read(maxFrames)` で不変バッファーを取得します。件数はゼロから予算まで、未到着なら空です。ネイティブ録音あふれは未読データを消去し、`Overflowed` を `Clear()` まで保持します。スクリプトが読まなくても変換後と生データを制限し、音声スレッドでスクリプトを呼びません。`GetState()` は `PendingFrames/Gain/Paused/Connected/Overflowed` のスナップショットです。再生待ち件数は機器と再サンプリングの遅延を含みません。

`GetDevices()` は `Count`、`Get(index)` を提供し、機器は `Direction/DeviceId/Name/SampleRate/Channels` を持ちます。`PollEvent()` は `Type/Device` を返し、種類は None、DeviceAdded、DeviceRemoved、DeviceChanged、TopologyReset です。ポーリング間の変更は集約され得ます。初期一覧は列挙から取得し、TopologyReset 後は再列挙します。Platform のイベントを消費せず、映像やパッドを初期化しません。指定機器が切断されたら閉じて再作成し、既定経路はバックエンドに従います。

操作はアプリケーション所有スレッドで行い、最後の `CollectRetired()` までランタイムとプロバイダーを保持します。別スレッドの最終解放は遅延されます。配布 SDK には実際の音声ドライバーが必要です。マイク権限、ブラウザー操作、機器の利用可否はターゲットの要件です。

[VeroScriptSdl3 — 基盤実装 — 音声](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ja-JP/index.md#audio-event-pumping) を参照してください。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
