# 已安装的脚本编译扩展

这个可移植样例安装一个编译期依赖。核心登记其脚本处理器，读取包内模板输入，给标注的类生成静态方法 `value()`，再按普通脚本编译、运行。应用通过 `Answer::value()` 取得结果。

扩展用 `reg.register_annotation` 登记 `@Answer`，设置 `case_sensitive = false`，并订阅类上的注解事件。应用中的 `@aNsWeR` 因而能够触发处理器；此选项只折叠 ASCII 字母大小写，默认未设置时仍区分大小写。源码拼写保持不变，订阅匹配和反射查询共用登记的策略；注解参数名及元数据键仍区分大小写。原生扩展的分析目录文件（binding catalog）通过核心接口导出／导入 `caseSensitive`，包管理器不另行改写该策略。

在 `app` 目录执行：

```text
veroscript install --offline
veroscript --check main.vs
veroscript main.vs
```

输出 `42`。Runtime／SDK 应提供标准库；源码构建的 CLI 可显式添加 `--stdlib-path <VeroScript/Resources/Scripts/stdlib 的绝对路径>`。

安装不执行此处理器，也不需要 `--allow-hooks`。处理器在后续编译时执行，通过核心的只读依赖接口读取 `templates/value.txt`；该读取参与编译缓存复核。依赖声明为 `compile`，部署运行闭包时不会携带它。使用已生成的 `.vsapp`／字节码运行；重新编译源文件仍需要编译环境和处理器。
