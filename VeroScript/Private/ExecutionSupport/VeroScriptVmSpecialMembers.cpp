// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "NativeSupport/VeroScriptHostValueServices.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptVmNativeLeaves.h"

#include <algorithm>

namespace VeroScript
{
	bool VeroScriptVm::CopyConstructFields(const FObjectHandle& Target,
	    const FObjectHandle& Source, const std::uint16_t TypeIndex,
	    const FSourceLocation& Location)
	{
		if (!ActiveModule || TypeIndex >= ActiveModule->Types.size() || !Target || !Source)
		{
			ReportError("invalid copy construction target", Location);
			return false;
		}
		const auto& Type = ActiveModule->Types[TypeIndex];
		if (Target->Fields.size() < Type.Fields.size() || Source->Fields.size() < Type.Fields.size())
		{
			ReportError("copy construction layout mismatch", Location);
			return false;
		}
		for (const auto Base : Type.BaseTypeIndices)
		{
			const auto CompletedBefore = Target->GetCompletedCopyBases().size();
			const auto Ctor = ActiveModule->Types[Base].CopyConstructorIndex;
			if (Ctor != 0xFFFF)
			{
				const auto Depth = Frames.size(), Stack = OperandStack.size();
				FValue Argument = Source;
				if (!CallFunctionAt(Ctor, &Argument, 1, Target, false) || !JitFinishNestedCall(Depth, Stack))
				{
					return false;
				}
				if (bExitRequested) return false;
				(void)Pop();
			}
			else if (!CopyConstructFields(Target, Source, Base, Location))
			{
				return false;
			}
			Target->EditCompletedCopyBases().resize(CompletedBefore);
			Target->EditCompletedCopyBases().push_back(Base);
		}
		if (Type.BaseTypeIndices.empty() && Source->GetHostBase())
		{
			FValue Cloned;
			if (!CopyLanguageValue(FValue{Source->GetHostBase()}, Cloned, Location, true))
			{
				return false;
			}
			const auto* Host = std::get_if<FHostObjectHandle>(&Cloned);
			if (!Host || !*Host)
			{
				ReportError("host base copy failed", Location);
				return false;
			}
			Target->SetHostBase(*Host);
			Target->GetHostBase()->ScriptOwner = Target;
		}
		for (std::size_t I = Type.OwnFieldStartIndex; I < Type.Fields.size(); ++I)
		{
			if (!ChargeExecutionWork(1)) return false;
			// Retain the source owner and reacquire the slot after every user call.
			// A representation snapshot here is a root, never a language copy.
			const FValue SourceValue = FThisFieldRef{Source, static_cast<std::uint16_t>(I)};
			FValue Copy;
			const auto* Empty = std::get_if<std::int32_t>(&Source->Fields.at(I));
			const bool Uninitialized = Empty && *Empty == 0 && std::any_of(Type.Fields[I].DeclMeta.begin(), Type.Fields[I].DeclMeta.end(), [](const auto& Entry)
			{
				return Entry.first == UninitializedPayloadMetaKey;
			});
			if (Uninitialized || Runtime::IsReferenceTypeName(Type.Fields[I].DeclaredTypeName))
			{
				Copy = Source->Fields.at(I);
			}
			else if (!CopyLanguageValue(SourceValue, Copy, Location, true, Type.Fields[I].TypeName))
			{
				return false;
			}
			Target->Fields.at(I) = std::move(Copy);
		}
		return !bHasError && !bHasPendingScriptException;
	}

