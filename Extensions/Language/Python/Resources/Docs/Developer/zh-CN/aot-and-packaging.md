# Python AOT 与打包

## 独立发射器

`VeroScriptPythonAot` 发射可读 Python 模块，其中含有小型兼容运行时，不加载 VeroScript VM、Interop 库或 CPython 扩展。控制包装器可以从可执行文件路径、SDK 根、`bin` 目录、环境变量或 `PATH` 查找用户提供的 CLI。

使用 `FCodegenOptions` 的 SDK 入口会将 `Bindings` 中的注解定义传入处理阶段，
包括 `CaseSensitive` 名称策略。仅注解元数据不会引入运行时依赖；独立输出不支持
运行时宿主函数和宿主类型。

## Bundle 契约

Bundle 模式写出 `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、`_veroscript_program.py` 和 `python-aot.manifest.json`。完整导入图作为一个程序检查。所有文件先暂存，再分阶段替换普通目录。目标只能是空目录，或是带有结构有效 VeroScript 清单且没有外来内容的目录；已识别的解释器 `__pycache__` 文件属于可容忍的生成内容。

最终路径不可为符号链接或 Windows 目录联接。提交期间会先移开原目录，输出路径可能短暂不存在；提交失败时尝试恢复原目录。

## 分发契约

wheel 与源码分发必须包含版本匹配的原生扩展、Interop 库、Python 源、类型信息、标准库和本地化目录。`VeroScriptVersion.h`、包元数据和导入运行时版本必须一致。发布验证检查支持的 CPython/平台矩阵、已安装包的类型接口和源码分发重建。

Python 独立运行时将数学定义域／溢出情况映射为 Core 浮点结果，不泄露 Python `ValueError`／`OverflowError`：零的对数产生负无穷，幂溢出保留奇整数指数的符号情况，floor／ceil／trunc 保留有符号零。Python 与 C# 生成的 NaN 文本遵循包括 Darwin 在内的目标平台拼写。通过后端回归维护这些发射细节，公开有限值检查见 [Math](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/math.md#floating-results)。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/aot-and-packaging.md) · [fr-FR](../fr-FR/aot-and-packaging.md) · [ja-JP](../ja-JP/aot-and-packaging.md) · [ko-KR](../ko-KR/aot-and-packaging.md) · [zh-CN](aot-and-packaging.md) · [zh-TW](../zh-TW/aot-and-packaging.md)
