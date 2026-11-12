# Python 前端

`VeroScriptPythonFrontend` 是 Python BindGen 与 Python 到 VeroScript 转换器
共用的静态前端。它不会导入或执行输入模块，而是生成由 VeroScript 定义
的 Python 声明模型。

前端保留源位置、模块与导入、类型化函数、类、dataclass、整数枚举、带注解
的字段和参数、装饰器、文档以及规范化类型注解。模块顶层执行、动态或无法
解析的类型，以及无法表达的语法都会产生诊断。

```powershell
python -m veroscript_python_frontend input.py -o input.python-ast.json
```

多个输入可以作为项目解析：

```powershell
python -m veroscript_python_frontend package/models.py package/service.py `
  --project-root . --output-dir generated/frontend
```

工具先暂存每个输入的 AST 和 `frontend.manifest.json`，再提交写入；失败时尝试
恢复原有输出。API 提供
`parse_source`、`parse_file`、`parse_files`。需要结构分析时使用前端，
需要宿主绑定目录时使用 BindGen，需要生成 VeroScript 时使用 Transpiler。

## 诊断回退

使用 `--lang` 选择诊断语言，其优先级高于 `VERO_LANG` 和系统语言。缺失或不可用的翻译消息回退到英文；诊断代码与源码位置在各语言中保持一致。

`--lang` 作用于使用 VeroScript 目录的工具消息，不会翻译 `argparse` 帮助和参数解析错误，也不会翻译 Python、操作系统或外部编译器透传的文字；这些内容沿用各自的语言行为。

## 选项与项目输出

单文件模式通过 `--module-name` 指定逻辑模块名，`--version` 显示工具版本。项目模式保留包结构，拒绝重复输入、重复模块名以及 `--project-root` 之外的文件。`--output-dir` 不能与 `-o` 或 `--module-name` 一起使用。输出采用 UTF-8，在替换前统一暂存。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](../ko-KR/frontend.md) · [zh-CN](frontend.md) · [zh-TW](../zh-TW/frontend.md)
