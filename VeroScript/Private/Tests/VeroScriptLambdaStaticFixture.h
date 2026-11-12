// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript::Test
{
	inline constexpr char LambdaStaticDefinitions[] = R"vs(
fn()->int lambdaStateCounter(int seed)
{
	return [count = seed]() mutable -> int { return ++count; };
}
fn()->int lambdaSharedStatic(int seed)
{
	return [seed]() -> int { static int count = seed; return ++count; };
}
int lambdaStaticEffects = 0;
int lambdaCaptureSeed() { return ++lambdaStaticEffects; }
class LambdaStaticOwner
{
public:
	int VsLocalStatic() { return 0; }
	int next(int seed) { static int count = seed; return ++count; }
};
int lambdaTypedStatic<T>(T seed)
{
	static int count = 0;
	return ++count;
}
void checkLambdaStaticSupport()
{
	auto first = lambdaStateCounter(0);
	auto alias = first;
	auto other = lambdaStateCounter(0);
	assert(first() == 1 && alias() == 2 && other() == 1);
	const int source = 9;
	auto initialized = [copy = source]() mutable -> int { return ++copy; };
	assert(initialized() == 10 && initialized() == 11 && source == 9);
	int outer = 7;
	auto renamed = [outer = outer + 1, second = outer]() -> int { return outer + second; };
	assert(renamed() == 15 && outer == 7);
	auto mixedDefault = [=, copy = outer]() mutable -> int { return ++copy; };
	assert(mixedDefault() == 8 && mixedDefault() == 9);
	int ___vs_initialized_capture_0 = 7;
	auto collision = [copy = ___vs_initialized_capture_0]() -> int { return copy; };
	assert(collision() == 7);
	auto unused = [left = lambdaCaptureSeed(), right = lambdaCaptureSeed()]() -> int { return 0; };
	assert(lambdaStaticEffects == 2 && unused() == 0 && lambdaStaticEffects == 2);
	auto a = lambdaSharedStatic(20);
	auto b = lambdaSharedStatic(30);
	assert(a() == 21 && b() == 22 && a() == 23);
	LambdaStaticOwner owner = LambdaStaticOwner();
	LambdaStaticOwner otherOwner = LambdaStaticOwner();
	assert(owner.next(50) == 51 && otherOwner.next(90) == 52);
	assert(lambdaTypedStatic<int>(4) == 1 && lambdaTypedStatic<int>(8) == 2);
	assert(lambdaTypedStatic<int64>(9i64) == 1 && lambdaTypedStatic<int64>(7i64) == 2);
	for (static int count = 0; count < 3; ++count) {}
	static const int answer = 42;
	assert(answer == 42);
}
)vs";
}
