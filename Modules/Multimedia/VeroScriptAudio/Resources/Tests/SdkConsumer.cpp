// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAudio.h"
#include <iostream>

int main()
{
	using namespace VeroScript::Audio;
	std::string Error;
	const std::vector<std::uint8_t> Wave{82, 73, 70, 70, 38,  0,  0,   0,  87,	65,	 86, 69, 102, 109, 116, 32,
										 16, 0,	 0,	 0,	 1,	  0,  1,   0,  128, 187, 0,	 0,	 128, 187, 0,	0,
										 1,	 0,	 8,	 0,	 100, 97, 116, 97, 2,	0,	 0,	 0,	 0,	  255};
	FBuffer Buffer;
	if (!ValidateStreamOptions({}, Error) || !DecodeWav(Wave, Buffer, Error) || Buffer.Format.Channels != 1 ||
		Buffer.Samples.size() != 2 || Buffer.Samples.front() != -1)
	{
		std::cerr << Error;
		return 1;
	}
	std::cout << "Audio SDK contracts without SDL/Core/Platform passed\n";
	return 0;
}
