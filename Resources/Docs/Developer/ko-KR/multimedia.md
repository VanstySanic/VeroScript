# 멀티미디어 유지보수

이 공통 유지보수 가이드는 멀티미디어 모듈의 공급자 경계와 검증 절차를 관리합니다. 공개 동작과 API는 각 모듈의 User에, 구현 상세는 Developer에 둡니다.

공개 헤더에 타사 형식을 노출하지 않습니다. 공통 계약은 Media 모듈, 구체 라이브러리와 원문 고지는 백엔드가 소유합니다. 오류, 출력 보존, 소유권 계약을 유지하세요. 서비스 계약이나 Core 식별자가 바뀌면 공급자와 소비자를 함께 다시 빌드합니다.

실행이 허용될 때 모듈 CMake 진입점과 등록된 테스트를 사용합니다. 설치된 CMake 소비자, 선택적 바인딩/공급자 및 지원을 선언하는 각 플랫폼을 별도로 검증합니다. 정적 검사만으로 렌더링, 글꼴, 장치, 모바일 및 IDE 동작을 인증하지 않습니다. 임시 측정 기록은 User에 넣지 않습니다.

재배포하는 모든 종속성의 원문 라이선스와 고정 소스 목록을 보존합니다. 타사 법적 문서는 원래 언어로 유지합니다.

## 모듈 가이드

| 모듈 | 사용 | 유지보수 |
| --- | --- | --- |
| VeroScriptPlatform | [사용자](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptGraphics | [사용자](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptAudio | [사용자](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptImage | [사용자](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptFont | [사용자](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptGui | [사용자](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptSdl3 | [사용자](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptStb | [사용자](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptFontBackend | [사용자](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/ko-KR/index.md) |
| VeroScriptRmlUi | [사용자](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/ko-KR/index.md) | [개발자](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/ko-KR/index.md) |

GUI/foundation 집계 구성 요소 설치는 CMake 3.25+ `block(SCOPE_FOR VARIABLES ...)` 범위를 사용합니다. 중첩된 구성 요소 선택은 복원하고 `CMAKE_INSTALL_MANIFEST_FILES`, `CMAKE_ABSOLUTE_DESTINATION_FILES`는 전파하여 Gui/RmlUi/리소스 간 의존성 설치와 파일 목록을 보존합니다. 설치 SDK 소비자와 최종 배포 필터는 별도 검사입니다.

<!-- docs-navigation -->

---

[문서 유지보수](index.md)

[en-US](../en-US/multimedia.md) · [fr-FR](../fr-FR/multimedia.md) · [ja-JP](../ja-JP/multimedia.md) · [ko-KR](multimedia.md) · [zh-CN](../zh-CN/multimedia.md) · [zh-TW](../zh-TW/multimedia.md)
