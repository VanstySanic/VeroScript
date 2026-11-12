// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplication.h"
#include "ApplicationInternal.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptVersion.h"
#include "VeroScriptJitBuild.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <regex>
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

namespace VeroScript::Application
{
	using namespace Detail;
	namespace
	{
		constexpr const char* DescriptorPath = "veroscript-app.json";
		constexpr const char* InventoryPath = "veroscript-app.files.json";
		constexpr const char* SignaturePath = "veroscript-app.files.json.sig.json";
		bool IsDigest(const std::string& S)
		{
			return S.size() == 64 && std::all_of(S.begin(), S.end(), [](char C)
			{
				return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
			});
		}
		bool IsSemver(const std::string& S)
		{
			if (S.empty() || S.size() > 255) return false;
			const auto Identifiers = [](std::string_view V, bool NumericLeadingZero)
			{
				for (std::size_t Start = 0;;)
				{
					const auto End = V.find('.', Start);
					const auto Part = V.substr(Start, End == std::string_view::npos ? V.size() - Start : End - Start);
					if (Part.empty()) return false;
					bool Numeric = true;
					for (const auto C : Part)
					{
						if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-')) return false;
						Numeric = Numeric && C >= '0' && C <= '9';
					}
					if (!NumericLeadingZero && Numeric && Part.size() > 1 && Part.front() == '0') return false;
					if (End == std::string_view::npos) return true;
					Start = End + 1;
				}
			};
			std::string_view V = S;
			const auto Plus = V.find('+');
			if (Plus != std::string_view::npos)
			{
				if (!Identifiers(V.substr(Plus + 1), true)) return false;
				V = V.substr(0, Plus);
			}
			const auto Dash = V.find('-');
			if (Dash != std::string_view::npos)
			{
				if (!Identifiers(V.substr(Dash + 1), false)) return false;
				V = V.substr(0, Dash);
			}
			unsigned Parts = 0;
			for (std::size_t Start = 0;;)
			{
				const auto End = V.find('.', Start);
				const auto Part = V.substr(Start, End == std::string_view::npos ? V.size() - Start : End - Start);
				if (Part.empty() || (Part.size() > 1 && Part.front() == '0') || !std::all_of(Part.begin(), Part.end(), [](char C)
				{
					return C >= '0' && C <= '9';
				}))
				{
					return false;
				}
				++Parts;
				if (End == std::string_view::npos) return Parts == 3;
				Start = End + 1;
			}
		}
		J Descriptor(std::string_view Text, const FLimits& Limits)
		{
			auto D = Parse(Text, Limits);
			Keys(D, {"format", "formatVersion", "id", "version", "entry", "platform", "requirements", "runtime", "environment", "services", "resources", "files", "extensions", "modules", "compile"}, "application");
			if (D.value("format", "") != "veroscript-application" || D.value("formatVersion", "") != "0.1.1")
			{
				Fail("application-format", "unsupported application descriptor format");
			}
			static const std::regex Id("[A-Za-z_][A-Za-z0-9_.-]{0,254}");
			if (!std::regex_match(D.at("id").get<std::string>(), Id) ||
			    !IsSemver(D.at("version").get<std::string>()))
			{
				Fail("application-identity", "invalid application identity or product version");
			}
			const auto& Entry = D.at("entry");
			Keys(Entry, {"kind", "path", "packageName", "packageVersion"}, "entry");
			const auto Kind = Entry.at("kind").get<std::string>();
			if (Kind != "compiled-image" && Kind != "source")
			{
				Fail("entry-kind", "application entry must be a compiled image or source");
			}
			Path(Entry.at("path").get<std::string>());
			if (Kind == "compiled-image")
			{
				if (!std::regex_match(Entry.at("packageName").get<std::string>(), Id))
				{
					Fail("entry-identity", "invalid entry image identity");
				}
				(void)Count(Entry.at("packageVersion"), "entry.packageVersion");
			}
			if (D.at("files") != InventoryPath)
			{
				Fail("inventory-path", "application must use the canonical content inventory");
			}
			for (const auto* Field : {"runtime", "environment"})
			{
				const auto& Component = D.at(Field);
				Keys(Component, {"mode", "path", "requirements"}, Field);
				const auto Mode = Component.at("mode").get<std::string>();
				if (Mode != "external" && Mode != "bundled")
				{
					Fail("component-mode", "unsupported application component mode", Field);
				}
				if (Mode == "bundled")
				{
					Path(Component.at("path").get<std::string>());
				}
				else if (Component.contains("path"))
				{
					Fail("component-path", "external components cannot contain a build-machine path", Field);
				}
			}
			Keys(D.at("requirements"), {"coreVersion", "coreBuild", "capabilities"}, "requirements");
			if (D.at("requirements").value("coreVersion", "") != "0.1.1" ||
			    !D.at("requirements").at("capabilities").is_array() || !D.at("resources").is_array() ||
			    !D.at("services").is_object() || !D.at("extensions").is_object())
			{
				Fail("application-requirements", "invalid application requirements or extension metadata");
			}
			if (!D.at("platform").is_object() && D.at("platform") != "portable")
			{
				Fail("application-platform", "platform must be portable or an explicit target descriptor");
			}
			if (D.at("platform").is_object())
			{
				FDiagnostic Error;
				if (!ValidateApplicationTarget(D.at("platform").dump(), Error, Limits))
				{
					Fail(Error.Code, Error.Message, Error.Path);
				}
			}
			for (const auto& Resource : D.at("resources"))
			{
				Keys(Resource, {"id", "ownerId", "group", "path", "moduleIds"}, "resource");
				if (!std::regex_match(Resource.at("id").get<std::string>(), Id))
				{
					Fail("resource-id", "invalid application resource provider identity");
				}
				Path(Resource.at("path").get<std::string>());
				if (Resource.contains("ownerId") && !std::regex_match(Resource.at("ownerId").get<std::string>(), Id))
				{
					Fail("resource-owner", "invalid resource owner identity");
				}
				if (Resource.contains("group") && !Resource.at("group").is_string())
				{
					Fail("resource-group", "resource group must be a string");
				}
				if (Resource.contains("moduleIds"))
				{
					if (!Resource.at("moduleIds").is_array())
					{
						Fail("resource-modules", "resource moduleIds must be an array");
					}
					for (const auto& M : Resource.at("moduleIds"))
					{
						if (!M.is_string() || M.get<std::string>().empty())
						{
							Fail("resource-modules", "resource module identity must be a nonempty string");
						}
					}
				}
			}
			for (auto S = D.at("services").begin(); S != D.at("services").end(); ++S)
			{
				if (!std::regex_match(S.key(), Id) || !S.value().is_string() || !std::regex_match(S.value().get<std::string>(), Id))
				{
					Fail("application-service", "service selectors must map service names to backend identifiers");
				}
			}
			if (D.contains("modules"))
			{
				if (!D.at("modules").is_object())
				{
					Fail("application-modules", "module mappings must be an object");
				}
				for (auto I = D.at("modules").begin(); I != D.at("modules").end(); ++I)
				{
					Path(I.key());
					Path(I.value().get<std::string>());
				}
			}
			if (D.contains("compile"))
			{
				if (Kind != "source")
				{
					Fail("application-compile", "only source applications have compilation defaults");
				}
				const auto& C = D.at("compile");
				Keys(C, {"defines", "predefinedMacros", "implicitImports", "allowCircularImports", "allowProjectWideAccess"}, "compile");
				for (const auto* K : {"allowCircularImports", "allowProjectWideAccess"})
				{
					if (C.contains(K) && !C.at(K).is_boolean())
					{
						Fail("application-compile", "compilation flags must be boolean", K);
					}
				}
				const auto Strings = [&](const J& A)
				{
					if (!A.is_array() || A.size() > 4096)
					{
						Fail("application-compile", "invalid compilation string array");
					}
					for (const auto& V : A)
					{
						if (!V.is_string() || V.get_ref<const std::string&>().find('\0') != std::string::npos)
						{
							Fail("application-compile", "compilation strings cannot contain zero bytes");
						}
					}
				};
				Strings(C.value("defines", J::array()));
				Strings(C.value("implicitImports", J::array()));
				for (const auto& P : C.value("implicitImports", J::array()))
				{
					Path(P.get<std::string>());
				}
				if (C.contains("predefinedMacros"))
				{
					if (!C.at("predefinedMacros").is_array() || C.at("predefinedMacros").size() > 4096)
					{
						Fail("application-compile", "invalid predefined macro array");
					}
					for (const auto& M : C.at("predefinedMacros"))
					{
						Keys(M, {"name", "parameters", "variadic", "body", "sourceName"}, "predefinedMacro");
						Strings(M.at("parameters"));
						for (const auto* K : {"name", "body", "sourceName"})
						{
							if (!M.at(K).is_string() || M.at(K).get_ref<const std::string&>().find('\0') != std::string::npos)
							{
								Fail("application-compile", "invalid predefined macro text", K);
							}
						}
						if (M.at("name").get_ref<const std::string&>().empty() || !M.at("variadic").is_boolean())
						{
							Fail("application-compile", "invalid predefined macro declaration");
						}
					}
				}
			}
			return D;
		}
		std::string Text(const std::vector<std::uint8_t>& Data)
		{
			return std::string(Data.begin(), Data.end());
		}
		void Raise(const FDiagnostic& E)
		{
			Fail(E.Code, E.Message, E.Path);
		}
		class FCacheLease
		{
		public:
			explicit FCacheLease(const fs::path& Path, bool Create = true)
			{
				Physical(Path, Create);
				#if defined(_WIN32)
				Handle = CreateFileW(Path.c_str(), Create ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
				    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, Create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (Handle == INVALID_HANDLE_VALUE)
				{
					Fail("cache-lock", "cannot open application cache lease");
				}
				#else
				Handle = ::open(Path.c_str(), (Create ? O_RDWR | O_CREAT : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
				if (Handle < 0)
				{
					Fail("cache-lock", "cannot open application cache lease");
				}
				#endif
			}
			~FCacheLease()
			{
				Unlock();
				#if defined(_WIN32)
				CloseHandle(Handle);
				#else
				::close(Handle);
				#endif
			}
			bool TryLock(bool Exclusive)
			{
				#if defined(_WIN32)
				OVERLAPPED Offset{};
				Held = LockFileEx(Handle, LOCKFILE_FAIL_IMMEDIATELY | (Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0),
				           0, 1, 0, &Offset) != 0;
				#else
				Held = ::flock(Handle, (Exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) == 0;
				#endif
				return Held;
			}
			void LockShared(const FLimits& Limits)
			{
				const auto Start = std::chrono::steady_clock::now();
				while (!TryLock(false))
				{
					Check(Limits);
					if (std::chrono::steady_clock::now() - Start > std::chrono::seconds(30))
					{
						Fail("cache-busy", "application cache lease timed out");
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			}
			void Unlock()
			{
				if (!Held) return;
				#if defined(_WIN32)
				OVERLAPPED Offset{};
				UnlockFileEx(Handle, 0, 1, 0, &Offset);
				#else
				::flock(Handle, LOCK_UN);
				#endif
				Held = false;
			}

		private:
			#if defined(_WIN32)
			HANDLE Handle = INVALID_HANDLE_VALUE;
			#else
			int Handle = -1;
			#endif
			bool Held = false;
		};
		class FApplicationReader final : public IArtifactReader
		{
		public:
			std::string GetIdentity() const override
			{
				return App->GetIdentity() + ":" + (Fallback ? Fallback->GetIdentity() : "files");
			}
			bool Read(const FArtifactRequest& Request, const FArtifactWork& Work,
			    FArtifactData& Data, std::string& Error) const override
			{
				if (Request.Path.rfind(Prefix, 0) == 0)
				{
					FDiagnostic D;
					if (!App->ReadStored(Request.Path.substr(Prefix.size()), Data, D, Work))
					{
						Error = D.Message;
						return false;
					}
					return true;
				}
				for (const auto& Root : PhysicalRoots)
				{
					if (PathUtil::FromUtf8(Request.Path).is_absolute() && Under(PathUtil::FromUtf8(Request.Path), PathUtil::FromUtf8(Root.first)))
					{
						FDiagnostic D;
						const auto Relative = PathUtil::ToUtf8(PathUtil::FromUtf8(Request.Path).lexically_relative(PathUtil::FromUtf8(Root.first)));
						if (!App->ReadStored(Root.second + "/" + Relative, Data, D, Work))
						{
							Error = D.Message;
							return false;
						}
						return true;
					}
				}
				return Fallback ? Fallback->Read(Request, Work, Data, Error) : ReadArtifactFile(Request, Work, Data, Error);
			}
			std::shared_ptr<const FApplication> App;
			std::shared_ptr<const IArtifactReader> Fallback;
			std::string Prefix;
			std::vector<std::pair<std::string, std::string>> PhysicalRoots;
		};
		class FApplicationResources final : public IResourceProvider
		{
		public:
			bool Read(const FResourceRequest& Request, const FResourceWork& Work,
			    FResourceData& Out, std::string& Error) const override
			{
				Out = {};
				if (Request.Group != Group)
				{
					return true;
				}
				std::string PathError;
				if (!ValidateContentPath(Request.Path, PathError))
				{
					Error = PathError;
					return false;
				}
				const auto Name = Root + "/" + Request.Path;
				const auto& Entries = App->GetEntries();
				if (std::none_of(Entries.begin(), Entries.end(), [&](const auto& E)
				{
					return E.Path == Name;
				}))
				{
					return true;
				}
				FArtifactData Data;
				FDiagnostic D;
				if (!App->Read(Name, Data, D, {Work.MaxBytes, Work.CheckCancelled}))
				{
					Error = D.Message;
					return false;
				}
				Out.Owner = App;
				Out.Bytes = std::move(Data.Bytes);
				Out.ContentIdentity = Data.ContentIdentity;
				Out.ArtifactTrustIdentity = Data.TrustIdentity;
				Out.bExists = true;
				return true;
			}
			std::shared_ptr<const FApplication> App;
			std::string Root, Group;
		};
	}
	std::shared_ptr<const void> AcquireApplicationDirectoryLease(const std::string& Root, bool Exclusive, FDiagnostic& Error)
	{
		try
		{
			Physical(PathUtil::FromUtf8(Root));
			Physical(PathUtil::FromUtf8(Root) / ".vero-lease");
			// Keep the cooperative lock outside the published tree: Windows cannot
			// rename a parent directory while a descendant file handle is open.
			const auto Locks = PathUtil::FromUtf8(DefaultApplicationCacheRoot()) / "directory-leases";
			Physical(Locks, true);
			fs::create_directories(Locks);
			Physical(Locks);
			const auto Identity = PathUtil::IdentityKey(PathUtil::ToUtf8(TreePath(PathUtil::FromUtf8(Root))));
			auto Lease = std::make_shared<FCacheLease>(Locks / (ContentSha256(Identity) + ".lock"));
			if (!Lease->TryLock(Exclusive))
			{
				Fail("content-busy", "application content is held by another reader or publisher", Root);
			}
			Error = {};
			return Lease;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"content-lease", E.what(), Root, {}};
			return {};
		}
	}
	struct FApplication::FImpl
	{
		bool ReadRaw(const std::string& Path, std::vector<std::uint8_t>& Out, FDiagnostic& Error,
		    const FArtifactWork& Work) const
		{
			if (Archive) return Archive->Read(Path, Out, Error, Work);
			FArtifactData Data;
			std::string Failure;
			const auto P = PathUtil::FromUtf8(Root) / PathUtil::FromUtf8(Path);
			if (!CheckPhysicalPath(PathUtil::ToUtf8(P), false, Error)) return false;
			if (!ReadArtifactFile({PathUtil::ToUtf8(P), "generic", {}}, Work, Data, Failure))
			{
				Error = {"content-read", Failure, Path, {}};
				return false;
			}
			Out = std::move(Data.Bytes);
			return true;
		}
		J Descriptor, Inventory;
		std::string Json, Identity, Root, TrustIdentity;
		std::shared_ptr<const FArchive> Archive;
		std::shared_ptr<const void> DirectoryLease;
		std::shared_ptr<const void> ContentOwner;
		bool ExternallyVerified = false;
		std::vector<FContentEntry> Entries, Actual;
		std::map<std::string, std::uint64_t> Sizes;
		std::map<std::string, std::string> Hashes;
		std::vector<std::uint8_t> IndexBytes, SignatureBytes;
		FApplicationServices Services;
		FLimits Limits;
	};
	std::shared_ptr<const FApplication> FApplication::Open(const std::string& Path,
	    const FApplicationServices& Services, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			Check(Limits);
			Physical(PathUtil::FromUtf8(Path));
			auto Impl = std::make_shared<FImpl>();
			Impl->Services = Services;
			Impl->Limits = Limits;

			if (fs::is_directory(PathUtil::FromUtf8(Path)))
			{
				Impl->Root = Path;
				Impl->DirectoryLease = AcquireApplicationDirectoryLease(Path, false, Error);
				if (!Impl->DirectoryLease) return {};
				const auto Base = TreePath(PathUtil::FromUtf8(Path));
				for (const auto& Item : fs::recursive_directory_iterator(Base))
				{
					Check(Limits);
					Physical(Item.path());
					if (Item.is_directory())
					{
						continue;
					}
					if (!Item.is_regular_file() || Impl->Actual.size() >= Limits.MaxFiles)
					{
						Fail("application-content", "unsupported file or directory entry budget exceeded");
					}
					Impl->Actual.push_back({PathUtil::ToUtf8(Item.path().lexically_relative(Base)), Item.file_size(), false});
				}
			}
			else
			{
				std::error_code SidecarError;
				const auto Sidecar = fs::symlink_status(PathUtil::FromUtf8(Path + ".sig.json"), SidecarError);
				if (SidecarError && SidecarError != std::errc::no_such_file_or_directory)
				{
					Fail("signature-read", "application signature cannot be inspected", Path);
				}
				if (!SidecarError && fs::exists(Sidecar))
				{
					FArtifactData Data;
					std::string Failure;
					if (!ReadArtifact({Path, "application", {}}, {Limits.MaxArchiveBytes, Limits.CheckCancelled}, Data, Failure,
					        nullptr, Services.Protection.get()))
					{
						Fail("application-signature", Failure, Path);
					}
					return Open(std::move(Data), Services, Error, Limits);
				}
				Impl->Archive = FArchive::Open(Path, Error, Limits);
				if (!Impl->Archive) return {};
				Impl->Actual = Impl->Archive->GetEntries();
			}
			return Initialize(std::move(Impl), Services, Error, Limits);
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"application", E.what(), Path, {}};
			return {};
		}
	}

	std::shared_ptr<const FApplication> FApplication::Open(std::vector<std::uint8_t> Bytes,
	    const FApplicationServices& Services, FDiagnostic& Error, const FLimits& Limits)
	{
		FArtifactData Data;
		Data.Bytes = std::move(Bytes);
		return Open(std::move(Data), Services, Error, Limits);
	}

	std::shared_ptr<const FApplication> FApplication::Open(FArtifactData Data,
	    const FApplicationServices& Services, FDiagnostic& Error, const FLimits& Limits)
	{
		auto State = std::make_shared<FImpl>();
		if (Data.SignatureRecords)
		{
			std::string Failure;
			if (!DecodeArtifact({"<application-container>", "application", {}}, {Limits.MaxArchiveBytes, Limits.CheckCancelled}, Data, Failure, Services.Protection.get()))
			{
				Error = {"application-signature", Failure, {}, {}};
				return {};
			}
		}
		State->ExternallyVerified = Services.Protection && Data.TrustIdentity == Services.Protection->GetIdentity();
		State->ContentOwner = std::move(Data.Owner);
		State->Archive = FArchive::Open(std::move(Data.Bytes), Error, Limits);
		if (!State->Archive) return {};
		State->Actual = State->Archive->GetEntries();
		return Initialize(std::move(State), Services, Error, Limits);
	}

	FApplication::~FApplication() = default;

	const std::string& FApplication::GetDescriptorJson() const noexcept
	{
		return Impl->Json;
	}

	const std::string& FApplication::GetIdentity() const noexcept
	{
		return Impl->Identity;
	}

	FByteView FApplication::GetContainerBytes() const noexcept
	{
		if (Impl->Archive)
		{
			if (const auto* Bytes = Impl->Archive->GetMemoryBytes()) return FByteView(*Bytes);
		}
		return {};
	}

	std::string FApplication::GetId() const
	{
		return Impl->Descriptor.at("id");
	}

	std::string FApplication::GetEntryPath() const
	{
		return Impl->Descriptor.at("entry").at("path");
	}

	bool FApplication::IsSourceEntry() const
	{
		return Impl->Descriptor.at("entry").at("kind") == "source";
	}

	const std::vector<FContentEntry>& FApplication::GetEntries() const noexcept
	{
		return Impl->Entries;
	}

	bool FApplication::Read(const std::string& Path, FArtifactData& Data, FDiagnostic& Error,
	    const FArtifactWork& Work, const std::string& Kind) const
	{
		if (!ReadStored(Path, Data, Error, Work)) return false;
		std::string Failure;
		if (!DecodeArtifact({Path, Kind, Impl->Descriptor.at("id").get<std::string>()}, Work,
		        Data, Failure, Impl->Services.Protection.get()))
		{
			Data = {};
			Error = {"artifact-decode", Failure, Path, {}};
			return false;
		}
		return true;
	}

	bool FApplication::ReadStored(const std::string& Path, FArtifactData& Data, FDiagnostic& Error,
	    const FArtifactWork& Work) const
	{
		Data = {};
		const auto Hash = Impl->Hashes.find(Path);
		if (Hash == Impl->Hashes.end())
		{
			Error = {"content-missing", "content is not declared by the application", Path, {}};
			return false;
		}
		if (!Impl->ReadRaw(Path, Data.Bytes, Error, Work)) return false;
		if (ContentSha256(Data.Bytes) != Hash->second)
		{
			Data = {};
			Error = {"content-integrity", "application content differs from its inventory", Path, {}};
			return false;
		}
		Data.Owner = shared_from_this();
		Data.ContentIdentity = Impl->Identity + ":" + Path + ":" + Hash->second;
		Data.TrustIdentity = Impl->TrustIdentity;
		const auto Signature = Impl->Hashes.find(Path + ".sig.json");
		if (Signature != Impl->Hashes.end())
		{
			std::vector<std::uint8_t> Records;
			if (!Impl->ReadRaw(Signature->first, Records, Error, {Impl->Limits.MaxMetadataBytes, Work.CheckCancelled}) || ContentSha256(Records) != Signature->second)
			{
				Data = {};
				Error = {"signature-integrity", "contained artifact signature differs from its inventory", Signature->first, {}};
				return false;
			}
			Data.SignatureRecords.emplace(Records.begin(), Records.end());
		}
		Error = {};
		return true;
	}

	bool FApplication::Export(std::vector<FContentFile>& Files, FDiagnostic& Error) const
	{
		Files.clear();
		for (const auto& Entry : Impl->Entries)
		{
			FArtifactData Data;
			if (!ReadStored(Entry.Path, Data, Error, {Impl->Limits.MaxFileBytes, Impl->Limits.CheckCancelled}))
			{
				Files.clear();
				return false;
			}
			Files.push_back({Entry.Path, std::move(Data.Bytes), Entry.Executable});
		}
		Files.push_back({InventoryPath, Impl->IndexBytes, false});
		if (!Impl->SignatureBytes.empty())
		{
			Files.push_back({SignaturePath, Impl->SignatureBytes, false});
		}
		std::sort(Files.begin(), Files.end(), [](const auto& A, const auto& B)
		{
			return A.Path < B.Path;
		});
		return true;
	}

	bool FApplication::Materialize(const std::string& Prefix, std::string& Root,
	    std::shared_ptr<const void>& Lease, FDiagnostic& Error) const
	{
		Root.clear();
		Lease.reset();
		fs::path Staging, Retired;
		bool StageOwned = false;
		try
		{
			Detail::Path(Prefix);
			if (Impl->Services.CacheRoot.empty())
			{
				Fail("cache-root", "materialization requires an explicit writable cache root");
			}
			const auto Cache = PathUtil::FromUtf8(Impl->Services.CacheRoot);
			Physical(Cache, true);
			const auto Object = TreePath(Cache / Impl->Identity / ContentSha256(Prefix));
			fs::create_directories(Object);
			Physical(Object);
			auto Guard = std::make_shared<FCacheLease>(Object / ".lease");
			const auto Content = Object / "content", Receipt = Object / ".application-cache.json";
			const J Identity{{"format", "veroscript-application-cache"}, {"formatVersion", "0.1.1"}, {"application", Impl->Identity}, {"root", Prefix}};
			std::vector<const FContentEntry*> Selected;
			std::set<std::string> Expected;
			for (const auto& E : Impl->Entries)
			{
				if (E.Path.rfind(Prefix + "/", 0) == 0)
				{
					Selected.push_back(&E);
					Expected.insert(E.Path.substr(Prefix.size() + 1));
				}
			}
			if (Selected.empty())
			{
				Fail("materialize-root", "requested content subtree is empty or absent", Prefix);
			}
			const auto Verify = [&]()
			{
				if (!fs::is_directory(Content)) return false;
				Physical(Content);
				for (const auto* E : Selected)
				{
					Check(Impl->Limits);
					const auto File = Content / PathUtil::FromUtf8(E->Path.substr(Prefix.size() + 1));
					Physical(File, true);
					std::string Hash;
					FDiagnostic D;
					if (!fs::is_regular_file(File) || !FileSha256(PathUtil::ToUtf8(File), Hash, D, Impl->Limits) || Hash != Impl->Hashes.at(E->Path)) return false;
					#if !defined(_WIN32)
					if (((fs::status(File).permissions() & fs::perms::owner_exec) != fs::perms::none) != E->Executable) return false;
					#endif
				}
				for (const auto& File : fs::recursive_directory_iterator(Content))
				{
					Check(Impl->Limits);
					Physical(File.path());
					if (!File.is_directory() && (!File.is_regular_file() || !Expected.count(PathUtil::ToUtf8(File.path().lexically_relative(Content))))) return false;
				}
				return true;
			};
			const auto LockStarted = std::chrono::steady_clock::now();
			const auto VerifyOwner = [&]()
			{
				if (!fs::is_regular_file(Receipt)) return false;
				auto Limits = Impl->Limits;
				Limits.MaxFileBytes = Limits.MaxMetadataBytes;
				if (Parse(Text(ReadFile(Receipt, Limits)), Limits) != Identity)
				{
					Fail("cache-owner", "cache ownership differs from the application");
				}
				return true;
			};
			for (;;)
			{
				Check(Impl->Limits);
				if (std::chrono::steady_clock::now() - LockStarted > std::chrono::seconds(30))
				{
					Fail("cache-in-use", "application cache is busy");
				}
				Guard->LockShared(Impl->Limits);
				if (VerifyOwner() && Verify())
				{
					break;
				}
				Guard->Unlock();
				if (!Guard->TryLock(true))
				{
					Check(Impl->Limits);
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				if (!VerifyOwner())
				{
					for (const auto& File : fs::directory_iterator(Object))
					{
						if (File.path().filename() != ".lease")
						{
							Fail("cache-owner", "refusing to replace unowned cache content");
						}
					}
					WriteFile(Receipt, Bytes(Identity.dump()));
				}
				if (Verify())
				{
					Guard->Unlock();
					continue;
				}
				static std::atomic<std::uint64_t> Sequence{0};
				const auto Suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++Sequence);
				Staging = Object / (".stage-" + Suffix);
				if (!fs::create_directory(Staging))
				{
					Fail("cache-stage", "cannot create an exclusive application staging directory");
				}
				StageOwned = true;
				#if !defined(_WIN32)
				fs::permissions(Staging, fs::perms::owner_all);
				#endif
				FContentTreeWriter Writer(Staging);
				for (const auto* Entry : Selected)
				{
					FArtifactData Data;
					if (!ReadStored(Entry->Path, Data, Error, {Impl->Limits.MaxFileBytes, Impl->Limits.CheckCancelled}))
					{
						Raise(Error);
					}
					const auto Relative = Entry->Path.substr(Prefix.size() + 1);
					Writer.Write(Relative, Data.Bytes);
					#if !defined(_WIN32)
					const auto File = Staging / PathUtil::FromUtf8(Relative);
					if (Entry->Executable)
					{
						fs::permissions(File, fs::perms::owner_exec, fs::perm_options::add);
					}
					#endif
				}
				if (fs::exists(Content))
				{
					Physical(Content);
					Retired = Object / (".retired-" + Suffix);
					fs::rename(Content, Retired);
				}
				try
				{
					fs::rename(Staging, Content);
					Staging.clear();
					StageOwned = false;
				}
				catch (...)
				{
					if (!Retired.empty() && !fs::exists(Content))
					{
						fs::rename(Retired, Content);
					}
					throw;
				}
				if (!Retired.empty())
				{
					Physical(Retired);
					fs::remove_all(Retired);
					Retired.clear();
				}
				Guard->Unlock();
			}
			Root = PathUtil::ToUtf8(Content);
			#if defined(_WIN32)
			// This output is a native filesystem path. Win32 device prefixes
			// cannot be consumed with the generic forward-slash spelling.
			std::replace(Root.begin(), Root.end(), '/', '\\');
			#endif
			Lease = Guard;
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
		}
		catch (const std::exception& E)
		{
			Error = {"materialization", E.what(), Prefix, {}};
		}
		if (StageOwned && !Staging.empty())
		{
			try
			{
				Physical(Staging);
				for (const auto& E : fs::recursive_directory_iterator(Staging))
				{
					Physical(E.path());
				}
				fs::remove_all(Staging);
			}
			catch (...)
			{
			}
		}
		return false;
	}

	bool FApplication::Prepare(FPreparedApplication& Result, FDiagnostic& Error) const
	{
		Result = {};
		FPreparedApplication Out;
		try
		{
			Check(Impl->Limits);
			const auto& D = Impl->Descriptor;
			const auto& Services = Impl->Services;
			const bool Source = D.at("entry").at("kind") == "source";
			const auto EntryPath = D.at("entry").at("path").get<std::string>();
			FArtifactData Data;
			if (!Read(EntryPath, Data, Error, {Impl->Limits.MaxFileBytes, Impl->Limits.CheckCancelled}, Source ? "source" : "application-image"))
			{
				return false;
			}
			if (Source)
			{
				Out.Source = Text(Data.Bytes);
				const auto C = D.value("compile", J::object());
				Out.Defines = C.value("defines", std::vector<std::string>{});
				Out.AllowCircularImports = C.value("allowCircularImports", true);
				Out.AllowProjectWideAccess = C.value("allowProjectWideAccess", false);
				for (const auto& M : C.value("predefinedMacros", J::array()))
				{
					Out.PredefinedMacros.push_back({M.at("name"), M.at("parameters").get<std::vector<std::string>>(), M.at("variadic"), M.at("body"), M.at("sourceName")});
				}
			}
			else
			{
				Out.Package = std::make_unique<FPackageData>();
				if (!ReadPackage(FByteView(Data.Bytes), *Out.Package, Error.Message) ||
				    Out.Package->Kind != EPackageKind::Application ||
				    Out.Package->PackageName != D.at("entry").at("packageName").get_ref<const std::string&>() ||
				    Out.Package->PackageVersion != Count(D.at("entry").at("packageVersion"), "entry.packageVersion"))
				{
					Fail("entry-image", Error.Message.empty() ? "application image identity or kind differs" : Error.Message);
				}
			}
			auto Base = Services.Environment;
			std::string DeploymentRoot;
			std::shared_ptr<const void> DeploymentLease;
			if (D.at("environment").at("mode") == "bundled")
			{
				std::string Root;
				if (!Materialize(D.at("environment").at("path").get<std::string>(), Root, DeploymentLease, Error)) return false;
				DeploymentRoot = Root;
				std::string Failure;
				if (!Services.LoadEnvironment || !Services.LoadEnvironment(Root, Source, D.at("services").dump(), Base, Failure))
				{
					Fail("environment-service", Failure.empty() ? "bundled environment requires a runtime environment adapter" : Failure);
				}
			}
			else if (Services.LoadExternalEnvironment)
			{
				std::string Failure;
				if (!Services.LoadExternalEnvironment(Source, D.at("services").dump(), Base, Failure))
				{
					Fail("external-environment", Failure);
				}
			}
			if (D.at("environment").at("mode") == "external" && !D.at("environment").value("requirements", J::array()).empty() && !Base)
			{
				Fail("environment-required", "application requires an explicit external environment");
			}
			const auto Required = D.at("environment").value("requirements", J::array());
			if (!Required.empty())
			{
				std::string Failure;
				if (!Services.ValidateRequirements || !Services.ValidateRequirements(Required.dump(), Base.get(), Failure))
				{
					Fail("environment-requirements", Failure.empty() ? "external requirements need a matching environment validator" : Failure);
				}
			}
			FModuleEnvironmentDesc Environment;
			if (Base)
			{
				Environment = Base->GetDescription();
			}
			const auto Owner = "application." + D.at("id").get<std::string>();
			if (std::any_of(Environment.Scopes.begin(), Environment.Scopes.end(), [&](const auto& S)
			{
				return S.Id == Owner;
			}))
			{
				Fail("application-owner", "application owner conflicts with the host environment");
			}
			FModuleScope Scope;
			Scope.Id = Owner;
			if (!Environment.EntryOwnerId.empty())
			{
				const auto Previous = std::find_if(Environment.Scopes.begin(), Environment.Scopes.end(),
				    [&](const auto& S)
				{
					return S.Id == Environment.EntryOwnerId;
				});
				if (Previous != Environment.Scopes.end())
				{
					Scope.DirectDependencies = Previous->DirectDependencies;
				}
			}
			Environment.Scopes.push_back(std::move(Scope));
			Environment.EntryOwnerId = Owner;
			Environment.Owners.push_back(shared_from_this());
			if (DeploymentLease)
			{
				Environment.Owners.push_back(DeploymentLease);
			}
			const auto Prefix = "veroscript-module:/vsapp/" + Impl->Identity + "/";
			if (Source)
			{
				for (const auto& I : D.value("compile", J::object()).value("implicitImports", J::array()))
				{
					Out.ImplicitImports.push_back(Prefix + I.get<std::string>());
				}
			}
			auto Reader = std::make_shared<FApplicationReader>();
			Reader->App = shared_from_this();
			Reader->Prefix = Prefix;
			Reader->Fallback = Environment.ArtifactReader;
			if (!DeploymentRoot.empty())
			{
				Reader->PhysicalRoots.emplace_back(DeploymentRoot, D.at("environment").at("path").get<std::string>());
			}
			Environment.ArtifactReader = Reader;
			Environment.ArtifactDecoder = Services.Protection ? Services.Protection : Environment.ArtifactDecoder;
			if (Out.Package)
			{
				std::set<std::string> Origins{Out.Package->Module->EntryModulePath};
				for (const auto& Function : Out.Package->Module->Functions)
				{
					Origins.insert(Function.ModulePath);
				}
				for (const auto& Origin : Origins)
				{
					if (Origin.empty() || Origin.front() == '<' || (Base && !Base->GetSourceOwner(Origin).empty()) ||
					    !PathUtil::FromUtf8(Origin).is_absolute())
					{
						continue;
					}
					FResolvedModule M;
					M.Name = "application-origin/" + ContentSha256(Origin) + ".vs";
					M.Id = M.Name;
					M.Path = Origin;
					M.OwnerId = Owner;
					M.ContentIdentity = Impl->Identity;
					M.Source = "";
					Environment.Modules.push_back(std::move(M));
				}
				FResolvedModule M;
				M.Name = "application-entry.bin";
				M.Id = Prefix + EntryPath;
				M.Path = M.Id;
				M.OwnerId = Owner;
				M.ContentIdentity = Impl->Identity;
				M.Source = "";
				Environment.Modules.push_back(std::move(M));
			}
			for (const auto& E : Impl->Entries)
			{
				const auto Extension = Fold(PathUtil::ToUtf8(PathUtil::FromUtf8(E.Path).extension()));
				const bool Library = Extension == ".vslib";
				if (!(Source && E.Path == EntryPath) &&
				    ((Extension != ".vs" && !Library) || E.Path.rfind("program/", 0) != 0))
				{
					continue;
				}
				FResolvedModule Module;
				Module.Name = E.Path;
				Module.Path = Prefix + E.Path;
				Module.Id = Module.Path;
				Module.OwnerId = Owner;
				Module.ContentIdentity = Impl->Hashes.at(E.Path);
				if (!Library)
				{
					FArtifactData ModuleSource;
					if (!Read(E.Path, ModuleSource, Error, {64ull << 20, Impl->Limits.CheckCancelled}, "source"))
					{
						return false;
					}
					Module.ContentIdentity = ModuleSource.ContentIdentity;
					Module.Source = Text(ModuleSource.Bytes);
				}
				Environment.Modules.push_back(std::move(Module));
			}
			if (D.contains("modules"))
			{
				for (auto I = D.at("modules").begin(); I != D.at("modules").end(); ++I)
				{
					const auto Path = I.value().get<std::string>();
					if (!Impl->Hashes.count(Path))
					{
						Fail("module-missing", "application module content is missing", Path);
					}
					const auto Existing = std::find_if(Environment.Modules.begin(), Environment.Modules.end(), [&](const auto& M)
					{
						return M.Path == Prefix + Path;
					});
					FResolvedModule Module;
					if (Existing != Environment.Modules.end())
					{
						Module = *Existing;
					}
					else
					{
						Module.Path = Prefix + Path;
						Module.Id = Module.Path;
						Module.OwnerId = Owner;
						Module.ContentIdentity = Impl->Hashes.at(Path);
						if (Fold(PathUtil::ToUtf8(PathUtil::FromUtf8(Path).extension())) != ".vslib")
						{
							FArtifactData ModuleSource;
							if (!Read(Path, ModuleSource, Error, {64ull << 20, Impl->Limits.CheckCancelled}, "source"))
							{
								return false;
							}
							Module.Source = Text(ModuleSource.Bytes);
							Module.ContentIdentity = ModuleSource.ContentIdentity;
						}
					}
					if (Existing != Environment.Modules.end() && Existing->Name == I.key())
					{
						continue;
					}
					Module.Name = I.key();
					Environment.Modules.push_back(std::move(Module));
				}
			}
			for (const auto& Root : Services.StandardLibraryPaths)
			{
				const auto Canonical = PathUtil::ToUtf8(fs::weakly_canonical(PathUtil::FromUtf8(Root)));
				if (std::any_of(Environment.StdlibSearchPaths.begin(), Environment.StdlibSearchPaths.end(),
				        [&](const auto& Existing)
				{
					return PathUtil::Equal(Existing, Canonical);
				}))
				{
					continue;
				}
				const auto ScopeId = "application.stdlib." + ContentSha256(Canonical);
				Environment.Scopes.push_back({ScopeId, {}, true});
				Environment.SourceRoots.push_back({Canonical, ScopeId});
				Environment.StdlibSearchPaths.push_back(Canonical);
			}
			for (const auto& R : D.at("resources"))
			{
				if (R.value("ownerId", Owner) != Owner)
				{
					Fail("resource-owner", "application resource cannot claim another owner");
				}
				auto Provider = std::make_shared<FApplicationResources>();
				Provider->App = shared_from_this();
				Provider->Root = R.at("path").get<std::string>();
				Provider->Group = R.value("group", "default");
				FResourceProviderBinding Binding;
				Binding.Id = R.at("id").get<std::string>();
				Binding.OwnerId = Owner;
				Binding.ContractIdentity = Impl->Identity + ":" + Binding.Id;
				Binding.Owner = shared_from_this();
				Binding.Provider = std::move(Provider);
				Binding.ModuleIds = R.value("moduleIds", std::vector<std::string>{Prefix + EntryPath, Owner});
				Environment.Resources.push_back(std::move(Binding));
			}
			Environment.bStrictSources = true;
			Environment.bAllowAmbientSearch = false;
			Out.Environment = FModuleEnvironment::Create(std::move(Environment), Error.Message);
			if (!Out.Environment)
			{
				Error.Code = "application-environment";
				return false;
			}
			Out.Owner = shared_from_this();
			Out.LogicalPath = Prefix + EntryPath;
			if (Out.Package && Services.PreparePackage)
			{
				std::string Failure;
				if (!Services.PreparePackage(*Out.Package, Failure))
				{
					Fail("application-package", Failure);
				}
			}
			Result = std::move(Out);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Out = {};
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Out = {};
			Error = {"application-prepare", E.what(), {}, {}};
			return false;
		}
	}

	bool FApplication::ReadEntryPackage(FPackageData& Package, FDiagnostic& Error) const
	{
		Package = {};
		if (IsSourceEntry())
		{
			Error = {"entry-source", "application entry is source", GetEntryPath(), {}};
			return false;
		}
		FArtifactData Data;
		if (!Read(GetEntryPath(), Data, Error, {Impl->Limits.MaxFileBytes, Impl->Limits.CheckCancelled}, "application-image"))
		{
			return false;
		}
		if (!ReadPackage(FByteView(Data.Bytes), Package, Error.Message))
		{
			Error.Code = "entry-image";
			return false;
		}
		const auto& Entry = Impl->Descriptor.at("entry");
		if (Package.Kind != EPackageKind::Application || Package.PackageName != Entry.at("packageName").get_ref<const std::string&>() ||
		    Package.PackageVersion != Count(Entry.at("packageVersion"), "entry.packageVersion"))
		{
			Package = {};
			Error = {"entry-image", "entry image kind or identity differs from application", GetEntryPath(), {}};
			return false;
		}
		Error = {};
		return true;
	}

	bool FApplication::ResolveRuntime(const std::string& Explicit, std::string& Executable,
	    std::shared_ptr<const void>& Lease, FDiagnostic& Error, std::vector<std::string>* StandardLibraryPaths) const
	{
		Executable.clear();
		Lease.reset();
		if (StandardLibraryPaths)
		{
			StandardLibraryPaths->clear();
		}
		try
		{
			std::string Root = Explicit;
			if (Root.empty())
			{
				const auto& R = Impl->Descriptor.at("runtime");
				if (R.at("mode") == "external")
				{
					Error = {};
					return true;
				}
				if (!Materialize(R.at("path").get<std::string>(), Root, Lease, Error)) return false;
			}
			Physical(PathUtil::FromUtf8(Root));
			if (!fs::is_directory(PathUtil::FromUtf8(Root)))
			{
				Fail("runtime-component", "Runtime selection must name a component directory");
			}
			auto MetadataLimits = Impl->Limits;
			MetadataLimits.MaxFileBytes = MetadataLimits.MaxMetadataBytes;
			const auto M = Parse(Text(ReadFile(PathUtil::FromUtf8(Root) / "veroscript-runtime.json", MetadataLimits)), Impl->Limits);
			Keys(M, {"format", "formatVersion", "applicationProtocol", "target", "executable", "stdlib", "files", "extensions"}, "runtime");
			if (M.at("format") != "veroscript-runtime" || M.at("formatVersion") != "0.1.1" || M.at("applicationProtocol") != "0.1.1")
			{
				Fail("runtime-component", "unsupported Runtime component protocol");
			}
			std::string Failure;
			if (!Impl->Services.ValidateTarget(M.at("target").dump(), Failure))
			{
				Fail("runtime-target", Failure);
			}
			const auto Rel = M.at("executable").get<std::string>();
			Detail::Path(Rel);
			const auto File = PathUtil::FromUtf8(Root) / PathUtil::FromUtf8(Rel);
			Physical(File);
			if (!fs::is_regular_file(File))
			{
				Fail("runtime-entry", "Runtime component executable is missing");
			}
			bool EntryPresent = false;
			std::set<std::string> Names;
			if (!M.at("files").is_array() || M.at("files").size() > Impl->Limits.MaxFiles)
			{
				Fail("runtime-inventory", "invalid Runtime inventory");
			}
			for (const auto& Record : M.at("files"))
			{
				Keys(Record, {"path", "bytes", "sha256", "mode"}, "runtime.file");
				const auto Name = Record.at("path").get<std::string>();
				Detail::Path(Name);
				if (Name == "veroscript-runtime.json" || !Names.insert(Fold(Name)).second)
				{
					Fail("runtime-inventory", "duplicate Runtime content");
				}
				const auto P = PathUtil::FromUtf8(Root) / PathUtil::FromUtf8(Name);
				std::string Digest;
				if (Count(Record.at("bytes"), "runtime.file.bytes") != fs::file_size(P) ||
				    !FileSha256(PathUtil::ToUtf8(P), Digest, Error, Impl->Limits) || Digest != Record.at("sha256").get_ref<const std::string&>() ||
				    (Record.at("mode") != "0644" && Record.at("mode") != "0755"))
				{
					Fail("runtime-integrity", "Runtime file differs from its component inventory", Name);
				}
				EntryPresent = EntryPresent || Name == Rel;
			}
			if (!EntryPresent)
			{
				Fail("runtime-inventory", "Runtime executable is not listed");
			}
			for (const auto& S : M.at("stdlib"))
			{
				const auto Name = S.get<std::string>();
				Detail::Path(Name);
				const auto P = PathUtil::FromUtf8(Root) / PathUtil::FromUtf8(Name);
				Physical(P);
				if (!fs::is_directory(P))
				{
					Fail("runtime-stdlib", "Runtime standard library directory is missing");
				}
				if (StandardLibraryPaths)
				{
					StandardLibraryPaths->push_back(PathUtil::ToUtf8(P));
				}
			}
			Executable = PathUtil::ToUtf8(File);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Error = {"runtime-component", E.what(), {}, {}};
			return false;
		}
	}

	std::shared_ptr<const FApplication> FApplication::Initialize(std::shared_ptr<FImpl> Impl,
	    const FApplicationServices& Services, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			Impl->Services = Services;
			Impl->Limits = Limits;
			const auto& Actual = Impl->Actual;
			const auto Defaults = CreateApplicationServices();
			if (!Impl->Services.ValidateTarget)
			{
				Impl->Services.ValidateTarget = Defaults.ValidateTarget;
			}
			if (!Impl->Services.ValidateRequirements)
			{
				Impl->Services.ValidateRequirements = Defaults.ValidateRequirements;
			}
			std::vector<std::uint8_t> DescriptorBytes, Index;
			const FArtifactWork MetadataWork{Limits.MaxMetadataBytes, Limits.CheckCancelled};
			if (!Impl->ReadRaw(DescriptorPath, DescriptorBytes, Error, MetadataWork) ||
			    !Impl->ReadRaw(InventoryPath, Index, Error, MetadataWork))
			{
				return {};
			}
			Impl->Descriptor = Descriptor(Text(DescriptorBytes), Limits);
			Impl->Json = Impl->Descriptor.dump();
			Impl->Inventory = Parse(Text(Index), Limits);
			Impl->IndexBytes = Index;
			Keys(Impl->Inventory, {"format", "formatVersion", "files"}, "inventory");
			if (Impl->Inventory.value("format", "") != "veroscript-application-files" ||
			    Impl->Inventory.value("formatVersion", "") != "0.1.1" || !Impl->Inventory.at("files").is_array() ||
			    Impl->Inventory.at("files").size() > Limits.MaxFiles)
			{
				Fail("inventory-format", "invalid application inventory");
			}
			std::set<std::string> Names;
			std::uint64_t Total = 0;
			std::string Previous;
			for (const auto& Record : Impl->Inventory.at("files"))
			{
				Keys(Record, {"path", "bytes", "sha256", "mode"}, "inventory.file");
				const auto Name = Record.at("path").get<std::string>();
				Detail::Path(Name);
				const auto Size = Count(Record.at("bytes"), "inventory.file.bytes");
				const auto Mode = Record.at("mode").get<std::string>();
				const auto Hash = Record.at("sha256").get<std::string>();
				if (Name == InventoryPath || Name == SignaturePath || !Names.insert(Fold(Name)).second ||
				    (!Previous.empty() && Previous >= Name) || !IsDigest(Hash) || (Mode != "0644" && Mode != "0755") ||
				    Size > Limits.MaxFileBytes || Size > Limits.MaxExpandedBytes - Total)
				{
					Fail("inventory-entry", "invalid, duplicate or oversized application inventory entry", Name);
				}
				Previous = Name;
				Total += Size;
				Impl->Entries.push_back({Name, Size, Mode == "0755"});
				Impl->Hashes.emplace(Name, Hash);
				Impl->Sizes.emplace(Name, Size);
			}
			if (!Impl->Hashes.count(DescriptorPath) || !Impl->Hashes.count(Impl->Descriptor.at("entry").at("path").get<std::string>()) ||
			    Impl->Hashes.at(DescriptorPath) != ContentSha256(DescriptorBytes))
			{
				Fail("application-integrity", "application descriptor or entry differs from its inventory");
			}
			std::string Signatures;
			for (const auto& Entry : Actual)
			{
				if (Entry.Path == SignaturePath)
				{
					std::vector<std::uint8_t> Data;
					if (!Impl->ReadRaw(SignaturePath, Data, Error, MetadataWork)) return {};
					Signatures = Text(Data);
					Impl->SignatureBytes = Data;
				}
				else if (Entry.Path != InventoryPath)
				{
					const auto I = Impl->Hashes.find(Entry.Path);
					const auto Expected = Impl->Sizes.find(Entry.Path);
					if (I == Impl->Hashes.end() || Expected == Impl->Sizes.end() || Expected->second != Entry.Bytes)
					{
						Fail("application-content", "unlisted content or size differs", Entry.Path);
					}
				}
			}
			if (Actual.size() != Impl->Entries.size() + 1 + (Signatures.empty() ? 0 : 1))
			{
				Fail("application-content", "application content is missing or duplicated");
			}
			if (Services.Protection)
			{
				if ((!Signatures.empty() || !Impl->ExternallyVerified) && !Services.Protection->Verify(Index, "applicationInventory", Signatures, Error, Limits)) return {};
				if (!Signatures.empty() || Impl->ExternallyVerified)
				{
					Impl->TrustIdentity = Services.Protection->GetIdentity();
				}
			}
			else if (!Signatures.empty())
			{
				Fail("signature-service", "signed application requires a trusted verification service");
			}
			const auto& Requirements = Impl->Descriptor.at("requirements");
			if (Requirements.contains("coreBuild") && Requirements.at("coreBuild") != VEROSCRIPT_CORE_IMPLEMENTATION_ID)
			{
				Fail("application-core", "application requires a different Core build");
			}
			for (const auto& Capability : Requirements.at("capabilities"))
			{
				if (!Services.Capabilities.count(Capability.get<std::string>()))
				{
					Fail("application-capability", "required capability is unavailable", Capability.get<std::string>());
				}
			}
			const auto& Platform = Impl->Descriptor.at("platform");
			if (Platform != "portable")
			{
				std::string Failure;
				if (!Impl->Services.ValidateTarget(Platform.dump(), Failure))
				{
					Fail("application-target", Failure.empty() ? "target application requires a matching target validator" : Failure);
				}
			}
			Impl->Identity = Impl->Archive ? Impl->Archive->GetIdentity() : ContentSha256(Index);
			Error = {};
			return std::shared_ptr<const FApplication>(new FApplication(std::move(Impl)));
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"application", E.what(), {}, {}};
			return {};
		}
	}

