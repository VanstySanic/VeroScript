# Variables

A variable is a named declaration with a type that stores a value at runtime. Variables can be declared at module scope, inside functions or blocks, and as struct or class members.

## Declarations

A variable declaration specifies a type, a name and an initializer:

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

Local and module-level variables require initialization; `int count;` is invalid. Use an explicit value, constructor call or brace initializer. Struct and class fields may omit an initializer and then use their type’s field-default rules.

## Initialization

VeroScript supports constructor calls and brace initialization:

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

User-defined types can be initialized by calling a constructor:

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## Assignment

After declaration, a variable can be modified with an assignment expression:

```VeroScript
int value = 1;
value = 2;
value += 3;
```

The two sides of an assignment must satisfy the language's conversion rules. Assignment does not change the variable's static type.

## Local and global variables

A variable declared in a function or block is local. A variable declared at module scope is global and can be accessed by functions in the same module:

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

Access to a global across modules is subject to import and export rules. Scope describes where a name is valid; visibility is also affected by module boundaries and access specifiers.

## `auto` variables

`auto` infers the type from the initializer:

```VeroScript
auto answer = 42;
auto text = "hello";
```

An `auto` variable must be initialized. Once inferred, it remains a statically typed variable and cannot be assigned a different type later.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](variables.md) · [fr-FR](../../fr-FR/language-basics/variables.md) · [ja-JP](../../ja-JP/language-basics/variables.md) · [ko-KR](../../ko-KR/language-basics/variables.md) · [zh-CN](../../zh-CN/language-basics/variables.md) · [zh-TW](../../zh-TW/language-basics/variables.md)
