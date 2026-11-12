// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	enum class EExternalReferenceKind : std::uint8_t
	{
		ScriptObject = 0,
		HostObject,
	};

	struct FExternalFieldView
	{
		std::string_view Name;
		// Source-facing type identity; stable for host reflection.
		std::string_view TypeName;
		// VM storage identity after transparent alias expansion.
		std::string_view RuntimeTypeName;
		std::uint16_t FieldIndex = 0;
		const FValue* Value = nullptr;
	};

	struct FExternalObjectView
	{
		std::string_view TypeName;
		std::uint64_t MutationVersion = 0;
		std::vector<FExternalFieldView> Fields;
	};

	struct FExternalFieldAssignment
	{
		std::string Name;
		FValue Value;
	};

	// FExternalSerializerDesc stores these callback types by value.
	using FExternalSerializeCallback = std::function<bool(
		const FExternalObjectView& Object,
		std::vector<std::uint8_t>& OutBytes,
		std::string& OutError)>;
	using FExternalDeserializeCallback = std::function<bool(
		const std::uint8_t* Bytes,
		std::size_t ByteCount,
		std::uint32_t EncodedVersion,
		std::vector<FExternalFieldAssignment>& OutFields,
		std::string& OutError)>;

	struct FExternalSerializerDesc
	{
		std::uint32_t Version = 1;
		FExternalSerializeCallback Serialize;
		FExternalDeserializeCallback Deserialize;
	};

	struct FExternalObjectReference
	{
		EExternalReferenceKind Kind = EExternalReferenceKind::ScriptObject;
		FObjectHandle ScriptObject;
		FHostObjectHandle HostObject;
	};

	using FExternalReferenceVisitor =
		std::function<void(const FExternalObjectReference&)>;
}
