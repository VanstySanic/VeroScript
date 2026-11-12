# 12：宿主绑定与脚本集成

VeroScript 的一个重要使用场景是嵌入 C++ 宿主。宿主负责创建引擎或会话、注册函数和类型；脚本负责调用稳定的公开能力。本篇先用绑定目录理解边界，再说明实际集成时的工作顺序。

## 第一步：认识绑定目录

绑定目录用 JSON 描述宿主 API。从本文档资源或单独的 Resources 发行取得 [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json)，以同名文件复制到可写的教程目录。SDK 归档不附带示例。以下命令均从该教程目录运行：

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

绑定目录包含 `version`、`functions`、`genericTypeTemplates`、`types`、`typeAliases` 和 `annotations` 等顶层字段。它描述接口，不会代替 C++ 注册代码，也不会单独产生运行时实现。

先把目录看成“编译器看到的接口清单”：函数至少描述 `name`、`returnType` 和 `paramTypes`；宿主对象和容器还需要对应的宿主类型、元素类型或键值类型元数据。引用参数使用与 `paramTypes` 平行的 `paramFlags`，参数注解使用平行的 `paramAnnotations`。这些字段必须和 C++ 注册签名保持一致。

例如，示例绑定目录中的 `host_add` 函数条目使用两个 `int` 参数，并为第一个参数附加了注解：

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

不要把目录中的元数据数组理解成运行时实现；它们只描述接口和工具可见信息。完整字段表、支持的类型名、泛型模板、类型别名和 JSON 限制见[宿主绑定参考](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。

## 第二步：在脚本中使用宿主能力

宿主注册完成后，脚本可以像使用普通声明一样调用公开函数或类型。例如，假设宿主注册了 `HostLog`：

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

假设宿主注册了带静态方法的类型，脚本通常通过类型名访问：

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

上面的名字只是展示调用形状；实际可调用名称必须以宿主注册内容和绑定目录为准。

## 第三步：按顺序集成

在 C++ 宿主中，推荐按下面的顺序推进：

执行前，通过 `FEngineConfig::VmBackendFactory`／`VmBackendPath` 或 Session 选择 API 配置匹配的 VM。仅链接 C++ SDK 不会选中解释器，脚本编译处理器也需要 VM。所需 SDK 目标及配置步骤见 [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/zh-CN/index.md)。

1. 创建 `VeroScriptEngine` 或长期存在的 `VeroScriptSession`。
2. 注册原生函数、宿主类型、成员和运算符。
3. 为需要工具支持的公开接口补充文档和参数信息。
4. 导出或加载绑定目录，并运行 `bindings validate`。
5. 先调用 `CheckSource`/`CheckFile`，通过后再调用 `RunSource`/`RunFile`。
6. 为脚本检查和运行失败保留诊断信息，不要只返回一个布尔值。

长期运行的编辑器、游戏或宿主工具通常使用 `VeroScriptSession` 管理模块、绑定、运行状态和热重载；一次性执行可以使用 `VeroScriptEngine` 的源代码或文件入口。

## 第四步：控制接口稳定性

把绑定当作版本化 API：新增函数通常是兼容扩展，但删除成员、改变参数类型、改变枚举值或改变所有权语义都可能破坏脚本。发布宿主版本时，同时检查：

- 绑定目录是否通过校验。
- 脚本是否通过 `--check`。
- 宿主注册表与目录是否一致。
- 类、结构体和句柄的生命周期是否有明确约定。

如果注册的静态宿主成员有显式 `HostObject` 参数，也可以用 `nullptr` 测试可空边界。
回调会收到空的宿主对象句柄，解引用载荷前必须先检查；空的实例接收者仍然无效。

## 练习

1. 为一个宿主函数设计稳定的名称、参数和返回类型。
2. 给绑定函数增加简短文档和参数说明。
3. 修改一个绑定条目后，分别运行目录校验和脚本检查。

宿主集成的完整边界见[宿主集成](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)，绑定目录见[宿主绑定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)。


下一篇学习如何在脚本运行时检查类型和声明信息：[运行时反射](13-reflection.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/advanced/12-host-bindings.md) · [fr-FR](../../fr-FR/advanced/12-host-bindings.md) · [ja-JP](../../ja-JP/advanced/12-host-bindings.md) · [ko-KR](../../ko-KR/advanced/12-host-bindings.md) · [zh-CN](12-host-bindings.md) · [zh-TW](../../zh-TW/advanced/12-host-bindings.md)
