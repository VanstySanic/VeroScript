// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "PackageOperation.h"
#include <algorithm>
#include <limits>
#include <random>

namespace VeroScript::Packages
{
	using namespace Detail;
	namespace
	{
		FInstalledPackage Describe(const FCandidate& C, const J& Configuration = J::object())
		{
			FInstalledPackage P;
			P.Name = C.Manifest["name"];
			P.Version = C.Manifest["version"];
			P.SourceId = C.Source["id"];
			P.Sha256 = C.Artifact["sha256"];
			P.ArtifactId = C.Artifact["id"];
			P.Path = NativePathText(C.Content);
			P.ManifestJson = C.Manifest.dump();
			P.TextDomain = C.OwnerId;
			P.ConfigurationJson = Configuration.dump();
			return P;
		}
		I18n::FI18nSnapshot Display(std::shared_ptr<I18n::FI18nServices> S, const FClientOptions& O, FWork& W)
		{
			W.Check();
			auto C = O.DisplayConfiguration;
			C.FallbackLanguages.erase(
			    std::remove(C.FallbackLanguages.begin(), C.FallbackLanguages.end(), "en-US"),
			    C.FallbackLanguages.end());
			C.FallbackLanguages.push_back("en-US");
			std::shared_ptr<I18n::FI18nContext> Context;
			I18n::FI18nError Error;
			if (!I18n::CreateContext(C, S, Context, Error))
			{
				Fail("display-configuration", Error.Diagnostic.Message);
			}
			return Context->CaptureSnapshot();
		}
		std::map<std::string, I18n::FI18nSnapshot> Displays(const FGraph& G, const FClientOptions& O,
		    FWork& W,
		    const std::set<std::string>* Only = nullptr)
		{
			std::map<std::string, I18n::FI18nSnapshot> R;
			std::set<std::string> Needed;
			for (const auto& N : G.Nodes)
			{
				if (!Only || Only->count(N.first))
				{
					Needed.insert(N.first);
					auto I = G.Edges.find(N.first);
					if (I != G.Edges.end())
					{
						Needed.insert(I->second.begin(), I->second.end());
					}
				}
			}
			std::vector<FInstalledPackage> Packages;
			for (const auto& Name : Needed)
			{
				Packages.push_back(Describe(G.Nodes.at(Name)));
			}
			auto Base = DisplayServices(Packages, W)->Acquire()->Resources;
			for (const auto& N : G.Nodes)
			{
				if (Only && !Only->count(N.first))
				{
					continue;
				}
				std::set<std::string> Domains = {N.second.OwnerId};
				for (const auto& D : G.Edges.at(N.first))
				{
					Domains.insert(G.Nodes.at(D).OwnerId);
				}
				R.emplace(N.first, Display(ScopedDisplayServices(Base, std::move(Domains)), O, W));
			}
			return R;
		}
		void Preflight(const FGraph& G, const FClientOptions& O, const std::set<std::string>* Only = nullptr,
		    bool Removal = false)
		{
			for (const auto& P : G.Nodes)
			{
				for (const auto& H :
				    P.second.Manifest.value("install", J::object()).value("hooks", J::array()))
				{
					if (Only && !Only->count(P.first))
					{
						continue;
					}
					if ((H.at("phase").get<std::string>().find("uninstall") != std::string::npos) != Removal)
					{
						continue;
					}
					if (!O.AllowHooks)
					{
						Fail("hooks-disabled",
						    "package declares installation hooks; explicit permission is "
						    "required",
						    P.first);
					}
					for (const auto& V : H.value("permissions", std::vector<std::string>{}))
					{
						if (V != "resources.read" && V != "generated.write" && V != "messages")
						{
							Fail("hook-permission", "unsupported hook permission", V);
						}
					}
					auto Path = P.second.Content / U8Path(H.at("path").get<std::string>());
					if (Path.extension() != ".vs")
					{
						Fail("hook-format",
						    "hook entry must be source; portable libraries can be imported as "
						    "helpers",
						    P.first);
					}
				}
			}
		}

		struct FRemovalPlan
		{
			std::map<std::string, J> Inputs, Secrets;
			std::map<std::string, I18n::FI18nSnapshot> Views;
			std::map<std::string, fs::path> Roots;
		};
		void CheckFeatures(const FCandidate& C, const FClientOptions& O)
		{
			for (const auto& F : C.Manifest.value("requires", J::object()).value("features", J::array()))
			{
				auto Id = F.at("id").get<std::string>();
				if (Id == "veroscript.package-manager")
				{
					continue;
				}
				auto I = O.Features.find(Id);
				if (I == O.Features.end() || !I->second || I->second->GetImplementationIdentity().empty() ||
				    !Satisfies(I->second->GetVersion(), F.at("version")))
				{
					Fail("installer-feature", "required installation processor is unavailable", Id);
				}
			}
		}
		FRemovalPlan PrepareRemoval(const FGraph& Old, const J& Profile,
		    const std::set<std::string>& Removing, const fs::path& Root,
		    const FEnvironmentLayout& L, FWork& W)
		{
			FRemovalPlan Plan;
			if (W.Options.SkipHooks || Removing.empty())
			{
				return Plan;
			}
			Preflight(Old, W.Options, &Removing, true);
			Plan.Views = Displays(Old, W.Options, W, &Removing);
			for (const auto& Name : Removing)
			{
				const auto& C = Old.Nodes.at(Name);
				CheckFeatures(C, W.Options);
				auto Config = Profile.at("packages").at(Name).value("configuration", J::object());
				bool Hook = false;
				for (const auto& H : C.Manifest.value("install", J::object()).value("hooks", J::array()))
				{
					Hook = Hook || H.at("phase").get<std::string>().find("uninstall") != std::string::npos;
				}
				auto Features = C.Manifest.value("requires", J::object()).value("features", J::array());
				for (const auto& F : Features)
				{
					Hook = Hook || F.at("id") != "veroscript.package-manager";
				}
				Plan.Inputs[Name] =
				    Hook ? PrepareInputs(C, Config, W.Options, W, Plan.Secrets[Name], &Plan.Views.at(Name))
				         : Config;
				auto P = Root / ("uninstall-" + Name);
				fs::create_directories(P);
				Plan.Roots[Name] = P;
				for (auto Phase : {"pre-uninstall", "post-uninstall"})
				{
					RunHooks(C, Phase, Old, Plan.Inputs[Name], Plan.Secrets[Name], P, L, Plan.Views.at(Name),
					    W, true);
				}
			}
			return Plan;
		}
		void RunRemoval(const FGraph& Old, FRemovalPlan& Plan, const FEnvironmentLayout& L, FWork& W)
		{
			for (auto I = Old.Order.rbegin(); I != Old.Order.rend(); ++I)
			{
				if (Plan.Roots.count(*I))
				{
					const auto& C = Old.Nodes.at(*I);
					const auto& Root = Plan.Roots.at(*I);
					const auto& View = Plan.Views.at(*I);
					RunHooks(C, "pre-uninstall", Old, Plan.Inputs[*I], Plan.Secrets[*I], Root, L, View, W);
					RunFeatures(C, "uninstall", Plan.Inputs[*I], Plan.Secrets[*I], Root, L, View, W);
					RunHooks(C, "post-uninstall", Old, Plan.Inputs[*I], Plan.Secrets[*I], Root, L, View, W);
				}
			}
		}
		J EmptyLock()
		{
			return {{"packages", J::array()}};
		}
		struct FPublished
		{
			~FPublished()
			{
				if (Result.Committed)
				{
					return;
				}
				std::error_code Error;
				if (fs::exists(Journal, Error) || Error)
				{
					return;
				}
				for (auto I = Directories.rbegin(); I != Directories.rend(); ++I)
				{
					try
					{
						if (Under(I->first, I->second) && I->first != I->second)
						{
							NoLinks(I->first);
							for (const auto& F : fs::recursive_directory_iterator(I->first))
							{
								NoLinks(F.path());
							}
							{
								FFileLock Lock(I->first / ".vero-lease", true, false, false);
							}
							fs::remove_all(I->first, Error);
						}
					}
					catch (...)
					{
					}
				}
			}

