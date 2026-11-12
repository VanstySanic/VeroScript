// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCSharpAot.h"
#include "VeroScriptHostBindingCatalog.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool WriteText(const std::filesystem::path& Path, const std::string& Text)
	{
		std::ofstream Output(Path, std::ios::out | std::ios::binary | std::ios::trunc);
		Output.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		return static_cast<bool>(Output);
	}

	bool ReadText(const std::filesystem::path& Path, std::string& Text)
	{
		std::ifstream Input(Path, std::ios::in | std::ios::binary);
		if (!Input)
		{
			return false;
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		Text = Buffer.str();
		for (std::size_t Position = 0;
			(Position = Text.find("\r\n", Position)) != std::string::npos;)
		{
			Text.replace(Position, 2, "\n");
		}
		return true;
	}

	std::string Quote(const std::filesystem::path& Path)
	{
		return "\"" + Path.string() + "\"";
	}

	bool Run(const std::string& Command, const char* Label)
	{
		#if defined(_WIN32)
		const std::string ShellCommand = "\"" + Command + "\"";
		#else
		const std::string& ShellCommand = Command;
		#endif
		const int Code = std::system(ShellCommand.c_str());
		if (Code == 0)
		{
			return true;
		}
		std::cerr << "FAIL: " << Label << " (exit=" << Code << ")\n"
				  << Command << '\n';
		return false;
	}
}

