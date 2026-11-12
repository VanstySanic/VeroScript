# 바이트코드 VM 유지 관리

Core는 언어 연산, 값, 논리 프레임, 실행 관리, 정리 및 디스패치를 소유합니다. 이 모듈은 `Private/VeroScriptBytecodeVmExecution.cpp`의 전체 인터프리터, 멤버 실행 및 진단을 소유합니다. 정적/동적 대상은 헤더 전용 `VeroScriptExecutionContract`에 링크하며 두 번째 Core 바이너리를 링크하지 않습니다. `BytecodeVm::CreateFactory`는 실제 소유 Core의 서비스를 받습니다.

`FVmInvocation`은 이미 준비한 Core 프레임을 받습니다. 도메인/컨텍스트/프로그램 세대, 호출 토큰, 프레임 경계를 유지하고 인수 준비나 프레임 진입을 반복하지 마세요. 첫 의미 연산 전에 `bEntered`를 설정합니다. `NotEntered`는 실행에 진입하지 않았을 때만 유효합니다. 이어 실행, 계측, 제한, 불완전 정리/reset은 Core 계약을 따릅니다.

활성 컨텍스트마다 인스턴스를 가지며 동기 재진입은 활성 인스턴스를 씁니다. 공식 백엔드는 같은 도메인/스레드의 유휴 재바인딩을 지원합니다. 교체 전에 이전 프로그램을 해제하고 인스턴스 수명 동안 팩터리/모듈을 유지하세요. 할당한 팩터리가 인스턴스를 파괴하고 설명자는 할당 모듈의 팩터리 파괴 함수를 제공합니다.

동적 진입점은 `veroscript_vm_query`입니다. 사용 전에 `CoreVersion`, 설명자 크기, `BuildVmModuleAbi()`, ID/버전, 기능을 검증합니다. 제품 버전은 major:8/minor:8/patch:16이며 C ABI 인코딩과 다릅니다. 생성된 구현 식별자는 소스를 추적합니다. 제품 버전 문자열이 같아도 헤더, 컴파일러/STL/CRT, 플랫폼 및 레이아웃이 일치해야 합니다.

`VS_VEROSCRIPT_VM_VERSION`은 독립 VM 제품의 숫자 `major.minor.patch` 형식 CMake 캐시 설정이며 기본값은 `0.1.1`입니다. 생성된 `VEROSCRIPT_VM_VERSION`, 팩터리·모듈 버전 설명자 및 설치된 CMake 패키지 버전에 반영됩니다. 전체 패키징은 등록된 `Vm` 제품 버전을 전달하고 오래된 캐시나 불일치 아티팩트를 거부합니다. 이 제품 버전은 `CoreVersion`, 서비스 ABI 및 구현·공개 헤더 지문과 구분합니다. 번호를 바꿔도 호환되지 않는 바이너리를 사용할 수는 없습니다.

독립 CMake는 설치된 `VeroScriptExecutionContract`를 사용하고 루트 통합은 테스트에 실제 Core도 제공합니다. `VeroScriptVmBackendStaticTests`, 선택적 `VeroScriptVmBackendDynamicTests`, `Resources/Tests/Contract.cpp`/`Package.py`를 관련 검증 지점으로 유지하세요. 기능, 진입 전 대체, 재진입, 제한, 정리/reset, 소유 수명, 설치 소비자를 확인합니다. iOS/Web은 정적 통합이며 데스크톱 결과는 기기/다른 플랫폼 검증을 입증하지 않습니다.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/ko-KR/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
