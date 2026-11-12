// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// numeric_primitives.vs - real primitive widths and numeric literal suffixes
// Run from this directory: veroscript numeric_primitives.vs

int width(float value)
{
	return 32;
}

int width(double value)
{
	return 64;
}

void main()
{
	int8 signedByte = -128;
	uint8 byte = 255u;
	int16 signedWord = -32768;
	uint16 word = 65535u;
	int32 signedValue = 0x0101;
	uint32 unsignedValue = 1u;
	int64 signedWide = 5000000000L;
	uint64 unsignedWide = 5000000001UL;
	float single = .5f;
	double precise = 2.05;

	print("8-bit: {}, {}", signedByte, byte);
	print("16-bit: {}, {}", signedWord, word);
	print("32-bit: {}, {}", signedValue, unsignedValue);
	print("64-bit: {}, {}", signedWide, unsignedWide);
	print("floating overloads: {}, {}", width(single), width(precise));

	// binary32 rounds here; the unsuffixed binary64 literal remains distinct.
	print(16777217.f == 16777216.f);
	print(16777217.0 == 16777216.0);
}
