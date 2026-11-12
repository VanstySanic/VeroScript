// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptCompilerDeclarationLookup.h"
#include "VeroScriptCompilerVariantInternal.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
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
	const FFunctionDecl* VeroScriptCompiler::FindUniqueFreeFunction(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		if (!ActiveProgram)
		{
			return nullptr;
		}
		const FFunctionDecl* Found = nullptr;
		int MatchCount = 0;
		const std::vector<std::string> LookupNames =
		    BuildFreeFunctionLookupNames(Name, ReferenceModulePath);
		for (const FFunctionDecl& Function : ActiveProgram->Functions)
		{
			if (std::find(LookupNames.begin(), LookupNames.end(), Function.Name) == LookupNames.end() || !CanAccessSymbol(Function.ModulePath, Function.bExported, ReferenceModulePath))
			{
				continue;
			}
			Found = &Function;
			++MatchCount;
			if (MatchCount > 1)
			{
				return nullptr;
			}
		}
		return MatchCount == 1 ? Found : nullptr;
	}

	std::vector<std::string> VeroScriptCompiler::BuildFreeFunctionLookupNames(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		if (!ActiveProgram)
		{
			return {};
		}
		const auto HasAccessibleFunction = [&](const std::string& Candidate)
		{
			if (ActiveNatives && std::any_of(ActiveNatives->begin(), ActiveNatives->end(), [&](const auto& N)
			{
				return N.Name == Candidate && CanAccessNativeBinding(N.Origin);
			}))
			{
				return true;
			}
			return std::any_of(
			    ActiveProgram->Functions.begin(),
			    ActiveProgram->Functions.end(),
			    [&](const FFunctionDecl& Function)
			{
				return Function.Name == Candidate && CanAccessSymbol(Function.ModulePath, Function.bExported, ReferenceModulePath);
			});
		};
		for (const std::string& Candidate : BuildLexicalNamespaceLookupNames(Name))
		{
			if (HasAccessibleFunction(Candidate))
			{
				return {Candidate};
			}
		}
		std::vector<std::string> Names;
		for (const std::string& Candidate :
		    BuildUsingNamespaceLookupNames(Name, ReferenceModulePath))
		{
			if (HasAccessibleFunction(Candidate))
			{
				Names.push_back(Candidate);
			}
		}
		return Names;
	}

	std::string VeroScriptCompiler::MakeFunctionTypeFromDecl(const FFunctionDecl& Function) const
	{
		auto& Compiler = *const_cast<VeroScriptCompiler*>(this);
		FFunctionDeclarationLookupScope Lookup(Compiler.Scopes, Compiler.LocalTypeAliasScopes, Compiler.CurrentFunction,
		    Compiler.CurrentModulePath, Compiler.CurrentNamespacePrefix, Compiler.CurrentOwnerTypeLinkage,
		    Compiler.CurrentAccessFunctionLinkage, Compiler.CurrentEmitLocation, Function);

		std::vector<std::string> ParamTypes;
		ParamTypes.reserve(Function.Parameters.size());
		for (const FFunctionParam& Parameter : Function.Parameters)
		{
			std::string ParamType = QualifyTypeName(
			    NormalizeTypeAlias(
			        SubstituteTypeName(Parameter.TypeName),
			        Function.ModulePath),
			    Function.ModulePath);
			if (Parameter.bIsVariadic)
			{
				ParamType = MakeDynamicArrayTypeName(ParamType);
			}
			if (Parameter.bIsRef)
			{
				ParamType = Parameter.bIsConst
				    ? "const ref " + ParamType
				    : "ref " + ParamType;
			}
			ParamTypes.push_back(std::move(ParamType));
		}
		const std::string ReturnType = QualifyTypeName(
		    NormalizeTypeAlias(
		        SubstituteTypeName(Function.ReturnType),
		        Function.ModulePath),
		    Function.ModulePath);
		return Runtime::MakeFunctionTypeName(ParamTypes, ReturnType);
	}

	bool VeroScriptCompiler::IsLocalRef(const std::string& Name) const
	{
		for (auto ScopeIt = Scopes.rbegin(); ScopeIt != Scopes.rend(); ++ScopeIt)
		{
			if (ScopeIt->EnumConstants.count(Name) || ScopeIt->StaticLocals.count(Name)) return false;
			if (ScopeIt->LocalRefs.find(Name) != ScopeIt->LocalRefs.end())
			{
				return true;
			}
			if (ScopeIt->Locals.find(Name) != ScopeIt->Locals.end())
			{
				return false;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::TryGetMapEntryViewBinding(
	    const std::string& Name,
	    FScope::FMapEntryViewBinding& OutBinding) const
	{
		for (auto ScopeIt = Scopes.rbegin(); ScopeIt != Scopes.rend(); ++ScopeIt)
		{
			if (ScopeIt->EnumConstants.count(Name) || ScopeIt->StaticLocals.count(Name)) return false;
			const auto It = ScopeIt->LocalMapEntryViews.find(Name);
			if (It != ScopeIt->LocalMapEntryViews.end())
			{
				OutBinding = It->second;
				return true;
			}
			if (ScopeIt->Locals.find(Name) != ScopeIt->Locals.end())
			{
				return false;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::TryGetMapEntryViewMemberPath(
	    const FExpr& Expression,
	    FScope::FMapEntryViewBinding& OutBinding,
	    std::vector<std::string>& OutMembers) const
	{
		OutMembers.clear();
		const FExpr* Walk = &Expression;
		while (Walk && Walk->Kind == EExprKind::MemberAccess)
		{
			const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(*Walk);
			if (!MemberAccess.Object)
			{
				return false;
			}
			OutMembers.push_back(MemberAccess.Member);
			Walk = MemberAccess.Object.get();
		}
		if (!Walk || Walk->Kind != EExprKind::Identifier)
		{
			OutMembers.clear();
			return false;
		}
		std::reverse(OutMembers.begin(), OutMembers.end());
		const std::string& Name = static_cast<const FIdentifierExpr&>(*Walk).Name;
		if (!TryGetMapEntryViewBinding(Name, OutBinding))
		{
			OutMembers.clear();
			return false;
		}
		return true;
	}

	bool VeroScriptCompiler::EmitMapEntryViewValueFieldRef(
	    FEmitter& Emitter,
	    const FScope::FMapEntryViewBinding& Binding,
	    const std::vector<std::string>& FieldsBeyondSecond)
	{
		if (FieldsBeyondSecond.empty())
		{
			return false;
		}
		// Resolve MapEntryRef to the shared map-value object, then MakeFieldRef each nest.
		Emitter.EmitU16Operand(EOpcode::LoadLocal, Binding.RefSlot);
		for (std::size_t Index = 0; Index < FieldsBeyondSecond.size(); ++Index)
		{
			if (Index > 0)
			{
				const std::uint16_t FieldRefTemp = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::BindLocal, FieldRefTemp);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, FieldRefTemp);
			}
			Emitter.EmitU16Operand(EOpcode::MakeFieldRef, AddString(FieldsBeyondSecond[Index]));
		}
		return true;
	}

	bool VeroScriptCompiler::EmitMaterializeMapEntryPair(
	    FEmitter& Emitter,
	    const FScope::FMapEntryViewBinding& Binding,
	    const std::string& PairTypeName,
	    const FSourceLocation& Location)
	{
		std::string ResolvedLinkage;
		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!EnsureStructTypeResolved(PairTypeName, ResolvedLinkage, &TypeIndex, Location))
		{
			return false;
		}
		Emitter.EmitU16Operand(EOpcode::NewObject, TypeIndex);
		const std::uint16_t TempSlot = AllocateLocal(
		    "$pair_mat" + std::to_string(NextAssignTempId++));
		Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);

		Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, Binding.KeySlot);
		EmitValueCopy(Emitter, Module->Types[TypeIndex].Fields.at(0).TypeName);
		Emitter.EmitU16Operand(EOpcode::StoreField, AddString("first"));
		Emitter.Emit(EOpcode::Pop);

		Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, Binding.RefSlot);
		EmitValueCopy(Emitter, Module->Types[TypeIndex].Fields.at(1).TypeName);
		Emitter.EmitU16Operand(EOpcode::StoreField, AddString("second"));
		Emitter.Emit(EOpcode::Pop);

		Emitter.EmitU16Operand(EOpcode::TakeLocal, TempSlot);
		return true;
	}

	bool VeroScriptCompiler::EnsureFunctionStub(
	    const std::string& Name,
	    const std::string& ReturnType,
	    const std::uint8_t ParamCount,
	    const bool bIsMethod,
	    const std::uint16_t OwnerTypeIndex,
	    const bool bIsVirtual,
	    const FSourceLocation& Location,
	    const bool bIsConstMethod)
	{
		if (Module->FunctionIndices.find(Name) != Module->FunctionIndices.end())
		{
			return true;
		}
		if (Module->Functions.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportError("function table overflow", Location);
			return false;
		}
		FBytecodeFunction Function;
		Function.Name = Name;
		Function.ModulePath = std::string(Location.FileLabel);
		Function.bReturnsVoid = SubstituteTypeName(ReturnType) == "void";
		Function.bReturnsRef = Runtime::IsReferenceTypeName(SubstituteTypeName(ReturnType));
		Function.bIsMethod = bIsMethod;
		Function.bIsVirtual = bIsVirtual;
		Function.bIsConstMethod = bIsConstMethod;
		Function.OwnerTypeIndex = OwnerTypeIndex;
		Function.ParamCount = ParamCount;
		// Abstract interface declarations remain as stubs in the final module,
		// so their frame metadata must already satisfy bytecode invariants.
		Function.LocalCount = ParamCount;
		Function.Location = Location;
		Module->Functions.push_back(std::move(Function));
		Module->FunctionIndices.emplace(
		    Name,
		    static_cast<std::uint16_t>(Module->Functions.size() - 1));
		return true;
	}

	void VeroScriptCompiler::SetFunctionDeclarationSignatureMetadata(
	    FBytecodeFunction& Function,
	    const std::string& ReturnType,
	    const std::vector<FFunctionParam>& Parameters,
	    const std::string& OwnerModulePath)
	{
		Function.ModulePath = OwnerModulePath;
		Function.LocalCount = static_cast<std::uint16_t>(Parameters.size());
		Function.LocalNames.clear();
		Function.LocalNames.reserve(Parameters.size());
		Function.ParamFlags.clear();
		Function.ParamFlags.reserve(Parameters.size());
		std::vector<std::string> ParamTypes;
		ParamTypes.reserve(Parameters.size());
		for (const FFunctionParam& Parameter : Parameters)
		{
			Function.LocalNames.push_back(Parameter.Name);
			std::uint8_t Flags = 0;
			if (Parameter.bIsRef)
			{
				Flags |= BytecodeParamFlagRef;
				if (Parameter.bIsConst)
				{
					Flags |= BytecodeParamFlagConstRef;
				}
			}
			Function.ParamFlags.push_back(Flags);

			std::string ParamType = QualifyTypeName(
			    NormalizeTypeAlias(
			        SubstituteTypeName(Parameter.TypeName), OwnerModulePath),
			    OwnerModulePath);
			if (Parameter.bIsVariadic)
			{
				ParamType = MakeDynamicArrayTypeName(ParamType);
			}
			if (Parameter.bIsRef)
			{
				ParamType = Parameter.bIsConst
				    ? "const ref " + ParamType
				    : "ref " + ParamType;
			}
			ParamTypes.push_back(std::move(ParamType));
		}
		Function.ClosureTypeName = Runtime::MakeFunctionTypeName(
		    ParamTypes,
		    QualifyTypeName(
		        NormalizeTypeAlias(SubstituteTypeName(ReturnType), OwnerModulePath),
		        OwnerModulePath));
		Function.bReturnsRef = Runtime::IsReferenceTypeName(ReturnType);
	}

	bool VeroScriptCompiler::EnsureMethodDeclarationStub(
	    const std::string& Name,
	    const FMethodDecl& Method,
	    const std::uint16_t OwnerTypeIndex,
	    const bool bIsVirtual,
	    const std::string& OwnerModulePath)
	{
		CheckAnalysisCancellation();
		if (Method.Parameters.size() > std::numeric_limits<std::uint8_t>::max())
		{
			ReportErrorId("diag.too_many_parameters_max_255", Method.Location);
			return false;
		}
		if (Method.bDeleted)
		{
			if (!EnsureDeletedFunctionDeclaration(Name, Method.ReturnType, Method.Parameters,
			        !Method.bIsStatic, Method.bIsStatic ? 0 : OwnerTypeIndex, OwnerModulePath,
			        Method.Location, Method.bIsConst, Method.DeclMeta))
			{
				return false;
			}
		}
		else if (Method.bPureVirtual)
		{
			if (!EnsurePureVirtualFunctionDeclaration(Name, Method.ReturnType, Method.Parameters,
			        OwnerTypeIndex, OwnerModulePath, Method.Location, Method.bIsConst,
			        Method.Access, Method.DeclMeta))
			{
				return false;
			}
		}
		else
		{
			if (!EnsureFunctionStub(Name, Method.ReturnType,
			        static_cast<std::uint8_t>(Method.Parameters.size()), !Method.bIsStatic,
			        Method.bIsStatic ? 0 : OwnerTypeIndex, Method.bIsStatic ? false : bIsVirtual,
			        Method.Location, Method.bIsConst))
			{
				return false;
			}
		}
		auto& Function = Module->Functions[Module->FunctionIndices.at(Name)];
		if (Function.Code.empty())
		{
			// Binding and reflection can query declarations while an earlier body
			// is being compiled, including during a generic owner's instantiation.
			SetFunctionDeclarationSignatureMetadata(
			    Function, Method.ReturnType, Method.Parameters, OwnerModulePath);
			Function.Access = Method.Access;
			Function.bIsStaticMethod = Method.bIsStatic;
			Function.DeclMeta = Method.DeclMeta;
		}
		return true;
	}

	bool VeroScriptCompiler::RegisterStaticMethodDeclarations(
	    const std::string& OwnerTypeName,
	    const std::string& OwnerModulePath,
	    const std::vector<FMethodDecl>& Methods)
	{
		for (const auto& Method : Methods)
		{
			if (!Method.bIsStatic || !TemplateParameterNames(Method).empty())
			{
				continue;
			}
			const auto Name = MakeMethodFunctionName(OwnerTypeName, Method.Name,
			    CanonicalMethodParamTypeNames(Method, OwnerModulePath, ActiveTypeSubst), Method.bIsConst);
			if (!EnsureMethodDeclarationStub(Name, Method, 0, false, OwnerModulePath)) return false;
		}
		return true;
	}

	bool VeroScriptCompiler::EnsurePureVirtualFunctionDeclaration(
	    const std::string& Name,
	    const std::string& ReturnType,
	    const std::vector<FFunctionParam>& Parameters,
	    const std::uint16_t OwnerTypeIndex,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location,
	    const bool bIsConstMethod,
	    const EMemberAccess Access,
	    const FDeclMeta& DeclMeta)
	{
		if (!EnsureFunctionStub(
		        Name,
		        ReturnType,
		        static_cast<std::uint8_t>(Parameters.size()),
		        true,
		        OwnerTypeIndex,
		        true,
		        Location,
		        bIsConstMethod))
		{
			return false;
		}
		const auto It = Module->FunctionIndices.find(Name);
		if (It == Module->FunctionIndices.end() || It->second >= Module->Functions.size())
		{
			ReportError("failed to register pure virtual method: " + Name, Location);
			return false;
		}
		FBytecodeFunction& Function = Module->Functions[It->second];
		Function.bIsAbstract = true;
		Function.bIsVirtual = true;
		Function.bDeleted = false;
		Function.Access = Access;
		Function.Code.clear();
		SetFunctionDeclarationSignatureMetadata(
		    Function, ReturnType, Parameters, OwnerModulePath);
		Function.DeclMeta = DeclMeta;
		return true;
	}

	bool VeroScriptCompiler::EnsureDeletedFunctionDeclaration(
	    const std::string& Name,
	    const std::string& ReturnType,
	    const std::vector<FFunctionParam>& Parameters,
	    const bool bIsMethod,
	    const std::uint16_t OwnerTypeIndex,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location,
	    const bool bIsConstMethod,
	    const FDeclMeta& DeclMeta)
	{
		if (Parameters.size() > std::numeric_limits<std::uint8_t>::max())
		{
			ReportErrorId("diag.too_many_parameters_max_255", Location);
			return false;
		}
		if (!EnsureFunctionStub(
		        Name,
		        ReturnType,
		        static_cast<std::uint8_t>(Parameters.size()),
		        bIsMethod,
		        OwnerTypeIndex,
		        false,
		        Location,
		        bIsConstMethod))
		{
			return false;
		}
		const auto It = Module->FunctionIndices.find(Name);
		if (It == Module->FunctionIndices.end() || It->second >= Module->Functions.size())
		{
			ReportError("deleted declaration stub is missing: " + Name, Location);
			return false;
		}
		FBytecodeFunction& Function = Module->Functions[It->second];
		Function.bDeleted = true;
		Function.bIsAbstract = false;
		Function.Code.clear();
		SetFunctionDeclarationSignatureMetadata(
		    Function, ReturnType, Parameters, OwnerModulePath);
		Function.DeclMeta = DeclMeta;
		return true;
	}

	bool VeroScriptCompiler::CompileConstructor(
	    const std::string& TypeLinkageName,
	    const std::string& TypeSimpleName,
	    const FConstructorDecl& Constructor,
	    const std::vector<FFieldDecl>& OwnFields,
	    const std::vector<std::string>& DeclaredBaseTypeNames,
	    const std::uint16_t OwnerTypeIndex,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location)
	{
		const bool bStruct = OwnerTypeIndex < Module->Types.size() && Module->Types[OwnerTypeIndex].Kind == EScriptObjectKind::Struct;
		const bool bCopy = bStruct && IsCopyConstructor(Constructor, TypeLinkageName, OwnerModulePath);
		if (bStruct && Constructor.Parameters.size() == 1 && !Constructor.Parameters[0].bIsRef)
		{
			const auto Types = CanonicalParamTypeNames(Constructor.Parameters, OwnerModulePath, ActiveTypeSubst);
			if (Types[0] == TypeLinkageName)
			{
				ReportError("a struct constructor cannot take its own type by value; use a const reference", Location);
				return false;
			}
		}
		if (Constructor.bDefaulted && !Constructor.Parameters.empty())
		{
			if (!bCopy || OwnerTypeIndex >= Module->Types.size() || Module->Types[OwnerTypeIndex].Kind != EScriptObjectKind::Struct || Constructor.Parameters.front().DefaultValue)
			{
				ReportError("defaulted copy constructor requires a struct and one same-type const-reference parameter", Location);
				return false;
			}
			const auto Key = MakeConstructorKey(TypeLinkageName, TypeSimpleName, Constructor, OwnerModulePath, ActiveTypeSubst);
			if (!EnsureFunctionStub(Key, "void", 1, true, OwnerTypeIndex, false, Location)) return false;
			auto& Function = Module->Functions[FindFunctionIndex(Key)];
			SetFunctionDeclarationSignatureMetadata(Function, "void", Constructor.Parameters, OwnerModulePath);
			Function.SpecialMemberKind = ESpecialMemberKind::CopyConstructor;
			Function.bDefaulted = true;
			Function.bExplicitCopy = Constructor.bExplicit;
			Function.LocalCount = 1;
			Function.DeclMeta = Constructor.DeclMeta;
			Function.Code = {static_cast<std::uint8_t>(EOpcode::DefaultCopyConstruct),
			    static_cast<std::uint8_t>(EOpcode::ConstNull), static_cast<std::uint8_t>(EOpcode::Return)};
			return true;
		}
		// Deleted signatures remain visible to host construction and reflection.
		if (Constructor.bDeleted)
		{
			const auto Key = MakeConstructorKey(TypeLinkageName, TypeSimpleName, Constructor, OwnerModulePath, ActiveTypeSubst);
			if (!EnsureDeletedFunctionDeclaration(Key, "void", Constructor.Parameters, true, OwnerTypeIndex,
			        OwnerModulePath, Location, false, Constructor.DeclMeta))
			{
				return false;
			}
			auto& Function = Module->Functions[FindFunctionIndex(Key)];
			Function.Access = Constructor.Access;
			if (bCopy)
			{
				Function.SpecialMemberKind = ESpecialMemberKind::CopyConstructor;
				Function.bExplicitCopy = Constructor.bExplicit;
			}
			return true;
		}
		if (!Constructor.Body)
		{
			ReportError(
			    "constructor declared but not defined in the same file: " + TypeNameLeaf(TypeSimpleName),
			    Location);
			return false;
		}
		const std::string LeafName = TypeNameLeaf(TypeSimpleName);
		const std::string CtorName = MakeConstructorKey(
		    TypeLinkageName,
		    LeafName,
		    Constructor,
		    OwnerModulePath,
		    ActiveTypeSubst);
		if (Module->Functions.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportError("function table overflow", Location);
			return false;
		}
		if (Constructor.Parameters.size() > 255)
		{
			ReportErrorId("diag.too_many_constructor_parameters_max_255", Location);
			return false;
		}

		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		const auto ExistingIt = Module->FunctionIndices.find(CtorName);
		if (ExistingIt != Module->FunctionIndices.end())
		{
			FunctionIndex = ExistingIt->second;
			if (FunctionIndex >= Module->Functions.size())
			{
				ReportError("invalid constructor stub: " + LeafName, Location);
				return false;
			}
			if (!Module->Functions[FunctionIndex].Code.empty())
			{
				ReportErrorId("diag.duplicate_constructor", Location, {LeafName});
				return false;
			}
		}

		FBytecodeFunction* const PreviousFunction = CurrentFunction;
		std::vector<FScope> PreviousScopes = std::move(Scopes);
		std::vector<std::unordered_map<std::string, std::string>> PreviousLocalTypeAliasScopes =
		    std::move(LocalTypeAliasScopes);
		std::vector<FLoopContext> PreviousLoopStack = std::move(LoopStack);
		std::unordered_map<std::string, FLabelInfo> PreviousLabelTable = std::move(LabelTable);
		std::vector<FGotoPatch> PreviousGotoPatches = std::move(GotoPatches);
		std::vector<FLocalInitMarker> PreviousLocalInitMarkers = std::move(LocalInitMarkers);
		std::vector<std::uint16_t> PreviousActiveCatchSlots = std::move(ActiveCatchSlots);
		std::vector<std::uint32_t> PreviousActiveExceptionScopePath =
		    std::move(ActiveExceptionScopePath);
		const std::uint16_t PreviousNextLocalSlot = NextLocalSlot;
		const std::uint32_t PreviousNextAssignTempId = NextAssignTempId;
		const std::uint32_t PreviousNextExceptionScopeId = NextExceptionScopeId;
		const std::uint32_t PreviousActiveFinallyDepth = ActiveFinallyDepth;
		const std::string PreviousReturnType = CurrentFunctionReturnType;
		const std::string PreviousModulePath = CurrentModulePath;
		const std::string PreviousNamespacePrefix = CurrentNamespacePrefix;
		const std::string PreviousOwnerTypeLinkage = CurrentOwnerTypeLinkage;
		const std::string PreviousAccessFunctionLinkage = CurrentAccessFunctionLinkage;
		const FSourceLocation PreviousEmitLocation = CurrentEmitLocation;

		const auto RestoreOuterFunctionState = [&]()
		{
			Scopes = std::move(PreviousScopes);
			LocalTypeAliasScopes = std::move(PreviousLocalTypeAliasScopes);
			LoopStack = std::move(PreviousLoopStack);
			LabelTable = std::move(PreviousLabelTable);
			GotoPatches = std::move(PreviousGotoPatches);
			LocalInitMarkers = std::move(PreviousLocalInitMarkers);
			ActiveCatchSlots = std::move(PreviousActiveCatchSlots);
			ActiveExceptionScopePath = std::move(PreviousActiveExceptionScopePath);
			NextLocalSlot = PreviousNextLocalSlot;
			NextAssignTempId = PreviousNextAssignTempId;
			NextExceptionScopeId = PreviousNextExceptionScopeId;
			ActiveFinallyDepth = PreviousActiveFinallyDepth;
			CurrentFunctionReturnType = PreviousReturnType;
			CurrentModulePath = PreviousModulePath;
			CurrentNamespacePrefix = PreviousNamespacePrefix;
			CurrentOwnerTypeLinkage = PreviousOwnerTypeLinkage;
			CurrentAccessFunctionLinkage = PreviousAccessFunctionLinkage;
			CurrentEmitLocation = PreviousEmitLocation;
			CurrentFunction = PreviousFunction;
		};
		CurrentEmitLocation = Location;
		CurrentAccessFunctionLinkage = CtorName;

		if (FunctionIndex == static_cast<std::uint16_t>(-1))
		{
			FBytecodeFunction Function;
			Function.Name = CtorName;
			Function.ModulePath = OwnerModulePath;
			Function.bReturnsVoid = true;
			Function.bIsMethod = true;
			Function.bIsVirtual = false;
			Function.OwnerTypeIndex = OwnerTypeIndex;
			Function.ParamCount = static_cast<std::uint8_t>(Constructor.Parameters.size());
			Function.Location = Location;
			Function.DeclMeta = Constructor.DeclMeta;
			Module->Functions.push_back(Function);
			FunctionIndex = static_cast<std::uint16_t>(Module->Functions.size() - 1);
			Module->FunctionIndices.emplace(CtorName, FunctionIndex);
		}
		else
		{
			FBytecodeFunction& Function = Module->Functions[FunctionIndex];
			Function.Name = CtorName;
			Function.ModulePath = OwnerModulePath;
			Function.bReturnsVoid = true;
			Function.bIsMethod = true;
			Function.bIsVirtual = false;
			Function.OwnerTypeIndex = OwnerTypeIndex;
			Function.ParamCount = static_cast<std::uint8_t>(Constructor.Parameters.size());
			Function.Location = Location;
			Function.DeclMeta = Constructor.DeclMeta;
			Function.Code.clear();
			Function.LocalNames.clear();
			Function.DebugLines.clear();
			Function.ExceptionRegions.clear();
			Function.LocalCount = 0;
		}

		CurrentModulePath = OwnerModulePath;
		CurrentNamespacePrefix = NamespaceFromFunctionLinkage(CtorName);
		if (OwnerTypeIndex < Module->Types.size())
		{
			CurrentOwnerTypeLinkage = Module->Types[OwnerTypeIndex].Name;
		}
		else
		{
			CurrentOwnerTypeLinkage = TypeLinkageName;
		}
		for (const FFunctionParam& Parameter : Constructor.Parameters)
		{
			if (CompilerDetail::IsVariantIntrinsicName(Parameter.Name) && Runtime::IsFunctionTypeName(NormalizeTypeAlias(SubstituteTypeName(Parameter.TypeName))))
			{
				ReportErrorId("diag.variant_reserved_name", Location);
				RestoreOuterFunctionState();
				return false;
			}
			if (!ValidateFieldTypeName(Parameter.TypeName, Location))
			{
				RestoreOuterFunctionState();
				return false;
			}
		}

		CurrentFunction = &Module->Functions[FunctionIndex];
		if (bCopy)
		{
			CurrentFunction->SpecialMemberKind = ESpecialMemberKind::CopyConstructor;
			CurrentFunction->bExplicitCopy = Constructor.bExplicit;
		}
		SetFunctionDeclarationSignatureMetadata(*CurrentFunction, "void", Constructor.Parameters, OwnerModulePath);
		CurrentFunctionReturnType = "void";
		Scopes.clear();
		LoopStack.clear();
		LabelTable.clear();
		GotoPatches.clear();
		LocalInitMarkers.clear();
		ActiveCatchSlots.clear();
		ActiveExceptionScopePath.clear();
		ActiveFinallyDepth = 0;
		NextLocalSlot = 0;
		NextAssignTempId = 0;
		NextExceptionScopeId = 1;
		BeginScope();
		for (const FFunctionParam& Parameter : Constructor.Parameters)
		{
			AllocateLocal(Parameter.Name);
		}
		if (!Scopes.empty())
		{
			for (const FFunctionParam& Parameter : Constructor.Parameters)
			{
				Scopes.back().LocalTypes.emplace(
				    Parameter.Name,
				    QualifyTypeName(
				        NormalizeTypeAlias(SubstituteTypeName(Parameter.TypeName)),
				        CurrentModulePath));
				if (Parameter.bIsConst)
				{
					Scopes.back().LocalConsts.insert(Parameter.Name);
				}
				if (Parameter.bIsRef)
				{
					Scopes.back().LocalRefs.insert(Parameter.Name);
				}
			}
		}

		const auto DiagnosticCount = Diagnostics.size();
		const auto FailConstructorCompilation = [&]()
		{
			const bool Deferred = Constructor.bDefaulted && Constructor.Parameters.empty() && bHasError;
			if (Deferred)
			{
				// A defaulted constructor can be unavailable without invalidating an
				// unused type. Script and host calls both observe this checked entry.
				CurrentFunction->UnavailableReason = Error.Message;
				CurrentFunction->bDefaulted = true;
				CurrentFunction->Access = Constructor.Access;
				bHasError = false;
				Error = {};
				Diagnostics.resize(DiagnosticCount);
				CurrentFunction->Code.clear();
				CurrentFunction->DebugLines.clear();
				FEmitter Emitter = MakeEmitter();
				Emitter.Emit(EOpcode::ConstNull);
				Emitter.Emit(EOpcode::Return);
			}
			RestoreOuterFunctionState();
			return Deferred;
		};
		auto BaseInitMatchesDeclared = [this](const std::string& InitName, const std::string& Declared) -> bool
		{
			if (InitName == Declared)
			{
				return true;
			}
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			if (SplitUserGenericTypeApp(Declared, GenericBase, GenericArgs) && GenericBase == InitName)
			{
				return true;
			}
			return false;
		};

		std::unordered_set<std::string> UsedBaseInits;
		for (const FBaseCtorInit& BaseInit : Constructor.BaseInits)
		{
			bool bMatchedDeclared = false;
			for (const std::string& DeclaredBaseTypeName : DeclaredBaseTypeNames)
			{
				if (BaseInitMatchesDeclared(BaseInit.BaseName, DeclaredBaseTypeName))
				{
					bMatchedDeclared = true;
					break;
				}
			}
			if (!bMatchedDeclared)
			{
				ReportErrorId(
				    "diag.base_initializer_must_name_a_declared_base_type_got",
				    Location,
				    {BaseInit.BaseName});
				return FailConstructorCompilation();
			}
			if (!UsedBaseInits.insert(BaseInit.BaseName).second)
			{
				ReportErrorId(
				    "diag.duplicate_base_initializer_for",
				    Location,
				    {BaseInit.BaseName});
				return FailConstructorCompilation();
			}
		}
		if (DeclaredBaseTypeNames.empty() && !Constructor.BaseInits.empty())
		{
			ReportError("constructor base initializer requires a declared base type", Location);
			return FailConstructorCompilation();
		}

		std::unordered_set<std::string> UsedFieldInits;
		for (const FFieldCtorInit& FieldInit : Constructor.FieldInits)
		{
			const auto FieldIt = std::find_if(
			    OwnFields.begin(),
			    OwnFields.end(),
			    [&](const FFieldDecl& Field)
			{
				return Field.Name == FieldInit.FieldName;
			});
			if (FieldIt == OwnFields.end())
			{
				ReportErrorId(
				    "diag.constructor_initializer_must_name_a_direct_field",
				    FieldInit.Location,
				    {FieldInit.FieldName});
				return FailConstructorCompilation();
			}
			if (FieldIt->bIsStatic)
			{
				ReportErrorId(
				    "diag.constructor_initializer_cannot_target_static_field",
				    FieldInit.Location,
				    {FieldInit.FieldName});
				return FailConstructorCompilation();
			}
			if (FieldIt->DefaultValue)
			{
				ReportErrorId(
				    "diag.field_already_has_declaration_initializer",
				    FieldInit.Location,
				    {FieldInit.FieldName});
				return FailConstructorCompilation();
			}
			if (!UsedFieldInits.insert(FieldInit.FieldName).second)
			{
				ReportErrorId(
				    "diag.duplicate_field_constructor_initializer",
				    FieldInit.Location,
				    {FieldInit.FieldName});
				return FailConstructorCompilation();
			}
		}

		for (const std::string& DeclaredBaseTypeName : DeclaredBaseTypeNames)
		{
			if (ActiveBindings && ActiveBindings->FindType(DeclaredBaseTypeName) != nullptr)
			{
				// Host base payload is created by NewObject/@subclass — no script base ctor.
				for (const FBaseCtorInit& BaseInit : Constructor.BaseInits)
				{
					if (BaseInitMatchesDeclared(BaseInit.BaseName, DeclaredBaseTypeName))
					{
						ReportErrorId(
						    "diag.cannot_call_host_base_constructor",
						    Location,
						    {DeclaredBaseTypeName});
						return FailConstructorCompilation();
					}
				}
				continue;
			}
			std::string BaseLinkage =
			    ResolveTypeLinkageName(DeclaredBaseTypeName, OwnerModulePath);
			if (BaseLinkage.empty())
			{
				std::string Resolved;
				std::uint16_t BaseTypeIndex = static_cast<std::uint16_t>(-1);
				if (EnsureStructTypeResolved(
				        DeclaredBaseTypeName,
				        Resolved,
				        &BaseTypeIndex,
				        Location))
				{
					BaseLinkage = Resolved;
				}
			}
			if (BaseLinkage.empty())
			{
				ReportErrorId("diag.unknown_base_type", Location, {DeclaredBaseTypeName});
				return FailConstructorCompilation();
			}

			const FBaseCtorInit* MatchingInit = nullptr;
			for (const FBaseCtorInit& BaseInit : Constructor.BaseInits)
			{
				if (BaseInitMatchesDeclared(BaseInit.BaseName, DeclaredBaseTypeName))
				{
					MatchingInit = &BaseInit;
					break;
				}
			}

			const std::string BaseSimpleName = [&]() -> std::string
			{
				std::string GenericBase;
				std::vector<std::string> GenericArgs;
				if (SplitUserGenericTypeApp(DeclaredBaseTypeName, GenericBase, GenericArgs))
				{
					return GenericBase;
				}
				const std::string FromLinkage = GetTypeSimpleName(BaseLinkage);
				return FromLinkage.empty() ? DeclaredBaseTypeName : FromLinkage;
			}();

			const auto EmitBaseCtorCall =
			    [&](const std::size_t Arity, const std::vector<std::unique_ptr<FExpr>>* Args) -> bool
			{
				std::vector<std::string> CtorArgTypes;
				if (Args)
				{
					CtorArgTypes.reserve(Args->size());
					for (const std::unique_ptr<FExpr>& ArgExpr : *Args)
					{
						if (!ArgExpr)
						{
							CtorArgTypes.emplace_back();
							continue;
						}
						std::string ArgType;
						if (!TryResolveExprTypeName(*ArgExpr, ArgType))
						{
							ArgType.clear();
						}
						CtorArgTypes.push_back(EncodeOperatorArgType(*ArgExpr));
					}
				}
				else
				{
					CtorArgTypes.assign(Arity, std::string{});
				}
				const FConstructorDecl* Matching = nullptr;
				std::string BaseCtorKey;
				if (!ResolveConstructorForCall(
				        BaseLinkage,
				        CtorArgTypes,
				        Matching,
				        BaseCtorKey,
				        Location) ||
				    !Matching)
				{
					return false;
				}
				const std::uint16_t BaseCtorIndex = FindFunctionIndex(BaseCtorKey);
				if (BaseCtorIndex == static_cast<std::uint16_t>(-1))
				{
					ReportError(
					    "base constructor not found: " + BaseSimpleName,
					    Location);
					return false;
				}
				FEmitter Emitter = MakeEmitter();
				Emitter.Emit(EOpcode::LoadThis);
				if (Args)
				{
					for (std::size_t ArgIndex = 0; ArgIndex < Args->size(); ++ArgIndex)
					{
						const FExpr& Arg = *(*Args)[ArgIndex];
						if (ArgIndex < Matching->Parameters.size())
						{
							std::unordered_map<std::string, std::string> BaseSubst;
							if (!TryBuildGenericOwnerSubst(BaseLinkage, BaseSubst, Location))
							{
								ReportError(
								    "failed to resolve type arguments for base constructor: " + BaseSimpleName,
								    Location);
								return false;
							}
							const auto& Parameter = Matching->Parameters[ArgIndex];
							if (Parameter.bIsRef && Arg.Kind != EExprKind::Ref && !(Module->Types[Module->Functions[BaseCtorIndex].OwnerTypeIndex].Kind == EScriptObjectKind::Struct && IsCopyConstructor(*Matching, BaseLinkage, ResolveOwnerModulePath(BaseLinkage))))
							{
								ReportError("constructor reference arguments require an explicit ref marker", Location);
								return false;
							}
							const auto Types = CanonicalParamTypeNames(Matching->Parameters, ResolveOwnerModulePath(BaseLinkage), BaseSubst);
							if (!CompileOperatorArgument(Emitter, Arg, Types[ArgIndex], Location))
							{
								return false;
							}
						}
					}
				}
				std::unordered_map<std::string, std::string> BaseSubst;
				TryBuildGenericOwnerSubst(BaseLinkage, BaseSubst, Location);
				if (!EmitTrailingDefaultArguments(Emitter, Matching->Parameters, Arity,
				        ResolveOwnerModulePath(BaseLinkage), BaseSubst, Location))
				{
					return false;
				}
				Emitter.EmitCallBoundMethod(BaseCtorIndex, static_cast<std::uint8_t>(Matching->Parameters.size()));
				Emitter.Emit(EOpcode::Pop);
				return true;
			};

			if (MatchingInit)
			{
				const std::size_t Arity = MatchingInit->Args.size();
				if (Arity == 0 && !TypeHasUserConstructors(BaseLinkage))
				{
					FEmitter Emitter = MakeEmitter();
					if (!EmitImplicitAncestorDefaultCtors(
					        Emitter,
					        BaseLinkage,
					        Location,
					        false))
					{
						return FailConstructorCompilation();
					}
				}
				else if (!EmitBaseCtorCall(Arity, &MatchingInit->Args))
				{
					return FailConstructorCompilation();
				}
			}
			else if (TypeHasUserConstructors(BaseLinkage))
			{
				if (!TypeHasZeroArgConstructor(BaseLinkage))
				{
					ReportErrorId("diag.base_type_has_no_default_constructor_call_it_explicitly", Location, {DeclaredBaseTypeName});
					return FailConstructorCompilation();
				}
				if (!EmitBaseCtorCall(0, nullptr))
				{
					return FailConstructorCompilation();
				}
			}
			else
			{
				FEmitter Emitter = MakeEmitter();
				if (!EmitImplicitAncestorDefaultCtors(
				        Emitter,
				        BaseLinkage,
				        Location,
				        false))
				{
					return FailConstructorCompilation();
				}
			}
		}

		// Field initializers are unconditional construction operations, not ordinary
		// assignments. Emit them in declaration order before the constructor body;
		// this is the only path that may initialize a const instance field.
		const auto EmitField = [&](const FFieldDecl& Field) -> bool
		{
			if (Field.bIsStatic)
			{
				return true;
			}
			const auto InitIt = std::find_if(
			    Constructor.FieldInits.begin(),
			    Constructor.FieldInits.end(),
			    [&](const FFieldCtorInit& Init)
			{
				return Init.FieldName == Field.Name;
			});
			const std::string FieldType = QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeName(Field.TypeName)), CurrentModulePath);
			const std::string NormalizedFieldType = NormalizeTypeAlias(FieldType);
			if (InitIt == Constructor.FieldInits.end())
			{
				if (std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(),
				        [](const auto& Entry)
				{
					return Entry.first == UninitializedPayloadMetaKey;
				}))
				{
					return true;
				}
				// The receiver contains storage only. Initialize each field once, after
				// its bases and before the next field's initializer can have effects.
				const auto& Fields = Module->Types[OwnerTypeIndex].Fields;
				const auto Bound = std::find_if(Fields.begin(), Fields.end(),
				    [&](const auto& Candidate)
				{
					return Candidate.Name == Field.Name;
				});
				FEmitter Emitter = MakeEmitter();
				if (Bound != Fields.end() && Bound->bHasDefault)
				{
					const auto Value = Bound->DefaultValue;
					if (Value.Kind == EConstantKind::Bool)
					{
						Emitter.EmitU8Operand(EOpcode::ConstBool, Value.Bool ? 1 : 0);
					}
					else
					{
						const auto Opcode = Value.Kind == EConstantKind::Str || Value.Kind == EConstantKind::WStr
						    ? EOpcode::ConstStr
						    : Value.Kind == EConstantKind::F32 || Value.Kind == EConstantKind::F64
						    ? EOpcode::ConstF64
						    : EOpcode::ConstI32;
						Emitter.EmitU16Operand(Opcode, AddConstant(Value));
					}
				}
				else if (Field.DefaultValue)
				{
					// Declaration initializers cannot capture constructor parameters.
					// Keep the slot counter so their temporaries follow those parameters.
					struct FRestoreDeclarationLocals
					{
						~FRestoreDeclarationLocals()
						{
							ScopeTarget.swap(SavedScopes);
							AliasTarget.swap(SavedAliases);
						}

						std::vector<FScope>& ScopeTarget;
						std::vector<std::unordered_map<std::string, std::string>>& AliasTarget;
						std::vector<FScope> SavedScopes;
						std::vector<std::unordered_map<std::string, std::string>> SavedAliases;
					} Restore{Scopes, LocalTypeAliasScopes, {}, {}};
					Scopes.swap(Restore.SavedScopes);
					LocalTypeAliasScopes.swap(Restore.SavedAliases);
					bool Compiled = false;
					if (Field.DefaultValue->Kind == EExprKind::MapInit)
					{
						Compiled = CompileMapInit(FieldType,
						    static_cast<const FMapInitExpr&>(*Field.DefaultValue), Emitter, Field.Location);
					}
					else
					{
						std::vector<std::uint8_t> Code;
						std::vector<FDebugLine> Lines;
						Compiled = CompileFieldInitExpr(*Field.DefaultValue, FieldType, Code, &Lines);
						if (Compiled)
						{
							Emitter.AppendCode(Code, &Lines);
						}
					}
					if (!Compiled)
					{
						return false;
					}
					if (NeedsPassByValueCopy(FieldType) && !CanTransferCoercedValue(*Field.DefaultValue, FieldType))
					{
						EmitValueCopy(Emitter, FieldType);
					}
				}
				else if (Bound != Fields.end() && Bound->bHasInitCode)
				{
					// A source-free implicit constructor has no parameter slots. Retained
					// field initializer bytecode can therefore keep its original locals.
					const auto Code = Bound->InitCode;
					NextLocalSlot = (std::max)(NextLocalSlot, Bound->InitLocalCount);
					CurrentFunction->LocalCount = (std::max)(CurrentFunction->LocalCount, NextLocalSlot);
					Emitter.AppendCode(Code);
				}
				else
				{
					const auto* Host = ActiveBindings ? ActiveBindings->FindType(FieldType) : nullptr;
					bool Ambiguous = false;
					if (Field.bAllowUninitializedHostStruct && Host && Host->Kind == EHostTypeKind::Struct && !ResolveHostConstructor(FieldType, {}, Ambiguous))
					{
						Emitter.Emit(EOpcode::ConstNull);
					}
					else if (!EmitDefaultInitializedValue(Emitter, FieldType, Field.Location))
					{
						return false;
					}
				}
				Emitter.EmitU16Operand(EOpcode::InitializeThisField, AddString(Field.Name));
				return true;
			}

			const bool bScalarField = IsNumericTypeName(NormalizedFieldType) || IsEnumTypeName(NormalizedFieldType) || NormalizedFieldType == "bool" || NormalizedFieldType == "string" || NormalizedFieldType == "wstring";
			if (bScalarField && InitIt->Args.size() > 1)
			{
				ReportErrorId(
				    "diag.scalar_field_initializer_expects_at_most_one_argument",
				    InitIt->Location,
				    {Field.Name});
				return false;
			}
			if (bScalarField && InitIt->Args.empty())
			{
				FEmitter Emitter = MakeEmitter();
				if (!EmitDefaultInitializedValue(Emitter, FieldType, InitIt->Location))
				{
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::InitializeThisField, AddString(Field.Name));
				return true;
			}

			const bool bReferenceValue = InitIt->Args.size() == 1 && !IsStructType(NormalizedFieldType) && !Runtime::IsNativeArrayTypeName(NormalizedFieldType);
			std::string InitializerType;
			const bool bSameTypeValue = InitIt->Args.size() == 1 && TryResolveExprTypeName(*InitIt->Args.front(), InitializerType) && CanonicalTypeNameForCoercion(InitializerType) == CanonicalTypeNameForCoercion(NormalizedFieldType);
			if (bScalarField || bReferenceValue || bSameTypeValue)
			{
				const FExpr& Value = *InitIt->Args.front();
				const std::string PreviousExpectedType = ExpressionExpectedType;
				ExpressionExpectedType = NormalizedFieldType;
				const bool bCompiled = CompileExpression(Value);
				ExpressionExpectedType = PreviousExpectedType;
				std::string SourceType;
				if (!bCompiled || !TryResolveExprTypeName(Value, SourceType))
				{
					return false;
				}
				const std::string NormalizedSourceType = NormalizeTypeAlias(SourceType);
				FEmitter Emitter = MakeEmitter();
				if (!EmitValueCoercionBetweenTypes(
				        Emitter,
				        NormalizedFieldType,
				        NormalizedSourceType,
				        InitIt->Location,
				        true, IsConstAssignTarget(Value)))
				{
					return false;
				}
				if (NeedsPassByValueCopy(NormalizedFieldType) && !CanTransferCoercedValue(Value, NormalizedFieldType))
				{
					EmitValueCopy(Emitter, NormalizedFieldType, true);
				}
			}
			else
			{
				FCallExpr FieldConstruction(FieldType, {});
				FieldConstruction.BorrowArgumentsFrom(InitIt->Args);
				FieldConstruction.Location = InitIt->Location;
				if (!CompileExpression(FieldConstruction))
				{
					return false;
				}
			}
			FEmitter Emitter = MakeEmitter();
			Emitter.EmitU16Operand(EOpcode::InitializeThisField, AddString(Field.Name));
			return true;
		};

		std::vector<std::pair<std::string, const FFieldDecl*>> InterfaceFields;
		std::unordered_set<std::string> VisitedInterfaces;
		const auto Interfaces = Module->Types[OwnerTypeIndex].ImplementedInterfaceIndices;
		for (const auto Interface : Interfaces)
		{
			const auto Name = Module->Interfaces[Interface].Name;
			if (!CollectFlattenedInterfaceFields(Name, InterfaceFields, VisitedInterfaces, &Location))
			{
				return FailConstructorCompilation();
			}
		}
		for (const auto& [Interface, Field] : InterfaceFields)
		{
			const auto Index = FindFieldIndex(OwnerTypeIndex, Field->Name);
			if (Index == static_cast<std::uint16_t>(-1) || Index < Module->Types[OwnerTypeIndex].OwnFieldStartIndex)
			{
				continue;
			}
			// Interface fields are an owned state slice. Their initializer expressions
			// use the declaring interface's bindings and the constructor's local slots.
			const auto* Declaration = FindInterfaceDeclByLinkage(Interface);
			const auto PreviousSubst = ActiveTypeSubst;
			const auto PreviousModule = CurrentModulePath;
			const auto PreviousOwner = CurrentOwnerTypeLinkage;
			const auto PreviousAccess = CurrentAccessFunctionLinkage;
			const auto PreviousNamespace = CurrentNamespacePrefix;
			BuildInterfaceTypeSubst(Interface, ActiveTypeSubst);
			CurrentModulePath = Declaration ? Declaration->ModulePath : OwnerModulePath;
			CurrentOwnerTypeLinkage = Interface;
			CurrentAccessFunctionLinkage = Interface + "::@initializer";
			CurrentNamespacePrefix = NamespaceFromFunctionLinkage(CurrentAccessFunctionLinkage);
			const bool Compiled = EmitField(*Field);
			ActiveTypeSubst = PreviousSubst;
			CurrentModulePath = PreviousModule;
			CurrentOwnerTypeLinkage = PreviousOwner;
			CurrentAccessFunctionLinkage = PreviousAccess;
			CurrentNamespacePrefix = PreviousNamespace;
			if (!Compiled) return FailConstructorCompilation();
		}
		for (const auto& Field : OwnFields)
		{
			if (!EmitField(Field)) return FailConstructorCompilation();
		}
		if (!Constructor.Body || !CompileBlock(*Constructor.Body))
		{
			return FailConstructorCompilation();
		}
		Module->Functions[FunctionIndex].bDefaulted = Constructor.bDefaulted;
		Module->Functions[FunctionIndex].Access = Constructor.Access;
		// Constructors have an implicit void return. Emitting it unconditionally is
		// harmless after an explicit return and keeps empty constructor bodies valid
		// for bytecode serialization.
		FBytecodeConstant Zero;
		Zero.Kind = EConstantKind::I32;
		FEmitter Emitter = MakeEmitter();
		Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
		Emitter.Emit(EOpcode::Return);

		if (!ResolveAndPatchGotos())
		{
			return FailConstructorCompilation();
		}

		RestoreOuterFunctionState();
		return true;
	}

	bool VeroScriptCompiler::CompileDestructor(
	    const std::string& TypeLinkageName,
	    const std::string& TypeSimpleName,
	    const FDestructorDecl& Destructor,
	    const std::uint16_t OwnerTypeIndex,
	    const std::string& OwnerModulePath)
	{
		if (!Destructor.Body)
		{
			ReportErrorId("diag.destructor_requires_body", Destructor.Location);
			return false;
		}
		const std::string LeafName = TypeNameLeaf(TypeSimpleName);
		const std::string DtorName = MakeDestructorFunctionName(TypeLinkageName, LeafName);
		// Walk the AST base chain (declaration-order independent). Once any ancestor
		// destructor is virtual, all derived destructors are virtual.
		bool bVirtual = Destructor.bVirtual;
		if (!bVirtual && ActiveProgram)
		{
			std::vector<std::string> Queue = GetAstBaseTypeNames(TypeLinkageName);
			std::unordered_set<std::string> Visited;
			for (std::size_t Qi = 0; Qi < Queue.size() && !bVirtual; ++Qi)
			{
				const std::string Current = Queue[Qi];
				if (Current.empty() || !Visited.insert(Current).second)
				{
					continue;
				}
				const std::string Lookup = [&]() -> std::string
				{
					const std::string TemplateBase = GenericTemplateBaseLinkage(Current);
					return TemplateBase.empty() ? Current : TemplateBase;
				}();
				for (const FStructDecl& StructDecl : ActiveProgram->Structs)
				{
					if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != Lookup)
					{
						continue;
					}
					if (StructDecl.Destructor && StructDecl.Destructor->bVirtual)
					{
						bVirtual = true;
					}
					break;
				}
				for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
				{
					if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != Lookup)
					{
						continue;
					}
					if (ClassDecl.Destructor && ClassDecl.Destructor->bVirtual)
					{
						bVirtual = true;
					}
					break;
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(Current);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
			}
		}
		const bool bCompiled = CompileFunction(
		    DtorName,
		    "void",
		    {},
		    *Destructor.Body,
		    true,
		    OwnerTypeIndex,
		    OwnerModulePath,
		    Destructor.Location,
		    bVirtual,
		    nullptr,
		    false,
		    &Destructor.DeclMeta);
		if (!bCompiled)
		{
			return false;
		}
		const std::uint16_t FunctionIndex = FindFunctionIndex(DtorName);
		if (FunctionIndex == static_cast<std::uint16_t>(-1) || OwnerTypeIndex >= Module->Types.size())
		{
			ReportErrorId("diag.destructor_stub_missing", Destructor.Location, {LeafName});
			return false;
		}
		Module->Types[OwnerTypeIndex].DestructorFunctionIndex = FunctionIndex;
		Module->Types[OwnerTypeIndex].bDestructorVirtual = bVirtual;
		Module->Types[OwnerTypeIndex].bDestructorPureVirtual = Destructor.bPureVirtual;
		// A pure virtual destructor may have a callable definition, but remains an
		// abstract virtual slot in declaration metadata. This also permits a truly
		// empty body to serialize without being mistaken for a missing definition.
		Module->Functions[FunctionIndex].bIsAbstract = Destructor.bPureVirtual;
		Module->Functions[FunctionIndex].bDefaulted = Destructor.bDefaulted;
		Module->Functions[FunctionIndex].Access = Destructor.Access;
		return true;
	}

	void VeroScriptCompiler::PatchRelativeJump(FEmitter& Emitter, const std::size_t OperandOffset)
	{
		Emitter.PatchI32(
		    OperandOffset,
		    Emitter.CurrentOffset() - static_cast<std::int32_t>(OperandOffset + 4));
	}

	void VeroScriptCompiler::CloseLoop(
	    FEmitter& Emitter,
	    const std::size_t JumpIfFalseOperand,
	    const bool bHasConditionJump)
	{
		if (LoopStack.empty())
		{
			return;
		}

		const FLoopContext& Loop = LoopStack.back();
		if (bHasConditionJump)
		{
			PatchRelativeJump(Emitter, JumpIfFalseOperand);
		}
		for (const std::size_t BreakPatch : Loop.BreakPatches)
		{
			PatchRelativeJump(Emitter, BreakPatch);
		}
		LoopStack.pop_back();
	}
}
