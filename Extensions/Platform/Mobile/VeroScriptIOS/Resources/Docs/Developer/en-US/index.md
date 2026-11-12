# iOS — Developer documentation

iOS owns the Objective-C/Objective-C++ adapter and static XCFramework integration. Keep UI dispatch and application lifecycle in the adapter/host; preserve device/simulator identity in every slice.

Shared embedded build, lifetime, package-output and localization responsibilities are maintained in [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/en-US/index.md).

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS packaging requires macOS/Xcode, three slice builds and `LC_BUILD_VERSION`/CPU/minimum-iOS checks for every archive object. Reject macOS/mixed-platform archives. Actual Objective-C/Swift app linkage, signing, background/resume, cancellation and resource measurements remain release gates.

[User documentation](../../User/en-US/index.md)


## Mobile resources and localization

The shared mobile catalog inventory, deployment paths and explicit loading procedure are maintained in [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/en-US/index.md#mobile-localization).

Translation buffer ownership, errors and export maintenance are specified in [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md#localization-abi).

<a id="ios-acceptance-evidence"></a>

Maintain evidence per exact source/artifact identity, Xcode and SDK version, deployment target, runtime OS and device/simulator architecture. Build device arm64 and simulator arm64/x86_64 separately with CMAKE_SYSTEM_NAME=iOS and the matching iphoneos/iphonesimulator sysroot; embedded builds use static Interop. Validate the merged archives and XCFramework before consuming them from Objective-C++, Swift, C ABI and static-AOT applications. Check imports, NSError/factory bridging, bundle resources, writable cache reuse, capability denial, cancellation, small-stack parsing/recovery, ARC lifetime and background/resume in a real application. A compiled device slice is not physical-device execution; simulator signing and screenshots do not qualify device signing, native Metal or performance. Minimum iOS 15.0 compilation does not establish runtime validation on iOS 15. Keep actual receipts and source differences in temporary evidence, not dated status paragraphs in the manuals; existing simulator receipts never approve a later build automatically.

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
