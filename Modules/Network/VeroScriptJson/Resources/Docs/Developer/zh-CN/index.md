# JSON 维护

数据层负责 DOM、文本和流。Schema 使用带记录补丁的 jsoncons 1.10.0 以及独立 QuickJS-ng 0.17.0 正则/Unicode 库，不包含 JavaScript 运行时。保留精确数值适配。C++ 映射不依赖 Core；脚本映射属于模块处理器。简单结构体模块使用缩减生成支持，含导入、泛型、继承、注解、可执行成员或复杂默认值时使用完整支持。保留声明顺序、所属作用域和封闭类型身份。

使用模块 CMake 入口。根集成由 VEROSCRIPT_BUILD_JSON 控制，静态注册/动态扩展由 VEROSCRIPT_JSON_BUILD_BINDINGS 控制。匹配的已安装 Core SDK 可独立构建绑定而不重编 Core。原生目标为 VeroScript::Json，COMPONENTS Bindings 提供 VeroScript::JsonBindings；Bindings 安装组件是数据层及许可的附加项。动态模块位于 lib/veroscript/modules。注册须使用实际实现身份，不能只看产品版本。

参阅[网络基础: 源码职责 — 源码构建与原生模块注册](../../../../../VeroScriptNetwork/Resources/Docs/Developer/zh-CN/index.md#network-execution-assembly)。

允许执行时，分别验证原生数据、静态/动态注册、源模块与二进制库消费。VEROSCRIPT_JSON_SCHEMA_CORPUS 指定锁定 Schema suite 及格式语料，未知 format 在注解模式执行；VEROSCRIPT_JSON_NORMALIZATION_CORPUS 指定 Unicode 17 NormalizationTest.txt，VEROSCRIPT_JSON_CORPUS 指定 JSONTestSuite。语料只由 Resources/Tools/Tests 显式辅助工具获取，正常构建不下载。记录实际语料/源码/工具链身份与结果，不用旧通过数量认证现行代码、其他宿主、JIT/AOT、移动或浏览器。

维护六语言 json.json、稳定 ID/参数及 share/VeroScriptJson/Resources/Localization 部署。json profile 会替换目标旧项，不可连续叠加。Unicode 表通过 Resources/Tools/Unicode/generate_schema_unicode.py 从锁定输入生成，不把 Python 带入产品运行时。保留原始声明与来源清单。

```sh
cmake -S Modules/Network/VeroScriptJson -B build/json -DCMAKE_BUILD_TYPE=Release
cmake --build build/json --config Release
ctest --test-dir build/json -C Release --output-on-failure
cmake --install build/json --config Release --prefix /absolute/sdk
```

[User](../../User/zh-CN/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)
