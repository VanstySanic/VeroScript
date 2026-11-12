# C++ Transpiler

`VeroScriptCppTranspiler`는 의미를 보존할 수 있는 엄격한 C++17 부분집합을 VeroScript
소스로 변환합니다. [C++ 프런트엔드](frontend.md)의 AST와 타입 매핑을 공유하지만
BindGen 및 Interop ABI와는 독립적입니다.

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

생성 소스와 선택적 소스 맵을 출력합니다. 기본적으로 VeroScript 컴파일러로 검증하며
파싱·lowering·검증이 성공한 뒤에만 출력합니다. project mode는 모든 입력을 먼저 파싱하고
병합한 뒤 입력별 `.vs`와 map, 결정적 manifest를 생성합니다. manifest는 생성 모듈을
implicit import로 선언하고 project-wide access를 활성화합니다.

project manifest의 안정적인 필드는 `version`, `kind`, `allowProjectWideAccess`,
`modulePaths`, `implicitImportPaths`, `files`입니다. `files`의 각 항목에는 `input`,
`source`, `sourceMap`이 포함됩니다. manifest, 생성 소스와 소스 맵은 같은 변환 실행에서
생성된 것을 함께 사용해야 하며 생성 파일 경로는 project-relative입니다.

프로젝트 모드 예시:

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

manifest 구조 예시:

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

CLI는 반복 가능한 `--attribute-map cpp=vero` 매핑을 지원합니다.
`veroscript::`와 `vero::` attribute는 직접 변환하며 다른 namespace는 명시적 매핑이
필요합니다. 생성 소스는 기본적으로 검증되며 별도 검증 단계가 담당할 때만
`--no-validate`를 사용하십시오.

CLI 옵션은 단일 출력의 `-o`/`--output`, 소스 맵의 `--source-map`, project mode의
`--output-dir`, manifest의 `--manifest`, 반복 가능한 `--attribute-map`,
`--no-validate`, `--version`, `--help`입니다. project mode에서는 입력마다 소스와 맵을
생성합니다.

C++ API의 `FTranspileOptions`는 검증, 소스 맵과 진단에 기록할 생성 소스 경로,
추가로 알려진 host/enum 형식, 알려진 type alias 및 명시적 attribute 매핑을
제어합니다. project 옵션에서는 검증과 attribute 매핑이 project 출력에 적용됩니다.

값 기반 코드, namespace, enum, class/struct, 생성자, 메서드, 전역, 컨테이너,
`this->member`, scalar `static_cast`, `typedef`/`using` 별칭, 일반 및 `u8` raw string,
typed `throw`, `try`/`catch`, catch-all, rethrow를 지원합니다. alias template은 구조화된
선언 형식에서 확장됩니다. attribute는 VeroScript annotation으로 변환되며 매개변수
attribute는 BindGen에 보존됩니다. 클래스 외부 정의와 정적 데이터 정의는 project mode에서
병합됩니다.

class, struct와 free function template은 `typename`/`class` 매개변수만 사용할 때 끝의 기본
형식 매개변수를 포함하여 VeroScript open generic으로 변환됩니다.

소유권 연산, 임의 포인터, RTTI, coroutine, 고급 C++ template 기능, shared ownership,
해결되지 않은 `extern`, allocation, reference field/global, 표현할 수 없는
`constexpr`/`consteval`/`constinit`/`noexcept`, `goto` 및 지원하지 않는 attribute/friend
형식은 거부합니다.

비형식 매개변수, parameter pack, specialization, SFINAE, concept, 종속 compile-time
표현식, method-local template과 명시적인 function-template argument call은 지원 범위
밖입니다. opaque 함수 본문, friend token sequence 또는 initializer에서 참조하는 generic
alias도 거부합니다.

일반 메서드 선언은 형식의 선언 모듈에, 본문은 구현 모듈에 유지됩니다.
생성자, 소멸자와 정적 데이터는 core의 동일 파일 제약을 따릅니다. 기본 검증은
manifest의 가시성 정책으로 최종 모듈을 컴파일하고 오류를 원본 C++ 파일에 매핑합니다.
이름 검색 범위를 보존할 수 없는 병합은 거부합니다. class template의 클래스 외부
정의는 아직 지원하지 않으며 명시적인 진단을 반환합니다.

프로젝트 병합은 링커가 아닙니다. 한정된 네임스페이스 정의는 해당 네임스페이스 선언으로 유지됩니다. 내부 연결 함수나 변수가 충돌하면 `cpp.merge.internal_linkage_collision`을 보고하며 이름을 바꾸거나 입력 순서로 정의를 선택하지 않습니다. 생성 소스와 소스 맵이 어긋나지 않도록 여러 출력을 함께 확정합니다.

## 숫자 타입과 정수 리터럴

지역 변수와 `static_cast` 대상은 선언 시그니처와 같은 플랫폼 타입 매핑을 사용합니다. 정수 리터럴 타입은 도구를 빌드한 플랫폼의 C++17 데이터 모델에 따라 선택하고 스크립트 접미사로 변환합니다. 8진수, 2진수, 자릿수 구분자의 값을 보존합니다. 16진 부동소수점과 `long double` 리터럴은 명시적으로 거부합니다. 숫자 변환은 대상 타입 범위 안의 값을 요구하며 범위를 벗어나면 스크립트 오류가 발생합니다. C++ 축소 변환의 잘림이나 부호가 다른 타입의 혼합 승격에 의존하는 코드는 명시적으로 다시 작성해야 합니다.

`std::pair<T,U>`는 soft builtin `pair<T,U>`로 매핑됩니다. 같은 이름과 타입 인수 수의 보이는 사용자 템플릿이 이 타입을 가로채면 프로젝트 모드나 생성 소스 검증을 끈 경우에도 `cpp.transpile.standard_pair_shadowed`로 거부합니다. 사용자 템플릿 이름을 바꾸거나 명시적 어댑터를 제공해야 합니다. 타입 인수 수가 다르거나 관계없는 네임스페이스의 템플릿은 공존할 수 있습니다.

VeroScript 템플릿 매개변수는 전역 타입이나 일반 별칭과 같은 이름을 사용할 수 없습니다.
해당 C++ 선언은 프로젝트 모드와 생성 소스 검증을 끈 경우에도 출력 전에
`cpp.transpile.template_parameter_shadow_unsupported` 진단으로 거부됩니다.
트랜스파일 전에 C++ 템플릿 매개변수 이름을 바꾸세요.
별칭 템플릿은 구조적으로 해석되며 자체 매개변수 바인딩을 유지합니다.

구조화된 선언은 템플릿 인수 안에서도 사용자 타입 이름을 절대 한정하여 출력 네임스페이스가 달라도 같은 타입을 유지합니다.
기반 타입은 원래 조회 범위를 보존합니다. 대상 상속 문법은 내부의 동명 타입에 가려진 전역 기반 타입을 표현할 수 없습니다.
이 경우 생성 소스 검증을 꺼도 출력 전에 `cpp.transpile.global_base_shadowed` 진단으로 거부됩니다.
내장 컨테이너나 타입 매개변수 자체를 기반 타입으로 사용하는 선언은 `cpp.transpile.base_type_unsupported`로 거부됩니다. 상속에는 알려진 클래스 또는 구조체가 필요합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
