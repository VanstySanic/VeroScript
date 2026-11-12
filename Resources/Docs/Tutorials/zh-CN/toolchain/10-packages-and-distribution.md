# 10：构建包并分发应用

当脚本从“本地文件”变成“可交付程序”，可以把它构建为应用包或库包：`.vsapp` 适合直接运行，`.vslib` 适合被其他脚本导入。

## 第一步：创建库模块

创建 `package_library.vs`：

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

只有 `export` 的声明会成为库 API。先构建库包：

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## 第二步：创建应用入口

创建 `package_app.vs`：

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

开发时编译应用需要让工具找到库包：

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

生成的应用包包含入口及其依赖，运行时不必把源库包放在旁边。`package_app.vs` 默认从同目录源文件导入库；使用 `-D VERO_USE_BINARY_PACKAGE` 后才会切换为导入已生成的 `DemoLibrary.vslib`，因此库包必须在编译应用时可见。

## 第三步：检查公开边界

如果你在库中写了没有 `export` 的函数，它仍然可以在库自己的源文件中使用，但下游脚本不能调用。把包 API 当作长期契约，发布前检查：

1. 是否只导出了真正需要的类型和函数。
2. 参数、返回类型和枚举值是否稳定。
3. 包版本是否与目标运行时和绑定兼容。
4. 是否已经运行过 `package inspect`。

分发的示例中有一套完整流程：[`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md)、[`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs) 和 [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs)。

## 练习

1. 为库增加一个 `subtract` 函数。
2. 删除 `export`，观察应用编译时的错误。
3. 为包设置版本号，并在发布前记录生成命令。

包选项和兼容性细节见[包与分发](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/packages.md)。下一篇学习如何定位运行时问题并生成 AOT 源码：[调试与 AOT 生成](11-debugging-and-aot.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/10-packages-and-distribution.md) · [fr-FR](../../fr-FR/toolchain/10-packages-and-distribution.md) · [ja-JP](../../ja-JP/toolchain/10-packages-and-distribution.md) · [ko-KR](../../ko-KR/toolchain/10-packages-and-distribution.md) · [zh-CN](10-packages-and-distribution.md) · [zh-TW](../../zh-TW/toolchain/10-packages-and-distribution.md)
