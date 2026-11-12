# C# SDK

C# 源码生成、bundle 和 NativeAOT 边界请参阅 [C# AOT](aot.md)。

`VeroScriptCSharp` 是面向 .NET 的 NativeAOT 友好适配器，基于稳定的 VeroScript Interop C ABI 提供托管会话、值、回调、宿主绑定、包、反射、本地化和 JIT 统计。它不会复制编译器或虚拟机实现。

托管包面向 .NET 8 和 .NET 9。原生 `VeroScriptInterop` 是单独部署的受信任输入，必须与进程架构匹配。VeroScript 源代码和包不依赖 C# 或 .NET。

当前 Interop ABI 为 `0x00000101`（ABI 0.1.1）。托管适配器要求 ABI 版本完全一致。创建会话前可检查 `VeroScriptRuntime.AbiVersion`；托管包与原生 Interop 应来自匹配的发布版本。

## 最小宿主

```CSharp
using System;
using VeroScript;

using var session = new VeroScriptSession(new()
{
	Print = Console.WriteLine,
	Diagnostic = Console.Error.WriteLine,
});
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
});

var loaded = session.LoadSource("""
    int Add(int left, int right)
    {
        return left + right;
    }

    int main()
    {
        print(Add(20, 22));
        return 0;
    }
    """, "main.vs");
if (!loaded.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "load failed");
}

var run = session.Run();
if (!run.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "execution failed");
}
```

`VeroScriptSessionOptions.Print` 接收脚本输出行，`VeroScriptSessionOptions.Diagnostic` 接收诊断行。两个回调均可省略，其托管引用会保留至会话释放。

## 页面索引

- [会话与运行时](session.md)：部署、配置、执行、诊断、JIT 策略和线程规则。
- [值与所有权](values.md)：标量、容器、回调借用视图和保留调用对象。
- [宿主绑定](host-bindings.md)：原生类型、函数、类、泛型、拦截和变更回调。
- [包、反射与本地化](packages.md)：包传输、信任边界、元数据、序列化和目录。
- [C# AOT](aot.md)：生成的 C# 源码、确定性 bundle、NativeAOT 部署和不支持的语法。
- [C# 前端](frontend.md)：Roslyn 语义声明分析和类型映射。
- [C# BindGen](bindgen.md)：Binding Catalog、注册代码、API reference 和 AOT bridge。
- [C# 转换器](transpiler.md)：将受支持的 C# 子集进行语义转换并生成 VeroScript 源码。

VeroScript 语法应查阅语言参考。本模块只说明托管 C# 边界及 Interop ABI 规定的生命周期。

- [Android 与 iOS / MAUI](mobile.md)

## 受管理内存

运行库须提供 `VeroScriptCapabilities.Memory`；旧版兼容运行库会在原生调用或固定内存前抛出 `NotSupportedException`。回调内使用 `context.Memory`，会话空闲时使用 `session.Memory`。自定义 `Memory<byte>` 管理器必须支持 `Pin`，并允许在最后释放引用的线程执行 `Unpin`；注册失败时固定内存会被释放。

回调内存操作和 `GetArgumentAddress` 必须在该回调的托管线程执行；跨线程访问会报告 `WrongThread`。租约使用完毕后可以在其他线程释放。

宿主接口：`session.Memory` / `SetMemoryLimits`。所有权、配额、视图失效与租借规则参阅 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)。

- [宿主中的受管理内存](memory.md)

## 源码工具的诊断语言

C# 前端及 C# BindGen/Transpiler 自身的帮助与诊断目前使用英文，Roslyn 诊断按固定区域性读取。BindGen/Transpiler CLI 不接受 `--lang`；`--language-version` 选择 C# 语法版本，不是显示语言。通过 `VeroScriptRuntime.CurrentLanguage` 选择的原生运行时诊断语言是另一项独立设置。

## variant

参阅[Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#exported-value-lifetime)。

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

使用 `VeroScriptSession.LoadApplication(absolutePath, optionsJson)` 加载应用，检查结果后再调用 `Run()`。宿主选项中的路径必须为绝对路径。环境、签名及信任配置遵循 [Application 加载约定](../../../../../../../VeroScriptApplication/Resources/Docs/User/zh-CN/index.md)。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
