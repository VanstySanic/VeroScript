# 기존 Session 디버깅

이 브리지는 VM 백엔드를 선택하지 않습니다. 실행 전에 [호스트 통합](embedding.md)에 따라 `VmDebugCapability`가 있는 백엔드를 명시적으로 구성하세요. 디버거 연결만으로 인터프리터가 제공되지는 않습니다.

실행 전에 `VeroScriptSessionDebug.h`의 `ISessionDebugBridge`를 `SetDebugBridge`로 설치합니다. `BeginExecution`은 현재 호출의 공유 디버거를 반환할 수 있으며, VM 디버깅을 선택하고 이미 실행 중인 JIT 호출을 재실행하지 않습니다. `Run`, 객체 생성, 메서드/함수 호출, 보관 callable 및 클래스 기본값을 다룹니다. 실패와 예외 해제를 포함해 begin마다 `EndExecution`이 한 번 대응합니다. 작업자는 이 브리지의 추가 컨텍스트가 아닙니다.

콜백과 정지 VM의 검사, 평가, 변경 및 재개는 Session 제어 스레드에서 수행합니다. 정지 콜백은 해당 스레드에서 요청을 처리해야 합니다. 스레드 간 직접 제어는 `RequestPause()`와 취소뿐입니다. 공유 `FExecutionControl::RequestCancel()` 또는 Session 수명을 보장할 때 `Session::RequestCancel()`을 사용하세요. 빌린 프로그램, 바인딩, 소스 overlay와 진입 이름 뷰는 콜백 반환 시 만료됩니다. 필요한 메타데이터를 복사하세요. overlay는 현재 구성이며 고정 소스 스냅샷이 아닙니다. 소스/바인딩 리비전은 별도로 게시합니다.

<a id="debug-disconnect-lifetime"></a>

연결 해제는 호스트 정책에 따라 정지 연결을 해제하며 그 자체로 실행을 취소하거나 Session을 파괴하지 않습니다. `Detached`는 제거/교체 시 및 파괴 전에 호출됩니다. 같은 제어 스레드에 넣은 작업을 기다리지 말고 콜백을 정리하세요. 종료할 때 정지를 해제하고 필요하면 취소를 요청한 후 실행과 전송 종료를 기다리고 Session을 파괴합니다. 취소가 걸린 토큰은 재사용 전에 초기화하세요. 블로킹 네이티브 콜백은 직접 협력해야 합니다.

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/ko-KR/debugging.md)

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/session-debugging.md) · [fr-FR](../fr-FR/session-debugging.md) · [ja-JP](../ja-JP/session-debugging.md) · [ko-KR](session-debugging.md) · [zh-CN](../zh-CN/session-debugging.md) · [zh-TW](../zh-TW/session-debugging.md)
