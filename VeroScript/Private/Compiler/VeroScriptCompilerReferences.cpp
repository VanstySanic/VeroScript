// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptCompiler.h"
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
	bool VeroScriptCompiler::IsForbiddenRuntimeCapture(const std::string& Name) const
	{
		bool Forbidden = false;
		FindLexicalMemberOwner(Name, Forbidden);
		return Forbidden;
	}

	std::string VeroScriptCompiler::FindLexicalMemberOwner(const std::string& Name, bool& OutForbidden) const
	{
		OutForbidden = false;
		std::uint16_t Slot = 0;
		if (!ActiveProgram || CurrentOwnerTypeLinkage.empty() || ResolveLocal(Name, Slot)
			|| !ResolveLocalStaticLinkageName(Name).empty() ||
			Name.find_first_of(".:<\x1F") != std::string::npos)
		{
			return {};
		}
		std::string Owner = CurrentOwnerTypeLinkage;
		for (std::size_t Depth = 0; Depth < 64 && !Owner.empty(); ++Depth)
		{
			FMemberAccessInfo Member;
			const bool Field = LookupMemberAccess(Owner, Name, false, Member);
			const bool Method = !Field && LookupMemberAccess(Owner, Name, true, Member);
			if (Field || Method)
			{
				if (Depth != 0)
				{
					std::string Global, Type;
					bool Const = false;
					const auto* Declaration = Method ? FindMethodDeclForReference(Owner, Name) : nullptr;
					OutForbidden = Field ? !FindStaticFieldOnTypeLinkage(Owner, Name, Global, Type, Const)
										 : !Declaration || !Declaration->bIsStatic;
				}
				return Owner;
			}
			const auto* Source = FindSourceTypeContext(Owner);
			if (!Source)
			{
				return {};
			}
			const auto* Scope = Source->Scope;
			for (const auto& Enum : ActiveProgram->Enums)
			{
				if (!Enum.bScoped && Enum.Scope.ScopeId == Scope->Id &&
					std::any_of(Enum.Members.begin(), Enum.Members.end(),
								[&](const auto& Member)
								{
									return Member.Name == Name;
								}))
				{
					return Owner;
				}
			}
			if (std::find(Scope->ForbiddenRuntimeNames.begin(), Scope->ForbiddenRuntimeNames.end(), Name) !=
				Scope->ForbiddenRuntimeNames.end())
			{
				OutForbidden = true;
				return {};
			}
			if (Scope->AccessTypeName.empty())
			{
				return {};
			}
			const auto Arguments = DecodeGenericConcreteTypes(Owner);
			const auto FindParent = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (Declaration.Name != Scope->AccessTypeName ||
						Declaration.ModulePath != Scope->AccessModulePath ||
						(Source->Context && Source->Context->TypeScope &&
						 Declaration.Scope.Id != Source->Context->TypeScope->Id))
					{
						continue;
					}
					const auto Linkage =
						GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
					if (TemplateParameterNames(Declaration).empty())
					{
						return Linkage;
					}
					std::vector<std::string> Values;
					for (const auto& Parameter : TemplateParameterNames(Declaration))
					{
						const auto I = std::find(EnclosingTemplateParameterNames(*Scope).begin(),
												 EnclosingTemplateParameterNames(*Scope).end(), Parameter);
						const auto Offset = static_cast<std::size_t>(I - EnclosingTemplateParameterNames(*Scope).begin());
						Values.push_back(Offset < Arguments.size() ? Arguments[Offset]
																   : SubstituteTypeName(Parameter));
					}
					return MangleGenericInstanceName(Linkage, Values);
				}
				return std::string{};
			};
			Owner = FindParent(ActiveProgram->Classes);
			if (Owner.empty())
			{
				Owner = FindParent(ActiveProgram->Structs);
			}
			if (Owner.empty())
			{
				Owner = FindParent(ActiveProgram->Interfaces);
			}
		}
		return {};
	}

	std::unique_ptr<FMethodCallExpr> VeroScriptCompiler::MakeLexicalStaticCall(const FCallExpr& Call) const
	{
		if (Call.CalleeExpr)
		{
			return nullptr;
		}
		bool Forbidden = false;
		const auto Owner = FindLexicalMemberOwner(Call.Callee, Forbidden);
		if (Forbidden || Owner.empty())
		{
			return nullptr;
		}
		const auto* Method = FindMethodDeclForReference(Owner, Call.Callee);
		if (!Method || !Method->bIsStatic)
		{
			return nullptr;
		}
		auto Receiver = std::make_unique<FIdentifierExpr>(Owner);
		Receiver->Location = Call.Location;
		auto Result = std::make_unique<FMethodCallExpr>(std::move(Receiver), Call.Callee,
														std::vector<std::unique_ptr<FExpr>>{});
		Result->Location = Call.Location;
		Result->TypeArgs = Call.TypeArgs;
		Result->bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
		Result->BorrowArgumentsFrom(Call.GetArguments());
		return Result;
	}

	const VeroScriptCompiler::FSourceTypeContext* VeroScriptCompiler::FindSourceTypeContext(
		const std::string& Linkage) const
	{
		const auto Base = GenericTemplateBaseLinkage(Linkage);
		const auto Entry = SourceTypeContexts.find((Base.empty() ? Linkage : Base) + '\x1E' +
												   std::to_string(TypeParamArityFromLinkage(Linkage)));
		return Entry == SourceTypeContexts.end() ? nullptr : &Entry->second;
	}

	bool VeroScriptCompiler::TryResolveVariableTypeName(const std::string& Name, std::string& OutTypeName) const
	{
		for (auto ScopeIt = Scopes.rbegin(); ScopeIt != Scopes.rend(); ++ScopeIt)
		{
			if (!ScopeIt->StaticLocals.empty())
			{
				if (const auto Static = ScopeIt->StaticLocals.find(Name); Static != ScopeIt->StaticLocals.end())
				{
					const auto Type = GlobalVarTypes.find(Static->second);
					if (Type == GlobalVarTypes.end()) return false;
					OutTypeName = Type->second;
					return true;
				}
			}
			if (ScopeIt->EnumConstants.count(Name)) return false;
			const auto TypeIt = ScopeIt->LocalTypes.find(Name);
			if (TypeIt != ScopeIt->LocalTypes.end())
			{
				OutTypeName = TypeIt->second;
				return true;
			}
		}
		const std::string GlobalLinkageName = ResolveGlobalLinkageName(Name, CurrentModulePath);
		if (GlobalLinkageName.empty())
		{
			return false;
		}
		const auto GlobalIt = GlobalVarTypes.find(GlobalLinkageName);
		if (GlobalIt == GlobalVarTypes.end())
		{
			return false;
		}
		OutTypeName = GlobalIt->second;
		return true;
	}

	bool VeroScriptCompiler::IsConstBinding(const std::string& Name) const
	{
		for (auto ScopeIt = Scopes.rbegin(); ScopeIt != Scopes.rend(); ++ScopeIt)
		{
			if (const auto Static = ScopeIt->StaticLocals.find(Name); Static != ScopeIt->StaticLocals.end())
			{
				return GlobalVarConsts.count(Static->second) != 0;
			}
			if (ScopeIt->LocalConsts.find(Name) != ScopeIt->LocalConsts.end())
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

	bool VeroScriptCompiler::FindFieldIsConstOnOwnerType(
		const std::string& OwnerTypeName,
		const std::string& FieldName,
		bool& OutIsConst,
		bool* const OutIsMutable) const
	{
		OutIsConst = false;
		if (OutIsMutable)
		{
			*OutIsMutable = false;
		}
		if (!ActiveProgram)
		{
			return false;
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
			const std::string TemplateBase = GenericTemplateBaseLinkage(
				FindGenericTypeMarker(OwnerTypeName) != std::string::npos
				? OwnerTypeName
				: QualifyTypeName(OwnerTypeName, CurrentModulePath));
			if (!TemplateBase.empty())
			{
				OwnerLinkageName = TemplateBase.find('\x1F') != std::string::npos
					? TemplateBase
					: ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
			}
		}
		if (OwnerLinkageName.empty())
		{
			return false;
		}
		// Completed layouts include inherited class and interface fields, as well
		// as package-provided fields whose declarations need not be in the AST.
		if (Module)
		{
			for (const FBytecodeTypeDesc& Type : Module->Types)
			{
				if (Type.Name != OwnerLinkageName && Type.Name != OwnerTypeName)
				{
					continue;
				}
				for (const FBytecodeFieldDesc& Field : Type.Fields)
				{
					if (Field.Name != FieldName)
					{
						continue;
					}
					OutIsConst = Field.bIsConst;
					if (OutIsMutable)
					{
						*OutIsMutable = Field.bIsMutable;
					}
					return true;
				}
			}
		}
		const std::string TemplateBase = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(OwnerLinkageName);
			return Base.empty() ? OwnerLinkageName : Base;
		}();
		const std::string AstOwnerLinkage = TemplateBase;
		const std::size_t WantedArity = TypeParamArityFromLinkage(
			FindGenericTypeMarker(OwnerLinkageName) != std::string::npos ? OwnerLinkageName : OwnerTypeName);
		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(OwnerLinkageName, OwnerSubst, {});
		const auto FindInheritedField = [&](const std::string& BaseName, const std::string& DeclModule)
		{
			const std::string BaseLinkage = ResolveInheritanceBaseLinkage(
				SubstituteTypeNameWith(BaseName, OwnerSubst), DeclModule);
			return !BaseLinkage.empty()
				&& FindFieldIsConstOnOwnerType(BaseLinkage, FieldName, OutIsConst, OutIsMutable);
		};
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, AstOwnerLinkage))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != AstOwnerLinkage
				|| TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : StructDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutIsConst = Field.bIsConst;
					if (OutIsMutable)
					{
						*OutIsMutable = Field.bIsMutable;
					}
					return true;
				}
			}
			if (!StructDecl.BaseTypeName.empty()
				&& FindInheritedField(StructDecl.BaseTypeName, StructDecl.ModulePath))
			{
				return true;
			}
			for (const std::string& InterfaceName : StructDecl.InterfaceTypeNames)
			{
				if (FindInheritedField(InterfaceName, StructDecl.ModulePath)) return true;
			}
			return false;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, AstOwnerLinkage))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != AstOwnerLinkage
				|| TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : ClassDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutIsConst = Field.bIsConst;
					if (OutIsMutable)
					{
						*OutIsMutable = Field.bIsMutable;
					}
					return true;
				}
			}
			for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
			{
				if (FindInheritedField(BaseSimple, ClassDecl.ModulePath))
				{
					return true;
				}
			}
			for (const std::string& InterfaceName : ClassDecl.InterfaceTypeNames)
			{
				if (FindInheritedField(InterfaceName, ClassDecl.ModulePath)) return true;
			}
			return false;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceIndices, AstOwnerLinkage))
		{
			const auto& InterfaceDecl = ActiveProgram->Interfaces[DeclarationIndex];
			if (GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name) != AstOwnerLinkage || TemplateParameterNames(InterfaceDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : InterfaceDecl.Fields)
			{
				if (Field.Name != FieldName)
				{
					continue;
				}
				OutIsConst = Field.bIsConst;
				if (OutIsMutable)
				{
					*OutIsMutable = Field.bIsMutable;
				}
				return true;
			}
			for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
			{
				if (FindInheritedField(ParentName, InterfaceDecl.ModulePath)) return true;
			}
			return false;
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name)
				!= AstOwnerLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : StructDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutIsConst = Field.bIsConst;
					if (OutIsMutable)
					{
						*OutIsMutable = Field.bIsMutable;
					}
					return true;
				}
			}
			return false;
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name)
				!= AstOwnerLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : ClassDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutIsConst = Field.bIsConst;
					if (OutIsMutable)
					{
						*OutIsMutable = Field.bIsMutable;
					}
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::IsConstAssignTarget(const FExpr& Target) const
	{
		switch (Target.Kind)
		{
		case EExprKind::This:
			return CurrentFunction && CurrentFunction->bIsConstMethod;
		case EExprKind::Identifier:
			{
				// `Super` is a const-sensitive alias of `this` (base view).
				if (IsSuperReceiver(Target))
				{
					return CurrentFunction && CurrentFunction->bIsConstMethod;
				}
				const std::string& Name = static_cast<const FIdentifierExpr&>(Target).Name;
				if (!ResolveLocalEnumConstantLinkageName(Name).empty()) return true;
				std::uint16_t Slot = 0;
				if (ResolveLocal(Name, Slot))
				{
					return IsConstBinding(Name);
				}
				std::uint16_t FieldIndex = 0;
				if (ResolveThisField(Name, FieldIndex))
				{
					if (CurrentFunction && CurrentFunction->bIsConstMethod)
					{
						if (!CurrentFunction || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
						{
							return true;
						}
						const std::string& OwnerTypeName =
							Module->Types[CurrentFunction->OwnerTypeIndex].Name;
						bool bFieldConst = false;
						bool bFieldMutable = false;
						return !FindFieldIsConstOnOwnerType(
							OwnerTypeName, Name, bFieldConst, &bFieldMutable)
							|| !bFieldMutable;
					}
					if (!CurrentFunction || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
					{
						return false;
					}
					const std::string& OwnerTypeName = Module->Types[CurrentFunction->OwnerTypeIndex].Name;
					bool bFieldConst = false;
					return FindFieldIsConstOnOwnerType(OwnerTypeName, Name, bFieldConst) && bFieldConst;
				}
				// Bare host-base property is an alias of `this.Prop` → const in const methods.
				if (CurrentFunction && CurrentFunction->bIsConstMethod)
				{
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
						!HostBase.empty())
					{
						std::uint16_t HostIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(
								MakeHostPropertyGetterNativeName(HostBase, Name), HostIndex)
							|| FindNative(
								MakeHostPropertySetterNativeName(HostBase, Name), HostIndex))
						{
							return true;
						}
					}
				}
				const std::string GlobalLinkageName = ResolveGlobalLinkageName(Name, CurrentModulePath);
				return !GlobalLinkageName.empty()
					&& GlobalVarConsts.find(GlobalLinkageName) != GlobalVarConsts.end();
			}
		case EExprKind::MemberAccess:
			{
				const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(Target);
				{
					// C++-style static data member: `Type.Field` is const iff declared const.
					std::string StaticGlobalName;
					std::string StaticTypeName;
					bool bStaticConst = false;
					std::string StaticAccessOwner;
					bool bStaticTypeNameReceiver = false;
					if (TryResolveStaticField(
							*MemberAccess.Object,
							MemberAccess.Member,
							StaticGlobalName,
							StaticTypeName,
							bStaticConst,
							StaticAccessOwner,
							bStaticTypeNameReceiver))
					{
						return bStaticConst;
					}
				}
				{
					// Namespace-qualified global: `Ns.x` is const iff the backing
					// module-scope global is declared const.
					std::string NsGlobalLinkageName;
					if (TryResolveNamespaceQualifiedGlobal(
							*MemberAccess.Object,
							MemberAccess.Member,
							NsGlobalLinkageName))
					{
						return GlobalVarConsts.find(NsGlobalLinkageName)
							!= GlobalVarConsts.end();
					}
				}
				FScope::FMapEntryViewBinding EntryView;
				std::vector<std::string> EntryMembers;
				if (TryGetMapEntryViewMemberPath(Target, EntryView, EntryMembers))
				{
					if (!EntryMembers.empty() && EntryMembers[0] == "first")
					{
						return true;
					}
					if (!EntryMembers.empty() && EntryMembers[0] == "second")
					{
						// Nested fields under second inherit the entry-view binding constness.
						const FIdentifierExpr* Root = nullptr;
						const FExpr* Walk = &Target;
						while (Walk && Walk->Kind == EExprKind::MemberAccess)
						{
							Walk = static_cast<const FMemberAccessExpr&>(*Walk).Object.get();
						}
						if (Walk && Walk->Kind == EExprKind::Identifier)
						{
							Root = static_cast<const FIdentifierExpr*>(Walk);
						}
						if (Root && IsConstBinding(Root->Name))
						{
							return true;
						}
						if (EntryMembers.size() == 1)
						{
							return false;
						}
						// Fall through to field-const check on nested tail.
					}
				}
				std::string OwnerTypeName;
				if (!TryResolveExprTypeName(*MemberAccess.Object, OwnerTypeName))
				{
					return IsConstAssignTarget(*MemberAccess.Object);
				}
				bool bFieldConst = false;
				bool bFieldMutable = false;
				const bool bKnownField = FindFieldIsConstOnOwnerType(
					OwnerTypeName, MemberAccess.Member, bFieldConst, &bFieldMutable);
				if (bKnownField && bFieldMutable)
				{
					return false;
				}
				if (IsConstAssignTarget(*MemberAccess.Object))
				{
					return true;
				}
				return bKnownField && bFieldConst;
			}
		case EExprKind::IndexAccess:
			{
				std::string RawType;
				if (TryResolveExprRawTypeName(Target, RawType) && Runtime::IsReferenceTypeName(RawType))
				{
					return Runtime::IsConstReferenceTypeName(RawType);
				}
				const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(Target);
				return IsConstAssignTarget(*IndexAccess.Object);
			}
		case EExprKind::Call:
			{
				const FCallExpr& Call = static_cast<const FCallExpr&>(Target);
				if (Call.Callee == "const_cast" && !Call.TypeArgs.empty())
				{
					return false;
				}
				std::string RawType;
				if (TryResolveExprRawTypeName(Target, RawType)
					&& Runtime::IsConstReferenceTypeName(RawType))
				{
					return true;
				}
				// An ordinary same-type cast must not be an accidental const escape.
				// `const_cast<T>` is the sole spelling that deliberately removes it.
				return Call.Callee == "cast" && !Call.TypeArgs.empty()
					&& Call.GetArguments().size() == 1
					&& IsConstAssignTarget(*Call.GetArguments().front());
			}
		case EExprKind::Unary:
		case EExprKind::Binary:
		case EExprKind::Assign:
		case EExprKind::MethodCall:
			{
				std::string RawType;
				return TryResolveExprRawTypeName(Target, RawType)
					&& Runtime::IsConstReferenceTypeName(RawType);
			}
		default:
			return false;
		}
	}

	bool VeroScriptCompiler::IsKnownContainerMutator(const std::string& MethodName) const
	{
		return MethodName == "push"
			|| MethodName == "reserve"
			|| MethodName == "clear"
			|| MethodName == "add"
			|| MethodName == "remove"
			|| MethodName == "remove_at"
			|| MethodName == "pop"
			|| MethodName == "push_back"
			|| MethodName == "push_front"
			|| MethodName == "pop_back"
			|| MethodName == "pop_front";
	}

	int VeroScriptCompiler::ExpectedContainerMethodArgCount(const std::string& MethodName) const
	{
		if (MethodName == "push"
			|| MethodName == "reserve"
			|| MethodName == "has"
			|| MethodName == "count"
			|| MethodName == "remove"
			|| MethodName == "remove_at"
			|| MethodName == "add"
			|| MethodName == "push_back"
			|| MethodName == "push_front")
		{
			return 1;
		}
		if (MethodName == "clear"
			|| MethodName == "capacity"
			|| MethodName == "keys"
			|| MethodName == "values"
			|| MethodName == "entries"
			|| MethodName == "pop"
			|| MethodName == "front"
			|| MethodName == "top"
			|| MethodName == "pop_back"
			|| MethodName == "pop_front"
			|| MethodName == "back")
		{
			return 0;
		}
		return -1;
	}

	bool VeroScriptCompiler::IsLenCompatibleTypeName(const std::string& TypeName) const
	{
		if (TypeName == "string" || TypeName == "wstring")
		{
			return true;
		}
		std::string ElementType;
		std::uint32_t Length = 0;
		if (Runtime::ParseNativeArrayType(TypeName, ElementType, Length))
		{
			return true;
		}
		return Runtime::IsDynamicArrayTypeName(TypeName)
			|| Runtime::IsMapTypeName(TypeName)
			|| Runtime::IsOrderedMapTypeName(TypeName)
			|| Runtime::IsSetTypeName(TypeName)
			|| Runtime::IsMultiSetTypeName(TypeName)
			|| Runtime::IsOrderedMultiSetTypeName(TypeName)
			|| Runtime::IsQueueTypeName(TypeName)
			|| Runtime::IsStackTypeName(TypeName)
			|| Runtime::IsDequeTypeName(TypeName)
			|| Runtime::IsScriptArgsArrayTypeName(TypeName);
	}

	bool VeroScriptCompiler::IsNumericTypeName(const std::string& TypeName) const
	{
		return IsSignedIntegerTypeName(TypeName)
			|| Runtime::IsUnsignedTypeName(TypeName)
			|| TypeName == "float"
			|| TypeName == "double";
	}

	bool VeroScriptCompiler::CompileBorrowedReceiver(FEmitter& Emitter, const FExpr& Receiver)
	{
		std::string ReceiverType;
		if ((Receiver.Kind == EExprKind::IndexAccess || Receiver.Kind == EExprKind::MemberAccess)
			&& TryResolveExprTypeName(Receiver, ReceiverType)
			&& (IsStructType(ReceiverType) || Runtime::IsNativeArrayTypeName(ReceiverType))
			&& CanBindImplicitRefArgument(Receiver, false))
		{
			if (!CompileRefArgument(Emitter, Receiver, Receiver.Location, false)) return false;
			Emitter.Emit(EOpcode::DerefRef);
			return true;
		}
		return CompileExpression(Receiver);
	}

	bool VeroScriptCompiler::CompileRefArgument(
		FEmitter& Emitter,
		const FExpr& Argument,
		const FSourceLocation& Location,
		const bool bMutableRef)
	{
		// Direct Make*Ref opcodes do not pass through CompileExpression, so
		// map them to the argument rather than the surrounding declaration/call.
		if (Argument.Location.Line != 0)
		{
			CurrentEmitLocation = Argument.Location;
		}
		std::string ReferenceResultType;
		if (TryResolveExprRawTypeName(Argument, ReferenceResultType)
			&& Runtime::IsReferenceTypeName(ReferenceResultType))
		{
			if (bMutableRef && Runtime::IsConstReferenceTypeName(ReferenceResultType))
			{
				ReportError("cannot bind mutable reference to const reference return", Location);
				return false;
			}
			bPreserveNextReferenceResult = true;
			return CompileExpression(Argument);
		}
		if (bMutableRef && IsConstAssignTarget(Argument))
		{
			ReportError("cannot pass const value to mutable ref parameter", Location);
			return false;
		}
		if (Argument.Kind == EExprKind::This)
		{
			if (!CompileExpression(Argument)) return false;
			const auto Slot = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
			Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
			return true;
		}
		if (Argument.Kind == EExprKind::Identifier)
		{
			const std::string& Name = static_cast<const FIdentifierExpr&>(Argument).Name;
			if (!ResolveLocalEnumConstantLinkageName(Name).empty())
			{
				ReportError("cannot take a reference to an enum constant: " + Name, Location);
				return false;
			}
			FScope::FMapEntryViewBinding EntryView;
			if (TryGetMapEntryViewBinding(Name, EntryView))
			{
				ReportErrorId("diag.cannot_take_ref_of_map_entry_view", Location);
				return false;
			}
			std::uint16_t Slot = 0;
			if (ResolveLocal(Name, Slot))
			{
				Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
				return true;
			}
			std::uint16_t FieldIndex = 0;
			if (ResolveThisField(Name, FieldIndex))
			{
				if (!ValidateThisFieldAccess(Location, Name))
				{
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::MakeThisFieldRef, AddString(Name));
				return true;
			}
			const std::string GlobalLinkageName = ResolveGlobalLinkageName(Name, CurrentModulePath);
			std::uint16_t GlobalSlot = 0;
			if (!GlobalLinkageName.empty() && ResolveGlobal(GlobalLinkageName, GlobalSlot))
			{
				Emitter.EmitU16Operand(EOpcode::MakeGlobalRef, GlobalSlot);
				return true;
			}
			ReportError("undeclared identifier: " + Name, Location);
			return false;
		}
		if (Argument.Kind == EExprKind::IndexAccess)
		{
			return EmitGetIndexRefChain(Emitter, Argument, Location);
		}
		if (Argument.Kind == EExprKind::MemberAccess)
		{
			FScope::FMapEntryViewBinding EntryView;
			std::vector<std::string> EntryMembers;
			if (TryGetMapEntryViewMemberPath(Argument, EntryView, EntryMembers))
			{
				if (EntryMembers.empty() || EntryMembers[0] == "first")
				{
					ReportErrorId("diag.cannot_take_ref_of_map_entry_view_key", Location);
					return false;
				}
				if (EntryMembers[0] != "second")
				{
					ReportErrorId(
						"diag.unknown_field_on_type",
						Location,
						{EntryMembers[0], "pair"});
					return false;
				}
				if (EntryMembers.size() == 1)
				{
					Emitter.EmitU16Operand(EOpcode::MakeLocalRef, EntryView.RefSlot);
					return true;
				}
				std::vector<std::string> NestedFields(
					EntryMembers.begin() + 1,
					EntryMembers.end());
				return EmitMapEntryViewValueFieldRef(Emitter, EntryView, NestedFields);
			}
			const FMemberAccessExpr& Member = static_cast<const FMemberAccessExpr&>(Argument);
			std::string GlobalName;
			std::string StaticType, AccessOwner;
			bool bStaticConst = false, bTypeReceiver = false;
			const bool bStaticField = Member.Object && TryResolveStaticField(*Member.Object, Member.Member,
				GlobalName, StaticType, bStaticConst, AccessOwner, bTypeReceiver);
			if (bStaticField || (Member.Object && TryResolveNamespaceQualifiedGlobal(
				*Member.Object, Member.Member, GlobalName)))
			{
				if (bStaticField)
				{
					if (!ValidateMemberAccess(Location, AccessOwner, Member.Member, false)) return false;
					EnsureStaticFieldGlobalRegistered(AccessOwner, GlobalName, Location);
					if (HasError()) return false;
					if (!bTypeReceiver)
					{
						if (!CompileExpression(*Member.Object)) return false;
						Emitter.Emit(EOpcode::Pop);
					}
				}
				std::uint16_t GlobalSlot = 0;
				if (!ResolveGlobal(GlobalName, GlobalSlot))
				{
					ReportError("undeclared global: " + GlobalName, Location);
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::MakeGlobalRef, GlobalSlot);
				return true;
			}
			std::string ObjectType;
			if (!Member.Object || !TryResolveExprTypeName(*Member.Object, ObjectType))
			{
				ReportError("could not resolve referenced field owner type", Location);
				return false;
			}
			const std::string OwnerType = CanonicalTypeNameForCoercion(ObjectType);
			if (bMutableRef && IsStructType(OwnerType)
				&& !CanBindImplicitRefArgument(*Member.Object, false))
			{
				ReportError("cannot bind mutable reference to a field of a temporary struct", Location);
				return false;
			}
			if (ActiveBindings && ActiveBindings->FindType(OwnerType))
			{
				ReportError("host properties cannot be bound as script reference variables", Location);
				return false;
			}
			if (!ValidateMemberAccess(
					Location,
					ObjectType,
					Member.Member,
					false))
			{
				return false;
			}
			if (!CompileBorrowedReceiver(Emitter, *Member.Object))
			{
				return false;
			}
			if (!CanBindImplicitRefArgument(*Member.Object, false))
			{
				Emitter.Emit(EOpcode::Dup);
				Emitter.EmitU16Operand(EOpcode::BindLocal, AllocateAssignTemp());
			}
			Emitter.EmitU16Operand(EOpcode::MakeFieldRef, AddString(Member.Member));
			return true;
		}
		ReportErrorId("diag.ref_argument_must_be_an_identifier_or_index_expression", Location);
		return false;
	}

	bool VeroScriptCompiler::CanBindImplicitRefArgument(
		const FExpr& Argument,
		const bool bMutableRef) const
	{
		std::string RawType;
		if (TryResolveExprRawTypeName(Argument, RawType)
			&& Runtime::IsReferenceTypeName(RawType))
		{
			return !bMutableRef || !Runtime::IsConstReferenceTypeName(RawType);
		}
		if (bMutableRef && IsConstAssignTarget(Argument))
		{
			return false;
		}
		if (Argument.Kind == EExprKind::This) return true;
		if (Argument.Kind == EExprKind::Identifier)
		{
			const std::string& Name = static_cast<const FIdentifierExpr&>(Argument).Name;
			if (!ResolveLocalEnumConstantLinkageName(Name).empty()) return false;
			FScope::FMapEntryViewBinding EntryView;
			if (TryGetMapEntryViewBinding(Name, EntryView))
			{
				return false;
			}
			std::uint16_t Slot = 0;
			std::uint16_t FieldIndex = 0;
			const std::string GlobalName = ResolveGlobalLinkageName(Name, CurrentModulePath);
			return ResolveLocal(Name, Slot)
				|| ResolveThisField(Name, FieldIndex)
				|| (!GlobalName.empty() && ResolveGlobal(GlobalName, Slot));
		}
		if (Argument.Kind == EExprKind::IndexAccess)
		{
			return CanEmitGetIndexRefChain(Argument);
		}
		if (Argument.Kind != EExprKind::MemberAccess)
		{
			return false;
		}

		FScope::FMapEntryViewBinding EntryView;
		std::vector<std::string> EntryMembers;
		if (TryGetMapEntryViewMemberPath(Argument, EntryView, EntryMembers))
		{
			return !EntryMembers.empty() && EntryMembers[0] == "second";
		}
		const FMemberAccessExpr& Member = static_cast<const FMemberAccessExpr&>(Argument);
		if (!Member.Object) return false;
		std::string GlobalName, StaticType, AccessOwner;
		bool bStaticConst = false, bTypeReceiver = false;
		if (TryResolveStaticField(*Member.Object, Member.Member, GlobalName, StaticType,
								  bStaticConst, AccessOwner, bTypeReceiver) ||
			TryResolveNamespaceQualifiedGlobal(*Member.Object, Member.Member, GlobalName))
		{
			return true;
		}
		std::string ObjectType;
		std::string FieldType;
		return TryResolveExprTypeName(*Member.Object, ObjectType)
			&& FindFieldTypeOnOwnerType(ObjectType, Member.Member, FieldType)
			&& !(ActiveBindings
				&& ActiveBindings->FindType(CanonicalTypeNameForCoercion(ObjectType)))
			&& (!IsStructType(ObjectType) || CanBindImplicitRefArgument(*Member.Object, false));
	}

	bool VeroScriptCompiler::EmitGetIndexRefChain(
		FEmitter& Emitter,
		const FExpr& IndexExpression,
		const FSourceLocation& Location,
		const bool bBorrow)
	{
		if (!CanEmitGetIndexRefChain(IndexExpression))
		{
			std::vector<const FExpr*> Indices;
			const FExpr* Walk = &IndexExpression;
			while (Walk && Walk->Kind == EExprKind::IndexAccess)
			{
				const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(*Walk);
				if (!IndexAccess.Object)
				{
					break;
				}
				Indices.push_back(IndexAccess.Index ? IndexAccess.Index.get() : nullptr);
				Walk = IndexAccess.Object.get();
			}
			if (!Walk
				|| Indices.empty()
				|| (Walk->Kind != EExprKind::Identifier && Walk->Kind != EExprKind::MemberAccess))
			{
				ReportErrorId("diag.ref_index_argument_requires_identifier_array", Location);
				return false;
			}
			if (Walk->Kind == EExprKind::Identifier)
			{
				const std::string& ArrayName = static_cast<const FIdentifierExpr&>(*Walk).Name;
				std::uint16_t ArraySlot = 0;
				std::uint16_t ThisFieldIndex = 0;
				const std::string GlobalLinkageName =
					ResolveGlobalLinkageName(ArrayName, CurrentModulePath);
				std::uint16_t GlobalSlot = 0;
				const bool bResolvable = ResolveLocal(ArrayName, ArraySlot)
					|| ResolveThisField(ArrayName, ThisFieldIndex)
					|| (!GlobalLinkageName.empty() && ResolveGlobal(GlobalLinkageName, GlobalSlot));
				if (!bResolvable)
				{
					ReportError("undeclared identifier: " + ArrayName, Location);
					return false;
				}
			}
			ReportErrorId("diag.ref_index_argument_requires_array_or_deque", Location);
			return false;
		}

		std::vector<const FExpr*> Indices;
		const FExpr* Walk = &IndexExpression;
		while (Walk && Walk->Kind == EExprKind::IndexAccess)
		{
			std::string RootRawType;
			if (Walk != &IndexExpression && TryResolveExprRawTypeName(*Walk, RootRawType) && Runtime::IsReferenceTypeName(RootRawType))
			{
				break;
			}
			const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(*Walk);
			Indices.push_back(IndexAccess.Index.get());
			Walk = IndexAccess.Object.get();
		}
		std::reverse(Indices.begin(), Indices.end());
		if (!Walk)
		{
			ReportErrorId("diag.ref_index_argument_requires_identifier_array", Location);
			return false;
		}

		const auto StripRefPrefix = [](std::string TypeName) -> std::string
		{
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				return TypeName.substr(10);
			}
			if (TypeName.rfind("ref ", 0) == 0)
			{
				return TypeName.substr(4);
			}
			return TypeName;
		};

		std::string ContainerType;
		std::uint16_t CurrentSlot = 0;
		std::string RootRawType;
		if (TryResolveExprRawTypeName(*Walk, RootRawType) && Runtime::IsReferenceTypeName(RootRawType))
		{
			ContainerType.assign(Runtime::StripReferenceTypeName(RootRawType));
			if (!CompileRefArgument(Emitter, *Walk, Location, false)) return false;
			CurrentSlot = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::BindLocal, CurrentSlot);
		}
		else if (Walk->Kind == EExprKind::MemberAccess)
		{
			if (!TryResolveExprTypeName(*Walk, ContainerType))
			{
				ReportErrorId("diag.ref_index_argument_requires_array_or_deque", Location);
				return false;
			}
			// Reuse field/global/map-entry binding so the chain aliases the original storage.
			if (!CompileRefArgument(Emitter, *Walk, Location, false)) return false;
			CurrentSlot = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::BindLocal, CurrentSlot);
		}
		else
		{
			const std::string& ArrayName = static_cast<const FIdentifierExpr&>(*Walk).Name;
			if (ResolveLocal(ArrayName, CurrentSlot))
			{
				if (!TryResolveVariableTypeName(ArrayName, ContainerType))
				{
					ReportErrorId("diag.ref_index_argument_requires_array_or_deque", Location);
					return false;
				}
			}
			else
			{
				// Seed a BindLocal alias so GetIndexRef/GetMapEntryRef MapSlot can
				// ResolveRefStorage through FGlobalRef / FThisFieldRef into the true storage.
				CurrentSlot = AllocateAssignTemp();
				std::uint16_t ThisFieldIndex = 0;
				if (ResolveThisField(ArrayName, ThisFieldIndex))
				{
					if (!ValidateThisFieldAccess(Location, ArrayName))
					{
						return false;
					}
					if (!CurrentFunction || CurrentFunction->OwnerTypeIndex >= Module->Types.size()
						|| !FindFieldTypeOnOwnerType(
							Module->Types[CurrentFunction->OwnerTypeIndex].Name,
							ArrayName,
							ContainerType))
					{
						ReportErrorId("diag.ref_index_argument_requires_array_or_deque", Location);
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::MakeThisFieldRef, AddString(ArrayName));
				}
				else
				{
					const std::string GlobalLinkageName =
						ResolveGlobalLinkageName(ArrayName, CurrentModulePath);
					std::uint16_t GlobalSlot = 0;
					if (GlobalLinkageName.empty() || !ResolveGlobal(GlobalLinkageName, GlobalSlot))
					{
						ReportError("undeclared identifier: " + ArrayName, Location);
						return false;
					}
					if (!TryResolveVariableTypeName(ArrayName, ContainerType))
					{
						ReportErrorId("diag.ref_index_argument_requires_array_or_deque", Location);
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::MakeGlobalRef, GlobalSlot);
				}
				Emitter.EmitU16Operand(EOpcode::BindLocal, CurrentSlot);
			}
		}

		ContainerType = StripRefPrefix(std::move(ContainerType));
		for (std::size_t Level = 0; Level < Indices.size(); ++Level)
		{
			if (!CompileExpression(*Indices[Level]))
			{
				return false;
			}
			const bool bMapLayer = Runtime::IsMapTypeName(ContainerType)
				|| Runtime::IsOrderedMapTypeName(ContainerType);
			if (bMapLayer)
			{
				Emitter.EmitU16Operand(EOpcode::GetMapEntryRef, CurrentSlot);
			}
			else
			{
				Emitter.EmitU16Operand(bBorrow ? EOpcode::GetIndexRef : EOpcode::GetIndexPathRef, CurrentSlot);
			}
			if (Level + 1 < Indices.size())
			{
				CurrentSlot = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::BindLocal, CurrentSlot);
				std::string ElementType;
				if (Runtime::IsDynamicArrayTypeName(ContainerType))
				{
					if (!Runtime::ParseDynamicArrayElementType(ContainerType, ElementType))
					{
						return false;
					}
				}
				else if (Runtime::IsDequeTypeName(ContainerType))
				{
					if (!Runtime::ParseDequeElementType(ContainerType, ElementType))
					{
						return false;
					}
				}
				else if (Runtime::IsNativeArrayTypeName(ContainerType))
				{
					std::uint32_t Length = 0;
					if (!Runtime::ParseNativeArrayType(ContainerType, ElementType, Length))
					{
						return false;
					}
				}
				else if (Runtime::IsScriptArgsArrayTypeName(ContainerType))
				{
					ElementType = "string";
				}
				else if (Runtime::IsMapTypeName(ContainerType))
				{
					std::string KeyType;
					if (!ParseMapTypeForCompile(ContainerType, KeyType, ElementType))
					{
						return false;
					}
				}
				else if (Runtime::IsOrderedMapTypeName(ContainerType))
				{
					std::string KeyType;
					if (!ParseOrderedMapTypeForCompile(ContainerType, KeyType, ElementType))
					{
						return false;
					}
				}
				else
				{
					return false;
				}
				ContainerType = StripRefPrefix(std::move(ElementType));
			}
		}
		return true;
	}

	bool VeroScriptCompiler::CanEmitGetIndexRefChain(
		const FExpr& IndexExpression,
		const bool bAllowMap) const
	{
		// Detect map/ordered_map anywhere in the index chain: assignment must not use
		// MapEntryRef (keys may be missing; SetIndex inserts). Ref args still allow maps.
		if (!bAllowMap)
		{
			const FExpr* Probe = &IndexExpression;
			std::string ProbeType;
			while (Probe && Probe->Kind == EExprKind::IndexAccess)
			{
				const FIndexAccessExpr& IndexAccess =
					static_cast<const FIndexAccessExpr&>(*Probe);
				if (IndexAccess.Object
					&& TryResolveExprTypeName(*IndexAccess.Object, ProbeType)
					&& (Runtime::IsMapTypeName(ProbeType)
						|| Runtime::IsOrderedMapTypeName(ProbeType)))
				{
					return false;
				}
				Probe = IndexAccess.Object.get();
			}
		}
		std::vector<const FExpr*> Indices;
		const FExpr* Walk = &IndexExpression;
		while (Walk && Walk->Kind == EExprKind::IndexAccess)
		{
			std::string RootRawType;
			if (Walk != &IndexExpression && TryResolveExprRawTypeName(*Walk, RootRawType) && Runtime::IsReferenceTypeName(RootRawType))
			{
				break;
			}
			const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(*Walk);
			if (!IndexAccess.Index || !IndexAccess.Object)
			{
				return false;
			}
			Indices.push_back(IndexAccess.Index.get());
			Walk = IndexAccess.Object.get();
		}
		if (!Walk || Indices.empty())
		{
			return false;
		}
		std::reverse(Indices.begin(), Indices.end());

		const auto StripRefPrefix = [](std::string TypeName) -> std::string
		{
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				return TypeName.substr(10);
			}
			if (TypeName.rfind("ref ", 0) == 0)
			{
				return TypeName.substr(4);
			}
			return TypeName;
		};

		std::string ContainerType;
		std::string RootRawType;
		if (TryResolveExprRawTypeName(*Walk, RootRawType) && Runtime::IsReferenceTypeName(RootRawType))
		{
			ContainerType.assign(Runtime::StripReferenceTypeName(RootRawType));
		}
		else if (Walk->Kind == EExprKind::Identifier)
		{
			const std::string& ArrayName = static_cast<const FIdentifierExpr&>(*Walk).Name;
			std::uint16_t ArraySlot = 0;
			std::uint16_t ThisFieldIndex = 0;
			const bool bLocal = ResolveLocal(ArrayName, ArraySlot);
			const bool bThisField = !bLocal && ResolveThisField(ArrayName, ThisFieldIndex);
			std::uint16_t GlobalSlot = 0;
			bool bGlobal = false;
			if (!bLocal && !bThisField)
			{
				const std::string GlobalLinkageName =
					ResolveGlobalLinkageName(ArrayName, CurrentModulePath);
				bGlobal = !GlobalLinkageName.empty() && ResolveGlobal(GlobalLinkageName, GlobalSlot);
			}
			if (!bLocal && !bThisField && !bGlobal)
			{
				return false;
			}
			if (bLocal || bGlobal)
			{
				if (!TryResolveVariableTypeName(ArrayName, ContainerType))
				{
					return false;
				}
			}
			else
			{
				if (!CurrentFunction || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
				{
					return false;
				}
				const std::string& OwnerTypeName = Module->Types[CurrentFunction->OwnerTypeIndex].Name;
				if (!FindFieldTypeOnOwnerType(OwnerTypeName, ArrayName, ContainerType))
				{
					return false;
				}
			}
		}
		else if (Walk->Kind == EExprKind::MemberAccess)
		{
			if (!TryResolveExprTypeName(*Walk, ContainerType))
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		ContainerType = StripRefPrefix(std::move(ContainerType));
		for (std::size_t Level = 0; Level < Indices.size(); ++Level)
		{
			// Assignment never uses MapEntryRef (missing-key insert stays on SetIndex).
			// Ref arguments still allow maps when bAllowMap is true.
			const bool bMapLayer = Runtime::IsMapTypeName(ContainerType)
				|| Runtime::IsOrderedMapTypeName(ContainerType);
			if (bMapLayer)
			{
				if (!bAllowMap)
				{
					return false;
				}
			}
			else if (!(Runtime::IsDynamicArrayTypeName(ContainerType)
					|| Runtime::IsNativeArrayTypeName(ContainerType)
					|| Runtime::IsScriptArgsArrayTypeName(ContainerType)
					|| Runtime::IsDequeTypeName(ContainerType)))
			{
				return false;
			}
			if (Level + 1 >= Indices.size())
			{
				break;
			}
			std::string ElementType;
			if (Runtime::IsDynamicArrayTypeName(ContainerType))
			{
				if (!Runtime::ParseDynamicArrayElementType(ContainerType, ElementType))
				{
					return false;
				}
			}
			else if (Runtime::IsDequeTypeName(ContainerType))
			{
				if (!Runtime::ParseDequeElementType(ContainerType, ElementType))
				{
					return false;
				}
			}
			else if (Runtime::IsNativeArrayTypeName(ContainerType))
			{
				std::uint32_t Length = 0;
				if (!Runtime::ParseNativeArrayType(ContainerType, ElementType, Length))
				{
					return false;
				}
			}
			else if (Runtime::IsScriptArgsArrayTypeName(ContainerType))
			{
				ElementType = "string";
			}
			else if (Runtime::IsMapTypeName(ContainerType))
			{
				std::string KeyType;
				if (!ParseMapTypeForCompile(ContainerType, KeyType, ElementType))
				{
					return false;
				}
			}
			else if (Runtime::IsOrderedMapTypeName(ContainerType))
			{
				std::string KeyType;
				if (!ParseOrderedMapTypeForCompile(ContainerType, KeyType, ElementType))
				{
					return false;
				}
			}
			ContainerType = StripRefPrefix(std::move(ElementType));
		}
		return true;
	}

	bool VeroScriptCompiler::ValidateRefArgumentType(
		const std::string& ParamTypeName,
		const FExpr& Argument,
		const FSourceLocation& Location)
	{
		if (ParamTypeName.empty())
		{
			return true;
		}
		std::string ArgTypeName;
		if (!TryResolveExprTypeName(Argument, ArgTypeName))
		{
			ReportError("could not resolve ref argument type", Location);
			return false;
		}
		const std::string QualifiedParam = CanonicalTypeNameForCoercion(ParamTypeName);
		const std::string QualifiedArg = CanonicalTypeNameForCoercion(ArgTypeName);
		if (QualifiedParam != QualifiedArg)
		{
			ReportErrorId("diag.ref_argument_type_mismatch_expected_got", Location, {DisplayTypeName(ParamTypeName), DisplayTypeName(ArgTypeName)});
			return false;
		}
		return true;
	}

	const FExpr& VeroScriptCompiler::UnwrapRefOperand(const FExpr& Expression) const
	{
		if (Expression.Kind == EExprKind::Ref)
		{
			return *static_cast<const FRefExpr&>(Expression).Operand;
		}
		return Expression;
	}

	const FHostMemberDesc* VeroScriptCompiler::ResolveHostOperator(
		const std::string& OwnerTypeName,
		const std::string_view OperatorName,
		const std::vector<std::string>& ArgTypeNames,
		bool& bOutAmbiguous,
		std::uint16_t* const OutNativeIndex,
		const bool bReceiverIsConst) const
	{
		bOutAmbiguous = false;
		if (OutNativeIndex)
		{
			*OutNativeIndex = static_cast<std::uint16_t>(-1);
		}
		if (!ActiveBindings)
		{
			return nullptr;
		}
		const FHostTypeDesc* const Type = ActiveBindings->FindType(OwnerTypeName);
		if (!Type)
		{
			return nullptr;
		}

		const FHostMemberDesc* Best = nullptr;
		int BestScore = (std::numeric_limits<int>::min)();
		bool BestBoxing = true;
		for (const FHostMemberDesc& Candidate : Type->Members)
		{
			if (!Candidate.bOperator || Candidate.Name != OperatorName ||
				(bReceiverIsConst && !Candidate.bConstMethod) ||
				Candidate.ParamTypes.size() != ArgTypeNames.size())
			{
				continue;
			}
			FNativeFunction Signature;
			Signature.Callback = Candidate.Callback;
			Signature.HostTypeName = Type->Name;
			Signature.ParamTypes.reserve(Candidate.ParamTypes.size() + 1);
			Signature.ParamTypes.push_back(ENativeType::HostObject);
			Signature.ParamTypes.insert(Signature.ParamTypes.end(), Candidate.ParamTypes.begin(),
										Candidate.ParamTypes.end());
			Signature.ParamHostTypeNames.push_back(Type->Name);
			Signature.ParamHostTypeNames.insert(Signature.ParamHostTypeNames.end(),
												Candidate.ParamHostTypeNames.begin(),
												Candidate.ParamHostTypeNames.end());
			Signature.ParamElementTypeNames.emplace_back();
			Signature.ParamElementTypeNames.insert(Signature.ParamElementTypeNames.end(),
												   Candidate.ParamElementTypeNames.begin(),
												   Candidate.ParamElementTypeNames.end());
			Signature.ParamKeyTypeNames.emplace_back();
			Signature.ParamKeyTypeNames.insert(Signature.ParamKeyTypeNames.end(),
											   Candidate.ParamKeyTypeNames.begin(),
											   Candidate.ParamKeyTypeNames.end());
			Signature.ParamValueTypeNames.emplace_back();
			Signature.ParamValueTypeNames.insert(Signature.ParamValueTypeNames.end(),
												 Candidate.ParamValueTypeNames.begin(),
												 Candidate.ParamValueTypeNames.end());
			Signature.ParamFlags.push_back(0);
			Signature.ParamFlags.insert(Signature.ParamFlags.end(), Candidate.ParamFlags.begin(),
										Candidate.ParamFlags.end());

			int Score = 0;
			bool bMatches = true;
			std::vector<std::string> ParameterTypes;
			for (std::size_t Index = 0; Index < ArgTypeNames.size(); ++Index)
			{
				int Contribution = 0;
				std::string ParamTypeName =
					ResolveNativeParamTypeName(Signature, Index + 1);
				const std::uint8_t ParamFlag = NativeParamFlagAt(Signature, Index + 1);
				// Operator syntax has no explicit `ref` token. A const-ref host operand
				// is observationally a value input and may bind normally; mutable refs
				// still require an addressable explicit-ref call and therefore do not match.
				if (NativeParamFlagIsMutableRef(ParamFlag))
				{
					ParamTypeName = "ref " + ParamTypeName;
				}
				if (!Numerics::ValidateArgumentType(Signature, Index + 1,
													CanonicalTypeNameForCoercion(ArgTypeNames[Index])) ||
					!TryScoreArgParamTypes(ArgTypeNames[Index], ParamTypeName, Contribution))
				{
					bMatches = false;
					break;
				}
				ParameterTypes.push_back(ParamTypeName);
				Score += Contribution;
			}
			if (!bMatches)
			{
				continue;
			}
			const bool Boxing = CompilerDetail::NeedsVariantBoxing(ArgTypeNames, ParameterTypes);
			if (!Best || (BestBoxing && !Boxing) || (BestBoxing == Boxing && Score > BestScore))
			{
				Best = &Candidate;
				BestScore = Score;
				BestBoxing = Boxing;
				bOutAmbiguous = false;
			}
			else if (BestBoxing == Boxing && Score == BestScore)
			{
				bOutAmbiguous = true;
			}
		}
		if (!Best || bOutAmbiguous)
		{
			return nullptr;
		}
		if (OutNativeIndex && !FindNative(Best->NativeName, *OutNativeIndex))
		{
			return nullptr;
		}
		return Best;
	}

	std::string VeroScriptCompiler::HostOperatorReturnTypeName(
		const FHostMemberDesc& Operator,
		const std::string& OwnerTypeName) const
	{
		FNativeFunction Signature;
		Signature.ReturnType = Operator.ReturnType;
		Signature.HostTypeName = OwnerTypeName;
		Signature.ReturnHostTypeName = Operator.ReturnHostTypeName.empty()
			? OwnerTypeName
			: Operator.ReturnHostTypeName;
		Signature.ReturnElementTypeName = Operator.ReturnElementTypeName;
		Signature.ReturnKeyTypeName = Operator.ReturnKeyTypeName;
		Signature.ReturnValueTypeName = Operator.ReturnValueTypeName;
		return ResolveNativeReturnTypeName(Signature);
	}

	bool VeroScriptCompiler::EmitHostOperatorCall(FEmitter& Emitter, const FHostMemberDesc& Operator,
												  const std::uint16_t NativeIndex,
												  const std::vector<std::string>& ActualArgTypes,
												  const FSourceLocation& Location,
												  const bool bLastArgumentIsConst)
	{
		FNativeFunction Signature;
		Signature.Callback = Operator.Callback;
		Signature.ParamTypes = Operator.ParamTypes;
		Signature.ParamHostTypeNames = Operator.ParamHostTypeNames;
		Signature.ParamElementTypeNames = Operator.ParamElementTypeNames;
		Signature.ParamKeyTypeNames = Operator.ParamKeyTypeNames;
		Signature.ParamValueTypeNames = Operator.ParamValueTypeNames;
		std::vector<std::string> ParamTypes;
		ParamTypes.reserve(Operator.ParamTypes.size());
		for (std::size_t Index = 0; Index < Operator.ParamTypes.size(); ++Index)
		{
			if (Index < ActualArgTypes.size() &&
				!Numerics::ValidateArgumentType(Signature, Index,
												CanonicalTypeNameForCoercion(ActualArgTypes[Index])))
			{
				ReportErrorId("diag.numeric_type_mismatch", Location);
				return false;
			}
			const std::string ParamType = ResolveNativeParamTypeName(Signature, Index);
			ParamTypes.push_back(ParamType);
			// Operator operands are emitted left-to-right. The final explicit operand
			// is on top and can be coerced without disturbing receiver order.
			if (Index + 1 == Operator.ParamTypes.size() && Index < ActualArgTypes.size() &&
				Numerics::ArgumentRole(Signature, Index) != Numerics::EArgumentRole::Integer &&
				!EmitValueCoercionBetweenTypes(Emitter, ParamType, ActualArgTypes[Index], Location, false,
											   bLastArgumentIsConst))
			{
				return false;
			}
		}
		EmitClonePassByValueArgs(Emitter, ParamTypes, static_cast<std::uint8_t>(ParamTypes.size()));
		Emitter.EmitCallNative(NativeIndex, static_cast<std::uint8_t>(ParamTypes.size() + 1));
		return true;
	}

	const FHostMemberDesc* VeroScriptCompiler::ResolveHostConstructor(
		const std::string& TypeName, const std::vector<std::string>& ArgTypeNames, bool& bOutAmbiguous,
		std::uint16_t* const OutNativeIndex) const
	{
		bOutAmbiguous = false;
		if (OutNativeIndex)
		{
			*OutNativeIndex = static_cast<std::uint16_t>(-1);
		}
		if (!ActiveBindings)
		{
			return nullptr;
		}
		const FHostTypeDesc* const Type = ActiveBindings->FindType(TypeName);
		if (!Type)
		{
			return nullptr;
		}

		const FHostMemberDesc* Best = nullptr;
		int BestScore = (std::numeric_limits<int>::min)();
		bool BestBoxing = true;
		for (const FHostMemberDesc& Candidate : Type->Members)
		{
			if (Candidate.Kind != EHostMemberKind::Constructor ||
				Candidate.ParamTypes.size() != ArgTypeNames.size())
			{
				continue;
			}
			FNativeFunction Signature;
			Signature.Callback = Candidate.Callback;
			Signature.HostTypeName = Type->Name;
			Signature.ParamTypes = Candidate.ParamTypes;
			Signature.ParamHostTypeNames = Candidate.ParamHostTypeNames;
			Signature.ParamElementTypeNames = Candidate.ParamElementTypeNames;
			Signature.ParamKeyTypeNames = Candidate.ParamKeyTypeNames;
			Signature.ParamValueTypeNames = Candidate.ParamValueTypeNames;
			Signature.ParamFlags = Candidate.ParamFlags;

			int Score = 0;
			bool bMatches = true;
			std::vector<std::string> ParameterTypes;
			for (std::size_t Index = 0; Index < ArgTypeNames.size(); ++Index)
			{
				int Contribution = 0;
				std::string ParamTypeName = ResolveNativeParamTypeName(Signature, Index);
				const std::uint8_t ParamFlag = NativeParamFlagAt(Signature, Index);
				if (NativeParamFlagIsRef(ParamFlag))
				{
					ParamTypeName = (ParamFlag & NativeParamFlagConstRef) != 0 ? "const ref " + ParamTypeName
																			   : "ref " + ParamTypeName;
				}
				if (!Numerics::ValidateArgumentType(Signature, Index,
													CanonicalTypeNameForCoercion(ArgTypeNames[Index])) ||
					!TryScoreArgParamTypes(ArgTypeNames[Index], ParamTypeName, Contribution))
				{
					bMatches = false;
					break;
				}
				ParameterTypes.push_back(ParamTypeName);
				Score += Contribution;
			}
			if (!bMatches)
			{
				continue;
			}
			const bool Boxing = CompilerDetail::NeedsVariantBoxing(ArgTypeNames, ParameterTypes);
			if (!Best || (BestBoxing && !Boxing) || (BestBoxing == Boxing && Score > BestScore))
			{
				Best = &Candidate;
				BestScore = Score;
				BestBoxing = Boxing;
				bOutAmbiguous = false;
			}
			else if (BestBoxing == Boxing && Score == BestScore)
			{
				bOutAmbiguous = true;
			}
		}
		if (!Best || bOutAmbiguous)
		{
			return nullptr;
		}
		if (OutNativeIndex
			&& (Best->NativeName.empty()
				|| !FindNative(Best->NativeName, *OutNativeIndex)))
		{
			return nullptr;
		}
		return Best;
	}
}
