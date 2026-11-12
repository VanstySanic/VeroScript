# Python 프런트엔드

`VeroScriptPythonFrontend`는 Python BindGen과 Python-to-VeroScript 변환기가
공유하는 정적 프런트엔드입니다. 입력 모듈을 import하거나 실행하지 않고
형식이 지정된 Python 선언 모델을 생성합니다.

소스 위치, 모듈과 import, 형식화된 함수, 클래스, dataclass, 정수 enum,
주석이 있는 필드와 매개변수, 데코레이터, 문서, 정규화된 형식 주석을
보존합니다. 모듈 최상위 실행, 동적 또는 해석되지 않는 형식, 표현할 수
없는 구문은 진단으로 거부됩니다.

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

프로젝트 분석은 여러 입력을 하나의 선언 집합으로 처리합니다.

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

입력별 AST와 `frontend.manifest.json`을 준비한 뒤 기록하고 실패 시 이전 출력
복원을 시도합니다. API는
`parse_source`, `parse_file`, `parse_files`를 제공합니다. 구조 분석에는
프런트엔드, 호스트 계약에는 BindGen, VeroScript 생성에는 Transpiler를
사용하십시오.

## 진단 폴백

`--lang`으로 진단 언어를 선택하세요. `VERO_LANG`과 시스템 언어보다 우선합니다. 번역 메시지가 없거나 사용할 수 없으면 영어로 대체됩니다. 진단 코드와 소스 위치는 모든 언어에서 동일합니다.

`--lang`은 VeroScript 카탈로그를 사용하는 도구 메시지에 적용됩니다. `argparse` 도움말과 인자 분석 오류, Python, 운영 체제 또는 외부 컴파일러에서 전달된 텍스트는 번역하지 않으며 각자의 언어 동작을 따릅니다.

## 옵션과 프로젝트 출력

단일 파일 모드에서는 `--module-name`으로 논리 모듈 이름을 지정하고 `--version`으로 도구 버전을 출력합니다. 프로젝트 모드는 패키지 구조를 유지하며 중복 입력, 중복 모듈 이름 및 `--project-root` 밖의 파일을 거부합니다. `--output-dir`는 `-o` 또는 `--module-name`과 함께 사용할 수 없습니다. 출력은 UTF-8이며 교체 전에 모두 준비합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
