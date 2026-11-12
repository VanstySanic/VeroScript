# 진단 현지화

이 페이지는 진단 및 호스트 도구의 표시 언어를 설명합니다. 로드한 카탈로그가 표시 문구를 선택하며 스크립트 의미는 바꾸지 않습니다. 애플리케이션 문구는 별도의 [std::i18n API](standard-library/i18n.md)를 사용합니다. 앱 컨텍스트를 변경해도 프로세스 진단 언어는 바뀌지 않습니다.

언어 선택은 로드된 카탈로그에 있는 진단에 적용됩니다. CLI 도움말과 일부 인수 검증 메시지는 현재 영어로 표시하며, 카탈로그에 없는 메시지는 원문을 유지합니다.

## 지원 정책

표준 패키지는 `en-US`, `fr-FR`, `ja-JP`, `ko-KR`, `zh-CN`, `zh-TW`를 제공합니다. 사용 가능한 언어는 실제로 로드한 카탈로그에 따라 달라집니다. `GetAvailableLanguageTags` 또는 해당 SDK API로 확인하세요. 유효한 문화권 디렉터리를 추가하면 다른 언어를 제공할 수 있습니다. 애플리케이션에 함께 배포하거나 카탈로그 루트를 명시적으로 로드하세요.

애플리케이션은 `en-US`, `zh-CN` 및 `zh-TW`와 같은 표준 BCP 47 태그를 사용해야 합니다. 또한 런타임은 `ja_JP.UTF-8`, `@modifier`가 포함된 태그, `C`/`POSIX`과 같은 일반적인 운영 체제 형식을 정규화합니다. 확인된 형식은 하이픈, 소문자 언어, 제목 대/소문자 스크립트 및 대문자 알파벳 영역 하위 태그를 사용합니다.

앞뒤 공백과 인코딩 및 수정자 접미사를 제거한 뒤 `C`/`POSIX`를 `en-US`로 해석합니다. `C.UTF-8`과 `POSIX.UTF-8@modifier`도 지원합니다.

## 일치 및 대체

언어 해결은 다음과 같이 진행됩니다.

1. 이미 로드된 표준 태그와 정확하게 일치합니다.
2. 마지막 하위 태그를 반복적으로 제거하고 상위 태그를 시도합니다.
3. 동일한 기본 언어에서 결정론적 후보를 선택합니다.
4. 후보가 없으면 명시적 선택 API는 실패를 보고하고 초기화는 `en-US`으로 폴백됩니다.

중국어의 간체·번체 표기 체계는 유지됩니다. `zh-Hant-HK`는 `zh-TW`, `zh-Hans`는 `zh-CN`에 일치할 수 있습니다. 명시적인 `Hans` 또는 `Hant` 하위 태그가 지역에 따른 추정보다 우선하며, 확장 및 비공개 용도 하위 태그는 표기 체계 선택에 사용하지 않습니다. 기본 카탈로그에서는 `zh-Hans-TW`가 `zh-CN`, `zh-Hant-CN`이 `zh-TW`에 일치합니다. 간체 또는 번체를 요청했을 때 다른 표기 체계를 자동으로 선택하지 않습니다. 번역 파일이나 메시지 키가 없으면 영어 기준 카탈로그에 해당 항목이 있는 경우 `en-US` 텍스트로 대체합니다.

`TrySetLanguageFromTag`이 성공하면 활성 언어는 실제로 일치하는 표준 태그가 됩니다. 선택에 실패하면 현재 언어가 변경되지 않습니다. `GetAvailableLanguageTags`은 현재 카탈로그에서 로드된 언어를 반환합니다.

영어 기준 카탈로그에 없는 진단은 원문을 유지합니다. 번역 카탈로그를 로드해도 스크립트 출력이나 임의의 앱 문자열은 번역되지 않습니다.

## 언어 선택

CLI는 `--lang`을 사용합니다. 환경 변수는 `VERO_LANG`입니다.

