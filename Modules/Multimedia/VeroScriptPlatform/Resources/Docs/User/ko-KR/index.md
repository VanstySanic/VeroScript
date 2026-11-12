# VeroScriptPlatform — 기반 구현

Platform은 창, 논리／픽셀 크기, 키보드, 마우스, 터치, UTF-8 텍스트／IME 및 클립보드 계약을 제공합니다. 네이티브 계약은 Core나 SDL에 의존하지 않으며 선택적 스크립트 어댑터가 `VS::Platform`을 제공합니다.

스크립트의 기능 확인, 창 크기 및 입력 스냅샷 조회 메서드는 const이므로 읽기 전용 값 캡처에서도 호출할 수 있습니다. 이벤트 소비, 입력 상태 초기화 및 창 변경에는 쓰기 가능한 수신 객체가 필요하므로 해당 콜백에서는 `mutable` 또는 쓰기 가능한 참조 캡처를 사용하세요.

서비스는 앱의 주 스레드에서 만듭니다. 공유 네이티브 이벤트 큐는 한 번만 처리하거나 외부 모드에서 정규화된 이벤트를 제출합니다. 그래픽 표면을 먼저 닫고 창을 닫습니다. 종료 전에 소유 스레드에서 지연 해제 리소스를 회수해야 합니다.

## 설치한 네이티브 SDK 사용

```cmake
find_package(VeroScriptPlatform 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Platform)
```

창 좌표는 네이티브 플랫폼을 따릅니다. `ScaleX/ScaleY`는 창 좌표를 그리기 픽셀로 변환하며 `DisplayScale`은 권장 UI 배율입니다. 두 배율은 다를 수 있습니다. 밀도 독립 입력은 `guiX = windowX * ScaleX / DisplayScale`, 그리기는 `pixelX = guiX * DisplayScale`을 사용합니다. 스크립트에서는 `window.GetDisplayScale()`을 조회하고 픽셀 크기나 표시 배율이 바뀌면 정보를 갱신합니다.

## 여러 입력 장치

<a id="input-device-snapshots"></a>

`service.GetInputDevices()`는 `int64 Count`와 `Get(index)`가 있는 스냅샷을 반환합니다. 장치 필드는 `Kind`, `DeviceId`, `Name`, `Aggregate`, `Connected`입니다. `service.GetInputState(kind,id)`는 불변 스냅샷이며 `Device`, `KeyCount`, `PointerCount`, `Modifiers`, `GetKey(index)`, `GetPointer(index)`, `IsKeyDown(Key)`, `IsScanCodeDown(int)`를 제공합니다. 키 필드는 `Key/ScanCode/WindowId`, 포인터 필드는 `PointerId/WindowId/X/Y/Pressure/Buttons`이며 인덱스를 검사합니다. 기능 확인은 `SupportsInputDevices/SupportsInputState/SupportsDeviceEvents()`입니다.

장치 식별은 `(InputDeviceKind,DeviceId)`이며 접촉점 ID는 장치 내부에서 유효합니다. 종류는 `Unknown/Keyboard/Mouse/Touch/Joystick/Gamepad`입니다. ID 0은 집계 입력을 명시합니다. 물리 마우스를 열거해도 운영체제가 마우스별 이벤트 ID를 제공한다고 보장하지 않습니다. 상태는 이 서비스가 소비한 이벤트에서 생성되므로 조회 전에 이벤트를 처리합니다. 수정 키는 장치 자체에서 관측한 눌림 상태이며 시스템 잠금 상태는 포함하지 않습니다. 연결 이벤트는 `InputDeviceAdded/Removed`, 추가 필드는 `DeviceKind`, `DeviceName`, `AggregateDevice`, `Synthetic`입니다.

