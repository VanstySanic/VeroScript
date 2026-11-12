# iOS — 實驗性行動嵌入

實驗性靜態 XCFramework 面向 iOS 15+、真機 arm64 和模擬器 arm64/x86_64。匯入 `VeroScriptIOS` 並連結 Foundation/libc++，不要按動態 framework 嵌入。保持 App Bundle 中 `assets/stdlib` 層級並檢查資源 URL。`VSMConfiguration` 複製組態和 `sourceResources`，更新時重建 engine。檔案標籤須絕對且與匯入一致；快取用應用程式擁有的可寫目錄，不能用唯讀 Bundle。在工作佇列執行，ARC 保持 execute/cancel 期間物件存活。每次呼叫建立新狀態；取消後等待結束再釋放。`outputUTF8`／`diagnosticsUTF8` 為保留 NUL 的 `NSData`，截斷可止於字元中間。此 SDK 保持實驗性狀態；部署前，請在目標模擬器和實機上驗證應用程式整合。

[實驗性行動嵌入](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md)

## 行動本地化的明確設定

[行動本地化的明確設定](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md)

## 受管理位元組儲存限額

`VSMConfiguration.maxBufferBytes` 限制單一受管理位元組緩衝區，`VSMConfiguration.maxRetainedBytes` 限制保留的底層儲存空間。預設分別為 16 MiB 和 32 MiB；0 關閉對應限制。請在建立 `VSMEngine` 前設定，引擎會複製組態。

## 受控原生執行與靜態 AOT

請參閱[實驗性行動嵌入 — 受控原生執行與靜態 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-TW/index.md#mobile-jit-selection)。

請參閱[實驗性行動嵌入 — 受控原生執行與靜態 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-TW/index.md#mobile-core-aot)。

<a id="ios-native-selection"></a>

iOS 使用 `VSMAotProgram.programWithFactory:error:` 建立程式，設定 `VSMConfiguration.aotPrograms`／`aotProgramId`，透過 `executePackage` 執行產生套件。目錄保留程式，入口程式碼須保持靜態連結，ID 不能攜帶函式位址。`VSMAotProgramFactory` 接收非空輸出指標，所指向的程式／錯誤指標值可以為空；工廠傳回 `vs_interop_status`；Objective-C 方法傳回可空程式，並可透過選用錯誤參數提供 NSError。使用前檢查傳回的程式；若為 nil，且提供了錯誤輸出參數，再檢查 NSError。

## Bundle 資源與執行

<a id="ios-application-resources"></a>

將靜態 `VeroScriptIOS.xcframework` 與 Foundation／libc++ 連結；模組也提供面向 C ABI 使用者的 `VeroScriptInterop.h`。複製標準庫時保留目錄層級。範例中 `stdlibURL` 是已檢查的實際部署目錄 URL，`cacheURL` 是已存在、可寫且由應用程式擁有的目錄。在宿主工作佇列呼叫。引擎複製包括 `sourceResources` 在內的設定，絕對虛擬標籤須與匯入一致；資源更新後重新建立引擎。

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

此原始碼成功時 `success == YES`、結束碼為 0，`outputUTF8` 位元組為 `0x34 0x32 0x0A`。傳回 nil 表示 API 失敗，應檢查所要求的 NSError；非 nil 且 `success == NO` 時檢查腳本診斷／錯誤資訊。需要保留 NUL 或截斷的 UTF-8 尾部時保持 NSData 形式。以上是預期行為，不代替應用程式驗收。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
