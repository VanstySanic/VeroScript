# VeroScriptSdl3 — 기반 구현

스크립트 호스트는 VM 백엔드를 명시적으로 선택해야 합니다. Platform, Graphics 또는 Audio 바인딩을 등록해도 실행기는 자동 선택되지 않습니다. 소스 예제 실행기는 `VeroScriptVisualExample --vm-backend <VM-라이브러리-또는-디렉터리> program.vs`로 실행합니다. `--check program.vs`는 VM 없이 일반 스크립트를 검사합니다. VM을 지정하면 컴파일과 실행에 같은 팩터리를 전달하므로 스크립트 컴파일 처리기도 선택한 백엔드를 사용합니다. 네이티브 서비스 SDK는 특정 VM 구현에 의존하지 않습니다.

SDL3 백엔드는 SDL 3.4.18과 공급자 ID `sdl3`를 사용합니다. `platform/sdl3`를 제공하며 설치한 패키지에 기능이 포함된 경우 `graphics/sdl3`와 `audio/sdl3`도 제공합니다. Platform, Graphics, Audio는 SDL 런타임을 공유합니다.

등록만으로 SDL을 초기화하지 않습니다. 서비스 생성과 작업은 애플리케이션 주/소유 스레드에서 수행합니다. 오프스크린 렌더링에도 사용할 수 있는 비디오/그래픽 드라이버가 필요합니다. 보유 리소스는 공급자 수명을 유지하며 종료 전에 소유 스레드에서 지연 해제를 마쳐야 합니다. 사용 가능 여부는 설치한 SDK, OS 및 장치에 따라 달라집니다.

## 설치한 네이티브 SDK 사용

```cmake
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Sdl3)
```

`VeroScript::Platform`, `VeroScript::Graphics`, `VeroScript::Sdl3`; `VeroScript::PlatformBindings` / `VeroScript::GraphicsBindings` → `VeroScript::Core`.

