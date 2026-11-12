// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// string.vs - VeroScript string helpers (v0.1.1)

export string __std_substring(string s, int64 start, int64 count)
{
	return __substr(s, start, count);
}

export int64 __std_find(string haystack, string needle)
{
	return __find(haystack, needle);
}

export int __std_char_code(string s, int64 index)
{
	return __char_code(s, index);
}

export bool __std_contains(string s, string sub)
{
	return __std_find(s, sub) >= 0;
}

export bool __std_starts_with(string s, string prefix)
{
	return __string_starts_with(s, prefix);
}

export bool __std_ends_with(string s, string suffix)
{
	return __string_ends_with(s, suffix);
}

export string __std_trim_left(string s)
{
	return __string_trim(s, true, false);
}

export string __std_trim_right(string s)
{
	return __string_trim(s, false, true);
}

export string __std_trim(string s)
{
	return __string_trim(s, true, true);
}

export array<string> __std_split(string s, string separator)
{
	return __split(s, separator);
}

export string __std_join(array<string> parts, string separator)
{
	return __join(parts, separator);
}

export string __std_to_lower(string s)
{
	return __to_lower(s);
}

export string __std_to_upper(string s)
{
	return __to_upper(s);
}

export string __std_replace(string s, string from, string to)
{
	return __string_replace(s, from, to);
}

/** Return the byte offset of the final occurrence, or -1 when absent.
 * String indices are deliberately UTF-8 byte offsets, matching substring,
 * find, and char_code.  This keeps indexing semantics deterministic. */
export int64 __std_last_find(string haystack, string needle)
{
	return __string_last_find(haystack, needle);
}

export string __std_repeat(string value, int64 count)
{
	return __string_repeat(value, count);
}

export string __std_remove_prefix(string value, string prefix)
{
	if (__std_starts_with(value, prefix))
	{
		return __std_substring(value, len(prefix), len(value) - len(prefix));
	}
	return value;
}

export string __std_remove_suffix(string value, string suffix)
{
	if (__std_ends_with(value, suffix))
	{
		return __std_substring(value, 0, len(value) - len(suffix));
	}
	return value;
}

// Full-token, locale-independent scalar parsing. Failed try_parse preserves result.
export T __std_parse<T>(string text)
{
	return __scalar_parse<T>(text);
}

export T __std_parse<T>(string text, int base)
{
	return __scalar_parse<T>(text, base);
}

export bool __std_try_parse<T>(string text, ref T result)
{
	return __scalar_try_parse<T>(text, ref result);
}

export bool __std_try_parse<T>(string text, ref T result, int base)
{
	return __scalar_try_parse<T>(text, ref result, base);
}

export std::memory::buffer __std_encode_utf8(string text)
{
	return __string_encode_utf8(text);
}

export string __std_decode_utf8(std::memory::buffer bytes)
{
	return __string_decode_utf8(bytes);
}
export string __std_decode_utf8(std::memory::view bytes)
{
	return __string_decode_utf8(bytes);
}
export string __std_decode_utf8(std::memory::readonly_view bytes)
{
	return __string_decode_utf8(bytes);
}
export bool __std_is_valid_utf8(string text)
{
	return __string_is_valid_utf8(text);
}
export bool __std_is_valid_utf8(std::memory::buffer bytes)
{
	return __string_is_valid_utf8(bytes);
}
export bool __std_is_valid_utf8(std::memory::view bytes)
{
	return __string_is_valid_utf8(bytes);
}
export bool __std_is_valid_utf8(std::memory::readonly_view bytes)
{
	return __string_is_valid_utf8(bytes);
}