연결 해제, 포커스 상실, 수명 주기 변경, 창 파괴 시 해당 상태를 지우고 원인 이벤트 전에 합성 해제/취소를 전달합니다. `ClearInputState(windowId)`는 지정 창, 0이면 전체를 지우며 해당 범위의 대기 중 정규화 입력을 제거하고 `InputStateReset`을 생성합니다. 호스트가 제출한 초기화는 후속 이벤트 순서를 보존합니다. 합성 전역 초기화는 알림 손실 후 재동기화에도 사용하므로 호출자는 캡처/눌림 상태를 지우고 다시 열거해야 합니다. 외부 서비스 상태는 호스트 입력별로 독립적입니다. 네이티브 장치 검색 방식과 지원 종류는 선택한 백엔드가 정의합니다. 물리 이벤트 제한은 해당 문서를 참고하세요. 버전은 유지하며 네이티브 호스트와 공급자를 함께 다시 빌드합니다.

0이 아닌 장치가 연결 해제되면 물리적 출처를 알 수 없으므로 같은 종류의 집계 입력 눌림 상태도 보수적으로 해제합니다. 남아 있는 집계 눌림은 후속 이벤트로 다시 관측해야 합니다. 해제 이벤트의 대상이나 키보드 배열이 달라지면 원래 소유자에게도 합성 해제/취소를 전달합니다.

`COMPONENTS Platform` → `VeroScript::Platform`; `COMPONENTS PlatformBindings` → `VeroScript::PlatformBindings` + `VeroScript::Core`.

## 호스트 창, 커서와 캡처

공통 스크립트 API는 Window.SetCursor(Cursor,bool visible), SetMouseCapture(bool), HasMouseCapture(), IsBorrowed()를 제공합니다. Service.SupportsSystemCursor/SupportsMouseCapture/SupportsBorrowedWindows는 API 지원을 나타내며 드라이버가 개별 작업을 거부할 수 있습니다. SDL 캡처에는 같은 창의 마우스 및 키보드 포커스가 모두 필요합니다. 포커스 상실, 숨김, 최소화, 백그라운드, 종료 및 입력 초기화 시 캡처를 해제합니다. 커서 설정은 마우스 포커스가 있을 때 적용하고 벗어나면 보이는 기본 커서로 복원합니다. 호스트는 SDL 전역 커서와 캡처 소유권을 조정해야 합니다. C++ IService.AttachWindow는 FBorrowedWindowOptions의 HandleKind="sdl3.window", 유효한 SDL_Window 주소 및 비어 있지 않은 공유 Owner를 받습니다. 주소는 스크립트에 공개하지 않습니다. Close는 등록만 해제하고 호스트 창을 파괴하지 않습니다. 네이티브 창 파괴 시 래퍼를 무효화하고 관측한 입력을 지웁니다. 호스트 창을 파괴하기 전에 그래픽 표면을 닫아야 합니다. Owner는 지연 정리 기록이 파괴될 때까지 유지되지만 호스트의 명시적인 조기 파괴를 막지는 못합니다.

