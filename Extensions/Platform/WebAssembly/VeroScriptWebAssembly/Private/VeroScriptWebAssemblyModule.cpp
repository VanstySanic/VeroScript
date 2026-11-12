// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <new>
#include <stdexcept>

#if VERO_WEBASSEMBLY_INTERACTIVE_MODULE
EM_ASYNC_JS(char*, VeroWebAwaitInputLineAsync, (void* Runtime, int* Failure), {
	const provider = Module.veroWebInputProvider;
	if (typeof provider !== "function")
	{
		return 0;
	}
	let line;
	try
	{
		line = await provider(Runtime);
	}
	catch
	{
		// SDK failures stop execution after resuming the native stack. Raw
		// providers retain the legacy EOF fallback. Protect property inspection
		// too: a raw provider may be a proxy with a throwing property trap.
		try
		{
			if (provider.__veroWebAbortOnInputError === true)
			{
				HEAPU32[Failure >>> 2] = 3;
			}
		}
		catch
		{
		}
		return 0;
	}
	if (line === null)
	{
		return 0;
	}
	// The public TypeScript wrapper rejects invalid provider results before they
	// reach this bridge. Raw module consumers still must not be allowed to turn
	// a bad value or allocation failure into a write through address zero.
	if (typeof line !== "string" || line.includes("\0"))
	{
		return 0;
	}
	const byteLength = lengthBytesUTF8(line);
	if (!Number.isSafeInteger(byteLength) || byteLength < 0 || byteLength >= 0xffffffff)
	{
		HEAPU32[Failure >>> 2] = 1;
		return 0;
	}
	const size = byteLength + 1;
	const result = _malloc(size);
	if (!result)
	{
		HEAPU32[Failure >>> 2] = 2;
		return 0;
	}
	stringToUTF8(line, result, size);
	return result;
});

extern "C" char* VeroWebAwaitInputLine(void* Runtime)
{
	// Resume through the native stack before reporting an input failure.
	// Normal EOF and invalid raw providers retain their existing null result.
	int Failure = 0;
	char* const Input = VeroWebAwaitInputLineAsync(Runtime, &Failure);
	if (Failure == 1)
	{
		throw std::length_error("interactive input exceeds the Wasm32 allocation range");
	}
	if (Failure == 2)
	{
		throw std::bad_alloc();
	}
	if (Failure == 3)
	{
		throw std::runtime_error("interactive input provider failed");
	}
	return Input;
}
#else
extern "C" char* VeroWebAwaitInputLine(void*)
{
	return nullptr;
}
#endif
#endif

// Emscripten links this no-entry executable to produce the ES module and Wasm
// pair. The exported ABI itself is implemented by the VeroScriptWebAssembly library.
namespace VeroScript::WebAssembly
{
	void LinkModuleAnchor()
	{
	}
}
