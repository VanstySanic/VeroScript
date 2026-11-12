// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPath.h"
#include "VeroScriptTestEngine.h"
#include "VeroScriptTestVm.h"
#include "VeroScriptLambdaStaticFixture.h"
#include "VeroScriptApplication.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "VeroScriptEngine.h"
#include "VeroScriptProcess.h"
#include "VeroScriptExecutable.h"
#include "VeroScriptVmModule.h"
#include "json/json.hpp"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
	using namespace VeroScript;
	using namespace VeroScript::Application;
	using J = nlohmann::json;
	namespace fs = std::filesystem;
	std::size_t Assertions = 0;
	void Expect(bool Value, const std::string& Message)
	{
		++Assertions;
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void ExpectOk(bool Value, const FDiagnostic& E, const char* Context)
	{
		Expect(Value, std::string(Context) + ": " + E.Code + " " + E.Message + " " + E.Path);
	}
	std::vector<std::uint8_t> Bytes(std::string_view S)
	{
		return {S.begin(), S.end()};
	}
	void RunPrepared(FPreparedApplication& Prepared, int Expected)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = EExecutionBackend::Vm;
		Config.ModuleEnvironment = Prepared.Environment;
		Session.ApplyConfig(Config);
		const auto Load = Prepared.Package ? Session.LoadPackage(*Prepared.Package, Prepared.LogicalPath) : Session.LoadSource(Prepared.Source, Prepared.LogicalPath);
		Expect(Load.bSuccess, "prepared application load: " + Load.Error.Message + Errors.str());
		const auto Run = Session.Run();
		Expect(Run.bSuccess && Run.ExitCode == Expected, "prepared application result: " + Run.Error.Message + Errors.str());
	}
	void ExecutableEnvelope(const std::string& Self, const fs::path& Work)
	{
		FDiagnostic E;
		auto Compiled = VeroScriptCompile::CompileSource("int main() { return 31; }");
		Expect(Compiled.bSuccess, "compile executable entry");
		FPackageBuildOptions Options;
		Options.PackageName = "envelope.tests";
		std::vector<FContentFile> Files;
		ExpectOk(BuildApplication(*Compiled.Module, Options, "0.1.1", {}, Files, E), E, "assemble executable entry");
		const auto Output = PathUtil::ToUtf8(Work / "envelope.exe");
		#if defined(__APPLE__)
		// The built-in envelope provider promises PE/ELF; Mach-O uses a host provider.
		Expect(!WriteExecutableApplication(PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(Self))), Files, Output, E) &&
		        E.Code == "executable-platform" && !fs::exists(PathUtil::FromUtf8(Output)),
		    "unsupported Mach-O bootstrap is rejected without publishing output");
		#else
		ExpectOk(WriteExecutableApplication(PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(Self))), Files, Output, E), E, "write executable envelope");
		std::vector<std::uint8_t> Payload;
		ExpectOk(ReadExecutableApplication(Output, Payload, E), E, "read executable envelope");
		auto App = FApplication::Open(std::move(Payload), CreateApplicationServices(), E);
		FPreparedApplication Prepared;
		ExpectOk(App && App->Prepare(Prepared, E), E, "prepare executable entry");
		RunPrepared(Prepared, 31);
		Expect(!WriteExecutableApplication(Output, Files, PathUtil::ToUtf8(Work / "nested.exe"), E) && E.Code == "executable-bootstrap", "reject nested native envelopes");
		std::ofstream(PathUtil::FromUtf8(Output), std::ios::binary | std::ios::app).put('\0');
		Expect(!ReadExecutableApplication(Output, Payload, E), "reject unexpected unsigned executable padding");
		#endif
	}
	#if defined(__APPLE__)
	void DarwinPhysicalPaths(const fs::path& Work)
	{
		const auto Alias = fs::path("/tmp") / ("VeroScriptApplication-" + PathUtil::ToUtf8(Work.filename()));
		struct FCleanup
		{
			fs::path Path;
			~FCleanup()
			{
				std::error_code Error;
				fs::remove_all(Path, Error);
			}
		} Cleanup{Alias};
		FDiagnostic Error;
		std::vector<FContentFile> Content;
		const auto Tree = Alias / "content";
		Expect(WriteContentTree({{"value.txt", Bytes("system alias"), false}}, PathUtil::ToUtf8(Tree), Error) &&
		        ReadContentTree(PathUtil::ToUtf8(Tree), Content, Error) && Content.size() == 1 &&
		        Content.front().Bytes == Bytes("system alias"),
		    "root-owned Darwin temporary alias supports application content: " + Error.Message);
		fs::create_directory_symlink(Tree, Alias / "user-link");
		Expect(!ReadContentTree(PathUtil::ToUtf8(Alias / "user-link"), Content, Error) && Error.Code == "physical-link",
		    "user-created root links below the system alias remain forbidden");
		Expect(!WriteArchive({{"value.txt", {}, false}}, PathUtil::ToUtf8(Alias / "user-link/escape.zip"), Error) &&
		        Error.Code == "physical-link", "user-linked ancestors remain forbidden");
		fs::create_directory_symlink(Work, Tree / "redirect");
		Expect(!ReadContentTree(PathUtil::ToUtf8(Tree), Content, Error), "links inside content trees remain forbidden");
	}
	#endif
	void MemoryLibrary()
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    std::string(VeroScript::Test::LambdaStaticDefinitions) +
		        "namespace LibraryFunctions { export T identity<T>(T value){return value;} }"
		        "export int value() { checkLambdaStaticSupport(); return 27; }"
		        "export int readonly_value(){fn()->const int& get=[value=7]()->const int&{return value;};"
		        "const int& result=get();return result;}",
		    "<memory-library>");
		Expect(Compiled.bSuccess, "compile memory library: " + Compiled.Error.Message);
		FPackageBuildOptions Library;
		Library.Kind = EPackageKind::Library;
		Library.PackageName = "memory.library";
		auto Data = std::make_shared<std::vector<std::uint8_t>>();
		std::string Error;
		Expect(BuildPackage(*Compiled.Module, Library, *Data, Error), "build memory library: " + Error);
		FModuleEnvironmentDesc D;
		D.EntryOwnerId = "entry";
		D.Scopes.push_back({"entry", {}, false});
		FResolvedModule M;
		M.Name = "memory.vslib";
		M.Id = "memory.library";
		M.Path = "veroscript-module:/memory/library.vslib";
		M.OwnerId = "entry";
		M.ContentIdentity = ContentSha256(*Data);
		M.Binary = Data;
		D.Modules.push_back(M);
		auto Environment = FModuleEnvironment::Create(std::move(D), Error);
		Expect(bool(Environment), "create memory environment: " + Error);
		FCompileOptions Options;
		VeroScript::Test::Configure(Options);
		Options.ModuleEnvironment = Environment;
		auto Main = VeroScriptCompile::CompileSource(
		    "import \"memory.vslib\"; int main() {"
		    "auto read=[call=LibraryFunctions::identity<int>]()->int{return call(27);};"
		    "assert(read()==27&&readonly_value()==7);return value();}",
		    "<memory-entry>", Options);
		Expect(Main.bSuccess, "import memory library: " + Main.Error.Message);
		FPackageBuildOptions App;
		App.PackageName = "memory.app";
		std::vector<std::uint8_t> Image;
		Expect(BuildPackage(*Main.Module, App, Image, Error), "build linked image: " + Error);
		FPreparedApplication Prepared;
		Prepared.Environment = Environment;
		Prepared.Package = std::make_unique<FPackageData>();
		Expect(ReadPackage(FByteView(Image), *Prepared.Package, Error), "read linked image: " + Error);
		Prepared.LogicalPath = "<memory-image>";
		RunPrepared(Prepared, 27);
		// LoadPackage transfers its module to the Session. Read the same image
		// again so the next Session owns fresh static storage and capture cells.
		Expect(ReadPackage(FByteView(Image), *Prepared.Package, Error), "reread linked image: " + Error);
		RunPrepared(Prepared, 27);
	}
	void BakedImages(const fs::path& Work)
	{
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int next(){static int count=20;return ++count;}"
		    "int main(){auto answer=[base=next()]()->int{return base+2;};return answer();}");
		Expect(Compiled.bSuccess, "compile baked application image");
		FPackageBuildOptions Options;
		Options.PackageName = "baked.image";
		Options.PackageVersion = (1ull << 40) + 7;
		Options.HostMetadata["baked.resources"] = {1, 2, 3};
		std::vector<std::uint8_t> Image;
		std::string Failure;
		Expect(BuildPackage(*Compiled.Module, Options, Image, Failure), "bake application image: " + Failure);
		const auto Original = Image;
		std::vector<FContentFile> Files;
		FDiagnostic Error;
		Expect(BuildApplication(std::move(Image), "0.1.1", {}, Files, Error), "wrap baked image: " + Error.Message);
		std::vector<std::uint8_t> Container;
		Expect(WriteArchive(Files, Container, Error), "write baked application container");
		bool EnvironmentReady = false;
		int Preparations = 0;
		const auto Stdlib = Work / "baked-stdlib";
		fs::create_directories(Stdlib);
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "host.stdlib";
		Description.Scopes.push_back({"host.stdlib", {}, true});
		Description.SourceRoots.push_back({PathUtil::ToUtf8(Stdlib), "host.stdlib"});
		Description.StdlibSearchPaths.push_back(PathUtil::ToUtf8(Stdlib));
		auto Environment = FModuleEnvironment::Create(std::move(Description), Failure);
		Expect(bool(Environment), "prepare the host standard library environment: " + Failure);
		auto Services = CreateApplicationServices();
		Services.StandardLibraryPaths = {PathUtil::ToUtf8(Stdlib / "..") + "/baked-stdlib"};
		Services.LoadExternalEnvironment = [&](bool, const auto&, auto& Out, auto&)
		{
			EnvironmentReady = true;
			Out = Environment;
			return true;
		};
		Services.PreparePackage = [&](const auto& Package, auto&)
		{
			Expect(EnvironmentReady, "compiled host preparation follows environment loading");
			Expect(Package.PackageVersion == Options.PackageVersion && Package.HostMetadata == Options.HostMetadata,
			    "baked version and host metadata survive the application container");
			++Preparations;
			return true;
		};
		auto App = FApplication::Open(Container, Services, Error);
		Expect(bool(App), "open baked application container: " + Error.Message);
		FArtifactData Stored;
		Expect(App->Read("program/entry.bin", Stored, Error) && Stored.Bytes == Original, "baked image remains byte-identical");
		FPackageData Inspected;
		Expect(App->ReadEntryPackage(Inspected, Error) && Preparations == 0, "image inspection does not invoke host preparation");
		FPreparedApplication Prepared;
		Expect(App->Prepare(Prepared, Error) && Preparations == 1, "compiled host preparation runs once");
		Expect(Prepared.Environment->GetDescription().StdlibSearchPaths.size() == 1,
		    "the same standard library root is not republished under a different path spelling");
		RunPrepared(Prepared, 23);
		Services.PreparePackage = [](const auto&, std::string& Error)
		{
			Error = "host cannot prepare this application";
			return false;
		};
		App = FApplication::Open(Container, Services, Error);
		Expect(App && !App->Prepare(Prepared, Error) && !Prepared.Package && !Prepared.Environment &&
		        Error.Code == "application-package",
		    "host preparation failure returns no partially prepared application");
		Expect(!BuildApplication(Bytes("not an image"), "0.1.1", {}, Files, Error) && Files.empty(),
		    "invalid baked images are not wrapped as applications");
	}
	void Containers(const fs::path& Work)
	{
		FDiagnostic Error;
		const std::vector<FContentFile> WithEmpty{
			{"empty.bin", {}, false}, {"payload.bin", {0, 7, 0, 9}, true}};
		std::vector<std::uint8_t> EmptyImage;
		ExpectOk(WriteArchive(WithEmpty, EmptyImage, Error), Error, "write archive with empty member to memory");
		const auto EmptyPath = PathUtil::ToUtf8(Work / "empty-member.zip");
		ExpectOk(WriteArchive(WithEmpty, EmptyPath, Error), Error, "write archive with empty member to file");
		auto MemoryArchive = FArchive::Open(EmptyImage, Error);
		auto FileArchive = FArchive::Open(EmptyPath, Error);
		Expect(MemoryArchive && FileArchive, "open memory and file archives with empty members");
		for (const auto& View : {MemoryArchive, FileArchive})
		{
			std::vector<std::uint8_t> Payload{42};
			ExpectOk(View->Read("empty.bin", Payload, Error), Error, "read empty archive member");
			Expect(Payload.empty(), "empty member replaces previous output bytes");
			ExpectOk(View->Read("payload.bin", Payload, Error), Error, "read nonempty archive member");
			Expect(Payload == WithEmpty[1].Bytes, "empty I/O preserves adjacent member bytes");
		}
		std::string FileDigest;
		ExpectOk(FileSha256(EmptyPath, FileDigest, Error), Error, "hash archive with empty member");
		Expect(FileDigest == ContentSha256(EmptyImage), "memory and file archive bytes are identical");
		auto Compiled = VeroScriptCompile::CompileSource("int main() { return 17; }", "<application>");
		Expect(Compiled.bSuccess, "compile application: " + Compiled.Error.Message);
		FPackageBuildOptions Package;
		Package.PackageName = "application.tests";
		std::vector<FContentFile> Files;
		Expect(BuildApplication(*Compiled.Module, Package, "0.1.1", {{"resources/test.txt", Bytes("hello"), false}},
		           Files, Error),
		    "assemble application: " + Error.Message);
		const auto Archive = PathUtil::ToUtf8(Work / "应用 with spaces.vsapp");
		Expect(WriteArchive(Files, Archive, Error), "write application: " + Error.Message);
		auto App = FApplication::Open(Archive, {}, Error);
		Expect(bool(App), "open application: " + Error.Message);
		FArtifactData Data;
		Expect(App->Read("resources/test.txt", Data, Error) && Data.Bytes == Bytes("hello"), "read resource");
		Expect(!App->Read("../external", Data, Error), "reject undeclared resource");
		FPreparedApplication Prepared;
		Expect(App->Prepare(Prepared, Error), "prepare application: " + Error.Message);
		RunPrepared(Prepared, 17);
		std::ostringstream HostOut, HostErrors;
		const auto EngineResult = VeroScript::Test::FEngine::RunFile(Archive, HostOut, HostErrors);
		Expect(EngineResult.bSuccess && EngineResult.ExitCode == 17, "high-level engine loads application container: " + EngineResult.Error.Message);
		const auto Directory = PathUtil::ToUtf8(Work / "directory");
		Expect(WriteContentTree(Files, Directory, Error), "write directory: " + Error.Message);
		auto Tree = FApplication::Open(Directory, {}, Error);
		Expect(bool(Tree) && Tree->Prepare(Prepared, Error), "directory application: " + Error.Message);
		RunPrepared(Prepared, 17);
		Expect(!AcquireApplicationDirectoryLease(Directory, true, Error), "directory publisher cannot replace active application");
		#if defined(_WIN32)
		const auto ExtendedDirectory = PathUtil::ToUtf8(fs::path(L"\\\\?\\" + PathUtil::FromUtf8(Directory).make_preferred().native()));
		Expect(!AcquireApplicationDirectoryLease(ExtendedDirectory, true, Error), "native path spelling cannot bypass directory leases");
		#endif
		std::vector<std::uint8_t> ModeZip;
		Expect(WriteArchive({{"bin/tool", Bytes("executable"), true}}, ModeZip, Error), "write executable ZIP member");
		auto ModeArchive = FArchive::Open(ModeZip, Error);
		Expect(ModeArchive && ModeArchive->GetEntries().front().Executable, "ZIP preserves executable permissions");
		auto CacheServices = CreateApplicationServices();
		CacheServices.CacheRoot = PathUtil::ToUtf8(Work / "cache");
		auto CachedApp = FApplication::Open(Archive, CacheServices, Error);
		std::string Materialized;
		std::shared_ptr<const void> Lease;
		ExpectOk(CachedApp && CachedApp->Materialize("resources", Materialized, Lease, Error), Error, "materialize resource snapshot");
		Lease.reset();
		std::ofstream(PathUtil::FromUtf8(Materialized) / "test.txt", std::ios::binary) << "corrupt";
		ExpectOk(CachedApp->Materialize("resources", Materialized, Lease, Error), Error, "repair owned corrupt materialization");
		std::ifstream Resource(PathUtil::FromUtf8(Materialized) / "test.txt");
		std::string Repaired;
		Resource >> Repaired;
		Expect(Repaired == "hello", "materialization repair restores authenticated stored bytes");
		std::vector<FContentFile> Exported;
		Expect(App->Export(Exported, Error), "export stored application");
		const auto Copy = PathUtil::ToUtf8(Work / "copy.vsapp");
		Expect(WriteArchive(Exported, Copy, Error), "repack application");
		std::string First, Second;
		Expect(FileSha256(Archive, First, Error) && FileSha256(Copy, Second, Error) && First == Second,
		    "deterministic application container");
		std::ofstream(PathUtil::FromUtf8(Directory) / "resources/test.txt", std::ios::binary) << "changed";
		Expect(!Tree->Read("resources/test.txt", Data, Error), "reject changed directory content");
		for (const auto& Path : {"../evil", "/evil", "a/../b", "a\\b", "C:/evil", "CON.txt", "a./b", "a//b"})
		{
			std::string Failure;
			Expect(!ValidateContentPath(Path, Failure), "reject path: " + std::string(Path));
		}
		Expect(!WriteArchive({{"A.txt", {}, false}, {"a.txt", {}, false}}, PathUtil::ToUtf8(Work / "duplicate.zip"), Error),
		    "reject case-ambiguous archive");
		Expect(!WriteArchive({{"a", {}, false}, {"a/b", {}, false}}, PathUtil::ToUtf8(Work / "prefix.zip"), Error),
		    "reject file/directory prefix conflict");
		#if defined(_WIN32)
		Expect(!WriteContentTree({{"Å/one", Bytes("one"), false}, {"å/two", Bytes("two"), false}},
		           PathUtil::ToUtf8(Work / "directory-alias"), Error) &&
		        Error.Code == "content-collision",
		    "distinct logical content directories must not merge on the staging filesystem");
		std::vector<FContentFile> Aliased;
		Expect(BuildApplication(*Compiled.Module, Package, "0.1.1",
		           {{"resources/Å/one", Bytes("one"), false}, {"resources/å/two", Bytes("two"), false}}, Aliased, Error),
		    "assemble logical content with filesystem aliases");
		std::vector<std::uint8_t> AliasedBytes;
		Expect(WriteArchive(Aliased, AliasedBytes, Error), "store distinct logical paths in an archive");
		const auto AliasedApp = FApplication::Open(std::move(AliasedBytes), CacheServices, Error);
		std::string AliasedRoot;
		std::shared_ptr<const void> AliasedLease;
		Expect(AliasedApp && !AliasedApp->Materialize("resources", AliasedRoot, AliasedLease, Error) &&
		        Error.Code == "content-collision" && AliasedRoot.empty() && !AliasedLease,
		    "materialization rejects directory aliases without retrying a corrupt snapshot");
		#endif
		const auto LongName = std::string(120, 'a') + "/" + std::string(120, 'b') + "/value.txt";
		const auto LongRoot = PathUtil::ToUtf8(Work / "long-content");
		Expect(WriteContentTree({{LongName, Bytes("long path"), false}}, LongRoot, Error), "write long content paths: " + Error.Message);
		std::vector<FContentFile> LongContent;
		Expect(ReadContentTree(LongRoot, LongContent, Error) && LongContent.size() == 1 &&
		        LongContent.front().Path == LongName && LongContent.front().Bytes == Bytes("long path"),
		    "long content paths retain their identity and bytes");
		auto Descriptor = J::parse(App->GetDescriptorJson());
		Descriptor["entry"] = {{"kind", "source"}, {"path", "program/main.vs"}};
		Files.clear();
		Expect(AssembleApplication(Descriptor.dump(), {{"program/main.vs", Bytes("import \"helper.vs\"; int main() { return helper(); }"), false}, {"program/helper.vs", Bytes("export int helper() { return 38; }"), false}}, Files, Error), "assemble source application");
		const auto Source = PathUtil::ToUtf8(Work / "source.vsapp");
		Expect(WriteArchive(Files, Source, Error), "write source application");
		auto SourceApp = FApplication::Open(Source, {}, Error);
		Expect(bool(SourceApp) && SourceApp->Prepare(Prepared, Error), "prepare source application: " + Error.Message);
		RunPrepared(Prepared, 38);
		FArtifactData Protected;
		Protected.Bytes = Bytes("VSAEbad");
		std::string Failure;
		Expect(!DecodeArtifact({"memory", "library", {}}, {}, Protected, Failure), "missing decoder rejects encryption");
	}
	void Processes(const std::string& Self)
	{
		FProcessRequest Q;
		Q.Executable = PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(Self)));
		Q.WorkingDirectory = PathUtil::ToUtf8(fs::absolute(PathUtil::FromUtf8(Self)).parent_path());
		Q.Arguments = {"--process-child", "with space and \"quotes\"\\"};
		std::string Captured;
		Q.Output = [&](std::string_view Text, bool)
		{
			Captured.append(Text);
		};
		int Code = -1;
		FDiagnostic E;
		Expect(RunProcess(Q, Code, E) && Code == 7 && Captured.find(Q.Arguments[1]) != std::string::npos,
		    "process output, argument quoting and exit code: " + E.Message);
		Q.MaxOutputBytes = 8;
		Expect(!RunProcess(Q, Code, E) && E.Code == "process-output", "process output budget terminates child");
		Q.MaxOutputBytes = 4096;
		Q.Arguments = {"--process-child", "loop"};
		Q.TimeoutMilliseconds = 40;
		Expect(!RunProcess(Q, Code, E) && E.Code == "process-timeout", "process deadline terminates child");
	}
	void Reloading(const fs::path& Work)
	{
		FDiagnostic E;
		const auto Make = [&](const std::string& Source)
		{
			auto C = VeroScriptCompile::CompileSource(Source);
			Expect(C.bSuccess, "compile reload image: " + C.Error.Message);
			FPackageBuildOptions O;
			O.PackageName = "reload.app";
			std::vector<FContentFile> Files;
			std::vector<std::uint8_t> Bytes;
			Expect(BuildApplication(*C.Module, O, "0.1.1", {}, Files, E) && WriteArchive(Files, Bytes, E), "build reload image");
			return FApplication::Open(std::move(Bytes), CreateApplicationServices(), E);
		};
		std::ostringstream Out, Error;
		FStreamEngineHost Host(Out, Error);
		VeroScriptSession S(Host);
		VeroScript::Test::Configure(S);
		S.SetHotReloadEnabled(true);
		Expect(S.LoadApplication(Make("int counter = 1; int main() { counter += 1; return counter; }")).bSuccess, "load initial application");
		Expect(S.Run().ExitCode == 2, "initial persistent state");
		const auto OldEnvironment = S.GetModuleEnvironment();
		Expect(S.ReloadApplication(Make("int counter = 100; int main() { counter += 2; return counter; }")).bSuccess, "stage application reload");
		Expect(S.Run().ExitCode == 4, "application reload preserves state");
		const auto CurrentEnvironment = S.GetModuleEnvironment();
		Expect(OldEnvironment != CurrentEnvironment, "successful reload publishes new content environment");
		Expect(S.ReloadApplication(Make("int zero() { return 0; } int counter = 100; int broken = 1 / zero(); int main() { return counter; }")).bSuccess, "stage failing initializer");
		Expect(!S.Run().bSuccess && S.GetModuleEnvironment() == CurrentEnvironment, "failed reload restores original environment");
		Expect(S.Run().ExitCode == 6, "old application remains usable after rollback");
		(void)Work;
	}
	std::vector<std::uint8_t> Hex(std::string_view S)
	{
		std::vector<std::uint8_t> Out;
		for (std::size_t I = 0; I < S.size(); I += 2)
		{
			Out.push_back(static_cast<std::uint8_t>(std::stoul(std::string(S.substr(I, 2)), nullptr, 16)));
		}
		return Out;
	}
	class FTestKeys final : public IProtectionKeys
	{
	public:
		std::string GetIdentity() const override
		{
			return "test.keys.0.1.1";
		}
		bool Get(const std::string& Id, const std::string& Version, const std::string& Purpose, FProtectionKey& Out, std::string& Error) const override
		{
			Out = {};
			Out.Id = Id;
			Out.Version = Version;
			if (Version != "1")
			{
				Error = "unknown key version";
				return false;
			}
			if (Id == "content" && (Purpose == "encrypt" || Purpose == "decrypt"))
			{
				Out.Secret.assign(32, 0x15);
				return true;
			}
			if ((Id == "signing" || Id == "alias") && (Purpose == "sign" || Purpose == "verify"))
			{
				// RFC 8032 test key; never a production credential.
				Out.Public = Hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
				if (Purpose == "sign")
				{
					Out.Secret = Hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
					Out.Secret.insert(Out.Secret.end(), Out.Public.begin(), Out.Public.end());
				}
				return true;
			}
			Error = "unknown key or purpose";
			return false;
		}
	};
	void Protection(const std::string& Library, const fs::path& Work)
	{
		FDiagnostic E;
		auto Crypto = LoadSodiumCrypto(Library, E);
		Expect(bool(Crypto), "load sodium: " + E.Message);
		auto Keys = std::make_shared<FTestKeys>();
		auto Writer = std::make_shared<FProtection>(Crypto, Keys);
		const auto Plain = Bytes("private payload");
		std::vector<std::uint8_t> Encrypted, Other;
		Expect(Writer->Encrypt(Plain, "resource", "content", "1", Encrypted, E), "encrypt: " + E.Message);
		Expect(Writer->Encrypt(Plain, "resource", "content", "1", Other, E) && Other != Encrypted, "fresh nonce per encryption");
		FArtifactData D;
		D.Bytes = Encrypted;
		std::string Error;
		Expect(Writer->Decode({"value", "resource", {}}, {}, D, Error) && D.Bytes == Plain, "authenticated decrypt: " + Error);
		D.Bytes = Encrypted;
		D.Bytes.back() ^= 1;
		Expect(!Writer->Decode({"value", "resource", {}}, {}, D, Error), "reject altered AEAD tag");
		D.Bytes = Encrypted;
		D.Bytes.push_back(0);
		Expect(!Writer->Decode({"value", "resource", {}}, {}, D, Error), "reject trailing envelope data");
		D.Bytes = Encrypted;
		Expect(!Writer->Decode({"value", "library", {}}, {}, D, Error), "reject envelope kind mismatch");
		std::string Signatures;
		Expect(Writer->Sign(Encrypted, "resource", "signing", "1", Signatures, E), "sign artifact");
		FTrustPolicy Policy;
		Policy.RequireSignature = true;
		auto Reader = std::make_shared<FProtection>(Crypto, Keys, Policy);
		Expect(Reader->Verify(Encrypted, "resource", Signatures, E), "trusted signature verification: " + E.Message);
		Expect(!Reader->Verify(Encrypted, "library", Signatures, E), "reject signature kind mismatch");
		Other = Encrypted;
		Other.back() ^= 1;
		Expect(!Reader->Verify(Other, "resource", Signatures, E), "reject signed byte changes");
		std::string Alias;
		Expect(Writer->Sign(Encrypted, "resource", "alias", "1", Alias, E), "sign key alias");
		auto Records = J::parse(Signatures);
		Records.push_back(J::parse(Alias).at(0));
		Policy.MinimumSignatures = 2;
		FProtection Threshold(Crypto, Keys, Policy);
		Expect(!Threshold.Verify(Encrypted, "resource", Records.dump(), E), "aliases of one public key cannot meet a two-key threshold");
		auto LibraryCode = VeroScriptCompile::CompileSource("export T identity<T>(T value) { return value; }");
		Expect(LibraryCode.bSuccess, "compile generic library");
		FPackageBuildOptions LibraryOptions;
		LibraryOptions.Kind = EPackageKind::Library;
		LibraryOptions.PackageName = "protected.library";
		std::vector<std::uint8_t> PlainLibrary, CipherLibrary;
		std::string Failure, LibrarySignature;
		Expect(BuildPackage(*LibraryCode.Module, LibraryOptions, PlainLibrary, Failure) && Writer->Encrypt(PlainLibrary, "library", "content", "1", CipherLibrary, E) &&
		        Writer->Sign(CipherLibrary, "library", "signing", "1", LibrarySignature, E),
		    "encrypt and sign generic library");
		class FLibraryReader final : public IArtifactReader
		{
		public:
			std::vector<std::uint8_t> Bytes;
			std::string Signature;
			std::string GetIdentity() const override
			{
				return ContentSha256(Bytes) + ContentSha256(Signature);
			}
			bool Read(const FArtifactRequest&, const FArtifactWork&, FArtifactData& D, std::string&) const override
			{
				D = {};
				D.Bytes = Bytes;
				D.SignatureRecords = Signature;
				return true;
			}
		};
		auto LibraryReader = std::make_shared<FLibraryReader>();
		LibraryReader->Bytes = CipherLibrary;
		LibraryReader->Signature = LibrarySignature;
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "entry";
		Description.Scopes.push_back({"entry", {}, false});
		FResolvedModule Mapping;
		Mapping.Name = "protected.vslib";
		Mapping.Id = "protected.library";
		Mapping.Path = "veroscript-module:/protected/library.vslib";
		Mapping.OwnerId = "entry";
		Mapping.ContentIdentity = ContentSha256(CipherLibrary);
		Description.Modules.push_back(Mapping);
		Description.ArtifactReader = LibraryReader;
		Description.ArtifactDecoder = Reader;
		auto LibraryEnvironment = FModuleEnvironment::Create(std::move(Description), Failure);
		Expect(bool(LibraryEnvironment), "protected module environment: " + Failure);
		FCompileOptions CompileOptions;
		VeroScript::Test::Configure(CompileOptions);
		CompileOptions.ModuleEnvironment = LibraryEnvironment;
		auto Linked = VeroScriptCompile::CompileSource("import \"protected.vslib\"; int main() { return identity<int>(39); }", "<protected-main>", CompileOptions);
		Expect(Linked.bSuccess, "protected generic library import: " + Linked.Error.Message);
		FPreparedApplication LinkedProgram;
		LinkedProgram.Environment = LibraryEnvironment;
		LinkedProgram.Package = std::make_unique<FPackageData>();
		LinkedProgram.Package->Module = std::move(Linked.Module);
		RunPrepared(LinkedProgram, 39);
		auto Compiled = VeroScriptCompile::CompileSource("int main() { return 51; }");
		Expect(Compiled.bSuccess, "compile protected app");
		FPackageBuildOptions Options;
		Options.PackageName = "protected.app";
		FProtectionOptions Protect;
		Protect.EncryptionKey = "content";
		Protect.EncryptionVersion = "1";
		Protect.SigningKey = "signing";
		Protect.SigningVersion = "1";
		std::vector<FContentFile> Files;
		Expect(BuildApplication(*Compiled.Module, Options, "0.1.1", {{"resources/value", Encrypted, false}, {"resources/value.sig.json", Bytes(Signatures), false}}, Files, E, {}, Protect, Writer.get()), "assemble protected app: " + E.Message);
		std::vector<std::uint8_t> Archive;
		Expect(WriteArchive(Files, Archive, E), "write protected app in memory");
		auto Services = CreateApplicationServices();
		Services.Protection = Reader;
		auto App = FApplication::Open(Archive, Services, E);
		Expect(bool(App), "verify protected container: " + E.Message);
		FArtifactData Resource;
		Expect(App->Read("resources/value", Resource, E) && Resource.Bytes == Plain, "protected resource verifies its independent signature and decrypts");
		FPreparedApplication Prepared;
		Expect(App->Prepare(Prepared, E), "prepare protected app: " + E.Message);
		RunPrepared(Prepared, 51);
		Expect(!FApplication::Open(Archive, {}, E), "signed app without trust service is rejected");
		const auto Broken = Work / "broken-keyring.json";
		std::ofstream(Broken) << "{\"secret\":\"never-print-this-key\", invalid}";
		Expect(!LoadProtectionKeys(PathUtil::ToUtf8(Broken), E) && E.Message.find("never-print") == std::string::npos, "keyring diagnostics redact key text");
	}
}
int main(int Argc, char** Argv)
{
	try
	{
		if (Argc == 3 && std::string(Argv[1]) == "--process-child")
		{
			if (std::string(Argv[2]) == "loop")
			{
				for (;;)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
			std::cout << Argv[2] << '\n';
			std::cerr << "child stderr\n";
			return 7;
		}
		if (Argc != 2 && Argc != 3)
		{
			throw std::runtime_error("expected a fresh absolute test directory");
		}
		const auto Root = std::filesystem::weakly_canonical(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))) /
		    ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (std::filesystem::exists(Root))
		{
			throw std::runtime_error("test directory already exists");
		}
		std::filesystem::create_directories(Root);
		#if defined(__APPLE__)
		DarwinPhysicalPaths(Root);
		#endif
		Expect(J::parse(HostApplicationTargetJson()).at("abis").at("vm") == BuildVmModuleAbi(),
		    "application targets include the independent VM ABI used by package deployments");
		MemoryLibrary();
		BakedImages(Root);
		Containers(Root);
		const auto Executable = PathUtil::ToUtf8(std::filesystem::canonical(PathUtil::FromUtf8(Argv[0])));
		ExecutableEnvelope(Executable, Root);
		Processes(Executable);
		Reloading(Root);
		if (Argc == 3)
		{
			Protection(Argv[2], Root);
		}
		std::cout << Assertions << " application assertions passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << "application tests: " << E.what() << '\n';
		return 1;
	}
}
