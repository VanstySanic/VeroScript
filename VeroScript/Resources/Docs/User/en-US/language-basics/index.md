# Language Basics

This section describes VeroScript's basic language rules by topic. Each page covers one relatively independent subject and uses small examples to define syntax and behavior. Complete APIs and runtime mechanisms are documented separately.

## Source, modules, and scope

- [Basic Syntax](basic-syntax.md): source files, statements, blocks, identifiers, and keywords.
- [Comments](comments.md): single-line and block comments.
- [Scope](scope.md): module, namespace, function, and block scopes.
- [Namespaces](namespaces.md): namespace declarations, qualified names, and `using namespace`.
- [The `std` namespace](standard-namespace.md): the complete list of built-in types, runtime functions, and compatibility aliases.
- [Modules](modules.md): module imports, exports, re-exports, and library-package imports.
- [Preprocessor](preprocessor.md): macros, conditional compilation, stringification, token pasting, and module inclusion.

## Values and types

- [Literals](literals.md): integers, floating-point values, strings, wide strings, booleans, and null values.
- [Data Types](data-types.md): built-in types, fixed-width integers, containers, and type inference.
- [Value utility types](value-utility-types.md): optional values, weak references, shared struct handles, and map-entry pairs.
- [Variables](variables.md): declarations, initialization, assignment, and inferred variables.
- [Constants](constants.md): `const` variables and constant members.
- [Declarations](declarations.md): `using`, `typedef`, `= default`, and `= delete`.
- [Structs](structs.md): value types, fields, methods, construction, and struct copy semantics.
- [Classes](classes.md): shared identity, member access, construction, inheritance, and virtual methods.
- [Interfaces](interfaces.md): method contracts, default implementations, interface inheritance, and conversions.
- [Enums](enums.md): enumerators, scoping, underlying integer types, and enum values.
- [Arrays](arrays.md): dynamic arrays, fixed-size native arrays, indexing, and range iteration.
- [Containers](containers.md): maps, sets, queues, stacks, deques, and channels.
- [Strings](strings.md): `string`, `wstring`, string operations, and standard-library helpers.
- [References](references.md): reference parameters, read-only references, and reference range iteration.
- [Reflection](reflection.md): `type<T>`, field and method information, enum information, and declaration metadata.
- [Static Members](static-members.md): static utility types, type-level fields, static methods, and independent storage in generic types.

## Expressions and operators

- [Operators](operators.md): arithmetic, comparison, logical, bitwise, assignment, and operator overloading.

## Generics, functions, and control flow

- [Generics](generics.md): generic types, generic functions, default type arguments, and `where` constraints.
- [Functions](functions.md): declarations, parameters, return values, defaults, and overloads.
- [Lambdas and Function Values](lambdas.md): function types, lambda expressions, captures, and reference parameters.
- [Control Flow](control-flow.md): conditions, loops, `switch`, jumps, and returns.

[Annotations and source metadata](annotations.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../../fr-FR/language-basics/index.md) · [ja-JP](../../ja-JP/language-basics/index.md) · [ko-KR](../../ko-KR/language-basics/index.md) · [zh-CN](../../zh-CN/language-basics/index.md) · [zh-TW](../../zh-TW/language-basics/index.md)
