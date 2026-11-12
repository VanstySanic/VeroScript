// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BuildInternal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptPath.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptExecutable.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace VeroScript::Build
{
	using namespace Detail;
	namespace
	{
		constexpr const char* ReceiptName = ".veroscript-build.json";
		class FOutputLease
		{
		public:
			explicit FOutputLease(const fs::path& Path)
			{
				Physical(Path, true);
				#if defined(_WIN32)
				Handle = CreateFileW(Path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				OVERLAPPED Offset{};
				if (Handle == INVALID_HANDLE_VALUE || !LockFileEx(Handle, LOCKFILE_FAIL_IMMEDIATELY | LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &Offset))
				{
					if (Handle != INVALID_HANDLE_VALUE)
					{
						CloseHandle(Handle);
					}
					Fail("output-busy", "build output is held by another build or artifact lease", NativePathText(Path));
				}
				#else
				Handle = ::open(Path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
				if (Handle < 0 || ::flock(Handle, LOCK_EX | LOCK_NB) != 0)
				{
					if (Handle >= 0)
					{
						::close(Handle);
					}
					Fail("output-busy", "build output is held by another build or artifact lease", NativePathText(Path));
				}
				#endif
			}
			~FOutputLease()
			{
				#if defined(_WIN32)
				OVERLAPPED Offset{};
				UnlockFileEx(Handle, 0, 1, 0, &Offset);
				CloseHandle(Handle);
				#else
				::flock(Handle, LOCK_UN);
				::close(Handle);
				#endif
			}

		private:
			#if defined(_WIN32)
			HANDLE Handle = INVALID_HANDLE_VALUE;
			#else
			int Handle = -1;
			#endif
		};
		fs::path TargetRoot(const FBuildPlan::FImpl& P, const std::string& N)
		{
			return PathUtil::FromUtf8(NativePathText(PathUtil::FromUtf8(P.OutputRoot) / P.Request.Profile / P.PlatformId / N));
		}
		FArtifact Resolve(const J& V, const FBuildPlan::FImpl& P,
		    const std::map<std::string, std::map<std::string, FArtifact>>& Produced,
		    std::map<std::string, std::string>& Inputs)
		{
			FArtifact A;
			if (V.is_string())
			{
				A.Path = NativePathText(Absolute(V.get<std::string>(), PathUtil::FromUtf8(P.Request.ProjectRoot)));
				A.Type = fs::is_directory(PathUtil::FromUtf8(A.Path)) ? "directory" : "file";
				A.Kind = "generic";
			}
			else
			{
				const auto T = V.at("target").get<std::string>(), O = V.at("output").get<std::string>();
				if (!Produced.count(T) || !Produced.at(T).count(O))
				{
					Fail("artifact-missing", "upstream artifact was not produced", T + ":" + O);
				}
				A = Produced.at(T).at(O);
				if (V.contains("path"))
				{
					const auto Child = PathUtil::FromUtf8(A.Path) / PathUtil::FromUtf8(V.at("path").get<std::string>());
					Physical(Child);
					A.Path = NativePathText(Child);
					A.Type = fs::is_directory(Child) ? "directory" : "file";
					if (A.Kind == "source-tree")
					{
						A.Kind = A.Type == "directory" ? "source-tree" : "source";
					}
					A.SignaturePath.clear();
				}
			}
			Snapshot(PathUtil::FromUtf8(A.Path), Inputs, P.Request.Limits);
			if (A.Type == "file" && A.SignaturePath.empty() && fs::is_regular_file(PathUtil::FromUtf8(A.Path + ".sig.json")))
			{
				A.SignaturePath = A.Path + ".sig.json";
			}
			if (!A.SignaturePath.empty())
			{
				Snapshot(PathUtil::FromUtf8(A.SignaturePath), Inputs, P.Request.Limits);
			}
			A.Identity = Inputs.at(A.Path);
			return A;
		}
		std::vector<std::uint8_t> Data(std::string_view S)
		{
			return {S.begin(), S.end()};
		}
		std::string Text(const std::vector<std::uint8_t>& B)
		{
			return {B.begin(), B.end()};
		}
		void LauncherRuntime(const std::vector<Application::FContentFile>& Content, const std::string& Runtime,
		    const fs::path& Launcher, const fs::path& Root, const FLimits& Limits)
		{
			if (Runtime.empty()) return;
			const auto Publish = [&](const fs::path& Relative, const std::vector<std::uint8_t>& Bytes)
			{
				const auto Path = Root / Relative;
				const auto Value = Text(Bytes);
				if (fs::exists(Path))
				{
					if (Read(Path, Limits) != Value)
					{
						Fail("launcher-runtime-conflict", "launcher runtime dependency has conflicting content", PathUtil::ToUtf8(Relative));
					}
				}
				else
				{
					Write(Path, Value);
				}
			};
			for (const auto& File : Content)
			{
				if (Limits.Content.CheckCancelled)
				{
					Limits.Content.CheckCancelled();
				}
				if (File.Path.rfind(Runtime + "/", 0) != 0)
				{
					continue;
				}
				const auto Path = PathUtil::FromUtf8(File.Path);
				auto Name = NativePathText(Path.filename());
				std::transform(Name.begin(), Name.end(), Name.begin(), [](unsigned char C)
				{
					return static_cast<char>(std::tolower(C));
				});
				if (Name.size() < 4 || Name.compare(Name.size() - 4, 4, ".dll") != 0 || (Name.rfind("msvcp", 0) != 0 && Name.rfind("vcruntime", 0) != 0 && Name.rfind("concrt", 0) != 0 && Name.rfind("vcomp", 0) != 0))
				{
					continue;
				}
				Publish(Launcher.parent_path() / Path.filename(), File.Bytes);
				const auto Notice = PathUtil::ToUtf8(Path.parent_path() / "licenses/MicrosoftVisualCpp/NOTICE.txt");
				const auto Found = std::find_if(Content.begin(), Content.end(), [&](const auto& F)
				{
					return F.Path == Notice;
				});
				if (Found != Content.end())
				{
					Publish(Launcher.parent_path() / "licenses/MicrosoftVisualCpp/NOTICE.txt", Found->Bytes);
				}
			}
		}
		Application::FApplicationServices ApplicationServices(const FBuildPlan::FImpl& P)
		{
			auto Services = P.Services.Application;
			if (P.Services.Protection)
			{
				Services.Protection = P.Services.Protection;
			}
			// Inspect and compile content for the declared build target. Runtime
			// compatibility with the executing host is checked when the app is run.
			Services.ValidateTarget = [Target = P.Platform, Limits = P.Request.Limits](const std::string& Json, std::string& Error)
			{
				if (Parse(Json, Limits) != Target)
				{
					Error = "application target does not match the selected build target";
					return false;
				}
				Error.clear();
				return true;
			};
			return Services;
		}
		void VerifyInputs(const std::map<std::string, std::string>& Inputs, const FLimits& L)
		{
			for (const auto& I : Inputs)
			{
				if (L.Content.CheckCancelled)
				{
					L.Content.CheckCancelled();
				}
				if (Fingerprint(PathUtil::FromUtf8(I.first), L) != I.second)
				{
					Fail("plan-stale", "build input changed after it was frozen", I.first);
				}
			}
		}
		void CheckOwned(const fs::path& Root, const std::string& Owner, const FLimits& L)
		{
			Physical(Root);
			const auto Receipt = Parse(Read(Root / ReceiptName, L), L);
			if (Receipt.value("format", "") != "veroscript-build-output" || Receipt.value("formatVersion", "") != "0.1.1" || Receipt.at("owner") != Owner)
			{
				Fail("output-owner", "build will not replace an unowned output directory", NativePathText(Root));
			}
			std::vector<fs::path> Allowed;
			for (const auto& O : Receipt.at("outputs"))
			{
				const auto Rel = O.at("path").get<std::string>();
				Safe(Rel);
				const auto Path = Root / PathUtil::FromUtf8(Rel);
				if (O.at("sha256") != Fingerprint(Path, L))
				{
					Fail("output-modified", "previous output was modified outside this build", NativePathText(Path));
				}
				Allowed.push_back(Path);
			}
			const auto Base = TreePath(Root);
			for (const auto& F : fs::recursive_directory_iterator(Base))
			{
				if (L.Content.CheckCancelled)
				{
					L.Content.CheckCancelled();
				}
				Physical(F.path());
				if (F.is_directory() || F.path() == Base / ReceiptName)
				{
					continue;
				}
				if (std::none_of(Allowed.begin(), Allowed.end(), [&](const auto& P)
				{
					return Under(F.path(), P);
				}))
				{
					Fail("output-unowned-file", "output directory contains an unowned file", NativePathText(F.path()));
				}
			}
		}
		void Files(const J& Specs, const fs::path& Staging, const FBuildPlan::FImpl& P,
		    const std::map<std::string, std::map<std::string, FArtifact>>& Produced, std::map<std::string, std::string>& Inputs)
		{
			for (const auto& F : Specs)
			{
				const auto A = Resolve(F.at("from"), P, Produced, Inputs);
				const auto Signature = F.contains("signature") ? Resolve(F.at("signature"), P, Produced, Inputs).Path : A.SignaturePath;
				const auto Destination = Staging / PathUtil::FromUtf8(F.at("to").get<std::string>());
				Copy(PathUtil::FromUtf8(A.Path), Destination, P.Request.Limits);
				bool Resigned = false;
				if (F.contains("protection"))
				{
					const auto Protection = Detail::Protection(F.at("protection"), false);
					if (!P.Services.Protection || A.Type != "file")
					{
						Fail("resource-protection", "protected file mapping requires a protection service and file input");
					}
					auto Bytes = Data(Read(Destination, P.Request.Limits));
					Application::FDiagnostic E;
					if (!Signature.empty())
					{
						auto SignatureLimits = P.Request.Limits;
						SignatureLimits.Content.MaxFileBytes = SignatureLimits.Content.MaxMetadataBytes;
						Require(P.Services.Protection->Verify(Bytes, "resource", Read(PathUtil::FromUtf8(Signature), SignatureLimits), E, P.Request.Limits.Content), E);
					}
					if (!Protection.EncryptionKey.empty())
					{
						if (!Signature.empty() && Protection.SigningKey.empty())
						{
							Fail("derived-signature", "encrypting a signed resource requires an explicit new signature policy", A.Path);
						}
						FArtifactData Decoded;
						Decoded.Bytes = std::move(Bytes);
						// The original signature was checked above; decode existing ciphertext once.
						Decoded.TrustIdentity = P.Services.Protection->GetIdentity();
						std::string Failure;
						if (!DecodeArtifact({A.Path, "resource", {}}, {P.Request.Limits.Content.MaxFileBytes, P.Request.Limits.Content.CheckCancelled}, Decoded, Failure, P.Services.Protection.get()))
						{
							Fail("resource-decode", Failure, A.Path);
						}
						std::vector<std::uint8_t> Cipher;
						Require(P.Services.Protection->Encrypt(Decoded.Bytes, "resource", Protection.EncryptionKey, Protection.EncryptionVersion, Cipher, E, P.Request.Limits.Content), E);
						Bytes = std::move(Cipher);
						fs::remove(Destination);
						Write(Destination, Text(Bytes));
					}
					if (!Protection.SigningKey.empty())
					{
						std::string Signatures;
						Require(P.Services.Protection->Sign(Bytes, "resource", Protection.SigningKey, Protection.SigningVersion, Signatures, E, P.Request.Limits.Content), E);
						Write(PathUtil::FromUtf8(NativePathText(Destination) + ".sig.json"), Signatures);
						Resigned = true;
					}
				}
				if (!Signature.empty() && !Resigned)
				{
					Copy(PathUtil::FromUtf8(Signature), PathUtil::FromUtf8(NativePathText(Destination) + ".sig.json"), P.Request.Limits);
				}
			}
		}
		std::string Version(const FBuildPlan::FImpl& P, const J& T)
		{
			const auto Options = T.value("options", J::object());
			if (Options.contains("version")) return Options.at("version");
			const auto Manifest = PathUtil::FromUtf8(P.Request.ProjectRoot) / "veroscript-project.json";
			if (!fs::exists(Manifest))
			{
				Fail("application-version", "application needs options.version or a project manifest version");
			}
			return Parse(Read(Manifest, P.Request.Limits), P.Request.Limits).at("version");
		}
		J BaseDescriptor(const FBuildPlan::FImpl& P, const J& T, const J& Entry)
		{
			J Required = J::array();
			const auto Environment = P.Services.Compile.ModuleEnvironment;
			if (Environment)
			{
				for (const auto& D : Environment->GetDescription().RequiredDependencies)
				{
					Required.push_back({{"id", D.Id}, {"version", D.Version}, {"abi", D.Abi}, {"buildIdentity", D.BuildIdentity}, {"contentIdentity", D.ContentIdentity}});
				}
			}
			return {{"format", "veroscript-application"}, {"formatVersion", "0.1.1"},
			    {"id", T.value("options", J::object()).value("applicationId", T.at("packageName").get<std::string>())},
			    {"version", Version(P, T)}, {"entry", Entry}, {"platform", P.Platform},
			    {"requirements", {{"coreVersion", "0.1.1"}, {"capabilities", J::array()}}},
			    {"runtime", {{"mode", "external"}}}, {"environment", {{"mode", "external"}, {"requirements", Required}}},
			    {"services", J::object()}, {"resources", J::array()}, {"files", "veroscript-app.files.json"}, {"extensions", J::object()}};
		}
		FCompileResult Compile(const FArtifact& Entry, const J& T, const FBuildPlan::FImpl& P,
		    const std::map<std::string, std::map<std::string, FArtifact>>& Produced,
		    std::map<std::string, std::string>& Inputs, const std::function<void()>& Check)
		{
			auto O = P.Services.Compile;
			const auto Settings = T.value("compile", J::object());
			std::vector<std::string> Imports;
			std::vector<std::string> Stdlib;
			const auto Defines = Settings.value("defines", std::vector<std::string>{});
			for (const auto& I : O.ImportPaths)
			{
				Imports.emplace_back(I);
			}
			for (const auto& I : O.StdlibSearchPaths)
			{
				Stdlib.emplace_back(I);
			}
			for (const auto& I : Settings.value("importPaths", std::vector<std::string>{}))
			{
				Imports.push_back(I);
			}
			for (const auto& I : Settings.value("stdlibSearchPaths", std::vector<std::string>{}))
			{
				Stdlib.push_back(I);
			}
			O.ImportPaths.clear();
			O.StdlibSearchPaths.clear();
			for (auto& I : Imports)
			{
				I = NativePathText(Absolute(I, PathUtil::FromUtf8(P.Request.ProjectRoot)));
			}
			for (auto& I : Stdlib)
			{
				I = NativePathText(Absolute(I, PathUtil::FromUtf8(P.Request.ProjectRoot)));
			}
			for (const auto& I : Imports)
			{
				O.ImportPaths.emplace_back(I);
			}
			for (const auto& I : Stdlib)
			{
				O.StdlibSearchPaths.emplace_back(I);
			}
			for (const auto& D : Defines)
			{
				O.Defines.emplace_back(D);
			}
			O.bAllowCircularImports = Settings.value("allowCircularImports", O.bAllowCircularImports);
			O.bAllowProjectWideAccess = Settings.value("allowProjectWideAccess", O.bAllowProjectWideAccess);
			FModuleEnvironmentDesc D;
			if (O.ModuleEnvironment)
			{
				D = O.ModuleEnvironment->GetDescription();
			}
			if (D.EntryOwnerId.empty())
			{
				D.EntryOwnerId = "build.project";
				D.Scopes.push_back({D.EntryOwnerId, {}, false});
			}
			const auto AddRoot = [&](const std::string& Root)
			{
				if (std::none_of(D.SourceRoots.begin(), D.SourceRoots.end(), [&](const auto& R)
				{
					return Under(PathUtil::FromUtf8(Root), PathUtil::FromUtf8(R.Path));
				}))
				{
					D.SourceRoots.push_back({Root, D.EntryOwnerId});
				}
			};
			AddRoot(P.Request.ProjectRoot);
			AddRoot(NativePathText(PathUtil::FromUtf8(Entry.Path).parent_path()));
			for (const auto& I : Imports)
			{
				AddRoot(I);
			}
			for (const auto& I : O.StdlibSearchPaths)
			{
				const auto Root = std::string(I);
				if (std::find(D.StdlibSearchPaths.begin(), D.StdlibSearchPaths.end(), Root) == D.StdlibSearchPaths.end())
				{
					const auto Scope = "build.stdlib." + Application::ContentSha256(Root);
					D.Scopes.push_back({Scope, {}, true});
					D.SourceRoots.push_back({Root, Scope});
					D.StdlibSearchPaths.push_back(Root);
				}
			}
			const auto Modules = T.value("modules", J::object());
			for (auto I = Modules.begin(); I != Modules.end(); ++I)
			{
				const auto A = Resolve(I.value(), P, Produced, Inputs);
				FResolvedModule M;
				M.Name = I.key();
				M.Id = "build.module." + Application::ContentSha256(A.Path);
				M.Path = A.Path;
				M.OwnerId = D.EntryOwnerId;
				M.ContentIdentity = A.Identity;
				auto Extension = NativePathText(PathUtil::FromUtf8(A.Path).extension());
				std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
				{
					return static_cast<char>(std::tolower(C));
				});
				if (A.Kind == "library" || Extension == ".vslib")
				{
					M.Binary = std::make_shared<const std::vector<std::uint8_t>>(Data(Read(PathUtil::FromUtf8(A.Path), P.Request.Limits)));
				}
				else
				{
					M.Source = Read(PathUtil::FromUtf8(A.Path), P.Request.Limits);
				}
				D.Modules.push_back(std::move(M));
			}
			if (P.Services.Protection)
			{
				D.ArtifactDecoder = P.Services.Protection;
			}
			D.bStrictSources = true;
			D.bAllowAmbientSearch = false;
			std::string Error;
			O.ModuleEnvironment = FModuleEnvironment::Create(std::move(D), Error);
			if (!O.ModuleEnvironment)
			{
				Fail("compile-environment", Error);
			}
			for (const auto& I : Inputs)
			{
				if (PathUtil::FromUtf8(I.first).extension() == ".vs" && fs::is_regular_file(PathUtil::FromUtf8(I.first)) &&
				    std::none_of(O.SourceOverlays.begin(), O.SourceOverlays.end(), [&](const auto& V)
				{
					return PathUtil::Equal(V.Path, I.first);
				}))
				{
					O.SourceOverlays.push_back({I.first, Read(PathUtil::FromUtf8(I.first), P.Request.Limits)});
				}
			}
			const auto Previous = O.CompileProcessing.CheckCancelled;
			O.CompileProcessing.CheckCancelled = [&]()
			{
				Check();
				if (Previous)
				{
					Previous();
				}
			};
			O.AnnotationProcessing.CheckCancelled = O.CompileProcessing.CheckCancelled;
			FAnalysisControlScope Scope([&]()
			{
				Check();
				return false;
			});
			auto C = VeroScriptCompile::CompileFile(Entry.Path, O);
			if (!C.bSuccess)
			{
				Fail("compile", C.Error.Message, Entry.Path);
			}
			for (const auto& Path : C.LoadedModulePaths)
			{
				if (PathUtil::FromUtf8(Path).is_absolute() && fs::exists(PathUtil::FromUtf8(Path)))
				{
					Snapshot(PathUtil::FromUtf8(Path), Inputs, P.Request.Limits);
				}
			}
			if (C.PreparedProgram)
			{
				for (const auto& Dp : C.PreparedProgram->GetDependencies())
				{
					if (Dp.bExists && PathUtil::FromUtf8(Dp.Path).is_absolute() && fs::exists(PathUtil::FromUtf8(Dp.Path)))
					{
						Snapshot(PathUtil::FromUtf8(Dp.Path), Inputs, P.Request.Limits);
					}
				}
			}
			return C;
		}
		void ProtectFile(const fs::path& File, const std::string& Kind, const Application::FProtectionOptions& Options,
		    const FBuildPlan::FImpl& P)
		{
			if (Options.EncryptionKey.empty() && Options.SigningKey.empty()) return;
			if (!P.Services.Protection)
			{
				Fail("protection-service", "artifact protection service is unavailable");
			}
			Application::FDiagnostic E;
			if (!Options.EncryptionKey.empty())
			{
				if (Kind != "library")
				{
					Fail("encryption-kind", "only libraries can use standalone artifact encryption");
				}
				std::vector<std::uint8_t> Protected;
				Require(P.Services.Protection->Encrypt(Data(Read(File, P.Request.Limits)), Kind, Options.EncryptionKey, Options.EncryptionVersion, Protected, E, P.Request.Limits.Content), E);
				fs::remove(File);
				Write(File, Text(Protected));
			}
			if (!Options.SigningKey.empty())
			{
				if (Options.SignatureScope != "artifact")
				{
					Fail("signature-scope", "standalone artifact requires artifact signature scope");
				}
				std::string Signature;
				Require(P.Services.Protection->Sign(Data(Read(File, P.Request.Limits)), Kind, Options.SigningKey, Options.SigningVersion, Signature, E, P.Request.Limits.Content), E);
				Write(PathUtil::FromUtf8(NativePathText(File) + ".sig.json"), Signature);
			}
		}
		void CompileTarget(const std::string& K, const J& T, const fs::path& Stage,
		    const FBuildPlan::FImpl& P, const std::map<std::string, std::map<std::string, FArtifact>>& Produced,
		    std::map<std::string, std::string>& Inputs, bool& Cacheable, const std::function<void()>& Check)
		{
			const auto Entry = Resolve(T.at("entry"), P, Produced, Inputs);
			if (Entry.Type != "file")
			{
				Fail("entry-type", "compile entry must be a source file", Entry.Path);
			}
			const auto Output = Stage / PathUtil::FromUtf8(T.at("output").get<std::string>());
			const auto Protection = Detail::Protection(T.value("protection", J::object()), K == "application");
			Application::FDiagnostic E;
			if (K == "application" && T.value("mode", "compiled") == "source")
			{
				if (!P.Services.Compile.SourceOverlays.empty())
				{
					Fail("source-overlays", "source applications require materialized source inputs; use a generated source tree for editor overlays");
				}
				const auto Content = Stage / ".content";
				fs::create_directory(Content);
				Files(T.value("files", J::array()), Content, P, Produced, Inputs);
				std::string EntryName;
				for (const auto& F : T.value("files", J::array()))
				{
					const auto A = Resolve(F.at("from"), P, Produced, Inputs);
					if (A.Type == "file" && PathUtil::Equal(A.Path, Entry.Path))
					{
						EntryName = F.at("to");
					}
					else if (A.Type == "directory" && Under(PathUtil::FromUtf8(Entry.Path), PathUtil::FromUtf8(A.Path)))
					{
						EntryName = PathUtil::ToUtf8(PathUtil::FromUtf8(F.at("to").get<std::string>()) / PathUtil::FromUtf8(Entry.Path).lexically_relative(PathUtil::FromUtf8(A.Path)));
					}
				}
				if (EntryName.empty())
				{
					if (T.at("entry").is_object() && T.at("entry").contains("path") && Produced.at(T.at("entry").at("target")).at(T.at("entry").at("output")).Kind == "source-tree")
					{
						auto Ref = T.at("entry");
						Ref.erase("path");
						const auto TreeInput = Resolve(Ref, P, Produced, Inputs);
						Copy(PathUtil::FromUtf8(TreeInput.Path), Content / "program", P.Request.Limits);
						EntryName = "program/" + T.at("entry").at("path").get<std::string>();
					}
					else
					{
						EntryName = "program/" + NativePathText(PathUtil::FromUtf8(Entry.Path).filename());
						Copy(PathUtil::FromUtf8(Entry.Path), Content / PathUtil::FromUtf8(EntryName), P.Request.Limits);
					}
				}
				auto D = BaseDescriptor(P, T, {{"kind", "source"}, {"path", EntryName}});
				const auto Settings = T.value("compile", J::object());
				J Defines = J::array(), Macros = J::array(), ImplicitImports = J::array();
				for (const auto& V : P.Services.Compile.Defines)
				{
					Defines.push_back(std::string(V));
				}
				for (const auto& V : Settings.value("defines", J::array()))
				{
					Defines.push_back(V);
				}
				for (const auto& M : P.Services.Compile.PredefinedMacros)
				{
					Macros.push_back({{"name", M.Name}, {"parameters", M.Parameters}, {"variadic", M.bVariadic}, {"body", M.Body}, {"sourceName", M.SourceName}});
				}
				for (const auto& I : P.Services.Compile.ImplicitImportPaths)
				{
					const auto A = Resolve(std::string(I), P, Produced, Inputs);
					if (A.Type != "file")
					{
						Fail("source-implicit-import", "implicit imports must resolve to a source or library file", A.Path);
					}
					const auto Name = "program/implicit/" + Application::ContentSha256(A.Path).substr(0, 16) + "/" + NativePathText(PathUtil::FromUtf8(A.Path).filename());
					Copy(PathUtil::FromUtf8(A.Path), Content / PathUtil::FromUtf8(Name), P.Request.Limits);
					ImplicitImports.push_back(Name);
					if (!A.SignaturePath.empty())
					{
						Copy(PathUtil::FromUtf8(A.SignaturePath), Content / PathUtil::FromUtf8(Name + ".sig.json"), P.Request.Limits);
					}
				}
				D["compile"] = {{"defines", Defines}, {"predefinedMacros", Macros}, {"implicitImports", ImplicitImports},
				    {"allowCircularImports", Settings.value("allowCircularImports", P.Services.Compile.bAllowCircularImports)},
				    {"allowProjectWideAccess", Settings.value("allowProjectWideAccess", P.Services.Compile.bAllowProjectWideAccess)}};
				J Mappings = J::object();
				const auto Modules = T.value("modules", J::object());
				for (auto M = Modules.begin(); M != Modules.end(); ++M)
				{
					const auto A = Resolve(M.value(), P, Produced, Inputs);
					const auto Name = "modules/" + Application::ContentSha256(M.key()).substr(0, 16) + "/" + NativePathText(PathUtil::FromUtf8(A.Path).filename());
					Copy(PathUtil::FromUtf8(A.Path), Content / PathUtil::FromUtf8(Name), P.Request.Limits);
					Mappings[M.key()] = Name;
					if (!A.SignaturePath.empty())
					{
						Copy(PathUtil::FromUtf8(A.SignaturePath), Content / PathUtil::FromUtf8(Name + ".sig.json"), P.Request.Limits);
					}
				}
				D["modules"] = Mappings;
				if (fs::exists(Content / "resources"))
				{
					D["resources"].push_back({{"id", "application.resources"}, {"path", "resources"}});
				}
				std::vector<Application::FContentFile> Sealed;
				Require(Application::AssembleApplication(D.dump(), Tree(Content, P.Request.Limits), Sealed, E, P.Request.Limits.Content, Protection.SignatureScope == "content" ? Protection : Application::FProtectionOptions{}, P.Services.Protection.get()), E);
				// Validate the shipped, isolated source view, never the development tree.
				std::vector<std::uint8_t> Container;
				Require(Application::WriteArchive(Sealed, Container, E, P.Request.Limits.Content), E);
				auto Services = ApplicationServices(P);
				Services.AnalysisOnly = true;
				auto App = Application::FApplication::Open(std::move(Container), Services, E, P.Request.Limits.Content);
				Require(bool(App), E);
				Application::FPreparedApplication Prepared;
				Require(App->Prepare(Prepared, E), E);
				auto Options = P.Services.Compile;
				Options.Defines.clear();
				Options.PredefinedMacros.clear();
				Options.ImplicitImportPaths.clear();
				Options.SourceOverlays.clear();
				const auto Previous = Options.CompileProcessing.CheckCancelled;
				Options.CompileProcessing.CheckCancelled = [&]()
				{
					Check();
					if (Previous)
					{
						Previous();
					}
				};
				Options.AnnotationProcessing.CheckCancelled = Options.CompileProcessing.CheckCancelled;
				FAnalysisControlScope Scope([&]()
				{
					Check();
					return false;
				});
				auto Validated = Prepared.CompileSource(Options);
				if (!Validated.bSuccess)
				{
					Fail("source-compile", Validated.Error.Message, Validated.Error.Location.FileLabel);
				}
				Cacheable = Cacheable && (!Validated.PreparedProgram || Validated.PreparedProgram->IsCacheable());
				if (!Validated.CompileExtensionDependencies.empty())
				{
					Cacheable = false;
				}
				Require(Application::WriteArchive(Sealed, NativePathText(Output), E, P.Request.Limits.Content), E);
				if (Protection.SignatureScope == "artifact")
				{
					ProtectFile(Output, "application", Protection, P);
				}
				RemoveOwnedTree(Content, Stage);
				return;
			}
			auto Compiled = Compile(Entry, T, P, Produced, Inputs, Check);
			Cacheable = Cacheable && (!Compiled.PreparedProgram || Compiled.PreparedProgram->IsCacheable());
			if (!Compiled.CompileExtensionDependencies.empty())
			{
				Cacheable = false;
			}
			if (Compiled.PreparedProgram)
			{
				for (const auto& D : Compiled.PreparedProgram->GetDependencies())
				{
					if (!PathUtil::FromUtf8(D.Path).is_absolute())
					{
						Cacheable = false;
					}
				}
			}
			FPackageBuildOptions Options;
			Options.Kind = K == "library" ? EPackageKind::Library : EPackageKind::Application;
			Options.PackageName = T.at("packageName");
			const auto Version = T.value("packageVersion", "0");
			std::from_chars(Version.data(), Version.data() + Version.size(), Options.PackageVersion);
			Options.bStripDebugInfo = T.value("compile", J::object()).value("stripDebugInfo", false);
			std::vector<std::uint8_t> Image;
			std::string Error;
			if (!BuildPackage(*Compiled.Module, Options, Image, Error))
			{
				Fail("package", Error);
			}
			if (K == "library")
			{
				Write(Output, Text(Image));
				ProtectFile(Output, "library", Protection, P);
				return;
			}
			const auto Content = Stage / ".content";
			fs::create_directory(Content);
			Files(T.value("files", J::array()), Content, P, Produced, Inputs);
			auto Payload = Tree(Content, P.Request.Limits);
			Payload.push_back({"program/entry.bin", std::move(Image), false});
			auto D = BaseDescriptor(P, T, {{"kind", "compiled-image"}, {"path", "program/entry.bin"}, {"packageName", Options.PackageName}, {"packageVersion", std::to_string(Options.PackageVersion)}});
			if (fs::exists(Content / "resources"))
			{
				D["resources"].push_back({{"id", "application.resources"}, {"path", "resources"}});
			}
			std::vector<Application::FContentFile> Sealed;
			auto ContentProtection = Protection;
			if (ContentProtection.SignatureScope != "content")
			{
				ContentProtection.SigningKey.clear();
			}
			Require(Application::AssembleApplication(D.dump(), Payload, Sealed, E, P.Request.Limits.Content, ContentProtection, P.Services.Protection.get()), E);
			Require(Application::WriteArchive(Sealed, NativePathText(Output), E, P.Request.Limits.Content), E);
			if (Protection.SignatureScope == "artifact")
			{
				auto Signature = Protection;
				Signature.EncryptionKey.clear();
				ProtectFile(Output, "application", Signature, P);
			}
			RemoveOwnedTree(Content, Stage);
		}
		void Bundle(const J& T, const fs::path& Stage, const FBuildPlan::FImpl& P,
		    const std::map<std::string, std::map<std::string, FArtifact>>& Produced,
		    std::map<std::string, std::string>& Inputs)
		{
			const auto Source = Resolve(T.at("application"), P, Produced, Inputs);
			Application::FDiagnostic E;
			auto Services = ApplicationServices(P);
			const auto App = Application::FApplication::Open(Source.Path, Services, E, P.Request.Limits.Content);
			Require(bool(App), E);
			std::vector<Application::FContentFile> Content;
			Require(App->Export(Content, E), E);
			const bool SignedInput = !Source.SignaturePath.empty() || std::any_of(Content.begin(), Content.end(), [](const auto& F)
			{
				return F.Path == "veroscript-app.files.json.sig.json";
			});
			if (SignedInput && Detail::Protection(T.value("protection", J::object()), true).SigningKey.empty())
			{
				Fail("derived-signature", "reassembling a signed application requires an explicit new signature policy");
			}
			auto Descriptor = Parse(App->GetDescriptorJson(), P.Request.Limits);
			Descriptor["platform"] = P.Platform;
			Content.erase(std::remove_if(Content.begin(), Content.end(), [](const auto& F)
			{
				return F.Path == "veroscript-app.json" || F.Path == "veroscript-app.files.json" || F.Path == "veroscript-app.files.json.sig.json" || F.Path == ".vero-lease";
			}),
			    Content.end());
			for (const auto* Field : {"runtime", "environment"})
			{
				if (!T.contains(Field))
				{
					continue;
				}
				const auto& C = T.at(Field);
				const auto Old = Descriptor.at(Field).value("path", "");
				if (!Old.empty())
				{
					Content.erase(std::remove_if(Content.begin(), Content.end(), [&](const auto& F)
					{
						return F.Path.rfind(Old + "/", 0) == 0;
					}),
					    Content.end());
				}
				if (C.at("mode") == "external")
				{
					Descriptor[Field] = {{"mode", "external"}, {"requirements", C.value("requirements", Descriptor.at(Field).value("requirements", J::array()))}};
					continue;
				}
				const char* Key = C.contains("artifact") ? "artifact" : C.contains("component") ? "component"
				                                                                                : "path";
				if (!C.contains(Key))
				{
					Fail("component-source", "bundled component needs an explicit source", Field);
				}
				const auto Component = Resolve(C.at(Key), P, Produced, Inputs);
				if (Component.Type != "directory")
				{
					Fail("component-type", "bundled component must be a directory", Field);
				}
				const auto ManifestPath = PathUtil::FromUtf8(Component.Path) / (Field == std::string("runtime") ? "veroscript-runtime.json" : "veroscript-deployment.json");
				const auto M = Parse(Read(ManifestPath, P.Request.Limits), P.Request.Limits);
				if (M.value("formatVersion", "") != "0.1.1" || M.value("format", "") != (Field == std::string("runtime") ? "veroscript-runtime" : "veroscript-deployment"))
				{
					Fail("component-format", "unsupported bundled component descriptor", NativePathText(ManifestPath));
				}
				if (P.Platform == "portable" || M.at("target") != P.Platform)
				{
					Fail("component-target", "bundled component does not match the build target", Field);
				}
				if (Field == std::string("environment") && Descriptor.at("entry").at("kind") == "source")
				{
					const auto U = M.value("usage", std::vector<std::string>{"runtime"});
					if (std::find(U.begin(), U.end(), "compile") == U.end())
					{
						Fail("source-environment", "source applications require a runtime and compile deployment");
					}
				}
				if (Field == std::string("runtime"))
				{
					Safe(M.at("executable").get<std::string>());
					if (M.value("applicationProtocol", "") != "0.1.1" || !fs::is_regular_file(PathUtil::FromUtf8(Component.Path) / PathUtil::FromUtf8(M.at("executable").get<std::string>())))
					{
						Fail("runtime-protocol", "runtime component has no compatible application entry");
					}
				}
				if (Field == std::string("runtime"))
				{
					if (!M.at("files").is_array() || M.at("files").size() > P.Request.Limits.Content.MaxFiles)
					{
						Fail("runtime-inventory", "invalid Runtime component inventory");
					}
					std::set<std::string> Names;
					bool EntryPresent = false;
					for (const auto& Record : M.at("files"))
					{
						Keys(Record, {"path", "bytes", "sha256", "mode"}, "runtime.file");
						const auto Rel = Record.at("path").get<std::string>();
						Safe(Rel);
						if (Rel == "veroscript-runtime.json" || !Names.insert(Rel).second)
						{
							Fail("runtime-inventory", "duplicate or reserved Runtime file", Rel);
						}
						const auto Bytes = Data(Read(PathUtil::FromUtf8(Component.Path) / PathUtil::FromUtf8(Rel), P.Request.Limits));
						if (Record.at("bytes") != std::to_string(Bytes.size()) || Record.at("sha256") != Application::ContentSha256(Bytes) ||
						    (Record.at("mode") != "0755" && Record.at("mode") != "0644"))
						{
							Fail("runtime-integrity", "Runtime component file differs from its inventory", Rel);
						}
						const bool Entry = M.at("executable") == Rel;
						EntryPresent = EntryPresent || Entry;
						Content.push_back({std::string(Field) + "/" + Rel, Bytes, Entry || Record.at("mode") == "0755"});
					}
					if (!EntryPresent)
					{
						Fail("runtime-inventory", "Runtime executable is absent from its inventory");
					}
					Content.push_back({"runtime/veroscript-runtime.json", Data(M.dump()), false});
				}
				else
				{
					for (auto F : Tree(PathUtil::FromUtf8(Component.Path), P.Request.Limits))
					{
						F.Path = std::string(Field) + "/" + F.Path;
						Content.push_back(std::move(F));
					}
				}
				Descriptor[Field] = {{"mode", "bundled"}, {"path", Field}, {"requirements", Descriptor.at(Field).value("requirements", J::array())}};
			}
			const auto ContentRoot = Stage / ".content";
			Require(Application::WriteContentTree(Content, NativePathText(ContentRoot), E, P.Request.Limits.Content), E);
			Files(T.value("files", J::array()), ContentRoot, P, Produced, Inputs);
			const auto Launcher = T.value("launcher", J{{"mode", "none"}});
			const auto Mode = Launcher.at("mode").get<std::string>();
			const auto Layout = T.value("layout", "vsapp");
			std::string LauncherFile;
			std::string LauncherOutput;
			if (Mode != "none")
			{
				if (Mode == "generated")
				{
					if (P.Services.LauncherTemplate.empty())
					{
						Fail("launcher-provider", "no compatible launcher template was supplied");
					}
					LauncherFile = P.Services.LauncherTemplate;
					Snapshot(PathUtil::FromUtf8(LauncherFile), Inputs, P.Request.Limits);
				}
				else
				{
					if (Launcher.value("protocol", "") != "0.1.1")
					{
						Fail("launcher-protocol", "existing launcher must declare the application protocol");
					}
					LauncherFile = Resolve(Launcher.at("path"), P, Produced, Inputs).Path;
				}
				if (Layout == "directory" || Layout == "zip")
				{
					const auto Name = Launcher.value("output", Descriptor.at("id").get<std::string>() +
					#if defined(_WIN32)
					        ".exe"
					#else
					        ""
					#endif
					);
					Safe(Name);
					Copy(PathUtil::FromUtf8(LauncherFile), ContentRoot / PathUtil::FromUtf8(Name), P.Request.Limits);
					LauncherOutput = Name;
					// The bootstrap starts before its bundled Runtime can be located.
					if (P.Platform.is_object() && P.Platform.at("os") == "windows")
					{
						LauncherRuntime(Content, Descriptor.at("runtime").value("path", ""), PathUtil::FromUtf8(Name), ContentRoot, P.Request.Limits);
					}
				}
			}
			else if (Layout == "single-file")
			{
				Fail("launcher-required", "single-file output requires a launcher");
			}
			auto Payload = Tree(ContentRoot, P.Request.Limits);
			// Windows cannot represent Unix executable bits on a staging tree.
			// Preserve the authoritative modes from the application/component inventories.
			std::map<std::string, bool> StoredModes;
			for (const auto& F : Content)
			{
				StoredModes.emplace(F.Path, F.Executable);
			}
			for (auto& F : Payload)
			{
				if (StoredModes.count(F.Path))
				{
					F.Executable = StoredModes.at(F.Path);
				}
				if (F.Path == LauncherOutput)
				{
					F.Executable = true;
				}
			}
			const auto Protection = Detail::Protection(T.value("protection", J::object()), true);
			if (!Protection.EncryptionKey.empty())
			{
				const auto Entry = Descriptor.at("entry").at("path").get<std::string>();
				for (auto& F : Payload)
				{
					if (F.Path == Entry)
					{
						FArtifactData Decoded;
						Decoded.Bytes = std::move(F.Bytes);
						// Reprotect authenticates and decrypts once; never nest VSAE envelopes.
						Decoded.TrustIdentity = Services.Protection ? Services.Protection->GetIdentity() : "";
						std::string Error;
						if (!DecodeArtifact({Entry, "application-image", {}}, {P.Request.Limits.Content.MaxFileBytes, P.Request.CheckCancelled}, Decoded, Error, Services.Protection.get()))
						{
							Fail("reprotect", Error);
						}
						F.Bytes = std::move(Decoded.Bytes);
					}
				}
			}
			std::vector<Application::FContentFile> Sealed;
			auto ContentProtection = Protection;
			if (ContentProtection.SignatureScope != "content")
			{
				ContentProtection.SigningKey.clear();
			}
			Require(Application::AssembleApplication(Descriptor.dump(), Payload, Sealed, E, P.Request.Limits.Content, ContentProtection, P.Services.Protection.get()), E);
			const auto Output = Stage / PathUtil::FromUtf8(T.at("output").get<std::string>());
			if (Layout == "directory")
			{
				if (Protection.SignatureScope == "artifact" && !Protection.SigningKey.empty())
				{
					Fail("signature-scope", "directories require content signatures");
				}
				Require(Application::WriteContentTree(Sealed, NativePathText(Output), E, P.Request.Limits.Content), E);
			}
			else if (Layout == "single-file")
			{
				// The envelope service validates native format and supported signing layout.
				Require(Application::WriteExecutableApplication(LauncherFile, Sealed, NativePathText(Output), E, P.Request.Limits.Content), E);
			}
			else
			{
				Require(Application::WriteArchive(Sealed, NativePathText(Output), E, P.Request.Limits.Content), E);
			}
			if (Layout != "directory" && Protection.SignatureScope == "artifact")
			{
				auto S = Protection;
				S.EncryptionKey.clear();
				ProtectFile(Output, "application", S, P);
			}
			RemoveOwnedTree(ContentRoot, Stage);
		}
	}
	bool FBuilder::Execute(const FBuildPlan& Plan, FBuildResult& Result, FDiagnostic& Error)
	{
		Result = {};
		std::string Current;
		fs::path Staging;
		bool StageOwned = false;
		try
		{
			// Keep the shared plan immutable; all local readers use this execution deadline.
			auto P = *Plan.Impl;
			const auto Started = std::chrono::steady_clock::now();
			const auto Check = [Cancel = P.Request.CheckCancelled, Timeout = P.Request.Limits.TimeoutMilliseconds, Started]()
			{
				if (Cancel)
				{
					Cancel();
				}
				if (Timeout && std::chrono::steady_clock::now() - Started > std::chrono::milliseconds(Timeout))
				{
					Fail("build-timeout", "build deadline exceeded");
				}
			};
			auto Limits = P.Request.Limits;
			Limits.Content.CheckCancelled = Check;
			P.Request.Limits.Content.CheckCancelled = Check;
			std::shared_ptr<const FBuildPlan> Refreshed;
			if (!FBuilder::Plan(P.Request, P.Services, Refreshed, Error)) return false;
			if (Refreshed->Impl->Identity != P.Identity)
			{
				Fail("plan-stale", "build inputs or provider configuration changed since planning");
			}
			std::map<std::string, std::map<std::string, FArtifact>> Produced;
			std::map<std::string, std::string> Inputs = P.Inputs;
			for (const auto& N : P.Order)
			{
				Check();
				Current = N;
				const auto& T = P.Targets.at(N);
				const auto K = T.at("kind").get<std::string>();
				FTargetResult R;
				R.Id = N;
				if (K == "group")
				{
					Result.Targets.push_back(std::move(R));
					Produced[N] = {};
					continue;
				}
				VerifyInputs(Inputs, Limits);
				const auto FinalRoot = TargetRoot(P, N);
				Physical(FinalRoot, true);
				fs::create_directories(FinalRoot.parent_path());
				auto Lease = std::make_shared<FOutputLease>(FinalRoot.parent_path() / ("." + N + ".lock"));
				const auto Owner = Application::ContentSha256(PhysicalIdentity(PathUtil::FromUtf8(P.Request.ProjectRoot)) + "\n" + PhysicalIdentity(PathUtil::FromUtf8(P.Request.BuildFile)) + "\n" + P.Request.Profile + "\n" + P.PlatformId + "\n" + N);
				const auto StepKey = Application::ContentSha256(J{{"plan", P.Identity}, {"target", T}, {"upstream", [&]()
				{
					J A = J::array();
					for (const auto& D : References(T))
					{
						for (const auto& O : Produced.at(D))
						{
							A.push_back({D, O.first, O.second.Identity});
						}
					}
					return A;
				}()}}.dump());
				bool Cacheable = K != "custom" || T.value("cache", false);
				// Trust and dynamic processor inputs are reassessed on every execution.
				if (P.Services.Protection || (P.Services.Compile.ModuleEnvironment && (P.Services.Compile.ModuleEnvironment->GetDescription().ArtifactDecoder || P.Services.Compile.ModuleEnvironment->GetDescription().CompileProcessing.Registry)))
				{
					Cacheable = false;
				}
				if (P.Services.Compile.CompileProcessing.Registry || P.Services.Compile.AnnotationProcessing.Registry ||
				    !P.Services.Compile.ScriptCompileExtensions.Roots.empty())
				{
					Cacheable = false;
				}
				if (K == "custom" && T.value("provider", "") == "script" && P.Services.Identity.empty() &&
				    P.Services.Compile.ScriptCompileExtensions.VmBackendFactory)
				{
					Cacheable = false;
				}
				// Native callbacks without a stable service identity cannot reuse compilation outputs.
				if ((K == "library" || K == "application") && P.Services.Identity.empty() &&
				    (P.Services.Compile.Bindings || P.Services.Compile.Natives || P.Services.Compile.CompileProcessing.Registry || P.Services.Compile.AnnotationProcessing.Registry))
				{
					Cacheable = false;
				}
				J Previous;
				if (fs::exists(FinalRoot))
				{
					CheckOwned(FinalRoot, Owner, Limits);
					Previous = Parse(Read(FinalRoot / ReceiptName, Limits), Limits);
				}
				if (Cacheable && !Previous.is_null() && Previous.value("cacheable", false) && Previous.at("step") == StepKey)
				{
					bool Valid = true;
					for (auto I = Previous.at("inputs").begin(); I != Previous.at("inputs").end(); ++I)
					{
						Check();
						const auto Path = PathUtil::FromUtf8(I.key());
						if (!fs::exists(Path) || I.value() != Fingerprint(Path, Limits))
						{
							Valid = false;
							break;
						}
					}
					if (Valid)
					{
						for (auto I = Previous.at("inputs").begin(); I != Previous.at("inputs").end(); ++I)
						{
							Inputs.emplace(I.key(), I.value().get<std::string>());
						}
						R.Cached = true;
						for (const auto& O : Previous.at("outputs"))
						{
							const auto Name = O.at("name").get<std::string>();
							auto A = P.Outputs.at(N).at(Name);
							A.Identity = O.at("sha256");
							A.Owner = Lease;
							if (Name == "main" && P.Outputs.at(N).count("signature"))
							{
								A.SignaturePath = P.Outputs.at(N).at("signature").Path;
							}
							Produced[N][Name] = A;
							R.Artifacts.push_back(std::move(A));
						}
						Result.Targets.push_back(std::move(R));
						continue;
					}
				}
				Staging = PathUtil::MakeTemporarySiblingPath(FinalRoot);
				std::vector<std::shared_ptr<const void>> DirectoryLeases;
				if (!Previous.is_null())
				{
					for (const auto& O : Previous.at("outputs"))
					{
						if (O.at("type") == "directory" && (O.at("kind") == "application-directory" || O.at("kind") == "environment"))
						{
							Application::FDiagnostic E;
							const auto Path = NativePathText(FinalRoot / PathUtil::FromUtf8(O.at("path").get<std::string>()));
							std::shared_ptr<const void> Hold;
							if (O.at("kind") == "environment")
							{
								FDiagnostic D;
								if (!P.Services.LeaseDeployment)
								{
									Fail("deployment-lease", "deployment publisher lease service is unavailable");
								}
								Hold = P.Services.LeaseDeployment(Path, D);
								if (!Hold)
								{
									Fail(D.Code, D.Message, D.Path);
								}
							}
							else
							{
								Hold = Application::AcquireApplicationDirectoryLease(Path, true, E);
								Require(bool(Hold), E);
							}
							DirectoryLeases.push_back(std::move(Hold));
						}
					}
				}
				Physical(Staging, true);
				if (!fs::create_directory(Staging))
				{
					Fail("stage", "cannot exclusively create build staging directory");
				}
				StageOwned = true;
				if (P.Services.Log)
				{
					P.Services.Log(N, "building " + K);
				}
				if (K == "library" || K == "application")
				{
					CompileTarget(K, T, Staging, P, Produced, Inputs, Cacheable, Check);
				}
				else if (K == "distribution")
				{
					const auto Source = Resolve(T.at("source"), P, Produced, Inputs);
					const auto Content = Staging / ".distribution";
					Copy(PathUtil::FromUtf8(Source.Path), Content, Limits);
					Files(T.value("files", J::array()), Content, P, Produced, Inputs);
					FDiagnostic E;
					if (!P.Services.Pack(NativePathText(Content), NativePathText(Staging / PathUtil::FromUtf8(T.at("output").get<std::string>())), E))
					{
						Fail(E.Code, E.Message, E.Path);
					}
					ProtectFile(Staging / PathUtil::FromUtf8(T.at("output").get<std::string>()), "distribution", Detail::Protection(T.value("protection", J::object()), false), P);
					RemoveOwnedTree(Content, Staging);
				}
				else if (K == "deployment")
				{
					FDiagnostic E;
					if (!P.Services.Deploy(T.value("usage", std::vector<std::string>{"runtime"}), T.value("roots", std::vector<std::string>{}), NativePathText(Staging / PathUtil::FromUtf8(T.at("output").get<std::string>())), E))
					{
						Fail(E.Code, E.Message, E.Path);
					}
				}
				else if (K == "bundle")
				{
					Bundle(T, Staging, P, Produced, Inputs);
				}
				else if (K == "custom")
				{
					FStepContext C;
					C.Target = N;
					C.ProjectRoot = P.Request.ProjectRoot;
					C.StagingRoot = NativePathText(Staging);
					C.PlatformJson = P.Platform.dump();
					C.OptionsJson = T.value("options", J::object()).dump();
					C.Limits = Limits;
					C.CheckCancelled = Check;
					C.StandardLibraryPaths = P.StdlibPaths;
					std::uint64_t Logged = 0;
					C.Log = [&](const std::string& Text)
					{
						if (Text.size() > Limits.MaxLogBytes - Logged)
						{
							Fail("log-budget", "build step exceeds log budget");
						}
						Logged += Text.size();
						if (P.Services.Log)
						{
							P.Services.Log(N, Text);
						}
					};
					C.TrackInput = [&](const std::string& Path)
					{
						Snapshot(PathUtil::FromUtf8(Path), Inputs, Limits);
					};
					const auto Ins = T.value("inputs", J::object());
					for (auto I = Ins.begin(); I != Ins.end(); ++I)
					{
						C.Inputs[I.key()] = Resolve(I.value(), P, Produced, Inputs);
					}
					for (const auto& O : P.Outputs.at(N))
					{
						auto A = O.second;
						A.Path = NativePathText(Staging / PathUtil::FromUtf8(A.Path).lexically_relative(FinalRoot));
						C.Outputs.emplace(O.first, A);
						fs::create_directories(PathUtil::FromUtf8(A.Path).parent_path());
					}
					const auto Provider = T.at("provider").get<std::string>();
					if (Provider == "process" || Provider == "script")
					{
						RunBuiltinStep(Provider, C, P.Services);
					}
					else
					{
						FDiagnostic E;
						const auto Service = P.Services.Providers.at(Provider);
						if (Service->GetIdentity() != P.ProviderIdentities.at(Provider) || Service->GetVersion() != P.ProviderVersions.at(Provider))
						{
							Fail("provider-stale", "provider identity changed after planning");
						}
						if (!Service->Execute(C, E))
						{
							Fail(E.Code, E.Message, E.Path);
						}
					}
				}
				Check();
				VerifyInputs(Inputs, Limits);
				J Outputs = J::array();
				for (const auto& O : P.Outputs.at(N))
				{
					const auto Relative = PathUtil::FromUtf8(O.second.Path).lexically_relative(FinalRoot);
					const auto File = Staging / Relative;
					Physical(File);
					if ((O.second.Type == "directory") != fs::is_directory(File))
					{
						Fail("output-type", "provider output has the wrong file or directory type", NativePathText(File));
					}
					const auto Hash = Fingerprint(File, Limits);
					Outputs.push_back({{"name", O.first}, {"path", PathUtil::ToUtf8(Relative)}, {"sha256", Hash}, {"kind", O.second.Kind}, {"type", O.second.Type}});
					auto A = O.second;
					A.Identity = Hash;
					A.Owner = Lease;
					if (O.first == "main" && P.Outputs.at(N).count("signature"))
					{
						A.SignaturePath = P.Outputs.at(N).at("signature").Path;
					}
					Produced[N][O.first] = A;
					R.Artifacts.push_back(std::move(A));
				}
				const J Receipt{{"format", "veroscript-build-output"}, {"formatVersion", "0.1.1"}, {"owner", Owner},
				    {"step", StepKey}, {"cacheable", Cacheable}, {"inputs", Inputs}, {"outputs", Outputs}};
				Write(Staging / ReceiptName, Receipt.dump());
				CheckOwned(Staging, Owner, Limits);
				Check();
				const auto Backup = PathUtil::MakeTemporarySiblingPath(FinalRoot);
				if (fs::exists(FinalRoot))
				{
					CheckOwned(FinalRoot, Owner, Limits);
					fs::rename(FinalRoot, Backup);
				}
				try
				{
					fs::rename(Staging, FinalRoot);
					Staging.clear();
					StageOwned = false;
				}
				catch (...)
				{
					if (fs::exists(Backup) && !fs::exists(FinalRoot))
					{
						fs::rename(Backup, FinalRoot);
					}
					throw;
				}
				DirectoryLeases.clear();
				if (fs::exists(Backup))
				{
					Physical(Backup);
					CheckOwned(Backup, Owner, Limits);
					RemoveOwnedTree(Backup, FinalRoot.parent_path());
				}
				Result.Targets.push_back(std::move(R));
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			Error.Target = Current;
			Error.Stage = "execute";
		}
		catch (const std::exception& E)
		{
			Error = {"build-execute", E.what(), Current, {}, "execute"};
		}
		if (StageOwned && !Staging.empty())
		{
			// Only the exclusively created sibling staging tree is eligible for cleanup.
			try
			{
				Physical(Staging);
				RemoveOwnedTree(Staging, Staging);
			}
			catch (...)
			{
			}
		}
		Result.Diagnostics.push_back(Error);
		return false;
	}
}
