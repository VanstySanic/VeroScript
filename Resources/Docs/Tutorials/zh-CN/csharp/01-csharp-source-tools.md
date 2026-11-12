# 01：生成 C# 绑定构件

从同一组托管声明生成绑定目录、API 参考、实时注册源码与 AOT bridge。语义前端在构建时分析源码，不创建脚本会话。

准备可从 PATH 调用的匹配版 `veroscript-csharp-bindgen`、`veroscript`，可写的教程目录，以及与所选 SDK 兼容的 .NET 宿主项目。所有命令从该目录执行。生成构建输入、编译宿主及注册回调是独立步骤。

下方的 AOT 运行使用生成的 `net8.0` 项目；请安装能构建该目标的 .NET SDK，以及用于运行它的匹配 .NET 运行时。

## 第一步：准备输入

将宿主声明保存为 `HostApi.cs`：

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

将调用脚本保存为 `app.vs`：

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## 第二步：生成实时注册

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

预期得到 `bindings.json`、`Bindings.g.cs`、`Bindings.vs.txt`。把注册源码与托管 C# SDK、原始 `HostApi.cs` 一同编译，在加载脚本前调用生成的注册入口。目录只描述接口，不会安装运行时回调。配置完成的宿主运行该脚本时应打印 `42` 并返回零。

在配置好的 VM 宿主中，加载 `app.vs` 之前调用以下生成入口；`session` 是宿主现有的 `VeroScriptSession`：

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## 第三步：生成 AOT bridge

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

将 `HostApi.cs`、`Bindings.Aot.g.cs` 和全部生成应用单元放在同一个 .NET 项目编译。bridge 直接调用托管代码，不加载 `VeroScriptInterop`。保留生成项目的运行时依赖；仅生成源码还没有得到可执行文件。

生成的 `generated/app/App.csproj` 设置了 `EnableDefaultCompileItems=false`，只显式列出生成单元。保留现有项目项，将以下 `ItemGroup` 加到其 `Project` 元素内。路径以 `App.csproj` 所在目录为基准：

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

从教程工作目录运行托管应用。预期打印 `42`，退出码为零：

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

这个 AOT 项目使用 `Bindings.Aot.g.cs`；`Bindings.g.cs` 属于第二步的 VM 宿主。重新生成 bundle 会替换 `App.csproj`，生成后需要重新加入这些额外输入。

## 第四步：生成项目

创建 `Models/State.cs` 并写入以下声明，同时保留第一步的 `HostApi.cs`：

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

一次传入两个文件，将同一轮的目录、注册、bridge、参考与 manifest 一起保留。partial 和相关声明在输出前合并。同名文件主干使用稳定的路径哈希后缀区分。

如果在此项目生成步骤后重新构建 AOT 应用，先使用新合并目录重复第三步的 AOT 命令，再重新加入项目输入，包括 `<Compile Include="../../Models/State.cs" />`，然后构建。目录、bridge、宿主声明与生成应用必须来自同一组输入。

## 第五步：检查契约

对照目录中的名称和签名、宿主源码及生成的 API 参考。默认考虑公开声明；定义并使用 `[VeroScriptBinding]` 后，配合 `--require-attribute` 选择标记类型，使用 `[VeroScriptIgnore]` 排除声明。不支持的 nullable、指针或适配器签名会报告诊断；应提供明确包装，不要编辑生成元数据来隐藏不一致。

## 练习

1. 添加一个受支持的只读属性，检查生成契约。
2. 定义绑定属性并标记一个类型，比较 `--require-attribute` 的输出。
3. 说明实时注册与 AOT bridge 分别由什么宿主使用。
4. 添加不受支持的 nullable 签名，阅读诊断后恢复受支持接口。

[BindGen 参考](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/bindgen.md) · [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/aot.md) · [C# 源码转换](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/transpiler.md) · [运行时值与所有权](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/index.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/csharp/01-csharp-source-tools.md) · [fr-FR](../../fr-FR/csharp/01-csharp-source-tools.md) · [ja-JP](../../ja-JP/csharp/01-csharp-source-tools.md) · [ko-KR](../../ko-KR/csharp/01-csharp-source-tools.md) · [zh-CN](01-csharp-source-tools.md) · [zh-TW](../../zh-TW/csharp/01-csharp-source-tools.md)
