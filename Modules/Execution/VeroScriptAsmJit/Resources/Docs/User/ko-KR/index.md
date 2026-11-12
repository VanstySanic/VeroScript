# 선택적 JIT 백엔드

## 선택과 호환성

AsmJit는 Runtime과 별도로 배포하는 선택적 JIT 백엔드입니다. 실행 정책과 백엔드를 각각 선택하세요. 프로젝트 선택 상속, `auto`, `none`, 명시적 ID/경로 및 폴백은 [CLI 백엔드 참조](../../../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md#cli-backend-policy)에서 설명합니다. 이 가이드는 AsmJit 패키지, 네이티브 호환성과 수동 로드를 다룹니다.

호환 JIT가 `MyProject`에 설치되어 활성화된 경우 다음 명령은 각각 설정 상속과 명시적 자동 선택을 사용합니다.

```text
veroscript --path ./MyProject --backend=jit ./MyProject/main.vs
veroscript --path ./MyProject --jit-backend=auto --backend=mixed ./MyProject/main.vs
```

이식 가능한 압축 파일에서 수동 배포하는 경우: 기능 패키지를 `Release/Modules/Execution/Jit/<target>/<version>/archives/`에서 받아 Runtime과 별도로 압축을 풉니다. 독립 설치 프로그램은 제공하지 않습니다. 압축을 푼 라이브러리나 디렉터리를 명시적으로 선택합니다. 같은 버전 문자열만으로 호환성을 보장하지 않습니다. Core, 아키텍처, 컴파일러, 표준 라이브러리와 런타임 구성을 호스트에 맞추세요. 이식 가능한 백엔드는 네이티브 기계어를 생성하지 않고 최적화할 수 있습니다.

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
veroscript --jit-backend="/path/to/backend-library" --backend=jit main.vs
```

## 경로와 선택 변경

다음 파일/디렉터리 로더 규칙은 명시적 경로에 적용되며 모듈 환경의 ID/자동 선택과 별개입니다. 파일을 지정하면 한 모듈로 고정합니다. 디렉터리는 루트와 최대 두 하위 수준을 UTF-8 사전순으로 검색하여 사용 가능한 첫 호환 후보를 선택합니다. Windows 이름은 `VeroScriptJit`, Linux/macOS는 `libVeroScriptJit`로 시작합니다. 최대 후보 수는 4096입니다. 읽을 수 없는 하위 항목은 건너뜁니다. 상대 경로는 현재 작업 디렉터리를 기준으로 합니다. 환경 변수, 레지스트리나 암묵적 기본 경로로 선택하지 않습니다. 유휴 상태에서만 변경하며 실패하면 이전 선택을 유지합니다. 임베딩 API의 빈 경로는 비활성화합니다.

## 임베딩 API

C++는 `SetJitBackendPath`, C는 `vs_interop_session_set_jit_backend`와 `vs_interop_session_get_jit_backend_info`, C#은 `SetJitBackend`와 `GetJitBackendInfo`, Python은 `set_jit_backend`와 `get_jit_backend_info`를 사용합니다. 실행 정책은 별도로 선택합니다. 팩터리, 인스턴스, 작업자나 보관 실행 산출물이 필요로 하는 동안 모듈을 유지합니다.

공식 패키지 이름과 릴리스 아카이브 접두사는 `vs-asmjit`입니다(예: `vs-asmjit-0.1.1-win-x64.zip`). 이름으로 선택할 때 사용하는 서비스 백엔드 ID는 계속 `asmjit`입니다.

## 실행 제어를 적용한 네이티브 실행과 정적 AOT

<a id="controlled-native-execution"></a>

제한, 취소, 권한, 프레임과 값은 Core가 관리합니다. 미지원 함수는 진입 전에 VM으로 전환하고 실패 후 함수를 다시 실행하지 않습니다. 제어된 Mixed JIT는 `FJitPolicy::bEnableControlledMixed`로 명시적으로 켭니다(C ABI `enable_controlled_mixed`, C# `EnableControlledMixed`, Python `enable_controlled_mixed`). 첫 버전은 VM보다 느릴 수 있습니다. 명시적 Jit와는 별개입니다. `ControlledNativeExecutions`／`AotExecutions`는 실제 네이티브 진입 수입니다. Android／iOS SDK 및 기기 검증은 미완료이며 데스크톱 시험으로 대체할 수 없습니다.

[실험적 모바일 임베딩 — 실행 제어를 적용한 네이티브 실행과 정적 AOT](../../../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md#mobile-jit-selection)을 참조하세요.

자동 선택, 명시 선택 및 비활성화의 공통 규칙은 다음을 참조하세요: [서비스 백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
