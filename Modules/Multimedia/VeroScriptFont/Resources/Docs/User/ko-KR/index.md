# Font

## 서비스 사용

`Font::IService`의 face는 글꼴 바이트를 소유하고 `IFace`는 메타데이터, 메트릭, 셰이핑과 래스터화를 제공합니다. 위치는 부호 있는 26.6픽셀, cluster는 원본 UTF-8 바이트 오프셋입니다. `FShapeOptions` 기본값은 16픽셀, 자동 방향, 커닝 사용, 전체 범위입니다. 비트맵은 A8 또는 premultiplied sRGB RGBA8로 Image 출력과 다릅니다. 계약: `font` / `veroscript.font` / `0.1.2`.

FTextLayout::Create(service,text,direction,error)의 첫 인수로 ITextLayoutService를 명시합니다. Font는 일반 인터페이스를 소유하고 별도 FontBackend가 SheenBidi 3.0.0 / Unicode 17.0.0의 CreateTextLayoutService()를 제공합니다. 계약은 text-layout / veroscript.text-layout / 0.1.0입니다. 캐시에는 백엔드 ID뿐 아니라 서비스 인스턴스 식별자도 넣습니다.

`FTextLayout::Create(service, text, direction, error)`는 명시적으로 선택한 레이아웃 서비스로 불변 논리 텍스트를 분석합니다. `GetVisualRuns`는 행별 bidi 순서를 구하며 한 문단 안의 UTF-8 경계만 허용합니다. `ShapeLine`은 주변 문맥과 시각 cluster/캐럿 위치를 유지합니다. 줄바꿈, atlas, GPU는 호출자 책임입니다. `ShapeWithFallback`은 선택한 face를 보존합니다. 기본 Lato Latin은 `Vero Sans`로 등록되며 다른 문자는 적절한 글꼴을 제공해야 합니다. 등록된 스크립트는 `VS::Font::Service::Create("font-backend").LoadDefault()`를 쓰며 위치 단위는 픽셀입니다.

## SDK 및 통합

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

네이티브 SDK는 Core 없이 사용할 수 있습니다. 선택적 스크립트 바인딩과 네이티브 공급자는 실제 Core 빌드, 공개 ABI, 도구 체인과 일치해야 합니다. 제품 버전만 같아서는 충분하지 않습니다. 필요한 네이티브 종속성과 Bindings 구성 요소도 함께 설치합니다.

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/ko-KR/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/ko-KR/index.md)

ShapeLine은 보이는 각 클러스터의 마지막 글리프에서 자간을 한 번 적용하며 반환 글리프 메트릭의 원래 이동량은 바꾸지 않습니다. 캐럿 X는 논리적 펜 끝 위치이며 시각적 경계 상자의 가장자리가 아닙니다. 음수 자간은 끝 위치를 클러스터의 시각적 영역 안에 놓을 수 있습니다.

<a id="font-layout-boundaries"></a>

자동 문단 방향은 첫 강한 문자를 사용하고 명시적 좌/우 방향은 기준 방향을 고정합니다. 줄바꿈 뒤 시각 run은 UAX #9 L1/L2를 따릅니다. `FTextLine`은 시각 순서 글리프, 논리 바이트 클러스터 범위와 캐럿을 반환하며 방향 경계의 두 위치는 affinity로 고릅니다. 보이지 않는 형식 제어만 있는 클러스터는 글리프나 간격 없이 논리 캐럿 끝점을 유지합니다. 클립보드 텍스트와 오프셋은 논리 순서를 유지하세요. 합자 내부의 정밀 캐럿, 완전한 자소 탐색, 줄바꿈과 정렬은 호스트 책임입니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
