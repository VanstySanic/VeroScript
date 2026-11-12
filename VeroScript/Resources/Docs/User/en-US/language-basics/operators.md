# Operators

Operators calculate, compare, combine, or modify expression values. VeroScript supports arithmetic, comparison, logical, bitwise, assignment, indexing, member-access, and conditional operators.

## Operator categories

| Category | Operators |
| --- | --- |
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logical | `!`, `&&`, `||` |
| Bitwise | `&`, `|`, `^`, `<<`, `>>` |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` |
| Increment/decrement | `++`, `--` |
| Conditional | `condition ? when_true : when_false` |
| Access | `.`, `::`, `[]`, `()` |

## Precedence and associativity

The following levels are ordered from lowest to highest precedence:

| Level | Operators | Associativity |
| --- | --- | --- |
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | right |
| Conditional | `?:` | right |
| Logical OR | `||` | left |
| Logical AND | `&&` | left |
| Bitwise OR | `|` | left |
| Bitwise XOR | `^` | left |
| Bitwise AND | `&` | left |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` | left |
| Shift | `<<`, `>>` | left |
| Additive | `+`, `-` | left |
| Multiplicative | `*`, `/`, `%` | left |
| Prefix | `!`, `~`, unary `+`/`-`, prefix `++`/`--`, `new`, `delete`, casts and `ref` | right |
| Postfix/access | calls `()`, indexing `[]`, `.`, `::`, postfix `++`/`--` | left |

`&&` and `||` evaluate their left operand first and short-circuit. `?:` evaluates its condition first and then exactly one branch. Outside these guarantees, do not depend on the relative evaluation order of operands or function arguments when side effects matter; split such work into separate statements. Operator overloading never changes precedence or associativity. VeroScript has no general comma operator; comma-separated expressions are supported only in the initializer and iteration clauses of a classic `for`.

Operators have language-defined precedence and associativity. Use parentheses to make the calculation order explicit:

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## Arithmetic operators

`+`, `-`, `*`, `/`, and `%` perform numeric operations. `+` can also concatenate values of the same text type:

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string` and `wstring` cannot be concatenated directly with each other.

## Comparison and logical operators

Comparison operators produce boolean results. Logical operators combine boolean expressions:

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

## Assignment operators

`=` performs ordinary assignment. Compound assignment combines an operation with assignment:

```VeroScript
value = 10;
value += 2;
value *= 3;
```

Both sides of an assignment must satisfy the language's conversion rules. Assignment does not change the static type of the left-hand variable.

## Access operators

The indexing operator `[]` accesses an array or container element. The member-access operator `.` accesses an object member:

```VeroScript
int first = values[0];
point.x = 10;
```

Namespaces and type members may also use `::`:

```VeroScript
Color color = Color::Red;
```

Use `.` for ordinary value-member access and prefer `::` for namespace members, type members, enum members, and static members. A leading `::` has an additional meaning: it starts lookup at the global/root scope and does not fall back to the current namespace, local aliases, or `using namespace` directives. `.` and `::` can both appear in qualified-name paths, but they are not interchangeable when the leading root qualifier is significant.

## Operator overloading

User-defined types can declare operator methods so that the type participates in selected operations. An overload must satisfy the compiler's parameter and return-type rules; do not overload an operator with a meaning that is surprising for the type.

A member operator uses the current object as its implicit left operand. A non-member operator function can declare its operands explicitly:

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

Operator overloads can cover binary and unary operations, compound assignment, indexing, and function-call syntax. A type can also declare a conversion operator such as `bool operator bool()` so that an object can be converted explicitly to a boolean value:

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```

Built-in boolean `&&` and `||` always remain short-circuiting; user-defined overloads do not replace them. A user-defined operator candidate is considered only when at least one operand belongs to an overloadable user or host type, so an operator that would require converting two primitive operands is not selected.

Some comparison forms can obtain mirrored behavior from a declared base operator, such as using `operator<` to support the reverse direction. Prefer declaring operators that directly express the type's semantics, and keep overloaded operators consistent with their familiar meaning.

## References in operator overloads

Operator operands bind reference parameters implicitly, as in C++: `a + b`, `a[i]`, and `f(x)` do not require `&` or `ref`. Ordinary function and method calls still require their explicit reference argument markers. `T&` accepts writable storage of the same canonical type (after alias normalization); it rejects const objects, temporaries, and numeric conversions. `const T&` accepts read-only storage and converted temporaries.

A mutable lvalue prefers `T&` over `const T&` when the remaining conversions tie. By-value and reference overloads with indistinguishable conversions are ambiguous; the compiler rejects them.

An operator returning `T&` can be used as an assignment target or a local alias. A `const T&` result is read-only. Receiver constness and member access restrictions apply to overloaded operators too. Reference return lifetime checks still apply; `return this` refers to the current object handle.

Prefix increment/decrement uses no explicit member parameters; postfix uses a by-value `int` tag. A free prefix overload takes the operand, and a free postfix overload takes the operand plus the tag. An explicit overload determines its return value: a `void` compound assignment cannot be used as an object value, and a non-void result is not automatically assigned back to the target.

`operator()` accepts default arguments and the language's typed variadic parameters. Other operators cannot use either. Member `operator=` takes one parameter; free `operator=` is rejected. Declaration initialization keeps its existing initialization behavior.

Only `operator()` and `operator[]` may be declared `static`. Inherited operators use the generic arguments of their declaring type. Static fields and namespace globals can bind reference parameters; a field of a temporary struct cannot bind a mutable reference.

These rules extend VeroScript's existing operator model. Free `operator[]`/`operator()`, mirrored comparisons, and synthesized compound assignment remain language extensions. Class handles and struct values retain VeroScript semantics; this is not full C++ pointer, rvalue-reference, or move support.

The examples in this section target the VM, mixed and JIT execution backends. Source AOT exporters have separate capability limits: the new reference-operator combinations, assignment overloads, postfix tags and callable defaults/rest are not yet exported and are rejected explicitly.

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](operators.md) · [fr-FR](../../fr-FR/language-basics/operators.md) · [ja-JP](../../ja-JP/language-basics/operators.md) · [ko-KR](../../ko-KR/language-basics/operators.md) · [zh-CN](../../zh-CN/language-basics/operators.md) · [zh-TW](../../zh-TW/language-basics/operators.md)
