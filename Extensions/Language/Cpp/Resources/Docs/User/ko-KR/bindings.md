# 호스트 바인딩 및 바인딩 카탈로그

호스트 바인딩은 C++ 호스트의 함수, 유형, 멤버 및 메타데이터를 VeroScript에 노출합니다. 바인딩 카탈로그는 해당 공용 인터페이스에 대한 JSON 설명입니다. CLI는 이를 검증하고 형식화할 수 있으며 스크립트를 확인하거나 실행할 때 하나 이상의 카탈로그를 로드할 수 있습니다.

카탈로그는 인터페이스 계약입니다. 이는 C++ 등록 코드를 대체하지 않으며 자체적으로 런타임 구현을 제공하지 않습니다. 카탈로그의 항목은 컴파일러가 해당 항목을 볼 수 있음을 의미합니다. 호스트는 스크립트가 실행되기 전에 일치하는 구현을 등록해야 합니다.

## 카탈로그 파일 및 버전

최상위 개체에는 다음 필드가 포함되어 있습니다. 카탈로그를 가져올 때 배열 필드가 생략될 수 있습니다. 호스트 내보내기는 그것들을 모두 씁니다. `version`이 있는 경우 정수 `1`이어야 합니다.

| 필드 | 유형 | 설명 |
| --- | --- | --- |
| `version` | 정수 | 바인딩 카탈로그 형식 버전. 현재 `1`. |
| `functions` | 배열 | 스크립트에서 호출할 수 있는 호스트 함수에 대한 설명입니다. |
| `genericTypeTemplates` | 배열 | 일반 호스트 유형 템플릿에 대한 설명입니다. |
| `types` | 배열 | 호스트 클래스, 구조체, 열거형 및 해당 멤버. |
| `typeAliases` | 배열 | 호스트 유형 별칭 설명. |
| `annotations` | 배열 | 반영, 도구 및 문서화에 대한 주석 어휘입니다. |

완전한 빈 카탈로그는 다음과 같이 작성할 수 있습니다.

```json
{
	"version": 1,
	"functions": [],
	"genericTypeTemplates": [],
	"types": [],
	"typeAliases": [],
	"annotations": []
}
```

파일을 `.json`으로 저장합니다. 코드 블록의 `json` 레이블은 문서 구문 강조 레이블일 뿐입니다. JSON을 `.vs` 스크립트로 만들지는 않습니다.

## 기능 설명

`functions`의 각 객체에는 최소한 `name`, `returnType` 및 `paramTypes`이 필요합니다.

| 필드 | 유형 | 필수 | 설명 |
| --- | --- | --- | --- |
| `name` | 문자열 | 예 | 스크립트에 표시되는 이름입니다. |
| `returnType` | 문자열 | 예 | `int`, `string` 또는 `object`와 같은 스크립트 유형 이름입니다. |
| `paramTypes` | 문자열 배열 | 예 | C++ 등록과 동일한 순서의 매개변수 유형입니다. |
| `documentation` | 문자열 | 아니요 | 기능 문서. |
| `paramNames` | 문자열 배열 | 아니요 | 매개변수 순서에 따른 매개변수 이름입니다. |
| `paramDocumentation` | 문자열 배열 | 아니요 | 매개변수 순서대로 매개변수 문서화. |
| `returnDocumentation` | 문자열 | 아니요 | 반환 값 문서. |
| `annotations` | 객체 배열 | 아니요 | 함수의 주석 인스턴스입니다. |
| `paramAnnotations` | 객체 배열의 배열 | 아니요 | 매개변수 주석 외부 위치는 `paramTypes`에 해당합니다. |
| `returnHostTypeName` | 문자열 | 아니요 | `object`, `enum` 또는 `function` 반환에 대한 호스트 유형 이름 또는 서명입니다. |
| `paramHostTypeNames` | 문자열 배열 | 아니요 | `paramTypes`과 병렬인 호스트 유형 이름입니다. |
| `returnElementTypeName` | 문자열 | 아니요 | 단항 컨테이너 반환의 요소 유형입니다. |
| `paramElementTypeNames` | 문자열 배열 | 아니요 | 단항 컨테이너 매개변수의 요소 유형입니다. |
| `returnKeyTypeName` | 문자열 | 아니요 | 지도 반환의 키 유형입니다. |
| `returnValueTypeName` | 문자열 | 아니요 | 지도 반환의 값 유형입니다. |
| `paramKeyTypeNames` | 문자열 배열 | 아니요 | 지도 매개변수의 주요 유형입니다. |
| `paramValueTypeNames` | 문자열 배열 | 아니요 | 맵 매개변수의 값 유형입니다. |
| `paramFlags` | 정수 배열 | 아니요 | 매개변수 전달 모드; 생략하면 모든 매개변수가 값으로 전달된다는 의미입니다. |
| `typeParams` | 문자열 배열 | 아니요 | 일반 호스트 함수의 매개변수를 입력합니다. |
| `scriptReturnTypeName` | 문자열 | 아니요 | 일반 호출의 스크립트 측에서 사용되는 반환 유형입니다. |
| `genericHostObjectCast` | 부울 | 아니요 | 해당 일반 등록에 대해 일반 호스트-객체 변환을 활성화합니다. |

