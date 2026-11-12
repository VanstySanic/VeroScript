// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"
#include <jni.h>

#ifdef __cplusplus
extern "C"
{
#endif
	/* Called by application native bootstrap code with generated Core-owned
 * programs. Returns VeroScriptEngine.AotPrograms, an immutable, owned catalog.
 * Java receives catalog objects and stable IDs, never entry-point addresses. */
	JNIEXPORT jobject JNICALL veroscript_android_create_aot_programs(JNIEnv* env,
	    const vs_interop_string_view* ids, const vs_interop_aot_program* const* programs, uint64_t count);
#ifdef __cplusplus
}
#endif
