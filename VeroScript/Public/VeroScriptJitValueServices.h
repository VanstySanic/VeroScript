// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitFrameServices.h"

namespace VeroScript
{
	inline constexpr std::uint64_t JitValueServiceCapability = 1u << 1u;

	/**
	 * Language value operations with the same ownership and mutation rules as
	 * their bytecode instructions. Loads share roots; CloneStruct is explicit.
	 * Outputs replace and release the previous value through the core runtime.
	 * Operations may invoke script destructors or host callbacks and invalidate
	 * all outstanding borrowed storage. A failed operation is never replayed.
	 */
	class IJitValueServices
	{
	public:
		virtual ~IJitValueServices() = default;

		// Checked generation/layout views. A view has the same borrow lifetime as
		// scalar local storage. Object remains rooted by the caller; no ownership
		// is reconstructed from a raw pointer. An incompatible shape declines.
		virtual bool ResolveObjectLayout(const FScriptObject& Object, std::uint16_t& OutTypeIndex) noexcept = 0;

		virtual bool BorrowObjectField(FScriptObject& Object, std::uint16_t NameIndex,
			std::uint16_t& OutFieldIndex, FValue*& OutStorage) noexcept = 0;

		// Raw slots remain incomplete until the selected constructor succeeds.
		virtual bool CreateObject(std::uint16_t TypeIndex, FValue& OutObject,
			bool bUninitialized = false) noexcept = 0;

		virtual bool CreateContainer(EOpcode Operation, std::uint16_t TypeNameIndex, FValue& OutContainer) noexcept = 0;

		virtual bool CloneStruct(FValue& Value) noexcept = 0;

		virtual bool LoadField(const FValue& Object, std::uint16_t NameIndex, FValue& OutValue) noexcept = 0;

		virtual bool StoreField(const FValue& Object, std::uint16_t NameIndex, const FValue& Value) noexcept = 0;

		// Construction initialization preserves field origins without assignment notifications.
		virtual bool InitializeField(const FValue& Object, std::uint16_t NameIndex, const FValue& Value) noexcept = 0;

		virtual bool IncrementField(const FValue& Object, std::uint16_t NameIndex) noexcept = 0;

		virtual bool MakeFieldReference(const FValue& Object, std::uint16_t NameIndex, FValue& OutReference) noexcept = 0;

		virtual bool GetIndex(const FValue& Container, const FValue& Index, FValue& OutValue) noexcept = 0;

		virtual bool SetIndex(const FValue& Container, const FValue& Index, const FValue& Value, FValue& OutContainer) noexcept = 0;

		virtual bool MakeIndexReference(FJitFrameToken Frame, std::uint16_t LocalSlot,
			const FValue& Index, FValue& OutReference, bool bBorrow = true) noexcept = 0;

		virtual bool MakeMapEntryReference(FJitFrameToken Frame, std::uint16_t LocalSlot,
			const FValue& Key, FValue& OutReference) noexcept = 0;
	};
}
