// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnvironment.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	bool Expect(const bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Condition;
	}
}

int main()
{
	using namespace VeroScript::Environment;

	// Escaped UTF-8 keeps this test valid in both C++17 and C++20 without
	// depending on the char8_t type of u8 string literals.
	const std::string Name = "VERO_\xE8\x84\x9A\xE6\x9C\xAC_\xE7\x8E\xAF\xE5\xA2\x83";
	const std::string Value = "\xE7\xBC\x93\xE5\xAD\x98/\xE6\xB5\x8B\xE8\xAF\x95";

	if (!Expect(UnsetUtf8(Name), "initial unset is idempotent")
		|| !Expect(!GetUtf8(Name).has_value(), "unset variable is absent")
		|| !Expect(SetUtf8(Name, Value), "set UTF-8 name and value"))
	{
		return EXIT_FAILURE;
	}

	const auto ReadValue = GetUtf8(Name);
	if (!Expect(ReadValue.has_value() && *ReadValue == Value, "UTF-8 roundtrip")
		|| !Expect(SetUtf8(Name, ""), "set explicit empty value"))
	{
		UnsetUtf8(Name);
		return EXIT_FAILURE;
	}

	const auto EmptyValue = GetUtf8(Name);
	if (!Expect(EmptyValue.has_value() && EmptyValue->empty(), "empty differs from absent")
		|| !Expect(UnsetUtf8(Name), "unset UTF-8 variable")
		|| !Expect(UnsetUtf8(Name), "repeated unset is idempotent")
		|| !Expect(!GetUtf8(Name).has_value(), "variable remains absent")
		|| !Expect(!SetUtf8("", Value), "reject empty name")
		|| !Expect(!SetUtf8("BAD=NAME", Value), "reject equals in name"))
	{
		return EXIT_FAILURE;
	}

	std::atomic<bool> ConcurrentOk{true};
	std::vector<std::thread> Workers;
	for (int Worker = 0; Worker < 8; ++Worker)
	{
		Workers.emplace_back([Worker, &ConcurrentOk, &Value]()
		{
			const std::string WorkerName =
				"VERO_SCRIPT_ENV_CONCURRENT_" + std::to_string(Worker);
			for (int Iteration = 0; Iteration < 200; ++Iteration)
			{
				if (!SetUtf8(WorkerName, Value))
				{
					ConcurrentOk.store(false, std::memory_order_relaxed);
					break;
				}
				const auto Current = GetUtf8(WorkerName);
				if (!Current || *Current != Value)
				{
					ConcurrentOk.store(false, std::memory_order_relaxed);
					break;
				}
			}
			if (!UnsetUtf8(WorkerName))
			{
				ConcurrentOk.store(false, std::memory_order_relaxed);
			}
		});
	}
	for (std::thread& Worker : Workers)
	{
		Worker.join();
	}
	if (!Expect(ConcurrentOk.load(std::memory_order_relaxed), "concurrent environment access"))
	{
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
