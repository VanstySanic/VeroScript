// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// integer_aliases.vs — fixed-width integer spellings and enum underlying types
// Run from this directory: veroscript integer_aliases.vs

enum class ByteState : uint8
{
	Off = 0u,
	On = 1u,
	Max = 255u
};

enum class SignedLimit : int16
{
	Min = -32768,
	Max = 32767
};

int8 first(array<int8> values)
{
	return values[0];
}

void main()
{
	int8 signedByte = -128;
	uint8 byte = 255u;
	int16 signedWord = -32768;
	uint16 word = 65535u;
	int32 signedValue = 100000;
	uint32 unsignedValue = 4000000000u;
	int64 signedWide = 9223372036854775807i64;
	uint64 unsignedWide = 18446744073709551615u64;

	array<int8> bytes = array<int8>{signedByte, 127};
	map<ByteState, string> stateNames = map<ByteState, string>();
	stateNames[ByteState::Off] = "off";
	stateNames[ByteState::On] = "on";
	set<ByteState> visited = set<ByteState>();
	visited.add(ByteState::On);
	visited.add(ByteState::On);

	print("8-bit: {}, {}", first(bytes), byte);
	print("16-bit: {}, {}", signedWord, word);
	print("32-bit: {}, {}", signedValue, unsignedValue);
	print("64-bit: {}, {}", signedWide, unsignedWide);
	print(ByteState::Max);
	print(SignedLimit::Max);
	print("enum key: {}, visited={}", stateNames[ByteState::On], len(visited));
}
