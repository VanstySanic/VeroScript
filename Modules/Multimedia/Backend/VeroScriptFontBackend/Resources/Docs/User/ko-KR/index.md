# FontBackend

## 서비스 사용

`FontBackend::CreateFontService()`는 `font/font-backend`를 제공합니다. FreeType은 로드, 메트릭, A8/컬러 glyph를, HarfBuzz는 문맥 형태, 합자, 문자 체계, 언어와 방향을 처리합니다. 각 face는 입력 바이트를 소유하고 변경을 동기화하며 공개 인터페이스에 타사 형식을 노출하지 않습니다.

네이티브 팩터리 또는 호환 호스트에 등록된 Font 스크립트 API에서 `font-backend`를 선택합니다. 유지한 face는 공급자 모듈도 보존합니다. Font/FontBackend와 필요한 공급자/Font Bindings를 설치하세요. 기본 글꼴과 라이선스는 Font가 소유합니다. 문단 bidi 분석은 `FTextLayout`이 담당하며 실제 문자 지원은 글꼴에도 달려 있습니다.

## SDK 및 통합

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

네이티브 SDK는 Core 없이 사용할 수 있습니다. 선택적 스크립트 바인딩과 네이티브 공급자는 실제 Core 빌드, 공개 ABI, 도구 체인과 일치해야 합니다. 제품 버전만 같아서는 충분하지 않습니다. 필요한 네이티브 종속성과 Bindings 구성 요소도 함께 설치합니다.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ko-KR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ko-KR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ko-KR/index.md)

FontBackend는 FreeType, HarfBuzz, SheenBidi와 문단 분석 및 원문 고지를 소유합니다. CreateFontService()와 CreateTextLayoutService()는 별도 서비스를 제공하고 선택적 네이티브 공급자는 둘 다 font-backend로 등록합니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
