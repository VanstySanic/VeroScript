# 유지보수: VeroScriptFontBackend

## 소유 범위와 검증

이 모듈은 글꼴 셰이핑, 래스터화 및 양방향 텍스트 분석 공급자을 관리합니다. 공개 인터페이스는 [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h)에, 모듈별 입력은 [`Resources/Tests`](../../../Tests)에 있습니다.

공통 공급자 경계, 재빌드 요구 사항, 검증 및 법적 리소스 관리는 [멀티미디어 유지보수](../../../../../../../../Resources/Docs/Developer/ko-KR/multimedia.md)에서 설명합니다.


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ko-KR/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)

<a id="native-provider-maintenance"></a>

선택적 `VeroScriptFontBackendModule`은 글꼴과 레이아웃 서비스를 각각 등록합니다. 정적 호스트는 `veroscript_font_backend_query`, 동적 확장 호스트는 `veroscript_extension_query`를 사용합니다. 형 변환 전에 각 서비스를 Font의 해당 계약과 대조하세요. 보관한 face와 분석은 원래 registry handle과 독립적으로 제공자 lease를 유지해야 합니다. FreeType/HarfBuzz/SheenBidi 입력, 고지와 제공자 빌드는 여기서 관리하며 공개 계약, 기본 글꼴과 독립 mock 검사는 Font가 소유합니다.

## 공급자 빌드와 수명

`VEROSCRIPT_BUILD_FONT_BACKEND`로 이 모듈을 선택합니다. `VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION`은 일치하는 Core용 공급자를 빌드하며 `VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER`는 이름 있는 정적 진입점을 선택합니다. 일반 인터페이스는 Font가 소유하며 구체적인 백엔드를 탐색하지 않습니다. FreeType/HarfBuzz 글꼴 객체, SheenBidi 분석, 고정된 의존 소스 및 법적 고지는 이 모듈에서 관리합니다. 백엔드 ID가 같아도 font와 text-layout 서비스 설명자는 독립적입니다.

스크립트 회귀 검증은 일치하는 `VeroScriptBytecodeVm`을 명시적으로 연결하고 공개 팩터리를 사용합니다. 레지스트리 핸들을 해제한 뒤 글꼴 객체, 분석 객체 및 레이아웃 스냅샷을 각각 유지해 모듈 lease를 확인합니다. 실행이 허용되면 네이티브 단독·Core 연동 빌드, 의존성 격리 및 이동한 SDK 소비자를 검증하고 실제 실패와 미검증 대상을 기록합니다.

[글꼴·레이아웃 공개 계약](../../../../../../VeroScriptFont/Resources/Docs/User/ko-KR/index.md) · [GUI 소비자 유지보수](../../../../../../VeroScriptGui/Resources/Docs/Developer/ko-KR/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
