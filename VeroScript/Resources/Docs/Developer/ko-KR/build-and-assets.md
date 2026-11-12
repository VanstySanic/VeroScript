# 코어 빌드와 런타임 asset

[교체 가능한 VM 백엔드](vm-backends.md)

## 빌드 대상

[교체 가능한 VM 백엔드](vm-backends.md#vm-provider-assembly)을 참조하세요.

## 런타임 asset

이 대상은 `VeroScriptRuntimeAssets`에 의존합니다. 공통 `Resources/Tools/CMake/VeroScriptLocalization.cmake` 배포 프로필을 사용하여 오래된 JSON 카탈로그를 제거하고 변경된 카탈로그와 표준 라이브러리 파일을 런타임 출력 디렉터리에 복사합니다. 개별 테스트 대상에도 이 의존성이 적용되어 현재 소스에 맞는 카탈로그를 사용합니다.

로더는 `VERO_LOCALIZATION_DIR`을 먼저 검사한 뒤 배포 및 소스 저장소 루트를 검색합니다. 정확한 순서는 [진단 현지화](localization-and-resources.md)를 참조하세요. 유효한 트리는 `en-US` 기준, 한정된 정규 BCP 47 문화권 디렉터리와 고유 메시지 ID를 요구합니다. 번역 파일과 키는 영어의 하위 집합일 수 있으며 누락된 항목은 영어로 대체합니다. 모듈과 표준 라이브러리는 기존 ID를 덮어쓰지 않고 유효한 카탈로그를 병합할 수 있습니다. 사용 가능한 언어는 로드된 리소스에서 검색됩니다.

## 바이너리 호환성

bytecode와 package 호환성은 asmjit 사용 여부와 독립적입니다. optional backend는 공개 runtime asset, diagnostic, module format 계약을 바꾸면 안 됩니다.

## 빌드 기반과 소스 카탈로그 추가

독립 저장소의 도우미는 `Resources/Tools/CMake`에 있습니다. 루트에서 `Script/` 접두사 없이 참조하세요. 공개 locale API의 `InitializeModuleFromEnvironment`는 발견한 소스 모듈에 상대적인 경로를 받습니다. 예는 `VeroScriptTooling` 및 `Extensions/Language/Cpp/VeroScriptCpp`입니다. 코어가 소스 카탈로그를 선택한 경우에만 기존 ID를 덮어쓰지 않고 병합합니다. 명시적 카탈로그 교체는 이 검색 상태를 지웁니다. `VeroScriptLocaleSourceTests`는 모듈 이름 변경 후 검색 및 모듈 카탈로그 추가를 검증합니다.

- [지역화 리소스와 배포](localization-and-resources.md)

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/build-and-assets.md) · [fr-FR](../fr-FR/build-and-assets.md) · [ja-JP](../ja-JP/build-and-assets.md) · [ko-KR](build-and-assets.md) · [zh-CN](../zh-CN/build-and-assets.md) · [zh-TW](../zh-TW/build-and-assets.md)
