// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Run: veroscript standard_library_foundation.vs
import "core.vs";

int twice(int value)
{
	return value * 2;
}

int main()
{
	int number = 7;
	assert(!std::strings::try_parse("+-12", ref number));
	assert(number == 7); // Ordinary parse failure preserves the output.
	assert(std::strings::try_parse("ff", ref number, 16));
	print("parsed: {}, hex64: {:x}", number, -1i64);

	auto bytes = std::strings::encode_utf8("VeroScript");
	assert(std::strings::is_valid_utf8(bytes));
	assert(std::strings::decode_utf8(bytes.readonly_slice(0, bytes.size())) == "VeroScript");

	print("path: {}", std::filesystem::lexically_normal("C:/assets/./ui/../", std::filesystem::path_style::windows));
	array<int> values = {2147483647, 1};
	print("wide sum: {}", std::numeric::sum_as<int64>(values));
	assert(std::clamp<uint64>(9u64, 0u64, 8u64) == 8u64);

	auto cell = std::concurrent::make_synchronized<int>(10);
	assert(std::concurrent::synchronized_fetch_add(cell, 2) == 10);
	assert(std::concurrent::synchronized_load(cell) == 12);

	task_group<int> work = task_group<int>();
	work.add(std::concurrent::spawn_task<int>(twice, 3));
	work.add(std::concurrent::spawn_task<int>(twice, 4));
	array<int> results = work.await_all();
	assert(results[0] == 6 && results[1] == 8 && work.size() == 0);
	print("standard library foundation example passed");
	return 0;
}
