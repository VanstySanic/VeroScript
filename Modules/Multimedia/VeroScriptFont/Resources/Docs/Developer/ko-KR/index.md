# 유지보수: VeroScriptFont

## 소유 범위와 검증

이 모듈은 글꼴, 셰이핑 및 텍스트 레이아웃 계약을 관리합니다. 공개 인터페이스는 [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h)에, 모듈별 입력은 [`Resources/Tests`](../../../Tests)에 있습니다.

공통 공급자 경계, 재빌드 요구 사항, 검증 및 법적 리소스 관리는 [멀티미디어 유지보수](../../../../../../../Resources/Docs/Developer/ko-KR/multimedia.md)에서 설명합니다.


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ko-KR/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)

<a id="font-layout-maintenance"></a>

불변 layout은 텍스트와 선택 서비스를 보관합니다. 동기화된 줄 분석 캐시는 최대 네 개의 불변 분석을 유지하며 스타일 조각은 교차하는 논리 run만 선택합니다. 검증된 layout이 정확히 같은 버퍼와 길이를 소유할 때만 UTF-8 검사를 생략합니다. fallback의 클러스터 범위 검사와 글리프 순서를 유지하세요. `VeroScriptFontContractTests`와 `VeroScriptFontLayoutContractTests`는 구체 제공자 없는 mock을 사용하며 corpus/스크립트 검사는 FontBackend 소유입니다. 바인딩은 Core 비공개 테스트 헤더에 의존하지 않고 VM을 명시적으로 선택합니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
