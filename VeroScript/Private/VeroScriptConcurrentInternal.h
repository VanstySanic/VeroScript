// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript::Concurrent::Detail
{
	// Owner teardown is independent of the worker's cooperative cancel token.
	bool IsWorkerShutdownRequested() noexcept;
}
