# iOS — Experimental mobile embedding

The experimental static XCFramework targets iOS 15+, device arm64 and simulator arm64/x86_64. Import `VeroScriptIOS` and link Foundation/libc++; do not embed it as a dynamic framework. Preserve the `assets/stdlib` hierarchy in the app bundle and check resource URLs. `VSMConfiguration` copies settings and `sourceResources`; recreate the engine for updates. Use absolute file labels matching imports and an app-owned writable cache, never the read-only bundle. Execute on a worker queue; ARC must keep the engine alive through execute/cancel. Each call creates fresh state; cancel and await completion before releasing it. `outputUTF8`/`diagnosticsUTF8` are `NSData`, preserve NUL, and may end mid-character when truncated. This SDK remains experimental. Verify your application’s integration on the intended simulators and physical devices before deployment.

[Experimental mobile embedding](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md)

## Explicit mobile localization

[Explicit mobile localization](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md)

## Managed byte-storage limits

`VSMConfiguration.maxBufferBytes` limits one managed byte buffer; `VSMConfiguration.maxRetainedBytes` limits retained backing storage. Their defaults are 16 MiB and 32 MiB respectively; zero disables the corresponding limit. Set them before creating `VSMEngine`; the engine copies its configuration.

## Controlled native execution and static AOT

See [Experimental mobile embedding — Controlled native execution and static AOT](../../../../../VeroScriptMobile/Resources/Docs/User/en-US/index.md#mobile-jit-selection).

See [Experimental mobile embedding — Controlled native execution and static AOT](../../../../../VeroScriptMobile/Resources/Docs/User/en-US/index.md#mobile-core-aot).

<a id="ios-native-selection"></a>

On iOS, create a program through `VSMAotProgram.programWithFactory:error:` and configure `VSMConfiguration.aotPrograms` / `aotProgramId`; execute the generated package with `executePackage`. Catalogs retain their programs; entry code must remain statically linked, and IDs never contain function addresses. `VSMAotProgramFactory` receives non-null output pointers whose program/error pointees may be null. The factory reports `vs_interop_status`; the Objective-C method returns a nullable program and can supply an NSError through the optional error parameter. Check the returned program before use; if it is nil, inspect the NSError when an error output was supplied.

## Bundle resources and execution

<a id="ios-application-resources"></a>

Link the static `VeroScriptIOS.xcframework` with Foundation/libc++; its module also exposes `VeroScriptInterop.h` for C ABI consumers. Copy the standard library without flattening its directories. In the example, `stdlibURL` is the checked URL of that deployed directory and `cacheURL` is an existing, writable, app-owned directory. Call on a host worker queue. The engine copies configuration, including `sourceResources`; absolute virtual labels must match imports. Recreate the engine when resources change.

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

For this source, a successful result has `success == YES`, exit code 0 and `outputUTF8` bytes `0x34 0x32 0x0A`. A nil result is an API failure: inspect the NSError when requested. A non-nil result with `success == NO` carries script diagnostics/error information. Keep output as NSData when preserving NUL or a truncated UTF-8 suffix matters. These examples describe expected behavior and do not replace application acceptance.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
