// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkStream.h"

namespace VeroScript::Network::Detail
{
	// Provider hooks only enqueue work on the owning executor; never execute script.
	struct FStreamAccess
	{
		static bool CreatePipe(const std::shared_ptr<FContext>& Context, std::uint64_t Capacity,
			FBytePipe& Out, FError& Error, std::shared_ptr<const void> Owner);
		static void OnWritable(FWritableStream&, std::function<void()>);
		static void OnReadable(FReadableStream&, std::function<void()>);
		static void OnCancel(FReadableStream&, std::function<void()>);
		static bool ClaimReader(FReadableStream&);
	};
} // namespace VeroScript::Network::Detail
