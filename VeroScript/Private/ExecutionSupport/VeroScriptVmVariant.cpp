// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptValueRoot.h"
#include "VeroScriptValueGraphInternal.h"
#include "VeroScriptHotReloadInternal.h"
#include <algorithm>

namespace VeroScript
{
	bool VeroScriptVm::DiscardExternalValues(const FBytecodeModule& Module, std::vector<FValue>& Values,
	    const std::vector<FValue>& Keep, const std::vector<FNativeFunction>* Natives, FHotReloadState* Persistent)
	{
		if (Persistent && Persistent->bExecutionResetRequired)
		{
			MarkExecutionResetRequired();
		}
		BeginExecutionGovernance();
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bHasError = false;
		Error = {};
		if (Persistent)
		{
			Globals = std::move(Persistent->Globals);
			LocalStatics = std::move(Persistent->LocalStatics);
		}
		struct FRestoreGlobals
		{
			FHotReloadState* Persistent;
			std::vector<FValue>& Values;
			std::shared_ptr<FLocalStaticState>& Statics;
			~FRestoreGlobals()
			{
				if (Persistent)
				{
					Persistent->Globals = std::move(Values);
					Persistent->LocalStatics = std::move(Statics);
				}
			}
		} RestoreGlobals{Persistent, Globals, LocalStatics};
		std::unordered_set<const void*> Identities;
		std::unordered_set<const void*> Seen;
		// A graph whose shared handles all have one owner cannot be reachable from
		// any Keep root. Still use the ordinary three-phase destructor/detach pass.
		// Shared subgraphs and cycles conservatively retain the full protection scan.
		bool NeedsKeep = false;
		for (const auto& Value : Values)
		{
			if (!VisitValueGraph(Value, [](const FValue& V, const void*)
			{
				return std::visit([](const auto& Handle)
				{
					using T = std::decay_t<decltype(Handle)>;
					if constexpr (std::is_same_v<T, FObjectHandle> || std::is_same_v<T, FFunctionHandle> ||
					    std::is_same_v<T, FScriptVariantHandle> || std::is_same_v<T, FDynamicArrayHandle> ||
					    std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> ||
					    std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle>)
					{
						return !Handle || Handle.use_count() == 1;
					}
					return true;
				}, V);
			}))
			{
				NeedsKeep = true;
				break;
			}
		}
		const auto KeepValue = [&](const FValue& V)
		{
			return ValueGraphInternal::Visit(V,
			    [&](const FValue&, const void* Id)
			{
				if (Id)
				{
					Identities.insert(Id);
				}
				return true;
			}, Seen);
		};
		bool Kept = true;
		if (NeedsKeep)
		{
			for (const auto& V : Keep)
			{
				if (!KeepValue(V))
				{
					Kept = false;
					break;
				}
			}
			for (const auto& V : Globals)
			{
				if (!KeepValue(V))
				{
					Kept = false;
					break;
				}
			}
		}
		if (!Kept)
		{
			ReportError("external value graph exceeds the traversal limit");
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return false;
		}
		const bool Released = DiscardValueRoots(Values, &Identities);
		const bool Exceptions = ReleasePendingScriptException();
		const bool Ok = !bHasError && ((Released && Exceptions) || bExitRequested);
		if (Persistent && RequiresReset())
		{
			Persistent->bExecutionResetRequired = true;
		}
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return Ok;
	}