			FResult& Result;

			fs::path Journal;

			std::vector<std::pair<fs::path, fs::path>> Directories;
		};
		FGraph Prune(const FGraph& Old, const J& Project, const FEnvironmentLayout& L,
		    ICandidateRepository& Sources, FWork& W)
		{
			struct FInstalledOnly final : ICandidateRepository
			{
				//~ Begin ICandidateRepository Interface
				J Source(const J& D) const override
				{
					return Sources.Source(D);
				}

				std::vector<FCandidate> Candidates(const std::string& N, const J& D, const J&) override
				{
					auto I = Old.Nodes.find(N);
					if (I == Old.Nodes.end())
					{
						Fail("uninstall-requirements",
						    "remaining requirements are not installed; run install first", N);
					}
					auto Expected = Source(D);
					if (Expected.at("id") != I->second.Source.at("id") ||
					    (Expected.contains("api") && Expected.at("api") != I->second.Source.at("api")))
					{
						Fail("uninstall-requirements", "remaining logical source changed; run install first",
						    N);
					}
					return {I->second};
				}
				//~ End ICandidateRepository Interface

				FInstalledOnly(const FGraph& G, ICandidateRepository& S) : Old(G), Sources(S)
				{
				}

				const FGraph& Old;

				ICandidateRepository& Sources;
			} Pool(Old, Sources);
			return Solve(Project, J{{"packages", J::array()}}, Pool, L, W, true);
		}

