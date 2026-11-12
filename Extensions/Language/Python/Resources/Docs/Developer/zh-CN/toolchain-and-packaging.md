# Python 工具链与包契约

## 严格的静态工具契约

当前端发现类中重复声明字段、枚举值、属性或方法时，会报告 `PYF0003`；仅允许一个与已有属性匹配的 setter。Transpiler 仅允许将 `None` 用作可调用项返回类型，要求每个构造函数声明 `-> None`，实例接收者必须是无默认值的位置参数 `self`，且方法装饰器只允许 `@staticmethod`。

在运行时绑定前，BindGen 还会验证完整 Binding Catalog 与实际 Python 可调用项。它会拒绝重复构造函数或可见成员名、字段/getter/setter 签名不匹配，以及必需位置参数形状与目录不完全一致的构造函数、函数、方法和属性访问器。这些失败不会部分注册绑定，也不会替换已生成的产物。

## 原生扩展

`VeroScriptPython` 是 CPython 扩展，不是嵌入式 Python 发行版。CMake 要求 Python 3.11 至 3.15 的 CPython 解释器和 `Development.Module`；拒绝其他 Python 实现，不能悄然生成不兼容扩展。

扩展链接 `VeroScriptInterop`，使用平台 CPython ABI 后缀，并将 Interop 库放在可导入包旁。构建还在包旁部署匹配的 `localization/` 和 `stdlib/` 树，使测试和本地导入不依赖全局 PATH 或库路径改变。

## 工具与组件

前端、BindGen、Transpiler 和 Python AOT 包作为 Python 工具组件安装。其 CMake 目标使用选定解释器测试，并安装相对 site-package 路径以使暂存前缀可重定位。运行时 wheel 包含原生扩展、Interop 库、Python 源、类型信息、标准库和 Catalog 资源。

## 发布验证

分发验证检查可导入性、原生库位置、版本一致性、包内容、wheel/源码分发行为和受支持解释器/平台矩阵。只能在仓库源码树中工作的包不是有效发布产物。

## Wheel 矩阵出版

Python 业务完整出版从 `Extensions/Language/Python/pyproject.toml` 推导解释器矩阵；`requires-python` 与 `cibuildwheel` 选择必须一致，当前覆盖 CPython 3.11–3.15。请预先准备与目标操作系统和架构匹配的五个原生解释器；自动发现使用带版本号的命令和 Windows `py` 启动器。完整出版可通过 Windows 的 `-PythonExecutables` 或 Unix 上重复的 `--python-executable` 参数指定路径，也可设置 `VEROSCRIPT_PYTHON_EXECUTABLES` 列表，Windows 以 `;` 分隔、Unix 以 `:` 分隔。缺少版本、调试构建、自由线程解释器或架构不匹配都会在预检阶段失败；不会自动安装解释器。

以下命令从仓库根目录检查 Windows x64 解释器集合，不进行构建。请选择与原生宿主匹配的目标；独立入口使用重复的 `--python` 参数指定解释器路径。

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

流水线创建一份源码分发包，并从该包在独立源码及构建目录中构建每个 wheel。每个 wheel 都在对应解释器的全新环境中安装，并运行 `Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` 选取的全部已安装包测试和 `pip check`。完整出版要求 wheel ABI 集合齐全、源码分发包存在、标签与原生身份一致，并包含许可证。业务清单的 `pythonWheelVersions` 记录矩阵，脱离源码仓库也可审计；部分组件出版保持部分状态。选择 Python 时，`-SkipBuild` 或 `--skip-build` 仍会构建并验证此矩阵。

Python sdist 仅存一份，位于 `Release/Extensions/Language/Python/any/<version>/packages/`；wheel 按真实平台／ABI 标签选择，仅在标签允许时共用清单。目标索引共同引用 sdist 与 wheel 清单。即使检出目录干净，也要检查实际 sdist 成员没有 `.cxx`、`.gradle` 或 `.externalNativeBuild` 缓存：`pyproject.toml` 已显式排除，源码分发校验也会拒绝它们。保留独立解压后构建所需的源码、头文件、构建脚本、元数据和法律声明。

SDK／安装器中的原生模块保留主构建的单一 CPython ABI。Linux wheel 保留实际原生标签；矩阵本身不代表已满足 `manylinux` 或 `musllinux` 兼容性。

## 本地化维护

各 Python 工具维护自身的 `Resources/Localization/<culture>/` 目录，只有多个工具共用的消息归入 Python 区域。部署时合并目录，不能重复定义 `python_tools.*` ID。文件必须是 UTF-8 JSON 对象，ID 唯一，值为非空白字符串。损坏的语言目录会被跳过，并尝试下一匹配候选。

英文目录定义可用消息 ID 和位置参数契约。翻译可以调整 `{0}`、`{1}` 等编号参数的顺序，但必须保留相同的参数索引集合；不支持命名字段、属性访问或索引访问。字面花括号使用 `{{` 和 `}}` 转义。模板缺失或无效时回退到实际加载的英文目录，格式化失败时再使用内置英文。出版前须通过 Frontend 本地化回归测试和六语言部署校验。

格式错误的 JSON、重复消息 ID、非字符串或空白值、无效 UTF-8 会使对应语言目录被拒绝；仅翻译中存在的 ID 会被忽略。

<a id="python-package"></a>

Python 工具组维护 `Extensions/Language/Python/pyproject.toml` 中的 PEP 517 工程。从仓库根目录执行下列命令可构建并审查本地 wheel／源码分发候选；从源码安装使用 `python -m pip install ./Extensions/Language/Python`。源码分发包含所需原生输入，但仍需要选定 CPython 的开发文件和原生工具链。`VEROSCRIPT_PYTHON_INSTALL_DIR` 使用相对安装目标以保持 CMake 暂存可迁移。构建配置中的完整 wheel 矩阵时，从仓库根运行 `cibuildwheel`；构建与校验均不会上传发行物。版本元数据取自 `VeroScriptVersion.h`，须与加载的运行时一致。

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## 分发元数据核验

wheel 和源码分发包的 `Requires-Python` 上下界须匹配完整声明矩阵，当前为 `>=3.11,<3.16`。部分发布也会校验每个已提供的产物。Windows 扩展后缀须与 `win32`、`win_amd64` 或 `win_arm64` 及选定 CPython ABI 一致。macOS wheel 的最低版本标签须满足扩展和随包 Interop 库各自要求的最低 OS 版本。跨目标组包保留已配置的编译器、工具链、生成器平台及部署设置，并在编译扩展前核对目标 CPython 头文件。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](../ja-JP/toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
