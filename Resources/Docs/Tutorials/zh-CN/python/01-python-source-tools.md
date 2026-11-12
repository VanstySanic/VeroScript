# 01：构建 Python 源工具流水线

前提：安装匹配的 Python Frontend、BindGen、Transpiler 包，并确保能调用 `veroscript`。在可写目录准备受支持的类型化 `host_api.py`、`app.py`、`package/models.py`、`package/service.py`；替换输入时统一修改路径。运行时绑定还需要配置好的 Python SDK Session，参见 [Python SDK 指南](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/index.md)。

## 1. 分析声明

```powershell
python -m veroscript_python_frontend host_api.py -o generated/host.python-ast.json
```

查看生成的 AST，确认模块、函数、类和源位置。输入模块不会被导入或执行。

## 2. 生成宿主契约

```powershell
python -m veroscript_python_bindgen host_api.py `
  --catalog generated/bindings.json `
  --api-reference generated/host.vs.txt `
  --validate --veroscript veroscript
```

把目录中的函数与宿主将要注册的实际 callable 对照。目录不会替代宿主实现。

## 3. 转换源码

```powershell
python -m veroscript_python_transpiler app.py `
  -o generated/app.vs --source-map generated/app.vs.map.json `
  --veroscript veroscript
veroscript --check generated/app.vs
```

无法保持语义的 Python 特性不会被静默近似，而会报告诊断。

## 4. 使用项目模式

当模块相互导入时，应把所有输入作为同一个项目传入，使声明和导入解析保持一致：

```powershell
python -m veroscript_python_bindgen package/models.py package/service.py --project-root . --output-dir generated/bindings --validate --veroscript veroscript
python -m veroscript_python_transpiler package/models.py package/service.py --project-root . --output-dir generated/veroscript --veroscript veroscript
```

应把生成的 manifest、源映射和输出作为对应的一组保存，不要混用不同输入集合生成的
构件。在运行时，由应用负责导入模块，并一次性注册合并后的 Catalog：

注册片段假设 `session` 是配置好的 Session，`models`／`service` 是显式导入 `package.models`／`package.service` 得到的模块对象，`project_bindings` 是解析 `generated/bindings/bindings.json` 得到的映射，或 `generate_binding_project` 的返回结果。源码工具不会替应用导入这些模块。

```Python
from veroscript_python_bindgen import bind_modules

bind_modules(session, (models, service), project_bindings)
```

该 helper 会在修改会话前检查模块覆盖范围，并在连接跨模块成员之前注册项目类型。

## 第 5 步：验证运行时绑定和值

运行时注册的枚举应使用 `IntEnum` 或 `IntFlag`；普通 `enum.Enum` 会被拒绝。枚举
成员名称与整数值必须与 Catalog 完全一致，Catalog 中的 instance method 和
`staticMethod` 也必须分别对应实际的实例方法与 static/class method。

`Value(None)` 表示脚本的 `null`，`Value.priority_queue("int", [3, 1, 2])` 创建
优先队列。向非可空值赋予 `None` 会抛出 `TypeError`。建议把这些检查加入宿主测试，
及时发现 Catalog 与运行时模块不一致。

## 练习

1. 添加带注解的枚举，并确认目录包含其值。
2. 添加未注解的局部变量，阅读前端诊断。
3. 以项目模式处理两个相关模块并比较 manifest。
4. 将生成的 Catalog 加载到 Python 宿主中，确认注册实现与生成的签名一致。

如果需要独立运行的 Python 构件，请继续阅读
[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/aot.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/python/01-python-source-tools.md) · [fr-FR](../../fr-FR/python/01-python-source-tools.md) · [ja-JP](../../ja-JP/python/01-python-source-tools.md) · [ko-KR](../../ko-KR/python/01-python-source-tools.md) · [zh-CN](01-python-source-tools.md) · [zh-TW](../../zh-TW/python/01-python-source-tools.md)