```powershell
veroscript --lang ko-KR --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

CLI는 명령줄 인수를 구문 분석하기 전에 환경과 카탈로그를 초기화하므로 `--lang`이 `VERO_LANG`을 재정의합니다. `VERO_LANG`는 운영 체제 언어를 재정의합니다. 두 값 중 하나를 확인할 수 없으면 초기화에서는 `en-US`을 사용합니다. 명시적으로 지원되지 않는 `--lang`는 인수 오류로 보고됩니다.

editor가 관리하는 Language Server session에서는 document를 열기 전에 BCP 47
preference를 `initialize.locale`에 설정하십시오. `workspace/didChangeConfiguration`은
analysis 설정을 바꿀 수 있지만 language는 다음 initialize에서 적용하여 열린 문서가
같은 catalog를 사용하게 하십시오. `veroscript-lsp`는 CLI와 같은 `tooling.json` 및
compiler catalog를 사용하므로 logic에서는 번역 text 대신 diagnostic ID를 사용해야 합니다.
`static class` 및 `static struct`의 제약을 포함한 파서 선언 진단도 동일한 안정적 ID와
카탈로그 경로를 사용합니다. 따라서 CLI와 LSP는 같은 소스 오류에 대해 동일한 현지화
메시지를 선택합니다.

## 디렉토리 레이아웃

현지화 루트에는 완전한 `en-US` 기준선이 포함되어야 합니다. 다른 모든 직접 디렉터리는 문화 디렉터리이며 해당 이름은 정규화된 정식 BCP 47 태그여야 합니다. VeroScript에는 현재 하이픈으로 연결된 정규 문화권 이름이 필요하므로 기본 `zh`, `ja` 또는 비정규 `zh_cn` 디렉터리는 거부됩니다.

기본 카탈로그 레이아웃은 다음과 같습니다.

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

트리는 동일한 규칙을 유지하면서 더 많은 문화권을 추가하거나 JSON 파일을 하위 디렉터리로 분할할 수 있습니다.

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

로더는 `.json` 파일을 재귀적으로 검색하고 상대 경로로 병합합니다. 번역된 모든 문화권에 대해:

- 번역된 각 파일은 `en-US`에서 동일한 경로를 가져야 합니다.
- 번역된 파일은 영어 파일에 없는 메시지 ID를 추가할 수 없습니다.
- 누락된 번역 파일 또는 키가 영어 텍스트를 사용합니다.
- 한 문화권 내의 파일은 중복된 메시지 ID를 정의할 수 없습니다.
- 문화 디렉토리는 중복되거나, 비어 있거나, 비정규적이거나, 규정되지 않을 수 없습니다.

각 JSON 파일은 키와 값이 문자열인 UTF-8 플랫 객체여야 합니다.

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

텍스트는 `{0}`부터 `{9}`까지 사용하여 진단 인수를 받을 수 있습니다. JSON은 중복 키, 유효하지 않은 이스케이프, 유효하지 않은 UTF-8, 후행 콘텐츠 또는 문자열이 아닌 값이 없는 완전한 객체여야 합니다.

## 로딩, 교체, 병합

`VERO_LOCALIZATION_DIR`로 완전한 지역화 루트를 지정합니다. 도구 실행 또는 SDK 초기화 전에 설정하세요. 유효한 명시적 트리가 없으면 런타임은 작업 디렉터리 리소스보다 실행 파일 주변의 배포 리소스(`localization`, `../assets/localization`)를 먼저 찾습니다. 자신의 카탈로그를 확실히 선택하려면 환경 변수 또는 `LoadCatalogDirectory`를 사용하세요. 자동 검색은 아직 카탈로그가 로드되지 않았을 때만 실행됩니다.

`LoadCatalogDirectory`은 전체 프로세스 전체 카탈로그 트리를 검증하고 교체합니다. 로드에 실패하면 이전에 로드된 트리가 활성 상태로 유지됩니다. `MergeCatalogDirectory`은 해당 인수가 완전한 유효한 카탈로그 트리여야 하며 모든 문화권을 활성 세트로 병합합니다. 기존 메시지 ID는 기본적으로 유지되며 재정의가 명시적으로 활성화된 경우에만 대체됩니다. 두 작업 모두 코어의 스레드로부터 안전한 프로세스 전체 지역화 상태를 사용합니다.

표준 라이브러리 루트, 라이브러리 패키지 루트 또는 모듈 파일의 형제 항목은 `localization/` 디렉터리를 가질 수 있습니다. 모듈 로더는 기존 ID를 재정의하지 않고 이러한 트리를 병합하므로 라이브러리가 호스트의 텍스트를 예기치 않게 변경하지 않고도 자체 메시지를 추가할 수 있습니다. 소스 오버레이는 논리 모듈 경로를 유지하므로 형제 카탈로그는 계속 검색 가능합니다.

## SDK 경계

- C++ 호스트는 `VeroScript::Locale` API를 직접 사용합니다. 언어 및 카탈로그 상태는 코어의 프로세스 전체에 적용됩니다.
- Interop은 `vs_interop_locale_set_language`, `vs_interop_locale_get_language`, `vs_interop_locale_get_available_languages`, `vs_interop_locale_load_catalog_directory` 및 `vs_interop_locale_merge_catalog_directory`를 통해 동일한 정책을 노출합니다.
- 웹/WASM 빌드는 가상 파일 시스템에 전체 빌드 시간 `localization` 트리를 포함합니다. `setLanguage`은 해당 빌드에 포함된 문화권만 선택할 수 있는 반면, `language` 및 `availableLanguages`는 표준 현재 언어 및 사용 가능한 카탈로그를 보고합니다.

CLI 옵션 및 환경 변수는 [명령줄 도구](../../../../../VeroScriptCli/Resources/Docs/User/ko-KR/index.md), C++ 호스트는 [호스트 통합](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ko-KR/embedding.md), C ABI는 [Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md), 웹 경계는 [웹/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ko-KR/index.md)를 참조하세요.

독립 Python Frontend, BindGen, Transpiler 및 AOT 도구는 네이티브 런타임과 별도로 `python_tools.*` 메시지를 선택합니다. `--lang`은 `VERO_LANG` 및 시스템 언어보다 우선합니다. 시스템 태그를 정규화하고 중국어 문자를 구분하며 정확한 태그, 부모, 결정적인 같은 언어 후보 순으로 검색하고 누락된 텍스트는 영어로 대체합니다. 네이티브 SDK의 프로세스 전체 언어는 변경하지 않습니다. [Python frontend](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/frontend.md)를 참조하세요.


## 모바일 언어 명시 설정

네이티브 Android AAR 및 iOS C ABI SDK는 여섯 언어 카탈로그를 제공하며 명시적으로 로드해야 합니다. 스크립트 실행 전에 전체 트리를 앱이 관리하는 실제 디렉터리로 복사하세요. APK 자산 경로는 파일 시스템 디렉터리가 아닙니다. Android AAR 리소스는 `veroscript/localization`, iOS SDK 리소스는 `assets/localization`에 있습니다. Android의 `VeroScriptEngine.loadCatalogDirectory(path)`와 `setLanguage(tag)`는 성공 여부를 bool로 반환합니다. iOS C ABI에서는 `vs_interop_locale_load_catalog_directory` 및 `vs_interop_locale_set_language`를 사용하고 오류 버퍼를 `vs_interop_buffer_destroy`로 해제합니다. 모든 반환값을 확인하세요.

C# 모바일 앱은 `VeroScriptMobileResources.InstallAsync`로 리소스를 설치한 뒤 `VeroScriptRuntime.LoadCatalogDirectory`를 호출하고 `VeroScriptRuntime.CurrentLanguage`를 설정합니다. Python은 `veroscript`를 가져올 때 인접한 `veroscript/localization` 트리를 로드합니다. 배포 패키지에 이 트리를 보존한 다음 `veroscript.set_language`를 호출하세요. [C# 모바일 통합](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ko-KR/mobile.md) 및 [Python 모바일 통합](../../../../../Extensions/Language/Python/Resources/Docs/User/ko-KR/mobile.md)을 참조하세요.

언어는 엔진별이 아닌 프로세스 전체에 적용됩니다. 선택에 실패하면 현재 언어가 유지됩니다. 스크립트 출력은 바뀌지 않습니다. 카탈로그에 없는 메시지는 원문을 유지하며 메모리 할당 실패 시에도 영어가 유지될 수 있습니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/localization.md) · [fr-FR](../fr-FR/localization.md) · [ja-JP](../ja-JP/localization.md) · [ko-KR](localization.md) · [zh-CN](../zh-CN/localization.md) · [zh-TW](../zh-TW/localization.md)
