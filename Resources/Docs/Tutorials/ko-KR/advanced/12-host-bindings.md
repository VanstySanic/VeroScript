# 12: 호스트 바인딩 및 스크립트 통합

중요한 VeroScript 사용 사례 중 하나는 이를 C++ 호스트에 포함하는 것입니다. 호스트는 엔진이나 세션을 생성하고 기능과 유형을 등록합니다. 스크립트는 안정적인 공개 표면을 호출합니다. 이 튜토리얼에서는 먼저 바인딩 카탈로그를 사용하여 경계를 구체적으로 만든 다음 통합 순서를 설명합니다.

## 1단계: 바인딩 카탈로그 검사

바인딩 catalog는 host API의 JSON 설명입니다. 문서 리소스나 별도 Resources 배포에서 [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)을 쓰기 가능한 학습 디렉터리에 같은 이름으로 복사합니다. SDK archive에는 example이 포함되지 않습니다. 다음 명령을 학습 디렉터리에서 실행합니다.

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

카탈로그에는 `version`, `functions`, `genericTypeTemplates`, `types`, `typeAliases` 및 `annotations`와 같은 최상위 필드가 포함됩니다. 인터페이스를 설명합니다. C++ 등록 코드를 대체하거나 자체적으로 런타임 구현을 제공하지 않습니다.

카탈로그를 컴파일러에 표시되는 인터페이스 목록으로 처리합니다. 함수에는 최소한 `name`, `returnType` 및 `paramTypes`가 필요합니다. 호스트 개체와 컨테이너에는 해당 호스트 유형, 요소 또는 키/값 메타데이터도 필요합니다. 참조 매개변수는 `paramTypes`와 병렬인 `paramFlags`을 사용하고, 매개변수 주석은 병렬 `paramAnnotations`를 사용합니다. 이러한 필드는 C++ 등록 서명과 일치해야 합니다.

예를 들어 샘플 카탈로그의 `host_add` 함수 항목은 두 개의 `int` 매개변수를 사용하고 첫 번째 매개변수에 주석을 답니다.

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

카탈로그 메타데이터 배열을 런타임 구현으로 취급하지 마십시오. 인터페이스와 툴링 표면만 설명합니다. 모든 필드, 지원되는 유형 이름, 일반 템플릿, 별칭 및 JSON 제한은 [호스트 바인딩 참조](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)를 참조하세요.

## 2단계: 스크립트에서 호스트 기능 호출

호스트가 함수를 등록한 후 스크립트는 이를 일반 선언처럼 호출할 수 있습니다. 예를 들어 호스트가 `HostLog`으로 등록되었다고 가정합니다.

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

정적 멤버가 있는 유형의 경우 스크립트는 일반적으로 유형 이름을 사용합니다.

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

이 이름은 통화 형태만 보여줍니다. 실제 이름은 호스트 등록 및 카탈로그에서 나와야 합니다.

## 3단계: 순서대로 통합

C++ 호스트에서는 다음 순서를 사용합니다.

실행 전에 `FEngineConfig::VmBackendFactory`/`VmBackendPath` 또는 Session 선택 API로 호환 VM을 지정하세요. C++ SDK 링크만으로는 인터프리터가 선택되지 않으며 컴파일 스크립트에도 VM이 필요합니다. 필요한 SDK 대상과 설정은 [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/ko-KR/index.md)를 참고하세요.

1. `VeroScriptEngine` 또는 수명이 긴 `VeroScriptSession`을 만듭니다.
2. 기본 함수, 호스트 유형, 멤버 및 연산자를 등록합니다.
3. 도구가 노출해야 하는 API에 대한 문서 및 매개변수 메타데이터를 추가합니다.
4. 바인딩 카탈로그를 내보내거나 로드하고 `bindings validate`를 실행합니다.
5. 먼저 `CheckSource`/`CheckFile`를 호출한 후 확인 후 `RunSource`/`RunFile`을 호출하면 성공합니다.
6. 부울만 반환하는 대신 검사 및 실행 실패에 대한 진단을 유지합니다.

스크립트 수명이 긴 편집기, 게임 및 호스트 도구는 일반적으로 모듈, 바인딩, 실행 상태 및 핫 리로드에 `VeroScriptSession`을 사용합니다. 원샷 실행은 `VeroScriptEngine`의 소스 및 파일 진입점을 사용할 수 있습니다.

## 4단계: 인터페이스를 안정적으로 유지

바인딩을 버전이 지정된 API로 처리합니다. 기능 추가는 일반적으로 호환 가능한 확장입니다. 멤버 제거, 매개변수 유형 변경, 열거형 값 변경 또는 소유권 의미 체계 변경으로 인해 스크립트가 중단될 수 있습니다. 호스트 버전을 배송하기 전에 다음을 확인하세요.

- 바인딩 카탈로그가 유효성을 검사합니다.
- 스크립트는 `--check`을 전달합니다.
- 등록 테이블이 카탈로그와 일치합니다.
- 개체, 구조체 및 핸들 수명이 문서화됩니다.

등록한 static host member에 명시적인 `HostObject` 인자가 있으면 `nullptr` nullable
경계도 테스트하십시오. 콜백에는 빈 호스트 객체 핸들이 전달되므로 payload를 역참조하기
전에 확인해야 합니다. null instance receiver는 유효하지 않습니다.

## 연습

1. 하나의 호스트 함수에 대한 안정적인 이름, 매개변수 목록 및 반환 유형을 디자인합니다.
2. 바인딩에 간단한 문서와 매개변수 설명을 추가합니다.
3. 하나의 카탈로그 항목을 수정하고 카탈로그 유효성 검사와 스크립트 검사를 모두 실행합니다.

전체 경계는 [호스트 통합](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md) 및 [호스트 바인딩](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/bindings.md)을 참조하세요.


다음: [런타임 반영](13-reflection.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/advanced/12-host-bindings.md) · [fr-FR](../../fr-FR/advanced/12-host-bindings.md) · [ja-JP](../../ja-JP/advanced/12-host-bindings.md) · [ko-KR](12-host-bindings.md) · [zh-CN](../../zh-CN/advanced/12-host-bindings.md) · [zh-TW](../../zh-TW/advanced/12-host-bindings.md)