[VeroScriptGraphics — 그래픽 리소스와 2D 그리기](../../../../../../VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md#graphics-service-contract)을 참조하세요.

[VeroScriptGraphics — 그래픽 리소스와 2D 그리기 — 설치한 네이티브 SDK 사용](../../../../../../VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md#vertex-color-mode)을 참조하세요.

## 입력 장치

[Platform: 여러 입력 장치](../../../../../../VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md)

[VeroScriptPlatform — 기반 구현 — 여러 입력 장치](../../../../../../VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md#input-device-snapshots)을 참조하세요.

연결 해제, 포커스 상실, 수명 주기 변경, 창 파괴 시 해당 상태를 지우고 원인 이벤트 전에 합성 해제/취소를 전달합니다. `ClearInputState(windowId)`는 지정 창, 0이면 전체를 지우며 해당 범위의 대기 중 정규화 입력을 제거하고 `InputStateReset`을 생성합니다. 호스트가 제출한 초기화는 후속 이벤트 순서를 보존합니다. 합성 전역 초기화는 알림 손실 후 재동기화에도 사용하므로 호출자는 캡처/눌림 상태를 지우고 다시 열거해야 합니다. 외부 서비스 상태는 호스트 입력별로 독립적입니다. SDL은 이벤트 처리/열거 시 250 ms 간격으로 터치 구성을 확인하며 키보드/마우스는 순서가 있는 연결 이벤트를 따릅니다. 전용 펜은 구현 범위 밖입니다. 버전은 유지하며 네이티브 호스트와 공급자를 함께 다시 빌드합니다.

[VeroScriptPlatform — 기반 구현 — 여러 입력 장치](../../../../../../VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md#input-device-snapshots)을 참조하세요.

SDL은 전역 스캔 코드 상태로 키 전환을 중복 제거합니다. 여러 물리 키보드가 같은 키를 겹쳐 누르면 두 번째 누름이 반복으로 표시되고 후속 해제가 누락될 수 있습니다. 장치 ID나 raw 모드만으로 물리 키보드별 완전한 상태를 보장하지 않습니다. 스냅샷은 수신 이벤트를 추적합니다. 완전한 물리 상태가 필요하면 모든 전환을 제출하는 외부 호스트를 사용하세요.

## 호스트 창, 커서와 캡처

공통 스크립트 API는 Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture(), IsBorrowed()를 제공합니다. Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows는 API 지원을 나타내며 드라이버가 개별 작업을 거부할 수 있습니다. SDL 캡처에는 같은 창의 마우스 및 키보드 포커스가 모두 필요합니다. 포커스 상실, 숨김, 최소화, 백그라운드, 종료 및 입력 초기화 시 캡처를 해제합니다. 커서 설정은 마우스 포커스 또는 캡처가 있을 때 적용합니다. 드래그 캡처 중에는 창 밖에서도 커서를 유지하며, 그 외에는 보이는 기본 커서로 복원합니다. 호스트는 SDL 전역 커서와 캡처 소유권을 조정해야 합니다. C++ IService.AttachWindow는 FBorrowedWindowOptions의 HandleKind="sdl3.window", 유효한 SDL_Window 주소 및 비어 있지 않은 공유 Owner를 받습니다. 주소는 스크립트에 공개하지 않습니다. Close는 등록만 해제하고 호스트 창을 파괴하지 않습니다. 네이티브 창 파괴 시 래퍼를 무효화하고 관측한 입력을 지웁니다. 호스트 창을 파괴하기 전에 그래픽 표면을 닫아야 합니다. Owner는 지연 정리 기록이 파괴될 때까지 유지되지만 호스트의 명시적인 조기 파괴를 막지는 못합니다.

시스템 커서에는 기본 네이티브 커서가 필요합니다. 현재 SDL의 명시적 캡처는 windows, cocoa, x11 드라이버에서만 지원을 선언합니다. 다른 드라이버에서는 지원되지 않는 SDL 캡처를 요청하지 않고 GUI 논리 캡처를 유지합니다.

<a id="host-window-retirement"></a>

SDL 백엔드는 네이티브 파괴 이벤트에서 GPU 창 점유를 해제하며 다른 스레드에서 지연 정리 중인 표면도 처리합니다. 이후 Close는 멱등성을 유지합니다. SDL_EVENT_WINDOW_DESTROYED가 활성화되어 있고 필터링되지 않아야 관측할 수 있습니다. 이벤트를 차단하는 호스트는 네이티브 창 파괴 전에 표면과 래퍼를 닫아야 합니다.

## 그래픽 API와 비동기 요청

<a id="sdl-graphics-drivers"></a>

설치한 공급자가 제공하는 드라이버를 선택합니다. Windows의 `direct3d11` / `direct3d11-warp`, `opengl`(3.3 core), `opengles`(ES 3.0), 선택적 `webgpu`를 사용할 수 있습니다. SDL_GPU는 지원 대상에서 Direct3D 12, Vulkan, Metal을 제공합니다. 선택한 서비스에 조회하고 없는 드라이버는 오류로 처리합니다.

FWindowOptions.GraphicsApi 또는 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi)로 호환 창을 만듭니다. 빌린 창은 이미 해당 API와 호환되어야 합니다.

Android와 선택적 iOS/iPadOS/tvOS `opengles` 드라이버는 장치를 만들기 전에 `GraphicsApi="opengles"`인 호환 Platform 창을 정확히 하나 만듭니다. Android EGL 컨텍스트는 창을 독립적으로 보유하므로 이전 장치를 유지한 채 복구용 새 장치를 준비할 수 있습니다. 표시 소유권을 갖는 표면은 동시에 하나뿐입니다. UIKit은 창에 컨텍스트 하나를 보유합니다. 장치는 보유한 창의 표면만 받습니다. 장치/창보다 먼저 표면과 리소스를 닫으며 창을 닫으면 해당 장치가 무효화됩니다. 기본 SDL_GPU는 별도 호스팅 경로를 사용합니다.

[VeroScriptGraphics — 그래픽 리소스와 2D 그리기 — 그래픽 API와 비동기 요청](../../../../../../VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md#asynchronous-requests)을 참조하세요.

[VeroScriptGraphics — 그래픽 리소스와 2D 그리기 — 그래픽 API와 비동기 요청](../../../../../../VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md#resource-retirement)을 참조하세요.

WebGPU에는 일치하는 네이티브 런타임 또는 브라우저 포트와 원본 라이선스 고지가 필요합니다. 브라우저는 비동기 생성과 프레임 간 제어 반환이 필요합니다. 네이티브와 브라우저 지원은 SDK와 대상별로 확인해야 합니다.

CreateDevice와 CreateDeviceAsync는 기본 API 선택을 공유합니다. 네이티브 WebGPU 전용 빌드는 드라이버 이름이 비어 있어도 비동기 요청을 사용합니다. DX11 Device.Poll은 장치 손실을 감지하고 리소스를 무효화하며, 복구 후에는 리소스를 다시 만들어야 합니다. WebGPU는 완료 전후 및 반복 취소를 지원하고, GetPixels 실패 시 출력은 바뀌지 않습니다.

## 게임패드와 진동

[VeroScriptPlatform — 기반 구현 — 게임패드와 진동](../../../../../../VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md#controllers-and-feedback)을 참조하세요.

## 오디오

오디오 기능이 포함된 SDL 공급자는 `audio/sdl3`(`veroscript.audio`, 0.1.1)를 제공합니다. `CreateAudio`는 공유 SDL 런타임에서 오디오만 초기화합니다. `VS::Audio`는 재생/녹음 스트림, 장치 스냅샷과 병합 가능한 연결 변경, 크기가 제한된 PCM 큐, 음량/일시정지를 제공합니다. SDL이 샘플 레이트와 채널을 변환하고 공용 Audio 모듈은 장치 없이 비압축 PCM/부동소수점 RIFF/WAVE를 디코딩합니다.

스트림은 일시정지 상태로 시작하며 서로 독립적입니다. 녹음 초과 시 읽지 않은 데이터를 비우고 플래그를 유지합니다. 오디오 스레드는 스크립트 콜백을 실행하지 않습니다. 스트림을 닫고 소유 스레드에서 지연 해제를 마칩니다. 배포한 Linux SDK에는 ALSA/PulseAudio 같은 실제 드라이버가 필요합니다. 마이크 권한과 브라우저 사용자 동작은 호스트 책임이며 장치 가용성은 실제 대상에서 확인합니다.

<a id="audio-event-pumping"></a>

소유 스레드의 애플리케이션 루프에서 `PollEvent()` 또는 `GetDevices()`를 주기적으로 호출하세요. SDL3 어댑터는 호스트의 SDL 이벤트를 꺼내지 않고 메인 스레드 콜백과 네이티브 알림을 처리하므로 창이 없는 오디오 전용 앱에서도 장치 오류와 기본 경로 전환을 처리합니다. 연결 상태는 현재 장치 열거를 기준으로 하며, 이전 스트림이 유지하는 형식만으로 연결 여부를 판단하지 않습니다.

<a id="uikit-device-recovery"></a>

iOS/iPadOS/tvOS의 `opengles` 장치는 `bDeviceRecovery == false`(스크립트의 `SupportsDeviceRecovery()`)를 보고합니다. 창이 EAGL 컨텍스트 하나만 보유하므로 이전 장치를 유지한 채 RecoverDevice/RecoverDeviceAsync로 교체를 준비하지 마세요. 이전 표면·리소스·장치를 닫고 참조를 해제한 뒤 소유 스레드의 지연 해제를 완료하고 새 장치를 생성하여 자산을 다시 업로드합니다. 다른 드라이버는 자체 장치 기능을 가지며 Android EGL 교체 규칙이 UIKit 복구 지원을 뜻하지 않습니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
