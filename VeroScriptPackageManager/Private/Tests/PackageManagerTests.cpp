// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptLambdaStaticFixture.h"
#include "../PackageInstalledInternal.h"
#include "../PackageSignatures.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHost.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPackageManager.h"
#include "VeroScriptSession.h"
#include "VeroScriptVm.h"
#include "miniz/miniz.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace VeroScript;
using namespace VeroScript::Packages;
namespace
{
	using J = nlohmann::json;
	namespace fs = std::filesystem;
	using Detail::NativePathText;
	using Detail::U8Path;
	int Failures = 0;
	unsigned Checks = 0;
	void Expect(bool Good, const std::string& Label)
	{
		++Checks;
		if (!Good)
		{
			++Failures;
			std::cerr << "FAIL: " << Label << '\n';
		}
	}
	void Write(const fs::path& P, const std::string& S)
	{
		fs::create_directories(P.parent_path());
		std::ofstream F(P, std::ios::binary);
		F << S;
		if (!F)
		{
			throw std::runtime_error("fixture write failed: " + NativePathText(P));
		}
	}
	J Read(const fs::path& P)
	{
		std::ifstream F(P, std::ios::binary);
		return J::parse(F);
	}
	J Manifest(const std::string& Name)
	{
		return {{"format", "veroscript-package"},
		    {"formatVersion", "0.1.1"},
		    {"name", Name},
		    {"version", "0.1.1"}};
	}
	bool Run(const std::shared_ptr<const FInstalledEnvironment>& Snapshot, const fs::path& Project,
	    int Expected)
	{
		FCoreEnvironmentOptions O;
		O.EntryRoot = NativePathText(Project);
		std::shared_ptr<const FModuleEnvironment> E;
		FDiagnostic Error;
		if (!Snapshot->BuildCoreEnvironment(O, E, Error))
		{
			std::cerr << Error.Code << ": " << Error.Message << '\n';
			return false;
		}
		FCompileOptions C;
		VeroScript::Test::Configure(C);
		C.ModuleEnvironment = E;
		auto Code = VeroScriptCompile::CompileSource("import \"sample/math.vs\";int main(){return answer();}",
		    NativePathText(Project / "main.vs"), C);
		if (!Code.bSuccess)
		{
			std::cerr << Code.Error.Message << '\n';
			return false;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string Failure;
		if (!Vm.SetModuleEnvironment(E, Failure) || !Vm.Execute(*Code.Module, ERunMode::Script))
		{
			std::cerr << Failure << Vm.GetError().Message << '\n';
			return false;
		}
		if (Vm.GetProcessExitCode() != Expected)
		{
			return false;
		}
		std::ostringstream SessionOut, SessionErrors;
		FStreamEngineHost Host(SessionOut, SessionErrors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ModuleEnvironment = E;
		Config.ExecutionBackend = EExecutionBackend::Vm;
		Session.ApplyConfig(Config);
		auto Loaded = Session.LoadSource("import \"sample/math.vs\";int main(){return answer();}",
		    NativePathText(Project / "session.vs"));
		if (!Loaded.bSuccess)
		{
			std::cerr << SessionErrors.str();
			return false;
		}
		return Session.Run().ExitCode == Expected;
	}
	void ModelTests()
	{
		for (const auto* Path : {"gui.vspkg", "gui.zip", "gui.VSPKG", "gui.ZIP", "dir/gui.VsPkG"})
		{
			Expect(Detail::IsPackageArchive(U8Path(Path)),
			    "distribution archive suffix: " + std::string(Path));
		}
		for (const auto* Path : {"gui.vslib", "gui.vsapp", "gui.vsbc", "gui.zip.tmp", "vspkg", "gui.vspkg/"})
		{
			Expect(!Detail::IsPackageArchive(U8Path(Path)), "unrelated suffix: " + std::string(Path));
		}
		FDiagnostic E;
		bool Good = false;
		std::string Out;
		for (const auto& P : std::vector<std::pair<std::string, bool>>{{"0.1.1", true},
		         {"0.1.1-alpha.2", true},
		         {"00.1.1", false},
		         {"0.1", false},
		         {"0.1.1+build", false},
		         {"0.1.1-alpha.01", false}})
		{
			Expect(VersionSatisfies(P.first, ">=0.0.0", Good, E) == P.second,
			    "SemVer validation: " + P.first);
		}
		Expect(VersionSatisfies("0.1.1", ">=0.1.0 <0.2.0", Good, E) && Good, "version interval intersection");
		Expect(VersionSatisfies("0.2.0", ">=0.1.0 <0.2.0", Good, E) && !Good, "exclusive upper boundary");
		Expect(Detail::CompareVersions("0.1.1-alpha.2", "0.1.1-alpha.11") < 0, "numeric prerelease sorting");
		Expect(ValidateDistribution(Manifest("test-package").dump(), Out, E),
		    "minimal data-only distribution");
		Expect(
		    !ValidateDistribution(
		        R"({"format":"veroscript-package","format":"veroscript-package","formatVersion":"0.1.1","name":"test-package","version":"0.1.1"})",
		        Out, E) &&
		        E.Code == "json-duplicate",
		    "duplicate JSON keys rejected");
		for (auto N : {"../escape", "/absolute", "C:drive", "CON", "bad--name", "x"})
		{
			Expect(!ValidateDistribution(Manifest(N).dump(), Out, E),
			    "invalid package name: " + std::string(N));
		}
		for (auto P : {"../outside.vs", "/absolute.vs", "a\\b.vs", "folder//a.vs", "name:stream.vs", "aux.vs",
		         "folder./a.vs"})
		{
			auto M = Manifest("test-package");
			M["entries"]["modules"]["test.vs"] = P;
			Expect(!ValidateDistribution(M.dump(), Out, E), "unsafe manifest path: " + std::string(P));
		}
		auto M = Manifest("test-package");
		M["unknown"] = true;
		Expect(!ValidateDistribution(M.dump(), Out, E) && E.Code == "unknown-field",
		    "unknown standard fields fail");
		M = Manifest("test-package");
		M["extensions"] = {{"com.example.test", {{"future", true}}}};
		Expect(ValidateDistribution(M.dump(), Out, E), "optional namespaced extension data retained");
		FLimits Limits;
		Limits.MaxJsonDepth = 2;
		M["extensions"]["com.example.test"]["future"] = {{"nested", J::object()}};
		Expect(!ValidateDistribution(M.dump(), Out, E, Limits), "JSON nesting budget checked");
		auto Large = Manifest("large-input");
		Large["extensions"]["test.values"] = {{"value", std::uint64_t(-1)}};
		Expect(ValidateDistribution(Large.dump(), Out, E), "uint64 maximum survives strict JSON parsing");
		auto TooLarge = Large.dump();
		auto Value = TooLarge.find("18446744073709551615");
		TooLarge.replace(Value, 20, "18446744073709551616");
		Expect(!ValidateDistribution(TooLarge, Out, E) && E.Code == "integer-range",
		    "integer overflow never silently becomes floating point");
	}
	void ExplicitRootSourceTests(const fs::path& Root)
	{
		auto Project = Root / "root-source-project", Vendor = Root / "root-source-vendor";
		auto Dependency = Manifest("source-dependency"), Consumer = Manifest("source-consumer");
		Consumer["dependencies"]["source-dependency"] = {{"version", "0.1.1"}};
		Write(Vendor / "dependency" / "veroscript-package.json", Dependency.dump());
		Write(Vendor / "consumer" / "veroscript-package.json", Consumer.dump());
		J Requirements = {{"format", "veroscript-project"},
		    {"formatVersion", "0.1.1"},
		    {"name", "root-source-test"},
		    {"version", "0.1.1"},
		    {"dependencies",
		        {{"source-dependency", {{"version", "0.1.1"}, {"directory", NativePathText(Vendor / "dependency")}}},
		            {"source-consumer", {{"version", "0.1.1"}, {"directory", NativePathText(Vendor / "consumer")}}}}}};
		Write(Project / "veroscript-project.json", Requirements.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "root-source-cache");
		Q.Options.Offline = true;
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E), "explicit root source resolves unqualified transitive dependency: " + E.Code);
		Q.Options.Locked = true;
		Expect(Manager.Execute(Q, R, E), "explicit root source supports locked offline reuse: " + E.Code);
		Q.Options.Locked = false;
		Consumer["dependencies"]["source-dependency"]["url"] = "https://example.invalid/dependency.vspkg";
		Consumer["dependencies"]["source-dependency"]["sha256"] = std::string(64, '0');
		Write(Vendor / "consumer" / "veroscript-package.json", Consumer.dump());
		const bool Conflicting = Manager.Execute(Q, R, E);
		Expect(!Conflicting && E.Code == "source-conflict",
		    "explicit transitive source conflict remains rejected: " + E.Code + " " + E.Message);
		Consumer["dependencies"]["source-dependency"].erase("url");
		Consumer["dependencies"]["source-dependency"].erase("sha256");
		Consumer["dependencies"]["source-dependency"]["version"] = "9.0.0";
		Write(Vendor / "consumer" / "veroscript-package.json", Consumer.dump());
		Expect(!Manager.Execute(Q, R, E), "root source cannot bypass transitive version constraints");
		Consumer["dependencies"]["source-dependency"]["version"] = "0.1.1";
		Consumer["dependencies"]["source-dependency"]["sha256"] = std::string(64, '0');
		Write(Vendor / "consumer" / "veroscript-package.json", Consumer.dump());
		Expect(!Manager.Execute(Q, R, E), "root source cannot bypass transitive digest constraints");
	}
	void LocalTests(const fs::path& Root)
	{
		auto Project = Root / U8Path("项目 with spaces"), Vendor = Root / U8Path("vendor/math"),
		     Cache = Root / "cache";
		fs::create_directories(Project);
		auto M = Manifest("test-math");
		M["requires"]["features"] = J::array({J{{"id", "veroscript.package-manager"}, {"version", "0.1.1"}}});
		M["entries"]["modules"]["sample/math.vs"] = "scripts/math.vs";
		Write(Vendor / "veroscript-package.json", M.dump(2));
		Write(Vendor / U8Path("scripts/math.vs"), "export int answer(){return 42;}");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Cache);
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		bool Success = Manager.Execute(Q, R, E);
		Expect(Success, "local installs: " + E.Code + " " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		auto Old = R.Environment;
		auto OldPath = Old->GetPackages()[0].Path;
		auto InitialLock = Read(Project / "veroscript.lock");
		Expect(R.Committed && Old->GetPackages().size() == 1, "install publishes complete generation");
		Expect(Run(Old, Project, 42), "installed exact module mapping executes");
		auto InitialRequirements = Read(Project / "veroscript-project.json");
		auto Configured = InitialRequirements;
		Configured["packages"]["test-math"]["configuration"]["option"] = "runtime-only";
		Write(Project / "veroscript-project.json", Configured.dump());
		std::shared_ptr<const FInstalledEnvironment> Reconfigured;
		Expect(Manager.Load(Q.Environment, Reconfigured, E) && Reconfigured &&
		        J::parse(Reconfigured->GetPackages().front().ConfigurationJson).at("option") ==
		            "runtime-only",
		    "client version requirement alone permits configuration reload without install processing");
		Write(Project / "veroscript-project.json", InitialRequirements.dump());
		Reconfigured.reset();
		{
			FCoreEnvironmentOptions Options;
			Options.EntryRoot = NativePathText(Project);
			std::shared_ptr<const FModuleEnvironment> Environment;
			Expect(Old->BuildCoreEnvironment(Options, Environment, E), "build governed host environment");
			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			FEngineConfig Config;
			VeroScript::Test::Configure(Config);
			Config.ModuleEnvironment = Environment;
			Config.I18nLimits.MaxOutputBytes = 3;
			Session.ApplyConfig(Config);
			auto Loaded =
			    Session.LoadSource("void main(){std::i18n::context "
			                       "c=std::i18n::context();c.resolve(std::i18n::text(\"oversized\"));}",
			        NativePathText(Project / "i18n-limit.vs"));
			Expect(Loaded.bSuccess && !Session.Run().bSuccess,
			    "host i18n output limit remains effective with package services: " + Errors.str());
		}
		auto StableFingerprint = Old->GetFingerprint();
		Q.Arguments.clear();
		bool Restored = Manager.Execute(Q, R, E);
		Expect(Restored && R.Environment && R.Environment->GetFingerprint() == StableFingerprint,
		    "unchanged restore preserves semantic fingerprint and loader locations");
		auto FreshLocal = Root / "fresh-local-project";
		fs::create_directories(FreshLocal);
		Write(FreshLocal / "veroscript-project.json", Detail::Read(Project / "veroscript-project.json", 1 << 20));
		Write(FreshLocal / "veroscript.lock", Detail::Read(Project / "veroscript.lock", 1 << 24));
		auto OriginalEnvironment = Q.Environment;
		Q.Environment.Root = NativePathText(FreshLocal);
		Q.Options.Locked = true;
		Q.Options.Offline = true;
		bool FreshLocalOk = Manager.Execute(Q, R, E);
		Expect(FreshLocalOk && R.Environment && Run(R.Environment, FreshLocal, 42),
		    "fresh directory lock restores without prior state: " + E.Code + " " + E.Message);
		Q.Environment = OriginalEnvironment;
		Q.Options.Locked = false;
		Q.Options.Offline = false;
		Q.Operation = EOperation::Disable;
		Q.Arguments = {"test-math"};
		Expect(Manager.Execute(Q, R, E), "disable installed root: " + E.Message);
		Expect(Read(Project / "veroscript.lock") == InitialLock, "ordinary disable preserves lock");
		Expect(R.Environment && !R.Environment->GetPackages()[0].Enabled, "disabled package not active");
		Q.Operation = EOperation::Enable;
		Expect(Manager.Execute(Q, R, E), "enable installed root offline: " + E.Message);
		Expect(R.Environment && Run(R.Environment, Project, 42), "enabled package usable");
		Q.Operation = EOperation::Install;
		Q.Arguments.clear();
		Q.Options.Locked = true;
		Q.Options.Offline = true;
		Write(Vendor / U8Path("scripts/math.vs"), "export int answer(){return 43;}");
		Expect(!Manager.Execute(Q, R, E) && E.Code == "locked-local-change",
		    "frozen restore rejects local change: " + E.Code);
		Expect(Read(Project / "veroscript.lock") == InitialLock && Run(Old, Project, 42),
		    "failed update preserves lock and old snapshot");
		Q.Options.Locked = false;
		Q.Options.Offline = false;
		Success = Manager.Execute(Q, R, E);
		Expect(Success, "restore advances changed local snapshot: " + E.Code + " " + E.Message);
		Expect(R.Environment && Run(R.Environment, Project, 43) && Run(Old, Project, 42),
		    "new/old immutable snapshots independent");
		auto Deployment = Root / "deployment";
		Q.Operation = EOperation::Deploy;
		Q.OutputPath = NativePathText(Deployment);
		Expect(Manager.Execute(Q, R, E), "deploy closure: " + E.Message);
		std::shared_ptr<const FInstalledEnvironment> Deployed;
		Expect(Manager.LoadDeployment(NativePathText(Deployment), Deployed, E),
		    "deployment loads offline: " + E.Message);
		Expect(Deployed && Run(Deployed, Project, 43), "copied deployment modules execute");
		Deployed.reset();
		Q.Operation = EOperation::Uninstall;
		Q.OutputPath.clear();
		Q.Arguments = {"test-math"};
		Q.Options.SkipHooks = true;
		Expect(Manager.Execute(Q, R, E), "skip-hooks uninstall uses installed graph: " + E.Message);
		Expect(R.Environment && R.Environment->GetPackages().empty(), "uninstall commits remaining graph");
		Expect(Run(Old, Project, 42), "old snapshot executes after uninstall");
		Q.Operation = EOperation::Clean;
		Q.Options.SkipHooks = false;
		Q.Arguments.clear();
		Expect(Manager.Execute(Q, R, E), "cleanup respects leases: " + E.Message);
		Expect(fs::exists(U8Path(OldPath)), "leased old artifact retained");
		Old.reset();
		R = {};
		// Simulate termination after immutable publication, before state/journal
		// publication. Only a complete receipt owned by this environment qualifies.
		auto Store = U8Path(OldPath).parent_path().parent_path();
		auto Orphan = Store / "orphan-copy" / U8Path(OldPath).filename();
		auto Foreign = Store / "foreign-copy" / U8Path(OldPath).filename();
		fs::create_directories(Orphan.parent_path());
		fs::create_directories(Foreign.parent_path());
		fs::copy(U8Path(OldPath), Orphan, fs::copy_options::recursive);
		fs::copy(U8Path(OldPath), Foreign, fs::copy_options::recursive);
		auto ForeignReceipt = Read(Foreign / ".vero-receipt.json");
		ForeignReceipt["environmentId"] = "another-owner";
		Write(Foreign / ".vero-receipt.json", ForeignReceipt.dump());
		Expect(Manager.Execute(Q, R, E), "cleanup after final release: " + E.Message);
		Expect(!fs::exists(U8Path(OldPath)), "artifact reclaimed after lease closes");
		Expect(!fs::exists(Orphan) && fs::exists(Foreign),
		    "cleanup reclaims owned orphan and preserves foreign directory");
		auto Bad = Root / "invalid-project";
		fs::create_directories(Bad);
		Write(Bad / "veroscript-project.json", "{broken");
		Q.Environment.Root = NativePathText(Bad);
		Q.Operation = EOperation::Install;
		Q.Arguments = {NativePathText(Vendor)};
		Expect(!Manager.Execute(Q, R, E) && !fs::exists(Bad / ".veroscript"),
		    "invalid project not overwritten or initialized");
		FEnvironmentInput Input;
		Input.Root = "relative";
		FEnvironmentLayout Layout;
		Expect(!ResolveEnvironment(Input, Layout, E) && E.Code == "path-base",
		    "no implicit cwd in public API");
		Input.Root = NativePathText(Project);
		Expect(ResolveEnvironment(Input, Layout, E), "absolute root needs no cwd");
	}
	class FRegistry final : public IHttpTransport
	{
	public:
		//~ Begin IHttpTransport Interface
		bool AllowsPublicMetadataCache() const noexcept override
		{
			return true;
		}

		bool Send(const FHttpRequest& Q, FHttpResponse& R, FDiagnostic& E) const override
		{
			Calls.push_back(Q.Method + " " + Q.Url);
			E = {};
			R = {};
			if (Q.CheckCancelled)
			{
				Q.CheckCancelled();
			}
			auto I = Data.find(Q.Url);
			if (I == Data.end())
			{
				R.Status = 404;
				R.Body = J{{"code", "not-found"},
				    {"message", "fixture resource not found"},
				    {"sourceId", "test.source"}}
				             .dump();
				return true;
			}
			R.Status = 200;
			R.Body = I->second;
			R.Headers["etag"] = "\"fixture\"";
			return true;
		}
		//~ End IHttpTransport Interface

		void Add(const fs::path& Root, J M, const std::string& Source = "export int unused(){return 1;}")
		{
			std::string N = M.at("name"), V = M.at("version"), Id = N + "-" + V;
			auto D = Root / Id;
			Write(D / "veroscript-package.json", M.dump());
			Write(D / "main.vs", Source);
			FClientOptions O;
			Detail::FWork W{O};
			auto Archive = Root / (Id + ".zip");
			Detail::Pack(D, Archive, M, W);
			auto Bytes = Detail::Read(Archive, 1 << 24);
			auto Target = M.value("target", J::object());
			J Artifact = {{"id", Id},
			    {"size", std::uint64_t(Bytes.size())},
			    {"sha256", Detail::Hash(Bytes)},
			    {"manifestSha256", Detail::Id(M)},
			    {"target", Target},
			    {"requires", M.value("requires", J::object())}};
			std::string Api = "https://fixture.invalid/api";
			auto P = Api + "/packages/" + N;
			J Summary = Data.count(P) ? J::parse(Data[P])
			                          : J{{"format", "veroscript-registry-package"},
			                                {"formatVersion", "0.1.1"},
			                                {"sourceId", "test.source"},
			                                {"name", N},
			                                {"versions", J::array()}};
			Summary["versions"].push_back({{"version", V}});
			Data[P] = Summary.dump();
			Data[P + "/versions/" + V] = J{
			    {"format", "veroscript-registry-version"},
			    {"formatVersion", "0.1.1"},
			    {"sourceId", "test.source"},
			    {"name", N},
			    {"version", V},
			    {"dependencies", M.value("dependencies", J::object())},
			    {"conflicts", M.value("conflicts", J::object())},
			    {"artifacts",
			        J::array({Artifact})}}.dump();
			Data[Api + "/artifacts/" + Id + "/download"] = Bytes;
		}

		mutable std::vector<std::string> Calls;

