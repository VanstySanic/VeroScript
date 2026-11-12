# C# 패키지, 리플렉션과 지역화

## 패키지 메타데이터

[인터롭 SDK — 패키지 및 핫 리로드](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#package-container-format)을 참조하세요.

`GetMetadata()`는 소유된 메타데이터를 반환하고 `HasCompiledModule`은 모듈을
아직 전송할 수 있는지 나타냅니다. `GetCanonicalSignedBytes()`는 서명 대상
바이트를 반환합니다. catalog는 `VeroScriptRuntime.LoadCatalogDirectory` 또는
`VeroScriptRuntime.MergeCatalogDirectory`로 로드하거나 병합합니다.

`VeroScriptPackage.Read`로 패키지 바이트를 구문 분석합니다. `LoadPackage`, `ReloadPackage`, `BuildPackageFile`이 패키지를 처리하고 `VeroScriptPackageKind`, `VeroScriptPackageMetadata`가 종류와 메타데이터를 표현합니다. 패키지는 전송과 로드 경계이지 샌드박스가 아닙니다.

`ExportReflectionJson`은 스크립트 함수, 타입, 필드, 메서드와 인자를, `ExportBindingsJson`은 호스트 바인딩을 내보냅니다. 반사 정보는 도구와 검증에 사용할 수 있지만 실행 시 권한 검사를 대신하지 않습니다. `EnumerateObjectReferences`의 값은 차용 수명을 따릅니다.

`VeroScriptRuntime`은 현재 언어, 사용 가능한 언어 JSON, 카탈로그 로드와 병합을 제공합니다. `GetAvailableLanguagesJson`으로 실제 포함된 BCP 47 태그를 확인하십시오. 언어는 진단과 형식만 변경합니다.

`VeroScriptRuntime.Capabilities`를 확인한 뒤 JIT, 패키지, 리플렉션, reload, 직렬화와 실행 거버넌스를 사용하십시오. 실행 거버넌스에는 협력적 명령/시간 제한, 취소와 표준 라이브러리 기능 정책이 포함됩니다. 시작, hot call, 캐시, 값 변환과 콜백 비용을 나누어 측정하십시오.

`VeroScriptRuntime.AbiVersion`은 어댑터 ABI `0x00000101`(0.1.1)과 정확히 일치해야 합니다. 관리 패키지, 네이티브 Interop 라이브러리와 런타임 카탈로그를 일치하는 릴리스에서 배포하십시오.

## 모듈 소유권 및 서명

`LoadPackage` 또는 `ReloadPackage`가 성공하면 컴파일된 모듈이 세션으로 전송됩니다. 메타데이터와 정규 서명 바이트는 계속 읽을 수 있지만 같은 인스턴스에서 모듈을 다시 전송할 수 없습니다. 다른 전송이 필요하면 원래 바이트를 다시 파싱하세요. `Read`는 컨테이너 형식만 검증하며 게시자 진위는 검증하지 않습니다. 신뢰하지 않는 패키지를 로드하기 전에 `GetCanonicalSignedBytes()`에 대한 서명을 확인하세요. `BuildPackageFile`은 패키지 바이트와 `VeroScriptRunResult`를 반환하며 옵션으로 애플리케이션/라이브러리 종류, 이름, 버전, 메타데이터 및 디버그 정보 제거를 설정합니다.

`EnumerateObjectReferences`의 값은 콜백 동안만 빌린 값이므로 반환 후 보관하지 마세요. 프로세스 전체 언어를 언제 변경할지 정하여 동시 작업의 진단 언어가 섞이지 않게 하세요. SDK 자체 인수·수명 검사 예외는 영어일 수 있습니다. 언어 선택은 런타임 진단에 적용됩니다.

[C# 값과 소유권](values.md#managed-call-lifetime)을 참조하세요.

## 진단 언어 선택

선택한 태그를 `VeroScriptRuntime.CurrentLanguage`에 지정합니다. 선택 항목을 제공하기 전에 `VeroScriptRuntime.GetAvailableLanguagesJson()`으로 실제 사용 가능한 태그를 확인합니다. 이 속성은 프로세스 전체의 진단 언어를 바꾸므로 다른 세션과 변경 시점을 조정합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
