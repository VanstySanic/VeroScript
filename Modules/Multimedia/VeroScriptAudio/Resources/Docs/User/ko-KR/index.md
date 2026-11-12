# VeroScriptAudio — 오디오 서비스

Audio는 독립적인 네이티브 계약과 선택적 `VS::Audio` 바인딩을 제공합니다. `find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED)`와 `VeroScript::Audio` 또는 통합 `VeroScriptVisual` 패키지의 `COMPONENTS Audio`를 사용합니다. 기본 계약에는 SDL, Threads, Platform, Core가 필요하지 않습니다. 스크립트 바인딩에는 일치하는 Core와 호스트가 공급한 서비스 팩터리가 필요합니다. 하드웨어용 공급자를 설치해야 하며 SDL은 `audio/sdl3`, 계약 `veroscript.audio`, 버전 0.1.1을 사용합니다.

스크립트의 기능, 장치, 스트림 상태 및 불변 버퍼 조회 메서드는 const이므로 읽기 전용 값 캡처에서도 호출할 수 있습니다. 샘플 추가, 녹음 데이터 소비, 스트림 변경 및 닫기에는 쓰기 가능한 수신 객체가 필요하므로 해당 콜백에서는 `mutable` 또는 쓰기 가능한 참조 캡처를 사용하세요.

`COMPONENTS Bindings` → `VeroScript::AudioBindings` + `VeroScript::Core`.

## 버퍼와 재생

애플리케이션 데이터는 유한한 인터리브 float32, 1..384000 Hz, 1..8채널입니다. 스크립트의 `array<double>`은 검증 후 변환합니다. 프레임은 채널당 샘플 하나를 포함하고 버퍼는 64 MiB 이내이며 개수/인덱스/예산은 `int64`입니다. `Buffer::Create(rate,channels,samples)`는 불변 복사본을 생성하며 `FrameCount/SampleRate/Channels`, `GetSample(index)`, `GetSamples()`를 제공합니다. `Buffer::FromWav(array<uint8>)`는 장치 없이 리틀엔디언 RIFF/WAVE PCM 8/16/24/32와 IEEE 부동소수점 32/64를 디코딩합니다. 파일 채널 순서를 유지하고 스피커 배치를 추정하지 않습니다. 압축 WAV, extensible/RF64/RIFX, MP3, Ogg는 명시적으로 거부합니다.

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

스트림은 일시정지 상태로 시작합니다. ID 0은 OS 기본 경로를 따르며 다른 ID는 방향이 맞는 물리 연결입니다. SDL은 레이트/채널을 변환하고 독립 스트림을 혼합합니다. `Queue(buffer)`는 같은 애플리케이션 형식이어야 하며 `QueueSamples(samples)`는 배열을 직접 받습니다. 재생 큐 초과는 제출 전에 실패합니다. `Flush()`는 리샘플러 선행 버퍼를 해제하며 실제 재생 완료를 기다리지 않습니다. `SetGain(0..16)`, `SetPaused(bool)`, `Clear()`, 멱등 `Close()`는 해당 스트림에만 적용됩니다.

## 녹음과 수명

`Direction::Recording`으로 열고 재개한 뒤 논블로킹 `Read(maxFrames)`로 불변 버퍼를 얻습니다. 개수는 0..스트림 예산이며 데이터가 없으면 빈 버퍼입니다. 네이티브 녹음 초과는 읽지 않은 데이터를 지우고 `Overflowed`를 `Clear()`까지 유지합니다. 스크립트가 읽지 않아도 변환 후/원시 입력 크기를 제한하며 오디오 스레드에서 스크립트 콜백을 실행하지 않습니다. `GetState()`는 `PendingFrames/Gain/Paused/Connected/Overflowed` 스냅샷입니다. 재생 대기 프레임은 장치/리샘플러 지연을 포함하지 않습니다.

`GetDevices()`는 `Count`, `Get(index)`를 제공하고 장치는 `Direction/DeviceId/Name/SampleRate/Channels`를 가집니다. `PollEvent()`의 `Type/Device` 종류는 None, DeviceAdded, DeviceRemoved, DeviceChanged, TopologyReset입니다. 폴링 사이 변경은 병합될 수 있습니다. 초기 장치는 열거로 가져오고 TopologyReset 뒤 다시 열거합니다. Platform 이벤트를 소비하거나 영상/컨트롤러를 초기화하지 않습니다. 지정 장치 연결이 끊기면 닫고 다시 열며 기본 경로는 백엔드를 따릅니다.

애플리케이션 소유 스레드에서 작업하고 마지막 `CollectRetired()`까지 런타임과 공급자를 유지합니다. 다른 스레드의 최종 해제는 지연됩니다. 배포 SDK에 실제 오디오 드라이버가 필요하며 마이크 권한, 브라우저 사용자 동작 및 장치 가용성은 대상 요구사항입니다.

[VeroScriptSdl3 — 기반 구현 — 오디오](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md#audio-event-pumping)을 참조하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
