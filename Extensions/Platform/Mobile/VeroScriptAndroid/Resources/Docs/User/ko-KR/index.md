# Android — 실험적 모바일 임베딩

실험적 AAR은 `arm64-v8a`, `x86_64`, API 26+를 대상으로 VM과 NDK `c++_shared`를 사용합니다. 호환 네이티브 의존성과 함께 앱에 추가합니다. `assets/veroscript/stdlib`, C ABI Prefab 헤더와 라이선스가 포함되며 앱 스크립트는 `src/main/assets/scripts`에 둡니다. `scripts/helper.vs`는 `/veroscript/scripts/helper.vs`로 매핑합니다. 생성/실행/닫기는 작업자에서 수행하고 `requestCancel()`용 engine 참조를 유지합니다. `executeAsset` 또는 `execute(source, absoluteLabel)`을 사용합니다. 매 호출은 새 상태이고 동시 실행은 거부합니다. `close()`는 기다리며 반복할 수 있지만 자체 콜백에서 동기 호출하지 않습니다. UTF-8 바이트 배열은 NUL과 보조 문자를 유지하므로 `outputTruncated`를 확인하세요.

SDK의 [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) 형식을 가져옵니다. 다음 메서드 조각은 작업 스레드에서 실행하며 Android `context`를 사용할 수 있다고 가정합니다.

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[실험적 모바일 임베딩](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md)

## 모바일 언어 명시 설정

[모바일 언어 명시 설정](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md)

## 관리 바이트 저장소 한도

Android는 전체 `VeroScriptEngine` 생성자의 마지막 `maxBufferBytes` / `maxRetainedBytes` 인수로 설정합니다. Java `long`은 uint64 비트 패턴을 전달하며 일반 예산에는 양수 바이트 수를 사용합니다. 기본값은 16 MiB / 32 MiB이고 0은 무제한입니다.

## 실행 제어를 적용한 네이티브 실행과 정적 AOT

[실험적 모바일 임베딩 — 실행 제어를 적용한 네이티브 실행과 정적 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ko-KR/index.md#mobile-jit-selection)을 참조하세요.

<a id="android-native-selection"></a>

Android는 `ExecutionBackend`, `jitBackendPath`, 애플리케이션이 만든 `AotPrograms` 카탈로그와 `aotProgramId`를 받습니다. 생성한 애플리케이션은 `executePackage`, 네이티브 초기화는 `VeroScriptAndroid.h`를 사용합니다. 카탈로그는 프로그램을 보유하며 생성 진입 코드는 정적으로 링크된 상태여야 합니다. ID에 함수 주소를 넣지 마세요.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
