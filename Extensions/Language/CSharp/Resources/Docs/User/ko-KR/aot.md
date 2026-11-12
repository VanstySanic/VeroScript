# C# AOT

`veroscript aot --target=csharp`는 검사된 VeroScript 프로그램을 C# 소스로
변환합니다. 생성 애플리케이션은 일반 .NET 빌드 또는 NativeAOT publish가
가능하며 실행 시 `VeroScriptInterop`가 필요하지 않습니다. 임베딩 SDK인
`VeroScriptCSharp`와는 별도의 경계입니다.

## 단일 파일

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

생성 결과는 실행 파일이 아닙니다. `net8.0` 또는 `net9.0`을 대상으로 하는
SDK-style project에 추가한 후 `dotnet`으로 빌드하십시오. 필요한 경우
`-I`/`--import-path`로 애플리케이션 모듈 검색 경로를 추가합니다.

## 소스 bundle

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

bundle에는 `App.runtime.g.cs`, `App.program.g.cs`, `App.entry.g.cs`,
`App.csproj`, `App.manifest.json`이 포함됩니다. manifest는 생성 파일과 컴파일
단위의 순서를 기록합니다.

<a id="aot-project-inputs"></a>

### 생성 project의 host 입력

생성 `.csproj`는 `EnableDefaultCompileItems=false`를 사용하므로 명시한 `Compile` 항목만 컴파일합니다. 원본 managed host 선언과 AOT bridge를 추가 입력으로 지정합니다. 파일을 project 옆에 두기만 하면 포함되지 않습니다. bridge의 partial class 구현은 생성 프로그램과 같은 assembly에서 컴파일해야 합니다. manifest의 모든 생성 단위를 보존합니다. VM 등록 소스는 별도 SDK 통합 산출물입니다.

bundle 재생성은 project file을 교체합니다. 추가 `Compile` 항목을 다시 적용하거나 application 소유 project에서 생성 단위와 host 입력을 참조합니다. 구체적 경로와 작업 순서는 [바인딩 튜토리얼](../../../../../../../Resources/Docs/Tutorials/ko-KR/csharp/01-csharp-source-tools.md)을 참고하세요.

## 지원 범위와 제한

scalar, string, `pair`, `auto`, container, free function, 제약 없는 open generic,
default, variadic, overload, `ref`, namespace, `using namespace`, enum, global,
import, 제어문, `switch`, jump, `throw`, `try`/`catch`/`finally` 및
`algorithm`/`numeric`/`random`을 지원합니다. function value와 indirect call,
expression/block lambda와 capture, script class/struct, constructor,
instance/static/virtual method, 단일 class 상속, reference return 및 local
reference alias도 지원합니다.

signed 8-bit `char` literal, same-type local declaration group, `do`/`while`,
classic `for`의 initializer/step expression list도 지원합니다. 일반 string과 raw
string은 `\xNN`으로 도입된 isolated byte를 포함해 정확한 VeroScript byte sequence를
보존합니다. Unicode newline scalar는 escape로 출력되므로 generated C# literal이나
source-location comment를 무효화하지 않습니다.

generic type parameter와 lexical shadowing local, field, loop/catch binding, lambda
parameter에는 hygienic한 C# 이름이 부여됩니다. initializer는 새 binding이 보이기 전의
VeroScript environment에 대해 출력되므로 C#이 이를 자기 자신으로 bind하거나 nesting을
거부할 수 있는 경우에도 VeroScript 의미를 보존합니다.

open generic 형식의 constructor field initializer list에서 C# 대상은 `T`에서 `T`로의
직접 초기화만 지원합니다. 제약 없는 형식 매개 변수를 인자 없이 또는 여러 인자로 생성하는
형식은 AOT 생성 중 거부됩니다. 구체적인 필드 형식을 사용하거나 미리 생성한 `T` 값을
전달하십시오.

