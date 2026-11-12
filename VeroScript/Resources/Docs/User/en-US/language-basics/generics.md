# Generics

Generics let a type or function use type parameters in its declaration and substitute concrete types at use sites. VeroScript supports generic structs, classes, interfaces, and functions, together with `where` type constraints.

## Generic types

Declare type parameters in angle brackets after the type name:

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

Trailing type parameters can have default types, and a default can refer to an earlier type parameter. Explicit type arguments form a prefix; later arguments can be deduced from context or call arguments.

## Generic functions

Declare function type parameters after the function name:

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

When the arguments provide enough information, the type arguments can be omitted. Supply them explicitly when a particular type must be selected.

`template<T>` is another readable spelling for a generic declaration. It follows the same generic rules as a type or function name followed by `<T>`:

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## Type constraints

Use `where` to require an interface contract from a type parameter:

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

A type parameter can have multiple interface constraints:

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

Constraints are checked at compile time. A concrete type used to call a generic function or instantiate a generic type must satisfy every declared constraint.

An interface type alias or alias chain can be used as a constraint: after `using Readable = IReadable;`, write `where T : Readable`. Aliases to concrete generic interfaces such as `IBox<int>` are also supported. The alias is resolved in the declaration's module, and the concrete argument must still implement the resulting interface.

## Generic members and interfaces

A generic type can inherit an interface or a generic base type, and an interface can itself have type parameters:

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

Members of a generic declaration still follow the normal rules for access control, virtual methods, overloads, and static members. Generic static members have independent storage for each concrete type; see [Static members](static-members.md).

## Boundaries for type parameters

A type parameter can be used only with operations that the compiler can establish from its declaration. Add a `where` constraint before calling an interface method or accessing a constrained member; do not rely on undeclared implicit members.

Qualified static access such as `T::member` is checked after substituting the concrete type: that type must supply the matching accessible static field or method. Access, overload and const/reference rules still apply. See [Static members](static-members.md) for self types, aliases and field-default examples.

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

## Method templates and value parameters

`T`, `typename T`, and `class T` declare type parameters. Instance methods, static methods, and nonvirtual interface helpers with bodies can own template parameters. Inner parameters cannot shadow enclosing parameters. Method templates cannot be virtual, override, or final.

```VeroScript
template<typename T>
class Box
{
public:
	template<class U = T>
	U echo(U value)
	{
		return value;
	}
};

template<typename T, int Count = 4>
struct Buffer
{
	T values[Count];
	int capacity() const { return Count; }
};
```

Value parameters accept bool, fixed-width integers, and script or registered host enums. Their type may depend on an earlier type parameter: `template<typename T, T Value>`. Values are immutable compile-time constants. Pure scalar expressions, checked casts, enum members, and evaluable const initializers are supported; qualified [constexpr functions](../language-reference/constexpr.md) can be evaluated; other functions and host callbacks are not executed. Overflow, division by zero, out-of-range values, and mismatched enum identities are errors. Fixed array extents are checked after substitution.

After the first default, all remaining own parameters need defaults. Defaults bind in the declaration context and may refer to enclosing or earlier parameters. Calls apply explicit prefix arguments, infer from argument type structure, then fill defaults. Runtime argument values do not deduce value parameters.

`box.echo<string>("x")` explicitly selects templates; `box.echo("x")` allows inference. `f<>()` considers only templates. Conversion quality is compared first; ordinary candidates win equal conversions. Further ordering uses provable structural specialization, otherwise the call is ambiguous. Type lookup prefers exact own total arity before default completion; multiple default-completable declarations are ambiguous.

`identity<int>`, `box.echo<string>`, and static method references form callables; an explicit `fn(...) -> ...` target signature can assist inference. Dynamic string names never instantiate open method templates. Out-of-class definitions use a template header for each generic owner and for the method, mapping parameters by position without repeating defaults.

See `method_templates.vs`, `value_template_parameters.vs`, and `template_methods_main.vs`. These features use Core VM/Mixed/JIT or Core AOT; independent source exporters diagnose unsupported templates before output. Compiled libraries retain open declarations for new consumer arguments without original sources. Reflecting open declarations does not instantiate executable bodies.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](generics.md) · [fr-FR](../../fr-FR/language-basics/generics.md) · [ja-JP](../../ja-JP/language-basics/generics.md) · [ko-KR](../../ko-KR/language-basics/generics.md) · [zh-CN](../../zh-CN/language-basics/generics.md) · [zh-TW](../../zh-TW/language-basics/generics.md)
