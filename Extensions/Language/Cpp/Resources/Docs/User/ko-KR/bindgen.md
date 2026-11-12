# C++ BindGen

`VeroScriptCppBindGen`은 지원되는 C++ 선언을 VeroScript Binding Catalog로 변환합니다.
카탈로그는 컴파일러와 도구용 메타데이터이지 런타임 구현이 아닙니다. 생성된 callback
stub은 native thunk가 아니므로 실제 C++ 함수는 호스트 어댑터가 등록해야 합니다.

`GenerateBindings`는 AST를 받고, `GenerateBindingsFromSource`, `GenerateBindingsFromFile`,
`GenerateBindingsFromFiles`는 소스를 받습니다. `GenerateBindingProject`는 하나의
`CatalogJson`, 선택적 `ApiReferences`, 결정적 `ManifestJson`, 진단 및 `Succeeded()`를
반환합니다. `FBindGenOptions::bEmitApiReference`로 API reference 출력을 선택합니다.

`FBindGenProjectLayout`은 병합 카탈로그, API 참조 디렉터리와 프로젝트 매니페스트의 출력 위치를 지정합니다. 컴파일러와 IDE에는 병합 카탈로그를 제공하세요. 입력별 참조는 문서 보기이며 병합된 선언 집합을 대체하지 않습니다.

project manifest의 안정적인 필드는 `version`, `kind`, `catalogRole`, `catalog`,
`inputs`, `apiReferences`입니다. `apiReferences`의 각 항목에는 `input`과 `path`가
포함됩니다. 병합된 catalog는 한 번만 출력되며 reference는 선택적인 사용자용 보기입니다.

프로젝트 모드는 병합된 catalog, 입력별 API reference와 manifest를 결정적인 결과
세트로 출력합니다. manifest 예시는 다음과 같습니다.

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

입력마다 API reference와 프로젝트 manifest를 생성하려면:

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

CLI 옵션은 필수 `--catalog <output.json>`, `--api-reference <output.vs.txt>`,
`--api-reference-dir <directory>`, `--manifest <output.json>`, `--version`, `--help`입니다.
두 reference 옵션은 동시에 사용할 수 없습니다.

정규화된 이름, enum identity와 underlying type, 멤버 종류, 생성자, 연산자, const 메서드,
`ref`/`const ref`, 타입 별칭과 alias template을 보존합니다. alias 기본값은 Catalog
스키마에 표현할 수 없어 거부됩니다. 타입, enum, 필드, 함수와 매개변수의 C++ attribute는
구조화된 annotation으로 보존되며 `veroscript::`/`vero::` 예약 namespace도 지원합니다.
enum initializer는 정수, 이식 가능한 단일 문자 리터럴, 앞서 선언된 enumerator,
괄호, 단항, 산술, 시프트, 비트, boolean, 논리, 관계 및 equality 연산을 검사하며
`int64` 오버플로를 확인합니다. 값이 없는 attribute는 flag로, 이름 있는 attribute
값은 catalog에서 보존됩니다.
raw pointer, return reference, host-to-host 상속, 삭제된
멤버, reference field, free global, 이름 있는 메서드 overload, free function overload,
default argument, internal-linkage free function과 기반 형식 범위를 벗어난 enum 값은
거부합니다. enum 초기화자는 검사된 방식으로 평가됩니다. 전체 JSON 필드와 등록 경계는
[호스트 바인딩](bindings.md)을 참조하십시오.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
