# VeroScriptPlatform

[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md#provider-boundary)을 참조하세요.

## 네이티브 계약 빌드

저장소 루트에서 다음 명령을 실행하면 스크립트 어댑터 없이 공개 계약을 빌드합니다. 스크립트를 실행하려면 공급자와 호환되는 Core에 해당 RegisterScriptBindings 어댑터를 등록하는 호스트도 필요합니다.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## 유지보수와 검증

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md)

[README](../../../../README.md) · [User](../../User/ko-KR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
