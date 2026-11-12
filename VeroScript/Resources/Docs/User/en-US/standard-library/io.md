# I/O module

Module file: `io.vs`

Import it with:

```VeroScript
import "io.vs";
```

## Files and directories

| Function | Description |
| --- | --- |
| `std::io::read_file(string path)` | Reads a complete text file. |
| `std::io::write_file(string path, string content)` | Overwrites a text file. |
| `std::io::append_file(string path, string content)` | Appends text and creates the file when missing. |
| `std::io::read_bytes(string path)` | Reads the complete file into an owned byte buffer. |
| `std::io::read_bytes(string path, int64 max_bytes)` | Reads bytes with an explicit maximum; excess input is an error. |
| `std::io::write_bytes(string path, region source)` | Overwrites with bytes from a buffer or view. |
| `std::io::append_bytes(string path, region source)` | Appends bytes from a buffer or view. |
| `std::filesystem::exists(string path)` | Tests whether a path exists. |
| `std::filesystem::delete_file(string path)` | Deletes a file. |
| `std::filesystem::is_file(string path)` | Tests whether the path resolves to a regular file. |
| `std::filesystem::is_directory(string path)` | Tests whether the path resolves to a directory. |
| `std::filesystem::remove_dir(string path)` | Removes one empty directory. |
| `std::filesystem::list_dir(string path)` | Returns an array of directory-entry names. |
| `std::filesystem::mkdir(string path)` | Creates a directory and its parents. |
| `std::filesystem::cwd()` | Returns the current working directory. |
| `std::filesystem::chdir(string path)` | Changes the current working directory. |

The accessible file-system scope depends on host permissions and the current working directory. A read, write, delete, directory-creation, or directory-change failure is reported at runtime.
`std::filesystem::exists` returns `false` only when the path is absent; inspection errors are reported. `std::filesystem::delete_file` accepts files and symbolic links but rejects actual directories. `std::filesystem::chdir` changes the host process working directory, affecting other sessions and host code. It requires both environment-write and process-directory-change permissions. Native C++ hosts set `FScriptCapabilities::bAllowProcessDirectoryChange`; C, C#, and Python hosts grant `PROCESS_DIRECTORY_CHANGE` through their script capability mask.

`std::filesystem::is_file` and `std::filesystem::is_directory` follow symbolic links, return `false` for missing paths, and require file-read permission. Other inspection errors are reported. `std::filesystem::remove_dir` requires file-delete permission, returns `false` for a missing path, and rejects files, nonempty directories, and directory symbolic links. It does not remove contents recursively.

## Standard input

| Function | Description |
| --- | --- |
| `std::io::read_line()` | Reads one line without its trailing newline; returns an empty string at EOF. |
| `std::io::try_read_line()` | Returns an empty optional only at normal EOF. |
| `std::io::try_read_token()` | Returns the next token, or an empty optional at normal EOF. |
| `std::io::pause()` | Waits for one input line; returns immediately at EOF in a non-interactive host. |
| `std::io::read_token()` | Reads the next whitespace-separated token. |
| `std::io::read_string()` | Reads a string token. |
| `std::io::read_int()` | Reads an integer token. |
| `std::io::read_double()` | Reads a floating-point token. |
| `std::io::read_bool()` | Reads a boolean token. |

Token input functions consume whitespace-separated values. Use `std::io::read_line` when the complete line matters and a typed `read_*` function when values should be read individually.


- [Standard library foundation](foundation.md)

`region` denotes the buffer, view and readonly_view overloads. File reads reject directories, including empty directories. Byte I/O limits, explicit-zero behavior and EOF contracts are specified in [the foundation reference](foundation.md#io-contract).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](io.md) · [fr-FR](../../fr-FR/standard-library/io.md) · [ja-JP](../../ja-JP/standard-library/io.md) · [ko-KR](../../ko-KR/standard-library/io.md) · [zh-CN](../../zh-CN/standard-library/io.md) · [zh-TW](../../zh-TW/standard-library/io.md)
