# 관리 메모리 구현 계약

Core가 `Public/VeroScriptMemory.h`와 `Private/Memory/`의 메모리 서비스를 소유합니다. 컴파일러 변환은 `Private/Compiler/VeroScriptCompilerMemory.cpp`, 실행은 `Private/ExecutionSupport/VeroScriptVmMemory.cpp`에 있습니다. `stdlib/memory.vs`는 모듈 진입점이며 별도 구현이 아닙니다. 어댑터는 이 서비스를 공유하며 독립 ID 레지스트리나 약화된 소유권 검사를 도입하면 안 됩니다.

## 불변 조건과 검증

페이로드를 읽기 전에 신뢰된 핸들의 출처를 검사합니다. ID는 약한 소유권 정보를 사용하여 수명을 늘리지 않고 재사용된 주소를 이전 객체와 혼동하지 않습니다. 영역은 소유자를 공유하며 뷰 설명자는 오프셋, 길이, 세대와 읽기 전용 종류를 보관합니다. `FMemoryContext`는 접근 도메인을 소유하고 worker는 예산만 공유하며 소유 스레드는 다릅니다. 다른 스레드 호출을 허용한 callable도 도메인을 이동할 수 없습니다.

길이가 0인 경우도 도메인, 세대, 경계, 쓰기 권한을 검사합니다. 확장은 커밋 전 새 할당과 이전 할당을 모두 계산합니다. 일괄 연산은 64 KiB마다 실행 제어를 검사하고 ceil(bytes/64)를 계산하며 스칼라 쓰기는 바이트를 변경하기 전에 검사합니다. 대여는 구조 변경과 충돌 접근을 막습니다. 해제 callback은 Core 잠금 밖에서 한 번 실행되며 예외를 던지면 안 됩니다.

## 호스트와 제한

C++는 `VeroScriptMemory.h`와 지속적 `FMemoryContext` 또는 Session의 `GetMemoryContext()`를 사용합니다. C ABI `vs_interop_memory_*`에는 하나의 Session 또는 활성 호출 컨텍스트를 전달합니다. 기존 `vs_interop_buffer`는 별도의 직렬화 결과 버퍼입니다.

외부 영역은 등록 성공 시에만 해제 책임을 인수하고 모든 별칭은 무효화를 공유합니다. 대여 중 충돌 접근과 구조 변경은 즉시 Busy입니다. 네이티브 작업 완료 후 불투명 토큰을 한 번만 해제합니다. 어떤 스레드에서든 Session 파괴 후에도 가능합니다. 최종 콜백은 코어 잠금 밖에서 실행되며 예외를 던질 수 없습니다. 대여 없는 주소는 즉시 관찰용입니다.

MaxBufferBytes는 각 용량/외부 길이, MaxRetainedBytes는 소유 용량과 대여 바이트 합계로 확장 중 이전 및 새 할당을 포함합니다. 0은 무제한입니다. OwnedBytes, BorrowedBytes, PeakOwnedBytes는 VM/객체 메타데이터를 제외합니다. Run, Clear, 재로드는 유지량을 초기화하지 않습니다. worker는 예산을 공유하지만 접근 영역은 별개입니다. 영역은 작업 캡처, 인수, 반환, 채널로 전달할 수 없습니다.

C#: `session.Memory`, `SetMemoryLimits`, `using var lease = memory.AcquireLease(bytes, true)`. Python: `session.memory`, `set_memory_limits`, `with memory.acquire_lease(bytes, write=True) as lease`. 대여를 닫기 전에 memoryview를 해제합니다. 외부 버퍼는 마지막 영역/대여까지 고정됩니다. 원래 ref/const-ref 대상은 `vs_interop_memory_call_argument_address`, C# `context.GetArgumentAddress`, Python `current_memory().argument_address`로 조회합니다. 값 전달 변환 복사본은 Unsupported입니다.

## 백엔드와 디버깅

VM과 Mixed를 지원합니다. AsmJit는 주소 민감 및 코어 메모리 멤버 함수를 진입 전에 거부하고 기존 VM 폴백을 사용합니다. 메모리 내장 연산은 코어 서비스를 사용합니다. 실행 제어는 기존 VM 정책을 유지합니다. 독립 C++/C#/Python AOT는 실제 사용을 거부하고 미사용 memory/core import는 허용합니다. 바이트코드/패키지는 포인터, 대여 토큰, ID 등록표를 저장하지 않습니다. 기본 객체 스냅샷은 중첩 영역을 거부하며 명시적 바이트 스냅샷이나 직렬화기를 사용합니다.

일시 정지 디버깅은 코어 정보를 표시합니다. DAP variablesReference는 프로토콜 ID이며 임의 readMemory/writeMemory는 활성화하지 않습니다. Android JNI와 iOS Runner는 코어를 공유하며 기본 주소 권한을 끕니다. C#/Python Session을 직접 사용하는 호스트는 예산과 권한을 명시적으로 설정해야 합니다. Wasm은 bigint ID/길이/제한과 인스턴스 소속을 유지하고 힙 증가 후 뷰를 다시 만듭니다. 호스트 영역 예산과 스크립트 Session 예산은 별개이며 기본 단일 영역 16 MiB, 유지 합계 32 MiB입니다.

Core 여섯 언어의 `memory.json`에 동일한 키와 안정 ID를 유지합니다. 소유/외부 저장소, ID, 만료, 한도, Busy, callback 소유권, 스레드 간 해제, Session 종료, reload, snapshot, worker 제한 및 AOT 거부를 검사합니다. Core/Cpp/Interop 메모리 테스트, binding 계약, 로케일 배포 및 문서 검증을 실행합니다. Web은 heap 확장과 대화형 입력 중 배제를 검사하고 모바일 실기기 검증은 별도로 수행합니다.


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory-runtime.md) · [fr-FR](../fr-FR/memory-runtime.md) · [ja-JP](../ja-JP/memory-runtime.md) · [ko-KR](memory-runtime.md) · [zh-CN](../zh-CN/memory-runtime.md) · [zh-TW](../zh-TW/memory-runtime.md)