모든 병렬 배열은 동일한 매개변수 위치를 사용합니다. `paramHostTypeNames[1]` 및 `paramFlags[1]`은 `paramTypes[1]`를 설명합니다. 일관되지 않은 길이나 위치는 CLI 검증자의 진단에 따라 수정되어야 합니다.

### 유형 이름 및 컨테이너 메타데이터

`returnType` 및 `paramTypes`은 호스트 바인딩에서 지원되는 유형 이름을 사용합니다.

| 유형 카테고리 | 허용되는 이름 |
| --- | --- |
| 기본 및 호출 가능 유형 | `void`, `bool`, `char`, `short`, `int`, `long`, `float`, `double`, `string`, `wstring`, `unsigned`, `unsigned int`, `unsigned char`, `unsigned short`, `unsigned long`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `enum`, `function`, `fn` |
| 호스트 개체 | `object`, `HostObject` |
| 단항 컨테이너 | `array`, `DynamicArray`, `set`, `DynamicSet`, `multiset`, `MultiSet`, `ordered_multiset`, `OrderedMultiSet`, `queue`, `stack`, `deque`, `channel`, `priority_queue`, `PriorityQueue` |
| 지도 | `map`, `DynamicMap`, `ordered_map`, `OrderedMap`, `multimap`, `MultiMap`, `ordered_multimap`, `OrderedMultiMap` |

컨테이너 요소, 키 및 값 유형은 별도의 메타데이터 필드에 저장됩니다.

| 유형 | 반환 필드 | 매개변수 필드 |
| --- | --- | --- |
| `array`, 세트, 큐, 스택, 데크, `channel` 및 우선순위 큐 | `returnElementTypeName` | `paramElementTypeNames` |
| `map`, `ordered_map`, `multimap` 및 `ordered_multimap` | `returnKeyTypeName`, `returnValueTypeName` | `paramKeyTypeNames`, `paramValueTypeNames` |
| `object`, `enum` 및 `function` | `returnHostTypeName` | `paramHostTypeNames` |

예를 들어, `Widget` 호스트 개체를 반환하고 `array<int>` 및 `map<string,double>`를 허용하는 함수는 해당 유형을 다음과 같이 설명할 수 있습니다.

```json
{
	"name": "inspect_widget",
	"returnType": "object",
	"paramTypes": ["array", "map"],
	"returnHostTypeName": "Widget",
	"paramElementTypeNames": ["int", ""],
	"paramKeyTypeNames": ["", "string"],
	"paramValueTypeNames": ["", "double"]
}
```

### 매개변수 플래그

각 `paramFlags` 요소는 비트 마스크입니다.

| 가치 | 의미 |
| --- | --- |
| `0` | 값으로 전달됩니다. |
| `1`(`0x01`) | `const`이 아닌 참조입니다. |
| `3`(`0x01 | 0x02`) | `const` 참조. |

다른 비트 조합은 유효하지 않습니다. `const` 비트는 참조 비트와 함께만 유효합니다. 플래그는 등록된 C++ 서명과 일치해야 합니다. 카탈로그는 값별 C++ 함수를 참조 함수로 변환할 수 없습니다.

## 호스트 유형 및 구성원

`types`의 각 개체에는 `name` 및 `kind`가 필요합니다. `kind`은 `enum`, `struct` 또는 `class`이어야 합니다.

| 필드 | 유형 | 설명 |
| --- | --- | --- |
| `name` | 문자열 | 스크립트에 표시되는 이름입니다. |
| `kind` | 문자열 | `enum`, `struct` 또는 `class`. |
| `documentation` | 문자열 | 문서를 입력하세요. |
| `annotations` | 객체 배열 | 유형에 대한 주석 인스턴스입니다. |
| `inheritable` | 부울 | 유형이 파생을 허용하는지 여부입니다. |
| `scopedEnum` | 부울 | 열거형이 범위가 지정된 액세스 의미 체계를 사용하는지 여부입니다. |
| `hasClone` | 부울 | 복사 동작이 제공되는지 여부입니다. |
| `hasEquals` | 부울 | 동등 비교가 제공되는지 여부입니다. |
| `hasHash` | 부울 | 해싱 제공 여부. |
| `hasSubclass` | 부울 | 호스트 하위 클래스 구성이 지원되는지 여부입니다. |
| `genericTemplateName` | 문자열 | 이 유형이 인스턴스화된 일반 호스트 템플릿입니다. |
| `genericTypeArguments` | 문자열 배열 | 일반 템플릿에 대한 실제 유형 인수입니다. |
| `members` | 객체 배열 | 생성자, 메서드, 속성, 필드 또는 열거형 멤버입니다. |

