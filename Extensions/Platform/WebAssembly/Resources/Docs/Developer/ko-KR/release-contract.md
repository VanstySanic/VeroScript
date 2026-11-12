# WebAssembly ABI, artifact, release 계약

## 세 가지 공개 인터페이스

`VeroScriptWebAssembly.h`는 C++ 파사드, `VeroScriptWebAssemblyC.h`는 WebAssembly C ABI이며 TypeScript 래퍼와 선언은 브라우저 SDK를 구성합니다. 공통 결과 필드, 백엔드 이름, 언어 태그와 입력 동작을 일치시킵니다. 별도의 네이티브 Interop ABI는 WebAssembly ABI를 정의하지 않습니다.

## 이름과 호환성

모듈과 CMake 대상은 `VeroScriptWebAssembly`, C++ 네임스페이스는 `VeroScript::WebAssembly`, TypeScript 진입점은 `veroscript-webassembly.ts`입니다. SDK 압축 파일은 `VeroScriptWebAssemblySDK`, 설치 프로그램은 `VeroScriptWebAssemblyInstaller`입니다. C ABI 이름 `vero_web_*`, `VeroWeb*`, `VERO_WEB_*`와 ABI 버전 `0.1.1`을 유지합니다. 프로필 `web`, 구성 요소 `web-sdk`, 제품 ID `dev.veroscript.web`은 기존 설치 기록을 식별합니다.

## 산출물과 게시 검증

일반 및 고속 엔진의 `.mjs`/`.wasm`, 래퍼 `.ts`/`.d.ts`, 두 엔진의 `.d.mts` 선언을 포함한 여덟 파일을 게시합니다. 표준 라이브러리와 카탈로그는 가상 파일 시스템에 포함됩니다. 바인딩과 선언의 타입 검사, 전체 목록 감사를 수행하고 headless Chromium에서 대화형 입력, Web Worker 실행과 종료를 확인합니다. 산출물 감사는 Release 다운로드 예산으로 일반 Wasm 17 MiB, 고속 Wasm 11 MiB를 적용합니다. 이는 ABI 제한이 아닌 게시 기준이며 소스 확인만으로 브라우저 검증을 완료할 수 없습니다.

현재 기준에는 정확한 수치 연산과 중첩·지역·제네릭 형식의 범위가 포함됩니다.

Web 제품은 `VeroScriptBytecodeVm`을 정적으로 구성합니다. 소스 검사, 컴파일 확장, 직접 실행 및 재사용 가능한 프로그램은 명시적으로 선택된 이 인터프리터를 사용합니다.

두 Wasm 엔진 모두 Core를 통해 초기화 캡처, `mutable` 클로저 및 지역 `static` 변수를 지원합니다. 값 캡처는 기본적으로 읽기 전용이며, 함수 값을 복사하면 환경을 공유하고 람다를 다시 평가하면 새 환경을 만듭니다. 지역 정적 변수는 처음 사용할 때 초기화되며 초기화 실패 후 다시 시도할 수 있습니다. 대화형 실행을 포함한 각 프로그램 실행은 지역 정적 변수까지 새로운 상태에서 시작합니다. Web ABI와 TypeScript API는 변경되지 않습니다.

## ABI와 현지화

런타임 의미 버전, WebAssembly ABI 버전, 바이트코드 버전, 기능 플래그와 선택 언어를 구분합니다. 사용 가능한 BCP 47 태그는 실행 중에 조회합니다. API 식별자, 프로토콜 필드, 진단 ID와 버전을 번역하지 않으며 고정된 언어 수로 동작을 판단하지 않습니다.

## 프로그램과 입력 기능

C ABI 내보내기, TypeScript 선언과 기능 감지를 일치시킵니다. 재사용 가능한 컴파일에는 컴파일, 실행, 해제 내보내기가 필요하며 대화형 프로그램 실행에는 Asyncify 대화형 내보내기도 필요합니다. 추가 내보내기가 없으면 기능을 사용할 수 없는 것으로 처리하며 프로그램 핸들을 만들어 내지 않습니다.

컨텍스트 배열의 UTF-8 경로와 소스 수는 같아야 하며 호출 중에만 빌립니다. 프로그램 핸들은 생성한 런타임에 속하며 다른 인스턴스로 옮길 수 없습니다. 결과 JSON에는 진단, 출력, 요청된 백엔드 정책, 시간과 JIT 통계를 포함합니다. 게시 검증은 핸들 재사용, 실행마다 새 상태, 해제, 대화형 일시 중단과 언어 선택을 확인해야 합니다.

<a id="web-execution-policy"></a>

공개 모듈은 JIT 백엔드를 선택하지 않습니다. `vm`은 바이트코드를 해석하며 기본값인 `mixed`도 VM으로 실행합니다. `jit`는 애플리케이션 초기화, `main` 또는 입력 요청 전에 구조화된 진단 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`를 반환합니다. 네이티브 JIT 실행 계층은 `Modules/Execution/VeroScriptAsmJit`에 속하며 Web 모듈은 이 JIT 백엔드를 포함하거나 동적으로 로드하지 않습니다. 기존에 `jit`를 지정한 호출자는 `vm` 또는 `mixed`를 선택해야 합니다. 정책 이름과 C ABI ID는 유지되며 `result.backend`는 요청한 정책을 나타내고 모든 JIT 카운터는 0입니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/release-contract.md) · [fr-FR](../fr-FR/release-contract.md) · [ja-JP](../ja-JP/release-contract.md) · [ko-KR](release-contract.md) · [zh-CN](../zh-CN/release-contract.md) · [zh-TW](../zh-TW/release-contract.md)
