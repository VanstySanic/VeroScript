# Python BindGen

`VeroScriptPythonBindGen` 将受支持的静态 Python 声明转换为 Binding Catalog
v1 JSON。它不会导入模块、执行装饰器或创建 Python 对象；Catalog 是宿主
API 契约，不是运行时实现。

支持公共函数、类、dataclass、`IntEnum`/`IntFlag`、带注解的实例字段、构造函数、
实例/静态/类方法和 property。动态或无类型签名、可变参数、默认参数、
模块全局量、不支持的继承以及无法表达的类型会失败。

```powershell
python -m veroscript_python_bindgen host_api.py `
  --module-name host --catalog host.bindings.json `
  --api-reference host.vs.txt --validate --veroscript veroscript
```

`--timeout <seconds>` 用于限制外部验证时间，必须是有限的正数。

## 项目模式命令

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py `
  --project-root . `
  --output-dir generated/bindings `
  --validate `
  --veroscript veroscript
```

`--output-dir` 选择项目模式；单个输入也可使用，多个输入时必须指定。`--project-root` 和 `--manifest` 仅用于项目模式，后者可覆盖清单输出路径。该模式不能与 `--catalog`、`--api-reference` 或 `--module-name` 混用。输出先统一暂存，失败时尽力回滚；清单必须与对应的 Catalog 和引用文件配套使用。

项目模式输出合并的 `bindings.json`、每个输入的 API reference 和
`bindgen.manifest.json`。库 API 包括 `generate_bindings`、
`generate_bindings_from_source`、`generate_bindings_from_file`、
`generate_binding_project`、`validate_catalog`、`bind_module` 和 `bind_modules`。
这两个绑定辅助函数只使用应用程序显式导入的模块；BindGen 本身不会导入或执行
被分析的源代码。`bind_modules` 会在修改会话之前验证项目中的所有目录所有者
模块都已提供。

绑定时，枚举类必须继承 `IntEnum` 或 `IntFlag`，并且成员名称与整数值必须与
Catalog 完全一致。Catalog 中的 instance method 必须仍是实例方法，`staticMethod`
必须仍是 static/class method。发现不匹配时会在修改会话之前失败。

项目中多个模块声明同名类型时，请使用 `models.Item` 这样的限定名称注解。
有歧义的裸名称会被明确拒绝，即使名称恰好是 `Int8` 等数值别名。
当前模块声明的类型优先于项目的裸名称别名和数值别名。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
