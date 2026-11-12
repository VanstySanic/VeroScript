# C++ Transpiler

`VeroScriptCppTranspiler` 将能够保持语义的严格 C++17 子集转换为 VeroScript 源码。它共享 [C++ 前端](frontend.md) 的 AST 和类型映射，但独立于 BindGen 与语言无关的 Interop ABI。

```powershell
VeroScriptCppTranspiler Resources/Examples/Basic.cpp `
  -o build/Basic.vs `
  --source-map build/Basic.vs.map.json
```

生成结果包含 VeroScript 源码和可选的声明级源映射。默认情况下会由 VeroScript 编译器校验，只有解析、降级和校验全部成功后才写出结果。项目模式先解析并合并所有输入，再为每个输入生成 `.vs` 与源映射，并生成确定性的 manifest；manifest 将生成模块列为隐式导入并启用项目级访问。

项目 manifest 使用稳定字段 `version`、`kind`、`allowProjectWideAccess`、`modulePaths`、`implicitImportPaths` 和 `files`。`files` 的每个元素包含 `input`、`source` 和 `sourceMap`。manifest、生成源码和源映射必须来自同一次转换；生成文件字段中的路径为相对于项目的路径。

项目模式示例：

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp `
  --output-dir build/generated `
  --manifest build/generated/transpile.manifest.json
```

manifest 的结构示例为：

```json
{
	"version": 1,
	"kind": "veroscript.transpiled-project",
	"allowProjectWideAccess": true,
	"modulePaths": ["Types.vs"],
	"implicitImportPaths": ["Types.vs"],
	"files": [
		{
			"input": "Source/Types.cpp",
			"source": "Types.vs",
			"sourceMap": "Types.vs.map.json"
		}
	]
}
```

CLI 还接受可重复的 `--attribute-map cpp=vero` 映射。`veroscript::` 和 `vero::` 命名空间中的属性会直接转换；其他属性命名空间需要显式映射。默认会校验生成源码；只有在由独立验证阶段负责校验时才使用 `--no-validate`。

CLI 选项包括单文件输出的 `-o`/`--output`、源映射的 `--source-map`、项目模式的 `--output-dir`、manifest 的 `--manifest`、可重复的 `--attribute-map`、`--no-validate`、`--version` 和 `--help`。项目模式会为每个输入生成源码和源映射。

C++ API 中，`FTranspileOptions` 还控制生成源码校验、记录在源映射和诊断中的生成源码路径、额外的已知宿主类型和枚举类型、已知类型别名以及显式属性映射。项目选项会将校验策略和属性映射应用于项目输出。

支持按值代码、命名空间、枚举、类/结构体、构造函数、方法、全局变量、容器、`this->member`、标量 `static_cast`、`typedef`/`using` 别名、普通和 `u8` 原始字符串，以及类型化 `throw`、`try`/`catch`、全捕获和重新抛出。别名模板会在结构化声明类型中展开。只使用 `typename`/`class` 类型参数的类、结构体和自由函数模板（包括末尾默认类型参数）会转换为 VeroScript 开放泛型。声明属性会转换为 VeroScript 注解；参数属性由 BindGen 保留，因为源转换没有对应的参数注解槽位。项目模式会匹配类外定义及静态数据定义。

所有权操作、任意指针、RTTI、协程、高级 C++ 模板特性、共享所有权、未解析的 `extern`、分配/存储构造、引用字段或全局变量、无法表示的 `constexpr`/`consteval`/`constinit`/`noexcept`、`goto` 以及不支持的属性或 friend 形式会被拒绝；工具不会静默删除无法表达的语义。

非类型模板参数、参数包、特化、SFINAE、concept、依赖编译期表达式、方法局部模板和显式函数模板实参调用不在支持范围内。泛型别名如果出现在不透明函数体、friend token 序列或初始化器中，也会被拒绝。

项目模式中，普通方法的声明保留在声明模块，正文保留在实现模块；构造／析构和静态数据仍按核心的同文件要求合并。默认验证会按 manifest 的可见性策略编译最终模块图，并将错误映射回原 C++ 文件。无法保留构造／析构查找作用域的合并会被拒绝。泛型类的类外定义仍不支持，前端会明确报错。

项目合并不是链接器。限定的命名空间定义仍属于该命名空间；内部链接函数或变量发生冲突时报告 `cpp.merge.internal_linkage_collision`，不会重命名实体或按输入顺序选择定义。多个输出一起提交，避免生成源码与源映射不同步。

## 数值类型与整数字面量

局部变量及 `static_cast` 的基本数值类型使用与声明签名相同的平台映射。整数字面量按工具编译平台的 C++17 数据模型选择类型，再转换为脚本后缀；八进制、二进制和数字分隔符保留其数值。十六进制浮点与 `long double` 字面量会被明确拒绝。数值转换要求输入值处于目标类型范围内，超范围按脚本规则报错；依赖 C++ 窄化截断或混合有符号提升的代码需要显式改写。

`std::pair<T,U>` 映射到核心的软内建 `pair<T,U>`。可见的同名同参数个数用户模板会捕获该类型时，转译以 `cpp.transpile.standard_pair_shadowed` 明确失败，包括项目模式和关闭生成源码校验的情况。应重命名用户模板或提供显式适配器；不同参数个数或不相关命名空间中的模板仍可共存。

VeroScript 模板参数不能遮蔽全局类型或普通别名。这类 C++ 声明会在输出前报告 `cpp.transpile.template_parameter_shadow_unsupported`，包括项目模式及关闭生成源码验证的情况。转译前请重命名 C++ 模板参数。别名模板按结构解析，并保留自身参数的绑定关系。

结构化声明中的用户类型采用绝对限定名，包括嵌套模板参数，避免在输出命名空间内重新绑定。基类保留原始查找作用域。目标继承语法无法表达被局部同名类型遮蔽的全局基类，此时会在输出前报告 `cpp.transpile.global_base_shadowed`；关闭生成源码验证时也会拒绝。
内建容器基类和依赖类型参数本身的基类会报告 `cpp.transpile.base_type_unsupported`；继承要求已知的类或结构体类型。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](../ja-JP/transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
