# RmlUi

## 서비스 사용

`RmlUi::CreateBackend`는 GUI 트리를 Graphics 렌더러에 연결합니다. 네이티브 호스트는 글꼴 및 텍스트 레이아웃 서비스를 제공하며 사용자 이미지 디코더가 없으면 Image도 제공합니다. 렌더링은 장치와 정확히 하나의 surface/texture 대상을 빌리며 명시적 layout-only 모드에는 렌더 대상이 필요 없습니다. 동기화와 업데이트 성공 후 렌더링합니다. GUI 서비스는 `gui` / `veroscript.gui`이며 인터페이스 버전은 Gui 릴리스 버전을 따르고 현재는 `0.1.1`입니다.

형식화된 레이아웃, 텍스트, 기본 컨트롤, 입력과 보고된 Graphics 합성을 지원합니다. ML/CSS는 GUI frontend 범위이며 임의 브라우저 CSS나 native RML이 아닙니다. 소유 스레드에서 닫고 빌린 리소스를 닫기 전에 지연 정리를 수거합니다. 정리 실패 시 재시도를 위해 리소스를 유지합니다. 기능과 제한은 네이티브 공급자에 달려 있으며 에디터 플러그인이 설치해 주지 않습니다.

## SDK 및 통합

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ko-KR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ko-KR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ko-KR/index.md)

<a id="composition-resources"></a>

## 합성 및 리소스 제한

RmlUi는 중첩/역 마스크, 레이어 캡처, 불투명도, 밝기, 대비, 반전, 회색조, 채도, 세피아, 색조 회전, 흐림 및 그림자를 지원합니다. 마스크와 필터에는 Graphics 합성이 필요하며 지원하지 않는 연산은 오류를 냅니다. 흐림 sigma는 최대 64픽셀, 그림자는 64/3픽셀입니다. 그라데이션은 기하마다 한 번 래스터화해 텍스처에 캐시하며 각 변 8192, 총 16,777,216픽셀로 제한합니다.

로드한 리소스 ID는 불변입니다. 누락되거나 잘못된 이미지는 업데이트/렌더링 오류를 냅니다. 실패한 데이터를 교체한 뒤에는 새 ID를 쓰거나 컨텍스트를 다시 만듭니다. 이미지 입력은 64 MiB, 디코딩한 이미지는 각 변 16,384와 총 16,777,216픽셀, 보관한 사용자 글꼴 데이터는 합계 128 MiB로 제한합니다. 표준 `ReadGuiResourceFile`도 파일마다 64 MiB로 제한합니다. 사용자 판독기를 써도 이미지와 글꼴 총량 제한은 유지됩니다. 텍스처를 사용하기 전에 판독기를 설정하세요.

<a id="font-atlas-and-services"></a>

글꼴 atlas는 1024 × 1024 픽셀이며 글리프/효과는 배치 여백을 포함해 들어가야 합니다. 실패하면 `rmlui.font_atlas_failed`를 보고하고 재생성 상태를 유지하므로 재시도도 텍스트를 조용히 누락하지 않고 실패를 보고합니다. 살아 있는 RmlUi context는 글꼴 레지스트리를 공유하며 동일한 글꼴/레이아웃 서비스 인스턴스를 써야 합니다. backend ID만 같아서는 충분하지 않습니다. 모든 context를 닫은 뒤 다른 인스턴스를 선택할 수 있습니다. `RmlUi::FOptions` 또는 GUI backend 옵션으로 제공하고 context 생존 중 교체하지 마세요.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
