# Type aliases and special declarations

VeroScript supports type aliases and uses `= default` or `= delete` to make the state of constructors, destructors, methods, operators, and free functions explicit.

## Type aliases

Use `using` to give an existing type another name:

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef` is also supported for type aliases:

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

A type alias does not create a new runtime type. An alias can be declared at module scope or in a local block; a local alias is visible only in its scope.

## Exporting aliases

Use `export using` or `export typedef` at module scope, including inside a namespace. Both use the `Alias = Type` spelling. An ordinary alias remains private to its module; a local alias remains local. Importers can use an exported alias with its namespace qualification. `export import` forwards exported aliases without exposing private aliases. `using namespace` affects lookup only in its containing module/scope; `export using namespace` is rejected.

Save this as aliases.vs:

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

Save the consumer as main.vs and run `veroscript main.vs`; it prints `42`. A facade containing `export import "aliases.vs";` can be imported instead. To distribute a binary library, run `veroscript --emit-package aliases.vslib aliases.vs`, then change the consumer import to `"aliases.vslib"`.

## Defaulted declarations

A constructor or destructor can use `= default` to request a compiler-generated implementation:

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

Default construction combines field defaults with the type's normal default rules. Writing `= default` makes the generated-implementation intent explicit and keeps the declaration visible in the type interface.

## Deleted declarations

Use `= delete` to keep a signature while explicitly prohibiting that call form:

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

Calling a deleted constructor, method, operator, or function produces a compile diagnostic. Deleted declarations are useful for rejecting unwanted implicit calls or incorrect overloads; they do not create an empty runtime function.

`= default` and `= delete` are part of the declaration and cannot be combined with a function body. Virtual functions and interface slots must still satisfy the inheritance and implementation rules.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](declarations.md) · [fr-FR](../../fr-FR/language-basics/declarations.md) · [ja-JP](../../ja-JP/language-basics/declarations.md) · [ko-KR](../../ko-KR/language-basics/declarations.md) · [zh-CN](../../zh-CN/language-basics/declarations.md) · [zh-TW](../../zh-TW/language-basics/declarations.md)
