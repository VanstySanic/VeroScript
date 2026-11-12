# 脚本编译扩展示例

`builder.vs` 注册 `@Builder` 的说明和处理器，在 Types 阶段生成 `CardBuilder`，在 Members 阶段生成 `Card::Create()`。`main.vs` 使用生成的静态工厂和链式调用，输出 `Save`。这个入门示例专门处理具有公开 `string text` 字段的 `Card`，不是任意类型的通用构建器。

## 使用 Runtime

在示例目录运行：

```sh
veroscript --check main.vs
veroscript main.vs
```

Runtime 的标准库目录需要包含 `compiler.vs`。

`builder.vs` 通过 `a.case_sensitive = false` 为 `@Builder` 显式启用忽略大小写；`@builder`、`@Builder` 等写法共用一个定义和处理器。省略该配置时仍区分大小写。反射的 `name()` 保留源码拼写，`definition().name()` 返回注册名称。

## 编译扩展包

```sh
veroscript compiler-package builder.vs builder.vslib
veroscript --compiler-extension builder.vslib --eval '@Builder class Card {public:string text;}; Card card=Card::Create().Text("Package").Build(); print(card.text);'
```

第二条命令输出 `Package`。`--eval` 执行顶层语句。这个消费者由命令行加载扩展包；源码示例 `main.vs` 则通过 `import compiler` 加载源码扩展，各自使用一个入口。

## 使用 Core SDK

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<CoreSDK 安装目录>;<BytecodeVmSDK 安装目录>"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake 示例查找精确的 0.1.1 Core SDK，并链接匹配的 Bytecode VM SDK；两者安装在同一目录时，只需提供一个前缀。CTest 使用 Core SDK 安装目录内的标准库。可通过 `VEROSCRIPT_EXAMPLE_STDLIB_PATH` 指定其他标准库目录。宿主程序接受入口脚本和标准库目录两个可选参数。

Windows 示例使用宽字符入口，将命令行文件路径转换为核心约定的 UTF-8；CTest 同时验证含空格和中文的入口脚本目录。