int main()
{
	const std::filesystem::path Root(VEROSCRIPT_CSHARP_AOT_BINDING_TEST_DIR);
	std::error_code ErrorCode;
	std::filesystem::remove_all(Root, ErrorCode);
	ErrorCode.clear();
	std::filesystem::create_directories(Root, ErrorCode);
	if (ErrorCode)
	{
		std::cerr << "FAIL: create binding test directory: " << ErrorCode.message() << '\n';
		return EXIT_FAILURE;
	}

	const std::filesystem::path HostSource = Root / "Host.cs";
	const std::filesystem::path Catalog = Root / "bindings.json";
	const std::filesystem::path Bridge = Root / "Bindings.Aot.g.cs";
	const std::filesystem::path Project = Root / "Generated.csproj";
	const std::filesystem::path Output = Root / "output.txt";
	#if VEROSCRIPT_CSHARP_AOT_TEST_NATIVE
	const std::filesystem::path NativeOutput = Root / "native-output.txt";
	const std::filesystem::path Publish = Root / "publish";
	#endif
	const std::string Host = R"CS(public sealed class Counter
{
    public const int Maximum = 10;
    public static int Answer { get; private set; } = 42;
    public int Value { get; set; }
    public Counter(int initial) => Value = initial;
    public Counter(ref int initial) { Value = initial; initial += 10; }
    public Counter(string initial) => Value = int.Parse(initial,
        System.Globalization.CultureInfo.InvariantCulture);
    public int Add(in int amount) => Value + amount;
    public void Bump(ref int value) => value += Value;
    public void Assign(out int value) => value = Value;
    public int ObserveAliasing(ref int first, ref int second) { first = 42; return second; }
    public int AssignAliasing(ref int first, out int second) { second = 43; return first; }
    public int ReadAliasing(ref int first, in int second) { first = 44; return second; }
    public int ReplaceAliasedList(ref System.Collections.Generic.List<int> first,
        in System.Collections.Generic.List<int> second) { first = new() { 2, 3 }; return second.Count; }
    public static string Label(int value) => value.ToString(
        System.Globalization.CultureInfo.InvariantCulture);
    public static System.Collections.Generic.List<int> Values(
        System.Collections.Generic.List<int> values) => values;
    public static System.Collections.Generic.Dictionary<int, int> Scores(
        System.Collections.Generic.Dictionary<int, int> values) => values;
}
public abstract class AbstractHost
{
    public AbstractHost() { }
    public abstract int Read();
    public static AbstractHost Create() => new Implementation();
    private sealed class Implementation : AbstractHost { public override int Read() => 42; }
}
)CS";
	if (!WriteText(HostSource, Host))
	{
		std::cerr << "FAIL: write managed host input\n";
		return EXIT_FAILURE;
	}

	const std::filesystem::path DotNet(VEROSCRIPT_CSHARP_AOT_DOTNET);
	const std::filesystem::path BindGenProject(VEROSCRIPT_CSHARP_BINDGEN_CLI_PROJECT);
	const std::string BindGenCommand = Quote(DotNet) + " run --project "
		+ Quote(BindGenProject) + " --configuration Release --framework net9.0 -- "
		+ Quote(HostSource) + " --catalog " + Quote(Catalog)
		+ " --aot-bridge " + Quote(Bridge);
	if (!Run(BindGenCommand, "generate managed AOT bindings"))
	{
		return EXIT_FAILURE;
	}

	VeroScript::FHostBindingRegistry Bindings;
	std::string Error;
	if (!VeroScript::LoadBindingCatalogFile(Catalog.string(), Bindings, Error))
	{
		std::cerr << "FAIL: load generated C# binding catalog\n"
				  << Error << '\n';
		return EXIT_FAILURE;
	}
	const std::string Script =
		"void main() { Counter counter = Counter(4); counter.Value += 2; "
		"print(counter.Add(3)); int changed = 1; counter.Bump(ref changed); print(changed); "
		"int assigned = 0; counter.Assign(ref assigned); print(assigned); "
		"print(Counter.Label(counter.Value)); print(Counter::Maximum); print(Counter::Answer); "
		"array<int> values = {1, 2}; print(len(Counter.Values(values))); "
		"Counter textCounter = Counter(\"5\"); print(textCounter.Value); "
		"map<int, int> scores = {1: 11}; print(Counter.Scores(scores)[1]); "
		"int seed = 8; Counter refCounter = Counter(ref seed); "
		"print(refCounter.Value); print(seed); "
		"AbstractHost abstractHost = AbstractHost.Create(); print(abstractHost.Read()); "
		"int alias = 0; print(counter.ObserveAliasing(ref alias, ref alias)); "
		"print(counter.AssignAliasing(ref alias, ref alias)); print(counter.ReadAliasing(ref alias, ref alias)); "
		"array<int> aliasValues = {1}; print(counter.ReplaceAliasedList(ref aliasValues, ref aliasValues)); }";
	VeroScript::FCodegenOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	Options.Bindings = &Bindings;
	VeroScript::FCSharpAotSourceBundle Bundle;
	if (!VeroScript::EmitCSharpSourceBundleFromSource(Script, "managed-bindings.vs",
		"Generated", Options, Bundle, Error) ||
		Bundle.Files.size() != 4)
	{
		std::cerr << "FAIL: emit multi-file C# with generated managed bindings\n"
			<< Error << '\n';
		return EXIT_FAILURE;
	}
	for (const VeroScript::FCSharpAotSourceFile& File : Bundle.Files)
	{
		std::string Contents = File.Contents;
		if (File.RelativePath == "Generated.csproj")
		{
			const std::size_t ItemGroupEnd = Contents.find("  </ItemGroup>");
			if (ItemGroupEnd == std::string::npos)
			{
				std::cerr << "FAIL: generated binding project has no compilation item group\n";
				return EXIT_FAILURE;
			}
			Contents.insert(ItemGroupEnd,
				"    <Compile Include=\"Host.cs\" />\n"
				"    <Compile Include=\"Bindings.Aot.g.cs\" />\n");
		}
		if (!WriteText(Root / File.RelativePath, Contents))
		{
			std::cerr << "FAIL: write generated binding bundle\n";
			return EXIT_FAILURE;
		}
	}
	const std::string RunCommand = Quote(DotNet) + " run --project " + Quote(Project)
		+ " --configuration Release > " + Quote(Output);
	if (!Run(RunCommand, "compile and run generated managed binding application"))
	{
		return EXIT_FAILURE;
	}
	std::string Actual;
	if (!ReadText(Output, Actual) || Actual != "9\n7\n6\n6\n10\n42\n2\n5\n11\n8\n18\n42\n42\n43\n44\n2\n")
	{
		std::cerr << "FAIL: managed AOT binding output\nactual=[" << Actual
			<< "]\nexpected=[9\\n7\\n6\\n6\\n10\\n42\\n2\\n5\\n11\\n8\\n18\\n42\\n42\\n43\\n44\\n2\\n]\n";
		return EXIT_FAILURE;
	}

	#if VEROSCRIPT_CSHARP_AOT_TEST_NATIVE
	const std::string PublishCommand = Quote(DotNet) + " publish " + Quote(Project)
		+ " --configuration Release --runtime " VEROSCRIPT_CSHARP_AOT_RID
		+ " --self-contained true -p:PublishAot=true --output " + Quote(Publish)
		+ " --nologo";
	if (!Run(PublishCommand, "publish managed binding application with NativeAOT"))
	{
		return EXIT_FAILURE;
	}
	#if defined(_WIN32)
	const std::filesystem::path NativeExe = Publish / "Generated.exe";
	#else
	const std::filesystem::path NativeExe = Publish / "Generated";
	#endif
	const std::string NativeCommand = Quote(NativeExe) + " > " + Quote(NativeOutput);
	if (!Run(NativeCommand, "run NativeAOT managed binding application"))
	{
		return EXIT_FAILURE;
	}
	Actual.clear();
	if (!ReadText(NativeOutput, Actual)
		|| Actual != "9\n7\n6\n6\n10\n42\n2\n5\n11\n8\n18\n42\n42\n43\n44\n2\n")
	{
		std::cerr << "FAIL: NativeAOT managed binding output\nactual=[" << Actual
			<< "]\nexpected=[9\\n7\\n6\\n6\\n10\\n42\\n2\\n5\\n11\\n8\\n18\\n42\\n42\\n43\\n44\\n2\\n]\n";
		return EXIT_FAILURE;
	}
	#endif

	std::cout << "VeroScriptCSharpAotBindingTests passed.\n";
	return EXIT_SUCCESS;
}
