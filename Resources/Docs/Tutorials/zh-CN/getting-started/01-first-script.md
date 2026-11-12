# 01：第一个脚本

本篇只做一件事：创建一个脚本，让 VeroScript 运行起来并输出一行文字。

## 学完你会知道什么

- 一个最小 VeroScript 程序需要什么。
- 如何运行脚本。
- 如何在不执行脚本的情况下检查它。
- `main` 和 `print` 分别负责什么。

## 第一步：创建文件

在工作目录中创建 `hello.vs`，写入：

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

这里有两个关键点：`main` 是脚本入口，`int` 表示它返回进程退出状态；`print` 把字符串写到标准输出。`void main` 仍然支持，但完整程序通常使用 `int main` 并显式返回 `0` 表示成功。

## 第二步：先检查，再运行

在保存 `hello.vs` 的目录中执行：

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

如果检查通过，运行结果应类似于：

```text
hello, VeroScript
```

`--check` 只编译并报告问题，不会执行 `main`。开发时可以先检查，确认没有问题后再运行。

## 第三步：让输出变得有用

把脚本改成：

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

再次运行，你会看到：

```text
hello, Ada
```

`string name` 是一个字符串变量。`+` 在这里连接两个字符串。变量名可以修改，但类型和初始化表达式要保持一致。

## 遇到问题时

- 如果提示找不到 `veroscript`，先确认 CLI 已经安装并加入 `PATH`。
- 如果提示找不到 `hello.vs`，确认终端当前目录就是文件所在目录，或者传入绝对路径。
- 如果提示语法错误，先运行 `veroscript --lang zh-CN --check hello.vs`，让诊断以中文显示。

## 练习

1. 把名字改成你自己的名字。
2. 增加一个 `string language = "VeroScript";` 变量，输出 `name + " is learning " + language`。
3. 故意删掉一个分号，运行 `--check`，观察诊断信息，再把代码修好。

下一篇会把固定字符串换成命令行参数：[检查、运行和读取命令行参数](02-cli-and-arguments.md)。

完整的最小示例见 [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs)；语法总览见[基本语法](../../../../../VeroScript/Resources/Docs/User/zh-CN/language-basics/basic-syntax.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/getting-started/01-first-script.md) · [fr-FR](../../fr-FR/getting-started/01-first-script.md) · [ja-JP](../../ja-JP/getting-started/01-first-script.md) · [ko-KR](../../ko-KR/getting-started/01-first-script.md) · [zh-CN](01-first-script.md) · [zh-TW](../../zh-TW/getting-started/01-first-script.md)