lambda가 인스턴스 멤버를 사용하면 캡처된 클래스 `this`는 수신자를 유지하여 이후
필드 변경을 관찰하고, 구조체 `this`는 값 스냅샷이 됩니다. 중첩 lambda는 이미 캡처된
수신자를 유지합니다. 캡처 없는 lambda는 `this`를 사용할 수 없습니다.

Binding Catalog v1의 호환되는 class, struct, enum, member, container는
`--binding-catalog`과 생성된 AOT bridge를 통해 runtime reflection 없이 직접
managed call로 lowering할 수 있습니다.

## 명시적 제외

interface와 interface implementation, 여러 concrete class base, non-public 또는
struct inheritance, deterministic destructor, friend/using-member, 제약 또는
기본값이 있는 generic parameter, const 또는 reference 방향만 다른 overload,
precompiled body, map destructuring, reference `foreach`, fixed-size native array,
host operator/indexer, generic host member, host property 증감 및 element 변환이
필요한 host container는 거부됩니다. 의미를 바꾸는 대신 diagnostic을 반환합니다.
`concurrent.vs`, `channel<T>`, `task<T>`, `task_group<T>`는 C# AOT에서 지원하지
않습니다. runtime concurrency나 host 기능이 필요하면 [C# SDK](index.md)를
사용하십시오.

지원 컨테이너에는 일반 map, ordered map, set, queue, stack, deque뿐 아니라
`multi_map`, `ordered_multi_map`, `multi_set`, `ordered_multi_set`,
`priority_queue`도 포함됩니다.

C# AOT의 부동 소수점에서 정수로의 변환은 0 방향으로 절삭하고 캐스팅 전에 이진 값이 대상 범위 안에 있는지 확인합니다. 표현 가능한 64비트 정수 값을 십진수 반올림 없이 보존합니다. 부호 없는 대상은 음의 소수를 포함한 음수 입력을 거부하며 NaN, 무한대 및 범위를 벗어난 값도 실패합니다. 

<a id="aot-floating-conversions"></a>

C# AOT는 정수를 float로 변환할 때 코어와 같은 double 중간 변환을 유지하며 혼합 연산과 복합 대입에도 적용합니다. 부동 소수점 혼합 비교는 double 피연산자를 사용하며 부동 소수점 나머지는 코어와 같이 정수에만 정의된다는 오류를 발생시킵니다.

## 런타임 메모리 지원

독립 AOT는 현재 `std::memory`의 조회, 버퍼, 뷰 및 관련 타입을 지원하지 않습니다. 실제 호출이나 해당 타입 사용 시 명확한 생성 오류를 반환합니다. 사용하지 않는 `memory.vs` 또는 `core.vs`의 import는 계속 지원합니다.

[Runtime SDK](index.md)

## 선언 준비와 캡처 제한

C# 생성은 Core의 선언 준비를 사용하며 클래스 외부 정의와 생성된 메서드도 처리합니다. 네이티브 호스트는 `FCSharpCodegenBackend`의 `FCodegenOptions.AnnotationProcessing`으로 어노테이션 프로세서를 제공할 수 있습니다. 프로세서 코드는 생성 중에 유효해야 합니다.

값으로 캡처한 바인딩은 기본적으로 읽기 전용입니다. `mutable` 람다는 자신이 소유한 캡처의 변경을 다음 호출에도 유지합니다. 함수 값의 복사본은 같은 환경을 공유하고, 별도로 평가하여 만든 클로저는 독립된 환경을 가집니다. 값 초기화 캡처는 바깥 범위에서 왼쪽부터 오른쪽으로 한 번 평가되며, 일반 이름으로 캡처한 const 바인딩은 읽기 전용으로 유지됩니다. 스칼라 지역 `static`은 처음 사용할 때 초기화되고 같은 람다 식에서 만든 클로저 사이를 포함하여 선언별로 저장소를 공유합니다. 제네릭 인스턴스와 생성된 진입점의 각 호출은 상태를 분리합니다. 초기화 실패 후 다시 시도할 수 있으며 재귀 초기화는 오류입니다. 참조 초기화 캡처, 비스칼라 지역 static, 중첩 람다에서 바깥 함수의 지역 static에 접근하는 경우는 Core 런타임 경로가 필요하며 독립 C# 생성에서 명시적으로 거부합니다.

독립 C# AOT는 콘솔 기능 조회, 크기, 색상, 커서와 화면 지우기를 지원합니다. 출력 리디렉션 시 제어 기능과 이스케이프 시퀀스를 제공하지 않습니다. 진입점 실행 경계에서 Windows 콘솔의 원래 속성과 커서 상태를 복원합니다. 다른 터미널에서는 스타일이나 표시 상태를 바꾼 후 스타일을 초기화하고 커서를 표시하며 스크립트 실패 시에도 정리합니다.

C# AOT는 함수 시그니처의 동등한 기본 형식 별칭에 같은 생성된 대리자 형식을 사용합니다. 참조 매개 변수/반환과 중첩 값 컨테이너도 포함됩니다. `long`/`int64` 및 `unsigned long`/`uint64`는 동등하지만 부호, 너비, 명목 개체 형식과 `ref`/`const ref` 한정자는 구분됩니다.

## 사용자 pair 템플릿

사용자 정의 `pair<T, U>`는 일반 제네릭 필드와 메서드를 사용합니다. 이 두 매개변수 템플릿이 소프트 내장 타입을 대체하면 독립 AOT는 맵의 `entries()`와 키·값 쌍 순회를 명시적으로 거부합니다. 이러한 작업에는 VM 백엔드를 사용하세요. 맵의 값 자체가 사용자 pair 타입인 경우 일반 값 순회는 계속 지원됩니다.

옵션을 받는 파일 생성 진입점(`FCSharpCodegenBackend.EmitFromFile`, `EmitCSharpSourceBundleFromFile`)은 Core 모듈 로더를 통해 진입 모듈과 가져오기를 읽습니다. 변경 불가능한 모듈 환경과 `SourceOverlays`는 가상 진입 경로에도 적용됩니다. 대상 런타임 어댑터가 필요한 네이티브 패키지 계약은 독립 출력에서 명시적으로 거부됩니다.

옵션을 받는 `EmitCSharpSourceBundleFromProgram` 오버로드도 모듈 환경이 게시한 바인딩 카탈로그를 사용하며 충돌하는 명시적 카탈로그를 거부합니다.

생성된 진입점은 `main` 이전의 실행 오류 처리 경계 안에서 컴파일러가 기록한 순서에 따라 스크립트 정적 데이터 멤버와 전역 변수를 초기화합니다. 초기화 중 예외와 `exit`에는 진입점 실행과 같은 처리 규칙이 적용됩니다. 초기화 전에 `int` 이외의 저장소를 읽거나 참조하려면 Core 런타임이 필요합니다. 원시 0 값을 형식이 지정된 C# 필드로 정확히 표현할 수 없기 때문입니다. 시작 검사는 정적으로 확인되는 호출과 직접적인 호출 가능 값 바인딩을 추적합니다. 이러한 저장소가 초기화되지 않은 동안 대상을 확인할 수 없는 시작 시 간접 호출은 거부됩니다. 지연 호출할 값을 생성하기만 하는 경우는 계속 지원됩니다. 직선 실행에서 먼저 쓴 뒤 읽는 경우와 일반 `int`의 0 값에 대한 전방 읽기도 계속 지원됩니다.

Core 컴파일이 성공한 뒤, 시작 시 함수 호출 및 타입 초기화 그래프의 중첩이 지나치게 깊으면 Core 런타임이
필요하다는 진단을 반환하고 생성을 거부합니다. 이 제한은 생성기의 네이티브 검사
스택을 보호하며 일반 재귀와 순차적인 시작 호출은 계속 지원됩니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
