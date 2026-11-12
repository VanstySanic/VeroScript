# Stb

바이너리 PGM/PPM은 `Maxval`(1–65535)에 따라 RGBA8로 정규화합니다. 잘못된 최댓값이나 범위 밖의 샘플은 오류를 반환하고 이전 출력을 보존합니다. 최댓값이 255/65535인 입력은 기존 양자화와 바이트 순서 동작을 유지합니다.

## 서비스 사용

`Stb::CreateImageService()`는 `image/stb`를 제공합니다. PNG, JPEG, BMP, TGA 등 활성 stb_image 형식을 straight-alpha sRGB RGBA8로 변환하고 GIF는 첫 프레임을 반환합니다. HDR/실수 출력은 비활성입니다. top-down BMP, 16비트 PGM/PPM 바이트 순서와 CgBI PNG도 같은 계약으로 정규화합니다. 실패하면 출력을 보존하며 명시한 제한을 따릅니다.

호출 동안 입력을 변경하지 않고 출력/오류 객체를 분리하면 동시에 호출할 수 있습니다. 디코더 thread-local storage가 없는 빌드는 인스턴스 간 디코딩을 직렬화합니다. `VS::Image`는 Image 바인딩이 제공하고 선택적 `VeroScriptStbModule`은 공급자를 제공합니다. Image, Stb와 필요한 Bindings를 함께 설치하세요.

## SDK 및 통합

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

네이티브 SDK는 Core 없이 사용할 수 있습니다. 선택적 스크립트 바인딩과 네이티브 공급자는 실제 Core 빌드, 공개 ABI, 도구 체인과 일치해야 합니다. 제품 버전만 같아서는 충분하지 않습니다. 필요한 네이티브 종속성과 Bindings 구성 요소도 함께 설치합니다.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/ko-KR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/ko-KR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
