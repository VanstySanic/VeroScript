# Scope

Scope determines where a name is valid in source code. Common VeroScript scopes include module, namespace, function, block, and member scope.

## Block scope

Braces inside a function or control-flow statement create a block. A local variable is valid from its declaration to the end of its enclosing block:

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

An inner block can access names already declared in an outer block. An outer block cannot access local variables declared in an inner block.

## Function scope

Function parameters and local variables belong to the function's scope. Different functions can use the same local name; each declaration is separate:

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## Module scope

Variables, functions, and types declared at the top level belong to the current module. Declarations that are not exported are normally available only inside the module; exported declarations can be used by other modules according to the import rules.

## Namespace scope

A namespace places declarations in a separate name range:

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

Namespace members can be accessed with `.` or `::`. A namespace does not replace a module: modules organize file and package imports, while namespaces organize declaration names.

### Root-qualified names

A leading `::` starts lookup at the global/root scope:

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

`::GlobalValue` does not fall back to the current namespace, a local type alias, or a `using namespace` directive. Use this form when the declaration at the root is intentional. The same root qualification can be used when naming a root-level type or namespace, and `using namespace ::Name` is valid.

## Member scope

Fields and methods in a struct, class, or interface belong to the type's member scope. Access is restricted by `public`, `protected`, and `private`:

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

Member functions can use `this` to access the current object. Static members do not depend on a particular object instance and have different access rules.

## Name hiding

An inner scope may declare the same name as an outer scope. The inner declaration then hides the outer declaration. Avoid unnecessary shadowing because it makes name lookup harder to read.


## Nested and local types

Types may declare nested class, struct, interface, enum and type aliases. Use `Outer::Inner`; each generic segment supplies its own arguments, for example `Outer<int>::Inner<string>`. A public nested type is available through an exported outer type. Private/protected paths and public aliases use ordinary access checks.

Executable blocks may declare local types. Their names are visible from the declaration to the end of the block; another block may shadow them. Local types can use visible types, aliases, enum constants and generic parameters. They do not capture enclosing variables or an outer `this`. Pass an object explicitly to access its instance members. Static members retain one store per concrete type and execution instance.

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

Nonlocal nested methods can be defined using `T Outer<T>::Inner<U>::read() { ... }`, including an imported source file that supplies the matching definition. Local types keep their method bodies within the local declaration or processor-generated body. Independent forward declarations, reopening a type body and definitions of local methods from another file are unsupported.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](scope.md) · [fr-FR](../../fr-FR/language-basics/scope.md) · [ja-JP](../../ja-JP/language-basics/scope.md) · [ko-KR](../../ko-KR/language-basics/scope.md) · [zh-CN](../../zh-CN/language-basics/scope.md) · [zh-TW](../../zh-TW/language-basics/scope.md)
