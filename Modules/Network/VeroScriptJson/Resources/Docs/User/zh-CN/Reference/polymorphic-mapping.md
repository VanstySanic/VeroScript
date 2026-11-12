# 封闭多态映射

原生和脚本映射通过明确登记的类型表选择派生类，统一使用外层对象：

```json
{"kind":"cat","value":{"lives":9}}
```

kind 是字符串标签，value 是派生类自身转换器的非 null 结果，根对象可以为 null。两个键名可以修改。
外层与派生类字段分开，避免标签与成员冲突，也可以复用输出字符串或数组的自定义转换器。
表包含 1–256 个具体派生类，类型和标签分别唯一。未登记的实际类型、未知标签、缺失或 null 载荷均报错。
不会将未登记子类降为某个已登记的基类输出。

## C++

```cpp
TObjectMapping<Cat> cat("cat");
cat.Field("lives", &Cat::Lives);
cat.Register(registry, error);
TObjectMapping<Dog> dog("dog");
dog.Field("name", &Dog::Name);
dog.Register(registry, error);
TPolymorphicMapping<Animal> animals("animal");
animals.Add<Cat>("cat").Add<Dog>("dog");
animals.Register(registry, error);
std::shared_ptr<Animal> source = std::make_shared<Cat>();
TryEncode(source, value, error, registry);
std::shared_ptr<Animal> result;
TryDecode(value, result, error, registry);
```

与普通原生映射一样，TryDecode<T> 的 shared_ptr<T> 输出是新构造 T 的持有者。
直接 TryDecode<Animal> 构造非 null Animal；需要把根 null 映射为可空值时，目标类型为
std::shared_ptr<Animal>，输出持有者相应为 std::shared_ptr<std::shared_ptr<Animal>>。
对象字段中的 shared_ptr<Animal> 直接按可空值处理；Schema 的目标类型应与实际转换目标保持一致。

示例省略错误分支，实际调用应检查 bool 和 error。Animal 必须有虚函数，可以是抽象类。
派生类型必须具体、公开且无歧义。先登记各派生类型自己的字段或自定义转换器，再登记基类表。
解码执行正常的派生类工厂，并核对工厂返回的实际类型。

构造函数第四个参数保留表提供方的代码 Owner，Add 的第二个参数保留派生类型提供方。
Owner 应覆盖使用的 RTTI、转换器及析构代码。解码结果的 shared_ptr 延续普通映射的代码保留规则；
释放注册表不会使仍持有的结果悬空。注册失败不替换旧表；转换调用使用不可变注册表快照。
表的复制、移动赋值和销毁均先释放旧的分派回调，再释放其代码 Owner。

## 脚本

```vero
@JsonConverter(type="AnimalJson")
@JsonFactory(type="AnimalJson")
class Animal
{
public:
    virtual string voice() = 0;
};
class Cat : public Animal
{
public:
    string voice() override
    {
        return "cat";
    }

    int lives = 9;
};
class Dog : public Animal
{
public:
    string voice() override
    {
        return "dog";
    }
};
@JsonPolymorphic(base="Animal", discriminator="kind", payload="value",
    variants=(cat="Cat", dog=(type="Dog",tag="good-dog")))
class AnimalJson
{
};

Animal source=Cat();
json::value data=json::converter<Animal>::write(ref source);
Animal copy=json::converter<Animal>::read(data);
```

注解标记非模板表类，生成普通类型化的静态 create/encode/decode/schema 方法。
表可以声明在模块、命名空间、类型或函数内；生成方法保留表的所有权和可见性。
表类不能带类型参数或值参数（包括捕获的外层参数）；数据类型的映射限制见[声明作用域](script-mapping.md#%E5%A3%B0%E6%98%8E%E4%BD%9C%E7%94%A8%E5%9F%9F)。
基类和派生类可以使用闭合泛型；类型名按表所在声明的作用域解析，普通别名按其声明的目标解析。
展开别名时优先使用所属模块的声明，并遵循导入与再导出可见性，避免误取其他模块的同名别名。
简写以表项名作为标签，扩展写法用 type/tag 支持 Unicode 或不适合作为标识符的标签。
未闭合泛型、非法继承与不可访问类型继续接受普通编译检查。

不能修改原始基类时，使用 json::custom_converter<Base,Table> 即可。
需要其他字段通过 json::converter<Base> 自动采用该表时，为基类添加 JsonConverter/JsonFactory。
导出的基类、派生类和表可跨源模块或 .vslib 使用；启用 JSON 处理器构建的库保留生成方法。
未启用该处理器的普通二进制库不会自动获得表方法，可在消费者声明自己的表。

完整可运行示例见 [polymorphic.vs](../../../../Examples/polymorphic.vs)。

## Schema 与失败语义

输入和输出 Schema 以 oneOf 列出标签和派生载荷，复用派生类 Schema，递归类型使用 $defs。
仅输入方向可以按 ignore_unknown 放行外层额外字段，输出 Schema 始终拒绝它们。
标签、载荷及派生字段错误保留相应 JSON Pointer，斜杠和波浪号正常转义。

失败不替换调用方原有结果，路径、节点、深度、取消和循环检测沿用同一上下文。
构造器和用户转换器的外部副作用不属于结果回滚范围。
共享对象按值重复输出，不恢复图身份；对象环报告 Cycle。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/polymorphic-mapping.md) · [fr-FR](../../fr-FR/Reference/polymorphic-mapping.md) · [ja-JP](../../ja-JP/Reference/polymorphic-mapping.md) · [ko-KR](../../ko-KR/Reference/polymorphic-mapping.md) · [zh-CN](polymorphic-mapping.md) · [zh-TW](../../zh-TW/Reference/polymorphic-mapping.md)
