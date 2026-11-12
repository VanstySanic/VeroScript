// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Public replacement bodies are expanded in the importing module.
#define LOCAL_ONLY 7
#define_export PUBLIC_VALUE 21
#define_export TWICE(x)((x) * 2)
#define_export TRACE(fmt, ...) print(fmt __VA_OPT__(, ) __VA_ARGS__)
export int module_value()
{
	return LOCAL_ONLY;
}