	bool VeroScriptVm::MakeVariant(const FBytecodeModule& Module, std::string_view TypeName,
	    const FValue& Payload, FValue& Output, const std::vector<FNativeFunction>* Natives)
	{
		BeginExecutionGovernance();
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bHasError = false;
		Error = {};
		std::string Canonical = CanonicalizeNativeElementTypeName(TypeName);
		if (MemoryBindings)
		{
			std::string Resolved, Failure;
			if (ResolveHostRuntimeTypeName(*MemoryBindings, Canonical, Resolved, Failure))
			{
				Canonical = std::move(Resolved);
			}
		}
		std::uint16_t Index = 0;
		if (ModuleMeta::FindTypeIndex(Module, Canonical, Index))
		{
			Canonical = Module.Types[Index].Name;
		}
		else if (ModuleMeta::FindInterfaceIndex(Module, Canonical, Index))
		{
			Canonical = Module.Interfaces[Index].Name;
		}
		else
		{
			const FBytecodeEnumDesc* Enum = nullptr;
			for (const auto& E : Module.Enums)
			{
				if (E.bScoped && ModuleMeta::TypeNameMatches(E.Name, Canonical))
				{
					if (E.Name == Canonical)
					{
						Enum = &E;
						break;
					}
					if (Enum)
					{
						Enum = nullptr;
						break;
					}
					Enum = &E;
				}
			}
			if (Enum)
			{
				Canonical = Enum->Name;
			}
		}
		bool Ok = false;
		const auto* ScriptNull = std::get_if<FObjectHandle>(&Payload);
		const auto* HostNull = std::get_if<FHostObjectHandle>(&Payload);
		const auto* HostType = MemoryBindings ? MemoryBindings->FindType(Canonical) : nullptr;
		if (Canonical == "variant")
		{
			Ok = CheckExecutionGovernance() && ValidateVariantValue(Payload);
			if (Ok)
			{
				Output = Payload;
			} // flat box copy, including the empty state
		}
		else if ((ScriptNull && !*ScriptNull) || (HostNull && !*HostNull))
		{
			const FValue TypedNull = HostType && HostType->Kind == EHostTypeKind::Class ? FValue(FHostObjectHandle{}) : FValue(FObjectHandle{});
			Ok = BoxVariantValue(TypedNull, Canonical, Output);
		}
		else
		{
			Ok = BoxVariantValue(Payload, Canonical, Output);
		}
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return Ok;
	}