[VeroScriptSdl3 — 기반 구현 — 호스트 창, 커서와 캡처](../../../../../Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md#host-window-retirement)을 참조하세요.

## 그래픽 API와 비동기 요청

FWindowOptions.GraphicsApi 또는 Service.CreateWindowForGraphics(title,width,height,visible,graphicsApi)로 호환 창을 만듭니다. 빌린 창은 이미 해당 API와 호환되어야 합니다.

[VeroScriptGraphics — 그래픽 리소스와 2D 그리기 — 그래픽 API와 비동기 요청](../../../../../VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md#resource-retirement)을 참조하세요.

## 게임패드와 진동

<a id="controllers-and-feedback"></a>

`InputDeviceKind::Gamepad`는 표준 `GamepadAxis/GamepadButton`, `Joystick`은 장치 고유의 0 기반 축·버튼·햇을 제공합니다. 물리 장치마다 입력 스트림은 하나이며 매핑 변경 시 이전 구성을 해제하고 새 구성을 알립니다. 다시 열거해야 합니다. 0이 아닌 ID는 연결별 식별자입니다. 컨트롤러 입력은 전역 (`WindowId=0`)이며 이벤트 처리 후 `(Kind,DeviceId)` 상태를 읽습니다.

연결 정보는 `AxisCount/ButtonCount/HatCount/SupportsRumble`(C++ `bRumble`)을 제공합니다. 표준 패드는 축 6개와 버튼 26개 슬롯이며 없는 입력은 중립입니다. 네이티브 상태는 `Axes/ControllerButtons/Hats`, 스크립트는 `GetAxis(int64)` / `GetGamepadAxis(GamepadAxis)`, `IsButtonDown(int64)` / `IsGamepadButtonDown(GamepadButton)`, `GetHat(int64)->Hat` 및 개수를 제공하고 인덱스를 검사합니다. 이벤트 `ControllerAxis/ControllerButtonDown/ControllerButtonUp/ControllerHat`의 `Control/Value`를 읽습니다. 축은 [-1,1], 트리거는 [0,1]이며 데드존은 자동 적용하지 않습니다.

`SupportsControllers()/SupportsRumble()`은 서비스 기능이며 장치별 진동 기능도 확인해야 합니다. `Rumble(kind,id,저주파 진폭,고주파 진폭,int64 밀리초)`는 유한한 0..1 진폭과 0..65535 ms를 받습니다. 새 호출은 이전 진동을 대체하고 시간 0 또는 두 진폭 0이면 정지합니다. 미지원, 연결 해제, 잘못된 인수와 다른 스레드 호출은 오류입니다.

전역 초기화, 백그라운드/종료, 연결 해제, 알림 넘침과 서비스 해제는 상태와 이 서비스가 시작한 진동을 정리합니다. 관리 창 포커스 상실/숨김/최소화도 전역 컨트롤러를 정리하지만 명시적인 창별 초기화는 정리하지 않습니다. 누락된 해제/중립 알림은 원인 이벤트보다 먼저 합성하며 이전 스냅샷은 보존합니다. 외부 모드는 호스트 구성/이벤트를 받고 네이티브 장치나 진동을 열지 않습니다. 호스트 공급자는 동일한 진동 인터페이스를 구현할 수 있습니다.

`SupportsControllers()`와 각 장치의 피드백 기능을 확인합니다. 실제 가용성은 OS, 장치 및 드라이버에 따라 다릅니다. 백그라운드 입력 정책은 호스트가 관리합니다. 자이로, 컨트롤러 터치패드, 트랙볼 및 고급 힘 피드백은 이 인터페이스에 포함되지 않습니다. 네이티브 리소스와 피드백은 소유 스레드에서 지연 해제를 마친 뒤 모듈을 내립니다.

<a id="window-and-events"></a>

네이티브 호스트는 `VeroScriptPlatformApi.h`를 사용합니다. `IService`는 창/이벤트, `IWindow`는 창 상태, `FInputDevice` / `FInputState`는 입력 스냅샷을 나타냅니다. ID는 불투명 식별자이며 네이티브 주소가 아닙니다. 닫기 요청은 이벤트이고 실제 닫기 시점은 호스트가 정합니다. `PollEvent`는 성공과 이벤트 유무를 분리하며 빈 결과를 `None`으로 초기화합니다. `WaitEvent`는 0..50 밀리초를 받으며 외부 펌프에서는 호스트가 대기하고 `SubmitEvent`로 전달합니다. 텍스트/IME는 소유된 UTF-8 문자열이지만 IME 선택 오프셋은 Unicode 문자 수입니다. `ScanCode`는 키보드 페이지 USB HID usage이며 매핑이 없으면 0입니다. 마우스 버튼은 1=왼쪽, 2=가운데, 3=오른쪽, 4=뒤로, 5=앞으로이고 마스크는 N-1 비트입니다. 입력 영역은 네이티브 좌표, 커서는 X 기준 수평 오프셋입니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
