# C# BindGen

`VeroScriptCSharpBindGen` 消费 C# 语义模型，生成 Binding Catalog v1 元数据。还可按需生成供 `VeroScriptCSharp` 使用的无反射注册代码、API 参考和供生成 C# 程序使用的 AOT bridge。

## 选择输出

CLI 必须指定 `--catalog`。`--registration`、`--aot-bridge`、`--api-reference` 和 `--api-reference-dir` 选择附加输出；仅生成元数据时，省略注册与 bridge 选项。Catalog 描述绑定契约，不会安装可执行回调。注册代码与 `VeroScript.CSharp` 一起编译；AOT bridge 与原始托管声明及生成的 C# 程序一起编译，通过直接调用工作，不依赖反射或 `VeroScriptInterop`。

库 API 的默认值不同：`CSharpBindGenOptions.EmitRegistrationSource` 与 `EmitApiReference` 默认为 `true`，`EmitAotBridgeSource` 默认为 `false`。只要元数据、不生成可执行适配器时，须显式设置 `EmitRegistrationSource=false` 和 `EmitAotBridgeSource=false`。附加适配器的限制不应自动当成相应 Catalog 元数据的限制。

## 共同输入规则

默认考虑公共类、结构体、枚举及公共成员。`--require-attribute` 只选择标记 `[VeroScriptBinding]` 的类型，`[VeroScriptIgnore]` 排除声明。泛型宿主类型或成员、async、指针、nullable 或委托类型、可选或 `params` 参数、运算符、引用返回与字段、索引器、required 成员和无法表达的重载会被拒绝。抽象类构造函数不导出。附加输出还各有独立限制。

### 容器键

map 和 ordered-map 的键以及 set 元素可以是 `sbyte`、`int`、`uint`、`long`、`ulong`、
`string` 或已声明的 enum。`sbyte` 映射为 VeroScript 的有符号 8 位 `char`；其他键形状会在 Binding Catalog 到达运行时导入器之前安全失败。

## 实时注册代码的限制

注册代码只支持单层 array/list、map、set、queue 和 stack，元素为标量、字符串或已声明枚举；map 键与 set 元素还须满足上述键规则。不支持嵌套容器或宿主对象元素。multi-map、multi-set、priority queue 和 deque 请使用 SDK 值 API 或显式宿主包装器。

实时注册对涉及容器的静态字段、属性和方法返回 `VSCB3004`；具有公共实例字段、属性或非构造方法的结构体返回 `VSCB3001`，即使这些成员只读也一样。这些是实时注册生成器的限制；仅生成元数据与生成 AOT bridge 分别判断。AOT bridge 对结构体的公共实例方法、可写实例字段和普通公共 setter 返回 `VSCB3006`，因为无法保留装箱接收者的回写语义，参阅 [C# AOT](aot.md)。

## 单文件命令

```powershell
veroscript-csharp-bindgen HostApi.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference generated/Bindings.vs.txt
```

## 项目命令与输出

```powershell
veroscript-csharp-bindgen Models/A.cs Models/B.cs `
  --catalog generated/bindings.json `
  --registration generated/Bindings.g.cs `
  --aot-bridge generated/Bindings.Aot.g.cs `
  --api-reference-dir generated/api `
  --manifest generated/bindings.manifest.json
```

项目模式合并 partial 声明、分配稳定参考名称，在替换前统一准备全部选定输出。生成失败时保留既有输出，写入失败时尝试恢复。`--reference` 可重复；`--language-version` 选择 C# 语法版本，默认为 `latest`。`--api-reference` 与 `--api-reference-dir` 互斥。若要同时输出 API 参考和 `--manifest`，使用 `--api-reference-dir`；单独指定 `--manifest` 不要求输出 API 参考。

## 执行边界

实时注册回调可能在多个 VM worker 上并发执行，共享宿主状态必须同步。AOT bridge 直接调用托管声明。请一起使用同次生成的 Catalog、bridge、原始声明和程序。

## 生成容器的执行边界

Binding Catalog 元数据可以描述 ordered map。实时注册对 `SortedDictionary<TKey, TValue>` 返回 `VSCB3002`，生成的 AOT bridge 返回 `VSCB3003`；执行接入请使用显式宿主包装器。实时容器编组创建托管快照，只有 `ref`/`out` 参数在回调成功后回写；修改按值传入的托管容器不会更新脚本容器。AOT bridge 直接传递受支持的托管容器并保留对象身份。

## 链接输出路径

Windows 的 `\\?\`、`\\.\` 和卷 GUID 路径别名会按最终文件系统位置进行比较。这些别名同样不能绕过校验；互不相同的输出路径仍然受支持。

BindGen 与 Transpiler CLI 在暂存输出前解析已有的目录和文件链接。通过符号链接或 Windows junction 指定的路径不能覆盖输入源码，也不能重复指向同一输出文件；链接目录中互不相同的输出路径仍然受支持。

抽象宿主类保留受支持的方法及工厂返回的实例；其构造函数不会导出到 Catalog、实时注册或 AOT bridge。依赖外层泛型参数的嵌套类型返回 `VSCB1001`，即使通过属性单独选中该类型也不会导出无效代码。

AOT 的 `ref`、`out`、`in` 和 `ref readonly` 参数在基本类型及受支持容器的表示一致时直接共享原存储，保留参数间的引用别名。字符串、字符、枚举或宿主对象等需要表示转换的引用参数返回 `VSCB3007`；可设置 `EmitRegistrationSource=false` 和 `EmitAotBridgeSource=false` 生成 Catalog，并提供显式 partial bridge。

## 输出文件名冲突

BindGen 的 API 参考文件名在所有宿主上都会忽略大小写。同名主干会添加稳定的路径哈希，仍有冲突时再添加数字后缀。名称按输入路径顺序分配，因此重新排列输入列表不会改变名称。请通过返回的 API 参考记录或清单定位生成的 `.api.vs.txt` 文件。

<a id="binding-marker-attributes"></a>

筛选特性的定义由输入声明提供，SDK 不会注入这些类型。库选项默认使用 `BindingAttributeName="VeroScriptBindingAttribute"` 和 `IgnoreAttributeName="VeroScriptIgnoreAttribute"`，也可配置等价名称。生成的实时注册类型默认为 `VeroScript.GeneratedBindings.VeroScriptCSharpBindings`，由 `RegistrationNamespace` 和 `RegistrationClassName` 调整；指定名称不会启用已经关闭的注册输出。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/bindgen.md) · [fr-FR](../fr-FR/bindgen.md) · [ja-JP](../ja-JP/bindgen.md) · [ko-KR](../ko-KR/bindgen.md) · [zh-CN](bindgen.md) · [zh-TW](../zh-TW/bindgen.md)
