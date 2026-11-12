// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "TypeSupport/VeroScriptConstantRepresentation.h"
#include "VeroScriptCompiler.h"
#include "TypeSupport/VeroScriptTemplateMetadataValidation.h"
#include "VeroScriptAst.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"

#include <algorithm>

namespace VeroScript
{
	const FEnumDecl* VeroScriptCompiler::FindEnumDeclaration(const std::string& Name) const
	{
		if (!ActiveProgram)
		{
			return nullptr;
		}
		std::string Base;
		std::vector<std::string> Arguments;
		if (!SplitUserGenericTypeApp(Name, Base, Arguments))
		{
			Base = GenericTemplateBaseLinkage(Name);
			if (Base.empty())
			{
				Base = Name;
			}
			else
			{
				Arguments = DecodeGenericConcreteTypes(Name);
			}
		}
		if (Base.rfind("::", 0) == 0)
		{
			Base.erase(0, 2);
		}
		const FEnumDecl* Result = nullptr;
		const FEnumDecl* SourceSpelling = nullptr;
		bool SourceAmbiguous = false;
		for (const auto& Declaration : ActiveProgram->Enums)
		{
			const auto Linkage =
			    GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
			if (Declaration.Name != Base && Linkage != Base)
			{
				if ((StripTypeScopeMarkers(Declaration.Name) == Base || GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, StripTypeScopeMarkers(Declaration.Name)) == Base) && Arguments.size() == EnclosingTemplateParameterNames(Declaration.Scope).size() && CanAccessSymbol(Declaration.ModulePath, Declaration.bExported, CurrentModulePath))
				{
					if (SourceSpelling && SourceSpelling->ModulePath == CurrentModulePath && Declaration.ModulePath != CurrentModulePath)
					{
						continue;
					}
					if (SourceSpelling && SourceSpelling->ModulePath != CurrentModulePath && Declaration.ModulePath == CurrentModulePath)
					{
						SourceAmbiguous = false;
						SourceSpelling = nullptr;
					}
					SourceAmbiguous = SourceAmbiguous || SourceSpelling != nullptr;
					SourceSpelling = &Declaration;
				}
				continue;
			}
			if (!Arguments.empty() && Arguments.size() != EnclosingTemplateParameterNames(Declaration.Scope).size())
			{
				continue;
			}
			if (Declaration.ModulePath == CurrentModulePath || Linkage == Base)
			{
				return &Declaration;
			}
			if (CanAccessSymbol(Declaration.ModulePath, Declaration.bExported, CurrentModulePath))
			{
				Result = &Declaration;
			}
		}
		return Result ? Result : SourceAmbiguous ? nullptr
		                                         : SourceSpelling;
	}

	std::string VeroScriptCompiler::EnsureEnumMetadata(const FEnumDecl& Declaration,
	    const std::vector<std::string>& Arguments) const
	{
		const auto Linkage = GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
		const auto Name = Arguments.empty() ? Linkage : MangleGenericInstanceName(Linkage, Arguments);
		if (!Module || (ConstantEvaluationState && ConstantEvaluationState->NativeDepth != 0)) return Name;
		auto& Compiler = *const_cast<VeroScriptCompiler*>(this);
		Compiler.EnumTypeNames.insert(Name);
		Compiler.EnumTypeNames.insert(Declaration.Name);
		if (std::any_of(Module->Enums.begin(), Module->Enums.end(),
		        [&](const auto& Type)
		{
			return Type.Name == Name;
		}))
		{
			return Name;
		}
		FBytecodeEnumDesc Type;
		Type.Name = Name;
		Type.bScoped = Declaration.bScoped;
		Type.UnderlyingTypeName = Declaration.UnderlyingTypeName;
		Type.DeclMeta = Declaration.DeclMeta;
		const auto Metadata = TemplateMetadataBindings.find(&Declaration.DeclMeta);
		if (Metadata != TemplateMetadataBindings.end())
		{
			Type.TemplateInstance.DeclarationId = Metadata->second.Id;
			Type.TemplateInstance.EnclosingArguments = Arguments;
		}
		if (!CaptureTemplateInstanceValueTypes(Type.TemplateInstance))
		{
			ReportError("invalid closed template instance metadata", Declaration.Location);
			return {};
		}
		std::vector<FNestedTypeDeclaration> Children;
		PopulateSourceTypeScope(Declaration.Scope, Arguments, Type.Scope, Children);
		for (const auto& Member : Declaration.Members)
		{
			std::int64_t Number = Member.Value;
			if (Member.Initializer)
			{
				std::unordered_map<std::string, std::string> Bindings;
				const auto Names = EnclosingTemplateParameterNames(Declaration.Scope);
				for (std::size_t I = 0; I < Names.size() && I < Arguments.size(); ++I)
				{
					Bindings[Names[I]] = Arguments[I];
				}
				FIdentifierExpr Reference(Name + "." + Member.Name);
				const auto Value = EvaluateTemplateExpression(Reference, Declaration.ModulePath, Bindings, Declaration.Name, false);
				if (!Value.Succeeded())
				{
					ReportError("invalid enum constant: " + Value.Reason, Member.Location);
					return {};
				}
				auto Bits = Value.Value.Bits;
				const auto& ValueType = Value.Value.Type;
				if (!ValueType.bUnsigned && ValueType.Width < 64 && (Bits & (std::uint64_t{1} << (ValueType.Width - 1))))
				{
					Bits |= ~((std::uint64_t{1} << ValueType.Width) - 1);
				}
				Number = DecodeConstantSignedBits(Bits);
			}
			Compiler.EnumConstants[Name + "." + Member.Name] = Number;
			if (!Declaration.bScoped)
			{
				Compiler.EnumConstantTypeNames[Name + "." + Member.Name] = Declaration.UnderlyingTypeName;
			}
			Type.Members.push_back({Member.Name, Number, Member.DeclMeta});
		}
		Module->Enums.push_back(std::move(Type));
		return Name;
	}
} // namespace VeroScript
