# VeroScriptGraphics

[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md#provider-boundary)을 참조하세요.

## 네이티브 계약 빌드

저장소 루트에서 다음 명령을 실행하면 스크립트 어댑터 없이 공개 계약을 빌드합니다. 스크립트를 실행하려면 공급자와 호환되는 Core에 해당 RegisterScriptBindings 어댑터를 등록하는 호스트도 필요합니다.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## 유지보수와 검증

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ko-KR/index.md)

[README](../../../../README.md) · [User](../../User/ko-KR/index.md)

## 선택적 네이티브 제출 범위

IRenderSubmission2D는 IRenderer2D에서 dynamic_cast로 조회하는 선택적 네이티브 인터페이스이며 기존 가상 함수 테이블이나 스크립트 API를 바꾸지 않습니다. SDL_GPU가 구현하고 SDL3 모듈도 전달합니다. BeginSubmission은 중첩을 거부하고 EndSubmission은 동일 렌더러의 연속 패스를 제출합니다. CancelSubmission은 반복 호출할 수 있으며 실패나 예외에도 정리에 사용합니다. 소유 스레드에서 호출하고 범위가 끝날 때까지 리소스를 열어 둡니다. 다른 렌더러가 출력을 사용하기 전에 범위를 끝내야 합니다. 범위 안에서 RenderSurface의 bPresented는 이미지 획득을 뜻하며 EndSubmission 성공은 제출만 확인합니다. GPU 완료나 화면 표시는 보장하지 않습니다. 스왑체인 이미지 획득 후 취소하더라도 제공자의 필수 제출 규칙을 따라야 합니다.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
