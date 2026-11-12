# Python SDK

독립 Python 생성과 제한 사항은 [Python AOT](aot.md)도 참조하십시오.

`VeroScriptPython`은 VeroScript Interop ABI를 위한 CPython 어댑터입니다. 스크립트 로드와 실행, 함수 호출, 타입이 지정된 콜백과 클래스 노출, 메타데이터 조회, 패키지와 값 처리를 제공합니다.

VM, mixed, JIT 백엔드, 구조화된 진단, 타입이 지정된 컨테이너, 호스트 바인딩, 소스 오버레이, 핫 리로드, JIT 통계, 리플렉션, 직렬화, 변경 이벤트와 지역화를 지원합니다.

## 설치

```text
python -m pip install veroscript
```

확장은 네이티브 Interop ABI의 정확한 일치를 요구합니다. 현재 값은 `0x00000101` (0.1.1)이며, 일치하지 않으면 `veroscript`를 가져올 때 `ImportError`가 발생합니다. Wheel의 네이티브 라이브러리를 함께 사용하고 Interop 라이브러리를 다른 릴리스의 파일로 교체하지 마십시오. 가져오기에 성공한 뒤 `veroscript.ABI_VERSION`으로 ABI를, `veroscript.RUNTIME_VERSION`으로 런타임 버전을 확인할 수 있습니다.

Wheel에는 Python 확장, 일치하는 Interop 라이브러리, 카탈로그, 타입 정보와 표준 라이브러리가 포함됩니다. 네이티브 확장은 CPython 마이너 버전과 아키텍처에 종속됩니다. `py.typed`와 `_veroscript.pyi`도 포함됩니다. Python 콜백 예외는 `CallbackError`로 변환되어 C ABI를 넘어 전파되지 않습니다.

배포되는 Wheel은 GIL이 활성화된 표준 CPython 3.11–3.15를 지원합니다. 디버그 및 자유 스레딩 인터프리터는 지원하지 않습니다. 운영 체제, 프로세스 아키텍처 및 CPython 마이너 버전이 일치하는 Wheel을 선택하세요. Linux Wheel의 호환성은 빌드 환경에 따라 달라지므로 다른 Linux 배포판에서 만든 Wheel이 현재 배포판에서도 작동한다고 가정하지 마세요.

Python SDK 아카이브나 설치 관리자의 네이티브 모듈은 해당 패키지에 사용된 CPython 마이너 버전에만 맞습니다. 다른 지원 버전을 사용하려면 해당 인터프리터 환경에 일치하는 Wheel을 설치하세요.

## 최소 실행

```Python
from veroscript import Backend, Session

source = """
int main(string[] args)
{
	print("arguments={}", len(args));
	return 0;
}
"""

with Session(backend=Backend.MIXED, script_args=("one", "two")) as session:
	result = session.load_source(source, "main.vs")
	if not result.success:
		raise RuntimeError(result.diagnostics)
	result = session.run()
```

## 문서 지도

- [세션과 실행](session.md): 구성, 호출, 결과, JIT, 리로드와 스레드.
- [값과 컨테이너](values.md): 스칼라, 소유권, 컨테이너와 보존 callable.
- [호스트 바인딩](host-bindings.md): 함수, 데코레이터, 클래스와 콜백.
- [패키지, 리플렉션과 지역화](packages.md).
- [Python 프런트엔드](frontend.md): 형식화된 Python의 정적 AST.
- [Python BindGen](bindgen.md): Binding Catalog와 프로젝트 산출물.
- [Python Transpiler](transpiler.md): 형식화된 Python을 검증된 VeroScript로 변환.

VeroScript 문법과 의미의 기준은 언어 레퍼런스입니다. 이 모듈은 Python 경계만 설명합니다.

[Android/iOS의 Python](mobile.md)

## 관리 메모리

호스트 API: `session.memory` / `current_memory()` / `set_memory_limits`. 소유권, 할당량, 뷰 만료와 임대는 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md) 문서를 참고하세요.

- [호스트에서 사용하는 관리 메모리](memory.md)

## variant

[인터롭 SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#exported-value-lifetime)을 참조하세요.

```python
from veroscript import Session, Value
with Session() as session:
	payload = Value(42, kind="int32")
	box = session.variant("int", payload)
	assert box.variant_has_value()
	assert box.variant_type_name() == "int"
	extracted = box.variant_get()
	assert extracted.value == 42
```

`Session.load_application(absolute_path, options=...)`로 애플리케이션을 로드하고 결과를 확인한 뒤 `run()`을 호출합니다. 호스트 옵션의 경로는 절대 경로여야 합니다. 환경, 서명 및 신뢰 설정은 [Application 로드 계약](../../../../../../../VeroScriptApplication/Resources/Docs/User/ko-KR/index.md)을 따릅니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
