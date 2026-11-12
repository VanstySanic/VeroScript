# 脚本编译扩展

<a id="source-extension-activation"></a>

使用宿主或应用提供的扩展，可在普通检查前处理注解或生成声明。在入口文件开头、可选注释和已有编译器导入之后，用 import compiler 激活源码扩展；普通 import 不执行扩展注册。

## 仅注册描述的扩展

将第一份文件保存为 notes.vs，第二份保存为 main.vs。此扩展只描述 @Note，不生成业务行为。

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

程序输出 A descriptive annotation.。Runtime 标准库需包含 compiler.vs；core.vs 不重导出这个编译专用入口。

## 扩展包与限制

compiler-package 生成 .vslib 扩展，不执行注册。将相同业务程序去掉 import compiler 后保存为 consumer.vs，显式选择 notes.vslib；不要同时激活源码和包中的副本。

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

参阅[命令行工具 — 脚本编译扩展](../../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md#cli-compiler-options)。

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[注解](../language-basics/annotations.md)

扩展整体默认预算为 1,000,000 条 VM 指令和 2,000 毫秒。0 表示关闭对应预算；这些限制不能强行中断阻塞的原生回调。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md) · [en-US](../../en-US/standard-library/compiler.md) · [fr-FR](../../fr-FR/standard-library/compiler.md) · [ja-JP](../../ja-JP/standard-library/compiler.md) · [ko-KR](../../ko-KR/standard-library/compiler.md) · [zh-CN](compiler.md) · [zh-TW](../../zh-TW/standard-library/compiler.md)
