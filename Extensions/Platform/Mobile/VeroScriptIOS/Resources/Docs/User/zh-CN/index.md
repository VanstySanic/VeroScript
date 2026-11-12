# iOS — 实验性移动嵌入

实验性静态 XCFramework 面向 iOS 15+、真机 arm64 和模拟器 arm64/x86_64。导入 `VeroScriptIOS` 并链接 Foundation/libc++，不要按动态 framework 嵌入。保持 App Bundle 中 `assets/stdlib` 层级并检查资源 URL。`VSMConfiguration` 复制配置和 `sourceResources`，更新时重建 engine。文件标签须绝对且与导入一致；缓存用应用拥有的可写目录，不能用只读 Bundle。在工作队列执行，ARC 保持 execute/cancel 期间对象存活。每次调用创建新状态；取消后等待结束再释放。`outputUTF8`／`diagnosticsUTF8` 为保留 NUL 的 `NSData`，截断可止于字符中间。此 SDK 保持实验性状态；部署前，请在目标模拟器和真机上验证应用集成。

[实验性移动嵌入](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md)

## 移动本地化的显式配置

[移动本地化的显式配置](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/zh-CN/index.md)

## 受管理字节存储限额

`VSMConfiguration.maxBufferBytes` 限制单个受管理字节缓冲区，`VSMConfiguration.maxRetainedBytes` 限制保留的底层存储。默认分别为 16 MiB 和 32 MiB；0 关闭对应限制。请在创建 `VSMEngine` 前设置，引擎会复制配置。

## 受控原生执行与静态 AOT

参阅[实验性移动嵌入 — 受控原生执行与静态 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-CN/index.md#mobile-jit-selection)。

参阅[实验性移动嵌入 — 受控原生执行与静态 AOT](../../../../../VeroScriptMobile/Resources/Docs/User/zh-CN/index.md#mobile-core-aot)。

<a id="ios-native-selection"></a>

iOS 使用 `VSMAotProgram.programWithFactory:error:` 创建程序，配置 `VSMConfiguration.aotPrograms`／`aotProgramId`，通过 `executePackage` 执行生成包。目录保留程序，入口代码须保持静态链接，ID 不能携带函数地址。`VSMAotProgramFactory` 接收非空输出指针，所指向的程序／错误指针值可以为空；工厂返回 `vs_interop_status`；Objective-C 方法返回可空程序，并可通过可选错误参数提供 NSError。使用前检查返回的程序；若为 nil，且提供了错误输出参数，再检查 NSError。

## Bundle 资源与执行

<a id="ios-application-resources"></a>

将静态 `VeroScriptIOS.xcframework` 与 Foundation／libc++ 链接；模块也提供面向 C ABI 使用者的 `VeroScriptInterop.h`。复制标准库时保留目录层级。示例中 `stdlibURL` 是已检查的实际部署目录 URL，`cacheURL` 是已存在、可写且由应用拥有的目录。在宿主工作队列调用。引擎复制包括 `sourceResources` 在内的配置，绝对虚拟标签须与导入一致；资源更新后重新创建引擎。

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

此源码成功时 `success == YES`、退出码为 0，`outputUTF8` 字节为 `0x34 0x32 0x0A`。返回 nil 表示 API 失败，应检查所请求的 NSError；非 nil 且 `success == NO` 时检查脚本诊断／错误信息。需要保留 NUL 或截断的 UTF-8 尾部时保持 NSData 形式。以上是预期行为，不代替应用验收。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
