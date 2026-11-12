# C++ BindGen

`VeroScriptCppBindGen` 将受支持的 C++ 声明转换为 VeroScript 绑定目录。绑定目录是编译器和工具的元数据，不是运行时实现；生成的 callback stub 也不是原生调用 thunk，真实 C++ 函数仍需由宿主适配器注册。

`GenerateBindings` 接收 AST；`GenerateBindingsFromSource`、`GenerateBindingsFromFile`、`GenerateBindingsFromFiles` 接收源码；`GenerateBindingProject` 合并多个输入并返回一个权威 `CatalogJson`、可选的 `ApiReferences`、确定性的 `ManifestJson`、诊断以及 `Succeeded()`。`FBindGenOptions::bEmitApiReference` 控制是否生成面向人的 API 参考。

`FBindGenProjectLayout` 指定合并目录、API 参考目录及项目清单的输出位置。编译器和 IDE 应使用合并后的权威目录；逐输入参考只是文档视图，不能替代合并后的声明集合。

项目 manifest 的稳定字段包括 `version`、`kind`、`catalogRole`、`catalog`、`inputs` 和 `apiReferences`。`apiReferences` 的每个元素包含 `input` 和 `path`。合并后的 catalog 只生成一次；reference 是可选的面向使用者的可读视图。

项目模式会把合并后的 catalog、每个输入的 API reference 以及 manifest 作为一个
确定性的结果集写出。manifest 的示例为：

```json
{
	"version": 1,
	"kind": "veroscript.cpp-binding-project",
	"catalogRole": "merged",
	"catalog": "bindings.json",
	"inputs": ["Include/Types.hpp"],
	"apiReferences": [
		{
			"input": "Include/Types.hpp",
			"path": "api/Types.vs.txt"
		}
	]
}
```

```powershell
VeroScriptCppBindGen Resources/Examples/BindingInput.hpp Resources/Examples/BindingExtras.hpp `
  --catalog build/BindingInput.bindings.json `
  --api-reference build/BindingInput.vs.txt
```

为每个输入生成 API reference 并同时写出项目 manifest：

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

CLI 选项包括必需的 `--catalog <output.json>`、`--api-reference <output.vs.txt>`、`--api-reference-dir <directory>`、`--manifest <output.json>`、`--version` 和 `--help`。两个 reference 选项不能同时使用。

工具保留限定名、枚举身份和底层类型、成员类别、构造函数、运算符、const 方法、`ref`/`const ref` 参数、普通类型别名和带类型参数的别名模板。别名默认参数无法表示在目录模式中，因此会被拒绝。类型、枚举、字段、函数和参数上的 C++ 属性会写入结构化注解；无命名空间属性以及 `veroscript::`/`vero::` 保留命名空间会被接受，其他厂商命名空间需要映射。枚举初始化器支持整数、可移植的单字符字面量、此前的枚举项、括号、一元运算、算术、移位、位运算、布尔、逻辑、关系和相等运算，并会检查 `int64` 溢出。无值属性仍作为标志参数保留，带名称的属性值会在目录中往返保存。

原始指针、返回引用、宿主到宿主继承、删除成员、引用字段、自由全局变量、命名方法重载、自由函数重载、默认参数、内部链接自由函数以及超出底层类型范围的枚举值会被拒绝。支持的枚举初始化器会经过检查后求值。完整目录字段与运行时注册边界见[宿主绑定](bindings.md)。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
