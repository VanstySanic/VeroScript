# Python 패키지, 리플렉션과 지역화

## 패키지 종류와 빌드 결과

[인터롭 SDK — 패키지 및 핫 리로드](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#package-container-format)을 참조하세요.

`PackageKind.APPLICATION`은 실행 가능한 패키지를, `PackageKind.LIBRARY`는
라이브러리 패키지를 만듭니다. `Session.build_package_file(...)`은
`(data, RunResult)`를 반환하므로 `Package(data)`를 만들기 전에
`result.success`와 진단을 확인하십시오. `Package.from_file(path)`은 파일을
분석하고 `package_kind`가 종류를 표시합니다. 분석은 컨테이너 구조만 검증하며
발행자 진위는 검증하지 않습니다. 신뢰할 수 없는 바이트를 로드하기 전에
`canonical_signed_bytes`에 대한 서명을 검증하십시오.

분석된 `Package`는 변경할 수 없는 스냅샷입니다. 다른 바이트를 읽으려면 새 객체를
만드십시오. `__init__`을 다시 호출하면 `veroscript.Error`가 발생합니다.

## 서명 대상과 catalog

공개 속성 `canonical_signed_bytes`는 서명 검증에 사용하는 정규 바이트를
반환합니다. `merge_catalog_directory`는 runtime catalog를 교체하지 않고
애플리케이션 catalog를 추가합니다.

`Package.from_file()`은 패키지를 읽고 `package_kind`는 종류를 표시합니다. 세션의 `load_package`, `reload_package`, `build_package_file`로 로드, 재로드와 생성을 수행합니다. 패키지는 로드 경계이지 보안 샌드박스가 아닙니다.

`reflection()`은 함수, 타입, 메서드, 필드, 인자와 메타데이터를 반환하고 `bindings()`는 호스트 바인딩을 반환합니다.

`set_language`, `get_language`, `available_languages`로 언어를 관리하고 `merge_catalog_directory`로 외부 카탈로그를 합칩니다. 신뢰할 수 있는 배포물만 사용하고 키와 형식 인자를 검증하십시오. 언어는 진단과 형식만 바꾸며 스크립트 의미는 바꾸지 않습니다.

`ABI_VERSION`, `RUNTIME_VERSION`, `CAPABILITIES`는 로드된 네이티브 런타임을 식별합니다. 패키지, 리플렉션, 컨테이너 값, JIT 진단, 핫 리로드 또는 실행 거버넌스와 같은 선택적 기능에 의존하기 전에 기능을 확인하십시오. 실행 거버넌스에는 협력적 명령/시간 제한, 취소와 표준 라이브러리 기능 정책이 포함됩니다. 성능 측정에서는 시작, hot call, 패키지, 캐시, Python 변환, GIL과 콜백 비용을 분리하십시오. JIT 통계는 진단 정보이며 `direct_native_calls`는 VM 값 브리지를 거치지 않고 다른 네이티브 진입점에 도달한 세대 범위 JIT 호출 수를 나타냅니다. JIT가 항상 더 빠른 것은 아닙니다.

## 반환된 객체 참조

`enumerate_object_references(instance)`는 스크립트 객체 `Value`를 받으며 세션이 유휴 상태일 때 생성 스레드에서 호출해야 합니다. `ExternalReference` 튜플을 반환합니다. `kind`는 `ExternalReferenceKind.SCRIPT_OBJECT` 또는 `ExternalReferenceKind.HOST_OBJECT`이며 `value`는 복제된 `Value` 래퍼를 소유합니다. 참조만 복사하고 객체 식별성은 유지하며 객체 그래프 전체를 복제하지 않습니다. 튜플과 값은 열거가 끝나도 보관할 수 있고 빌린 콜백 뷰가 아닙니다. 일반 값, 세션, 해제 및 스레드 규칙은 계속 적용됩니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
