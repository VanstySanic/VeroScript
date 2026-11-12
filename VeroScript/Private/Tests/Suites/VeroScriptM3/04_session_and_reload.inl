// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / session and reload suite. Included by the owning translation unit.

	{
		const std::filesystem::path TempRoot = std::filesystem::temp_directory_path()
			/ VeroScript::PathUtil::FromUtf8("VeroScript-import-\u8def\u5f84-\U0001F680");
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path MainPath = TempRoot / "main.vs";
		const std::filesystem::path ImportPath = TempRoot
			/ VeroScript::PathUtil::FromUtf8("module-\u6a21\u5757.vs");
		{
			std::ofstream Imported(ImportPath, std::ios::binary);
			Imported << "export int imported() { return 42; }\n";
			std::ofstream Main(MainPath, std::ios::binary);
			Main << "import \"module-\u6a21\u5757.vs\";\n"
				"void main() { print(imported()); }\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const auto Result = VeroScript::Test::FEngine::RunFile(
			VeroScript::PathUtil::ToUtf8(MainPath), Output, Errors, {}, {}, false);
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess && Output.str() == "42\n" && Errors.str().empty(),
				"UTF-8 module paths and filenames must resolve and execute"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-bc-cache-custom-dir-test";
		const std::filesystem::path CacheDir = TempRoot / "shared-cache";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		{
			std::ofstream MainFile(ScriptPath);
			MainFile << "void main() { print(3); }\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string CacheDirStr = CacheDir.string();
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			ScriptPath.string(),
			Output,
			Errors,
			{},
			{},
			true,
			nullptr,
			{},
			CacheDirStr,
			{});
		const std::filesystem::path ExpectedCache =
			VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
				ScriptPath.string(),
				CacheDirStr);
		const bool bCacheExists = std::filesystem::exists(ExpectedCache);
		const bool bDefaultCacheExists =
			std::filesystem::exists(TempRoot / ".verocache" / "main.vs.vsbc");
		const bool bFlatBasenameCacheExists = std::filesystem::exists(CacheDir / "main.vs.vsbc");
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "RunFile with custom cache dir should succeed"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "3\n", "RunFile with custom cache dir output"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(bCacheExists, "custom cache dir should contain hashed .vsbc"))
		{
			std::cerr << "expected cache at: " << ExpectedCache.string() << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
				!bDefaultCacheExists && !bFlatBasenameCacheExists,
				"custom cache dir should not write default .verocache or flat basename"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Callable parameter types are nested function signatures.  Keep the
		// complete stdlib/re-export graph serializable so a successful direct VM
		// run cannot silently degrade into a cache-write warning.
		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path()
			/ "VeroScript-bc-cache-callable-signature-test";
		const std::filesystem::path CacheDir = TempRoot / "shared-cache";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "main.vs";
		{
			std::ofstream MainFile(ScriptPath);
			MainFile << "import \"core.vs\";\n"
				"void main() { print(4); }\n";
		}
		std::ostringstream Output;
		std::ostringstream Errors;
		const std::string CacheDirStr = CacheDir.string();
		const std::vector<std::string_view> StdlibPaths{"stdlib"};
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			ScriptPath.string(),
			Output,
			Errors,
			{},
			{},
			true,
			nullptr,
			{},
			CacheDirStr,
			StdlibPaths);
		const std::filesystem::path ExpectedCache =
			VeroScript::VeroScriptBytecodeIO::GetCachePathForScript(
				ScriptPath.string(), CacheDirStr);
		const bool bCacheExists = std::filesystem::exists(ExpectedCache);
		std::filesystem::remove_all(TempRoot, RemoveError);
		if (!Expect(Result.bSuccess, "stdlib callable-signature cache run should succeed")
			|| !Expect(Output.str() == "4\n", "stdlib callable-signature cache output")
			|| !Expect(bCacheExists, "stdlib callable-signature module should be cacheable")
			|| !Expect(
				Errors.str().find("failed to write bytecode cache") == std::string::npos,
				"valid stdlib callable signatures must not emit a cache warning"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		(void)VeroScript::Locale::TrySetLanguageFromTag("zh-CN");
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { print(missing()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
		if (!Expect(!Result.bSuccess, "zh-CN unknown function should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("未知函数：") != std::string::npos,
				"zh-CN diagnostic should translate unknown function"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    print(L\"hi\");\n"
			"    print(to_string(L\"ok\"));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"hi\nok\n",
			"print(wstring) UTF-8 fallback"))
	{
		return EXIT_FAILURE;
	}

	{
		struct FCaptureHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		{
			FCaptureHost Host;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				"void main() { print(1 + 2); print(\"ok\"); }\n",
				"<embed>",
				Host,
				VeroScript::ERunMode::Script);
			if (!Expect(Result.bSuccess, "embed host RunSource should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
			if (!Expect(Host.PrintText == "3\nok\n", "embed host should receive print lines"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText;
				return EXIT_FAILURE;
			}
			if (!Expect(Host.DiagnosticText.empty(), "embed host success should have no diagnostics"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost Host;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				"void main() { print(missing()); }\n",
				"<embed>",
				Host,
				VeroScript::ERunMode::Script);
			if (!Expect(!Result.bSuccess, "embed host compile error should fail"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(Result.ExitCode == 3, "embed host compile error should exit 3"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
					Host.DiagnosticText.find("unknown function: missing") != std::string::npos,
					"embed host should receive diagnostic line"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost Host;
			VeroScript::FEngineConfig Config;
			Config.ScriptArgs = {"alpha", "beta"};
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				"void main(string[] args) {\n"
				"    print(len(args));\n"
				"    print(args[0]);\n"
				"    print(args[1]);\n"
				"}\n",
				"<embed>",
				Host,
				VeroScript::ERunMode::Script,
				Config);
			if (!Expect(Result.bSuccess, "embed host RunSource ScriptArgs should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
			if (!Expect(Host.PrintText == "2\nalpha\nbeta\n", "embed host should receive ScriptArgs"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText;
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost Host;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				"void main() { print(1 / 0); }\n",
				"<embed>",
				Host,
				VeroScript::ERunMode::Script);
			if (!Expect(!Result.bSuccess, "embed host runtime error should fail"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(Result.ExitCode == 1, "embed host runtime error should exit 1"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(
					Host.DiagnosticText.find("division by zero") != std::string::npos,
					"embed host should receive runtime diagnostic"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost Host;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
				"void main() { print(\"a\\nb\"); }\n",
				"<embed>",
				Host,
				VeroScript::ERunMode::Script);
			if (!Expect(Result.bSuccess, "embed host embedded-newline print should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
			if (!Expect(Host.PrintText == "a\nb\n", "one print with embedded newline → one OnPrintLine"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText;
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost Host;
			VeroScript::FEngineConfig Config;
			const VeroScript::FRunResult Result = VeroScript::Test::FEngine::CheckSource(
				"void main() { print(1); }\n",
				"<embed>",
				Host,
				Config);
			if (!Expect(Result.bSuccess, "embed host CheckSource should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				return EXIT_FAILURE;
			}
			if (!Expect(Host.PrintText.empty(), "CheckSource should not print"))
			{
				std::cerr << Host.PrintText;
				return EXIT_FAILURE;
			}
		}
	}

	{
		struct FCaptureHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-session-reload-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "script.vs";
		{
			std::ofstream(ScriptPath) << "void main() { print(1); }\n";
		}

		FCaptureHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		{
			const VeroScript::FRunResult LoadResult = Session.LoadFile(ScriptPath.generic_string());
			if (!Expect(LoadResult.bSuccess, "session LoadFile should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Session.IsLoaded(), "session should be loaded"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			Host.PrintText.clear();
			const VeroScript::FRunResult RunResult = Session.Run();
			if (!Expect(RunResult.bSuccess, "session Run should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Host.PrintText == "1\n", "session first run output"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		{
			std::ofstream(ScriptPath) << "void main() { print(2); }\n";
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult ReloadResult = Session.Reload();
			if (!Expect(ReloadResult.bSuccess, "session Reload should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			const VeroScript::FRunResult RunResult = Session.Run();
			if (!Expect(RunResult.bSuccess && Host.PrintText == "2\n", "session reload run output"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		{
			std::ofstream(ScriptPath) << "void main() { print(missing()); }\n";
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult ReloadResult = Session.Reload();
			if (!Expect(!ReloadResult.bSuccess, "bad Reload should fail"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(Session.IsLoaded(), "failed Reload should keep previous module"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult RunResult = Session.Run();
			if (!Expect(RunResult.bSuccess && Host.PrintText == "2\n", "session keeps last good module"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		{
			Host.PrintText.clear();
			Host.DiagnosticText.clear();
			const VeroScript::FRunResult SourceLoad = Session.LoadSource(
				"void main() { print(9); }\n",
				"<session>");
			if (!Expect(SourceLoad.bSuccess, "session LoadSource should succeed"))
			{
				std::cerr << Host.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			const VeroScript::FRunResult BadReload = Session.Reload();
			if (!Expect(!BadReload.bSuccess, "Reload after LoadSource should fail"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			Host.PrintText.clear();
			const VeroScript::FRunResult RunResult = Session.Run();
			if (!Expect(RunResult.bSuccess && Host.PrintText == "9\n", "LoadSource module still runnable"))
			{
				std::cerr << "got:\n"
						  << Host.PrintText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		{
			FCaptureHost ArgsHost;
			VeroScript::VeroScriptSession ArgsSession(ArgsHost);
		VeroScript::Test::Configure(ArgsSession);
			VeroScript::FEngineConfig Config;
			Config.ScriptArgs = {"x", "y"};
			ArgsSession.ApplyConfig(Config);
			const VeroScript::FRunResult LoadResult = ArgsSession.LoadSource(
				"void main(string[] args) {\n"
				"    print(len(args));\n"
				"    print(args[1]);\n"
				"}\n",
				"<session-args>");
			if (!Expect(LoadResult.bSuccess, "session ScriptArgs LoadSource should succeed"))
			{
				std::cerr << ArgsHost.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			const VeroScript::FRunResult RunResult = ArgsSession.Run();
			if (!Expect(RunResult.bSuccess && ArgsHost.PrintText == "2\ny\n", "session ScriptArgs Run output"))
			{
				std::cerr << "got:\n"
						  << ArgsHost.PrintText << ArgsHost.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			ArgsSession.Clear();
			if (!Expect(!ArgsSession.IsLoaded(), "session Clear should unload"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			ArgsHost.DiagnosticText.clear();
			const VeroScript::FRunResult EmptyRun = ArgsSession.Run();
			if (!Expect(!EmptyRun.bSuccess, "Run after Clear should fail"))
			{
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
			if (!Expect(
					ArgsHost.DiagnosticText.find("no module loaded") != std::string::npos,
					"Clear then Run should report no module loaded"))
			{
				std::cerr << ArgsHost.DiagnosticText;
				std::filesystem::remove_all(TempRoot, RemoveError);
				return EXIT_FAILURE;
			}
		}

		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// State-preserving hot reload: retain globals/object identity, initialize added
	// fields, and reject incompatible layouts without losing the last good state.
	{
		struct FHotReloadHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-hot-reload-state-test";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "state.vs";
		const std::filesystem::path HelperPath = TempRoot / "helper.vs";

		const auto WriteScript = [&ScriptPath](const std::string_view Source)
		{
			std::ofstream Out(ScriptPath, std::ios::binary | std::ios::trunc);
			Out << Source;
		};
		const auto WriteHelper = [&HelperPath](const std::string_view Source)
		{
			std::ofstream Out(HelperPath, std::ios::binary | std::ios::trunc);
			Out << Source;
		};

		WriteHelper("export int delta() { return 1; }\n");
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter { public: int value = 1; Counter peer; }\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { print(90); return 0; }\n"
			"int runs = initialize();\n"
			"void main() {\n"
			"    runs++;\n"
			"    if (runs == 1) { counter.peer = other; other.peer = counter; }\n"
			"    if (runs == 1) { values.push(7); }\n"
			"    counter.value += delta();\n"
			"    print(runs);\n"
			"    print(counter.value);\n"
			"    print(len(values));\n"
			"}\n");

		FHotReloadHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Expect(Session.IsHotReloadEnabled(), "hot reload should be enabled")
			|| !Expect(Session.LoadFile(ScriptPath.generic_string()).bSuccess,
				"hot reload initial load")
					|| !Expect(Session.Run().bSuccess, "hot reload initial run")
					|| !Expect(Host.PrintText == "90\n1\n2\n1\n", "hot reload initial state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteHelper("export int delta() { return 10; }\n");
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter { public: int value = 1; Counter peer; int bonus = 99; }\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { print(91); return 0; }\n"
			"int runs = initialize();\n"
			"int observed = runs;\n"
			"void main() {\n"
			"    runs++;\n"
			"    counter.value += delta();\n"
			"    counter.peer.value += 20;\n"
			"    counter.bonus++;\n"
			"    print(runs);\n"
			"    print(counter.value);\n"
			"    print(counter.peer.value);\n"
			"    print(counter.bonus);\n"
			"    print(observed);\n"
			"    print(len(values));\n"
			"}\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "compatible hot reload")
			|| !Expect(Session.Run().bSuccess, "run compatible hot reload")
			|| !Expect(
				Host.PrintText == "2\n12\n21\n100\n1\n1\n",
				"hot reload preserves globals and defaults added fields"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		// Compilation and schema migration succeed, but the replacement's global
		// initializer fails. The next run must transparently return to the last
		// validated module and its exact pre-reload state.
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter { public: int value = 1; Counter peer; int bonus = 0; }\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { values.push(99); assert(false); return 0; }\n"
			"int runs = 0;\n"
			"int observed = 0;\n"
			"int failure = initialize();\n"
			"void main() { print(999); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "initializer-failing module should compile")
			|| !Expect(!Session.Run().bSuccess, "replacement initializer should fail"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Run().bSuccess, "initializer failure restores previous module")
			|| !Expect(
				Host.PrintText == "3\n22\n41\n101\n1\n1\n",
				"initializer failure restores previous state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		// A failing initializer that allocates into retained objects must still
		// run script destructors for those exclusive allocations on rollback.
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Tracked { public: ~Tracked() { print(\"tracked-destroyed\"); } }\n"
			"class Counter { public: int value = 1; Counter peer; int bonus = 0; Tracked extra; }\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { counter.extra = Tracked(); assert(false); return 0; }\n"
			"int runs = 0;\n"
			"int observed = 0;\n"
			"int failure = initialize();\n"
			"void main() { print(999); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "tracked-allocating module should compile")
			|| !Expect(!Session.Run().bSuccess, "tracked-allocating initializer should fail"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.PrintText.find("tracked-destroyed\n") != std::string::npos,
				"failed initializer rollback destroys exclusive allocations"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Run().bSuccess, "tracked-alloc rollback restores previous module")
			|| !Expect(
				Host.PrintText == "4\n32\n61\n102\n1\n1\n",
				"tracked-alloc rollback restores previous state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		// Seed a retained object-field container, then fail init while that same
		// handle is also present in discarded replacement fields. Cleanup must
		// not clear the restored shared container.
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter {\n"
			"    public: int value = 1; Counter peer; int bonus = 0; array<int> bag;\n"
			"}\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { return 0; }\n"
			"int runs = initialize();\n"
			"int observed = runs;\n"
			"void main() {\n"
			"    runs++;\n"
			"    if (len(counter.bag) == 0) { counter.bag.push(7); }\n"
			"    counter.value += delta();\n"
			"    counter.peer.value += 20;\n"
			"    counter.bonus++;\n"
			"    print(runs);\n"
			"    print(counter.value);\n"
			"    print(counter.peer.value);\n"
			"    print(counter.bonus);\n"
			"    print(observed);\n"
			"    print(len(values));\n"
			"    print(len(counter.bag));\n"
			"}\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "shared-container seed reload")
			|| !Expect(Session.Run().bSuccess, "shared-container seed run")
			|| !Expect(
				Host.PrintText == "5\n42\n81\n103\n1\n1\n1\n",
				"seed retained object-field container"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter {\n"
			"    public: int value = 1; Counter peer; int bonus = 0; array<int> bag;\n"
			"}\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { counter.bag.push(99); assert(false); return 0; }\n"
			"int runs = 0;\n"
			"int observed = 0;\n"
			"int failure = initialize();\n"
			"void main() { print(999); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "shared-container failing module should compile")
			|| !Expect(!Session.Run().bSuccess, "shared-container initializer should fail"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Run().bSuccess, "shared-container rollback restores previous module")
			|| !Expect(
				Host.PrintText == "6\n52\n101\n104\n1\n1\n1\n",
				"rollback must preserve retained object-field containers"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		// An imported file is part of the same compiled module. A dependency error
		// must reject the complete generation, not partially update the main file.
		WriteHelper("export int delta() { return missing(); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(!Session.Reload().bSuccess, "import compile failure rejects reload")
			|| !Expect(Session.Run().bSuccess, "import failure keeps previous module")
			|| !Expect(
				Host.PrintText == "7\n62\n121\n105\n1\n1\n1\n",
				"import failure keeps complete previous state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteHelper("export int delta() { return 10; }\n");
		WriteScript(
			"import \"helper.vs\";\n"
			"class InitProbe { public: ~InitProbe() { print(\"probe-destroyed\"); } }\n"
			"class Counter { public: string value = \"bad\"; Counter peer; int bonus = 0; }\n"
			"InitProbe probe = InitProbe();\n"
			"Counter counter = Counter();\n"
			"Counter other = Counter();\n"
			"array<int> values = array<int>();\n"
			"int initialize() { print(92); return 0; }\n"
			"int runs = initialize();\n"
			"int observed = 0;\n"
			"void main() { print(runs); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(!Session.Reload().bSuccess, "incompatible hot reload should fail")
			|| !Expect(Session.IsLoaded(), "failed hot reload keeps module")
			|| !Expect(Session.Run().bSuccess, "last good module remains runnable")
			|| !Expect(
				Host.PrintText == "8\n72\n141\n106\n1\n1\n1\n",
				"failed hot reload keeps last good state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Session.Clear();
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// Hot reload must run a NEW initializer (generic static) even when it is
	// emitted *nested inside* a retained global's init range.
	//
	// Critical: do NOT reference Box from `main`/any function. Function bodies
	// compile before top-level statements; Ensure there defers generic static
	// init and flushes it *before* `keep`, producing a disjoint range that
	// passes even under whole-range skip (false green). Referencing Box only
	// from top-level `keep = Box<int>.Y` nests Mark()'s side effect inside
	// keep's range. keep's StoreGlobal is dropped (retained → stays 1); "y"
	// proves the punch ran.
	{
		struct FNestedInitHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-hot-reload-nested-init";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "nested.vs";
		const auto WriteScript = [&ScriptPath](const std::string_view Source)
		{
			std::ofstream Out(ScriptPath, std::ios::binary | std::ios::trunc);
			Out << Source;
		};

		WriteScript(
			"int keep = 1;\n"
			"void main() { print(keep); }\n");

		FNestedInitHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		if (!Expect(Session.LoadFile(ScriptPath.generic_string()).bSuccess,
				"nested-init initial load")
					|| !Expect(Session.Run().bSuccess, "nested-init initial run")
					|| !Expect(Host.PrintText == "1\n", "nested-init initial state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteScript(
			"int Mark() { print(\"y\"); return 42; }\n"
			"class Box<T> { public: static int Y = Mark(); T pad; }\n"
			"int keep = Box<int>.Y;\n"
			"void main() { print(keep); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "nested-init reload")
			|| !Expect(Session.Run().bSuccess, "nested-init reload run")
			|| !Expect(
				Host.PrintText == "y\n1\n",
				"nested new generic static init runs under retained outer global"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Session.Clear();
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// Reverse nesting: a retained initializer nested inside a non-retained outer
	// range must stay skipped. Session 1 materializes Side<int>.Y; session 2's
	// first Ensure of Side happens while compiling NEW `outer`, so Y's init bytes
	// nest inside outer's range while Y's slot remains retained.
	{
		struct FReverseNestHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-hot-reload-reverse-nest";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "reverse.vs";
		const auto WriteScript = [&ScriptPath](const std::string_view Source)
		{
			std::ofstream Out(ScriptPath, std::ios::binary | std::ios::trunc);
			Out << Source;
		};

		FReverseNestHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		WriteScript(
			"int Inc() { print(\"inc\"); return 7; }\n"
			"class Side<T> { public: static int Y = Inc(); T pad; }\n"
			"int probe = Side<int>.Y;\n"
			"void main() { print(probe); }\n");
		if (!Expect(Session.LoadFile(ScriptPath.generic_string()).bSuccess,
				"reverse-nest initial load")
					|| !Expect(Session.Run().bSuccess, "reverse-nest initial run")
					|| !Expect(Host.PrintText == "inc\n7\n", "reverse-nest initial state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteScript(
			"int Inc() { print(\"inc\"); return 7; }\n"
			"class Side<T> { public: static int Y = Inc(); T pad; }\n"
			"int probe = 7;\n"
			"int outer = Side<int>.Y;\n"
			"void main() { print(probe); print(outer); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "reverse-nest reload")
			|| !Expect(Session.Run().bSuccess, "reverse-nest reload run")
			|| !Expect(
				Host.PrintText == "7\n7\n",
				"retained nested static init not re-run under non-retained outer"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Session.Clear();
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// Multi-level nesting: retained ⊃ non-retained ⊃ retained.
	// Session 2 must Ensure New/Old only from top-level `keep = New<int>.A`
	// (never from main), so bytes are truly `keep ⊃ New.A ⊃ Old.B`. A broken
	// punch that clears the inner retained range would re-run Seed → "seed".
	// keep's StoreGlobal is dropped → stays 1; assert no second "seed".
	{
		struct FMultiNestHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-hot-reload-multi-nest";
		std::error_code RemoveError;
		std::filesystem::remove_all(TempRoot, RemoveError);
		std::filesystem::create_directories(TempRoot);
		const std::filesystem::path ScriptPath = TempRoot / "multi.vs";
		const auto WriteScript = [&ScriptPath](const std::string_view Source)
		{
			std::ofstream Out(ScriptPath, std::ios::binary | std::ios::trunc);
			Out << Source;
		};

		FMultiNestHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		WriteScript(
			"int Seed() { print(\"seed\"); return 5; }\n"
			"class Old<T> { public: static int B = Seed(); T pad; }\n"
			"int probe = Old<int>.B;\n"
			"int keep = 1;\n"
			"void main() { print(keep); print(probe); }\n");
		if (!Expect(Session.LoadFile(ScriptPath.generic_string()).bSuccess,
				"multi-nest initial load")
					|| !Expect(Session.Run().bSuccess, "multi-nest initial run")
					|| !Expect(Host.PrintText == "seed\n1\n5\n", "multi-nest initial state"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		WriteScript(
			"int Seed() { print(\"seed\"); return 5; }\n"
			"class Old<T> { public: static int B = Seed(); T pad; }\n"
			"class New<T> { public: static int A = Old<int>.B; T pad; }\n"
			"int probe = 5;\n"
			"int keep = New<int>.A;\n"
			"void main() { print(keep); }\n");
		Host.PrintText.clear();
		Host.DiagnosticText.clear();
		if (!Expect(Session.Reload().bSuccess, "multi-nest reload")
			|| !Expect(Session.Run().bSuccess, "multi-nest reload run")
			|| !Expect(
				Host.PrintText == "1\n",
				"multi-level retained⊃new⊃retained: inner retained init not re-run"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			std::filesystem::remove_all(TempRoot, RemoveError);
			return EXIT_FAILURE;
		}

		Session.Clear();
		std::filesystem::remove_all(TempRoot, RemoveError);
	}

	// A retained snapshot must transfer (not duplicate) ownership into the live VM;
	// otherwise shared_ptr use_count would suppress last-owner script destructors.
	{
		struct FDestructorHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(std::string_view) override
			{
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;

		};

		FDestructorHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"class Tracked { public: ~Tracked() { print(\"destroyed\"); } }\n"
			"Tracked value = Tracked();\n"
			"int runs = 0;\n"
			"void main() { runs++; if (runs == 2) { value = Tracked(); } }\n",
			"<hot-reload-destructor>");
		if (!Expect(LoadResult.bSuccess, "hot reload destructor load")
			|| !Expect(Session.Run().bSuccess, "hot reload destructor first run")
			|| !Expect(Host.PrintText.empty(), "retained object stays alive")
			|| !Expect(Session.Run().bSuccess, "hot reload destructor second run")
			|| !Expect(
				Host.PrintText == "destroyed\n",
				"retained global replacement runs destructor exactly once"))
		{
			std::cerr << Host.PrintText;
			return EXIT_FAILURE;
		}
	}

	// Retained class cycles must still run destructors on Clear/teardown.
	{
		struct FCycleHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(std::string_view) override
			{
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;

		};

		FCycleHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"class Node {\n"
			"    public: Node peer;\n"
			"    ~Node() { print(\"node\"); }\n"
			"}\n"
			"Node a = Node();\n"
			"Node b = Node();\n"
			"void main() { a.peer = b; b.peer = a; }\n",
			"<hot-reload-cycle-clear>");
		if (!Expect(LoadResult.bSuccess, "hot reload cycle load")
			|| !Expect(Session.Run().bSuccess, "hot reload cycle run"))
		{
			std::cerr << Host.PrintText;
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		Session.Clear();
		std::size_t NodeLines = 0;
		for (std::size_t Index = 0; Index + 4 < Host.PrintText.size(); ++Index)
		{
			if (Host.PrintText.compare(Index, 5, "node\n") == 0)
			{
				++NodeLines;
			}
		}
		if (!Expect(NodeLines == 2, "clearing retained cycles destroys both nodes"))
		{
			std::cerr << Host.PrintText;
			return EXIT_FAILURE;
		}
	}

	// Retained destructors must observe intact field values on Clear/teardown:
	// owners are destroyed before the objects they own, and cycles are only
	// severed after every destructor has run.
	{
		struct FFieldHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(std::string_view) override
			{
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;

		};

		FFieldHost Host;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetHotReloadEnabled(true);
		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"class Leaf { public: int tag = 0; ~Leaf() { print(tag); } }\n"
			"class Owner {\n"
			"    public: Leaf child;\n"
			"    ~Owner() { print(child.tag); }\n"
			"}\n"
			"Owner owner = Owner();\n"
			"void main() { owner.child = Leaf(); owner.child.tag = 42; }\n",
			"<hot-reload-field-visibility>");
		if (!Expect(LoadResult.bSuccess, "field visibility load")
			|| !Expect(Session.Run().bSuccess, "field visibility run"))
		{
			std::cerr << Host.PrintText;
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		Session.Clear();
		// Owner's destructor reads child.tag (42) before Leaf is destroyed, then
		// Leaf's destructor reports its own tag (42).
		if (!Expect(
				Host.PrintText == "42\n42\n",
				"teardown destructors see intact owned fields"))
		{
			std::cerr << Host.PrintText;
			return EXIT_FAILURE;
		}
	}

	{
		struct FCaptureHost final : VeroScript::IEngineHost
		{
			//~ Begin VeroScript::IEngineHost Interface
			void OnPrintLine(const std::string_view Line) override
			{
				PrintText.append(Line);
				PrintText.push_back('\n');
			}

			void OnDiagnosticLine(const std::string_view Line) override
			{
				DiagnosticText.append(Line);
				DiagnosticText.push_back('\n');
			}
			//~ End VeroScript::IEngineHost Interface

			std::string PrintText;
			std::string DiagnosticText;

		};

		struct FNativeState
		{
			int Sum = 0;
			std::string LastMessage;
		};

		const auto AddInts = [](
			const VeroScript::FNativeCallContext& Context,
			VeroScript::FValue& OutReturn,
			std::string& OutError) -> bool
		{
			(void)OutError;
			auto* const State = static_cast<FNativeState*>(Context.UserData);
			const int Left = std::get<std::int32_t>(Context.Args[0]);
			const int Right = std::get<std::int32_t>(Context.Args[1]);
			const int Result = Left + Right;
			if (State)
			{
				State->Sum = Result;
			}
			OutReturn = static_cast<std::int32_t>(Result);
			return true;
		};

		const auto LogMessage = [](
			const VeroScript::FNativeCallContext& Context,
			VeroScript::FValue& OutReturn,
			std::string& OutError) -> bool
		{
			(void)OutReturn;
			(void)OutError;
			auto* const State = static_cast<FNativeState*>(Context.UserData);
			if (State)
			{
				State->LastMessage = std::get<std::string>(Context.Args[0]);
			}
			return true;
		};

		FCaptureHost Host;
		FNativeState State;
		VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Expect(
				Session.RegisterNative(
					"add_ints",
					VeroScript::ENativeType::Int,
					{VeroScript::ENativeType::Int, VeroScript::ENativeType::Int},
					AddInts,
						&State),
				"RegisterNative add_ints"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Session.RegisterNative(
					"log_message",
					VeroScript::ENativeType::Void,
					{VeroScript::ENativeType::String},
					LogMessage,
						&State),
				"RegisterNative log_message"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				!Session.RegisterNative(
					"print",
					VeroScript::ENativeType::Void,
					{},
					LogMessage,
						&State),
				"RegisterNative should reject builtin name"))
		{
			return EXIT_FAILURE;
		}

		const VeroScript::FRunResult LoadResult = Session.LoadSource(
			"void main()\n"
			"{\n"
			"    int total = add_ints(40, 2);\n"
			"    print(total);\n"
			"    log_message(\"hello-native\");\n"
			"}\n",
			"<native-test>");
		if (!Expect(LoadResult.bSuccess, "native LoadSource should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.PrintText.clear();
		const VeroScript::FRunResult RunResult = Session.Run();
		if (!Expect(RunResult.bSuccess, "native Run should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		if (!Expect(Host.PrintText == "42\n", "native add_ints output"))
		{
			std::cerr << "got:\n"
					  << Host.PrintText;
			return EXIT_FAILURE;
		}
		if (!Expect(State.Sum == 42, "native UserData sum"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(State.LastMessage == "hello-native", "native void callback"))
		{
			std::cerr << "got message: " << State.LastMessage << '\n';
			return EXIT_FAILURE;
		}

		Session.Clear();
		if (!Expect(!Session.IsLoaded(), "Clear should unload after native test"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FRunResult ReloadNatives = Session.LoadSource(
			"void main() { print(add_ints(1, 2)); }\n",
			"<native-persist>");
		if (!Expect(ReloadNatives.bSuccess, "natives should persist across Clear"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		const VeroScript::FRunResult PersistRun = Session.Run();
		if (!Expect(PersistRun.bSuccess && Host.PrintText == "3\n", "native after Clear"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		const auto Scale = [](
			const VeroScript::FNativeCallContext& Context,
			VeroScript::FValue& OutReturn,
			std::string& OutError) -> bool
		{
			(void)OutError;
			const double Value = std::get<double>(Context.Args[0]);
			OutReturn = Value * 2.0;
			return true;
		};
		if (!Expect(
				Session.RegisterNative(
					"scale",
					VeroScript::ENativeType::Double,
					{VeroScript::ENativeType::Double},
					Scale,
					nullptr),
				"RegisterNative scale"))
		{
			return EXIT_FAILURE;
		}
		const VeroScript::FRunResult ScaleLoad = Session.LoadSource(
			"void main() { print(scale(21)); }\n",
			"<native-double>");
		if (!Expect(ScaleLoad.bSuccess, "native double LoadSource should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		const VeroScript::FRunResult ScaleRun = Session.Run();
		if (!Expect(ScaleRun.bSuccess && Host.PrintText == "42\n", "native int→double coercion"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult BoolScaleLoad = Session.LoadSource(
			"void main() { print(scale(true)); }\n",
			"<native-bool-double>");
		if (!Expect(BoolScaleLoad.bSuccess, "native bool→double LoadSource should succeed"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
		Host.PrintText.clear();
		const VeroScript::FRunResult BoolScaleRun = Session.Run();
		if (!Expect(BoolScaleRun.bSuccess && Host.PrintText == "2\n", "native bool→double coercion"))
		{
			std::cerr << Host.PrintText << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult VoidAsValue = Session.LoadSource(
			"void main()\n"
			"{\n"
			"    int x = log_message(\"nope\");\n"
			"    print(x);\n"
			"}\n",
			"<native-void-value>");
		if (!Expect(!VoidAsValue.bSuccess, "void native as value should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.DiagnosticText.find("cannot convert void") != std::string::npos,
				"void native as value should report cannot convert void"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult AutoVoid = Session.LoadSource(
			"void main()\n"
			"{\n"
			"    auto x = log_message(\"nope\");\n"
			"    print(x);\n"
			"}\n",
			"<native-auto-void>");
		if (!Expect(!AutoVoid.bSuccess, "auto from void native should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.DiagnosticText.find("cannot convert void") != std::string::npos,
				"auto from void native should report cannot convert void"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult VoidInExpr = Session.LoadSource(
			"void main() { print(1 + log_message(\"x\")); }\n",
			"<native-void-expr>");
		if (!Expect(!VoidInExpr.bSuccess, "void native in arithmetic should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.DiagnosticText.find("cannot use void value in expression") != std::string::npos,
				"void native in arithmetic should report void-in-expression"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult VoidCompound = Session.LoadSource(
			"void main()\n"
			"{\n"
			"    int x = 1;\n"
			"    x += log_message(\"x\");\n"
			"    print(x);\n"
			"}\n",
			"<native-void-compound>");
		if (!Expect(!VoidCompound.bSuccess, "void native in compound assign should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.DiagnosticText.find("cannot use void value in expression") != std::string::npos,
				"void native in compound assign should report void-in-expression"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}

		Host.DiagnosticText.clear();
		const VeroScript::FRunResult VoidPrint = Session.LoadSource(
			"void main() { print(log_message(\"x\")); }\n",
			"<native-void-print>");
		if (!Expect(!VoidPrint.bSuccess, "print(void native) should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Host.DiagnosticText.find("cannot use void value in expression") != std::string::npos,
				"print(void native) should report void-in-expression"))
		{
			std::cerr << Host.DiagnosticText;
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<int,array<int>> m = map<int,array<int>>();\n"
			"    m[1] = array<int>();\n"
			"    m[1].push(7);\n"
			"    m[1].push(8);\n"
			"    print(len(m[1]));\n"
			"    print(m[1][0] + m[1][1]);\n"
			"    m[1][0] = 9;\n"
			"    print(m[1][0]);\n"
			"    m[1][1] += 2;\n"
			"    print(m[1][1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n15\n9\n10\n",
			"map<int,array<int>> nested generic close and share"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void bump(int set) {\n"
			"    print(set);\n"
			"}\n"
			"void main() {\n"
			"    int set = 3;\n"
			"    int map = 4;\n"
			"    int queue = 5;\n"
			"    print(set + map + queue);\n"
			"    bump(9);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"12\n9\n",
			"container soft keywords as names"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int map = 3;\n"
			"    int x = 5;\n"
			"    print(map < x);\n"
			"    map < x;\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"soft keyword comparison statement and expression"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    int set = 2;\n"
			"    int queue = 9;\n"
			"    print(set < queue);\n"
			"    set < queue;\n"
			"    int map = 1;\n"
			"    print(map < set);\n"
			"    map < set;\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\n",
			"soft keyword vs soft keyword comparison"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"int set = 2;\n"
			"int queue = 9;\n"
			"set < queue;\n"
			"void main() {\n"
			"    print(set < queue);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"top-level soft keyword vs soft keyword comparison"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Box { int x; }\n"
			"void main() { Box b = map<int,int>(); print(b); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "struct = map should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot convert") != std::string::npos,
				"struct = map mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct S { int x; }\n"
			"void main() { S s = S(); S t = s.no_such(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "unknown method assign should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("unknown method: no_such") != std::string::npos,
				"unknown method compile message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"class Node {\n"
			"    public:\n"
			"    Node next;\n"
			"    map<int,Node> kids = map<int,Node>();\n"
			"}\n"
			"void main() {\n"
			"    Node n = Node();\n"
			"    print(n.next == nullptr);\n"
			"    n.next = Node();\n"
			"    print(n.next == nullptr);\n"
			"    Node c = Node();\n"
			"    n.kids[1] = c;\n"
			"    print(n.kids.has(1));\n"
			"    print(n.kids[1] == c);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\nfalse\ntrue\ntrue\n",
			"class self-reference field and map value"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Node { Node next; }\n"
			"void main() { Node n = Node(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "struct self-reference should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("recursive type") != std::string::npos,
				"struct recursive type message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"class C { public: int x; }\n"
			"void main() {\n"
			"    C a[2];\n"
			"    print(a[0] == nullptr);\n"
			"    a[0] = C();\n"
			"    a[0].x = 5;\n"
			"    print(a[0].x);\n"
			"    a[1] = nullptr;\n"
			"    print(a[1] == nullptr);\n"
			"    map<int,C> m = map<int,C>();\n"
			"    m[1] = nullptr;\n"
			"    print(m.has(1));\n"
			"    print(m[1] == nullptr);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n5\ntrue\ntrue\ntrue\n",
			"class null defaults in native array and map"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class C { public: int x; }\n"
			"C make() { return array<int>(); }\n"
			"void main() { print(make()); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "class return array should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("cannot convert") != std::string::npos,
				"class return array mismatch message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"int map = 3;\n"
			"int x = 5;\n"
			"map < x;\n"
			"void main() {\n"
			"    print(map < x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n",
			"top-level soft keyword comparison statement"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<int>> a = array<int>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "extra generic close should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("unexpected '>' after type name") != std::string::npos,
				"extra generic close message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    m[\"b\"] = 2;\n"
			"    print(len(m));\n"
			"    print(m[\"a\"] + m[\"b\"]);\n"
			"    print(m.has(\"a\"));\n"
			"    print(m.has(\"z\"));\n"
			"    print(m.remove(\"a\"));\n"
			"    print(len(m));\n"
			"    array<string> ks = m.keys();\n"
			"    print(len(ks));\n"
			"    print(ks[0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n3\ntrue\nfalse\ntrue\n1\n1\nb\n",
			"map<string,int> get set has remove keys len"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<int,string> m = map<int,string>();\n"
			"    m[1] = \"one\";\n"
			"    m[2] = \"two\";\n"
			"    print(m[1]);\n"
			"    print(m.has(2));\n"
			"    print(len(m));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"one\ntrue\n2\n",
			"map<int,string> basic"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    map<string,int> a = map<string,int>();\n"
			"    map<string,int> b = map<string,int>();\n"
			"    print(a == b);\n"
			"    b = a;\n"
			"    a[\"x\"] = 7;\n"
			"    print(a == b);\n"
			"    print(b[\"x\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"false\ntrue\n7\n",
			"map share semantics and identity equality"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    map<string,int> m = map<string,int>();\n"
			"    print(m[\"missing\"]);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map missing key get should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("map key not found") != std::string::npos, "map missing key message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { map<string,int> m = map<int,int>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "map type mismatch should fail compile"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("container type mismatch") != std::string::npos,
				"map type mismatch message"))
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
			"    map<string,map<int,int>> m = map<string,map<int,int>>();\n"
			"    m[\"a\"] = map<int,int>();\n"
			"    m[\"a\"][1] = 7;\n"
			"    print(m[\"a\"][1]);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "nested map value should run"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Output.str() == "7\n", "nested map value output"))
		{
			std::cerr << Output.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    set<int> s = set<int>();\n"
			"    s.add(1);\n"
			"    s.add(2);\n"
			"    s.add(1);\n"
			"    print(len(s));\n"
			"    print(s.has(1));\n"
			"    print(s.has(3));\n"
			"    print(s.remove(2));\n"
			"    print(len(s));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\ntrue\nfalse\ntrue\n1\n",
			"set<int> add has remove len"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    set<string> s = set<string>();\n"
			"    s.add(\"hi\");\n"
			"    print(s.has(\"hi\"));\n"
			"    set<string> t = s;\n"
			"    t.add(\"bye\");\n"
			"    print(len(s));\n"
			"    print(s == t);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n2\ntrue\n",
			"set<string> share semantics"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { set<double> s = set<double>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "set<double> should fail"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    queue<int> q = queue<int>();\n"
			"    q.push(1);\n"
			"    q.push(2);\n"
			"    print(len(q));\n"
			"    print(q.front());\n"
			"    print(q.pop());\n"
			"    print(q.pop());\n"
			"    print(len(q));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n1\n1\n2\n0\n",
			"queue<int> push pop front len"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    queue<int> q = queue<int>();\n"
			"    print(q.pop());\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "empty queue pop should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("pop from empty queue") != std::string::npos, "empty queue pop message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    stack<int> s = stack<int>();\n"
			"    s.push(1);\n"
			"    s.push(2);\n"
			"    print(len(s));\n"
			"    print(s.top());\n"
			"    print(s.pop());\n"
			"    print(s.pop());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n2\n2\n1\n",
			"stack<int> push pop top len"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    stack<int> s = stack<int>();\n"
			"    print(s.top());\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "empty stack top should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("top of empty stack") != std::string::npos, "empty stack top message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    queue<int> a = queue<int>();\n"
			"    queue<int> b = a;\n"
			"    a.push(9);\n"
			"    print(b.front());\n"
			"    print(a == b);\n"
			"    stack<double> s = stack<double>();\n"
			"    s.push(1.5);\n"
			"    print(s.top());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\ntrue\n1.5\n",
			"queue share and stack<double>"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void main() {\n"
			"    queue<map<int,int>> q = queue<map<int,int>>();\n"
			"    map<int,int> m = {1: 10};\n"
			"    q.push(m);\n"
			"    print(q.front()[1]);\n"
			"    print(len(q));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"10\n1\n",
			"queue of map nesting"))
	{
		return EXIT_FAILURE;
	}

	// ordered_map string keys: insert a,b,c; remove b; keys order; has; len; values
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    m[\"a\"] = 1;\n"
			"    m[\"b\"] = 2;\n"
			"    m[\"c\"] = 3;\n"
			"    m.remove(\"b\");\n"
			"    array<string> k = m.keys();\n"
			"    print(len(k));\n"
			"    print(k[0]);\n"
			"    print(k[1]);\n"
			"    print(m.has(\"a\"));\n"
			"    print(m.has(\"b\"));\n"
			"    print(len(m));\n"
			"    print(m[\"a\"]);\n"
			"    print(m[\"c\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\na\nc\ntrue\nfalse\n2\n1\n3\n",
			"ordered_map<string,int> keys insertion order after remove"))
	{
		return EXIT_FAILURE;
	}

	// ordered_map int keys basic
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<int,string> m = ordered_map<int,string>();\n"
			"    m[10] = \"x\";\n"
			"    m[20] = \"y\";\n"
			"    m[10] = \"z\";\n"
			"    array<int> k = m.keys();\n"
			"    print(len(k));\n"
			"    print(k[0]);\n"
			"    print(k[1]);\n"
			"    print(m[10]);\n"
			"    print(m[20]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n10\n20\nz\ny\n",
			"ordered_map<int,string> update keeps insertion order"))
	{
		return EXIT_FAILURE;
	}

	// ordered_map share + == identity
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,int> a = ordered_map<string,int>();\n"
			"    ordered_map<string,int> b = a;\n"
			"    a[\"x\"] = 7;\n"
			"    print(b[\"x\"]);\n"
			"    print(a == b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\ntrue\n",
			"ordered_map share and identity"))
	{
		return EXIT_FAILURE;
	}

	// missing key error
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    ordered_map<string,int> m = ordered_map<string,int>();\n"
			"    print(m[\"missing\"]);\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "ordered_map missing key should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("ordered_map key not found") != std::string::npos,
				"ordered_map missing key message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	// nesting ordered_map of ordered_map
	if (!ExpectOutput(
			"void main() {\n"
			"    ordered_map<string,ordered_map<string,int>> m ="
			" ordered_map<string,ordered_map<string,int>>();\n"
			"    ordered_map<string,int> inner = {\"inner\": 3};\n"
			"    m[\"outer\"] = inner;\n"
			"    print(m[\"outer\"][\"inner\"]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n",
			"ordered_map of ordered_map nesting"))
	{
		return EXIT_FAILURE;
	}

	// soft keyword ordered_map as variable when not type
	if (!ExpectOutput(
			"void main() {\n"
			"    int ordered_map = 42;\n"
			"    print(ordered_map);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"42\n",
			"ordered_map as variable name"))
	{
		return EXIT_FAILURE;
	}

	// deque push_back/front pop_back/front front/back len
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    d.push_back(1);\n"
			"    d.push_back(2);\n"
			"    d.push_front(0);\n"
			"    print(len(d));\n"
			"    print(d.front());\n"
			"    print(d.back());\n"
			"    print(d.pop_front());\n"
			"    print(d.pop_back());\n"
			"    print(len(d));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n0\n2\n0\n2\n1\n",
			"deque<int> push_back push_front pop_back pop_front front back len"))
	{
		return EXIT_FAILURE;
	}

	// empty pop_front error
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    deque<int> d = deque<int>();\n"
			"    print(d.pop_front());\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "empty deque pop_front should fail"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("pop_front from empty deque") != std::string::npos,
				"empty deque pop_front message"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	// deque share
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<int> a = deque<int>();\n"
			"    deque<int> b = a;\n"
			"    a.push_back(5);\n"
			"    print(b.front());\n"
			"    print(a == b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\ntrue\n",
			"deque share and identity"))
	{
		return EXIT_FAILURE;
	}

	// queue of deque nesting
	if (!ExpectOutput(
			"void main() {\n"
			"    queue<deque<int>> q = queue<deque<int>>();\n"
			"    deque<int> d = {1, 2};\n"
			"    q.push(d);\n"
			"    print(q.front()[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"queue of deque nesting"))
	{
		return EXIT_FAILURE;
	}

	// deque of map nesting
	if (!ExpectOutput(
			"void main() {\n"
			"    deque<map<int,int>> d = deque<map<int,int>>();\n"
			"    map<int,int> m = {7: 8};\n"
			"    d.push_back(m);\n"
			"    print(d[0][7]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"8\n",
			"deque of map nesting"))
	{
		return EXIT_FAILURE;
	}

	// v1.18: map<wstring,int>
