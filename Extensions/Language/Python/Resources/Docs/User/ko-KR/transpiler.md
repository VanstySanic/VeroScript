# Python Transpiler

`VeroScriptPythonTranspiler`는 엄격한 정적 Python 3.11 부분집합을
VeroScript 소스로 변환합니다. Python 프런트엔드의 선언 분석을 공유하며
Python 런타임 및 VeroScript-to-Python AOT와는 독립적입니다.

형식화된 함수, 일반 클래스, 정수 enum, 주석이 있는 필드와 지역
변수, 생성자, 메서드, 리터럴, 호출, 대입, 조건문, `while`, 일반 표현식을
지원합니다. 임의 정밀도 정수, 동적 import, async, comprehension, generator,
descriptor, property, 다중 상속, reflection, 주석 없는 지역 변수는 조용히
변환하지 않고 진단으로 거부합니다.

`@dataclass`는 거부됩니다. 자동 생성되는 생성자, 동등 비교 및 다른 런타임
동작은 구현되지 않았습니다. `metaclass`를 포함한 클래스 키워드 인수도 런타임 동작이
구현되지 않아 거부됩니다. 지원하는 부분집합 안에서 생성자와 메서드를
명시적으로 작성한 일반 클래스를 사용하세요.
내장 `str(...)` 변환은 Python의 서식 의미론이 구현되지 않아 거부됩니다.
현재 모듈에서 선언하거나 명시적으로 가져온 `str`이라는 이름의 사용자 클래스와 함수는 계속 호출할 수 있습니다.

클래스 본문의 필드 기본값은 불변 스칼라 리터럴, 지원되는 순수 상수식과 알려진 정수 enum
멤버를 허용합니다. 공유 컨테이너, 객체, 임의의 이름 참조 및 함수 호출은 거부됩니다. 인스턴스 값은 명시적인
`__init__`에서 초기화하세요. 컨테이너는 형식 주석이 있는 지역 변수에 먼저 만든 뒤
인스턴스 필드에 대입합니다.

```powershell
python -m veroscript_python_transpiler input.py `
  -o input.vs --source-map input.vs.map.json `
  --veroscript veroscript
```

`--timeout <seconds>`는 외부 컴파일러 검증 시간을 제한하며 유한한 양수여야 합니다.
라이브러리 API에서는 두 옵션 형식의 `timeout_seconds`로 같은 값을 지정합니다.

기본적으로 생성 결과를 실제 VeroScript compiler로 검증합니다. 프로젝트
모드는 상대 import, 출력 모듈 구조, source map과
`transpile.manifest.json`을 함께 처리합니다. API는 `transpile`,
`transpile_source`, `transpile_file`, `transpile_files`를 제공합니다.

`as`로 프로젝트 모듈이나 선언의 이름을 바꾸는 가져오기는 명시적으로 거부됩니다. 원래 이름을 사용하세요.
`import typing as t` 같은 메타데이터 가져오기는 계속 지원됩니다.

## 검증과 프로젝트 모드

외부 컴파일러 검증은 기본적으로 활성화됩니다. 별도 단계가 생성 코드 검증을 명시적으로 담당하는 경우에만 `--no-validate`를 사용합니다. `--timeout` 기본값은 `120.0`초이며 프로젝트 전체가 아닌 각 외부 호출에 적용됩니다. 소스, 소스 맵과 매니페스트는 교체 전에 준비하며 쓰기 실패 시 복원을 시도합니다. 해결되지 않거나 모호한 가져오기는 거부됩니다.

```powershell
python -m veroscript_python_transpiler package/models.py package/service.py `
  --project-root . `
  --output-dir generated/veroscript `
  --veroscript veroscript
```

`len(str)`은 UTF-8 바이트 수가 아니라 유니코드 코드 포인트 수를 셉니다. 보충 평면 문자와 결합 부호도 각각 코드 포인트로 계산합니다. 문자열 리터럴 길이는 실행 시 의존성이 없습니다. 그 밖의 선언된 문자열은 선형 Core UTF-8 버퍼 보조 코드를 사용하고 표준 라이브러리 `string.vs`를 가져옵니다. 이 경로는 Python의 O(1) 길이 조회와 달리 O(n) 시간과 O(n) 임시 공간을 사용합니다. Core 런타임이 필요하며 현재 독립 Python AOT는 명시적으로 거부합니다. 생성되거나 기존에 있는 `string.vs`가 이 의존성을 가리지 않아야 합니다. 컨테이너 길이는 기존 Core 직접 연산을 유지합니다. 표현식의 문자열／컨테이너 타입을 확인할 수 없다면 먼저 타입을 명시한 지역 변수에 저장하세요.

Python 전역, 가져오기 또는 지역 바인딩 이름이 `print`, `len`, `abs`, `min`, `max`이고 Core가 이를 내장 함수 호출로 가로채는 경우에는 거부합니다. 일반 메서드와 선언된 `str`은 계속 호출할 수 있습니다. 기본값 없이 타입만 선언한 필드는 읽기 전에 명시적으로 초기화해야 합니다. Python의 누락된 속성 및 완전한 확정 대입 의미는 이 하위 집합에서 모델링하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
