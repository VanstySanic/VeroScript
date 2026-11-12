// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptArtifact.h"
#include "VeroScriptPath.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

namespace VeroScript
{
	bool ReadArtifactFile(const FArtifactRequest& Request, const FArtifactWork& Work,
		FArtifactData& Data, std::string& Error)
	{
		Data = {};
		Error.clear();
		if (Work.CheckCancelled)
		{
			Work.CheckCancelled();
		}
		if (Request.Path.empty() || Request.Path.find('\0') != std::string::npos ||
			Request.Path.rfind("veroscript-module:/", 0) == 0)
		{
			Error = "artifact requires a physical file reader";
			return false;
		}
		std::ifstream Input(PathUtil::FromUtf8(Request.Path), std::ios::binary | std::ios::ate);
		const auto End = Input ? Input.tellg() : std::streampos{-1};
		if (End < 0 || static_cast<std::uint64_t>(End) > Work.MaxBytes ||
			static_cast<std::uint64_t>(End) > std::numeric_limits<std::size_t>::max())
		{
			Error = "artifact cannot be opened or exceeds its byte budget";
			return false;
		}
		Data.Bytes.resize(static_cast<std::size_t>(End));
		Input.seekg(0);
		for (std::size_t Offset = 0; Offset < Data.Bytes.size();)
		{
			if (Work.CheckCancelled)
			{
				Work.CheckCancelled();
			}
			const auto Count = std::min<std::size_t>(65536, Data.Bytes.size() - Offset);
			if (!Input.read(reinterpret_cast<char*>(Data.Bytes.data() + Offset),
				static_cast<std::streamsize>(Count)))
			{
				Data = {};
				Error = "artifact was truncated while reading";
				return false;
			}
			Offset += Count;
		}
		if (Input.peek() != std::char_traits<char>::eof())
		{
			Data = {};
			Error = "artifact changed while reading";
			return false;
		}
		return true;
	}

	bool DecodeArtifact(const FArtifactRequest& Request, const FArtifactWork& Work,
		FArtifactData& Data, std::string& Error, const IArtifactDecoder* Decoder)
	{
		Error.clear();
		if (Request.Path.find('\0') != std::string::npos)
		{
			Data = {};
			Error = "artifact path contains a zero byte";
			return false;
		}
		if (Work.CheckCancelled)
		{
			Work.CheckCancelled();
		}
		if (Data.Bytes.size() > Work.MaxBytes)
		{
			Data = {};
			Error = "artifact exceeds its byte budget";
			return false;
		}
		if (!Data.SignatureRecords && Data.TrustIdentity.empty() && PathUtil::FromUtf8(Request.Path).is_absolute())
		{
			const auto Sidecar = PathUtil::FromUtf8(Request.Path + ".sig.json");
			std::error_code Ec;
			const auto Status = std::filesystem::symlink_status(Sidecar, Ec);
			if (Ec && Ec != std::errc::no_such_file_or_directory)
			{
				Data = {};
				Error = "artifact signature metadata cannot be inspected";
				return false;
			}
			if (!Ec && std::filesystem::exists(Status))
			{
				FArtifactData Record;
				if (!ReadArtifactFile({PathUtil::ToUtf8(Sidecar), "signature", Request.OwnerId}, {4ull << 20, Work.CheckCancelled}, Record, Error))
				{
					Data = {};
					return false;
				}
				Data.SignatureRecords.emplace(Record.Bytes.begin(), Record.Bytes.end());
			}
		}
		if (Decoder)
		{
			if (!Decoder->Decode(Request, Work, Data, Error))
			{
				Data = {};
				return false;
			}
		}
		else if (Data.Bytes.size() >= 4 && std::memcmp(Data.Bytes.data(), "VSAE", 4) == 0)
		{
			Data = {};
			Error = "protected artifact requires an artifact decoder and key service";
			return false;
		}
		else if (Data.SignatureRecords && Data.TrustIdentity.empty())
		{
			Data = {};
			Error = "signed artifact requires a trusted artifact verification service";
			return false;
		}
		if (Data.Bytes.size() > Work.MaxBytes)
		{
			Data = {};
			Error = "decoded artifact exceeds its byte budget";
			return false;
		}
		if (Work.CheckCancelled)
		{
			Work.CheckCancelled();
		}
		return true;
	}

	bool ReadArtifact(const FArtifactRequest& Request, const FArtifactWork& Work,
		FArtifactData& Data, std::string& Error, const IArtifactReader* Reader,
		const IArtifactDecoder* Decoder)
	{
		Data = {};
		Error.clear();
		if (!(Reader ? Reader->Read(Request, Work, Data, Error) :
			ReadArtifactFile(Request, Work, Data, Error)))
		{
			Data = {};
			return false;
		}
		return DecodeArtifact(Request, Work, Data, Error, Decoder);
	}
}
