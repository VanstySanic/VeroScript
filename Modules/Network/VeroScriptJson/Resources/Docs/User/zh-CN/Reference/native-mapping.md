# C++ 类型映射

`VeroScriptJsonMapping.h` 提供原生 C++17 类型映射。链接 `VeroScript::Json` 即可使用，不需要 VM。
此处描述 C++ 接口；脚本对象与注解参见[脚本映射](script-mapping.md)。各宿主和 AOT 后端继续分别验收。

## 字段描述与构造

```cpp
#include <VeroScriptJsonMapping.h>
using namespace VeroScript::Json;

struct Settings
{
	std::string Name;
	int Port = 8080;
};
FMappingRegistry registry;
FError error;
FFieldOptions required;
required.bRequired = true;
bool registered = TObjectMapping<Settings>("app::Settings")
	.Field("name", &Settings::Name, required)
	.Field("port", &Settings::Port)
	.Register(registry, error);

FJsonValue input;
std::shared_ptr<Settings> settings;
bool decoded = registered && TryParse("{\"name\":\"Vero\"}", input, error)
	&& TryDecode(input, settings, error, registry);
// Successful decoding preserves the constructed default port, 8080.
```

`TryEncode` 和 `TryDecode` 在成功时才替换输出。解码先检查字段并准备转换结果，再调用正常构造器和字段赋值。
非默认构造类型可向 `TObjectMapping` 传入返回 `shared_ptr<T>` 的工厂。框架不直接拼装对象布局。
构造器、属性和转换器的外部副作用不回滚；失败时原输出对象保持不变。

字段名支持合法 UTF-8 与 NUL；错误路径使用 RFC 6901 转义。
默认拒绝未知字段；`bIgnoreUnknownFields` 可显式允许未知字段。
`FFieldOptions` 分别控制 required、ignore、serialize 和 deserialize。
只输出字段在输入中出现会失败；ignore 字段允许出现但跳过。重名和矛盾方向在注册时失败。
基类字段须显式加入描述；静态成员与属性不会自动枚举。属性只有通过 `Property` 显式登记后才执行。

## 类型与转换器

- 布尔、至多 64 位的整数、有限浮点和拥有的 UTF-8 字符串保持 JSON 类型；整数不会经过 double。
- 浮点默认允许受检舍入；`bExactFloatingPoint` 要求精确可表示。
- vector、list、deque、固定数组映射为 array；固定数组读回检查长度。
- set 读回拒绝重复值，multiset 保留重复。输出遵循容器本身的迭代顺序。
- 唯一字符串键 map 默认映射为 object；其他 map/multimap 需显式设置 `bMapAsPairs`，使用 key/value 对数组。
- optional、shared_ptr 和 unique_ptr 接受 null；普通值不接受 null。
- `RegisterEnum` 默认使用已登记名字；`bEnumsAsNumbers` 显式选择底层整数，仍拒绝未登记值。
- 未登记的对象或特殊类型报告 Unsupported，不输出占位空对象。

`RegisterConverter<T>` 使用类型化编码、解码和 Schema 回调。
转换器在活动 `FMappingContext` 内递归调用 `Encode/Decode/Schema`，共享路径、预算和取消检查。
每次调用捕获不可变注册表快照；调用中替换注册项不改变当前调用。
注册表副本共享注册状态；每次转换仍分别取得自己的快照。
转换器可提供代码 `Owner`；构造结果和通过映射得到的嵌套 `shared_ptr` 均保留本次调用的转换器，确保其析构函数和转换器代码继续存活。取出嵌套对象后，可独立释放外层结果及注册表；转换器集合在一次调用内共享，不逐对象复制。
此保留关系随返回的 `shared_ptr` 及其拷贝、别名保持；自行按值移出对象或获取转换器内部的其他控制块时，调用方需自行保留代码所有者。
注册表的类型键借用 C++ type_info；提供插件类型时，Owner 也必须覆盖该类型的 RTTI。
替换转换器后，首次注册键的 Owner 会保留到注册表及其活动快照释放。
回调和工厂先于 Owner 析构；替换项的析构发生在注册表锁之外，允许析构时注册其他转换器。
新注册项插入失败时，也在解除锁后释放其最后一份回调／Owner 引用。
对象路径上的环报告 `json.cycle`；不同分支复用同一对象允许重复按值输出，不恢复共享引用身份。

## 输入和输出 Schema

带标签的基类/派生类转换使用 TPolymorphicMapping，见[多态映射](polymorphic-mapping.md)。

```cpp
FJsonValue input_schema, output_schema;
bool ok = TryMappingSchema<Settings>(EMappingDirection::Input, input_schema, error, registry)
	&& TryMappingSchema<Settings>(EMappingDirection::Output, output_schema, error, registry);
```

两种方向复用字段描述，区分 required、忽略字段、只输入／只输出字段与未知字段策略。
递归类型使用 `$defs` 和 `$ref`；整数范围、固定长度和枚举进入约束。
自定义转换器没有提供 Schema 时明确失败。
Schema 不能表达的构造器不变量、自定义容器比较规则、键值对数组的键唯一性和精确浮点转换仍由解码器检查。

映射使用 `FMappingOptions::Limits` 和借用的 `FWork`。
输入／输出字节预算按紧凑 JSON 表示核对；深度、节点、文本和数值另有限制。
异常转换为受检错误，保留首份错误和实例路径；取消不提交输出。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/native-mapping.md) · [fr-FR](../../fr-FR/Reference/native-mapping.md) · [ja-JP](../../ja-JP/Reference/native-mapping.md) · [ko-KR](../../ko-KR/Reference/native-mapping.md) · [zh-CN](native-mapping.md) · [zh-TW](../../zh-TW/Reference/native-mapping.md)
