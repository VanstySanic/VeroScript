# 02：检查、运行和读取命令行参数

一个脚本一旦需要处理不同输入，就不应该把数据写死在源文件中。本篇会让脚本读取命令行参数，并学习如何用 `--check` 和不同后端验证它。

## 第一步：接收参数

创建 `argv_echo.vs`：

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)` 接收脚本路径之后的参数。脚本文件名本身不会进入 `args`，所以 `len(args)` 表示真正传给脚本的参数数量。

## 第二步：传入参数

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

输出类似于：

```text
argc=2
red
blue
```

参数都是字符串。`to_int` 等数值转换函数接受数值或布尔值，不负责将字符串解析为数字。需要数值参数时，应先定义并验证允许的文本格式，再用于计算。详见[转换函数](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/builtins.md)。

## 第三步：做一个问候工具

将下面的程序保存为 `greet.vs`，放在 `argv_echo.vs` 所在目录：

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

先检查程序，再分别以无参数和有参数的方式运行：

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

无参数运行时使用默认值；有参数运行时使用第一个参数覆盖默认值。

## 第四步：选择执行后端

普通运行默认使用混合后端。为了比较不同执行方式，可以显式指定：

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

VM 命令使用 Runtime 自带的解释器。尝试 JIT 命令前，将 `/path/to/Jit` 替换为兼容后端；加载失败时，CLI 会在运行脚本前停止。成功后，两条命令都像前面一样把 `one` 和 `two` 传给 `argv_echo.vs`。项目默认值、自动选择与禁用 JIT 的规则见 [CLI 后端参考](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md#cli-backend-policy)。

调试时会使用 VM 调试路径；不要用 JIT 选项判断调试器是否生效。现在先记住：开发阶段优先使用 `--check`，需要比较运行方式时再指定后端。

## 练习

1. 如果没有参数，输出 `usage: greet.vs <name>`，然后返回一个非零状态码。
2. 检查第二个参数是否存在，并用 `len` 输出它的长度。
3. 使用 `--lang zh-CN` 和 `--lang en-US` 比较诊断语言。

下一篇开始学习变量、条件和循环：[变量、条件和循环](../language/03-values-and-control-flow.md)。

对应的完整示例是 [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs)，CLI 选项详见[命令行工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)。

比较性能时，使用固定输入和后端的普通运行，并等待预热。调试、保留初始化和有界清理使用 VM；持久会话中满足条件的普通函数可以使用所选 JIT。字节码缓存减少启动时的编译工作。会话 JIT 缓存的行为和限制见 [C++ 嵌入指南](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md#threads-and-hot-reload)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/getting-started/02-cli-and-arguments.md) · [fr-FR](../../fr-FR/getting-started/02-cli-and-arguments.md) · [ja-JP](../../ja-JP/getting-started/02-cli-and-arguments.md) · [ko-KR](../../ko-KR/getting-started/02-cli-and-arguments.md) · [zh-CN](02-cli-and-arguments.md) · [zh-TW](../../zh-TW/getting-started/02-cli-and-arguments.md)
