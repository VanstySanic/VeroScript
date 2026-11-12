// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerStackGuard.h"
#include "VeroScriptVariant.h"
#include "VeroScriptBytecodeLinker.h"

#include "VeroScriptAst.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptPackageInterface.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptNumericBindings.h"
#include "Processing/VeroScriptSourceMetadataIO.h"

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
	void VeroScriptCompiler::SetCollectMultipleErrors(const bool bEnabled, const std::size_t MaxErrors)
	{
		bCollectMultipleErrors = bEnabled;
		MaxCollectedErrors = MaxErrors == 0 ? 1 : MaxErrors;
	}
	VeroScriptCompiler::FEmitter VeroScriptCompiler::MakeEmitter()
	{
		if (CurrentFunction)
		{
			return FEmitter(CurrentFunction->Code, &CurrentFunction->DebugLines, &CurrentEmitLocation,
			    Module ? &Module->DebugFiles : nullptr, ActiveNatives, Module ? &Module->NativeReferences : nullptr);
		}
		return FEmitter(Module->InitCode, &Module->InitDebugLines, &CurrentEmitLocation, &Module->DebugFiles,
		    ActiveNatives, &Module->NativeReferences);
	}

	void VeroScriptCompiler::ReportError(std::string Message, const FSourceLocation& Location) const
	{
		if (!bHasError)
		{
			bHasError = true;
			FillFromEnglishMessage(Error, std::move(Message), Location);
			CommitCurrentError();
		}
	}

	void VeroScriptCompiler::ReportErrorId(
	    const std::string_view MessageId,
	    const FSourceLocation& Location,
	    const std::initializer_list<FDiagnosticArg> Args) const
	{
		if (!bHasError)
		{
			bHasError = true;
			FillDiagnostic(Error, MessageId, Location, Args);
			CommitCurrentError();
		}
	}

	void VeroScriptCompiler::CommitCurrentError() const
	{
		const auto OriginalLabel = Error.Location.FileLabel;
		const auto OriginalLine = Error.Location.Line;
		const bool Mapped = ActiveProgram && ActiveProgram->SourceMetadata &&
		    MapGeneratedDiagnostic(*ActiveProgram->SourceMetadata, Error);
		if (ActiveProgram)
		{
			for (const auto& Document : ActiveProgram->GeneratedDocuments)
			{
				if (Document.FileLabel == OriginalLabel)
				{
					Error.Message += " [generated " + Document.GenerationId + ":" + std::to_string(OriginalLine) + "]";
					if (!Mapped)
					{
						Error.Location =
						    FSourceLocation{Document.Origin.FileLabel, Document.Origin.Line, Document.Origin.Column};
					}
					break;
				}
			}
		}

		if (Error.Message.empty())
		{
			return;
		}
		if (Diagnostics.size() >= MaxCollectedErrors)
		{
			return;
		}
		for (const FScriptError& Existing : Diagnostics)
		{
			if (Existing.Message == Error.Message && Existing.MessageId == Error.MessageId &&
			    Existing.Location.Line == Error.Location.Line && Existing.Location.Column == Error.Location.Column &&
			    Existing.Location.FileLabel == Error.Location.FileLabel)
			{
				return;
			}
		}
		Diagnostics.push_back(Error);
	}

	bool VeroScriptCompiler::RecoverAfterCompileUnitError()
	{
		if (bHasError)
		{
			CommitCurrentError();
		}
		if (!bCollectMultipleErrors || Diagnostics.size() >= MaxCollectedErrors)
		{
			if (!Diagnostics.empty())
			{
				Error = Diagnostics.front();
			}
			bHasError = true;
			return false;
		}
		bHasError = false;
		Error = {};
		return true;
	}

	std::uint16_t VeroScriptCompiler::AddString(const std::string_view Value)
	{
		const std::string Key(Value);
		for (std::size_t Index = 0; Index < Module->StringPool.size(); ++Index)
		{
			if (Module->StringPool[Index] == Key)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		if (Module->StringPool.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportErrorId("diag.string_pool_overflow");
			return 0;
		}
		Module->StringPool.push_back(Key);
		return static_cast<std::uint16_t>(Module->StringPool.size() - 1);
	}

	std::uint16_t VeroScriptCompiler::AddWString(const std::u16string_view Value)
	{
		const std::u16string Key(Value);
		for (std::size_t Index = 0; Index < Module->WStringPool.size(); ++Index)
		{
			if (Module->WStringPool[Index] == Key)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		if (Module->WStringPool.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportErrorId("diag.wstring_pool_overflow");
			return 0;
		}
		Module->WStringPool.push_back(Key);
		return static_cast<std::uint16_t>(Module->WStringPool.size() - 1);
	}

	std::uint16_t VeroScriptCompiler::AddConstant(const FBytecodeConstant& Constant)
	{
		if (Module->Constants.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportErrorId("diag.constant_pool_overflow");
			return 0;
		}
		Module->Constants.push_back(Constant);
		return static_cast<std::uint16_t>(Module->Constants.size() - 1);
	}

	std::uint16_t VeroScriptCompiler::AddGlobal(const std::string& Name)
	{
		const auto Existing = Module->GlobalSlots.find(Name);
		if (Existing != Module->GlobalSlots.end())
		{
			return Existing->second;
		}
		if (Module->GlobalNames.size() >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportErrorId("diag.global_table_overflow");
			return 0;
		}
		const std::uint16_t Slot = static_cast<std::uint16_t>(Module->GlobalNames.size());
		Module->GlobalNames.push_back(Name);
		Module->GlobalTypeNames.emplace_back();
		Module->GlobalMetadata.emplace_back();
		Module->GlobalSlots.emplace(Name, Slot);
		return Slot;
	}

	const std::vector<std::size_t>& VeroScriptCompiler::FindDeclarationCandidates(
	    const FDeclarationIndex& Index, const std::string& Linkage)
	{
		static const std::vector<std::size_t> Empty;
		const auto Found = Index.find(Linkage);
		return Found == Index.end() ? Empty : Found->second;
	}

	std::uint16_t VeroScriptCompiler::FindRegisteredTypeIndex(const std::string& TypeName) const
	{
		if (!Module) return static_cast<std::uint16_t>(-1);
		if (IndexedTypeModule != Module.get() || IndexedTypeCount > Module->Types.size())
		{
			RegisteredTypeIndices.clear();
			IndexedTypeModule = Module.get();
			IndexedTypeCount = 0;
		}
		while (IndexedTypeCount < Module->Types.size())
		{
			RegisteredTypeIndices.emplace(Module->Types[IndexedTypeCount].Name,
			    static_cast<std::uint16_t>(IndexedTypeCount));
			++IndexedTypeCount;
		}
		const auto Found = RegisteredTypeIndices.find(TypeName);
		return Found == RegisteredTypeIndices.end() ? static_cast<std::uint16_t>(-1) : Found->second;
	}

	std::uint16_t VeroScriptCompiler::FindOrAddType(const std::string& TypeName)
	{
		const auto Registered = FindRegisteredTypeIndex(TypeName);
		if (Registered != static_cast<std::uint16_t>(-1)) return Registered;

		if (TypesInProgress.find(TypeName) != TypesInProgress.end())
		{
			ReportErrorId("diag.recursive_type_or_inheritance_cycle", {}, {TypeName});
			return static_cast<std::uint16_t>(-1);
		}

		FBytecodeTypeDesc TypeDesc;
		TypeDesc.Name = TypeName;

		std::vector<const FStructDecl*> StructDeclarations;
		const auto& StructCandidates = FindDeclarationCandidates(SourceStructIndices, TypeName);
		StructDeclarations.reserve(StructCandidates.size() + BuiltinStructTemplates.size());
		for (const auto Index : StructCandidates)
		{
			StructDeclarations.push_back(&ActiveProgram->Structs[Index]);
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			StructDeclarations.push_back(&StructDecl);
		}
		for (const FStructDecl* const StructDeclPtr : StructDeclarations)
		{
			const FStructDecl& StructDecl = *StructDeclPtr;
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != TypeName)
			{
				continue;
			}
			if (!TemplateParameterNames(StructDecl).empty())
			{
				ReportErrorId("diag.generic_struct_requires_type_arguments", {}, {StructDecl.Name});
				return static_cast<std::uint16_t>(-1);
			}
			const std::string PreviousModulePath = CurrentModulePath;
			CurrentModulePath = StructDecl.ModulePath;
			TypesInProgress.insert(TypeName);
			TypeDesc.Kind = EScriptObjectKind::Struct;
			TypeDesc.bIsStatic = StructDecl.bIsStatic;
			TypeDesc.DeclMeta = StructDecl.DeclMeta;
			PopulateSourceTypeScope(StructDecl.Scope, {}, TypeDesc.Scope, TypeDesc.NestedTypes);
			for (const FUsingMemberDecl& Using : StructDecl.UsingMembers)
			{
				TypeDesc.UsingMembers.push_back({Using.BaseTypeName,
				    Using.MemberName, Using.Access});
			}
			const std::vector<std::string> StructBases = StructDecl.BaseTypeName.empty()
			    ? std::vector<std::string>{}
			    : std::vector<std::string>{StructDecl.BaseTypeName};
			if (!AppendInheritedAndOwnFields(
			        TypeDesc,
			        TypeName,
			        StructBases,
			        EScriptObjectKind::Struct,
			        StructDecl.Fields,
			        StructDecl.ModulePath,
			        StructDecl.Location,
			        StructDecl.InterfaceTypeNames,
			        std::vector<EMemberAccess>{StructDecl.BaseInheritanceAccess}))
			{
				TypesInProgress.erase(TypeName);
				CurrentModulePath = PreviousModulePath;
				return static_cast<std::uint16_t>(-1);
			}
			for (const std::uint16_t BaseTypeIndex : TypeDesc.BaseTypeIndices)
			{
				if (BaseTypeIndex >= Module->Types.size())
				{
					continue;
				}
				for (const std::uint16_t InheritedIface :
				    Module->Types[BaseTypeIndex].ImplementedInterfaceIndices)
				{
					bool bAlready = false;
					for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
					{
						if (Existing == InheritedIface)
						{
							bAlready = true;
							break;
						}
					}
					if (!bAlready)
					{
						TypeDesc.ImplementedInterfaceIndices.push_back(InheritedIface);
					}
				}
			}
			{
				std::vector<std::uint16_t> OwnInterfaces;
				if (!ResolveInheritanceInterfaces(
				        StructDecl.InterfaceTypeNames,
				        StructDecl.ModulePath,
				        StructDecl.Location,
				        OwnInterfaces))
				{
					TypesInProgress.erase(TypeName);
					CurrentModulePath = PreviousModulePath;
					return static_cast<std::uint16_t>(-1);
				}
				for (const std::uint16_t InterfaceIndex : OwnInterfaces)
				{
					bool bAlready = false;
					for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
					{
						if (Existing == InterfaceIndex)
						{
							bAlready = true;
							break;
						}
					}
					if (!bAlready)
					{
						TypeDesc.ImplementedInterfaceIndices.push_back(InterfaceIndex);
					}
				}
			}
			if (!VerifyInterfaceConformance(
			        TypeName,
			        StructDecl.InterfaceTypeNames,
			        TypeDesc.ImplementedInterfaceIndices,
			        StructDecl.ModulePath,
			        StructDecl.Location))
			{
				TypesInProgress.erase(TypeName);
				CurrentModulePath = PreviousModulePath;
				return static_cast<std::uint16_t>(-1);
			}
			TypesInProgress.erase(TypeName);
			CurrentModulePath = PreviousModulePath;
			Module->Types.push_back(std::move(TypeDesc));
			return static_cast<std::uint16_t>(Module->Types.size() - 1);
		}

		for (const auto Index : FindDeclarationCandidates(SourceClassIndices, TypeName))
		{
			const auto& ClassDecl = ActiveProgram->Classes[Index];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != TypeName)
			{
				continue;
			}
			if (!TemplateParameterNames(ClassDecl).empty())
			{
				ReportErrorId("diag.generic_class_requires_type_arguments", {}, {ClassDecl.Name});
				return static_cast<std::uint16_t>(-1);
			}
			const std::string PreviousModulePath = CurrentModulePath;
			CurrentModulePath = ClassDecl.ModulePath;
			TypesInProgress.insert(TypeName);
			TypeDesc.Kind = EScriptObjectKind::Class;
			TypeDesc.DeclMeta = ClassDecl.DeclMeta;
			PopulateSourceTypeScope(ClassDecl.Scope, {}, TypeDesc.Scope, TypeDesc.NestedTypes);
			for (const FUsingMemberDecl& Using : ClassDecl.UsingMembers)
			{
				TypeDesc.UsingMembers.push_back({Using.BaseTypeName,
				    Using.MemberName, Using.Access});
			}
			TypeDesc.bIsStatic = ClassDecl.bIsStatic;
			if (!CheckDiamondInheritance(
			        ClassDecl.BaseTypeNames,
			        ClassDecl.ModulePath,
			        ClassDecl.Location))
			{
				TypesInProgress.erase(TypeName);
				CurrentModulePath = PreviousModulePath;
				return static_cast<std::uint16_t>(-1);
			}
			if (!AppendInheritedAndOwnFields(
			        TypeDesc,
			        TypeName,
			        ClassDecl.BaseTypeNames,
			        EScriptObjectKind::Class,
			        ClassDecl.Fields,
			        ClassDecl.ModulePath,
			        ClassDecl.Location,
			        ClassDecl.InterfaceTypeNames,
			        ClassDecl.BaseInheritanceAccess))
			{
				TypesInProgress.erase(TypeName);
				CurrentModulePath = PreviousModulePath;
				return static_cast<std::uint16_t>(-1);
			}
			for (const std::uint16_t BaseTypeIndex : TypeDesc.BaseTypeIndices)
			{
				if (BaseTypeIndex >= Module->Types.size())
				{
					continue;
				}
				for (const std::uint16_t InheritedIface :
				    Module->Types[BaseTypeIndex].ImplementedInterfaceIndices)
				{
					bool bAlready = false;
					for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
					{
						if (Existing == InheritedIface)
						{
							bAlready = true;
							break;
						}
					}
					if (!bAlready)
					{
						TypeDesc.ImplementedInterfaceIndices.push_back(InheritedIface);
					}
				}
			}
			{
				std::vector<std::uint16_t> OwnInterfaces;
				if (!ResolveInheritanceInterfaces(
				        ClassDecl.InterfaceTypeNames,
				        ClassDecl.ModulePath,
				        ClassDecl.Location,
				        OwnInterfaces))
				{
					TypesInProgress.erase(TypeName);
					CurrentModulePath = PreviousModulePath;
					return static_cast<std::uint16_t>(-1);
				}
				for (const std::uint16_t InterfaceIndex : OwnInterfaces)
				{
					bool bAlready = false;
					for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
					{
						if (Existing == InterfaceIndex)
						{
							bAlready = true;
							break;
						}
					}
					if (!bAlready)
					{
						TypeDesc.ImplementedInterfaceIndices.push_back(InterfaceIndex);
					}
				}
			}
			if (!VerifyInterfaceConformance(
			        TypeName,
			        ClassDecl.InterfaceTypeNames,
			        TypeDesc.ImplementedInterfaceIndices,
			        ClassDecl.ModulePath,
			        ClassDecl.Location))
			{
				TypesInProgress.erase(TypeName);
				CurrentModulePath = PreviousModulePath;
				return static_cast<std::uint16_t>(-1);
			}
			TypesInProgress.erase(TypeName);
			CurrentModulePath = PreviousModulePath;
			Module->Types.push_back(std::move(TypeDesc));
			return static_cast<std::uint16_t>(Module->Types.size() - 1);
		}

		ReportErrorId("diag.unknown_type", {}, {TypeName});
		return static_cast<std::uint16_t>(-1);
	}

	bool VeroScriptCompiler::ValidateFieldTypeName(const std::string& TypeName, const FSourceLocation& Location)
	{
		// Include aliases and container/callable wrappers in the native recursion
		// budget, even when they do not introduce another layout in TypesInProgress.
		static thread_local unsigned ValidationDepth = 0;
		if (ValidationDepth >= 64 || !HasCompilerTypeStackHeadroom())
		{
			ReportError("type resolution nesting limit exceeded", Location);
			return false;
		}
		struct FDepthRestore
		{
			unsigned& Depth;
			~FDepthRestore() { --Depth; }
		} DepthRestore{ValidationDepth};
		++ValidationDepth;
		if (TypeName == "variant") return true;
		{
			const std::string Expanded = ExpandHostTypeAlias(TypeName);
			if (Expanded != TypeName)
			{
				return ValidateFieldTypeName(Expanded, Location);
			}
		}
		{
			const std::string Expanded = ExpandUserTypeAlias(TypeName, CurrentModulePath);
			if (Expanded != TypeName)
			{
				return ValidateFieldTypeName(Expanded, Location);
			}
		}
		const std::string EffectiveTypeName = SubstituteTypeName(TypeName);
		if (EffectiveTypeName != TypeName)
		{
			return ValidateFieldTypeName(EffectiveTypeName, Location);
		}
		// Completed arguments may name a private declaration whose layout is
		// still being built. Resolve its arity in the identity's owning module.
		const auto IdentityStart = FindLinkageIdentityStart(TypeName);
		const std::string LookupTypeName = IdentityStart != 0 ? TypeName.substr(IdentityStart)
		    : TypeName.rfind("::", 0) == 0                    ? TypeName.substr(2)
		                                                      : TypeName;
		const std::string IdentityModule = IdentityStart != 0 ? TypeName.substr(0, IdentityStart - 1) : std::string{};
		const std::string& LookupModule = IdentityStart != 0 ? IdentityModule : CurrentModulePath;
		// Substitution values are canonical identities resolved at the call site.
		// In particular an exported caller type need not be imported by the
		// generic's declaration module. Do not perform another lexical lookup.
		for (const auto& Binding : ActiveTypeSubst)
		{
			if (Binding.second != TypeName || !ResolveTypeLinkageName(TypeName, CurrentModulePath).empty())
			{
				continue;
			}
			for (const auto& Decl : ActiveProgram->Classes)
			{
				if (TemplateParameterNames(Decl).empty() &&
				    GetSymbolLinkageName(Decl.ModulePath, Decl.bExported, Decl.Name) == TypeName)
				{
					if (TypesInProgress.find(TypeName) != TypesInProgress.end())
					{
						return true;
					}
					return FindOrAddType(TypeName) != static_cast<std::uint16_t>(-1);
				}
			}
			for (const auto& Decl : ActiveProgram->Structs)
			{
				if (TemplateParameterNames(Decl).empty() &&
				    GetSymbolLinkageName(Decl.ModulePath, Decl.bExported, Decl.Name) == TypeName)
				{
					return FindOrAddType(TypeName) != static_cast<std::uint16_t>(-1);
				}
			}
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (!Runtime::ParseFunctionType(TypeName, ParamTypes, ReturnType))
			{
				ReportError("invalid function type: " + TypeName, Location);
				return false;
			}
			for (const std::string& ParamType : ParamTypes)
			{
				const std::string ValueType = ParamType.rfind("const ref ", 0) == 0
				    ? ParamType.substr(10)
				    : (ParamType.rfind("ref ", 0) == 0
				              ? ParamType.substr(4)
				              : ParamType);
				if (!ValidateFieldTypeName(ValueType, Location))
				{
					return false;
				}
			}
			if (ReturnType == "void")
			{
				return true;
			}
			// References are valid here as part of the callable signature, while
			// standalone fields/locals still store the callable handle by value.
			// Validate the referenced value type rather than treating the internal
			// canonical `ref T` spelling as a concrete storage type.
			return ValidateFieldTypeName(
			    std::string(Runtime::StripReferenceTypeName(ReturnType)),
			    Location);
		}

		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
			{
				ReportError("invalid dynamic array type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (Runtime::IsMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!ParseMapTypeForCompile(TypeName, KeyType, ValueType))
			{
				ReportError("invalid map type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(KeyType, Location) && ValidateFieldTypeName(ValueType, Location);
		}

		if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			std::string KeyType;
			std::string ValueType;
			if (!ParseOrderedMapTypeForCompile(TypeName, KeyType, ValueType))
			{
				ReportError("invalid ordered_map type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(KeyType, Location) && ValidateFieldTypeName(ValueType, Location);
		}

		if (Runtime::IsSetTypeName(TypeName))
		{
			std::string ElementType;
			if (!ParseSetElementTypeForCompile(TypeName, ElementType))
			{
				ReportError("invalid set type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (Runtime::IsMultiSetTypeName(TypeName) || Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseMultiSetElementType(TypeName, ElementType) || !IsAllowedMapOrSetKeyTypeForCompile(ElementType))
			{
				ReportError("invalid multiset element type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (Runtime::IsQueueTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseQueueElementType(TypeName, ElementType))
			{
				ReportError("invalid queue type: " + TypeName, Location);
				return false;
			}
			if (!ValidateFieldTypeName(ElementType, Location))
			{
				return false;
			}
			if (Runtime::IsPriorityQueueTypeName(TypeName) && !Runtime::IsAllowedPriorityQueueElementType(ElementType))
			{
				ReportError(
				    "priority_queue element type must have a built-in total order: " + ElementType,
				    Location);
				return false;
			}
			return true;
		}

		if (Runtime::IsStackTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseStackElementType(TypeName, ElementType))
			{
				ReportError("invalid stack type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (Runtime::IsDequeTypeName(TypeName))
		{
			std::string ElementType;
			if (!Runtime::ParseDequeElementType(TypeName, ElementType))
			{
				ReportError("invalid deque type: " + TypeName, Location);
				return false;
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (Runtime::IsChannelTypeName(TypeName))
		{
			if (Variant::ContainsType(TypeName))
			{
				ReportErrorId("diag.variant_not_allowed_in_channel", Location);
				return false;
			}
			std::string ElementType;
			if (!Runtime::ParseChannelElementType(TypeName, ElementType))
			{
				// The parser permits channel<T> only while parsing a generic
				// declaration. The concrete instantiation validates T after
				// ActiveTypeSubst has been applied.
				return true;
			}
			return true;
		}

		if (Runtime::IsScriptArgsArrayTypeName(TypeName))
		{
			return true;
		}

		if (Runtime::IsNativeArrayTypeName(TypeName))
		{
			std::string ElementType;
			std::uint32_t Length = 0;
			if (!Runtime::ParseNativeArrayType(TypeName, ElementType, Length))
			{
				ReportError("invalid array type: " + TypeName, Location);
				return false;
			}
			std::string LimitError;
			if (!Runtime::ValidateNativeArrayTypeLimits(TypeName, &LimitError))
			{
				ReportErrorId("diag.native_array_size_exceeds_limit", Location);
				return false;
			}
			// Dimensions have their own validated bound. Peel that shape without
			// spending one native/type-dependency stack frame per dimension.
			while (Runtime::IsNativeArrayTypeName(ElementType))
			{
				std::string NextElement;
				if (!Runtime::ParseNativeArrayType(ElementType, NextElement, Length))
				{
					ReportError("invalid array type: " + TypeName, Location);
					return false;
				}
				ElementType = std::move(NextElement);
			}
			return ValidateFieldTypeName(ElementType, Location);
		}

		if (TypeName == "auto")
		{
			// Inference placeholder only for local/global `auto x = …` and range-for
			// bindings — never a concrete field, parameter, or nested type argument.
			ReportErrorId("diag.auto_not_valid_type_here", Location);
			return false;
		}

		static constexpr const char* PrimitiveTypes[] = {
		    "bool",
		    "char",
		    "short",
		    "int",
		    "long",
		    "int64",
		    "float",
		    "double",
		    "string",
		    "wstring",
		    "unsigned",
		    "unsigned char",
		    "unsigned short",
		    "unsigned int",
		    "unsigned long",
		    "uint64",
		};
		for (const char* PrimitiveType : PrimitiveTypes)
		{
			if (TypeName == PrimitiveType)
			{
				return true;
			}
		}

		if (IsEnumTypeName(LookupTypeName))
		{
			return true;
		}

		if (ActiveBindings && ActiveBindings->FindType(LookupTypeName) != nullptr)
		{
			return true;
		}
		{
			std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
			if (const FNativeFunction* const Native = FindNative(LookupTypeName, NativeIndex);
			    Native && Native->ReturnType == ENativeType::HostObject)
			{
				return true;
			}
		}

		if (!Module)
		{
			std::string Base;
			std::vector<std::string> Arguments;
			if (!SplitUserGenericTypeApp(TypeName, Base, Arguments))
			{
				Base = GenericTemplateBaseLinkage(TypeName);
				if (Base.empty())
				{
					Base = LookupTypeName;
				}
				else
				{
					Arguments = DecodeGenericConcreteTypes(TypeName);
				}
			}
			auto ReferenceModule = CurrentModulePath;
			if (const auto Start = FindLinkageIdentityStart(Base); Start != 0)
			{
				ReferenceModule = Base.substr(0, Start - 1);
				Base.erase(0, Start);
			}
			const FTemplateParameterList* Schema = nullptr;
			std::string DeclarationModule;
			const auto Select = [&](const auto* Declaration)
			{
				if (Declaration)
				{
					Schema = &Declaration->TemplateParameters;
					DeclarationModule = Declaration->ModulePath;
				}
			};
			Select(FindGenericStructDecl(Base, ReferenceModule, Arguments.size()));
			if (!Schema)
			{
				Select(FindGenericClassDecl(Base, ReferenceModule, Arguments.size()));
			}
			if (!Schema)
			{
				Select(FindGenericInterfaceDecl(Base, ReferenceModule, Arguments.size()));
			}
			if (Schema)
			{
				if (!Schema->IsEmpty() && !CompleteTemplateArguments(*Schema, DeclarationModule, Arguments, Location)) return false;
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if ((Schema->IsEmpty() || Schema->GetParameters()[Index].Kind == ETemplateParameterKind::Type) && Arguments[Index] != "void" && !ValidateFieldTypeName(Arguments[Index], Location))
					{
						return false;
					}
				}
				return true;
			}
		}

		// A class field stores a nullable handle, so validating its closed type
		// does not require the owner's unfinished layout. Its template arguments
		// and bounds were checked before the layout entered TypesInProgress.
		// Keep full instantiation checks for structs and for layout consumers.
		if (!TypesInProgress.empty() &&
		    (TypeName.find('<') != std::string::npos || FindGenericTypeMarker(TypeName) != std::string::npos))
		{
			const std::string FieldType = QualifyTypeName(TypeName, CurrentModulePath);
			if (TypesInProgress.count(FieldType) != 0 && IsClassOrStructType(FieldType) && !IsStructType(FieldType))
			{
				return !bHasError;
			}
		}

		{
			std::string ResolvedLinkage;
			if (EnsureStructTypeResolved(TypeName, ResolvedLinkage, nullptr, Location))
			{
				return true;
			}
			if (bHasError)
			{
				return false;
			}
		}
		{
			std::string ResolvedIface;
			if (EnsureInterfaceTypeResolved(TypeName, ResolvedIface, nullptr, Location))
			{
				return true;
			}
			if (bHasError)
			{
				return false;
			}
		}

		const std::string LinkageName = ResolveTypeLinkageName(TypeName, CurrentModulePath);
		if (!LinkageName.empty())
		{
			const bool bHasArity0Interface =
			    FindGenericInterfaceDecl(LookupTypeName, LookupModule, 0) != nullptr;
			const bool bHasArity0Struct =
			    FindGenericStructDecl(LookupTypeName, LookupModule, 0) != nullptr;
			const bool bHasArity0Class =
			    FindGenericClassDecl(LookupTypeName, LookupModule, 0) != nullptr;
			const auto HasGenericArity =
			    [&](const auto& Decls) -> bool
			{
				for (const auto& Decl : Decls)
				{
					if (Decl.Name == LookupTypeName && !TemplateParameterNames(Decl).empty() && CanAccessSymbol(Decl.ModulePath, Decl.bExported, LookupModule))
					{
						return true;
					}
				}
				return false;
			};
			if (IsInterfaceTypeName(LinkageName) || IsInterfaceTypeName(LookupTypeName) || HasGenericArity(ActiveProgram->Interfaces))
			{
				if (bHasArity0Interface)
				{
					return true;
				}
				if (HasGenericArity(ActiveProgram->Interfaces))
				{
					ReportErrorId(
					    "diag.generic_interface_requires_type_arguments",
					    Location,
					    {TypeName});
					return false;
				}
				return true;
			}
			if (!bHasArity0Struct && HasGenericArity(ActiveProgram->Structs))
			{
				ReportErrorId("diag.generic_struct_requires_type_arguments", Location, {TypeName});
				return false;
			}
			{
				bool bBuiltinGenericStruct = false;
				for (const FStructDecl& StructDecl : BuiltinStructTemplates)
				{
					if (StructDecl.Name == LookupTypeName && !TemplateParameterNames(StructDecl).empty())
					{
						bBuiltinGenericStruct = true;
						break;
					}
				}
				// Soft pair<T,U> coexists with a non-generic class/struct pair: bare `pair`
				// binds the concrete type; `pair<K,V>` uses the soft template.
				if (!bHasArity0Struct && !bHasArity0Class && bBuiltinGenericStruct)
				{
					ReportErrorId("diag.generic_struct_requires_type_arguments", Location, {TypeName});
					return false;
				}
			}
			if (!bHasArity0Class && HasGenericArity(ActiveProgram->Classes))
			{
				ReportErrorId("diag.generic_class_requires_type_arguments", Location, {TypeName});
				return false;
			}
			{
				bool bBuiltinGenericClass = false;
				for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
				{
					if (ClassDecl.Name == LookupTypeName && !TemplateParameterNames(ClassDecl).empty())
					{
						bBuiltinGenericClass = true;
						break;
					}
				}
				if (!bHasArity0Class && bBuiltinGenericClass)
				{
					ReportErrorId("diag.generic_class_requires_type_arguments", Location, {TypeName});
					return false;
				}
			}
			if (!bHasArity0Struct && !bHasArity0Class)
			{
				ReportError("unknown field type: " + TypeName, Location);
				return false;
			}
			// Class fields may reference the type currently being built (handle semantics).
			// Struct fields may not embed themselves (value layout).
			if (TypesInProgress.find(LinkageName) != TypesInProgress.end())
			{
				for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
				{
					if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LinkageName)
					{
						return true;
					}
				}
				ReportError("recursive type: " + LinkageName, Location);
				return false;
			}
			return FindOrAddType(LinkageName) != static_cast<std::uint16_t>(-1);
		}

		if (!bHasError)
		{
			ReportError("unknown field type: " + TypeName, Location);
		}
		return false;
	}

	std::uint16_t VeroScriptCompiler::FindFunctionIndex(const std::string& Name) const
	{
		const auto It = Module->FunctionIndices.find(Name);
		return It == Module->FunctionIndices.end() ? static_cast<std::uint16_t>(-1) : It->second;
	}

	const FNativeFunction* VeroScriptCompiler::FindNative(
	    const std::string& Name,
	    std::uint16_t& OutIndex) const
	{
		if (!ActiveNatives)
		{
			return nullptr;
		}
		auto LookupNames = !Name.empty() && Name.front() == '\x1f'
		    ? std::vector<std::string>{Name}
		    : BuildFreeFunctionLookupNames(Name, CurrentModulePath);
		if (LookupNames.empty())
		{
			LookupNames.push_back(Name);
		}
		for (const auto& Lookup : LookupNames)
		{
			for (std::size_t Index = 0; Index < ActiveNatives->size(); ++Index)
			{
				if (NativeNameMatchesLookup((*ActiveNatives)[Index].Name, Lookup))
				{
					const auto& Native = (*ActiveNatives)[Index];
					if (!CanAccessNativeBinding(Native.Origin, !Native.Name.empty() && Native.Name.front() == '\x1f' ? Native.HostTypeName : std::string{}))
					{
						continue;
					}
					OutIndex = static_cast<std::uint16_t>(Index);
					return &Native;
				}
			}
		}
		return nullptr;
	}

	bool VeroScriptCompiler::ResolveGlobal(const std::string& Name, std::uint16_t& OutSlot) const
	{
		const auto It = Module->GlobalSlots.find(Name);
		if (It == Module->GlobalSlots.end())
		{
			return false;
		}
		OutSlot = It->second;
		return true;
	}

	std::uint16_t VeroScriptCompiler::FindFieldIndex(const std::uint16_t TypeIndex, const std::string& FieldName) const
	{
		if (TypeIndex >= Module->Types.size())
		{
			return static_cast<std::uint16_t>(-1);
		}
		const FBytecodeTypeDesc& TypeDesc = Module->Types[TypeIndex];
		for (std::size_t Index = 0; Index < TypeDesc.Fields.size(); ++Index)
		{
			if (TypeDesc.Fields[Index].Name == FieldName)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		return static_cast<std::uint16_t>(-1);
	}

	std::uint16_t VeroScriptCompiler::AllocateLocal(const std::string& Name)
	{
		const auto TemplateBinding = ActiveTypeSubst.find(Name);
		if (TemplateBinding != ActiveTypeSubst.end() && TemplateBinding->second.rfind("@value:", 0) == 0)
		{
			ReportError("a runtime variable cannot shadow a template value parameter: " + Name);
			return 0;
		}
		if (NextLocalSlot >= std::numeric_limits<std::uint16_t>::max())
		{
			ReportError("local slot overflow");
			return 0;
		}
		const std::uint16_t Slot = NextLocalSlot++;
		if (CurrentFunction)
		{
			if (CurrentFunction->LocalNames.size() <= Slot)
			{
				CurrentFunction->LocalNames.resize(static_cast<std::size_t>(Slot) + 1);
			}
			CurrentFunction->LocalNames[Slot] = Name;
		}
		if (!Scopes.empty())
		{
			if (Scopes.back().Locals.count(Name) > 0 || Scopes.back().EnumConstants.count(Name) > 0 || (!Scopes.back().StaticLocals.empty() && Scopes.back().StaticLocals.count(Name) > 0))
			{
				ReportError("duplicate local: " + Name);
				return 0;
			}
			Scopes.back().Locals.emplace(Name, Slot);
		}
		if (CurrentFunction && Slot >= CurrentFunction->LocalCount)
		{
			CurrentFunction->LocalCount = static_cast<std::uint16_t>(Slot + 1);
		}
		return Slot;
	}

	bool VeroScriptCompiler::ResolveLocal(const std::string& Name, std::uint16_t& OutSlot) const
	{
		for (auto ScopeIt = Scopes.rbegin(); ScopeIt != Scopes.rend(); ++ScopeIt)
		{
			if (!ScopeIt->StaticLocals.empty() && ScopeIt->StaticLocals.count(Name)) return false;
			if (ScopeIt->EnumConstants.count(Name)) return false;
			const auto LocalIt = ScopeIt->Locals.find(Name);
			if (LocalIt != ScopeIt->Locals.end())
			{
				OutSlot = LocalIt->second;
				return true;
			}
		}
		return false;
	}

	std::string VeroScriptCompiler::ResolveLocalStaticLinkageName(const std::string& Name) const
	{
		for (auto Scope = Scopes.rbegin(); Scope != Scopes.rend(); ++Scope)
		{
			if (const auto Found = Scope->StaticLocals.find(Name); Found != Scope->StaticLocals.end()) return Found->second;
			if (Scope->Locals.count(Name) || Scope->EnumConstants.count(Name)) return {};
		}
		return {};
	}

	std::string VeroScriptCompiler::ResolveLocalEnumConstantLinkageName(const std::string& Name) const
	{
		for (auto Scope = Scopes.rbegin(); Scope != Scopes.rend(); ++Scope)
		{
			if (Scope->Locals.count(Name) || Scope->StaticLocals.count(Name)) return {};
			if (const auto Found = Scope->EnumConstants.find(Name); Found != Scope->EnumConstants.end()) return Found->second;
		}
		return {};
	}

	bool VeroScriptCompiler::ResolveThisField(const std::string& Name, std::uint16_t& OutFieldIndex) const
	{
		for (auto Scope = Scopes.rbegin(); Scope != Scopes.rend(); ++Scope)
		{
			if (!Scope->StaticLocals.empty() && Scope->StaticLocals.count(Name)) return false;
			if (Scope->Locals.count(Name))
			{
				break;
			}
		}
		if (!CurrentFunction || !CurrentFunction->bIsMethod)
		{
			return false;
		}
		const std::uint16_t FieldIndex = FindFieldIndex(CurrentFunction->OwnerTypeIndex, Name);
		if (FieldIndex == static_cast<std::uint16_t>(-1))
		{
			return false;
		}
		OutFieldIndex = FieldIndex;
		return true;
	}

	void VeroScriptCompiler::BeginScope()
	{
		Scopes.emplace_back();
		LocalTypeAliasScopes.emplace_back();
	}

	void VeroScriptCompiler::EndScope()
	{
		if (!Scopes.empty())
		{
			if (CurrentFunction && !Scopes.back().StaticLocals.empty())
			{
				for (auto& Initializer : CurrentFunction->LocalStaticInitializers)
				{
					if (Initializer.ScopeEndOffset != 0)
					{
						continue;
					}
					for (const auto& Binding : Scopes.back().StaticLocals)
					{
						if (Binding.second == Module->GlobalNames[Initializer.GlobalSlot])
						{
							Initializer.ScopeEndOffset =
							    static_cast<std::uint32_t>(CurrentFunction->Code.size());
							break;
						}
					}
				}
			}
			Scopes.pop_back();
		}
		if (!LocalTypeAliasScopes.empty())
		{
			LocalTypeAliasScopes.pop_back();
		}
	}

	std::string VeroScriptCompiler::TypeNameLeaf(const std::string& Name)
	{
		const std::size_t Dot = Name.rfind('.');
		return Dot == std::string::npos ? Name : Name.substr(Dot + 1);
	}

	bool VeroScriptCompiler::CompileFormatFamilyCall(const FCallExpr& Call, FEmitter& Emitter)
	{
		if (!Call.TypeArgs.empty())
		{
			ReportErrorId("diag.format_family_does_not_take_type_arguments", Call.Location);
			return false;
		}
		if (Call.GetArguments().empty())
		{
			ReportErrorId("diag.expects_a_format_string", Call.Location, {Call.Callee});
			return false;
		}

		// Match other builtins: `ref` is not a value; allowing it would ToString an FLocalRef
		// into an empty string and silently format wrong output.
		for (const FExpr* Argument : Call.GetArguments())
		{
			if (Argument->Kind == EExprKind::Ref)
			{
				ReportErrorId("diag.does_not_accept_ref_arguments", Argument->Location, {Call.Callee});
				return false;
			}
			std::string ArgType;
			if (TryResolveExprTypeName(*Argument, ArgType) && ArgType == "void")
			{
				ReportError("cannot use void value in expression", Argument->Location);
				return false;
			}
		}

		const bool bPrint = Call.Callee == "print";
		const bool bPrintf = Call.Callee == "printf";
		const bool bPrintfDialect = bPrintf || Call.Callee == "formatf" || Call.Callee == "__format_printf";

		std::string FmtType;
		if (TryResolveExprTypeName(*Call.GetArguments()[0], FmtType) && FmtType != "string")
		{
			ReportErrorId("diag.expects_string_format", Call.Location, {Call.Callee});
			return false;
		}

		bool bPassArrayDirect = false;
		if (Call.GetArguments().size() == 2)
		{
			std::string SecondType;
			if (TryResolveExprTypeName(*Call.GetArguments()[1], SecondType) && SecondType == "array<string>")
			{
				bPassArrayDirect = true;
			}
		}

		auto IsAutoStringArgType = [this](const std::string& TypeName) -> bool
		{
			return TypeName == "string" || TypeName == "bool" || TypeName == "float" || TypeName == "double" || IsSignedIntegerTypeName(TypeName) || Runtime::IsUnsignedTypeName(TypeName) || Numerics::IsTypeName(CanonicalTypeNameForCoercion(TypeName));
		};

		if (!CompileExpression(*Call.GetArguments()[0]))
		{
			return false;
		}

		if (bPassArrayDirect)
		{
			if (!CompileExpression(*Call.GetArguments()[1]))
			{
				return false;
			}
		}
		else
		{
			Emitter.EmitU16Operand(EOpcode::NewDynamicArray, AddString("array<string>"));
			for (std::size_t Index = 1; Index < Call.GetArguments().size(); ++Index)
			{
				std::string ArgType;
				const bool bKnown = TryResolveExprTypeName(*Call.GetArguments()[Index], ArgType);
				if (bKnown)
				{
					// A reference-returning call is consumed as its referenced value in
					// ordinary expression contexts, including formatting varargs.
					ArgType.assign(Runtime::StripReferenceTypeName(ArgType));
				}
				if (bKnown && !IsAutoStringArgType(ArgType))
				{
					ReportErrorId(
					    "diag.varargs_only_accept_scalars_or_string",
					    Call.GetArguments()[Index]->Location,
					    {Call.Callee});
					return false;
				}
				Emitter.Emit(EOpcode::Dup);
				if (!CompileExpression(*Call.GetArguments()[Index]))
				{
					return false;
				}
				if (bKnown && ArgType != "string")
				{
					Emitter.EmitCallBuiltin(EBuiltinId::ToString, 1);
				}
				// push returns `this` for chaining; drop it so only [fmt, arr] remain.
				Emitter.EmitCallMethod(AddString("push"), 1);
				Emitter.Emit(EOpcode::Pop);
			}
		}

		Emitter.EmitCallBuiltin(
		    bPrintfDialect ? EBuiltinId::FormatPrintf : EBuiltinId::Format,
		    2);
		if (bPrint || bPrintf)
		{
			Emitter.EmitCallBuiltin(EBuiltinId::Print, 1);
			FBytecodeConstant Zero;
			Zero.Kind = EConstantKind::I32;
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
		}
		return true;
	}

}
