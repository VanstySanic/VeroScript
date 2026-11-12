// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptValueGraphInternal.h"

#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		const FBytecodeTypeDesc* FindType(
		    const std::vector<FBytecodeTypeDesc>& Types,
		    const std::string_view Name)
		{
			for (const FBytecodeTypeDesc& Type : Types)
			{
				if (Type.Name == Name)
				{
					return &Type;
				}
			}
			return nullptr;
		}

		bool IsInterface(
		    const FBytecodeModule& Module,
		    const std::string_view Name)
		{
			return std::any_of(
			    Module.Interfaces.begin(),
			    Module.Interfaces.end(),
			    [Name](const FBytecodeInterfaceDesc& Interface)
			{
				return Interface.Name == Name;
			});
		}

		bool IsSignedType(const std::string_view TypeName)
		{
			return TypeName == "int" || TypeName == "char" || TypeName == "short" || TypeName == "long";
		}

		// Leaf name after the last '\x1F' module-linkage separator (namespace dots kept).
		std::string_view TypeNameLeaf(const std::string_view TypeName)
		{
			return TypeName.substr(FindLinkageIdentityStart(TypeName));
		}

		/**
		 * True when TypeName is an enum field type (script Module.Enums or host enum).
		 * Field type names stay as enum names (not normalized to "int"),
		 * while private enums are stored as ModulePath + '\\x1F' + Name — so unqualified field
		 * names may match an enum leaf. Module-qualified TypeNames only match exactly,
		 * and never win over a concrete Module.Types / Interfaces entry (avoids treating
		 * a private class whose leaf collides with an exported enum as int).
		 */
		bool IsEnumRuntimeType(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    const IHostBindingCatalog* const HostBindings)
		{
			if (TypeName.empty())
			{
				return false;
			}
			if (FindType(Module.Types, TypeName) != nullptr || IsInterface(Module, TypeName))
			{
				return false;
			}
			for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
			{
				if (EnumDesc.Name == TypeName)
				{
					return true;
				}
			}
			// Unqualified names only: private enum linkage leaf, or host enum catalog name.
			if (TypeName.find('\x1F') == std::string_view::npos)
			{
				for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
				{
					if (TypeNameLeaf(EnumDesc.Name) == TypeName)
					{
						return true;
					}
				}
				if (HostBindings != nullptr)
				{
					if (const FHostTypeDesc* const HostType = HostBindings->FindType(TypeName);
					    HostType != nullptr && HostType->Kind == EHostTypeKind::Enum)
					{
						return true;
					}
				}
			}
			return false;
		}

		bool IsScopedScriptEnumRuntimeType(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName)
		{
			if (TypeName.empty() || FindType(Module.Types, TypeName) != nullptr || IsInterface(Module, TypeName))
			{
				return false;
			}
			for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
			{
				if (EnumDesc.bScoped && (EnumDesc.Name == TypeName || (TypeName.find('\x1F') == std::string_view::npos && TypeNameLeaf(EnumDesc.Name) == TypeName)))
				{
					return true;
				}
			}
			return false;
		}

		std::size_t FieldOccurrenceBefore(
		    const std::vector<FBytecodeFieldDesc>& Fields,
		    const std::size_t Index)
		{
			std::size_t Occurrence = 0;
			for (std::size_t Candidate = 0; Candidate < Index; ++Candidate)
			{
				if (Fields[Candidate].Name == Fields[Index].Name)
				{
					++Occurrence;
				}
			}
			return Occurrence;
		}

		const FBytecodeFieldDesc* FindFieldOccurrence(
		    const std::vector<FBytecodeFieldDesc>& Fields,
		    const std::string_view Name,
		    const std::size_t WantedOccurrence,
		    std::size_t* const OutIndex = nullptr)
		{
			std::size_t Occurrence = 0;
			for (std::size_t Index = 0; Index < Fields.size(); ++Index)
			{
				if (Fields[Index].Name != Name)
				{
					continue;
				}
				if (Occurrence++ == WantedOccurrence)
				{
					if (OutIndex != nullptr)
					{
						*OutIndex = Index;
					}
					return &Fields[Index];
				}
			}
			return nullptr;
		}

		class FStateMigrator
		{
		public:
			FStateMigrator(
			    const FHotReloadState& InOldState,
			    const FBytecodeModule& InNewModule,
			    const IHostBindingCatalog* const InHostBindings,
			    const std::uint64_t InNewProgramGeneration,
			    std::shared_ptr<const void> InProgramOwner)
			    : OldState(InOldState), NewModule(InNewModule), HostBindings(InHostBindings), NewProgramGeneration(InNewProgramGeneration), ProgramOwner(std::move(InProgramOwner))
			{
			}

			bool Build(std::vector<FValue>& OutGlobals, std::vector<std::string>& OutNames,
			    std::shared_ptr<FLocalStaticState>& OutStatics, std::vector<FValue>& OutExternalRoots, std::string& OutError)
			{
				if (OldState.GlobalNames.size() != OldState.Globals.size())
				{
					OutError = "hot reload state has inconsistent global metadata";
					return false;
				}

				std::unordered_map<std::string, const FHotReloadFunctionSchema*> StaticOwners;
				std::unordered_map<std::string, std::uint16_t> StaticSlots;
				for (const auto& Function : OldState.Functions)
				{
					for (const auto& Static : Function.LocalStatics)
					{
						StaticOwners.emplace(Static.GlobalName, &Function);
						StaticSlots.emplace(Static.GlobalName, Static.GlobalSlot);
					}
				}
				std::vector<std::uint16_t> SlotMap;
				std::unordered_set<const void*> StaticValuesSeen;
				std::unordered_set<std::string> StaticTypesSeen;
				const auto CompatibleStaticType = [&](const std::string& Name, const auto& Self) -> bool
				{
					if (!StaticTypesSeen.insert(Name).second) return true;
					const auto* Old = FindType(OldState.Types, Name);
					const auto* New = FindType(NewModule.Types, Name);
					if (!Old || !New || Old->Kind != New->Kind || Old->bIsStatic != New->bIsStatic || Old->Fields.size() != New->Fields.size() || Old->BaseTypeIndices.size() != New->BaseTypeIndices.size() || Old->OwnFieldStartIndex != New->OwnFieldStartIndex || Old->HostBaseTypeName != New->HostBaseTypeName || Old->HostBaseTypeId != New->HostBaseTypeId)
					{
						return false;
					}
					for (std::size_t Field = 0; Field < Old->Fields.size(); ++Field)
					{
						const auto& A = Old->Fields[Field];
						const auto& B = New->Fields[Field];
						if (A.Name != B.Name || A.TypeName != B.TypeName || A.bIsConst != B.bIsConst || A.bIsMutable != B.bIsMutable)
						{
							return false;
						}
					}
					for (std::size_t Base = 0; Base < Old->BaseTypeIndices.size(); ++Base)
					{
						if (Old->BaseTypeIndices[Base] >= OldState.Types.size() || New->BaseTypeIndices[Base] >= NewModule.Types.size()) return false;
						const auto& BaseName = OldState.Types[Old->BaseTypeIndices[Base]].Name;
						if (BaseName != NewModule.Types[New->BaseTypeIndices[Base]].Name || !Self(BaseName, Self)) return false;
					}
					return true;
				};
				if (OldState.LocalStatics)
				{
					if (!OldState.LocalStatics->Guards.empty() || OldState.LocalStatics->bDestroying)
					{
						OutError = "hot reload cannot retain active or destroying local static storage";
						return false;
					}
					OutStatics = std::make_shared<FLocalStaticState>();
					OutStatics->States.resize(NewModule.GlobalNames.size(), ELocalStaticInitialization::Uninitialized);
					SlotMap.resize(OldState.LocalStatics->States.size(), std::numeric_limits<std::uint16_t>::max());
				}
				OutGlobals.reserve(OldState.Globals.size());
				OutNames.reserve(OldState.GlobalNames.size());
				for (std::size_t Index = 0; Index < OldState.Globals.size(); ++Index)
				{
					const std::string& Name = OldState.GlobalNames[Index];
					const auto Static = StaticSlots.find(Name);
					const bool bStatic = Static != StaticSlots.end();
					if (bStatic && (!OldState.LocalStatics || Static->second >= OldState.LocalStatics->States.size() || OldState.LocalStatics->States[Static->second] != ELocalStaticInitialization::Initialized))
					{
						// Unreached declarations own no constructed value and may change or disappear.
						continue;
					}
					const auto NewSlot = NewModule.GlobalSlots.find(Name);
					if (NewSlot == NewModule.GlobalSlots.end())
					{
						OutError = "hot reload cannot remove retained global '" + Name + "'";
						return false;
					}

					if (bStatic)
					{
						const auto* Owner = StaticOwners.at(Name);
						const auto NewOwner = NewModule.FunctionIndices.find(Owner->Name);
						if (NewOwner == NewModule.FunctionIndices.end() || NewSlot->second >= NewModule.GlobalMetadata.size() || !NewModule.GlobalMetadata[NewSlot->second].bLocalStatic || NewModule.Functions[NewOwner->second].LocalStaticSemanticIdentity != Owner->LocalStaticSemanticIdentity)
						{
							OutError = "hot reload cannot change an initialized local static's owning function: " + Owner->Name + "; clear the session first";
							return false;
						}
						SlotMap[Static->second] = NewSlot->second;
						OutStatics->States[NewSlot->second] = ELocalStaticInitialization::Initialized;
					}
					else if (NewSlot->second < NewModule.GlobalMetadata.size() && NewModule.GlobalMetadata[NewSlot->second].bConstexpr)
					{
						continue;
					}

					const std::string_view OldType =
					    Index < OldState.GlobalTypeNames.size()
					    ? std::string_view(OldState.GlobalTypeNames[Index])
					    : std::string_view{};
					const std::string_view NewType =
					    NewSlot->second < NewModule.GlobalTypeNames.size()
					    ? std::string_view(NewModule.GlobalTypeNames[NewSlot->second])
					    : std::string_view{};
					if (!OldType.empty() && !NewType.empty() && OldType != NewType)
					{
						OutError = "hot reload cannot change type of global '" + Name + "' from '" + std::string(OldType) + "' to '" + std::string(NewType) + "'";
						return false;
					}

					FValue Value = OldState.Globals[Index];
					if (bStatic && !ValueGraphInternal::Visit(Value, [&](const FValue& Node, const void*)
					{
						const auto* Object = std::get_if<FObjectHandle>(&Node);
						return !Object || !*Object || CompatibleStaticType((*Object)->GetTypeName(), CompatibleStaticType);
					}, StaticValuesSeen))
					{
						OutError = "hot reload cannot change initialized local static value layouts: " + Name + "; clear the session first";
						return false;
					}
					if (!PlanValue(Value, NewType.empty() ? OldType : NewType, Name, OutError, true))
					{
						return false;
					}
					OutGlobals.push_back(std::move(Value));
					OutNames.push_back(Name);
				}
				OutExternalRoots.reserve(OldState.ExternalRoots.size());
				for (std::size_t Index = 0; Index < OldState.ExternalRoots.size(); ++Index)
				{
					FValue Value = OldState.ExternalRoots[Index];
					if (!PlanValue(Value, {}, "host root " + std::to_string(Index), OutError, true)) return false;
					OutExternalRoots.push_back(std::move(Value));
				}
				if (OutStatics)
				{
					for (const auto Slot : OldState.LocalStatics->Completed)
					{
						if (Slot >= SlotMap.size() || SlotMap[Slot] == std::numeric_limits<std::uint16_t>::max())
						{
							OutError = "hot reload local static completion order is inconsistent";
							return false;
						}
						OutStatics->Completed.push_back(SlotMap[Slot]);
					}
				}
				return true;
			}

			bool MakeFieldDefault(const FBytecodeFieldDesc& Field, FValue& OutValue, std::string& OutError)
			{
				if (Field.bHasInitCode)
				{
					OutError = "hot reload cannot default a field with a runtime initializer: " + Field.Name;
					return false;
				}
				if (Field.bHasDefault)
				{
					OutValue = Runtime::ConstantToValue(NewModule, Field.DefaultValue);
					return true;
				}
				std::unordered_set<std::string> Constructing;
				return MakeDefaultValue(Field.TypeName, OutValue, Constructing, OutError);
			}

			void Apply(FHotReloadRollback* const Rollback)
			{
				if (Rollback != nullptr)
				{
					Rollback->Objects.reserve(ObjectPlans.size());
					Rollback->Functions.reserve(FunctionPlans.size());
					Rollback->DynamicArrays.reserve(DynamicArrays.size());
					Rollback->Maps.reserve(Maps.size());
					Rollback->Sets.reserve(Sets.size());
					Rollback->Queues.reserve(Queues.size());
					Rollback->Stacks.reserve(Stacks.size());
					Rollback->OrderedMaps.reserve(OrderedMaps.size());
					Rollback->Deques.reserve(Deques.size());
					for (FDynamicArray* Value : DynamicArrays)
					{
						Rollback->DynamicArrays.push_back({Value, *Value});
					}
					for (FMap* Value : Maps)
					{
						Rollback->Maps.push_back({Value, *Value});
					}
					for (FSet* Value : Sets)
					{
						Rollback->Sets.push_back({Value, *Value});
					}
					for (FQueue* Value : Queues)
					{
						Rollback->Queues.push_back({Value, *Value});
					}
					for (FStack* Value : Stacks)
					{
						Rollback->Stacks.push_back({Value, *Value});
					}
					for (FOrderedMap* Value : OrderedMaps)
					{
						Rollback->OrderedMaps.push_back({Value, *Value});
					}
					for (FDeque* Value : Deques)
					{
						Rollback->Deques.push_back({Value, *Value});
					}
				}
				for (auto& [Slot, Value] : VariantSlots)
				{
					*Slot = std::move(Value);
				}
				for (auto& [Object, Fields] : ObjectPlans)
				{
					if (Rollback != nullptr)
					{
						FHotReloadObjectUndo Undo;
						Undo.Object = Object;
						Undo.Fields = std::move(Object->Fields);
						Undo.bDestroying = Object->bDestroying;
						Undo.bDestroyed = Object->bDestroyed;
						Rollback->Objects.push_back(std::move(Undo));
					}
					Object->Fields = std::move(Fields);
					Object->bDestroying = false;
					Object->bDestroyed = false;
				}
				for (auto& [Function, Plan] : FunctionPlans)
				{
					if (Rollback != nullptr)
					{
						Rollback->Functions.push_back(
						    {Function, Function->FunctionIndex,
						        Function->ProgramGeneration, Function->Captures});
					}
					Function->FunctionIndex = Plan.NewIndex;
					if (NewProgramGeneration != 0)
					{
						Function->ProgramGeneration = NewProgramGeneration;
					}
					Function->Captures = std::move(Plan.Captures);
				}
			}

		private:
			struct FFunctionPlan
			{
				std::uint16_t NewIndex = 0;
				std::vector<FValue> Captures;
			};

			bool TypeAcceptsValue(
			    const FValue& Value,
			    const std::string_view ExpectedType) const
			{
				if (ExpectedType.empty())
				{
					return true;
				}
				if (std::holds_alternative<FScriptVariantHandle>(Value)) return ExpectedType == "variant";
				if (ExpectedType == "variant") return false;
				if (std::holds_alternative<std::int32_t>(Value))
				{
					return ExpectedType == "int" || (IsEnumRuntimeType(NewModule, ExpectedType, HostBindings) && !IsScopedScriptEnumRuntimeType(NewModule, ExpectedType));
				}
				if (std::holds_alternative<std::int8_t>(Value))
				{
					return ExpectedType == "char";
				}
				if (std::holds_alternative<std::uint8_t>(Value))
				{
					return ExpectedType == "unsigned char";
				}
				if (std::holds_alternative<std::int16_t>(Value))
				{
					return ExpectedType == "short";
				}
				if (std::holds_alternative<std::uint16_t>(Value))
				{
					return ExpectedType == "unsigned short";
				}
				if (std::holds_alternative<std::uint32_t>(Value))
				{
					return ExpectedType == "unsigned int" || ExpectedType == "unsigned";
				}
				if (std::holds_alternative<std::int64_t>(Value))
				{
					return ExpectedType == "int64" || ExpectedType == "long";
				}
				if (std::holds_alternative<std::uint64_t>(Value))
				{
					return ExpectedType == "uint64" || ExpectedType == "unsigned long";
				}
				if (const auto* EnumValue = std::get_if<FEnumValue>(&Value))
				{
					return IsScopedScriptEnumRuntimeType(NewModule, ExpectedType) && (EnumValue->TypeName == ExpectedType || (ExpectedType.find('\x1F') == std::string_view::npos && TypeNameLeaf(EnumValue->TypeName) == ExpectedType) || (EnumValue->TypeName.find('\x1F') == std::string::npos && ExpectedType.substr(FindLinkageIdentityStart(ExpectedType)) == EnumValue->TypeName));
				}
				if (std::holds_alternative<float>(Value))
				{
					return ExpectedType == "float";
				}
				if (std::holds_alternative<double>(Value))
				{
					return ExpectedType == "double";
				}
				if (std::holds_alternative<bool>(Value))
				{
					return ExpectedType == "bool";
				}
				if (std::holds_alternative<std::string>(Value))
				{
					return ExpectedType == "string";
				}
				if (std::holds_alternative<std::u16string>(Value))
				{
					return ExpectedType == "wstring";
				}
				return true;
			}

			bool PlanValue(
			    FValue& Value,
			    const std::string_view ExpectedType,
			    const std::string& Path,
			    std::string& OutError,
			    const bool Detached = false)
			{
				if (auto* Box = std::get_if<FScriptVariantHandle>(&Value))
				{
					if (!ExpectedType.empty() && ExpectedType != "variant")
					{
						OutError = "hot reload variant type changed at '" + Path + "'";
						return false;
					}
					if (!*Box) return true;
					const auto Existing = Variants.find(Box->get());
					FScriptVariantHandle Replacement;
					if (Existing != Variants.end())
					{
						Replacement = Existing->second;
					}
					else
					{
						Replacement = std::make_shared<FScriptVariantStorage>();
						Replacement->CanonicalTypeName = (*Box)->CanonicalTypeName;
						Replacement->Value = (*Box)->Value;
						Replacement->bCopyRequiresVm = Variant::CopyRequiresVm(NewModule, Replacement->Value);
						if ((*Box)->Source)
						{
							auto Stamp = std::make_shared<FVariantSourceStamp>();
							Stamp->ProgramOwner = ProgramOwner;
							Stamp->Generation = NewProgramGeneration ? NewProgramGeneration : (*Box)->Source->Generation;
							Replacement->Source = std::move(Stamp);
						}
						Variants.emplace(Box->get(), Replacement);
						if (!PlanValue(Replacement->Value, Replacement->CanonicalTypeName, Path + ".$value", OutError, true)) return false;
					}
					if (Detached)
					{
						Value = Replacement;
					}
					else
					{
						VariantSlots.emplace_back(&Value, FValue(Replacement));
					}
					return true;
				}
				if (IsClosureReferenceCell(Value))
				{
					DynamicArrays.insert(std::get<FDynamicArrayHandle>(Value).get());
					FValue* const Visible = UnwrapClosureReferenceCell(&Value);
					if (!Visible)
					{
						OutError = "hot reload found an invalid reference cell at '" + Path + "'";
						return false;
					}
					return PlanValue(*Visible, ExpectedType, Path, OutError);
				}
				if (!TypeAcceptsValue(Value, ExpectedType))
				{
					OutError = "hot reload value type changed at '" + Path + "'";
					return false;
				}

				if (auto* Object = std::get_if<FObjectHandle>(&Value))
				{
					if (!*Object)
					{
						if (!ExpectedType.empty() && FindType(NewModule.Types, ExpectedType) == nullptr && !IsInterface(NewModule, ExpectedType))
						{
							OutError = "hot reload null object has incompatible type at '" + Path + "'";
							return false;
						}
						return true;
					}
					return PlanObject(**Object, Path, OutError);
				}
				if (auto* Array = std::get_if<FNativeArray>(&Value))
				{
					if (!ExpectedType.empty() && Array->TypeName() != ExpectedType)
					{
						OutError = "hot reload array type changed at '" + Path + "'";
						return false;
					}
					for (std::size_t Index = 0;
					    Index < Array->Storage().Elements.size(); ++Index)
					{
						if (!PlanValue(
						        Array->Storage().Elements[Index],
						        Array->ElementTypeName(),
						        Path + "[" + std::to_string(Index) + "]",
						        OutError))
						{
							return false;
						}
					}
					return true;
				}
				if (auto* Array = std::get_if<FDynamicArrayHandle>(&Value))
				{
					if (!*Array)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Array)->TypeName != ExpectedType)
					{
						OutError = "hot reload dynamic array type changed at '" + Path + "'";
						return false;
					}
					if (!DynamicArrays.insert(Array->get()).second)
					{
						return true;
					}
					for (std::size_t Index = 0; Index < (*Array)->Elements.size(); ++Index)
					{
						if (!PlanValue(
						        (*Array)->Elements[Index],
						        (*Array)->ElementTypeName,
						        Path + "[" + std::to_string(Index) + "]",
						        OutError))
						{
							return false;
						}
					}
					return true;
				}
				if (auto* Map = std::get_if<FMapHandle>(&Value))
				{
					if (!*Map)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Map)->TypeName != ExpectedType)
					{
						OutError = "hot reload map type changed at '" + Path + "'";
						return false;
					}
					if (!Maps.insert(Map->get()).second)
					{
						return true;
					}
					return PlanMapValues(**Map, Path, OutError);
				}
				if (auto* Map = std::get_if<FOrderedMapHandle>(&Value))
				{
					if (!*Map)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Map)->TypeName != ExpectedType)
					{
						OutError = "hot reload ordered_map type changed at '" + Path + "'";
						return false;
					}
					if (!OrderedMaps.insert(Map->get()).second)
					{
						return true;
					}
					return PlanOrderedMapValues(**Map, Path, OutError);
				}
				if (auto* Set = std::get_if<FSetHandle>(&Value))
				{
					if (*Set)
					{
						if (!ExpectedType.empty() && (*Set)->TypeName != ExpectedType)
						{
							OutError = "hot reload set type changed at '" + Path + "'";
							return false;
						}
						Sets.insert(Set->get());
					}
					return true;
				}
				if (auto* Queue = std::get_if<FQueueHandle>(&Value))
				{
					if (!*Queue)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Queue)->TypeName != ExpectedType)
					{
						OutError = "hot reload queue type changed at '" + Path + "'";
						return false;
					}
					if (!Queues.insert(Queue->get()).second)
					{
						return true;
					}
					return PlanSequence(
					    (*Queue)->Elements, (*Queue)->ElementTypeName, Path, OutError);
				}
				if (auto* Stack = std::get_if<FStackHandle>(&Value))
				{
					if (!*Stack)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Stack)->TypeName != ExpectedType)
					{
						OutError = "hot reload stack type changed at '" + Path + "'";
						return false;
					}
					if (!Stacks.insert(Stack->get()).second)
					{
						return true;
					}
					return PlanSequence(
					    (*Stack)->Elements, (*Stack)->ElementTypeName, Path, OutError);
				}
				if (auto* Deque = std::get_if<FDequeHandle>(&Value))
				{
					if (!*Deque)
					{
						return true;
					}
					if (!ExpectedType.empty() && (*Deque)->TypeName != ExpectedType)
					{
						OutError = "hot reload deque type changed at '" + Path + "'";
						return false;
					}
					if (!Deques.insert(Deque->get()).second)
					{
						return true;
					}
					return PlanSequence(
					    (*Deque)->Elements, (*Deque)->ElementTypeName, Path, OutError);
				}
				if (auto* Function = std::get_if<FFunctionHandle>(&Value))
				{
					return !*Function || PlanFunction(**Function, Path, OutError);
				}
				if (const auto* Channel = std::get_if<FChannelHandle>(&Value))
				{
					if (*Channel)
					{
						OutError = "hot reload cannot retain a live channel at '" + Path + "'";
						return false;
					}
					return true;
				}
				if (std::holds_alternative<FLocalRef>(Value) || std::holds_alternative<FGlobalRef>(Value) || std::holds_alternative<FThisFieldRef>(Value) || std::holds_alternative<FIndexRef>(Value) || std::holds_alternative<FMapEntryRef>(Value))
				{
					OutError = "hot reload cannot retain a transient reference at '" + Path + "'";
					return false;
				}
				return true;
			}

			bool PlanObject(
			    FScriptObject& Object,
			    const std::string& Path,
			    std::string& OutError)
			{
				if (ObjectPlans.find(&Object) != ObjectPlans.end())
				{
					return true;
				}

				const FBytecodeTypeDesc* OldType = FindType(OldState.Types, Object.GetTypeName());
				const FBytecodeTypeDesc* NewType = FindType(NewModule.Types, Object.GetTypeName());
				if (OldType == nullptr || NewType == nullptr || OldType->Kind != NewType->Kind || OldType->bIsStatic != NewType->bIsStatic)
				{
					OutError = "hot reload type is missing or changed kind/static contract: '" + Object.GetTypeName() + "'";
					return false;
				}
				if (Object.Fields.size() != OldType->Fields.size())
				{
					OutError = "hot reload object layout is inconsistent for '" + Object.GetTypeName() + "'";
					return false;
				}

				// Reserve the identity before descending so cyclic class graphs terminate.
				auto PlanResult = ObjectPlans.emplace(&Object, std::vector<FValue>{});
				std::vector<FValue>& NewFields = PlanResult.first->second;
				NewFields.reserve(NewType->Fields.size());

				for (std::size_t OldIndex = 0;
				    OldIndex < OldType->Fields.size();
				    ++OldIndex)
				{
					const FBytecodeFieldDesc& OldField = OldType->Fields[OldIndex];
					const FBytecodeFieldDesc* Found = FindFieldOccurrence(
					    NewType->Fields,
					    OldField.Name,
					    FieldOccurrenceBefore(OldType->Fields, OldIndex));
					if (Found == nullptr)
					{
						ObjectPlans.erase(&Object);
						OutError = "hot reload cannot remove retained field '" + Object.GetTypeName() + "." + OldField.Name + "'";
						return false;
					}
					if (Found->TypeName != OldField.TypeName)
					{
						ObjectPlans.erase(&Object);
						OutError = "hot reload cannot change type of retained field '" + Object.GetTypeName() + "." + OldField.Name + "'";
						return false;
					}
				}

				for (std::size_t NewIndex = 0;
				    NewIndex < NewType->Fields.size();
				    ++NewIndex)
				{
					const FBytecodeFieldDesc& NewField = NewType->Fields[NewIndex];
					std::size_t OldIndex = 0;
					const FBytecodeFieldDesc* Found = FindFieldOccurrence(
					    OldType->Fields,
					    NewField.Name,
					    FieldOccurrenceBefore(NewType->Fields, NewIndex),
					    &OldIndex);
					if (Found == nullptr)
					{
						FValue Default;
						if (NewField.bHasInitCode)
						{
							ObjectPlans.erase(&Object);
							OutError = "hot reload cannot add retained field '" + Object.GetTypeName() + "." + NewField.Name + "' with a runtime initializer";
							return false;
						}
						if (!MakeFieldDefault(NewField, Default, OutError))
						{
							ObjectPlans.erase(&Object);
							return false;
						}
						NewFields.push_back(std::move(Default));
						continue;
					}

					FValue FieldValue = Object.Fields[OldIndex];
					const auto* Empty = std::get_if<std::int32_t>(&FieldValue);
					if (Empty && *Empty == 0 && std::any_of(NewField.DeclMeta.begin(), NewField.DeclMeta.end(), [](const auto& Entry)
					{
						return Entry.first == UninitializedPayloadMetaKey;
					}))
					{
						NewFields.push_back(std::move(FieldValue));
						continue;
					}
					if (!PlanValue(
					        FieldValue,
					        NewField.TypeName,
					        Path + "." + NewField.Name,
					        OutError, true))
					{
						ObjectPlans.erase(&Object);
						return false;
					}
					NewFields.push_back(std::move(FieldValue));
				}
				return true;
			}

			template <typename TSequence>
			bool PlanSequence(
			    TSequence& Values,
			    const std::string& ElementType,
			    const std::string& Path,
			    std::string& OutError)
			{
				std::size_t Index = 0;
				for (FValue& Value : Values)
				{
					if (!PlanValue(
					        Value,
					        ElementType,
					        Path + "[" + std::to_string(Index++) + "]",
					        OutError))
					{
						return false;
					}
				}
				return true;
			}

			template <typename TEntries>
			bool PlanEntries(
			    TEntries& Entries,
			    const std::string& ValueType,
			    const std::string& Path,
			    std::string& OutError)
			{
				for (auto& [Key, Value] : Entries)
				{
					(void)Key;
					if (!PlanValue(Value, ValueType, Path + "[entry]", OutError))
					{
						return false;
					}
				}
				return true;
			}

			bool PlanMapValues(
			    FMap& Map,
			    const std::string& Path,
			    std::string& OutError)
			{
				return PlanEntries(Map.IntEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.UIntEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.Int64Entries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.UInt64Entries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.StrEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.WStrEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.EnumEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.HostEntries, Map.ValueTypeName, Path, OutError);
			}

			bool PlanOrderedMapValues(
			    FOrderedMap& Map,
			    const std::string& Path,
			    std::string& OutError)
			{
				return PlanEntries(Map.IntEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.UIntEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.Int64Entries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.UInt64Entries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.StrEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.WStrEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.EnumEntries, Map.ValueTypeName, Path, OutError) && PlanEntries(Map.HostEntries, Map.ValueTypeName, Path, OutError);
			}

			bool PlanFunction(
			    FFunctionValue& Function,
			    const std::string& Path,
			    std::string& OutError)
			{
				if (FunctionPlans.find(&Function) != FunctionPlans.end())
				{
					return true;
				}
				if (Function.FunctionIndex >= OldState.Functions.size())
				{
					OutError = "hot reload closure has invalid function at '" + Path + "'";
					return false;
				}
				const FHotReloadFunctionSchema& Old =
				    OldState.Functions[Function.FunctionIndex];
				if (Function.FunctionName.empty() || Function.FunctionName != Old.Name || Function.TypeName != Old.ClosureTypeName)
				{
					OutError = "hot reload closure identity is stale at '" + Path + "'";
					return false;
				}
				if (Old.Captures.size() != Function.Captures.size())
				{
					OutError = "hot reload closure has incomplete capture metadata at '" + Path + "'";
					return false;
				}
				for (std::size_t Index = 0; Index < NewModule.Functions.size(); ++Index)
				{
					const FBytecodeFunction& Candidate = NewModule.Functions[Index];
					if (Candidate.Name == Old.Name && Candidate.ParamCount == Old.ParamCount && Candidate.ClosureTypeName == Old.ClosureTypeName)
					{
						if (Candidate.Captures.size() != Old.Captures.size())
						{
							OutError = "hot reload cannot add or remove closure captures at '" + Path + "'";
							return false;
						}
						FFunctionPlan Plan;
						Plan.NewIndex = static_cast<std::uint16_t>(Index);
						Plan.Captures.reserve(Candidate.Captures.size());
						std::vector<std::size_t> PreviousSlots;
						if (!BuildHotReloadCaptureSlots(Old, Candidate, PreviousSlots, Path, OutError)) return false;
						for (std::size_t NewSlot = 0;
						    NewSlot < Candidate.Captures.size();
						    ++NewSlot)
						{
							const std::size_t OldSlot = PreviousSlots[NewSlot];
							FValue Capture = Function.Captures[OldSlot];
							std::string CaptureType = Candidate.Captures[NewSlot].BindingTypeName();
							const bool bReferenceCapture = CaptureType.rfind("ref ", 0) == 0 || CaptureType.rfind("const ref ", 0) == 0 || Candidate.Captures[NewSlot].bStableStorage;
							if (CaptureType.rfind("const ref ", 0) == 0)
							{
								CaptureType.erase(0, 10);
							}
							else if (CaptureType.rfind("ref ", 0) == 0)
							{
								CaptureType.erase(0, 4);
							}
							FValue* PlannedValue = &Capture;
							if (bReferenceCapture)
							{
								if (!IsClosureReferenceCell(Capture))
								{
									OutError = "hot reload closure reference capture is invalid at '" + Path + "." + Candidate.Captures[NewSlot].Name + "'";
									return false;
								}
								PlannedValue = &std::get<FDynamicArrayHandle>(Capture)->Elements[0];
								DynamicArrays.insert(std::get<FDynamicArrayHandle>(Capture).get());
							}
							if (!PlanValue(
							        *PlannedValue,
							        CaptureType,
							        Path + "." + Candidate.Captures[NewSlot].Name,
							        OutError, !bReferenceCapture))
							{
								return false;
							}
							Plan.Captures.push_back(std::move(Capture));
						}
						FunctionPlans.emplace(&Function, std::move(Plan));
						if (Function.CapturedThis && !PlanObject(*Function.CapturedThis, Path + ".this", OutError))
						{
							return false;
						}
						return true;
					}
				}
				OutError = "hot reload cannot remap closure function at '" + Path + "'; clear the session first";
				return false;
			}

			bool MakeDefaultValue(
			    const std::string& TypeName,
			    FValue& OutValue,
			    std::unordered_set<std::string>& Constructing,
			    std::string& OutError)
			{
				if (TypeName == "bool")
				{
					OutValue = false;
					return true;
				}
				if (TypeName == "string")
				{
					OutValue = std::string{};
					return true;
				}
				if (TypeName == "variant")
				{
					OutValue = FScriptVariantHandle{};
					return true;
				}
				if (TypeName == "wstring")
				{
					OutValue = std::u16string{};
					return true;
				}
				if (TypeName == "float" || TypeName == "double")
				{
					OutValue = 0.0;
					return true;
				}
				if (TypeName == "uint64")
				{
					OutValue = std::uint64_t{0};
					return true;
				}
				if (Runtime::IsUnsignedTypeName(TypeName))
				{
					OutValue = std::uint32_t{0};
					return true;
				}
				if (TypeName == "int64")
				{
					OutValue = std::int64_t{0};
					return true;
				}
				if (IsScopedScriptEnumRuntimeType(NewModule, TypeName))
				{
					OutValue = FEnumValue{Runtime::EnumValueTypeName(NewModule, TypeName), 0};
					return true;
				}
				if (IsSignedType(TypeName) || IsEnumRuntimeType(NewModule, TypeName, HostBindings))
				{
					OutValue = std::int32_t{0};
					return true;
				}
				if (Runtime::IsFunctionTypeName(TypeName))
				{
					OutValue = FFunctionHandle{};
					return true;
				}
				if (Runtime::IsDynamicArrayTypeName(TypeName))
				{
					std::string Element;
					if (!Runtime::ParseDynamicArrayElementType(TypeName, Element))
					{
						OutError = "hot reload cannot parse array type '" + TypeName + "'";
						return false;
					}
					auto Array = std::make_shared<FDynamicArray>();
					Array->TypeName = TypeName;
					Array->ElementTypeName = std::move(Element);
					// Match normal VM construction so a primitive array introduced by a
					// hot reload does not silently lose the canonical compact storage path.
					(void)Array->EnableScalarStorage();
					OutValue = std::move(Array);
					return true;
				}
				if (Runtime::IsMapTypeName(TypeName))
				{
					std::string Key;
					std::string Value;
					if (!Runtime::ParseMapType(TypeName, Key, Value))
					{
						OutError = "hot reload cannot parse map type '" + TypeName + "'";
						return false;
					}
					auto Map = std::make_shared<FMap>();
					Map->TypeName = TypeName;
					Map->KeyTypeName = std::move(Key);
					Map->ValueTypeName = std::move(Value);
					OutValue = std::move(Map);
					return true;
				}
				if (Runtime::IsMultiSetTypeName(TypeName))
				{
					std::string Element;
					if (!Runtime::ParseMultiSetElementType(TypeName, Element))
					{
						OutError = "hot reload cannot parse multiset type '" + TypeName + "'";
						return false;
					}
					auto Map = std::make_shared<FMap>();
					Map->TypeName = TypeName;
					Map->KeyTypeName = std::move(Element);
					Map->ValueTypeName = "array<bool>";
					OutValue = std::move(Map);
					return true;
				}
				if (Runtime::IsSetTypeName(TypeName))
				{
					std::string Element;
					if (!Runtime::ParseSetElementType(TypeName, Element))
					{
						OutError = "hot reload cannot parse set type '" + TypeName + "'";
						return false;
					}
					auto Set = std::make_shared<FSet>();
					Set->TypeName = TypeName;
					Set->ElementTypeName = std::move(Element);
					OutValue = std::move(Set);
					return true;
				}
				if (Runtime::IsQueueTypeName(TypeName) || Runtime::IsStackTypeName(TypeName) || Runtime::IsDequeTypeName(TypeName))
				{
					std::string Element;
					const bool bParsed = Runtime::IsQueueTypeName(TypeName)
					    ? Runtime::ParseQueueElementType(TypeName, Element)
					    : Runtime::IsStackTypeName(TypeName)
					    ? Runtime::ParseStackElementType(TypeName, Element)
					    : Runtime::ParseDequeElementType(TypeName, Element);
					if (!bParsed)
					{
						OutError = "hot reload cannot parse sequence type '" + TypeName + "'";
						return false;
					}
					if (Runtime::IsQueueTypeName(TypeName))
					{
						auto Queue = std::make_shared<FQueue>();
						Queue->TypeName = TypeName;
						Queue->ElementTypeName = std::move(Element);
						OutValue = std::move(Queue);
					}
					else if (Runtime::IsStackTypeName(TypeName))
					{
						auto Stack = std::make_shared<FStack>();
						Stack->TypeName = TypeName;
						Stack->ElementTypeName = std::move(Element);
						OutValue = std::move(Stack);
					}
					else
					{
						auto Deque = std::make_shared<FDeque>();
						Deque->TypeName = TypeName;
						Deque->ElementTypeName = std::move(Element);
						OutValue = std::move(Deque);
					}
					return true;
				}
				if (Runtime::IsOrderedMapTypeName(TypeName))
				{
					std::string Key;
					std::string Value;
					if (!Runtime::ParseOrderedMapType(TypeName, Key, Value))
					{
						OutError = "hot reload cannot parse ordered_map type '" + TypeName + "'";
						return false;
					}
					auto Map = std::make_shared<FOrderedMap>();
					Map->TypeName = TypeName;
					Map->KeyTypeName = std::move(Key);
					Map->ValueTypeName = std::move(Value);
					OutValue = std::move(Map);
					return true;
				}
				if (Runtime::IsOrderedMultiSetTypeName(TypeName))
				{
					std::string Element;
					if (!Runtime::ParseMultiSetElementType(TypeName, Element))
					{
						OutError =
						    "hot reload cannot parse ordered_multiset type '" + TypeName + "'";
						return false;
					}
					auto Map = std::make_shared<FOrderedMap>();
					Map->TypeName = TypeName;
					Map->KeyTypeName = std::move(Element);
					Map->ValueTypeName = "array<bool>";
					OutValue = std::move(Map);
					return true;
				}
				if (Runtime::IsChannelTypeName(TypeName))
				{
					OutValue = FChannelHandle{};
					return true;
				}
				if (Runtime::IsNativeArrayTypeName(TypeName))
				{
					std::string Element;
					std::uint32_t Length = 0;
					if (!Runtime::ParseNativeArrayType(TypeName, Element, Length))
					{
						OutError = "hot reload cannot parse native array type '" + TypeName + "'";
						return false;
					}
					FNativeArray Array;
					Array.TypeName() = TypeName;
					Array.ElementTypeName() = Element;
					Array.Length() = Length;
					Array.Storage().Elements.reserve(Length);
					for (std::uint32_t Index = 0; Index < Length; ++Index)
					{
						FValue ElementValue;
						if (!MakeDefaultValue(
						        Element,
						        ElementValue,
						        Constructing,
						        OutError))
						{
							return false;
						}
						Array.Storage().Elements.push_back(std::move(ElementValue));
					}
					OutValue = std::move(Array);
					return true;
				}

				const FBytecodeTypeDesc* Type = FindType(NewModule.Types, TypeName);
				if (Type == nullptr)
				{
					if (IsInterface(NewModule, TypeName))
					{
						OutValue = FObjectHandle{};
						return true;
					}
					OutError = "hot reload cannot construct new field type '" + TypeName + "'";
					return false;
				}
				if (Type->Kind == EScriptObjectKind::Class)
				{
					OutValue = FObjectHandle{};
					return true;
				}
				if (!Type->HostBaseTypeName.empty())
				{
					// Host-derived structs need the host @subclass factory, which the
					// migrator cannot invoke; a null HostBase would be an invalid value.
					OutError = "hot reload cannot construct new field type '" + TypeName + "' deriving from host type '" + Type->HostBaseTypeName + "'";
					return false;
				}
				if (!Constructing.insert(TypeName).second)
				{
					OutError = "hot reload found recursive value type '" + TypeName + "'";
					return false;
				}
				auto Object = std::make_shared<FScriptObject>();
				Object->ProgramOwner = ProgramOwner;
				Object->Kind = Type->Kind;
				Object->SetTypeName(Type->Name);
				Object->Fields.reserve(Type->Fields.size());
				for (const FBytecodeFieldDesc& Field : Type->Fields)
				{
					if (std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(),
					        [](const auto& Entry)
					{
						return Entry.first == UninitializedPayloadMetaKey;
					}))
					{
						Object->Fields.push_back(std::int32_t{0});
						continue;
					}
					if (Field.bHasInitCode)
					{
						Constructing.erase(TypeName);
						OutError = "hot reload cannot default field '" + TypeName + "." + Field.Name + "' with a runtime initializer";
						return false;
					}
					FValue FieldValue;
					if (Field.bHasDefault)
					{
						FieldValue = Runtime::ConstantToValue(
						    NewModule,
						    Field.DefaultValue);
					}
					else if (!MakeDefaultValue(
					             Field.TypeName,
					             FieldValue,
					             Constructing,
					             OutError))
					{
						Constructing.erase(TypeName);
						return false;
					}
					Object->Fields.push_back(std::move(FieldValue));
				}
				Constructing.erase(TypeName);
				OutValue = std::move(Object);
				return true;
			}

			const FHotReloadState& OldState;
			const FBytecodeModule& NewModule;
			const IHostBindingCatalog* HostBindings = nullptr;
			std::uint64_t NewProgramGeneration = 0;
			std::shared_ptr<const void> ProgramOwner;
			std::unordered_map<FScriptObject*, std::vector<FValue>> ObjectPlans;
			std::unordered_map<FScriptVariantStorage*, FScriptVariantHandle> Variants;
			std::vector<std::pair<FValue*, FValue>> VariantSlots;
			std::unordered_map<FFunctionValue*, FFunctionPlan> FunctionPlans;
			std::unordered_set<FDynamicArray*> DynamicArrays;
			std::unordered_set<FMap*> Maps;
			std::unordered_set<FSet*> Sets;
			std::unordered_set<FQueue*> Queues;
			std::unordered_set<FStack*> Stacks;
			std::unordered_set<FOrderedMap*> OrderedMaps;
			std::unordered_set<FDeque*> Deques;
		};
	}

	bool BuildHotReloadCaptureSlots(
	    const FHotReloadFunctionSchema& Previous,
	    const FBytecodeFunction& Replacement,
	    std::vector<std::size_t>& PreviousSlots,
	    const std::string& Path,
	    std::string& OutError)
	{
		if (Previous.Captures.size() != Replacement.Captures.size())
		{
			OutError = "hot reload cannot add or remove closure captures at '" + Path + "'";
			return false;
		}
		PreviousSlots.clear();
		PreviousSlots.reserve(Replacement.Captures.size());
		std::unordered_set<std::size_t> UsedSlots;
		for (const auto& Capture : Replacement.Captures)
		{
			const auto Found = std::find_if(Previous.Captures.begin(), Previous.Captures.end(),
			    [&](const FBytecodeCapture& Candidate)
			{
				return Candidate.Name == Capture.Name;
			});
			if (Found == Previous.Captures.end())
			{
				OutError = "hot reload cannot replace closure capture '" + Capture.Name + "' at '" + Path + "'";
				return false;
			}
			const auto Slot = static_cast<std::size_t>(Found - Previous.Captures.begin());
			if (!UsedSlots.insert(Slot).second || !(*Found == Capture))
			{
				OutError = "hot reload closure capture type changed at '" + Path + "." + Capture.Name + "'";
				return false;
			}
			PreviousSlots.push_back(Slot);
		}
		return true;
	}

	bool BuildHotReloadFieldSlots(
	    const FHotReloadState& PreviousState,
	    const FBytecodeModule& Module,
	    const FScriptObject& Object,
	    std::vector<std::size_t>& PreviousSlots,
	    std::size_t& PreviousFieldCount,
	    std::string& OutError)
	{
		const auto* Previous = FindType(PreviousState.Types, Object.GetTypeName());
		const auto* Replacement = FindType(Module.Types, Object.GetTypeName());
		if (!Previous || !Replacement || Previous->Kind != Replacement->Kind || Previous->bIsStatic != Replacement->bIsStatic)
		{
			OutError = "hot reload cleanup type is missing or incompatible: " + Object.GetTypeName();
			return false;
		}
		for (std::size_t Slot = 0; Slot < Previous->Fields.size(); ++Slot)
		{
			const auto& Field = Previous->Fields[Slot];
			const auto* Found = FindFieldOccurrence(Replacement->Fields, Field.Name,
			    FieldOccurrenceBefore(Previous->Fields, Slot));
			if (!Found || Found->TypeName != Field.TypeName)
			{
				OutError = "hot reload cleanup cannot restore field '" + Object.GetTypeName() + "." + Field.Name + "'";
				return false;
			}
		}
		PreviousFieldCount = Previous->Fields.size();
		PreviousSlots.assign(Replacement->Fields.size(), std::numeric_limits<std::size_t>::max());
		for (std::size_t Slot = 0; Slot < Replacement->Fields.size(); ++Slot)
		{
			const auto& Field = Replacement->Fields[Slot];
			std::size_t PreviousSlot = 0;
			if (FindFieldOccurrence(Previous->Fields, Field.Name,
			        FieldOccurrenceBefore(Replacement->Fields, Slot), &PreviousSlot))
			{
				PreviousSlots[Slot] = PreviousSlot;
			}
		}
		return true;
	}

	bool BuildHotReloadCleanupFields(
	    const FHotReloadState& PreviousState,
	    const FBytecodeModule& Module,
	    const FScriptObject& Object,
	    std::vector<std::size_t>& PreviousSlots,
	    std::vector<FValue>& Fields,
	    std::string& OutError,
	    const IHostBindingCatalog* const HostBindings,
	    std::shared_ptr<const void> ProgramOwner)
	{
		std::size_t PreviousFieldCount = 0;
		if (!BuildHotReloadFieldSlots(PreviousState, Module, Object, PreviousSlots, PreviousFieldCount, OutError)) return false;
		const auto* Replacement = FindType(Module.Types, Object.GetTypeName());
		FStateMigrator Defaults(PreviousState, Module, HostBindings, 0, std::move(ProgramOwner));
		Fields.assign(PreviousSlots.size(), std::int32_t{0});
		for (std::size_t Slot = 0; Slot < PreviousSlots.size(); ++Slot)
		{
			if (PreviousSlots[Slot] == std::numeric_limits<std::size_t>::max() && !Defaults.MakeFieldDefault(Replacement->Fields[Slot], Fields[Slot], OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool MigrateHotReloadState(
	    FHotReloadState& State,
	    const FBytecodeModule& NewModule,
	    FHotReloadRollback* const OutRollback,
	    std::string& OutError,
	    const IHostBindingCatalog* const HostBindings,
	    const std::uint64_t NewProgramGeneration,
	    std::shared_ptr<const void> ProgramOwner)
	{
		OutError.clear();
		if (State.Empty())
		{
			if (OutRollback != nullptr)
			{
				OutRollback->Clear();
			}
			return true;
		}

		FStateMigrator Migrator(
		    State, NewModule, HostBindings, NewProgramGeneration,
		    std::move(ProgramOwner));
		std::vector<FValue> MigratedGlobals;
		std::vector<std::string> MigratedNames;
		std::shared_ptr<FLocalStaticState> MigratedStatics;
		std::vector<FValue> MigratedExternalRoots;
		if (!Migrator.Build(MigratedGlobals, MigratedNames, MigratedStatics, MigratedExternalRoots, OutError))
		{
			return false;
		}
		if (OutRollback != nullptr)
		{
			OutRollback->Clear();
			OutRollback->PreviousState = State;
		}
		Migrator.Apply(OutRollback);
		State.InvalidateCapturedModule();
		State.Globals = std::move(MigratedGlobals);
		State.ExternalRoots = std::move(MigratedExternalRoots);
		State.GlobalNames = std::move(MigratedNames);
		State.LocalStatics = std::move(MigratedStatics);
		State.GlobalTypeNames.clear();
		State.GlobalTypeNames.reserve(State.GlobalNames.size());
		for (const std::string& Name : State.GlobalNames)
		{
			const auto Slot = NewModule.GlobalSlots.find(Name);
			State.GlobalTypeNames.push_back(
			    Slot != NewModule.GlobalSlots.end() && Slot->second < NewModule.GlobalTypeNames.size()
			        ? NewModule.GlobalTypeNames[Slot->second]
			        : std::string{});
		}
		State.Types = NewModule.Types;
		if (NewProgramGeneration != 0)
		{
			State.ProgramGeneration = NewProgramGeneration;
		}
		State.Functions.clear();
		State.Functions.reserve(NewModule.Functions.size());
		for (const FBytecodeFunction& Function : NewModule.Functions)
		{
			State.Functions.emplace_back(Function, NewModule);
		}
		State.bNeedsModuleInitialization = true;
		return true;
	}

	void CaptureHotReloadState(
	    FHotReloadState& State,
	    const FBytecodeModule& Module,
	    std::vector<FValue>& Globals,
	    const std::uint64_t ProgramGeneration,
	    const std::shared_ptr<const FBytecodeModule>& ModuleOwner)
	{
		const bool bHasModuleSchema = State.HasModuleSchema(Module, ModuleOwner, ProgramGeneration);
		if (State.bNeedsModuleInitialization || !bHasModuleSchema)
		{
			State.GlobalNames = Module.GlobalNames;
			State.GlobalTypeNames = Module.GlobalTypeNames;
			if (!bHasModuleSchema)
			{
				State.InvalidateCapturedModule();
				State.Types = Module.Types;
				State.Functions.clear();
				State.Functions.reserve(Module.Functions.size());
				for (const FBytecodeFunction& Function : Module.Functions)
				{
					State.Functions.emplace_back(Function, Module);
				}
				State.RememberModuleSchema(Module, ModuleOwner);
			}
			if (State.CapturedModuleAddress == &Module)
			{
				State.bCapturedGlobalsInSlotOrder = true;
				for (std::size_t Index = 0; Index < Module.GlobalNames.size(); ++Index)
				{
					const auto Slot = Module.GlobalSlots.find(Module.GlobalNames[Index]);
					if (Slot == Module.GlobalSlots.end() || Slot->second != Index ||
					    (Index < Module.GlobalMetadata.size() && Module.GlobalMetadata[Index].bConstexpr &&
					        !Module.GlobalMetadata[Index].bLocalStatic))
					{
						// The ordinary restore handles remapped slots and skips constexpr globals.
						State.bCapturedGlobalsInSlotOrder = false;
						break;
					}
				}
			}
		}
		State.Globals = std::move(Globals);
		State.ProgramGeneration = ProgramGeneration;
		State.bNeedsModuleInitialization = false;
		Globals.clear();
	}
}
