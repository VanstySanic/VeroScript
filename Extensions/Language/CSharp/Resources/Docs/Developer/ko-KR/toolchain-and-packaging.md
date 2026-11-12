# C# toolchain과 package 계약

## strict tool 경계

entry point가 있는 경우 C# Transpiler는 class-name heuristic이 아니라 C# compiler가 선택한 것을 lower합니다. static type에 선언한 parameterless `static void Main()` 또는 `static int Main()`만 허용하며, ambiguous, argument-taking 또는 instance-container entry point는 `VSCT2001`로 실패합니다. Binding Catalog v1에는 host-index lowering이 없으므로 BindGen은 C# indexer를 거부합니다. 두 tool 모두 지원하지 않는 entry나 member를 조용히 생략하지 않으며 validation이 성공한 뒤에만 output을 commit합니다.

## framework와 SDK

`VeroScriptCSharp`는 `net8.0`/`net9.0`, C# 12.0, XML documentation, package API validation을 사용하고 runtime package는 `VeroScript.CSharp`입니다. CMake는 installed .NET SDK를 확인하고 두 framework에서 managed test를 실행합니다. frontend, Transpiler, BindGen도 같은 framework를 target하지만 build-time tool에는 .NET 9.0 이상 SDK가 필요하며 runtime framework 지원과 구별됩니다. 패키지된 command-line tool은 요청된 .NET 8/9 runtime이 설치되어 있지 않으면 `Major` roll-forward를 사용하여 지원되는 이후 major runtime에서 실행됩니다.

## 패키지 내용

런타임 `.nupkg`에는 net8.0/net9.0 관리 어셈블리, XML API 문서, buildTransitive 통합 대상과 라이선스를 포함합니다. 디버그 심볼은 `.snupkg`로 별도 배포합니다. 소스의 README.md, API.md와 MOBILE.md는 포함하지 않으며 사용 안내는 독립 Resources 문서에서 받습니다. 네이티브 Interop 런타임은 계속 호스트가 배포해야 합니다. CMake는 프레임워크별 관리 출력과 생성된 NuGet 패키지를 SDK 영역에 설치합니다.

## C# AOT source bundle

`VeroScriptCSharpAot`는 generated compilation unit와 deterministic `FCSharpAotSourceBundle`을 제공합니다. bundle은 runtime support, lowered program, entry point의 relative source, SDK project, `ManifestJson`을 포함하고 `BuildCSharpSourceBundle`은 emitted C#를 변경하지 않습니다. source/file/checked-program entry는 import/implicit import/`Defines`/circular/project-wide policy를 공유하며 option overload는 host catalog/codegen setting도 받습니다. load/semantic check/lowering/bundle failure는 partial build-ready bundle을 반환하지 않고 relative path/manifest field는 번역하지 않습니다.

## 생성 등록 코드 검사

BindGen packaging은 catalog generation, generated registration compile/runtime test, C++ catalog import test, package verification을 포함합니다. generated C# 또는 catalog shape 변경 뒤에도 모든 generated-registration consumer가 compile/run해야 합니다.

## 소스 변환

`VeroScript.CSharpTranspiler`와 `VeroScript.CSharpTranspiler.Tool`은 strict C#-to-VeroScript conversion을 제공합니다. project mode는 단일 Roslyn compilation으로 source를 해석해 semantic cross-file import/source map/deterministic manifest를 emit하고 output set을 atomic commit하며 real VeroScript validator가 generated source를 compile합니다.

## 도구 의존성 고지

C# 프런트엔드, BindGen CLI 및 Transpiler CLI 출력은 원본 Roslyn 고지를 `licenses/`에 보존합니다. 프런트엔드 NuGet과 두 .NET 도구 패키지에도 포함됩니다. 이 고지는 프로젝트의 MIT 고지를 보완합니다. Roslyn 버전을 변경하면 [고지 출처](../../../Licenses/README.md)를 갱신하고 `Roslyn-LICENSE.txt` 및 `Roslyn-ThirdPartyNotices.rtf` 전체를 RTF 인코딩까지 그대로 유지하십시오. 패키지 검증은 원본 고지와의 일치를 확인합니다. 상위 프로젝트의 라이선스 원문은 번역하지 마십시오.

