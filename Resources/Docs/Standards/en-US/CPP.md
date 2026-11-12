# C++ coding standards

Scope: [VeroScript project development and contributions](README.md).

This file is the current authoritative C++ standard. Unless explicitly marked **Recommended** or **As needed**, its formatting and structural rules are mandatory.

## 1. Indentation, scopes, and braces

- Use one Tab per logical nesting level. Namespaces, types, methods, conditions, loops, lambdas, and local scopes must each add one indentation level.
- Opening braces must occupy their own line and align with the declaration or control statement they belong to:

  ```cpp
  void Process(const FRequest& Request)
  {
  	if (Request.bEnabled)
  	{
  		Execute(Request);
  	}
  }
  ```

- Blank lines immediately after opening braces or before closing braces are not mandatory; use blank lines only to separate logical blocks.
- Closing braces align with their opening braces. Place `else`, `else if`, `catch`, and the trailing `while` of `do` on a new line aligned with the corresponding closing brace; each clause opening brace also occupies its own line:

  ```cpp
  if (bPrimary)
  {
  	HandlePrimary();
  }
  else
  {
  	HandleFallback();
  }
  ```

- Every control branch with an execution body must use braces and an independent scope, including `if`, `else`, `for`, `while`, `do`, range `for`, and each `switch case`.
- The only execution branch allowed to omit braces is a **single-line guard whose body consists solely of one direct `return`**:

  ```cpp
  if (!bInitialized) return;
  ```

This exception applies only to `return;` or `return <expression>;`. It must not be combined with an `else` chain, cleanup, logging, assignment, macro conditions, or nested logic. `break`, `continue`, `throw`, `co_return`, and branches with multiple statements are not exceptions and require a braced scope.

## 2. Overall type layout

Arrange type contents in the following order. Do not emit empty blocks where there is no content.

1. Relationships and aliases such as `friend`, `using`, and `typedef`, as needed at the top;
2. required macros such as `DECLARE_XXX`;
3. access regions in the fixed order `public`, `protected`, `private`;
4. inherited `override` methods first within each access region;
5. nested types;
6. methods defined by this type;
7. member variables last.

Recommended skeleton:

```cpp
class FExample : public IBase, protected ISecondary
{
	friend class FExampleFactory;
	using FBase = IBase;

	DECLARE_EXAMPLE_TYPE(FExample)

public:
	//~ Begin IBase Interface
	virtual void Initialize() override;
	virtual void Shutdown() override;
	//~ End IBase Interface

	enum class EState
	{
		Idle,
		Running,
	};

	struct FOptions
	{
		bool bEnabled = false;
	};

	void Start(const FOptions& Options);

protected:
	//~ Begin ISecondary Interface
	virtual bool CanStart() const override;
	//~ End ISecondary Interface

	void NotifyStarted();

private:
	void ResetState();

	EState State = EState::Idle;
	FOptions Options;
};
```

### 2.1 Access regions

- Access regions must follow `public`, `protected`, `private`, in that order.
- Keep each access level in one contiguous region where possible. Repeat access specifiers only when language rules, macro expansion, or a clear semantic boundary requires it; repetitions must not disrupt the overall order.
- Place member variables at the end of their actual access region. Do not mix data among methods or nested types.

### 2.2 Inherited methods

- Within each access region, place `override` methods before newly defined methods of this type.
- With multiple bases, group methods in the left-to-right order of the inheritance list. Within a base, follow its interface declaration order.
- Use paired markers when an inherited group has multiple methods, its base is not obvious, or adjacency to this type's methods is confusing:

  ```cpp
  //~ Begin ISerializable Interface
  virtual bool Serialize(FArchive& Archive) override;
  virtual bool Deserialize(FArchive& Archive) override;
  //~ End ISerializable Interface
  ```

- `Begin` and `End` must be paired with exactly matching names, using the direct base or interface name. Omit markers as needed for one obvious `override`; do not add them mechanically to every function.

### 2.3 Nested types

Within an access region, nested types have this fixed priority:

1. `enum`, `enum class`;
2. `struct`;
3. other type declarations, such as `union`, local aliases, or function-object types;
4. `class`.

- Separate nested-type groups with one blank line. Order types within a category by dependency and readable naming.
- Ordinary `using`/`typedef` declarations that belong at the type's top must not be left in the nested-type region without reason. Place an alias in the “other type declarations” position only when it depends on a preceding nested type.
- Prefer forward declarations to resolve complete-definition dependencies that conflict with this order. If departure is unavoidable, explain the complete-type dependency nearby.

## 3. Headers, forward declarations, and implementation files

### 3.1 Headers and implementation files as needed

- Every owned C++ `.h` begins with the two-line SPDX copyright and license header, a blank line, `#pragma once`, another blank line, and then includes or other declarations:

  ```cpp
  // SPDX-FileCopyrightText: <copyright-holder>
  // SPDX-License-Identifier: MIT

  #pragma once

  #include <cstdint>
  ```

- An `.inl` is an included implementation fragment, not an `.h`. It also begins with the two-line SPDX copyright and license header and a blank line, but by default has no `#pragma once` and requires no paired `.cpp`. Use `#pragma once` as for an `.h` only after confirming the fragment is header-like content that may be included only once per translation unit.
- An `.h` does not require a same-stem `.cpp`. Organize a `.cpp` by responsibility only when implementation or build duties require a separate translation unit; do not create empty files for file counts or directory symmetry.
- Headers containing only declarations, type definitions, constants, templates, or `inline` definitions may stand alone. Keep definitions that require header visibility there rather than moving them for file separation.
- When a `.cpp` is needed, place it in the same module’s implementation area and explicitly include the headers it implements. Without `Public/` and `Private/` layers the files normally share a directory; with those layers use a suitable semantic subdirectory of `Private/`, without requiring paths to mirror `Public/`.
- An implementation-only `.cpp` may have no same-stem `.h`.
- When removing an existing empty `.cpp`, check its actual build, registration, explicit-instantiation, and platform duties, and update build references together.
- Third-party and generated files retain their applicable file organization.