	bool VeroScriptVm::BoxVariantValue(const FValue& Value, const std::string& TypeName, FValue& Output)
	{
		if (!CheckExecutionGovernance()) return false;
		const auto* Visible = UnwrapClosureReferenceCell(&Value);
		const auto Known = [&](auto& Self, const std::string& Name, std::size_t Depth) -> bool
		{
			if (Depth > 128 || Name.empty() || Name == "void" || Name == "nullptr" || Runtime::IsReferenceTypeName(Name)) return false;
			if (Name == "variant" || Runtime::IsPrimitiveValueTypeName(Name)) return true;
			if (Runtime::IsScriptArgsArrayTypeName(Name)) return true;
			if (ActiveModule && (FindTypeIndex(Name) < ActiveModule->Types.size() || std::any_of(ActiveModule->Interfaces.begin(), ActiveModule->Interfaces.end(), [&](const auto& T)
			{
				return T.Name == Name;
			}) || std::any_of(ActiveModule->Enums.begin(), ActiveModule->Enums.end(), [&](const auto& T)
			{
				return T.Name == Name;
			})))
			{
				return true;
			}
			if ((MemoryBindings && MemoryBindings->FindType(Name)) || IsActiveHostTypeName(Name)) return true;
			std::string E, K, V;
			std::uint32_t Length = 0;
			if (Runtime::ParseDynamicArrayElementType(Name, E) || Runtime::ParseQueueElementType(Name, E) || Runtime::ParseStackElementType(Name, E) || Runtime::ParseDequeElementType(Name, E) || Runtime::SplitSetElementType(Name, E) || Runtime::ParseMultiSetElementType(Name, E) || Runtime::ParseChannelElementType(Name, E) || Runtime::ParseNativeArrayType(Name, E, Length))
			{
				return Self(Self, E, Depth + 1);
			}
			if (Runtime::SplitMapType(Name, K, V) || Runtime::SplitOrderedMapType(Name, K, V))
			{
				return Self(Self, K, Depth + 1) && Self(Self, V, Depth + 1) && K != "variant";
			}
			std::vector<std::string> P;
			if (Runtime::ParseFunctionType(Name, P, V))
			{
				if (V != "void" && !Self(Self, std::string(Runtime::StripReferenceTypeName(V)), Depth + 1)) return false;
				for (const auto& T : P)
				{
					if (!Self(Self, std::string(Runtime::StripReferenceTypeName(T)), Depth + 1)) return false;
				}
				return true;
			}
			return false;
		};
		const auto* DeclaredHost = MemoryBindings ? MemoryBindings->FindType(TypeName) : nullptr;
		const auto* Script = Visible ? std::get_if<FObjectHandle>(Visible) : nullptr;
		const bool HostBase = DeclaredHost && DeclaredHost->Kind == EHostTypeKind::Class && Script && *Script && (*Script)->GetHostBase() && HostObjectMatchesType((*Script)->GetHostBase(), TypeName);
		const auto* Enum = Visible ? std::get_if<FEnumValue>(Visible) : nullptr;
		const bool EnumMatch = Enum && ActiveModule && std::any_of(ActiveModule->Enums.begin(), ActiveModule->Enums.end(), [&](const auto& E)
		{
			return E.bScoped && E.Name == TypeName && ModuleMeta::TypeNameMatches(E.Name, Enum->TypeName);
		});
		if (!Visible || TypeName.empty() || TypeName == "void" || TypeName == "nullptr" || TypeName == "variant" || Runtime::IsReferenceTypeName(TypeName) || !Known(Known, TypeName, 0) || (!HostBase && !EnumMatch && !ValueMatchesElementType(*Visible, TypeName, {})))
		{
			ReportError("variant source does not match its declared concrete type");
			return false;
		}
		if (const auto* Host = std::get_if<FHostObjectHandle>(Visible); Host && *Host)
		{
			const auto* Type = MemoryBindings ? MemoryBindings->FindType(TypeName) : nullptr;
			if (Type && Type->Kind == EHostTypeKind::Struct && !(*Host)->CloneCallback)
			{
				ReportError("variant host value requires a stamped clone protocol");
				return false;
			}
		}
		if (const auto* Host = std::get_if<FHostObjectHandle>(Visible); Host && !*Host)
		{
			const auto* Type = MemoryBindings ? MemoryBindings->FindType(TypeName) : nullptr;
			if (!Type || Type->Kind != EHostTypeKind::Class)
			{
				ReportError("variant typed null requires a registered host class");
				return false;
			}
		}
		std::size_t Visited = 0;
		if (!VisitValueGraph(*Visible, [&](const FValue& V, const void*)
		{
			if ((++Visited & 1023u) == 0 && !CheckExecutionGovernance()) return false;
			if (const auto* H = std::get_if<FObjectHandle>(&V); H && *H && (*H)->ProgramOwner != ProgramOwner) return false;
			if (const auto* H = std::get_if<FFunctionHandle>(&V); H && *H && ((*H)->ProgramOwner != ProgramOwner || (*H)->ProgramGeneration != ProgramGeneration))
			{
				return false;
			}
			if (Variant::IsVariant(V) && !ValidateVariantValue(V)) return false;
			return true;
		}))
		{
			if (!bHasError)
			{
				ReportError("variant payload contains a foreign value or exceeds the traversal limit");
			}
			return false;
		}
		FVariantTypeDescriptor Type;
		Type.Name = TypeName;
		if (!Runtime::IsPrimitiveValueTypeName(TypeName))
		{
			auto Source = std::make_shared<FVariantSourceStamp>();
			Source->ProgramOwner = ProgramOwner;
			Source->Generation = ProgramGeneration;
			Type.Source = std::move(Source);
		}
		FValue Payload;
		if (!CopyLanguageValue(*Visible, Payload, {}, false, TypeName)) return false;
		auto Storage = std::make_shared<FScriptVariantStorage>();
		Storage->CanonicalTypeName = Type.Name;
		Storage->Source = Type.Source;
		Storage->Value = std::move(Payload);
		Storage->bCopyRequiresVm = Variant::CopyRequiresVm(*ActiveModule, Storage->Value);
		FValue Prepared = std::move(Storage);
		if (!CheckExecutionGovernance())
		{
			(void)ReleaseValue(Prepared, {});
			return false;
		}
		Output = std::move(Prepared);
		return true;
	}

