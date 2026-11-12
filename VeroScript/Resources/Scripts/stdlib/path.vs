// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// path.vs - path string helpers (v0.1.1)

// Pure lexical queries. Explicit styles behave identically on every host.
export bool __std_path_is_absolute(string path)
{
	return __path_is_absolute(path, std::filesystem::path_style::native);
}
export bool __std_path_is_absolute(string path, std::filesystem::path_style style)
{
	return __path_is_absolute(path, style);
}
export string __std_path_root(string path)
{
	return __path_root(path, std::filesystem::path_style::native);
}
export string __std_path_root(string path, std::filesystem::path_style style)
{
	return __path_root(path, style);
}
export string __std_path_normalize(string path)
{
	return __path_normalize(path, std::filesystem::path_style::native);
}
export string __std_path_normalize(string path, std::filesystem::path_style style)
{
	return __path_normalize(path, style);
}

export string __std_basename(string path)
{
	int64 n = len(path);
	int64 i = n - 1;
	while (i >= 0)
	{
		int c = __char_code(path, i);
		if (c == 47 || c == 92)
		{
			return __substr(path, i + 1, n - i - 1);
		}
		i = i - 1;
	}
	return path;
}

export string __std_dirname(string path)
{
	int64 n = len(path);
	int64 i = n - 1;
	while (i >= 0)
	{
		int c = __char_code(path, i);
		if (c == 47 || c == 92)
		{
			if (i == 0)
			{
				return __substr(path, 0, 1);
			}
			// Keep the separator in a Windows drive root (C:/ or C:\).
			if (i == 2 && __char_code(path, 1) == 58)
			{
				int drive = __char_code(path, 0);
				if ((drive >= 65 && drive <= 90) || (drive >= 97 && drive <= 122))
				{
					return __substr(path, 0, 3);
				}
			}
			return __substr(path, 0, i);
		}
		i = i - 1;
	}
	return ".";
}

export string __std_join_path(string left, string right)
{
	if (len(left) == 0)
	{
		return right;
	}
	if (len(right) == 0)
	{
		return left;
	}
	int first = __char_code(right, 0);
	if (first == 47 || first == 92)
	{
		return right;
	}
	if (len(right) >= 2 && __char_code(right, 1) == 58 && ((first >= 65 && first <= 90) || (first >= 97 && first <= 122)))
	{
		return right;
	}
	if (len(left) == 2 && __char_code(left, 1) == 58)
	{
		int drive = __char_code(left, 0);
		if ((drive >= 65 && drive <= 90) || (drive >= 97 && drive <= 122))
		{
			return left + right;
		}
	}
	int last = __char_code(left, len(left) - 1);
	if (last == 47 || last == 92)
	{
		return left + right;
	}
	return left + "/" + right;
}

export string __std_extension(string path)
{
	string name = __std_basename(path);
	int64 n = len(name);
	int64 i = n - 1;
	while (i >= 0)
	{
		if (__char_code(name, i) == 46)
		{
			if (i == 0)
			{
				return "";
			}
			return __substr(name, i, n - i);
		}
		i = i - 1;
	}
	return "";
}

export string __std_change_extension(string path, string new_ext)
{
	string name = __std_basename(path);
	string dir = __std_dirname(path);
	int64 n = len(name);
	int64 i = n - 1;
	int64 dot = -1;
	while (i > 0)
	{
		if (__char_code(name, i) == 46)
		{
			dot = i;
			break;
		}
		i = i - 1;
	}
	string stem = name;
	if (dot >= 0)
	{
		stem = __substr(name, 0, dot);
	}
	string ext = new_ext;
	if (len(ext) > 0 && __char_code(ext, 0) != 46)
	{
		ext = "." + ext;
	}
	// dirname of a bare filename is "."; keep a bare result (not "./name").
	if (dir == ".")
	{
		return stem + ext;
	}
	return __std_join_path(dir, stem + ext);
}