## 표준 컨테이너의 제네릭 인수 수

[C# 프런트엔드 — 표준 컨테이너의 제네릭 인수 수](../../User/ko-KR/frontend.md#container-mapping-arity)을 참조하세요.

## 정수 변환 및 수치 지원 범위

Transpiler는 정수 덧셈, 뺄셈, 곱셈, 부호 반전, 증감, 복합 할당 및 정수 캐스트에서 해석된 비트 폭으로 순환하는 C# 기본 unchecked 동작을 보존합니다. 시프트 횟수는 31 또는 63으로 마스킹합니다. 복합 할당은 오른쪽 피연산자보다 먼저 대상과 기존 값을 한 번만 평가하며 자동 속성은 필드로 출력합니다. 정수 나눗셈과 나머지는 0인 제수 또는 부호 있는 최솟값/-1 오버플로에서 잡을 수 있는 스크립트 예외를 발생시킵니다. 정수 값인 `double` 상수도 부동 소수점 형식을 유지합니다. CLR/VeroScript 실행 결과 비교 테스트로 검증합니다.

[C# 트랜스파일러 — 정수 변환 및 수치 지원 범위](../../User/ko-KR/transpiler.md#numeric-and-console-boundaries)을 참조하세요.

도구 패키지 검증은 Transpiler를 설치하여 소스를 생성하고 실제 컴파일러로 검증한 뒤 실행합니다. 부호 없는 나눗셈과 나머지, 부호 있는 정수의 래핑, 불리언 텍스트와 줄바꿈, 포착 가능한 0으로 나누기, 검증을 꺼도 지원하지 않는 checked 문자열 상수를 거부하는지 확인합니다.

C# AOT의 부동 소수점에서 정수로의 변환은 0 방향으로 절삭하고 캐스팅 전에 이진 값이 대상 범위 안에 있는지 확인합니다. 표현 가능한 64비트 정수 값을 십진수 반올림 없이 보존합니다. 부호 없는 대상은 음의 소수를 포함한 음수 입력을 거부하며 NaN, 무한대 및 범위를 벗어난 값도 실패합니다. 회귀 검증은 관리 실행과 NativeAOT 실행에서 정수 경계값을 확인합니다.

[C# AOT — 명시적 제외](../../User/ko-KR/aot.md#aot-floating-conversions)을 참조하세요.

C# 트랜스파일러는 정수와 부동 소수점 혼합 비교 전에 Roslyn 피연산자 승격을 적용하여 코어 비교 전에 C# float 반올림을 보존합니다.

숫자 조건 분기는 Roslyn이 선택한 암시적 변환을 적용하여 정수 너비와 float 반올림을 보존하며 선택한 분기만 평가합니다. 조건 분기에서도 상수가 아닌 64비트 정수를 float로 변환하는 경우를 거부합니다.

[C# runtime과 Interop adapter — 유지된 callable 해제](runtime-and-interop.md#managed-marshalling-maintenance)을 참조하세요.

[Android 및 iOS / MAUI](../../User/ko-KR/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

모바일 패키징도 같은 프로젝트와 패키지 이름을 사용합니다. `VeroScriptIncludeMobile=true`는 .NET 8/9 및 .NET 10 Android/iOS 대상과 검증된 네이티브 SDK를 포함합니다.

`core.vs`가 가져온 사용하지 않는 구현을 제거할 때 해석된 바이트코드 호출의 실행 의존성과 제네릭 본문의 자유 심볼 의존성을 유지하고, AST 선언과 스코프 내 참조에서 타입 의존성을 수집하고 공개 타입 이름을 Core 별칭 테이블로 확인합니다. `std::sqrt`, `std::algorithm::sort` 같은 호출과 `std::delegate` 같은 타입에 필요한 구현은 유지합니다. 지역 이름, 인스턴스 멤버 이름 및 일반 문자열 내용은 전역 함수 참조로 취급하지 않습니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
