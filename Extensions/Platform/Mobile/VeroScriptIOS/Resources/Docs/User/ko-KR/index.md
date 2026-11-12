# iOS — 실험적 모바일 임베딩

실험적 정적 XCFramework는 iOS 15+, 기기 arm64 및 시뮬레이터 arm64/x86_64가 대상입니다. `VeroScriptIOS`를 import하고 Foundation/libc++를 링크하며 동적 framework로 포함하지 않습니다. 번들에 `assets/stdlib` 계층을 보존하고 URL을 확인합니다. `VSMConfiguration`은 설정과 `sourceResources`를 복사하므로 갱신 시 engine을 다시 만듭니다. import와 일치하는 절대 라벨 및 앱 소유 쓰기 가능한 캐시를 사용하고 읽기 전용 번들은 피합니다. 작업자 큐에서 실행하고 ARC가 execute/cancel 동안 수명을 유지해야 합니다. 매 호출은 새 상태이며 취소 후 완료를 기다려 해제합니다. `outputUTF8`/`diagnosticsUTF8`는 NUL을 유지하는 `NSData`이며 잘릴 때 문자 중간에서 끝날 수 있습니다. 이 SDK는 실험 단계입니다. 배포 전에 대상 시뮬레이터와 실제 장치에서 앱 통합을 검증하세요.

[실험적 모바일 임베딩](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md)

## 모바일 언어 명시 설정

[모바일 언어 명시 설정](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/ko-KR/index.md)

## 관리 바이트 저장소 한도

`VSMConfiguration.maxBufferBytes`는 관리되는 단일 바이트 버퍼를, `VSMConfiguration.maxRetainedBytes`는 유지되는 기반 저장소를 제한합니다. 기본값은 각각 16 MiB와 32 MiB이며, 0은 해당 제한을 해제합니다. `VSMEngine`은 구성을 복사하므로 생성 전에 설정하세요.

## 실행 제어를 적용한 네이티브 실행과 정적 AOT

[실험적 모바일 임베딩 — 실행 제어를 적용한 네이티브 실행과 정적 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ko-KR/index.md#mobile-jit-selection)을 참조하세요.

[실험적 모바일 임베딩 — 실행 제어를 적용한 네이티브 실행과 정적 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/ko-KR/index.md#mobile-core-aot)을 참조하세요.

<a id="ios-native-selection"></a>

iOS에서는 `VSMAotProgram.programWithFactory:error:`로 프로그램을 만들고 `VSMConfiguration.aotPrograms` / `aotProgramId`를 구성한 뒤 `executePackage`로 생성 패키지를 실행합니다. 카탈로그는 프로그램을 보유하며 진입 코드는 정적으로 링크된 상태여야 합니다. ID에는 함수 주소를 넣지 않습니다. `VSMAotProgramFactory`의 출력 포인터 자체는 null이 아니어야 하지만 프로그램/오류 포인터 값은 null일 수 있습니다. 팩터리는 `vs_interop_status`를 반환하며 Objective-C 메서드는 null 허용 프로그램을 반환하며 선택적 오류 매개변수로 NSError를 제공할 수 있습니다. 사용 전에 반환된 프로그램을 확인하세요. nil이면 오류 출력 매개변수를 지정한 경우 NSError를 확인하세요.

## Bundle 리소스와 실행

<a id="ios-application-resources"></a>

정적 `VeroScriptIOS.xcframework`를 Foundation/libc++와 링크합니다. 모듈은 C ABI 소비자를 위해 `VeroScriptInterop.h`도 제공합니다. 표준 라이브러리의 디렉터리 구조를 보존해 복사하세요. 예제의 `stdlibURL`은 배포한 디렉터리의 확인된 URL이고 `cacheURL`은 앱 소유의 기존 쓰기 가능한 디렉터리입니다. 호스트 작업 큐에서 호출합니다. 엔진은 `sourceResources`를 포함한 설정을 복사하며 가상 절대 라벨을 import와 일치시켜야 합니다. 리소스 변경 후 엔진을 다시 만드세요.

```objc
@import VeroScriptIOS;

// Call on a host worker queue after validating both URLs.
static VSMExecutionResult* RunExample(NSURL* stdlibURL, NSURL* cacheURL, NSError** error)
{
	VSMConfiguration* config = [VSMConfiguration new];
	config.stdlibDirectory = stdlibURL.path;
	config.cacheDirectory = cacheURL.path;
	VSMEngine* engine = [[VSMEngine alloc] initWithConfiguration:config error:error];
	if (!engine)
	{
		return nil;
	}
	NSData* source = [@"void main(){print(42);}" dataUsingEncoding:NSUTF8StringEncoding];
	return [engine executeSource:source fileLabel:@"/veroscript/scripts/main.vs" error:error];
}
```

이 소스의 성공 결과는 `success == YES`, 종료 코드 0, `outputUTF8` 바이트 `0x34 0x32 0x0A`입니다. nil 결과는 API 실패이므로 요청한 NSError를 확인하세요. nil이 아니면서 `success == NO`이면 스크립트 진단/오류를 확인합니다. NUL이나 잘린 UTF-8 끝부분을 보존해야 하면 NSData를 유지하세요. 이 예제는 예상 동작을 설명하며 앱 검증을 대신하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
