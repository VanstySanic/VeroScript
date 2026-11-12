// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// io.vs - VeroScript 0.1.1 standard file I/O

/** Only normal EOF produces an empty optional; an empty line is a present value. */
export optional<string> __std_try_read_line()
{
	return __try_read_line();
}
export optional<string> __std_try_read_token()
{
	return __try_read_token();
}

export std::memory::buffer __std_read_bytes(string path)
{
	return __read_bytes(path);
}
export std::memory::buffer __std_read_bytes(string path, int64 max_bytes)
{
	return __read_bytes(path, max_bytes);
}
export void __std_write_bytes(string path, std::memory::buffer source)
{
	__write_bytes(path, source);
}
export void __std_write_bytes(string path, std::memory::view source)
{
	__write_bytes(path, source);
}
export void __std_write_bytes(string path, std::memory::readonly_view source)
{
	__write_bytes(path, source);
}
export void __std_append_bytes(string path, std::memory::buffer source)
{
	__append_bytes(path, source);
}
export void __std_append_bytes(string path, std::memory::view source)
{
	__append_bytes(path, source);
}
export void __std_append_bytes(string path, std::memory::readonly_view source)
{
	__append_bytes(path, source);
}

export string __std_read_file(string path)
{
	return __read_file(path);
}

export void __std_write_file(string path, string content)
{
	__write_file(path, content);
}

export bool __std_exists(string path)
{
	return __exists(path);
}

export bool __std_is_file(string path)
{
	return __is_file(path);
}

export bool __std_is_directory(string path)
{
	return __is_directory(path);
}

// Delete one empty directory. This never recursively removes its contents.
export bool __std_remove_dir(string path)
{
	return __remove_dir(path);
}

export bool __std_delete_file(string path)
{
	return __delete_file(path);
}

// Returns entry names as array<string>.
export array<string> __std_list_dir(string path)
{
	return __list_dir(path);
}

// Read one line from stdin (no trailing newline; EOF → "").
export string __std_read_line()
{
	return __read_line();
}

// Wait until one input line is submitted (normally by pressing Enter).
// EOF returns immediately, so non-interactive hosts do not hang indefinitely.
export void __std_pause()
{
	__read_line();
}

// I/O token format 0.1.1: whitespace-separated tokens.
export string __std_read_token()
{
	return __read_token();
}

export string __std_read_string()
{
	return __read_string();
}

export int __std_read_int()
{
	return __read_int();
}

export double __std_read_double()
{
	return __read_double();
}

export bool __std_read_bool()
{
	return __read_bool();
}

// Create directory (and parents); fails at runtime if not creatable.
export void __std_mkdir(string path)
{
	__mkdir(path);
}

// Append text to a file (creates if missing).
export void __std_append_file(string path, string content)
{
	__append_file(path, content);
}

// Current working directory (generic separators).
export string __std_cwd()
{
	return __cwd();
}

// Change working directory; fails at runtime if not accessible.
export void __std_chdir(string path)
{
	__chdir(path);
}
