# 11：调试与 AOT 生成

本篇介绍两种交付前常用的工具链动作：用调试器观察脚本，以及根据部署目标选择 AOT 输出形式。

## 第一步：准备一个可观察的脚本

```VeroScript
int calculate(int value)
{
	int result = value * 2;
	return result + 1;
}

int main()
{
	int input = 20;
	int output = calculate(input);
	print(output);
	return 0;
}
```

将它保存为 `debug_demo.vs`，先做普通检查：

```powershell
veroscript --check debug_demo.vs
```

## 第二步：启动 DAP 调试

标准输入输出模式适合由编辑器启动并管理：

```powershell
veroscript --debug debug_demo.vs
```

如果调试客户端需要通过 TCP 连接：

```powershell
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 debug_demo.vs
```

在编辑器中配置断点、单步和变量查看。调试执行使用虚拟机路径，因此不要用调试时的后端行为代表普通运行性能。

应在活动代码上设置源代码断点。使用 DAP 客户端时，应通过 `setBreakpoints` 发送。启动前设置的断点会
处于待绑定状态；程序加载后，客户端会收到同一断点 ID、`reason: "changed"` 的 `breakpoint` 事件。
始终采用返回的已验证状态和行号。被 `#if` 移除、超出源文件范围或没有可执行位置的行会保持未验证。

## 第三步：生成 AOT 源码

当脚本不需要动态加载时，应根据部署目标选择 AOT 目标。默认目标是 C++17：

```powershell
veroscript aot debug_demo.vs -o generated.cpp
```

`generated.cpp` 不是最终可执行文件；宿主项目仍需使用兼容的 C++17 工具链和 VeroScript 运行时完成编译链接。生成后先检查文件，再在目标项目中构建，不要把生成文件误当作独立程序运行。

如果需要独立 Python 程序，请显式选择 Python：

```powershell
veroscript aot --target=python debug_demo.vs -o generated.py
python generated.py
```

如果需要生成 C# 项目或 NativeAOT 程序，请生成源码 bundle：

```powershell
veroscript aot --target=csharp debug_demo.vs --output-dir generated-csharp --bundle-name Demo
dotnet publish generated-csharp/Demo.csproj -c Release
```

支持范围与明确限制请参阅 [Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/aot.md) 和 [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/aot.md)。目标无法保持语义时会返回诊断并停止生成。

## 选择建议

- 快速迭代、热重载和交互调试：使用 VM 或默认混合后端。
- 需要断点和变量检查：使用 `--debug`。
- 纳入本地 C++ 构建：使用 C++ AOT，并验证宿主绑定和并发行为。
- 独立 Python 源码：使用 `--target=python`，并在目标 Python 版本中测试。
- .NET 或 NativeAOT：使用 `--target=csharp`，并构建生成的 bundle。

## 练习

1. 在 `calculate` 内设置断点，查看 `input` 和 `result`。
2. 修改输入值，比较 VM 运行与调试运行的输出。
3. 为一个使用数组的脚本生成 C++ AOT 源码，确认目标项目能够编译它。
4. 为同一个脚本生成 Python 或 C# 输出，比较不同目标的限制诊断。

完整选项见[调试](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)、[C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/aot.md)和[C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/aot.md)。下一篇进入嵌入式场景：[宿主绑定与脚本集成](../advanced/12-host-bindings.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/11-debugging-and-aot.md) · [fr-FR](../../fr-FR/toolchain/11-debugging-and-aot.md) · [ja-JP](../../ja-JP/toolchain/11-debugging-and-aot.md) · [ko-KR](../../ko-KR/toolchain/11-debugging-and-aot.md) · [zh-CN](11-debugging-and-aot.md) · [zh-TW](../../zh-TW/toolchain/11-debugging-and-aot.md)
