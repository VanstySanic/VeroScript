// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

#include <cstdint>
#include <string_view>

namespace VeroScript::Builtins
{
	enum class EReflectionQuery : std::int32_t
	{
		TypeKind = 0,
		TypeIsAbstract,
		TypeBaseCount,
		TypeBaseName,
		TypeFieldCount,
		TypeMethodCount,
		TypeMetadataCount,
		TypeMetadataKey,
		TypeMetadataValue,
		TypeEnumMemberCount,
		TypeEnumMemberName,
		TypeEnumMemberValue,
		TypeEnumUnderlyingType,
		TypeEnumIsScoped,
		TypeHasMetadata = 14,
		TypeMetadataByKey,
		TypeHasAnnotation,

		FieldName = 20,
		FieldDeclaredType,
		FieldRuntimeType,
		FieldIsStatic,
		FieldIsDeclaredOnType,
		FieldMetadataCount,
		FieldMetadataKey,
		FieldMetadataValue,
		FieldHasMetadata,
		FieldMetadataByKey,
		FieldHasAnnotation,
		FieldIsMutable,
		FieldIsConstexpr,

		MethodName = 40,
		MethodReturnType,
		MethodParameterCount,
		MethodParameterType,
		MethodParameterName,
		MethodParameterIsRef,
		MethodParameterIsConst,
		MethodIsStatic,
		MethodIsVirtual,
		MethodIsAbstract,
		MethodIsConst,
		MethodIsDeleted,
		MethodIsConstructor,
		MethodIsDestructor,
		MethodMetadataCount,
		MethodMetadataKey,
		MethodMetadataValue,
		MethodHasMetadata,
		MethodMetadataByKey,
		MethodHasAnnotation,
		MethodIsDefaulted,
		MethodIsAvailable,
		MethodSpecialMemberKind,
		MethodUnavailableReason,
		MethodIsConstexpr,

		ModulePath = 70,
		ModuleFunctionCount,
		ModuleGlobalCount,
		ModuleTypeCount,
		ModuleTypeName,

		FunctionName = 80,
		FunctionLinkageName,
		FunctionModulePath,
		FunctionReturnType,
		FunctionParameterCount,
		FunctionParameterType,
		FunctionParameterName,
		FunctionParameterIsRef,
		FunctionParameterIsConst,
		FunctionIsDeleted,
		FunctionIsExported,
		FunctionMetadataCount,
		FunctionMetadataKey,
		FunctionMetadataValue,
		FunctionHasMetadata,
		FunctionMetadataByKey,
		FunctionHasAnnotation,
		FunctionIsConstexpr,

		GlobalName = 110,
		GlobalLinkageName,
		GlobalModulePath,
		GlobalDeclaredType,
		GlobalRuntimeType,
		GlobalIsConst,
		GlobalIsExported,
		GlobalMetadataCount,
		GlobalMetadataKey,
		GlobalMetadataValue,
		GlobalHasMetadata,
		GlobalMetadataByKey,
		GlobalHasAnnotation,
		GlobalIsConstexpr,

		EnumMemberName = 130,
		EnumMemberValue,
		EnumMemberMetadataCount,
		EnumMemberMetadataKey,
		EnumMemberMetadataValue,
		EnumMemberHasMetadata,
		EnumMemberMetadataByKey,
		EnumMemberHasAnnotation,

		TypeDisplayName = 150,
		TypeIsNested,
		TypeIsLocal,
		TypeDeclaringType,
		TypeDeclaringFunction,
		TypeAccess,
		TypeNestedCount,
		TypeNestedName,
		TypeNestedParameterCount,
		TypeNestedDeclaration,
	};

	struct FDescriptor
	{
		constexpr FDescriptor(
			const std::string_view InName,
			const EBuiltinId InId,
			const std::uint8_t InArgCount,
			const bool bInPushVoidResult)
			: Name(InName)
			, Id(InId)
			, ArgCount(InArgCount)
			, MaxArgCount(InArgCount)
			, bPushVoidResult(bInPushVoidResult)
		{
		}

		constexpr FDescriptor(
			const std::string_view InName,
			const EBuiltinId InId,
			const std::uint8_t InMinArgCount,
			const std::uint8_t InMaxArgCount,
			const bool bInPushVoidResult)
			: Name(InName)
			, Id(InId)
			, ArgCount(InMinArgCount)
			, MaxArgCount(InMaxArgCount)
			, bPushVoidResult(bInPushVoidResult)
		{
		}

		constexpr bool AcceptsArgCount(const std::uint8_t Count) const
		{
			return Count >= ArgCount && Count <= MaxArgCount;
		}

		std::string_view Name;
		EBuiltinId Id;
		// Minimum argument count. MaxArgCount is equal for fixed-arity builtins.
		std::uint8_t ArgCount;
		std::uint8_t MaxArgCount;
		bool bPushVoidResult;
	};

	const FDescriptor* Lookup(std::string_view Name);
	const FDescriptor* LookupById(EBuiltinId Id);
}
