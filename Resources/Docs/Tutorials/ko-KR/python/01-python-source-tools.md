# 01: Python 소스 도구 파이프라인

전제 조건: 일치하는 Python Frontend, BindGen, Transpiler package를 설치하고 `veroscript`를 사용할 수 있게 합니다. 쓰기 가능한 디렉터리에 지원되는 typed `host_api.py`, `app.py`, `package/models.py`, `package/service.py`를 준비하고 경로를 함께 조정합니다. runtime binding에는 구성된 Python SDK Session도 필요합니다. [Python SDK 가이드](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/index.md)를 참고하세요.

## 1. 선언 분석

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

생성된 AST에서 module, 함수, class와 source location을 확인합니다. 입력
module은 import되거나 실행되지 않습니다.

## 2. 호스트 계약 생성

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

Catalog의 함수를 호스트가 등록할 실제 callable과 비교합니다. Catalog는
등록 구현을 대신하지 않습니다.

## 3. 소스 변환

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs --source-map generated/app.vs.map.json `
  --veroscript veroscript
veroscript --check generated/app.vs
```

의미를 보존할 수 없는 Python 기능은 조용히 근사하지 않고 진단으로 거부됩니다.

## 4. project mode 사용

모듈이 서로 import할 때는 모든 입력을 같은 project로 전달하여 선언과 import
해결이 일관되도록 하십시오.

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py --project-root . --output-dir generated/bindings --validate --veroscript veroscript
python -m veroscript_python_transpiler package/models.py package/service.py --project-root . --output-dir generated/veroscript --veroscript veroscript
```

생성된 manifest, source map과 출력물은 서로 대응하는 묶음으로 보관하고 다른 입력
집합에서 생성한 artifact를 섞지 마십시오. 런타임에서는 애플리케이션이 관리하는
방식으로 모듈을 import하고 병합된 Catalog를 한 번에 등록합니다.

등록 예제는 구성된 `session`, 명시적으로 import한 `package.models`/`package.service`의 module object `models`/`service`, 그리고 `generated/bindings/bindings.json`의 parsed mapping 또는 `generate_binding_project` 결과인 `project_bindings`를 전제합니다. source tool은 이 module들을 대신 import하지 않습니다.

```Python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

이 helper는 session을 변경하기 전에 모듈 범위를 확인하고, 모듈 사이에서 참조되는
멤버를 연결하기 전에 project 타입을 등록합니다.

## 5단계: 런타임 바인딩과 값 확인

런타임에 등록할 enum에는 `IntEnum` 또는 `IntFlag`를 사용합니다. 일반
`enum.Enum`은 거부됩니다. enum 멤버 이름과 정수 값은 Catalog와 정확히 일치해야
하며 instance method와 `staticMethod`의 descriptor 종류도 일치해야 합니다.

`Value(None)`은 script의 `null`을 나타내고 `Value.priority_queue("int", [3, 1, 2])`
는 priority queue를 만듭니다. nullable이 아닌 값에 `None`을 대입하면 `TypeError`가
발생합니다. Catalog와 런타임 module의 불일치를 찾도록 host test에 추가하십시오.

## 연습

1. 주석이 있는 enum을 추가하고 Catalog의 값을 확인합니다.
2. 주석 없는 local을 추가하고 진단을 읽습니다.
3. 연결된 두 module을 project mode로 처리하고 manifest를 비교합니다.
4. 생성된 Catalog를 Python host에 로드하고 등록 구현이 생성된 signature와
   일치하는지 확인합니다.

독립 실행 Python artifact가 필요하면 다음으로
[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/aot.md)를 읽으십시오.

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/python/01-python-source-tools.md) · [fr-FR](../../fr-FR/python/01-python-source-tools.md) · [ja-JP](../../ja-JP/python/01-python-source-tools.md) · [ko-KR](01-python-source-tools.md) · [zh-CN](../../zh-CN/python/01-python-source-tools.md) · [zh-TW](../../zh-TW/python/01-python-source-tools.md)
