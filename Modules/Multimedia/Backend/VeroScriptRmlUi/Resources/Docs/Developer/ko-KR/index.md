# 유지보수: VeroScriptRmlUi

## 소유 범위와 검증

이 모듈은 GUI 렌더링, 레이아웃, 입력 어댑터와 내장 의존성을 관리합니다. 공개 인터페이스는 [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h)에, 모듈별 입력은 [`Resources/Tests`](../../../Tests)에 있습니다.

공통 공급자 경계, 재빌드 요구 사항, 검증 및 법적 리소스 관리는 [멀티미디어 유지보수](../../../../../../../../Resources/Docs/Developer/ko-KR/multimedia.md)에서 설명합니다.


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/ko-KR/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/ko-KR/GENERAL.md)

텍스트 측정은 내용 기반의 제한된 캐시를 사용합니다. 너비 키에는 문단과 행 범위, 방향, 언어, 자간, 커닝 및 대체 글꼴 개정이 포함되며 문단 분석은 실제 텍스트 레이아웃 서비스도 구분합니다. 같은 불변 텍스트의 소유자를 다시 만들어도 분석을 재사용합니다. 글꼴 면과 크기별 너비 캐시는 최대 1,024개 / 512 KiB, 문단 분석은 2,048개 / 텍스트 8 MiB입니다. 전체 글리프, 캐럿 및 선택 영역은 기존 셰이핑 경로를 유지합니다.

중첩된 자동 높이 세로 컨테이너는 블록 흐름과 내부 간격 요소를 사용하여 중첩 Flex의 반복적인 고유 크기 측정을 줄입니다. Hidden은 공간을 유지하며 Collapsed와 부동 요소는 흐름에서 제외됩니다. 가로, Grid, Wrap, Splitter 및 편집 영역은 전용 경로를 유지합니다. 두 색의 아핀 선형 그라데이션은 정점 색을 사용할 수 있으며 나머지는 래스터화합니다. 격리된 클립 마스크는 제한된 임시 대상을 사용하고 예산이 부족하면 일반 합성기로 돌아갑니다. 중첩, 부드러운 경계 및 반전 마스크는 미리 곱한 source-over 의미를 유지합니다.

측정 중에는 LRU 텍스트 예산보다 큰 단일 문단도 포함하여 가장 최근 문단 분석을 유지합니다. 다음 문단 분석으로 교체하며 글꼴 리소스를 해제할 때도 제거합니다.

<a id="native-provider-maintenance"></a>

선택적 `VeroScriptRmlUiModule`은 네이티브 GUI 제공자이며 Core/Gui 빌드와 일치해야 합니다. 로컬 패치 RmlUi 헤더, 라이브러리와 패치/고지 목록을 동기화하고 시스템 RmlUi와 어댑터 헤더를 혼용하지 마세요. 네이티브 SDK 데이터 경로는 선택적인 Core 바인딩 제공자를 요구하지 않습니다. Font, 레이아웃, Image, Graphics는 선택 의존이며 서비스 인스턴스 수명과 atlas 실패는 공개 RmlUi 안내를 따릅니다. 리소스 캐시와 동기화 변경에서도 이 경계를 유지하세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
