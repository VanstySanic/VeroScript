# Network — 네트워크 참조

공통 프런트엔드 설치와 백엔드 선택 규칙은 [Network](../index.md#backend-selection)에 정의합니다.

vs-network 0.1.1 은 context/operation, URL/DNS, TCP/UDP, 바이트 파이프, 파일 및 공유 TLS 를 제공합니다. Http/WS 는 독립 모듈입니다. 네이티브 ID network, 스크립트 Network, CMake VeroScript::Network 입니다. 네이티브 SDK 는 Core 독립, Bindings 는 일치 Core 가 필요합니다. 프로토콜은 같은 Network 에 의존하며 오프라인 설치는 로컬 원본을 명시합니다.

FUrl::TryResolve 는 authority 가 있는 네트워크 URL 에 RFC3986 §5 를 적용합니다. 경로의 리터럴 ./..만 정규화하고 반복 슬래시/percent/query/fragment 의 점은 보존합니다. 빈 query 와 없음은 구분하고 빈 참조는 query 를 상속하며 fragment 를 지웁니다. 빈 경로는 /, g:h 같은 불투명 URI 는 거부하고 실패 시 출력을 유지합니다. 바이트당 한 번 인코딩하며 form 에서만 +/공백 변환, NUL 을 보존합니다.

백그라운드는 context 당 네이티브 I/O 스레드 하나, HostDriven 은 생성 스레드에서 제한된 PollIo 가 필요합니다. PollIo 는 콜백을 실행하지 않고 재귀 불가입니다. Dispatch 는 생성 스레드 배치이며 작업당 완료 등록/실행 한 번입니다. 종결과 큐 등록은 별개이므로 계속 진행/파견합니다. 큐 할당 실패의 등록은 재시도할 수 있고 배치 할당 실패는 0/Allocation 과 큐/할당량 유지입니다. 텍스트 할당 실패에도 오류 코드를 유지합니다.

Wait 는 관찰만 합니다. 0 Completed, 1 WaitTimedOut(취소 아님), 2 RequiresPoll. 기한은 ms, -1 무제한, 0 즉시이며 제출부터 대기열을 포함합니다. 완료/취소는 종결 하나, Result<T>는 성공/일치 형식만 가능합니다. Shutdown 은 작업/자원을 닫고 외부 결과/Owner 는 살아도 I/O 를 재개하지 않습니다.

MaxRetainedBytes 기본 256 MiB 는 관리 버퍼/파이프/큐/프로토콜 본문/결과를 계산하며 종결/Shutdown 이 외부 보유량을 조기 해제하지 않습니다. MemoryUsage 와 retained_bytes/peak_retained_bytes/memory_limit 는 현재/최대/한도이며 RSS/OS/백엔드/할당자/호출자 복사/재할당 임시 메모리의 엄격한 상한은 아닙니다. 실행기 Allocation/Internal 은 context 를 닫고 대기를 깨우지만 정상 Shutdown/파괴가 여전히 필요합니다. 정리는 새 작업 목록/오류 문자열을 요구하지 않습니다. 네이티브 DNS/파일 작업은 논리 취소보다 늦어 물리 종료를 지연하며 PollIo 의 지연 Close 도 기다릴 수 있습니다.

TCP 는 읽기 하나와 MaxQueuedWrites/MaxBufferBytes 로 제한한 직렬 쓰기 큐입니다. 대기 쓰기 취소는 해당 항목만, 활성 I/O 취소는 소켓과 다른 작업을 닫습니다. FTcpListener 는 비동기 accept/멱등 close 입니다. UDP 는 빈 데이터그램, 복사/원래 길이와 잘림을 보존합니다. broadcast/multicast 는 인터페이스를 명시하고 실제 전달은 네트워크에 달립니다.

parse_url/resolve_url 은 scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment 를 제공합니다. percent_encode/decode 에는 form 을 명시합니다. create_context/resolve/connect/listen/bind_udp 및 send/receive/accept/close, done/state/cancel/error/error_code/message_id/wait 와 검사된 int64 count/결과를 사용합니다. 핸들은 실제 정리까지 context/코드를 보유합니다. RetainCodeOwner 는 공유 제어 블록(null 별칭 포함)으로 중복 제거하며 원시 주소만으로 Owner 가 되지 않습니다.

Network를 선택한 프로토콜 모듈보다 먼저 로드하고 패키지 의존성을 선언합니다. Boost 공급자와 일치하는 `VeroScriptBoostRuntime`을 Windows 호스트 옆 또는 네이티브 모듈 배치에 보관합니다. iOS는 같은 호스트 이미지의 정적 등록을 사용합니다. 이 네이티브 API는 브라우저/Node 전송이나 언어의 await/future를 제공하지 않습니다.

- [완료 알림과 프로그램 수명](completion.md)
- [진단과 현지화](diagnostics.md)
- [파일과 원자적 출력](files.md)
- [유한 바이트 스트림](streaming.md)
- [TLS 구성](tls.md)

## 기본값

| API | 값 |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
