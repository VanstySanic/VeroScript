# variant — 动态值盒

variant 是核心类型，无需 import。std::variant 和 variant 指向同一类型。它保存一个带规范源类型的值，或处于空状态。

明确的 variant 目标位置允许具体值隐式装箱。读取必须显式 cast<T> 或 try_cast<T>；variant_is<T> 精确判断存储类型，不做转换。`optional<T>` 和 `try_cast` 同样由编译器提供，无需导入标准库。空盒的 `cast<T>` 报错；空盒或普通转换不匹配的 `try_cast<T>` 返回空 optional。复制失败、取消和限额仍然报错。

复制 variant 共享不可变盒。脚本 struct 在装箱和提取时按值快照；class 和容器保持原有引用语义。保存静态声明类型，包括基类类型和有类型的 null。后者仍为有值，与空盒不同；裸 nullptr 必须先存入明确类型的变量。

只定义 variant 与 variant 的相等比较：规范类型和原载荷相等规则都必须匹配。没有动态算术、成员查找、调用、隐式拆箱、排序或哈希。禁止 variant 键、channel 传递，以及含 variant 的 worker 参数、结果、捕获或全局状态；worker 内部局部使用允许。原生数组及普通 array/map 的值可以包含 variant。

```vero
import "core.vs";
variant empty;
variant answer = 42;
assert(!variant_has_value(empty));
assert(variant_is<int>(answer));
int exact = cast<int>(answer);
optional<double> converted = try_cast<double>(answer);
map<string, variant> properties = map<string, variant>();
properties["width"] = answer;
properties["text"] = "Save";
assert(cast<int>(properties["width"]) == exact);
```

VM 完整支持语义。Mixed／asmjit 对不支持的 variant 指令安全回退 VM。可移植字节码和包保留声明及指令。独立 C++、C#、Python AOT 在生成输出前明确拒绝 variant 依赖。调试器提供只读 $present、$type、$value 子项；type<variant>().kind() 返回 "variant"。

## SDK

参阅[Interop SDK — variant](../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#exported-value-lifetime)。

导出对象跨宿主调用存活，且方法或析构需要脚本全局状态时，使用既有持久化会话模式：C++ SetHotReloadEnabled(true)、C# HotReloadEnabled 或 Python hot_reload_enabled=True。普通临时执行在调用结束时释放全局状态。

```text
C:      vs_interop_value_set_variant_empty / vs_interop_value_set_variant
        vs_interop_variant_has_value / vs_interop_variant_get_type_name / vs_interop_variant_get
C#:     value.SetVariantEmpty(); session.CreateVariant(typeName, payload);
        value.VariantHasValue; value.VariantTypeName; value.GetVariantValue();
Python: Value.empty_variant(); session.variant(type_name, payload_value);
        value.set_variant_empty(); value.assign_variant(other_box);
        value.variant_has_value(); value.variant_type_name(); value.variant_get();
C++:    VeroScriptVariant.h, VeroScriptValueRoot.h
        session.MakeVariant(typeName, payload, output, error);
        session.RetainValue(output, root, error); root.Read(); root.Reset();
```

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/variant.md) · [fr-FR](../../fr-FR/standard-library/variant.md) · [ja-JP](../../ja-JP/standard-library/variant.md) · [ko-KR](../../ko-KR/standard-library/variant.md) · [zh-CN](variant.md) · [zh-TW](../../zh-TW/standard-library/variant.md)
