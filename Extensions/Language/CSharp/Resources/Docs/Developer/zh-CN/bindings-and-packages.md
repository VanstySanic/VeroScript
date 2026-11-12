# C# 绑定、包与兼容性

## 宿主绑定模型

托管定义表示宿主函数、宿主类型、成员、操作符、注解、enum 值、文档、类型参数和协议回调。宿主类型协议可包含 clone、相等性、哈希和子类工厂。泛型宿主类型模板经 Interop 回调契约实例化，且必须保留目录使用的类型身份。

生成目录是元数据。注册回调负责连接真实托管对象和函数；仅有目录不会调用 C# 方法。

## 包

包构建选项包括包种类、名称、版本、宿主元数据和调试信息策略。包从字节读取，公开元数据和规范签名字节，并需要显式释放。只有会话接受该包及其种类/入口形状时，加载才会消费已编译模块。

## API 兼容性

经过检查的 API 契约是公共托管类型、构造函数、属性、enum 值、回调签名和目标框架的兼容边界。必须结合 `VeroScriptInterop.h` 审查原生布局变更。只有托管测试、公共 API 契约、包验证和原生 ABI 消费者都保持一致，包装器修改才算完成。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/bindings-and-packages.md) · [fr-FR](../fr-FR/bindings-and-packages.md) · [ja-JP](../ja-JP/bindings-and-packages.md) · [ko-KR](../ko-KR/bindings-and-packages.md) · [zh-CN](bindings-and-packages.md) · [zh-TW](../zh-TW/bindings-and-packages.md)
