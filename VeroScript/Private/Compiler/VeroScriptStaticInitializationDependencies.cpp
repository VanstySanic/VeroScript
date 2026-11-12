// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStaticInitializationDependencies.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace CompilerDetail
	{
		namespace
		{
			struct FDependencyBinding;

			struct FDependencyValue
			{
				std::uint16_t Closure = 0xFFFF;
				std::string Type;
				std::size_t Allocation = 0;
				std::shared_ptr<const std::vector<FDependencyValue>> Captures;
				FDependencyBinding* Reference = nullptr;
			};

			struct FDependencyBinding
			{
				FDependencyValue Value;
				bool bInitialized = false;
				bool bShared = false;
			};

			struct FPreparedDependencyFunction
			{
				std::size_t EntryRevision = 0;
				FDependencyValue Returned;
			};

			struct FPreparedDependencyDescriptor
			{
				std::optional<std::size_t> EntryRevision;
				bool bActive = false;
			};

			class FDescriptorPreparationScope
			{
			public:
				FDescriptorPreparationScope(std::unordered_map<std::uint16_t, FPreparedDependencyDescriptor>& InStates,
				    const std::uint16_t InIndex, const std::size_t InEntryRevision)
				    : States(InStates), Index(InIndex), EntryRevision(InEntryRevision)
				{
					const auto& State = States[Index];
					bRequired = !State.bActive && State.EntryRevision != EntryRevision;
					if (bRequired)
					{
						States[Index].bActive = true;
					}
				}

				~FDescriptorPreparationScope()
				{
					if (bRequired)
					{
						States[Index].bActive = false;
					}
				}

				bool IsRequired() const
				{
					return bRequired;
				}

				bool Complete()
				{
					States[Index].EntryRevision = EntryRevision;
					return true;
				}

			private:
				std::unordered_map<std::uint16_t, FPreparedDependencyDescriptor>& States;
				std::uint16_t Index;
				std::size_t EntryRevision;
				bool bRequired = false;
			};

			class FStaticInitializationDependencies
			{
			public:
				FStaticInitializationDependencies(FBytecodeModule& InModule,
				    const std::function<bool(const std::string&)>& InPrepareOwner,
				    const std::function<void()>& InCheckCancelled)
				    : Module(InModule), PrepareOwner(InPrepareOwner), CheckCancelled(InCheckCancelled)
				{
				}

				bool Prepare(const std::vector<std::uint8_t>& Code)
				{
					return PrepareCode(Code, {}, {});
				}

			private:
				FDependencyValue Visible(FDependencyValue Value) const
				{
					std::unordered_set<const FDependencyBinding*> Visited;
					while (Value.Reference)
					{
						if (!Visited.insert(Value.Reference).second) return {};
						Value = Value.Reference->Value;
					}
					return Value;
				}

				void WriteBinding(FDependencyBinding* Binding, FDependencyValue Value,
				    const bool WriteThrough)
				{
					std::unordered_set<const FDependencyBinding*> Visited;
					while (WriteThrough && Binding->Value.Reference)
					{
						if (!Visited.insert(Binding).second) return;
						Binding = Binding->Value.Reference;
					}
					Binding->Value = WriteThrough ? Visible(std::move(Value)) : std::move(Value);
					Binding->bInitialized = true;
					if (Binding->bShared)
					{
						++StateRevision;
					}
				}

				FDependencyBinding* CreateBinding()
				{
					BindingArena.push_back(std::make_unique<FDependencyBinding>());
					return BindingArena.back().get();
				}

				FDependencyBinding* GlobalBinding(const std::uint16_t Slot)
				{
					auto& Binding = GlobalBindings[Slot];
					if (!Binding)
					{
						Binding = CreateBinding();
						Binding->bShared = true;
					}
					return Binding;
				}

				std::uint16_t FindType(const std::string& Name) const
				{
					for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
					{
						if (Module.Types[Index].Name == Name) return static_cast<std::uint16_t>(Index);
					}
					return 0xFFFF;
				}

				std::string String(const std::uint16_t Index) const
				{
					return Index < Module.StringPool.size() ? Module.StringPool[Index] : std::string{};
				}

				FDependencyValue OwnedValue(const std::string& Type)
				{
					const auto Allocation = NextAllocation++;
					AllocationTypes.emplace(Allocation, Type);
					return {0xFFFF, Type, Allocation, {}};
				}

				std::string ElementType(const std::string& Container) const
				{
					std::string Element, Key;
					std::uint32_t Length = 0;
					if (Runtime::ParseNativeArrayType(Container, Element, Length) ||
					    Runtime::ParseDynamicArrayElementType(Container, Element) ||
					    Runtime::ParseDequeElementType(Container, Element) ||
					    Runtime::SplitMapType(Container, Key, Element) ||
					    Runtime::SplitOrderedMapType(Container, Key, Element))
					{
						return Element;
					}
					return {};
				}

				bool PrepareDestruction(const std::string& Name)
				{
					std::string Element;
					std::uint32_t Length = 0;
					if (Runtime::ParseNativeArrayType(Name, Element, Length))
					{
						const auto Type = FindType(Element);
						return Length == 0 || (Type < Module.Types.size() && Module.Types[Type].Kind == EScriptObjectKind::Class) || PrepareDestruction(Element);
					}
					const auto Index = FindType(Name);
					if (Index >= Module.Types.size()) return true;
					FDescriptorPreparationScope Preparation(PreparedDestructors, Index, StateRevision);
					if (!Preparation.IsRequired()) return true;
					const auto Type = Module.Types[Index];
					if (!PrepareFunction(Type.DestructorFunctionIndex, {}, {0xFFFF, Name})) return false;
					for (std::size_t FieldIndex = Type.Fields.size(); FieldIndex > Type.OwnFieldStartIndex; --FieldIndex)
					{
						const auto& Field = Type.Fields[FieldIndex - 1];
						const auto FieldType = FindType(Field.TypeName);
						if ((Runtime::IsNativeArrayTypeName(Field.TypeName) ||
						        (FieldType < Module.Types.size() && Module.Types[FieldType].Kind == EScriptObjectKind::Struct)) &&
						    !Runtime::IsReferenceTypeName(Field.DeclaredTypeName) &&
						    !std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(), [](const auto& Entry)
						{
							return Entry.first == UninitializedPayloadMetaKey;
						}) &&
						    !PrepareDestruction(Field.TypeName))
						{
							return false;
						}
					}
					for (auto Base = Type.BaseTypeIndices.rbegin(); Base != Type.BaseTypeIndices.rend(); ++Base)
					{
						if (*Base < Module.Types.size())
						{
							const auto BaseName = Module.Types[*Base].Name;
							if (!PrepareDestruction(BaseName)) return false;
						}
					}
					return Preparation.Complete();
				}

				FDependencyValue FieldValue(const FDependencyValue& Object, const std::uint16_t Name) const
				{
					const auto Type = FindType(Visible(Object).Type);
					if (Type == 0xFFFF || Name >= Module.StringPool.size()) return {};
					for (const auto& Field : Module.Types[Type].Fields)
					{
						if (Field.Name == Module.StringPool[Name]) return {0xFFFF, Field.TypeName};
					}
					return {};
				}

				FDependencyValue ReturnValue(const std::uint16_t Index) const
				{
					if (Index >= Module.Functions.size()) return {};
					std::vector<std::string> Parameters;
					std::string Result;
					if (!Runtime::ParseFunctionType(Module.Functions[Index].ClosureTypeName, Parameters, Result)) return {};
					return {0xFFFF, std::move(Result)};
				}

				FDependencyValue GlobalValue(const std::uint16_t Slot)
				{
					const auto Binding = GlobalBinding(Slot);
					if (Binding->bInitialized) return Visible(Binding->Value);
					if (Slot >= Module.GlobalTypeNames.size() ||
					    !Runtime::IsFunctionTypeName(Module.GlobalTypeNames[Slot]))
					{
						return {};
					}
					if (!ActiveGlobals.insert(Slot).second) return {};
					// Only an already-emitted initializer can prove a stored callable's identity.
					// Inspecting its provenance must not execute deferred closure dependencies.
					std::vector<std::uint8_t> Initializer;
					for (auto Range = Module.GlobalInitRanges.rbegin(); Range != Module.GlobalInitRanges.rend(); ++Range)
					{
						if (Range->GlobalSlot == Slot && Range->BeginOffset <= Range->EndOffset &&
						    Range->EndOffset <= Module.InitCode.size())
						{
							bool Stable = true;
							for (std::size_t Ip = Range->EndOffset; Ip < Module.InitCode.size();)
							{
								CheckCancelled();
								const auto Opcode = static_cast<EOpcode>(Module.InitCode[Ip]);
								const auto Size = Bytecode::InstructionSize(Opcode);
								if (Size > Module.InitCode.size() - Ip)
								{
									Stable = false;
									break;
								}
								if (Opcode == EOpcode::StoreGlobal || Opcode == EOpcode::MakeGlobalRef)
								{
									if (Bytecode::ReadU16LE(Module.InitCode.data() + Ip + 1) == Slot)
									{
										Stable = false;
										break;
									}
								}
								else if (Opcode == EOpcode::Call || Opcode == EOpcode::CallBoundMethod ||
								    Opcode == EOpcode::CallIndirect || Opcode == EOpcode::CallMethod ||
								    Opcode == EOpcode::CallNative || Opcode == EOpcode::CallBuiltin || Opcode == EOpcode::CallMemory ||
								    Opcode == EOpcode::NewObject || Opcode == EOpcode::NewNativeArray || Opcode == EOpcode::TryCast ||
								    Opcode == EOpcode::CloneStruct || Opcode == EOpcode::CloneValueOfType ||
								    Opcode == EOpcode::SetIndex || Opcode == EOpcode::StoreField || Opcode == EOpcode::StoreThisField)
								{
									// A later callback or aliased mutation can replace the callable.
									Stable = false;
									break;
								}
								Ip += Size;
							}
							if (Stable)
							{
								Initializer.assign(Module.InitCode.begin() + Range->BeginOffset, Module.InitCode.begin() + Range->EndOffset);
							}
							break;
						}
					}
					(void)PrepareCode(Initializer, {}, {}, nullptr, false);
					ActiveGlobals.erase(Slot);
					Binding->bInitialized = true;
					return Visible(Binding->Value);
				}

				bool PrepareGlobal(const std::uint16_t Slot)
				{
					if (Slot >= Module.GlobalNames.size() || Slot >= Module.GlobalMetadata.size() ||
					    !Module.GlobalMetadata[Slot].bStaticMember)
					{
						return true;
					}
					const auto Name = Module.GlobalNames[Slot];
					const auto Separator = Name.rfind('.');
					return Separator == std::string::npos || PrepareOwner(Name.substr(0, Separator));
				}

				bool PrepareDefault(const std::string& Name)
				{
					std::string Element;
					std::uint32_t Length = 0;
					if (Runtime::ParseNativeArrayType(Name, Element, Length))
					{
						return Length == 0 || PrepareDefault(Element);
					}
					const auto Type = FindType(Name);
					// Reference fields default to null; empty containers construct no elements.
					if (Type == 0xFFFF || Module.Types[Type].Kind != EScriptObjectKind::Struct) return true;
					const auto Factory = Module.FunctionIndices.find(Name + std::string(DefaultConstructionFactorySuffix));
					if (Factory != Module.FunctionIndices.end()) return PrepareFunction(Factory->second, {}, {});
					return PrepareType(Type);
				}

				bool PrepareType(const std::uint16_t Index)
				{
					if (Index >= Module.Types.size()) return true;
					FDescriptorPreparationScope Preparation(PreparedTypes, Index, StateRevision);
					if (!Preparation.IsRequired()) return true;
					// Recursive callbacks can grow both descriptor tables; retain owned snapshots.
					const auto Name = Module.Types[Index].Name;
					const auto Fields = Module.Types[Index].Fields;
					if (!PrepareOwner(Name)) return false;
					for (const auto& Field : Fields)
					{
						CheckCancelled();
						if (Field.bHasDefault || std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(), [](const auto& Entry)
						{
							return Entry.first == UninitializedPayloadMetaKey;
						}))
						{
							continue;
						}
						if (Field.bHasInitCode)
						{
							if (!PrepareCode(Field.InitCode, {}, {0xFFFF, Name})) return false;
						}
						else if (!PrepareDefault(Field.TypeName))
						{
							return false;
						}
					}
					return Preparation.Complete();
				}

				bool PrepareCopyFields(const std::uint16_t Index)
				{
					if (Index >= Module.Types.size()) return true;
					FDescriptorPreparationScope Preparation(PreparedCopyFields, Index, StateRevision);
					if (!Preparation.IsRequired()) return true;
					const auto Type = Module.Types[Index];
					for (const auto Base : Type.BaseTypeIndices)
					{
						if (Base < Module.Types.size() && !PrepareCopy(Module.Types[Base].Name, true)) return false;
					}
					for (std::size_t FieldIndex = Type.OwnFieldStartIndex; FieldIndex < Type.Fields.size(); ++FieldIndex)
					{
						CheckCancelled();
						const auto& Field = Type.Fields[FieldIndex];
						if (!Runtime::IsReferenceTypeName(Field.DeclaredTypeName) &&
						    !std::any_of(Field.DeclMeta.begin(), Field.DeclMeta.end(), [](const auto& Entry)
						{
							return Entry.first == UninitializedPayloadMetaKey;
						}) &&
						    !PrepareCopy(Field.TypeName, true))
						{
							return false;
						}
					}
					return Preparation.Complete();
				}

				bool PrepareCopy(const std::string& Name, const bool AllowExplicit)
				{
					std::string Element;
					std::uint32_t Length = 0;
					if (Runtime::ParseNativeArrayType(Name, Element, Length))
					{
						return Length == 0 || PrepareCopy(Element, AllowExplicit);
					}
					const auto Type = FindType(Name);
					if (Type == 0xFFFF || Module.Types[Type].Kind != EScriptObjectKind::Struct) return true;
					auto Constructor = Module.Types[Type].CopyConstructorIndex;
					if (Constructor >= Module.Functions.size())
					{
						// The type-level cache is finalized after static initializers. The
						// compiled constructor already carries its exact special-member kind.
						for (std::size_t Index = 0; Index < Module.Functions.size(); ++Index)
						{
							const auto& Function = Module.Functions[Index];
							if (Function.bIsMethod && Function.OwnerTypeIndex == Type &&
							    Function.SpecialMemberKind == ESpecialMemberKind::CopyConstructor)
							{
								Constructor = static_cast<std::uint16_t>(Index);
								break;
							}
						}
					}
					if (Constructor < Module.Functions.size())
					{
						if (!AllowExplicit && Module.Functions[Constructor].bExplicitCopy) return true;
						return PrepareFunction(Constructor, {{0xFFFF, Name}}, {0xFFFF, Name});
					}
					return PrepareCopyFields(Type);
				}

				bool PrepareFunction(const std::uint16_t Index, const std::vector<FDependencyValue>& Arguments,
				    FDependencyValue Receiver, FDependencyValue* const Result = nullptr, const bool DiscoverDependencies = true)
				{
					if (Result)
					{
						*Result = ReturnValue(Index);
					}
					if (Index >= Module.Functions.size() || ActiveFunctions.count(Index) != 0) return true;
					const auto EntryRevision = StateRevision;
					const bool HasStatefulInput = Receiver.Reference || Receiver.Allocation != 0 || !Receiver.Type.empty() ||
					    std::any_of(Arguments.begin(), Arguments.end(), [&](const auto& Argument)
					{
						const auto Value = Visible(Argument);
						return Argument.Reference || Value.Closure != 0xFFFF || Value.Allocation != 0 ||
						    !Value.Type.empty() || Value.Captures;
					});
					const auto Prepared = PreparedFunctions.find(Index);
					if (DiscoverDependencies && !HasStatefulInput && Prepared != PreparedFunctions.end() &&
					    Prepared->second.EntryRevision == EntryRevision)
					{
						if (Result)
						{
							*Result = Prepared->second.Returned;
							if (Result->Allocation != 0)
							{
								const auto Allocation = AllocationTypes.find(Result->Allocation);
								Result->Allocation = Arguments.empty() && Receiver.Allocation == 0 && Allocation != AllocationTypes.end()
								    ? OwnedValue(Allocation->second).Allocation
								    : 0;
							}
						}
						return true;
					}
					const auto Function = Module.Functions[Index];
					ActiveFunctions.insert(Index);
					struct FActiveScope
					{
						~FActiveScope()
						{
							Active.erase(Index);
						}
						std::unordered_set<std::uint16_t>& Active;
						std::uint16_t Index;
					} ActiveScope{ActiveFunctions, Index};
					if (Function.bIsMethod && Receiver.Type.empty() && Function.OwnerTypeIndex < Module.Types.size())
					{
						Receiver.Type = Module.Types[Function.OwnerTypeIndex].Name;
					}
					const auto Separator = FindMethodLinkageSeparator(Function.Name);
					if (DiscoverDependencies && Separator != std::string::npos && !PrepareOwner(Function.Name.substr(0, Separator))) return false;
					auto Parameters = Arguments;
					for (std::size_t Parameter = 0; Parameter < Parameters.size(); ++Parameter)
					{
						const auto Capture = Parameter >= Function.ParamCount ? Parameter - Function.ParamCount : Function.Captures.size();
						const bool ByReference = Parameter < Function.ParamCount
						    ? Parameter < Function.ParamFlags.size() && (Function.ParamFlags[Parameter] & BytecodeParamFlagRef) != 0
						    : Capture < Function.Captures.size() && Function.Captures[Capture].Mode != EClosureCaptureMode::Value;
						if (!ByReference)
						{
							Parameters[Parameter] = Visible(Parameters[Parameter]);
						}
					}
					FDependencyValue Returned;
					if (!PrepareCode(Function.Code, Parameters, Visible(Receiver), &Returned, DiscoverDependencies, &Function.ExceptionRegions)) return false;
					if (!Function.bReturnsRef)
					{
						Returned = Visible(std::move(Returned));
					}
					if (Returned.Type.empty())
					{
						Returned.Type = ReturnValue(Index).Type;
					}
					if (DiscoverDependencies && !HasStatefulInput)
					{
						// Reuse is valid only in the state that existed before this call.
						// A function that writes shared bindings cannot reuse its post-write result.
						PreparedFunctions[Index] = {EntryRevision, Returned};
					}
					if (Result)
					{
						*Result = std::move(Returned);
					}
					return true;
				}

				bool PrepareCode(const std::vector<std::uint8_t>& Source,
				    const std::vector<FDependencyValue>& Arguments, const FDependencyValue& Receiver,
				    FDependencyValue* const Returned = nullptr, const bool DiscoverDependencies = true,
				    const std::vector<FBytecodeExceptionRegion>* const ExceptionRegions = nullptr)
				{
					const auto Code = Source;
					std::unordered_set<std::size_t> Joins;
					std::unordered_map<std::size_t, std::size_t> Sizes, Branches;
					for (std::size_t Ip = 0; Ip < Code.size();)
					{
						CheckCancelled();
						const auto Opcode = static_cast<EOpcode>(Code[Ip]);
						const auto Size = Bytecode::InstructionSize(Opcode);
						if (Size > Code.size() - Ip) return false;
						Sizes.emplace(Ip, Size);
						if (Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse ||
						    Opcode == EOpcode::JmpIfLocalGeImm || Opcode == EOpcode::BeginLocalStatic)
						{
							const auto Operand = Ip + (Opcode == EOpcode::JmpIfLocalGeImm ? 7 : Opcode == EOpcode::BeginLocalStatic ? 3
							                                                                                                        : 1);
							const auto Target = static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Code.data() + Operand);
							if (Target >= 0 && static_cast<std::uint64_t>(Target) <= Code.size())
							{
								Joins.insert(static_cast<std::size_t>(Target));
								Branches.emplace(Ip, static_cast<std::size_t>(Target));
							}
						}
						Ip += Size;
					}
					std::unordered_set<std::size_t> Reachable;
					std::vector<std::size_t> Pending{0};
					while (!Pending.empty())
					{
						CheckCancelled();
						const auto Ip = Pending.back();
						Pending.pop_back();
						const auto Size = Sizes.find(Ip);
						if (Size == Sizes.end() || !Reachable.insert(Ip).second)
						{
							continue;
						}
						const auto Opcode = static_cast<EOpcode>(Code[Ip]);
						const auto Branch = Branches.find(Ip);
						if (Branch != Branches.end())
						{
							Pending.push_back(Branch->second);
						}
						if (Opcode != EOpcode::Jmp && Opcode != EOpcode::Return && Opcode != EOpcode::Throw)
						{
							Pending.push_back(Ip + Size->second);
						}
						if (ExceptionRegions)
						{
							for (const auto& Region : *ExceptionRegions)
							{
								if (Ip >= Region.BeginOffset && Ip < Region.EndOffset)
								{
									for (const auto& Handler : Region.Catches)
									{
										Pending.push_back(Handler.HandlerOffset);
										Joins.insert(Handler.HandlerOffset);
									}
									Pending.push_back(Region.FinallyOffset);
									Joins.insert(Region.FinallyOffset);
								}
							}
						}
					}

					std::vector<FDependencyValue> Stack;
					std::unordered_map<std::uint16_t, FDependencyBinding*> Locals;
					std::unordered_map<std::size_t, std::string> Owned;
					std::unordered_set<std::size_t> Borrowed, Transferred;
					Borrowed.insert(Receiver.Allocation);
					bool StraightLine = true;
					bool HasReturn = false;
					for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
					{
						auto* Binding = CreateBinding();
						Binding->Value = Arguments[Index];
						Binding->bInitialized = true;
						Locals.emplace(static_cast<std::uint16_t>(Index), Binding);
						Borrowed.insert(Visible(Arguments[Index]).Allocation);
					}
					const auto LocalBinding = [&](const std::uint16_t Slot)
					{
						auto& Binding = Locals[Slot];
						if (!Binding)
						{
							Binding = CreateBinding();
						}
						return Binding;
					};
					const auto Pop = [&]() -> FDependencyValue
					{
						if (Stack.empty()) return {};
						auto Value = std::move(Stack.back());
						Stack.pop_back();
						return Value;
					};
					const auto PopArguments = [&](const std::size_t Count)
					{
						std::vector<FDependencyValue> Values(Count);
						for (std::size_t Index = Count; Index > 0; --Index)
						{
							Values[Index - 1] = Pop();
						}
						return Values;
					};
					const auto Track = [&](FDependencyValue Value)
					{
						const auto Allocation = AllocationTypes.find(Value.Allocation);
						if (Allocation != AllocationTypes.end() && Borrowed.count(Value.Allocation) == 0)
						{
							Owned.emplace(Allocation->first, Allocation->second);
						}
						return Value;
					};
					const auto Release = [&](const FDependencyValue& Value) -> bool
					{
						if (!DiscoverDependencies || Value.Allocation == 0 || Borrowed.count(Value.Allocation) != 0 ||
						    Transferred.count(Value.Allocation) != 0 || RetainedAllocations.count(Value.Allocation) != 0)
						{
							return true;
						}
						const auto SameAllocation = [&](const auto& Other)
						{
							return Other.Allocation == Value.Allocation;
						};
						if (std::any_of(Stack.begin(), Stack.end(), SameAllocation) ||
						    std::any_of(Locals.begin(), Locals.end(), [&](const auto& Local)
						{
							return SameAllocation(Visible(Local.second->Value));
						}))
						{
							return true;
						}
						const auto Allocation = AllocationTypes.find(Value.Allocation);
						if (Allocation == AllocationTypes.end()) return true;
						const auto Name = Allocation->second;
						Owned.erase(Value.Allocation);
						return PrepareDestruction(Name);
					};
					const auto ReleaseScope = [&](const std::size_t Preserved) -> bool
					{
						Transferred.insert(Preserved);
						Stack.clear();
						Locals.clear();
						const auto Values = Owned;
						for (const auto& Value : Values)
						{
							if (!Release({0xFFFF, Value.second, Value.first, {}})) return false;
						}
						return true;
					};
					for (std::size_t Ip = 0; Ip < Code.size();)
					{
						CheckCancelled();
						if (Reachable.count(Ip) == 0)
						{
							Ip += Sizes.at(Ip);
							continue;
						}
						// All branches contribute dependencies, but branch-local provenance cannot
						// identify a later indirect call without a complete control-flow analysis.
						if (Joins.count(Ip) != 0)
						{
							Stack.clear();
							Locals.clear();
							StraightLine = false;
						}
						const auto Opcode = static_cast<EOpcode>(Code[Ip]);
						const auto Size = Bytecode::InstructionSize(Opcode);
						const auto Operand = Size >= 3 ? Bytecode::ReadU16LE(Code.data() + Ip + 1) : std::uint16_t{0};
						switch (Opcode)
						{
						case EOpcode::LoadGlobal:
						case EOpcode::MakeGlobalRef:
						case EOpcode::StoreGlobal:
							{
								if (DiscoverDependencies && !PrepareGlobal(Operand)) return false;
								if (Opcode == EOpcode::StoreGlobal)
								{
									const auto Value = Visible(Pop());
									RetainedAllocations.insert(Value.Allocation);
									if (Value.Captures)
									{
										for (const auto& Capture : *Value.Captures)
										{
											RetainedAllocations.insert(Capture.Allocation);
										}
									}
									WriteBinding(GlobalBinding(Operand), StraightLine ? Value : FDependencyValue{}, true);
								}
								else
								{
									auto Value = GlobalValue(Operand);
									if (Value.Type.empty() && Operand < Module.GlobalTypeNames.size())
									{
										Value.Type = Module.GlobalTypeNames[Operand];
									}
									if (Opcode == EOpcode::MakeGlobalRef)
									{
										Value.Reference = GlobalBinding(Operand);
									}
									Stack.push_back(std::move(Value));
								}
								break;
							}
						case EOpcode::Call:
						case EOpcode::CallBoundMethod:
							{
								const auto Values = PopArguments(Code[Ip + 3]);
								const auto Object = Opcode == EOpcode::CallBoundMethod ? Pop() : FDependencyValue{};
								FDependencyValue Result;
								if (!PrepareFunction(Operand, Values, Object, &Result, DiscoverDependencies)) return false;
								Stack.push_back(Track(std::move(Result)));
								for (const auto& Value : Values)
								{
									if (!Release(Value)) return false;
								}
								if (!Release(Object)) return false;
								break;
							}
						case EOpcode::MakeClosure:
							{
								auto Captures = PopArguments(Code[Ip + 3]);
								for (std::size_t Capture = 0; Operand < Module.Functions.size() &&
								    Capture < Captures.size() && Capture < Module.Functions[Operand].Captures.size();
								    ++Capture)
								{
									const auto Mode = Module.Functions[Operand].Captures[Capture].Mode;
									if (Mode == EClosureCaptureMode::MutableValue)
									{
										auto* Binding = CreateBinding();
										Binding->Value = Visible(Captures[Capture]);
										Binding->bInitialized = true;
										Binding->bShared = true;
										Captures[Capture].Reference = Binding;
									}
									else if (Mode == EClosureCaptureMode::Value)
									{
										Captures[Capture] = Visible(Captures[Capture]);
									}
								}
								Stack.push_back({Operand, {}, 0, std::make_shared<const std::vector<FDependencyValue>>(Captures)});
								break;
							}
						case EOpcode::CallIndirect:
							{
								const auto Callee = Visible(Pop());
								auto Values = PopArguments(Code[Ip + 1]);
								if (Callee.Captures)
								{
									Values.insert(Values.end(), Callee.Captures->begin(), Callee.Captures->end());
								}
								FDependencyValue Result;
								if (Callee.Closure != 0xFFFF && !PrepareFunction(Callee.Closure, Values, Receiver, &Result, DiscoverDependencies)) return false;
								Stack.push_back(Track(std::move(Result)));
								for (const auto& Value : Values)
								{
									if (!Release(Value)) return false;
								}
								break;
							}
						case EOpcode::NewObject:
						case EOpcode::NewUninitializedObject:
							{
								if (DiscoverDependencies && Opcode == EOpcode::NewObject && !PrepareType(Operand)) return false;
								Stack.push_back(Track(OwnedValue(Operand < Module.Types.size() ? Module.Types[Operand].Name : std::string{})));
								break;
							}
						case EOpcode::TryCast:
							{
								const auto Optional = Bytecode::ReadU16LE(Code.data() + Ip + 3);
								if (DiscoverDependencies && !PrepareType(Optional)) return false;
								const auto SourceValue = Pop();
								const auto Target = String(Operand);
								const auto Type = FindType(Target);
								auto Result = OwnedValue(Optional < Module.Types.size() ? Module.Types[Optional].Name : std::string{});
								if (SourceValue.Type == Target && Type < Module.Types.size())
								{
									if (Module.Types[Type].Kind == EScriptObjectKind::Struct)
									{
										if (DiscoverDependencies && !PrepareCopy(Target, false)) return false;
										Result.Allocation = OwnedValue(Target).Allocation;
									}
									else
									{
										Result.Allocation = SourceValue.Allocation;
									}
								}
								Stack.push_back(Track(std::move(Result)));
								if (!Release(SourceValue)) return false;
								break;
							}
						case EOpcode::NewNativeArray:
						case EOpcode::NewUninitializedNativeArray:
						case EOpcode::NewDynamicArray:
						case EOpcode::NewMap:
						case EOpcode::NewSet:
						case EOpcode::NewQueue:
						case EOpcode::NewStack:
						case EOpcode::NewOrderedMap:
						case EOpcode::NewDeque:
						case EOpcode::NewChannel:
							{
								const auto Name = String(Operand);
								if (DiscoverDependencies && Opcode == EOpcode::NewNativeArray && !PrepareDefault(Name)) return false;
								Stack.push_back(Opcode == EOpcode::NewUninitializedNativeArray
								        ? FDependencyValue{0xFFFF, Name}
								        : Track(OwnedValue(Name)));
								break;
							}
						case EOpcode::CloneStruct:
						case EOpcode::CloneValueOfType:
							{
								const auto SourceValue = Pop();
								auto Value = SourceValue;
								if (Opcode == EOpcode::CloneValueOfType)
								{
									Value.Type = String(Operand);
								}
								if (DiscoverDependencies && !PrepareCopy(Value.Type, Opcode == EOpcode::CloneValueOfType && Bytecode::ReadU16LE(Code.data() + Ip + 3) != 0))
								{
									return false;
								}
								const auto Type = FindType(Value.Type);
								if (Type < Module.Types.size() && Module.Types[Type].Kind == EScriptObjectKind::Struct)
								{
									Value.Allocation = OwnedValue(Value.Type).Allocation;
								}
								Stack.push_back(Track(std::move(Value)));
								if (!Release(SourceValue)) return false;
								break;
							}
						case EOpcode::DefaultCopyConstruct:
							{
								if (DiscoverDependencies && !PrepareCopyFields(FindType(Receiver.Type))) return false;
								break;
							}
						case EOpcode::LoadLocal:
						case EOpcode::MakeLocalRef:
						case EOpcode::TakeLocal:
							{
								const auto Binding = LocalBinding(Operand);
								auto Value = Opcode == EOpcode::TakeLocal ? Binding->Value : Visible(Binding->Value);
								if (Opcode == EOpcode::MakeLocalRef)
								{
									Binding->bShared = true;
									Value.Reference = Binding;
								}
								Stack.push_back(std::move(Value));
								if (Opcode == EOpcode::TakeLocal)
								{
									WriteBinding(Binding, {}, false);
								}
								break;
							}
						case EOpcode::StoreLocal:
						case EOpcode::BindLocal:
							{
								const auto Value = Pop();
								const auto Binding = LocalBinding(Operand);
								const auto Previous = Visible(Binding->Value);
								WriteBinding(Binding, Track(Value), Opcode == EOpcode::StoreLocal);
								if (!Release(Previous)) return false;
								break;
							}
						case EOpcode::LoadThis:
							{
								Stack.push_back(Receiver);
								break;
							}
						case EOpcode::LoadField:
						case EOpcode::LoadFieldKeep:
						case EOpcode::MakeFieldRef:
							{
								const auto Object = Opcode == EOpcode::LoadFieldKeep && !Stack.empty() ? Stack.back() : Pop();
								Stack.push_back(FieldValue(Object, Operand));
								break;
							}
						case EOpcode::LoadThisField:
						case EOpcode::MakeThisFieldRef:
							{
								Stack.push_back(FieldValue(Receiver, Operand));
								break;
							}
						case EOpcode::Dup:
							{
								Stack.push_back(Stack.empty() ? FDependencyValue{} : Stack.back());
								break;
							}
						case EOpcode::StoreField:
							{
								const auto Value = Pop();
								const auto Object = Pop();
								RetainedAllocations.insert(Value.Allocation);
								Stack.push_back(Value);
								if (!Release(Object)) return false;
								break;
							}
						case EOpcode::SetIndex:
						case EOpcode::InitIndex:
							{
								const auto Value = Pop();
								(void)Pop();
								const auto Container = Pop();
								if (DiscoverDependencies && Opcode == EOpcode::SetIndex)
								{
									const auto Element = ElementType(Container.Type);
									const auto Type = FindType(Element);
									std::string Key;
									std::string MapValue;
									const bool Map = Runtime::SplitMapType(Container.Type, Key, MapValue) ||
									    Runtime::SplitOrderedMapType(Container.Type, Key, MapValue);
									if (!Map && Type < Module.Types.size() && Module.Types[Type].Kind == EScriptObjectKind::Struct)
									{
										auto Assignment = Module.FunctionIndices.find(
										    MakeMethodFunctionName(Element, "operator=", {"const ref " + Element}));
										if (Assignment == Module.FunctionIndices.end())
										{
											Assignment = Module.FunctionIndices.find(
											    MakeMethodFunctionName(Element, "@copy_assign", {"const ref " + Element}));
										}
										if (Assignment != Module.FunctionIndices.end())
										{
											if (!PrepareFunction(Assignment->second, {Value}, {0xFFFF, Element})) return false;
										}
										else if (!PrepareCopyFields(Type))
										{
											return false;
										}
									}
									else if (!PrepareCopy(Value.Type, false))
									{
										return false;
									}
								}
								if (Opcode == EOpcode::InitIndex || FindType(Value.Type) == 0xFFFF ||
								    Module.Types[FindType(Value.Type)].Kind == EScriptObjectKind::Class)
								{
									RetainedAllocations.insert(Value.Allocation);
								}
								Stack.push_back(Container);
								if (!Release(Value)) return false;
								break;
							}
						case EOpcode::GetIndexCopy:
							{
								(void)Pop();
								const auto Container = Pop();
								const auto Element = ElementType(Container.Type);
								if (DiscoverDependencies && !PrepareCopy(Element, false)) return false;
								const auto Type = FindType(Element);
								if (Type < Module.Types.size() && Module.Types[Type].Kind == EScriptObjectKind::Struct)
								{
									Stack.push_back(Track(OwnedValue(Element)));
								}
								else
								{
									Stack.push_back({0xFFFF, Element});
								}
								if (!Release(Container)) return false;
								break;
							}
						case EOpcode::Add:
						case EOpcode::Sub:
						case EOpcode::Mul:
						case EOpcode::Div:
						case EOpcode::Mod:
						case EOpcode::Eq:
						case EOpcode::Ne:
						case EOpcode::Lt:
						case EOpcode::Le:
						case EOpcode::Gt:
						case EOpcode::Ge:
						case EOpcode::BitAnd:
						case EOpcode::BitOr:
						case EOpcode::BitXor:
						case EOpcode::Shl:
						case EOpcode::Shr:
						case EOpcode::SameObject:
						case EOpcode::AssignHostBase:
						case EOpcode::EqualHostBase:
						case EOpcode::AssignCoreMetadata:
							{
								(void)PopArguments(2);
								Stack.push_back({});
								break;
							}
						case EOpcode::Not:
						case EOpcode::Neg:
						case EOpcode::BitNot:
						case EOpcode::VariantIs:
						case EOpcode::GetIndexRef:
						case EOpcode::GetIndexPathRef:
						case EOpcode::GetMapEntryRef:
							{
								const auto Value = Pop();
								Stack.push_back({});
								if (!Release(Value)) return false;
								break;
							}
						case EOpcode::BoxVariant:
						case EOpcode::CheckedCast:
							{
								const auto SourceValue = Pop();
								const auto Target = String(Operand);
								const auto Type = FindType(Target);
								auto Result = SourceValue;
								if (DiscoverDependencies && !PrepareCopy(Target, Opcode == EOpcode::CheckedCast && SourceValue.Type != "variant")) return false;
								if (Type < Module.Types.size() && Module.Types[Type].Kind == EScriptObjectKind::Struct)
								{
									Result = OwnedValue(Target);
								}
								Result.Type = Opcode == EOpcode::BoxVariant ? "variant" : Target;
								Result.Closure = 0xFFFF;
								Stack.push_back(Track(std::move(Result)));
								if (!Release(SourceValue)) return false;
								break;
							}
						case EOpcode::IncLocal:
						case EOpcode::AddLocalImm:
						case EOpcode::AddLocalLocal:
							{
								WriteBinding(LocalBinding(Operand), {}, true);
								break;
							}
						case EOpcode::ConstI32:
						case EOpcode::ConstF64:
						case EOpcode::ConstBool:
						case EOpcode::ConstStr:
						case EOpcode::ConstNull:
							{
								Stack.push_back({});
								break;
							}
						case EOpcode::Pop:
						case EOpcode::IncField:
							{
								const auto Value = Pop();
								if (!Release(Value)) return false;
								break;
							}
						case EOpcode::StoreThisField:
						case EOpcode::InitializeThisField:
							{
								RetainedAllocations.insert(Pop().Allocation);
								break;
							}
						case EOpcode::DerefRef:
							{
								Stack.push_back(Visible(Pop()));
								break;
							}
						case EOpcode::Return:
							{
								const auto Value = Pop();
								if (Returned)
								{
									if (!HasReturn)
									{
										*Returned = Value;
									}
									else if (Returned->Closure != Value.Closure || Returned->Type != Value.Type ||
									    Returned->Captures != Value.Captures)
									{
										*Returned = {};
									}
								}
								HasReturn = true;
								if (!ReleaseScope(Value.Allocation)) return false;
								StraightLine = false;
								break;
							}
						case EOpcode::CallMethod:
						case EOpcode::CallNative:
						case EOpcode::CallMemory:
						case EOpcode::CallBuiltin:
							{
								(void)PopArguments(Code[Ip + (Opcode == EOpcode::CallBuiltin ? 2 : 3)]);
								if (Opcode == EOpcode::CallMethod)
								{
									(void)Pop();
								}
								Stack.push_back({});
								break;
							}
						case EOpcode::Nop:
						case EOpcode::EndFinally:
						case EOpcode::CommitLocalStatic:
							{
								break;
							}
						default:
							{
								// Unknown results never borrow an earlier closure identity. Direct
								// dependencies are still visited throughout the remaining bytecode.
								Stack.clear();
								Locals.clear();
								StraightLine = false;
								break;
							}
						}
						Ip += Size;
					}
					if (!HasReturn && !ReleaseScope(Stack.empty() ? 0 : Stack.back().Allocation)) return false;
					return true;
				}

				FBytecodeModule& Module;
				const std::function<bool(const std::string&)>& PrepareOwner;
				const std::function<void()>& CheckCancelled;
				// Bindings outlive every analysis value; capture back edges are nonowning to permit recursive callables.
				std::vector<std::unique_ptr<FDependencyBinding>> BindingArena;
				std::unordered_map<std::uint16_t, FPreparedDependencyDescriptor> PreparedTypes;
				std::unordered_map<std::uint16_t, FPreparedDependencyDescriptor> PreparedCopyFields;
				std::unordered_map<std::uint16_t, FPreparedDependencyFunction> PreparedFunctions;
				std::unordered_set<std::uint16_t> ActiveFunctions;
				std::unordered_set<std::uint16_t> ActiveGlobals;
				std::unordered_map<std::uint16_t, FPreparedDependencyDescriptor> PreparedDestructors;
				std::size_t NextAllocation = 1;
				std::unordered_map<std::size_t, std::string> AllocationTypes;
				std::unordered_set<std::size_t> RetainedAllocations;
				std::unordered_map<std::uint16_t, FDependencyBinding*> GlobalBindings;
				std::size_t StateRevision = 0;
			};
		}

		bool PrepareStaticInitializationDependencies(
		    FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::function<bool(const std::string&)>& PrepareOwner,
		    const std::function<void()>& CheckCancelled)
		{
			return FStaticInitializationDependencies(Module, PrepareOwner, CheckCancelled).Prepare(Code);
		}
	}
}
