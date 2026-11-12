// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	std::string VeroScriptCompiler::ResolveOwnerModulePath(const std::string& OwnerTypeName) const
	{
		if (!ActiveProgram)
		{
			return {};
		}
		std::string OwnerLinkageName = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (OwnerLinkageName.empty() && SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
		{
			OwnerLinkageName = ResolveTypeLinkageName(GenericBase, CurrentModulePath);
		}
		if (OwnerLinkageName.empty())
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerTypeName);
			if (!TemplateBase.empty())
			{
				OwnerLinkageName = TemplateBase.find('\x1F') != std::string::npos
				    ? TemplateBase
				    : ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
			}
		}
		if (OwnerLinkageName.empty())
		{
			// Resolved inherited owners may not be directly imported by the
			// caller. Their linkage still identifies the declaration module.
			OwnerLinkageName = OwnerTypeName;
		}
		const std::string TemplateBase = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(OwnerLinkageName);
			return Base.empty() ? OwnerLinkageName : Base;
		}();
		const std::size_t WantedArity = TypeParamArityFromLinkage(
		    FindGenericTypeMarker(OwnerTypeName) != std::string::npos
		        ? OwnerTypeName
		        : OwnerLinkageName);
		for (const auto Index : FindDeclarationCandidates(SourceStructIndices, TemplateBase))
		{
			const FStructDecl& StructDecl = ActiveProgram->Structs[Index];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == TemplateBase && TemplateParameterNames(StructDecl).size() == WantedArity)
			{
				return StructDecl.ModulePath;
			}
		}
		for (const auto Index : FindDeclarationCandidates(SourceClassIndices, TemplateBase))
		{
			const FClassDecl& ClassDecl = ActiveProgram->Classes[Index];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == TemplateBase && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return ClassDecl.ModulePath;
			}
		}
		for (const auto Index : FindDeclarationCandidates(SourceInterfaceIndices, TemplateBase))
		{
			const FInterfaceDecl& InterfaceDecl = ActiveProgram->Interfaces[Index];
			if (GetSymbolLinkageName(
			        InterfaceDecl.ModulePath,
			        InterfaceDecl.bExported,
			        InterfaceDecl.Name) == TemplateBase &&
			    TemplateParameterNames(InterfaceDecl).size() == WantedArity)
			{
				return InterfaceDecl.ModulePath;
			}
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == TemplateBase && TemplateParameterNames(StructDecl).size() == WantedArity)
			{
				return StructDecl.ModulePath;
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == TemplateBase && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return ClassDecl.ModulePath;
			}
		}
		return {};
	}

	bool VeroScriptCompiler::ValidateDynamicArrayAssignment(
	    const std::string& TargetTypeName,
	    const FExpr& ValueExpr,
	    const FSourceLocation& Location)
	{
		if (Runtime::IsDynamicArrayTypeName(TargetTypeName))
		{
			std::string SourceTypeName;
			if (!TryResolveExprTypeName(ValueExpr, SourceTypeName))
			{
				ReportErrorId("diag.cannot_convert_expression_of_unknown_type_to", Location, {TargetTypeName});
				return false;
			}
			const std::string QualifiedTarget = CanonicalTypeNameForCoercion(TargetTypeName);
			const std::string QualifiedSource = CanonicalTypeNameForCoercion(SourceTypeName);
			if (!Runtime::IsDynamicArrayTypeName(SourceTypeName) || QualifiedSource != QualifiedTarget)
			{
				ReportErrorId("diag.dynamic_array_type_mismatch_expected_got", Location, {TargetTypeName, SourceTypeName});
				return false;
			}
			return true;
		}

		if (Runtime::IsCommonContainerTypeName(TargetTypeName))
		{
			std::string SourceTypeName;
			if (!TryResolveExprTypeName(ValueExpr, SourceTypeName))
			{
				ReportErrorId("diag.cannot_convert_expression_of_unknown_type_to", Location, {TargetTypeName});
				return false;
			}
			const std::string QualifiedTarget = CanonicalTypeNameForCoercion(TargetTypeName);
			const std::string QualifiedSource = CanonicalTypeNameForCoercion(SourceTypeName);
			if (!Runtime::IsCommonContainerTypeName(SourceTypeName) || QualifiedSource != QualifiedTarget)
			{
				ReportErrorId("diag.container_type_mismatch_expected_got", Location, {TargetTypeName, SourceTypeName});
				return false;
			}
			return true;
		}

		if (Runtime::IsNativeArrayTypeName(TargetTypeName))
		{
			std::string SourceTypeName;
			if (!TryResolveExprTypeName(ValueExpr, SourceTypeName))
			{
				ReportErrorId("diag.cannot_convert_expression_of_unknown_type_to", Location, {TargetTypeName});
				return false;
			}
			const std::string QualifiedTarget = CanonicalTypeNameForCoercion(TargetTypeName);
			const std::string QualifiedSource = CanonicalTypeNameForCoercion(SourceTypeName);
			if (!Runtime::IsNativeArrayTypeName(SourceTypeName) || QualifiedSource != QualifiedTarget)
			{
				ReportErrorId("diag.native_array_type_mismatch_expected_got", Location, {TargetTypeName, SourceTypeName});
				return false;
			}
		}

		return true;
	}

	bool VeroScriptCompiler::EmitValueCoercionForTarget(
	    FEmitter& Emitter,
	    const std::string& TargetTypeName,
	    const FExpr& SourceExpr,
	    const FSourceLocation& Location)
	{
		const auto IsVoidSource = [&]() -> bool
		{
			std::string SourceType;
			return TryResolveExprTypeName(SourceExpr, SourceType) && SourceType == "void";
		};

		if (IsVoidSource())
		{
			if (TargetTypeName == "void")
			{
				return true;
			}
			const std::string DisplayTarget =
			    (TargetTypeName.empty() || TargetTypeName == "auto") ? "auto" : TargetTypeName;
			ReportErrorId("diag.cannot_convert_void_to", Location, {DisplayTypeName(DisplayTarget)});
			return false;
		}

		if (TargetTypeName.empty() || TargetTypeName == "auto")
		{
			return true;
		}

		const std::string EffectiveTarget = NormalizeTypeAlias(TargetTypeName);
		// An untyped list is materialized directly in its selected parameter type.
		// Keep it untyped during overload selection; an outer result type must not
		// choose a different parameter overload for the list.
		if (SourceExpr.Kind == EExprKind::ArrayInit &&
		    static_cast<const FArrayInitExpr&>(SourceExpr).TypeName.empty() &&
		    EffectiveTarget == NormalizeTypeAlias(SubstituteTypeName(ExpressionExpectedType)) &&
		    (Runtime::IsDynamicArrayTypeName(EffectiveTarget) || Runtime::IsNativeArrayTypeName(EffectiveTarget) ||
		        Runtime::IsSetTypeName(EffectiveTarget) || Runtime::IsMultiSetTypeName(EffectiveTarget) ||
		        Runtime::IsOrderedMultiSetTypeName(EffectiveTarget) || Runtime::IsDequeTypeName(EffectiveTarget) ||
		        Runtime::IsQueueTypeName(EffectiveTarget) || Runtime::IsStackTypeName(EffectiveTarget)))
		{
			return true;
		}

		const bool bScalarTarget = EffectiveTarget == "string" || EffectiveTarget == "wstring" || EffectiveTarget == "bool" || EffectiveTarget == "float" || EffectiveTarget == "double" || Runtime::IsUnsignedTypeName(EffectiveTarget) || IsSignedIntegerTypeName(EffectiveTarget);

		std::string SourceTypeName;
		if (!TryResolveExprTypeName(SourceExpr, SourceTypeName))
		{
			ReportErrorId("diag.cannot_convert_expression_of_unknown_type_to", Location, {DisplayTypeName(EffectiveTarget)});
			return false;
		}

		if (SourceTypeName == "nullptr" && bScalarTarget)
		{
			ReportErrorId("diag.cannot_convert_nullptr_to", Location, {DisplayTypeName(EffectiveTarget)});
			return false;
		}
		if (IsInterfaceTypeName(EffectiveTarget) && IsStructType(SourceTypeName) && IsSubtypeOf(SourceTypeName, EffectiveTarget) && CanTransferCoercedValue(SourceExpr, SourceTypeName))
		{
			return true;
		}

		return EmitValueCoercionBetweenTypes(Emitter, EffectiveTarget, SourceTypeName, Location,
		    false, SourceTypeName != EffectiveTarget && TypeSupportsOperatorOverload(SourceTypeName) && IsConstAssignTarget(SourceExpr));
	}

	bool VeroScriptCompiler::EmitValueCoercionBetweenTypes(
	    FEmitter& Emitter,
	    const std::string& TargetTypeName,
	    const std::string& SourceTypeName,
	    const FSourceLocation& Location,
	    const bool bAllowExplicitConstructor,
	    const bool bSourceIsConst)
	{
		if (TargetTypeName.empty() || TargetTypeName == "auto" || SourceTypeName.empty())
		{
			return true;
		}
		if (SourceTypeName == TargetTypeName)
		{
			return true;
		}
		if (TargetTypeName == "variant")
		{
			const auto Source = CanonicalVariantTypeName(SourceTypeName);
			if (Source == "nullptr")
			{
				ReportErrorId("diag.variant_requires_concrete_null_type", Location);
				return false;
			}
			if (Source == "void" || Runtime::IsReferenceTypeName(Source))
			{
				ReportError("variant boxing requires a concrete value", Location);
				return false;
			}
			if (Source != "variant")
			{
				Emitter.EmitU16Operand(EOpcode::BoxVariant, AddString(Source));
			}
			return true;
		}
		if (SourceTypeName == "variant")
		{
			ReportError("variant requires explicit cast or try_cast", Location);
			return false;
		}

		const auto TryEmitConversionOperator = [&]() -> bool
		{
			const std::string NormalizedTarget = NormalizeTypeAlias(TargetTypeName);
			const std::string QualifiedTarget = CanonicalTypeNameForCoercion(NormalizedTarget);
			const std::string QualifiedSource =
			    CanonicalTypeNameForCoercion(NormalizeTypeAlias(SourceTypeName));
			if (!(TypeSupportsOperatorOverload(SourceTypeName) || TypeSupportsOperatorOverload(QualifiedSource)))
			{
				return false;
			}
			const std::string& ConvSource =
			    TypeSupportsOperatorOverload(SourceTypeName) ? SourceTypeName : QualifiedSource;
			const std::string ConvCandidates[] = {
			    ConversionOperatorMethodName(NormalizedTarget),
			    ConversionOperatorMethodName(QualifiedTarget),
			    ConversionOperatorMethodName(TargetTypeName),
			};
			std::unordered_set<std::string> Tried;
			for (const std::string& ConvName : ConvCandidates)
			{
				if (!Tried.insert(ConvName).second)
				{
					continue;
				}
				std::string OwnerLinkage;
				const FMethodDecl* MethodDecl = nullptr;
				bool bAmbiguous = false;
				std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
				if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
				        ConvSource, ConvName, {}, bAmbiguous, &HostNativeIndex, bSourceIsConst))
				{
					return EmitHostOperatorCall(
					    Emitter, *HostOperator, HostNativeIndex, {}, Location);
				}
				if (bAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {ConvName});
					return false;
				}
				if (ResolveMethodForCall(
				        ConvSource,
				        ConvName,
				        {},
				        OwnerLinkage,
				        MethodDecl,
				        Location,
				        false,
				        false,
				        &bAmbiguous, bSourceIsConst) &&
				    MethodDecl)
				{
					return EmitMemberOperatorCall(
					    Emitter,
					    ConvSource,
					    MethodDecl->Name,
					    *MethodDecl,
					    OwnerLinkage,
					    0,
					    Location);
				}
				if (bAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {ConvName});
					return false;
				}
				bool bFreeAmbiguous = false;
				const FFunctionDecl* FreeOp = nullptr;
				std::vector<std::string> GenericConcreteTypes;
				if (ResolveFreeOperatorOverload(
				        ConvName,
				        {ConvSource},
				        FreeOp,
				        GenericConcreteTypes,
				        &bFreeAmbiguous,
				        false))
				{
					const std::vector<std::string> ParamTypes =
					    SpecializedFreeFunctionParamTypes(*FreeOp, GenericConcreteTypes);
					if (!ParamTypes.empty())
					{
						const std::string ParamType = QualifyTypeName(
						    NormalizeTypeAlias(ParamTypes[0]),
						    FreeOp->ModulePath);
						if (!EmitValueCoercionBetweenTypes(
						        Emitter,
						        ParamType,
						        SourceTypeName,
						        Location, false, bSourceIsConst))
						{
							return false;
						}
					}
					const std::vector<std::string>* GenericArgs = GenericConcreteTypes.empty()
					    ? nullptr
					    : &GenericConcreteTypes;
					return EmitFreeOperatorCall(Emitter, *FreeOp, 1, Location, GenericArgs);
				}
				if (bFreeAmbiguous)
				{
					ReportErrorId("diag.ambiguous_call_to", Location, {ConvName});
					return false;
				}
			}
			return false;
		};

		if (Runtime::IsFunctionTypeName(TargetTypeName))
		{
			if (SourceTypeName == TargetTypeName || (Runtime::IsFunctionTypeName(TargetTypeName) && Runtime::IsFunctionTypeName(SourceTypeName) && CanonicalTypeNameForCoercion(NormalizeTypeAlias(SourceTypeName)) == CanonicalTypeNameForCoercion(NormalizeTypeAlias(TargetTypeName))))
			{
				return true;
			}
			if (!Runtime::IsFunctionTypeName(SourceTypeName) && TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId(
			    "diag.cannot_convert_to",
			    Location,
			    {DisplayTypeName(SourceTypeName), DisplayTypeName(TargetTypeName)});
			return false;
		}

		if (TargetTypeName == "wstring")
		{
			if (SourceTypeName == "wstring")
			{
				return true;
			}
			if (SourceTypeName == "string")
			{
				ReportError("cannot assign string to wstring without to_wstring", Location);
				return false;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to_wstring", Location, {DisplayTypeName(SourceTypeName)});
			return false;
		}
		if (TargetTypeName == "string")
		{
			if (SourceTypeName == "string")
			{
				return true;
			}
			if (SourceTypeName == "wstring")
			{
				ReportError("cannot assign wstring to string without to_string", Location);
				return false;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to_string", Location, {DisplayTypeName(SourceTypeName)});
			return false;
		}
		if (Runtime::IsUnsignedTypeName(TargetTypeName))
		{
			const auto TargetUnsignedConversion = [&]()
			{
				if (TargetTypeName == "unsigned char")
				{
					return EBuiltinId::ToUInt8;
				}
				if (TargetTypeName == "unsigned short")
				{
					return EBuiltinId::ToUInt16;
				}
				if (TargetTypeName == "unsigned long" || TargetTypeName == "uint64")
				{
					return EBuiltinId::ToUInt64;
				}
				return EBuiltinId::ToUnsigned;
			};
			if (Runtime::IsUnsignedTypeName(SourceTypeName))
			{
				Emitter.EmitCallBuiltin(TargetUnsignedConversion(), 1);
				return true;
			}
			if (IsSignedIntegerTypeName(SourceTypeName) || SourceTypeName == "float" || SourceTypeName == "double" || SourceTypeName == "bool")
			{
				Emitter.EmitCallBuiltin(TargetUnsignedConversion(), 1);
				return true;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to_without_to_unsigned", Location, {SourceTypeName, TargetTypeName});
			return false;
		}
		if (IsSignedIntegerTypeName(TargetTypeName))
		{
			const auto TargetSignedConversion = [&]()
			{
				if (TargetTypeName == "char")
				{
					return EBuiltinId::ToInt8;
				}
				if (TargetTypeName == "short")
				{
					return EBuiltinId::ToInt16;
				}
				if (TargetTypeName == "long" || TargetTypeName == "int64")
				{
					return EBuiltinId::ToInt64;
				}
				return EBuiltinId::ToInt;
			};
			if (IsSignedIntegerTypeName(SourceTypeName))
			{
				Emitter.EmitCallBuiltin(TargetSignedConversion(), 1);
				return true;
			}
			if (Runtime::IsUnsignedTypeName(SourceTypeName) || SourceTypeName == "float" || SourceTypeName == "double" || SourceTypeName == "bool")
			{
				Emitter.EmitCallBuiltin(TargetSignedConversion(), 1);
				return true;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to_without_to_int", Location, {SourceTypeName, TargetTypeName});
			return false;
		}
		if (TargetTypeName == "bool")
		{
			if (SourceTypeName == "bool")
			{
				return true;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to_bool", Location, {SourceTypeName});
			return false;
		}
		if (TargetTypeName == "float" || TargetTypeName == "double")
		{
			if (SourceTypeName == "float" || SourceTypeName == "double")
			{
				if (SourceTypeName != TargetTypeName)
				{
					Emitter.EmitCallBuiltin(
					    TargetTypeName == "float" ? EBuiltinId::ToFloat : EBuiltinId::ToDouble,
					    1);
				}
				return true;
			}
			if (IsSignedIntegerTypeName(SourceTypeName) || Runtime::IsUnsignedTypeName(SourceTypeName) || SourceTypeName == "bool")
			{
				Emitter.EmitCallBuiltin(
				    TargetTypeName == "float" ? EBuiltinId::ToFloat : EBuiltinId::ToDouble,
				    1);
				return true;
			}
			if (TryEmitConversionOperator())
			{
				return !HasError();
			}
			ReportErrorId("diag.cannot_convert_to", Location, {SourceTypeName, TargetTypeName});
			return false;
		}
		const std::string NormalizedTarget = NormalizeTypeAlias(TargetTypeName);
		const std::string NormalizedSource = NormalizeTypeAlias(SourceTypeName);
		if (NormalizedTarget == NormalizedSource)
		{
			return true;
		}
		const std::string QualifiedTarget = CanonicalTypeNameForCoercion(NormalizedTarget);
		const std::string QualifiedSource = CanonicalTypeNameForCoercion(NormalizedSource);
		if (QualifiedTarget == QualifiedSource)
		{
			return true;
		}
		// Struct → interface: require implementation, then box by cloning (handle to a copy).
		if (IsInterfaceTypeName(QualifiedTarget) && IsStructType(QualifiedSource))
		{
			if (!IsSubtypeOf(QualifiedSource, QualifiedTarget))
			{
				ReportErrorId("diag.cannot_convert_struct_to_interface_type_does_not_implement_it_to", Location, {DisplayTypeName(SourceTypeName), DisplayTypeName(TargetTypeName)});
				return false;
			}
			EmitValueCopy(Emitter, QualifiedSource);
			return true;
		}
		if (NormalizedSource == "nullptr")
		{
			const FHostTypeDesc* const HostTarget = ActiveBindings
			    ? ActiveBindings->FindType(QualifiedTarget)
			    : nullptr;
			if (HostTarget && HostTarget->Kind == EHostTypeKind::Class)
			{
				// Script and host class nulls use distinct FValue alternatives. Preserve
				// that ABI distinction whenever a context gives nullptr a host type.
				Emitter.EmitU16Operand(
				    EOpcode::CheckedCast, AddString(QualifiedTarget));
				return true;
			}
			// Class / interface handles only — structs remain value types.
			if (IsInterfaceTypeName(QualifiedTarget) || IsInterfaceTypeName(NormalizedTarget))
			{
				return true;
			}
			if ((!IsStructType(QualifiedTarget) && IsClassOrStructType(QualifiedTarget)) || (!IsStructType(NormalizedTarget) && IsClassOrStructType(NormalizedTarget)))
			{
				return true;
			}
			// A value wrapper may accept a nullable class/interface argument in its
			// ordinary converting constructor. Keep the original null on the stack
			// until that constructor parameter performs its standard conversion.
		}
		if (IsSubtypeOf(QualifiedSource, QualifiedTarget))
		{
			return true;
		}
		// Conversion operators: operator conv T on source (stack already holds the value).
		if (TypeSupportsOperatorOverload(SourceTypeName) || TypeSupportsOperatorOverload(QualifiedSource))
		{
			const std::string& ConvSource =
			    TypeSupportsOperatorOverload(SourceTypeName) ? SourceTypeName : QualifiedSource;
			const std::string ConvCandidates[] = {
			    ConversionOperatorMethodName(NormalizedTarget),
			    ConversionOperatorMethodName(QualifiedTarget),
			    ConversionOperatorMethodName(TargetTypeName),
			};
			std::unordered_set<std::string> Tried;
			for (const std::string& ConvName : ConvCandidates)
			{
				if (!Tried.insert(ConvName).second)
				{
					continue;
				}
				std::string OwnerLinkage;
				const FMethodDecl* MethodDecl = nullptr;
				bool bAmbiguous = false;
				std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
				if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
				        ConvSource, ConvName, {}, bAmbiguous, &HostNativeIndex, bSourceIsConst))
				{
					return EmitHostOperatorCall(
					    Emitter, *HostOperator, HostNativeIndex, {}, Location);
				}
				if (bAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {ConvName});
					return false;
				}
				if (ResolveMethodForCall(
				        ConvSource,
				        ConvName,
				        {},
				        OwnerLinkage,
				        MethodDecl,
				        Location,
				        false,
				        false,
				        &bAmbiguous, bSourceIsConst) &&
				    MethodDecl)
				{
					return EmitMemberOperatorCall(
					    Emitter,
					    ConvSource,
					    MethodDecl->Name,
					    *MethodDecl,
					    OwnerLinkage,
					    0,
					    Location);
				}
				if (bAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {ConvName});
					return false;
				}
				bool bFreeAmbiguous = false;
				const FFunctionDecl* FreeOp = nullptr;
				std::vector<std::string> GenericConcreteTypes;
				if (ResolveFreeOperatorOverload(
				        ConvName,
				        {ConvSource},
				        FreeOp,
				        GenericConcreteTypes,
				        &bFreeAmbiguous,
				        false))
				{
					const std::vector<std::string>* GenericArgs = GenericConcreteTypes.empty()
					    ? nullptr
					    : &GenericConcreteTypes;
					return EmitFreeOperatorCall(Emitter, *FreeOp, 1, Location, GenericArgs);
				}
				if (bFreeAmbiguous)
				{
					ReportErrorId("diag.ambiguous_call_to", Location, {ConvName});
					return false;
				}
			}
		}

		// A non-explicit single-argument constructor is a user-defined conversion.
		// cast<T> may additionally select an explicit constructor. Constructor
		// parameter matching below is deliberately limited to standard conversions,
		// preventing chains such as A -> B -> C and references to temporaries.
		std::string TargetLinkage = QualifiedTarget;
		if (!IsClassOrStructType(TargetLinkage))
		{
			TargetLinkage = ResolveTypeLinkageName(
			    NormalizedTarget, CurrentModulePath);
		}
		{
			const FConstructorDecl* Constructor = nullptr;
			std::string ConstructorKey;
			bool bAmbiguous = false;
			if (ResolveConstructorForCall(
			        TargetLinkage,
			        {NormalizedSource},
			        Constructor,
			        ConstructorKey,
			        Location,
			        false,
			        &bAmbiguous,
			        bAllowExplicitConstructor,
			        false) &&
			    Constructor && Constructor->Parameters.size() == 1 && !Constructor->Parameters.front().bIsRef && !Constructor->Parameters.front().bIsVariadic)
			{
				const std::uint16_t ConstructorIndex = FindFunctionIndex(ConstructorKey);
				const std::uint16_t TypeIndex = FindOrAddType(TargetLinkage);
				if (ConstructorIndex == static_cast<std::uint16_t>(-1) || TypeIndex == static_cast<std::uint16_t>(-1) || !RequireConcreteType(TargetLinkage, Location))
				{
					return false;
				}
				std::unordered_map<std::string, std::string> OwnerSubst;
				if (!TryBuildGenericOwnerSubst(TargetLinkage, OwnerSubst, Location))
				{
					return false;
				}
				std::string ParameterType = OwnerSubst.empty()
				    ? SubstituteTypeName(Constructor->Parameters.front().TypeName)
				    : SubstituteTypeNameWith(
				          Constructor->Parameters.front().TypeName, OwnerSubst);

				const std::uint16_t SourceTemp = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::StoreLocal, SourceTemp);
				Emitter.EmitU16Operand(EOpcode::NewObject, TypeIndex);
				// Preserve value construction copies; class results stay on the stack.
				const std::uint16_t ObjectTemp = Module->Types[TypeIndex].Kind == EScriptObjectKind::Class
				    ? static_cast<std::uint16_t>(-1)
				    : AllocateAssignTemp();
				Emitter.Emit(EOpcode::Dup);
				if (ObjectTemp != static_cast<std::uint16_t>(-1))
				{
					Emitter.EmitU16Operand(EOpcode::StoreLocal, ObjectTemp);
				}
				Emitter.EmitU16Operand(EOpcode::LoadLocal, SourceTemp);
				if (!EmitValueCoercionBetweenTypes(
				        Emitter, ParameterType, NormalizedSource, Location, false, bSourceIsConst))
				{
					return false;
				}
				if (NeedsPassByValueCopy(ParameterType))
				{
					EmitValueCopy(Emitter, ParameterType);
				}
				Emitter.EmitCallBoundMethod(ConstructorIndex, 1);
				Emitter.Emit(EOpcode::Pop);
				if (ObjectTemp != static_cast<std::uint16_t>(-1))
				{
					Emitter.EmitU16Operand(EOpcode::TakeLocal, ObjectTemp);
				}
				if (!Runtime::IsPrimitiveValueTypeName(NormalizedSource) && !NeedsPassByValueCopy(NormalizedSource))
				{
					// The converted result owns any retained payload. Release the
					// staging slot so it cannot keep a class argument alive.
					Emitter.Emit(EOpcode::ConstNull);
					Emitter.EmitU16Operand(EOpcode::StoreLocal, SourceTemp);
				}
				return true;
			}
			if (bAmbiguous)
			{
				ReportErrorId(
				    "diag.ambiguous_overload_for_method",
				    Location,
				    {DisplayTypeName(TargetTypeName)});
				return false;
			}
		}
		ReportErrorId(
		    "diag.cannot_convert_to",
		    Location,
		    {DisplayTypeName(SourceTypeName), DisplayTypeName(TargetTypeName)});
		return false;
	}

	bool VeroScriptCompiler::CanAssignBetweenTypes(
	    const std::string& TargetTypeName,
	    const std::string& SourceTypeName,
	    const bool bAllowUserDefinedConversions) const
	{
		if (TargetTypeName.empty() || TargetTypeName == "auto" || SourceTypeName.empty())
		{
			return true;
		}
		if (SourceTypeName == "void" || TargetTypeName == "void")
		{
			return SourceTypeName == TargetTypeName;
		}
		if (SourceTypeName == TargetTypeName)
		{
			return true;
		}
		if (Runtime::IsFunctionTypeName(TargetTypeName) && Runtime::IsFunctionTypeName(SourceTypeName))
		{
			return Runtime::IsFunctionTypeName(TargetTypeName) && Runtime::IsFunctionTypeName(SourceTypeName) && CanonicalTypeNameForCoercion(NormalizeTypeAlias(SourceTypeName)) == CanonicalTypeNameForCoercion(NormalizeTypeAlias(TargetTypeName));
		}
		if (TargetTypeName == "variant") return SourceTypeName != "nullptr" && !Runtime::IsReferenceTypeName(SourceTypeName);
		if (SourceTypeName == "variant") return false;

		const auto HasConversionOperator = [&]() -> bool
		{
			if (!bAllowUserDefinedConversions)
			{
				return false;
			}
			const std::string NormalizedTarget = NormalizeTypeAlias(TargetTypeName);
			const std::string NormalizedSource = NormalizeTypeAlias(SourceTypeName);
			const std::string QualifiedTarget = CanonicalTypeNameForCoercion(NormalizedTarget);
			const std::string QualifiedSource = CanonicalTypeNameForCoercion(NormalizedSource);
			if (!(TypeSupportsOperatorOverload(SourceTypeName) || TypeSupportsOperatorOverload(QualifiedSource)))
			{
				return false;
			}
			const std::string& ConvSource =
			    TypeSupportsOperatorOverload(SourceTypeName) ? SourceTypeName : QualifiedSource;
			const std::string ConvCandidates[] = {
			    ConversionOperatorMethodName(NormalizedTarget),
			    ConversionOperatorMethodName(QualifiedTarget),
			    ConversionOperatorMethodName(TargetTypeName),
			};
			std::unordered_set<std::string> Tried;
			for (const std::string& ConvName : ConvCandidates)
			{
				if (!Tried.insert(ConvName).second)
				{
					continue;
				}
				std::string OwnerLinkage;
				const FMethodDecl* MethodDecl = nullptr;
				bool bAmbiguous = false;
				FSourceLocation DummyLocation{};
				if (ResolveHostOperator(
				        ConvSource, ConvName, {}, bAmbiguous))
				{
					return true;
				}
				if (bAmbiguous)
				{
					return false;
				}
				if (ResolveMethodForCall(
				        ConvSource,
				        ConvName,
				        {},
				        OwnerLinkage,
				        MethodDecl,
				        DummyLocation,
				        false,
				        false,
				        &bAmbiguous) &&
				    MethodDecl)
				{
					return true;
				}
				{
					const FFunctionDecl* FreeOp = nullptr;
					std::vector<std::string> GenericConcreteTypes;
					if (ResolveFreeOperatorOverload(
					        ConvName,
					        {ConvSource},
					        FreeOp,
					        GenericConcreteTypes,
					        nullptr,
					        false))
					{
						return true;
					}
				}
			}
			return false;
		};

		if (Runtime::IsFunctionTypeName(TargetTypeName))
		{
			return HasConversionOperator();
		}

		if (TargetTypeName == "wstring" || TargetTypeName == "string")
		{
			return SourceTypeName == TargetTypeName || HasConversionOperator();
		}
		if (Runtime::IsUnsignedTypeName(TargetTypeName))
		{
			return Runtime::IsUnsignedTypeName(SourceTypeName) || IsSignedIntegerTypeName(SourceTypeName) || SourceTypeName == "float" || SourceTypeName == "double" || SourceTypeName == "bool" || HasConversionOperator();
		}
		if (IsSignedIntegerTypeName(TargetTypeName))
		{
			return IsSignedIntegerTypeName(SourceTypeName) || Runtime::IsUnsignedTypeName(SourceTypeName) || SourceTypeName == "float" || SourceTypeName == "double" || SourceTypeName == "bool" || HasConversionOperator();
		}
		if (TargetTypeName == "bool")
		{
			return SourceTypeName == "bool" || HasConversionOperator();
		}
		if (TargetTypeName == "float" || TargetTypeName == "double")
		{
			return SourceTypeName == "float" || SourceTypeName == "double" || IsSignedIntegerTypeName(SourceTypeName) || Runtime::IsUnsignedTypeName(SourceTypeName) || SourceTypeName == "bool" || HasConversionOperator();
		}
		const std::string NormalizedTarget = NormalizeTypeAlias(TargetTypeName);
		const std::string NormalizedSource = NormalizeTypeAlias(SourceTypeName);
		if (NormalizedTarget == NormalizedSource)
		{
			return true;
		}
		const std::string QualifiedTarget = CanonicalTypeNameForCoercion(NormalizedTarget);
		const std::string QualifiedSource = CanonicalTypeNameForCoercion(NormalizedSource);
		if (QualifiedTarget == QualifiedSource)
		{
			return true;
		}
		if (IsInterfaceTypeName(QualifiedTarget) && IsStructType(QualifiedSource))
		{
			return IsSubtypeOf(QualifiedSource, QualifiedTarget);
		}
		if (IsSubtypeOf(QualifiedSource, QualifiedTarget))
		{
			return true;
		}
		if (NormalizedSource == "nullptr" && (((!IsStructType(QualifiedTarget) && IsClassOrStructType(QualifiedTarget)) || (!IsStructType(NormalizedTarget) && IsClassOrStructType(NormalizedTarget))) || IsInterfaceTypeName(QualifiedTarget) || IsInterfaceTypeName(NormalizedTarget)))
		{
			return true;
		}
		if (bAllowUserDefinedConversions)
		{
			std::string TargetLinkage = QualifiedTarget;
			if (!IsClassOrStructType(TargetLinkage))
			{
				TargetLinkage = ResolveTypeLinkageName(
				    NormalizedTarget, CurrentModulePath);
			}
			{
				const FConstructorDecl* Constructor = nullptr;
				std::string ConstructorKey;
				if (ResolveConstructorForCall(
				        TargetLinkage,
				        {NormalizedSource},
				        Constructor,
				        ConstructorKey,
				        {},
				        false,
				        nullptr,
				        false,
				        false) &&
				    Constructor && Constructor->Parameters.size() == 1 && !Constructor->Parameters.front().bIsRef && !Constructor->Parameters.front().bIsVariadic)
				{
					return true;
				}
			}
		}
		return HasConversionOperator();
	}

	bool VeroScriptCompiler::RequireBoolCondition(
	    const FExpr& Condition,
	    const FSourceLocation& Location)
	{
		std::string ConditionType;
		if (!TryResolveExprTypeName(Condition, ConditionType))
		{
			ReportError("condition must be bool", Location);
			return false;
		}
		if (!CanAssignBetweenTypes("bool", ConditionType))
		{
			ReportError("condition must be bool", Location);
			return false;
		}
		return true;
	}

	bool VeroScriptCompiler::EmitCompoundAssignOp(
	    FEmitter& Emitter,
	    const EBinaryOp Op,
	    const std::string& LeftType,
	    const std::string& RightType,
	    const FSourceLocation& Location,
	    std::string* OutOverloadedResultType,
	    const FExpr* RightExpression)
	{
		if (TryEmitOverloadedCompoundOp(
		        Emitter,
		        Op,
		        LeftType,
		        RightType,
		        Location,
		        OutOverloadedResultType,
		        RightExpression && TypeSupportsOperatorOverload(RightType) && IsConstAssignTarget(*RightExpression)))
		{
			return true;
		}
		if (HasError())
		{
			return false;
		}

		const bool bLeftWString = LeftType == "wstring";
		const bool bRightWString = RightType == "wstring";
		const bool bLeftString = LeftType == "string";
		const bool bRightString = RightType == "string";
		const bool bLeftText = bLeftString || bLeftWString;
		const bool bRightText = bRightString || bRightWString;
		const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
		const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
		const bool bLeftSignedInt = IsSignedIntegerTypeName(LeftType);
		const bool bRightSignedInt = IsSignedIntegerTypeName(RightType);

		if (Op == EBinaryOp::Add)
		{
			if (bLeftText || bRightText)
			{
				if (bLeftWString && bRightWString)
				{
					Emitter.Emit(EOpcode::Add);
					return true;
				}
				if (bLeftString && bRightString)
				{
					Emitter.Emit(EOpcode::Add);
					return true;
				}
				if ((bLeftWString || bRightWString) && (bLeftString || bRightString))
				{
					ReportError("cannot mix string and wstring concatenation", Location);
					return false;
				}
				if (bLeftWString || bRightWString)
				{
					ReportError("wstring concatenation requires wstring operands", Location);
					return false;
				}
				ReportError("string concatenation requires string operands", Location);
				return false;
			}
		}
		else if (bLeftText || bRightText)
		{
			ReportError("arithmetic operands must be numeric", Location);
			return false;
		}

		if ((bLeftUnsigned && bRightSignedInt) || (bRightUnsigned && bLeftSignedInt))
		{
			const bool bIsShift = Op == EBinaryOp::ShiftLeft || Op == EBinaryOp::ShiftRight;
			if (!bIsShift)
			{
				ReportError("cannot mix signed and unsigned", Location);
				return false;
			}
		}

		if (Op == EBinaryOp::BitAnd || Op == EBinaryOp::BitOr || Op == EBinaryOp::BitXor || Op == EBinaryOp::ShiftLeft || Op == EBinaryOp::ShiftRight)
		{
			if (!(bLeftSignedInt || bLeftUnsigned) || !(bRightSignedInt || bRightUnsigned))
			{
				ReportError("bitwise operands must be integers", Location);
				return false;
			}
		}

		switch (Op)
		{
		case EBinaryOp::Add:
			{
				Emitter.Emit(EOpcode::Add);
				return true;
			}
		case EBinaryOp::Subtract:
			{
				Emitter.Emit(EOpcode::Sub);
				return true;
			}
		case EBinaryOp::Multiply:
			{
				Emitter.Emit(EOpcode::Mul);
				return true;
			}
		case EBinaryOp::Divide:
			{
				Emitter.Emit(EOpcode::Div);
				return true;
			}
		case EBinaryOp::Modulo:
			{
				Emitter.Emit(EOpcode::Mod);
				return true;
			}
		case EBinaryOp::BitAnd:
			{
				Emitter.Emit(EOpcode::BitAnd);
				return true;
			}
		case EBinaryOp::BitOr:
			{
				Emitter.Emit(EOpcode::BitOr);
				return true;
			}
		case EBinaryOp::BitXor:
			{
				Emitter.Emit(EOpcode::BitXor);
				return true;
			}
		case EBinaryOp::ShiftLeft:
			{
				Emitter.Emit(EOpcode::Shl);
				return true;
			}
		case EBinaryOp::ShiftRight:
			{
				Emitter.Emit(EOpcode::Shr);
				return true;
			}
		default:
			{
				ReportError("unsupported compound assignment operator", Location);
				return false;
			}
		}
	}

	std::string VeroScriptCompiler::ResolveCompoundAssignResultType(
	    const EBinaryOp Op,
	    const std::string& TargetTypeName,
	    const std::string& RightTypeName,
	    const std::string& OverloadedResultType) const
	{
		if (!OverloadedResultType.empty())
		{
			return OverloadedResultType;
		}
		if (Op == EBinaryOp::Add && (TargetTypeName == "string" || TargetTypeName == "wstring"))
		{
			return TargetTypeName;
		}
		if (Runtime::IsUnsignedTypeName(TargetTypeName) && Runtime::IsUnsignedTypeName(RightTypeName))
		{
			return TargetTypeName == "uint64" || TargetTypeName == "unsigned long" || RightTypeName == "uint64" || RightTypeName == "unsigned long"
			    ? "uint64"
			    : "unsigned int";
		}
		if (TargetTypeName == "double" || RightTypeName == "double")
		{
			return "double";
		}
		if (TargetTypeName == "float" || RightTypeName == "float")
		{
			return "float";
		}
		if (IsSignedIntegerTypeName(TargetTypeName) && IsSignedIntegerTypeName(RightTypeName))
		{
			return TargetTypeName == "int64" || TargetTypeName == "long" || RightTypeName == "int64" || RightTypeName == "long"
			    ? "int64"
			    : "int";
		}
		return TargetTypeName;
	}

	bool VeroScriptCompiler::TryEmitIncDecUnitOne(
	    FEmitter& Emitter,
	    const FAssignExpr& Assign,
	    const std::string& TargetTypeName,
	    std::string& InOutRightTypeName)
	{
		if (Assign.CompoundOp != EBinaryOp::Add && Assign.CompoundOp != EBinaryOp::Subtract)
		{
			return false;
		}
		if (!Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec)
		{
			return false;
		}
		if (Assign.Value->Kind != EExprKind::IntegerLiteral)
		{
			return false;
		}
		const FIntegerLiteralExpr& Literal = static_cast<const FIntegerLiteralExpr&>(*Assign.Value);
		if (Literal.bUnsigned || Literal.Value != 1)
		{
			return false;
		}

		if (Runtime::IsUnsignedTypeName(TargetTypeName))
		{
			FBytecodeConstant Constant;
			Constant.Kind = EConstantKind::U32;
			Constant.U32 = 1;
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
			InOutRightTypeName = "unsigned int";
			return true;
		}
		if (TargetTypeName == "double" || TargetTypeName == "float")
		{
			FBytecodeConstant Constant;
			if (TargetTypeName == "float")
			{
				Constant.Kind = EConstantKind::F32;
				Constant.F32 = 1.0f;
			}
			else
			{
				Constant.Kind = EConstantKind::F64;
				Constant.F64 = 1.0;
			}
			Emitter.EmitU16Operand(EOpcode::ConstF64, AddConstant(Constant));
			InOutRightTypeName = TargetTypeName;
			return true;
		}
		return false;
	}

	bool VeroScriptCompiler::IsSignedIntegerTypeName(const std::string_view TypeName)
	{
		return TypeName == "int" || TypeName == "char" || TypeName == "short" || TypeName == "long" || TypeName == "int64";
	}

	std::string VeroScriptCompiler::GetSymbolLinkageName(
	    const std::string& ModulePath,
	    const bool bExported,
	    const std::string& SimpleName) const
	{
		if (bExported || ModulePath.empty())
		{
			return SimpleName;
		}
		// Linkage identity must not depend on the host platform's separator or on
		// whether a direct parser entry used `\` while the module loader used `/`.
		std::string StableModulePath = ModulePath;
		std::replace(StableModulePath.begin(), StableModulePath.end(), '\\', '/');
		return StableModulePath + '\x1F' + SimpleName;
	}

	bool VeroScriptCompiler::CanAccessSymbol(
	    const std::string& SymbolModulePath,
	    const bool bExported,
	    const std::string& ReferenceModulePath) const
	{
		if (SymbolModulePath == ReferenceModulePath)
		{
			return true;
		}
		if (!ActiveProgram)
		{
			return bExported;
		}
		return ModuleVisibility::CanAccessSymbol(
		    *ActiveProgram,
		    SymbolModulePath,
		    bExported,
		    ReferenceModulePath,
		    ActiveImportIndex);
	}

	std::string VeroScriptCompiler::ResolveTypeLinkageName(
	    const std::string& SimpleName,
	    const std::string& ReferenceModulePath,
	    const std::size_t TypeParamArity) const
	{
		if (Runtime::IsPrimitiveValueTypeName(SimpleName) || SimpleName == "void")
		{
			return {};
		}
		for (const std::string& Candidate : BuildLexicalNamespaceLookupNames(SimpleName))
		{
			if (const std::string Resolved =
			        ResolveTypeLinkageNameExact(Candidate, ReferenceModulePath, TypeParamArity);
			    !Resolved.empty())
			{
				return Resolved;
			}
		}

		std::vector<std::string> Matches;
		for (const std::string& Candidate :
		    BuildUsingNamespaceLookupNames(SimpleName, ReferenceModulePath))
		{
			const std::string Resolved =
			    ResolveTypeLinkageNameExact(Candidate, ReferenceModulePath, TypeParamArity);
			if (!Resolved.empty() && std::find(Matches.begin(), Matches.end(), Resolved) == Matches.end())
			{
				Matches.push_back(Resolved);
			}
		}
		if (Matches.size() == 1)
		{
			return Matches.front();
		}
		if (Matches.size() > 1)
		{
			ReportError("ambiguous type name from using namespace: " + SimpleName,
			    CurrentEmitLocation);
		}
		return {};
	}

	std::string VeroScriptCompiler::ResolveTypeLinkageNameExact(const std::string& SimpleName,
	    const std::string& ReferenceModulePath,
	    const std::size_t TypeParamArity) const
	{
		if (!ActiveProgram || SimpleName.empty())
		{
			return {};
		}
		// Already a linkage name (private types, or qualified leaves from QualifyTypeName).
		if (SimpleName.find('\x1F') != std::string::npos)
		{
			return SimpleName;
		}
		if (TypeParamArity != static_cast<std::size_t>(-1))
		{
			const auto* Struct = FindGenericStructDecl(SimpleName, ReferenceModulePath, TypeParamArity);
			const auto* Class = FindGenericClassDecl(SimpleName, ReferenceModulePath, TypeParamArity);
			const auto* Interface = FindGenericInterfaceDecl(SimpleName, ReferenceModulePath, TypeParamArity);
			const FEnumDecl* Enum = nullptr;
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceEnumNames, SimpleName))
			{
				const auto& Declaration = ActiveProgram->Enums[DeclarationIndex];
				if (Declaration.Name == SimpleName &&
				    EnclosingTemplateParameterNames(Declaration.Scope).size() == TypeParamArity &&
				    CanAccessSymbol(Declaration.ModulePath, Declaration.bExported, ReferenceModulePath))
				{
					if (!Enum || Declaration.ModulePath == ReferenceModulePath)
					{
						Enum = &Declaration;
					}
				}
			}
			// A local type hides an imported type of any declaration kind.
			if (Struct && Struct->ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(Struct->ModulePath, Struct->bExported, Struct->Name);
			}
			if (Class && Class->ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(Class->ModulePath, Class->bExported, Class->Name);
			}
			if (Interface && Interface->ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(Interface->ModulePath, Interface->bExported, Interface->Name);
			}
			if (Enum && Enum->ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(Enum->ModulePath, Enum->bExported, Enum->Name);
			}
			if (Struct)
			{
				return GetSymbolLinkageName(Struct->ModulePath, Struct->bExported, Struct->Name);
			}
			if (Class)
			{
				return GetSymbolLinkageName(Class->ModulePath, Class->bExported, Class->Name);
			}
			if (Interface)
			{
				return GetSymbolLinkageName(Interface->ModulePath, Interface->bExported, Interface->Name);
			}
			return Enum ? GetSymbolLinkageName(Enum->ModulePath, Enum->bExported, Enum->Name) : std::string{};
		}

		const FStructDecl* SelectedStruct = nullptr;
		const FClassDecl* SelectedClass = nullptr;
		const FInterfaceDecl* SelectedInterface = nullptr;
		// Bare name resolution prefers arity-0; arity overloads share linkage SimpleName.
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructNames, SimpleName))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (StructDecl.Name != SimpleName || !TemplateParameterNames(StructDecl).empty() ||
			    !CanAccessSymbol(StructDecl.ModulePath, StructDecl.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (StructDecl.ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			}
			if (!SelectedStruct)
			{
				SelectedStruct = &StructDecl;
			}
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassNames, SimpleName))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (ClassDecl.Name != SimpleName || !TemplateParameterNames(ClassDecl).empty() || !CanAccessSymbol(ClassDecl.ModulePath, ClassDecl.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (ClassDecl.ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			}
			if (!SelectedClass)
			{
				SelectedClass = &ClassDecl;
			}
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceNames, SimpleName))
		{
			const auto& InterfaceDecl = ActiveProgram->Interfaces[DeclarationIndex];
			if (InterfaceDecl.Name != SimpleName || !TemplateParameterNames(InterfaceDecl).empty() || !CanAccessSymbol(InterfaceDecl.ModulePath, InterfaceDecl.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (InterfaceDecl.ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(
				    InterfaceDecl.ModulePath,
				    InterfaceDecl.bExported,
				    InterfaceDecl.Name);
			}
			if (!SelectedInterface)
			{
				SelectedInterface = &InterfaceDecl;
			}
		}
		// Fallback: any arity (legacy bare-name linkage for generics-only names).
		if (!SelectedStruct && !SelectedClass && !SelectedInterface)
		{
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructNames, SimpleName))
			{
				const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
				if (StructDecl.Name != SimpleName || !CanAccessSymbol(StructDecl.ModulePath, StructDecl.bExported, ReferenceModulePath))
				{
					continue;
				}
				if (StructDecl.ModulePath == ReferenceModulePath)
				{
					return GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				}
				if (!SelectedStruct)
				{
					SelectedStruct = &StructDecl;
				}
			}
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassNames, SimpleName))
			{
				const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
				if (ClassDecl.Name != SimpleName || !CanAccessSymbol(ClassDecl.ModulePath, ClassDecl.bExported, ReferenceModulePath))
				{
					continue;
				}
				if (ClassDecl.ModulePath == ReferenceModulePath)
				{
					return GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
				}
				if (!SelectedClass)
				{
					SelectedClass = &ClassDecl;
				}
			}
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceNames, SimpleName))
			{
				const auto& InterfaceDecl = ActiveProgram->Interfaces[DeclarationIndex];
				if (InterfaceDecl.Name != SimpleName || !CanAccessSymbol(InterfaceDecl.ModulePath, InterfaceDecl.bExported, ReferenceModulePath))
				{
					continue;
				}
				if (InterfaceDecl.ModulePath == ReferenceModulePath)
				{
					return GetSymbolLinkageName(
					    InterfaceDecl.ModulePath,
					    InterfaceDecl.bExported,
					    InterfaceDecl.Name);
				}
				if (!SelectedInterface)
				{
					SelectedInterface = &InterfaceDecl;
				}
			}
		}
		if (SelectedStruct)
		{
			return GetSymbolLinkageName(
			    SelectedStruct->ModulePath,
			    SelectedStruct->bExported,
			    SelectedStruct->Name);
		}
		if (SelectedClass)
		{
			return GetSymbolLinkageName(
			    SelectedClass->ModulePath,
			    SelectedClass->bExported,
			    SelectedClass->Name);
		}
		if (SelectedInterface)
		{
			return GetSymbolLinkageName(
			    SelectedInterface->ModulePath,
			    SelectedInterface->bExported,
			    SelectedInterface->Name);
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (StructDecl.Name == SimpleName)
			{
				return GetSymbolLinkageName(
				    StructDecl.ModulePath,
				    StructDecl.bExported,
				    StructDecl.Name);
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (ClassDecl.Name == SimpleName)
			{
				return GetSymbolLinkageName(
				    ClassDecl.ModulePath,
				    ClassDecl.bExported,
				    ClassDecl.Name);
			}
		}
		return {};
	}

	std::string VeroScriptCompiler::QualifyTypeName(
	    const std::string& TypeName,
	    const std::string& ReferenceModulePath) const
	{
		if (TypeName.empty() || TypeName == "auto" || TypeName == "void" || TypeName == "nullptr" || Runtime::IsPrimitiveValueTypeName(TypeName))
		{
			return TypeName;
		}
		{
			const std::string Expanded = ExpandUserTypeAlias(TypeName, ReferenceModulePath);
			if (Expanded != TypeName)
			{
				return QualifyTypeName(Expanded, ReferenceModulePath);
			}
		}
		if (TypeName.rfind("const ref ", 0) == 0)
		{
			return "const ref " + QualifyTypeName(TypeName.substr(10), ReferenceModulePath);
		}
		if (TypeName.rfind("ref ", 0) == 0)
		{
			return "ref " + QualifyTypeName(TypeName.substr(4), ReferenceModulePath);
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (!Runtime::ParseFunctionType(TypeName, ParamTypes, ReturnType))
			{
				return TypeName;
			}
			for (std::string& ParamType : ParamTypes)
			{
				ParamType = QualifyTypeName(ParamType, ReferenceModulePath);
			}
			ReturnType = QualifyTypeName(ReturnType, ReferenceModulePath);
			return Runtime::MakeFunctionTypeName(ParamTypes, ReturnType);
		}
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs))
		{
			const std::string BaseLinkage =
			    ResolveTypeLinkageName(GenericBase, ReferenceModulePath, GenericArgs.size());
			if (BaseLinkage.empty())
			{
				// Deferred template source separates tokens with whitespace. Restore
				// built-in container spelling before the runtime type parsers run.
				std::string ContainerType = GenericBase + "<";
				for (std::size_t Index = 0; Index < GenericArgs.size(); ++Index)
				{
					if (Index != 0)
					{
						ContainerType.push_back(',');
					}
					ContainerType += GenericArgs[Index];
				}
				ContainerType.push_back('>');
				if (Runtime::IsDynamicArrayTypeName(ContainerType) || Runtime::IsCommonContainerTypeName(ContainerType))
				{
					return QualifyTypeName(ContainerType, ReferenceModulePath);
				}
				return TypeName;
			}
			std::vector<std::string> QualifiedArgs;
			QualifiedArgs.reserve(GenericArgs.size());
			for (const std::string& GenericArg : GenericArgs)
			{
				QualifiedArgs.push_back(CanonicalTypeNameForCoercion(
				    QualifyTypeName(GenericArg, ReferenceModulePath)));
			}
			const FTemplateParameterList* Schema = nullptr;
			std::string DeclarationModule;
			const auto SelectSchema = [&](const auto* Declaration)
			{
				if (Declaration)
				{
					Schema = &Declaration->TemplateParameters;
					DeclarationModule = Declaration->ModulePath;
				}
			};
			SelectSchema(FindGenericStructDecl(GenericBase, ReferenceModulePath, GenericArgs.size()));
			if (!Schema)
			{
				SelectSchema(FindGenericClassDecl(GenericBase, ReferenceModulePath, GenericArgs.size()));
			}
			if (!Schema)
			{
				SelectSchema(FindGenericInterfaceDecl(GenericBase, ReferenceModulePath, GenericArgs.size()));
			}
			if (!Schema)
			{
				if (const auto* Enum = FindEnumDeclaration(BaseLinkage))
				{
					Schema = &Enum->Scope.EnclosingTemplateParameters;
					DeclarationModule = Enum->ModulePath;
				}
			}
			if (Schema && !Schema->IsEmpty())
			{
				auto Arguments = GenericArgs;
				const auto PreviousModule = CurrentModulePath;
				struct FModuleRestore
				{
					~FModuleRestore()
					{
						Target = Previous;
					}
					std::string& Target;
					const std::string& Previous;
				} Restore{CurrentModulePath, PreviousModule};
				CurrentModulePath = ReferenceModulePath;
				if (!CompleteTemplateArguments(*Schema, DeclarationModule, Arguments, CurrentEmitLocation))
				{
					// An open pattern must remain symbolic until its environment closes.
					return TypeName;
				}
				QualifiedArgs = std::move(Arguments);
			}

			const std::string Instantiated =
			    MangleGenericInstanceName(BaseLinkage, QualifiedArgs);
			if (const auto* Enum = FindEnumDeclaration(Instantiated))
			{
				return EnsureEnumMetadata(*Enum, QualifiedArgs);
			}
			const std::string Completed =
			    ResolveInheritanceBaseLinkage(Instantiated, ReferenceModulePath);
			return Completed.empty() ? Instantiated : Completed;
		}
		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "array<" + QualifyTypeName(ElementType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!Runtime::SplitMapType(TypeName, KeyType, ValueType))
			{
				return TypeName;
			}
			const std::string Prefix =
			    Runtime::IsMultiMapTypeName(TypeName) ? "multimap<" : "map<";
			return Prefix + QualifyTypeName(KeyType, ReferenceModulePath) + "," + QualifyTypeName(ValueType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsSetTypeName(TypeName))
		{
			std::string ElementType;
			return Runtime::SplitSetElementType(TypeName, ElementType)
			    ? "set<" + QualifyTypeName(ElementType, ReferenceModulePath) + ">"
			    : TypeName;
		}
		if (Runtime::IsMultiSetTypeName(TypeName) || Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseMultiSetElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			const std::string Prefix = Runtime::IsOrderedMultiSetTypeName(TypeName)
			    ? "ordered_multiset<"
			    : "multiset<";
			return Prefix + QualifyTypeName(ElementType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsQueueTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseQueueElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			const std::string Prefix =
			    Runtime::IsPriorityQueueTypeName(TypeName) ? "priority_queue<" : "queue<";
			return Prefix + QualifyTypeName(ElementType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsStackTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseStackElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "stack<" + QualifyTypeName(ElementType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!Runtime::SplitOrderedMapType(TypeName, KeyType, ValueType))
			{
				return TypeName;
			}
			const std::string Prefix = Runtime::IsOrderedMultiMapTypeName(TypeName)
			    ? "ordered_multimap<"
			    : "ordered_map<";
			return Prefix + QualifyTypeName(KeyType, ReferenceModulePath) + "," + QualifyTypeName(ValueType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsDequeTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDequeElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "deque<" + QualifyTypeName(ElementType, ReferenceModulePath) + ">";
		}
		if (Runtime::IsNativeArrayTypeName(TypeName))
		{
			// Peel left-to-right so "int[2][3]" stays outer-first after qualifying the leaf.
			std::vector<std::uint32_t> Lengths;
			std::string Current = TypeName;
			std::string ElementType;
			std::uint32_t Length = 0;
			while (Runtime::ParseNativeArrayType(Current, ElementType, Length))
			{
				Lengths.push_back(Length);
				Current = std::move(ElementType);
			}
			if (Lengths.empty())
			{
				return TypeName;
			}
			std::string Result = QualifyTypeName(Current, ReferenceModulePath);
			for (const std::uint32_t Dim : Lengths)
			{
				Result.push_back('[');
				Result.append(std::to_string(Dim));
				Result.push_back(']');
			}
			return Result;
		}
		if (!GenericTemplateBaseLinkage(TypeName).empty())
		{
			const std::string Completed =
			    ResolveInheritanceBaseLinkage(TypeName, CurrentModulePath);
			if (!Completed.empty())
			{
				return Completed;
			}
		}
		if (TypeName.find('\x1F') != std::string::npos)
		{
			return TypeName;
		}
		if (EnumTypeNames.count(TypeName) != 0 ||
		    (TypeName.rfind("::", 0) == 0 && EnumTypeNames.count(TypeName.substr(2)) != 0))
		{
			const auto EnumLinkage = ResolveTypeLinkageName(TypeName, ReferenceModulePath, 0);
			if (!EnumLinkage.empty() && FindEnumDeclaration(EnumLinkage))
			{
				return EnumLinkage;
			}
		}
		const std::string LinkageName = ResolveTypeLinkageName(TypeName, ReferenceModulePath);
		return LinkageName.empty() ? TypeName : LinkageName;
	}

	std::string VeroScriptCompiler::CanonicalTypeNameForCoercion(const std::string& TypeName) const
	{
		if (TypeName.empty() || TypeName == "auto" || TypeName == "void" || TypeName == "nullptr")
		{
			return TypeName;
		}
		// Fixed-width spellings and their C++-style primitive spellings are true
		// aliases of one runtime type. Canonicalize them for identity/overload
		// resolution without rewriting the source-facing declaration metadata.
		if (TypeName == "long" || TypeName == "int64")
		{
			return "int64";
		}
		if (TypeName == "unsigned long" || TypeName == "uint64")
		{
			return "uint64";
		}
		if (TypeName.rfind("const ref ", 0) == 0)
		{
			return "const ref " + CanonicalTypeNameForCoercion(TypeName.substr(10));
		}
		if (TypeName.rfind("ref ", 0) == 0)
		{
			return "ref " + CanonicalTypeNameForCoercion(TypeName.substr(4));
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (!Runtime::ParseFunctionType(TypeName, ParamTypes, ReturnType))
			{
				return TypeName;
			}
			for (std::string& ParamType : ParamTypes)
			{
				ParamType = CanonicalTypeNameForCoercion(ParamType);
			}
			ReturnType = CanonicalTypeNameForCoercion(ReturnType);
			return Runtime::MakeFunctionTypeName(ParamTypes, ReturnType);
		}
		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "array<" + CanonicalTypeNameForCoercion(ElementType) + ">";
		}
		if (Runtime::IsMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!Runtime::SplitMapType(TypeName, KeyType, ValueType))
			{
				return TypeName;
			}
			const std::string Prefix =
			    Runtime::IsMultiMapTypeName(TypeName) ? "multimap<" : "map<";
			return Prefix + KeyType + "," + CanonicalTypeNameForCoercion(ValueType) + ">";
		}
		if (Runtime::IsSetTypeName(TypeName))
		{
			return TypeName;
		}
		if (Runtime::IsMultiSetTypeName(TypeName) || Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseMultiSetElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			const std::string Prefix = Runtime::IsOrderedMultiSetTypeName(TypeName)
			    ? "ordered_multiset<"
			    : "multiset<";
			return Prefix + CanonicalTypeNameForCoercion(ElementType) + ">";
		}
		if (Runtime::IsQueueTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseQueueElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			const std::string Prefix =
			    Runtime::IsPriorityQueueTypeName(TypeName) ? "priority_queue<" : "queue<";
			return Prefix + CanonicalTypeNameForCoercion(ElementType) + ">";
		}
		if (Runtime::IsStackTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseStackElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "stack<" + CanonicalTypeNameForCoercion(ElementType) + ">";
		}
		if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!Runtime::SplitOrderedMapType(TypeName, KeyType, ValueType))
			{
				return TypeName;
			}
			const std::string Prefix = Runtime::IsOrderedMultiMapTypeName(TypeName)
			    ? "ordered_multimap<"
			    : "ordered_map<";
			return Prefix + KeyType + "," + CanonicalTypeNameForCoercion(ValueType) + ">";
		}
		if (Runtime::IsDequeTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDequeElementType(TypeName, ElementType))
			{
				return TypeName;
			}
			return "deque<" + CanonicalTypeNameForCoercion(ElementType) + ">";
		}
		if (Runtime::IsNativeArrayTypeName(TypeName))
		{
			std::vector<std::uint32_t> Lengths;
			std::string Current = TypeName;
			std::string ElementType;
			std::uint32_t Length = 0;
			while (Runtime::ParseNativeArrayType(Current, ElementType, Length))
			{
				Lengths.push_back(Length);
				Current = std::move(ElementType);
			}
			if (Lengths.empty())
			{
				return TypeName;
			}
			std::string Result = CanonicalTypeNameForCoercion(Current);
			for (const std::uint32_t Dim : Lengths)
			{
				Result.push_back('[');
				Result.append(std::to_string(Dim));
				Result.push_back(']');
			}
			return Result;
		}
		if (!GenericTemplateBaseLinkage(TypeName).empty())
		{
			const std::string Completed =
			    ResolveInheritanceBaseLinkage(TypeName, CurrentModulePath);
			if (!Completed.empty())
			{
				return Completed;
			}
		}
		if (TypeName.find('\x1F') != std::string::npos)
		{
			return TypeName;
		}
		// Compare ordinary enum types by declaration identity while retaining the
		// established source spelling in emitted enum values and host callbacks.
		if (EnumTypeNames.count(TypeName) != 0 ||
		    (TypeName.rfind("::", 0) == 0 && EnumTypeNames.count(TypeName.substr(2)) != 0))
		{
			const auto Linkage = ResolveTypeLinkageName(TypeName, CurrentModulePath, 0);
			if (!Linkage.empty())
			{
				return Linkage;
			}
		}
		if (FindRegisteredTypeIndex(TypeName) != static_cast<std::uint16_t>(-1)) return TypeName;
		return QualifyTypeName(TypeName, CurrentModulePath);
	}

}
