// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPath.h"
#include "VeroScriptBuild.h"
#include "VeroScriptSession.h"
#include "VeroScriptHost.h"
#include "VeroScriptExecutable.h"
#include "VeroScriptTestVm.h"
#include "VeroScriptLambdaStaticFixture.h"
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>

namespace
{
	using namespace VeroScript;
	namespace B = VeroScript::Build;
	namespace A = VeroScript::Application;
	namespace fs = std::filesystem;
	using J = nlohmann::json;
	void Check(bool V, const std::string& Message)
	{
		if (!V)
		{
			throw std::runtime_error(Message);
		}
	}
	void CheckBuild(bool V, const B::FDiagnostic& E, const char* Context)
	{
		if (!V)
		{
			throw std::runtime_error(std::string(Context) + ": " + E.Code + " " + E.Message + " " + E.Path);
		}
	}
	void Write(const fs::path& P, const std::string& Text)
	{
		fs::create_directories(P.parent_path());
		std::ofstream O(P, std::ios::binary | std::ios::trunc);
		O << Text;
	}
	class FGenerator final : public B::IBuildStepProvider
	{
	public:
		mutable int Runs = 0;
		std::string GetIdentity() const override
		{
			return "test.generator";
		}
		std::string GetVersion() const override
		{
			return "0.1.1";
		}
		bool IsDeterministic() const noexcept override
		{
			return true;
		}
		bool Plan(const std::string&, const std::string&, B::FDiagnostic&) const override
		{
			return true;
		}
		bool Execute(const B::FStepContext& C, B::FDiagnostic&) const override
		{
			++Runs;
			const auto Root = PathUtil::FromUtf8(C.Outputs.at("sources").Path);
			Write(Root / "main.vs", "import \"helper.vs\"; int main() { return helper(); }");
			Write(Root / "helper.vs", "export int helper() { return 38; }");
			return true;
		}
	};
	class FMutatingProvider final : public B::IBuildStepProvider
	{
	public:
		std::string GetIdentity() const override
		{
			return "test.input-mutation";
		}
		std::string GetVersion() const override
		{
			return "0.1.1";
		}
		bool Plan(const std::string&, const std::string&, B::FDiagnostic&) const override
		{
			return true;
		}
		bool Execute(const B::FStepContext& C, B::FDiagnostic&) const override
		{
			const auto Input = C.Inputs.at("template").Path;
			Write(PathUtil::FromUtf8(Input), "changed during execution");
			C.TrackInput(Input);
			Write(PathUtil::FromUtf8(C.Outputs.at("main").Path), "must not be published");
			return true;
		}
	};
	J Config();
	void ProviderBoundaries(const fs::path& Root)
	{
		auto C = Config();
		C["defaultTargets"] = {"generate"};
		C["targets"]["generate"]["options"] = {{"target", "vulkan"}, {"output", "shader.spv"},
		    {"nested", {{"target", "provider-data"}, {"output", "unrelated"}}}};
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		S.Providers.emplace("generator", std::make_shared<FGenerator>());
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "provider options are opaque to the artifact graph");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "execute opaque provider options");
		Result = {};
		C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "portable"}, {"defaultTargets", {"mutation"}},
		    {"targets", {{"mutation", {{"kind", "custom"}, {"provider", "mutation"}, {"options", J::object()}, {"inputs", {{"template", "template.txt"}}}, {"outputs", {{"main", {{"path", "result.txt"}, {"type", "file"}, {"kind", "generic"}}}}}}}}}};
		Write(Root / "veroscript-build.json", C.dump());
		Write(Root / "template.txt", "frozen input");
		S.Providers.emplace("mutation", std::make_shared<FMutatingProvider>());
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "input mutation plan");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "plan-stale", "retracking a changed input replaced its frozen identity");
		Check(!fs::exists(Root / "dist/release/portable/mutation/result.txt"), "changed inputs published an output");
	}
	J Config()
	{
		return {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "portable"},
		    {"defaultTargets", {"all"}}, {"targets", {{"generate", {{"kind", "custom"}, {"provider", "generator"}, {"cache", true}, {"options", J::object()}, {"outputs", {{"sources", {{"path", "source"}, {"type", "directory"}, {"kind", "source-tree"}}}}}}}, {"library", {{"kind", "library"}, {"entry", "src/library.vs"}, {"packageName", "sample.library"}, {"output", "sample.vslib"}}}, {"app", {{"kind", "application"}, {"entry", "src/main.vs"}, {"packageName", "sample.app"}, {"options", {{"version", "0.1.1"}}}, {"modules", {{"linked.vs", {{"target", "library"}, {"output", "main"}}}}}, {"output", "sample.vsapp"}}}, {"generated", {{"kind", "application"}, {"mode", "source"}, {"entry", {{"target", "generate"}, {"output", "sources"}, {"path", "main.vs"}}}, {"packageName", "sample.generated"}, {"options", {{"version", "0.1.1"}}}, {"output", "generated.vsapp"}}}, {"directory", {{"kind", "bundle"}, {"application", {{"target", "app"}, {"output", "main"}}}, {"layout", "directory"}, {"output", "SampleApp"}}}, {"zip", {{"kind", "bundle"}, {"application", {{"target", "app"}, {"output", "main"}}}, {"layout", "zip"}, {"output", "SampleApp.zip"}}}, {"all", {{"kind", "group"}, {"dependsOn", {"directory", "zip", "generated"}}}}}}};
	}
	void Run(const B::FBuildResult& Result, const std::string& Target, int Code)
	{
		std::string Path;
		for (const auto& R : Result.Targets)
		{
			if (R.Id == Target)
			{
				for (const auto& O : R.Artifacts)
				{
					if (O.Output == "main")
					{
						Path = O.Path;
					}
				}
			}
		}
		Check(!Path.empty(), "missing built application");
		std::ostringstream Out, Error;
		FStreamEngineHost Host(Out, Error);
		VeroScriptSession S(Host);
		Test::Configure(S);
		const auto Loaded = S.LoadApplication(Path, A::CreateApplicationServices());
		Check(Loaded.bSuccess, "load built app: " + Loaded.Error.Message);
		const auto R = S.Run();
		Check(R.bSuccess && R.ExitCode == Code, "run built app: " + R.Error.Message + Error.str());
	}
	void Exercise(const fs::path& Root)
	{
		Write(Root / "src/library.vs", "export int answer() { return 42; }");
		Write(Root / "src/main.vs", "import \"linked.vs\"; int main() { return answer(); }");
		auto C = Config();
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		S.Application = A::CreateApplicationServices();
		auto Generator = std::make_shared<FGenerator>();
		S.Providers.emplace("generator", Generator);
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		Check(B::FBuilder::Plan(Q, S, Plan, E), "plan: " + E.Code + " " + E.Message);
		Check(Generator->Runs == 0 && !fs::exists(Root / "dist"), "planning caused side effects");
		B::FBuildResult Result;
		Check(B::FBuilder::Execute(*Plan, Result, E), "execute: " + E.Code + " " + E.Message + " " + E.Path);
		Run(Result, "app", 42);
		Run(Result, "generated", 38);
		Run(Result, "directory", 42);
		for (const auto& Target : Result.Targets)
		{
			if (Target.Id == "directory")
			{
				A::FDiagnostic ModeError;
				auto App = A::FApplication::Open(Target.Artifacts.front().Path, A::CreateApplicationServices(), ModeError);
				Check(bool(App), "inspect bundled modes");
				for (const auto& File : App->GetEntries())
				{
					if (File.Path == "program/entry.bin")
					{
						Check(!File.Executable, "bundling changed the entry image executable mode");
					}
				}
			}
		}
		Check(Generator->Runs == 1, "shared generator ran more than once");
		Result = {};
		Check(B::FBuilder::Execute(*Plan, Result, E), "cached execute: " + E.Code + " " + E.Message);
		Check(Generator->Runs == 1, "deterministic generator cache was ignored");
		for (const auto& R : Result.Targets)
		{
			if (R.Id != "all")
			{
				Check(R.Cached, "pure target was not cached: " + R.Id);
			}
		}
		Result = {};
		Write(Root / "src/library.vs", "export int answer() { return 43; }");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "plan-stale", "stale plan was accepted");
		Check(B::FBuilder::Plan(Q, S, Plan, E), "updated plan: " + E.Message);
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "updated execute");
		Run(Result, "app", 43);
		std::string AppPath;
		for (const auto& R : Result.Targets)
		{
			if (R.Id == "app")
			{
				AppPath = R.Artifacts.front().Path;
			}
		}
		Result = {};
		Write(PathUtil::FromUtf8(AppPath), "foreign change");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "output-modified", "modified owned output was overwritten");
		C["targets"]["all"]["dependsOn"].push_back("all");
		Write(Root / "veroscript-build.json", C.dump());
		Check(!B::FBuilder::Plan(Q, S, Plan, E) && E.Code == "build-cycle", "cycle was accepted");
		Write(Root / "veroscript-build.json", "{\"format\":1,\"format\":2}");
		Check(!B::FBuilder::Plan(Q, S, Plan, E) && E.Code == "json-key", "duplicate JSON key was accepted");
	}
	void ScriptSteps(const fs::path& Root)
	{
		Write(Root / "tools/generate.vs", std::string(VeroScript::Test::LambdaStaticDefinitions) + "int main() { checkLambdaStaticSupport(); string code = build_read_text(\"source\", \"\"); build_write_text(\"sources\", \"main.vs\", code); return 0; }");
		Write(Root / "template.txt", "int main() { return 42; }");
		J C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "portable"}, {"defaultTargets", {"application"}},
		    {"targets", {{"generator", {{"kind", "custom"}, {"provider", "script"}, {"options", {{"entry", "tools/generate.vs"}}}, {"inputs", {{"source", "template.txt"}}}, {"outputs", {{"sources", {{"path", "source"}, {"type", "directory"}, {"kind", "source-tree"}}}}}}}, {"application", {{"kind", "application"}, {"mode", "source"}, {"packageName", "script.generated"}, {"output", "script.vsapp"}, {"options", {{"version", "0.1.1"}}}, {"entry", {{"target", "generator"}, {"output", "sources"}, {"path", "main.vs"}}}}}}}};
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		Check(!B::FBuilder::Plan(Q, S, Plan, E) && E.Code == "step-permission", "script custom step requires explicit permission");
		S.AllowScript = true;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "script without VM plan");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "script-vm", "script step must require an explicit VM");
		// The known build artifact may be reached through a junction or symlink.
		S.Compile.ScriptCompileExtensions.VmBackendPath = PathUtil::ToUtf8(fs::canonical(PathUtil::FromUtf8(VEROSCRIPT_TEST_VM_LIBRARY)));
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "script plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "script execute");
		Run(Result, "application", 42);
		Result = {};
		Write(Root / "template.txt", "int main() { return 43; }");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "plan-stale", "script declared input changes invalidate plan");
		Write(Root / "outside.vs", "export int value() { return 1; }");
		Write(Root / "tools/generate.vs", "import \"../outside.vs\"; int main() { return value(); }");
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "restricted script plan");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "script-compile", "script generator cannot import outside declared source roots");
	}
}
namespace
{
	void SourceCompilation(const fs::path& Root)
	{
		Write(Root / "main.vs", "#if ANSWER == 42\nint main() { return HOST(ANSWER); }\n#else\n#error Missing application compile defaults\n#endif\n");
		J C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "portable"}, {"defaultTargets", {"source"}},
		    {"targets", {{"source", {{"kind", "application"}, {"mode", "source"}, {"entry", "main.vs"}, {"packageName", "source.options"}, {"options", {{"version", "0.1.1"}}}, {"compile", {{"defines", {"ANSWER=42"}}}}, {"output", "source.vsapp"}}}}}};
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		S.Application = A::CreateApplicationServices();
		S.Compile.PredefinedMacros.push_back({"HOST", {"value"}, false, "value", "<build-test>"});
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "source options plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "source options execute");
		Run(Result, "source", 42);
		Q.BuildFile = PathUtil::ToUtf8(Root / "veroscript-build.json");
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "explicit configuration plan");
		Result = {};
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "explicit configuration preserves output ownership");
		Write(Root / "helper.vs", "export int helper() { return 1; }");
		Write(Root / "main.vs", "import \"helper.vs\"; int main() { return helper(); }");
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "unshipped source plan");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "source-compile", "unshipped relative import was accepted");
		C["targets"]["source"]["files"] = J::array({{{"from", "helper.vs"}, {"to", "program/helper.vs"}}});
		Write(Root / "veroscript-build.json", C.dump());
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "shipped source plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "shipped source execute");
		Run(Result, "source", 1);
		Write(Root / "implicit.vs", "export int implicit_value() { return 29; }");
		Write(Root / "main.vs", "int main() { return implicit_value(); }");
		const auto Implicit = PathUtil::ToUtf8(Root / "implicit.vs");
		S.Compile.ImplicitImportPaths.emplace_back(Implicit);
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "implicit source plan");
		Result = {};
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "implicit source execute");
		Run(Result, "source", 29);
		auto Library = VeroScriptCompile::CompileSource("export int implicit_value() { return 30; }");
		Check(Library.bSuccess, "compile implicit library");
		FPackageBuildOptions Package;
		Package.Kind = EPackageKind::Library;
		Package.PackageName = "implicit.library";
		std::vector<std::uint8_t> Image;
		std::string Failure;
		Check(BuildPackage(*Library.Module, Package, Image, Failure), "build implicit library: " + Failure);
		const auto ImplicitLibrary = PathUtil::ToUtf8(Root / "implicit.vslib");
		Write(PathUtil::FromUtf8(ImplicitLibrary), {Image.begin(), Image.end()});
		S.Compile.ImplicitImportPaths.clear();
		S.Compile.ImplicitImportPaths.emplace_back(ImplicitLibrary);
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "implicit library plan");
		Result = {};
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "implicit library execute");
		Run(Result, "source", 30);
	}
	void HostImportPaths(const fs::path& Root)
	{
		const auto Project = Root / "project", Modules = Root / "modules";
		Write(Project / "main.vs", "import \"library.vs\"; int main() { return library_value(); }");
		Write(Modules / "library.vs", "export int library_value() { return 63; }");
		const J C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "portable"}, {"defaultTargets", {"application"}},
		    {"targets", {{"application", {{"kind", "application"}, {"entry", "main.vs"}, {"packageName", "host.imports"}, {"options", {{"version", "0.1.1"}}}, {"output", "imports.vsapp"}}}}}};
		Write(Project / "veroscript-build.json", C.dump());
		const auto ImportRoot = PathUtil::ToUtf8(Modules);
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Project);
		B::FServices S;
		S.Compile.ImportPaths.emplace_back(ImportRoot);
		S.Application = A::CreateApplicationServices();
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "host import root plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "host import root execution");
		Run(Result, "application", 63);
	}
	void LauncherInputs(const fs::path& Root)
	{
		Write(Root / "main.vs", "int main() { return 0; }");
		Write(Root / "templates/first.bin", "first launcher");
		Write(Root / "templates/second.bin", "second launcher");
		const J C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", "host"}, {"defaultTargets", {"bundle"}},
		    {"targets", {{"application", {{"kind", "application"}, {"entry", "main.vs"}, {"packageName", "launcher.inputs"}, {"options", {{"version", "0.1.1"}}}, {"output", "app.vsapp"}}}, {"bundle", {{"kind", "bundle"}, {"application", {{"target", "application"}, {"output", "main"}}}, {"layout", "directory"}, {"launcher", {{"mode", "generated"}, {"output", "entry.bin"}}}, {"output", "app"}}}}}};
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		S.Application = A::CreateApplicationServices();
		S.LauncherTemplate = PathUtil::ToUtf8(Root / "templates/first.bin");
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "first launcher plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "first launcher execution");
		Result = {};
		S.LauncherTemplate = PathUtil::ToUtf8(Root / "templates/second.bin");
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "replacement launcher plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "replacement launcher execution");
		bool Rebuilt = false;
		for (const auto& T : Result.Targets)
		{
			if (T.Id == "bundle")
			{
				Rebuilt = !T.Cached;
				std::ifstream I(PathUtil::FromUtf8(T.Artifacts.front().Path) / "entry.bin", std::ios::binary);
				const std::string Bytes{std::istreambuf_iterator<char>(I), std::istreambuf_iterator<char>()};
				Check(Bytes == "second launcher", "changing launcher template reused a stale application bundle");
			}
		}
		Check(Rebuilt, "replacement launcher must invalidate bundle cache");
		Result = {};
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "frozen launcher plan");
		Write(Root / "templates/second.bin", "changed after planning");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "plan-stale", "launcher changes must invalidate a frozen plan");
	}
	void CrossTargetApplications(const fs::path& Root)
	{
		Write(Root / "main.vs", "int main() { return 47; }");
		auto Platform = J::parse(A::HostApplicationTargetJson());
		Platform["os"] = Platform.at("os") == "windows" ? "linux" : "windows";
		const J C = {{"format", "veroscript-build"}, {"formatVersion", "0.1.1"}, {"platform", Platform},
		    {"defaultTargets", {"compiled-bundle", "source-bundle"}}, {"targets", {{"compiled", {{"kind", "application"}, {"entry", "main.vs"}, {"packageName", "cross.compiled"}, {"options", {{"version", "0.1.1"}}}, {"output", "compiled.vsapp"}}}, {"source", {{"kind", "application"}, {"mode", "source"}, {"entry", "main.vs"}, {"packageName", "cross.source"}, {"options", {{"version", "0.1.1"}}}, {"output", "source.vsapp"}}}, {"compiled-bundle", {{"kind", "bundle"}, {"application", {{"target", "compiled"}, {"output", "main"}}}, {"output", "compiled.vsapp"}}}, {"source-bundle", {{"kind", "bundle"}, {"application", {{"target", "source"}, {"output", "main"}}}, {"output", "source.vsapp"}}}}}};
		Write(Root / "veroscript-build.json", C.dump());
		B::FBuildRequest Q;
		Q.ProjectRoot = PathUtil::ToUtf8(Root);
		B::FServices S;
		S.Application = A::CreateApplicationServices();
		B::FDiagnostic E;
		std::shared_ptr<const B::FBuildPlan> Plan;
		B::FBuildResult Result;
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "cross target application plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "cross target application packaging");
		auto Inspection = A::CreateApplicationServices();
		Inspection.ValidateTarget = [&](const std::string& Json, std::string&)
		{
			return J::parse(Json) == Platform;
		};
		std::string CompiledPath;
		for (const auto& T : Result.Targets)
		{
			for (const auto& Artifact : T.Artifacts)
			{
				if (T.Id == "compiled")
				{
					CompiledPath = Artifact.Path;
				}
				A::FDiagnostic Error;
				Check(!A::FApplication::Open(Artifact.Path, A::CreateApplicationServices(), Error) && Error.Code == "application-target",
				    "target packaging must not relax runtime host validation");
				const auto App = A::FApplication::Open(Artifact.Path, Inspection, Error);
				Check(App && J::parse(App->GetDescriptorJson()).at("platform") == Platform, "cross target application retains its declared platform");
			}
		}
		Result = {};
		auto Mismatch = C;
		Mismatch["platform"]["arch"] = "different-architecture";
		Mismatch["defaultTargets"] = {"compiled-bundle"};
		Mismatch["targets"]["compiled-bundle"]["application"] = CompiledPath;
		Write(Root / "veroscript-build.json", Mismatch.dump());
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "mismatched application plan");
		Check(!B::FBuilder::Execute(*Plan, Result, E) && E.Code == "application-target", "bundling must reject an application for a different target");
		auto Portable = C;
		Portable["platform"] = "portable";
		Portable["defaultTargets"] = {"compiled"};
		Write(Root / "veroscript-build.json", Portable.dump());
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "portable application plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "portable application packaging");
		CompiledPath = Result.Targets.front().Artifacts.front().Path;
		Result = {};
		auto Targeted = C;
		Targeted["defaultTargets"] = {"compiled-bundle"};
		Targeted["targets"]["compiled-bundle"]["application"] = CompiledPath;
		Write(Root / "veroscript-build.json", Targeted.dump());
		CheckBuild(B::FBuilder::Plan(Q, S, Plan, E), E, "targeted portable bundle plan");
		CheckBuild(B::FBuilder::Execute(*Plan, Result, E), E, "targeted portable bundle packaging");
		A::FDiagnostic Error;
		const auto App = A::FApplication::Open(Result.Targets.front().Artifacts.front().Path, Inspection, Error);
		Check(App && J::parse(App->GetDescriptorJson()).at("platform") == Platform, "targeted bundle must not retain a portable platform declaration");
	}
}
int main(int Count, char** Values)
{
	try
	{
		if (Count != 2) return 2;
		const auto Root = fs::absolute(PathUtil::FromUtf8(Values[1])) / ("build-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(Root);
		ProviderBoundaries(Root / "provider-boundaries");
		Exercise(Root);
		ScriptSteps(Root / "script-steps");
		SourceCompilation(Root / "source-options");
		HostImportPaths(Root / "host-imports");
		LauncherInputs(Root / "launcher-inputs");
		CrossTargetApplications(Root / "cross-target");
		std::cout << "Build graph, references, source trees, cache and output ownership: passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
