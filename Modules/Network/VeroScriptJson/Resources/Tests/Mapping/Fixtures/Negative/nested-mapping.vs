// expect: Automatic JSON mapping requires a module- or namespace-scope type
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct Outer
{
	@JsonConverter(type="Adapter") struct Item { int value=7; };
};
int main() { return 0; }