		std::map<std::string, J> LockIndex(const J& Lock)
		{
			std::map<std::string, J> R;
			for (const auto& N : Lock.value("packages", J::array()))
			{
				R[N.at("name")] = N;
			}
			return R;
		}
		void OldContent(FGraph& G, const J& Profile, const FEnvironmentLayout& L, const FWork& W,
		    const std::string& Owner, std::vector<std::shared_ptr<const void>>& Leases,
		    bool Verify)
		{
			for (auto& N : G.Nodes)
			{
				if (!Profile.value("packages", J::object()).contains(N.first))
				{
					Fail("state-invalid", "old locked package has no receipt");
				}
				auto R = Profile["packages"][N.first];
				auto Rel = R.at("path").get<std::string>();
				CheckPath(Rel);
				auto Root = U8Path(L.Packages) / U8Path(Rel);
				NoLinks(Root);
				Leases.push_back(std::make_shared<FFileLock>(Root / ".vero-lease", false, false, true, &W));
				auto Receipt = ReadRecord(Root / ".vero-receipt.json", W);
				if (Receipt.at("environmentId") != Owner || Receipt.at("artifact") != N.second.Artifact ||
				    Receipt.at("inventory") != R.at("inventory"))
				{
					Fail("receipt-invalid", "old package receipt differs from locked installation");
				}
				if (Verify)
				{
					VerifyInventory(Root, R.at("inventory"), W);
				}
				N.second.Content = Root;
				N.second.Inventory = R.at("inventory");
			}
		}
		J PrepareDistribution(const fs::path& Root, const J& Input, const FWork& W)
		{
			auto P = Project(Input);
			auto Distribution = P.value("packageManager", J::object()).value("distribution", J{});
			if (Distribution.is_null())
			{
				Fail("distribution-metadata", "project pack requires packageManager.distribution metadata");
			}
			if (Distribution.is_string())
			{
				Distribution = ReadRecord(Absolute(Distribution, NativePathText(Root)), W);
			}
			if (!Distribution.is_object())
			{
				Fail("distribution-metadata", "distribution must be a manifest object or filename");
			}
			J M = {{"format", "veroscript-package"},
			    {"formatVersion", FormatVersion},
			    {"name", P["name"]},
			    {"version", P["version"]},
			    {"dependencies", P.value("dependencies", J::object())}};
			M.update(Distribution);
			return Manifest(M);
		}
		J StateReadOnly(const FEnvironmentLayout& L, const FWork& W)
		{
			return fs::exists(U8Path(L.State)) ? LoadState(L, W) : J::object();
		}
		J Clean(const FEnvironmentLayout& L, J& State, const FWork& W)
		{
			J Retained = J::array(), Removed = J::array();
			std::set<std::string> Referenced, PhysicalReferences;
			const auto References = [&](const J& S)
			{
				for (const auto& Profile : S.value("profiles", J::object()))
				{
					for (const auto& P : Profile.at("packages"))
					{
						W.Check();
						for (auto Generated : {false, true})
						{
							auto Path = P.at(Generated ? "generated" : "path").get<std::string>();
							CheckPath(Path);
							Referenced.insert((Generated ? "generated:" : "") + Path);
							auto Full = U8Path(Generated ? L.Management : L.Packages) / U8Path(Path);
							NoLinks(Full, true);
							if (fs::exists(Full))
							{
								PhysicalReferences.insert(FileIdentity(Full));
							}
						}
					}
				}
			};
			References(State);
			// Directory spelling may differ on a case-insensitive or Unicode-normalizing
			// filesystem. Protect active directories by physical identity as well.
			const auto IsReferenced = [&](const std::string& Kind, const std::string& Path,
			                              const fs::path& Full)
			{
				return Referenced.count((Kind == "generated" ? "generated:" : "") + Path) ||
				    (fs::exists(Full) && PhysicalReferences.count(FileIdentity(Full)));
			};
			// A process may terminate after publishing immutable directories but
			// before its management journal commits. Discover only owned receipt
			// roots; never infer ownership from a directory name alone.
			std::set<std::string> Known;
			for (const auto& R : State.value("retired", J::array()))
			{
				Known.insert(R.at("kind").get<std::string>() + ":" + R.at("path").get<std::string>());
			}
			const auto Recorded = Known;
			const auto RemainingInventory = [&](const fs::path& Full, const J& Files)
			{
				if (!Files.is_array() || Files.size() > W.Options.Limits.MaxFiles)
				{
					Fail("receipt-invalid", "invalid retired file inventory");
				}
				std::map<std::string, const J*> Expected;
				for (const auto& F : Files)
				{
					W.Check();
					const auto Path = F.at("path").get<std::string>();
					CheckPath(Path);
					if (!Expected.emplace(Path, &F).second)
					{
						Fail("receipt-invalid", "retired file inventory contains a duplicate path");
					}
				}
				// Inventory omits installer metadata. A store owner marker is never
				// part of a retired package/generated directory and must not be hidden.
				NoLinks(Full / ".vero-owner.json", true);
				if (fs::exists(Full / ".vero-owner.json"))
				{
					Fail("package-corrupt", "retired directory contains unexpected metadata");
				}
				auto Actual = Inventory(Full, W);
				for (const auto& F : Actual)
				{
					W.Check();
					const auto I = Expected.find(F.at("path").get<std::string>());
					if (I == Expected.end() || *I->second != F)
					{
						Fail("package-corrupt", "remaining retired content differs from its receipt", NativePathText(Full));
					}
				}
				return Actual;
			};
			std::uint64_t Scanned = 0;
			const auto CheckDirectory = [&](const fs::path& P)
			{
				W.Check();
				if (++Scanned > W.Options.Limits.MaxFiles)
				{
					Fail("cleanup-limit", "owned-directory scan exceeds budget");
				}
				NoLinks(P);
				return fs::is_directory(P);
			};
			const auto Discover = [&](const fs::path& Full, const fs::path& Base, const std::string& Kind)
			{
				auto Path = PathUtil::ToUtf8(Relative(Full, Base));
				if (Known.count(Kind + ":" + Path) || IsReferenced(Kind, Path, Full))
				{
					return;
				}
				auto File = Full / ".vero-receipt.json";
				if (!fs::exists(File) || !fs::exists(Full / ".vero-lease"))
				{
					return;
				}
				auto Receipt = ReadRecord(File, W);
				if (Receipt.value("environmentId", std::string{}) !=
				        State.value("environmentId", std::string{}) ||
				    Receipt.value("format", std::string{}) !=
				        (Kind == "package" ? "veroscript-package-receipt" : "veroscript-generated-receipt") ||
				    Receipt.value("formatVersion", std::string{}) != FormatVersion)
				{
					return;
				}
				if (Kind == "package")
				{
					auto Name = NativePathText(Full.filename());
					if (Name.size() < 66 || Name[Name.size() - 65] != '-' ||
					    Receipt.at("artifact").at("sha256") != Name.substr(Name.size() - 64))
					{
						return;
					}
				}
				CheckPath(Path);
				State["retired"].push_back(
				    {{"kind", Kind}, {"path", Path}, {"inventory", Receipt.at("inventory")}});
				Known.insert(Kind + ":" + Path);
			};
			auto Store = U8Path(L.Packages);
			if (fs::is_directory(Store))
			{
				for (const auto& Package : fs::directory_iterator(Store))
				{
					if (Name(NativePathText(Package.path().filename())) && CheckDirectory(Package.path()))
					{
						for (const auto& Artifact : fs::directory_iterator(Package.path()))
						{
							if (CheckDirectory(Artifact.path()))
							{
								Discover(Artifact.path(), Store, "package");
							}
						}
					}
				}
			}
			auto Generated = U8Path(L.Management) / "generated";
			if (fs::is_directory(Generated))
			{
				for (const auto& Profile : fs::directory_iterator(Generated))
				{
					if (CheckDirectory(Profile.path()))
					{
						for (const auto& Version : fs::directory_iterator(Profile.path()))
						{
							if (CheckDirectory(Version.path()))
							{
								for (const auto& Package : fs::directory_iterator(Version.path()))
								{
									if (CheckDirectory(Package.path()))
									{
										Discover(Package.path(), U8Path(L.Management), "generated");
									}
								}
							}
						}
					}
				}
			}
			for (const auto& R : State.value("retired", J::array()))
			{
				W.Check();
				auto Kind = R.at("kind").get<std::string>(), Path = R.at("path").get<std::string>();
				if (Kind != "package" && Kind != "generated")
				{
					Fail("cleanup-kind", "invalid owned cleanup record");
				}
				CheckPath(Path);
				auto Base = U8Path(Kind == "generated" ? L.Management : L.Packages),
				     Full = Base / U8Path(Path);
				if (!Under(Full, Base))
				{
					Fail("cleanup-path", "retired path is outside its owned root");
				}
				if (IsReferenced(Kind, Path, Full))
				{
					Retained.push_back(R);
					continue;
				}
				if (!fs::exists(Full))
				{
					continue;
				}
				try
				{
					NoLinks(Full);
					const auto LeasePath = Full / ".vero-lease", ReceiptPath = Full / ".vero-receipt.json";
					NoLinks(LeasePath, true);
					NoLinks(ReceiptPath, true);
					if (!fs::exists(ReceiptPath))
					{
						// A prior cleanup may have removed its receipt immediately before
						// stopping. Only committed retired records authorize this empty tail;
						// an unrecorded orphan without a receipt has no ownership evidence.
						if (!Recorded.count(Kind + ":" + Path))
						{
							Fail("package-corrupt", "retired ownership receipt is missing");
						}
						const auto CheckTail = [&]
						{
							for (const auto& F : fs::directory_iterator(Full))
							{
								W.Check();
								NoLinks(F.path());
								if (F.path().filename() != ".vero-lease" || !F.is_regular_file() || F.file_size() != 0)
								{
									Fail("package-corrupt", "receiptless retired directory is not empty");
								}
							}
						};
						if (fs::exists(LeasePath))
						{
							if (!fs::is_regular_file(LeasePath) || fs::file_size(LeasePath) != 0)
							{
								Fail("package-corrupt", "retired lease is not an empty regular file");
							}
							FFileLock Lease(LeasePath, true, false, false, &W);
							CheckTail();
						}
						else
						{
							CheckTail();
						}
					}
					else
					{
						if (!fs::is_regular_file(LeasePath) || fs::file_size(LeasePath) != 0)
						{
							Fail("package-corrupt", "retired lease is missing or invalid");
						}
						FFileLock Lease(LeasePath, true, false, false, &W);
						auto Receipt = ReadRecord(ReceiptPath, W);
						if (Receipt.value("format", std::string{}) !=
						        (Kind == "package" ? "veroscript-package-receipt" : "veroscript-generated-receipt") ||
						    Receipt.value("formatVersion", std::string{}) != FormatVersion ||
						    Receipt.at("environmentId") != State.at("environmentId") ||
						    Receipt.at("inventory") != R.at("inventory"))
						{
							Fail("package-corrupt", "retired ownership receipt differs");
						}
						// Missing entries may already have been removed by an interrupted
						// cleanup. Every remaining entry must still match before any deletion.
						const auto Actual = RemainingInventory(Full, R.at("inventory"));
						for (auto I = Actual.rbegin(); I != Actual.rend(); ++I)
						{
							W.Check();
							auto F = Full / U8Path(I->at("path").get<std::string>());
							NoLinks(F);
							fs::remove(F);
						}
						fs::remove(ReceiptPath);
					}
					// The lease has closed. Delete only metadata and an empty directory.
					fs::remove(LeasePath);
					if (!fs::remove(Full))
					{
						Retained.push_back(R);
						continue;
					}
					Removed.push_back(R);
				}
				catch (const FFailure& E)
				{
					if (E.Diagnostic.Code != "environment-busy" && E.Diagnostic.Code != "package-corrupt")
					{
						throw;
					}
					Retained.push_back(R);
					continue;
				}
				catch (const fs::filesystem_error&)
				{
					// Sharing/permission failures can interrupt a deletion after earlier
					// entries were removed. Preserve the record so a later clean can resume.
					Retained.push_back(R);
				}
			}
			State["retired"] = std::move(Retained);
			return {{"removed", Removed}, {"retained", State["retired"]}};
		}
	} // namespace
	bool FPackageManager::Execute(const FRequest& Request, FResult& Result, FDiagnostic& Error) const
	{
		Result = {};
		Error = {};
		std::string Stage = "arguments";
		try
		{
			FRequest Q = Request;
			auto& O = Q.Options;
			if (O.Upgrade && O.Locked)
			{
				Fail("argument-conflict", "upgrade cannot be combined with locked or frozen");
			}
			if (O.SkipHooks && Q.Operation != EOperation::Uninstall)
			{
				Fail("argument-conflict", "skip-hooks is only valid for uninstall");
			}
			if (O.SkipHooks)
			{
				O.Offline = true;
			}
			FWork W{O};
			W.Check();
			if (!Q.RequirementFiles.empty() && Q.Operation != EOperation::Install)
			{
				Fail("argument-conflict", "requirements files are only valid for install");
			}
			auto InstallInputs =
			    Q.Operation == EOperation::Install ? ReadInstallInputs(Q, W) : FInstallInputs{};
			FEnvironmentLayout L;
			if (!ResolveEnvironment(Q.Environment, L, Error))
			{
				return false;
			}
			FOperationScope Scope(L.Management);
			auto P = (Q.Operation == EOperation::Pack || Q.Operation == EOperation::Publish)
			    ? NewProject("pack-project")
			    : LoadRequirements(L, W, true),
			     State = LoadState(L, W);
			ConfigureStorage(L, Q.Environment, P, State);
			Stage = "environment";
			if ((Q.Operation == EOperation::Show || Q.Operation == EOperation::List) &&
			    !fs::exists(U8Path(L.State)) && fs::exists(U8Path(L.Management) / "transaction.json"))
			{
				Fail("recovery-required", "installation transaction needs writer recovery");
			}
			if (Q.Operation == EOperation::Search || Q.Operation == EOperation::Show)
			{
				if (Q.Arguments.size() != 1)
				{
					Fail("arguments", "search/show requires exactly one query");
				}
				FRepository R(L, P, O, W);
				if (Q.Operation == EOperation::Search)
				{
					Result.Json = R.RequestJson("official", "/search?q=" + EscapeUrl(Q.Arguments[0])).dump(2);
					return true;
				}
				// Installed descriptions are available offline without loading code.
				if (fs::exists(U8Path(L.State)))
				{
					FFileLock Guard(ManagementRoot(L) / "manager.lock", false, false, true, &W);
					auto Data = ReadInstalled(L, W);
					for (const auto& N : Data->Packages)
					{
						if (N.Name == Q.Arguments[0])
						{
							Result.Json = J{
							    {"formatVersion", FormatVersion},
							    {"name", N.Name},
							    {"version", N.Version},
							    {"sourceId", N.SourceId},
							    {"manifest", Parse(N.ManifestJson, O.Limits)},
							    {"enabled",
							        N.Enabled}}
							                  .dump(2);
							return true;
						}
					}
				}
				if (!Name(Q.Arguments[0]))
				{
					Fail("package-name", "show requires a canonical package name");
				}
				Result.Json = R.RequestJson("official", "/packages/" + EscapeUrl(Q.Arguments[0])).dump(2);
				return true;
			}
			if (Q.Operation == EOperation::Pack)
			{
				if (Q.Arguments.size() > 1)
				{
					Fail("arguments", "pack accepts at most one source directory");
				}
				auto Root = Q.Arguments.empty() ? Absolute(Q.Environment.WorkingDirectory.empty()
				                                          ? L.Root
				                                          : Q.Environment.WorkingDirectory,
				                                      {})
				                                : Absolute(Q.Arguments[0], Q.Environment.WorkingDirectory);
				const bool FromProject = !fs::exists(Root / "veroscript-package.json");
				const auto ManifestPath = Root / (FromProject ? "veroscript-project.json" : "veroscript-package.json");
				const auto ManifestHash = HashFile(ManifestPath, W, O.Limits.MaxManifestBytes);
				auto ManifestInput = ReadRecord(ManifestPath, W);
				auto M = FromProject ? PrepareDistribution(Root, ManifestInput, W) : Manifest(ManifestInput);
				auto Output = Q.OutputPath.empty() ? U8Path(L.Root).parent_path() /
				        U8Path(M.at("name").get<std::string>() + "-" +
				            M.at("version").get<std::string>() + ".vspkg")
				                                   : Absolute(Q.OutputPath, Q.Environment.WorkingDirectory);
				if (FromProject)
				{
					FTempDirectory Temp(U8Path(L.Cache) / "staging");
					// The source project config is not the generated distribution manifest.
					auto Files = Inventory(Root, W, true, M, false);
					Files.erase(std::remove_if(Files.begin(), Files.end(), [](const J& F)
					{
						auto Path = F.at("path").get<std::string>();
						std::transform(Path.begin(), Path.end(), Path.begin(), [](unsigned char C)
						{
							return char(C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C);
						});
						return Path == "veroscript-project.json";
					}),
					    Files.end());
					CopyInventory(Root, Temp.Path / "content", Files, W);
					Write(Temp.Path / U8Path("content/veroscript-package.json"), M.dump(2) + "\n");
					if (HashFile(ManifestPath, W, O.Limits.MaxManifestBytes) != ManifestHash)
					{
						Fail("pack-input-changed", "project configuration changed while preparing the distribution");
					}
					Pack(Temp.Path / "content", Output, M, W);
				}
				else
				{
					Pack(Root, Output, M, W);
				}
				Result.Json = J{{"formatVersion", FormatVersion},
				    {"path", NativePathText(Output)},
				    {"size", fs::file_size(Output)},
				    {"sha256", HashFile(Output, W, O.Limits.MaxArchiveBytes)}}
				                  .dump(2);
				return true;
			}
			if (Q.Operation == EOperation::Publish)
			{
				Result.Json = Publication(Q, L, W).dump(2);
				return true;
			}
			if (Q.Operation == EOperation::List || Q.Operation == EOperation::Verify ||
			    Q.Operation == EOperation::Deploy)
			{
				if (!fs::exists(U8Path(L.State)) && Q.Operation == EOperation::List)
				{
					Result.Json =
					    J{{"formatVersion", FormatVersion}, {"root", L.Root}, {"packages", J::array()}}.dump(
					        2);
					return true;
				}
				FFileLock Guard(ManagementRoot(L) / "manager.lock", false, false, true, &W);
				auto Data = ReadInstalled(L, W);
				Result.Environment =
				    std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(Data));
				Result.Generation = Data->Generation;
				if (Q.Operation == EOperation::Deploy)
				{
					const auto Target = Q.Environment.Target.Core.empty() ? HostTargetContext() : Q.Environment.Target;
					return Result.Environment->DeployForTarget(Target, NativePathText(Absolute(Q.OutputPath, Q.Environment.WorkingDirectory)),
					    Q.DeploymentUsage, Q.DeploymentRoots, Result.Json, Error, O);
				}
				Result.Json = Result.Environment->ToJson();
				return true;
			}
			if (Q.Operation != EOperation::Install && Q.Operation != EOperation::Clean && Q.Arguments.empty())
			{
				Fail("arguments", "operation requires package names");
			}
			// Existing invalid requirements are rejected before creating any management
			// files. The lock only serializes writers after argument validation.
			FFileLock Guard(ManagementRoot(L) / "manager.lock", true, true, true, &W);
			FFileLock StoreGuard(U8Path(L.Packages) / ".vero-store.lock", true, true, true, &W);
			Recover(L, W);
			auto Expected = CaptureManagementSnapshot(L, W);
			auto LockedStore = L.Packages;
			P = LoadRequirements(L, W,
			    Q.Operation == EOperation::Install &&
			        (!InstallInputs.Items.empty() || !Q.RequirementFiles.empty()));
			State = LoadState(L, W);
			ConfigureStorage(L, Q.Environment, P, State);
			if (L.Packages != LockedStore)
			{
				Fail("environment-changed", "package directory changed while acquiring locks; retry");
			}
			if (State.contains("packageRoot") && Absolute(State["packageRoot"], L.Root) != U8Path(L.Packages))
			{
				Fail(
				    "store-relocation",
				    "relocating an existing package store requires explicit reinstallation into a new environment");
			}
			auto Owner = StoreOwner(L, State, W);
			auto Key = ProfileKey(L);
			FPublished Published{Result, U8Path(L.Management) / "transaction.json"};
			if (Q.Operation == EOperation::Clean)
			{
				Result.Json = Clean(L, State, W).dump(2);
				Commit(L, {{U8Path(L.State), State.dump(2) + "\n"}}, Expected, W);
				Result.Committed = true;
				Result.Generation = State.at("generation");
				return true;
			}
			auto Previous = fs::exists(U8Path(L.Lockfile)) ? ReadRecord(U8Path(L.Lockfile), W) : EmptyLock();
			J Profile = State["profiles"].value(Key, J{{"packages", J::object()}});
			auto OldProject = P;
			FGraph Old;
			bool HadInstalledProfile = State["profiles"].contains(Key);
			if (HadInstalledProfile && Profile.contains("target") &&
			    Profile.at("target") != TargetJson(L.Target))
			{
				Fail("lockfile-target", "select a separate lockfile for another target");
			}
			if (HadInstalledProfile &&
			    (!Previous.contains("format") || Profile.at("lockSha256") != Id(Previous) ||
			        Profile.at("target") != Previous.at("target")))
			{
				Fail(
				    "environment-stale",
				    "existing lock differs from its installed profile; restore the lock before modifying this environment");
			}
			if (Previous.contains("format") && HadInstalledProfile)
			{
				Old = GraphFromLock(Previous, P, W, false);
			}
			if (!HadInstalledProfile && Q.Operation != EOperation::Install)
			{
				Fail("environment-uninstalled", "selected profile has not been installed");
			}
			std::vector<std::shared_ptr<const void>> OldLeases;
			if (!Old.Nodes.empty())
			{
				OldContent(Old, Profile, L, W, Owner, OldLeases, !O.SkipHooks);
			}
			J UpgradeNames = J::array();
			std::set<std::string> PinNew;
			FRepository Repository(L, P, O, W);
			if (Q.Operation == EOperation::Install)
			{
				std::map<std::string, J> Requested;
				std::set<std::string> ExplicitNames;
				for (const auto& Input : InstallInputs.Items)
				{
					try
					{
						W.Check();
						J D;
						std::string Name = Input.Name;
						if (!Input.Location.empty())
						{
							const auto& Path = Input.Location;
							bool Url = Path.rfind("https://", 0) == 0 || Path.rfind("http://", 0) == 0;
							auto Location = Path;
							if (!Url)
							{
								Location = PathReference(U8Path(Path), U8Path(L.Root));
							}
							D = {{"version", ">=0.0.0"},
							    {Url ? "url"
							            : fs::is_directory(U8Path(Path)) ? "directory"
							                                             : "archive",
							        Location}};
							if (Url && (O.Locked || O.Offline))
							{
								// A repeated explicit URL still needs its recorded digest to
								// address verified cache content without fetching the URL again.
								auto Dependencies = P.find("dependencies");
								if (Dependencies != P.end())
								{
									for (auto I = Dependencies->begin(); I != Dependencies->end(); ++I)
									{
										W.Check();
										if ((!Name.empty() && Name != I.key()) ||
										    I.value().value("url", "") != Location ||
										    !I.value().contains("sha256"))
										{
											continue;
										}
										if (D.contains("sha256") && D["sha256"] != I.value()["sha256"])
										{
											Fail("requirements-conflict",
											    "URL matches different recorded content digests", Location);
										}
										D["sha256"] = I.value()["sha256"];
									}
								}
							}
							auto C = Repository.Local(Name, D);
							Name = C.Manifest["name"];
							D["version"] = C.Manifest["version"];
							if (!D.contains("directory"))
							{
								D["sha256"] = C.Artifact["sha256"];
							}
						}
						else
						{
							if (!Input.Version.empty())
							{
								D = {{"version", Input.Version}, {"source", "official"}};
							}
							else if (P.value("dependencies", J::object()).contains(Name))
							{
								D = P["dependencies"][Name];
							}
							else
							{
								D = {{"version", ">=0.0.0"}, {"source", "official"}};
								PinNew.insert(Name);
							}
						}
						bool Explicit = !Input.Version.empty() || !Input.Location.empty();
						if (Explicit)
						{
							PinNew.erase(Name);
						}
						auto Prior = Requested.find(Name);
						if (Explicit && Prior != Requested.end() && ExplicitNames.count(Name))
						{
							auto Existing = Prior->second, Incoming = D;
							Existing.erase("version");
							Incoming.erase("version");
							if (Existing != Incoming)
							{
								Fail("requirements-conflict",
								    "the same package has different requested sources or settings", Name);
							}
							D["version"] =
							    IntersectInstallVersions(Prior->second.at("version"), D.at("version"));
						}
						if (Explicit)
						{
							ExplicitNames.insert(Name);
						}
						Requested[Name] = D;
						P["dependencies"][Name] = D;
						if (!P.value("packages", J::object()).contains(Name))
						{
							P["packages"][Name] = {{"enabled", true}};
						}
						UpgradeNames.push_back(Name);
					}
					catch (FFailure& Failure)
					{
						LocateInstallFailure(Failure, Input);
						throw;
					}
				}
			}
			else
			{
				for (const auto& Name : Q.Arguments)
				{
					if (!Detail::Name(Name))
					{
						Fail("package-name", "invalid package name", Name);
					}
					if (Q.Operation == EOperation::Uninstall)
					{
						if (!P.value("dependencies", J::object()).contains(Name))
						{
							Fail("root-dependency", "uninstall accepts declared root dependencies only",
							    Name);
						}
						P["dependencies"].erase(Name);
						if (P.contains("packages"))
						{
							P["packages"].erase(Name);
						}
					}
					else
					{
						if (!Old.Nodes.count(Name))
						{
							Fail("package-uninstalled",
							    "package must be installed before changing its enabled state", Name);
						}
						P["packages"][Name]["enabled"] = Q.Operation == EOperation::Enable;
					}
				}
			}
			Stage = "resolve";
			auto Settings = P;
			Settings["_lockedSources"] = Previous.value("sources", J::object());
			if (!UpgradeNames.empty())
			{
				Settings["_upgradeNames"] = UpgradeNames;
			}
			FRepository Resolver(L, Settings, O, W);
			FGraph Graph;
			auto Sources = Resolver.Registries();
			bool Same = Previous.contains("format") && Previous.at("target") == TargetJson(L.Target) &&
			    Previous.at("requirementsFingerprint") ==
			        RequirementsFingerprint(P, L, Sources, O.Development, Previous["packages"]);
			if (Q.Operation == EOperation::Uninstall)
			{
				Graph = Prune(Old, P, L, Resolver, W);
				auto Lock = LockJson(Graph, P, L, Sources, O.Development);
				if (O.Locked && Lock != Previous)
				{
					Fail("locked-change", "uninstall would change the lockfile");
				}
				std::set<std::string> Removing;
				for (const auto& N : Old.Nodes)
				{
					if (!Graph.Nodes.count(N.first))
					{
						Removing.insert(N.first);
					}
				}
				FTempDirectory Staging(U8Path(L.Management) / "staging");
				auto Removal = PrepareRemoval(Old, Profile, Removing, Staging.Path, L, W);
				RunRemoval(Old, Removal, L, W);
				auto Generation = State.at("generation").get<std::uint64_t>();
				if (Generation == std::numeric_limits<std::uint64_t>::max())
				{
					Fail("generation-limit", "installation generation exhausted");
				}
				++Generation;
				auto NewProfile = Profile;
				NewProfile["lockSha256"] = Id(Lock);
				NewProfile["generation"] = Generation;
				for (const auto& Name : Removing)
				{
					const auto& R = Profile.at("packages").at(Name);
					State["retired"].push_back(
					    {{"kind", "package"}, {"path", R["path"]}, {"inventory", R["inventory"]}});
					State["retired"].push_back({{"kind", "generated"},
					    {"path", R["generated"]},
					    {"inventory", R["generatedInventory"]}});
					NewProfile["packages"].erase(Name);
				}
				State["profiles"][Key] = NewProfile;
				State["generation"] = Generation;
				auto Data = BuildInstalled(L, W, P, State, Lock, Owner);
				Commit(L,
				    {{U8Path(L.Requirements), P.dump(2) + "\n"},
				        {U8Path(L.Lockfile), Lock.dump(2) + "\n"},
				        {U8Path(L.State), State.dump(2) + "\n"}},
				    Expected, W);
				Result.Committed = true;
				Result.Generation = Generation;
				Result.Environment =
				    std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Data)));
				Result.Json = Result.Environment->ToJson();
				if (O.SkipHooks)
				{
					Result.Warnings.push_back(
					    {"hooks-skipped", "uninstall",
					        "Uninstall hooks and package processors were explicitly skipped; "
					        "external side effects were not reverted."});
				}
				return true;
			}
			if (!O.Upgrade && Same && InstallInputs.Items.empty())
			{
				for (const auto& N : Old.Nodes)
				{
					if (N.second.Source.at("kind") == "directory" || N.second.Source.at("kind") == "archive")
					{
						auto Path = Absolute(N.second.Source.at("location"), L.Root);
						if (fs::exists(Path))
						{
							J Spec = {{"version", N.second.Manifest.at("version")},
							    {N.second.Source.at("kind").get<std::string>(),
							        N.second.Source.at("location")}};
							auto Current = Resolver.Local(N.first, Spec);
							if (Current.Artifact.at("sha256") != N.second.Artifact.at("sha256"))
							{
								if (O.Locked)
								{
									Fail("locked-local-change", "local package content changed since locking",
									    N.first);
								}
								Same = false;
							}
						}
					}
				}
			}
			if ((Q.Operation == EOperation::Enable || Q.Operation == EOperation::Disable) ||
			    (!O.Upgrade && Same && InstallInputs.Items.empty() && HadInstalledProfile))
			{
				Graph = GraphFromLock(Previous, P, W);
			}
			else
			{
				Graph = Solve(P, Previous, Resolver, L, W);
			}
			for (auto& N : Graph.Nodes)
			{
				W.Check();
				auto Existing = Old.Nodes.find(N.first);
				if (Existing != Old.Nodes.end() &&
				    Existing->second.Artifact["sha256"] == N.second.Artifact["sha256"])
				{
					N.second.Content = Existing->second.Content;
					N.second.Inventory = Existing->second.Inventory;
					N.second.Manifest = Existing->second.Manifest;
				}
				else
				{
					Resolver.Materialize(N.second);
				}
			}
			for (const auto& N : PinNew)
			{
				P["dependencies"][N]["version"] = Graph.Nodes.at(N).Manifest["version"];
			}
			Activate(Graph, P, L.Target, O.Development);
			ValidateLibraries(Graph);
			auto Lock = LockJson(Graph, P, L, Sources, O.Development);
			if (O.Locked && (!Previous.contains("format") || Lock != Previous))
			{
				Fail("locked-change", "operation would change the lockfile");
			}
			if (Q.Operation == EOperation::Enable || Q.Operation == EOperation::Disable)
			{
				if (!Same)
				{
					Fail("optional-uninstalled",
					    "activation changes the dependency graph; run install explicitly");
				}
				auto Generation = State.at("generation").get<std::uint64_t>();
				if (Generation == std::numeric_limits<std::uint64_t>::max())
				{
					Fail("generation-limit", "installation generation exhausted");
				}
				State["generation"] = Generation + 1;
				State["profiles"][Key]["generation"] = State["generation"];
				auto Data = BuildInstalled(L, W, P, State, Lock, Owner);
				Commit(L,
				    {{U8Path(L.Requirements), P.dump(2) + "\n"}, {U8Path(L.State), State.dump(2) + "\n"}},
				    Expected, W);
				Result.Committed = true;
				Result.Generation = State["generation"];
				Result.Environment =
				    std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(Data));
				Result.Json = Result.Environment->ToJson();
				return true;
			}
			Stage = "prepare";
			Preflight(Graph, O);
			std::set<std::string> Removing;
			for (const auto& N : Old.Nodes)
			{
				if (!Graph.Nodes.count(N.first) ||
				    Graph.Nodes.at(N.first).Artifact["sha256"] != N.second.Artifact["sha256"])
				{
					Removing.insert(N.first);
				}
			}
			for (const auto& N : Graph.Nodes)
			{
				CheckFeatures(N.second, O);
			}
			auto Views = Displays(Graph, O, W);
			std::map<std::string, J> Inputs, Secrets;
			for (const auto& N : Graph.Nodes)
			{
				auto Configuration = P.value("packages", J::object())
				                         .value(N.first, J::object())
				                         .value("configuration", J::object());
				Inputs[N.first] =
				    PrepareInputs(N.second, Configuration, O, W, Secrets[N.first], &Views.at(N.first));
				if (!Inputs[N.first].empty())
				{
					P["packages"][N.first]["configuration"] = Inputs[N.first];
				}
			}
			auto Generation = State.at("generation").get<std::uint64_t>();
			if (Generation == std::numeric_limits<std::uint64_t>::max())
			{
				Fail("generation-limit", "installation generation exhausted");
			}
			++Generation;
			FTempDirectory Generated(U8Path(L.Management) / "staging");
			std::map<std::string, J> Loaders;
			for (const auto& N : Graph.Nodes)
			{
				auto Root = Generated.Path / U8Path(N.first);
				fs::create_directories(Root);
				Loaders[N.first] = PrepareLoaders(Graph, N.first, Root, W);
			}
			// Validate all inputs, locales and loader closures before the first hook.
			std::vector<std::string> After;
			for (const auto& N : Graph.Order)
			{
				auto Pkg = Describe(Graph.Nodes.at(N));
				(void)Render(Pkg, "install.before", Views.at(N), W);
				After.push_back(Render(Pkg, "install.after", Views.at(N), W));
			}
			auto Removal = PrepareRemoval(Old, Profile, Removing, Generated.Path, L, W);
			for (const auto& Name : Graph.Order)
			{
				for (auto Phase : {"pre-install", "post-install"})
				{
					RunHooks(Graph.Nodes.at(Name), Phase, Graph, Inputs[Name], Secrets[Name],
					    Generated.Path / U8Path(Name), L, Views.at(Name), W, true);
				}
			}
			Stage = "processing";
			RunRemoval(Old, Removal, L, W);
			for (const auto& Name : Graph.Order)
			{
				const auto& N = Graph.Nodes.at(Name);
				auto Root = Generated.Path / U8Path(Name);
				auto Before = Render(Describe(N), "install.before", Views.at(Name), W);
				if (O.Message && !Before.empty())
				{
					O.Message(Before);
				}
				RunFeatures(N, "install", Inputs[Name], Secrets[Name], Root, L, Views.at(Name), W);
				RunHooks(N, "pre-install", Graph, Inputs[Name], Secrets[Name], Root, L, Views.at(Name), W);
				InstallActions(N, Root, Inputs[Name], W);
				RunHooks(N, "post-install", Graph, Inputs[Name], Secrets[Name], Root, L, Views.at(Name), W);
			}
			J NewProfile = {{"target", TargetJson(L.Target)},
			    {"generation", Generation},
			    {"lockfile", PathReference(U8Path(L.Lockfile), U8Path(L.Root))},
			    {"lockSha256", Id(Lock)},
			    {"packages", J::object()}};
			Stage = "publish";
			FTempDirectory PackageStaging(U8Path(L.Packages) / ".staging");
			for (const auto& Name : Graph.Order)
			{
				auto& N = Graph.Nodes.at(Name);
				auto Relative = fs::path(Name) / (N.Manifest.at("version").get<std::string>() + "-" + N.Artifact.at("sha256").get<std::string>());
				auto Final = U8Path(L.Packages) / Relative;
				if (!fs::exists(Final))
				{
					auto Staged = PackageStaging.Path / U8Path(Name);
					CopyInventory(N.Content, Staged, N.Inventory, W);
					Write(Staged / ".vero-lease", "");
					Write(Staged / ".vero-receipt.json",
					    J{{"format", "veroscript-package-receipt"},
					        {"formatVersion", FormatVersion},
					        {"environmentId", Owner},
					        {"artifact", N.Artifact},
					        {"manifestSha256", N.Artifact.at("manifestSha256")},
					        {"inventory", N.Inventory}}
					        .dump());
					NoLinks(Final.parent_path(), true);
					fs::create_directories(Final.parent_path());
					fs::rename(Staged, Final);
					Published.Directories.emplace_back(Final, U8Path(L.Packages));
				}
				else
				{
					auto Receipt = ReadRecord(Final / ".vero-receipt.json", W);
					if (Receipt.at("environmentId") != Owner || Receipt.at("artifact") != N.Artifact)
					{
						Fail("immutable-collision", "existing immutable directory has "
						                            "different ownership or identity");
					}
					VerifyInventory(Final, N.Inventory, W);
				}
				auto Source = Generated.Path / U8Path(Name);
				VerifyLoaders(Source, Loaders[Name], W);
				auto Files = Inventory(Source, W);
				J FeatureIdentities = J::object();
				for (const auto& F : N.Manifest.value("requires", J::object()).value("features", J::array()))
				{
					auto Id = F.at("id").get<std::string>();
					if (Id != "veroscript.package-manager")
					{
						auto H = O.Features.at(Id);
						FeatureIdentities[Id] = {{"version", H->GetVersion()},
						    {"implementation", H->GetImplementationIdentity()}};
					}
				}
				Write(Source / ".vero-lease", "");
				Write(Source / ".vero-receipt.json", J{{"format", "veroscript-generated-receipt"}, {"formatVersion", FormatVersion}, {"environmentId", Owner}, {"inventory", Files}}.dump());
				auto GenRelative = fs::path("generated") / Key / InventoryHash(Files) / Name;
				auto GenFinal = U8Path(L.Management) / GenRelative;
				fs::create_directories(GenFinal.parent_path());
				if (fs::exists(GenFinal))
				{
					auto Receipt = ReadRecord(GenFinal / ".vero-receipt.json", W);
					if (Receipt.at("environmentId") != Owner || Receipt.at("inventory") != Files)
					{
						Fail("generated-collision",
						    "abandoned generated directory has invalid ownership or content");
					}
					VerifyInventory(GenFinal, Files, W);
				}
				else
				{
					fs::rename(Source, GenFinal);
					Published.Directories.emplace_back(GenFinal, U8Path(L.Management));
				}
				NewProfile["packages"][Name] = {
				    {"path", PathUtil::ToUtf8(Relative)},
				    {"inventory", N.Inventory},
				    {"configuration", Inputs[Name]},
				    {"generated", PathUtil::ToUtf8(GenRelative)},
				    {"generatedInventory", Files},
				    {"loaders", Loaders[Name]},
				    {"processing", J{{"host", TargetJson(L.Host)}, {"target", TargetJson(L.Target)}, {"package", N.Artifact["sha256"]}, {"inputs", Inputs[Name]}, {"hasSecrets", !Secrets[Name].empty()}, {"displayLanguage", Views[Name].Configuration().Language}, {"displayFallbacks", Views[Name].Configuration().FallbackLanguages}, {"skippedUninstallHooks", O.SkipHooks}, {"processors", FeatureIdentities}}}};
			}
			auto OldRecords = Profile.value("packages", J::object());
			for (auto I = OldRecords.begin(); I != OldRecords.end(); ++I)
			{
				const auto& R = I.value();
				if (!NewProfile["packages"].contains(I.key()) ||
				    NewProfile["packages"][I.key()]["generated"] != R["generated"])
				{
					State["retired"].push_back({{"kind", "generated"},
					    {"path", R["generated"]},
					    {"inventory", R["generatedInventory"]}});
				}
				if (!NewProfile["packages"].contains(I.key()) ||
				    NewProfile["packages"][I.key()]["path"] != R["path"])
				{
					State["retired"].push_back(
					    {{"kind", "package"}, {"path", R["path"]}, {"inventory", R["inventory"]}});
				}
			}
			State["profiles"][Key] = NewProfile;
			State["generation"] = Generation;
			State["environmentId"] = Owner;
			State["packageRoot"] = PathReference(U8Path(L.Packages), U8Path(L.Root));
			J Marker = {{"format", "veroscript-package-store"},
			    {"formatVersion", FormatVersion},
			    {"environmentId", Owner},
			    {"management", PathReference(ManagementRoot(L), U8Path(L.Packages))}};
			Stage = "commit";
			auto Data = BuildInstalled(L, W, P, State, Lock, Owner);
			VerifyInstallInputs(InstallInputs, W);
			Commit(L,
			    {{U8Path(L.Requirements), P.dump(2) + "\n"},
			        {U8Path(L.Lockfile), Lock.dump(2) + "\n"},
			        {U8Path(L.State), State.dump(2) + "\n"},
			        {U8Path(L.Packages) / ".vero-owner.json", Marker.dump(2) + "\n"}},
			    Expected, W);
			Result.Committed = true;
			Result.Generation = Generation;
			Result.Environment =
			    std::shared_ptr<const FInstalledEnvironment>(new FInstalledEnvironment(std::move(Data)));
			Result.Json = Result.Environment->ToJson();
			Stage = "after-message";
			if (O.Message)
			{
				for (const auto& Message : After)
				{
					if (!Message.empty())
					{
						O.Message(Message);
					}
				}
			}
			return true;
		}
		catch (const FFailure& X)
		{
			if (Result.Committed)
			{
				Result.Warnings.push_back(X.Diagnostic);
				return true;
			}
			Error = X.Diagnostic;
			Error.Stage = Stage;
			return false;
		}
		catch (const std::exception& X)
		{
			FDiagnostic E;
			E.Code = "package-operation";
			E.Stage = Stage;
			E.Message = X.what();
			if (Result.Committed)
			{
				Result.Warnings.push_back(E);
				return true;
			}
			Error = std::move(E);
			return false;
		}
	}
} // namespace VeroScript::Packages
