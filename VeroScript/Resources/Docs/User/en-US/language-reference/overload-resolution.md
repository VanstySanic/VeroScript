# Overload resolution

When multiple functions, methods, or operators share a name in one scope, the compiler selects one callable candidate from the call context.

## Candidate functions

Overload resolution first collects candidates by name and call form, then checks:

- whether the argument count matches;
- whether argument types satisfy the parameter conversion rules;
- whether `ref`, `const ref`, or `&` markers match the declaration;
- whether generic type arguments can be inferred and all `where` constraints hold;
- whether default arguments can fill omitted trailing positions.

Return type is normally not sufficient to distinguish same-name overloads:

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

When arguments are supplied, the compiler selects the best matching candidate using their types and available conversions. If no candidate matches or multiple candidates match equally, compilation reports a diagnostic.

Implicit conversions are considered during candidate matching, including compatible numeric conversions and non-`explicit` single-value converting constructors. An `explicit` constructor is not a candidate for an ordinary assignment or argument conversion; request it with direct construction or `cast<T>`. The compiler does not chain multiple user-defined conversions.

## Reference parameters

Reference markers are part of parameter matching and must be written at the call site:

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

Do not omit the reference marker or use a read-only reference as writable. Explicit markers make reference overloads unambiguous; `ref T` and `ref expression` remain supported compatibility spellings.

## Generic overloads

A generic candidate is viable only when its type arguments can be determined, its constraints hold, and its parameters can accept the arguments:

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

Provide explicit type arguments or adjust the parameters when inference is ambiguous.

## Operator overloads

Operator overloads follow the same candidate-matching rules. A member operator includes the implicit current object, while a non-member operator uses explicit operands; its parameter and return types must satisfy the operator's implementation rules. See [Operators](../language-basics/operators.md) for declarations.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](overload-resolution.md) · [fr-FR](../../fr-FR/language-reference/overload-resolution.md) · [ja-JP](../../ja-JP/language-reference/overload-resolution.md) · [ko-KR](../../ko-KR/language-reference/overload-resolution.md) · [zh-CN](../../zh-CN/language-reference/overload-resolution.md) · [zh-TW](../../zh-TW/language-reference/overload-resolution.md)
