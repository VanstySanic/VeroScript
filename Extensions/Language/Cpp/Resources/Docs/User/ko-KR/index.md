# C++ SDK

C++ SDK는 VeroScript의 네이티브 통합 표면입니다. 임베딩 런타임 API와 C++용
소스 도구를 함께 제공합니다. Python, C#, Rust 등의 어댑터가 사용하는 C ABI는
[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md)에서 설명합니다.

## 모듈 선택

| 모듈 | 용도 |
| --- | --- |
| [임베딩](embedding.md) | C++ 호스트에서 검사, 로드, 실행, 호출, 리로드 및 설정을 수행합니다. |
| [호스트 바인딩](bindings.md) | 네이티브 함수, 호스트 타입, 멤버, 연산자 및 제네릭 호스트 타입을 설명하고 등록합니다. |
| [AOT](aot.md) | 네이티브 빌드용 VeroScript C++17 소스를 생성합니다. |
| [C++ 프런트엔드](frontend.md) | 지원되는 C++17 부분집합을 공유 공개 AST로 파싱합니다. |
| [BindGen](bindgen.md) | C++ 선언에서 바인딩 카탈로그와 API 레퍼런스를 생성합니다. |
| [Transpiler](transpiler.md) | 지원되는 C++17 부분집합을 VeroScript와 소스 맵으로 변환합니다. |

프런트엔드는 BindGen과 Transpiler가 공유합니다. 의미를 보존할 수 없는 구성은
임의로 근사하지 않고 진단과 함께 거부합니다. 이 도구들은 일반 C++ 컴파일러가
아닙니다.

## 통합 순서

<a id="explicit-vm-selection"></a>

실행 전에 `FEngineConfig::VmBackendFactory`/`VmBackendPath` 또는 Session 선택 API로 호환 VM을 지정하세요. C++ SDK 링크만으로는 인터프리터가 선택되지 않으며 컴파일 스크립트에도 VM이 필요합니다. 필요한 SDK 대상과 설정은 [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/ko-KR/index.md)를 참고하세요.

1. 일회성 작업에는 `VeroScriptEngine`, 장기 모듈과 상태에는
   `VeroScriptSession`을 선택합니다.
2. 모듈을 로드하기 전에 호스트 함수와 타입을 등록합니다.
3. 별도 검증 오류가 필요하면 `CheckSource`/`CheckFile` 후
   `RunSource`/`RunFile`을 호출합니다.
4. IDE, LSP, 패키지 또는 정적 검사기가 사용하면 카탈로그를 내보내고 검증합니다.
5. 콜백과 worker 실행은 동시에 일어날 수 있으므로 호스트 상태를 동기화합니다.

스크립트 전역과 클래스 정적 필드는 네이티브 worker별로 격리됩니다. 명시적 동기화
공유에는 `channel<T>`를 사용합니다. 네이티브 콜백, 인스턴스 인터셉터, mutation
observer, AOT 호스트 심볼은 worker에서 동시에 호출될 수 있습니다.

## 관리 메모리

호스트 API: `VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`. 소유권, 할당량, 뷰 만료와 임대는 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md) 문서를 참고하세요.

## 소스 도구의 진단 언어

C++ 프런트엔드와 C++ BindGen/Transpiler의 자체 도움말, 인자 오류 및 선언 진단은 현재 영어입니다. BindGen/Transpiler CLI는 `--lang`을 받지 않으며 런타임 진단 언어를 바꿔도 도구 메시지는 번역되지 않습니다. 진단 코드와 소스 위치는 프로그램에서 처리할 수 있습니다.

## 모듈 환경과 백엔드

전달한 모듈 환경에 실행 해석기가 있으면 Engine/Session은 자동 선택을 상속할 수 있습니다. 명시적 팩터리나 파일／디렉터리가 우선하며 원시 `VeroScriptVm`은 명시적으로 조립합니다. [백엔드 선택](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/introduction.md#backend-selection)을 참고하세요.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