	FCompileResult FPreparedApplication::CompileSource(const FCompileOptions& HostOptions) const
	{
		if (Package)
		{
			FCompileResult Result;
			Result.Error.Message = "a compiled application has no source entry";
			return Result;
		}
		auto Options = HostOptions;
		Options.ModuleEnvironment = Environment;
		if (!Options.Bindings && Environment && Environment->GetDescription().Bindings)
		{
			Options.Bindings = Environment->GetDescription().Bindings.get();
		}
		for (const auto& D : Defines)
		{
			Options.Defines.emplace_back(D);
		}
		Options.PredefinedMacros.insert(Options.PredefinedMacros.end(), PredefinedMacros.begin(), PredefinedMacros.end());
		for (const auto& I : ImplicitImports)
		{
			Options.ImplicitImportPaths.emplace_back(I);
		}
		Options.bAllowCircularImports = AllowCircularImports;
		Options.bAllowProjectWideAccess = AllowProjectWideAccess;
		return VeroScriptCompile::CompileSource(Source, LogicalPath, Options);
	}

	FApplication::FApplication(std::shared_ptr<FImpl> State) : Impl(std::move(State))
	{
	}

	bool ValidateApplicationDescriptor(std::string_view Text, std::string& Canonical,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		Canonical.clear();
		try
		{
			Canonical = Descriptor(Text, Limits).dump();
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Error = {"application-descriptor", E.what(), {}, {}};
			return false;
		}
	}
	bool AssembleApplication(std::string_view Json, const std::vector<FContentFile>& Input,
	    std::vector<FContentFile>& Out, FDiagnostic& Error, const FLimits& Limits,
	    const FProtectionOptions& Options, const FProtection* Protection)
	{
		Out.clear();
		try
		{
			const auto D = Descriptor(Json, Limits);
			const auto Entry = D.at("entry").at("path").get<std::string>();
			std::map<std::string, FContentFile> Files;
			std::set<std::string> Names;
			std::uint64_t Total = 0;
			for (auto F : Input)
			{
				Check(Limits);
				Detail::Path(F.Path);
				if (Fold(F.Path) == DescriptorPath || Fold(F.Path) == InventoryPath || Fold(F.Path) == SignaturePath || Fold(F.Path) == ".vero-lease")
				{
					Fail("reserved-content", "application control files belong to the assembler", F.Path);
				}
				if (!Names.insert(Fold(F.Path)).second || Files.size() >= Limits.MaxFiles ||
				    F.Bytes.size() > Limits.MaxFileBytes || F.Bytes.size() > Limits.MaxExpandedBytes - Total)
				{
					Fail("content-limit", "duplicate content or content budget exceeded", F.Path);
				}
				Total += F.Bytes.size();
				Files.emplace(F.Path, std::move(F));
			}
			if (!Files.count(Entry))
			{
				Fail("entry-missing", "application entry is missing from content", Entry);
			}
			if (!Options.EncryptionKey.empty())
			{
				if (!Protection || D.at("entry").at("kind") != "compiled-image")
				{
					Fail("encryption-kind", "entry encryption requires a compiled image and protection service");
				}
				if (Files.at(Entry).Bytes.size() >= 4 && std::memcmp(Files.at(Entry).Bytes.data(), "VSAE", 4) == 0)
				{
					Fail("nested-encryption", "decrypt an existing entry before applying a new encryption policy");
				}
				std::vector<std::uint8_t> Protected;
				if (!Protection->Encrypt(Files.at(Entry).Bytes, "application-image", Options.EncryptionKey,
				        Options.EncryptionVersion, Protected, Error, Limits))
				{
					return false;
				}
				Files.at(Entry).Bytes = std::move(Protected);
			}
			Files.emplace(DescriptorPath, FContentFile{DescriptorPath, Bytes(D.dump()), false});
			Files.emplace(".vero-lease", FContentFile{".vero-lease", {}, false});
			J Inventory{{"format", "veroscript-application-files"}, {"formatVersion", "0.1.1"}, {"files", J::array()}};
			for (const auto& F : Files)
			{
				Inventory["files"].push_back({{"path", F.first}, {"bytes", std::to_string(F.second.Bytes.size())},
				    {"sha256", ContentSha256(F.second.Bytes)}, {"mode", F.second.Executable ? "0755" : "0644"}});
			}
			const auto Index = Bytes(Inventory.dump());
			if (Index.size() > Limits.MaxMetadataBytes)
			{
				Fail("inventory-limit", "generated application inventory exceeds metadata budget");
			}
			for (auto& F : Files)
			{
				Out.push_back(std::move(F.second));
			}
			Out.push_back({InventoryPath, Index, false});
			if (!Options.SigningKey.empty())
			{
				if (!Protection || Options.SignatureScope != "content")
				{
					Fail("signature-scope", "content assembly requires a content signing service");
				}
				std::string Records;
				if (!Protection->Sign(Index, "applicationInventory", Options.SigningKey, Options.SigningVersion,
				        Records, Error, Limits))
				{
					Out.clear();
					return false;
				}
				Out.push_back({SignaturePath, Bytes(Records), false});
			}
			std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B)
			{
				return A.Path < B.Path;
			});
			if (Out.size() > Limits.MaxFiles)
			{
				Fail("file-count", "application control files exceed file count budget");
			}
			std::set<std::string> FinalNames;
			Total = 0;
			for (const auto& F : Out)
			{
				if (F.Bytes.size() > Limits.MaxFileBytes || F.Bytes.size() > Limits.MaxExpandedBytes - Total)
				{
					Fail("content-limit", "sealed application exceeds expanded byte budget");
				}
				Total += F.Bytes.size();
				FinalNames.insert(Fold(F.Path));
			}
			for (const auto& Name : FinalNames)
			{
				for (auto P = PathUtil::FromUtf8(Name).parent_path(); !P.empty(); P = P.parent_path())
				{
					if (FinalNames.count(PathUtil::ToUtf8(P)))
					{
						Fail("prefix-path", "application file overlaps a content directory", Name);
					}
				}
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Out.clear();
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Out.clear();
			Error = {"application-assembly", E.what(), {}, {}};
			return false;
		}
	}
	namespace
	{
		bool AssembleCompiledApplication(std::vector<std::uint8_t> Image, const std::string& Name,
		    std::uint64_t PackageVersion, const std::string& Version, const std::vector<FContentFile>& Resources,
		    std::vector<FContentFile>& Out, FDiagnostic& Error, const FLimits& Limits,
		    const FProtectionOptions& Options, const FProtection* Protection)
		{
			const J D{{"format", "veroscript-application"}, {"formatVersion", "0.1.1"},
			    {"id", Name}, {"version", Version},
			    {"entry", {{"kind", "compiled-image"}, {"path", "program/entry.bin"}, {"packageName", Name}, {"packageVersion", std::to_string(PackageVersion)}}},
			    {"platform", "portable"}, {"requirements", {{"coreVersion", "0.1.1"}, {"capabilities", J::array()}}},
			    {"runtime", {{"mode", "external"}}}, {"environment", {{"mode", "external"}, {"requirements", J::array()}}},
			    {"services", J::object()}, {"resources", J::array()}, {"files", InventoryPath}, {"extensions", J::object()}};
			auto Files = Resources;
			Files.push_back({"program/entry.bin", std::move(Image), false});
			return AssembleApplication(D.dump(), Files, Out, Error, Limits, Options, Protection);
		}
	}
	bool BuildApplication(const FBytecodeModule& Module, const FPackageBuildOptions& Package,
	    const std::string& Version, const std::vector<FContentFile>& Resources,
	    std::vector<FContentFile>& Out, FDiagnostic& Error, const FLimits& Limits,
	    const FProtectionOptions& Options, const FProtection* Protection)
	{
		Out.clear();
		std::vector<std::uint8_t> Image;
		if (Package.Kind != EPackageKind::Application || !BuildPackage(Module, Package, Image, Error.Message))
		{
			Error.Code = "application-image";
			return false;
		}
		return AssembleCompiledApplication(std::move(Image), Package.PackageName, Package.PackageVersion,
		    Version, Resources, Out, Error, Limits, Options, Protection);
	}
	bool BuildApplication(std::vector<std::uint8_t> Image, const std::string& Version,
	    const std::vector<FContentFile>& Resources, std::vector<FContentFile>& Out,
	    FDiagnostic& Error, const FLimits& Limits, const FProtectionOptions& Options, const FProtection* Protection)
	{
		Out.clear();
		FPackageData Package;
		if (!ReadPackage(FByteView(Image), Package, Error.Message) || Package.Kind != EPackageKind::Application)
		{
			Error.Code = "application-image";
			if (Error.Message.empty())
			{
				Error.Message = "expected a Core application image";
			}
			return false;
		}
		return AssembleCompiledApplication(std::move(Image), Package.PackageName, Package.PackageVersion,
		    Version, Resources, Out, Error, Limits, Options, Protection);
	}
}