각 구성원에는 `kind`이 필요합니다. 일반 회원에게도 `name`이 필요합니다. 지원되는 멤버 종류는 다음과 같습니다.

| `kind` | 의미 |
| --- | --- |
| `constructor` | 건설자; 그 이름은 빈 문자열일 수 있습니다. |
| `method` | 인스턴스 방법. |
| `staticMethod` | 정적 방법. |
| `propertyGetter` | 부동산 게터. |
| `propertySetter` | 속성 설정자. |
| `enumMember` | 열거형 멤버입니다. |
| `field` | 인스턴스 필드. |
| `staticConstant` | 정적 상수. |

멤버는 함수 문서, 주석, 매개변수, 반환 및 유형 메타데이터 필드를 허용합니다. 추가로 다음을 지원합니다.

| 필드 | 유형 | 설명 |
| --- | --- | --- |
| `enumValue` | 정수 | 열거형 값; 안전한 JSON 숫자 범위 내의 정확한 JSON 정수여야 합니다. |
| `enumValueInt64` | 문자열 | 완전 부호 있는 64비트 열거형 값. 정확한 JSON 숫자 범위 밖에서 사용하세요. |
| `operator` | 부울 | 회원이 운영자인지 여부. |
| `constMethod` | 부울 | 메소드가 `const`인지 여부입니다. |
| `mutableThis` | 부울 | 메소드가 현재 객체를 변경할 수 있는지 여부입니다. |
| `nativeName` | 문자열 | 기본 호스트 이름. 스크립트에 표시되는 이름은 `name`입니다. |

멤버는 `returnType`, `paramTypes` 및 `paramFlags`에 대해 동일한 의미를 함수로 사용합니다. 열거형 항목에는 `enumMember`을 사용합니다. 정확한 JSON 정수 범위를 벗어나는 값의 경우 `enumValue`에 대략적인 JSON 숫자를 쓰지 마십시오. 대신 `enumValueInt64`에 10진수 표현을 작성하세요.

## 일반 템플릿 및 유형 별칭

### `genericTypeTemplates`

각 일반 템플릿에는 다음이 포함됩니다.

| 필드 | 유형 | 필수 | 설명 |
| --- | --- | --- | --- |
| `name` | 문자열 | 예 | 템플릿 이름. |
| `arity` | 정수 `1`–`255` | 예 | 유형 매개변수의 수. |
| `documentation` | 문자열 | 아니요 | 템플릿 문서. |

### `typeAliases`

각 별칭에는 다음이 포함됩니다.

| 필드 | 유형 | 필수 | 설명 |
| --- | --- | --- | --- |
| `name` | 문자열 | 예 | 별칭 이름. |
| `typeParams` | 문자열 배열 | 예 | 별칭 유형 매개변수. 비어 있을 수 있습니다. |
| `targetTypeName` | 문자열 | 예 | 대상 유형 이름. |
| `documentation` | 문자열 | 아니요 | 별칭 문서. |

예를 들어:

```json
{
	"genericTypeTemplates": [
		{ "name": "Box", "arity": 1, "documentation": "A generic host box." }
	],
	"typeAliases": [
		{
			"name": "WidgetId",
			"typeParams": [],
			"targetTypeName": "int",
			"documentation": "The identifier used by the host."
		}
	]
}
```

## 주석 어휘 및 인스턴스

최상위 `annotations` 배열은 주석 어휘입니다. 각 항목은 다음을 지원합니다.

| 필드 | 유형 | 설명 |
| --- | --- | --- |
| `name` | 문자열 | 주석 이름. |
| `detail` | 문자열 | 짧은 카테고리 또는 서명 설명. |
| `documentation` | 문자열 | 주석 문서. |
| `arguments` | 객체 배열 | 주석에 대한 인수 설명입니다. |
| `caseSensitive` | 불리언 | 기본값은 `true`이며 `false`이면 이 정의의 ASCII 영문 대소문자를 구분하지 않습니다. |
| `targets` | 정수 | 대상 비트 마스크. 기본값은 `131071`(지원하는 모든 대상)이며 유효 범위는 `1`~`131071`입니다. |

