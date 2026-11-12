// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptExecutable.h"
#include "ApplicationInternal.h"
#include <array>
#include <cstring>
#include <fstream>

namespace VeroScript::Application
{
	using namespace Detail;
	namespace
	{
		constexpr std::array<std::uint8_t, 16> Magic{'V', 'e', 'r', 'o', 'S', 'c', 'r', 'i', 'p', 't', 'A', 'p', 'p', 0, 1, 1};
		constexpr std::size_t FooterSize = 96;
		std::uint64_t Number(const std::uint8_t* P, unsigned Size)
		{
			std::uint64_t N = 0;
			for (unsigned I = 0; I < Size; ++I)
			{
				N |= static_cast<std::uint64_t>(P[I]) << (I * 8);
			}
			return N;
		}
		void Put(std::uint8_t* P, std::uint64_t N)
		{
			for (unsigned I = 0; I < 8; ++I)
			{
				P[I] = static_cast<std::uint8_t>(N >> (I * 8));
			}
		}
		std::uint64_t ContentEnd(std::ifstream& Input, std::uint64_t Size, bool RequireUnsigned)
		{
			const auto ReadAt = [&](std::uint64_t Offset, std::size_t N)
			{
				if (Offset > Size || N > Size - Offset)
				{
					Fail("executable-bounds", "native executable contains an invalid range");
				}
				std::vector<std::uint8_t> B(N);
				Input.clear();
				Input.seekg(static_cast<std::streamoff>(Offset));
				if (!Input.read(reinterpret_cast<char*>(B.data()), static_cast<std::streamsize>(N)))
				{
					Fail("executable-read", "cannot read native executable");
				}
				return B;
			};
			const auto Header = ReadAt(0, 64);
			if (Header[0] == 0x7f && Header[1] == 'E' && Header[2] == 'L' && Header[3] == 'F')
			{
				if ((Header[4] != 1 && Header[4] != 2) || (Header[5] != 1 && Header[5] != 2) || Header[6] != 1)
				{
					Fail("executable-elf", "invalid ELF class, byte order or version");
				}
				const auto Field = [&](std::size_t Offset, unsigned N)
				{
					std::uint64_t V = 0;
					for (unsigned I = 0; I < N; ++I)
					{
						V |= static_cast<std::uint64_t>(Header[Offset + I]) << ((Header[5] == 1 ? I : N - 1 - I) * 8);
					}
					return V;
				};
				const bool Wide = Header[4] == 2;
				const auto Type = Field(16, 2), PhOff = Field(Wide ? 32 : 28, Wide ? 8 : 4);
				const auto PhSize = Field(Wide ? 54 : 42, 2), PhCount = Field(Wide ? 56 : 44, 2);
				if ((Type != 2 && Type != 3) || !Field(18, 2) || Field(20, 4) != 1 || Field(Wide ? 52 : 40, 2) != (Wide ? 64 : 52) ||
				    !PhCount || PhSize < (Wide ? 56u : 32u) || PhOff > Size || PhCount > (Size - PhOff) / PhSize)
				{
					Fail("executable-elf", "invalid ELF executable header or program table");
				}
				return Size;
			}
			if (Header[0] != 'M' || Header[1] != 'Z')
			{
				Fail("executable-platform", "this envelope provider supports PE and ELF bootstraps");
			}
			const auto PE = Number(Header.data() + 60, 4);
			const auto FileHeader = ReadAt(PE, 26);
			if (std::memcmp(FileHeader.data(), "PE\0\0", 4) != 0)
			{
				Fail("executable-pe", "invalid PE signature");
			}
			const auto OptionalSize = Number(FileHeader.data() + 20, 2);
			const auto Optional = ReadAt(PE + 24, static_cast<std::size_t>(OptionalSize));
			if (Optional.size() < 2)
			{
				Fail("executable-pe", "truncated PE optional header");
			}
			const auto Bits = Number(Optional.data(), 2);
			const std::size_t Directory = Bits == 0x20b ? 112 : Bits == 0x10b ? 96
			                                                                  : 0;
			if (!Directory || Optional.size() < Directory + 40 || Number(Optional.data() + Directory - 4, 4) < 5)
			{
				Fail("executable-pe", "unsupported PE optional header");
			}
			const auto Offset = Number(Optional.data() + Directory + 32, 4), Bytes = Number(Optional.data() + Directory + 36, 4);
			if (!Offset && !Bytes) return Size;
			if (RequireUnsigned || !Offset || !Bytes || Offset % 8 || Offset > Size || Bytes != Size - Offset)
			{
				Fail("executable-signature", "PE certificate range is invalid or bootstrap is already signed");
			}
			return Offset;
		}
	}
	bool ReadExecutableApplication(const std::string& Path, std::vector<std::uint8_t>& Out,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		Out.clear();
		try
		{
			Physical(PathUtil::FromUtf8(Path));
			std::ifstream Input(PathUtil::FromUtf8(Path), std::ios::binary | std::ios::ate);
			const auto End = Input ? Input.tellg() : std::streampos{-1};
			if (End < 160 || static_cast<std::uint64_t>(End) > Limits.MaxArchiveBytes + Limits.MaxFileBytes + FooterSize + (32ull << 20))
			{
				Fail("executable-budget", "native application exceeds its size budget");
			}
			const auto Bound = ContentEnd(Input, static_cast<std::uint64_t>(End), false);
			std::array<std::uint8_t, FooterSize> Footer{};
			bool Found = false;
			for (std::uint64_t Padding = 0; Padding < (Bound == static_cast<std::uint64_t>(End) ? 1u : 8u) && Bound >= FooterSize + Padding; ++Padding)
			{
				Input.clear();
				Input.seekg(static_cast<std::streamoff>(Bound - FooterSize - Padding));
				if (Input.read(reinterpret_cast<char*>(Footer.data()), Footer.size()) && std::equal(Magic.begin(), Magic.end(), Footer.begin()))
				{
					std::array<char, 7> Zeros{};
					if (Padding && (!Input.read(Zeros.data(), Padding) || std::any_of(Zeros.begin(), Zeros.begin() + Padding, [](char C)
					{
						return C != 0;
					})))
					{
						Fail("executable-padding", "certificate alignment padding must contain zero bytes");
					}
					Input.seekg(static_cast<std::streamoff>(Bound - Padding));
					Found = true;
					break;
				}
			}
			if (!Found)
			{
				Fail("executable-locator", "native executable has no valid application locator");
			}
			const auto Offset = Number(Footer.data() + 16, 8), Size = Number(Footer.data() + 24, 8);
			const auto FooterOffset = static_cast<std::uint64_t>(Input.tellg()) - FooterSize;
			if (!Size || Size > Limits.MaxArchiveBytes || Offset < 64 || Offset > FooterOffset || Size != FooterOffset - Offset)
			{
				Fail("executable-bounds", "application locator has an invalid payload range");
			}
			Out.resize(static_cast<std::size_t>(Size));
			Input.seekg(static_cast<std::streamoff>(Offset));
			for (std::size_t I = 0; I < Out.size();)
			{
				Check(Limits);
				const auto N = std::min<std::size_t>(65536, Out.size() - I);
				if (!Input.read(reinterpret_cast<char*>(Out.data() + I), N))
				{
					Fail("executable-read", "embedded application is truncated");
				}
				I += N;
			}
			if (ContentSha256(Out) != std::string(Footer.begin() + 32, Footer.begin() + 96))
			{
				Fail("executable-integrity", "embedded application digest differs");
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
			Error = {"executable", E.what(), Path, {}};
			return false;
		}
	}
	bool WriteExecutableApplication(const std::string& Launcher, const std::vector<FContentFile>& Files,
	    const std::string& Output, FDiagnostic& Error, const FLimits& Limits)
	{
		fs::path Archive;
		try
		{
			Physical(PathUtil::FromUtf8(Launcher));
			Physical(PathUtil::FromUtf8(Output), true);
			if (fs::exists(PathUtil::FromUtf8(Output)))
			{
				Fail("output-exists", "executable output already exists", Output);
			}
			const auto Stub = ReadFile(PathUtil::FromUtf8(Launcher), Limits);
			if (Stub.size() >= FooterSize && std::equal(Magic.begin(), Magic.end(), Stub.end() - FooterSize))
			{
				Fail("executable-bootstrap", "an application executable cannot be reused as an empty bootstrap");
			}
			std::ifstream Input(PathUtil::FromUtf8(Launcher), std::ios::binary);
			(void)ContentEnd(Input, Stub.size(), true);
			// The temporary ZIP stays beside the new output, on its final volume.
			Archive = PathUtil::MakeTemporarySiblingPath(PathUtil::FromUtf8(Output));
			if (!WriteArchive(Files, PathUtil::ToUtf8(Archive), Error, Limits)) return false;
			auto ArchiveLimits = Limits;
			ArchiveLimits.MaxFileBytes = Limits.MaxArchiveBytes;
			const auto Payload = ReadFile(Archive, ArchiveLimits);
			std::array<std::uint8_t, FooterSize> Footer{};
			std::copy(Magic.begin(), Magic.end(), Footer.begin());
			Put(Footer.data() + 16, Stub.size());
			Put(Footer.data() + 24, Payload.size());
			const auto Hash = ContentSha256(Payload);
			std::copy(Hash.begin(), Hash.end(), Footer.begin() + 32);
			std::vector<std::uint8_t> Combined = Stub;
			Combined.insert(Combined.end(), Payload.begin(), Payload.end());
			Combined.insert(Combined.end(), Footer.begin(), Footer.end());
			WriteFile(PathUtil::FromUtf8(Output), Combined);
			#if !defined(_WIN32)
			fs::permissions(PathUtil::FromUtf8(Output), fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec);
			#endif
			fs::remove(Archive);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
		}
		catch (const std::exception& E)
		{
			Error = {"executable-write", E.what(), Output, {}};
		}
		if (!Archive.empty())
		{
			std::error_code Ec;
			fs::remove(Archive, Ec);
		}
		return false;
	}
}
