# Python BindGen

`VeroScriptPythonBindGen`은 지원되는 정적 Python 선언을 Binding Catalog v1
JSON으로 변환합니다. 모듈을 import하거나 데코레이터를 실행하지 않습니다.
Catalog는 호스트 API 계약이며 런타임 구현 자체가 아닙니다.

공개 함수, 클래스, dataclass, `IntEnum`/`IntFlag`, 주석이 있는 필드, 생성자,
인스턴스/정적/클래스 메서드와 property를 지원합니다. 동적 또는 무형식
시그니처, 가변 인자, 기본 인자, 모듈 전역, 지원하지 않는 상속과 표현할 수
없는 형식은 실패합니다.

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host --catalog host.bindings.json `
  --api-reference host.vs.txt --validate --veroscript veroscript
```

`--timeout <seconds>`는 외부 검증의 시간을 제한하며 유한한 양수여야 합니다.

## 프로젝트 단위 생성

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . `
  --output-dir generated/bindings `
  --validate `
  --veroscript veroscript
```

`--output-dir`로 프로젝트 모드를 선택합니다. 입력이 하나여도 사용할 수 있고 여러 입력에는 필수입니다. `--project-root`와 `--manifest`는 이 모드에서만 유효하며, `--manifest`는 매니페스트 출력 경로를 바꿉니다. `--catalog`, `--api-reference`, `--module-name`과 함께 사용할 수 없습니다. 출력은 함께 준비하며 실패 시 가능한 범위에서 복구합니다. 매니페스트는 해당 카탈로그 및 참조 파일과 함께 사용하세요.

프로젝트 모드는 병합된 `bindings.json`, 입력별 API reference와
`bindgen.manifest.json`을 출력합니다. 라이브러리 API는
`generate_bindings`, `generate_bindings_from_source`,
`generate_bindings_from_file`, `generate_binding_project`,
`validate_catalog`, `bind_module`, `bind_modules`도 제공합니다. 두 binding helper는
애플리케이션이 명시적으로 import한 모듈만 사용하며 BindGen 자체는 분석 입력을
import하거나 실행하지 않습니다. `bind_modules`는 세션을 변경하기 전에 프로젝트의
모든 Catalog 소유 모듈이 제공되었는지 확인합니다.

bind할 때 enum은 `IntEnum` 또는 `IntFlag`를 상속해야 하며 Catalog의 멤버 이름과
정수 값이 정확히 일치해야 합니다. Catalog의 instance method는 instance method로,
`staticMethod`는 static/class method로 유지되어야 합니다. 불일치는 세션을 변경하기
전에 거부됩니다.

여러 모듈에서 같은 타입 이름을 선언하면 `models.Item`처럼 한정된 주석을
사용하세요. 모호한 단순 이름은 `Int8` 같은 스칼라 별칭과 일치해도 거부됩니다.
현재 모듈에서 선언한 타입은 프로젝트의 단순 이름 별칭과 스칼라 별칭보다
우선합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](bindgen.md) · [zh-CN](../zh-CN/bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
