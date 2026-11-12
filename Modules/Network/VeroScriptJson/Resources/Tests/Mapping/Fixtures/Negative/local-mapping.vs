// expect: Automatic JSON mapping requires a module- or namespace-scope type
// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main()
{
	@JsonFactory(type="Factory") struct Local { int value=7; };
	return 0;
}
