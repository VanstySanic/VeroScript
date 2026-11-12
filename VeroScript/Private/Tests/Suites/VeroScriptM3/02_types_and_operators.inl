// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / types and operators suite. Included by the owning translation unit.

	if (!ExpectOutput(
			"int side() { print(99); return 1; }\n"
			"void main() {\n"
			"    int arr[2];\n"
			"    arr[side()] = 7;\n"
			"    print(arr[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"99\n7\n",
			"indexed assignment evaluates rhs once"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { int xs[2]; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    s.xs[0] = 11;\n"
			"    s.xs[1] = 22;\n"
			"    print(s.xs[0]);\n"
			"    print(s.xs[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n22\n",
			"struct field indexed assignment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"print(1);\n"
			"void main() { print(2); }\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"script mode runs top-level init and main"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int g = 0;\n"
			"int g = 1;\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "duplicate global should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 3, "compile error should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int x = 1 / 0; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "division by zero should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "runtime error should exit 1"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct S { int x = 0; void inc() { x = x + 1; } }\n"
			"void main() { S s = S(); s.inc(); print(s.x); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"struct method mutates receiver"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Inner { int x = 0; }\n"
			"struct Outer { Inner i; }\n"
			"void main() { Outer o = Outer(); print(o.i.x); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"same-file struct field forward reference"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    if (true) { int x = 1; print(x); }\n"
			"    else { int x = 2; print(x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"if branches create separate scopes"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(not_defined); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "undeclared identifier should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("undeclared identifier") != std::string::npos, "undeclared identifier message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {}\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "duplicate main should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { int x = 0; }\n"
			"void main() { new S(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "new struct should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Bad { int x = 1 / 0; }\n"
			"void main() { Bad b = Bad(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "field init runtime error should fail"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "field init runtime error should exit 1"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { arr[0] = 1; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "indexed assign to undeclared should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int i = 10;\n"
			"    for (int i = 0; i < 3; i = i + 1) { print(i); }\n"
			"    print(i);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n1\n2\n10\n",
			"for-init shadows outer local"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int x = 0; int x = 1; print(x); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "duplicate local in same scope should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Bad { Unknown u; }\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unknown struct field type should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 3, "unknown field type should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void fill(int xs[2]) { xs[0] = 9; xs[1] = 8; }\n"
			"void main() {\n"
			"    int arr[2];\n"
			"    arr[0] = 1;\n"
			"    arr[1] = 2;\n"
			"    fill(arr);\n"
			"    print(arr[0]);\n"
			"    print(arr[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"native array pass-by-value to function"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void sum(int xs[3]) { print(len(xs)); }\n"
			"void main() { int a[3]; sum(a); }\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"function parameter int arr[N]"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::CheckSource(
			"import \"core.vs\";\n"
			"void main() { print(abs(-1)); }\n",
			"<test>",
			Errors);
		if (!Expect(Result.bSuccess, "CheckSource should compile stdlib import"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int arr[3] = {10, 20, 30};\n"
			"    print(arr[0]);\n"
			"    print(arr[1]);\n"
			"    print(arr[2]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"10\n20\n30\n",
			"native array initializer list"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct Plain { int value = 4; }\n"
			"struct Point {\n"
			"    int value;\n"
			"    Point(int input) { value = input; }\n"
			"}\n"
			"class Box<T> {\n"
			"public:\n"
			"    T value;\n"
			"    Box(T input) { value = input; }\n"
			"}\n"
			"using Ints = array<int>;\n"
			"using MoreInts = Ints;\n"
			"using Scores = map<string,int>;\n"
			"struct AliasHolder {\n"
			"    Ints values = {71, 73};\n"
			"    Scores scores = {\"value\": 79};\n"
			"}\n"
			"Ints global_alias_values = {83, 89};\n"
			"Scores global_alias_scores = {\"value\": 97};\n"
			"Point make_point() { return Point{7}; }\n"
			"array<int> make_array() { return array<int>{2, 3, 5}; }\n"
			"int sum(array<int> values) {\n"
			"    int result = 0;\n"
			"    for (int i = 0; i < len(values); i = i + 1) { result += values[i]; }\n"
			"    return result;\n"
			"}\n"
			"void main() {\n"
			"    Plain plain = Plain{};\n"
			"    Point point = make_point();\n"
			"    Box<int> boxed = Box<int>{9};\n"
			"    array<int> values = array<int>{1};\n"
			"    values = make_array();\n"
			"    map<string,int> scores = map<string,int>{\"a\": 11, \"b\": 13};\n"
			"    set<int> unique = set<int>{1, 1, 2};\n"
			"    Ints alias_bare = {29, 31};\n"
			"    MoreInts alias_explicit = MoreInts{37, 41};\n"
			"    Scores alias_map = Scores{\"answer\": 43};\n"
			"    AliasHolder alias_holder = AliasHolder{};\n"
			"    {\n"
			"        using LocalInts = array<int>;\n"
			"        using LocalScores = map<string,int>;\n"
			"        LocalInts local_bare = {47, 53};\n"
			"        LocalInts local_explicit = LocalInts{59, 61};\n"
			"        LocalScores local_map = LocalScores{\"value\": 67};\n"
			"        print(local_bare[1]);\n"
			"        print(local_explicit[0]);\n"
			"        print(local_map[\"value\"]);\n"
			"    }\n"
			"    print(plain.value);\n"
			"    print(point.value);\n"
			"    print(boxed.value);\n"
			"    print(sum(array<int>{4, 6}));\n"
			"    print(values[2]);\n"
			"    print(scores[\"b\"]);\n"
			"    print(len(unique));\n"
			"    print(Point{17}.value);\n"
			"    print(array<int>{8, 19}[1]);\n"
			"    print(Point(23).value);\n"
			"    print(alias_bare[1]);\n"
			"    print(alias_explicit[0]);\n"
			"    print(alias_map[\"answer\"]);\n"
			"    print(alias_holder.values[1]);\n"
			"    print(alias_holder.scores[\"value\"]);\n"
			"    print(global_alias_values[1]);\n"
			"    print(global_alias_scores[\"value\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"53\n59\n67\n4\n7\n9\n10\n5\n13\n2\n17\n19\n23\n31\n37\n43\n73\n79\n89\n97\n",
			"typed brace construction expressions"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Aggregate { int value; }\n"
			"void main() { Aggregate item = Aggregate{1}; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess,
				"brace construction must not bypass constructors as positional aggregate init"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    { using ScopedInts = array<int>; ScopedInts values = {1}; }\n"
			"    ScopedInts leaked = {2};\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "local type aliases must not escape their lexical block"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<string> values = array<int>{1};\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess,
				"explicit typed brace initializer must preserve its source container type"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int a[2] = {1, 2};\n"
			"    int b[2] = a;\n"
			"    b[0] = 9;\n"
			"    print(a[0]);\n"
			"    print(b[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n9\n",
			"native array copy initializer"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int a[2] = {1, 2}; int b[3] = a; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "native array copy length mismatch should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int arr[3] = {5};\n"
			"    print(arr[0]);\n"
			"    print(arr[1]);\n"
			"    print(arr[2]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n0\n0\n",
			"native array partial initializer"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Counter { public: int value = 0; }\n"
			"void main() {\n"
			"    auto a = Counter();\n"
			"    auto b = a;\n"
			"    a.value = 42;\n"
			"    print(b.value);\n"
			"    print(a == b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\ntrue\n",
			"class shared handle semantics"))
	{
		return EXIT_FAILURE;
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-list-dir-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream EntryFile(TempRoot / "entry.txt");
			EntryFile << "x";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string Path = TempRoot.generic_string();
		const std::string Source =
			"import \"io.vs\";\n"
			"void main() {\n"
			"    array<string> entries = std::filesystem::list_dir(\"" +
			Path + "\");\n"
			"    bool found = false;\n"
			"    for (int i = 0; i < len(entries); i = i + 1) {\n"
			"        if (entries[i] == \"entry.txt\") { found = true; }\n"
			"    }\n"
			"    print(found);\n"
			"}\n";
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "list_dir stdlib should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "true\n", "list_dir finds entry"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct S { int xs[2] = {7, 8}; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    print(s.xs[0]);\n"
			"    print(s.xs[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n8\n",
			"struct field array initializer"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int g[2] = {1, 2};\n"
			"void main() {\n"
			"    print(g[0]);\n"
			"    print(g[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"global native array initializer"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct DirectPoint {\n"
			"    int x = 1;\n"
			"    int y = 2;\n"
			"    DirectPoint() {}\n"
			"    DirectPoint(int in_x, int in_y) { x = in_x; y = in_y; }\n"
			"}\n"
			"struct DirectBox<T> {\n"
			"    T value;\n"
			"    DirectBox(T in_value) { value = in_value; }\n"
			"}\n"
			"using PointAlias = DirectPoint;\n"
			"using CountAlias = int32;\n"
			"DirectPoint global_default();\n"
			"DirectPoint global_value(3, 4);\n"
			"DirectBox<int> global_box(24);\n"
			"namespace Samples { DirectPoint namespaced(5, 6); }\n"
			"DirectPoint make_point(int x, int y) { return DirectPoint(x, y); }\n"
			"void main() {\n"
			"    DirectPoint local_default();\n"
			"    DirectPoint local_value(7, 8);\n"
			"    const PointAlias aliased(9, 10);\n"
			"    array<int> empty_values();\n"
			"    int scalar_value(21);\n"
			"    int scalar_zero();\n"
			"    bool bool_zero();\n"
			"    string string_zero();\n"
			"    CountAlias alias_value(22);\n"
			"    auto inferred_value(23);\n"
			"    DirectBox<int> local_box(25);\n"
			"    print(global_default.x + global_default.y);\n"
			"    print(global_value.x + global_value.y);\n"
			"    print(Samples::namespaced.x + Samples::namespaced.y);\n"
			"    print(local_default.x + local_default.y);\n"
			"    print(local_value.x + local_value.y);\n"
			"    print(aliased.x + aliased.y);\n"
			"    print(len(empty_values));\n"
			"    print(scalar_value); print(scalar_zero); print(bool_zero);\n"
			"    print(len(string_zero)); print(alias_value); print(inferred_value);\n"
			"    print(global_box.value); print(local_box.value);\n"
			"    print(make_point(11, 12).x);\n"
			"    for (DirectPoint cursor(1, 2); cursor.x < 2; cursor.x += 1) {\n"
			"        print(cursor.x + cursor.y);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n7\n11\n3\n15\n19\n0\n21\n0\nfalse\n0\n22\n23\n24\n25\n11\n3\n",
			"direct constructor initialization"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int value(1, 2); }\n",
			"<scalar_direct_init_arity>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess
					&& Errors.str().find("accepts at most one argument") != std::string::npos,
				"scalar direct initialization should reject multiple arguments clearly"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    print(formatf(\"d=%+06d x=%#08X o=%#o\", -42, 255U, 8U));\n"
			"    print(formatf(\"f=%.2f s=%-5.3s c=%c %%\", 3.14159, \"abcdef\", 65));\n"
			"    printf(\"wide=%lld\", 9223372036854775807i64);\n"
			"    array<string> args = {\"7\", \"255\"};\n"
			"    print(formatf(\"array=%d/%x\", args));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "formatf and printf should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Output.str()
					== "d=-00042 x=0X0000FF o=010\n"
						"f=3.14 s=abc   c=A %\n"
						"wide=9223372036854775807\n"
						"array=7/ff\n",
				"printf-style formatting should honor conversions and flags"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	for (const std::string& Source : {
			 std::string("void main() { print(formatf(\"%n\", 1)); }\n"),
				std::string("void main() { print(formatf(\"%*d\", 4, 1)); }\n"),
				std::string("void main() { print(formatf(\"%d\", \"text\")); }\n"),
				std::string("void main() { print(formatf(\"%d\")); }\n"),
				std::string("void main() { print(formatf(\"%.2c\", 65)); }\n"),
				std::string("void main() { print(formatf(\"%0s\", \"text\")); }\n")})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsafe or invalid printf-style formats should fail"))
		{
			std::cerr << "source:\n"
					  << Source;
			return EXIT_FAILURE;
		}
	}

	for (const std::string& RemovedName : {
			 std::string("print_fmt"),
			std::string("eprint_fmt"),
			std::string("printf_fmt"),
			std::string("format_f")})
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string Source =
			"void main() { " + RemovedName + "(\"value\", 1); }\n";
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<removed-format-api>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "removed format API names must stay unavailable"))
		{
			std::cerr << "unexpectedly accepted: " << RemovedName << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(1, 2); }\n",
			"<print-format-requires-string>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess
					&& Errors.str().find("string format") != std::string::npos,
				"multi-argument print must require a string format"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { int values[2](); }\n",
			"<native_array_direct_init>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(
				!Result.bSuccess
					&& Errors.str().find("native array direct initialization is not supported")
						!= std::string::npos,
				"native array direct initialization should have an explicit diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/hello.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/hello.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "hello, VeroScript\n", "hello.vs output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/fib.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/fib.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "count = 1\nfib(10) = 55\n", "fib.vs output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/arrays.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/arrays.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Output.str()
					== "len = 4\n"
				"data[2] = 30\n"
				"data[0] after alias range-for = 11\n"
				"row.cells[0] = 1\n"
				"row.cells[2] = 3\n"
				"grid[1][2] = 6\n"
				"grid[0][1] = 9\n"
				"grid[1][0] = 5\n"
				"grid[0][2] after row alias = 13\n",
				"arrays.vs output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/brace_initialization.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/brace_initialization.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Output.str()
					== "local aliases: 43, 47\n"
				"default point: (0, 0)\n"
				"constructed point sum: 7\n"
				"returned point sum: 15\n"
				"postfix chain sum: 11\n"
				"typed array: 17, 19\n"
				"aliases: 29, 31\n"
				"typed map: 99\n"
				"set size: 2\n"
				"field defaults: 3, 42\n"
				"globals: 13, 1\n",
				"brace_initialization.vs output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/integer_aliases.vs",
			Output,
			Errors);
		if (!Expect(Result.bSuccess, "examples/integer_aliases.vs should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Output.str()
					== "8-bit: -128, 255\n"
				"16-bit: -32768, 65535\n"
				"32-bit: 100000, 4000000000\n"
				"64-bit: 9223372036854775807, 18446744073709551615\n"
				"ByteState(255)\n"
				"SignedLimit(32767)\n"
				"enum key: on, visited=1\n",
				"integer_aliases.vs output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {}\n"
			"void last(int g) {}\n"
			"int g = 99;\n"
			"print(g);\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "top-level init should resolve globals after function compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "99\n", "top-level init global not shadowed by stale function param"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct S { int xs[2]; void set() { xs[0] = 11; xs[1] = 22; } }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    s.set();\n"
			"    print(s.xs[0]);\n"
			"    print(s.xs[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n22\n",
			"method indexed assignment on this field"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int xs[2];\n"
			"struct S { int xs[2]; void f() { xs[0] = 99; print(xs[0]); } }\n"
			"void main() {\n"
			"    xs[0] = 1;\n"
			"    S s = S();\n"
			"    s.f();\n"
			"    print(xs[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"99\n1\n",
			"method indexed assign/read prefer this field over global"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int sum = 0;\n"
			"for (int i = 0; i < 4; i++) {\n"
			"    int doubled = i * 2;\n"
			"    sum += doubled;\n"
			"}\n"
			"print(sum);\n",
			VeroScript::ERunMode::Eval,
			"12\n",
			"top-level for initializer and body variables are init-frame locals"))
	{
		return EXIT_FAILURE;
	}

	{
		std::string Session;
		auto RunSession = [&Session](const std::string_view Chunk, const char* Message) -> bool
		{
			Session += Chunk;
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				Session,
				"<repl>",
				Output,
				Errors,
				VeroScript::ERunMode::Eval);
			if (!Expect(Result.bSuccess, Message))
			{
				std::cerr << Errors.str();
				return false;
			}
			return true;
		};

		if (!RunSession("int x = 1;\n", "repl session initial global"))
		{
			return EXIT_FAILURE;
		}
		if (!RunSession("x = 2;\n", "repl session assignment"))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectOutput(
				std::string(Session) + "print(x);\n",
				VeroScript::ERunMode::Eval,
				"2\n",
				"repl session accumulated print"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path MissingScript =
			std::filesystem::temp_directory_path() / "VeroScript-missing-script-test.vs";

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			MissingScript.generic_string(),
			Output,
			Errors);
		if (!Expect(!Result.bSuccess, "missing script should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("cannot open script:") != std::string::npos, "missing script message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 2, "missing script should exit 2"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-missing-import-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "main.vs") << "import \"missing.vs\";\nvoid main() {}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!Result.bSuccess, "missing import file should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("cannot resolve import:") != std::string::npos, "missing import message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 2, "missing import file should exit 2"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-unresolved-import-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "main.vs") << "import \"nowhere/foo.vs\";\nvoid main() {}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!Result.bSuccess, "unresolved import should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("cannot resolve import:") != std::string::npos, "unresolved import message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 2, "unresolved import should exit 2"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-duplicate-import-fn-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "dep.vs") << "export void foo() {}\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"dep.vs\";\n"
				<< "export void foo() {}\n"
				<< "void main() {}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!Result.bSuccess, "duplicate function across imports should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("duplicate exported symbol:") != std::string::npos
					|| Errors.str().find("duplicate function:") != std::string::npos,
				"duplicate function message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 3, "duplicate function across imports should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-circular-import-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "a.vs")
				<< "import \"b.vs\";\n"
				<< "export struct A { B make_b() { return B(); } }\n"
				<< "void main() { A a = A(); B b = a.make_b(); print(value_b()); }\n";
			std::ofstream(TempRoot / "b.vs")
				<< "import \"a.vs\";\n"
				<< "export struct B { A make_a() { return A(); } }\n"
				<< "export int value_b() { return 9; }\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "a.vs").generic_string(),
			Output,
			Errors);
		if (!Expect(
				Result.bSuccess && Output.str() == "9\n",
				"circular imports should be allowed by default"))
		{
			std::cerr << Errors.str();
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		std::ostringstream StrictOutput;
		std::ostringstream StrictErrors;
		const VeroScript::FRunResult StrictResult = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "a.vs").generic_string(),
			StrictOutput,
			StrictErrors,
			{},
			{},
			true,
			nullptr,
			{},
			{},
			{},
			VeroScript::EExecutionBackend::Mixed,
			nullptr,
			{},
			{},
			false);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!StrictResult.bSuccess, "strict circular imports should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				StrictErrors.str().find("circular import") != std::string::npos,
				"strict circular import message"))
		{
			std::cerr << StrictErrors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(StrictResult.ExitCode == 3, "strict circular import should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	#ifdef _WIN32
	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-case-import-dedupe-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "Lib.vs") << "export int answer() { return 3; }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"Lib.vs\";\n"
				<< "import \"lib.vs\";\n"
				<< "void main() { print(answer()); }\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(
				Result.bSuccess && Output.str() == "3\n",
				"Windows case-alias imports dedupe to one module"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}
	#endif

	if (!ExpectOutput(
			"struct Vec2 {\n"
			"    int x;\n"
			"    void set(int v) { this.x = v; }\n"
			"}\n"
			"void main() {\n"
			"    auto v = Vec2();\n"
			"    v.set(7);\n"
			"    print(v.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"explicit this member assignment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"class Counter {\n"
			"    public:\n"
			"    int n;\n"
			"}\n"
			"void main() {\n"
			"    Counter c = nullptr;\n"
			"    if (c == nullptr) { print(1); }\n"
			"    if (c != nullptr) { print(2); }\n"
			"    print(c);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\nnullptr\n",
			"nullptr literal and comparison"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int n = 10;\n"
			"    n += 4;\n"
			"    n -= 2;\n"
			"    n *= 3;\n"
			"    n /= 2;\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"18\n",
			"compound assignment operators"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int n = 10;\n"
			"    n %= 4;\n"
			"    ++n;\n"
			"    --n;\n"
			"    ++n;\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"modulo assign and prefix increment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int u = 1U;\n"
			"    ++u;\n"
			"    --u;\n"
			"    ++u;\n"
			"    print(u);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"prefix increment on unsigned"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int n = 5;\n"
			"    print(n++);\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n6\n",
			"postfix increment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int u = 1U;\n"
			"    print(u++);\n"
			"    print(u);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"postfix increment on unsigned"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red, Green = 2, Blue };\n"
			"void main() {\n"
			"    Color c = Green;\n"
			"    print(c);\n"
			"    print(Blue);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n",
			"enum members as int constants"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum class Color { Red, Green = 2, Blue };\n"
			"void main() {\n"
			"    Color c = Color.Green;\n"
			"    print(c);\n"
			"    print(Color.Blue);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"Color(2)\nColor(3)\n",
			"enum class members require qualification"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum class Color { Red, Green = 2, Blue };\n"
			"void main() {\n"
			"    Color c = Color::Green;\n"
			"    print(c);\n"
			"    print(Color::Blue);\n"
			"    switch (c) {\n"
			"        case Color::Red:\n"
			"            print(1);\n"
			"        case Color::Green:\n"
			"            print(2);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"Color(2)\nColor(3)\n2\n",
			"enum class :: qualification equivalent to ."))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum class A { Red = 1 }\n"
			"enum class B { Red = 9 }\n"
			"void main() {\n"
			"    print(A.Red);\n"
			"    print(B.Red);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"A(1)\nB(9)\n",
			"enum class allows duplicate member names across enums"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum class Color { Red, Green };\n"
			"void main() { print(Green); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bare enum class member should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"enum class Color { Red, Green = 2, Blue }\n"
			"void main() {\n"
			"    Color c = Color.Green;\n"
			"    switch (c) {\n"
			"        case Color.Red:\n"
			"            print(1);\n"
			"        case Color.Green:\n"
			"            print(2);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"enum class switch cases use qualification"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum class State { Idle, Running, Done }\n"
			"void main() {\n"
			"  State value = State.Running;\n"
			"  switch (value) {\n"
			"    case State.Idle: print(0); break;\n"
			"    case State.Running: print(1); break;\n"
			"    case State.Done: print(2); break;\n"
			"  }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"exhaustive enum class switch needs no default"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum class State { Idle, Running, Done }\n"
			"void main() {\n"
			"  State value = State.Idle;\n"
			"  switch (value) {\n"
			"    case State.Idle: break;\n"
			"    case State.Running: break;\n"
			"  }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "non-exhaustive enum class switch should fail")
			|| !Expect(
				Errors.str().find("missing: Done") != std::string::npos,
				"non-exhaustive enum class switch should name missing members"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"enum Signed { Neg = -2, Zero, Pos };\n"
			"void main() {\n"
			"    print(Neg);\n"
			"    print(Zero);\n"
			"    print(Pos);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"-2\n-1\n0\n",
			"enum negative member values"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum E { A };\n"
			"void main() { A++; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "enum constant increment should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot modify enum constant") != std::string::npos,
				"enum constant increment should report cannot modify"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(true ? 1 : 0);\n"
			"    print(false ? 1 : 0);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n0\n",
			"ternary conditional expression"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(5 & 3);\n"
			"    print(5 | 2);\n"
			"    print(5 ^ 1);\n"
			"    print(~0);\n"
			"    print(1 << 4);\n"
			"    print(16 >> 2);\n"
			"    int a = 1;\n"
			"    a <<= 3;\n"
			"    a |= 1;\n"
			"    print(a);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n7\n4\n-1\n16\n4\n9\n",
			"bitwise operators and compound assigns"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int u = 1U;\n"
			"    u <<= 3;\n"
			"    print(u);\n"
			"    print(~0U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"8\n4294967295\n",
			"bitwise operators on unsigned"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red, Green = 2, Blue };\n"
			"void main() {\n"
			"    print(Color.Red);\n"
			"    print(Color.Green);\n"
			"    print(Color.Blue);\n"
			"    Color c = Color.Green;\n"
			"    print(c);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n2\n3\n2\n",
			"qualified enum member names"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red, Green };\n"
			"class Box {\n"
			"    public:\n"
			"    int Red;\n"
			"}\n"
			"void main() {\n"
			"    Box Color = new Box();\n"
			"    Color.Red = 7;\n"
			"    print(Color.Red);\n"
			"    print(Green);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n1\n",
			"local variable shadows enum type for member access"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red };\n"
			"class Box {\n"
			"    public:\n"
			"    int Color;\n"
			"    void set() {\n"
			"        Color = 3;\n"
			"        print(Color);\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    Box b = new Box();\n"
			"    b.set();\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"this-field assignment when name equals enum type"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red };\n"
			"void main() {\n"
			"    int Red = 1;\n"
			"    Red = 2;\n"
			"    Red |= 1;\n"
			"    print(Red);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"local shadows enum member for assignment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red };\n"
			"class Box {\n"
			"    public:\n"
			"    int Red;\n"
			"    void f() {\n"
			"        Red = 5;\n"
			"        print(Red);\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    Box b = new Box();\n"
			"    b.f();\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"this-field shadows enum member for assignment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"enum Color { Red };\n"
			"void main() {\n"
			"    int Color = 1;\n"
			"    print(Color << 1);\n"
			"    Color << 1;\n"
			"    print(Color);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n1\n",
			"type-name local used in bitwise expression statement"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum Color { Red };\n"
			"class Box {\n"
			"    public:\n"
			"    int Color;\n"
			"    void go() { print(Color.Red); }\n"
			"}\n"
			"void main() {\n"
			"    Box b = new Box();\n"
			"    b.go();\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "int this-field .Red should fail at compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("unknown field") != std::string::npos,
				"int this-field .Red should report unknown field"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum Color { Red };\n"
			"void main() {\n"
			"    Color.Red = 1;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "qualified enum assign should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot modify enum constant") != std::string::npos,
				"qualified enum assign should report cannot modify"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    print(1.5 & 1);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bitwise on float should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("bitwise operands must be integers") != std::string::npos,
				"bitwise on float should report integer requirement"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum Color { Red };\n"
			"void main() {\n"
			"    Color = 1;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "assign to enum type name should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot assign to type name") != std::string::npos,
				"assign to enum type name should report cannot assign to type name"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum Color { Red };\n"
			"void main() {\n"
			"    ++Color;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "increment enum type name should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot assign to type name") != std::string::npos,
				"increment enum type name should report cannot assign to type name"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    unsigned int u = 1U;\n"
			"    u += 1;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsigned += signed 1 should still fail (D9)"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot mix signed and unsigned") != std::string::npos,
				"unsigned += 1 should report signed/unsigned mix"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    string s = \"a\";\n"
			"    ++s;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "prefix ++ on string should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("increment/decrement requires numeric operand") != std::string::npos,
				"prefix ++ on string should report numeric requirement"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"import \"math.vs\";\n"
			"import \"string.vs\";\n"
			"void main() {\n"
			"    print(to_string(std::floor(3.7)));\n"
			"    print(to_string(std::ceil(3.2)));\n"
			"    print(std::strings::to_lower(\"AbC\"));\n"
			"    print(std::strings::to_upper(\"AbC\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n4\nabc\nABC\n",
			"math floor/ceil and string case helpers"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S {\n"
			"    int x;\n"
			"    int xs[2];\n"
			"    void bump() {\n"
			"        this.x += 1;\n"
			"        this.xs[0] += 5;\n"
			"    }\n"
			"}\n"
			"void main() {\n"
			"    auto s = S();\n"
			"    s.bump();\n"
			"    print(s.x);\n"
			"    print(s.xs[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n5\n",
			"compound assignment on explicit this fields"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    int n = -2147483647;\n"
			"    n = n - 1;\n"
			"    n /= -1;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "INT_MIN / -1 should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "integer overflow should exit 1"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("integer overflow") != std::string::npos, "INT_MIN / -1 message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    int n = -2147483647;\n"
			"    n = n - 1;\n"
			"    print(abs(n));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "abs(INT_MIN) should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "abs(INT_MIN) should exit 1"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-main-args-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "argv_echo.vs")
				<< "void main(string[] args) {\n"
				<< "    print(to_string(len(args)));\n"
				<< "    print(args[0]);\n"
				<< "    print(args[1]);\n"
				<< "}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const std::vector<std::string_view> ScriptArgs = {"hello", "world"};
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "argv_echo.vs").generic_string(),
			Output,
			Errors,
			{},
			ScriptArgs);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "main(string[] args) should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "2\nhello\nworld\n", "script args should reach main"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main(int x) {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "main(int) should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("main parameter must be string[]") != std::string::npos, "main param message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 3, "invalid main signature should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main(string[] args) {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "main(string[] args) with no CLI args should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.push(1);\n"
			"    xs.push(2);\n"
			"    print(len(xs));\n"
			"    print(xs[0] + xs[1]);\n"
			"    xs[0] = 10;\n"
			"    print(xs[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\n10\n",
			"dynamic array<int> push len index"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.reserve(32);\n"
			"    print(xs.capacity() >= 32);\n"
			"    xs.push(7);\n"
			"    print(xs[0]);\n"
			"    map<int,int> m = map<int,int>();\n"
			"    m.reserve(32);\n"
			"    m[7] = 11;\n"
			"    print(m[7]);\n"
			"    ordered_map<string,int> om = ordered_map<string,int>();\n"
			"    om.reserve(32);\n"
			"    om[\"a\"] = 13;\n"
			"    print(om[\"a\"]);\n"
			"    set<int> s = set<int>();\n"
			"    s.reserve(32);\n"
			"    s.add(17);\n"
			"    print(s.has(17));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n7\n11\n13\ntrue\n",
			"container reserve and array capacity"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<int> xs = array<int>(); xs.reserve(-1); }\n",
			"<negative-reserve>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "negative reserve should fail safely")
			|| !Expect(
				Errors.str().find("reserve capacity cannot be negative") != std::string::npos,
				"negative reserve diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-export-private-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "secret.vs") << "int hidden() { return 99; }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"secret.vs\";\n"
				<< "void main() { print(hidden()); }\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!Result.bSuccess, "non-exported import should fail compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("unknown function: hidden") != std::string::npos, "hidden symbol message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 3, "non-exported import should exit 3"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-reexport-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export int answer() { return 42; }\n";
			std::ofstream(TempRoot / "facade.vs")
				<< "export import \"lib.vs\";\n";
			std::ofstream(TempRoot / "plain.vs")
				<< "import \"lib.vs\";\n";
			std::ofstream(TempRoot / "main_ok.vs")
				<< "import \"facade.vs\";\n"
				<< "void main() { print(answer()); }\n";
			std::ofstream(TempRoot / "main_fail.vs")
				<< "import \"plain.vs\";\n"
				<< "void main() { print(answer()); }\n";
		}

		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_ok.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(Result.bSuccess, "export import facade should expose answer"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Output.str() == "42\n", "re-export answer output"))
			{
				std::cerr << "got:\n"
						  << Output.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_fail.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(!Result.bSuccess, "plain import aggregator should not re-export"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(
					Errors.str().find("unknown function: answer") != std::string::npos,
					"plain aggregator should hide nested export"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}
