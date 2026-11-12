// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstdint>
#include <functional>
#include <string>

namespace VeroScript
{
	enum class EMutationKind : std::uint8_t
	{
		FieldWrite = 0,
		ContainerAdd,
		ContainerRemove,
		ContainerSet,
		ContainerClear,
	};

	/**
	 * Synchronous, non-owning mutation notification. Object/Value are valid only
	 * for the callback. FieldName + ObjectTypeName are the stable identity used to
	 * remap across reload; FieldIndex is only a fast generation-local hint.
	 * An isolated worker never follows a shared channel's field origin into the
	 * object graph of another VM thread; communicate that work through the channel
	 * or task result and apply host-visible state on the owning VM thread.
	 */
	struct FMutationEvent
	{
		FScriptObject* Object = nullptr;
		std::string ObjectTypeName;
		std::string FieldName;
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		EMutationKind Kind = EMutationKind::FieldWrite;
		const FValue* Value = nullptr;
		std::uint64_t ObjectMutationVersion = 0;
	};

	// May be invoked concurrently by isolated worker VMs; see VeroScriptVm.
	using FMutationCallback = std::function<void(const FMutationEvent&)>;
}
