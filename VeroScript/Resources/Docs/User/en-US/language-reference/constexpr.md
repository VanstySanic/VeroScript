# constexpr

`constexpr` requires a compile-time scalar value. It implies `const` and supports local, module and namespace variables, plus explicitly `static constexpr` fields.

Free functions, namespace functions and static methods can be `constexpr`, including supported templates. Parameters and the non-void return value must be bool, fixed-width integers, char or enums. Functions support initialized scalar locals, assignment, branches, loops, switch and bounded recursion. An ordinary call with runtime arguments remains an ordinary call.

All branches of ordinary if statements are checked; if constexpr checks only the selected branch. Compile-time execution cannot call native callbacks, access mutable global state, construct objects or use references, floating-point values, strings, containers, exceptions or asynchronous tasks. Ordinary function parameters cannot initialize an explicitly constexpr local.

Constant initializers, value template arguments/defaults, fixed array lengths, integer/enum case labels and enum values use the same exact evaluator. Overflow, invalid conversions, division by zero and exhausted budgets are errors; they do not fall back to runtime initialization. Short circuit evaluation skips arithmetic in unselected branches.

Exported constexpr declarations use normal imports. Build source-free `.vslib` files through the core compile/package services so the original definition context and private helper dependencies are retained. Loading such a library does not rerun compilation processors.

Hosts configure `FCompileOptions::ConstantEvaluationLimits`; direct compiler users can use `SetConstantEvaluationLimits`. Defaults are 1,000,000 work units, 64 calls, 256 expression/native depth, 16 MiB temporary storage and 8 MiB cache. Positive limits are required; cache size zero disables caching. Analysis cancellation and deadlines remain active. Runtime VM limits are separate.

`is_constexpr()` on reflection field, method, function and global descriptors queries the compiled qualifier. `std::compiler` declaration views expose `is_constexpr`. Queries do not execute constant functions. Core AOT uses the same bytecode; standalone exporters lower accepted constants to typed literals and retain their existing template restrictions.

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

Standalone exporters retain their existing feature boundaries. C++ receives resolved closed array extents; fixed arrays rejected by C# or Python and instance-dependent generic constants use Core AOT. Function defaults retain the existing literal/template-value restrictions.

## if constexpr branches

The condition must be a bool, integral, char, or unscoped enum constant; scoped enums require an explicit conversion. constexpr calls and template values are supported; ordinary runtime parameters are not constant conditions. Each template instance selects independently. Both branches must parse, but only the selected branch undergoes type checking, further instantiation, return-path analysis, and lambda capture analysis. It keeps its own block scope. Ordinary functions use the same rule; ordinary if still checks both branches.

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

constexpr functions also support this statement, but ordinary parameters cannot become its condition even during a constant call. VM, JIT, and Core AOT consume the same selected result. Instance-dependent generic source exports retain the existing Core AOT requirement.

Bare members of local unscoped enums are also constant conditions, following ordinary lexical shadowing. A lazily evaluated const initializer keeps its declaration environment, unaffected by later declarations. Lambdas can use enclosing enum constants without capturing runtime storage; local enums in templates are evaluated per concrete instance.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](constexpr.md) · [fr-FR](../../fr-FR/language-reference/constexpr.md) · [ja-JP](../../ja-JP/language-reference/constexpr.md) · [ko-KR](../../ko-KR/language-reference/constexpr.md) · [zh-CN](../../zh-CN/language-reference/constexpr.md) · [zh-TW](../../zh-TW/language-reference/constexpr.md)
