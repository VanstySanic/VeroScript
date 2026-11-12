# VeroScriptSdl3

<a id="provider-boundary"></a>

공개 Platform/Graphics/Audio 계약은 SDL과 Core에 의존하지 않아야 합니다. SDL 구현, 런타임, 드라이버 설정 및 고지는 VeroScriptSdl3가 소유합니다. 공급자 변경 시 일치하는 계약, 설치/이동 SDK, 모듈 수명과 소유 스레드의 지연 해제를 검증합니다. 소스 확인은 실제 장치 및 플랫폼 검증을 대신하지 않습니다.

## 빌드 및 호스트 연결

```sh
cmake -S . -B build/visual -DVS_BUILD_VEROSCRIPT_EMBEDDED_ONLY=ON -DVEROSCRIPT_BUILD_MOBILE_ADAPTERS=OFF -DVEROSCRIPT_BUILD_VISUAL=ON
cmake --build build/visual --config Release
cmake --install build/visual --config Release --component VeroScriptVisual --prefix out/visual-sdk
```

## 유지보수와 검증

`VEROSCRIPT_SDL3_GRAPHICS`가 그래픽 서비스 등록을 선택합니다. 드라이버는 `VEROSCRIPT_SDL3_GPU`, `VEROSCRIPT_SDL3_D3D11`, `VEROSCRIPT_SDL3_OPENGL`, `VEROSCRIPT_SDL3_WEBGPU`로 별도 제어하며 WebGPU 기본값은 OFF입니다. 컨트롤러와 오디오는 `VEROSCRIPT_SDL3_CONTROLLERS`, `VEROSCRIPT_SDL3_AUDIO`를 사용합니다. 네이티브 WebGPU는 `VEROSCRIPT_WEBGPU_ROOT`의 고정 wgpu-native v29.0.1.1, 브라우저는 고정 Emdawn 포트를 사용합니다. 빌드 지원과 실제 검증 결과를 구분합니다.

<a id="sdl-audio-build"></a>

루트 오디오 빌드는 `VEROSCRIPT_BUILD_AUDIO=ON`, SDL 구현은 추가로 `VEROSCRIPT_BUILD_SDL3=ON`, `VEROSCRIPT_SDL3_AUDIO=ON`을 사용합니다. 기반 단독 빌드도 Audio를 지원합니다. 실제 Linux 드라이버는 더미 장치 테스트와 별도로 검증합니다.

`VEROSCRIPT_SDL3_CONTROLLERS` 기본값은 ON이며 OFF는 네이티브 컨트롤러만 비활성화합니다. 장치당 한 번 열고 다른 스레드의 최종 해제도 소유 스레드에서 회수합니다. 호스트의 백그라운드 입력 설정은 변경하지 않습니다. 자이로, 패드 터치패드, 트랙볼과 고급 힘 피드백은 포함하지 않습니다. 오디오는 `VEROSCRIPT_SDL3_AUDIO`로 별도 활성화하며 카메라, SDL_Renderer와 별도 Haptic은 비활성화 상태를 유지합니다. 버전은 유지하며 호스트/공급자/바인딩/SDK를 함께 다시 빌드합니다.

`VeroScriptSdl3ControllerTests`는 SDL 가상 장치로 열거, 핫플러그, 매핑, 독립 상태, 진동, 범위와 해제를 검증합니다. 메모리 공급자 및 스크립트 테스트도 포함됩니다. 물리 장치와 Apple/모바일/브라우저 드라이버는 별도 실기기 검증이 필요합니다.

`VeroScriptSdl3OpenGl.cpp`는 `SDL_PLATFORM_ANDROID`, `SDL_PLATFORM_IOS`, `SDL_PLATFORM_TVOS`에서 OpenGL ES와 호환 Platform 창 하나를 요구하고 숨겨진 보조 창은 만들지 않습니다. Android는 유지한 EGL 컨텍스트 수와 표면 표시 점유를 분리해 교체 확정/취소 전 이전 장치를 보존하며 UIKit은 배타적 단일 컨텍스트 규칙을 유지합니다. Android는 SDLActivity/`SDL_main`의 네이티브 이벤트 스레드 연동으로 초기화합니다. Java UI 준비 완료는 SDL 소유 스레드 초기화와 다릅니다. 다른 플랫폼은 애플리케이션 주 스레드 초기화가 계속 필요합니다. 창·장치·이벤트·지연 정리의 소유 스레드를 지키고 SDK/장치 동작을 별도로 검증하세요.

[README](../../../../README.md) · [User](../../User/ko-KR/index.md)

합성 테스트는 이 SDL 경로를 우회합니다.

`VEROSCRIPT_SDL3_GRAPHICS`는 활성 그래픽 드라이버에서 계산됩니다. 이 파생 값을 직접 덮어쓰지 말고 드라이버 옵션을 설정합니다.

SDL_GPU 렌더러는 회수된 정점/인덱스 버퍼 쌍을 최대 128쌍 / 8 MiB 및 MaxGeometryBytes 범위에서 재사용합니다. 업로드 저장소도 재사용하며 전송 매핑과 대상 업로드의 cycling으로 대기 중인 GPU 작업이 참조하는 저장소를 보존합니다. 렌더러를 닫으면 유휴 저장소를 해제합니다. 남은 지오메트리는 기존 소유권과 소유 스레드 회수 규칙을 유지합니다.

<a id="uikit-recovery-capability"></a>

`Graphics/VeroScriptSdl3GraphicsBackend.cpp::GetCapabilities`를 컨텍스트 소유권과 일치시키세요. SDL_PLATFORM_IOS/SDL_PLATFORM_TVOS에서 opengles만 bDeviceRecovery를 끕니다. 공급자 ID나 나머지 일곱 활성 기능으로 복구를 추론하면 안 됩니다. UIKit은 이전 EAGL 소유자와 동시에 교체 장치를 만들 수 없으며 Android의 독립 보유 컨텍스트는 별도 계약입니다. 런타임 기능 보고와 공개 복구 문서를 함께 유지하세요.

<a id="native-provider-maintenance"></a>

제공자 소스는 SDL 전용 서비스 설명자와 정적 query `veroscript_sdl3_query`를 소유하며 네이티브 확장 빌드는 `veroscript_extension_query`를 노출합니다. 빌드에 포함된 서비스/그래픽 드라이버만 등록하고 각 공개 계약과 실제 기능을 따로 확인하세요. shader 입력, 드라이버 의존성과 lease는 독립 계약이 아닌 SDL3에서 관리합니다. 일치하는 소비자로 정적/동적 검색, 입력, 그래픽, 오디오와 소유 스레드 해제를 검증하며 합성/더미 드라이버 검사를 실제 장치 인증으로 취급하지 않습니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
