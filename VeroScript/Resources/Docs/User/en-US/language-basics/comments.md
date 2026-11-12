# Comments

Comments add notes to a source file. VeroScript supports single-line comments and block comments.

## Single-line comments

A single-line comment starts with `//` and ends at the end of the current line:

```VeroScript
// Explain the purpose of the variable below.
int retry_count = 3;

int value = 1; // A comment may follow a statement.
```

## Block comments

A block comment starts with `/*` and ends with `*/`. It may span multiple lines:

```VeroScript
/*
	This note applies to the following code block.
*/
int result = 0;
```

Comments are not executable statements. An unterminated block comment is a lexical error. The host may retain all comments, documentation comments only, or no comments; source metadata can therefore expose comments and documentation. Activated compile extensions may use retained metadata when generating code. Annotation metadata is independent from comment retention.

## Source documentation for editor tools

Tooling hover reads contiguous comments immediately before a source declaration. Source-defined preprocessor macro completion items can also carry their preceding comments; host completion documentation comes from binding metadata. Use `//` (including `///`) or a `/* ... */` block, with no blank line between the comment and declaration. This is a Tooling convention; comments remain ordinary lexical comments to the compiler.

`@param name text` documents a parameter. `@return text` and `@returns text` document the return value. Hover renders these tags with labels from the active language catalog. Unknown tags remain visible as ordinary comment text. Macro completion documentation preserves the comment text as written.

```VeroScript
// Adds two integers.
// @param left First addend.
// @param right Second addend.
// @return The sum.
int Add(int left, int right)
{
	return left + right;
}
```

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](comments.md) · [fr-FR](../../fr-FR/language-basics/comments.md) · [ja-JP](../../ja-JP/language-basics/comments.md) · [ko-KR](../../ko-KR/language-basics/comments.md) · [zh-CN](../../zh-CN/language-basics/comments.md) · [zh-TW](../../zh-TW/language-basics/comments.md)
