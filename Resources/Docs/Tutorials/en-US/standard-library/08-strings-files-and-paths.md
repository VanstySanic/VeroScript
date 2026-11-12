# 08: Strings, files, and paths

This tutorial completes a small task: read a text file, clean its contents, and write the result to another file. It uses the string, I/O, and path modules together.

## Step 1: prepare the working directory

Create `input.txt` beside the script:

```text
  VeroScript
  is practical
```

Create `clean_text.vs`:

```VeroScript
import "io.vs";
import "path.vs";
import "string.vs";

int main()
{
	string input_path = "input.txt";
	string output_path = std::filesystem::join_path("output", "clean.txt");

	if (!std::filesystem::is_file(input_path))
	{
		print("missing: " + input_path);
		return 1;
	}

	std::filesystem::mkdir(std::filesystem::dirname(output_path));
	string content = std::io::read_file(input_path);
	string cleaned = std::strings::trim(content);
	std::io::write_file(output_path, cleaned + "\n");

	print("wrote " + output_path);
	return 0;
}
```

`io.vs` handles files and directories, `path.vs` joins paths, and `string.vs` cleans text. Keeping those responsibilities separate avoids hand-written path logic and repeated string helpers.

## Step 2: run and inspect the result

```powershell
veroscript --check clean_text.vs
veroscript clean_text.vs
Get-Content output/clean.txt
```

The output file has its leading and trailing ASCII whitespace removed. `trim` removes bytes `0x09` through `0x0D` and space (`0x20`) only at the ends; it preserves interior and non-ASCII bytes. `is_file` follows symbolic links and reports whether the input resolves to a regular file. `read_file` and `write_file` resolve relative paths from the current working directory, so pay attention to where the terminal command is running.

## Step 3: process individual lines

When each line needs separate treatment, split and join the text:

```VeroScript
import "io.vs";
import "string.vs";

int main()
{
	string content = std::io::read_file("input.txt");
	array<string> lines = std::strings::split(content, "\n");
	array<string> cleaned = array<string>();

	for (string line : lines)
	{
		if (std::strings::trim(line) != "")
		{
			cleaned.push(std::strings::trim(line));
		}
	}

	std::io::write_file(
		"clean-lines.txt",
		std::strings::join(cleaned, "\n") + "\n");
	return 0;
}
```

Windows text may use `\r\n`. If input can come from multiple platforms, explicitly handle `\r` instead of assuming one line-ending convention.

## Step 4: format the result

After processing the file, a readable summary is often useful. `format` uses brace fields and returns a string; the multi-argument form of `print` uses the same dialect:

```VeroScript
import "string.vs";

int main()
{
	string path = "output/clean.txt";
	int line_count = 2;
	string summary = format("wrote {}, lines={}", path, line_count);
	print(summary);
	printf("status=%s%%\n", "100");
	return 0;
}
```

Use `formatf` or `printf` when printf-style conversions are required. Do not put `%d` or `%.2f` syntax into a `format` string. See [String language behavior](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/strings.md#formatting) for fields, width, precision, and conversions.

## Exercises

1. Change the output extension to `.clean.txt` with `std::filesystem::change_extension`.
2. Print the input file's `std::filesystem::basename`, `dirname`, and `extension`.
3. If the output exists, append a processing record with `std::io::append_file`.
4. Generate the summary with `format`, then try using `formatf` to print a duration with two decimal places.
5. Delete `output/clean.txt`, check `output` with `std::filesystem::is_directory`, then remove the empty directory with `std::filesystem::remove_dir`.

Compare [`examples/path_helpers.vs`](../../../../../VeroScript/Resources/Scripts/examples/path_helpers.vs). The full APIs are in [I/O](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/io.md), [Paths](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/path.md), and [Strings](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/string.md).

Next: [Concurrency and events](09-concurrency-and-events.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](08-strings-files-and-paths.md) · [fr-FR](../../fr-FR/standard-library/08-strings-files-and-paths.md) · [ja-JP](../../ja-JP/standard-library/08-strings-files-and-paths.md) · [ko-KR](../../ko-KR/standard-library/08-strings-files-and-paths.md) · [zh-CN](../../zh-CN/standard-library/08-strings-files-and-paths.md) · [zh-TW](../../zh-TW/standard-library/08-strings-files-and-paths.md)
