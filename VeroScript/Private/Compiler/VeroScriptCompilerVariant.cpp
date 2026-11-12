// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptRuntime.h"

namespace VeroScript
{
	bool VeroScriptCompiler::CompileVariantCall(const FCallExpr& Call, FEmitter& Emitter)
	{
		const auto Args = Call.GetArguments();
		const bool Construct = Call.Callee != "variant_is" && Call.Callee != "variant_has_value" && Call.Callee != "variant_type_name";
		const bool Exact = Call.Callee == "variant_is";
		if ((Construct ? Args.size() > 1 : Args.size() != 1) || Call.TypeArgs.size() != (Exact ? 1u : 0u))
		{
			ReportError("invalid variant intrinsic arguments", Call.Location);
			return false;
		}
		if (Construct && Args.empty())
		{
			Emitter.EmitCallBuiltin(EBuiltinId::VariantEmpty, 0);
			return true;
		}
		if (Args.front()->Kind == EExprKind::Ref)
		{
			ReportErrorId("diag.builtin_does_not_accept_ref_arguments", Call.Location);
			return false;
		}
		std::string Source;
		if (!TryResolveExprTypeName(*Args.front(), Source))
		{
			ReportError("variant argument has no concrete type", Call.Location);
			return false;
		}
		Source = CanonicalTypeNameForCoercion(NormalizeTypeAlias(Source));
		if (!Construct && Source != "variant")
		{
			ReportErrorId("diag.variant_expected", Call.Location);
			return false;
		}
		std::string Target;
		if (Exact)
		{
			Target = CanonicalVariantTypeName(SubstituteTypeName(Call.TypeArgs.front()));
			if (Target == "variant" || Target == "void" || Target == "nullptr" || Runtime::IsReferenceTypeName(Target) || !ValidateFieldTypeName(Target, Call.Location))
			{
				if (!HasError())
				{
					ReportError("variant_is requires a concrete non-variant value type", Call.Location);
				}
				return false;
			}
		}
		if (!CompileExpression(*Args.front())) return false;
		if (Construct) return EmitValueCoercionForTarget(Emitter, "variant", *Args.front(), Call.Location);
		if (Exact)
		{
			Emitter.EmitU16Operand(EOpcode::VariantIs, AddString(Target));
		}
		else
		{
			Emitter.EmitCallBuiltin(Call.Callee == "variant_has_value" ? EBuiltinId::VariantHasValue : EBuiltinId::VariantTypeName, 1);
		}
		return true;
	}

	std::string VeroScriptCompiler::CanonicalVariantTypeName(const std::string& TypeName) const
	{
		const auto Name = CanonicalTypeNameForCoercion(QualifyTypeName(NormalizeTypeAlias(TypeName), CurrentModulePath));
		const FEnumDecl* Selected = nullptr;
		if (ActiveProgram)
		{
			for (const auto& Enum : ActiveProgram->Enums)
			{
				if (!Enum.bScoped || (Enum.Name != Name && GetSymbolLinkageName(Enum.ModulePath, Enum.bExported, Enum.Name) != Name) || !CanAccessSymbol(Enum.ModulePath, Enum.bExported, CurrentModulePath))
				{
					continue;
				}
				if (Enum.ModulePath == CurrentModulePath) return GetSymbolLinkageName(Enum.ModulePath, Enum.bExported, Enum.Name);
				if (!Selected)
				{
					Selected = &Enum;
				}
			}
		}
		return Selected ? GetSymbolLinkageName(Selected->ModulePath, Selected->bExported, Selected->Name) : Name;
	}
}
