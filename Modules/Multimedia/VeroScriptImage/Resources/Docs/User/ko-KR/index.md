# Image

## 서비스 사용

`Image::IService::Decode`는 조밀한 straight-alpha sRGB RGBA8을 반환하며 실패 시 이전 결과를 보존합니다. `Image::Validate`는 크기와 버퍼를 검사합니다. `FDecodeOptions` 기본 제한은 각 변 16,384, 16,777,216픽셀, 입력 64 MiB입니다. 리소스 조회, 캐시, GPU 업로드는 호출자가 담당합니다. 계약: `image` / `veroscript.image` / `0.1.0`.

`stb` 같은 공급자가 디코더를 제공합니다. Image 바인딩 등록 후 `VS::Image::Service::Create("stb").Load("image.png")` 또는 `Decode(bytes)`를 사용합니다. 스크립트 크기와 인덱스는 `int64`이며 큰 문자열을 복사하기 전에도 기본 제한을 적용합니다. 주입한 reader가 리소스를 해석하고, 없으면 파일 경로는 프로세스 작업 디렉터리를 기준으로 합니다.

## SDK 및 통합

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

네이티브 SDK는 Core 없이 사용할 수 있습니다. 선택적 스크립트 바인딩과 네이티브 공급자는 실제 Core 빌드, 공개 ABI, 도구 체인과 일치해야 합니다. 제품 버전만 같아서는 충분하지 않습니다. 필요한 네이티브 종속성과 Bindings 구성 요소도 함께 설치합니다.

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/ko-KR/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/ko-KR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
