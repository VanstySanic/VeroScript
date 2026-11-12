# 18: C++ 도구 체인 모듈 사용하기

이 튜토리얼에서는 네이티브 통합 목적에 따라 임베딩, BindGen, Transpiler, AOT를
선택하고 작은 작업 흐름을 완성합니다.

전제 조건은 호환되는 `veroscript`, Cpp BindGen, Transpiler가 실행 파일 검색 경로에 있고 작업 디렉터리에 쓸 수 있는 것입니다. 아래 경로는 사용자가 제공할 입력입니다. 지원되는 선언을 `Include/Types.hpp`, `Include/Api.hpp`에, 정의를 `Source/Types.cpp`, `Source/Game.cpp`에 준비하고 유효한 `script.vs`를 만들거나 모든 경로를 기존 파일로 바꾸세요. 해당 작업 디렉터리에서 명령을 실행합니다. 예상 결과는 병합된 `bindings.json`, API 참조와 매니페스트, 소스 맵이 있는 VeroScript 모듈 및 AOT의 `generated.cpp`입니다. 파일 생성만으로 런타임 호스트 콜백을 등록하지는 않습니다.

## 1단계: 경계 선택

[C++ SDK 레퍼런스](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/index.md)를 기준으로 선택합니다.
실행 중 세션과 스크립트 호출에는 `VeroScriptCpp`, C++ 선언에서 카탈로그를 만들 때는
`VeroScriptCppBindGen`, 지원되는 C++ 소스를 VeroScript로 바꿀 때는
`VeroScriptCppTranspiler`, VeroScript를 네이티브 빌드용 C++17로 바꿀 때는
`VeroScriptCppAot` 또는 `veroscript aot`를 사용합니다.

BindGen은 네이티브 API를 설명하고, Transpiler는 소스 동작을 변환하며, AOT는
VeroScript를 C++로 변환합니다. 서로 대체하는 도구가 아닙니다.

## 2단계: 바인딩 카탈로그 생성

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

카탈로그는 컴파일러와 도구용이고 API reference는 사람용입니다. 실제 callback은
호스트가 등록해야 합니다.

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

여러 입력은 project mode로 처리하여 출력 전에 선언을 병합하십시오.

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp --output-dir build/generated --manifest build/generated/transpile.manifest.json
```

## 3단계: 변환과 AOT

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json

veroscript aot script.vs -o build/generated.cpp
```

여러 입력은 project mode로 처리하여 manifest와 source map을 생성 소스와 함께
보관하십시오. AOT 결과는 실행 파일이 아니라 C++17 소스입니다. 대상 프로젝트에서
런타임 연결, callback 수명과 호스트 상태 동기화를 확인하십시오.

결정적인 다중 파일 C++ 산출물이 필요하면 `FCppAotSourceBundle` API를 사용하세요. 진입 소스, 생성된 조각과 매니페스트를 함께 보관합니다. 이 파일들은 대상 C++ 프로젝트의 입력이며 이미 빌드된 실행 파일이 아닙니다.

생성된 최상위 전역과 클래스 정적 필드는 네이티브 작업자별로 격리됩니다. AOT는 각
작업자의 전역 접근자를 선언 순서대로 `main`보다 먼저 초기화합니다. 전역을 암묵적인
스레드 간 공유 수단으로 사용하지 말고 채널이나 동기화된 호스트 상태를 사용하십시오.

멤버 람다가 수신자를 사용하면 AOT는 소스 형식의 의미 체계를 유지합니다. 클래스
수신자는 살아 있는 유지 객체로 처리되고 구조체 수신자는 값 스냅샷이 됩니다. 중첩
람다는 이미 캡처된 수신자를 유지하며 캡처 없는 람다는 `this`를 사용할 수 없습니다.
C++ AOT는 수신자가 이미 소멸 중이므로 소멸자 본문에서 클래스 수신자를 캡처하는
것도 거부합니다.

## 연습

1. 카탈로그 항목과 해당 C++ 등록을 비교합니다.
2. 두 입력을 project mode로 변환하고 manifest를 확인합니다.
3. channel을 사용하는 스크립트를 AOT로 생성하고 동기화할 호스트 상태를 적습니다.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/cpp/01-cpp-toolchain.md) · [fr-FR](../../fr-FR/cpp/01-cpp-toolchain.md) · [ja-JP](../../ja-JP/cpp/01-cpp-toolchain.md) · [ko-KR](01-cpp-toolchain.md) · [zh-CN](../../zh-CN/cpp/01-cpp-toolchain.md) · [zh-TW](../../zh-TW/cpp/01-cpp-toolchain.md)