### 3.2 Declaration and definition order

- Define methods in `.cpp` in the declaration order of the `.h` when they are declared there and require an out-of-line implementation.
- Preserve groups too: inherited-interface implementations and this type's public/protected/private methods must retain their relative declaration order.
- Anonymous-namespace helpers, file-local constants, and implementation-only types may precede method definitions; they do not change the relative order of definitions corresponding to header declarations.
- When adding a declaration, check its `.cpp` definition position instead of appending every implementation to the end.

### 3.3 Forward declarations and multiple top-level types

- Order all forward declarations by `enum` → `struct` → other declarations → `class`.
- Enum forward declarations must use a valid opaque-enum form: `enum class EName;`, or an explicitly based form such as `enum EName : std::uint8_t;`. Do not declare an unscoped `enum EName;` without an underlying type.
- A header may define multiple top-level types. Where complete-type dependencies permit, use the same order: `enum` → `struct` → other declarations → `class`.
- “Other type declarations” include `union`, `using`/`typedef` aliases, and function-type aliases. Class templates follow `class`, struct templates follow `struct`; do not categorize every `template` declaration as “other”.
- If complete-type dependencies require another order, try forward declarations first. If unavoidable, prioritize dependencies and explain briefly nearby.

### 3.4 Member defaults and Boolean bit-fields

- When a member needs an initial value and its default state can be expressed directly and clearly at the declaration, provide the default there.
- Values depending on constructor arguments, runtime state, or the initialization of other members belong in the constructor initializer list. Do not write a misleading fixed default to satisfy this rule.
- Prefer a 1-bit field for Boolean switches suitable for compact storage that do not require taking an address, references, atomic operations, or a stable external binary layout. Examples: `uint8 bEnabled : 1;` or `std::uint8_t bEnabled : 1;`. The width is `: 1`; `uint8` is the storage type, not the width.
- The current project minimum is C++17. C++17 bit-fields cannot use in-class default member initializers. Initialize fields that require a nonzero or explicit state in the constructor initializer list. Do not write `uint8 bEnabled : 1 = true;` until the project adopts a language standard that supports it.

## 4. `switch` and execution branches

- Every `switch` must include `default`.
- `case` and `default` align at the `switch` contents level. Indent their execution-body braces one further level and the statements another level:

```cpp
switch (Kind)
{
case EKind::First:
	{
		HandleFirst();
		break;
	}
case EKind::Second:
	{
		HandleSecond();
		break;
	}
default:
	{
		HandleUnknown();
		break;
	}
}
```

- A `case` whose sole execution path directly ends the current method with `return` or `return <expression>` may use the single-line guard exception in section 1. Do not mix logging, cleanup, local declarations, conditions, or other statements into it. All other `case` and `default` bodies require the independent braced scope above.
- Even a `default` intentionally performing no business action must retain a braced scope and explicit `break`. Add a short English explanation only when the reason for doing nothing is not apparent from context.
- Do not rely on implicit fallthrough. Use a project-approved explicit marker and explain intentional fallthrough.
- All execution branches use braces except the strictly defined direct-`return` guard exception in section 1.

## 5. Preprocessor macros

- Indent `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, and `#endif` at the **same logical level** as the code they wrap. Preprocessor conditions do not add an indentation level.
- Code inside a macro condition retains its original nesting and readability when the condition is removed:

  ```cpp
  void Initialize()
  {
  	#if WITH_DIAGNOSTICS
  	RegisterDiagnostics();
  	#endif

  	StartRuntime();
  }
  ```

- Conditional macros must cover whole semantic blocks where possible; do not split one declaration or expression into hard-to-read fragments.
- Paired macros must have the same indentation. Add a short condition comment to `#endif` as needed for longer blocks.
- Place macro-generated declarations according to the type layout in this file.

## 6. Comments and regions

- Use `//` for a single-line explanation immediately above the declaration or block. Short trailing comments are only appropriate when they do not impair readability.
- Use `/** ... */` documentation comments for multiline API semantics, ownership, threading, failure conditions, or complex invariants.
- Do not repeat an obvious name, such as `// Gets the name` before `GetName()`.
- When a member needs explanation, prioritize units, ownership, validity, thread constraints, cache invalidation, or relationships to other fields.
- `//~ Begin <Name> Interface` / `//~ End <Name> Interface` are exclusively for inherited-interface groups, not ordinary method-section headings.
- Other large groups may have short explanatory comments, but prefer the fixed layout to make structure self-evident.

## 7. Other recommendations

- No include order or grouping is currently specified for `.cpp` or `.h`. Follow local readability and build constraints. Forward-declaration layout still follows section 3.3.
- Use `nullptr` for null pointers and explicit conversions where information may be lost.
- State resource ownership, nullable objects, and cross-thread callbacks clearly in type or method comments.
- Express empty functions, default implementations, and deliberately unused parameters as briefly and readably as possible. Do not create meaningless locals or empty scopes merely to satisfy a formality.

## 8. Topics for the maintainer

The following have no mandatory rule yet: naming details, templates/concepts, exception policy, smart-pointer policy, test frameworks, formatting tools, and line width. The maintainer will define them later. Include order is explicitly unspecified at present.
