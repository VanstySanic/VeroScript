# 지역화 리소스와 배포

이 페이지는 카탈로그 및 빌드·배포 도구 유지보수자를 위한 문서입니다. 언어 선택, 사용자 카탈로그 및 SDK 사용은 [진단 지역화](../../User/ko-KR/localization.md)를 참조하세요.

## 소스 소유권과 배포

번역 소스는 각 모듈 또는 영역의 `Resources/Localization/<culture>/`에서 관리합니다. 여러 Python 도구가 사용하는 메시지만 Python 영역에서 공유합니다. 빌드와 패키징은 필요한 의존 모듈의 카탈로그를 배포 디렉터리 `localization/`에 모읍니다. Runtime/Core SDK는 CLI, Tooling, LSP, DAP, Interop 의존 리소스를 포함하며, WebAssembly는 코어와 C++, Python 패키지는 코어, C++, Interop, Python 도구 카탈로그를 포함합니다. IDE와 설치 프로그램의 UI 카탈로그는 독립적으로 유지합니다.

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

소스 저장소에서 직접 실행하면 코어는 자체 카탈로그를 찾고 각 SDK 또는 도구 모듈은 자체 Resources/Localization을 추가합니다. 명시적으로 로드한 카탈로그와 배포 카탈로그가 기준이 되며, 교체가 성공한 뒤 소스 리소스가 자동으로 추가되지 않습니다.

## 소스 리소스 검색

`VERO_LOCALIZATION_DIR`은 완전한 현지화 루트를 지정합니다. 설정되지 않았거나 로드에 실패하면 코어는 다음 위치를 순서대로 검사하고 첫 번째 유효한 트리에서 멈춥니다.

1. 실행 파일 옆의 `localization`, 이어서 실행 파일 디렉터리를 기준으로 한 `../assets/localization`.
2. 실행 파일 디렉터리의 최대 여덟 단계 상위 디렉터리: 각 디렉터리의 `localization`, 다음으로 그 디렉터리 자체 또는 저장소의 바로 아래 자식에서 찾은 소스 모듈의 코어 카탈로그;
3. 현재 작업 디렉터리의 `localization`.
4. 현재 디렉터리, 부모, 조부모 순서로 소스 모듈을 찾으며, 찾은 모듈의 `VeroScript/Resources/Localization`을 읽습니다.

소스 검색은 `VeroScript/Public/VeroScriptVersion.h`가 있는 모듈 또는 `CMakeLists.txt`가 있고 그런 바로 아래 자식이 정확히 하나인 저장소를 허용합니다. 바깥 디렉터리 이름은 바꿀 수 있으며, 소스 모듈이 없거나 모호하면 무시합니다. `InitializeModuleFromEnvironment`는 초기화가 소스 카탈로그를 선택한 경우에만 소스 모듈에 상대적인 `Resources/Localization`을 추가합니다. 명시적으로 읽거나 배포된 트리는 자체 리소스 집합을 유지합니다.

## 릴리스 확인

- 모든 배포에는 완전하고 유효한 `en-US` 디렉터리가 포함되어야 합니다.
- 릴리스에 필요한 모든 추가 언어는 대상 호스트와 함께 복사, 포함 또는 패키지되어야 합니다.
- 번역된 문화 경로 및 메시지 ID는 영어 기준의 하위 집합이어야 합니다.
- 이전 도메인 우선 `en.json`/`zh-CN.json` 레이아웃을 사용하지 마십시오. 현재 형식은 `<culture>/<component>.json`입니다.
- 진단 ID는 표시 텍스트보다 안정적이므로 호스트, LSP 및 DAP는 논리에 `MessageId`를 사용해야 합니다.
- 패키징 후 사용 가능한 언어 세트, 누락된 키에 대한 영어 대체 및 진단 자리 표시자 대체를 확인합니다.


## 모바일 리소스와 언어

공통 모바일 카탈로그 목록, 배포 경로와 명시적 로드 절차는 [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/ko-KR/index.md#mobile-localization)에서 관리합니다.

번역 버퍼 소유권, 오류와 내보내기 유지보수는 [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/ko-KR/abi-and-lifetime.md#localization-abi)에 정의합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/localization-and-resources.md) · [fr-FR](../fr-FR/localization-and-resources.md) · [ja-JP](../ja-JP/localization-and-resources.md) · [ko-KR](localization-and-resources.md) · [zh-CN](../zh-CN/localization-and-resources.md) · [zh-TW](../zh-TW/localization-and-resources.md)
