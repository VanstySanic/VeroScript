// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// preprocessor.vs — object-like/function-like macros, __VA_ARGS__, __VA_OPT__, #/##
// Run from this directory: veroscript preprocessor.vs
// See also: language-basics/preprocessor.md in the VeroScript documentation.

#define VERSION 2
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STR(x) #x
#define CAT(a, b) a##b
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)
#if VERSION >= 2
#define max MAX
#endif
void main()
{
	print("source = {}, line = {}", __FILE__, __LINE__);
	print("VERSION = {}", VERSION);
	print("MAX(3, 8) = {}", MAX(3, 8));
	print("alias max(1, 4) = {}", max(1, 4));
	print("STR(hello) = {}", STR(hello));

	int VALUE = 9;
	print("CAT(VAL, UE) = {}", CAT(VAL, UE));

	LOG("hi");
	LOG("n={}", 42);
}
