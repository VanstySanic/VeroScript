# constexpr 编译期常量

`constexpr` 要求编译期标量值，并隐含 `const`。适用于局部、模块及命名空间变量，以及显式 `static constexpr` 字段。

自由函数、命名空间函数和静态方法可以声明为 `constexpr`，包含受支持的泛型形式。按值参数和非 void 返回值限于 bool、固定位宽整数、char 和枚举。正文支持初始化的标量局部量、赋值、分支、循环、switch 及受限递归；使用运行时参数的普通调用仍按普通函数执行。

普通 if 的全部分支都要检查；if constexpr 只检查选中分支。编译期不能调用原生回调、读取可写全局状态、构造对象，或使用引用、浮点、字符串、容器、异常和异步任务。普通函数参数不能初始化显式 constexpr 局部变量。

常量初始化、值模板实参和默认项、固定数组长度、整数／枚举 case 标签及枚举值使用同一精确求值器。溢出、非法转换、除零和预算耗尽明确报错，不退回运行时初始化。短路求值不会执行未选中分支中的算术。

导出 constexpr 使用普通 import。通过核心编译和打包服务构建 source-free `.vslib`，保留定义环境及私有辅助声明依赖；消费库时不会重跑编译处理器。

宿主通过 `FCompileOptions::ConstantEvaluationLimits` 配置预算，直接使用编译器时可调用 `SetConstantEvaluationLimits`。默认总工作量 1,000,000、调用深度 64、表达式／原生递归深度 256、临时存储 16 MiB、缓存 8 MiB。限额必须为正，缓存容量零表示禁用。分析取消和期限仍有效；VM 运行限额独立管理。

反射字段、方法、函数和全局描述符提供 `is_constexpr()`；`std::compiler` 声明视图提供 `is_constexpr`。查询只读取已编译的资格，不执行常量函数。Core AOT 消费相同字节码；独立源码导出器输出已检查的带类型字面量，并保留其既有泛型支持限制。

```VeroScript
constexpr int square(int value)
{
	return value * value;
}
constexpr int capacity = square(8);

int main()
{
	constexpr int count = capacity + 1;
	int input = 3;
	print(count);         // 65
	print(square(input)); // 9
	return 0;
}
```

[constexpr.vs](../../../../Scripts/examples/constexpr.vs)

独立源码导出保留既有支持范围：C++ 使用已确定的闭合数组长度；C#／Python 原有不支持的固定数组，以及具有实例相关常量的泛型，使用 Core AOT。函数默认实参继续受既有字面量／模板值表达式规则限制。

## if constexpr 编译期分支

条件必须是 bool、整数、char 或非 scoped 枚举常量；scoped 枚举需要显式转换。支持 constexpr 调用和模板值，不能使用普通运行时参数。每个模板实例独立选择；所有分支必须语法正确，只有选中分支参与类型检查、继续实例化、返回路径检查和 Lambda 捕获。选中分支具有自己的块作用域。普通函数也采用相同规则，普通 if 仍检查两侧。

```VeroScript
template<int N>
int sum()
{
	if constexpr (N == 0)
		return 0;
	else
		return N + sum<N - 1>();
}
```

constexpr 函数也支持此语句，但普通参数即使在编译期调用时有值，也不能用作条件。VM、JIT 和 Core AOT 消费相同的已选结果；实例相关泛型的独立源码导出继续使用 Core AOT。

局部非 scoped 枚举的裸成员名也可用作常量条件；名称遮蔽与普通词法作用域一致。延迟求值的 const 初始化式保留声明时环境，不受后续同名声明影响。Lambda 使用外层枚举常量无需捕获运行时存储；模板内局部枚举按具体实例求值。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-reference/constexpr.md) · [fr-FR](../../fr-FR/language-reference/constexpr.md) · [ja-JP](../../ja-JP/language-reference/constexpr.md) · [ko-KR](../../ko-KR/language-reference/constexpr.md) · [zh-CN](constexpr.md) · [zh-TW](../../zh-TW/language-reference/constexpr.md)
