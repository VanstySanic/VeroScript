// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAudio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
	std::string Error;
	unsigned Failures = 0;
	void Check(bool V, const char* Name)
	{
		if (!V)
		{
			++Failures;
			std::cerr << Name << ": " << Error << '\n';
		}
	}
	std::vector<std::uint8_t> Wave(unsigned Tag, unsigned Bits, const std::vector<std::uint8_t>& Data)
	{
		std::vector<std::uint8_t> B(44 + Data.size() + (Data.size() & 1));
		const auto Put = [&B](unsigned Offset, std::uint64_t Value, unsigned Count)
		{
			for (unsigned I = 0; I < Count; ++I)
			{
				B[Offset + I] = static_cast<std::uint8_t>(Value >> (8 * I));
			}
		};
		std::memcpy(B.data(), "RIFF", 4);
		Put(4, B.size() - 8, 4);
		std::memcpy(B.data() + 8, "WAVEfmt ", 8);
		Put(16, 16, 4);
		Put(20, Tag, 2);
		Put(22, 1, 2);
		Put(24, 48000, 4);
		Put(28, 48000 * (Bits / 8), 4);
		Put(32, Bits / 8, 2);
		Put(34, Bits, 2);
		std::memcpy(B.data() + 36, "data", 4);
		Put(40, Data.size(), 4);
		std::copy(Data.begin(), Data.end(), B.begin() + 44);
		return B;
	}
}
int main()
{
	using namespace VeroScript::Audio;
	Check(ValidateFormat({48000, 2}, Error) && !ValidateFormat({0, 2}, Error) && !ValidateFormat({48000, 9}, Error),
		  "format ranges");
	FStreamOptions O;
	Check(ValidateStreamOptions(O, Error), "default stream options");
	O.MaxBufferedFrames = std::numeric_limits<std::int64_t>::max();
	Check(!ValidateStreamOptions(O, Error), "frame budget overflow");
	Check(!ValidateSamples({48000, 2}, {0}, Error) &&
			  !ValidateSamples({48000, 1}, {std::numeric_limits<float>::infinity()}, Error),
		  "partial frames and nonfinite samples");
	FBuffer Out;
	Check(DecodeWav(Wave(1, 8, {0, 128, 255}), Out, Error) && Out.Samples.size() == 3 && Out.Samples[0] == -1 &&
			  Out.Samples[1] == 0 && Out.Samples[2] == 127.0f / 128,
		  "unsigned PCM and odd chunk padding");
	Check(DecodeWav(Wave(1, 16, {0, 128, 0, 0, 255, 127}), Out, Error) && Out.Samples[0] == -1 && Out.Samples[1] == 0 &&
			  Out.Samples[2] == 32767.0f / 32768,
		  "signed PCM16");
	Check(DecodeWav(Wave(1, 24, {0, 0, 128, 255, 255, 127}), Out, Error) && Out.Samples[0] == -1 &&
			  Out.Samples[1] > 0.99f,
		  "signed PCM24");
	Check(DecodeWav(Wave(1, 32, {0, 0, 0, 128, 0, 0, 0, 64}), Out, Error) && Out.Samples[0] == -1 &&
			  Out.Samples[1] == 0.5f,
		  "signed PCM32");
	Check(DecodeWav(Wave(3, 32, {0, 0, 0, 191}), Out, Error) && Out.Samples[0] == -0.5f, "IEEE float32");
	Check(DecodeWav(Wave(3, 64, {0, 0, 0, 0, 0, 0, 224, 63}), Out, Error) && Out.Samples[0] == 0.5f, "IEEE float64");
	const auto Saved = Out.Samples;
	Check(!DecodeWav(Wave(3, 32, {0, 0, 192, 127}), Out, Error) && Out.Samples == Saved,
		  "NaN failure preserves buffer");
	Check(!DecodeWav(Wave(1, 16, {0}), Out, Error), "truncated sample rejected");
	auto Bytes = Wave(1, 16, {0, 0});
	for (std::size_t I = 0; I < Bytes.size(); ++I)
	{
		std::vector<std::uint8_t> Truncated(Bytes.begin(), Bytes.begin() + I);
		Check(!DecodeWav(Truncated, Out, Error), "truncated WAV rejected");
	}
	Bytes[40] = 255;
	Check(!DecodeWav(Bytes, Out, Error), "oversized data chunk rejected");
	Check(!DecodeWav(Wave(2, 16, {0, 0}), Out, Error), "compressed WAV explicitly unsupported");
	std::cout << "Audio contract failures: " << Failures << '\n';
	return Failures ? 1 : 0;
}
