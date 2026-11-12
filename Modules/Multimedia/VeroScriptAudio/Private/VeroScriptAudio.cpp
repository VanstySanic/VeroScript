// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAudio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace VeroScript::Audio
{
	static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
					  std::numeric_limits<double>::is_iec559,
				  "Audio PCM requires IEEE float32/float64");
	bool ValidateFormat(const FFormat& F, std::string& Error)
	{
		if (F.SampleRate < 1 || F.SampleRate > 384000 || F.Channels < 1 || F.Channels > 8)
		{
			Error = "audio.invalid_format: rate must be 1..384000 Hz and channels 1..8";
			return false;
		}
		Error.clear();
		return true;
	}
	bool ValidateSamples(const FFormat& F, const std::vector<float>& Samples, std::string& Error)
	{
		if (!ValidateFormat(F, Error))
		{
			return false;
		}
		if (Samples.size() > MaxBufferBytes / sizeof(float) || Samples.size() % F.Channels)
		{
			Error = "audio.invalid_samples: expected whole interleaved frames within 64 MiB";
			return false;
		}
		for (float V : Samples)
		{
			if (!std::isfinite(V))
			{
				Error = "audio.invalid_samples: samples must be finite";
				return false;
			}
		}
		Error.clear();
		return true;
	}
	bool ValidateStreamOptions(const FStreamOptions& O, std::string& Error)
	{
		if (!ValidateFormat(O.Format, Error))
		{
			return false;
		}
		if (O.Direction > EDirection::Recording || O.MaxBufferedFrames < 1 ||
			static_cast<std::uint64_t>(O.MaxBufferedFrames) > MaxBufferBytes / (sizeof(float) * O.Format.Channels))
		{
			Error = "audio.invalid_stream: invalid direction or frame budget outside 1..64 MiB";
			return false;
		}
		Error.clear();
		return true;
	}
	bool ValidateGain(double Gain, std::string& Error)
	{
		if (!std::isfinite(Gain) || Gain < 0 || Gain > 16)
		{
			Error = "audio.invalid_gain: expected finite gain in 0..16";
			return false;
		}
		Error.clear();
		return true;
	}
	bool DecodeWav(const std::vector<std::uint8_t>& B, FBuffer& Result, std::string& Error)
	{
		const auto Fail = [&Error]()
		{
			Error = "audio.invalid_wav: expected bounded RIFF/WAVE PCM 8/16/24/32 or float 32/64";
			return false;
		};
		if (B.size() < 12 || B.size() > MaxBufferBytes || std::memcmp(B.data(), "RIFF", 4) ||
			std::memcmp(B.data() + 8, "WAVE", 4))
		{
			return Fail();
		}
		const auto U = [&B](std::size_t P, unsigned Count)
		{
			std::uint64_t V = 0;
			for (unsigned I = 0; I < Count; ++I)
			{
				V |= std::uint64_t{B[P + I]} << (8 * I);
			}
			return V;
		};
		const auto End = U(4, 4) + 8;
		if (End < 12 || End > B.size())
		{
			return Fail();
		}
		std::size_t Fmt = 0, Data = 0, DataSize = 0;
		for (std::size_t P = 12; P < End;)
		{
			if (End - P < 8)
			{
				return Fail();
			}
			const auto Size = U(P + 4, 4);
			if (Size > End - P - 8)
			{
				return Fail();
			}
			if (!std::memcmp(B.data() + P, "fmt ", 4))
			{
				if (Fmt || Size < 16)
				{
					return Fail();
				}
				Fmt = P + 8;
			}
			else if (!std::memcmp(B.data() + P, "data", 4))
			{
				if (Data)
				{
					return Fail();
				}
				Data = P + 8;
				DataSize = static_cast<std::size_t>(Size);
			}
			const auto Next = P + 8 + Size + (Size & 1);
			if (Next > End)
			{
				return Fail();
			}
			P = static_cast<std::size_t>(Next);
		}
		if (!Fmt || !Data)
		{
			return Fail();
		}
		const auto Tag = U(Fmt, 2), Channels = U(Fmt + 2, 2), Rate = U(Fmt + 4, 4), Align = U(Fmt + 12, 2),
				   Bits = U(Fmt + 14, 2);
		if (Rate > 384000 || Channels < 1 || Channels > 8 || Rate < 1 ||
			!((Tag == 1 && (Bits == 8 || Bits == 16 || Bits == 24 || Bits == 32)) ||
			  (Tag == 3 && (Bits == 32 || Bits == 64))) ||
			Align != Channels * (Bits / 8) || U(Fmt + 8, 4) != Rate * Align || DataSize % Align)
		{
			return Fail();
		}
		const auto Count = DataSize / (Bits / 8);
		if (Count > std::min<std::uint64_t>(MaxBufferBytes, std::numeric_limits<std::size_t>::max()) / sizeof(float))
		{
			return Fail();
		}
		FBuffer Copy;
		Copy.Format = {static_cast<std::int32_t>(Rate), static_cast<std::int32_t>(Channels)};
		Copy.Samples.resize(Count);
		for (std::size_t I = 0; I < Count; ++I)
		{
			const auto Raw = U(Data + I * (Bits / 8), static_cast<unsigned>(Bits / 8));
			double V;
			if (Tag == 3 && Bits == 32)
			{
				const auto Word = static_cast<std::uint32_t>(Raw);
				float Sample;
				std::memcpy(&Sample, &Word, sizeof(Sample));
				V = Sample;
			}
			else if (Tag == 3)
			{
				std::memcpy(&V, &Raw, sizeof(V));
			}
			else if (Bits == 8)
			{
				V = (static_cast<double>(Raw) - 128) / 128;
			}
			else
			{
				const auto Sign = std::uint64_t{1} << (Bits - 1);
				const auto Signed =
					static_cast<std::int64_t>(Raw) - (Raw & Sign ? static_cast<std::int64_t>(Sign * 2) : 0);
				V = static_cast<double>(Signed) / Sign;
			}
			if (!std::isfinite(V) || std::abs(V) > std::numeric_limits<float>::max())
			{
				return Fail();
			}
			Copy.Samples[I] = static_cast<float>(V);
		}
		Result = std::move(Copy);
		Error.clear();
		return true;
	}
}