이름은 소스 표기를 유지합니다. 허용되는 이름 표기가 겹치는 등록은 거부됩니다. 인수 이름과 메타데이터 키는 대소문자를 구분합니다. 각 인수 설명은 같은 구조의 재귀적 `children` 배열을 포함할 수 있습니다. 이는 중첩 인수의 설명이며 실행 가능한 값이 아닙니다. 네이티브 등록에는 `FHostBindingRegistry::RegisterAnnotation(..., Targets, CaseSensitive)`를 사용합니다. 카탈로그 메타데이터는 프로세서 콜백을 등록하지 않습니다.

각 어휘 인수는 `name`, `detail` 및 `documentation`를 지원합니다. 어휘 문서에는 사용 가능한 주석 이름이 있습니다. 함수나 유형에 주석을 자동으로 적용하지 않습니다.

함수, 유형 또는 멤버의 `annotations` 필드에는 주석 인스턴스가 포함되어 있습니다.

```json
{
	"name": "HOST_FUNCTION",
	"arguments": [
		{ "name": "Category", "value": "Examples|Math" },
		{ "name": "ReadOnly" }
	]
}
```

`value` 필드는 생략될 수 있습니다. 생략은 값이 없는 플래그 인수를 의미합니다. `"value": ""`은 인수에 명시적으로 빈 문자열 값이 있음을 의미합니다. 이러한 의미는 서로 다릅니다. `paramAnnotations`는 매개변수 주석 인스턴스의 2차원 배열이며 해당 외부 위치는 `paramTypes`과 일치해야 합니다.

통합 Resources 문서에서 [바인딩 카탈로그 예제](../../../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)를 받아 쓰기 가능한 작업 디렉터리에 `bindings.sample.json`으로 복사하세요. 사용자 주석 메타데이터와 호스트 형식을 보여 주는 예제이며 기능 SDK 아카이브에 포함된다고 가정하면 안 됩니다. 해당 디렉터리에서 아래 명령을 실행하고 `app.vs`를 자신의 스크립트로 바꾸세요.

## 유효성 검사, 서식 지정 및 로드

CLI는 카탈로그 유지 관리 명령을 제공합니다.

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

`-o` 또는 `--output`을 생략하면 `bindings format`은 입력 카탈로그를 다시 씁니다. 출력 경로를 지정하면 해당 파일에 씁니다. 표준 출력에는 형식화된 JSON이 아닌 쓰기 확인 메시지가 나오므로 결과는 출력 파일에서 읽으세요.

`validate`은 JSON 유형, 필수 필드, 지원되는 열거형 값, 병렬 배열 및 숫자 범위를 확인합니다. `format`은 툴체인의 안정적인 형식으로 JSON을 내보냅니다. 새 파일로 포맷하는 경우 임시 경로를 사용하고 원본을 교체하기 전에 결과를 검토하세요.

반복 가능한 `--binding-catalog` 옵션을 사용하여 스크립트를 확인하거나 실행하는 동안 하나 이상의 카탈로그를 로드합니다.

```powershell
veroscript --binding-catalog bindings.sample.json app.vs
```

가져온 카탈로그는 컴파일 시간 가시성과 메타데이터를 제공합니다. 가져온 함수 및 멤버 설명은 런타임 콜백을 대체하지 않습니다. 호스트가 일치하는 구현을 등록하지 않은 경우 런타임 시 스크립트가 계속 실패할 수 있습니다.

또한 가져오기 도구는 리소스 제한(`128 MiB` 최대 파일 크기, `4,194,304` JSON 값 및 중첩 깊이 `256`)을 적용합니다. 이러한 제한은 유효성 검사 중에 카탈로그가 과도한 리소스를 소비하는 것을 방지합니다.

## C++에서 바인딩 등록

임베디드 호스트는 일반적으로 `VeroScriptSession`을 사용하여 바인딩을 등록합니다. 공개 등록 경계에는 다음이 포함됩니다.

- 간단한 기본 함수 또는 매개변수 메타데이터가 있는 구조화된 함수의 경우 `RegisterNative`입니다.
- 호스트 클래스, 구조체 및 열거형의 경우 `RegisterHostType`입니다.
- 생성자, 메서드, 정적 멤버, 속성 및 필드의 경우 `RegisterHostMember`입니다.
- 호스트 유형의 운영자를 위한 `RegisterHostOperator`입니다.
- 호스트 유형 복사 동작의 경우 `RegisterHostTypeClone`입니다.
- 유형 및 열거형 의미 체계에 대한 `SetHostTypeInheritable` 및 `SetHostEnumScoped`
- 파생 가능한 호스트 유형의 인스턴스를 구성하기 위한 `RegisterHostTypeSubclassFactory`.
- 일반 호스트 유형 템플릿의 경우 `RegisterGenericHostTypeTemplate` 및 `InstantiateGenericHostType`.
- 현재 세션에서 내보낸 카탈로그를 읽기 위한 `GetBindingCatalog`입니다.

