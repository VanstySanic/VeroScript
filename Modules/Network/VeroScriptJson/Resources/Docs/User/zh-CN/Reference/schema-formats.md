# Schema 格式断言与方言

默认 Draft 2020-12 的 `format` 是注解，不改变有效性。需要格式断言时，可显式设置
C++ `FSchemaOptions::bAssertFormats=true`，或脚本 `schema_options.set_assert_formats(true)`。
该选项对本次编译的全部资源生效，未知格式名称是编译错误。

```vs
json::schema_options options = json::schema_options();
options.set_assert_formats(true);
json::schema schema = json::compile_schema_with_options(
    json::parse("{\"format\":\"email\"}"), options, json::object());
bool valid = schema.is_valid(json::from_string("x@example.test"));
```

完整示例：[schema_formats.vs](../../../../Examples/schema_formats.vs)。

## 支持表

所有格式均只作用于字符串。格式不匹配是正常的无效结果；取消、分配或额度失败表示验证没有完成。

| 格式 | 校验内容 |
| --- | --- |
| `date-time`、`date`、`time` | RFC 3339 日期、时间、偏移和有数字的小数秒；闰秒检查偏移换算后的时分，不查询闰秒公告 |
| `duration` | RFC 3339 附录 A 的时长语法，含周形式、分量顺序和中间分量要求 |
| `email`、`idn-email` | SMTP 邮箱语法、引号/转义、长度及 IP 字面量；国际化形式支持 UTF-8 本地部分和域名处理 |
| `hostname`、`idn-hostname` | DNS 标签/总长度、A-label、Punycode、NFC、IDNA 上下文与双向规则；国际化形式使用非过渡 UTS #46 映射 |
| `ipv4`、`ipv6` | 严格地址语法，包括 IPv6 压缩及嵌入 IPv4；IPv4 不接受前导零 |
| `uri`、`uri-reference` | RFC 3986 绝对标识符或引用，含 authority、IP 字面量、转义及路径/查询/片段 |
| `iri`、`iri-reference` | RFC 3987 Unicode 范围；私用字符仅在允许的位置使用 |
| `uuid` | 标准带连字符 UUID 文本，十六进制大小写均可；不接受 URN 包装 |
| `uri-template` | RFC 6570 模板、变量列表、操作符、前缀和 explode 语法 |
| `json-pointer`、`relative-json-pointer` | 字符串形式 Pointer 及相对 Pointer 的整数前缀/取名形式，不当作 URI fragment 解码 |
| `regex` | Unicode ECMAScript 正则语法，与 `pattern` 使用同一引擎 |

上述 19 种标准格式均有断言实现。`format` 不做 DNS 查询、网络访问、邮箱投递验证或资源读取，
不会修改原始 JSON 字符串。主机名采用不带末尾根点的文本形式。

IDNA 数据固定为 Unicode 17 / python-idna 3.12；只有生成后的 C++ 表及适配代码参与构建，
无需 Python 运行时。NFC 使用已包含的独立 QuickJS Unicode 库。出于确定的资源边界，
国际化主机名映射输入最多 4096 字节，映射阶段每个标签最多 256 个码点，正规化临时存储最多 64 KiB。
达到处理额度报告 `Limit`；正常 DNS 标签仍受 63 个 A-label 字节和总计 253 字节约束。
执行预算和取消检查继续生效。

## 自定义方言

也可注册包含 `https://json-schema.org/draft/2020-12/vocab/format-assertion` 的元 Schema，
由业务 Schema 的 `$schema` 引用它。该词汇在声明为可选时仍具有完整语义；只有未知且必需的词汇
会被拒绝。未知可选词汇及其未知关键字保持注解行为。

词汇声明属于直接指定的元 Schema，通过 `$ref` 引用其他元 Schema不会继承其声明。
普通业务 Schema 自己的 `$vocabulary` 不切换方言。以上规则依据
[Core 8.1.2](https://json-schema.org/draft/2020-12/json-schema-core#section-8.1.2)。

嵌入资源默认继承所在方言，可通过自己的 `$schema` 显式切换。独立检索的文档未指定 `$schema`
时采用本模块固定的 Draft 2020-12 默认值。自引用及循环元 Schema 声明可使用已支持的词汇；
其定义仍需要通过对应元 Schema 校验，资源、执行和栈额度继续约束整个编译过程。
`bAssertFormats` 是调用方的显式覆盖选项，不依赖资源是否声明格式词汇。

内置标准元 Schema 包含格式断言词汇的语法资源 `.../meta/format-assertion`。
未知格式在断言模式中直接拒绝编译；默认注解模式允许普通未知格式。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/schema-formats.md) · [fr-FR](../../fr-FR/Reference/schema-formats.md) · [ja-JP](../../ja-JP/Reference/schema-formats.md) · [ko-KR](../../ko-KR/Reference/schema-formats.md) · [zh-CN](schema-formats.md) · [zh-TW](../../zh-TW/Reference/schema-formats.md)
