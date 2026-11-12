# Python AOT와 packaging

## 독립 실행 코드 생성

`VeroScriptPythonAot`는 작은 compatibility runtime을 포함한 readable Python module을 emit하며 VeroScript VM, Interop library, CPython extension을 load하지 않습니다. control wrapper는 executable path, SDK root, `bin`, environment, `PATH`에서 user-provided CLI를 찾습니다.

`FCodegenOptions`를 사용하는 SDK 호출은 `Bindings`의 주석 정의와
`CaseSensitive` 이름 정책을 처리 단계에 전달합니다. 주석 메타데이터만으로는
런타임 의존성이 추가되지 않습니다. 런타임 호스트 함수와 형식은 지원하지 않습니다.

## 번들 계약

bundle mode는 `__main__.py`, `__init__.py`, `_veroscript_runtime.py`, `_veroscript_program.py`, `python-aot.manifest.json`을 쓰고 complete import graph를 하나의 program으로 검사합니다. 파일을 임시 디렉터리에 준비한 뒤 교체하며, 빈 일반 디렉터리 또는 구조가 유효한 VeroScript manifest가 있고 외부 파일이 없는 디렉터리만 교체합니다. interpreter `__pycache__`는 허용 generated content입니다.

최종 경로의 심볼릭 링크와 Windows junction은 거부합니다. 교체 중 기존 디렉터리를 옮기므로 출력 경로가 잠시 없을 수 있습니다. 교체에 실패하면 기존 디렉터리 복원을 시도합니다.

## 배포 계약

wheel/source distribution은 version-matched native extension, Interop library, Python source, type information, standard library, locale catalog를 담습니다. `VeroScriptVersion.h`, package metadata, imported runtime version은 일치해야 합니다. release validation은 CPython/platform matrix, installed-package type surface, source-distribution rebuild를 확인합니다.

Python 독립 런타임은 `ValueError`/`OverflowError`를 노출하지 않고 Core 부동소수점 결과로 바꿉니다. 0의 로그는 음의 무한대, 거듭제곱 오버플로는 홀수 정수 지수의 부호를 유지하고 floor/ceil/trunc는 부호 있는 0을 보존합니다. Python/C# 생성 NaN 문자열은 Darwin을 포함한 대상 플랫폼 표기를 따릅니다. 이미터 회귀 검증으로 유지하고 공개 유한성 검사는 [Math](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/math.md#floating-results)를 참조하세요.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/aot-and-packaging.md) · [fr-FR](../fr-FR/aot-and-packaging.md) · [ja-JP](../ja-JP/aot-and-packaging.md) · [ko-KR](aot-and-packaging.md) · [zh-CN](../zh-CN/aot-and-packaging.md) · [zh-TW](../zh-TW/aot-and-packaging.md)