	bool VeroScriptVm::CopyLanguageValue(const FValue& Value, FValue& Out,
	    const FSourceLocation& Location, const bool bAllowExplicit, const std::string_view DeclaredType)
	{
		// Direct class values retain the handle without cloning subobjects or
		// installing host copy callbacks. Resolve indirect and variant views below.
		if (LanguageCopyDepth < 128 && DeclaredType != "variant" && DeclaredType.find('<') == std::string_view::npos)
		{
			if (const auto* Handle = std::get_if<FObjectHandle>(&Value);
			    Handle && (!*Handle || (*Handle)->Kind == EScriptObjectKind::Class) &&
			    (DeclaredType.empty() || ValueMatchesElementType(Value, std::string(DeclaredType), Location)))
			{
				FValue Copy{*Handle};
				Out = std::move(Copy);
				return true;
			}
		}

		const Runtime::FHostValueServicesScope HostServices(this, this, this, this);
		// Sealed numeric value copies have no script hooks or nested language
		// roots. Preserve distinct wrapper identities without a generic graph copy.
		if (LanguageCopyDepth < 128)
		{
			if (const auto* Host = std::get_if<FHostObjectHandle>(&Value); Host && *Host && Numerics::IsTypeName((*Host)->TypeName) && (DeclaredType.empty() || DeclaredType == (*Host)->TypeName) && HostObjectMatchesType(*Host, (*Host)->TypeName))
			{
				FNumericError Failure;
				bool Handled = false;
				if (!Numerics::TryCopyValue(Value, Out, Failure, Handled))
				{
					ReportError("failed to clone value", Location);
					return false;
				}
				if (Handled) return true;
			}
		}
		// Primitive copies need neither type-table lookup nor a recursive copy
		// context. Keep a snapshot before replacing Out, which may own Value.
		if (LanguageCopyDepth < 128 &&
		    (DeclaredType.empty() || Runtime::IsPrimitiveValueTypeName(DeclaredType)) &&
		    (Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value) ||
		        std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value)))
		{
			FValue Copy = Value;
			Out = std::move(Copy);
			return true;
		}
		// Native container entries describe pair<K,V> using its public spelling;
		// script type descriptors use the ordinary length-encoded generic identity.
		if (ActiveModule && !DeclaredType.empty() && FindTypeIndex(std::string(DeclaredType)) == 0xFFFF)
		{
			std::string First, Second;
			if (Runtime::ParsePairType(DeclaredType, First, Second))
			{
				const auto Mangled = "pair#" + std::to_string(First.size()) + ":" + First + std::to_string(Second.size()) + ":" + Second;
				const auto Pair = FindTypeIndex(Mangled);
				if (Pair != 0xFFFF) return CopyLanguageValue(Value, Out, Location, bAllowExplicit, ActiveModule->Types[Pair].Name);
			}
		}
		if (LanguageCopyDepth >= 128)
		{
			ReportError("value copy nesting exceeds the runtime limit", Location);
			return false;
		}
		struct FDepthScope
		{
			std::uint32_t& Depth;
			explicit FDepthScope(std::uint32_t& D) : Depth(D)
			{
				++Depth;
			}
			~FDepthScope()
			{
				--Depth;
			}
		} Scope(LanguageCopyDepth);
		FValue Root = Value;
		const auto* Storage = ResolveRefStorage(Root);
		if (!Storage) return false;
		const bool Borrowed = Storage != &Root;
		const void* ArrayIdentity = std::holds_alternative<FNativeArray>(*Storage)
		    ? static_cast<const void*>(&std::get<FNativeArray>(*Storage).Storage())
		    : nullptr;
		const FValue Stable = Borrowed ? *Storage : std::move(Root);
		// Script enum constants can retain their source spelling while the copy
		// view is the module-qualified identity. Use the active declaration to
		// validate that relationship, as variant boxing and checked casts do.
		const auto* Enum = std::get_if<FEnumValue>(&Stable);
		bool EnumMatches = false;
		if (Enum && ActiveModule)
		{
			for (const auto& Declaration : ActiveModule->Enums)
			{
				if (Declaration.bScoped && Declaration.Name == DeclaredType && ModuleMeta::TypeNameMatches(Declaration.Name, Enum->TypeName))
				{
					EnumMatches = true;
					break;
				}
			}
		}
		if (!DeclaredType.empty() && !Runtime::IsPrimitiveValueTypeName(DeclaredType) && !EnumMatches && !ValueMatchesElementType(Stable, std::string(DeclaredType), Location))
		{
			std::string Actual;
			if (const auto* Object = std::get_if<FObjectHandle>(&Stable); Object && *Object)
			{
				Actual = (*Object)->GetTypeName();
			}
			else if (const auto* Host = std::get_if<FHostObjectHandle>(&Stable); Host && *Host)
			{
				Actual = (*Host)->TypeName;
			}
			if (!bHasError)
			{
				ReportError("value does not match its copy view: " + std::string(DeclaredType) + " (actual " + Actual + ")", Location);
			}
			return false;
		}
		if (const auto* Host = std::get_if<FHostObjectHandle>(&Stable); Host && *Host && !(*Host)->CloneCallback && ActiveNatives && FindActiveNativeIndexByName(MakeHostCloneNativeName((*Host)->TypeName)) != 0xFFFF)
		{
			FValue Copy = Stable;
			if (!JitCloneStruct(Copy)) return false;
			Out = std::move(Copy);
			return true;
		}
		if (const auto* Handle = std::get_if<FObjectHandle>(&Stable);
		    Handle && *Handle && (*Handle)->Kind == EScriptObjectKind::Struct)
		{
			const auto Source = *Handle;
			const auto ActualIndex = ResolveObjectTypeIndex(*Source);
			const auto ViewIndex = DeclaredType.empty() ? ActualIndex : FindTypeIndex(std::string(DeclaredType));
			const auto TypeIndex = ViewIndex == 0xFFFF ? ActualIndex : ViewIndex;
			if (!ActiveModule || Source->bDestroyed || TypeIndex >= ActiveModule->Types.size() || ActualIndex >= ActiveModule->Types.size() || Source->Fields.size() != ActiveModule->Types[ActualIndex].Fields.size())
			{
				ReportError("copy source does not match the active program layout", Location);
				return false;
			}
			if (Source->RuntimeTypeModule && Source->RuntimeTypeModule != ActiveModule && (!Source->ProgramOwner || Source->ProgramOwner != ProgramOwner))
			{
				ReportError(Source->GetDescriptorData().ReflectionOwnerType.empty() ? "cannot copy a value from a different program layout"
				                                                                    : "foreign reflection descriptor belongs to another program",
				    Location);
				return false;
			}
			if (!IsActiveObjectTypeAssignableTo(Source->GetTypeName(), ActiveModule->Types[TypeIndex].Name))
			{
				ReportError("incompatible copy construction view", Location);
				return false;
			}
			const auto Ctor = ActiveModule->Types[TypeIndex].CopyConstructorIndex;
			if (LanguageCopyPlanModule != ActiveModule || LanguageCopyHookTypes.size() != ActiveModule->Types.size())
			{
				// Governance checks can interrupt or reenter through the host. Build
				// privately so cleanup never sees a partial plan and nested copies
				// cannot publish a table that the outer build then overwrites.
				LanguageCopyPlanModule = nullptr;
				auto PreparedHookTypes = std::move(LanguageCopyHookTypes);
				PreparedHookTypes.assign(ActiveModule->Types.size(), false);
				for (std::size_t I = 0; I < ActiveModule->Types.size(); ++I)
				{
					if (!ChargeExecutionWork(1)) return false;
					PreparedHookTypes[I] = ActiveModule->Types[I].CopyConstructorIndex != 0xFFFF;
				}
				bool Changed;
				do
				{
					Changed = false;
					for (std::size_t I = 0; I < ActiveModule->Types.size(); ++I)
					{
						if (!ChargeExecutionWork(1)) return false;
						if (PreparedHookTypes[I])
						{
							continue;
						}
						for (const auto Base : ActiveModule->Types[I].BaseTypeIndices)
						{
							if (PreparedHookTypes[Base])
							{
								PreparedHookTypes[I] = true;
								Changed = true;
								break;
							}
						}
						if (PreparedHookTypes[I])
						{
							continue;
						}
						for (const auto& Field : ActiveModule->Types[I].Fields)
						{
							std::string Type = Field.TypeName, Element;
							std::uint32_t Length = 0;
							while (Runtime::ParseNativeArrayType(Type, Element, Length))
							{
								Type = Element;
							}
							std::uint16_t Child = 0xFFFF;
							std::uint16_t Interface = 0xFFFF;
							if (ModuleMeta::FindInterfaceIndex(*ActiveModule, Type, Interface))
							{
								PreparedHookTypes[I] = true;
								Changed = true;
								break;
							}
							if (ModuleMeta::FindTypeIndex(*ActiveModule, Type, Child) && PreparedHookTypes[Child])
							{
								PreparedHookTypes[I] = true;
								Changed = true;
								break;
							}
						}
					}
				}
				while (Changed);
				LanguageCopyHookTypes = std::move(PreparedHookTypes);
				LanguageCopyPlanModule = ActiveModule;
			}
			if (!LanguageCopyHookTypes[TypeIndex] && TypeIndex == ActualIndex)
			{
				if (Runtime::TryCloneValue(Stable, Out)) return true;
				ReportError("failed to clone value", Location);
				return false;
			}
			if (Ctor != 0xFFFF)
			{
				const auto& Function = ActiveModule->Functions.at(Ctor);
				if (Function.bDeleted || !Function.UnavailableReason.empty() || (!bAllowExplicit && (Function.bExplicitCopy || (Frames.empty() && Function.Access != EMemberAccess::Public))))
				{
					ReportError("copy constructor is deleted, explicit, or unavailable: " + Source->GetTypeName(), Location);
					return false;
				}
			}
			// Handwritten constructors follow ordinary initialization. Defaulted and
			// implicit copies allocate only storage, without default initializer effects.
			FObjectHandle Target;
			if (Ctor == 0xFFFF || ActiveModule->Functions[Ctor].bDefaulted)
			{
				// Descriptor identity and other core metadata are representation state.
				// Preserve them while allocating empty language subobjects.
				Target = std::make_shared<FScriptObject>(*Source);
				Target->SetTypeName(ActiveModule->Types[TypeIndex].Name);
				Target->RuntimeTypeGeneration = ProgramGeneration;
				Target->ProgramOwner = ProgramOwner;
				Target->RuntimeTypeModule = ActiveModule;
				Target->RuntimeTypeIndex = TypeIndex;
				Target->MutationOrigin = {};
				Target->ClearCompletedCopyBases();
				Target->Fields.assign(ActiveModule->Types[TypeIndex].Fields.size(), FValue{std::int32_t{0}});
				Target->SetHostBase({});
				Target->bConstructionIncomplete = true;
				Target->bDestroying = Target->bDestroyed = false;
				Target->MutationVersion = 0;
			}
			else
			{
				Target = CreateObject(TypeIndex, Location, true);
			}
			if (!Target) return false;
			bool Ok;
			if (Ctor != 0xFFFF)
			{
				const auto Depth = Frames.size(), Stack = OperandStack.size();
				FValue Argument = Source;
				Ok = CallFunctionAt(Ctor, &Argument, 1, Target, false) && JitFinishNestedCall(Depth, Stack);
				// Exit removes every runtime frame and has no ordinary return sentinel.
				if (bExitRequested) return false;
				if (Ok)
				{
					(void)Pop();
				}
			}
			else
			{
				Ok = CopyConstructFields(Target, Source, TypeIndex, Location);
			}
			if (!Ok)
			{
				Target->bConstructionIncomplete = true;
				(void)DestroyObject(Target, Location);
				return false;
			}
			Target->bConstructionIncomplete = false;
			Target->ClearCompletedCopyBases();
			Out = std::move(Target);
			return true;
		}
		if (const auto* Array = std::get_if<FNativeArray>(&Stable))
		{
			std::string ExpectedElement = Array->ElementTypeName();
			std::uint32_t DeclaredLength = 0;
			const bool TypedArray = !DeclaredType.empty() && Runtime::ParseNativeArrayType(DeclaredType, ExpectedElement, DeclaredLength);
			if (TypedArray && DeclaredLength != Array->Length())
			{
				ReportError("incompatible array copy view", Location);
				return false;
			}
			FNativeArray Copy;
			Copy.TypeName() = TypedArray ? std::string(DeclaredType) : Array->TypeName();
			Copy.ElementTypeName() = ExpectedElement;
			Copy.Length() = Array->Length();
			Copy.Storage().Elements.reserve(Array->Storage().Elements.size());
			for (std::size_t I = 0; I < Array->Storage().Elements.size(); ++I)
			{
				const FNativeArray* CurrentArray = Array;
				if (Borrowed)
				{
					const auto* Current = ResolveRefStorage(Root);
					CurrentArray = Current ? std::get_if<FNativeArray>(Current) : nullptr;
					if (!CurrentArray || &CurrentArray->Storage() != ArrayIdentity || CurrentArray->Storage().Elements.size() != Array->Storage().Elements.size())
					{
						ReportError("array copy source changed during a callback", Location);
						(void)ReleaseValueList(Copy.Storage().Elements, Location);
						return false;
					}
				}
				const FValue Element = CurrentArray->Storage().Elements[I];
				FValue Cloned;
				if (!ChargeExecutionWork(1) || !CopyLanguageValue(Element, Cloned, Location, bAllowExplicit, Copy.ElementTypeName()))
				{
					(void)ReleaseValueList(Copy.Storage().Elements, Location);
					return false;
				}
				Copy.Storage().Elements.push_back(std::move(Cloned));
			}
			Out = std::move(Copy);
			return true;
		}
		if (!Runtime::TryCloneValue(Stable, Out))
		{
			ReportError("failed to clone value", Location);
			return false;
		}
		return true;
	}
}
