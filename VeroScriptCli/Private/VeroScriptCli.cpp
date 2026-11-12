// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCli.h"
#include "VeroScriptCliExtensions.h"
#include "VeroScriptCliProject.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptBytecodeVmAssembly.h"

#include "VeroScriptCliRepl.h"
#include "VeroScriptCliWatch.h"

#include "VeroScriptCppAot.h"
#include "VeroScriptCSharpAot.h"
#include "VeroScriptPythonAot.h"
#include "VeroScript.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPath.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptSession.h"
#include "VeroScriptTooling.h"
#include "VeroScriptVersion.h"
#include "VeroScriptCompile.h"
#if defined(VEROSCRIPT_WITH_BUILD_SYSTEM)
#include "VeroScriptCliBuild.h"
#endif
#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptApplication.h"
#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
#include "VeroScriptApplicationPackages.h"
#endif
#endif
#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
#include "VeroScriptPackageManager.h"
#endif

#if defined(VEROSCRIPT_WITH_GUI)
#include "VeroScriptGuiTools.h"
#include "VeroScriptGuiServices.h"
#endif

#include <charconv>
#include <stdexcept>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace VeroScript::Cli
{
	namespace
	{
		void PrintUsage()
		{
			std::cout
			    << "VeroScript interpreter\n"
			    << "\n"
			    << "Usage:\n"
			    << "  veroscript project init [--path <directory>]\n"
			    << "  veroscript build [--path <project>] [--plan] [--json] [--target <name>]\n"
			    << "  veroscript [options] <script.vs|app.vsapp> [args...]\n"
			    << "  veroscript --emit-package <app.vsapp|library.vslib> [options] <script.vs>\n"
			    << "  veroscript --debug [options] [<script.vs> [args...]]\n"
			    << "  veroscript --debug --dap-port=<N> [options] [<script.vs> [args...]]\n"
			    << "  veroscript aot [--target=cpp|csharp|python] [options] <script.vs> -o <output>\n"
			    << "  veroscript aot --target=csharp <script.vs> --output-dir <directory> [--bundle-name "
			       "<name>]\n"
			    << "  veroscript aot --target=python <script.vs> --output-dir <directory> [--bundle-name "
			       "<name>]\n"
			    << "  veroscript package inspect <app.vsapp|library.vslib>\n"
			    << "  veroscript bindings validate <catalog.json>\n"
			    << "  veroscript bindings format <catalog.json> [-o <out.json>]\n"
			    << "  veroscript -e <source>\n"
			    << "  veroscript -i\n"
			    << "\n"
			    << "Options:\n"
			    << "      --cli-module <file>    Load a native CLI module (repeatable)\n"
			    << "  -e, --eval <source>       Execute source string\n"
			    << "  -i, --interactive         Interactive REPL\n"
			    << "  -I, --import-path <path>  Append module search path\n"
			    << "  --compiler-extension <file> Load an explicit compilation extension\n"
			    << "  --compiler-config <key=value> Configure compilation extensions\n"
			    << "  --no-compiler-imports Disable source compilation-extension imports\n"
			    << "  --compiler-instructions <n> / --compiler-timeout <ms> Set compile limits\n"
			    << "  compiler-package [options] <source.vs> <output.vslib> Build an extension\n"
			    << "      --implicit-import <file>  Import public declarations/macros; repeatable\n"
			    << "      --no-circular-imports  Reject cyclic module graphs (allowed by default)\n"
			    << "      --allow-project-wide-access  Share non-exported project declarations\n"
			    << "      --stdlib-path <path>  Prepend stdlib search root\n"
			    << "      --application-options <file>  Explicit application cache/key/trust options\n"
			    << "      --binding-catalog <path>  Host binding catalog JSON; repeat to merge\n"
			    << "  -D, --define <name[=value]>  Define object-like macro / symbol\n"
			    << "      --macro <NAME(params)=body>  Define structured host-style function macro\n"
			    << "  -o, --output <path>       AOT / bindings format output path\n"
			    << "      --output-dir <path>   C# or Python AOT multi-file bundle directory\n"
			    << "      --bundle-name <name>  Portable bundle stem (default: input stem)\n"
			    << "      --manifest <path>     C# bundle manifest path (default: output directory)\n"
			    << "      --target <name>       AOT target: cpp (default), csharp, or python\n"
			    << "      --check               Compile script only (no execution)\n"
			    << "      --debug               DAP stdio debug host (same as veroscript-dap)\n"
			    << "      --dap-port=<N>        With --debug: TCP DAP on port N (1..65535)\n"
			    << "      --dap-bind=<addr>     With --dap-port: bind address (default 127.0.0.1)\n"
			    << "      --bc-cache=off|on     Disable/enable .vsbc bytecode cache (default: on)\n"
			    << "      --bc-cache-dir <path> Shared bytecode cache root (default: platform user cache)\n"
			    << "      --emit-package <path> Emit .vsapp application or .vslib library\n"
			    << "      --package-file <file> Add a source module to the package; repeatable\n"
			    << "      --package-version <N> Package version (default: 0)\n"
			    << "      --strip-package-debug Remove source-file and line tables from package\n"
			    << "      --backend=vm|mixed|jit  Execution backend (default: mixed)\n"
			    << "      --vm-backend=<auto|none|id|file|directory> Select the interpreter\n"
			    << "      --jit-backend=<auto|none|id|file|directory> Select a JIT backend\n"
			    << "      --lang <tag>          Diagnostic language (BCP 47; discovered catalogs, default: "
			       "system)\n"
			    << "      --watch               Re-run script when files change\n"
			    << "      --watch-once          Like --watch, but exit on any non-zero run result\n"
			    << "      --repl-strict         REPL exits non-zero if last input failed\n"
			    << "  -h, --help                Show help\n"
			    << "      --version             Show version\n"
			    << "\n"
			    << "Debug:\n"
			    << "  --debug speaks Debug Adapter Protocol on stdin/stdout.\n"
			    << "  --debug --dap-port=<N> listens on TCP instead (for IDE Attach).\n"
			    << "  IDE Attach sends DAP attach (live bindings); F6 uses launch + stubs.\n"
			    << "  disconnect terminateDebuggee=true stops a multi-session TCP host.\n"
			    << "  Optional <script.vs> and trailing args pre-seed the launch/attach request.\n"
			    << "  veroscript-dap is a thin alias of the same host.\n"
			    << "  Embed hosts: VeroScriptDapTcp::Run + FDapStdioRunOptions::LiveBindings.\n"
			    << "\n"
			    << "Backends:\n"
			    << "  vm     Bytecode interpreter only (full debug/hot-reload).\n"
			    << "  mixed  JIT when supported, otherwise fall back to vm (default).\n"
			    << "  jit    Prefer compile-all; unsupported bodies still fall back to vm.\n"
			    << "  Debugger and hot-reload sessions still force effective vm"
			       " (requested mixed/jit is kept for after detach).\n"
			    << "\n"
			    << "Environment:\n"
			    << "  VERO_MAX_CALL_DEPTH sets the total recursion limit"
			       " (default: 16384).\n"
			    << "\n"
			    << "AOT:\n"
			    << "  --target=cpp emits a standalone C++17 translation unit.\n"
			    << "  --target=csharp emits one .NET 8+ C# compilation unit suitable for NativeAOT.\n"
			    << "  --target=python emits a standalone Python 3.11-3.15 program.\n"
			    << "  The default target remains cpp for command-line compatibility.\n"
			    << "  C++ target support:\n"
			    << "  Supported: free fn overloads, uniform extension calls, and open generics\n"
			    << "  as C++ templates\n"
			    << "  (class/struct/interface; where T:I → enable_if_t/is_convertible_v, C++17),\n"
			    << "  class (RC shared_ptr) / struct (value), interface/inheritance/virtual,\n"
			    << "  shared<T> / handle<T>, nullptr, operator overloads (MVP), fn/lambda captures,\n"
			    << "  delegate/multicast_delegate (runtime glue), pair/map entries,\n"
			    << "  concurrent OS-thread workers with thread-local script state and channel sharing,\n"
			    << "  default/variadic params, void/int main() with optional string[] args,\n"
			    << "  array/map/multimap/set/multiset, ordered variants,\n"
			    << "  queue/priority_queue/stack/deque, control flow, using/alias, enum,\n"
			    << "  explicit constructors, mutable fields, readonly<T>, cast/try_cast/const_cast,\n"
			    << "  switch, foreach, try/catch/finally, throw/rethrow, goto/label, ++/--,\n"
			    << "  builtins, stdlib __* (math/string/io/path),\n"
			    << "  ref params, imports.\n"
			    << "  Generated TU targets C++17 (where-bounds emit enable_if/type_traits).\n"
			    << "  Concurrent uses OS-thread workers, thread-local script state, and channel sharing.\n"
			    << "\n"
			    << "bindings:\n"
			    << "  validate   Load catalog JSON and report errors\n"
			    << "  format     Load then rewrite catalog JSON (normalize)\n"
			    << "  Embed hosts: VeroScriptSession::ExportBindingsToFile(path)\n"
			    << "\n"
			    << "Script args:\n"
			    << "  Arguments after <script.vs> are passed to main(string[] args).\n"
			    << "  main() ignores extra arguments. The script path is not in args.\n"
			    << "\n"
			    << "Environment:\n"
			    << "  VERO_PATH                 Module search paths (Windows: ;  Unix: :)\n"
			    << "  VERO_STDLIB_PATH          Stdlib search roots (Windows: ;  Unix: :)\n"
			    << "  VERO_CACHE_DIR            Shared bytecode cache directory\n"
			    << "  VERO_LANG                 Diagnostic language (BCP 47 catalog tag)\n"
			    << "  VERO_LOCALIZATION_DIR     Localization catalog root\n";
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			std::cout << "\nPackage management:\n"
			          << "  veroscript install [name[@version]|directory|archive.vspkg|archive.zip] [options]\n"
			          << "  veroscript install -r <requirements.txt> [options]  Install a dependency list\n"
			          << "  veroscript uninstall|enable|disable <name> [options]\n"
			          << "  veroscript list|verify|clean|env [options]\n"
			          << "  veroscript search|show <query> [options]\n"
			          << "  veroscript pack [directory] [--output=<archive.vspkg|archive.zip>] [options]\n"
			          << "  veroscript publish <archive.vspkg|archive.zip>... [options]\n"
			          << "  .vspkg and .zip use the same ZIP format; pack defaults to .vspkg\n"
			          << "  veroscript deploy --output=<new-directory> [options]\n"
			          << "  --root / --path <directory>  Select a component or project environment\n"
			          << "  --locked --offline --upgrade --development --prerelease --json\n"
			          << "  --registry-api=<url> --mirror=<url> --proxy=<url> --ca-file=<file>\n"
			          << "  --credential-env=<name> --credential-scope=<url>  Scoped authentication\n"
			          << "  --allow-hooks --input=<name=JSON> --secret-env=<name=ENV>\n"
			          << "  --packages-dir=<path> --cache-dir=<path> --lockfile=<path> --timeout-ms=<n>\n"
			          << "  --jit-backend=<auto|none|id|file|directory>  Backend selection for execution\n";
			#endif
		}

		bool IsOption(const std::string_view Arg)
		{
			return Arg.size() >= 2 && Arg[0] == '-' && Arg[1] != '\0';
		}

		std::string ResolveCliFilePath(const std::string_view Path, const std::string_view Base = {})
		{
			std::error_code Error;
			std::filesystem::path NativePath = PathUtil::FromUtf8(Path);
			if (NativePath.is_relative())
			{
				NativePath = Base.empty() ? std::filesystem::absolute(NativePath, Error)
				                          : PathUtil::FromUtf8(Base) / NativePath;
			}
			if (Error)
			{
				return PathUtil::Normalize(std::string(Path));
			}
			return PathUtil::ToUtf8(NativePath.lexically_normal());
		}

		#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
		void AddDefaultPackageStdlibPaths(std::vector<std::string>& Paths)
		{
			if (!Paths.empty()) return;
			const auto ExecutableRoot = PathUtil::FromUtf8(PathUtil::ExecutableDirectory());
			for (const auto& P : {ExecutableRoot / "stdlib", ExecutableRoot.parent_path() / "assets/stdlib"})
			{
				if (std::filesystem::is_directory(P))
				{
					Paths.push_back(PathUtil::ToUtf8(P));
				}
			}
		}
		#endif

		enum class EPackageFileKind : std::uint8_t
		{
			None,
			Application,
			Library,
		};

		EPackageFileKind PackageFileKind(const std::string_view Path)
		{
			std::string Extension =
			    PathUtil::FromUtf8(Path).extension().string();
			for (char& Character : Extension)
			{
				if (Character >= 'A' && Character <= 'Z')
				{
					Character = static_cast<char>(Character - 'A' + 'a');
				}
			}
			if (Extension == ".vsapp")
			{
				return EPackageFileKind::Application;
			}
			if (Extension == ".vslib")
			{
				return EPackageFileKind::Library;
			}
			return EPackageFileKind::None;
		}

		bool IsPackagePath(const std::string_view Path)
		{
			return PackageFileKind(Path) != EPackageFileKind::None;
		}

		bool ReadBinaryFile(
		    const std::string_view Path,
		    std::vector<std::uint8_t>& OutBytes,
		    std::string& OutError)
		try
		{
			OutBytes.clear();
			std::ifstream Input(
			    PathUtil::FromUtf8(Path),
			    std::ios::binary | std::ios::ate);
			// Size and contents come from the same open handle; the final EOF
			// check also rejects concurrent growth instead of accepting a prefix.
			if (!Input)
			{
				OutError = "cannot open package";
				return false;
			}
			const std::streamoff Size = Input.tellg();
			if (Size <= 0 || static_cast<std::uintmax_t>(Size) > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) || static_cast<std::uintmax_t>(Size) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) || static_cast<std::uint64_t>(Size) > VEROSCRIPT_PACKAGE_MAX_BYTES)
			{
				OutError = "package is empty or too large for this platform";
				return false;
			}
			OutBytes.resize(static_cast<std::size_t>(Size));
			Input.seekg(0, std::ios::beg);
			if (!Input.read(
			        reinterpret_cast<char*>(OutBytes.data()),
			        static_cast<std::streamsize>(Size)) ||
			    Input.peek() != std::char_traits<char>::eof())
			{
				OutError = "failed to read package";
				OutBytes.clear();
				return false;
			}
			return true;
		}
		catch (const std::bad_alloc&)
		{
			OutBytes.clear();
			OutError = "out of memory while reading package";
			return false;
		}
		catch (const std::length_error&)
		{
			OutBytes.clear();
			OutError = "invalid package allocation size";
			return false;
		}

		bool WriteBinaryFileAtomically(
		    const std::string_view Path,
		    const std::vector<std::uint8_t>& Bytes,
		    std::string& OutError)
		{
			const std::filesystem::path FinalPath = PathUtil::FromUtf8(Path);
			const std::filesystem::path Parent = FinalPath.parent_path();
			if (!Parent.empty())
			{
				std::error_code DirectoryError;
				std::filesystem::create_directories(Parent, DirectoryError);
				if (DirectoryError)
				{
					OutError = "cannot create output directory";
					return false;
				}
			}
			const std::filesystem::path TempPath =
			    PathUtil::MakeTemporarySiblingPath(FinalPath);
			{
				std::ofstream Output(
				    TempPath, std::ios::binary | std::ios::trunc);
				if (!Output)
				{
					OutError = "cannot open temporary output";
					return false;
				}
				Output.write(
				    reinterpret_cast<const char*>(Bytes.data()),
				    static_cast<std::streamsize>(Bytes.size()));
				if (!Output)
				{
					OutError = "failed to write output";
					Output.close();
					std::error_code RemoveError;
					std::filesystem::remove(TempPath, RemoveError);
					return false;
				}
				Output.close();
				if (!Output)
				{
					OutError = "failed to close output";
					std::error_code RemoveError;
					std::filesystem::remove(TempPath, RemoveError);
					return false;
				}
			}
			std::error_code RenameError;
			if (!PathUtil::ReplaceFileAtomically(
			        TempPath, FinalPath, RenameError))
			{
				std::error_code RemoveError;
				std::filesystem::remove(TempPath, RemoveError);
				OutError = "failed to replace output";
				return false;
			}
			return true;
		}

		bool WriteTextFileAtomically(
		    const std::string_view Path,
		    const std::string_view Text,
		    std::string& OutError)
		{
			return WriteBinaryFileAtomically(Path,
			    std::vector<std::uint8_t>(Text.begin(), Text.end()), OutError);
		}

		// 0 = not a define flag; 1 = consumed; 2 = error already printed.
		int TryParseDefineOption(
		    const std::string_view Arg,
		    const int Argc,
		    char** Argv,
		    int& Index,
		    std::vector<std::string>& OutOwnedDefines)
		{
			const auto AcceptSpec = [&](const std::string_view Spec) -> int
			{
				std::string Name;
				std::string Body;
				std::string Error;
				if (!ParseDefineSpec(Spec, Name, Body, Error))
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 2;
				}
				OutOwnedDefines.emplace_back(Spec);
				return 1;
			};

			if (Arg == "-D" || Arg == "--define")
			{
				if (Index + 1 >= Argc)
				{
					std::cerr << "VeroScript: " << Arg << " requires a name\n";
					return 2;
				}
				const int Result = AcceptSpec(Argv[Index + 1]);
				if (Result == 1)
				{
					Index += 2;
				}
				return Result;
			}
			if (Arg.rfind("-D", 0) == 0 && Arg.size() > 2)
			{
				const int Result = AcceptSpec(Arg.substr(2));
				if (Result == 1)
				{
					Index += 1;
				}
				return Result;
			}
			return 0;
		}

		int TryParseCompilerOption(std::string_view Arg, int argc, char** argv, int& Index, FScriptCompileExtensionsOptions& Options)
		{
			if (Arg == "--no-compiler-imports")
			{
				Options.bAllowSourceImports = false;
				++Index;
				return 1;
			}
			if (Arg != "--compiler-extension" && Arg != "--compiler-config" &&
			    Arg != "--compiler-instructions" && Arg != "--compiler-timeout")
			{
				return 0;
			}
			if (Index + 1 >= argc)
			{
				std::cerr << "VeroScript: missing value for " << Arg << '\n';
				return -1;
			}
			const std::string_view Value = argv[Index + 1];
			if (Arg == "--compiler-extension")
			{
				if (Value.empty()) return -1;
				Options.Roots.push_back({ResolveCliFilePath(Value), {}});
			}
			else if (Arg == "--compiler-config")
			{
				const auto Equal = Value.find('=');
				if (Equal == 0 || Equal == Value.npos)
				{
					std::cerr << "VeroScript: --compiler-config expects key=value\n";
					return -1;
				}
				if (!Options.Configuration.emplace(Value.substr(0, Equal), Value.substr(Equal + 1)).second)
				{
					std::cerr << "VeroScript: duplicate compiler configuration key\n";
					return -1;
				}
			}
			else
			{
				std::uint64_t Number = 0;
				const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
				if (Parsed.ec != std::errc{} || Parsed.ptr != Value.data() + Value.size())
				{
					std::cerr << "VeroScript: invalid compiler limit\n";
					return -1;
				}
				if (Arg == "--compiler-instructions")
				{
					Options.MaxInstructions = Number;
				}
				else
				{
					Options.TimeoutMilliseconds = Number;
				}
			}
			Index += 2;
			return 1;
		}
		bool EmitConfiguredSource(std::string_view Target, std::string_view Input, std::string_view Output, const FCodegenOptions& Options, std::string& Error)
		{
			if (PathUtil::Equal(std::string(Input), std::string(Output)))
			{
				Error = "output path must differ from input";
				return false;
			}
			std::unique_ptr<ICodegenBackend> Backend;
			if (Target == "cpp")
			{
				Backend = std::make_unique<FCppCodegenBackend>();
			}
			else if (Target == "python")
			{
				Backend = std::make_unique<FPythonCodegenBackend>();
			}
			else
			{
				Backend = std::make_unique<FCSharpCodegenBackend>();
			}

			FCodegenResult Result;
			if (!Backend->EmitFromFile(Input, Options, Result))
			{
				Error = Result.Error;
				return false;
			}
			return WriteTextFileAtomically(Output, Result.Output, Error);
		}
		int RunCompilerPackage(const int argc, char** argv)
		{
			FCompileOptions Options;
			int Index = 1;
			while (Index < argc && IsOption(argv[Index]))
			{
				auto Arg = std::string_view(argv[Index]);
				const auto Parsed =
				    TryParseCompilerOption(Arg, argc, argv, Index, Options.ScriptCompileExtensions);
				if (Parsed < 0) return 2;
				if (Parsed > 0)
				{
					continue;
				}
				if (Arg == "-I" && Index + 1 < argc)
				{
					Options.ImportPaths.push_back(argv[Index + 1]);
					Index += 2;
					continue;
				}
				if (Arg == "--stdlib-path" && Index + 1 < argc)
				{
					Options.StdlibSearchPaths.push_back(argv[Index + 1]);
					Index += 2;
					continue;
				}
				std::cerr << "VeroScript: unknown compiler-package option: " << Arg << '\n';
				return 2;
			}
			if (argc - Index != 2)
			{
				std::cerr << "Usage: veroscript compiler-package [options] source.vs output.vslib\n";
				return 2;
			}
			const auto Input = ResolveCliFilePath(argv[Index]), Output = ResolveCliFilePath(argv[Index + 1]);
			if (PathUtil::Equal(Input, Output) || PackageFileKind(Output) != EPackageFileKind::Library)
			{
				std::cerr << "VeroScript: output must be a distinct .vslib file\n";
				return 2;
			}
			std::ifstream Stream(PathUtil::FromUtf8(Input), std::ios::binary);
			if (!Stream)
			{
				std::cerr << "VeroScript: cannot read compilation extension\n";
				return 2;
			}
			std::string Source((std::istreambuf_iterator<char>(Stream)), {});
			auto Result = CompileScriptCompileExtensionSource(Source, Input, Options);
			if (!Result.bSuccess)
			{
				std::cerr << Result.Error.Message << '\n';
				return 1;
			}
			FPackageBuildOptions Package;
			Package.Kind = EPackageKind::Library;
			Package.PackageName = PathUtil::ToUtf8(PathUtil::FromUtf8(Output).stem());
			Package.CompileExtension = *Result.CompileExtensionManifest;
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (!BuildPackage(*Result.Module, Package, Bytes, Error) ||
			    !WriteBinaryFileAtomically(Output, Bytes, Error))
			{
				std::cerr << Error << '\n';
				return 1;
			}
			return 0;
		}
		int RunAot(const int argc, char** argv, const std::string& Startup = {},
		    const std::string& Executable = {})
		{
			std::vector<std::string> ImportPaths;
			std::vector<std::string> StdlibSearchPaths;
			std::vector<std::string> ImplicitImportPaths;
			std::vector<std::string> Defines;
			std::vector<std::string> BindingCatalogPaths;
			bool bAllowCircularImports = true;
			bool bAllowProjectWideAccess = false;
			std::string_view OutputPath;
			std::string_view OutputDirectory;
			std::string_view BundleName;
			std::string_view ManifestPath;
			std::string_view ScriptPath;
			std::string_view Target = "cpp";
			FScriptCompileExtensionsOptions ScriptExtensions;
			#if VEROSCRIPT_RUNTIME_HAS_VM
			ScriptExtensions.VmBackendPath = BytecodeVm::RuntimeLibraryPath(Executable);
			#endif
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			Packages::FRunEnvironmentRequest PackageRequest;
			PackageRequest.StartupDirectory = Startup;
			PackageRequest.Executable = Executable;
			#endif
			int Index = 1;
			while (Index < argc)
			{
				const std::string_view Arg = argv[Index];
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				unsigned PackageConsumed = 0;
				Packages::FDiagnostic SelectionError;
				if (!Packages::ParseEnvironmentOption(Arg, Index + 1 < argc ? std::string_view(argv[Index + 1]) : std::string_view{},
				        PackageRequest.Selection, PackageConsumed, SelectionError))
				{
					std::cerr << "VeroScript packages [" << SelectionError.Code << "] " << SelectionError.Message << '\n';
					return 2;
				}
				if (PackageConsumed)
				{
					Index += static_cast<int>(PackageConsumed);
					continue;
				}

				#endif
				const int CompilerOption = TryParseCompilerOption(Arg, argc, argv, Index, ScriptExtensions);
				if (CompilerOption < 0) return 2;
				if (CompilerOption > 0)
				{
					continue;
				}

				if (Arg == "-h" || Arg == "--help" || Arg == "-?")
				{
					PrintUsage();
					return 0;
				}
				if (Arg == "-I" || Arg == "--import-path")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: " << Arg << " requires a path\n";
						return 2;
					}
					ImportPaths.emplace_back(argv[Index + 1]);
					Index += 2;
					continue;
				}
				if (Arg == "--implicit-import")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: " << Arg << " requires a file\n";
						return 2;
					}
					ImplicitImportPaths.emplace_back(ResolveCliFilePath(argv[Index + 1]));
					Index += 2;
					continue;
				}
				if (Arg == "--stdlib-path")
				{
					if (Index + 1 >= argc || std::string_view(argv[Index + 1]).empty())
					{
						std::cerr << "VeroScript: --stdlib-path requires a path\n";
						return 2;
					}
					StdlibSearchPaths.emplace_back(argv[Index + 1]);
					Index += 2;
					continue;
				}
				if (Arg == "--binding-catalog")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: --binding-catalog requires a path\n";
						return 2;
					}
					BindingCatalogPaths.emplace_back(argv[Index + 1]);
					Index += 2;
					continue;
				}
				if (Arg.rfind("--binding-catalog=", 0) == 0)
				{
					const std::string_view Path =
					    Arg.substr(std::string_view("--binding-catalog=").size());
					if (Path.empty())
					{
						std::cerr << "VeroScript: --binding-catalog expects a non-empty path\n";
						return 2;
					}
					BindingCatalogPaths.emplace_back(Path);
					++Index;
					continue;
				}
				if (Arg.rfind("--implicit-import=", 0) == 0)
				{
					const std::string_view Path =
					    Arg.substr(std::string_view("--implicit-import=").size());
					if (Path.empty())
					{
						std::cerr << "VeroScript: --implicit-import expects a non-empty file\n";
						return 2;
					}
					ImplicitImportPaths.emplace_back(ResolveCliFilePath(Path));
					++Index;
					continue;
				}
				if (Arg == "--no-circular-imports")
				{
					bAllowCircularImports = false;
					++Index;
					continue;
				}
				if (Arg == "--allow-circular-imports")
				{
					bAllowCircularImports = true;
					++Index;
					continue;
				}
				if (Arg == "--allow-project-wide-access")
				{
					bAllowProjectWideAccess = true;
					++Index;
					continue;
				}
				if (Arg == "--target")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: --target requires cpp, csharp, or python\n";
						return 2;
					}
					Target = argv[Index + 1];
					Index += 2;
					continue;
				}
				if (Arg.rfind("--target=", 0) == 0)
				{
					Target = Arg.substr(std::string_view("--target=").size());
					++Index;
					continue;
				}
				const int DefineParse = TryParseDefineOption(Arg, argc, argv, Index, Defines);
				if (DefineParse == 1)
				{
					continue;
				}
				if (DefineParse == 2)
				{
					return 2;
				}
				if (Arg == "-o" || Arg == "--output")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: " << Arg << " requires a path\n";
						return 2;
					}
					OutputPath = argv[Index + 1];
					Index += 2;
					continue;
				}
				if (Arg == "--output-dir" || Arg == "--bundle-name" || Arg == "--manifest")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: " << Arg << " requires a value\n";
						return 2;
					}
					if (Arg == "--output-dir")
					{
						OutputDirectory = argv[Index + 1];
					}
					else if (Arg == "--bundle-name")
					{
						BundleName = argv[Index + 1];
					}
					else
					{
						ManifestPath = argv[Index + 1];
					}
					Index += 2;
					continue;
				}
				if (IsOption(Arg))
				{
					std::cerr << "VeroScript: unknown aot option: " << Arg << '\n';
					return 2;
				}
				if (!ScriptPath.empty())
				{
					std::cerr << "VeroScript: unexpected argument: " << Arg << '\n';
					return 2;
				}
				ScriptPath = Arg;
				Index += 1;
			}

			if (ScriptPath.empty() || (OutputPath.empty() == OutputDirectory.empty()))
			{
				std::cerr << "VeroScript: aot requires <script.vs> and exactly one of -o or --output-dir\n";
				return 2;
			}
			if (Target != "cpp" && Target != "csharp" && Target != "python")
			{
				std::cerr << "VeroScript: --target expects cpp, csharp, or python\n";
				return 2;
			}
			if (!OutputDirectory.empty() && Target != "csharp" && Target != "python")
			{
				std::cerr << "VeroScript: --output-dir requires --target=csharp or --target=python\n";
				return 2;
			}
			if (OutputDirectory.empty() && (!BundleName.empty() || !ManifestPath.empty()))
			{
				std::cerr << "VeroScript: --bundle-name and --manifest require --output-dir\n";
				return 2;
			}
			if (!BindingCatalogPaths.empty() && Target != "csharp")
			{
				std::cerr << "VeroScript: aot --binding-catalog currently requires --target=csharp\n";
				return 2;
			}
			FHostBindingRegistry BindingRegistry;
			const IHostBindingCatalog* Bindings = nullptr;
			for (std::size_t CatalogIndex = 0;
			    CatalogIndex < BindingCatalogPaths.size(); ++CatalogIndex)
			{
				std::string CatalogError;
				if (!LoadBindingCatalogFile(BindingCatalogPaths[CatalogIndex],
				        BindingRegistry, CatalogError, CatalogIndex == 0))
				{
					std::cerr << "VeroScript: failed to load binding catalog '"
					          << BindingCatalogPaths[CatalogIndex] << "': " << CatalogError << '\n';
					return 2;
				}
				Bindings = &BindingRegistry;
			}
			FCodegenOptions CSharpOptions;
			CSharpOptions.ImportPaths = ImportPaths;
			CSharpOptions.StdlibSearchPaths = StdlibSearchPaths;
			CSharpOptions.ImplicitImportPaths = ImplicitImportPaths;
			CSharpOptions.Defines = Defines;
			CSharpOptions.bAllowCircularImports = bAllowCircularImports;
			CSharpOptions.bAllowProjectWideAccess = bAllowProjectWideAccess;
			CSharpOptions.Bindings = Bindings;
			CSharpOptions.ScriptCompileExtensions = ScriptExtensions;
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			PackageRequest.EntryFile = ScriptPath;
			std::shared_ptr<const Packages::FInstalledEnvironment> Installed;
			std::string PackageScriptPath;
			std::string PackageOutputPath;
			Packages::FDiagnostic PackageError;
			if (!Packages::ResolveRunEnvironment(PackageRequest, Installed, PackageError))
			{
				std::cerr << "VeroScript packages [" << PackageError.Code << "] " << PackageError.Message
				          << '\n';
				return 2;
			}
			if (Installed)
			{
				PackageScriptPath = ResolveCliFilePath(ScriptPath, Startup);
				ScriptPath = PackageScriptPath;
				if (!OutputPath.empty())
				{
					PackageOutputPath = ResolveCliFilePath(OutputPath, Startup);
					OutputPath = PackageOutputPath;
				}
				Packages::FCoreEnvironmentOptions Options;
				Options.ScriptCompileExtensions = ScriptExtensions;
				Options.EntryRoot =
				    Installed->GetLayout().Scope == Packages::EEnvironmentScope::Project
				    ? Installed->GetLayout().Root
				    : PathUtil::ToUtf8(
				          (PathUtil::FromUtf8(Startup) / PathUtil::FromUtf8(ScriptPath)).parent_path());
				Options.SelectedBackends["jit"] = "";
				Options.BaseBindings = Bindings ? &BindingRegistry : nullptr;
				Options.StdlibSearchPaths = StdlibSearchPaths;
				AddDefaultPackageStdlibPaths(Options.StdlibSearchPaths);
				if (!Installed->BuildCoreEnvironment(Options, CSharpOptions.ModuleEnvironment, PackageError))
				{
					std::cerr << "VeroScript packages [" << PackageError.Code << "] " << PackageError.Message
					          << '\n';
					return 2;
				}
				CSharpOptions.Bindings = CSharpOptions.ModuleEnvironment->GetDescription().Bindings.get();
			}
			#endif

			if (!OutputDirectory.empty())
			{
				std::string OwnedBundleName = BundleName.empty()
				    ? PathUtil::ToUtf8(PathUtil::FromUtf8(ScriptPath).stem())
				    : std::string(BundleName);
				if (Target == "python")
				{
					if (!ManifestPath.empty())
					{
						std::cerr << "VeroScript: Python AOT writes python-aot.manifest.json inside --output-dir\n";
						return 2;
					}
					std::error_code SourcePathError;
					const std::filesystem::path Source = std::filesystem::absolute(
					    PathUtil::FromUtf8(ScriptPath), SourcePathError)
					                                         .lexically_normal();
					std::error_code OutputPathError;
					const std::filesystem::path OutputRoot = std::filesystem::absolute(
					    PathUtil::FromUtf8(OutputDirectory), OutputPathError)
					                                             .lexically_normal();
					if (SourcePathError || OutputPathError)
					{
						std::cerr << "VeroScript: cannot resolve Python AOT input or output path\n";
						return 2;
					}
					const std::filesystem::path Relative = Source.lexically_relative(OutputRoot);
					if (!Relative.empty() && *Relative.begin() != "..")
					{
						std::cerr << "VeroScript: Python AOT output directory must not contain the input script\n";
						return 2;
					}
					FPythonAotSourceBundle Bundle;
					std::string Error;
					if (!EmitPythonSourceBundleFromFile(
					        ScriptPath, OwnedBundleName, CSharpOptions, Bundle, Error) ||
					    !WritePythonSourceBundle(Bundle, OutputDirectory, Error))
					{
						std::cerr << "VeroScript: " << Error << '\n';
						return 1;
					}
					std::cout << "Wrote Python source bundle to " << OutputDirectory << '\n';
					return 0;
				}
				FCSharpAotSourceBundle Bundle;
				std::string Error;
				if (!EmitCSharpSourceBundleFromFile(
				        ScriptPath, OwnedBundleName, CSharpOptions, Bundle, Error))
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 1;
				}
				const std::filesystem::path OutputRoot = PathUtil::FromUtf8(OutputDirectory);
				std::vector<std::pair<std::string, std::string_view>> Outputs;
				Outputs.reserve(Bundle.Files.size() + 1);
				for (const FCSharpAotSourceFile& File : Bundle.Files)
				{
					const std::string Path = PathUtil::ToUtf8(OutputRoot / PathUtil::FromUtf8(File.RelativePath));
					if (PathUtil::Equal(Path, std::string(ScriptPath)))
					{
						std::cerr << "VeroScript: generated output must differ from the input script\n";
						return 2;
					}
					Outputs.emplace_back(Path, File.Contents);
				}
				const std::string OwnedManifestPath = ManifestPath.empty()
				    ? PathUtil::ToUtf8(OutputRoot / PathUtil::FromUtf8(OwnedBundleName + ".manifest.json"))
				    : std::string(ManifestPath);
				if (PathUtil::Equal(OwnedManifestPath, std::string(ScriptPath)))
				{
					std::cerr << "VeroScript: manifest path must differ from the input script\n";
					return 2;
				}
				for (const auto& Output : Outputs)
				{
					if (PathUtil::Equal(Output.first, OwnedManifestPath))
					{
						std::cerr << "VeroScript: manifest and generated source paths must be distinct\n";
						return 2;
					}
				}
				for (const auto& Output : Outputs)
				{
					if (!WriteTextFileAtomically(Output.first, Output.second, Error))
					{
						std::cerr << "VeroScript: " << Error << '\n';
						return 1;
					}
				}
				if (!WriteTextFileAtomically(OwnedManifestPath, Bundle.ManifestJson, Error))
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 1;
				}
				std::cout << "Wrote C# source bundle to " << OutputDirectory << '\n';
				return 0;
			}

			std::string Error;
			bool bEmitted = false;
			if (Target == "csharp" && Bindings)
			{
				if (PathUtil::Equal(std::string(ScriptPath), std::string(OutputPath)))
				{
					Error = "output path must differ from the input script: " + std::string(ScriptPath);
				}
				else
				{
					FCSharpCodegenBackend Backend;
					FCodegenResult Result;
					bEmitted = Backend.EmitFromFile(ScriptPath, CSharpOptions, Result) && WriteTextFileAtomically(OutputPath, Result.Output, Error);
					if (!Result.Error.empty())
					{
						Error = std::move(Result.Error);
					}
				}
			}
			else
			{
				bEmitted = EmitConfiguredSource(Target, ScriptPath, OutputPath, CSharpOptions, Error);
			}
			if (!bEmitted)
			{
				std::cerr << "VeroScript: " << Error << '\n';
				return 1;
			}
			std::cout << "Wrote " << OutputPath << '\n';
			return 0;
		}

		int RunBindings(const int argc, char** argv)
		{
			if (argc < 2)
			{
				std::cerr << "VeroScript: bindings requires validate|format <catalog.json>\n";
				return 2;
			}
			const std::string_view Action = argv[1];
			if (Action == "-h" || Action == "--help" || Action == "-?")
			{
				PrintUsage();
				return 0;
			}
			if (Action != "validate" && Action != "format")
			{
				std::cerr << "VeroScript: unknown bindings action: " << Action << '\n';
				return 2;
			}

			std::string_view InputPath;
			std::string_view OutputPath;
			for (int Index = 2; Index < argc; ++Index)
			{
				const std::string_view Arg = argv[Index];
				if (Arg == "-o" || Arg == "--output")
				{
					if (Index + 1 >= argc)
					{
						std::cerr << "VeroScript: " << Arg << " requires a path\n";
						return 2;
					}
					OutputPath = argv[Index + 1];
					Index += 1;
					continue;
				}
				if (IsOption(Arg))
				{
					std::cerr << "VeroScript: unknown bindings option: " << Arg << '\n';
					return 2;
				}
				if (!InputPath.empty())
				{
					std::cerr << "VeroScript: unexpected argument: " << Arg << '\n';
					return 2;
				}
				InputPath = Arg;
			}

			if (InputPath.empty())
			{
				std::cerr << "VeroScript: bindings " << Action << " requires <catalog.json>\n";
				return 2;
			}

			FHostBindingRegistry Registry;
			std::string Error;
			if (!LoadBindingCatalogFile(InputPath, Registry, Error, true))
			{
				std::cerr << "VeroScript: " << Error << '\n';
				return 1;
			}

			if (Action == "validate")
			{
				std::cout << "OK: " << InputPath
				          << " (" << Registry.GetFunctions().size() << " functions, "
				          << Registry.GetTypes().size() << " types)\n";
				return 0;
			}

			const std::string_view WritePath = OutputPath.empty() ? InputPath : OutputPath;
			if (!SaveBindingCatalogFile(Registry, WritePath, Error))
			{
				std::cerr << "VeroScript: " << Error << '\n';
				return 1;
			}
			std::cout << "Wrote " << WritePath << '\n';
			return 0;
		}

		int RunPackage(const int argc, char** argv)
		{
			if (argc < 2)
			{
				std::cerr << "VeroScript: package requires inspect <app.vsapp|library.vslib>\n";
				return 2;
			}
			const std::string_view Action = argv[1];
			if (Action == "-h" || Action == "--help" || Action == "-?")
			{
				PrintUsage();
				return 0;
			}
			if (Action != "inspect")
			{
				std::cerr << "VeroScript: unknown package action: " << Action << '\n';
				return 2;
			}
			if (argc != 3)
			{
				std::cerr << "VeroScript: package inspect requires exactly one package path\n";
				return 2;
			}

			const std::string_view InputPath = argv[2];
			if (!IsPackagePath(InputPath))
			{
				std::cerr << "VeroScript: package path must end in .vsapp or .vslib\n";
				return 2;
			}
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			if (PackageFileKind(InputPath) != EPackageFileKind::Application && !ReadBinaryFile(InputPath, Bytes, Error))
			{
				std::cerr << "VeroScript: " << Error << ": " << InputPath << '\n';
				return 1;
			}
			FPackageData Package;
			const auto OriginalPackageSize = std::filesystem::file_size(PathUtil::FromUtf8(InputPath));
			bool Parsed = false;
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (PackageFileKind(InputPath) == EPackageFileKind::Application)
			{
				Application::FDiagnostic E;
				auto App = Application::FApplication::Open(
				    PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(InputPath))),
				    Application::CreateApplicationServices(), E);
				if (App && App->IsSourceEntry())
				{
					std::cout << "Kind: application\nName: " << App->GetId() << "\nEntry: source\n"
					          << App->GetDescriptorJson() << '\n';
					return 0;
				}
				Parsed = App && App->ReadEntryPackage(Package, E);
				if (!Parsed)
				{
					Error = E.Code + ": " + E.Message;
				}
			}
			else
			#endif
				Parsed = ReadPackage(FByteView(Bytes), Package, Error);
			if (!Parsed || !Package.Module)
			{
				std::cerr << "VeroScript: invalid package '" << InputPath
				          << "': " << Error << '\n';
				return 1;
			}
			const EPackageFileKind ExpectedKind = PackageFileKind(InputPath);
			if ((ExpectedKind == EPackageFileKind::Application && Package.Kind != EPackageKind::Application) || (ExpectedKind == EPackageFileKind::Library && Package.Kind != EPackageKind::Library))
			{
				std::cerr << "VeroScript: package extension does not match package kind: "
				          << InputPath << '\n';
				return 1;
			}

			const FBytecodeModule& Module = *Package.Module;
			std::cout
			    << "Path: " << InputPath << '\n'
			    << "Kind: "
			    << (Package.Kind == EPackageKind::Application ? "application" : "library") << '\n'
			    << "Name: " << Package.PackageName << '\n'
			    << "Version: " << Package.PackageVersion << '\n'
			    << "Package format: " << VEROSCRIPT_PACKAGE_FORMAT_MAJOR << '.'
			    << VEROSCRIPT_PACKAGE_FORMAT_MINOR << '\n'
			    << "Bytecode version: " << Module.Version.Major << '.'
			    << Module.Version.Minor << '.' << Module.Version.Patch << '\n'
			    << "Size: " << OriginalPackageSize << " bytes\n"
			    << "Debug info: " << (Module.DebugFiles.empty() ? "stripped" : "present") << '\n'
			    << "Host metadata: " << Package.HostMetadata.size() << '\n'
			    << "Dependencies: " << Module.PackageComponents.size() << '\n'
			    << "Functions: " << Module.Functions.size() << '\n'
			    << "Types: " << Module.Types.size() << '\n'
			    << "Interfaces: " << Module.Interfaces.size() << '\n'
			    << "Enums: " << Module.Enums.size() << '\n'
			    << "Globals: " << Module.GlobalNames.size() << '\n';
			// `package inspect` is commonly consumed through a short-lived pipe
			// (CMake/CI/process launchers). Flush before returning so Windows never
			// observes a successful process with an empty buffered report.
			std::cout.flush();
			if (!std::cout)
			{
				std::cerr << "VeroScript: failed to write package inspection output\n";
				return 1;
			}
			return 0;
		}
	}

	static int RunWithModules(const int argc, char** argv, FCommandLineExtensions& Extensions
	#if defined(VEROSCRIPT_WITH_GUI)
	    ,
	    Gui::FCommandLineRegistration& GuiTools
	#endif
	)
	{
		if (argc <= 1)
		{
			PrintUsage();
			Extensions.PrintHelp(std::cout);
			return 2;
		}

		Locale::InitializeFromEnvironment();
	#if !defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
		for (const std::string_view Command : {"install", "uninstall", "enable", "disable", "list", "verify",
		         "clean", "env", "search", "show", "pack", "publish", "deploy"})
		{
			if (argv[1] == Command)
			{
				std::cerr << "VeroScript: package management was not built into this executable\n";
				return 2;
			}
		}
	#endif
		std::error_code StartupError;
		const std::string StartupDirectory = PathUtil::ToUtf8(std::filesystem::current_path(StartupError));
		const std::string Executable =
		    PathUtil::ToUtf8(PathUtil::FromUtf8(PathUtil::ExecutableDirectory()) / "veroscript");
		if (std::string_view(argv[1]) == "project")
		{
			if (StartupError)
			{
				std::cerr << "VeroScript: cannot determine startup directory\n";
				return 2;
			}
			return RunProject(std::vector<std::string>(argv + 2, argv + argc), StartupDirectory, std::cout, std::cerr);
		}
		if (std::string_view(argv[1]) == "build")
		{
			#if defined(VEROSCRIPT_WITH_BUILD_SYSTEM)
			return RunBuild(std::vector<std::string>(argv + 2, argv + argc), StartupDirectory, Executable);
			#else
			std::cerr << "VeroScript: build services were not built into this executable\n";
			return 2;
			#endif
		}
	#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
		if (Packages::IsPackageCommand(argv[1]))
		{
			if (StartupError)
			{
				std::cerr << "VeroScript: cannot determine startup directory\n";
				return 2;
			}
			std::function<std::shared_ptr<const IArtifactDecoder>(const std::string&, Packages::FDiagnostic&)> ProtectionFactory;
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			ProtectionFactory = [](const std::string& Path, Packages::FDiagnostic& Error) -> std::shared_ptr<const IArtifactDecoder>
			{
				FArtifactData Options;
				std::string Failure;
				Application::FDiagnostic E;
				auto Services = Application::CreateApplicationServices();
				if (!ReadArtifactFile({Path, "configuration", {}}, {4ull << 20, {}}, Options, Failure) ||
				    !Application::ConfigureApplicationServices(std::string_view(reinterpret_cast<const char*>(Options.Bytes.data()), Options.Bytes.size()), Services, E))
				{
					Error.Code = "protection-service";
					Error.Message = Failure.empty() ? E.Message : Failure;
					return {};
				}
				return Services.Protection;
			};
			#endif
			return Packages::RunPackageCommand(std::vector<std::string>(argv + 1, argv + argc),
			    StartupDirectory, Executable, std::cout, std::cerr, ProtectionFactory);
		}
		Packages::FEnvironmentSelection PackageSelection;
		std::string PackageJitId, PackageVmId;
		std::shared_ptr<const FModuleEnvironment> PackageModuleEnvironment;
		std::shared_ptr<const Packages::FInstalledEnvironment> InstalledPackages;
	#endif

		if (std::string_view(argv[1]) == "aot")
		{
			return RunAot(argc - 1, argv + 1, StartupDirectory, Executable);
		}
		if (std::string_view(argv[1]) == "bindings")
		{
			return RunBindings(argc - 1, argv + 1);
		}
		if (std::string_view(argv[1]) == "package")
		{
			return RunPackage(argc - 1, argv + 1);
		}
		if (std::string_view(argv[1]) == "compiler-package") return RunCompilerPackage(argc - 1, argv + 1);

		std::vector<std::string_view> ImportPaths;
		FScriptCompileExtensionsOptions ScriptExtensions;
		std::vector<std::string> OwnedImplicitImportPaths;
		std::vector<std::string_view> ImplicitImportPaths;
		std::vector<std::string_view> StdlibSearchPaths;
		std::vector<std::string> OwnedDefines;
		std::vector<std::string_view> Defines;
		std::vector<FPredefinedMacro> PredefinedMacros;
		bool bCheckOnly = false;
		std::string ApplicationOptionsFile;
		bool bRepl = false;
		bool bWatch = false;
		bool bWatchOnce = false;
		bool bReplStrict = false;
		bool bEval = false;
		bool bDebug = false;
		bool bUseBytecodeCache = true;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		bool bPackageVersionSet = false;
		bool bPackageFileSet = false;
		bool bStripPackageDebug = false;
		std::string_view BytecodeCacheDirectory;
		std::string_view PackageOutputPath;
		std::uint64_t PackageVersion = 0;
		std::string_view EvalSource;
		std::vector<std::string> BindingCatalogPaths;
		EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed;
		std::shared_ptr<const IJitBackendFactory> JitBackendFactory;
		std::shared_ptr<const IVmBackendFactory> VmBackendFactory;
		std::string ProductVmPath;
	#if VEROSCRIPT_RUNTIME_HAS_VM
		ProductVmPath = BytecodeVm::RuntimeLibraryPath(Executable);
	#endif
		std::string VmBackendPath = ProductVmPath;
		bool bVmBackendSpecified = false, bDisableVmBackend = false, bDisableJitBackend = false;
		int DapPort = 0;
		std::string DapBindAddress = "127.0.0.1";
		bool bDapBindSet = false;
		int ScriptArgIndex = 1;

		FHostBindingRegistry BindingRegistry;
		const IHostBindingCatalog* Bindings = nullptr;
		bool EnvironmentPrepared = false, EnvironmentHasEntry = false;
		bool BindingCatalogsPrepared = false;
		const auto PrepareBindings = [&]() -> bool
		{
			if (BindingCatalogsPrepared) return true;
			if (!BindingCatalogPaths.empty())
			{
				for (std::size_t Index = 0; Index < BindingCatalogPaths.size(); ++Index)
				{
					std::string CatalogError;
					if (!LoadBindingCatalogFile(
					        BindingCatalogPaths[Index],
					        BindingRegistry,
					        CatalogError,
					        Index == 0))
					{
						std::cerr << "VeroScript: failed to load binding catalog '"
						          << BindingCatalogPaths[Index] << "': " << CatalogError << '\n';
						return false;
					}
				}
				Bindings = &BindingRegistry;
			}

			#if defined(VEROSCRIPT_WITH_GUI)
			std::string GuiError;
			if (!GuiTools.RegisterBindings(BindingRegistry, GuiError))
			{
				std::cerr << "VeroScript: " << GuiError << '\n';
				return false;
			}
			Bindings = &BindingRegistry;
			#endif
			BindingCatalogsPrepared = true;
			return true;
		};
		const auto PrepareEnvironment = [&](bool HasEntry, bool OnlyCli, bool AllowUninstalled = false) -> bool
		{
			if (EnvironmentPrepared)
			{
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				if (HasEntry && !EnvironmentHasEntry && !bEval && !bRepl && ScriptArgIndex < argc)
				{
					Packages::FRunEnvironmentRequest EntryRequest;
					EntryRequest.StartupDirectory = StartupDirectory;
					EntryRequest.Executable = Executable;
					EntryRequest.Selection = PackageSelection;
					EntryRequest.EntryFile = argv[ScriptArgIndex];
					std::shared_ptr<const Packages::FInstalledEnvironment> EntryPackages;
					Packages::FDiagnostic Error;
					if (!Packages::ResolveRunEnvironment(EntryRequest, EntryPackages, Error) ||
					    bool(EntryPackages) != bool(InstalledPackages) ||
					    (EntryPackages && (EntryPackages->GetLayout().Root != InstalledPackages->GetLayout().Root || EntryPackages->GetLayout().Scope != InstalledPackages->GetLayout().Scope || EntryPackages->GetFingerprint() != InstalledPackages->GetFingerprint())))
					{
						std::cerr << "VeroScript: module options and script select different package environments; specify --path before module options\n";
						return false;
					}
					if (PackageModuleEnvironment && InstalledPackages->GetLayout().Scope != Packages::EEnvironmentScope::Project)
					{
						auto Desc = PackageModuleEnvironment->GetDescription();
						for (auto& Root : Desc.SourceRoots)
						{
							if (Root.OwnerId == Desc.EntryOwnerId)
							{
								Root.Path = PathUtil::ToUtf8((PathUtil::FromUtf8(StartupDirectory) / PathUtil::FromUtf8(EntryRequest.EntryFile)).parent_path());
							}
						}

						std::string Message;
						PackageModuleEnvironment = FModuleEnvironment::Create(std::move(Desc), Message);
						if (!PackageModuleEnvironment)
						{
							std::cerr << "VeroScript: " << Message << '\n';
							return false;
						}
					}
				}
				#endif
				return true;
			}
			if (!PrepareBindings()) return false;

			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			{
				if (StartupError)
				{
					std::cerr << "VeroScript: cannot determine startup directory\n";
					return false;
				}
				Packages::FRunEnvironmentRequest Request;
				Request.StartupDirectory = StartupDirectory;
				Request.Executable = Executable;
				Request.Selection = PackageSelection;
				if (HasEntry && !bEval && !bRepl && ScriptArgIndex < argc)
				{
					Request.EntryFile = argv[ScriptArgIndex];
				}
				Packages::FDiagnostic Error;
				if (!Packages::ResolveRunEnvironment(Request, InstalledPackages, Error))
				{
					// A fresh project can request basic help before installing dependencies.
					if ((OnlyCli || AllowUninstalled) && Error.Code == "environment-uninstalled")
					{
						Extensions.Close();
						return true;
					}
					std::cerr << "VeroScript packages [" << Error.Code << "] " << Error.Message << '\n';
					return false;
				}
				if (OnlyCli && (!InstalledPackages || !InstalledPackages->HasCommandLineModules()))
				{
					Extensions.Close();
					return true;
				}
				if (InstalledPackages)
				{
					Packages::FCoreEnvironmentOptions Options;
					Options.ScriptCompileExtensions = ScriptExtensions;
					if (InstalledPackages->GetLayout().Scope == Packages::EEnvironmentScope::Project)
					{
						Options.EntryRoot = InstalledPackages->GetLayout().Root;
					}
					else if (Request.EntryFile.empty())
					{
						Options.EntryRoot = StartupDirectory;
					}
					else
					{
						Options.EntryRoot = PathUtil::ToUtf8((PathUtil::FromUtf8(StartupDirectory) /
						    PathUtil::FromUtf8(Request.EntryFile))
						        .parent_path());
					}
					if (InstalledPackages->HasCommandLineModules())
					{
						Options.Services.push_back(Extensions.Binding());
						Options.Services.insert(Options.Services.end(), Extensions.Services.begin(), Extensions.Services.end());
					}

					Options.BaseBindings = Bindings ? &BindingRegistry : nullptr;
					#if defined(VEROSCRIPT_WITH_GUI)
					for (const auto& Choice : Extensions.GetValues("veroscript-gui-tools"))
					{
						if (Choice.second.size() == 1)
						{
							const auto Role =
							    Choice.first.substr(0, Choice.first.size() - std::string("-backend").size());
							Options.SelectedBackends[Role] = Choice.second.front();
						}
					}
					#endif
					if (bDisableVmBackend || (bVmBackendSpecified && VmBackendFactory))
					{
						Options.SelectedBackends["vm"] = "";
					}
					else if (!PackageVmId.empty())
					{
						Options.SelectedBackends["vm"] = PackageVmId;
					}
					if (OnlyCli || JitBackendFactory || bDisableJitBackend || ExecutionBackend == EExecutionBackend::Vm)
					{
						Options.SelectedBackends["jit"] = "";
					}
					else if (!PackageJitId.empty())
					{
						Options.SelectedBackends["jit"] = PackageJitId;
					}
					for (auto P : StdlibSearchPaths)
					{
						Options.StdlibSearchPaths.emplace_back(P);
					}
					AddDefaultPackageStdlibPaths(Options.StdlibSearchPaths);
					#if defined(VEROSCRIPT_WITH_GUI)
					std::vector<FNativeServiceProviderDesc> GuiProviders;
					const bool BuiltEnvironment = InstalledPackages->BuildCoreEnvironment(
					    Options, PackageModuleEnvironment, Error, &GuiProviders);
					#else
					const bool BuiltEnvironment =
					    InstalledPackages->BuildCoreEnvironment(Options, PackageModuleEnvironment, Error);
					#endif
					if (!BuiltEnvironment)
					{
						std::cerr << "VeroScript packages [" << Error.Code << "] " << Error.Message << '\n';
						return false;
					}
					#if defined(VEROSCRIPT_WITH_GUI)
					Gui::FScriptServices GuiServices;
					GuiServices.Application =
					    Gui::MakeRegisteredServiceFactories(std::move(GuiProviders), PackageModuleEnvironment);
					GuiServices.DefaultBackend = [Environment = PackageModuleEnvironment](std::string_view Role)
					{
						return ResolveBackendSelection(Role, {}, Environment->GetDescription().SelectedBackends);
					};
					GuiTools.SetRuntimeServices(std::move(GuiServices));
					#endif
					if (!OnlyCli && !bVmBackendSpecified && !VmBackendFactory)
					{
						const auto Choice = PackageVmId.empty() ? InstalledPackages->GetSelectedBackend("vm") : PackageVmId;
						if (!InstalledPackages->LoadVmBackend(Choice, VmBackendFactory, Error))
						{
							std::cerr << "VeroScript packages [" << Error.Code << "] " << Error.Message << '\n';
							return false;
						}
						if (Choice == DisabledBackend)
						{
							bDisableVmBackend = true;
							VmBackendPath.clear();
						}
						else if (!VmBackendFactory && VmBackendPath.empty())
						{
							VmBackendPath = ProductVmPath;
						}
					}
					const bool bExplicitJitId = !PackageJitId.empty() && PackageJitId != AutomaticBackend && PackageJitId != DisabledBackend;
					if (!OnlyCli && !bDisableJitBackend && !JitBackendFactory && (bExplicitJitId || (!bCheckOnly && ExecutionBackend != EExecutionBackend::Vm)))
					{
						const auto Choice = PackageJitId.empty() ? InstalledPackages->GetSelectedBackend("jit") : PackageJitId;
						if (!InstalledPackages->LoadJitBackend(Choice, JitBackendFactory, Error))
						{
							std::cerr << "VeroScript packages [" << Error.Code << "] " << Error.Message << '\n';
							return false;
						}
						bDisableJitBackend = Choice == DisabledBackend;
					}
				}
				else if ((!PackageJitId.empty() && PackageJitId != AutomaticBackend && PackageJitId != DisabledBackend) || (!PackageVmId.empty() && PackageVmId != AutomaticBackend && PackageVmId != DisabledBackend))
				{
					std::cerr
					    << "VeroScript: requested execution backend is not installed in the selected environment\n";
					return false;
				}
			}
			#endif
			std::string ExtensionError;
			Extensions.Close();
			if (!Extensions.Validate(ExtensionError))
			{
				std::cerr << "VeroScript: " << ExtensionError << '\n';
				return false;
			}
			if (!ScriptExtensions.VmBackendFactory && ScriptExtensions.VmBackendPath.empty())
			{
				ScriptExtensions.VmBackendFactory = VmBackendFactory;
				ScriptExtensions.VmBackendPath = VmBackendFactory ? std::string{} : VmBackendPath;
			}
			EnvironmentHasEntry = HasEntry;
			EnvironmentPrepared = true;
			return true;
		};

		while (ScriptArgIndex < argc && IsOption(argv[ScriptArgIndex]))
		{
			const std::string_view Arg = argv[ScriptArgIndex];
			if (Arg == "--")
			{
				++ScriptArgIndex;
				break;
			}
			if (EnvironmentPrepared && (Arg == "--path" || Arg.rfind("--path=", 0) == 0 || Arg == "--root" || Arg.rfind("--root=", 0) == 0 || Arg == "--stdlib-path" || Arg == "--binding-catalog" || Arg.rfind("--binding-catalog=", 0) == 0 || Arg == "--backend" || Arg.rfind("--backend=", 0) == 0 || Arg == "--jit-backend" || Arg.rfind("--jit-backend=", 0) == 0 || Arg == "--vm-backend" || Arg.rfind("--vm-backend=", 0) == 0))
			{
				std::cerr << "VeroScript: environment/backend options must precede module options: " << Arg << '\n';
				return 2;
			}
			if (Arg == "--cli-module" || Arg.rfind("--cli-module=", 0) == 0)
			{
				if (EnvironmentPrepared)
				{
					std::cerr << "VeroScript: --cli-module must precede module options\n";
					return 2;
				}
				std::string Path;
				if (Arg == "--cli-module")
				{
					if (++ScriptArgIndex >= argc)
					{
						std::cerr << "VeroScript: missing --cli-module file\n";
						return 2;
					}
					Path = argv[ScriptArgIndex];
				}
				else
				{
					Path = std::string(Arg.substr(13));
				}

				std::string Error;
				if (Path.empty() || !Extensions.Load(Path, StartupDirectory, Error))
				{
					std::cerr << "VeroScript: " << (Error.empty() ? "empty --cli-module file" : Error) << '\n';
					return 2;
				}
				++ScriptArgIndex;
				continue;
			}

			const int CompilerOption =
			    TryParseCompilerOption(Arg, argc, argv, ScriptArgIndex, ScriptExtensions);
			if (CompilerOption < 0) return 2;
			if (CompilerOption > 0)
			{
				continue;
			}

			if (Arg == "--version")
			{
				std::cout << "VeroScript " << GetVersionString()
				          << " [" << GetHostPlatformString()
				          << "; vm-backend=" << (VmBackendFactory ? VmBackendFactory->GetDescription().Id : "product-default")
				          << "; jit-backend=" << (JitBackendFactory ? JitBackendFactory->GetDescription().Id : "none")
				          << "; jit-target=" << (JitBackendFactory ? JitBackendFactory->GetDescription().Target : "none")
				          << "; native=" << (JitBackendFactory && JitBackendFactory->GetDescription().bNativeCode ? "on" : "off") << "]\n";
				return 0;
			}

			if (Arg == "--help" || Arg == "-h" || Arg == "-?")
			{
				PrintUsage();
				if (!PrepareEnvironment(false, true)) return 2;
				Extensions.PrintHelp(std::cout);
				return 0;
			}

			if (Arg == "-i" || Arg == "--interactive")
			{
				bRepl = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--watch")
			{
				bWatch = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--watch-once")
			{
				bWatchOnce = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--repl-strict")
			{
				bReplStrict = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "-e" || Arg == "--eval")
			{
				if (bEval)
				{
					std::cerr << "VeroScript: duplicate " << Arg << " option\n";
					return 2;
				}
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing source for " << Arg << '\n';
					return 2;
				}

				bEval = true;
				EvalSource = argv[ScriptArgIndex + 1];
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "-I" || Arg == "--import-path")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing path for " << Arg << '\n';
					return 2;
				}
				ImportPaths.push_back(argv[ScriptArgIndex + 1]);
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--no-circular-imports")
			{
				bAllowCircularImports = false;
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--allow-circular-imports")
			{
				bAllowCircularImports = true;
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--allow-project-wide-access")
			{
				bAllowProjectWideAccess = true;
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--implicit-import")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing file for --implicit-import\n";
					return 2;
				}
				OwnedImplicitImportPaths.push_back(
				    ResolveCliFilePath(argv[ScriptArgIndex + 1]));
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg.rfind("--implicit-import=", 0) == 0)
			{
				const std::string_view Path =
				    Arg.substr(std::string_view("--implicit-import=").size());
				if (Path.empty())
				{
					std::cerr << "VeroScript: --implicit-import expects a non-empty file\n";
					return 2;
				}
				OwnedImplicitImportPaths.push_back(ResolveCliFilePath(Path));
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--stdlib-path")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing path for --stdlib-path\n";
					return 2;
				}
				StdlibSearchPaths.push_back(argv[ScriptArgIndex + 1]);
				ScriptArgIndex += 2;
				continue;
			}
			if (Arg == "--application-options" || Arg.rfind("--application-options=", 0) == 0)
			{
				if (!ApplicationOptionsFile.empty())
				{
					std::cerr << "VeroScript: duplicate --application-options\n";
					return 2;
				}
				if (Arg == "--application-options")
				{
					if (++ScriptArgIndex >= argc)
					{
						std::cerr << "VeroScript: missing application options path\n";
						return 2;
					}
					ApplicationOptionsFile = ResolveCliFilePath(argv[ScriptArgIndex]);
				}
				else
				{
					ApplicationOptionsFile = ResolveCliFilePath(Arg.substr(std::string_view("--application-options=").size()));
				}
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--binding-catalog")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing path for --binding-catalog\n";
					return 2;
				}
				BindingCatalogPaths.emplace_back(argv[ScriptArgIndex + 1]);
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg.rfind("--binding-catalog=", 0) == 0)
			{
				const std::string CatalogPath(
				    Arg.substr(std::string_view("--binding-catalog=").size()));
				if (CatalogPath.empty())
				{
					std::cerr << "VeroScript: --binding-catalog expects a non-empty path\n";
					return 2;
				}
				BindingCatalogPaths.push_back(CatalogPath);
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--macro" || Arg == "--predefined-macro")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing NAME(params)=body for " << Arg << '\n';
					return 2;
				}
				FPredefinedMacro Macro;
				std::string MacroError;
				if (!ParsePredefinedMacroSpec(argv[ScriptArgIndex + 1], Macro, MacroError))
				{
					std::cerr << "VeroScript: invalid predefined macro: " << MacroError << '\n';
					return 2;
				}
				PredefinedMacros.push_back(std::move(Macro));
				ScriptArgIndex += 2;
				continue;
			}
			if (Arg.rfind("--macro=", 0) == 0 || Arg.rfind("--predefined-macro=", 0) == 0)
			{
				const std::size_t PrefixLength = Arg.rfind("--macro=", 0) == 0
				    ? std::string_view("--macro=").size()
				    : std::string_view("--predefined-macro=").size();
				FPredefinedMacro Macro;
				std::string MacroError;
				if (!ParsePredefinedMacroSpec(Arg.substr(PrefixLength), Macro, MacroError))
				{
					std::cerr << "VeroScript: invalid predefined macro: " << MacroError << '\n';
					return 2;
				}
				PredefinedMacros.push_back(std::move(Macro));
				++ScriptArgIndex;
				continue;
			}

			{
				const int DefineParse = TryParseDefineOption(
				    Arg, argc, argv, ScriptArgIndex, OwnedDefines);
				if (DefineParse == 1)
				{
					continue;
				}
				if (DefineParse == 2)
				{
					return 2;
				}
			}

			if (Arg == "--check")
			{
				bCheckOnly = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--debug")
			{
				bDebug = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg.rfind("--dap-port=", 0) == 0)
			{
				const std::string_view Value = Arg.substr(std::string_view("--dap-port=").size());
				int Parsed = 0;
				const auto ParseResult = std::from_chars(
				    Value.data(), Value.data() + Value.size(), Parsed, 10);
				if (Value.empty() || ParseResult.ec != std::errc{} || ParseResult.ptr != Value.data() + Value.size())
				{
					std::cerr << "VeroScript: --dap-port expects an integer\n";
					return 2;
				}
				if (Parsed < 1 || Parsed > 65535)
				{
					std::cerr << "VeroScript: --dap-port expects 1..65535\n";
					return 2;
				}
				DapPort = Parsed;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg.rfind("--dap-bind=", 0) == 0)
			{
				DapBindAddress = std::string(Arg.substr(std::string_view("--dap-bind=").size()));
				if (DapBindAddress.empty())
				{
					std::cerr << "VeroScript: --dap-bind expects a non-empty address\n";
					return 2;
				}
				bDapBindSet = true;
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg.rfind("--bc-cache=", 0) == 0)
			{
				const std::string_view Value = Arg.substr(std::string_view("--bc-cache=").size());
				if (Value == "off")
				{
					bUseBytecodeCache = false;
				}
				else if (Value == "on")
				{
					bUseBytecodeCache = true;
				}
				else
				{
					std::cerr << "VeroScript: --bc-cache expects off or on\n";
					return 2;
				}
				ScriptArgIndex += 1;
				continue;
			}

			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			unsigned PackageConsumed = 0;
			Packages::FDiagnostic SelectionError;
			if (!Packages::ParseEnvironmentOption(Arg, ScriptArgIndex + 1 < argc ? std::string_view(argv[ScriptArgIndex + 1]) : std::string_view{},
			        PackageSelection, PackageConsumed, SelectionError))
			{
				std::cerr << "VeroScript packages [" << SelectionError.Code << "] " << SelectionError.Message << '\n';
				return 2;
			}
			if (PackageConsumed)
			{
				ScriptArgIndex += static_cast<int>(PackageConsumed);
				continue;
			}

			#endif
			if (Arg.rfind("--vm-backend=", 0) == 0 || Arg == "--vm-backend")
			{
				std::string_view Path;
				if (Arg == "--vm-backend")
				{
					if (++ScriptArgIndex >= argc)
					{
						std::cerr << "VeroScript: --vm-backend requires auto, none, an ID, file or directory\n";
						return 2;
					}
					Path = argv[ScriptArgIndex];
				}
				else
				{
					Path = Arg.substr(std::string_view("--vm-backend=").size());
				}
				std::string Error;
				if (Path == AutomaticBackend || Path == DisabledBackend)
				{
					bDisableVmBackend = Path == DisabledBackend;
					bVmBackendSpecified = bDisableVmBackend;
					VmBackendFactory.reset();
					VmBackendPath = bDisableVmBackend ? std::string{} : ProductVmPath;
					#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
					PackageVmId = std::string(Path);
					#endif
					++ScriptArgIndex;
					continue;
				}
				bVmBackendSpecified = true;
				bDisableVmBackend = false;
				if (Path == "vs-bytecode")
				{
					Path = ProductVmPath;
				}
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				if (!Path.empty() && Path.find_first_of("/\\.") == std::string_view::npos)
				{
					PackageVmId = std::string(Path);
					VmBackendFactory.reset();
					VmBackendPath.clear();
					bVmBackendSpecified = false;
					++ScriptArgIndex;
					continue;
				}
				PackageVmId.clear();
				#endif
				const std::string RequestedVmPath(Path);
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				PackageVmId.clear();
				#endif
				VmBackendFactory = LoadVmBackend(RequestedVmPath, Error, &VmBackendPath);
				if (!VmBackendFactory)
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 2;
				}
				++ScriptArgIndex;
				continue;
			}
			if (Arg.rfind("--jit-backend=", 0) == 0 || Arg == "--jit-backend")
			{
				std::string_view Path;
				if (Arg == "--jit-backend")
				{
					if (++ScriptArgIndex >= argc)
					{
						std::cerr << "VeroScript: --jit-backend requires auto, none, an ID, file or directory\n";
						return 2;
					}
					Path = argv[ScriptArgIndex];
				}
				else
				{
					Path = Arg.substr(std::string_view("--jit-backend=").size());
				}
				std::string Error;
				if (Path.empty() || Path == AutomaticBackend || Path == DisabledBackend)
				{
					bDisableJitBackend = Path.empty() || Path == DisabledBackend;
					JitBackendFactory.reset();
					#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
					PackageJitId = bDisableJitBackend ? std::string(DisabledBackend) : std::string(AutomaticBackend);
					#endif
					++ScriptArgIndex;
					continue;
				}
				bDisableJitBackend = false;
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				if (!Path.empty() && Path.find_first_of("/\\.") == std::string_view::npos)
				{
					PackageJitId = std::string(Path);
					JitBackendFactory.reset();
					++ScriptArgIndex;
					continue;
				}
				PackageJitId.clear();
				#endif
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				PackageJitId.clear();
				#endif
				JitBackendFactory = LoadJitBackend(Path, Error);
				if (!JitBackendFactory)
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 2;
				}
				++ScriptArgIndex;
				continue;
			}

			if (Arg.rfind("--backend=", 0) == 0)
			{
				const std::string_view Value = Arg.substr(std::string_view("--backend=").size());
				if (!TryParseExecutionBackend(Value, ExecutionBackend))
				{
					std::cerr << "VeroScript: --backend expects vm, mixed, or jit\n";
					return 2;
				}
				ScriptArgIndex += 1;
				continue;
			}

			if (Arg == "--backend")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing value for --backend\n";
					return 2;
				}
				if (!TryParseExecutionBackend(argv[ScriptArgIndex + 1], ExecutionBackend))
				{
					std::cerr << "VeroScript: --backend expects vm, mixed, or jit\n";
					return 2;
				}
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--bc-cache-dir")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing path for --bc-cache-dir\n";
					return 2;
				}
				BytecodeCacheDirectory = argv[ScriptArgIndex + 1];
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--package-file")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing file for --package-file\n";
					return 2;
				}
				if (std::string_view(argv[ScriptArgIndex + 1]).empty())
				{
					std::cerr << "VeroScript: --package-file expects a non-empty path\n";
					return 2;
				}
				OwnedImplicitImportPaths.push_back(
				    ResolveCliFilePath(argv[ScriptArgIndex + 1]));
				bPackageFileSet = true;
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--emit-package")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing path for --emit-package\n";
					return 2;
				}
				if (std::string_view(argv[ScriptArgIndex + 1]).empty())
				{
					std::cerr << "VeroScript: --emit-package expects a non-empty path\n";
					return 2;
				}
				if (!PackageOutputPath.empty())
				{
					std::cerr << "VeroScript: duplicate --emit-package option\n";
					return 2;
				}
				PackageOutputPath = argv[ScriptArgIndex + 1];
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--package-version")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing value for --package-version\n";
					return 2;
				}
				const std::string_view Text = argv[ScriptArgIndex + 1];
				std::uint64_t ParsedVersion = 0;
				const auto ParseResult = std::from_chars(
				    Text.data(), Text.data() + Text.size(), ParsedVersion, 10);
				if (Text.empty() || ParseResult.ec != std::errc{} || ParseResult.ptr != Text.data() + Text.size())
				{
					std::cerr
					    << "VeroScript: --package-version expects an unsigned integer\n";
					return 2;
				}
				PackageVersion = ParsedVersion;
				bPackageVersionSet = true;
				ScriptArgIndex += 2;
				continue;
			}

			if (Arg == "--strip-package-debug")
			{
				bStripPackageDebug = true;
				++ScriptArgIndex;
				continue;
			}

			if (Arg == "--lang")
			{
				if (ScriptArgIndex + 1 >= argc)
				{
					std::cerr << "VeroScript: missing language tag for --lang\n";
					return 2;
				}
				if (!Locale::TrySetLanguageFromTag(argv[ScriptArgIndex + 1]))
				{
					std::cerr << "VeroScript: unsupported --lang value; available:";
					for (const std::string& Tag : Locale::GetAvailableLanguageTags())
					{
						std::cerr << ' ' << Tag;
					}
					std::cerr << '\n';
					return 2;
				}
				ScriptArgIndex += 2;
				continue;
			}

			std::string ExtensionError;
			int Consumed = Extensions.ParseOption(Arg,
			    ScriptArgIndex + 1 < argc ? std::string_view(argv[ScriptArgIndex + 1]) : std::string_view{},
			    ScriptArgIndex + 1 < argc, ExtensionError);
			if (Consumed == 0)
			{
				if (!PrepareEnvironment(false, false)) return 2;
				Consumed = Extensions.ParseOption(Arg,
				    ScriptArgIndex + 1 < argc ? std::string_view(argv[ScriptArgIndex + 1]) : std::string_view{},
				    ScriptArgIndex + 1 < argc, ExtensionError);
			}
			if (Consumed < 0)
			{
				std::cerr << "VeroScript: " << ExtensionError << '\n';
				return 2;
			}
			if (Consumed > 0)
			{
				#if defined(VEROSCRIPT_WITH_GUI)
				const auto Name = Arg.substr(0, Arg.find('='));
				if (EnvironmentPrepared &&
				    (Name == "--platform-backend" || Name == "--graphics-backend" || Name == "--gui-backend"))
				{
					std::cerr
					    << "VeroScript: GUI backend options must precede options that load the package environment\n";
					return 2;
				}
				#endif
				ScriptArgIndex += Consumed;
				continue;
			}
			std::cerr << "VeroScript: unknown option: " << Arg << '\n';
			return 2;
		}

		const auto MakeExecutionConfig = [&]()
		{
			FEngineConfig Config;
			Config.ImportPaths = ImportPaths;
			Config.Defines = Defines;
			Config.PredefinedMacros = PredefinedMacros;
			Config.StdlibSearchPaths = StdlibSearchPaths;
			Config.ImplicitImportPaths = ImplicitImportPaths;
			Config.bAllowCircularImports = bAllowCircularImports;
			Config.bAllowProjectWideAccess = bAllowProjectWideAccess;
			Config.ScriptCompileExtensions = ScriptExtensions;
			if (!Config.ScriptCompileExtensions.VmBackendFactory && Config.ScriptCompileExtensions.VmBackendPath.empty())
			{
				Config.ScriptCompileExtensions.VmBackendFactory = VmBackendFactory;
				Config.ScriptCompileExtensions.VmBackendPath = VmBackendFactory ? std::string{} : VmBackendPath;
			}
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			Config.ModuleEnvironment = PackageModuleEnvironment;
			#endif
			Config.ExecutionBackend = ExecutionBackend;
			Config.JitBackendFactory = JitBackendFactory;
			Config.bDisableJitBackend = bDisableJitBackend;
			Config.VmBackendFactory = VmBackendFactory;
			Config.bClearVmBackend = bDisableVmBackend;
			if (!VmBackendFactory && !bDisableVmBackend)
			{
				if (bVmBackendSpecified)
				{
					Config.VmBackendPath = VmBackendPath;
				}
				else
				{
					Config.FallbackVmBackendPath = VmBackendPath.empty() ? ProductVmPath : VmBackendPath;
				}
			}
			Config.bUseBytecodeCache = bUseBytecodeCache;
			Config.BytecodeCacheDirectory = BytecodeCacheDirectory;
			Config.InputStream = &std::cin;
			#if defined(VEROSCRIPT_WITH_GUI)
			std::string GuiError;
			if (!GuiTools.ConfigureEngine(Config, Bindings ? &BindingRegistry : nullptr, GuiError))
			{
				throw std::runtime_error(GuiError);
			}
			#endif
			return Config;
		};

		if (ScriptArgIndex < argc)
		{
			const auto Candidate = PathUtil::FromUtf8(argv[ScriptArgIndex]);
			const bool LooksLikeCommand = Candidate.extension().empty() &&
			    Candidate == Candidate.filename();
			if (Extensions.HasCommand(argv[ScriptArgIndex]) || LooksLikeCommand)
			{
				if (!PrepareEnvironment(false, false, Extensions.HasCommand(argv[ScriptArgIndex]))) return 2;
				if (Extensions.HasCommand(argv[ScriptArgIndex]) &&
				    (bEval || bRepl || bReplStrict || bWatch || bWatchOnce || bDebug || bCheckOnly ||
				        DapPort != 0 || bDapBindSet ||
				        !PackageOutputPath.empty() || bPackageVersionSet || bPackageFileSet || bStripPackageDebug))
				{
					std::cerr << "VeroScript: module commands cannot be combined with script execution modes\n";
					return 2;
				}
				FCommandLineContext Context{StartupDirectory, Executable, &std::cin, &std::cout, &std::cerr};
				Context.LanguageTag = Locale::GetLanguageTag();
				#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
				Context.Environment = PackageModuleEnvironment;
				#endif
				int ExitCode = 0;
				std::string Error;
				#if defined(VEROSCRIPT_WITH_GUI)
				GuiTools.SetEngineOptions(MakeExecutionConfig());
				#endif
				if (Extensions.RunCommand(argv[ScriptArgIndex],
				        std::vector<std::string>(argv + ScriptArgIndex + 1, argv + argc), Context,
				        ExitCode, Error))
				{
					if (!Error.empty())
					{
						std::cerr << "VeroScript: " << Error << '\n';
					}

					return ExitCode;
				}
			}
		}

		if (bRepl && bWatch)
		{
			std::cerr << "VeroScript: -i and --watch cannot be used together\n";
			return 2;
		}

		if (bRepl && bWatchOnce)
		{
			std::cerr << "VeroScript: -i and --watch-once cannot be used together\n";
			return 2;
		}

		if (bCheckOnly && bRepl)
		{
			std::cerr << "VeroScript: --check and -i cannot be used together\n";
			return 2;
		}

		if (bCheckOnly && bWatch)
		{
			std::cerr << "VeroScript: --check and --watch cannot be used together\n";
			return 2;
		}

		if (bCheckOnly && bWatchOnce)
		{
			std::cerr << "VeroScript: --check and --watch-once cannot be used together\n";
			return 2;
		}

		if (bEval && bRepl)
		{
			std::cerr << "VeroScript: -e and -i cannot be used together\n";
			return 2;
		}

		if (bEval && bWatch)
		{
			std::cerr << "VeroScript: -e and --watch cannot be used together\n";
			return 2;
		}

		if (bEval && bWatchOnce)
		{
			std::cerr << "VeroScript: -e and --watch-once cannot be used together\n";
			return 2;
		}

		if (bEval && bCheckOnly)
		{
			std::cerr << "VeroScript: -e and --check cannot be used together\n";
			return 2;
		}

		if (bEval && bReplStrict)
		{
			std::cerr << "VeroScript: -e and --repl-strict cannot be used together\n";
			return 2;
		}

		if (DapPort != 0 && !bDebug)
		{
			std::cerr << "VeroScript: --dap-port requires --debug\n";
			return 2;
		}

		if (bDapBindSet && DapPort == 0)
		{
			std::cerr << "VeroScript: --dap-bind requires --debug --dap-port\n";
			return 2;
		}

		if (bDebug && (bEval || bRepl || bWatch || bWatchOnce || bCheckOnly || bReplStrict))
		{
			std::cerr << "VeroScript: --debug cannot be combined with -e/-i/--watch/--check\n";
			return 2;
		}

		if (!PackageOutputPath.empty() && (bEval || bRepl || bWatch || bWatchOnce || bCheckOnly || bDebug))
		{
			std::cerr
			    << "VeroScript: --emit-package requires one source file and cannot "
			    << "be combined with -e/-i/--watch/--check/--debug\n";
			return 2;
		}
		if (bPackageVersionSet && PackageOutputPath.empty())
		{
			std::cerr
			    << "VeroScript: --package-version requires --emit-package\n";
			return 2;
		}
		if (bPackageFileSet && PackageOutputPath.empty())
		{
			std::cerr << "VeroScript: --package-file requires --emit-package\n";
			return 2;
		}
		if (bStripPackageDebug && PackageOutputPath.empty())
		{
			std::cerr
			    << "VeroScript: --strip-package-debug requires --emit-package\n";
			return 2;
		}

		if (bWatchOnce)
		{
			bWatch = true;
		}

		if (bReplStrict && !bRepl)
		{
			std::cerr << "VeroScript: --repl-strict requires -i\n";
			return 2;
		}

		Defines.reserve(OwnedDefines.size());
		for (const std::string& Define : OwnedDefines)
		{
			Defines.emplace_back(Define);
		}
		ImplicitImportPaths.reserve(OwnedImplicitImportPaths.size());
		for (const std::string& Path : OwnedImplicitImportPaths)
		{
			ImplicitImportPaths.emplace_back(Path);
		}

		if (!PrepareBindings()) return 2;

		if (bDebug)
		{
			if (Extensions.HasOptions())
			{
				std::cerr << "VeroScript: module runtime options are not supported by the DAP command\n";
				return 2;
			}
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			if (PackageSelection.ComponentRoot || !PackageSelection.Path.empty() || !PackageJitId.empty() || !PackageVmId.empty())
			{
				std::cerr
				    << "VeroScript: the DAP adapter does not yet accept package environment selectors\n";
				return 2;
			}
			#endif
			Tooling::FDapStdioRunOptions StdioOptions;
			if (ScriptArgIndex < argc)
			{
				StdioOptions.DefaultProgram = argv[ScriptArgIndex];
				++ScriptArgIndex;
			}
			if (ScriptArgIndex < argc)
			{
				for (; ScriptArgIndex < argc; ++ScriptArgIndex)
				{
					StdioOptions.DefaultScriptArgs.emplace_back(argv[ScriptArgIndex]);
				}
			}
			for (const std::string_view Path : ImportPaths)
			{
				StdioOptions.DefaultImportPaths.emplace_back(Path);
			}
			for (const std::string_view Path : ImplicitImportPaths)
			{
				StdioOptions.DefaultImplicitImportPaths.emplace_back(Path);
			}
			for (const std::string_view Path : StdlibSearchPaths)
			{
				StdioOptions.DefaultStdlibSearchPaths.emplace_back(Path);
			}
			for (const std::string& Define : OwnedDefines)
			{
				StdioOptions.DefaultDefines.emplace_back(Define);
			}
			StdioOptions.DefaultPredefinedMacros = PredefinedMacros;
			StdioOptions.ScriptCompileExtensions = ScriptExtensions;
			StdioOptions.VmBackendFactory = VmBackendFactory;
			StdioOptions.VmBackendPath = VmBackendFactory ? std::string{} : VmBackendPath;
			StdioOptions.bDefaultAllowCircularImports = bAllowCircularImports;
			StdioOptions.bDefaultAllowProjectWideAccess = bAllowProjectWideAccess;
			StdioOptions.DefaultExecutionBackend = ExecutionBackend;
			StdioOptions.DefaultBindingCatalogPaths = BindingCatalogPaths;

			if (DapPort != 0)
			{
				Tooling::FDapTcpRunOptions TcpOptions;
				TcpOptions.BindAddress = DapBindAddress;
				TcpOptions.Port = static_cast<std::uint16_t>(DapPort);
				TcpOptions.Stdio = std::move(StdioOptions);
				return Tooling::VeroScriptDapTcp::Run(TcpOptions);
			}

			#if defined(_WIN32)
			if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1)
			{
				std::cerr << "VeroScript: failed to configure binary debug stdio\n";
				return 1;
			}
			#endif
			return Tooling::VeroScriptDapStdio::Run(std::cin, std::cout, StdioOptions);
		}

		if (!BindingCatalogPaths.empty() && (bEval || bRepl || bWatch || bWatchOnce))
		{
			std::cerr << "VeroScript: --binding-catalog cannot be combined with -e/-i/--watch\n";
			return 2;
		}

	#if defined(VEROSCRIPT_WITH_APPLICATION)
		auto ApplicationExtension =
		    ScriptArgIndex < argc ? PathUtil::ToUtf8(PathUtil::FromUtf8(argv[ScriptArgIndex]).extension()) : std::string{};
		std::transform(ApplicationExtension.begin(), ApplicationExtension.end(), ApplicationExtension.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		const bool ApplicationInput =
		    ScriptArgIndex < argc &&
		    (PackageFileKind(argv[ScriptArgIndex]) == EPackageFileKind::Application || ApplicationExtension == ".zip" ||
		        std::filesystem::is_directory(PathUtil::FromUtf8(argv[ScriptArgIndex])) ||
		        PathUtil::FromUtf8(argv[ScriptArgIndex]).filename() == "veroscript-app.json");
		if (!ApplicationInput && !ApplicationOptionsFile.empty())
		{
			std::cerr << "VeroScript: --application-options requires an application container or directory\n";
			return 2;
		}
		if (ApplicationInput)
		{
			if (!PrepareBindings()) return 2;
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			if (!PackageSelection.Path.empty() || PackageSelection.ComponentRoot)
			{
				Packages::FRunEnvironmentRequest R;
				R.StartupDirectory = StartupDirectory;
				R.Executable = Executable;
				R.Selection = PackageSelection;
				R.EntryFile = argv[ScriptArgIndex];
				Packages::FDiagnostic E;
				if (!Packages::ResolveRunEnvironment(R, InstalledPackages, E))
				{
					std::cerr << E.Code << ": " << E.Message << '\n';
					return 2;
				}
			}
			else
			{
				InstalledPackages.reset();
				PackageModuleEnvironment.reset();
			}
			#endif
		}
		else
	#endif
		if (!PrepareEnvironment(true, false))
		{
			return 2;
		}
		FCommandLineContext ModuleContext{StartupDirectory, Executable, &std::cin, &std::cout, &std::cerr};
		ModuleContext.LanguageTag = Locale::GetLanguageTag();
	#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
		ModuleContext.Environment = PackageModuleEnvironment;
	#endif
		std::string ModuleError;
		if (!Extensions.Configure(ModuleContext, ModuleError))
		{
			std::cerr << "VeroScript: " << ModuleError << '\n';
			return 2;
		}

		if (bEval)
		{
			if (ScriptArgIndex < argc)
			{
				std::cerr << "VeroScript: unexpected argument: " << argv[ScriptArgIndex] << '\n';
				return 2;
			}
			FStreamEngineHost Host(std::cout, std::cerr);
			const FRunResult Result = VeroScriptEngine::RunSource(
			    EvalSource, "<eval>", Host, ERunMode::EvalOnce, MakeExecutionConfig());
			return Result.ExitCode;
		}

		if (bRepl)
		{
			if (ScriptArgIndex < argc)
			{
				std::cerr << "VeroScript: unexpected argument: " << argv[ScriptArgIndex] << '\n';
				return 2;
			}
			return RunRepl(ImportPaths, bReplStrict, Defines, StdlibSearchPaths, ExecutionBackend,
			    PredefinedMacros, ImplicitImportPaths, bAllowCircularImports,
			    bAllowProjectWideAccess, JitBackendFactory, ScriptExtensions,
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			    PackageModuleEnvironment,
			#else
			    {},
			#endif
			    VmBackendFactory, VmBackendPath);
		}

		if (ScriptArgIndex >= argc)
		{
			PrintUsage();
			return 2;
		}

		std::string_view ScriptPath = argv[ScriptArgIndex];
	#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
		const std::string PackageScriptPath =
		    PackageModuleEnvironment ? ResolveCliFilePath(ScriptPath, StartupDirectory) : std::string{};
		if (PackageModuleEnvironment)
		{
			ScriptPath = PackageScriptPath;
		}

	#endif
		std::vector<std::string_view> ScriptArgs;
		for (int ArgIndex = ScriptArgIndex + 1; ArgIndex < argc; ++ArgIndex)
		{
			ScriptArgs.push_back(argv[ArgIndex]);
		}

		if (!PackageOutputPath.empty())
		{
			const EPackageFileKind OutputKind =
			    PackageFileKind(PackageOutputPath);
			if (OutputKind == EPackageFileKind::None)
			{
				std::cerr
				    << "VeroScript: package output must end in .vsapp or .vslib\n";
				return 2;
			}
			if (IsPackagePath(ScriptPath))
			{
				std::cerr
				    << "VeroScript: --emit-package input must be source, not a package file\n";
				return 2;
			}
			if (!ScriptArgs.empty())
			{
				std::cerr
				    << "VeroScript: --emit-package does not accept script arguments\n";
				return 2;
			}

			FStreamEngineHost Host(std::cout, std::cerr);
			VeroScriptSession Session(Host);
			FEngineConfig Config = MakeExecutionConfig();
			Config.bUseBytecodeCache = false;
			if (!Session.ApplyConfig(Config))
			{
				return 2;
			}
			if (Bindings != nullptr && !Config.ModuleEnvironment)
			{
				Session.GetBindingRegistry().Swap(BindingRegistry);
			}

			std::unique_ptr<FBytecodeModule> Module;
			const FRunResult Compile = Session.CompileFile(ScriptPath, Module);
			if (!Compile.bSuccess || !Module)
			{
				return Compile.ExitCode;
			}
			FPackageBuildOptions BuildOptions;
			BuildOptions.Kind =
			    OutputKind == EPackageFileKind::Application
			    ? EPackageKind::Application
			    : EPackageKind::Library;
			BuildOptions.PackageName = PathUtil::ToUtf8(
			    PathUtil::FromUtf8(PackageOutputPath).stem());
			BuildOptions.PackageVersion = PackageVersion;
			BuildOptions.bStripDebugInfo = bStripPackageDebug;
			std::vector<std::uint8_t> PackageBytes;
			std::string PackageError;
			bool Built = false;
			#if defined(VEROSCRIPT_WITH_APPLICATION)
			if (OutputKind == EPackageFileKind::Application)
			{
				Application::FDiagnostic E;
				std::vector<Application::FContentFile> Files;
				Built = Application::BuildApplication(*Module, BuildOptions, "0.1.1", {}, Files, E) && Application::WriteArchive(Files, PackageBytes, E);
				if (!Built)
				{
					PackageError = E.Code + ": " + E.Message;
				}
			}
			else
			#endif
				Built = BuildPackage(*Module, BuildOptions, PackageBytes, PackageError);
			if (!Built || !WriteBinaryFileAtomically(PackageOutputPath, PackageBytes, PackageError))
			{
				std::cerr << "VeroScript: failed to emit package: "
				          << PackageError << '\n';
				return 2;
			}
			return 0;
		}

	#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (ApplicationInput)
		{
			if (bWatch)
			{
				std::cerr << "VeroScript: application snapshots are replaced through the application host API\n";
				return 2;
			}
			auto Services = Application::CreateApplicationServices();
			if (!ApplicationOptionsFile.empty())
			{
				FArtifactData Options;
				std::string Failure;
				Application::FDiagnostic E;
				if (!ReadArtifactFile({ApplicationOptionsFile, "configuration", {}}, {4ull << 20, {}}, Options, Failure) ||
				    !Application::ConfigureApplicationServices(std::string_view(reinterpret_cast<const char*>(Options.Bytes.data()), Options.Bytes.size()), Services, E))
				{
					std::cerr << "VeroScript: " << (Failure.empty() ? E.Code + ": " + E.Message : Failure) << '\n';
					return 2;
				}
			}
			for (const auto& P : StdlibSearchPaths)
			{
				Services.StandardLibraryPaths.emplace_back(ResolveCliFilePath(P, StartupDirectory));
			}
			Services.AnalysisOnly = bCheckOnly;
			#if defined(VEROSCRIPT_WITH_GUI)
			Services.PreparePackage = [&GuiTools](const auto& Package, auto& Error)
			{
				return !Package.HostMetadata.count("veroscript.gui.contract") || GuiTools.UseArtifact(Package, Error);
			};
			#endif
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			Packages::FCoreEnvironmentOptions Options;
			Options.BaseBindings = Bindings ? &BindingRegistry : nullptr;
			Options.ScriptCompileExtensions = ScriptExtensions;
			Options.StdlibSearchPaths = Services.StandardLibraryPaths;
			if (!PackageVmId.empty())
			{
				Options.SelectedBackends["vm"] = PackageVmId;
			}
			if (!PackageJitId.empty())
			{
				Options.SelectedBackends["jit"] = PackageJitId;
			}
			if (bDisableVmBackend)
			{
				Options.SelectedBackends["vm"] = "";
			}
			if (bDisableJitBackend)
			{
				Options.SelectedBackends["jit"] = "";
			}
			#if defined(VEROSCRIPT_WITH_GUI)
			for (const auto& Choice : Extensions.GetValues("veroscript-gui-tools"))
			{
				if (Choice.second.size() == 1)
				{
					Options.SelectedBackends[Choice.first.substr(0, Choice.first.size() - std::string("-backend").size())] = Choice.second.front();
				}
			}
			Application::ConfigurePackageApplicationServices(Services, InstalledPackages, Options,
			    [&GuiTools](auto Providers, auto Environment, auto SelectBackend)
			{
				Gui::FScriptServices Runtime;
				Runtime.ReadResource = Gui::MakeGuiResourceReader(Environment);
				Runtime.Application = Gui::MakeRegisteredServiceFactories(std::move(Providers), std::move(Environment));
				Runtime.DefaultBackend = std::move(SelectBackend);
				GuiTools.SetRuntimeServices(std::move(Runtime));
			});
			#else
			Application::ConfigurePackageApplicationServices(Services, InstalledPackages, Options);
			#endif
			#endif
			auto Path = std::filesystem::absolute(PathUtil::FromUtf8(ScriptPath)).lexically_normal();
			if (Path.filename() == "veroscript-app.json")
			{
				Path = Path.parent_path();
			}
			Application::FDiagnostic E;
			auto App = Application::FApplication::Open(PathUtil::ToUtf8(Path), Services, E);
			if (!App)
			{
				std::cerr << "VeroScript application [" << E.Code << "]: " << E.Message << '\n';
				return 2;
			}
			FStreamEngineHost Host(std::cout, std::cerr);
			VeroScriptSession Session(Host);
			auto Config = MakeExecutionConfig();
			Config.ScriptArgs = ScriptArgs;
			Config.ModuleEnvironment.reset();
			if (!Session.ApplyConfig(Config))
			{
				return 2;
			}
			if (Bindings)
			{
				// Application preparation still borrows BaseBindings while building its environment.
				Session.GetBindingRegistry() = BindingRegistry;
			}
			const auto Loaded = Session.LoadApplication(std::move(App));
			if (!Loaded.bSuccess)
			{
				std::cerr << Loaded.Error.Message << '\n';
				return Loaded.ExitCode;
			}
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			for (const auto& Choice : {std::pair<std::string_view, std::string_view>{"vm", PackageVmId}, {"jit", PackageJitId}})
			{
				if (Choice.second.empty() || Choice.second == AutomaticBackend || Choice.second == DisabledBackend)
				{
					continue;
				}
				const auto Environment = Session.GetModuleEnvironment();
				if (!Environment || (Choice.first == "vm" ? !Environment->GetDescription().ExecutionBackends.Vm : !Environment->GetDescription().ExecutionBackends.Jit))
				{
					std::cerr << "VeroScript: backend ID requires an available package environment: " << Choice.second << '\n';
					return 2;
				}
				std::string Error;
				if (!(Choice.first == "vm" ? Session.SetVmBackendPath(Choice.second, Error)
				                           : Session.SetJitBackendPath(Choice.second, Error)))
				{
					std::cerr << "VeroScript: " << Error << '\n';
					return 2;
				}
			}
			#endif
			return bCheckOnly ? 0 : Session.Run().ExitCode;
		}
	#endif
		if (IsPackagePath(ScriptPath))
		{
			if (bWatch)
			{
				std::cerr
				    << "VeroScript: --watch is not supported for package files; "
				    << "the host should atomically replace and ReloadPackage\n";
				return 2;
			}
			FPackageData Package;
			{
				std::vector<std::uint8_t> PackageBytes;
				std::string PackageError;
				if (!ReadBinaryFile(ScriptPath, PackageBytes, PackageError))
				{
					std::cerr << "VeroScript: " << PackageError << ": " << ScriptPath << '\n';
					return 2;
				}
				if (!ReadPackage(FByteView(PackageBytes), Package, PackageError))
				{
					std::cerr << "VeroScript: invalid package '" << ScriptPath << "': " << PackageError << '\n';
					return 2;
				}
			}
			const EPackageFileKind ExpectedKind = PackageFileKind(ScriptPath);
			const bool bKindMatches =
			    (ExpectedKind == EPackageFileKind::Application && Package.Kind == EPackageKind::Application) || (ExpectedKind == EPackageFileKind::Library && Package.Kind == EPackageKind::Library);
			if (!bKindMatches)
			{
				std::cerr
				    << "VeroScript: package extension does not match package kind: "
				    << ScriptPath << '\n';
				return 2;
			}
			if (bCheckOnly)
			{
				return 0;
			}
			if (Package.Kind == EPackageKind::Library)
			{
				std::cerr
				    << "VeroScript: .vslib is a library and cannot be run directly; "
				    << "run a .vsapp that depends on it\n";
				return 2;
			}

			FStreamEngineHost Host(std::cout, std::cerr);
			VeroScriptSession Session(Host);
			#if defined(VEROSCRIPT_WITH_GUI)
			if (Package.HostMetadata.count("veroscript.gui.contract"))
			{
				std::string GuiError;
				if (!GuiTools.UseArtifact(Package, GuiError))
				{
					std::cerr << GuiError << '\n';
					return 1;
				}
			}
			#endif
			FEngineConfig Config = MakeExecutionConfig();
			Config.ScriptArgs = ScriptArgs;
			if (!Session.ApplyConfig(Config))
			{
				return 2;
			}
			if (Bindings != nullptr && !Config.ModuleEnvironment)
			{
				Session.GetBindingRegistry().Swap(BindingRegistry);
			}
			const FRunResult Load =
			    Session.LoadPackage(Package, std::string(ScriptPath));
			if (!Load.bSuccess)
			{
				return Load.ExitCode;
			}
			return Session.Run().ExitCode;
		}

		if (bWatch)
		{
			return RunWatch(ScriptPath, ImportPaths, ScriptArgs, bWatchOnce, bUseBytecodeCache, Defines,
			    BytecodeCacheDirectory, StdlibSearchPaths, ExecutionBackend, PredefinedMacros,
			    ImplicitImportPaths, bAllowCircularImports, bAllowProjectWideAccess,
			    JitBackendFactory, ScriptExtensions,
			#if defined(VEROSCRIPT_WITH_PACKAGE_MANAGER)
			    PackageModuleEnvironment,
			#else
			    {},
			#endif
			    VmBackendFactory, VmBackendPath);
		}

		if (bCheckOnly)
		{
			FStreamEngineHost Host(std::cout, std::cerr);
			VeroScriptSession Session(Host);
			auto Config = MakeExecutionConfig();
			if (!Session.ApplyConfig(Config))
			{
				return 2;
			}
			if (Bindings != nullptr && !Config.ModuleEnvironment)
			{
				Session.GetBindingRegistry().Swap(BindingRegistry);
			}
			std::unique_ptr<FBytecodeModule> Module;
			return Session.CompileFile(ScriptPath, Module).ExitCode;
		}
		FStreamEngineHost Host(std::cout, std::cerr);
		VeroScriptSession Session(Host);
		FEngineConfig Config = MakeExecutionConfig();
		Config.ScriptArgs = ScriptArgs;
		if (!Session.ApplyConfig(Config))
		{
			return 2;
		}
		if (Bindings != nullptr && !Config.ModuleEnvironment)
		{
			Session.GetBindingRegistry().Swap(BindingRegistry);
		}
		const FRunResult Result = Session.LoadFile(ScriptPath);
		if (!Result.bSuccess) return Result.ExitCode;
		return Session.Run().ExitCode;
	}
	int Run(const int argc, char** argv)
	{
		if (argc < 1 || !argv || !argv[0])
		{
			PrintUsage();
			return 2;
		}
		Locale::InitializeFromEnvironment();
		#if defined(VEROSCRIPT_WITH_GUI)
		Gui::FToolOptions GuiToolOptions;
		GuiToolOptions.bHostOwnsCommandRegistration = true;
		auto GuiService = Gui::CreateCommandLineService(std::move(GuiToolOptions));
		auto GuiTools = GuiService->CreateRegistration();
		#endif
		FCommandLineExtensions Extensions;
		#if defined(VEROSCRIPT_WITH_GUI)
		std::string GuiError;
		if (!Extensions.Register(GuiTools.Module, GuiError))
		{
			std::cerr << GuiError << '\n';
			return 2;
		}
		FNativeExtensionServiceBinding GuiServiceBinding;
		GuiServiceBinding.Id = Gui::ToolServiceId;
		GuiServiceBinding.Version = Gui::ToolServiceVersion;
		GuiServiceBinding.Service = GuiService;
		Extensions.Services.push_back(std::move(GuiServiceBinding));
		#endif
		if (argc <= 1 ||
		    (std::string_view(argv[1]) != "--cli-module" && std::string_view(argv[1]).rfind("--cli-module=", 0) != 0))
		{
			return RunWithModules(argc, argv, Extensions
			#if defined(VEROSCRIPT_WITH_GUI)
			    ,
			    GuiTools
			#endif
			);
		}

		std::vector<char*> Forwarded{argv[0]};
		std::error_code Error;
		const auto WorkingDirectory = PathUtil::ToUtf8(std::filesystem::current_path(Error));
		int Index = 1;
		while (Index < argc)
		{
			const std::string_view Arg = argv[Index];
			if (Arg != "--cli-module" && Arg.rfind("--cli-module=", 0) != 0)
			{
				break;
			}

			std::string Path;
			if (Arg == "--cli-module")
			{
				if (++Index >= argc)
				{
					std::cerr << "VeroScript: missing --cli-module file\n";
					return 2;
				}
				Path = argv[Index];
			}
			else
			{
				Path = std::string(Arg.substr(13));
			}

			std::string Message;
			if (Error || Path.empty() || !Extensions.Load(Path, WorkingDirectory, Message))
			{
				std::cerr << "VeroScript: " << (Message.empty() ? "invalid --cli-module path or working directory" : Message) << '\n';
				return 2;
			}
			++Index;
		}
		Forwarded.insert(Forwarded.end(), argv + Index, argv + argc);
		return RunWithModules(static_cast<int>(Forwarded.size()), Forwarded.data(), Extensions
		#if defined(VEROSCRIPT_WITH_GUI)
		    ,
		    GuiTools
		#endif
		);
	}

}
