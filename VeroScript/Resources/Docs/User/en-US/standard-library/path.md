# Path module

Module file: `path.vs`

Import it with:

```VeroScript
import "path.vs";
```

## Functions

| Function | Description |
| --- | --- |
| `std::filesystem::basename(string path)` | Returns the name after the final path separator. |
| `std::filesystem::dirname(string path)` | Returns the directory before the final separator; returns `"."` when there is no separator. |
| `std::filesystem::join_path(string left, string right)` | Joins two path components with a separator. |
| `std::filesystem::extension(string path)` | Returns the extension including its dot, or an empty string when there is none. |
| `std::filesystem::change_extension(string path, string new_ext)` | Replaces or appends an extension. |

The path module recognizes both `/` and `\` as separators. `std::filesystem::join_path` handles an existing separator at the end of the left component and an empty component on either side.
`std::filesystem::dirname` preserves the separator of a Windows drive root: `std::filesystem::dirname("C:\\foo.txt")` returns `C:\`.

`std::filesystem::change_extension` accepts a new extension with or without a leading dot; `"json"` and `".json"` produce the same result. A leading dot in a file name is not treated as an extension by itself.
`std::filesystem::join_path` is lexical: an empty side returns the other side; a right side beginning with `/` or `\\`, or an ASCII drive prefix such as `C:`, replaces the left side. Otherwise it inserts `/` unless the left side already ends with a separator or is exactly a drive prefix (`C:` + `file` becomes `C:file`). It does not normalize `.` or `..` or inspect the file system.


- [Standard library foundation](foundation.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](path.md) · [fr-FR](../../fr-FR/standard-library/path.md) · [ja-JP](../../ja-JP/standard-library/path.md) · [ko-KR](../../ko-KR/standard-library/path.md) · [zh-CN](../../zh-CN/standard-library/path.md) · [zh-TW](../../zh-TW/standard-library/path.md)