	bool VeroScriptVm::ValidateVariantValue(const FValue& Value)
	{
		std::string Failure;
		if (!Variant::Validate(Value, Failure))
		{
			ReportError(Failure);
			return false;
		}
		const auto& Box = std::get<FScriptVariantHandle>(Value);
		if (Box && Box->Source && (Box->Source->ProgramOwner != ProgramOwner || Box->Source->Generation != ProgramGeneration))
		{
			ReportError("variant belongs to a foreign or stale type environment");
			return false;
		}
		return true;
	}

	bool VeroScriptVm::ExtractVariantValue(const FValue& Source, const std::string& Target, FValue& Output)
	{
		if (!CheckExecutionGovernance() || !ValidateVariantValue(Source)) return false;
		const auto& Box = std::get<FScriptVariantHandle>(Source);
		if (!Box) return false;
		if (Box->CanonicalTypeName == Target)
		{
			return CloneValueOrReport(Box->Value, Output, {});
		}
		const auto ScriptReference = [&](const std::string& Type)
		{
			const auto Index = FindTypeIndex(Type);
			std::uint16_t Interface = 0;
			return ActiveModule && ((Index < ActiveModule->Types.size() && ActiveModule->Types[Index].Kind == EScriptObjectKind::Class) || ModuleMeta::FindInterfaceIndex(*ActiveModule, Type, Interface));
		};
		const auto HostReference = [&](const std::string& Type)
		{
			const auto* Desc = MemoryBindings ? MemoryBindings->FindType(Type) : nullptr;
			return Desc && Desc->Kind == EHostTypeKind::Class;
		};
		const bool Primitive = Runtime::IsPrimitiveValueTypeName(Box->CanonicalTypeName) && Runtime::IsPrimitiveValueTypeName(Target);
		const bool EnumSource = std::holds_alternative<FEnumValue>(Box->Value);
		const bool EnumTarget = ActiveModule && std::any_of(ActiveModule->Enums.begin(), ActiveModule->Enums.end(), [&](const auto& E)
		{
			return E.bScoped && E.Name == Target;
		});
		const bool References = (ScriptReference(Box->CanonicalTypeName) && ScriptReference(Target)) || (HostReference(Box->CanonicalTypeName) && HostReference(Target));
		const auto IsInterface = [&](const std::string& Name)
		{
			std::uint16_t Index;
			return ActiveModule && ModuleMeta::FindInterfaceIndex(*ActiveModule, Name, Index);
		};
		const auto IsStruct = [&](const std::string& Name)
		{
			const auto Index = FindTypeIndex(Name);
			return ActiveModule && Index < ActiveModule->Types.size() && ActiveModule->Types[Index].Kind == EScriptObjectKind::Struct;
		};
		const bool StructInterface = (IsStruct(Box->CanonicalTypeName) && IsInterface(Target)) || (IsInterface(Box->CanonicalTypeName) && IsStruct(Target));
		if (!Primitive && !EnumSource && !EnumTarget && !References && !StructInterface) return false;
		FValue Candidate = Box->Value;
		if (HostReference(Box->CanonicalTypeName) && HostReference(Target))
		{
			if (const auto* Script = std::get_if<FObjectHandle>(&Candidate); Script && *Script && (*Script)->GetHostBase() && HostObjectMatchesType((*Script)->GetHostBase(), Target))
			{
				Output = std::move(Candidate);
				return true;
			}
		}
		if (!TryCheckedCastValue(Candidate, Target)) return false;
		if (IsStruct(Target)) return CloneValueOrReport(Candidate, Output, {});
		Output = std::move(Candidate);
		return true;
	}
}