		std::map<std::string, std::string> Data;
	};
	void RequirementsTests(const fs::path& Root)
	{
		auto Area = Root / U8Path("requirements 路径 with spaces"), Lists = Area / "lists";
		auto Registry = std::make_shared<FRegistry>();
		for (const auto& Version : {"0.1.1", "0.2.0"})
		{
			auto M = Manifest("requirements-lib");
			M["version"] = Version;
			Registry->Add(Area / "registry", M);
		}
		Registry->Add(Area / "registry", Manifest("requirements-extra"));
		auto Project = Area / "app";
		auto Configuration = Detail::NewProject("requirements-app");
		Configuration["dependencies"]["requirements-lib"] = {{"version", "*"}};
		Configuration["packageManager"]["registries"]["official"] = {{"id", "test.source"},
		    {"api", "https://fixture.invalid/api"}};
		Write(Project / "veroscript-project.json", Configuration.dump());
		Write(
		    Lists / "main.txt",
		    "\xef\xbb\xbf# UTF-8 and CRLF\r\nrequirements-lib >= 0.1.1, \\\r\n < 0.2.0 # bounded\r\n-r \"nested/list with spaces.txt\"\r\n");
		Write(Lists / U8Path("nested/list with spaces.txt"),
		    "requirements-lib==0.1.1\nrequirements-extra@0.1.1\n");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.WorkingDirectory = NativePathText(Area);
		Q.Environment.CacheDirectory = NativePathText(Area / "cache");
		Q.Options.Transport = Registry;
		Q.Arguments = {"requirements-lib"};
		Q.RequirementFiles = {"lists/main.txt"};
		FPackageManager Manager;
		FResult Result;
		FDiagnostic Error;
		auto Installed = Manager.Execute(Q, Result, Error);
		Expect(Installed, "library requirements install: " + Error.Code + " " + Error.Message);
		if (!Installed)
		{
			return;
		}
		Result = {};
		auto Saved = Read(Project / "veroscript-project.json");
		Expect(Saved.at("dependencies").size() == 2 &&
		        Detail::Satisfies("0.1.1", Saved["dependencies"]["requirements-lib"]["version"]) &&
		        !Detail::Satisfies("0.2.0", Saved["dependencies"]["requirements-lib"]["version"]),
		    "duplicate requirements intersect instead of overwriting the earlier range");
		auto Lock = Read(Project / "veroscript.lock");
		auto Calls = Registry->Calls.size();
		Q.Options.Locked = Q.Options.Offline = true;
		Q.RequirementFiles.push_back("lists/main.txt");
		Expect(Manager.Execute(Q, Result, Error), "requirements locked offline repeat: " + Error.Code);
		Expect(Read(Project / "veroscript.lock") == Lock && Registry->Calls.size() == Calls,
		    "requirements repeat preserves the exact lock without network access");
		Result = {};
		Write(Lists / "main.txt", "requirements-lib>=0.2.0\nrequirements-lib<0.2.0\n");
		Q.Options.Locked = false;
		Expect(!Manager.Execute(Q, Result, Error), "contradictory duplicate constraints cannot install");
		Expect(Read(Project / "veroscript-project.json") == Saved && Read(Project / "veroscript.lock") == Lock,
		    "failed requirements resolution preserves the installed environment");

		auto Vendor = Area / U8Path("vendor @ source # 路径");
		Q.Arguments.clear();
		Write(Vendor / "veroscript-package.json", Manifest("requirements-local").dump());
		Write(Lists / "local.txt", "\"../vendor @ source # 路径\" # quoted path\n");
		Q.Environment.Root = NativePathText(Area / "local-app");
		fs::create_directories(Area / "local-app");
		Q.RequirementFiles = {"lists/local.txt"};
		Expect(Manager.Execute(Q, Result, Error),
		    "quoted relative directory with literal @ and #: " + Error.Message);
		Result = {};
		auto LocalProject = Read(Area / U8Path("local-app/veroscript-project.json"));
		Expect(LocalProject["dependencies"]["requirements-local"]["directory"] == "../vendor @ source # 路径",
		    "stored dependency path is rebased from requirements file to selected project");
		Write(Lists / "local.txt", "different-name @ \"../vendor @ source # 路径\"\n");
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "package-identity" &&
		        !Error.Source.empty(),
		    "named direct reference validates the actual manifest identity and locates its source");
		Write(Lists / "local.txt", "\"../vendor @ source # 路径\"\nrequirements-local==0.1.1\n");
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "requirements-conflict",
		    "different explicit sources for the same root are rejected");
		auto LocalManifest = Manifest("requirements-local");
		LocalManifest["localization"] = {{"root", "locale"}, {"baseCulture", "en-US"}};
		Write(Vendor / "veroscript-package.json", LocalManifest.dump());
		Write(Vendor / U8Path("locale/en-US/messages.json"),
		    J{{"install.before", "requirements-check"}}.dump());
		Write(Lists / "local.txt", "\"../vendor @ source # 路径\"\n");
		Q.Options.Message = [&](const std::string&)
		{
			Write(Lists / "local.txt", "# external edit\n");
		};
		auto LocalLock = Read(Area / U8Path("local-app/veroscript.lock"));
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "requirements-changed" &&
		        !Result.Committed,
		    "a callback editing a requirements input prevents the actual install commit");
		Expect(Read(Area / U8Path("local-app/veroscript-project.json")) == LocalProject &&
		        Read(Area / U8Path("local-app/veroscript.lock")) == LocalLock,
		    "concurrent text input edits preserve the previously installed project and lock");
		Q.Options.Message = {};

		Q.Environment.Root = NativePathText(Area / "invalid-app");
		const auto Invalid = [&](const std::string& Text, const std::string& Code)
		{
			Write(Lists / "invalid.txt", Text);
			Q.RequirementFiles = {"lists/invalid.txt"};
			Expect(!Manager.Execute(Q, Result, Error) && Error.Code == Code && !Error.Source.empty(),
			    "invalid requirements diagnostic: " + Code + ": " + Error.Code);
			Expect(!fs::exists(Area / U8Path("invalid-app/.veroscript")),
			    "invalid requirements cause no management writes");
		};
		Invalid("# comment\n--allow-hooks\n", "requirements-option");
		Invalid("-r invalid.txt\n", "requirements-cycle");
		Invalid("-r\n", "requirements-syntax");
		Invalid("requirements-lib>=0.1.1,\n", "requirements-syntax");
		Invalid("requirements-lib@\n", "requirements-syntax");
		Invalid("requirements-lib \\\n", "requirements-syntax");
		Invalid("\"unterminated path\n", "requirements-syntax");
		Invalid(std::string("bad\0encoding", 12), "requirements-encoding");
		Invalid("\xff\xfeinvalid", "requirements-encoding");
		Invalid("-r absent.txt\n", "file-missing");
		Write(Lists / "invalid.txt", "not-a-name!\n");
		Expect(!Manager.Execute(Q, Result, Error) && Error.Field.find("invalid.txt:1") != std::string::npos,
		    "syntax diagnostics contain the requirements filename and physical starting line");
		Write(Lists / "invalid.txt", "requirements-lib\n");
		Q.Options.Limits.MaxManifestBytes = 1;
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "file-limit", "requirements byte budget");
		Q.Options.Limits = {};
		Q.Options.Limits.MaxMetadataBytes = 1;
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "metadata-limit",
		    "requirements aggregate budget");
		Q.Options.Limits = {};
		Q.Options.Limits.MaxEdges = 0;
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "requirements-limit",
		    "requirements entry budget");
		Q.Options.Limits = {};
		Q.Options.Limits.MaxDependencyDepth = 1;
		Write(Lists / "invalid.txt", "-r local.txt\n");
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "requirements-limit",
		    "requirements include depth budget");
		Q.Options.Limits = {};
		Q.Options.Limits.MaxFiles = 1;
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "requirements-limit",
		    "requirements include count budget");
		Q.Options.Limits = {};
		Q.Operation = EOperation::List;
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "argument-conflict",
		    "library rejects requirements files for another operation");
		Q.Operation = EOperation::Install;
		Q.Options.CheckCancelled = []
		{
			Detail::Fail("test-cancelled", "cancelled");
		};
		Expect(!Manager.Execute(Q, Result, Error) && Error.Code == "test-cancelled",
		    "requirements cancellation");
		Q.Options.CheckCancelled = {};
		Write(Lists / "invalid.txt", "# original input\n");
		Detail::FWork Work{Q.Options};
		auto Inputs = Detail::ReadInstallInputs(Q, Work);
		Write(Lists / "invalid.txt", "# concurrent edit\n");
		try
		{
			Detail::VerifyInstallInputs(Inputs, Work);
			Expect(false, "concurrent requirements edits must invalidate the plan");
		}
		catch (const Detail::FFailure& Failure)
		{
			Expect(Failure.Diagnostic.Code == "requirements-changed",
			    "concurrent requirements edits are detected");
		}
		std::string LongRequirement = "requirements-lib>=0.1.1";
		for (unsigned I = 0; I < 4096; ++I)
		{
			LongRequirement += ", >=0.1.1";
		}
		Write(Lists / "invalid.txt", LongRequirement + "\n");
		unsigned CancellationChecks = 0;
		Q.Options.CheckCancelled = [&]()
		{
			if (++CancellationChecks == 32)
			{
				Detail::Fail("test-cancelled", "cancel while parsing one long version constraint");
			}
		};
		try
		{
			(void)Detail::ReadInstallInputs(Q, Work);
			Expect(false, "long single-line constraint parsing must observe cancellation");
		}
		catch (const Detail::FFailure& Failure)
		{
			Expect(Failure.Diagnostic.Code == "test-cancelled" &&
			        Failure.Diagnostic.Field.find("invalid.txt:1") != std::string::npos,
			    "long single-line constraint cancellation retains its input location");
		}
		Q.Options.CheckCancelled = {};
		auto Parsed = Detail::ReadInstallInputs(Q, Work);
		Expect(Parsed.Items.size() == 1 && Detail::Satisfies("0.1.1", Parsed.Items.front().Version),
		    "long comma-separated version constraints retain their semantics");
	}
	void RequirementsUrlTests(const fs::path& Root)
	{
		auto Area = Root / "requirements-url";
		auto Registry = std::make_shared<FRegistry>();
		Registry->Add(Area / "registry", Manifest("requirements-url"));
		const std::string Url = "https://fixture.invalid/api/artifacts/requirements-url-0.1.1/download";
		FPackageManager Manager;
		for (bool Named : {false, true})
		{
			auto Project = Area / (Named ? "named-app" : "plain-app");
			fs::create_directories(Project);
			auto File = Project / "requirements.txt";
			auto Entry = (Named ? "requirements-url @ " : "") + Url;
			Write(File, Entry + "\n");
			FRequest Request;
			Request.Environment.Root = NativePathText(Project);
			Request.Environment.CacheDirectory = NativePathText(Project / "cache");
			Request.Options.Transport = Registry;
			Request.RequirementFiles = {NativePathText(File)};
			FResult Result;
			FDiagnostic Error;
			auto Installed = Manager.Execute(Request, Result, Error);
			Expect(Installed, "URL requirements baseline install: " + Error.Code);
			if (!Installed)
			{
				continue;
			}
			Result = {};
			auto Configuration = Read(Project / "veroscript-project.json"), Lock = Read(Project / "veroscript.lock");
			auto Calls = Registry->Calls.size();
			Request.Options.Locked = Request.Options.Offline = true;
			Expect(Manager.Execute(Request, Result, Error),
			    "URL requirements locked offline repeat: " + Error.Code);
			Expect(Registry->Calls.size() == Calls && Read(Project / "veroscript.lock") == Lock,
			    "URL repeat addresses verified cache by the recorded digest and retains its lock");
			Result = {};
			Request.Options.Offline = false;
			Expect(Manager.Execute(Request, Result, Error) && Registry->Calls.size() == Calls,
			    "locked URL repeat also retains its recorded identity when online");
			Result = {};
			Request.Options.Offline = true;
			Write(File, Entry + "-changed\n");
			Expect(!Manager.Execute(Request, Result, Error) && Error.Code == "offline-missing" &&
			        Registry->Calls.size() == Calls,
			    "a different URL cannot reuse the previous URL's digest");
			Write(File, Entry + "\n");
			auto Digest = Configuration["dependencies"]["requirements-url"]["sha256"].get<std::string>();
			Write(Project / "cache" / "artifacts" / "direct" / (Digest + ".zip"), "corrupt cache");
			Expect(!Manager.Execute(Request, Result, Error) && Error.Code == "cache-corrupt" &&
			        Registry->Calls.size() == Calls,
			    "URL requirements never trust corrupt pinned cache content");
			Expect(Read(Project / "veroscript-project.json") == Configuration &&
			        Read(Project / "veroscript.lock") == Lock,
			    "failed URL cache checks leave installed requirements and lock unchanged");
		}
	}
	void RemoteTests(const fs::path& Root)
	{
		auto Registry = std::make_shared<FRegistry>();
		auto Packages = Root / "registry";
		for (auto V : {"1.0.0", "2.0.0"})
		{
			auto M = Manifest("common-lib");
			M["version"] = V;
			Registry->Add(Packages, M);
		}
		for (auto V : {"1.0.0", "2.0.0"})
		{
			auto M = Manifest("alpha-lib");
			M["version"] = V;
			M["dependencies"]["common-lib"] = {{"version", std::string(V) == "1.0.0" ? "<2.0.0" : ">=2.0.0"}};
			Registry->Add(Packages, M);
		}
		auto B = Manifest("beta-lib");
		B["version"] = "1.0.0";
		B["dependencies"]["common-lib"] = {{"version", "<2.0.0"}};
		Registry->Add(Packages, B);
		auto M = Manifest("fallback-lib");
		M["version"] = "1.0.0";
		Registry->Add(Packages, M);
		M["version"] = "2.0.0";
		M["requires"]["core"] = ">=99.0.0";
		Registry->Add(Packages, M);
		auto Project = Root / "remote-project";
		fs::create_directories(Project);
		auto P = Detail::NewProject("remote-project");
		P["packageManager"]["registries"]["official"] = {{"id", "test.source"},
		    {"api", "https://fixture.invalid/api"}};
		Write(Project / "veroscript-project.json", P.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "remote-cache");
		Q.Options.Transport = Registry;
		Q.Arguments = {"alpha-lib", "beta-lib", "fallback-lib"};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		auto Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "registry backtracking install: " + E.Code + " " + E.Message + " [" + E.Field + "]");
		if (!Ok || !R.Environment)
		{
			for (const auto& W : R.Warnings)
			{
				std::cerr << "WARNING " << W.Code << ": " << W.Message << " " << W.Field << '\n';
			}
			Expect(!Ok || bool(R.Environment), "successful mutation provides readable environment");
			return;
		}
		std::map<std::string, std::string> Selected;
		for (const auto& N : R.Environment->GetPackages())
		{
			Selected[N.Name] = N.Version;
		}
		Expect(Selected["alpha-lib"] == "1.0.0" && Selected["common-lib"] == "1.0.0" &&
		        Selected["fallback-lib"] == "1.0.0",
		    "backtracking and incompatible-version fallback");
		const auto BudgetProject = Detail::Read(Project / "veroscript-project.json", 1 << 20);
		const auto BudgetLock = Detail::Read(Project / "veroscript.lock", 1 << 24);
		for (unsigned Limit = 0; Limit != 3; ++Limit)
		{
			auto Restricted = Q.Options;
			if (Limit == 0)
			{
				Restricted.Limits.MaxPackages = 1;
			}
			else if (Limit == 1)
			{
				Restricted.Limits.MaxEdges = 0;
			}
			else
			{
				Restricted.Limits.MaxDependencyDepth = 1;
			}
			const std::string Expected = Limit == 0 ? "graph-limit" : Limit == 1 ? "edge-limit"
			                                                                     : "dependency-depth";
			std::shared_ptr<const FInstalledEnvironment> Limited;
			Expect(!Manager.Load(Q.Environment, Limited, E, Restricted) && E.Code == Expected && !Limited,
			    "installed snapshots enforce graph budget: " + Expected + ": " + E.Code);
			auto Replay = Q;
			Replay.Arguments.clear();
			Replay.Options = Restricted;
			Replay.Options.Locked = true;
			FResult LimitedResult;
			Expect(!Manager.Execute(Replay, LimitedResult, E) && E.Code == Expected && !LimitedResult.Committed,
			    "locked replay enforces graph budget: " + Expected + ": " + E.Code);
		}
		Expect(Detail::Read(Project / "veroscript-project.json", 1 << 20) == BudgetProject &&
		        Detail::Read(Project / "veroscript.lock", 1 << 24) == BudgetLock,
		    "rejected graph budgets preserve the installed project and lock");
		auto OriginalInput = Q.Environment;
		auto Fresh = Root / "fresh-locked-project";
		fs::create_directories(Fresh);
		Write(Fresh / "veroscript-project.json", Detail::Read(Project / "veroscript-project.json", 1 << 20));
		Write(Fresh / "veroscript.lock", Detail::Read(Project / "veroscript.lock", 1 << 24));
		Q.Environment.Root = NativePathText(Fresh);
		Q.Arguments.clear();
		Q.Options.Locked = true;
		Q.Options.Offline = true;
		auto CallsBefore = Registry->Calls.size();
		bool FreshOk = Manager.Execute(Q, R, E);
		Expect(FreshOk && R.Environment, "fresh checkout restores exact registry "
		                                 "lock without installation state: " +
		        E.Code + " " + E.Message);
		Expect(Registry->Calls.size() == CallsBefore,
		    "fresh locked offline restoration uses only pinned cache");
		Q.Environment = OriginalInput;
		Q.Arguments = {"alpha-lib", "beta-lib", "fallback-lib"};
		Q.Options.Locked = false;
		Q.Options.Offline = false;
		auto Calls = Registry->Calls.size();
		auto Offline = Root / "offline-project";
		fs::create_directories(Offline);
		Write(Offline / "veroscript-project.json", P.dump());
		Q.Environment.Root = NativePathText(Offline);
		Q.Options.Offline = true;
		Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "first offline install from verified complete cache: " + E.Code + " " + E.Message);
		Expect(Registry->Calls.size() == Calls, "offline never sends a request");
		auto Lock = Read(Offline / "veroscript.lock");
		Q.Options.Locked = true;
		Q.Arguments.clear();
		Expect(Manager.Execute(Q, R, E), "locked offline restore: " + E.Message);
		Expect(Read(Offline / "veroscript.lock") == Lock, "locked restore preserves exact identities");
		auto Cy = Manifest("cycle-one");
		Cy["dependencies"]["cycle-two"] = {{"version", "0.1.1"}};
		Registry->Add(Packages, Cy);
		Cy = Manifest("cycle-two");
		Cy["dependencies"]["cycle-one"] = {{"version", "0.1.1"}};
		Registry->Add(Packages, Cy);
		Q.Environment.Root = NativePathText(Project);
		Q.Arguments = {"cycle-one"};
		Q.Options.Locked = false;
		Q.Options.Offline = false;
		auto Previous = Read(Project / "veroscript.lock");
		const auto BeforeCycle = Registry->Calls.size();
		Expect(!Manager.Execute(Q, R, E) && E.Code == "dependency-cycle",
		    "cycle reports dependency chain: " + E.Code);
		Expect(std::none_of(Registry->Calls.begin() + BeforeCycle, Registry->Calls.end(), [](const auto& Call)
		{
			return Call.find("/download") != std::string::npos;
		}),
		    "cyclic metadata graphs are rejected before artifact downloads");
		Expect(Read(Project / "veroscript.lock") == Previous,
		    "solver failure leaves previous generation intact");
		// Visit leaves before their parents to exercise cached subtree heights.
		auto DepthLeaf = Manifest("aaa-depth-leaf");
		Registry->Add(Packages, DepthLeaf);
		auto DepthMiddle = Manifest("mmm-depth-middle");
		DepthMiddle["dependencies"]["aaa-depth-leaf"] = {{"version", "0.1.1"}};
		Registry->Add(Packages, DepthMiddle);
		auto DepthRoot = Manifest("zzz-depth-root");
		DepthRoot["dependencies"]["mmm-depth-middle"] = {{"version", "0.1.1"}};
		Registry->Add(Packages, DepthRoot);
		auto DepthProject = Detail::NewProject("depth-project");
		DepthProject["packageManager"] = P["packageManager"];
		DepthProject["dependencies"]["zzz-depth-root"] = {{"version", "0.1.1"}};
		auto DepthRequest = Q;
		DepthRequest.Environment.Root = NativePathText(Root / "depth-project");
		DepthRequest.Arguments.clear();
		DepthRequest.Options.Limits.MaxDependencyDepth = 2;
		Write(Root / "depth-project" / "veroscript-project.json", DepthProject.dump());
		const auto BeforeDepth = Registry->Calls.size();
		Expect(!Manager.Execute(DepthRequest, R, E) && E.Code == "dependency-depth",
		    "solver checks the longest dependency path regardless of lexical order: " + E.Code);
		Expect(std::none_of(Registry->Calls.begin() + BeforeDepth, Registry->Calls.end(), [](const auto& Call)
		{
			return Call.find("/download") != std::string::npos;
		}),
		    "excessive dependency depth is rejected before artifact downloads");
		DepthRequest.Options.Limits.MaxDependencyDepth = 3;
		const auto DepthInstalled = Manager.Execute(DepthRequest, R, E);
		Expect(DepthInstalled, "exact dependency depth budget permits the graph: " + E.Code + " " + E.Message);
		if (DepthInstalled)
		{
			DepthRequest.Options.Limits.MaxDependencyDepth = 2;
			std::shared_ptr<const FInstalledEnvironment> Limited;
			Expect(!Manager.Load(DepthRequest.Environment, Limited, E, DepthRequest.Options) && E.Code == "dependency-depth",
			    "locked graphs also check cached subtree heights: " + E.Code);
		}
		auto WildcardProject = Root / "wildcard-project";
		fs::create_directories(WildcardProject);
		auto Wildcard = Detail::NewProject("wildcard-project");
		Wildcard["packageManager"] = P["packageManager"];
		Wildcard["dependencies"] = {{"beta-lib", {{"version", "1.0.0"}}}, {"common-lib", {{"version", "*"}}}};
		Write(WildcardProject / "veroscript-project.json", Wildcard.dump());
		Q.Environment.Root = NativePathText(WildcardProject);
		Q.Arguments.clear();
		auto WildcardOk = Manager.Execute(Q, R, E);
		Expect(WildcardOk && R.Environment &&
		        std::any_of(R.Environment->GetPackages().begin(), R.Environment->GetPackages().end(),
		            [](const auto& N)
		{
			return N.Name == "common-lib" && N.Version == "1.0.0";
		}),
		    "wildcard and transitive version interval intersect correctly: " + E.Code + " " + E.Message);
	}
	void MirrorTrustTests(const fs::path& Root)
	{
		auto Registry = std::make_shared<FRegistry>();
		Registry->Add(Root / "mirror-trust-registry", Manifest("mirror-trust"));
		const std::string Origin = "https://fixture.invalid/api",
		                  Mirror = "https://private-mirror.invalid/api";
		auto OriginalData = Registry->Data;
		for (const auto& V : OriginalData)
		{
			Registry->Data[Mirror + V.first.substr(Origin.size())] = V.second;
		}
		auto Project = Root / "mirror-trust-project";
		fs::create_directories(Project);
		auto P = Detail::NewProject("mirror-trust-project");
		P["packageManager"]["registries"]["official"] = {{"id", "official.source"},
		    {"api", "https://official.invalid/api"}};
		P["packageManager"]["registries"]["private"] = {
		    {"id", "test.source"}, {"api", Origin}, {"mirror", Mirror}};
		P["dependencies"]["mirror-trust"] = {{"version", "0.1.1"}, {"source", "private"}};
		Write(Project / "veroscript-project.json", P.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "mirror-trust-cache");
		Q.Options.Transport = Registry;
		Q.Options.Mirror = "https://official-mirror.invalid/api";
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		auto Before = Registry->Calls.size();
		auto Ok = Manager.Execute(Q, R, E);
		Expect(!Ok && E.Code == "untrusted-mirror" && Registry->Calls.size() == Before &&
		        !fs::exists(Project / "veroscript.lock"),
		    "explicit official mirror cannot authorize an untrusted private mirror: " + E.Code);
		P["packageManager"]["registries"]["private"]["trustedMirror"] = true;
		Write(Project / "veroscript-project.json", P.dump());
		Expect(Manager.Execute(Q, R, E) && R.Environment,
		    "private mirror with its own trust authorization remains usable: " + E.Message);
	}
	void ConditionalRootTests(const fs::path& Root)
	{
		auto Registry = std::make_shared<FRegistry>();
		auto Shared = Manifest("conditional-shared");
		Shared["entries"]["modules"]["conditional/shared.vs"] = "main.vs";
		Registry->Add(Root / "conditional-registry", Shared, "export int shared_value(){return 42;}");
		auto Tool = Manifest("conditional-tool");
		Tool["entries"]["modules"]["conditional/tool.vs"] = "main.vs";
		Tool["dependencies"]["conditional-shared"] = {{"version", "0.1.1"}};
		Registry->Add(Root / "conditional-registry", Tool,
		    "import \"conditional/shared.vs\";export int tool_value(){return shared_value();}");
		for (const auto& Mode : {"platform", "optional", "development", "compile-root"})
		{
			bool CompileRoot = std::string(Mode) == "compile-root";
			auto Project = Root / (std::string("conditional-") + Mode);
			fs::create_directories(Project);
			auto P = Detail::NewProject("conditional-project");
			P["packageManager"]["registries"]["official"] = {{"id", "test.source"},
			    {"api", "https://fixture.invalid/api"}};
			P["dependencies"]["conditional-tool"] = {{"version", "0.1.1"}, {"usage", J::array({"compile"})}};
			auto& Spec = P["dependencies"]["conditional-shared"];
			Spec = {{"version", "0.1.1"}};
			if (std::string(Mode) == "platform")
			{
				Spec["when"] = {{"os", "not-this-platform"}};
			}
			else if (std::string(Mode) == "optional")
			{
				Spec["optional"] = true;
			}
			else
			{
				Spec["usage"] = J::array({CompileRoot ? "compile" : "development"});
			}
			Write(Project / "veroscript-project.json", P.dump());
			FRequest Q;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Root / "conditional-cache");
			Q.Options.Transport = Registry;
			FPackageManager Manager;
			FResult R;
			FDiagnostic E;
			Expect(Manager.Execute(Q, R, E) && R.Environment,
			    "conditional root baseline: " + std::string(Mode) + " " + E.Code + " " + E.Message);
			if (!R.Environment)
			{
				continue;
			}
			const auto& Packages = R.Environment->GetPackages();
			auto SharedPackage = std::find_if(Packages.begin(), Packages.end(),
			    [](const auto& N)
			{
				return N.Name == "conditional-shared";
			});
			Expect(SharedPackage != Packages.end() &&
			        SharedPackage->Usage == std::vector<std::string>{"compile"},
			    "inactive direct root cannot add runtime/development usage to a transitive package");
			FCoreEnvironmentOptions Options;
			Options.EntryRoot = NativePathText(Project);
			std::shared_ptr<const FModuleEnvironment> Environment;
			Expect(R.Environment->BuildCoreEnvironment(Options, Environment, E),
			    "conditional environment builds: " + E.Message);
			if (Environment)
			{
				FCompileOptions C;
				VeroScript::Test::Configure(C);
				C.ModuleEnvironment = Environment;
				auto Legal = VeroScriptCompile::CompileSource(
				    "import \"conditional/tool.vs\";int main(){return tool_value();}",
				    NativePathText(Project / "legal.vs"), C);
				auto Hidden = VeroScriptCompile::CompileSource(
				    "import \"conditional/shared.vs\";int main(){return shared_value();}",
				    NativePathText(Project / "hidden.vs"), C);
				Expect(Legal.bSuccess && Hidden.bSuccess == CompileRoot,
				    "direct visibility follows root eligibility while transitive access remains valid");
			}
			Q.Operation = EOperation::Deploy;
			Q.OutputPath = NativePathText(Project / "compile-deployment");
			Expect(Manager.Execute(Q, R, E) && J::parse(R.Json).at("packages").empty(),
			    "compile-only closure is excluded from runtime deployment: " + E.Message);
			if (R.Environment)
			{
				std::string Snapshot;
				const auto Extended = Project / "source-deployment";
				Expect(R.Environment->Deploy(NativePathText(Extended), {"runtime", "compile"}, {"conditional-tool"}, Snapshot, E),
				    "source deployment exports the compile closure: " + E.Message);
				std::shared_ptr<const FInstalledEnvironment> Loaded;
				Expect(Manager.LoadDeployment(NativePathText(Extended), Loaded, E) && Loaded && Loaded->GetPackages().size() == 2,
				    "load source deployment retains both packages: " + E.Message);
				if (Loaded)
				{
					for (const auto& Package : Loaded->GetPackages())
					{
						Expect(Package.Usage == std::vector<std::string>{"compile"}, "deployment retains actual compile usage");
					}
					Expect(!R.Environment->Deploy(NativePathText(Project / "bad-usage-deployment"), {"compile"}, {}, Snapshot, E),
					    "compile deployment must explicitly include runtime usage");
				}
			}
			P["dependencies"]["conditional-tool"]["usage"] = J::array({"runtime"});
			Write(Project / "veroscript-project.json", P.dump());
			Q.Operation = EOperation::Install;
			Expect(Manager.Execute(Q, R, E), "runtime conditional root reinstall: " + E.Message);
			Options.RuntimeOnly = true;
			Environment.reset();
			bool RuntimeBuilt = R.Environment && R.Environment->BuildCoreEnvironment(Options, Environment, E);
			Expect(RuntimeBuilt, "runtime environment retains required transitive packages: " + E.Message);
			if (Environment)
			{
				FCompileOptions C;
				VeroScript::Test::Configure(C);
				C.ModuleEnvironment = Environment;
				auto Hidden = VeroScriptCompile::CompileSource(
				    "import \"conditional/shared.vs\";int main(){return shared_value();}",
				    NativePathText(Project / "runtime-hidden.vs"), C);
				Expect(!Hidden.bSuccess, "runtime domain excludes inactive and compile-only direct roots");
			}
			Q.Operation = EOperation::Deploy;
			Q.OutputPath = NativePathText(Project / "runtime-deployment");
			Expect(Manager.Execute(Q, R, E) &&
			        J::parse(R.Json).at("roots") == J::array({"conditional-tool"}) &&
			        J::parse(R.Json).at("packages").size() == 2,
			    "deployment preserves transitive closure without promoting an inactive root");
			Q.Operation = EOperation::Disable;
			Q.Arguments = {"conditional-tool"};
			const auto ExpectedActivation = [CompileRoot](const auto& N)
			{
				return N.Enabled == (CompileRoot && N.Name == "conditional-shared");
			};
			Expect(Manager.Execute(Q, R, E) && R.Environment &&
			        std::all_of(R.Environment->GetPackages().begin(), R.Environment->GetPackages().end(),
			            ExpectedActivation),
			    "only an eligible direct root stays active after its parent is disabled: " + E.Message);
			std::shared_ptr<const FInstalledEnvironment> Reloaded;
			Expect(Manager.Load(Q.Environment, Reloaded, E, Q.Options) && Reloaded &&
			        std::all_of(Reloaded->GetPackages().begin(), Reloaded->GetPackages().end(),
			            ExpectedActivation),
			    "reloading lock preserves conditional root activation");
		}
	}
	void HookTests(const fs::path& Root)
	{
		auto Project = Root / "hook-project", Vendor = Root / "hook-package";
		fs::create_directories(Project);
		auto M = Manifest("hook-package");
		M["resources"]["data"] = "data";
		M["install"]["inputs"]["x"] = {{"type", "integer"},
		    {"default", std::uint64_t(18446744073709551615ull)}};
		M["install"]["actions"] =
		    J::array({J{{"type", "template"}, {"source", "template.txt"}, {"destination", "value.txt"}}});
		M["install"]["hooks"] =
		    J::array({J{{"phase", "post-install"},
		        {"path", "hook.vs"},
		        {"entry", "install"},
		        {"permissions", J::array({"resources.read", "generated.write"})}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / U8Path("data/source.txt"), "owned resource");
		Write(Vendor / "template.txt", "{x}");
		Write(Vendor / "hook.vs", std::string(VeroScript::Test::LambdaStaticDefinitions) + "export void install(std::package::install_context "
		                                                                                   "c){checkLambdaStaticSupport();auto save=[c]()->void{"
		                                                                                   "c.write_text(\"hook.txt\",c.read_text(\"data\",\"source.txt\"));};save();}");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "hook-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hooks-disabled", "hooks require explicit consent");
		Expect(!fs::exists(Project / "veroscript-project.json"), "denied hook creates no requirements");
		Q.Options.AllowHooks = true;
		bool Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "bounded typed installation hook: " + E.Code + " " + E.Message);
		if (!Ok || !R.Environment)
		{
			for (const auto& W : R.Warnings)
			{
				std::cerr << "WARNING " << W.Code << ": " << W.Message << " " << W.Field << '\n';
			}
			Expect(!Ok || bool(R.Environment), "successful mutation provides readable environment");
			return;
		}
		auto P = R.Environment->GetPackages().front();
		Expect(Detail::Read(U8Path(P.GeneratedPath) / "hook.txt", 100) == "owned resource",
		    "hook reads declared resources and writes generated output");
		Expect(Detail::Read(U8Path(P.GeneratedPath) / "value.txt", 100) == "18446744073709551615",
		    "template retains uint64 precision");
		auto Before = Read(Project / "veroscript.lock");
		Write(Vendor / "hook.vs", "export void install(std::package::install_context c){while(true){}}");
		Q.Arguments.clear();
		Q.Options.Limits.HookInstructions = 1000;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed",
		    "unbounded hook stops at instruction limit: " + E.Message);
		Expect(Read(Project / "veroscript.lock") == Before, "failed hook does not publish a generation");
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"hook-package"};
		Q.Options.SkipHooks = true;
		Q.Options.AllowHooks = false;
		Expect(Manager.Execute(Q, R, E), "recovery uninstall skips package code: " + E.Message);
	}
	void NumericHookTests(const fs::path& Root)
	{
		auto Project = Root / "numeric-hook-project", Vendor = Root / "numeric-hook-package";
		fs::create_directories(Project);
		auto M = Manifest("numeric-hook-package");
		M["install"]["hooks"] = J::array();
		// Hook VMs must retain ordinary construction semantics under their budgets.
		std::string Source = "int constructed=0;int initial(){constructed+=1;return 7;}"
		                     "struct HookItem{int value=initial();};";
		for (const auto& Phase : {"pre-install", "post-install", "pre-uninstall", "post-uninstall"})
		{
			const auto Entry = "hook" + std::to_string(M["install"]["hooks"].size());
			M["install"]["hooks"].push_back({{"phase", Phase}, {"path", "hook.vs"}, {"entry", Entry},
			    {"permissions", J::array({"generated.write"})}});
			Source += "export void " + Entry + "(std::package::install_context c){"
			                                   "constructed=0;HookItem items[2];assert(constructed==2 && items[0].value==7);"
			                                   "std::bigint a=std::bigint(\"123456789012345678901234567890\");"
			                                   "assert((a+std::bigint(1)).to_string()==\"123456789012345678901234567891\");"
			                                   "assert(std::rational(1,3)+std::rational(1,6)==std::rational(1,2));";
			if (std::string(Phase).rfind("post-", 0) == 0)
			{
				Source += "assert(!std::bigint(\"1234567890123456789012345678901234567890\").is_zero());";
			}
			if (std::string(Phase).find("uninstall") == std::string::npos)
			{
				Source += "c.write_text(\"" + Entry + ".txt\",a.to_string());";
			}
			Source += "}";
		}
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / "hook.vs", Source);
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "numeric-hook-cache");
		Q.Arguments = {NativePathText(Vendor)};
		Q.Options.AllowHooks = true;
		Q.Options.Limits.HookNumericLimits.MaxValueBits = 8;
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed", "numeric install budget stops execution: " + E.Message);
		Expect(!fs::exists(Project / "veroscript.lock"), "failed numeric install publishes no lockfile");
		Q.Options.Limits.HookNumericLimits.MaxValueBits = 100;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed", "numeric post-install budget stops execution");
		Expect(!fs::exists(Project / "veroscript.lock"), "failed numeric post-install publishes no lockfile");
		const auto CheckLimit = [&](const FNumericLimits& Limits, const char* Label)
		{
			Q.Options.Limits.HookNumericLimits = Limits;
			Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed", Label);
			Expect(!fs::exists(Project / "veroscript.lock"), "failed numeric hook publishes no lockfile");
		};
		FNumericLimits Limits;
		Limits.MaxInputBytes = 8;
		CheckLimit(Limits, "numeric hook input byte limit");
		Limits = {};
		Limits.MaxOutputBytes = 8;
		CheckLimit(Limits, "numeric hook output byte limit");
		Limits = {};
		Limits.MaxScratchBytes = 1;
		CheckLimit(Limits, "numeric hook scratch byte limit");
		Limits = {};
		Limits.MaxWorkUnits = 1;
		CheckLimit(Limits, "numeric hook work unit limit");
		Q.Options.Limits.HookNumericLimits = {};
		Expect(Manager.Execute(Q, R, E), "numeric pre/post install hooks: " + E.Message);
		if (!R.Environment || R.Environment->GetPackages().empty()) return;
		auto P = R.Environment->GetPackages().front();
		Expect(Detail::Read(U8Path(P.GeneratedPath) / "hook0.txt", 100) == "123456789012345678901234567890" && Detail::Read(U8Path(P.GeneratedPath) / "hook1.txt", 100) == "123456789012345678901234567890", "both numeric install phases execute");
		auto Before = Read(Project / "veroscript.lock");
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"numeric-hook-package"};
		Q.Options.Limits.HookNumericLimits.MaxValueBits = 8;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed", "numeric uninstall budget stops execution");
		Expect(Read(Project / "veroscript.lock") == Before, "failed numeric uninstall keeps published generation");
		Q.Options.Limits.HookNumericLimits.MaxValueBits = 100;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-failed", "numeric post-uninstall budget stops execution");
		Expect(Read(Project / "veroscript.lock") == Before, "failed numeric post-uninstall keeps published generation");
		Q.Options.Limits.HookNumericLimits = {};
		Expect(Manager.Execute(Q, R, E), "numeric pre/post uninstall hooks: " + E.Message);
	}
	void ConstantHookTests(const fs::path& Root)
	{
		auto Project = Root / "constant-hook-project", Vendor = Root / "constant-hook-package";
		fs::create_directories(Project);
		auto Library = VeroScriptCompile::CompileSource(
		    "export constexpr uint64 maximum(){return 18446744073709551615u64;}"
		    "export template<int N> int choose(){if constexpr(N>0)return N+1;else return missing();}",
		    "constant-hook-library.vs");
		Expect(Library.bSuccess, "compile constant hook library: " + Library.Error.Message);
		if (!Library.bSuccess) return;
		FPackageBuildOptions Build;
		Build.Kind = EPackageKind::Library;
		Build.PackageName = "constant-hook-library";
		std::vector<std::uint8_t> Bytes;
		std::string Failure;
		const auto Built = BuildPackage(*Library.Module, Build, Bytes, Failure);
		Expect(Built, "package constant hook library: " + Failure);
		if (!Built) return;
		Write(Vendor / U8Path("scripts/constants.vslib"), std::string(Bytes.begin(), Bytes.end()));
		Write(Vendor / U8Path("scripts/sum.vs"),
		    "export constexpr int sum(int n){int value=0;for(int i=0;i<n;++i)value+=i;return value;}"
		    "export constexpr int depth(int n){if(n==0)return 0;return depth(n-1)+1;}");
		auto M = Manifest("constant-hook-package");
		M["entries"]["modules"] = {{"hook/constants.vslib", "scripts/constants.vslib"},
		    {"hook/sum.vs", "scripts/sum.vs"}};
		M["install"]["hooks"] = J::array();
		std::string Source = "import \"hook/constants.vslib\";import \"hook/sum.vs\";"
		                     "constexpr int count=sum(10);constexpr int recursive=depth(6);"
		                     "constexpr uint64 bound=maximum();";
		for (const auto* Phase : {"pre-install", "post-install", "pre-uninstall", "post-uninstall"})
		{
			const auto Entry = "hook" + std::to_string(M["install"]["hooks"].size());
			M["install"]["hooks"].push_back({{"phase", Phase}, {"path", "hook.vs"}, {"entry", Entry},
			    {"permissions", J::array({"generated.write"})}});
			Source += "export void " + Entry + "(std::package::install_context c){"
			                                   "if constexpr(count==45){int values[count];values[44]=choose<4>();"
			                                   "assert(values[44]==5 && recursive==6 && bound==18446744073709551615u64);";
			if (std::string(Phase).find("uninstall") == std::string::npos)
			{
				Source += "c.write_text(\"" + Entry + ".txt\",\"constant hook\");";
			}
			Source += "}else{discarded_branch_must_not_resolve();}}";
		}
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / "hook.vs", Source);
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "constant-hook-cache");
		Q.Arguments = {NativePathText(Vendor)};
		Q.Options.AllowHooks = true;
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		const auto CheckLimit = [&](const FConstantEvaluationLimits& Limits, const char* Label,
		                            const char* Code = "hook-compile")
		{
			Q.Options.Limits.HookConstantEvaluationLimits = Limits;
			Expect(!Manager.Execute(Q, R, E) && E.Code == Code, std::string(Label) + ": " + E.Code + " " + E.Message);
			Expect(!fs::exists(Project / "veroscript.lock") && !fs::exists(Project / "veroscript-project.json"),
			    "failed constant hook preflight publishes no project or lockfile");
		};
		FConstantEvaluationLimits Limits;
		Limits.MaxWork = 8;
		CheckLimit(Limits, "constant hook work budget");
		Limits = {};
		Limits.MaxCallDepth = 2;
		CheckLimit(Limits, "constant hook call-depth budget");
		Limits = {};
		Limits.MaxExpressionDepth = 1;
		CheckLimit(Limits, "constant hook expression-depth budget");
		Limits = {};
		Limits.MaxMemoryBytes = 256;
		CheckLimit(Limits, "constant hook memory budget");
		Limits = {};
		Limits.MaxWork = 0;
		CheckLimit(Limits, "invalid constant hook policy", "hook-limits");
		Q.Options.Limits.HookConstantEvaluationLimits = {};
		Write(Vendor / "hook.vs", "import \"hook/sum.vs\";constexpr int slow=sum(4000);"
		                          "export void hook0(std::package::install_context c){assert(slow==7998000);}");
		bool Parsed = false;
		unsigned Checks = 0;
		Q.Options.CheckCancelled = [&]()
		{
			if (Parsed && ++Checks == 256)
			{
				Detail::Fail("test-hook-compile-cancelled", "cancel once during hook constant evaluation");
			}
		};
		const auto* Previous = ActiveAnalysisControl;
		{
			FAnalysisControlScope Control([]
			{
				return false;
			}, [&](std::string_view Stage, double)
			{
				if (Stage == "parse")
				{
					Parsed = true;
				}
			});
			Expect(!Manager.Execute(Q, R, E) && E.Code == "test-hook-compile-cancelled",
			    "hook constant evaluation preserves a one-shot cancellation: " + E.Code + " " + E.Message);
			Expect(Parsed && ActiveAnalysisControl == &Control, "hook compilation preserves the host analysis scope");
		}
		Expect(ActiveAnalysisControl == Previous, "hook cancellation restores the previous analysis scope");
		Expect(!fs::exists(Project / "veroscript.lock"), "cancelled hook compilation publishes no generation");
		Q.Options.CheckCancelled = {};
		Write(Vendor / "hook.vs", Source);
		Q.Options.Limits.HookConstantEvaluationLimits.MaxCacheBytes = 0;
		Expect(Manager.Execute(Q, R, E), "uncached constant hooks import source and libraries: " + E.Code + " " + E.Message);
		if (!R.Environment || R.Environment->GetPackages().empty()) return;
		const auto P = R.Environment->GetPackages().front();
		for (const auto* File : {"hook0.txt", "hook1.txt"})
		{
			Expect(Detail::Read(U8Path(P.GeneratedPath) / File, 100) == "constant hook", "both constant install phases execute");
		}
		const auto Before = Read(Project / "veroscript.lock");
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"constant-hook-package"};
		Q.Options.Limits.HookConstantEvaluationLimits.MaxWork = 8;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "hook-compile", "uninstall uses the same constant evaluation limits");
		Expect(Read(Project / "veroscript.lock") == Before, "failed constant uninstall preserves the published generation");
		Q.Options.Limits.HookConstantEvaluationLimits = {};
		Expect(Manager.Execute(Q, R, E), "constant pre/post uninstall hooks: " + E.Code + " " + E.Message);
	}
	void DiscoveryTests(const fs::path& Root)
	{
		auto Parent = Root / "discovery-parent", Boundary = Parent / "workspace";
		fs::create_directories(Boundary / "nested");
		auto Project = J{{"format", "veroscript-project"},
		    {"formatVersion", "0.1.1"},
		    {"name", "discovery-test"},
		    {"version", "0.1.1"}};
		Write(Parent / "veroscript-project.json", Project.dump());
		std::string Found;
		FDiagnostic Error;
		auto Entry = NativePathText(Boundary / "nested" / "main.vs");
		Expect(DiscoverProject(Entry, NativePathText(Boundary), Found, Error) && Found.empty(),
		    "project discovery stops at workspace boundary");
		#if defined(_WIN32)
		Expect(DiscoverProject(Entry, NativePathText(Parent / "WORKSPACE"), Found, Error) && Found.empty(),
		    "workspace boundary respects Windows path identity");
		#endif
		Expect(!DiscoverProject(NativePathText(Parent / "main.vs"), NativePathText(Boundary), Found, Error) &&
		        Found.empty() && Error.Code == "workspace-boundary",
		    "entry outside workspace fails without a selected project");
		Write(Boundary / "veroscript-project.json", Project.dump());
		Expect(DiscoverProject(Entry, NativePathText(Boundary), Found, Error) &&
		        PathUtil::Equal(Found, NativePathText(Boundary)),
		    "project at workspace boundary remains discoverable");
	}
	void IncompleteEnvironmentTests(const fs::path& Root)
	{
		const auto Project = Root / "incomplete-project";
		const auto Journal = Project / ".veroscript" / "transaction.json";
		Write(Journal, J{{"format", "veroscript-package-transaction"}, {"formatVersion", "0.1.1"}, {"management", NativePathText(Journal.parent_path())}, {"phase", "applying"}, {"writes", J::array()}}.dump());
		FRunEnvironmentRequest Q;
		Q.StartupDirectory = NativePathText(Project);
		Q.EntryFile = NativePathText(Project / "main.vs");
		Q.WorkspaceBoundary = NativePathText(Project);
		Q.Executable = NativePathText(Root / "absent-cli" / "veroscript");
		std::shared_ptr<const FInstalledEnvironment> Snapshot;
		FDiagnostic E;
		const auto Transport = std::make_shared<FRegistry>();
		FPackageManager Manager;
		const auto CheckReadCommands = [&]
		{
			const auto Before = Detail::Read(Journal, 1 << 20);
			for (const auto Operation : {EOperation::List, EOperation::Show})
			{
				FRequest Request;
				Request.Operation = Operation;
				Request.Environment.Root = NativePathText(Project);
				Request.Environment.CacheDirectory = NativePathText(Root / "incomplete-command-cache");
				Request.Options.Transport = Transport;
				if (Operation == EOperation::Show)
				{
					Request.Arguments = {"incomplete-package"};
				}
				FResult Result;
				const bool Accepted = Manager.Execute(Request, Result, E);
				Expect(!Accepted && !Result.Environment && E.Code == "recovery-required",
				    "list/show reject an interrupted first installation: " + E.Code + " " + E.Message);
				Expect(Transport->Calls.empty(), "incomplete local state does not fall back to a remote description");
				Expect(Detail::Read(Journal, 1 << 20) == Before &&
				        !fs::exists(Project / ".veroscript" / "state.json"),
				    "list/show preserve the pending journal and do not initialize state");
			}
		};
		Expect(!ResolveRunEnvironment(Q, Snapshot, E) && !Snapshot && E.Code == "recovery-required",
		    "automatic discovery rejects a first installation interrupted before project creation");
		CheckReadCommands();
		Write(Project / "veroscript-project.json", Detail::NewProject("incomplete-project").dump());
		Expect(!ResolveRunEnvironment(Q, Snapshot, E) && !Snapshot && E.Code == "recovery-required",
		    "automatic discovery rejects an incomplete environment with empty requirements");
		CheckReadCommands();
		Q.Selection.Path = NativePathText(Project);
		Expect(!ResolveRunEnvironment(Q, Snapshot, E) && !Snapshot && E.Code == "recovery-required",
		    "explicit path rejects a first installation without state");
		Expect(fs::exists(Journal) && !fs::exists(Project / ".veroscript" / "state.json"),
		    "read-only selection preserves the incomplete installation journal");
		const auto Startup = Root / "incomplete-startup";
		fs::create_directories(Startup);
		for (const auto* Role : {"runtime", "core-sdk"})
		{
			const auto Component = Root / (std::string("incomplete-") + Role);
			const auto Log = Component / ".veroscript" / "transaction.json";
			Write(Component / "bin" / "veroscript-component.json",
			    J{{"format", "veroscript-component"}, {"formatVersion", "0.1.1"}, {"role", Role},
			        {"root", ".."}, {"target", Detail::TargetJson(HostTargetContext())}}
			        .dump());
			Write(Log, J{{"format", "veroscript-package-transaction"}, {"formatVersion", "0.1.1"}, {"management", NativePathText(Log.parent_path())}, {"phase", "applying"}, {"writes", J::array()}}.dump());
			Q.Selection = {};
			Q.StartupDirectory = NativePathText(Startup);
			Q.EntryFile = NativePathText(Startup / "main.vs");
			Q.WorkspaceBoundary = NativePathText(Startup);
			Q.Executable = NativePathText(Component / "bin" / "veroscript");
			Expect(!ResolveRunEnvironment(Q, Snapshot, E) && !Snapshot && E.Code == "recovery-required",
			    "component fallback rejects incomplete " + std::string(Role) + " installation");
			Q.Selection.ComponentRoot = true;
			Q.Selection.Path = NativePathText(Component);
			Expect(!ResolveRunEnvironment(Q, Snapshot, E) && !Snapshot && E.Code == "recovery-required",
			    "explicit component selection rejects incomplete " + std::string(Role) + " installation");
			Expect(fs::exists(Log) && !fs::exists(Component / ".veroscript" / "state.json"),
			    "component selection leaves pending recovery untouched");
		}
	}
	void CliTests(const fs::path& Root)
	{
		auto Project = Root / "cli-project";
		fs::create_directories(Project);
		auto Component = Root / "component";
		auto Exe = NativePathText(Component / U8Path("bin/veroscript"));
		std::ostringstream Out, Err;
		const auto Environment = [&](const std::vector<std::string>& Args, const fs::path& Expected,
		                             const std::string& Scope)
		{
			Out.str("");
			Err.str("");
			auto Code = RunPackageCommand(Args, NativePathText(Project), Exe, Out, Err);
			Expect(Code == 0, "environment selection: " + Err.str());
			if (Code == 0)
			{
				auto Value = J::parse(Out.str());
				Expect(Value.at("scope") == Scope &&
				        PathUtil::Equal(Value.at("root"),
				            NativePathText(Detail::Absolute(NativePathText(Expected), {}))),
				    "selected exact environment root and scope");
			}
		};
		Environment({"env", "--json"}, Project, "project");
		Expect(!fs::exists(Project / ".veroscript"), "env creates no management directory");
		for (const auto& Args :
		    std::vector<std::vector<std::string>>{{"env", "--path"},
		        {"env", "--path="},
		        {"env", "--path", "--root"},
		        {"env", "--root=wrong"},
		        {"env", "--root", "--root"},
		        {"env", "--path", NativePathText(Project), "--path=other"},
		        {"env", "--project"},
		        {"env", "--project=other"},
		        {"env", "--user"}})
		{
			Out.str("");
			Err.str("");
			Expect(RunPackageCommand(Args, NativePathText(Project), Exe, Out, Err) != 0,
			    "invalid or removed selector rejected");
		}
		Expect(!fs::exists(Project / ".veroscript"), "invalid selectors cause no writes");
		FEnvironmentInput RemovedScope;
		RemovedScope.Root = NativePathText(Project);
		RemovedScope.Scope = static_cast<EEnvironmentScope>(3);
		FEnvironmentLayout RemovedLayout;
		FDiagnostic RemovedError;
		Expect(!ResolveEnvironment(RemovedScope, RemovedLayout, RemovedError) &&
		        RemovedError.Code == "environment-scope",
		    "removed user scope is not accepted as another scope");
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand({"env", "--root"}, NativePathText(Project), Exe, Out, Err) != 0,
		    "root requires a component descriptor");
		const auto Descriptor = [](const std::string& Role, const std::string& Relative)
		{
			return J{{"format", "veroscript-component"}, {"formatVersion", "0.1.1"}, {"role", Role},
			    {"root", Relative}, {"target", Detail::TargetJson(HostTargetContext())}};
		};
		Write(Component / U8Path("bin/veroscript-component.json"), Descriptor("core-sdk", "..").dump());
		Environment({"env", "--json"}, Project, "project");
		Environment({"env", "--root", "--json"}, Component, "sdk");
		Environment({"env", "--path", NativePathText(Component), "--root", "--json"}, Component, "sdk");
		Environment({"env", "--root", "--path=../component", "--json"}, Component, "sdk");
		auto Other = Root / U8Path("项目 with spaces");
		fs::create_directories(Other);
		Environment({"env", "--path", "../项目 with spaces", "--json"}, Other, "project");
		Environment({"env", "--path=" + NativePathText(Other), "--json"}, Other, "project");
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand({"env", "--root", "--path", NativePathText(Other)}, NativePathText(Project), Exe, Out,
		           Err) != 0,
		    "explicit component root does not accept an ordinary project");
		auto Vendor = Root / "cli-source";
		Write(Vendor / "veroscript-package.json", Manifest("cli-data").dump());
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand({"install", NativePathText(Vendor), "--offline"}, NativePathText(Project), Exe, Out,
		           Err) == 0,
		    "SDK CLI default installation uses startup project: " + Err.str());
		Expect(fs::is_directory(Project / U8Path(".veroscript/packages")) &&
		        !fs::exists(Component / ".veroscript"),
		    "default project install does not modify component management");
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand({"install", NativePathText(Vendor), "--root", "--offline"}, NativePathText(Project),
		           Exe, Out, Err) == 0,
		    "explicit SDK shared installation: " + Err.str());
		Expect(fs::is_directory(Component / U8Path(".veroscript/packages")), "SDK shared package location");
		auto Runtime = Root / "runtime-component";
		Write(Runtime / "veroscript-component.json", Descriptor("runtime", ".").dump());
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand(
		           {"install", NativePathText(Vendor), "--root", "--path", NativePathText(Runtime), "--offline"},
		           NativePathText(Project), Exe, Out, Err) == 0,
		    "explicit alternate Runtime component installation: " + Err.str());
		Expect(fs::is_directory(Runtime / "Packages"), "Runtime shared package location");
		FRunEnvironmentRequest Request;
		Request.StartupDirectory = NativePathText(Project);
		Request.Executable = Exe;
		FDiagnostic Error;
		std::shared_ptr<const FInstalledEnvironment> Snapshot;
		Expect(ResolveRunEnvironment(Request, Snapshot, Error) && Snapshot &&
		        Snapshot->GetLayout().Scope == EEnvironmentScope::Project,
		    "interactive discovery uses current project before component");
		Request.Selection.ComponentRoot = true;
		Expect(ResolveRunEnvironment(Request, Snapshot, Error) && Snapshot &&
		        Snapshot->GetLayout().Scope == EEnvironmentScope::Sdk,
		    "explicit run root bypasses current project");
		Request.Selection.Path = NativePathText(Runtime);
		Expect(ResolveRunEnvironment(Request, Snapshot, Error) && Snapshot &&
		        Snapshot->GetLayout().Scope == EEnvironmentScope::Runtime,
		    "explicit run component path selects the alternate Runtime");
		Write(Component / U8Path("bin/veroscript-component.json"), "invalid descriptor");
		Environment({"env", "--json"}, Project, "project");
		Out.str("");
		Err.str("");
		Expect(RunPackageCommand({"env", "--root"}, NativePathText(Project), Exe, Out, Err) != 0,
		    "corrupt selected component does not fall back to project");
		Write(Runtime / U8Path("bin/veroscript-component.json"), Descriptor("core-sdk", "..").dump());
		FEnvironmentInput Input;
		bool Found = false;
		Expect(!DiscoverComponentRoot(NativePathText(Runtime), Input, Found, Error) &&
		        Error.Code == "component-ambiguous",
		    "conflicting component descriptors rejected");
	}

	void ResourceEnvironmentTests(const fs::path& Root)
	{
		auto Project = Root / "resource-project", Vendor = Root / "resource-package";
		fs::create_directories(Project);
		auto M = Manifest("resource-package");
		M["resources"]["data"] = "data";
		M["install"]["actions"] =
		    J::array({J{{"type", "copy"}, {"source", "data/value.txt"}, {"destination", "generated.txt"}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / U8Path("data/value.txt"), "42");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "resource-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E) && R.Environment,
		    "install immutable resource environment: " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		FCoreEnvironmentOptions Options;
		Options.EntryRoot = NativePathText(Project);
		std::shared_ptr<const FModuleEnvironment> Environment;
		Expect(R.Environment->BuildCoreEnvironment(Options, Environment, E),
		    "build resource environment: " + E.Message);
		if (!Environment)
		{
			return;
		}
		const auto& P = R.Environment->GetPackages().front();
		FResourceRequest Request{P.TextDomain, P.TextDomain, "data", "value.txt"};
		FResourceData Data;
		std::string Error;
		Expect(Environment->ReadResource(Request, Options.EntryOwnerId, {}, Data, Error) && Data.bExists &&
		        std::string(Data.Bytes.begin(), Data.Bytes.end()) == "42",
		    "installed resource reads with its identity");
		auto Missing = Request;
		Missing.Path = "optional.txt";
		Expect(Environment->ReadResource(Missing, Options.EntryOwnerId, {}, Data, Error) && !Data.bExists,
		    "resource absent from receipt remains a successful missing result");
		const auto Original = U8Path(P.Path) / U8Path("data/value.txt");
		fs::remove(Original);
		Expect(!Environment->ReadResource(Request, Options.EntryOwnerId, {}, Data, Error) &&
		        Error.find("resource-integrity") != std::string::npos && !Data.bExists &&
		        Data.Bytes.empty(),
		    "deleted installed resource reports corruption rather than absence");
		Write(Original, "43");
		Expect(!Environment->ReadResource(Request, Options.EntryOwnerId, {}, Data, Error),
		    "edited installed resource rejects stale content identity");
		Write(Original, "42");
		FResourceWork Budget;
		Budget.MaxBytes = 1;
		Expect(!Environment->ReadResource(Request, Options.EntryOwnerId, Budget, Data, Error),
		    "package resource obeys core read budget");
		Write(U8Path(P.Path) / U8Path("data/optional.txt"), "unrecorded");
		Expect(!Environment->ReadResource(Missing, Options.EntryOwnerId, {}, Data, Error),
		    "unrecorded resource cannot enter an immutable environment");
		fs::remove(U8Path(P.Path) / U8Path("data/optional.txt"));
		Request.Group = "generated";
		Request.Path = "generated.txt";
		Expect(Environment->ReadResource(Request, Options.EntryOwnerId, {}, Data, Error) && Data.bExists,
		    "generated resource shares the core resource contract");
		const auto Generated = U8Path(P.GeneratedPath) / "generated.txt";
		fs::remove(Generated);
		Expect(!Environment->ReadResource(Request, Options.EntryOwnerId, {}, Data, Error) &&
		        Error.find("resource-integrity") != std::string::npos,
		    "deleted generated resource reports corruption");
		Write(Generated, "42");
	}

	void ScriptExtensionPackageTests(const fs::path& Root)
	{
		auto Project = Root / "script-extension-project", Vendor = Root / "script-extension-package";
		fs::create_directories(Project);
		auto M = Manifest("script-extension-package");
		M["entries"]["compiler"] = J::array({J{{"path", "scripts/processor.vs"}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / U8Path("data/value.txt"), "42");
		Write(Vendor / U8Path("scripts/processor.vs"), R"vs(
import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 assert(ctx.annotation().matches_name("@PACKAGEVALUE"));
 auto input=ctx.read_dependency("../data/value.txt");
 out.add_declaration(ctx.top_scope(),"int generated_package(){return "+input.content()+";}");
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::annotation_definition a=std::compiler::annotation_definition();
 a.name="@PackageValue";a.case_sensitive=false;
 a.targets=array<std::compiler::target>{std::compiler::target::class_type};reg.register_annotation(a);
 std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::annotation;
 s.annotation_name="@PackageValue";s.targets=array<std::compiler::target>{std::compiler::target::class_type};
 std::compiler::processor_definition p=std::compiler::processor_definition();p.id="package.script";p.version="0.1.1";
 p.phases=array<std::compiler::phase>{std::compiler::phase::members};
 p.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
 p.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(p,create);
})vs");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "script-extension-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E) && R.Environment,
		    "install script compiler extension without running it: " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		FCoreEnvironmentOptions Options;
		Options.EntryRoot = NativePathText(Project);
		Options.StdlibSearchPaths = {
		    NativePathText(fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
		        U8Path("VeroScript/Resources/Scripts/stdlib"))};
		Options.ScriptCompileExtensions.TimeoutMilliseconds = 15000;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		std::shared_ptr<const FModuleEnvironment> Environment;
		Expect(R.Environment->BuildCoreEnvironment(Options, Environment, E),
		    "build installed script processor environment: " + E.Message);
		if (!Environment)
		{
			return;
		}
		Expect(Environment->GetDescription().ScriptCompileExtensions.TimeoutMilliseconds == 15000 &&
		        Environment->GetDescription().ScriptCompileExtensions.VmBackendFactory == Options.ScriptCompileExtensions.VmBackendFactory,
		    "installed compiler environment preserves the trusted host deadline and VM selection");
		FCompileOptions Compile;
		VeroScript::Test::Configure(Compile);
		Compile.ModuleEnvironment = Environment;
		Compile.ScriptCompileExtensions.AllowedRoots = {R.Environment->GetPackages().front().Path};
		auto Code = VeroScriptCompile::CompileSource(
		    "@pAcKaGeVaLuE class PackageSource{};int main(){"
		    "assert(type<PackageSource>().has_annotation(\"PACKAGEVALUE\"));"
		    "assert(type<PackageSource>().annotation(0).name()==\"@pAcKaGeVaLuE\");"
		    "assert(!type<PackageSource>().annotation(0).definition().case_sensitive());"
		    "return generated_package();}",
		    NativePathText(Project / "main.vs"), Compile);
		Expect(Code.bSuccess,
		    "installed compiler root and scoped dependency participate in normal compilation: " +
		        Code.Error.Message);
		if (!Code.bSuccess)
		{
			return;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string Error;
		Expect(Vm.SetModuleEnvironment(Environment, Error) && Vm.Execute(*Code.Module, ERunMode::Script) &&
		        Vm.GetProcessExitCode() == 42,
		    "execute generated declaration from installed compiler package");
		Options.RuntimeOnly = true;
		std::shared_ptr<const FModuleEnvironment> Runtime;
		Expect(R.Environment->BuildCoreEnvironment(Options, Runtime, E) &&
		        Runtime->GetDescription().ScriptCompileExtensions.Roots.empty(),
		    "runtime environment does not rerun installed compiler registrations");
		std::ostringstream RuntimeOutput;
		VeroScriptVm RuntimeVm(RuntimeOutput);
		VeroScript::Test::Configure(RuntimeVm);
		Expect(Runtime && RuntimeVm.SetModuleEnvironment(Runtime, Error) &&
		        RuntimeVm.Execute(*Code.Module, ERunMode::Script) && RuntimeVm.GetProcessExitCode() == 42,
		    "generated bytecode runs with only the runtime environment");
	}

	std::uint32_t VEROSCRIPT_EXTENSION_CALL ServiceSelectionQuery(std::uint32_t Version, std::uint32_t Size, void* Out)
	{
		if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Out) return 0;
		FNativeExtensionDescriptor Descriptor;
		const auto Abi = BuildNativeExtensionAbi();
		std::memcpy(Descriptor.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(Descriptor.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(Descriptor.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
		std::memcpy(Descriptor.Id, "z-ready", 8);
		std::memcpy(Descriptor.Version, "0.1.1", 6);
		Descriptor.Roles = NativeExtensionRole(ENativeExtensionRole::Provider);
		Descriptor.Flags = NativeExtensionParallelRegistrations;
		Descriptor.Create = [](const void* Host, char* Error, std::uint32_t Capacity) -> void*
		{
			FNativeServiceProviderDesc Provider;
			Provider.ServiceId = "test";
			Provider.BackendId = "z-ready";
			Provider.ContractId = "test.service";
			Provider.ContractVersion = "0.1.1";
			Provider.Create = [](const auto&, void*, std::string&) -> void*
			{
				return new int(42);
			};
			Provider.Destroy = [](void* Value, void*)
			{
				delete static_cast<int*>(Value);
			};
			std::string Failure;
			if (!const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(Host))->RegisterProvider(Provider, Failure))
			{
				if (Capacity)
				{
					const auto Count = std::min<std::size_t>(Failure.size(), Capacity - 1);
					std::memcpy(Error, Failure.data(), Count);
					Error[Count] = 0;
				}
				return nullptr;
			}
			return new int(0);
		};
		Descriptor.Destroy = [](void* State)
		{
			delete static_cast<int*>(State);
		};
		std::memcpy(Out, &Descriptor, sizeof(Descriptor));
		return NativeExtensionApiVersion;
	}

	void ServiceSelectionTests(const fs::path& Root)
	{
		const auto Project = Root / "service-selection-project", Vendor = Root / "service-selection-source";
		fs::create_directories(Project);
		auto M = Manifest("service-selection");
		const auto Target = HostTargetContext();
		M["target"] = {{"os", Target.Os}, {"arch", Target.Architecture}, {"environment", Target.Environment},
		    {"coreBuild", Target.CoreBuild}, {"abis", {{"native-extension", Target.Abis.at("native-extension")}}}};
		for (const auto* Id : {"a-absent", "z-ready"})
		{
			const auto Path = std::string("native/") + Id + ".a";
			M["entries"]["native"].push_back({{"id", Id}, {"path", Path}, {"linkage", "static"},
			    {"componentVersion", "0.1.1"}, {"roles", J::array({"provider"})}});
			M["entries"]["backends"].push_back({{"service", "test"}, {"id", Id}, {"module", Id}});
			Write(Vendor / U8Path(Path), "host-linked selection fixture");
		}
		Write(Vendor / "veroscript-package.json", M.dump());
		FRequest Request;
		Request.Environment.Root = NativePathText(Project);
		Request.Environment.CacheDirectory = NativePathText(Root / "service-selection-cache");
		Request.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult Result;
		FDiagnostic Diagnostic;
		const auto Installed = Manager.Execute(Request, Result, Diagnostic);
		Expect(Installed && Result.Environment, "install service selection fixture: " + Diagnostic.Code + ": " + Diagnostic.Message);
		if (!Result.Environment) return;
		for (const auto* Reserved : {"auto", "none"})
		{
			auto Invalid = M;
			Invalid["entries"]["backends"][0]["id"] = Reserved;
			std::string Normalized;
			Expect(!ValidateDistribution(Invalid.dump(), Normalized, Diagnostic) && Diagnostic.Code == "backend-id",
			    "selection tokens are reserved backend IDs");
		}
		Expect(Result.Environment->GetSelectedBackend("test") == AutomaticBackend && Result.Environment->GetBackendCandidates("test") == std::vector<std::string>{"a-absent", "z-ready"},
		    "omitted service selection exposes deterministic enabled candidates");
		FCoreEnvironmentOptions Options;
		Options.EntryRoot = NativePathText(Project);
		auto& Static = Options.StaticExtensions["service-selection:z-ready"];
		Static.Query = ServiceSelectionQuery;
		Static.Owner = std::make_shared<int>(0);
		Static.ContentIdentity = "service-selection:0.1.1";
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::vector<FNativeServiceProviderDesc> Providers;
		Expect(Result.Environment->BuildCoreEnvironment(Options, Environment, Diagnostic, &Providers) && Providers.size() == 1, "automatic registration skips unavailable provider: " + Diagnostic.Message);
		if (Environment)
		{
			const auto& Diagnostics = Environment->GetDescription().BackendDiagnostics;
			Expect(Diagnostics.count("test") && Diagnostics.at("test").front().BackendId == "a-absent",
			    "skipped provider has an observable diagnostic");
			std::string Error;
			FNativeServiceCreateContext Context;
			Context.Environment = Environment;
			FBackendSelectionResult Selected;
			auto Service = CreateNativeService(Providers, "test", {}, "test.service", "0.1.1", Context, Error, &Selected);
			Expect(Service && Selected.BackendId == "z-ready" && Selected.Rejected.size() == 1 && *static_cast<int*>(Service->Get()) == 42, "registered automatic service and skipped diagnostics reach consumer");
		}
		Options.SelectedBackends["test"] = "a-absent";
		Expect(!Result.Environment->BuildCoreEnvironment(Options, Environment, Diagnostic),
		    "explicit unavailable provider is a hard failure");
		Options.SelectedBackends["test"] = "none";
		Expect(Result.Environment->BuildCoreEnvironment(Options, Environment, Diagnostic, &Providers) && Providers.empty(),
		    "disabled service does not register candidates");
		Options.SelectedBackends.clear();
		Options.AnalysisOnly = true;
		Options.StaticExtensions.clear();
		Expect(Result.Environment->BuildCoreEnvironment(Options, Environment, Diagnostic, &Providers) && Providers.empty() && !Environment->GetDescription().ExecutionBackends.Vm,
		    "analysis does not load provider candidates or execution factories");
	}

	void StaticCatalogTests(const fs::path& Root)
	{
		auto Project = Root / "static-catalog-project", Vendor = Root / "static-catalog-package";
		fs::create_directories(Project);
		auto M = Manifest("static-catalog-package");
		auto T = HostTargetContext();
		M["target"] = {{"os", T.Os},
		    {"arch", T.Architecture},
		    {"environment", T.Environment},
		    {"coreBuild", T.CoreBuild},
		    {"abis", {{"native-extension", T.Abis.at("native-extension")}}}};
		M["entries"]["native"] = J::array({J{{"id", "static-fixture"},
		    {"componentVersion", "0.1.1"},
		    {"path", "native/fixture.a"},
		    {"linkage", "static"},
		    {"roles", J::array({"bindings"})},
		    {"catalog", "native/catalog.json"}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / U8Path("native/fixture.a"), "host-linked static fixture");
		std::string Catalog;
		FHostBindingRegistry Bindings;
		std::string BindingError;
		Expect(Bindings.RegisterAnnotation("@PackageLabel", "Package label", BindingError, "", {},
		           AnnotationAllTargets, false) &&
		        Bindings.RegisterAnnotation("@PackageExact", "Exact annotation", BindingError),
		    "register catalog annotation name policies: " + BindingError);
		Expect(ExportBindingCatalogJson(Bindings, Catalog), "export static analysis declarations");
		Write(Vendor / U8Path("native/catalog.json"), Catalog);
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "static-catalog-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E) && R.Environment, "install static catalog package: " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		FCoreEnvironmentOptions Options;
		Options.EntryRoot = NativePathText(Project);
		Options.AnalysisOnly = true;
		Options.StaticExtensions["static-catalog-package:static-fixture"].ContentIdentity =
		    "linked.fixture:0.1.1";
		std::shared_ptr<const FModuleEnvironment> Environment;
		Expect(R.Environment->BuildCoreEnvironment(Options, Environment, E),
		    "analyze static package without querying target code: " + E.Message);
		Expect(Environment && Environment->GetDescription().AvailableDependencies.size() == 1 &&
		        Environment->GetDescription().AvailableDependencies.front().ContentIdentity ==
		            "linked.fixture:0.1.1",
		    "static analysis retains the explicitly supplied runtime content identity");
		if (Environment)
		{
			const auto& Annotations = Environment->GetDescription().Bindings->GetAnnotations();
			Expect(Annotations.size() == 2 && !Annotations[0].CaseSensitive && Annotations[1].CaseSensitive,
			    "installed static analysis catalog preserves both annotation name policies");
			FCompileOptions Compile;
			VeroScript::Test::Configure(Compile);
			Compile.ModuleEnvironment = Environment;
			auto Code = VeroScriptCompile::CompileSource(
			    "@pAcKaGeLaBeL @PackageExact class CatalogSource{};int main(){"
			    "assert(type<CatalogSource>().has_annotation(\"PACKAGELABEL\"));"
			    "assert(type<CatalogSource>().has_annotation(\"PackageExact\"));"
			    "assert(!type<CatalogSource>().has_annotation(\"packageexact\"));return 0;}",
			    NativePathText(Project / "catalog.vs"), Compile);
			Expect(Code.bSuccess, "compile installed catalog annotation policies: " + Code.Error.Message);
			if (Code.bSuccess)
			{
				std::ostringstream Output;
				VeroScriptVm Vm(Output);
				VeroScript::Test::Configure(Vm);
				Expect(Vm.SetModuleEnvironment(Environment, BindingError) &&
				        Vm.Execute(*Code.Module, ERunMode::Script) && Vm.GetProcessExitCode() == 0,
				    "installed catalog annotations retain their reflection policies: " + BindingError +
				        Vm.GetError().Message);
			}
		}
	}

	void NativeTests(const fs::path& Root, const std::string& Fixture)
	{
		auto Vendor = Root / "native-package", Project = Root / "native-project";
		fs::create_directories(Project);
		fs::create_directories(Vendor / "native");
		auto Binary = U8Path(Fixture);
		fs::copy_file(Binary, Vendor / "native" / Binary.filename());
		auto T = HostTargetContext();
		auto M = Manifest("native-package");
		M["target"] = {{"os", T.Os},
		    {"arch", T.Architecture},
		    {"environment", T.Environment},
		    {"coreBuild", T.CoreBuild},
		    {"abis", {{"native-extension", T.Abis.at("native-extension")}}}};
		M["entries"]["native"] =
		    J::array({J{{"id", "native-fixture"},
		        {"componentVersion", "0.1.1"},
		        {"path", PathUtil::ToUtf8(fs::path("native") / Binary.filename())},
		        {"linkage", "dynamic"},
		        {"roles", J::array({"bindings", "compiler", "provider"})}}});
		M["entries"]["backends"] =
		    J::array({J{{"service", "test"}, {"id", "fixture"}, {"module", "native-fixture"}}});
		{
			FClientOptions Policy;
			Detail::FWork Work{Policy};
			std::string Error;
			auto Original = Detail::Absolute(NativePathText(Vendor / "native" / Binary.filename()), {});
			auto Hash = Detail::HashFile(Original, Work, 1ull << 30);
			auto Provider = NativeProviderId("native-package", "native-fixture", Hash);
			auto Module = LoadNativeExtension(NativePathText(Original), Error);
			FNativeExtensionRegistry Registry;
			FNativeExtensionRegistrationOptions Registration;
			Registration.Origin.ProviderId = Provider;
			Expect(Module && Registry.Add(Module, Registration, Error), "producer native query: " + Error);
			std::string Catalog;
			Expect(ExportBindingCatalogJson(*Registry.GetBindings(), Catalog),
			    "export native analysis catalog");
			Write(Vendor / U8Path("native/catalog.json"), Catalog);
			M["entries"]["native"][0]["catalog"] = "native/catalog.json";
			FModuleEnvironmentDesc D;
			D.EntryOwnerId = "producer";
			D.Scopes = {{"producer", {Provider}}, {Provider, {}}};
			D.SourceRoots = {{NativePathText(Project), "producer"}};
			D.Bindings = Registry.GetBindings();
			D.AvailableDependencies = Registry.GetDependencies();
			auto Environment = FModuleEnvironment::Create(std::move(D), Error);
			FCompileOptions C;
			VeroScript::Test::Configure(C);
			C.ModuleEnvironment = Environment;
			auto Library = VeroScriptCompile::CompileSource(
			    "export int wrapped_native(){return native_fixture_number();} export "
			    "int generic_native<T>(T "
			    "v){return native_fixture_number();}",
			    NativePathText(Project / "producer.vs"), C);
			FPackageBuildOptions B;
			B.Kind = EPackageKind::Library;
			B.PackageName = "native-api";
			std::vector<std::uint8_t> Bytes;
			Expect(Library.bSuccess && BuildPackage(*Library.Module, B, Bytes, Error),
			    "precompile same-package native library: " + Library.Error.Message + Error);
			if (!Bytes.empty())
			{
				Write(Vendor / U8Path("scripts/native.vslib"), std::string(Bytes.begin(), Bytes.end()));
				M["entries"]["modules"]["native/api.vslib"] = "scripts/native.vslib";
			}
		}
		Write(Vendor / "veroscript-package.json", M.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "native-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		bool Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "native distribution installs without executing module: " + E.Message + " " + E.Field);
		if (!Ok || !R.Environment)
		{
			for (const auto& W : R.Warnings)
			{
				std::cerr << "WARNING " << W.Code << ": " << W.Message << " " << W.Field << '\n';
			}
			Expect(!Ok || bool(R.Environment), "successful mutation provides readable environment");
			return;
		}
		auto Path = R.Environment->GetPackages().front().Path;
		FHostObjectHandle Escaped;
		{
			FCoreEnvironmentOptions O;
			O.EntryRoot = NativePathText(Project);
			O.SelectedBackends["test"] = "fixture";
			O.PrepareBindings = [](FHostBindingRegistry& B, std::string& Error)
			{
				auto T = B.FindGenericTypeTemplate("NativeFixtureBox");
				FHostTypeId Id;
				return T && B.InstantiateGenericHostType(T->Id, {"int"}, Id, Error);
			};
			std::shared_ptr<const FModuleEnvironment> Env;
			std::vector<FNativeServiceProviderDesc> Providers;
			bool Built = R.Environment->BuildCoreEnvironment(O, Env, E, &Providers);
			Expect(Built,
			    "native registration/generic/provider: " + E.Code + " " + E.Message + " " + E.Field);
			if (!Env)
			{
				return;
			}
			Expect(Providers.size() == 1, "provider is declared and selected");
			FCompileOptions C;
			VeroScript::Test::Configure(C);
			C.ModuleEnvironment = Env;
			auto Code = VeroScriptCompile::CompileSource(
			    "import \"native/api.vslib\";class Generated{} NativeFixtureObject "
			    "obj=NativeFixtureObject(7);int "
			    "main(){NativeFixtureBox<int> b=NativeFixtureBox<int>();return "
			    "wrapped_native()+b.value()+Generated().generated()+generic_native<int>"
			    "(1);}",
			    NativePathText(Project / "main.vs"), C);
			Expect(Code.bSuccess,
			    "native functions/types/generics/compiler processor compile: " + Code.Error.Message);
			if (Code.bSuccess)
			{
				std::ostringstream Out;
				VeroScriptVm Vm(Out);
				VeroScript::Test::Configure(Vm);
				std::string Error;
				Expect(Vm.SetModuleEnvironment(Env, Error) && Vm.Execute(*Code.Module, ERunMode::Script) &&
				        Vm.GetProcessExitCode() == 166,
				    "native compiler/binding execution: " + Vm.GetError().Message);
				const auto& Natives = Env->GetDescription().Bindings->GetNativeFunctions();
				auto Ctor = std::find_if(Natives.begin(), Natives.end(),
				    [](const FNativeFunction& N)
				{
					return N.ReturnType == ENativeType::HostObject &&
					    N.ReturnHostTypeName == "NativeFixtureObject";
				});
				if (Ctor != Natives.end())
				{
					FValue Arg = std::int32_t{7}, Returned;
					FNativeCallContext Context;
					Context.Args = &Arg;
					Context.ArgCount = 1;
					Context.UserData = Ctor->UserData;
					Expect(Ctor->Callback(Context, Returned, Error),
					    "construct escaped object through retained bridge");
					if (auto H = std::get_if<FHostObjectHandle>(&Returned))
					{
						Escaped = *H;
					}
				}
			}
			std::ostringstream Out, Errors;
			FStreamEngineHost Host(Out, Errors);
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			FEngineConfig Config;
			VeroScript::Test::Configure(Config);
			Config.ModuleEnvironment = Env;
			Session.ApplyConfig(Config);
			auto Load = Session.LoadSource("int main(){return native_fixture_number();}",
			    NativePathText(Project / "session.vs"));
			Expect(Load.bSuccess && Session.Run().ExitCode == 37,
			    "SDK session uses installed native catalog: " + Errors.str());
			auto PreviousModule = Session.GetLoadedModule();
			FEngineConfig OtherConfig;
			VeroScript::Test::Configure(OtherConfig);
			Session.ApplyConfig(OtherConfig);
			Expect(Session.GetLoadedModule() == PreviousModule && Session.Run().ExitCode == 37,
			    "loaded session rejects replacement binding environment");
			Session.Clear();
			Session.ApplyConfig(OtherConfig);
			Expect(
			    Session.LoadSource("int main(){return 6;}", NativePathText(Project / "cleared.vs")).bSuccess &&
			        Session.Run().ExitCode == 6,
			    "cleared session accepts a new environment");
			O.AnalysisOnly = true;
			O.SelectedBackends.clear();
			O.PrepareBindings = {}; // Catalog templates do not execute native instantiation factories.
			std::shared_ptr<const FModuleEnvironment> Analysis;
			bool Analyzed = R.Environment->BuildCoreEnvironment(O, Analysis, E);
			Expect(Analyzed, "read-only native catalog analysis: " + E.Message);
			if (Analysis)
			{
				FCompileOptions Analyze;
				VeroScript::Test::Configure(Analyze);
				Analyze.ModuleEnvironment = Analysis;
				auto Checked = VeroScriptCompile::CompileSource(
				    "import \"native/api.vslib\";int main(){return wrapped_native();}",
				    NativePathText(Project / "analysis.vs"), Analyze);
				Expect(Checked.bSuccess,
				    "analysis preserves native identity in compiled libraries: " + Checked.Error.Message);
			}
		}
		Expect(bool(Escaped), "native object escapes VM with owned registration");
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"native-package"};
		Q.Options.SkipHooks = true;
		Expect(Manager.Execute(Q, R, E), "native package uninstall publishes new generation");
		Q.Operation = EOperation::Clean;
		Q.Arguments.clear();
		Q.Options.SkipHooks = false;
		Expect(Manager.Execute(Q, R, E) && fs::exists(U8Path(Path)),
		    "native object retains package filesystem lease");
		Escaped.reset();
		R = {};
		Expect(Manager.Execute(Q, R, E) && !fs::exists(U8Path(Path)),
		    "native artifact reclaimed only after last object release");
	}
	void JitTests(const fs::path& Root, const std::string& Backend)
	{
		auto Project = Root / "jit-project", Vendor = Root / "jit-package";
		fs::create_directories(Project);
		fs::create_directories(Vendor / "native");
		auto Binary = U8Path(Backend);
		fs::copy_file(Binary, Vendor / "native" / Binary.filename());
		auto T = HostTargetContext();
		auto M = Manifest("vs-asmjit");
		M["target"] = {{"os", T.Os},
		    {"arch", T.Architecture},
		    {"environment", T.Environment},
		    {"abis", {{"jit", T.Abis.at("jit")}}}};
		M["entries"]["backends"] =
		    J::array({J{{"service", "jit"},
		        {"id", "asmjit"},
		        {"contract", "jit"},
		        {"path", PathUtil::ToUtf8(fs::path("native") / Binary.filename())}}});
		auto Unavailable = M["entries"]["backends"][0];
		Unavailable["id"] = "a-wrong-identity";
		M["entries"]["backends"].push_back(std::move(Unavailable));
		Write(Vendor / "veroscript-package.json", M.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "jit-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		auto Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "dedicated JIT package installation: " + E.Message);
		if (!Ok || !R.Environment)
		{
			for (const auto& W : R.Warnings)
			{
				std::cerr << "WARNING " << W.Code << ": " << W.Message << " " << W.Field << '\n';
			}
			Expect(!Ok || bool(R.Environment), "successful mutation provides readable environment");
			return;
		}
		auto Old = R.Environment;
		Expect(Old->GetPackages().size() == 1 && Old->GetPackages().front().Name == "vs-asmjit",
		    "official package name is independent of its backend ID");
		std::shared_ptr<const IJitBackendFactory> Factory;
		Expect(Old->LoadJitBackend("asmjit", Factory, E), "installed AsmJit handshake: " + E.Message);
		if (!Factory)
		{
			return;
		}
		std::string BackendPath = "asmjit";
		Expect(Old->ResolveBackend("jit", BackendPath, BackendPath, E) && fs::is_regular_file(U8Path(BackendPath)),
		    "backend ID may share its output path storage: " + E.Message);
		Expect(Old->LoadJitBackend(Factory->GetDescription().Id, Factory, E) && Factory,
		    "backend reload preserves an ID borrowed from the replaced factory: " + E.Message);
		if (!Factory) return;
		FBackendSelectionResult Selected;
		Expect(Old->LoadJitBackend({}, Factory, E, &Selected) && Factory && Selected.BackendId == "asmjit" && Selected.Rejected.size() == 1 && Selected.Rejected.front().BackendId == "a-wrong-identity", "automatic JIT skips incompatible candidate");
		Expect(!Old->LoadJitBackend("a-wrong-identity", Factory, E, &Selected) && !Factory && Selected.State == EBackendSelectionState::Failed && !Selected.Error.empty(),
		    "explicit incompatible JIT never falls through to another candidate");
		Expect(Old->LoadJitBackend("none", Factory, E, &Selected) && !Factory && Selected.State == EBackendSelectionState::Disabled, "JIT disabled state is distinct from automatic absence");
		Expect(Old->LoadJitBackend("auto", Factory, E), "explicit automatic JIT can be restored");
		E.Message = "auto";
		Expect(Old->LoadJitBackend(E.Message, Factory, E) && Factory,
		    "automatic request may borrow diagnostic storage without losing its selection");

		FCoreEnvironmentOptions EnvironmentOptions;
		EnvironmentOptions.EntryRoot = NativePathText(Project);
		std::shared_ptr<const FModuleEnvironment> Environment;
		Expect(Old->BuildCoreEnvironment(EnvironmentOptions, Environment, E), "installed execution defaults environment");
		std::ostringstream SessionOutput, SessionErrors;
		FStreamEngineHost SessionHost(SessionOutput, SessionErrors);
		VeroScriptSession Session(SessionHost);
		VeroScript::Test::Configure(Session);
		FEngineConfig SessionConfig;
		SessionConfig.ModuleEnvironment = Environment;
		SessionConfig.ExecutionLimits.MaxInstructions = 1000;
		Session.ApplyConfig(SessionConfig);
		Expect(Session.GetJitBackendFactory() && Session.GetJitBackendFactory()->GetDescription().Id == "asmjit" && Session.LoadSource("int main(){return 49;}", NativePathText(Project / "defaults.vs")).bSuccess && Session.Run().ExitCode == 49, "Session uses installed automatic JIT and product VM with execution controls");
		FCompileOptions O;
		VeroScript::Test::Configure(O);
		auto Code = VeroScriptCompile::CompileSource(
		    "int square(int n){return n*n;}int main(){return square(7);}", "<jit>", O);
		std::ostringstream Out;
		VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
		std::string Error;
		Vm.SetJitBackendFactory(Factory, Error);
		Vm.SetExecutionBackend(EExecutionBackend::Mixed);
		Expect(Code.bSuccess && Vm.Execute(*Code.Module, ERunMode::Script) && Vm.GetProcessExitCode() == 49,
		    "installed backend executes normal bytecode");
		Q.Operation = EOperation::Deploy;
		Q.OutputPath = NativePathText(Root / "jit-deployment");
		Expect(Manager.Execute(Q, R, E), "JIT deployment copies loader closure");
		std::shared_ptr<const FInstalledEnvironment> Deployment;
		Expect(Manager.LoadDeployment(Q.OutputPath, Deployment, E), "JIT deployment reloads offline");
		std::shared_ptr<const IJitBackendFactory> DeployedFactory;
		Expect(Deployment && Deployment->LoadJitBackend("asmjit", DeployedFactory, E),
		    "deployed JIT can be selected by name: " + E.Message);
	}
	void CompositionTests(const fs::path& Root)
	{
		FPackageManager Manager;
		FDiagnostic E;
		FResult R;
		std::shared_ptr<const FInstalledEnvironment> Component, User;
		for (int I = 0; I < 2; ++I)
		{
			auto Project = Root / (I ? "user" : "sdk"), Vendor = Root / (I ? "user-source" : "sdk-source");
			fs::create_directories(Project);
			auto M = Manifest("shared-data");
			M["version"] = I ? "2.0.0" : "1.0.0";
			M["dependencies"]["inactive-platform"] = {{"version", "*"},
			    {"when", {{"os", "not-this-platform"}}}};
			M["dependencies"]["inactive-development"] = {{"version", "*"},
			    {"usage", J::array({"development"})}};
			Write(Vendor / "veroscript-package.json", M.dump());
			FRequest Q;
			Q.Environment.Scope = I ? EEnvironmentScope::Project : EEnvironmentScope::Sdk;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Root / "composition-cache");
			auto Requirements = Detail::NewProject("composition-project");
			Requirements["dependencies"]["inactive-root"] = {{"version", "*"},
			    {"usage", J::array({"development"})}};
			FEnvironmentLayout Layout;
			Expect(ResolveEnvironment(Q.Environment, Layout, E), "resolve component/user requirements");
			Write(U8Path(Layout.Requirements), Requirements.dump());
			Q.Arguments = {NativePathText(Vendor)};
			Expect(Manager.Execute(Q, R, E), "shared environment install: " + E.Message);
			if (!R.Environment)
			{
				return;
			}
			(I ? User : Component) = R.Environment;
		}
		std::shared_ptr<const FInstalledEnvironment> Combined;
		Expect(Manager.Combine(Component, User, Combined, E),
		    "offline component/user combination: " + E.Code + " " + E.Message);
		Expect(Combined && Combined->GetPackages().size() == 1 &&
		        Combined->GetPackages().front().Version == "2.0.0",
		    "user root overrides component root without duplicate versions");
		Expect(Component->GetPackages().front().Version == "1.0.0",
		    "composition does not mutate component snapshot");
		FRequest Q;
		Q.Environment.Scope = EEnvironmentScope::Project;
		Q.Environment.Root = NativePathText(Root / "user");
		Q.Environment.CacheDirectory = NativePathText(Root / "composition-cache");
		auto RequirementsPath = U8Path(User->GetLayout().Requirements);
		auto Requirements = Read(RequirementsPath);
		Requirements["dependencies"]["shared-data"]["optional"] = true;
		Requirements["packages"]["shared-data"]["enabled"] = false;
		Write(RequirementsPath, Requirements.dump());
		Expect(Manager.Execute(Q, R, E), "unselected user optional root installs an empty closure");
		auto Ok = Manager.Combine(Component, R.Environment, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().empty(),
		    "explicit inactive user root still overrides the same component root: " + E.Code + " " +
		        E.Message);
	}

	void CompositionPinTests(const fs::path& Root)
	{
		auto Transport = std::make_shared<FRegistry>();
		auto M = Manifest("pinned-data");
		const std::string Download = "https://fixture.invalid/pinned-data.zip";
		Transport->Add(Root / "pin-artifacts", M, "export int value(){return 1;}");
		auto OldBytes =
		    Transport->Data.at("https://fixture.invalid/api/artifacts/pinned-data-0.1.1/download");
		Transport->Add(Root / "pin-artifacts-other", M, "export int value(){return 2;}");
		auto NewBytes =
		    Transport->Data.at("https://fixture.invalid/api/artifacts/pinned-data-0.1.1/download");
		Transport->Data[Download] = OldBytes;
		auto Pinned = J{{"version", "0.1.1"}, {"url", Download}, {"sha256", Detail::Hash(OldBytes)}};
		auto Parent = Manifest("pinned-parent");
		Parent["dependencies"]["pinned-data"] = Pinned;
		auto Vendor = Root / "pin-parent";
		Write(Vendor / "veroscript-package.json", Parent.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Root / "pin-component");
		Q.Environment.Scope = EEnvironmentScope::Sdk;
		Q.Environment.CacheDirectory = NativePathText(Root / "pin-cache");
		fs::create_directories(U8Path(Q.Environment.Root));
		Q.Options.Transport = Transport;
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		FEnvironmentLayout ComponentLayout;
		Expect(ResolveEnvironment(Q.Environment, ComponentLayout, E), "resolve pinned component environment");
		auto ComponentProject = Detail::NewProject("pin-component");
		ComponentProject["packages"]["pinned-data"]["configuration"]["option"] = "component";
		Write(U8Path(ComponentLayout.Requirements), ComponentProject.dump());
		auto Ok = Manager.Execute(Q, R, E);
		Expect(Ok,
		    "install component with explicitly pinned transitive artifact: " + E.Code + " " + E.Message);
		if (!Ok || !R.Environment)
		{
			return;
		}
		auto Component = R.Environment;
		Q.Environment.Root = NativePathText(Root / "pin-user");
		Q.Environment.Scope = EEnvironmentScope::Project;
		fs::create_directories(U8Path(Q.Environment.Root));
		FEnvironmentLayout Layout;
		Expect(ResolveEnvironment(Q.Environment, Layout, E), "resolve pinned user environment");
		auto Project = Detail::NewProject("pin-user");
		Project["dependencies"]["pinned-data"] = Pinned;
		Project["dependencies"]["pinned-data"]["sha256"] = Detail::Hash(NewBytes);
		Write(U8Path(Layout.Requirements), Project.dump());
		Transport->Data[Download] = NewBytes;
		Q.Arguments.clear();
		Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "install user artifact at the same source and version: " + E.Code + " " + E.Message);
		if (!Ok || !R.Environment)
		{
			return;
		}
		std::shared_ptr<const FInstalledEnvironment> Combined;
		Ok = Manager.Combine(Component, R.Environment, Combined, E);
		Expect(!Ok && E.Code == "dependency-conflict",
		    "composition preserves incompatible explicit artifact pins");
		Project["dependencies"]["pinned-data"] = Pinned;
		Write(U8Path(Layout.Requirements), Project.dump());
		Expect(Manager.Execute(Q, R, E), "restore matching user artifact pin from cache");
		Ok = Manager.Combine(Component, R.Environment, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().size() == 2,
		    "composition accepts matching explicit artifact pins: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			auto P = std::find_if(Combined->GetPackages().begin(), Combined->GetPackages().end(),
			    [](const FInstalledPackage& N)
			{
				return N.Name == "pinned-data";
			});
			Expect(P != Combined->GetPackages().end() && J::parse(P->ConfigurationJson) == J::object(),
			    "user root without configuration resets component configuration to the default");
		}
	}
	void CompositionProcessingTests(const fs::path& Root)
	{
		auto Area = Root / "composition-processing";
		auto Registry = std::make_shared<FRegistry>();
		auto Leaf = Manifest("composition-leaf");
		Leaf["entries"]["modules"]["composition/leaf.vs"] = "main.vs";
		Registry->Add(Area / "registry", Leaf, "export string leaf_value(){return \"old\";}");
		Leaf["version"] = "0.2.0";
		Registry->Add(Area / "registry", Leaf, "export string leaf_value(){return \"new\";}");
		auto Middle = Manifest("composition-middle");
		Middle["dependencies"]["composition-leaf"] = {{"version", "*"}};
		Middle["entries"]["modules"]["composition/middle.vs"] = "main.vs";
		Middle["resources"]["data"] = "data";
		Write(Area / U8Path("registry/composition-middle-0.1.1/data/value.txt"), "old resource");
		Registry->Add(Area / "registry", Middle,
		    "import \"composition/leaf.vs\";export string middle_value(){return leaf_value();}");
		Middle["version"] = "0.2.0";
		Write(Area / U8Path("registry/composition-middle-0.2.0/data/value.txt"), "new resource");
		Registry->Add(Area / "registry", Middle,
		    "import \"composition/leaf.vs\";export string middle_value(){return \"updated\";}");
		auto Parent = Manifest("composition-parent");
		Parent["dependencies"]["composition-middle"] = {{"version", "*"}};
		Parent["install"]["hooks"] = J::array({J{{"phase", "post-install"}, {"path", "main.vs"},
		    {"entry", "install"}, {"permissions", J::array({"resources.read", "generated.write"})}}});
		Registry->Add(Area / "registry", Parent,
		    "import \"composition/middle.vs\";export void install(std::package::install_context c){"
		    "c.write_text(\"dependency.txt\",middle_value());"
		    "c.write_text(\"resource.txt\",c.read_dependency_text(\"composition-middle\",\"data\",\"value.txt\"));}");
		auto Pure = Manifest("composition-pure");
		Pure["dependencies"]["composition-leaf"] = {{"version", "*"}};
		Registry->Add(Area / "registry", Pure);
		FPackageManager Manager;
		FDiagnostic E;
		const auto Install = [&](const std::string& Label, const J& Dependencies)
		{
			auto Project = Area / Label;
			auto Configuration = Detail::NewProject("composition-processing-project");
			Configuration["packageManager"]["registries"]["official"] = {
			    {"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
			Configuration["dependencies"] = Dependencies;
			Write(Project / "veroscript-project.json", Configuration.dump());
			FRequest Q;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Area / "cache");
			Q.Options.Transport = Registry;
			Q.Options.AllowHooks = true;
			FResult R;
			const auto Ok = Manager.Execute(Q, R, E);
			Expect(Ok && R.Environment,
			    "install composition processing fixture " + Label + ": " + E.Code + " " + E.Message);
			return R.Environment;
		};
		const auto ParentPackage = [](const std::shared_ptr<const FInstalledEnvironment>& Environment)
		    -> const FInstalledPackage*
		{
			for (const auto& P : Environment->GetPackages())
			{
				if (P.Name == "composition-parent") return &P;
			}
			return nullptr;
		};
		const J BaseRoots = {{"composition-parent", {{"version", "0.1.1"}}},
		    {"composition-middle", {{"version", "0.1.1"}}}, {"composition-leaf", {{"version", "0.1.1"}}}};
		auto Base = Install("base", BaseRoots);
		auto Transitive = Install("transitive", J{{"composition-leaf", {{"version", "0.2.0"}}}});
		if (!Base || !Transitive) return;
		const auto* Original = ParentPackage(Base);
		Expect(Original && Detail::Read(U8Path(Original->GeneratedPath) / "dependency.txt", 100) == "old",
		    "hook generation observes its installed transitive script dependency");
		std::shared_ptr<const FInstalledEnvironment> Combined;
		Expect(!Manager.Combine(Base, Transitive, Combined, E) && !Combined && E.Code == "combine-processing",
		    "composition rejects generated output after only a transitive dependency changes: " + E.Code);
		const J DirectRoots = {{"composition-middle", {{"version", "0.2.0"}}},
		    {"composition-leaf", {{"version", "0.1.1"}}}};
		auto Direct = Install("direct", DirectRoots);
		if (!Direct) return;
		Expect(!Manager.Combine(Base, Direct, Combined, E) && !Combined && E.Code == "combine-processing",
		    "composition rejects stale direct dependency resource processing: " + E.Code);
		auto MatchingRoots = DirectRoots;
		MatchingRoots["composition-parent"] = {{"version", "0.1.1"}};
		auto Matching = Install("matching", MatchingRoots);
		if (!Matching) return;
		const auto Ok = Manager.Combine(Base, Matching, Combined, E);
		Expect(Ok && Combined, "composition reuses another matching installed origin: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			const auto* P = ParentPackage(Combined);
			Expect(P && Original && P->Sha256 == Original->Sha256 &&
			        Detail::Read(U8Path(P->GeneratedPath) / "dependency.txt", 100) == "updated" &&
			        Detail::Read(U8Path(P->GeneratedPath) / "resource.txt", 100) == "new resource",
			    "same parent artifact selects generated output installed against the effective dependency graph");
		}
		auto PureBase = Install("pure", J{{"composition-pure", {{"version", "0.1.1"}}}, {"composition-leaf", {{"version", "0.1.1"}}}});
		if (!PureBase) return;
		Expect(Manager.Combine(PureBase, Transitive, Combined, E) && Combined &&
		        Combined->GetPackages().size() == 2,
		    "pure script composition can still replace dependencies without installation processing: " + E.Code);
		Expect(Original && Detail::Read(U8Path(Original->GeneratedPath) / "dependency.txt", 100) == "old",
		    "composition never rewrites the source snapshot's generated output");
	}
	void CompositionLoaderTests(const fs::path& Root)
	{
		auto Area = Root / "composition-loaders";
		auto Registry = std::make_shared<FRegistry>();
		const auto Host = HostTargetContext();
		const J Target = {{"os", Host.Os}, {"arch", Host.Architecture}, {"environment", Host.Environment},
		    {"coreBuild", Host.CoreBuild}, {"abis", {{"native-extension", Host.Abis.at("native-extension")}}}};
		auto Script = Manifest("loader-script");
		Registry->Add(Area / "registry", Script);
		Script["version"] = "0.2.0";
		Registry->Add(Area / "registry", Script);
		auto Own = Manifest("loader-own");
		Own["target"] = Target;
		Own["dependencies"]["loader-script"] = {{"version", "*"}};
		Own["install"]["inputs"]["mode"] = {{"type", "string"}, {"default", "base"}};
		Own["entries"]["native"] = J::array({J{{"id", "own-loader"}, {"componentVersion", "0.1.1"},
		    {"path", "native/own.bin"}, {"linkage", "dynamic"}, {"roles", J::array({"bindings"})}}});
		Write(Area / U8Path("registry/loader-own-0.1.1/native/own.bin"), "unexecuted native fixture");
		Registry->Add(Area / "registry", Own);
		auto OwnRoot = Manifest("loader-own-root");
		OwnRoot["dependencies"]["loader-own"] = {{"version", "0.1.1"}};
		Registry->Add(Area / "registry", OwnRoot);
		auto Library = Manifest("loader-library");
		Library["target"] = Target;
		Library["entries"]["libraries"] = J::array({J{{"id", "support-lib"},
		    {"path", "native/support.bin"}, {"linkage", "dynamic"}}});
		Write(Area / U8Path("registry/loader-library-0.1.1/native/support.bin"), "old support library");
		Registry->Add(Area / "registry", Library);
		Library["version"] = "0.2.0";
		Write(Area / U8Path("registry/loader-library-0.2.0/native/support.bin"), "new support library");
		Registry->Add(Area / "registry", Library);
		Library["version"] = "0.3.0";
		Library["entries"]["libraries"][0]["linkage"] = "static";
		Write(Area / U8Path("registry/loader-library-0.3.0/native/support.bin"), "old static library");
		Registry->Add(Area / "registry", Library);
		Library["version"] = "0.4.0";
		Write(Area / U8Path("registry/loader-library-0.4.0/native/support.bin"), "new static library");
		Registry->Add(Area / "registry", Library);
		Library["version"] = "0.5.0";
		Library["entries"]["libraries"][0]["id"] = "other-lib";
		Write(Area / U8Path("registry/loader-library-0.5.0/native/support.bin"), "different library identity");
		Registry->Add(Area / "registry", Library);
		auto Consumer = Manifest("loader-consumer");
		Consumer["target"] = Target;
		Consumer["dependencies"]["loader-library"] = {{"version", "*"}};
		Consumer["entries"]["native"] = J::array({J{{"id", "consumer-loader"}, {"componentVersion", "0.1.1"},
		    {"path", "native/consumer.bin"}, {"linkage", "dynamic"}, {"roles", J::array({"bindings"})},
		    {"libraries", J::array({"loader-library:support-lib"})}}});
		Write(Area / U8Path("registry/loader-consumer-0.1.1/native/consumer.bin"), "unexecuted consumer fixture");
		Registry->Add(Area / "registry", Consumer);
		auto CompileConsumer = Consumer;
		CompileConsumer["name"] = "loader-compile-consumer";
		CompileConsumer["dependencies"]["loader-library"] = {
		    {"version", "0.1.1"}, {"usage", J::array({"compile"})}};
		Write(Area / U8Path("registry/loader-compile-consumer-0.1.1/native/consumer.bin"), "unexecuted compile-linked consumer");
		Registry->Add(Area / "registry", CompileConsumer);
		Registry->Add(Area / "registry", Manifest("snapshot-compile-tool"));
		Registry->Add(Area / "registry", Manifest("snapshot-overlay"));
		auto RuntimeParent = Manifest("snapshot-runtime-parent");
		RuntimeParent["dependencies"]["snapshot-compile-tool"] = {
		    {"version", "0.1.1"}, {"usage", J::array({"compile"})}};
		Registry->Add(Area / "registry", RuntimeParent);
		FPackageManager Manager;
		FDiagnostic E;
		const auto Install = [&](const std::string& Label, const J& Dependencies, const J& Packages = J::object())
		{
			auto Project = Area / Label;
			auto Configuration = Detail::NewProject("composition-loader-project");
			Configuration["packageManager"]["registries"]["official"] = {
			    {"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
			Configuration["dependencies"] = Dependencies;
			Configuration["packages"] = Packages;
			Write(Project / "veroscript-project.json", Configuration.dump());
			FRequest Q;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Area / "cache");
			Q.Options.Transport = Registry;
			FResult R;
			const auto Ok = Manager.Execute(Q, R, E);
			Expect(Ok && R.Environment,
			    "install composition loader fixture " + Label + ": " + E.Code + " " + E.Message);
			return R.Environment;
		};
		const auto FindPackage = [](const std::shared_ptr<const FInstalledEnvironment>& Environment,
		                             const std::string& Name) -> const FInstalledPackage*
		{
			for (const auto& P : Environment->GetPackages())
			{
				if (P.Name == Name) return &P;
			}
			return nullptr;
		};
		auto OwnBase = Install("own-base", J{{"loader-own-root", {{"version", "0.1.1"}}}, {"loader-script", {{"version", "0.1.1"}}}});
		auto OwnOverlay = Install("own-overlay", J{{"loader-script", {{"version", "0.2.0"}}}},
		    J{{"loader-own", {{"configuration", {{"mode", "changed"}}}}}});
		if (!OwnBase || !OwnOverlay) return;
		std::shared_ptr<const FInstalledEnvironment> Combined;
		auto Ok = Manager.Combine(OwnBase, OwnOverlay, Combined, E);
		Expect(Ok && Combined,
		    "own loader permits unrelated script and runtime configuration changes: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			const auto* P = FindPackage(Combined, "loader-own");
			const auto* Before = FindPackage(OwnBase, "loader-own");
			Expect(P && Before && P->GeneratedPath == Before->GeneratedPath &&
			        J::parse(P->ConfigurationJson).value("mode", "") == "changed" &&
			        !J::parse(P->LoadersJson).empty(),
			    "unchanged own binary reuses its loader without processing the effective configuration");
		}
		auto LibraryBase = Install("library-base", J{{"loader-consumer", {{"version", "0.1.1"}}}, {"loader-library", {{"version", "0.1.1"}}}});
		auto LibraryOverlay = Install("library-overlay", J{{"loader-library", {{"version", "0.2.0"}}}});
		if (!LibraryBase || !LibraryOverlay) return;
		Expect(!Manager.Combine(LibraryBase, LibraryOverlay, Combined, E) && !Combined &&
		        E.Code == "combine-processing",
		    "cross-package loader rejects changed library provider bytes: " + E.Code);
		auto Matching = Install("library-matching", J{{"loader-consumer", {{"version", "0.1.1"}}}, {"loader-library", {{"version", "0.2.0"}}}});
		if (!Matching) return;
		Ok = Manager.Combine(LibraryBase, Matching, Combined, E);
		Expect(Ok && Combined, "cross-package loader can reuse a matching installed origin: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			const auto* P = FindPackage(Combined, "loader-consumer");
			const auto* Before = FindPackage(LibraryBase, "loader-consumer");
			Expect(P && Before && P->Sha256 == Before->Sha256,
			    "matching library environment retains the same consumer artifact");
			if (P)
			{
				const auto Loaders = J::parse(P->LoadersJson);
				const auto Path = Loaders.at("native:consumer-loader").at("libraries").at(0).get<std::string>();
				Expect(Detail::Read(U8Path(P->GeneratedPath) / U8Path(Path), 100) == "new support library",
				    "matching origin uses the newly selected provider's copied library without loading it");
			}
			std::shared_ptr<const FInstalledEnvironment> RecombinedLoader;
			Expect(Manager.Combine(Combined, LibraryOverlay, RecombinedLoader, E) && RecombinedLoader,
			    "a previously combined loader snapshot remains reusable with the same library provider: " + E.Code);
		}
		auto StaticBase = Install("static-base", J{{"loader-consumer", {{"version", "0.1.1"}}}, {"loader-library", {{"version", "0.3.0"}}}});
		auto StaticOverlay = Install("static-overlay", J{{"loader-library", {{"version", "0.4.0"}}}});
		auto MissingLibrary = Install("missing-library", J{{"loader-library", {{"version", "0.5.0"}}}});
		if (!StaticBase || !StaticOverlay || !MissingLibrary) return;
		Ok = Manager.Combine(StaticBase, StaticOverlay, Combined, E);
		Expect(Ok && Combined, "static library version changes do not invalidate unchanged linked consumer bytes: " + E.Code);
		if (Ok && Combined)
		{
			const auto* P = FindPackage(Combined, "loader-consumer");
			const auto* Before = FindPackage(StaticBase, "loader-consumer");
			Expect(P && Before && P->GeneratedPath == Before->GeneratedPath &&
			        J::parse(P->LoadersJson).at("native:consumer-loader").at("libraries").empty(),
			    "static dependencies stay outside the loader's copied library view");
		}
		Expect(!Manager.Combine(LibraryBase, StaticOverlay, Combined, E) && !Combined && E.Code == "combine-processing",
		    "dynamic-to-static library transition rejects the old dynamic loader view: " + E.Code);
		Expect(!Manager.Combine(StaticBase, LibraryOverlay, Combined, E) && !Combined && E.Code == "combine-processing",
		    "static-to-dynamic library transition rejects the missing copied dynamic library: " + E.Code);
		Expect(!Manager.Combine(StaticBase, MissingLibrary, Combined, E) && !Combined && E.Code == "combine-processing",
		    "provider with no matching library ID rejects loader reuse: " + E.Code);
		auto Runtime = Install("runtime", J{{"snapshot-runtime-parent", {{"version", "0.1.1"}}}});
		auto Unrelated = Install("unrelated", J{{"snapshot-overlay", {{"version", "0.1.1"}}}});
		if (!Runtime || !Unrelated) return;
		std::string Snapshot;
		const auto DeploymentPath = Area / "runtime-deployment";
		Ok = Runtime->Deploy(NativePathText(DeploymentPath), {"runtime"}, {}, Snapshot, E);
		Expect(Ok, "deploy runtime snapshot without compile-only dependency: " + E.Code + " " + E.Message);
		if (!Ok) return;
		std::shared_ptr<const FInstalledEnvironment> Deployed;
		Ok = Manager.LoadDeployment(NativePathText(DeploymentPath), Deployed, E);
		Expect(Ok && Deployed && Deployed->GetPackages().size() == 1,
		    "reload pruned runtime deployment: " + E.Code + " " + E.Message);
		if (!Ok || !Deployed) return;
		std::shared_ptr<const FInstalledEnvironment> PrunedOverlay;
		Ok = Manager.Combine(Runtime, Deployed, PrunedOverlay, E);
		Expect(Ok && PrunedOverlay && PrunedOverlay->GetPackages().size() == 1 &&
		        !FindPackage(PrunedOverlay, "snapshot-compile-tool"),
		    "same-artifact deployment overlay selects its frozen pruned edges ahead of the original installation: " + E.Code);
		if (Ok && PrunedOverlay)
		{
			std::shared_ptr<const FInstalledEnvironment> RecombinedOverlay;
			Expect(Manager.Combine(PrunedOverlay, Unrelated, RecombinedOverlay, E) && RecombinedOverlay &&
			        RecombinedOverlay->GetPackages().size() == 2 &&
			        !FindPackage(RecombinedOverlay, "snapshot-compile-tool"),
			    "same-artifact frozen-edge override remains pruned after another composition: " + E.Code);
		}
		Ok = Manager.Combine(Deployed, Unrelated, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().size() == 2,
		    "composition uses frozen deployment edges instead of restoring the original compile dependency: " + E.Code);
		if (!Ok || !Combined) return;
		std::shared_ptr<const FInstalledEnvironment> Recombined;
		Ok = Manager.Combine(Combined, Unrelated, Recombined, E);
		Expect(Ok && Recombined && Recombined->GetPackages().size() == 2,
		    "a composed deployment can be composed again without restoring discarded dependencies: " + E.Code);
		if (Ok && Recombined)
		{
			const auto* P = FindPackage(Recombined, "snapshot-runtime-parent");
			Expect(P && P->Dependencies.empty() && !FindPackage(Recombined, "snapshot-compile-tool"),
			    "repeated composition preserves the runtime snapshot's original dependency pruning");
		}
		auto CompileRuntime = Install("compile-loader", J{{"loader-compile-consumer", {{"version", "0.1.1"}}}});
		if (!CompileRuntime) return;
		const auto LoaderDeployment = Area / "runtime-loader-deployment";
		Ok = CompileRuntime->Deploy(NativePathText(LoaderDeployment), {"runtime"}, {}, Snapshot, E);
		Expect(Ok, "deploy self-contained loader while pruning its compile-only library provider: " + E.Code + " " + E.Message);
		if (!Ok) return;
		Ok = Manager.LoadDeployment(NativePathText(LoaderDeployment), Deployed, E);
		Expect(Ok && Deployed && Deployed->GetPackages().size() == 1,
		    "self-contained loader deployment loads without the compile-only provider package: " + E.Code);
		if (!Ok || !Deployed) return;
		const auto ReadCopiedLibrary = [&](const std::shared_ptr<const FInstalledEnvironment>& Environment)
		{
			const auto* P = FindPackage(Environment, "loader-compile-consumer");
			if (!P) return std::string{};
			const auto Loaders = J::parse(P->LoadersJson);
			const auto Path = Loaders.at("native:consumer-loader").at("libraries").at(0).get<std::string>();
			return Detail::Read(U8Path(P->GeneratedPath) / U8Path(Path), 100);
		};
		Expect(ReadCopiedLibrary(Deployed) == "old support library" && !FindPackage(Deployed, "loader-library"),
		    "pruned loader retains the original dynamic library bytes in its immutable generated view");
		Ok = Manager.Combine(Deployed, Unrelated, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().size() == 2,
		    "self-contained deployed loader combines without resurrecting its compile-only provider: " + E.Code);
		if (!Ok || !Combined) return;
		Expect(ReadCopiedLibrary(Combined) == "old support library" && !FindPackage(Combined, "loader-library"),
		    "composition preserves copied bytes while both snapshots omit the compile-only provider edge");
		Ok = Manager.Combine(Combined, Unrelated, Recombined, E);
		Expect(Ok && Recombined && Recombined->GetPackages().size() == 2,
		    "self-contained loader survives repeated composition with its provider edge still pruned: " + E.Code);
		if (Ok && Recombined)
		{
			const auto* P = FindPackage(Recombined, "loader-compile-consumer");
			Expect(P && P->Dependencies.empty() && !FindPackage(Recombined, "loader-library") &&
			        ReadCopiedLibrary(Recombined) == "old support library",
			    "repeated composition neither recreates the provider edge nor changes the copied dynamic library");
		}
	}
	void CompositionDeploymentConfigurationTests(const fs::path& Root)
	{
		auto Area = Root / "composition-deployment-configuration";
		auto Registry = std::make_shared<FRegistry>();
		auto Processed = Manifest("configuration-processed");
		Processed["install"]["inputs"]["value"] = {{"type", "string"}, {"default", "fallback"}};
		Processed["install"]["actions"] = J::array({J{{"type", "template"},
		    {"source", "template.txt"}, {"destination", "resolved.txt"}}});
		Write(Area / U8Path("registry/configuration-processed-0.1.1/template.txt"), "configured={value}");
		Registry->Add(Area / "registry", Processed);
		Registry->Add(Area / "registry", Manifest("configuration-plain"));
		Registry->Add(Area / "registry", Manifest("configuration-unrelated"));
		const J Configurations = {{"configuration-processed", {{"value", "deployed input"}}},
		    {"configuration-plain", {{"marker", "plain value"}, {"count", 7}}}};
		FPackageManager Manager;
		FDiagnostic E;
		const auto Install = [&](const std::string& Label, const J& Dependencies, bool Configured)
		{
			auto Project = Area / Label;
			auto Configuration = Detail::NewProject("deployment-configuration-project");
			Configuration["packageManager"]["registries"]["official"] = {
			    {"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
			Configuration["dependencies"] = Dependencies;
			if (Configured)
			{
				for (auto I = Configurations.begin(); I != Configurations.end(); ++I)
				{
					Configuration["packages"][I.key()]["configuration"] = I.value();
				}
			}
			Write(Project / "veroscript-project.json", Configuration.dump());
			FRequest Q;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Area / "cache");
			Q.Options.Transport = Registry;
			FResult R;
			const auto Ok = Manager.Execute(Q, R, E);
			Expect(Ok && R.Environment,
			    "install deployment configuration fixture " + Label + ": " + E.Code + " " + E.Message);
			return R.Environment;
		};
		auto Original = Install("configured", J{{"configuration-processed", {{"version", "0.1.1"}}}, {"configuration-plain", {{"version", "0.1.1"}}}}, true);
		auto Unrelated = Install("unrelated", J{{"configuration-unrelated", {{"version", "0.1.1"}}}}, false);
		if (!Original || !Unrelated) return;
		std::string Snapshot;
		const auto Deployment = Area / "deployment";
		auto Ok = Original->Deploy(NativePathText(Deployment), {"runtime"}, {}, Snapshot, E);
		Expect(Ok, "deploy configured processing and ordinary packages: " + E.Code + " " + E.Message);
		if (!Ok) return;
		std::shared_ptr<const FInstalledEnvironment> Deployed;
		Ok = Manager.LoadDeployment(NativePathText(Deployment), Deployed, E);
		Expect(Ok && Deployed, "load deployment configuration snapshot: " + E.Code + " " + E.Message);
		if (!Ok || !Deployed) return;
		const auto CheckConfiguration = [&](const std::shared_ptr<const FInstalledEnvironment>& Environment)
		{
			for (auto I = Configurations.begin(); I != Configurations.end(); ++I)
			{
				const auto P = std::find_if(Environment->GetPackages().begin(), Environment->GetPackages().end(),
				    [&](const FInstalledPackage& Item)
				{
					return Item.Name == I.key();
				});
				Expect(P != Environment->GetPackages().end() && J::parse(P->ConfigurationJson) == I.value(),
				    "deployment composition retains exact package configuration: " + I.key());
				if (P != Environment->GetPackages().end() && I.key() == "configuration-processed")
				{
					Expect(Detail::Read(U8Path(P->GeneratedPath) / "resolved.txt", 100) == "configured=deployed input",
					    "processing output remains consistent with the deployment's nonempty input configuration");
				}
			}
		};
		CheckConfiguration(Deployed);
		std::shared_ptr<const FInstalledEnvironment> Combined;
		Ok = Manager.Combine(Unrelated, Deployed, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().size() == 3,
		    "configured deployment serves as an overlay without clearing inputs or rerunning processing: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			CheckConfiguration(Combined);
			std::shared_ptr<const FInstalledEnvironment> Recombined;
			const auto Again = Manager.Combine(Combined, Deployed, Recombined, E);
			Expect(Again && Recombined, "configured deployment overlay remains reusable: " + E.Code + " " + E.Message);
			if (Again && Recombined)
			{
				CheckConfiguration(Recombined);
			}
		}
		const auto Descriptor = Deployment / "veroscript-deployment.json";
		const auto Valid = Read(Descriptor);
		for (const auto& Invalid : std::vector<J>{J{}, false, 17, "not-an-object", J::array()})
		{
			auto Malformed = Valid;
			Malformed["packages"][0]["configuration"] = Invalid;
			Write(Descriptor, Malformed.dump());
			std::shared_ptr<const FInstalledEnvironment> Rejected;
			Expect(!Manager.LoadDeployment(NativePathText(Deployment), Rejected, E) && !Rejected &&
			        E.Code == "manifest-type" && E.Field == "deployment.package.configuration",
			    "deployment rejects nonobject configuration with an explicit diagnostic: " + E.Code);
		}
		Write(Descriptor, Valid.dump());
	}
	void InstalledPrereleaseTests(const fs::path& Root)
	{
		auto Area = Root / "installed-prerelease";
		auto Registry = std::make_shared<FRegistry>();
		auto Child = Manifest("prerelease-child");
		Child["version"] = "2.0.0-beta.1";
		Registry->Add(Area / "registry", Child);
		auto Parent = Manifest("prerelease-parent");
		Parent["version"] = "1.0.0";
		Parent["dependencies"]["prerelease-child"] = {{"version", ">=1.0.0"}};
		Registry->Add(Area / "registry", Parent);
		Registry->Add(Area / "registry", Manifest("prerelease-unused"));
		Registry->Add(Area / "registry", Manifest("prerelease-overlay"));
		auto Configuration = Detail::NewProject("prerelease-project");
		Configuration["packageManager"]["registries"]["official"] = {
		    {"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
		Configuration["dependencies"] = {{"prerelease-parent", {{"version", "*"}}},
		    {"prerelease-unused", {{"version", "*"}}}};
		FRequest Q;
		Q.Environment.Root = NativePathText(Area / "project");
		Q.Environment.CacheDirectory = NativePathText(Area / "cache");
		Q.Options.Transport = Registry;
		auto Requirements = U8Path(Q.Environment.Root) / "veroscript-project.json";
		Write(Requirements, Configuration.dump());
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "dependency-conflict" &&
		        Read(Requirements) == Configuration,
		    "ordinary installation still requires opt-in for a transitive prerelease");
		Q.Options.AllowPrerelease = true;
		auto Ok = Manager.Execute(Q, R, E);
		Expect(Ok && R.Environment && R.Environment->GetPackages().size() == 3,
		    "explicit prerelease install succeeds: " + E.Code + " " + E.Message);
		if (!Ok || !R.Environment) return;
		auto Initial = R.Environment;
		const auto InitialFingerprint = Initial->GetFingerprint();
		Q.Options.AllowPrerelease = false;
		Q.Options.Offline = true;
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"prerelease-unused"};
		auto Calls = Registry->Calls.size();
		Ok = Manager.Execute(Q, R, E);
		Expect(Ok && R.Environment && R.Environment->GetPackages().size() == 2,
		    "uninstall retains an installed transitive prerelease without opt-in: " + E.Code + " " + E.Message);
		Expect(Registry->Calls.size() == Calls, "prerelease pruning uses installed content without network access");
		if (!Ok || !R.Environment) return;
		auto Pruned = R.Environment;
		const auto PrunedFingerprint = Pruned->GetFingerprint();
		const auto FindChild = [](const std::shared_ptr<const FInstalledEnvironment>& Environment)
		{
			return std::find_if(Environment->GetPackages().begin(), Environment->GetPackages().end(),
			    [](const FInstalledPackage& P)
			{
				return P.Name == "prerelease-child";
			});
		};
		auto InitialChild = FindChild(Initial), PrunedChild = FindChild(Pruned);
		Expect(InitialChild != Initial->GetPackages().end() && PrunedChild != Pruned->GetPackages().end() &&
		        PrunedChild->Version == "2.0.0-beta.1" && PrunedChild->Sha256 == InitialChild->Sha256,
		    "pruning preserves the exact prerelease artifact identity");
		Q.Operation = EOperation::Install;
		Q.Arguments.clear();
		Q.Options.Offline = false;
		Q.Environment.Root = NativePathText(Area / "overlay");
		Configuration["dependencies"] = {{"prerelease-overlay", {{"version", "*"}}}};
		Write(U8Path(Q.Environment.Root) / "veroscript-project.json", Configuration.dump());
		Ok = Manager.Execute(Q, R, E);
		Expect(Ok && R.Environment && R.Environment->GetPackages().size() == 1,
		    "prepare unrelated stable overlay for prerelease composition: " + E.Code + " " + E.Message);
		if (!Ok || !R.Environment) return;
		auto Overlay = R.Environment;
		const auto OverlayFingerprint = Overlay->GetFingerprint();
		std::shared_ptr<const FInstalledEnvironment> Combined;
		Calls = Registry->Calls.size();
		Ok = Manager.Combine(Pruned, Overlay, Combined, E);
		Expect(Ok && Combined && Combined->GetPackages().size() == 3,
		    "composition retains an installed transitive prerelease without opt-in: " + E.Code + " " + E.Message);
		if (Ok && Combined)
		{
			auto CombinedChild = FindChild(Combined);
			Expect(CombinedChild != Combined->GetPackages().end() && PrunedChild != Pruned->GetPackages().end() &&
			        CombinedChild->Version == "2.0.0-beta.1" && CombinedChild->Sha256 == PrunedChild->Sha256,
			    "composition preserves the installed prerelease artifact identity");
		}
		Expect(Registry->Calls.size() == Calls, "prerelease composition never fetches a new candidate");
		Expect(Initial->GetFingerprint() == InitialFingerprint && Initial->GetPackages().size() == 3 &&
		        Pruned->GetFingerprint() == PrunedFingerprint && Pruned->GetPackages().size() == 2 &&
		        Overlay->GetFingerprint() == OverlayFingerprint && Overlay->GetPackages().size() == 1,
		    "pruning and composition preserve their original immutable snapshots");
	}
	void LockedDependencyArrayTests()
	{
		auto Parent = Manifest("locked-parent"), Child = Manifest("locked-child");
		Parent["dependencies"]["locked-child"] = {{"version", "*"}};
		const auto Package = [](const J& M, const J& Dependencies)
		{
			return J{{"name", M.at("name")}, {"version", M.at("version")}, {"manifest", M},
			    {"source", {{"id", "test.source"}}}, {"ownerId", "package.locked-array-test"},
			    {"artifact", {{"sha256", Detail::Id(M)}, {"manifestSha256", Detail::Id(M)}}},
			    {"dependencies", Dependencies}, {"usage", J::array({"runtime"})}};
		};
		J Lock = {{"format", "veroscript-lock"}, {"formatVersion", "0.1.1"},
		    {"target", Detail::TargetJson(HostTargetContext())},
		    {"packages", J::array({Package(Parent, J::array({"locked-child"})), Package(Child, J::array())})}};
		auto Project = Detail::NewProject("locked-array-project");
		Project["dependencies"]["locked-parent"] = {{"version", "*"}};
		FClientOptions Options;
		Options.Limits.MaxEdges = 2;
		Detail::FWork W{Options};
		const auto Graph = Detail::GraphFromLock(Lock, Project, W);
		Expect(Graph.Nodes.size() == 2 && Graph.Edges.at("locked-parent").size() == 1,
		    "lock graph accepts the exact root-plus-dependency edge budget");
		const auto Reject = [&](const J& Dependencies, std::size_t MaxEdges, const char* Code)
		{
			Lock["packages"][0]["dependencies"] = Dependencies;
			Options.Limits.MaxEdges = MaxEdges;
			Detail::FWork Work{Options};
			try
			{
				Detail::GraphFromLock(Lock, Project, Work);
				Expect(false, "invalid dependency array must not be silently normalized");
			}
			catch (const Detail::FFailure& E)
			{
				Expect(E.Diagnostic.Code == Code, "lock dependency array rejection: " + E.Diagnostic.Code);
			}
		};
		Reject(J::array({"locked-child", "locked-child"}), 2, "edge-limit");
		Reject(J::array({"locked-child", "locked-child"}), 3, "lock-invalid");
		Reject(J::array({"locked-child", 7}), 3, "lock-invalid");
		Reject(J::object(), 2, "lock-invalid");
	}
	void ArchivePublicationTests(const fs::path& Root)
	{
		auto Pending = Root / "new-file-pending", Existing = Root / "new-file-existing";
		Write(Pending, "new archive");
		Write(Existing, "keep existing output");
		const auto RejectExisting = [&](const fs::path& Destination)
		{
			try
			{
				Detail::MovePathNoReplace(Pending, Destination);
				Expect(false, "new-file commit must not replace an existing output");
			}
			catch (const Detail::FFailure& E)
			{
				Expect(E.Diagnostic.Code == "output-exists", "new-file commit reports an existing output");
			}
			Expect(Detail::Read(Pending, 100) == "new archive",
			    "failed new-file commit preserves the staged source bytes");
		};
		RejectExisting(Existing);
		Expect(Detail::Read(Existing, 100) == "keep existing output",
		    "failed new-file commit preserves existing destination bytes");
		auto ExistingDirectory = Root / "new-file-existing-directory";
		Write(ExistingDirectory / "marker", "keep directory contents");
		RejectExisting(ExistingDirectory);
		Expect(fs::is_directory(ExistingDirectory) &&
		        Detail::Read(ExistingDirectory / "marker", 100) == "keep directory contents",
		    "failed new-file commit preserves an existing directory and its contents");
		auto MissingTarget = Root / "new-file-missing-link-target", Link = Root / "new-file-dangling-link";
		const auto LinkTarget = MissingTarget.filename();
		std::error_code LinkError;
		fs::create_symlink(LinkTarget, Link, LinkError);
		#if !defined(_WIN32)
		Expect(!LinkError, "dangling symlink fixture can be created");
		#endif
		if (!LinkError)
		{
			RejectExisting(Link);
			Expect(fs::is_symlink(fs::symlink_status(Link)) && fs::read_symlink(Link) == LinkTarget &&
			        !fs::exists(MissingTarget),
			    "failed new-file commit preserves a dangling symlink and its target");
			fs::remove(Link);
		}
		auto Created = Root / "new-file-created";
		Detail::MovePathNoReplace(Pending, Created);
		Expect(!fs::exists(Pending) && Detail::Read(Created, 100) == "new archive",
		    "new-file commit publishes complete bytes and removes its staging name");
		const auto PendingDirectory = Root / "new-directory-pending";
		const auto EmptyDirectory = Root / "new-directory-existing";
		Write(PendingDirectory / "nested" / "marker", "staged directory contents");
		fs::create_directory(EmptyDirectory);
		try
		{
			Detail::MovePathNoReplace(PendingDirectory, EmptyDirectory);
			Expect(false, "directory commit must not replace an existing empty directory");
		}
		catch (const Detail::FFailure& E)
		{
			Expect(E.Diagnostic.Code == "output-exists", "directory commit reports an existing output");
		}
		Expect(fs::is_directory(EmptyDirectory) && fs::is_empty(EmptyDirectory) &&
		        Detail::Read(PendingDirectory / "nested" / "marker", 100) == "staged directory contents",
		    "failed directory commit preserves the empty destination and complete staged tree");
		const auto CreatedDirectory = Root / "new-directory-created";
		Detail::MovePathNoReplace(PendingDirectory, CreatedDirectory);
		Expect(!fs::exists(PendingDirectory) &&
		        Detail::Read(CreatedDirectory / "nested" / "marker", 100) == "staged directory contents",
		    "directory commit publishes the complete tree and removes its staging name");

		const fs::path Sources[] = {Root / "new-file-race-a", Root / "new-file-race-b"};
		const auto Destination = Root / "new-file-race-output";
		const std::string Contents[] = {"first writer", "second writer"};
		Write(Sources[0], Contents[0]);
		Write(Sources[1], Contents[1]);
		std::atomic<unsigned> Ready{0};
		std::atomic<bool> Start{false};
		std::string Outcomes[2];
		const auto Publish = [&](unsigned Index)
		{
			++Ready;
			while (!Start.load())
			{
				std::this_thread::yield();
			}
			try
			{
				Detail::MovePathNoReplace(Sources[Index], Destination);
				Outcomes[Index] = "committed";
			}
			catch (const Detail::FFailure& E)
			{
				Outcomes[Index] = E.Diagnostic.Code;
			}
			catch (...)
			{
				Outcomes[Index] = "unexpected-failure";
			}
		};
		std::thread First(Publish, 0), Second(Publish, 1);
		while (Ready.load() != 2)
		{
			std::this_thread::yield();
		}
		Start = true;
		First.join();
		Second.join();
		const bool FirstWon = Outcomes[0] == "committed" && Outcomes[1] == "output-exists";
		const bool SecondWon = Outcomes[1] == "committed" && Outcomes[0] == "output-exists";
		Expect(FirstWon || SecondWon, "concurrent new-file commits publish exactly one writer");
		if (FirstWon || SecondWon)
		{
			const auto Winner = FirstWon ? 0u : 1u, Loser = 1u - Winner;
			Expect(Detail::Read(Destination, 100) == Contents[Winner] && !fs::exists(Sources[Winner]) &&
			        Detail::Read(Sources[Loser], 100) == Contents[Loser],
			    "concurrent commit preserves the complete winning output and losing staging bytes");
		}
	}
	void ArchiveTests(const fs::path& Root)
	{
		FClientOptions Options;
		Detail::FWork Work{Options};
		auto Make =
		    [&](const std::string& Id, const std::vector<std::pair<std::string, std::string>>& Entries)
		{
			mz_zip_archive Z{};
			if (!mz_zip_writer_init_heap(&Z, 0, 0))
			{
				throw std::runtime_error("zip fixture initialization");
			}
			for (const auto& E : Entries)
			{
				if (!mz_zip_writer_add_mem(&Z, E.first.c_str(), E.second.data(), E.second.size(), 6))
				{
					throw std::runtime_error("zip fixture entry");
				}
			}
			void* P = nullptr;
			size_t N = 0;
			if (!mz_zip_writer_finalize_heap_archive(&Z, &P, &N))
			{
				throw std::runtime_error("zip fixture finalize");
			}
			auto A = Root / (Id + ".zip");
			Write(A, std::string(static_cast<const char*>(P), N));
			mz_free(P);
			mz_zip_writer_end(&Z);
			return A;
		};
		const auto Reject = [&](const fs::path& Archive, const std::string& Name)
		{
			try
			{
				Detail::Extract(Archive, Root / (Name + "-output"), Work);
				Expect(false, "hostile ZIP rejected: " + Name);
			}
			catch (const Detail::FFailure&)
			{
			}
		};
		Reject(Make("escape", {{"../escape", "bad"}}), "escape");
		Reject(Make("duplicate", {{"file", "one"}, {"FILE", "two"}}), "duplicate");
		Reject(Make("prefix", {{"a/b", "one"}, {"a", "two"}}), "prefix");
		Reject(Make("reserved", {{".vero-receipt.json", "bad"}}), "reserved");
		Reject(Make("managed", {{".veroscript/state.json", "bad"}}), "managed");
		Reject(Make("ratio", {{"huge", std::string(500000, 'x')}}), "ratio");
		#if defined(_WIN32)
		Reject(Make("unicode-alias", {{"Å/one", "one"}, {"å/two", "two"}}), "unicode-alias");
		#endif
		auto Vendor = Root / "repro-source";
		auto M = Manifest("repro-package");
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / "hello.txt", "stable content");
		auto A = Root / "repro-one.zip", B = Root / "repro-two.zip";
		Detail::Pack(Vendor, A, M, Work);
		Detail::Pack(Vendor, B, M, Work);
		Expect(Detail::HashFile(A, Work, 1 << 20) == Detail::HashFile(B, Work, 1 << 20),
		    "reproducible archive bytes");
		try
		{
			Detail::Pack(Vendor, A, M, Work);
			Expect(false, "pack must not replace a published output");
		}
		catch (const Detail::FFailure& E)
		{
			Expect(E.Diagnostic.Code == "output-exists" &&
			        Detail::HashFile(A, Work, 1 << 20) == Detail::HashFile(B, Work, 1 << 20),
			    "pack preserves an existing output archive without changing its bytes");
		}
		auto Frozen = Root / "frozen.zip";
		auto Digest = Detail::HashFile(A, Work, 1 << 20);
		Detail::SnapshotArchive(A, Frozen, Work, Digest, fs::file_size(A));
		Write(A, "modified original archive");
		Detail::Extract(Frozen, Root / "frozen-output", Work);
		Expect(Detail::Read(Root / U8Path("frozen-output/hello.txt"), 100) == "stable content",
		    "archive snapshot remains exact after original is modified");
		try
		{
			Detail::SnapshotArchive(A, Root / "bad-snapshot.zip", Work, Digest);
			Expect(false, "snapshot rejects changed archive bytes");
		}
		catch (const Detail::FFailure& X)
		{
			Expect(X.Diagnostic.Code == "artifact-digest", "snapshot verifies expected artifact hash");
		}
		Detail::Pack(Vendor, Vendor / "output.zip", M, Work);
		Expect(fs::is_regular_file(Vendor / "output.zip"),
		    "pack can write inside package root without including itself");
		auto Repetitive = Root / "repetitive-source";
		Write(Repetitive / "veroscript-package.json", M.dump());
		auto Repeated = std::string(500000, 'x');
		Write(Repetitive / "repeated.txt", Repeated);
		std::string Ordinary;
		for (unsigned I = 0; I < 1000; ++I)
		{
			Ordinary += "package resource line " + std::to_string(I) + "\n";
		}
		Write(Repetitive / "ordinary.txt", Ordinary);
		auto Packed = Root / "repetitive.vspkg", PackedAgain = Root / "repetitive.zip";
		Detail::Pack(Repetitive, Packed, M, Work);
		Detail::Pack(Repetitive, PackedAgain, M, Work);
		Expect(Detail::HashFile(Packed, Work, 1 << 20) == Detail::HashFile(PackedAgain, Work, 1 << 20),
		    "highly compressible packages remain reproducible across archive suffixes");
		Detail::Extract(Packed, Root / "repetitive-output", Work);
		Expect(Detail::Read(Root / U8Path("repetitive-output/repeated.txt"), 1 << 20) == Repeated &&
		        Detail::Read(Root / U8Path("repetitive-output/ordinary.txt"), 1 << 20) == Ordinary,
		    "pack output passes unchanged extraction limits for highly compressible content");
		auto CheckCompression = [&](const char* Name, unsigned Method)
		{
			mz_zip_archive Z{};
			auto Bytes = Detail::Read(Packed, 1 << 20);
			bool Good = mz_zip_reader_init_mem(&Z, Bytes.data(), Bytes.size(), 0) != 0;
			mz_zip_archive_file_stat S{};
			if (Good)
			{
				auto Index = mz_zip_reader_locate_file(&Z, Name, nullptr, 0);
				Good = Index >= 0 && mz_zip_reader_file_stat(&Z, static_cast<unsigned>(Index), &S) &&
				    S.m_method == Method;
			}
			mz_zip_reader_end(&Z);
			Expect(Good, "packing preserves suitable compression for " + std::string(Name));
		};
		CheckCompression("repeated.txt", 0);
		CheckCompression("ordinary.txt", MZ_DEFLATED);
		auto Strict = Options;
		Strict.Limits.MaxExpansionRatio = 1;
		Detail::FWork StrictWork{Strict};
		auto StrictArchive = Root / "repetitive-strict.vspkg";
		Detail::Pack(Repetitive, StrictArchive, M, StrictWork);
		Detail::Extract(StrictArchive, Root / "repetitive-strict-output", StrictWork);
		Expect(Detail::Read(Root / U8Path("repetitive-strict-output/repeated.txt"), 1 << 20) == Repeated,
		    "stored fallback honors an exact one-to-one expansion budget");
		auto Limited = Options;
		Limited.Limits.MaxArchiveBytes = 16384;
		Detail::FWork LimitedWork{Limited};
		auto TooSmall = Root / "repetitive-too-small.vspkg";
		try
		{
			Detail::Pack(Repetitive, TooSmall, M, LimitedWork);
			Expect(false, "stored content must still fit the archive byte budget");
		}
		catch (const Detail::FFailure& X)
		{
			Expect(X.Diagnostic.Code == "archive-limit" && !fs::exists(TooSmall),
			    "stored fallback respects archive budget without publishing partial output: " +
			        X.Diagnostic.Code);
		}
		Limited.Limits.MaxArchiveBytes = 100;
		Detail::FWork TinyWork{Limited};
		auto Tiny = Root / "repetitive-tiny.vspkg";
		try
		{
			Detail::Pack(Repetitive, Tiny, M, TinyWork);
			Expect(false, "initial compression must still fit the archive byte budget");
		}
		catch (const Detail::FFailure& X)
		{
			Expect(X.Diagnostic.Code == "archive-limit" && !fs::exists(Tiny),
			    "initial ZIP writer preserves callback budget failures: " + X.Diagnostic.Code);
		}
		const auto CheckCancellation =
		    [&](const std::string& Label,
		        const std::function<void(const fs::path&, const Detail::FWork&)>& Operation,
		        bool FileOutput)
		{
			auto Probe = Options;
			unsigned Calls = 0;
			Probe.CheckCancelled = [&]()
			{
				++Calls;
			};
			Detail::FWork Baseline{Probe};
			Operation(Root / (Label + "-baseline"), Baseline);
			auto Points = Calls;
			for (unsigned Point = 1; Point <= Points; ++Point)
			{
				Calls = 0;
				Probe.CheckCancelled = [&]()
				{
					if (++Calls == Point)
					{
						Detail::Fail("test-cancelled", "injected archive cancellation");
					}
				};
				Detail::FWork Cancelled{Probe};
				auto Output = Root / (Label + "-" + std::to_string(Point));
				try
				{
					Operation(Output, Cancelled);
					Expect(false, Label + " ignored cancellation at " + std::to_string(Point));
				}
				catch (const Detail::FFailure& X)
				{
					Expect(X.Diagnostic.Code == "test-cancelled" && (!FileOutput || !fs::exists(Output)),
					    Label + " propagates cancellation at " + std::to_string(Point) + ": " +
					        X.Diagnostic.Code);
				}
			}
		};
		for (bool Stored : {false, true})
		{
			auto Input = Root / (Stored ? "cancel-stored-source" : "cancel-compressed-source");
			Write(Input / "veroscript-package.json", M.dump());
			Write(Input / "data.txt", Stored ? std::string(50000, 'x') : "ordinary package resource");
			CheckCancellation(
			    Stored ? "pack-cancel-stored" : "pack-cancel-compressed",
			    [&](const fs::path& Output, const Detail::FWork& W)
			{
				Detail::Pack(Input, Output, M, W);
			},
			    true);
		}
		CheckCancellation(
		    "extract-cancel",
		    [&](const fs::path& Output, const Detail::FWork& W)
		{
			Detail::Extract(Packed, Output, W);
		},
		    false);
	}
	void CleanupIdentityTests(const fs::path& Root)
	{
		#if defined(_WIN32)
		auto Project = Root / "cleanup-identity-project", Vendor = Root / "cleanup-identity-source";
		auto Key = Detail::Hash("veroscript.lock");
		std::transform(Key.begin(), Key.end(), Key.begin(),
		    [](unsigned char C)
		{
			return char(std::toupper(C));
		});
		fs::create_directories(Project / ".veroscript" / "generated" / Key);
		Write(Vendor / "veroscript-package.json", Manifest("cleanup-identity").dump());
		Write(Vendor / "data.txt", "active package");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "cleanup-identity-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E) && R.Environment, "case-alias cleanup baseline: " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		auto Generated = U8Path(R.Environment->GetPackages().front().GeneratedPath);
		Q.Arguments.clear();
		Q.Operation = EOperation::Clean;
		Expect(Manager.Execute(Q, R, E) && fs::is_directory(Generated),
		    "cleanup preserves active generated directories with different physical spelling");
		std::shared_ptr<const FInstalledEnvironment> Loaded;
		Expect(Manager.Load(Q.Environment, Loaded, E),
		    "environment remains valid after alias-aware cleanup: " + E.Message);
		Loaded.reset();
		Q.Operation = EOperation::Uninstall;
		Q.Options.SkipHooks = true;
		Q.Arguments = {"cleanup-identity"};
		Expect(Manager.Execute(Q, R, E), "case-alias package uninstall succeeds: " + E.Message);
		Q.Operation = EOperation::Clean;
		Q.Options.SkipHooks = false;
		Q.Arguments.clear();
		Expect(Manager.Execute(Q, R, E) && !fs::exists(Generated),
		    "unreferenced aliased generated content can still be reclaimed");
		#else
		(void)Root;
		#endif
	}
	void CleanupRecoveryTests(const fs::path& Root)
	{
		const auto Vendor = Root / "cleanup-recovery-source";
		auto M = Manifest("cleanup-recovery");
		M["install"]["actions"] = J::array({{{"type", "copy"}, {"source", "payload.txt"}, {"destination", "nested/one.txt"}},
		    {{"type", "copy"}, {"source", "keep.txt"}, {"destination", "two.txt"}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / "payload.txt", "removable payload");
		Write(Vendor / "keep.txt", "remaining payload");
		for (const std::string Mode : {"partial-package", "partial-generated", "lease-tail", "empty-tail",
		         "unexpected-file", "changed-file", "owner-marker", "nonempty-lease", "receiptless-content", "unrecorded-tail"})
		{
			const auto Project = Root / ("cleanup-recovery-" + Mode);
			fs::create_directories(Project);
			FRequest Q;
			Q.Environment.Root = NativePathText(Project);
			Q.Environment.CacheDirectory = NativePathText(Root / "cleanup-recovery-cache");
			Q.Arguments = {NativePathText(Vendor)};
			FPackageManager Manager;
			FResult R;
			FDiagnostic E;
			const bool Installed = Manager.Execute(Q, R, E);
			Expect(Installed && R.Environment, "cleanup recovery baseline " + Mode + ": " + E.Message);
			if (!Installed || !R.Environment)
			{
				continue;
			}
			const auto Package = R.Environment->GetPackages().front();
			const bool Generated = Mode == "partial-generated";
			const auto Target = U8Path(Generated ? Package.GeneratedPath : Package.Path);
			const auto Payload = Target / (Generated ? "nested/one.txt" : "payload.txt");
			const auto Keep = Target / (Generated ? "two.txt" : "keep.txt");
			Q.Operation = EOperation::Uninstall;
			Q.Options.SkipHooks = true;
			Q.Arguments = {"cleanup-recovery"};
			const bool Uninstalled = Manager.Execute(Q, R, E);
			Expect(Uninstalled, "retire cleanup recovery fixture " + Mode + ": " + E.Message);
			if (!Uninstalled)
			{
				continue;
			}
			R = {};
			const auto StatePath = Project / U8Path(".veroscript/state.json");
			auto State = Read(StatePath);
			const auto Record = std::find_if(State.at("retired").begin(), State.at("retired").end(),
			    [&](const J& V)
			{
				return V.at("kind") == (Generated ? "generated" : "package");
			});
			Expect(Record != State.at("retired").end(), "retired recovery record exists " + Mode);
			if (Record == State.at("retired").end())
			{
				continue;
			}
			const auto Files = Record->at("inventory");
			const bool Tail = Mode == "lease-tail" || Mode == "empty-tail" || Mode == "unrecorded-tail";
			if (Tail)
			{
				for (auto I = Files.rbegin(); I != Files.rend(); ++I)
				{
					fs::remove(Target / U8Path(I->at("path").get<std::string>()));
				}
				fs::remove(Target / ".vero-receipt.json");
				if (Mode == "empty-tail")
				{
					fs::remove(Target / ".vero-lease");
				}
				if (Mode == "unrecorded-tail")
				{
					State["retired"].erase(Record);
					Write(StatePath, State.dump());
				}
			}
			else if (Mode == "receiptless-content")
			{
				fs::remove(Target / ".vero-receipt.json");
			}
			else
			{
				fs::remove(Payload);
				if (Mode == "unexpected-file")
				{
					Write(Target / "unknown.txt", "foreign file");
				}
				if (Mode == "changed-file")
				{
					Write(Keep, "modified payload");
				}
				if (Mode == "owner-marker")
				{
					Write(Target / ".vero-owner.json", "foreign metadata");
				}
				if (Mode == "nonempty-lease")
				{
					Write(Target / ".vero-lease", "foreign metadata");
				}
			}
			Q.Operation = EOperation::Clean;
			Q.Options.SkipHooks = false;
			Q.Arguments.clear();
			if (Mode == "partial-package")
			{
				auto Lease = std::make_shared<Detail::FFileLock>(Target / ".vero-lease", false, false, false);
				const bool LeasedClean = Manager.Execute(Q, R, E);
				Expect(LeasedClean && fs::exists(Keep), "partial cleanup respects the existing package lease");
			}
			const bool Cleaned = Manager.Execute(Q, R, E);
			const bool Reclaimed = Mode == "partial-package" || Mode == "partial-generated" ||
			    Mode == "lease-tail" || Mode == "empty-tail";
			Expect(Cleaned && fs::exists(Target) != Reclaimed,
			    "cleanup resumes only owned, matching retired content " + Mode + ": " + E.Code + " " + E.Message);
			if (!Reclaimed && !Tail)
			{
				Expect(fs::exists(Keep) && Detail::Read(Keep, 100) == (Mode == "changed-file" ? "modified payload" : "remaining payload"),
				    "rejected cleanup preserves remaining payload " + Mode);
				if (Mode == "unexpected-file")
				{
					Expect(Detail::Read(Target / "unknown.txt", 100) == "foreign file", "unknown retired file remains untouched");
				}
				if (Mode == "owner-marker")
				{
					Expect(Detail::Read(Target / ".vero-owner.json", 100) == "foreign metadata", "unexpected owner metadata remains untouched");
				}
				if (Mode == "nonempty-lease")
				{
					Expect(Detail::Read(Target / ".vero-lease", 100) == "foreign metadata", "nonempty retired lease remains untouched");
				}
			}
			const bool Repeated = Manager.Execute(Q, R, E);
			Expect(Repeated && fs::exists(Target) != Reclaimed, "cleanup recovery is repeatable " + Mode);
		}
	}
	void CrossTargetDeploymentTests(const fs::path& Root)
	{
		const auto Area = Root / U8Path("cross-target 部署");
		const auto RegistryRoot = Area / "registry";
		const auto Project = Area / "project";
		const auto Output = Area / "deployment";
		auto Target = HostTargetContext();
		Target.Os = Target.Os == "linux" ? "windows" : "linux";
		Target.Environment = Target.Os == "linux" ? "gnu" : "msvc";
		Target.CoreBuild = std::string(64, 'c');
		const J TargetRequirement = {{"os", Target.Os}, {"arch", Target.Architecture},
		    {"environment", Target.Environment}, {"coreBuild", Target.CoreBuild},
		    {"abis", {{"native-extension", Target.Abis.at("native-extension")}}}};
		const auto Registry = std::make_shared<FRegistry>();
		auto Support = Manifest("target-support");
		Support["target"] = TargetRequirement;
		Support["entries"]["libraries"] = J::array({
		    J{{"id", "support"}, {"path", "native/support.bin"}, {"linkage", "dynamic"}}});
		Write(RegistryRoot / U8Path("target-support-0.1.1/native/support.bin"), "foreign library bytes");
		Registry->Add(RegistryRoot, Support);
		auto Package = Manifest("target-package");
		Package["target"] = TargetRequirement;
		Package["requires"]["core"] = Target.Core;
		Package["dependencies"]["target-support"] = {{"version", "0.1.1"}};
		Package["resources"]["data"] = "data";
		Package["entries"]["native"] = J::array({J{{"id", "extension"}, {"componentVersion", "0.1.1"},
		    {"path", "native/extension.bin"}, {"linkage", "dynamic"}, {"roles", J::array({"bindings"})},
		    {"libraries", J::array({"target-support:support"})}}});
		Write(RegistryRoot / U8Path("target-package-0.1.1/native/extension.bin"), "foreign extension bytes");
		Write(RegistryRoot / U8Path("target-package-0.1.1/data/内容.txt"), "target resource");
		Registry->Add(RegistryRoot, Package);
		auto Configuration = Detail::NewProject("target-deployment");
		Configuration["packageManager"]["registries"]["official"] =
		    {{"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
		Write(Project / "veroscript-project.json", Configuration.dump());

		FPackageManager Manager;
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Area / "cache");
		Q.Environment.Target = Target;
		Q.Options.Transport = Registry;
		Q.Arguments = {"target-package"};
		FResult R;
		FDiagnostic E;
		const bool Installed = Manager.Execute(Q, R, E);
		Expect(Installed && R.Environment, "install foreign target artifact data: " + E.Code + " " + E.Message);
		if (!Installed || !R.Environment) return;

		std::string Snapshot;
		Expect(!R.Environment->Deploy(NativePathText(Output), {"runtime"}, {}, Snapshot, E)
		        && E.Code == "deploy-cross-target" && !fs::exists(Output),
		    "implicit host export rejects foreign target");
		auto WrongTarget = Target;
		WrongTarget.CoreBuild = std::string(64, 'd');
		Expect(!R.Environment->DeployForTarget(WrongTarget, NativePathText(Output), {"runtime"}, {}, Snapshot, E)
		        && E.Code == "deploy-cross-target" && !fs::exists(Output),
		    "explicit export requires the installed target identity");

		Q.Operation = EOperation::Deploy;
		Q.Arguments.clear();
		Q.OutputPath = NativePathText(Output);
		const bool Exported = Manager.Execute(Q, R, E);
		Expect(Exported, "export the foreign target without loading its native files: " + E.Code + " " + E.Message);
		if (!Exported) return;
		const auto Record = Read(Output / "veroscript-deployment.json");
		Expect(Record.at("target") == Detail::TargetJson(Target) && Record.at("packages").size() == 2,
		    "exported target identity and transitive dependency");
		for (const auto& Item : Record.at("packages"))
		{
			if (Item.at("name") == "target-package")
			{
				const auto Content = Output / U8Path(Item.at("path").get<std::string>());
				const auto Generated = Output / U8Path(Item.at("generated").get<std::string>());
				const auto& Loader = Item.at("loaders").at("native:extension");
				Expect(Detail::Read(Content / U8Path("data/内容.txt"), 100) == "target resource"
				        && Detail::Read(Generated / U8Path(Loader.at("path").get<std::string>()), 100) == "foreign extension bytes"
				        && Detail::Read(Generated / U8Path(Loader.at("libraries").at(0).get<std::string>()), 100) == "foreign library bytes",
				    "foreign deployment preserves native closure and Unicode resource bytes");
			}
		}
		std::shared_ptr<const FInstalledEnvironment> Loaded;
		Expect(!Manager.LoadDeployment(NativePathText(Output), Loaded, E) && !Loaded && E.Code == "deployment-target",
		    "the exporting host cannot open the foreign deployment for execution");

		for (const auto& InstalledPackage : R.Environment->GetPackages())
		{
			if (InstalledPackage.Name == "target-package")
			{
				Write(U8Path(InstalledPackage.Path) / U8Path("data/内容.txt"), "corrupt");
			}
		}
		const auto Rejected = Area / "rejected";
		Expect(!R.Environment->DeployForTarget(Target, NativePathText(Rejected), {"runtime"}, {}, Snapshot, E)
		        && !fs::exists(Rejected) && Snapshot.empty(),
		    "corrupt foreign inventory is rejected before publication");
	}

	void DeploymentLimitsTests(const fs::path& Root)
	{
		const auto Registry = std::make_shared<FRegistry>();
		const auto RegistryRoot = Root / "deployment-limits-registry";
		Registry->Add(RegistryRoot, Manifest("deployment-limit-leaf"));
		auto Middle = Manifest("deployment-limit-middle");
		Middle["dependencies"]["deployment-limit-leaf"] = {{"version", "0.1.1"}};
		Registry->Add(RegistryRoot, Middle);
		auto Top = Manifest("deployment-limit-root");
		Top["dependencies"]["deployment-limit-middle"] = {{"version", "0.1.1"}};
		Registry->Add(RegistryRoot, Top);
		const auto Project = Root / "deployment-limits-project", Deployment = Root / "deployment-limits-output";
		auto ProjectData = Detail::NewProject("deployment-limits-project");
		ProjectData["packageManager"]["registries"]["official"] =
		    {{"id", "test.source"}, {"api", "https://fixture.invalid/api"}};
		Write(Project / "veroscript-project.json", ProjectData.dump());
		FPackageManager Manager;
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "deployment-limits-cache");
		Q.Options.Transport = Registry;
		Q.Arguments = {"deployment-limit-root"};
		FResult R;
		FDiagnostic E;
		const bool Installed = Manager.Execute(Q, R, E);
		Expect(Installed && R.Environment, "deployment limits install baseline: " + E.Message);
		if (!Installed || !R.Environment) return;
		const auto ConcurrentOutput = Root / "deployment-concurrent-output";
		FClientOptions ConcurrentOptions;
		bool CreatedOutput = false;
		ConcurrentOptions.CheckCancelled = [&]
		{
			if (!CreatedOutput)
			{
				CreatedOutput = true;
				fs::create_directory(ConcurrentOutput);
			}
		};
		std::string ConcurrentSnapshot;
		Expect(!R.Environment->Deploy(NativePathText(ConcurrentOutput), {"runtime"}, {}, ConcurrentSnapshot, E,
		           ConcurrentOptions) &&
		        E.Code == "output-exists" && CreatedOutput &&
		        fs::is_directory(ConcurrentOutput) && fs::is_empty(ConcurrentOutput) && ConcurrentSnapshot.empty(),
		    "deployment does not replace an empty directory created during staging: " + E.Code);
		unsigned TrailingIndex = 0;
		for (const auto Separator : std::set<char>{'/', static_cast<char>(fs::path::preferred_separator)})
		{
			const auto Directory = Root / ("deployment-trailing-output-" + std::to_string(TrailingIndex++));
			const auto OutputPath = NativePathText(Directory) + Separator;
			std::string TrailingSnapshot;
			const bool TrailingExported = R.Environment->Deploy(OutputPath, {"runtime"}, {}, TrailingSnapshot, E);
			Expect(TrailingExported && !TrailingSnapshot.empty() &&
			        fs::is_regular_file(Directory / "veroscript-deployment.json"),
			    "deployment accepts a trailing directory separator: " + E.Code + " " + E.Message);
			if (TrailingExported)
			{
				std::shared_ptr<const FInstalledEnvironment> Reloaded;
				Expect(Manager.LoadDeployment(OutputPath, Reloaded, E) && Reloaded &&
				        Reloaded->GetPackages().size() == 3,
				    "deployment with a trailing separator reloads successfully: " + E.Code + " " + E.Message);
			}
		}
		std::string Snapshot;
		const bool Exported = R.Environment->Deploy(NativePathText(Deployment), {"runtime"}, {}, Snapshot, E);
		Expect(Exported, "deployment limits export baseline: " + E.Message);
		if (!Exported) return;
		std::shared_ptr<const FInstalledEnvironment> Loaded;
		FClientOptions Options;
		Options.Limits.MaxEdges = 2;
		const bool AtBudget = Manager.LoadDeployment(NativePathText(Deployment), Loaded, E, Options);
		Expect(AtBudget && Loaded && Loaded->GetPackages().size() == 3,
		    "deployment runtime subset does not double-charge the edge budget: " + E.Message);
		Loaded.reset();
		Options.Limits.MaxEdges = 1;
		const bool OverBudget = Manager.LoadDeployment(NativePathText(Deployment), Loaded, E, Options);
		Expect(!OverBudget && !Loaded && E.Code == "deployment-limit", "deployment enforces the total edge budget");
		Options.Limits = {};
		const auto Descriptor = Deployment / "veroscript-deployment.json";
		const auto Baseline = Read(Descriptor);
		for (const std::string Mode : {"duplicate-dependency", "duplicate-runtime", "missing-runtime", "invalid-array"})
		{
			auto Changed = Baseline;
			auto Package = std::find_if(Changed["packages"].begin(), Changed["packages"].end(),
			    [](const J& P)
			{
				return P.at("name") == "deployment-limit-root";
			});
			if (Mode == "duplicate-dependency")
			{
				Package->at("dependencies").push_back("deployment-limit-middle");
			}
			if (Mode == "duplicate-runtime")
			{
				Package->at("runtimeDependencies").push_back("deployment-limit-middle");
			}
			if (Mode == "missing-runtime")
			{
				Package->at("runtimeDependencies").push_back("deployment-limit-leaf");
			}
			if (Mode == "invalid-array")
			{
				(*Package)["dependencies"] = J::object();
			}
			Write(Descriptor, Changed.dump());
			const bool Accepted = Manager.LoadDeployment(NativePathText(Deployment), Loaded, E, Options);
			Expect(!Accepted && !Loaded && E.Code == "deployment-dependency", "deployment rejects invalid dependency records " + Mode);
		}
		Write(Descriptor, Baseline.dump());
		std::size_t Checks = 0;
		Options.CheckCancelled = [&]
		{
			++Checks;
		};
		const bool Counted = Manager.LoadDeployment(NativePathText(Deployment), Loaded, E, Options);
		Expect(Counted && Checks > 2, "deployment cancellation probe baseline");
		Loaded.reset();
		const auto CancelAt = Checks > 2 ? Checks - 1 : 1;
		Checks = 0;
		Options.CheckCancelled = [&]
		{
			if (++Checks == CancelAt)
			{
				Detail::Fail("test-deployment-cancelled", "cancel during deployment validation");
			}
		};
		const bool Cancelled = Manager.LoadDeployment(NativePathText(Deployment), Loaded, E, Options);
		Expect(!Cancelled && !Loaded && E.Code == "test-deployment-cancelled", "late deployment validation remains cancellable");
	}
	void TransactionTests(const fs::path& Root)
	{
		auto Project = Root / "transaction-project", Vendor = Root / "transaction-source";
		fs::create_directories(Project);
		auto M = Manifest("transaction-data");
		M["localization"] = {{"root", "locale"}, {"baseCulture", "en-US"}};
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / U8Path("locale/en-US/messages.json"),
		    J{{"install.before", "transaction-start"}}.dump());
		Write(Vendor / "data.txt", "original");
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "transaction-cache");
		Q.Arguments = {NativePathText(Vendor)};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E), "transaction baseline");
		auto Requirements = Read(Project / "veroscript-project.json"), Lock = Read(Project / "veroscript.lock"),
		     State = Read(Project / U8Path(".veroscript/state.json"));
		Write(Vendor / "data.txt", "changed");
		Q.Arguments.clear();
		Q.Options.CheckCancelled = [&]()
		{
			if (fs::exists(Project / U8Path(".veroscript/transaction.json")))
			{
				Detail::Fail("test-cancelled", "cancel during atomic publication");
			}
		};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "test-cancelled",
		    "cancellation during journal application: " + E.Message);
		Expect(Read(Project / "veroscript-project.json") == Requirements &&
		        Read(Project / "veroscript.lock") == Lock &&
		        Read(Project / U8Path(".veroscript/state.json")) == State,
		    "journal rollback restores all prior records");
		Expect(!fs::exists(Project / U8Path(".veroscript/transaction.json")),
		    "successful rollback closes journal");
		Q.Options.CheckCancelled = {};
		Expect(Manager.Execute(Q, R, E),
		    "retry after cancellation succeeds without staging collision: " + E.Message);
		Lock = Read(Project / "veroscript.lock");
		State = Read(Project / U8Path(".veroscript/state.json"));
		auto Edited = Requirements;
		Edited["packages"]["transaction-data"]["configuration"]["externalEdit"] = true;
		Q.Options.Message = [&](const std::string&)
		{
			Write(Project / "veroscript-project.json", Edited.dump());
		};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "environment-changed",
		    "concurrent requirements edit rejects stale transaction: " + E.Code + " " + E.Message);
		Expect(Read(Project / "veroscript-project.json") == Edited && Read(Project / "veroscript.lock") == Lock &&
		        Read(Project / U8Path(".veroscript/state.json")) == State &&
		        !fs::exists(Project / U8Path(".veroscript/transaction.json")),
		    "conflict preserves external edit and committed installation records");
		Q.Options.Message = {};
		Expect(Manager.Execute(Q, R, E), "retry uses external edit: " + E.Message);
		auto Layout = R.Environment->GetLayout();
		for (const auto& Path :
		    {U8Path(Layout.Lockfile), U8Path(Layout.State), U8Path(Layout.Packages) / ".vero-owner.json"})
		{
			auto Original = Detail::Read(Path, 1 << 24);
			Q.Options.Message = [&](const std::string&)
			{
				Write(Path, Original + "\n");
			};
			Expect(!Manager.Execute(Q, R, E) && E.Code == "environment-changed",
			    "concurrent lock/state/ownership edit invalidates transaction");
			Expect(Detail::Read(Path, 1 << 24) == Original + "\n", "conflict preserves edited record bytes");
			Write(Path, Original);
		}
		Q.Options.Message = {};
		const auto JournalPath = U8Path(Layout.Management) / "transaction.json";
		J Completed = {{"format", "veroscript-package-transaction"}, {"formatVersion", VeroScript::Packages::FormatVersion},
		    {"management", Layout.Management}, {"phase", "committed"}, {"writes", J::array()}};
		std::map<fs::path, std::string> CommittedRecords;
		for (const auto& Path : {U8Path(Layout.Requirements), U8Path(Layout.Lockfile), U8Path(Layout.State),
		         U8Path(Layout.Packages) / ".vero-owner.json"})
		{
			const auto Bytes = Detail::Read(Path, 1 << 24);
			CommittedRecords.emplace(Path, Bytes);
			Completed["writes"].push_back({{"path", NativePathText(Path)}, {"old", Bytes + "\n"}, {"new", Bytes}});
		}
		const auto Generation = Read(U8Path(Layout.State)).at("generation").get<std::uint64_t>();
		const auto CheckReadOnlyJournal = [&](const J& Value, const std::string& Code)
		{
			const auto Bytes = Value.dump();
			Write(JournalPath, Bytes);
			std::map<fs::path, std::string> Before;
			for (const auto& Entry : CommittedRecords)
			{
				Before[Entry.first] = Detail::Read(Entry.first, 1 << 24);
			}
			std::shared_ptr<const FInstalledEnvironment> Loaded;
			const bool LoadedOk = Manager.Load(Q.Environment, Loaded, E, Q.Options);
			Expect(Code.empty() ? LoadedOk && Loaded && Loaded->GetGeneration() == Generation
			                    : !LoadedOk && !Loaded && E.Code == Code,
			    "read-only committed journal validation: " + Code + " " + E.Code + " " + E.Message);
			Expect(fs::exists(JournalPath) && Detail::Read(JournalPath, 1 << 24) == Bytes,
			    "read-only loading neither rewrites nor deletes the journal");
			for (const auto& Entry : Before)
			{
				Expect(Detail::Read(Entry.first, 1 << 24) == Entry.second, "read-only loading preserves management bytes");
			}
		};
		CheckReadOnlyJournal(Completed, {});
		for (const auto* Phase : {"applying", "rollback"})
		{
			auto Pending = Completed;
			Pending["phase"] = Phase;
			CheckReadOnlyJournal(Pending, "recovery-required");
		}
		const auto LockPath = U8Path(Layout.Lockfile);
		Write(LockPath, CommittedRecords.at(LockPath) + "\n");
		CheckReadOnlyJournal(Completed, "recovery-required");
		Write(LockPath, CommittedRecords.at(LockPath) + "\n\n");
		CheckReadOnlyJournal(Completed, "recovery-conflict");
		Write(LockPath, CommittedRecords.at(LockPath));
		auto InvalidJournal = Completed;
		InvalidJournal["management"] = NativePathText(Root / "foreign-journal-management");
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		InvalidJournal = Completed;
		InvalidJournal["formatVersion"] = "invalid";
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		InvalidJournal = Completed;
		InvalidJournal["writes"][0]["path"] = NativePathText(Root / "foreign-journal-destination");
		CheckReadOnlyJournal(InvalidJournal, "recovery-layout");
		InvalidJournal = Completed;
		InvalidJournal["writes"][1] = InvalidJournal["writes"][0];
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		InvalidJournal = Completed;
		InvalidJournal["writes"][0]["old"] = J::array();
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		InvalidJournal = Completed;
		InvalidJournal["writes"][0].erase("new");
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		InvalidJournal = Completed;
		InvalidJournal["writes"] = J::object();
		CheckReadOnlyJournal(InvalidJournal, "recovery-invalid");
		CheckReadOnlyJournal(Completed, {});
		FRunEnvironmentRequest RunRequest;
		RunRequest.StartupDirectory = NativePathText(Project);
		RunRequest.EntryFile = NativePathText(Project / "main.vs");
		RunRequest.WorkspaceBoundary = NativePathText(Project);
		RunRequest.Options = Q.Options;
		for (const bool Explicit : {false, true})
		{
			RunRequest.Selection.Path = Explicit ? NativePathText(Project) : std::string{};
			std::shared_ptr<const FInstalledEnvironment> Loaded;
			Expect(ResolveRunEnvironment(RunRequest, Loaded, E) && Loaded && Loaded->GetGeneration() == Generation,
			    "run selection accepts a fully applied committed journal: " + E.Code + " " + E.Message);
			Expect(Detail::Read(JournalPath, 1 << 24) == Completed.dump(),
			    "run selection leaves the committed journal unchanged");
		}
		fs::remove(JournalPath);
		auto CurrentRequirements = Read(Project / "veroscript-project.json");
		auto RelocatedRequirements = CurrentRequirements;
		RelocatedRequirements["packageManager"]["directory"] =
		    NativePathText(Root / "transaction-clean-relocation");
		Write(Project / "veroscript-project.json", RelocatedRequirements.dump());
		Q.Operation = EOperation::Clean;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "store-relocation",
		    "cleanup cannot use a different store from committed installation state");
		Write(Project / "veroscript-project.json", CurrentRequirements.dump());
		Q.Operation = EOperation::Install;
		auto Relocated = Root / "transaction-store-race";
		auto Fresh = Root / "transaction-store-project";
		fs::create_directories(Fresh);
		auto FreshRequirements = Detail::NewProject("transaction-store-project");
		Write(Fresh / "veroscript-project.json", FreshRequirements.dump());
		Q.Environment.Root = NativePathText(Fresh);
		Q.Arguments = {NativePathText(Vendor)};
		bool ChangedStore = false;
		Q.Options.CheckCancelled = [&]()
		{
			if (!ChangedStore && fs::exists(Fresh / U8Path(".veroscript/manager.lock")))
			{
				ChangedStore = true;
				FreshRequirements["packageManager"]["directory"] = NativePathText(Relocated);
				Write(Fresh / "veroscript-project.json", FreshRequirements.dump());
			}
		};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "environment-changed" && ChangedStore &&
		        !fs::exists(Relocated),
		    "changed package root cannot bypass the acquired store lock");
		Q.Options.CheckCancelled = {};
		auto BudgetProject = Root / "record-budget-project";
		fs::create_directories(BudgetProject);
		Write(BudgetProject / "veroscript-project.json", Detail::NewProject("record-budget").dump());
		Q.Environment.Root = NativePathText(BudgetProject);
		Q.Options.Limits.MaxManifestBytes = 256;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "state-limit" &&
		        !fs::exists(BudgetProject / "veroscript.lock"),
		    "expanded requirements cannot exceed the manifest read budget at commit: " + E.Code);
		auto CollisionProject = Root / "layout-collision-project";
		fs::create_directories(CollisionProject);
		auto CollisionRequirements = Detail::NewProject("layout-collision").dump();
		Write(CollisionProject / "veroscript-project.json", CollisionRequirements);
		Q.Environment.Root = NativePathText(CollisionProject);
		Q.Options.Limits = {};
		for (const auto* Relative :
		    {"veroscript-project.json", ".veroscript/state.json", ".veroscript/transaction.json",
		        ".veroscript/manager.lock", ".veroscript/packages/.vero-owner.json",
		        ".veroscript/packages/.vero-store.lock"})
		{
			Q.Environment.Lockfile = NativePathText(CollisionProject / U8Path(Relative));
			Expect(!Manager.Execute(Q, R, E) && E.Code == "layout-conflict" &&
			        Detail::Read(CollisionProject / "veroscript-project.json", 1 << 20) == CollisionRequirements &&
			        !fs::exists(CollisionProject / U8Path(".veroscript/state.json")),
			    "custom lockfile cannot overwrite environment or synchronization records");
		}
	}

	void JournalRecoveryTests(const fs::path& Root)
	{
		FEnvironmentInput Input;
		Input.Root = NativePathText(Root / "journal-recovery-project");
		Input.CacheDirectory = NativePathText(Root / "journal-recovery-cache");
		fs::create_directories(U8Path(Input.Root));
		FEnvironmentLayout L;
		FDiagnostic Error;
		const auto Resolved = ResolveEnvironment(Input, L, Error);
		Expect(Resolved, "resolve journal recovery fixture: " + Error.Message);
		if (!Resolved) return;
		const auto Requirements = U8Path(L.Requirements), Lock = U8Path(L.Lockfile), State = U8Path(L.State);
		const auto File = U8Path(L.Management) / "transaction.json";
		const auto Foreign = Root / "journal-foreign.txt";
		Write(Foreign, "foreign");
		FClientOptions Options;
		Detail::FWork Work{Options};
		const J Writes = J::array({{{"path", NativePathText(Requirements)}, {"old", "old requirements"}, {"new", "new requirements"}},
		    {{"path", NativePathText(Lock)}, {"old", "old lock"}, {"new", "new lock"}},
		    {{"path", NativePathText(State)}, {"old", nullptr}, {"new", "new state"}}});
		const auto Journal = [&](const char* Phase)
		{
			return J{{"format", "veroscript-package-transaction"}, {"formatVersion", VeroScript::Packages::FormatVersion},
			    {"management", L.Management}, {"phase", Phase}, {"writes", Writes}};
		};
		for (const auto* Phase : {"applying", "rollback", "committed"})
		{
			const bool New = std::string(Phase) == "committed";
			for (const bool Created : {false, true})
			{
				Write(Requirements, "new requirements");
				Write(Lock, "old lock");
				if (Created)
				{
					Write(State, "new state");
				}
				else
				{
					fs::remove(State);
				}
				Write(File, Journal(Phase).dump());
				Detail::Recover(L, Work);
				Expect(Detail::Read(Requirements, 100) == (New ? "new requirements" : "old requirements") &&
				        Detail::Read(Lock, 100) == (New ? "new lock" : "old lock") && !fs::exists(File) &&
				        (New ? fs::exists(State) && Detail::Read(State, 100) == "new state" : !fs::exists(State)),
				    std::string("partial journal recovery: ") + Phase);
				Detail::Recover(L, Work);
			}
		}
		const auto Reject = [&](const J& Value, const std::string& Code)
		{
			const auto BeforeRequirements = Detail::Read(Requirements, 100), BeforeLock = Detail::Read(Lock, 100);
			const bool HadState = fs::exists(State);
			const auto BeforeState = HadState ? Detail::Read(State, 100) : std::string{};
			const auto Bytes = Value.dump();
			Write(File, Bytes);
			bool Rejected = false;
			try
			{
				Detail::Recover(L, Work);
			}
			catch (const Detail::FFailure& Failure)
			{
				Rejected = Failure.Diagnostic.Code == Code;
			}
			Expect(Rejected && fs::exists(File) && Detail::Read(File, 1 << 20) == Bytes &&
			        Detail::Read(Requirements, 100) == BeforeRequirements && Detail::Read(Lock, 100) == BeforeLock &&
			        fs::exists(State) == HadState && (!HadState || Detail::Read(State, 100) == BeforeState) &&
			        Detail::Read(Foreign, 100) == "foreign",
			    "journal rejects without partial replay: " + Code);
		};
		for (const auto* Phase : {"applying", "rollback", "committed"})
		{
			Write(Requirements, std::string(Phase) == "committed" ? "old requirements" : "new requirements");
			Write(Lock, "external lock edit");
			Reject(Journal(Phase), "recovery-conflict");
			Write(Lock, "old lock");
			Write(State, "external state edit");
			Reject(Journal(Phase), "recovery-conflict");
			Write(State, "new state");
			const auto Value = Journal(Phase);
			for (const auto* Key : {"old", "new"})
			{
				auto Invalid = Value;
				Invalid["writes"][1][Key] = J::array();
				Reject(Invalid, "recovery-invalid");
				Invalid = Value;
				Invalid["writes"][1].erase(Key);
				Reject(Invalid, "recovery-invalid");
			}
			auto Invalid = Value;
			Invalid["writes"][1] = Invalid["writes"][0];
			Reject(Invalid, "recovery-invalid");
			Invalid = Value;
			Invalid["writes"][1]["path"] = NativePathText(Foreign);
			Reject(Invalid, "recovery-layout");
			Invalid = Value;
			Invalid["writes"] = J::object();
			Reject(Invalid, "recovery-invalid");
			Invalid = Value;
			Invalid["writes"][1]["new"] = std::string(65, 'x');
			Options.Limits.MaxStateBytes = 64;
			Reject(Invalid, "recovery-invalid");
			Options.Limits.MaxStateBytes = FLimits{}.MaxStateBytes;
		}
		fs::remove(File);
		Write(Requirements, "old requirements");
		Write(Lock, "old lock");
		fs::remove(State);
		const auto Expected = Detail::CaptureManagementSnapshot(L, Work);
		bool DuplicateRejected = false;
		try
		{
			Detail::Commit(L, {{Requirements, "new requirements"}, {Requirements, "another value"}}, Expected, Work);
		}
		catch (const Detail::FFailure& Failure)
		{
			DuplicateRejected = Failure.Diagnostic.Code == "transaction-path";
		}
		Expect(DuplicateRejected && !fs::exists(File) && Detail::Read(Requirements, 100) == "old requirements",
		    "duplicate commit destinations are rejected before publishing a journal");
		bool Edited = false;
		Options.CheckCancelled = [&]
		{
			if (!Edited && fs::exists(File) && Detail::Read(Requirements, 100) == "new requirements")
			{
				Edited = true;
				Write(Lock, "external lock edit");
			}
		};
		bool NeedsRecovery = false;
		try
		{
			Detail::Commit(L, {{Requirements, "new requirements"}, {Lock, "new lock"}}, Expected, Work);
		}
		catch (const Detail::FFailure& Failure)
		{
			NeedsRecovery = Failure.Diagnostic.Code == "recovery-required";
		}
		Expect(NeedsRecovery && Edited && fs::exists(File) && Read(File)["phase"] == "rollback" &&
		        Detail::Read(Lock, 100) == "external lock edit",
		    "commit and rollback retain a mid-transaction external edit");
		Options.CheckCancelled = {};
		Write(Lock, "old lock");
		Detail::Recover(L, Work);
		Expect(!fs::exists(File) && Detail::Read(Requirements, 100) == "old requirements" &&
		        Detail::Read(Lock, 100) == "old lock",
		    "recovery finishes after the external conflict is resolved");
		const auto ApplyingBytes = Journal("applying").dump().size();
		const auto CommittedBytes = Journal("committed").dump().size();
		Expect(CommittedBytes == ApplyingBytes + 1, "committed journal requires one extra byte");
		const auto BoundarySnapshot = Detail::CaptureManagementSnapshot(L, Work);
		const std::vector<std::pair<fs::path, std::string>> BoundaryWrites = {
		    {Requirements, "new requirements"}, {Lock, "new lock"}, {State, "new state"}};
		Options.Limits.MaxJournalBytes = ApplyingBytes;
		bool BoundaryRejected = false;
		try
		{
			Detail::Commit(L, BoundaryWrites, BoundarySnapshot, Work);
		}
		catch (const Detail::FFailure& Failure)
		{
			BoundaryRejected = Failure.Diagnostic.Code == "journal-limit";
		}
		Expect(BoundaryRejected && !fs::exists(File) && !fs::exists(State) &&
		        Detail::Read(Requirements, 100) == "old requirements" && Detail::Read(Lock, 100) == "old lock",
		    "applying-only journal budget rejects before publishing or changing records");
		Options.Limits.MaxJournalBytes = CommittedBytes;
		Detail::Commit(L, BoundaryWrites, BoundarySnapshot, Work);
		Expect(!fs::exists(File) && Detail::Read(Requirements, 100) == "new requirements" &&
		        Detail::Read(Lock, 100) == "new lock" && Detail::Read(State, 100) == "new state",
		    "exact committed journal budget accepts the complete transaction");
		Options.Limits.MaxJournalBytes = FLimits{}.MaxJournalBytes;
		const auto Owner = U8Path(L.Packages) / ".vero-owner.json";
		for (const bool CreateMissing : {false, true})
		{
			Write(State, "retained old state");
			Write(Lock, "retained old lock");
			fs::remove(Owner);
			const auto RetainedSnapshot = Detail::CaptureManagementSnapshot(L, Work);
			bool Changed = false;
			Options.CheckCancelled = [&]
			{
				if (!Changed && fs::exists(File) && Detail::Read(State, 100) == "retained new state")
				{
					Changed = true;
					Write(CreateMissing ? Owner : Lock, CreateMissing ? "external owner" : "external lock edit");
				}
			};
			bool RetainedRejected = false;
			try
			{
				Detail::Commit(L, {{State, "retained new state"}}, RetainedSnapshot, Work);
			}
			catch (const Detail::FFailure& Failure)
			{
				RetainedRejected = Failure.Diagnostic.Code == "environment-changed";
			}
			Options.CheckCancelled = {};
			Expect(RetainedRejected && Changed && !fs::exists(File) &&
			        Detail::Read(State, 100) == "retained old state" &&
			        Detail::Read(Lock, 100) == (CreateMissing ? "retained old lock" : "external lock edit") &&
			        fs::exists(Owner) == CreateMissing && (!CreateMissing || Detail::Read(Owner, 100) == "external owner"),
			    "replay detects changed or newly created retained records and rolls back only its own state write");
		}
		for (const bool RestoreNew : {false, true})
		{
			Write(Requirements, "early old requirements");
			Write(State, "later old state");
			const auto WrittenSnapshot = Detail::CaptureManagementSnapshot(L, Work);
			bool Changed = false;
			Options.CheckCancelled = [&]
			{
				if (!Changed && fs::exists(File) && Detail::Read(Requirements, 100) == "early new requirements")
				{
					Changed = true;
					Write(Requirements, "external requirements edit");
				}
			};
			bool WrittenRejected = false;
			try
			{
				Detail::Commit(L, {{Requirements, "early new requirements"}, {State, "later new state"}}, WrittenSnapshot, Work);
			}
			catch (const Detail::FFailure& Failure)
			{
				WrittenRejected = Failure.Diagnostic.Code == "recovery-required";
			}
			Options.CheckCancelled = {};
			Expect(WrittenRejected && Changed && fs::exists(File) && Read(File)["phase"] == "rollback" &&
			        Detail::Read(Requirements, 100) == "external requirements edit" && Detail::Read(State, 100) == "later new state",
			    "editing an already applied record preserves the conflict and the recovery journal");
			Write(Requirements, RestoreNew ? "early new requirements" : "early old requirements");
			Detail::Recover(L, Work);
			Expect(!fs::exists(File) && Detail::Read(Requirements, 100) == "early old requirements" &&
			        Detail::Read(State, 100) == "later old state",
			    "recovery resumes after restoring either recorded state of an edited write");
		}
		Write(State, "guard old state");
		Write(Lock, "guard old lock");
		const auto GuardSnapshot = Detail::CaptureManagementSnapshot(L, Work);
		unsigned FinalCallbacks = 0;
		Options.CheckCancelled = [&]
		{
			if (fs::exists(File) && Detail::Read(State, 100) == "guard new state" && ++FinalCallbacks > 1)
			{
				Write(Lock, "late external lock edit");
			}
		};
		bool GuardCommitted = false;
		std::string GuardError;
		try
		{
			Detail::Commit(L, {{State, "guard new state"}}, GuardSnapshot, Work);
			GuardCommitted = true;
		}
		catch (const Detail::FFailure& Failure)
		{
			GuardError = Failure.Diagnostic.Code;
		}
		Options.CheckCancelled = {};
		Expect(GuardCommitted && FinalCallbacks == 1 && !fs::exists(File) &&
		        Detail::Read(State, 100) == "guard new state" && Detail::Read(Lock, 100) == "guard old lock",
		    "final verification invokes no further host callbacks after its single cancellation check: " + GuardError);
	}

	class FTestProcessor final : public IInstallationProcessor
	{
	public:
		//~ Begin IInstallationProcessor Interface
		std::string GetVersion() const override
		{
			return "0.1.1";
		}

		std::string GetImplementationIdentity() const override
		{
			return "test.processor:0.1.1";
		}

		bool Process(const FInstallProcessingContext& C, FDiagnostic& E) const override
		{
			Phases.push_back(C.Phase);
			Retained = C;
			FResourceData Data;
			if (!C.ReadResource("data", "input.txt", Data, E) || !Data.bExists)
			{
				return false;
			}
			I18n::FI18nArgument Input;
			if (!C.ReadInput("secret", Input, E))
			{
				return false;
			}
			if (C.Phase == "install")
			{
				Write(U8Path(C.GeneratedRoot) / "processor.txt",
				    std::string(Data.Bytes.begin(), Data.Bytes.end()));
			}
			return true;
		}
		//~ End IInstallationProcessor Interface

		mutable std::vector<std::string> Phases;

		mutable FInstallProcessingContext Retained;
	};
	void ProcessingAndDisplayTests(const fs::path& Root)
	{
		auto Project = Root / "processing-project", Vendor = Root / "processing-package";
		fs::create_directories(Project);
		auto M = Manifest("processing-package");
		M["requires"]["features"] = J::array({J{{"id", "test.processor"}, {"version", "0.1.1"}}});
		M["resources"]["data"] = "data";
		M["localization"] = {{"root", "locale"}, {"baseCulture", "en-US"}};
		M["install"]["inputs"]["secret"] = {{"type", "string"}, {"required", true}, {"sensitive", true}};
		Write(Vendor / "veroscript-package.json", M.dump(2));
		Write(Vendor / U8Path("data/input.txt"), "typed processor resource");
		Write(Vendor / U8Path("locale/en-US/messages.json"),
		    J{{"install.before",
		          {{"pattern", "{package.name} {package.version}"}, {"formatId", "vero.named"}}},
		        {"install.after", "English after"}}
		        .dump());
		Write(Vendor / U8Path("locale/zh-CN/messages.json"),
		    J{{"install.before", {{"pattern", "安装 {package.name}"}, {"formatId", "vero.named"}}},
		        {"install.after", ""}}
		        .dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "processing-cache");
		Q.Arguments = {NativePathText(Vendor)};
		auto English = Read(Vendor / U8Path("locale/en-US/messages.json"));
		English["inputs.secret.description"] = "Secret input";
		Write(Vendor / U8Path("locale/en-US/messages.json"), English.dump());
		auto Chinese = Read(Vendor / U8Path("locale/zh-CN/messages.json"));
		Chinese["inputs.secret.description"] = "安全输入";
		Write(Vendor / U8Path("locale/zh-CN/messages.json"), Chinese.dump());
		Q.Options.NonInteractive = false;
		Q.Options.RequestInput = [&](const std::string&, const std::string& Definition, std::string& Value,
		                             FDiagnostic&)
		{
			Expect(J::parse(Definition).at("description") == "安全输入",
			    "input prompts use scoped localized descriptions");
			Value = J("ephemeral secret").dump();
			return true;
		};
		Q.Options.DisplayConfiguration.Language = "zh-CN";
		auto Handler = std::make_shared<FTestProcessor>();
		Q.Options.Features["test.processor"] = Handler;
		std::vector<std::string> Messages;
		Q.Options.Message = [&](const std::string& S)
		{
			Messages.push_back(S);
		};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		bool Ok = Manager.Execute(Q, R, E);
		Expect(Ok && R.Environment, "typed feature and localized installation: " + E.Code + " " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		Expect(Handler->Phases == std::vector<std::string>{"install"},
		    "custom processor invoked once after preflight");
		Expect(Messages == std::vector<std::string>{"安装 processing-package"},
		    "exact language and empty translation do not fall through");
		Expect(Read(Project / "veroscript-project.json").dump().find("ephemeral secret") == std::string::npos &&
		        Read(Project / U8Path(".veroscript/state.json")).dump().find("ephemeral secret") ==
		            std::string::npos,
		    "sensitive inputs excluded from configuration and receipts");
		FDiagnostic CallbackError;
		FResourceData Data;
		Expect(!Handler->Retained.ReadResource("data", "input.txt", Data, CallbackError) &&
		        CallbackError.Code == "processor-lifetime",
		    "retained installation services reject expired access");
		auto Snapshot = R.Environment;
		auto Requirements = Read(Project / "veroscript-project.json");
		auto Modified = Requirements;
		Modified["packages"]["processing-package"]["configuration"]["option"] = "changed";
		Write(Project / "veroscript-project.json", Modified.dump());
		std::shared_ptr<const FInstalledEnvironment> Stale;
		Ok = Manager.Load(Q.Environment, Stale, E, Q.Options);
		Expect(!Ok && E.Code == "processing-required",
		    "configuration changes invalidate custom processor output without actions or hooks");
		Write(Project / "veroscript-project.json", Requirements.dump());
		Expect(Manager.Load(Q.Environment, Stale, E, Q.Options),
		    "restored configuration can reuse its original processor output");
		I18n::FI18nSnapshot Display;
		I18n::FI18nConfiguration Config;
		Config.Language = "en-US";
		Expect(Snapshot->DisplaySnapshot(Config, Display, E), "independent display snapshot");
		auto Pkg = Snapshot->GetPackages().front();
		FClientOptions O;
		Detail::FWork W{O};
		Expect(Detail::Render(Pkg, "install.after", Display, W) == "English after",
		    "same package resolves through independent context");
		Q.Operation = EOperation::Uninstall;
		Q.Arguments = {"processing-package"};
		Expect(Manager.Execute(Q, R, E), "custom removal processor succeeds");
		Expect(Handler->Phases == std::vector<std::string>({"install", "uninstall"}),
		    "uninstall runs required native processor through staged contract");
		Expect(Detail::Render(Pkg, "install.after", Display, W) == "English after",
		    "display resources outlive uninstallation");
		Write(Vendor / U8Path("locale/zh-CN/messages.json"),
		    J{{"install.before", {{"pattern", "{missing.parameter}"}, {"formatId", "vero.named"}}}}.dump());
		Q.Operation = EOperation::Install;
		Q.Arguments = {NativePathText(Vendor)};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "package-text",
		    "malformed standard translations fail before processing");
		Expect(Handler->Phases.size() == 2, "invalid display never executes processor");
	}
	void StoredIdentityTests(const fs::path& Root)
	{
		class FCounter final : public IInstallationProcessor
		{
		public:
			//~ Begin IInstallationProcessor Interface
			std::string GetVersion() const override
			{
				return "0.1.1";
			}

			std::string GetImplementationIdentity() const override
			{
				return "identity-test:0.1.1";
			}

			bool Process(const FInstallProcessingContext&, FDiagnostic&) const override
			{
				++Calls;
				return true;
			}
			//~ End IInstallationProcessor Interface

			mutable unsigned Calls = 0;
		};
		auto Project = Root / "stored-identity-project", Vendor = Root / "stored-identity-source";
		fs::create_directories(Project);
		auto M = Manifest("stored-identity");
		M["requires"]["features"] = J::array({J{{"id", "test.identity"}, {"version", "0.1.1"}}});
		Write(Vendor / "veroscript-package.json", M.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "stored-identity-cache");
		Q.Arguments = {NativePathText(Vendor)};
		auto Counter = std::make_shared<FCounter>();
		Q.Options.Features["test.identity"] = Counter;
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		Expect(Manager.Execute(Q, R, E) && R.Environment, "stored identity baseline: " + E.Message);
		if (!R.Environment)
		{
			return;
		}
		auto Installed = R.Environment;
		auto LockPath = Project / "veroscript.lock";
		auto ReceiptPath = U8Path(Installed->GetPackages().front().Path) / ".vero-receipt.json";
		auto Lock = Read(LockPath), Receipt = Read(ReceiptPath);
		auto Requirements = Read(Project / "veroscript-project.json"),
		     State = Read(Project / U8Path(".veroscript/state.json"));
		Q.Arguments.clear();
		for (auto Kind : {"lock", "receipt", "missing-lock"})
		{
			if (std::string(Kind) == "lock")
			{
				auto Changed = Lock;
				Changed["packages"][0]["manifest"]["description"] = "changed locked metadata";
				Write(LockPath, Changed.dump());
			}
			else if (std::string(Kind) == "receipt")
			{
				auto Changed = Receipt;
				Changed["environmentId"] = "another-environment";
				Write(ReceiptPath, Changed.dump());
			}
			else
			{
				fs::remove(LockPath);
			}
			auto Calls = Counter->Calls;
			Expect(!Manager.Execute(Q, R, E) && !R.Committed,
			    "invalid existing identity fails: " + std::string(Kind));
			Expect(Counter->Calls == Calls,
			    "invalid identity is rejected before installation processing: " + std::string(Kind));
			Expect(Read(Project / "veroscript-project.json") == Requirements &&
			        Read(Project / U8Path(".veroscript/state.json")) == State,
			    "invalid identity preserves existing management records");
			Write(LockPath, Lock.dump());
			Write(ReceiptPath, Receipt.dump());
		}
		Expect(Manager.Execute(Q, R, E) && R.Environment,
		    "restored identity permits normal reinstall: " + E.Code + " " + E.Message);
	}
	class FPublisher final : public IHttpTransport
	{
	public:
		bool Send(const FHttpRequest& Q, FHttpResponse& R, FDiagnostic& E) const override
		{
			E = {};
			R = {};
			R.Status = 200;
			if (Q.Url.rfind(Api + "/", 0) == 0)
			{
				if (Authenticated)
				{
					Expect(Q.Credential && Q.Headers.count("Authorization") &&
					        Q.Credential(Q.Url) == "Bearer registry-fixture" &&
					        Q.Credential("https://upload.invalid/object").empty(),
					    "publication and recovery propagate the exact-URL credential policy");
				}
				else
				{
					Expect(!Q.Credential && !Q.Headers.count("Authorization"),
					    "a different publication API does not inherit registry credentials");
				}
			}
			const auto Conflict = [&]()
			{
				R.Status = 409;
				R.Body = J{{"code", "publish-conflict"}, {"message", "immutable artifact or target conflict"}}
				             .dump();
				return true;
			};
			if (Q.Method == "POST" && Q.Url == Api + "/uploads")
			{
				auto Id = "upload-" + std::to_string(++Uploads);
				Allocations[Id] = J::parse(Q.Body);
				R.Body = J{{"format", "veroscript-upload"},
				    {"formatVersion", "0.1.1"},
				    {"sourceId", SourceId},
				    {"uploadId", Id},
				    {"method", "PUT"},
				    {"url", "https://upload.invalid/object/" + Id + "?signature=short-lived"},
				    {"headers", {{"Authorization", "signed-upload-fixture"}}}}
				             .dump();
				return true;
			}
			if (Q.Method == "PUT")
			{
				Expect(!Q.Credential && Q.Headers.count("Authorization") &&
				        Q.Headers.at("Authorization") == "signed-upload-fixture",
				    "explicit signed upload authorization is independent of registry credentials");
				FClientOptions O;
				Detail::FWork W{O};
				auto Bytes = Detail::Read(U8Path(Q.BodyFile), 1 << 24);
				auto Id = Q.Url.substr(Q.Url.find("/object/") + 8);
				Id = Id.substr(0, Id.find('?'));
				Expect(Allocations.at(Id).at("sha256") == Detail::Hash(Bytes),
				    "uploaded archive matches allocation");
				Blobs[Detail::Hash(Bytes)] = Bytes;
				R.Body = "stored";
				return true;
			}
			if (Q.Method == "POST")
			{
				auto Commit = J::parse(Q.Body);
				++Commits;
				LastCommit = Commit;
				if (FirstCommit.is_null())
				{
					FirstCommit = Commit;
				}
				Expect(Commit.value("mode", "") == "append",
				    "publication uses explicit append-only protocol");
				auto Key = Commit.at("idempotencyKey").get<std::string>();
				if (Requests.count(Key) && Requests[Key] != Commit)
				{
					return Conflict();
				}
				if (ErrorBeforeCommit && !CommitErrorBody.empty())
				{
					R.Status = 502;
					R.Body = CommitErrorBody;
					return true;
				}
				if (FailCommitOnce)
				{
					FailCommitOnce = false;
					if (FailRecoveryOnce)
					{
						FailedReads = 1;
					}
					E.Code = "http-transfer";
					E.Message = "request lost before commit";
					return false;
				}
				if (BeforeCommit)
				{
					BeforeCommit(Existing);
				}
				J Next = Existing;
				if (Next.is_null())
				{
					Next = {{"format", "veroscript-registry-version"},
					    {"formatVersion", "0.1.1"},
					    {"sourceId", Commit["sourceId"]},
					    {"name", Commit["name"]},
					    {"version", Commit["version"]},
					    {"dependencies", Commit["dependencies"]},
					    {"conflicts", Commit["conflicts"]},
					    {"artifacts", J::array()}};
				}
				for (const auto* K : {"sourceId", "name", "version", "dependencies", "conflicts"})
				{
					if (Next[K] != Commit[K])
					{
						return Conflict();
					}
				}
				for (const auto& Ref : Commit["artifacts"])
				{
					auto A = Data(Ref);
					bool Same = false;
					for (const auto& Prior : Next["artifacts"])
					{
						if (Data(Prior) == A)
						{
							Same = true;
						}
						else if (Overlap(Prior["target"], A["target"]))
						{
							return Conflict();
						}
					}
					if (!Same)
					{
						A["id"] = "published-" + std::to_string(++NextId);
						Next["artifacts"].push_back(std::move(A));
					}
				}
				Requests[Key] = Commit;
				Existing = std::move(Next);
				if (AfterCommit)
				{
					AfterCommit(Existing);
				}
				if (CorruptVersion)
				{
					Existing["artifacts"][0]["sha256"] = std::string(64, '0');
				}
				if (LoseFirst)
				{
					LoseFirst = false;
					if (FailRecoveryOnce)
					{
						FailedReads = 1;
					}
					E.Code = "http-transfer";
					E.Message = "response lost after append commit";
					return false;
				}
				if (!CommitErrorBody.empty())
				{
					R.Status = 502;
					R.Body = CommitErrorBody;
					return true;
				}
				R.Body = Existing.dump();
				return true;
			}
			if (Q.Method == "GET")
			{
				if (NullVersion)
				{
					R.Body = "null";
					return true;
				}
				if (FailedReads)
				{
					--FailedReads;
					R.Status = 503;
					R.Body = J{{"code", "unavailable"}, {"message", "fixture unavailable"}}.dump();
					return true;
				}
				if (!Existing.is_null())
				{
					auto Base = Api + "/packages/" + Existing.at("name").get<std::string>();
					if (Q.Url == Base)
					{
						R.Body = J{{"format", "veroscript-registry-package"},
						    {"formatVersion", "0.1.1"},
						    {"sourceId", SourceId},
						    {"name", Existing["name"]},
						    {"versions", J::array({J{{"version", Existing["version"]}}})}}
						             .dump();
						return true;
					}
					if (Q.Url == Base + "/versions/" + Existing.at("version").get<std::string>())
					{
						R.Body = Existing.dump();
						return true;
					}
					for (const auto& A : Existing["artifacts"])
					{
						if (Q.Url == Api + "/artifacts/" + A.at("id").get<std::string>() + "/download")
						{
							R.Body = Blobs.at(A.at("sha256").get<std::string>());
							return true;
						}
					}
				}
			}
			R.Status = 404;
			R.Body = J{{"code", "not-found"}, {"message", "fixture absent"}}.dump();
			return true;
		}

		static J Data(const J& A)
		{
			J R = A;
			R.erase("id");
			R.erase("uploadId");
			R.erase("blocked");
			return R;
		}
		static bool Overlap(const J& A, const J& B)
		{
			for (const auto* K : {"os", "arch", "environment", "coreBuild"})
			{
				if (A.contains(K) && B.contains(K) && A[K] != B[K])
				{
					return false;
				}
			}
			auto Abis = A.value("abis", J::object());
			for (auto I = Abis.begin(); I != Abis.end(); ++I)
			{
				if (B.value("abis", J::object()).contains(I.key()) && B["abis"][I.key()] != I.value())
				{
					return false;
				}
			}
			return true;
		}

		std::string SourceId = "publish.source", Api = "https://publish.invalid/api";
		std::string CommitErrorBody;
		mutable J Existing, FirstCommit, LastCommit;
		mutable std::map<std::string, J> Allocations, Requests;
		mutable std::map<std::string, std::string> Blobs;
		mutable unsigned Uploads = 0, Commits = 0, NextId = 0, FailedReads = 0;
		mutable bool LoseFirst = true, FailCommitOnce = false;
		bool FailRecoveryOnce = false, CorruptVersion = false, Authenticated = true, NullVersion = false,
		     ErrorBeforeCommit = false;
		std::function<void(J&)> BeforeCommit, AfterCommit;
	};
	void SignedDistributionTests(const fs::path& Root)
	{
		const auto Vendor = Root / "signed-distribution";
		auto M = Manifest("signed-package");
		Write(Vendor / "veroscript-package.json", M.dump());
		Write(Vendor / "main.vs", "export int value() { return 9; }");
		const auto Archive = Root / "signed.vspkg";
		FClientOptions Options;
		Detail::FWork Work{Options};
		Detail::Pack(Vendor, Archive, M, Work);
		const auto Hash = Detail::HashFile(Archive, Work, Options.Limits.MaxArchiveBytes);
		// Transport/lock fixture only. Cryptographic vectors are Application tests.
		J Record = {{"format", "veroscript-artifact-signature"}, {"formatVersion", "0.1.1"}, {"algorithm", "ed25519"},
		    {"kind", "distribution"}, {"bytes", std::to_string(fs::file_size(Archive))}, {"sha256", Hash},
		    {"keyId", "fixture"}, {"keyVersion", "1"}, {"signature", std::string(86, 'A') + "=="}};
		const auto Records = J::array({Record});
		Write(U8Path(NativePathText(Archive) + ".sig.json"), Records.dump());
		const auto Project = Root / "signed-publish-project";
		fs::create_directories(Project);
		auto P = Detail::NewProject("signed-project");
		auto Publisher = std::make_shared<FPublisher>();
		Publisher->Authenticated = false;
		P["packageManager"]["registries"]["official"] = {{"id", Publisher->SourceId}, {"api", Publisher->Api}};
		Write(Project / "veroscript-project.json", P.dump());
		FRequest Q;
		Q.Operation = EOperation::Publish;
		Q.Arguments = {NativePathText(Archive)};
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.WorkingDirectory = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "signed-cache");
		Q.Options.Transport = Publisher;
		FResult R;
		FDiagnostic E;
		Expect(FPackageManager().Execute(Q, R, E) && Publisher->Existing["artifacts"][0]["signatures"] == Records,
		    "publication confirmation/recovery retains artifact signatures: " + E.Message);
		Q.Operation = EOperation::Install;
		Q.Environment.Root = NativePathText(Root / "signed-install-project");
		fs::create_directories(U8Path(Q.Environment.Root));
		Q.Environment.WorkingDirectory = Q.Environment.Root;
		Expect(!FPackageManager().Execute(Q, R, E) && E.Code == "artifact-trust", "signed installation needs an explicit trust service");
		class FVerifier final : public IArtifactDecoder
		{
		public:
			std::string Expected;
			mutable unsigned Calls = 0;
			std::string GetIdentity() const override
			{
				return "signature-fixture";
			}
			bool Decode(const FArtifactRequest& R, const FArtifactWork&, FArtifactData& D, std::string& E) const override
			{
				++Calls;
				if (R.Kind != "distribution" || !D.SignatureRecords || *D.SignatureRecords != Expected)
				{
					E = "unexpected signature subject";
					return false;
				}
				D.TrustIdentity = GetIdentity();
				return true;
			}
		};
		auto Verifier = std::make_shared<FVerifier>();
		Verifier->Expected = Records.dump();
		Q.Options.ArtifactDecoder = Verifier;
		Expect(FPackageManager().Execute(Q, R, E) && Verifier->Calls && J::parse(R.Json).is_object(), "authorized installation verifies stored signature records: " + E.Message);
		const auto Consumer = Root / "signed-registry-consumer";
		Write(Consumer / "veroscript-project.json", P.dump());
		Q.Environment.Root = NativePathText(Consumer);
		Q.Environment.WorkingDirectory = NativePathText(Consumer);
		Q.Arguments = {"signed-package"};
		const bool Installed = FPackageManager().Execute(Q, R, E);
		Expect(Installed, "registry signature records are preserved in the lock: " + E.Code + " " + E.Message);
		if (Installed)
		{
			const auto Lock = Detail::Read(Consumer / "veroscript.lock", Q.Options.Limits.MaxStateBytes);
			const auto Original = Publisher->Existing;
			for (const auto* Mutation : {"removed", "changed", "added"})
			{
				Publisher->Existing = Original;
				auto& Artifact = Publisher->Existing["artifacts"][0];
				if (std::string(Mutation) == "removed")
				{
					Artifact.erase("signatures");
				}
				else if (std::string(Mutation) == "changed")
				{
					Artifact["signatures"][0]["keyVersion"] = "2";
				}
				else
				{
					auto Added = Record;
					Added["keyId"] = "second-key";
					Artifact["signatures"].push_back(std::move(Added));
				}
				Expect(!FPackageManager().Execute(Q, R, E) && E.Code == "registry-mutated",
				    "online refresh rejects " + std::string(Mutation) + " signatures on a locked artifact");
				Expect(Detail::Read(Consumer / "veroscript.lock", Q.Options.Limits.MaxStateBytes) == Lock,
				    "rejected signature mutation leaves the lock unchanged");
			}
			Publisher->Existing = Original;
			Expect(FPackageManager().Execute(Q, R, E),
			    "unchanged registry signatures permit normal reinstall: " + E.Code);
		}
		Q.Arguments = {NativePathText(Archive)};
		Write(U8Path(NativePathText(Archive) + ".sig.json"), "null");
		Q.Environment.Root = NativePathText(Root / "null-signature-project");
		fs::create_directories(U8Path(Q.Environment.Root));
		Q.Environment.WorkingDirectory = Q.Environment.Root;
		Expect(!FPackageManager().Execute(Q, R, E) && E.Code == "artifact-signatures", "null signature file is not treated as unsigned");
	}
	void PublicationTests(const fs::path& Root)
	{
		auto Project = Root / "publish-project", Vendor = Root / "publish-package";
		fs::create_directories(Project);
		auto M = Manifest("publish-package");
		Write(Vendor / "veroscript-package.json", M.dump(2));
		Write(Vendor / "repeated.txt", std::string(500000, 'x'));
		auto Archive = Root / "publish-package.zip";
		FClientOptions O;
		Detail::FWork W{O};
		Detail::Pack(Vendor, Archive, M, W);
		auto P = Detail::NewProject("publish-project");
		P["packageManager"]["registries"]["official"] = {{"id", "publish.source"},
		    {"api", "https://publish.invalid/api"}};
		Write(Project / "veroscript-project.json", P.dump());
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Environment.CacheDirectory = NativePathText(Root / "publish-cache");
		Q.Operation = EOperation::Publish;
		Q.Arguments = {NativePathText(Archive)};
		auto Transport = std::make_shared<FPublisher>();
		Q.Options.Transport = Transport;
		Q.Options.Credential = [Api = Transport->Api](const std::string& Url)
		{
			return Url.rfind(Api + "/", 0) == 0 ? "Bearer registry-fixture" : std::string{};
		};
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		bool Ok = Manager.Execute(Q, R, E);
		Expect(Ok, "lost publication response recovers immutable artifact set: " + E.Code + " " + E.Message);
		Expect(Transport->Uploads == 1 && Transport->Commits == 1, "single allocation and final commit");
		Expect(Manager.Execute(Q, R, E), "repeat publication reuses recovery record");
		Expect(Transport->Uploads == 1 && Transport->Commits == 1,
		    "confirmed publication is idempotent without uploading or committing again");
		for (const auto* Name : {"published.ZIP", "发布 package.VsPkG"})
		{
			auto Renamed = Root / U8Path(Name);
			fs::copy_file(Archive, Renamed);
			Q.Arguments = {NativePathText(Renamed)};
			Expect(Manager.Execute(Q, R, E) && Transport->Uploads == 1,
			    "renamed ZIP publication shares immutable bytes and upload recovery: " + E.Code + " " +
			        E.Message);
		}
		auto OriginalRoot = Q.Environment.Root;
		Q.Environment.Root = NativePathText(Vendor);
		Q.Options.OfficialApi = "https://publish.invalid/api";
		auto DistributionTransport = std::make_shared<FPublisher>();
		DistributionTransport->SourceId = "veroscript.official";
		Q.Options.Transport = DistributionTransport;
		bool DistributionPublished = Manager.Execute(Q, R, E);
		Expect(DistributionPublished, "publish from distribution directory: " + E.Code + " " + E.Message);
		Q.Environment.Root = OriginalRoot;
		Q.Options.Transport = Transport;
		for (const auto& F : fs::recursive_directory_iterator(U8Path(Q.Environment.CacheDirectory)))
		{
			if (F.is_regular_file() && F.path().extension() == ".json")
			{
				auto Record = Detail::Read(F.path(), 1 << 24);
				Expect(
				    Record.find("short-lived") == std::string::npos &&
				        Record.find("registry-fixture") == std::string::npos &&
				        Record.find("signed-upload-fixture") == std::string::npos,
				    "signed upload URLs, authorization headers and registry credentials are not persisted");
			}
		}
		Q.Arguments.push_back(NativePathText(Archive));
		auto Calls = Transport->Uploads;
		Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-duplicate" && Transport->Uploads == Calls,
		    "duplicate publication artifacts rejected before upload");
		Q.Arguments.pop_back();
		auto Incorrect = std::make_shared<FPublisher>();
		Incorrect->LoseFirst = false;
		Incorrect->CorruptVersion = true;
		Q.Options.Transport = Incorrect;
		Q.Environment.CacheDirectory = NativePathText(Root / "publish-corrupt-cache");
		Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-conflict",
		    "successful HTTP response still verifies immutable artifact identity");
		auto Alternate = std::make_shared<FPublisher>();
		Alternate->LoseFirst = false;
		Alternate->Api = "https://other-publish.invalid/api";
		Alternate->Authenticated = false;
		P["packageManager"]["registries"]["official"]["api"] = Alternate->Api;
		Write(Project / "veroscript-project.json", P.dump());
		Q.Options.Transport = Alternate;
		Expect(Manager.Execute(Q, R, E) && Alternate->Uploads == 1,
		    "publication recovery is scoped to registry API as well as logical "
		    "source");
	}
	void PublicationAppendTests(const fs::path& Root)
	{
		auto Project = Root / "append-project";
		auto P = Detail::NewProject("append-project");
		P["packageManager"]["registries"]["official"] = {{"id", "publish.source"},
		    {"api", "https://publish.invalid/api"}};
		Write(Project / "veroscript-project.json", P.dump());
		FPackageManager Manager;
		FResult R;
		FDiagnostic E;
		FRequest Q;
		Q.Environment.Root = NativePathText(Project);
		Q.Operation = EOperation::Publish;
		Q.Options.Credential = [](const std::string& Url)
		{
			return Url.rfind("https://publish.invalid/api/", 0) == 0 ? "Bearer registry-fixture"
			                                                         : std::string{};
		};
		Q.Options.Mirror = "https://mirror-must-not-publish.invalid/api";
		std::map<std::string, J> Metadata;
		const auto Archive = [&](const std::string& Label, const J& Target, bool Dependency = false)
		{
			auto Vendor = Root / ("append-source-" + Label);
			auto M = Manifest("platform-package");
			M["target"] = Target;
			if (Dependency)
			{
				M["dependencies"]["other-package"] = {{"version", "0.1.1"}};
			}
			Write(Vendor / "veroscript-package.json", M.dump(2));
			Write(Vendor / "payload.txt", Label);
			auto Path = Root / ("append-" + Label + ".vspkg");
			FClientOptions O;
			Detail::FWork W{O};
			Detail::Pack(Vendor, Path, M, W);
			Metadata[Label] = {{"sha256", Detail::HashFile(Path, W, 1 << 24)},
			    {"size", fs::file_size(Path)},
			    {"manifestSha256", Detail::Hash(M.dump(2))},
			    {"target", Target},
			    {"requires", J::object()}};
			return NativePathText(Path);
		};
		std::vector<std::string> Systems{HostTargetContext().Os};
		for (const auto* Os : {"windows", "linux", "macos", "android", "ios"})
		{
			if (std::find(Systems.begin(), Systems.end(), Os) == Systems.end())
			{
				Systems.push_back(Os);
			}
		}
		auto First = Archive("first", J{{"os", Systems[0]}});
		auto Second = Archive("second", J{{"os", Systems[1]}});
		auto Third = Archive("third", J{{"os", Systems[2]}});
		auto Fourth = Archive("fourth", J{{"os", Systems[3]}});
		auto Portable = Archive("portable", J::object());
		auto Replacement = Archive("replacement", J{{"os", Systems[0]}});
		auto DifferentDeps = Archive("dependencies", J{{"os", Systems[2]}}, true);
		auto Transport = std::make_shared<FPublisher>();
		Transport->LoseFirst = false;
		Q.Options.Transport = Transport;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-cache");
		Q.Arguments = {First};
		Expect(Manager.Execute(Q, R, E), "create first platform artifact: " + E.Code + " " + E.Message);
		auto Original = Transport->Existing["artifacts"][0];
		Q.Arguments = {Second};
		Expect(Manager.Execute(Q, R, E),
		    "append another platform to the same version: " + E.Code + " " + E.Message);
		Expect(Transport->Existing["artifacts"].size() == 2 &&
		        Transport->Existing["artifacts"][0] == Original,
		    "append preserves every original artifact field and ID");
		Expect(Transport->LastCommit["artifacts"].size() == 1 && Transport->Uploads == 2,
		    "append uploads and submits only the new platform");
		Q.Arguments = {First, Second};
		Expect(Manager.Execute(Q, R, E) && Transport->Uploads == 2 && Transport->Commits == 2,
		    "republishing an existing subset is a no-op");
		auto Baseline = Transport->Existing;
		auto Blobs = Transport->Blobs;
		for (const auto& Bad : {Replacement, DifferentDeps, Portable})
		{
			Q.Arguments = {Bad};
			Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-conflict" && Transport->Uploads == 2 &&
			        Transport->Commits == 2 && Transport->Existing == Baseline,
			    "reject replacement, changed dependencies and portable overlap before upload: " + E.Code);
		}
		Q.Arguments = {First, Second};
		for (const auto* Field : {"id", "sha256", "manifestSha256", "size", "requires", "target", "removed"})
		{
			auto& A = Transport->Existing["artifacts"][0];
			std::string Key = Field;
			if (Key == "removed")
			{
				Transport->Existing["artifacts"].erase(0);
			}
			else if (Key == "size")
			{
				A[Key] = A[Key].get<std::uint64_t>() + 1;
			}
			else if (Key == "requires")
			{
				A[Key] = {{"core", ">=0.1.1"}};
			}
			else if (Key == "target")
			{
				A[Key] = {{"os", "changed-system"}};
			}
			else
			{
				A[Key] = Key == "id" ? "changed-id" : std::string(64, '0');
			}
			Expect(!Manager.Execute(Q, R, E) && Transport->Uploads == 2 && Transport->Commits == 2,
			    "saved publication detects changed existing identity: " + Key);
			Transport->Existing = Baseline;
		}
		const auto Seed = [&]()
		{
			auto T = std::make_shared<FPublisher>();
			T->Existing = Baseline;
			T->Blobs = Blobs;
			T->NextId = 2;
			T->LoseFirst = false;
			return T;
		};
		const auto Extra = [&]()
		{
			auto A = Metadata.at("fourth");
			A["id"] = "concurrent-fourth";
			return A;
		};
		auto NullVersion = Seed();
		NullVersion->NullVersion = true;
		Q.Options.Transport = NullVersion;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-null-version-cache");
		Q.Arguments = {Third};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-contract" && NullVersion->Uploads == 0 &&
		        NullVersion->Commits == 0,
		    "HTTP 200 null is invalid metadata, not an absent version");
		unsigned ErrorCase = 0;
		for (const auto* Body :
		    {"[]", "null", "\"gateway error\"", "{\"code\":502,\"message\":false,\"requestId\":{}}"})
		{
			for (bool Applied : {false, true})
			{
				auto T = Seed();
				T->CommitErrorBody = Body;
				T->ErrorBeforeCommit = !Applied;
				Q.Options.Transport = T;
				Q.Environment.CacheDirectory =
				    NativePathText(Root / ("append-error-cache-" + std::to_string(++ErrorCase)));
				auto Success = Manager.Execute(Q, R, E);
				Expect(Success == Applied && T->Uploads == 1 && T->Commits == 1 &&
				        T->Existing["artifacts"].size() == (Applied ? 3 : 2) &&
				        (Applied || E.Code == "publish-http"),
				    "malformed HTTP errors reconcile actual commit state without false success: " +
				        E.Code);
			}
		}
		for (bool Lost : {false, true})
		{
			auto T = Seed();
			T->LoseFirst = Lost;
			T->BeforeCommit = [&](J& V)
			{
				V["artifacts"].push_back(Extra());
			};
			Q.Options.Transport = T;
			Q.Environment.CacheDirectory =
			    NativePathText(Root / (Lost ? "append-lost-cache" : "append-concurrent-cache"));
			Q.Arguments = {Third};
			Expect(Manager.Execute(Q, R, E) && T->Existing["artifacts"].size() == 4 &&
			        T->Existing["artifacts"][0] == Original && T->Uploads == 1 && T->Commits == 1,
			    "confirm disjoint concurrent append, including lost response: " + E.Code + " " +
			        E.Message);
		}
		auto Retry = Seed();
		Retry->FailCommitOnce = true;
		Retry->FailRecoveryOnce = true;
		Q.Options.Transport = Retry;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-retry-cache");
		Q.Arguments = {Third};
		Expect(!Manager.Execute(Q, R, E) && Retry->Uploads == 1 && Retry->Commits == 1,
		    "failed commit and unavailable lookup retain pending upload");
		Expect(Manager.Execute(Q, R, E) && Retry->Uploads == 1 && Retry->Commits == 2 &&
		        Retry->FirstCommit == Retry->LastCommit,
		    "resume reuses exact upload references, request and idempotency key: " + E.Code);
		auto Lost = Seed();
		Lost->LoseFirst = true;
		Lost->FailRecoveryOnce = true;
		Q.Options.Transport = Lost;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-later-recovery-cache");
		Expect(!Manager.Execute(Q, R, E), "lost append response and failed lookup leave recovery pending");
		Lost->Existing["artifacts"].push_back(Extra());
		Expect(Manager.Execute(Q, R, E) && Lost->Uploads == 1 && Lost->Commits == 1 &&
		        Lost->Existing["artifacts"].size() == 4,
		    "resume recognizes committed append after later concurrent addition");
		auto BadServer = Seed();
		BadServer->AfterCommit = [](J& V)
		{
			V["artifacts"][0]["id"] = "replaced-original";
		};
		Q.Options.Transport = BadServer;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-invalid-confirmation-cache");
		Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-conflict",
		    "successful HTTP response cannot confirm a replaced original artifact");
		auto Race = Seed();
		Race->BeforeCommit = [&](J& V)
		{
			auto A = Metadata.at("third");
			A["sha256"] = std::string(64, '0');
			A["id"] = "conflicting-third";
			V["artifacts"].push_back(A);
		};
		Q.Options.Transport = Race;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-race-cache");
		Q.Arguments = {Third, Fourth};
		Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-conflict" &&
		        Race->Existing["artifacts"].size() == 3 && Race->Existing["artifacts"][0] == Original,
		    "concurrent overlap rejects the complete batch without partial append");
		for (const auto* Flag : {"blocked", "withdrawn"})
		{
			auto T = Seed();
			T->Existing[Flag] = true;
			Q.Options.Transport = T;
			Q.Environment.CacheDirectory = NativePathText(Root / (std::string("append-") + Flag + "-cache"));
			Q.Arguments = {Third};
			Expect(!Manager.Execute(Q, R, E) && E.Code == "publish-state" && T->Uploads == 0,
			    "blocked or withdrawn versions reject append before upload");
		}
		// Appending other targets must not invalidate an installed target's lock.
		FRequest Install;
		Install.Operation = EOperation::Install;
		Install.Environment.Root = NativePathText(Root / "append-consumer");
		Install.Environment.CacheDirectory = NativePathText(Root / "append-consumer-cache");
		Write(U8Path(Install.Environment.Root) / "veroscript-project.json", P.dump());
		Install.Options = Q.Options;
		Install.Options.Mirror.clear();
		Install.Options.Transport = Transport;
		Install.Arguments = {"platform-package"};
		Expect(Manager.Execute(Install, R, E), "install original platform before append: " + E.Code);
		auto Lock = Read(U8Path(Install.Environment.Root) / "veroscript.lock");
		Q.Options.Transport = Transport;
		Q.Environment.CacheDirectory = NativePathText(Root / "append-cache");
		Q.Arguments = {Third};
		Expect(Manager.Execute(Q, R, E), "append preserves installed original target: " + E.Code);
		Install.Arguments.clear();
		Install.Options.Locked = true;
		Install.Options.Offline = true;
		Expect(Manager.Execute(Install, R, E) &&
		        Read(U8Path(Install.Environment.Root) / "veroscript.lock") == Lock,
		    "locked offline reinstall is unchanged after another platform is appended: " + E.Code);
	}

} // namespace
int main(int Argc, char** Argv)
{
	try
	{
		#if !defined(_WIN32)
		if (Argc == 3 && std::string(Argv[1]) == "--lock-then-exec")
		{
			Detail::FFileLock Lock(U8Path(Argv[2]), true);
			execl(Argv[0], Argv[0], "--check-exec-lock", Argv[2], static_cast<char*>(nullptr));
			return 127;
		}
		if (Argc == 3 && std::string(Argv[1]) == "--check-exec-lock")
		{
			Detail::FFileLock Lock(U8Path(Argv[2]), true, false, false);
			return 0;
		}
		#endif
		ModelTests();
		auto Root =
		    Detail::Absolute(NativePathText(fs::temp_directory_path() /
		                         ("veroscript-package-tests-" +
		                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))),
		        {});
		fs::create_directories(Root);
		#if !defined(_WIN32)
		const auto Executable = NativePathText(fs::absolute(U8Path(Argv[0])));
		const auto ExecLock = NativePathText(Root / "exec-inheritance.lock");
		const auto Child = fork();
		if (Child == 0)
		{
			execl(Executable.c_str(), Executable.c_str(), "--lock-then-exec", ExecLock.c_str(),
			    static_cast<char*>(nullptr));
			_exit(127);
		}
		int Status = 0;
		pid_t Waited = -1;
		if (Child > 0)
		{
			do
			{
				Waited = waitpid(Child, &Status, 0);
			}
			while (Waited < 0 && errno == EINTR);
		}
		Expect(Child > 0 && Waited == Child && WIFEXITED(Status) && WEXITSTATUS(Status) == 0,
		    "exec does not inherit package environment or lease locks");
		#endif
		std::cerr << "SUITE: LocalTests\n";
		LocalTests(Root);
		ExplicitRootSourceTests(Root);
		std::cerr << "SUITE: RemoteTests\n";
		RemoteTests(Root);
		RequirementsTests(Root);
		RequirementsUrlTests(Root);
		MirrorTrustTests(Root);
		ConditionalRootTests(Root);
		std::cerr << "SUITE: HookTests\n";
		HookTests(Root);
		NumericHookTests(Root);
		ConstantHookTests(Root);
		std::cerr << "SUITE: CliTests\n";
		DiscoveryTests(Root);
		IncompleteEnvironmentTests(Root);
		CliTests(Root);
		std::cerr << "SUITE: CompositionTests\n";
		CompositionTests(Root);
		CompositionPinTests(Root);
		CompositionProcessingTests(Root);
		CompositionLoaderTests(Root);
		CompositionDeploymentConfigurationTests(Root);
		InstalledPrereleaseTests(Root);
		LockedDependencyArrayTests();
		std::cerr << "SUITE: ArchiveTests\n";
		ArchiveTests(Root);
		ArchivePublicationTests(Root);
		std::cerr << "SUITE: TransactionTests\n";
		CleanupIdentityTests(Root);
		CleanupRecoveryTests(Root);
		DeploymentLimitsTests(Root);
		CrossTargetDeploymentTests(Root);
		TransactionTests(Root);
		JournalRecoveryTests(Root);
		ProcessingAndDisplayTests(Root);
		ResourceEnvironmentTests(Root);
		ScriptExtensionPackageTests(Root);
		ServiceSelectionTests(Root);
		StaticCatalogTests(Root);
		StoredIdentityTests(Root);
		PublicationTests(Root);
		SignedDistributionTests(Root);
		PublicationAppendTests(Root);
		if (Argc > 1)
		{
			NativeTests(Root, Argv[1]);
		}
		if (Argc > 2)
		{
			JitTests(Root, Argv[2]);
		}
		if (!Failures)
		{
			Detail::NoLinks(Root);
			for (const auto& F : fs::recursive_directory_iterator(Root))
			{
				Detail::NoLinks(F.path());
			}
			fs::remove_all(Root);
		}
		else
		{
			std::cerr << "fixtures retained: " << NativePathText(Root) << '\n';
		}
		std::cout << Checks << " checks, " << Failures << " failures\n";
		return Failures ? 1 : 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << "test exception: " << E.what() << '\n';
		return 1;
	}
}
