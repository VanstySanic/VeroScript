// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAnalysisControl.h"
#include <algorithm>
#include <functional>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::IsCopyConstructor(const FConstructorDecl& C,
	    const std::string& Owner, const std::string& DeclModule) const
	{
		if (C.Parameters.size() != 1 || !C.Parameters[0].bIsRef || !C.Parameters[0].bIsConst || C.Parameters[0].bIsVariadic)
		{
			return false;
		}
		std::unordered_map<std::string, std::string> Subst;
		TryBuildGenericOwnerSubst(Owner, Subst, C.Location);
		const auto Types = CanonicalParamTypeNames(C.Parameters, DeclModule, Subst);
		return Types.size() == 1 && Runtime::StripReferenceTypeName(Types[0]) == Owner;
	}

	bool VeroScriptCompiler::CompileDefaultedMethodBody(const std::vector<FFunctionParam>& Params,
	    const ESpecialMemberKind Kind, const FSourceLocation& Location)
	{
		const auto Index = CurrentFunction->OwnerTypeIndex;
		std::vector<std::string> Types;
		std::string Return;
		Runtime::ParseFunctionType(CurrentFunction->ClosureTypeName, Types, Return);
		const bool Assignment = Kind == ESpecialMemberKind::CopyAssignment;
		if (Index >= Module->Types.size() || CurrentFunction->bIsStaticMethod || Types.size() != 1 || !Runtime::IsConstReferenceTypeName(Types[0]) || Runtime::StripReferenceTypeName(Types[0]) != Module->Types[Index].Name || Params[0].DefaultValue || Params[0].bIsVariadic || (Assignment && (CurrentFunction->bIsConstMethod || Module->Types[Index].Kind != EScriptObjectKind::Struct || Return != "ref " + Module->Types[Index].Name)) || (!Assignment && (Kind == ESpecialMemberKind::None || !CurrentFunction->bIsConstMethod || Return != "bool")))
		{
			CurrentFunction->SpecialMemberKind = ESpecialMemberKind::None;
			ReportError("invalid defaulted special member signature", Location);
			return false;
		}
		auto Emitter = MakeEmitter();
		const auto L = "$defaultThis" + std::to_string(NextAssignTempId++);
		const auto Slot = AllocateLocal(L);
		Scopes.back().LocalTypes[L] = Module->Types[Index].Name;
		if (!Assignment)
		{
			Scopes.back().LocalConsts.insert(L);
		}
		Emitter.Emit(EOpcode::LoadThis);
		Emitter.EmitU16Operand(EOpcode::BindLocal, Slot);
		const auto R = "$defaultSource" + std::to_string(NextAssignTempId++);
		const auto RSlot = AllocateLocal(R);
		Scopes.back().LocalTypes[R] = Module->Types[Index].Name;
		Scopes.back().LocalConsts.insert(R);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, 0);
		Emitter.EmitU16Operand(EOpcode::BindLocal, RSlot);
		if (Assignment && CurrentFunction->Name.find("::@copy_assign#") != std::string::npos)
		{
			FIdentifierExpr Receiver(L), Source(R);
			Receiver.Location = Source.Location = Location;
			FResolvedScriptOperator Resolved;
			bool Ambiguous = false;
			if (ResolveScriptOperator("operator=", Receiver, {&Source}, Resolved, Ambiguous))
			{
				if (Resolved.Method)
				{
					CurrentFunction->Access = Resolved.Method->Access;
				}
				if (!CompileScriptOperator(Emitter, "operator=", Receiver, {&Source}, Resolved, Location))
				{
					return false;
				}
				Emitter.Emit(EOpcode::Pop);
				Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
				Emitter.Emit(EOpcode::Return);
				return true;
			}
			if (Ambiguous || HasError())
			{
				if (!HasError())
				{
					ReportError("ambiguous copy assignment", Location);
				}
				return false;
			}
		}
		if (Kind == ESpecialMemberKind::Inequality)
		{
			FBinaryExpr Equal(EBinaryOp::Equal, std::make_unique<FIdentifierExpr>(L), std::make_unique<FIdentifierExpr>(R));
			Equal.Location = Location;
			FResolvedScriptOperator Resolved;
			bool Ambiguous = false;
			if (!ResolveScriptOperator("operator==", *Equal.Left, {Equal.Right.get()}, Resolved, Ambiguous))
			{
				if (Ambiguous || HasError())
				{
					if (!HasError())
					{
						ReportError("ambiguous equality for defaulted operator!=", Location);
					}
					return false;
				}
				// Use the ordinary built-in equality directly. Feeding this through
				// binary mirror lookup would rediscover operator!= and recurse.
				if (!CompileExpression(*Equal.Left) || !CompileExpression(*Equal.Right)) return false;
				Emitter.Emit(EOpcode::Eq);
				Emitter.Emit(EOpcode::Not);
				Emitter.Emit(EOpcode::Return);
				return true;
			}
			if (!CompileScriptOperator(Emitter, "operator==", *Equal.Left, {Equal.Right.get()}, Resolved, Location))
			{
				return false;
			}
			if (Resolved.ReturnType != "bool")
			{
				ReportError("equality must return bool", Location);
				return false;
			}
			Emitter.Emit(EOpcode::Not);
			Emitter.Emit(EOpcode::Return);
			return true;
		}
		std::vector<std::size_t> FailJumps;
		if (Assignment)
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, 0);
			Emitter.Emit(EOpcode::SameObject);
			const auto Different = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
			Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
			Emitter.Emit(EOpcode::Return);
			Emitter.PatchI32(Different, Emitter.CurrentOffset() - static_cast<std::int32_t>(Different + 4));
		}
		if (!CompileMemberwiseFields(Emitter, Index, L, R, Kind, FailJumps, Location)) return false;
		if (Assignment)
		{
			Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
		}
		else
		{
			Emitter.EmitU8Operand(EOpcode::ConstBool, 1);
		}
		Emitter.Emit(EOpcode::Return);
		if (!Assignment)
		{
			for (auto Jump : FailJumps)
			{
				Emitter.PatchI32(Jump, Emitter.CurrentOffset() - static_cast<std::int32_t>(Jump + 4));
			}
			Emitter.EmitU8Operand(EOpcode::ConstBool, 0);
			Emitter.Emit(EOpcode::Return);
		}
		return true;
	}

	bool VeroScriptCompiler::CompileMemberwiseAssignment(FEmitter& Emitter,
	    const FExpr& Target, const FExpr& Source, const std::string& Type,
	    const FSourceLocation& Location)
	{
		std::string Linkage;
		std::uint16_t Index = 0xFFFF;
		if (!EnsureStructTypeResolved(Type, Linkage, &Index, Location) || Index >= Module->Types.size() || Module->Types[Index].Kind != EScriptObjectKind::Struct)
		{
			return false;
		}
		if (IsConstAssignTarget(Target))
		{
			ReportError("cannot assign to const value", Location);
			return false;
		}
		if (!CompileRefArgument(Emitter, Target, Location, true)) return false;
		const auto L = "$assignL" + std::to_string(NextAssignTempId++);
		const auto R = "$assignR" + std::to_string(NextAssignTempId++);
		const auto LSlot = AllocateLocal(L), RSlot = AllocateLocal(R);
		const auto TargetRefSlot = AllocateAssignTemp();
		Scopes.back().LocalTypes[L] = Linkage;
		Scopes.back().LocalTypes[R] = Linkage;
		Scopes.back().LocalConsts.insert(R);
		Emitter.EmitU16Operand(EOpcode::BindLocal, TargetRefSlot);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, TargetRefSlot);
		Emitter.EmitU16Operand(EOpcode::BindLocal, LSlot);
		const bool SourceRef = CanBindImplicitRefArgument(Source, false);
		if (!(SourceRef ? CompileRefArgument(Emitter, Source, Location, false) : CompileExpression(Source))) return false;
		if (SourceRef)
		{
			Emitter.Emit(EOpcode::DerefRef);
		}
		Emitter.EmitU16Operand(EOpcode::BindLocal, RSlot);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, RSlot);
		Emitter.Emit(EOpcode::SameObject);
		const auto Different = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
		Emitter.EmitU16Operand(EOpcode::TakeLocal, TargetRefSlot);
		const auto Done = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
		Emitter.PatchI32(Different, Emitter.CurrentOffset() - static_cast<std::int32_t>(Different + 4));
		std::vector<std::size_t> Unused;
		const auto PreviousOwner = CurrentOwnerTypeLinkage;
		const auto PreviousAccess = CurrentAccessFunctionLinkage;
		CurrentOwnerTypeLinkage = Linkage;
		CurrentAccessFunctionLinkage = Linkage + "::@default";
		const bool Ok = CompileMemberwiseFields(Emitter, Index, L, R, ESpecialMemberKind::CopyAssignment, Unused, Location);
		CurrentOwnerTypeLinkage = PreviousOwner;
		CurrentAccessFunctionLinkage = PreviousAccess;
		if (Ok)
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TargetRefSlot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
			Emitter.Emit(EOpcode::SameObject);
			Emitter.EmitCallBuiltin(EBuiltinId::Assert, 1);
			Emitter.EmitU16Operand(EOpcode::TakeLocal, TargetRefSlot);
			Emitter.PatchI32(Done, Emitter.CurrentOffset() - static_cast<std::int32_t>(Done + 4));
			for (const auto Temp : {LSlot, RSlot})
			{
				Emitter.EmitU16Operand(EOpcode::TakeLocal, Temp);
				Emitter.Emit(EOpcode::Pop);
			}
		}
		return Ok;
	}

	bool VeroScriptCompiler::CompileMemberwiseFields(FEmitter& Emitter,
	    const std::uint16_t TypeIndex, const std::string& Left, const std::string& Right,
	    const ESpecialMemberKind Kind, std::vector<std::size_t>& FailJumps,
	    const FSourceLocation& Location)
	{
		// Type instantiation while resolving an operator can grow the type table.
		// Keep a value snapshot of the plan, never a reference into that table.
		const auto Type = Module->Types.at(TypeIndex);
		CheckAnalysisCancellation();
		for (const auto BaseIndex : Type.BaseTypeIndices)
		{
			const auto Base = Module->Types.at(BaseIndex);
			const auto L = "$baseL" + std::to_string(NextAssignTempId++);
			const auto R = "$baseR" + std::to_string(NextAssignTempId++);
			const auto BindView = [&](const std::string& Name, const std::string& From, bool Const)
			{
				const auto Slot = AllocateLocal(Name);
				Scopes.back().LocalTypes[Name] = Base.Name;
				if (Const)
				{
					Scopes.back().LocalConsts.insert(Name);
				}
				std::uint16_t FromSlot = 0;
				ResolveLocal(From, FromSlot);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, FromSlot);
				Emitter.EmitU16Operand(EOpcode::BindLocal, Slot);
			};
			BindView(L, Left, Kind == ESpecialMemberKind::Equality);
			BindView(R, Right, true);
			FIdentifierExpr Receiver(L), Source(R);
			Receiver.Location = Source.Location = Location;
			const char* Op = Kind == ESpecialMemberKind::CopyAssignment ? "operator=" : "operator==";
			FResolvedScriptOperator Resolved;
			bool Ambiguous = false;
			if (ResolveScriptOperator(Op, Receiver, {&Source}, Resolved, Ambiguous))
			{
				if (Resolved.Method)
				{
					if (Resolved.Method->bDeleted || !ValidateMemberAccess(Location, Base.Name, Op, true, 1, Resolved.Method))
					{
						if (!HasError())
						{
							ReportError("base special member is deleted", Location);
						}
						return false;
					}
					if (!CompileExpression(Receiver) || !CompileOperatorArgument(Emitter, Source, Resolved.ParamTypes[0], Location)) return false;
					const auto Owner = Resolved.OwnerLinkage.empty() ? Base.Name : Resolved.OwnerLinkage;
					const auto Index = FindFunctionIndex(MakeMethodFunctionName(Owner, Op, Resolved.ParamTypes, Resolved.Method->bIsConst));
					if (Index == 0xFFFF)
					{
						ReportError("missing base special member body", Location);
						return false;
					}
					// Qualified subobject operation: virtual dispatch must not reenter
					// the derived defaulted implementation.
					Emitter.EmitCallBoundMethod(Index, 1);
				}
				else if (!CompileScriptOperator(Emitter, Op, Receiver, {&Source}, Resolved, Location))
				{
					return false;
				}
				if (Kind == ESpecialMemberKind::Equality)
				{
					if (Resolved.ReturnType != "bool")
					{
						ReportError("base equality must return bool", Location);
						return false;
					}
					FailJumps.push_back(Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0));
				}
				else
				{
					Emitter.Emit(EOpcode::Pop);
				}
			}
			else
			{
				if (Ambiguous || HasError())
				{
					if (!HasError())
					{
						ReportError("ambiguous base special member", Location);
					}
					return false;
				}
				if (Kind == ESpecialMemberKind::Equality && Base.Kind == EScriptObjectKind::Class)
				{
					ReportError("defaulted equality requires value equality for its class base", Location);
					return false;
				}
				const auto SavedOwner = CurrentOwnerTypeLinkage;
				const auto SavedFunction = CurrentAccessFunctionLinkage;
				CurrentOwnerTypeLinkage = Base.Name;
				CurrentAccessFunctionLinkage = Base.Name + "::@default";
				const bool Ok = CompileMemberwiseFields(Emitter, BaseIndex, L, R, Kind, FailJumps, Location);
				CurrentOwnerTypeLinkage = SavedOwner;
				CurrentAccessFunctionLinkage = SavedFunction;
				if (!Ok) return false;
			}
		}
		if (!Type.HostBaseTypeName.empty() && Type.BaseTypeIndices.empty())
		{
			const auto* Host = ActiveBindings ? ActiveBindings->FindType(Type.HostBaseTypeName) : nullptr;
			const bool Assignment = Kind == ESpecialMemberKind::CopyAssignment;
			if (!Host || (Assignment ? !Host->CloneCallback : !Host->EqualsCallback))
			{
				ReportError("host base lacks a value copy/equality protocol: " + Type.HostBaseTypeName, Location);
				return false;
			}
			std::uint16_t LSlot = 0, RSlot = 0;
			ResolveLocal(Left, LSlot);
			ResolveLocal(Right, RSlot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, RSlot);
			Emitter.Emit(Assignment ? EOpcode::AssignHostBase : EOpcode::EqualHostBase);
			if (Assignment)
			{
				Emitter.Emit(EOpcode::Pop);
			}
			else
			{
				FailJumps.push_back(Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0));
			}
		}
		const bool DeferredPresence = Kind == ESpecialMemberKind::CopyAssignment && std::any_of(Type.Fields.begin(), Type.Fields.end(), [](const auto& Field)
		{
			return std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(),
			    [](const auto& Entry)
			{
				return Entry.first == UninitializedPayloadMetaKey;
			});
		});
		for (std::size_t I = Type.OwnFieldStartIndex; I < Type.Fields.size(); ++I)
		{
			const auto& Field = Type.Fields[I];
			if (DeferredPresence && Field.Name == "present")
			{
				continue;
			}
			if (Kind == ESpecialMemberKind::CopyAssignment && std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(), [](const auto& Entry)
			{
				return Entry.first == UninitializedPayloadMetaKey;
			}))
			{
				// Optional storage can contain an inactive sentinel. Copy its actual
				// representation, while retaining the payload's language copy checks.
				CopyRequirements.push_back({Field.TypeName, CurrentFunction ? CurrentFunction->Name : CurrentOwnerTypeLinkage + "::@initializer", Location, false, CurrentAccessFunctionLinkage});
				std::uint16_t LSlot = 0, RSlot = 0;
				ResolveLocal(Left, LSlot);
				ResolveLocal(Right, RSlot);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, RSlot);
				Emitter.EmitU16Operand(EOpcode::LoadField, AddString(Field.Name));
				Emitter.Emit(EOpcode::CloneStruct);
				Emitter.EmitU16Operand(EOpcode::StoreField, AddString(Field.Name));
				Emitter.Emit(EOpcode::Pop);
				continue;
			}
			if (Kind == ESpecialMemberKind::CopyAssignment && (Field.bIsConst || Runtime::IsReferenceTypeName(Field.DeclaredTypeName)))
			{
				ReportError("copy assignment is unavailable for const/reference field: " + Field.Name, Location);
				return false;
			}
			std::vector<std::string> Indices;
			const auto Member = [&](const std::string& Name) -> std::unique_ptr<FExpr>
			{
				std::unique_ptr<FExpr> E = std::make_unique<FMemberAccessExpr>(std::make_unique<FIdentifierExpr>(Name), Field.Name);
				E->Location = Location;
				for (const auto& Index : Indices)
				{
					E = std::make_unique<FIndexAccessExpr>(std::move(E), std::make_unique<FIdentifierExpr>(Index));
					E->Location = Location;
				}
				return E;
			};
			std::function<bool(const std::string&)> EmitElement;
			EmitElement = [&](const std::string& ElementType)
			{
				std::string Element;
				std::uint32_t Length = 0;
				if (Runtime::ParseNativeArrayType(ElementType, Element, Length))
				{
					const auto Counter = "$memberIndex" + std::to_string(NextAssignTempId++);
					const auto Slot = AllocateLocal(Counter);
					Scopes.back().LocalTypes[Counter] = "int";
					FBytecodeConstant Zero, Limit;
					Zero.Kind = Limit.Kind = EConstantKind::I32;
					Limit.I32 = static_cast<std::int32_t>(Length);
					Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
					Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
					const auto Loop = Emitter.CurrentOffset();
					Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
					Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Limit));
					Emitter.Emit(EOpcode::Lt);
					const auto End = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
					Indices.push_back(Counter);
					const auto Ok = EmitElement(Element);
					Indices.pop_back();
					if (!Ok) return false;
					Emitter.EmitU16Operand(EOpcode::IncLocal, Slot);
					Emitter.EmitI32Operand(EOpcode::Jmp, Loop - Emitter.CurrentOffset() - 5);
					Emitter.PatchI32(End, Emitter.CurrentOffset() - static_cast<std::int32_t>(End + 4));
					return true;
				}
				if (Kind == ESpecialMemberKind::CopyAssignment)
				{
					FAssignExpr Assign(Member(Left), Member(Right));
					Assign.Location = Location;
					if (!CompileExpression(Assign)) return false;
					Emitter.Emit(EOpcode::Pop);
				}
				else
				{
					FBinaryExpr Equal(EBinaryOp::Equal, Member(Left), Member(Right));
					Equal.Location = Location;
					if (!CompileExpression(Equal)) return false;
					FailJumps.push_back(Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0));
				}
				return true;
			};
			if (!EmitElement(Field.TypeName)) return false;
		}
		if (DeferredPresence)
		{
			// Publish optional state only after its payload copy has succeeded.
			std::uint16_t LSlot = 0, RSlot = 0;
			ResolveLocal(Left, LSlot);
			ResolveLocal(Right, RSlot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, RSlot);
			Emitter.EmitU16Operand(EOpcode::LoadField, AddString("present"));
			Emitter.EmitU16Operand(EOpcode::StoreField, AddString("present"));
			Emitter.Emit(EOpcode::Pop);
		}
		if (Kind == ESpecialMemberKind::CopyAssignment)
		{
			const auto Base = GenericTemplateBaseLinkage(Type.Name);
			const auto Lookup = Base.empty() ? Type.Name : Base;
			static const std::unordered_set<std::string> Descriptors = {"type", "field_info", "method_info",
			    "module_info", "function_info", "global_info", "enum_member_info", "source_node_info",
			    "annotation_info", "annotation_argument_info", "comment_info", "annotation_definition_info"};
			for (const auto& D : BuiltinStructTemplates)
			{
				if (Descriptors.count(D.Name) && GetSymbolLinkageName(D.ModulePath, D.bExported, D.Name) == Lookup)
				{
					std::uint16_t LSlot = 0, RSlot = 0;
					ResolveLocal(Left, LSlot);
					ResolveLocal(Right, RSlot);
					Emitter.EmitU16Operand(EOpcode::LoadLocal, LSlot);
					Emitter.EmitU16Operand(EOpcode::LoadLocal, RSlot);
					Emitter.Emit(EOpcode::AssignCoreMetadata);
					Emitter.Emit(EOpcode::Pop);
					break;
				}
			}
		}
		return true;
	}

	bool VeroScriptCompiler::FinalizeSpecialMembers()
	{
		// Session setters and executable reflection use the same checked memberwise
		// code as source assignments. Internal entry points stay out of AST lookup.
		for (std::size_t I = 0; I < Module->Types.size(); ++I)
		{
			CheckAnalysisCancellation();
			const auto Type = Module->Types[I];
			if (Type.Kind != EScriptObjectKind::Struct || Type.bIsStatic)
			{
				continue;
			}
			const auto Name = MakeMethodFunctionName(Type.Name, "@copy_assign", {"const ref " + Type.Name});
			if (Module->FunctionIndices.count(Name))
			{
				continue;
			}
			FSourceLocation Location;
			for (const auto& F : Module->Functions)
			{
				if (F.bIsMethod && F.OwnerTypeIndex == I)
				{
					Location = F.Location;
					break;
				}
			}
			if (!EnsureFunctionStub(Name, "ref " + Type.Name, 1, true, static_cast<std::uint16_t>(I), false, Location)) return false;
			auto& Function = Module->Functions[FindFunctionIndex(Name)];
			// Implicit assignment is public. A user-declared operator, if selected
			// below, supplies its own access instead of the stub's private default.
			Function.Access = EMemberAccess::Public;
			Function.bDefaulted = true;
			Function.SpecialMemberKind = ESpecialMemberKind::CopyAssignment;
			FFunctionParam Parameter;
			Parameter.Name = "other";
			Parameter.TypeName = Type.Name;
			Parameter.bIsRef = Parameter.bIsConst = true;
			std::vector<FFunctionParam> Parameters;
			Parameters.push_back(std::move(Parameter));
			FBlockStmt Body;
			if (!CompileFunction(Name, "ref " + Type.Name, Parameters, Body, true,
			        static_cast<std::uint16_t>(I), ResolveOwnerModulePath(Type.Name), Location))
			{
				return false;
			}
		}
		for (std::size_t I = 0; I < Module->Functions.size(); ++I)
		{
			CheckAnalysisCancellation();
			auto& F = Module->Functions[I];
			if (!F.bIsMethod || F.OwnerTypeIndex >= Module->Types.size())
			{
				continue;
			}
			auto& Type = Module->Types[F.OwnerTypeIndex];
			std::vector<std::string> Params;
			std::string Return;
			if (!Runtime::ParseFunctionType(F.ClosureTypeName, Params, Return))
			{
				continue;
			}
			if (Type.Kind == EScriptObjectKind::Struct && Params == std::vector<std::string>{"const ref " + Type.Name} && F.Name == MakeConstructorFunctionName(Type.Name, TypeNameLeaf(GetTypeSimpleName(Type.Name)), Params))
			{
				F.SpecialMemberKind = ESpecialMemberKind::CopyConstructor;
				Type.CopyConstructorIndex = static_cast<std::uint16_t>(I);
				const auto Apply = [&](const auto& Declarations)
				{
					for (const auto& D : Declarations)
					{
						const auto Base = GenericTemplateBaseLinkage(Type.Name);
						if (GetSymbolLinkageName(D.ModulePath, D.bExported, D.Name) != (Base.empty() ? Type.Name : Base))
						{
							continue;
						}
						for (const auto& C : D.Constructors)
						{
							if (IsCopyConstructor(C, Type.Name, D.ModulePath))
							{
								F.Access = C.Access;
								F.bExplicitCopy = C.bExplicit;
								return;
							}
						}
					}
				};
				Apply(ActiveProgram->Structs);
				Apply(BuiltinStructTemplates);
			}
			if (!F.bIsStaticMethod && Params == std::vector<std::string>{"const ref " + Type.Name})
			{
				if (Type.Kind == EScriptObjectKind::Struct && !F.bIsConstMethod && Return == "ref " + Type.Name && F.Name == MakeMethodFunctionName(Type.Name, "operator=", Params))
				{
					F.SpecialMemberKind = ESpecialMemberKind::CopyAssignment;
				}
				if (F.bIsConstMethod && Return == "bool")
				{
					if (F.Name == MakeMethodFunctionName(Type.Name, "operator==", Params, true))
					{
						F.SpecialMemberKind = ESpecialMemberKind::Equality;
					}
					if (F.Name == MakeMethodFunctionName(Type.Name, "operator!=", Params, true))
					{
						F.SpecialMemberKind = ESpecialMemberKind::Inequality;
					}
				}
			}
		}
		// A later-compiled default member can depend on an unavailable operation.
		// Resolve this finite dependency graph before accepting any executable use.
		const auto Propagate = [&]()
		{
			bool Changed;
			do
			{
				Changed = false;
				for (auto& F : Module->Functions)
				{
					CheckAnalysisCancellation();
					if (!F.bDefaulted || !F.UnavailableReason.empty())
					{
						continue;
					}
					for (std::size_t Ip = 0; Ip < F.Code.size();)
					{
						const auto Op = static_cast<EOpcode>(F.Code[Ip]);
						if (Op == EOpcode::CallBoundMethod || Op == EOpcode::Call)
						{
							const auto Called = Bytecode::ReadU16LE(F.Code.data() + Ip + 1);
							if (Called < Module->Functions.size() && !Module->Functions[Called].UnavailableReason.empty())
							{
								F.UnavailableReason = Module->Functions[Called].UnavailableReason;
								Changed = true;
								break;
							}
						}
						Ip += Bytecode::InstructionSize(Op);
					}
				}
			}
			while (Changed);
		};
		const auto CopyFailure = [&](auto&& Self, std::string TypeName,
		                             bool Direct, std::unordered_set<std::string>& Visiting,
		                             const std::string& Context, const std::string& FunctionName) -> std::string
		{
			CheckAnalysisCancellation();
			TypeName = CanonicalTypeNameForCoercion(TypeName);
			std::string Element;
			std::uint32_t Length = 0;
			if (Runtime::ParseNativeArrayType(TypeName, Element, Length))
			{
				return Self(Self, Element, Direct, Visiting, Context, FunctionName);
			}
			std::uint16_t TypeIndex = 0xFFFF;
			if (!ModuleMeta::FindTypeIndex(*Module, TypeName, TypeIndex)) return {};
			const auto& Type = Module->Types[TypeIndex];
			if (Type.Kind != EScriptObjectKind::Struct || !Visiting.insert(TypeName).second) return {};
			const auto Ctor = Type.CopyConstructorIndex;
			if (Ctor != 0xFFFF)
			{
				const auto& F = Module->Functions[Ctor];
				if (F.bDeleted || (!Direct && F.bExplicitCopy))
				{
					return "copy constructor is deleted or explicit: " + Type.Name;
				}
				if (F.Access != EMemberAccess::Public)
				{
					FMemberAccessInfo Info;
					Info.bFound = true;
					Info.EffectiveAccess = F.Access;
					const auto Base = GenericTemplateBaseLinkage(Type.Name);
					Info.DeclaringOwnerLinkage = Base.empty() ? Type.Name : Base;
					Info.DeclaringArity = TypeParamArityFromLinkage(Type.Name);
					const auto Previous = CurrentAccessFunctionLinkage;
					CurrentAccessFunctionLinkage = FunctionName;
					const bool Friend = IsCurrentFunctionFriendOf(Info, Type.Name);
					CurrentAccessFunctionLinkage = Previous;
					if (!Friend && !CanAccessMemberFromContext(Info, Context, *ActiveProgram, Type.Name, false, ActiveImportIndex))
					{
						return "copy constructor is inaccessible: " + Type.Name + " from " + FunctionName;
					}
				}
				if (!F.UnavailableReason.empty()) return F.UnavailableReason;
				if (!F.bDefaulted)
				{
					Visiting.erase(TypeName);
					return {};
				}
			}
			if (Type.BaseTypeIndices.empty() && !Type.HostBaseTypeName.empty() && ActiveBindings)
			{
				const auto* Host = ActiveBindings->FindType(Type.HostBaseTypeName);
				std::uint16_t Native = 0;
				if (Host && Host->Kind == EHostTypeKind::Struct && !Host->CloneCallback && !FindNative(MakeHostCloneNativeName(Type.HostBaseTypeName), Native))
				{
					return "host base lacks a value copy protocol: " + Type.HostBaseTypeName;
				}
			}
			for (const auto Base : Type.BaseTypeIndices)
			{
				const auto Failure = Self(Self, Module->Types[Base].Name, true, Visiting, Type.Name, Type.Name + "::@copy");
				if (!Failure.empty()) return Failure;
			}
			for (std::size_t I = Type.OwnFieldStartIndex; I < Type.Fields.size(); ++I)
			{
				const auto& Field = Type.Fields[I];
				if (Runtime::IsReferenceTypeName(Field.DeclaredTypeName))
				{
					continue;
				}
				const auto Failure = Self(Self, Field.TypeName, true, Visiting, Type.Name, Type.Name + "::@copy");
				if (!Failure.empty()) return Type.Name + "." + Field.Name + ": " + Failure;
			}
			Visiting.erase(TypeName);
			return {};
		};
		for (auto& F : Module->Functions)
		{
			if (F.bDefaulted && F.SpecialMemberKind == ESpecialMemberKind::CopyConstructor)
			{
				std::unordered_set<std::string> Visiting;
				F.UnavailableReason = CopyFailure(CopyFailure, Module->Types[F.OwnerTypeIndex].Name, true, Visiting,
				    Module->Types[F.OwnerTypeIndex].Name, F.Name);
			}
		}
		for (const auto& Requirement : CopyRequirements)
		{
			std::unordered_set<std::string> Visiting;
			const auto& Access = Requirement.AccessFunction.empty() ? Requirement.FunctionName : Requirement.AccessFunction;
			const auto Separator = FindMethodLinkageSeparator(Access);
			const auto Context = Separator == std::string::npos ? std::string{} : Access.substr(0, Separator);
			const auto Failure = CopyFailure(CopyFailure, Requirement.TypeName, Requirement.bAllowExplicit, Visiting, Context, Access);
			if (Failure.empty())
			{
				continue;
			}
			const auto FunctionIt = Module->FunctionIndices.find(Requirement.FunctionName);
			if (FunctionIt != Module->FunctionIndices.end() && Module->Functions[FunctionIt->second].bDefaulted)
			{
				Module->Functions[FunctionIt->second].UnavailableReason = Failure;
			}
			else
			{
				ReportError(Failure, Requirement.Location);
				return false;
			}
		}
		Propagate();
		for (const auto& F : Module->Functions)
		{
			if (F.bDefaulted && !F.UnavailableReason.empty())
			{
				if (F.bIsVirtual)
				{
					ReportError("unavailable defaulted virtual method: " + F.UnavailableReason, F.Location);
					return false;
				}
				continue;
			}
			for (std::size_t Ip = 0; Ip < F.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(F.Code[Ip]);
				if (Op == EOpcode::CallBoundMethod || Op == EOpcode::Call || Op == EOpcode::MakeClosure)
				{
					const auto Called = Bytecode::ReadU16LE(F.Code.data() + Ip + 1);
					if (Called < Module->Functions.size() && !Module->Functions[Called].UnavailableReason.empty())
					{
						ReportError("use of unavailable defaulted operation: " + Module->Functions[Called].UnavailableReason, F.Location);
						return false;
					}
				}
				Ip += Bytecode::InstructionSize(Op);
			}
		}
		for (std::size_t Ip = 0; Ip < Module->InitCode.size();)
		{
			const auto Op = static_cast<EOpcode>(Module->InitCode[Ip]);
			if (Op == EOpcode::CallBoundMethod || Op == EOpcode::Call || Op == EOpcode::MakeClosure)
			{
				const auto Called = Bytecode::ReadU16LE(Module->InitCode.data() + Ip + 1);
				if (Called < Module->Functions.size() && !Module->Functions[Called].UnavailableReason.empty())
				{
					ReportError("use of unavailable defaulted operation in initializer: " + Module->Functions[Called].UnavailableReason);
					return false;
				}
			}
			Ip += Bytecode::InstructionSize(Op);
		}
		return true;
	}

	void VeroScriptCompiler::EmitValueCopy(FEmitter& Emitter, const std::string& TypeName, const bool bAllowExplicit)
	{
		if (TypeName.empty())
		{
			Emitter.Emit(EOpcode::CloneStruct);
			return;
		}
		auto Type = CanonicalTypeNameForCoercion(QualifyTypeName(NormalizeTypeAlias(SubstituteTypeName(TypeName)), CurrentModulePath));
		if (IsStructType(Type) && !(ActiveBindings && ActiveBindings->FindType(Type)))
		{
			std::string Linkage;
			if (!EnsureStructTypeResolved(Type, Linkage, nullptr, CurrentEmitLocation)) return;
			Type = std::move(Linkage);
		}
		CopyRequirements.push_back({Type, CurrentFunction ? CurrentFunction->Name : CurrentOwnerTypeLinkage.empty() ? ""
		                                                                                                            : CurrentOwnerTypeLinkage + "::@initializer",
		    CurrentEmitLocation, bAllowExplicit, CurrentAccessFunctionLinkage});
		Emitter.EmitU16U16Operands(EOpcode::CloneValueOfType, AddString(Type), bAllowExplicit ? 1 : 0);
	}

	bool VeroScriptCompiler::CanTransferCoercedValue(const FExpr& Expression, const std::string& TargetType) const
	{
		if (ExpressionYieldsUnaliasedByValue(Expression, TargetType)) return true;
		std::string SourceType;
		if (!IsStructType(TargetType) || !TryResolveExprTypeName(Expression, SourceType)) return false;
		const auto Source = CanonicalTypeNameForCoercion(SourceType);
		const auto Target = CanonicalTypeNameForCoercion(TargetType);
		// After a successful user conversion, the stack already owns its result.
		// A base subobject conversion still needs its own value construction.
		return Source != Target && !IsSubtypeOf(Source, Target);
	}
}
