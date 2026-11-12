// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include "miniz/miniz.h"
#include <algorithm>
#include <array>
#include <exception>
#include <fstream>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		// Logical ZIP names must not merge on a filesystem with broader case or
		// Unicode equivalence rules. Cache directory identities for linear work.
		struct FPathWriter
		{
			explicit FPathWriter(fs::path R) : Root(std::move(R))
			{
				NoLinks(Root, true);
				fs::create_directories(Root);
			}

			void Directory(const fs::path& Relative)
			{
				if (Relative.empty())
				{
					return;
				}
				auto Name = PathUtil::ToUtf8(Relative);
				if (Logical.count(Name))
				{
					return;
				}
				Directory(Relative.parent_path());
				auto P = Root / Relative;
				NoLinks(P, true);
				fs::create_directory(P);
				if (!fs::is_directory(P))
				{
					Fail("archive-prefix", "directory aliases a file", Name);
				}
				auto Id = FileIdentity(P);
				auto I = Physical.emplace(Id, Name);
				if (!I.second && I.first->second != Name)
				{
					Fail("archive-collision", "directory names alias on the staging filesystem", Name);
				}
				Logical.insert(Name);
			}

			void File(const fs::path& Relative)
			{
				Directory(Relative.parent_path());
				auto P = Root / Relative;
				NoLinks(P, true);
				if (fs::exists(P))
				{
					Fail("archive-collision", "file names alias on the staging filesystem",
					    PathUtil::ToUtf8(Relative));
				}
			}

			fs::path Root;

			std::map<std::string, std::string> Physical;

			std::set<std::string> Logical;
		};
		std::string Fold(std::string S)
		{
			std::transform(S.begin(), S.end(), S.begin(),
			    [](unsigned char C)
			{
				return char(std::tolower(C));
			});
			return S;
		}
		bool Glob(const std::string& Pattern, const std::string& Path, const FWork& Work)
		{
			if (Pattern.empty() || Pattern.size() > 4096)
			{
				Fail("file-pattern", "file patterns must contain 1 to 4096 bytes");
			}
			std::vector<unsigned char> Current(Path.size() + 1), Next(Current.size());
			Current[0] = 1;
			for (std::size_t I = 0; I < Pattern.size(); ++I)
			{
				if ((I & 7) == 0)
				{
					Work.Check();
				}
				std::fill(Next.begin(), Next.end(), 0);
				char C = Pattern[I];
				if (C == '*')
				{
					bool Double = I + 1 < Pattern.size() && Pattern[I + 1] == '*';
					if (Double)
					{
						++I;
					}
					if (Double && I + 1 < Pattern.size() && Pattern[I + 1] == '/')
					{
						++I;
						Next = Current;
						bool Prefix = false;
						for (std::size_t J = 1; J < Next.size(); ++J)
						{
							Prefix = Prefix || Current[J - 1];
							if (Path[J - 1] == '/' && Prefix)
							{
								Next[J] = 1;
							}
						}
					}
					else
					{
						Next[0] = Current[0];
						for (std::size_t J = 1; J < Next.size(); ++J)
						{
							Next[J] = Current[J] || ((Double || Path[J - 1] != '/') && Next[J - 1]);
						}
					}
				}
				else
				{
					for (std::size_t J = 1; J < Next.size(); ++J)
					{
						Next[J] = Current[J - 1] && (C == '?' ? Path[J - 1] != '/' : C == Path[J - 1]);
					}
				}
				Current.swap(Next);
			}
			return Current.back() != 0;
		}
		bool Selected(const std::string& P, const J& M, const FWork& W)
		{
			bool Include = !M.contains("files");
			if (M.contains("files"))
			{
				for (const auto& G : M["files"])
				{
					if (Glob(G, P, W))
					{
						Include = true;
						break;
					}
				}
			}
			if (M.contains("exclude"))
			{
				for (const auto& G : M["exclude"])
				{
					if (Glob(G, P, W))
					{
						return false;
					}
				}
			}
			return Include;
		}

		void Required(const fs::path& Root, const J& M, const J& Files, bool RequirePackageManifest)
		{
			std::set<std::string> Paths;
			for (const auto& F : Files)
			{
				Paths.insert(F["path"]);
			}
			const auto Need = [&](std::string P, bool Dir = false)
			{
				CheckPath(P, Dir);
				if (Dir && P.back() == '/')
				{
					P.pop_back();
				}
				if (!Paths.count(P))
				{
					Fail("entry-missing", "required package path is absent or excluded", P);
				}
				NoLinks(Root / U8Path(P));
			};
			if (RequirePackageManifest)
			{
				Need("veroscript-package.json");
			}
			for (const auto* K : {"licenseFile", "readme", "icon"})
			{
				if (M.contains(K))
				{
					Need(M[K]);
				}
			}
			if (M.contains("resources"))
			{
				for (const auto& R : M["resources"])
				{
					Need(R, true);
				}
			}
			if (M.contains("localization"))
			{
				Need(M["localization"]["root"], true);
			}
			const auto E = M.value("entries", J::object());
			if (E.contains("modules"))
			{
				for (const auto& P : E["modules"])
				{
					Need(P);
				}
			}
			for (const auto* K : {"native", "libraries", "compiler", "backends"})
			{
				for (const auto& P : E.value(K, J::array()))
				{
					if (P.contains("path"))
					{
						Need(P["path"]);
					}
					if (P.contains("catalog"))
					{
						Need(P["catalog"]);
					}
				}
			}
			const auto I = M.value("install", J::object());
			for (const auto& H : I.value("hooks", J::array()))
			{
				Need(H["path"]);
			}
			for (const auto& A : I.value("actions", J::array()))
			{
				Need(A["source"]);
			}
			for (const auto* K : {"readme", "license"})
			{
				if (I.value("messages", J::object()).contains(K))
				{
					Need(I["messages"][K]);
				}
			}
		}
		struct FReader
		{
			FReader(const fs::path& P, const FWork& W) : Stream(P, std::ios::binary), Work(W)
			{
				if (!Stream)
				{
					Fail("archive-read", "cannot open archive input");
				}
			}

			static std::size_t ReadAt(void* P, mz_uint64 Offset, void* Buffer, std::size_t N)
			{
				auto& S = *static_cast<FReader*>(P);
				try
				{
					S.Work.Check();
					S.Stream.clear();
					S.Stream.seekg(static_cast<std::streamoff>(Offset));
					S.Stream.read(static_cast<char*>(Buffer), static_cast<std::streamsize>(N));
					return static_cast<std::size_t>(S.Stream.gcount());
				}
				catch (...)
				{
					S.Error = std::current_exception();
					return 0;
				}
			}

			std::ifstream Stream;

			const FWork& Work;

			std::exception_ptr Error;
		};
		struct FWriter
		{
			FWriter(const fs::path& P, const FWork& W, std::uint64_t L)
			    : Stream(P, std::ios::binary), Work(W), Limit(L)
			{
				if (!Stream)
				{
					Fail("archive-write", "cannot open archive output");
				}
			}

			static std::size_t WriteAt(void* P, mz_uint64 Offset, const void* Buffer, std::size_t N)
			{
				auto& S = *static_cast<FWriter*>(P);
				try
				{
					S.Work.Check();
					if (Offset != S.ExpectedOffset || N > S.Limit - S.Written)
					{
						Fail("archive-limit", "archive output exceeds declared budget");
					}
					if (!S.Stream.write(static_cast<const char*>(Buffer), static_cast<std::streamsize>(N)))
					{
						Fail("archive-write", "failed writing archive output");
					}
					S.Written += N;
					S.ExpectedOffset += N;
					return N;
				}
				catch (...)
				{
					S.Error = std::current_exception();
					return 0;
				}
			}

			std::ofstream Stream;

			const FWork& Work;

			std::uint64_t Limit, Written = 0, ExpectedOffset = 0;

			std::exception_ptr Error;
		};
		struct FZip
		{
			~FZip()
			{
				mz_zip_end(&Zip);
			}

			mz_zip_archive Zip{};
		};
		// Read callback state after the ZIP operation, even if its reader recovered.
		void CheckZip(bool Ok, FZip& Z, const std::exception_ptr& E = {})
		{
			if (E)
			{
				std::rethrow_exception(E);
			}
			if (!Ok)
			{
				Fail("invalid-archive", mz_zip_get_error_string(mz_zip_get_last_error(&Z.Zip)));
			}
		}
		bool ExceedsExpansionRatio(std::uint64_t Expanded, std::uint64_t Compressed, std::uint64_t Maximum)
		{
			auto Denominator = std::max<std::uint64_t>(1, Compressed);
			auto Ratio = Expanded / Denominator;
			return Ratio > Maximum || (Ratio == Maximum && Expanded % Denominator);
		}
		fs::path StoreOvercompressedEntries(const fs::path& Archive, const fs::path& Root, const J& Files,
		    const FWork& W)
		{
			FReader Reader(Archive, W);
			FZip Source;
			Source.Zip.m_pRead = FReader::ReadAt;
			Source.Zip.m_pIO_opaque = &Reader;
			auto Initialized = mz_zip_reader_init(&Source.Zip, fs::file_size(Archive), 0) != 0;
			CheckZip(Initialized, Source, Reader.Error);
			std::set<unsigned> Stored;
			for (unsigned I = 0; I < Files.size(); ++I)
			{
				W.Check();
				mz_zip_archive_file_stat S{};
				CheckZip(mz_zip_reader_file_stat(&Source.Zip, I, &S) != 0, Source);
				if (ExceedsExpansionRatio(S.m_uncomp_size, S.m_comp_size, W.Options.Limits.MaxExpansionRatio))
				{
					if (!W.Options.Limits.MaxExpansionRatio)
					{
						Fail("archive-ratio",
						    "nonempty package entries require an expansion ratio of at least one");
					}
					Stored.insert(I);
				}
			}
			if (Stored.empty())
			{
				return Archive;
			}
			// Keep ordinary entries compressed. Only the exceptional entries are read
			// again; cloning the others avoids recompression and unbounded buffers.
			auto Output = Archive.parent_path() / "stored.zip";
			FWriter Writer(Output, W, W.Options.Limits.MaxArchiveBytes);
			FZip Destination;
			Destination.Zip.m_pWrite = FWriter::WriteAt;
			Destination.Zip.m_pIO_opaque = &Writer;
			Initialized =
			    mz_zip_writer_init_v2(&Destination.Zip, 0,
			        mz_zip_is_zip64(&Source.Zip) ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0) != 0;
			CheckZip(Initialized, Destination, Writer.Error);
			MZ_TIME_T Time = 315532800;
			for (unsigned I = 0; I < Files.size(); ++I)
			{
				W.Check();
				if (Stored.count(I))
				{
					std::string Name = Files[I].at("path");
					FReader Input(Root / U8Path(Name), W);
					auto Written = mz_zip_writer_add_read_buf_callback(
					                   &Destination.Zip, Name.c_str(), FReader::ReadAt, &Input,
					                   Files[I].at("size").get<std::uint64_t>(), &Time, nullptr, 0, 0,
					                   nullptr, 0, nullptr, 0) != 0;
					CheckZip(Written, Destination, Input.Error ? Input.Error : Writer.Error);
				}
				else
				{
					auto Written = mz_zip_writer_add_from_zip_reader(&Destination.Zip, &Source.Zip, I) != 0;
					CheckZip(Written, Destination, Reader.Error ? Reader.Error : Writer.Error);
				}
			}
			auto Finalized = mz_zip_writer_finalize_archive(&Destination.Zip) != 0;
			CheckZip(Finalized, Destination, Writer.Error);
			if (!Writer.Stream.flush())
			{
				Fail("archive-write", "failed flushing stored ZIP entries");
			}
			return Output;
		}
	} // namespace
	bool IsPackageArchive(const fs::path& Path)
	{
		auto Extension = NativePathText(Path.extension());
		for (auto& C : Extension)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C = static_cast<char>(C + ('a' - 'A'));
			}
		}
		return Extension == ".vspkg" || Extension == ".zip";
	}
	J Inventory(const fs::path& Root, const FWork& W, bool Filter, const J& M, bool RequirePackageManifest)
	{
		W.Check();
		NoLinks(Root);
		if (!fs::is_directory(Root))
		{
			Fail("package-directory", "package root is not a directory");
		}
		J Result = J::array();
		std::uint64_t Total = 0;
		std::set<std::string> Seen;
		for (fs::recursive_directory_iterator I(Root), End; I != End; ++I)
		{
			W.Check();
			auto Rel = PathUtil::ToUtf8(I->path().lexically_relative(Root));
			auto Base = NativePathText(I->path().filename());
			if (Filter && (Base == ".git" || Base == ".veroscript"))
			{
				if (I->is_directory())
				{
					I.disable_recursion_pending();
				}
				continue;
			}
			if (Rel == ".vero-lease" || Rel == ".vero-receipt.json" || Rel == ".vero-owner.json")
			{
				if (Filter)
				{
					Fail("reserved-file", "package source contains installer-owned metadata", Rel);
				}
				continue;
			}
			CheckPath(Rel);
			NoLinks(I->path());
			if (!Seen.insert(Fold(Rel)).second)
			{
				Fail("path-ambiguity", "case-ambiguous package paths", Rel);
			}
			bool Dir = I->is_directory();
			if (!Dir && !I->is_regular_file())
			{
				Fail("file-type", "unsupported package file", Rel);
			}
			if (Filter && !Dir && !Selected(Rel, M, W))
			{
				continue;
			}
			if (Result.size() >= W.Options.Limits.MaxFiles)
			{
				Fail("file-count", "package file count exceeds budget");
			}
			auto Size = Dir ? 0 : fs::file_size(I->path());
			if (Size > W.Options.Limits.MaxFileBytes || Size > W.Options.Limits.MaxExpandedBytes - Total)
			{
				Fail("expanded-limit", "package content exceeds byte budget", Rel);
			}
			Total += Size;
			bool Exec = !Dir && (I->status().permissions() & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none;
			#if defined(_WIN32)
			Exec = false;
			#endif
			Result.push_back({{"path", Rel},
			    {"directory", Dir},
			    {"size", Size},
			    {"sha256", Dir ? "" : HashFile(I->path(), W, W.Options.Limits.MaxFileBytes)},
			    {"executable", Exec}});
		}
		std::sort(Result.begin(), Result.end(),
		    [](const J& A, const J& B)
		{
			return A["path"] < B["path"];
		});
		if (M.is_object() && !M.empty())
		{
			Required(Root, M, Result, RequirePackageManifest);
		}
		return Result;
	}
	std::string InventoryHash(const J& I)
	{
		return Id(I);
	}
	void VerifyInventory(const fs::path& P, const J& Files, const FWork& W)
	{
		if (!Files.is_array() || Files.size() > W.Options.Limits.MaxFiles)
		{
			Fail("receipt-invalid", "invalid file inventory");
		}
		auto Actual = Inventory(P, W);
		if (Actual != Files)
		{
			Fail("package-corrupt", "installed content differs from its receipt", NativePathText(P));
		}
	}
	void CopyInventory(const fs::path& From, const fs::path& To, const J& Files, const FWork& W)
	{
		FPathWriter Paths(To);
		for (const auto& F : Files)
		{
			W.Check();
			std::string Rel = F.at("path");
			CheckPath(Rel);
			auto Src = From / U8Path(Rel), Dest = To / U8Path(Rel);
			NoLinks(Src);
			if (F.at("directory").get<bool>())
			{
				Paths.Directory(U8Path(Rel));
				continue;
			}
			Paths.File(U8Path(Rel));
			std::ifstream In(Src, std::ios::binary);
			std::ofstream Out(Dest, std::ios::binary);
			std::array<char, 65536> Buffer;
			std::uint64_t N = 0, Limit = F.at("size");
			while (In)
			{
				W.Check();
				In.read(Buffer.data(), Buffer.size());
				auto Size = static_cast<std::uint64_t>(In.gcount());
				if (Size > Limit - N)
				{
					Fail("source-changed", "source grew during snapshot copy", Rel);
				}
				N += Size;
				if (!Out.write(Buffer.data(), static_cast<std::streamsize>(Size)))
				{
					Fail("file-write", "failed copying content", Rel);
				}
			}
			Out.close();
			if (!In.eof() || N != Limit || F.at("sha256") != HashFile(Dest, W, W.Options.Limits.MaxFileBytes))
			{
				Fail("source-changed", "source changed during snapshot copy", Rel);
			}
			#if !defined(_WIN32)
			fs::permissions(Dest, F.value("executable", false) ? fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec : fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
			#endif
		}
	}
	void SnapshotArchive(const fs::path& From, const fs::path& To, const FWork& W,
	    const std::string& Expected, std::uint64_t ExpectedSize)
	{
		NoLinks(From);
		if (fs::file_size(From) > W.Options.Limits.MaxArchiveBytes)
		{
			Fail("archive-limit", "archive exceeds snapshot budget");
		}
		NoLinks(To, true);
		fs::create_directories(To.parent_path());
		std::ifstream In(From, std::ios::binary);
		std::ofstream Out(To, std::ios::binary);
		if (!In || !Out)
		{
			Fail("archive-copy", "cannot snapshot archive");
		}
		std::array<char, 65536> Buffer;
		std::uint64_t Total = 0;
		while (In)
		{
			W.Check();
			In.read(Buffer.data(), Buffer.size());
			auto N = static_cast<std::uint64_t>(In.gcount());
			if (N > W.Options.Limits.MaxArchiveBytes - Total)
			{
				Fail("archive-limit", "archive grew beyond snapshot budget");
			}
			Total += N;
			if (!Out.write(Buffer.data(), static_cast<std::streamsize>(N)))
			{
				Fail("archive-copy", "failed writing archive snapshot");
			}
		}
		Out.close();
		if (!Out)
		{
			Fail("archive-copy", "failed flushing archive snapshot");
		}
		if (!In.eof())
		{
			Fail("archive-copy", "archive changed or failed during snapshot");
		}
		if (ExpectedSize && Total != ExpectedSize)
		{
			Fail("artifact-size", "archive snapshot size differs from expected metadata");
		}
		if (!Expected.empty() && HashFile(To, W, W.Options.Limits.MaxArchiveBytes) != Expected)
		{
			Fail("artifact-digest", "archive snapshot differs from expected SHA-256");
		}
	}
	void Extract(const fs::path& Archive, const fs::path& Root, const FWork& W)
	{
		NoLinks(Archive);
		auto Size = fs::file_size(Archive);
		if (Size > W.Options.Limits.MaxArchiveBytes)
		{
			Fail("archive-limit", "ZIP exceeds its byte budget");
		}
		FReader Reader(Archive, W);
		FZip Z;
		Z.Zip.m_pRead = FReader::ReadAt;
		Z.Zip.m_pIO_opaque = &Reader;
		CheckZip(mz_zip_reader_init(&Z.Zip, Size, 0) != 0, Z, Reader.Error);
		auto Count = mz_zip_reader_get_num_files(&Z.Zip);
		if (Count > W.Options.Limits.MaxFiles)
		{
			Fail("file-count", "ZIP contains too many entries");
		}
		std::set<std::string> Names, Files;
		std::uint64_t Total = 0;
		struct Entry
		{
			unsigned Index;
			std::string Name;
			bool Directory, Executable;
			std::uint64_t Size;
		};
		std::vector<Entry> Entries;
		for (unsigned I = 0; I < Count; ++I)
		{
			W.Check();
			mz_zip_archive_file_stat S{};
			CheckZip(mz_zip_reader_file_stat(&Z.Zip, I, &S) != 0, Z);
			auto Length = mz_zip_reader_get_filename(&Z.Zip, I, nullptr, 0);
			if (!Length || Length > 4096)
			{
				Fail("package-path", "ZIP filename exceeds budget");
			}
			std::string N(Length, '\0');
			mz_zip_reader_get_filename(&Z.Zip, I, N.data(), Length);
			N.pop_back();
			bool Dir = mz_zip_reader_is_file_a_directory(&Z.Zip, I) != 0;
			CheckPath(N, Dir);
			if (Dir && !N.empty() && N.back() == '/')
			{
				N.pop_back();
			}
			if (N == ".vero-lease" || N == ".vero-receipt.json" || N == ".vero-owner.json")
			{
				Fail("reserved-file", "archive contains installer-owned metadata");
			}
			for (const auto& Part : U8Path(N))
			{
				if (Part == ".git" || Part == ".veroscript")
				{
					Fail("managed-content", "archive contains installation or source-control state", N);
				}
			}
			auto Kind = (S.m_external_attr >> 16) & 0170000u;
			if ((S.m_bit_flag & 1) || S.m_is_encrypted || (!S.m_is_supported) ||
			    (Kind && Kind != (Dir ? 0040000u : 0100000u)))
			{
				Fail("archive-type", "encrypted, linked or unsupported ZIP entry", N);
			}
			if (!Names.insert(Fold(N)).second)
			{
				Fail("archive-duplicate", "duplicate or case-ambiguous ZIP entry", N);
			}
			if (!Dir)
			{
				Files.insert(Fold(N));
			}
			for (auto Parent = U8Path(Fold(N)).parent_path(); !Parent.empty(); Parent = Parent.parent_path())
			{
				if (Files.count(PathUtil::ToUtf8(Parent)))
				{
					Fail("archive-prefix", "file/directory prefix conflict", N);
				}
			}
			if (!Dir)
			{
				auto Prefix = Fold(N) + "/";
				auto Descendant = Names.lower_bound(Prefix);
				if (Descendant != Names.end() && Descendant->rfind(Prefix, 0) == 0)
				{
					Fail("archive-prefix", "file/directory prefix conflict", N);
				}
			}
			if (S.m_uncomp_size > W.Options.Limits.MaxFileBytes ||
			    S.m_uncomp_size > W.Options.Limits.MaxExpandedBytes - Total)
			{
				Fail("expanded-limit", "ZIP expanded size exceeds budget", N);
			}
			Total += S.m_uncomp_size;
			if (ExceedsExpansionRatio(S.m_uncomp_size, S.m_comp_size, W.Options.Limits.MaxExpansionRatio))
			{
				Fail("archive-ratio", "ZIP expansion ratio exceeds budget", N);
			}
			Entries.push_back({I, N, Dir, (S.m_external_attr >> 16 & 0111u) != 0, S.m_uncomp_size});
		}
		if (ExceedsExpansionRatio(Total, Size, W.Options.Limits.MaxExpansionRatio))
		{
			Fail("archive-ratio", "total ZIP expansion ratio exceeds budget");
		}
		NoLinks(Root, true);
		fs::create_directories(Root);
		FPathWriter Paths(Root);
		for (const auto& E : Entries)
		{
			W.Check();
			auto P = Root / U8Path(E.Name);
			if (E.Directory)
			{
				Paths.Directory(U8Path(E.Name));
				continue;
			}
			Paths.File(U8Path(E.Name));
			FWriter Output(P, W, E.Size);
			auto Extracted =
			    mz_zip_reader_extract_to_callback(&Z.Zip, E.Index, FWriter::WriteAt, &Output, 0) != 0;
			CheckZip(Extracted, Z, Output.Error ? Output.Error : Reader.Error);
			if (Output.Written != E.Size)
			{
				Fail("archive-size", "actual ZIP size differs from metadata");
			}
			Output.Stream.close();
			if (!Output.Stream)
			{
				Fail("archive-write", "failed closing extracted ZIP entry", E.Name);
			}
			#if !defined(_WIN32)
			if (E.Executable)
			{
				fs::permissions(P, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
				    fs::perm_options::add);
			}
			#endif
		}
	}
	void Pack(const fs::path& Root, const fs::path& Output, const J& M, const FWork& W)
	{
		if (fs::exists(Output))
		{
			Fail("output-exists", "pack does not overwrite an existing file", NativePathText(Output));
		}
		NoLinks(Output, true);
		auto Files = Inventory(Root, W, true, M);
		FTempDirectory Temp(Under(Output, Root) ? Root / ".veroscript" / "pack-staging"
		                                        : Output.parent_path());
		auto P = Temp.Path / "package.zip";
		{
			FWriter Writer(P, W, W.Options.Limits.MaxArchiveBytes);
			FZip Z;
			Z.Zip.m_pWrite = FWriter::WriteAt;
			Z.Zip.m_pIO_opaque = &Writer;
			CheckZip(mz_zip_writer_init(&Z.Zip, 0) != 0, Z, Writer.Error);
			MZ_TIME_T Time = 315532800;
			for (const auto& F : Files)
			{
				W.Check();
				std::string Name = F["path"];
				if (F["directory"].get<bool>())
				{
					Name += '/';
					CheckZip(mz_zip_writer_add_mem_ex_v2(&Z.Zip, Name.c_str(), nullptr, 0, nullptr, 0, 0, 0,
					             0, &Time, nullptr, 0, nullptr, 0) != 0,
					    Z, Writer.Error);
				}
				else
				{
					FReader Source(Root / U8Path(Name), W);
					auto Written =
					    mz_zip_writer_add_read_buf_callback(&Z.Zip, Name.c_str(), FReader::ReadAt, &Source,
					        F["size"].get<std::uint64_t>(), &Time, nullptr, 0,
					        6, nullptr, 0, nullptr, 0) != 0;
					CheckZip(Written, Z, Source.Error ? Source.Error : Writer.Error);
				}
			}
			CheckZip(mz_zip_writer_finalize_archive(&Z.Zip) != 0, Z, Writer.Error);
			if (!Writer.Stream.flush())
			{
				Fail("archive-write", "failed flushing ZIP archive");
			}
		}
		P = StoreOvercompressedEntries(P, Root, Files, W);
		if (Inventory(Root, W, true, M) != Files)
		{
			Fail("source-changed", "source changed while packing");
		}
		// Preserve Unix execute attributes without inheriting timestamps or platform
		// metadata. The central records were just generated by our fixed ZIP writer.
		{
			FReader Reader(P, W);
			FZip Z;
			Z.Zip.m_pRead = FReader::ReadAt;
			Z.Zip.m_pIO_opaque = &Reader;
			CheckZip(mz_zip_reader_init(&Z.Zip, fs::file_size(P), 0) != 0, Z, Reader.Error);
			std::fstream Out(P, std::ios::in | std::ios::out | std::ios::binary);
			for (unsigned I = 0; I < Files.size(); ++I)
			{
				W.Check();
				mz_zip_archive_file_stat S{};
				CheckZip(mz_zip_reader_file_stat(&Z.Zip, I, &S) != 0, Z);
				auto Offset = Z.Zip.m_central_directory_file_ofs + S.m_central_dir_ofs;
				char Made[] = {20, 3};
				Out.seekp(Offset + 4);
				Out.write(Made, 2);
				unsigned Mode = Files[I]["directory"].get<bool>() ? 0040755u
				    : Files[I]["executable"].get<bool>()          ? 0100755u
				                                                  : 0100644u;
				std::uint32_t Attr = Mode << 16;
				char Bytes[4];
				for (unsigned N = 0; N < 4; ++N)
				{
					Bytes[N] = char(Attr >> (8 * N));
				}
				Out.seekp(Offset + 38);
				Out.write(Bytes, 4);
				char Stamp[] = {0, 0, 33, 0};
				Out.seekp(Offset + 12);
				Out.write(Stamp, 4);
				Out.seekp(S.m_local_header_ofs + 10);
				Out.write(Stamp, 4);
			}
			if (!Out.flush())
			{
				Fail("archive-write", "failed writing ZIP attributes");
			}
		}
		W.Check();
		if (fs::exists(Output))
		{
			Fail("output-exists", "pack does not overwrite an existing file", NativePathText(Output));
		}
		fs::create_directories(Output.parent_path());
		MovePathNoReplace(P, Output);
	}
} // namespace VeroScript::Packages::Detail
