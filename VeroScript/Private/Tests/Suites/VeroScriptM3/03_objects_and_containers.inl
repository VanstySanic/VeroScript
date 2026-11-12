// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / objects and containers suite. Included by the owning translation unit.

	{
		// Implicit imports are ordinary all-module imports. The cohort is prepared
		// before parsing so exported types can refer to one another cyclically.
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-implicit-import-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::string APath = (TempRoot / "a.vs").generic_string();
		const std::string BPath = (TempRoot / "b.vs").generic_string();
		const std::string MainPath = (TempRoot / "main.vs").generic_string();
		{
			std::ofstream(TempRoot / "a.vs")
				<< "export struct A { B make_b() { return B(); } }\n"
				<< "export int from_a() { return from_b() + 2; }\n"
				<< "print(\"a-init\");\n";
			std::ofstream(TempRoot / "b.vs")
				<< "export struct B { A make_a() { return A(); } }\n"
				<< "export int from_b() { return 40; }\n"
				<< "int hidden() { return 99; }\n"
				<< "print(\"b-init\");\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"a.vs\";\n"
				<< "void main() {\n"
				<< "    A a = A();\n"
				<< "    B b = a.make_b();\n"
				<< "    A again = b.make_a();\n"
				<< "    print(from_a());\n"
				<< "}\n";
		}

		struct FCaptureHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				Output.append(Line);
				Output.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				Errors.append(Line);
				Errors.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string Output;
			std::string Errors;

		};

		FCaptureHost Host;
		VeroScript::FEngineConfig Config;
		// Reverse input order deliberately: initialization must still be stable by path.
		Config.ImplicitImportPaths = {BPath, APath, APath};
		const VeroScript::FRunResult Result =
			VeroScript::Test::FEngine::RunFile(MainPath, Host, Config);
		if (!Expect(Result.bSuccess, "implicit imports should support cyclic exported types"))
		{
			std::cerr << Host.Errors;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.Output == "a-init\nb-init\n42\n",
				"implicit modules should initialize once in stable path order"))
		{
			std::cerr << "got:\n"
					  << Host.Output;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		{
			std::ofstream(TempRoot / "main.vs")
				<< "void main() { print(hidden()); }\n";
		}
		FCaptureHost PrivateHost;
		const VeroScript::FRunResult PrivateResult =
			VeroScript::Test::FEngine::RunFile(MainPath, PrivateHost, Config);
		if (!Expect(
				!PrivateResult.bSuccess
					&& PrivateHost.Errors.find("unknown function: hidden") != std::string::npos,
				"implicit import must preserve export visibility"))
		{
			std::cerr << PrivateHost.Errors;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		VeroScript::FEngineConfig ProjectConfig = Config;
		ProjectConfig.bAllowProjectWideAccess = true;
		FCaptureHost ProjectHost;
		const VeroScript::FRunResult ProjectResult =
			VeroScript::Test::FEngine::RunFile(MainPath, ProjectHost, ProjectConfig);
		if (!Expect(
				ProjectResult.bSuccess
					&& ProjectHost.Output == "a-init\nb-init\n99\n",
				"project-wide access should expose implicit-module internals"))
		{
			std::cerr << ProjectHost.Errors;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		{
			std::ofstream(TempRoot / "external.vs")
				<< "export int external_probe() { return hidden(); }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"external.vs\";\n"
				<< "void main() { print(external_probe()); }\n";
		}
		FCaptureHost ExternalHost;
		const VeroScript::FRunResult ExternalResult =
			VeroScript::Test::FEngine::RunFile(MainPath, ExternalHost, ProjectConfig);
		if (!Expect(
				!ExternalResult.bSuccess
					&& ExternalHost.Errors.find("unknown function: hidden") != std::string::npos,
				"project internals must not leak to explicit dependencies outside the cohort"))
		{
			std::cerr << ExternalHost.Errors;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		VeroScript::FEngineConfig MissingConfig;
		const std::string MissingPath = (TempRoot / "missing.vs").generic_string();
		MissingConfig.ImplicitImportPaths = {MissingPath};
		FCaptureHost MissingHost;
		const VeroScript::FRunResult MissingResult =
			VeroScript::Test::FEngine::RunFile(MainPath, MissingHost, MissingConfig);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(!MissingResult.bSuccess, "missing implicit import should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-reexport-struct-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export struct Point { int x; }\n"
				<< "export Point make() {\n"
				<< "    Point p = Point();\n"
				<< "    p.x = 5;\n"
				<< "    return p;\n"
				<< "}\n";
			std::ofstream(TempRoot / "facade.vs")
				<< "export import \"lib.vs\";\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"facade.vs\";\n"
				<< "void main() {\n"
				<< "    Point p = make();\n"
				<< "    print(p.x);\n"
				<< "}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "export import should re-export struct type"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "5\n", "re-export struct output"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		// Re-export closure is transitive and diamond imports are idempotent, while
		// declarations without export remain private at every forwarding depth.
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-reexport-transitive-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export int public_value() { return 77; }\n"
				<< "int private_value() { return 13; }\n";
			std::ofstream(TempRoot / "left.vs")
				<< "export import \"lib.vs\";\n";
			std::ofstream(TempRoot / "right.vs")
				<< "export import \"lib.vs\";\n";
			std::ofstream(TempRoot / "facade.vs")
				<< "export import \"left.vs\";\n"
				<< "export import \"right.vs\";\n";
			std::ofstream(TempRoot / "main_ok.vs")
				<< "import \"facade.vs\";\n"
				<< "void main() { print(public_value()); }\n";
			std::ofstream(TempRoot / "main_private.vs")
				<< "import \"facade.vs\";\n"
				<< "void main() { print(private_value()); }\n";
		}

		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_ok.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(
					Result.bSuccess && Output.str() == "77\n",
					"transitive diamond re-export should expose one public symbol"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_private.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(
					!Result.bSuccess
						&& Errors.str().find("unknown function: private_value")
							!= std::string::npos,
					"re-export closure must not expose private declarations"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-private-dup-fn-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "a.vs") << "int helper() { return 1; }\n";
			std::ofstream(TempRoot / "b.vs") << "int helper() { return 2; }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"a.vs\";\n"
				<< "import \"b.vs\";\n"
				<< "int helper() { return 3; }\n"
				<< "void main() { print(helper()); }\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "private duplicate function names across modules should compile"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "3\n", "main module should resolve its own private helper"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-shadowed-type-array-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export struct S { int x = 1; }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "void main() {\n"
				<< "    S a[1];\n"
				<< "    print(a[0].x);\n"
				<< "    array<S> d = array<S>();\n"
				<< "    d.push(S());\n"
				<< "    print(d[0].x);\n"
				<< "    map<int,S> m = map<int,S>();\n"
				<< "    m[0] = S();\n"
				<< "    print(m[0].x);\n"
				<< "    queue<S> q = queue<S>();\n"
				<< "    q.push(S());\n"
				<< "    print(q.front().x);\n"
				<< "}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "shadowed private S should win in containers"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "2\n2\n2\n2\n", "local private S defaults/values in containers"))
		{
			std::cerr << "got:\n"
					  << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-shadowed-field-init-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "lib.vs")
				<< "export struct S { int x = 1; }\n"
				<< "export S makeS() { return S(); }\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "struct H { S a[2] = { S(), S() }; }\n"
				<< "void main() {\n"
				<< "    H h = H();\n"
				<< "    print(h.a[0].x);\n"
				<< "    print(h.a[1].x);\n"
				<< "}\n";
			std::ofstream(TempRoot / "main_reject.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "void main() { S x = makeS(); print(x.x); }\n";
			std::ofstream(TempRoot / "main_field_ctor.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "struct H { S f = S(); }\n"
				<< "void main() { H h = H(); print(h.f.x); }\n";
			std::ofstream(TempRoot / "main_auto_reject.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "void main() { auto x = makeS(); S y = x; print(y.x); }\n";
			std::ofstream(TempRoot / "main_foreach_reject.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S { int x = 2; }\n"
				<< "void main() {\n"
				<< "    S arr[1];\n"
				<< "    for (S x : arr) { x = makeS(); print(x.x); }\n"
				<< "}\n";
			std::ofstream(TempRoot / "main_this.vs")
				<< "import \"lib.vs\";\n"
				<< "struct S {\n"
				<< "    int x = 2;\n"
				<< "    S id() { return this; }\n"
				<< "}\n"
				<< "void main() { S s = S(); print(s.id().x); }\n";
		}

		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(Result.bSuccess, "field native-array init should use local S"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Output.str() == "2\n2\n", "field native-array init local S values"))
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
				(TempRoot / "main_field_ctor.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(Result.bSuccess, "private field default S() should compile under shadow"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Output.str() == "2\n", "private field default S() value"))
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
				(TempRoot / "main_reject.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(!Result.bSuccess, "exported S must not assign to shadowed private S"))
			{
				std::cerr << "unexpected success:\n"
						  << Output.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(
					Errors.str().find("cannot convert") != std::string::npos,
					"shadowed return assign should report cannot convert"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_auto_reject.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(!Result.bSuccess, "auto export S must not assign to private S"))
			{
				std::cerr << "unexpected success:\n"
						  << Output.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_foreach_reject.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(!Result.bSuccess, "foreach private S must not accept export S"))
			{
				std::cerr << "unexpected success:\n"
						  << Output.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		{
			std::ostringstream Output;
			std::ostringstream Errors;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
				(TempRoot / "main_this.vs").generic_string(),
				Output,
				Errors);
			if (!Expect(Result.bSuccess, "return this under shadowed private S"))
			{
				std::cerr << Errors.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Output.str() == "2\n", "return this under shadow value"))
			{
				std::cerr << "got:\n"
						  << Output.str();
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-import-init-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "a.vs")
				<< "int helper() { return 7; }\n"
				<< "print(helper());\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"a.vs\";\n"
				<< "void main() {}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "imported module top-level init should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "7\n", "imported module top-level init output"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { int x; }\n"
			"export struct S { int y; }\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "duplicate symbol in module should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("duplicate symbol in module:") != std::string::npos, "duplicate symbol message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<int> xs = array<long>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "dynamic array initializer mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<double> xs = array<double>();\n"
			"    xs.push(1);\n"
			"    print(xs[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"dynamic array<double> accepts int literal in push"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> a = array<int>();\n"
			"    array<int> b = array<int>();\n"
			"    print(a == b);\n"
			"    b = a;\n"
			"    print(a == b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"false\ntrue\n",
			"dynamic array equality uses reference identity"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<long> a = array<long>();\n"
			"    array<int> b = a;\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "dynamic array var assignment mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"dynamic array var assignment mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"export struct B { A a; }\n"
			"export struct A { int x; }\n"
			"void main() {}\n",
			VeroScript::ERunMode::Script,
			"",
			"export struct forward reference should parse and compile"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box { array<int> xs; }\n"
			"void main() {\n"
			"    Box b = Box();\n"
			"    b.xs = array<long>();\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "member dynamic array assignment mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"member dynamic array assignment mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void take(array<int> a) {}\n"
			"void main() { take(array<long>()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "dynamic array argument mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"dynamic array argument mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct S { int x; }\n"
			"S get(S arr[1]) { return arr[0]; }\n"
			"void main() {\n"
			"    S arr[1];\n"
			"    arr[0] = S();\n"
			"    arr[0].x = 1;\n"
			"    S s = get(arr);\n"
			"    s.x = 99;\n"
			"    print(arr[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"struct return from array index is by value"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { int x; }\n"
			"void main() {\n"
			"    S arr[1];\n"
			"    arr[0] = S();\n"
			"    arr[0].x = 7;\n"
			"    print(arr[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"struct array indexed field assignment"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { int x; void inc() { x = x + 1; } }\n"
			"void main() {\n"
			"    S arr[1];\n"
			"    arr[0] = S();\n"
			"    arr[0].inc();\n"
			"    print(arr[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"struct array indexed method mutates element"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { int x; }\n"
			"void main() {\n"
			"    S arr[1];\n"
			"    arr[0] = S();\n"
			"    arr[0].x = 1;\n"
			"    S s = arr[0];\n"
			"    s.x = 99;\n"
			"    print(arr[0].x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"struct array index read via assignment is by value"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box { void take(array<int> a) {} }\n"
			"void main() {\n"
			"    Box b = Box();\n"
			"    b.take(array<long>());\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "method dynamic array argument mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"method dynamic array argument mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-export-global-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);

		{
			std::ofstream(TempRoot / "cfg.vs") << "export int counter = 0;\n";
			std::ofstream(TempRoot / "main.vs")
				<< "import \"cfg.vs\";\n"
				<< "void main() {\n"
				<< "    counter = 42;\n"
				<< "    print(counter);\n"
				<< "}\n";
		}

		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "exported global variable should be visible across modules"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "42\n", "exported global variable read/write"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Pair { int a; int b; }\n"
			"void main() {\n"
			"    array<Pair> xs = array<Pair>();\n"
			"    xs.push(Pair());\n"
			"    xs[0].a = 3;\n"
			"    xs[0].b = 4;\n"
			"    print(xs[0].a + xs[0].b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"dynamic array<Pair> push and indexed field access"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"array<long> make() { return array<long>(); }\n"
			"void main() { array<int> a = make(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "function return dynamic array mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"function return dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"array<int> bad() { return array<long>(); }\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "return statement dynamic array mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"return statement dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box { array<long> get() { return array<long>(); } }\n"
			"void main() {\n"
			"    Box b = Box();\n"
			"    array<int> a = b.get();\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "method return dynamic array mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"method return dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"struct Box { array<int> get() { return array<int>(); } }\n"
			"void main() {\n"
			"    Box b = Box();\n"
			"    array<int> a = b.get();\n"
			"    a.push(9);\n"
			"    print(a[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"method returning array<T> parses and runs"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { array<int> xs; void set() { this.xs = array<long>(); } }\n"
			"void main() { S s = S(); s.set(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "this.field dynamic array assignment mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"this.field dynamic array assignment mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S {\n"
			"    array<long> get() { return array<long>(); }\n"
			"    void use() { array<int> a = this.get(); }\n"
			"}\n"
			"void main() { S s = S(); s.use(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "this.method() dynamic array mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"this.method() dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"array<long> make() { return array<long>(); }\n"
			"void take(array<int> a) {}\n"
			"void main() { take(make()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "take(make()) dynamic array mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"take(make()) dynamic array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(to_int(3.9));\n"
			"    print(to_double(2));\n"
			"    print(to_int(true));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n2\n1\n",
			"to_int and to_double builtins"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.push(10);\n"
			"    xs.push(20);\n"
			"    for (int x : xs) { print(x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"10\n20\n",
			"range-for over dynamic array"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int xs[3] = {1, 2, 3};\n"
			"    for (auto x : xs) { print(x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n3\n",
			"range-for over native array with auto"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> xs = array<int>();\n"
			"    xs.push(1); xs.push(2); xs.push(3);\n"
			"    for (int x : xs) {\n"
			"        if (x == 2) { continue; }\n"
			"        print(x);\n"
			"        if (x == 3) { break; }\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n3\n",
			"range-for break and continue"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<long> xs = array<long>();\n"
			"    for (int x : xs) { print(x); }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "range-for element type mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("range-for element type mismatch") != std::string::npos,
				"range-for element type mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    for (int i = 0; i < 2; i = i + 1) { print(i); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"0\n1\n",
			"C-style for still works after range-for"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<int> a = array<int>();\n"
			"    a.push(1);\n"
			"    for (int x : a) { a.push(2); print(x); }\n"
			"    print(len(a));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"range-for snapshots length against push mutation"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() { print(to_int(2147483647.5)); }\n",
			VeroScript::ERunMode::Script,
			"2147483647\n",
			"to_int truncates values within int32 after truncation"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main(string[] args) { array<int> xs = args; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "array<T> = string[] should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("dynamic array type mismatch") != std::string::npos,
				"array<T> = string[] mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-foreach-args-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		{
			std::ofstream(TempRoot / "main.vs")
				<< "void main(string[] args) {\n"
				<< "    for (auto s : args) { print(s); }\n"
				<< "}\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::vector<std::string_view> ScriptArgs = {"hi"};
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors,
			{},
			ScriptArgs);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "range-for over main string[] args should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "hi\n", "range-for over main string[] args"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() { print(1U + 2U); }\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"unsigned add"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() { print(0xFFFFFFFFU + 1U); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"unsigned wrap"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int x = 1;\n"
			"    print(x + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"unsigned init from signed literal coerces"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"unsigned int f() { return 3; }\n"
			"void g(unsigned int x) { print(x + 1U); }\n"
			"void main() {\n"
			"    g(5);\n"
			"    print(f() + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"6\n4\n",
			"unsigned param/return coerce from signed"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(1 + 1U); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "mixed signed/unsigned should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot mix signed and unsigned") != std::string::npos,
				"mixed signed/unsigned error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    wstring s = L\"hi\";\n"
			"    print(len(s));\n"
			"    print(to_string(s));\n"
			"    print(s);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\nhi\nhi\n",
			"wstring len/print/to_string"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(\"a\" + L\"b\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "string + wstring should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot mix string and wstring") != std::string::npos,
				"string + wstring mix error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(2147483648); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsuffixed int out of int32 range should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int x = 1 + 2;\n"
			"    print(x + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"unsigned init from signed binary coerces"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int x = 1U + 2U;\n"
			"    print(x + 1);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"signed init from unsigned binary coerces"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { unsigned int x = -1; print(x); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsigned init from -1 should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("to_unsigned overflow") != std::string::npos,
				"unsigned init from -1 overflow message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    auto x = 1U + 2U;\n"
			"    print(x + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"auto infers unsigned from unsigned binary"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { wstring w = \"hi\"; }\n"
			"void main() { auto s = S(); print(s.w); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "field default string to wstring should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot assign string to wstring") != std::string::npos,
				"field default string to wstring error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Inner { int x = 7; }\n"
			"struct Outer { Inner nested = 0; }\n"
			"void main() { Outer o = Outer(); print(o.nested.x); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "struct field default literal should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("invalid field default") != std::string::npos,
				"struct field default literal error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"enum Color { Red = 1 }\n"
			"struct Outer { Color c = Red; }\n"
			"void main() { Outer o = Outer(); print(o.c); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"enum field default constant still works"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(L\"a\" + 1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring + int should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<unsigned int> a = array<unsigned int>();\n"
			"    a.push(1);\n"
			"    print(a[0] + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"array push coerces signed to unsigned element"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int x = 1.5;\n"
			"    print(x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"signed init from float coerces via to_int"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { unsigned int n = 1.5; }\n"
			"void main() { auto s = S(); print(s.n + 1U); }\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"unsigned field default from float coerces"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { unsigned int n = \"x\"; }\n"
			"void main() { auto s = S(); print(s.n); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsigned field default from string should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { wstring w = 1; print(w); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring init from int should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { string s = 1; print(s); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "string init from int should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { wstring w = nullptr; print(w); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring init from nullptr should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void f() {}\n"
			"void main() { unsigned int x = f(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unsigned init from void call should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    wstring p = L\"C:\\\\Temp\\\\test.txt\";\n"
			"    print(to_string(p));\n"
			"    print(len(p));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"C:\\Temp\\test.txt\n16\n",
			"win_path style wstring"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(L\"a\" == L\"a\");\n"
			"    print(L\"a\" < L\"b\");\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\n",
			"wstring equality and ordering"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(L\"a\" == \"a\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "string == wstring should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot mix string and wstring") != std::string::npos,
				"string == wstring mix error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { wstring w = L\"hi\"; print(w == nullptr); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring == nullptr should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot compare") != std::string::npos,
				"wstring == nullptr error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(\"a\" == nullptr); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "string == nullptr should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(L\"a\" < \"b\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring < string should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(L\"a\" * 2); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "wstring * int should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(1 + \"x\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "int + string should coerce via to_string"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "1x\n", "int + string output"))
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
			"void main() { array<int> a = array<int>(); print(\"x\" + a); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "string + array should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(1U + \"x\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "unsigned + string should coerce via to_string"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "1x\n", "unsigned + string output"))
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
			"void main() { print(to_wstring(1)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "to_wstring(int) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(to_unsigned(\"x\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "to_unsigned(string) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(min(1U, 2)); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "min signed/unsigned mix should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<unsigned int> a = array<unsigned int>();\n"
			"    a.push(1);\n"
			"    for (int x : a) { print(x); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"range-for coerces unsigned element to int"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() { auto w = L\"hi\"; print(to_string(w)); }\n",
			VeroScript::ERunMode::Script,
			"hi\n",
			"auto infers wstring from wide literal"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() { print(2147483648L); print(4294967296UL); }\n",
			VeroScript::ERunMode::Script,
			"2147483648\n4294967296\n",
			"L and UL literals use the real 64-bit long types"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    double d = 1U;\n"
			"    float f = 2;\n"
			"    print(d == 1.0);\n"
			"    print(f == 2.0);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\n",
			"float/double slots coerce from int/unsigned"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { float f = L\"hi\"; print(f); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "float from wstring should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { bool b = 1U; if (b) { print(1); } }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bool from unsigned should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    array<bool> a = array<bool>();\n"
			"    a.push(1U);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "push unsigned into array<bool> should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(to_int(L\"1\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "to_int(wstring) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(to_double(L\"1\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "to_double(wstring) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    array<unsigned int> a = array<unsigned int>();\n"
			"    a.push(3);\n"
			"    for (double x : a) { print(x == 3.0); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"range-for coerces unsigned element to double"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"struct S { wstring w = L\"ok\"; unsigned int n = 7; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    print(to_string(s.w));\n"
			"    print(s.n + 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"ok\n8\n",
			"struct wstring/unsigned field defaults"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(1.0 == 1);\n"
			"    print(1.0 == 1U);\n"
			"    print(1.0 != 1U);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\nfalse\n",
			"numeric equality compares by value not tag"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(true == 1U); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "bool == unsigned should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(true && 1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "true && 1 should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(-L\"a\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unary minus on wstring should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(!\"x\"); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "logical not on string should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() { print(true && false); print(false || true); print(!false); }\n",
			VeroScript::ERunMode::Script,
			"false\ntrue\ntrue\n",
			"logical operators with bool operands"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { if (1) { print(1); } }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "if(1) should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void f() {}\n"
			"void main() { print(true && f()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "true && void call should fail at compile"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"int n = 0;\n"
			"int idx() { int r = n; n = n + 1; return r; }\n"
			"void main() {\n"
			"    int a[3] = {10, 20, 30};\n"
			"    a[idx()] += 1;\n"
			"    print(a[0]);\n"
			"    print(a[1]);\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n20\n1\n",
			"compound index assign evaluates LHS once"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    unsigned int x = 1U;\n"
			"    x += 2U;\n"
			"    print(x);\n"
			"    wstring w = L\"a\";\n"
			"    w += L\"b\";\n"
			"    print(to_string(w));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\nab\n",
			"compound assign for unsigned and wstring"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    int a[2] = {1, 2};\n"
			"    int b[3] = {1, 2, 3};\n"
			"    a = b;\n"
			"    print(len(a));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "native array size mismatch assign should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("native array type mismatch") != std::string::npos,
				"native array size mismatch error message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void f(int xs[2]) { print(len(xs)); }\n"
			"void main() {\n"
			"    int b[3] = {1, 2, 3};\n"
			"    f(b);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "native array size mismatch param should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int a[2] = {1, 2};\n"
			"    int b[2] = {3, 4};\n"
			"    a = b;\n"
			"    print(a[0]);\n"
			"    print(len(a));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n2\n",
			"native array same-type assign still works"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"import \"string.vs\";\n"
			"void main() {\n"
			"    array<string> parts = std::strings::split(\"a,,b\", \",\");\n"
			"    print(len(parts));\n"
			"    print(parts[0]);\n"
			"    print(parts[1]);\n"
			"    print(parts[2]);\n"
			"    print(std::strings::join(parts, \"|\"));\n"
			"    array<string> empty_join = array<string>();\n"
			"    print(std::strings::join(empty_join, \",\"));\n"
			"    array<string> one = std::strings::split(\"only\", \",\");\n"
			"    print(len(one));\n"
			"    print(one[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\na\n\nb\na||b\n\n1\nonly\n",
			"public split/join standard-library wrappers"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"string.vs\";\n"
			"void main() { print(std::strings::split(\"ab\", \"\")); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "empty public split separator should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "empty public split separator should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"string.vs\";\n"
			"void main() {\n"
			"    string sep = \"\";\n"
			"    print(std::strings::split(\"ab\", sep));\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "empty public split separator variable should fail at runtime"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Result.ExitCode == 1, "empty __split separator variable should be runtime error"))
		{
			std::cerr << "exit=" << Result.ExitCode << '\n'
					  << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-bc-cache-off-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		{
			std::ofstream MainFile(ScriptPath);
			MainFile << "void main() { print(1); }\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			ScriptPath.string(),
			Output,
			Errors,
			{},
			{},
			false);
		const bool bLegacyCacheExists = std::filesystem::exists(ScriptPath.string() + ".vsbc");
		const bool bCacheDirExists = std::filesystem::exists(TempRoot / ".verocache");
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "RunFile with cache off should succeed"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "1\n", "RunFile with cache off output"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!bLegacyCacheExists && !bCacheDirExists,
				"RunFile with cache off should not write .vsbc / .verocache"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-bc-cache-default-test";
		const std::filesystem::path DefaultCacheRoot = TempRoot / "user-cache";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		{
			std::ofstream MainFile(ScriptPath);
			MainFile << "void main() { print(2); }\n";
		}
		#ifdef _WIN32
		char* PreviousCacheDirBuffer = nullptr;
		std::size_t PreviousCacheDirLength = 0;
		_dupenv_s(
			&PreviousCacheDirBuffer,
			&PreviousCacheDirLength,
			"VERO_CACHE_DIR");
		const std::string PreviousCacheDir =
			PreviousCacheDirBuffer != nullptr ? PreviousCacheDirBuffer : "";
		const bool bHadPreviousCacheDir = PreviousCacheDirBuffer != nullptr;
		std::free(PreviousCacheDirBuffer);
		_putenv_s("VERO_CACHE_DIR", DefaultCacheRoot.string().c_str());
		#else
		const char* const PreviousCacheDirValue = std::getenv("VERO_CACHE_DIR");
		const std::string PreviousCacheDir =
			PreviousCacheDirValue != nullptr ? PreviousCacheDirValue : "";
		const bool bHadPreviousCacheDir = PreviousCacheDirValue != nullptr;
		setenv("VERO_CACHE_DIR", DefaultCacheRoot.string().c_str(), 1);
		#endif
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			ScriptPath.string(),
			Output,
			Errors,
			{},
			{},
			true);
		const std::filesystem::path ExpectedCache =
			VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
				ScriptPath.string(),
				DefaultCacheRoot.string());
		const bool bCacheExists = std::filesystem::exists(ExpectedCache);
		const bool bProjectLocalCacheExists =
			std::filesystem::exists(TempRoot / ".verocache");
		const bool bLegacyCacheExists = std::filesystem::exists(ScriptPath.string() + ".vsbc");
		#ifdef _WIN32
		_putenv_s("VERO_CACHE_DIR", bHadPreviousCacheDir ? PreviousCacheDir.c_str() : "");
		#else
		if (bHadPreviousCacheDir)
		{
			setenv("VERO_CACHE_DIR", PreviousCacheDir.c_str(), 1);
		}
		else
		{
			unsetenv("VERO_CACHE_DIR");
		}
		#endif
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "RunFile with cache on should succeed"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "2\n", "RunFile with cache on output"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				bCacheExists && !bProjectLocalCacheExists,
				"RunFile default cache should use VERO_CACHE_DIR without project-local files"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!bLegacyCacheExists, "RunFile with cache on should not write legacy sidecar .vsbc"))
		{
			return EXIT_FAILURE;
		}
	}
