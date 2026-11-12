# C# 트랜스파일러

`VeroScriptCSharpTranspiler`는 엄격하게 정적 분석할 수 있는 C# 12 부분집합을
VeroScript 소스로 변환합니다. `VeroScriptCSharpFrontend`와 Roslyn 의미 정보를
사용하며 텍스트 토큰 치환은 수행하지 않습니다. C# 런타임 SDK, BindGen 및
VeroScript-to-C# AOT 백엔드와 분리된 소스 변환 도구입니다.

## 단일 파일 변환

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript`는 생성된 소스를 검증할 VeroScript 실행 파일 또는 SDK 루트를
지정합니다. 검증은 기본적으로 활성화됩니다. 파싱, lowering, 검증 및 출력 staging이
모두 성공한 뒤에만 VeroScript 파일과 source map을 기록하므로, 변환이 실패해도
기존 출력은 교체되지 않습니다.

공개 library API는 `CSharpTranspiler.TranspileSource`와
`CSharpTranspiler.TranspileFile`을 제공합니다. 결과에는 생성된 소스, source-map
JSON 및 `CSharpDiagnostic` 목록이 포함됩니다. `CSharpTranspileOptions`에서 검증,
validator, 생성 경로, timeout 및 C# parse options를 설정할 수 있습니다.

## 프로젝트 변환

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

프로젝트 모드는 모든 입력을 하나의 C# compilation으로 분석하고 파일 간 symbol을
해결하며 `--project-root`를 기준으로 상대 경로를 보존합니다. 입력마다 VeroScript
소스 파일과 `.map.json` source map을 하나씩 기록합니다. 기본 manifest는
`transpile.manifest.json`이며 `--manifest`로 경로를 지정할 수 있습니다. 생성 파일과
manifest는 하나의 출력 세트로 commit되고 staging이 실패하면 rollback됩니다.

library API의 프로젝트 진입점은 `CSharpTranspiler.TranspileProject`입니다.
`CSharpTranspileProjectOptions`로 설정하며, 결과에는 생성 파일 레코드, deterministic
manifest JSON 및 diagnostics가 포함됩니다. 각 입력은 project root 안에 있어야 하고
같은 경로를 중복해서 지정할 수 없습니다.

출력 상대 이름이 충돌하면 뒤의 이름에 결정적인 path-hash suffix가 추가됩니다.

## 명령줄 옵션

| 옵션 | 범위와 의미 |
| --- | --- |
| `-o`, `--output` | 단일 파일 출력 경로입니다. 단일 파일 모드에서 필수이며 프로젝트 모드에서는 사용할 수 없습니다. |
| `-h`, `--help` | 사용법을 출력하고 종료합니다. |
| `--version` | 트랜스파일러 버전을 출력하고 종료합니다. |
| `--source-map` | 단일 파일 source-map 경로입니다. 기본값은 `<output>.map.json`이며 프로젝트 모드에서는 사용할 수 없습니다. |
| `--output-dir` | 프로젝트 모드를 활성화하고 생성 출력 디렉터리를 선택합니다. |
| `--manifest` | 프로젝트 manifest 경로입니다. 기본값은 출력 디렉터리의 `transpile.manifest.json`입니다. |
| `--project-root` | 프로젝트 상대 출력 이름을 계산할 root입니다. 모든 입력은 그 안에 있어야 합니다. |
| `--reference` | metadata assembly를 추가합니다. 여러 assembly에는 옵션을 반복합니다. |
| `--language-version` | 파싱에 사용할 C# language version을 선택합니다. |
| `--veroscript` | validator로 사용할 VeroScript 실행 파일 또는 SDK root를 선택합니다. |
| `--timeout` | 생성 소스 검증 timeout(초)을 설정하며 기본값은 120입니다. |
| `--no-validate` | 컴파일러 검증을 생략합니다. 별도의 명시적 단계가 생성 소스를 검증할 때만 사용합니다. |

입력이 둘 이상이거나 `--output-dir` 또는 `--manifest`가 있으면 프로젝트 모드가
됩니다. 프로젝트 모드에서는 항상 `--output-dir`가 필요합니다.

## 출력 메타데이터

source map은 source 및 generated 경로와 mapping 배열을 기록합니다. 각 mapping은
생성된 줄과 대응하는 source path, line 및 column을 식별합니다.

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

프로젝트 manifest는 다음의 안정적인 필드를 사용합니다.

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

manifest의 경로는 프로젝트 상대 경로이며 구분 기호로 `/`를 사용합니다. 같은
transpile 실행에서 생성된 manifest, source 및 source map을 함께 보관하십시오.

## 지원하는 C# 범위

다음 기능을 포함한 부분집합을 지원합니다.

- namespace, enum, class, struct 및 namespace 수준 선언으로 사용되는 static class;
- public, protected, private field, 대칭형 자동 property, constructor, static 및
  instance member, method;
- 제약이 없는 generic type과 free function, primitive type 및 표준 VeroScript
  container type;
- local inference, 호출, object/collection creation, indexing, cast, 산술, 대입,
  조건문, `do`/`while` 및 여러 선언이나 반복 식을 가진 classic `for`를 포함한 loop,
  `foreach`, `switch`;
- 일반적인 `throw`, `try`, `catch`, `finally`;
- type alias, constant local, `params`, delegate/function type, expression/block
  lambda;
- reference-returning method, reference local, label, `goto`. 해당 VeroScript
  aliasing 및 control-flow 형식으로 lowering됩니다.

`Console.Write`와 `Console.WriteLine`의 개행 동작 차이를 보존합니다. 지원되는
lambda에서는 C# by-reference closure 동작도 보존됩니다.
string literal은 NUL과 지원되는 다른 control character를 보존합니다. unpaired
UTF-16 surrogate는 VeroScript에 손실 없는 UTF-8 표현이 없으므로 안전하게 거부됩니다.

명시적 generic member invocation은 표현 가능한 경우 구체적인 타입 인자를
보존합니다. 예를 들어 `Helpers.Pick<long>(1)`은 `Helpers::Pick<int64>(1)`이
됩니다. nested type declaration은 모호하거나 잘못된 VeroScript를 생성하지
않도록 diagnostic으로 거부되므로 transpile하려면 namespace 범위로 이동하십시오.

## 명시적 한계

의미를 충실하게 표현할 수 없는 기능은 조용히 근사하지 않고 `VSCT2001` diagnostic으로
거부합니다. 현재 거부되는 범위는 다음과 같습니다.

- `async`/`await`, task, iterator, LINQ query syntax;
- delegate declaration 및 event;
- nullable reference semantics, unsafe/pointer code, reflection, `dynamic`;
- generic constraint 및 interface;
- `catch` filter;
- asymmetric 또는 computed property;
- constructor chaining;
- 여러 파일로 나뉜 partial type.

`--reference`는 메타데이터 어셈블리를 추가하도록 반복할 수 있고 `--language-version`은 C# 구문 버전을 선택합니다. 두 옵션 모두 지원되는 VeroScript 의미를 확장하지 않습니다. `--timeout`은 생성 소스 검증 시간을 제한합니다. 별도의 명시적 단계가 컴파일러 검증을 수행할 때만 `--no-validate`를 사용합니다. 소스 생성 성공만으로 출력이 실행 가능하다고 판단할 수 없습니다.

## 정수 변환 및 수치 지원 범위

Transpiler는 정수 덧셈, 뺄셈, 곱셈, 부호 반전, 증감, 복합 할당 및 정수 캐스트에서 해석된 비트 폭으로 순환하는 C# 기본 unchecked 동작을 보존합니다. 시프트 횟수는 31 또는 63으로 마스킹합니다. 복합 할당은 오른쪽 피연산자보다 먼저 대상과 기존 값을 한 번만 평가하며 자동 속성은 필드로 출력합니다. 정수 나눗셈과 나머지는 0인 제수 또는 부호 있는 최솟값/-1 오버플로에서 잡을 수 있는 스크립트 예외를 발생시킵니다. 정수 값인 `double` 상수도 부동 소수점 형식을 유지합니다.

<a id="numeric-and-console-boundaries"></a>

명시적 `checked`/`unchecked`, 상수가 아닌 부동 소수점 나눗셈·나머지 및 부동 소수점에서 정수로의 변환, 유한하지 않은 부동 소수점 상수는 검증을 꺼도 `VSCT2001`로 실패합니다. 유한한 컴파일 시간 수치 상수는 Roslyn이 계산합니다. 분석은 기본 unchecked 컴파일 설정을 사용하며 소비 프로젝트의 오버플로 검사 옵션을 가져오지 않습니다.

enum의 정수 캐스트는 선언된 기반 형식의 폭으로 순환합니다. 부호 없는 64비트 enum, enum 산술·비트 연산 변경, 상수가 아닌 `double`에서 `float`로의 축소 및 64비트 정수에서 `float`로의 변환은 `VSCT2001`로 실패합니다. Console은 문자열·정수·bool 단일 인수와 빈 `Console.WriteLine()`을 지원합니다. `Console.Write`는 개행 없는 `std::console::write`, `Console.WriteLine`은 개행하는 `std::print`를 사용합니다. bool 텍스트는 문자열 연결 및 `+=`에서도 `True`/`False`입니다. 표준 함수를 한정하여 이름이 같은 소스 함수가 출력이나 정수 텍스트 변환을 가로채지 못하게 합니다. 서식·다중 인수 Console 오버로드 및 부동 소수점·enum·객체·배열 서식 변환은 CLR 텍스트 규칙을 바꾸지 않고 명시적으로 거부합니다.

C# 트랜스파일러는 정수와 부동 소수점 혼합 비교 전에 Roslyn 피연산자 승격을 적용하여 코어 비교 전에 C# float 반올림을 보존합니다.

숫자 조건 분기는 Roslyn이 선택한 암시적 변환을 적용하여 정수 너비와 float 반올림을 보존하며 선택한 분기만 평가합니다. 조건 분기에서도 상수가 아닌 64비트 정수를 float로 변환하는 경우를 거부합니다.

## 링크된 출력 경로

Windows의 `\\?\`, `\\.\` 및 볼륨 GUID 경로 별칭은 최종 파일 시스템 위치로 비교합니다. 이런 별칭으로 검증을 우회할 수 없으며 서로 다른 출력 경로는 계속 지원합니다.

BindGen과 Transpiler CLI는 출력을 준비하기 전에 기존 디렉터리 및 파일 링크를 확인합니다. symlink나 Windows junction 경로로 입력 소스를 덮어쓰거나 같은 출력을 두 번 지정할 수 없습니다. 링크된 디렉터리 안의 서로 다른 출력은 계속 지원합니다.

`--timeout`과 `ValidationTimeout`은 호출마다 validator 종료와 표준 출력 및 표준 오류 읽기를 함께 제한합니다. 실행기가 종료한 뒤 자식 프로세스가 리디렉션 파이프를 계속 열어 두어도 기한이 지나면 `VSCT2004`로 검증이 실패합니다. 검증 실패 시 CLI 출력 파일을 교체하지 않습니다.

정적 필드와 자동 속성의 초기화는 생략하거나 컴파일 시간 상수여야 합니다. 상수가 아닌 정적 초기화와 정적 생성자는 검증을 꺼도 `VSCT2001`로 거부됩니다. VeroScript 전역 초기화는 CLR 형식 초기화 시점, 부작용 및 종속성 순서를 보존할 수 없기 때문입니다. 동적 정적 상태는 `Main` 또는 애플리케이션이 제어하는 메서드에서 명시적으로 초기화하세요.

문자열, 배열 및 매핑된 컬렉션 필드와 자동 속성에는 null이 아닌 명시적 초기화가 필요합니다. 매핑된 컬렉션에는 `List<T>`, `Dictionary<TKey, TValue>`, `SortedDictionary<TKey, TValue>`, `HashSet<T>`, `Queue<T>`, `Stack<T>`이 포함됩니다. 초기화 생략과 해당 형식에 대한 null 대입은 검증을 꺼도 `VSCT2001`로 거부됩니다. CLR null 참조를 빈 문자열이나 컨테이너로 바꾸면 동등성 비교와 예외 동작이 달라지기 때문입니다. 지원되는 null이 아닌 배열 및 매핑된 컬렉션과 null의 동등성 및 비동등성 비교는 계속 지원되지만 문자열과 null의 비교는 지원 범위 밖입니다.

## 출력 이름 충돌

Transpiler 프로젝트 출력 이름은 모든 호스트에서 대소문자를 구분하지 않고 Unicode NFC 정규화 후 비교합니다. 입력 경로 순서대로 이름을 할당합니다. 충돌하면 결정적인 경로 해시 접미사를 붙이고, 그 이름도 사용 중이면 숫자 접미사를 추가합니다. 파일 이름을 추측하지 말고 반환된 레코드나 매니페스트에서 실제 이름을 확인하세요.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