호스트 등록 후 카탈로그를 내보내고 CLI 유효성 검사기를 실행하여 설명서, 정적 검사 및 런타임 등록이 정렬된 상태로 유지되도록 합니다. 정적 호스트 멤버는 일반적으로 `Type.staticMethod(...)`으로 액세스됩니다. 호스트 상수는 `Type::Constant`을 사용합니다. 더 넓은 엔진 수명 주기와 `CheckSource`/`RunSource` 또는 `CheckFile`/`RunFile` 사용법에 대해서는 [호스트 통합](embedding.md)을 참조하세요.

## nullable 호스트 객체 인자

명시적 `HostObject` 인자를 가진 static host member는 script의 `nullptr`를 받을 수
있습니다. 콜백에는 빈 호스트 객체 핸들이 전달되므로 payload를 역참조하기 전에
확인해야 합니다. null instance receiver는 여전히 유효하지 않으며, 이 규칙은 static
call의 명시적 인자에 적용됩니다.

## 호환성 원칙

바인딩 카탈로그는 공개 인터페이스의 일부입니다. 함수나 멤버를 추가하는 것은 일반적으로 호환되는 확장입니다. 항목 제거, 매개변수 유형 또는 참조 플래그 변경, 열거형 값 변경, 호스트 유형 이름 변경 또는 멤버 가시성 변경으로 인해 스크립트가 중단될 수 있습니다. 카탈로그 또는 호스트 바인딩을 업그레이드할 때 카탈로그 유효성 검사, 스크립트 확인 및 대상 프로젝트의 통합 테스트를 함께 실행하세요.

## 호스트 오버로드

<a id="host-overload-resolution"></a>

일반 호스트 인스턴스 메서드, 정적 메서드 및 네이티브 자유 함수는 정규화된 매개변수 형식과 참조 한정자에 따른 오버로드를 지원합니다. 중복 매개변수 서명과 반환 형식만 다른 선언은 등록 시 거부합니다. 호출은 인수 형식, 숫자 변환 및 variant 대체 규칙으로 선택하고 동률은 모호성 진단을 생성합니다. 명명된 호스트 멤버는 서명에서 안정적인 네이티브 식별자를 생성합니다. 생성자와 연산자의 기존 규약은 유지합니다. 이름만 사용하는 `FindFunction` 은 오버로드 집합에 null 을 반환합니다. `GetFunctions()` 로 각 선언과 개별 문서를 열거할 수 있습니다. 바인딩 카탈로그 내보내기와 가져오기는 모든 오버로드를 보존합니다. 기존 단일 서명 등록 API 는 계속 사용할 수 있습니다.

`ref` 를 생략하면 기존 네이티브 값 입력/핸들 동작을 유지하며 호출자 저장소 교체에는 명시적 `ref` 가 필요합니다. 일반 인수에는 사용 가능한 값 서명을 우선합니다. 명시적 참조는 저장 형식과 쓰기 가능 여부를 검사하고 `const ref` 는 const 변수를 받을 수 있습니다. 컴파일 확장은 모든 오버로드를 보존합니다. VM, Mixed, JIT 및 Core AOT 는 코어 선택을 공유합니다. 독립 C++ 소스 내보내기는 별도의 C 연결 브리지 규약을 유지하며 지원하지 않는 명명된 호스트 오버로드에는 Core AOT 사용을 안내합니다.

## 네이티브 콜백 메타데이터

IScriptCallInvoker::GetActiveModule()은 현재 네이티브 콜백 동안만 빌린 읽기 전용 의미 메타데이터를 제공합니다. 지원하지 않는 실행 호스트는 nullptr을 반환할 수 있습니다. 콜백 이후 포인터, 빌린 호출 서비스나 내부 참조를 보관하면 안 됩니다. 값이나 프로그램 소유자를 유지해도 이 대여 기간은 늘어나지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/bindings.md) · [fr-FR](../fr-FR/bindings.md) · [ja-JP](../ja-JP/bindings.md) · [ko-KR](bindings.md) · [zh-CN](../zh-CN/bindings.md) · [zh-TW](../zh-TW/bindings.md)
