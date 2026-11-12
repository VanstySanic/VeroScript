// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNative.h"
#include "VeroScriptBytecode.h"

#include "VeroScriptRuntime.h"

namespace VeroScript
{
	FBytecodeNativeReference DescribeNativeFunction(const FNativeFunction& Native)
	{
		std::string Signature = NativeParameterSignature(Native);
		const auto Append = [&](const std::string& Value)
		{
			Signature += std::to_string(Value.size()) + ':' + Value;
		};
		Append(ResolveNativeReturnTypeName(Native));
		Append(Native.bHostStaticMethod ? "static" : "ordinary");
		Append(Native.bGenericHostObjectCast ? "generic-cast" : "function");
		Append(Native.ScriptReturnTypeName);
		for (const auto& Parameter : Native.TypeParams)
		{
			Append(Parameter);
		}
		return {Native.Name, std::move(Signature), Native.Origin.ProviderId,
		    static_cast<std::uint16_t>(NativeRuntimeArgumentCount(Native))};
	}

	namespace
	{
		struct FNativeBoundaryCloneContext
		{
			void DiscardClones()
			{
				// Successful clones may contain cycles. If a later nested clone
				// fails, break every cloned container edge before releasing the
				// memo tables so the failure path cannot leak a cyclic graph.
				for (auto& Pair : Arrays)
				{
					Pair.second->Elements.clear();
				}
				for (auto& Pair : Structs)
				{
					Pair.second->Fields.clear();
					Pair.second->SetHostBase({});
				}
				for (auto& Pair : Maps)
				{
					Pair.second->IntEntries.clear();
					Pair.second->UIntEntries.clear();
					Pair.second->Int64Entries.clear();
					Pair.second->UInt64Entries.clear();
					Pair.second->StrEntries.clear();
					Pair.second->WStrEntries.clear();
					Pair.second->EnumEntries.clear();
					Pair.second->HostEntries.clear();
				}
				for (auto& Pair : Sets)
				{
					Pair.second->IntElements.clear();
					Pair.second->UIntElements.clear();
					Pair.second->Int64Elements.clear();
					Pair.second->UInt64Elements.clear();
					Pair.second->StrElements.clear();
					Pair.second->WStrElements.clear();
					Pair.second->EnumElements.clear();
					Pair.second->HostElements.clear();
				}
				for (auto& Pair : Queues)
				{
					Pair.second->Elements.clear();
				}
				for (auto& Pair : Stacks)
				{
					Pair.second->Elements.clear();
				}
				for (auto& Pair : OrderedMaps)
				{
					Pair.second->IntEntries.clear();
					Pair.second->UIntEntries.clear();
					Pair.second->Int64Entries.clear();
					Pair.second->UInt64Entries.clear();
					Pair.second->StrEntries.clear();
					Pair.second->WStrEntries.clear();
					Pair.second->EnumEntries.clear();
					Pair.second->HostEntries.clear();
				}
				for (auto& Pair : Deques)
				{
					Pair.second->Elements.clear();
				}
			}

			static constexpr std::size_t MaxDepth = 128;
			std::size_t ActiveDepth = 0;
			std::unordered_map<const FDynamicArray*, FDynamicArrayHandle> Arrays;
			std::unordered_map<const FScriptObject*, FObjectHandle> Structs;
			std::unordered_map<const FMap*, FMapHandle> Maps;
			std::unordered_map<const FSet*, FSetHandle> Sets;
			std::unordered_map<const FQueue*, FQueueHandle> Queues;
			std::unordered_map<const FStack*, FStackHandle> Stacks;
			std::unordered_map<const FOrderedMap*, FOrderedMapHandle> OrderedMaps;
			std::unordered_map<const FDeque*, FDequeHandle> Deques;

		};

		bool DeepCloneNativeBoundaryValue(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context);

		void MaybeUnwrapHostObjectElement(FValue& ElementCopy)
		{
			if (!std::holds_alternative<FObjectHandle>(ElementCopy))
			{
				return;
			}
			std::string CoerceError;
			if (!CoerceValueToNativeType(ENativeType::HostObject, ElementCopy, CoerceError))
			{
				// Keep script object handles for non-host elements.
				(void)CoerceError;
			}
		}

		void MaterializeScalarMapEntries(FMap& Map)
		{
			std::visit(
				[&Map](const auto& Entries)
				{
					using TEntries = std::decay_t<decltype(Entries)>;
					if constexpr (!std::is_same_v<TEntries, std::monostate>)
					{
						Entries.ForEach([&Map](const auto& Key, const auto& Mapped)
						{
							using TMapped = std::decay_t<decltype(Mapped)>;
							FValue BoxedValue = [&]() -> FValue
							{
								if constexpr (std::is_same_v<
										TMapped, FMap::FPrimitiveScalar>)
								{
									return FMap::UnpackPrimitiveScalar(Mapped);
								}
								else
								{
									return FValue{Mapped};
								}
							}();
							using TKey = std::decay_t<decltype(Key)>;
							if constexpr (std::is_same_v<TKey, std::int32_t>)
							{
								Map.IntEntries.insert_or_assign(Key, std::move(BoxedValue));
							}
							else if constexpr (std::is_same_v<TKey, std::uint32_t>)
							{
								Map.UIntEntries.insert_or_assign(Key, std::move(BoxedValue));
							}
							else if constexpr (std::is_same_v<TKey, std::int64_t>)
							{
								Map.Int64Entries.insert_or_assign(Key, std::move(BoxedValue));
							}
							else if constexpr (std::is_same_v<TKey, std::uint64_t>)
							{
								Map.UInt64Entries.insert_or_assign(Key, std::move(BoxedValue));
							}
							else if constexpr (std::is_same_v<TKey, std::string>)
							{
								Map.StrEntries.insert_or_assign(Key, std::move(BoxedValue));
							}
							else if constexpr (std::is_same_v<TKey, std::u16string>)
							{
								Map.WStrEntries.insert_or_assign(Key, std::move(BoxedValue));
							}
						});
					}
				},
				Map.ScalarEntries);
			Map.ScalarEntries.emplace<std::monostate>();
		}

		void MaterializeScalarSetElements(FSet& Set)
		{
			std::visit(
				[&Set](const auto& Elements)
				{
					using TElements = std::decay_t<decltype(Elements)>;
					if constexpr (!std::is_same_v<TElements, std::monostate>)
					{
						Elements.ForEach([&Set](const auto& Element)
						{
							using TElement = std::decay_t<decltype(Element)>;
							if constexpr (std::is_same_v<TElement, std::int32_t>)
							{
								Set.IntElements.insert(Element);
							}
							else if constexpr (std::is_same_v<TElement, std::uint32_t>)
							{
								Set.UIntElements.insert(Element);
							}
							else if constexpr (std::is_same_v<TElement, std::int64_t>)
							{
								Set.Int64Elements.insert(Element);
							}
							else if constexpr (std::is_same_v<TElement, std::uint64_t>)
							{
								Set.UInt64Elements.insert(Element);
							}
							else if constexpr (std::is_same_v<TElement, std::string>)
							{
								Set.StrElements.insert(Element);
							}
							else if constexpr (std::is_same_v<TElement, std::u16string>)
							{
								Set.WStrElements.insert(Element);
							}
						});
					}
				},
				Set.ScalarElements);
			Set.ScalarElements.emplace<std::monostate>();
		}

		bool HasConflictingScalarStorage(const FDynamicArray& Array)
		{
			return Array.HasScalarElements() && !Array.Elements.empty();
		}

		bool HasConflictingScalarStorage(const FMap& Map)
		{
			return Map.ScalarEntries.index() != 0
				&& (!Map.IntEntries.empty() || !Map.UIntEntries.empty()
					|| !Map.Int64Entries.empty() || !Map.UInt64Entries.empty()
					|| !Map.StrEntries.empty() || !Map.WStrEntries.empty()
					|| !Map.EnumEntries.empty() || !Map.HostEntries.empty());
		}

		bool HasConflictingScalarStorage(const FSet& Set)
		{
			return Set.ScalarElements.index() != 0
				&& (!Set.IntElements.empty() || !Set.UIntElements.empty()
					|| !Set.Int64Elements.empty() || !Set.UInt64Elements.empty()
					|| !Set.StrElements.empty() || !Set.WStrElements.empty()
					|| !Set.EnumElements.empty() || !Set.HostElements.empty());
		}

		bool ValidateNativeBoundaryStorageRepresentations(
			const FValue& Value,
			std::string& OutError)
		{
			// Validate the complete graph before materializing anything. This keeps a
			// malformed mutable argument transactional: a nested conflict cannot leave
			// earlier containers irreversibly converted to the boxed representation.
			struct FPendingValue
			{
				const FValue* Value = nullptr;
				std::size_t FixedArrayDepth = 0;
			};
			std::unordered_set<const void*> Visited;
			std::vector<FPendingValue> Pending{{&Value, 0}};
			while (!Pending.empty())
			{
				const FPendingValue PendingValue = Pending.back();
				Pending.pop_back();
				const FValue* const Current = PendingValue.Value;
				const auto QueueValues = [&Pending](
					const auto& Values,
					const std::size_t FixedArrayDepth = 0)
				{
					for (const auto& Element : Values)
					{
						Pending.push_back({&Element, FixedArrayDepth});
					}
				};
				const auto QueueMapValues = [&Pending](const auto& Entries)
				{
					for (const auto& Entry : Entries)
					{
						Pending.push_back({&Entry.second, 0});
					}
				};

				if (const auto* const Array = std::get_if<FDynamicArrayHandle>(Current);
					Array && *Array)
				{
					if (!Visited.insert(Array->get()).second)
					{
						continue;
					}
					if (HasConflictingScalarStorage(**Array))
					{
						OutError = "array has conflicting scalar and boxed storage";
						return false;
					}
					QueueValues((*Array)->Elements);
					continue;
				}
				if (const auto* const Array = std::get_if<FNativeArray>(Current))
				{
					const std::size_t FixedArrayDepth =
						PendingValue.FixedArrayDepth + 1;
					if (FixedArrayDepth > FNativeBoundaryCloneContext::MaxDepth)
					{
						OutError = "native container graph nested too deeply";
						return false;
					}
					if (!Visited.insert(&Array->Storage()).second)
					{
						continue;
					}
					QueueValues(Array->Storage().Elements, FixedArrayDepth);
					continue;
				}
				if (const auto* const Object = std::get_if<FObjectHandle>(Current);
					Object && *Object)
				{
					if (!Visited.insert(Object->get()).second)
					{
						continue;
					}
					QueueValues((*Object)->Fields);
					continue;
				}
				if (const auto* const Map = std::get_if<FMapHandle>(Current); Map && *Map)
				{
					if (!Visited.insert(Map->get()).second)
					{
						continue;
					}
					if (HasConflictingScalarStorage(**Map))
					{
						OutError = "map has conflicting scalar and boxed storage";
						return false;
					}
					QueueMapValues((*Map)->IntEntries);
					QueueMapValues((*Map)->UIntEntries);
					QueueMapValues((*Map)->Int64Entries);
					QueueMapValues((*Map)->UInt64Entries);
					QueueMapValues((*Map)->StrEntries);
					QueueMapValues((*Map)->WStrEntries);
					QueueMapValues((*Map)->EnumEntries);
					QueueMapValues((*Map)->HostEntries);
					continue;
				}
				if (const auto* const Set = std::get_if<FSetHandle>(Current); Set && *Set)
				{
					if (!Visited.insert(Set->get()).second)
					{
						continue;
					}
					if (HasConflictingScalarStorage(**Set))
					{
						OutError = "set has conflicting scalar and boxed storage";
						return false;
					}
					continue;
				}
				if (const auto* const Map = std::get_if<FOrderedMapHandle>(Current);
					Map && *Map)
				{
					if (!Visited.insert(Map->get()).second)
					{
						continue;
					}
					QueueMapValues((*Map)->IntEntries);
					QueueMapValues((*Map)->UIntEntries);
					QueueMapValues((*Map)->Int64Entries);
					QueueMapValues((*Map)->UInt64Entries);
					QueueMapValues((*Map)->StrEntries);
					QueueMapValues((*Map)->WStrEntries);
					QueueMapValues((*Map)->EnumEntries);
					QueueMapValues((*Map)->HostEntries);
					continue;
				}
				const auto QueueSequence = [&Visited, &QueueValues](const auto* const Handle)
				{
					if (Handle && *Handle && Visited.insert(Handle->get()).second)
					{
						QueueValues((*Handle)->Elements);
					}
				};
				if (const auto* const Queue = std::get_if<FQueueHandle>(Current))
				{
					QueueSequence(Queue);
					continue;
				}
				if (const auto* const Stack = std::get_if<FStackHandle>(Current))
				{
					QueueSequence(Stack);
					continue;
				}
				if (const auto* const Deque = std::get_if<FDequeHandle>(Current))
				{
					QueueSequence(Deque);
				}
			}
			return true;
		}

		void MaterializeNativeBoundaryContainers(
			FValue& Value,
			std::unordered_set<const void*>& Visited)
		{
			// A host can construct a very deep (or cyclic) FValue graph. Walk it
			// iteratively so merely crossing a mutable native boundary cannot exhaust
			// the C++ call stack. Container storage is not structurally mutated after
			// its children are queued, so the collected FValue addresses stay stable.
			std::vector<FValue*> Pending{&Value};
			while (!Pending.empty())
			{
				FValue* const Current = Pending.back();
				Pending.pop_back();
				const auto QueueValues = [&Pending](auto& Values)
				{
					for (auto& Element : Values)
					{
						Pending.push_back(&Element);
					}
				};
				const auto QueueMapValues = [&Pending](auto& Entries)
				{
					for (auto& Entry : Entries)
					{
						Pending.push_back(&Entry.second);
					}
				};

				if (auto* const Array = std::get_if<FDynamicArrayHandle>(Current);
					Array && *Array)
				{
					if (!Visited.insert(Array->get()).second)
					{
						continue;
					}
					(*Array)->MaterializeBoxedElements();
					QueueValues((*Array)->Elements);
					continue;
				}
				if (auto* const Array = std::get_if<FNativeArray>(Current))
				{
					if (!Visited.insert(&Array->Storage()).second)
					{
						continue;
					}
					QueueValues(Array->Storage().Elements);
					continue;
				}
				if (auto* const Object = std::get_if<FObjectHandle>(Current);
					Object && *Object)
				{
					if (!Visited.insert(Object->get()).second)
					{
						continue;
					}
					QueueValues((*Object)->Fields);
					continue;
				}
				if (auto* const Map = std::get_if<FMapHandle>(Current); Map && *Map)
				{
					if (!Visited.insert(Map->get()).second)
					{
						continue;
					}
					MaterializeScalarMapEntries(**Map);
					QueueMapValues((*Map)->IntEntries);
					QueueMapValues((*Map)->UIntEntries);
					QueueMapValues((*Map)->Int64Entries);
					QueueMapValues((*Map)->UInt64Entries);
					QueueMapValues((*Map)->StrEntries);
					QueueMapValues((*Map)->WStrEntries);
					QueueMapValues((*Map)->EnumEntries);
					QueueMapValues((*Map)->HostEntries);
					continue;
				}
				if (auto* const Set = std::get_if<FSetHandle>(Current); Set && *Set)
				{
					if (!Visited.insert(Set->get()).second)
					{
						continue;
					}
					MaterializeScalarSetElements(**Set);
					continue;
				}
				if (auto* const Map = std::get_if<FOrderedMapHandle>(Current);
					Map && *Map)
				{
					if (!Visited.insert(Map->get()).second)
					{
						continue;
					}
					QueueMapValues((*Map)->IntEntries);
					QueueMapValues((*Map)->UIntEntries);
					QueueMapValues((*Map)->Int64Entries);
					QueueMapValues((*Map)->UInt64Entries);
					QueueMapValues((*Map)->StrEntries);
					QueueMapValues((*Map)->WStrEntries);
					QueueMapValues((*Map)->EnumEntries);
					QueueMapValues((*Map)->HostEntries);
					continue;
				}
				const auto QueueSequence = [&Visited, &QueueValues](auto* const Handle)
				{
					if (Handle && *Handle && Visited.insert(Handle->get()).second)
					{
						QueueValues((*Handle)->Elements);
					}
				};
				if (auto* const Queue = std::get_if<FQueueHandle>(Current))
				{
					QueueSequence(Queue);
					continue;
				}
				if (auto* const Stack = std::get_if<FStackHandle>(Current))
				{
					QueueSequence(Stack);
					continue;
				}
				if (auto* const Deque = std::get_if<FDequeHandle>(Current))
				{
					QueueSequence(Deque);
				}
			}
		}

		void MaterializeNativeBoundaryContainers(FValue& Value)
		{
			std::unordered_set<const void*> Visited;
			MaterializeNativeBoundaryContainers(Value, Visited);
		}

		bool PrepareMutableNativeBoundaryContainers(
			FValue& Value,
			std::string& OutError)
		{
			if (!ValidateNativeBoundaryStorageRepresentations(Value, OutError))
			{
				return false;
			}
			MaterializeNativeBoundaryContainers(Value);
			return true;
		}

		bool CloneDynamicArrayForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Handle = std::get_if<FDynamicArrayHandle>(&Value);
			if (!Handle || !*Handle)
			{
				OutError = "expected array";
				return false;
			}
			const FDynamicArrayHandle StableHandle = *Handle;
			const FDynamicArray& Source = *StableHandle;
			if (HasConflictingScalarStorage(Source))
			{
				OutError = "array has conflicting scalar and boxed storage";
				return false;
			}
			if (const auto Existing = Context.Arrays.find(&Source);
				Existing != Context.Arrays.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<FDynamicArray>();
			Context.Arrays.emplace(&Source, Copy);
			Copy->TypeName = Source.TypeName;
			Copy->ElementTypeName = Source.ElementTypeName;
			// The native ABI intentionally exposes the stable boxed representation.
			// Keep primitive arrays compact inside the VM, but materialize only the
			// boundary clone so existing and manually supplied host callbacks see the
			// same FDynamicArray::Elements contract as non-primitive arrays.
			if (Source.HasScalarElements())
			{
				Copy->ScalarElements = Source.ScalarElements;
				Copy->MaterializeBoxedElements();
			}
			const std::vector<FValue> SourceElements = Source.HasScalarElements()
				? Copy->Elements
				: Source.Elements;
			std::vector<FValue> BoxedElements;
			BoxedElements.reserve(SourceElements.size());
			for (const FValue& Element : SourceElements)
			{
				FValue ElementCopy;
				if (!DeepCloneNativeBoundaryValue(Element, ElementCopy, OutError, Context))
				{
					return false;
				}
				MaybeUnwrapHostObjectElement(ElementCopy);
				BoxedElements.push_back(std::move(ElementCopy));
			}
			Copy->Elements = std::move(BoxedElements);
			Out = std::move(Copy);
			return true;
		}

		bool CloneNativeArrayForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Source = std::get_if<FNativeArray>(&Value);
			if (!Source)
			{
				OutError = "expected fixed array";
				return false;
			}
			FNativeArray Copy;
			Copy.TypeName() = Source->TypeName();
			Copy.ElementTypeName() = Source->ElementTypeName();
			Copy.Length() = Source->Length();
			const std::vector<FValue> SourceElements = Source->Storage().Elements;
			Copy.Storage().Elements.reserve(SourceElements.size());
			for (const FValue& Element : SourceElements)
			{
				FValue ElementCopy;
				if (!DeepCloneNativeBoundaryValue(Element, ElementCopy, OutError, Context))
				{
					return false;
				}
				MaybeUnwrapHostObjectElement(ElementCopy);
				Copy.Storage().Elements.push_back(std::move(ElementCopy));
			}
			Out = std::move(Copy);
			return true;
		}

		bool CloneScriptStructForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Handle = std::get_if<FObjectHandle>(&Value);
			if (!Handle || !*Handle || (*Handle)->Kind != EScriptObjectKind::Struct)
			{
				OutError = "expected script struct";
				return false;
			}
			const FObjectHandle StableHandle = *Handle;
			const FScriptObject& Source = *StableHandle;
			if (const auto Existing = Context.Structs.find(&Source);
				Existing != Context.Structs.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<FScriptObject>();
			Context.Structs.emplace(&Source, Copy);
			Copy->Kind = Source.Kind;
			Copy->SetTypeName(Source.GetTypeName());
			Copy->RuntimeTypeGeneration = Source.RuntimeTypeGeneration;
			Copy->ProgramOwner = Source.ProgramOwner;
			Copy->DescriptorData = Source.DescriptorData;
			Copy->RuntimeTypeModule = Source.RuntimeTypeModule;
			Copy->RuntimeTypeIndex = Source.RuntimeTypeIndex;
			const std::vector<FValue> SourceFields = Source.Fields;
			const FHostObjectHandle SourceHostBase = Source.GetHostBase();
			Copy->Fields.reserve(SourceFields.size());
			for (const FValue& Field : SourceFields)
			{
				FValue FieldCopy;
				if (!DeepCloneNativeBoundaryValue(Field, FieldCopy, OutError, Context))
				{
					return false;
				}
				Copy->Fields.push_back(std::move(FieldCopy));
			}
			if (SourceHostBase)
			{
				if (!SourceHostBase->CloneCallback)
				{
					OutError = "failed to copy script struct host base";
					return false;
				}
				FValue HostCopyValue;
				if (!DeepCloneNativeBoundaryValue(
						FValue(SourceHostBase), HostCopyValue, OutError, Context))
				{
					return false;
				}
				const auto* const HostCopy =
					std::get_if<FHostObjectHandle>(&HostCopyValue);
				if (!HostCopy || !*HostCopy)
				{
					OutError = "failed to copy script struct host base";
					return false;
				}
				Copy->SetHostBase(*HostCopy);
				Copy->GetHostBase()->ScriptOwner = Copy;
			}
			Out = std::move(Copy);
			return true;
		}

		template <typename TMap>
		bool CloneMapEntries(
			const TMap& SourceEntries,
			TMap& DestEntries,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			using TKey = typename TMap::key_type;
			std::vector<std::pair<TKey, FValue>> StableEntries;
			StableEntries.reserve(SourceEntries.size());
			for (const auto& Pair : SourceEntries)
			{
				StableEntries.emplace_back(Pair.first, Pair.second);
			}
			for (const auto& Pair : StableEntries)
			{
				FValue ValueCopy;
				if (!DeepCloneNativeBoundaryValue(Pair.second, ValueCopy, OutError, Context))
				{
					return false;
				}
				MaybeUnwrapHostObjectElement(ValueCopy);
				DestEntries.emplace(Pair.first, std::move(ValueCopy));
			}
			return true;
		}

		bool CloneHostMapEntries(
			const decltype(FMap::HostEntries)& SourceEntries,
			decltype(FMap::HostEntries)& DestEntries,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			std::vector<std::pair<FHostObjectHandle, FValue>> StableEntries;
			StableEntries.reserve(SourceEntries.size());
			for (const auto& Pair : SourceEntries)
			{
				StableEntries.emplace_back(Pair.first, Pair.second);
			}
			for (const auto& Pair : StableEntries)
			{
				FValue KeyCopyValue;
				FValue ValueCopy;
				if (!DeepCloneNativeBoundaryValue(
						FValue(Pair.first), KeyCopyValue, OutError, Context)
					|| !DeepCloneNativeBoundaryValue(
						Pair.second, ValueCopy, OutError, Context))
				{
					return false;
				}
				const auto* KeyCopy = std::get_if<FHostObjectHandle>(&KeyCopyValue);
				if (!KeyCopy || !*KeyCopy)
				{
					OutError = "failed to copy host map key";
					return false;
				}
				MaybeUnwrapHostObjectElement(ValueCopy);
				DestEntries.emplace(*KeyCopy, std::move(ValueCopy));
			}
			return true;
		}

		bool CloneDynamicMapForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Handle = std::get_if<FMapHandle>(&Value);
			if (!Handle || !*Handle)
			{
				OutError = "expected map";
				return false;
			}
			const FMapHandle StableHandle = *Handle;
			const FMap& Source = *StableHandle;
			if (HasConflictingScalarStorage(Source))
			{
				OutError = "map has conflicting scalar and boxed storage";
				return false;
			}
			if (const auto Existing = Context.Maps.find(&Source);
				Existing != Context.Maps.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<FMap>();
			Context.Maps.emplace(&Source, Copy);
			Copy->TypeName = Source.TypeName;
			Copy->KeyTypeName = Source.KeyTypeName;
			Copy->ValueTypeName = Source.ValueTypeName;
			// Scalar map storage is a VM/JIT implementation detail. Native callbacks
			// always receive the stable boxed representation, including when a host
			// returns a manually constructed value that uses the public storage type.
			Copy->ScalarEntries = Source.ScalarEntries;
			MaterializeScalarMapEntries(*Copy);
			if (!CloneMapEntries(Source.IntEntries, Copy->IntEntries, OutError, Context)
				|| !CloneMapEntries(Source.UIntEntries, Copy->UIntEntries, OutError, Context)
				|| !CloneMapEntries(
					Source.Int64Entries, Copy->Int64Entries, OutError, Context)
				|| !CloneMapEntries(
					Source.UInt64Entries, Copy->UInt64Entries, OutError, Context)
				|| !CloneMapEntries(Source.StrEntries, Copy->StrEntries, OutError, Context)
				|| !CloneMapEntries(Source.WStrEntries, Copy->WStrEntries, OutError, Context)
				|| !CloneMapEntries(Source.EnumEntries, Copy->EnumEntries, OutError, Context)
				|| !CloneHostMapEntries(
					Source.HostEntries, Copy->HostEntries, OutError, Context))
			{
				return false;
			}
			Out = std::move(Copy);
			return true;
		}

		bool CloneDynamicSetForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Handle = std::get_if<FSetHandle>(&Value);
			if (!Handle || !*Handle)
			{
				OutError = "expected set";
				return false;
			}
			const FSetHandle StableHandle = *Handle;
			const FSet& Source = *StableHandle;
			if (HasConflictingScalarStorage(Source))
			{
				OutError = "set has conflicting scalar and boxed storage";
				return false;
			}
			if (const auto Existing = Context.Sets.find(&Source);
				Existing != Context.Sets.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<FSet>();
			Context.Sets.emplace(&Source, Copy);
			Copy->TypeName = Source.TypeName;
			Copy->ElementTypeName = Source.ElementTypeName;
			Copy->IntElements = Source.IntElements;
			Copy->UIntElements = Source.UIntElements;
			Copy->Int64Elements = Source.Int64Elements;
			Copy->UInt64Elements = Source.UInt64Elements;
			Copy->StrElements = Source.StrElements;
			Copy->WStrElements = Source.WStrElements;
			Copy->EnumElements = Source.EnumElements;
			Copy->ScalarElements = Source.ScalarElements;
			MaterializeScalarSetElements(*Copy);
			const std::vector<FHostObjectHandle> StableHostElements(
				Source.HostElements.begin(), Source.HostElements.end());
			for (const FHostObjectHandle& Element : StableHostElements)
			{
				FValue ElementCopyValue;
				if (!DeepCloneNativeBoundaryValue(
						FValue(Element), ElementCopyValue, OutError, Context))
				{
					return false;
				}
				const auto* ElementCopy =
					std::get_if<FHostObjectHandle>(&ElementCopyValue);
				if (!ElementCopy || !*ElementCopy)
				{
					OutError = "failed to copy host set element";
					return false;
				}
				Copy->HostElements.insert(*ElementCopy);
			}
			Out = std::move(Copy);
			return true;
		}

		template <typename THandle, typename TObject, typename TMap>
		bool CloneSequenceForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context,
			TMap& Memo,
			const char* const Expected)
		{
			const auto* const Handle = std::get_if<THandle>(&Value);
			if (!Handle || !*Handle)
			{
				OutError = std::string("expected ") + Expected;
				return false;
			}
			const THandle StableHandle = *Handle;
			const TObject& Source = *StableHandle;
			if (const auto Existing = Memo.find(&Source); Existing != Memo.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<TObject>();
			Memo.emplace(&Source, Copy);
			Copy->TypeName = Source.TypeName;
			Copy->ElementTypeName = Source.ElementTypeName;
			const std::vector<FValue> StableElements(
				Source.Elements.begin(), Source.Elements.end());
			for (const FValue& Element : StableElements)
			{
				FValue ElementCopy;
				if (!DeepCloneNativeBoundaryValue(Element, ElementCopy, OutError, Context))
				{
					return false;
				}
				MaybeUnwrapHostObjectElement(ElementCopy);
				Copy->Elements.push_back(std::move(ElementCopy));
			}
			Out = std::move(Copy);
			return true;
		}

		bool CloneOrderedMapForNativeBoundary(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			const auto* const Handle = std::get_if<FOrderedMapHandle>(&Value);
			if (!Handle || !*Handle)
			{
				OutError = "expected ordered_map";
				return false;
			}
			const FOrderedMapHandle StableHandle = *Handle;
			const FOrderedMap& Source = *StableHandle;
			if (const auto Existing = Context.OrderedMaps.find(&Source);
				Existing != Context.OrderedMaps.end())
			{
				Out = Existing->second;
				return true;
			}
			auto Copy = std::make_shared<FOrderedMap>();
			Context.OrderedMaps.emplace(&Source, Copy);
			Copy->TypeName = Source.TypeName;
			Copy->KeyTypeName = Source.KeyTypeName;
			Copy->ValueTypeName = Source.ValueTypeName;
			Copy->IntKeyOrder = Source.IntKeyOrder;
			Copy->UIntKeyOrder = Source.UIntKeyOrder;
			Copy->Int64KeyOrder = Source.Int64KeyOrder;
			Copy->UInt64KeyOrder = Source.UInt64KeyOrder;
			Copy->StrKeyOrder = Source.StrKeyOrder;
			Copy->WStrKeyOrder = Source.WStrKeyOrder;
			Copy->EnumKeyOrder = Source.EnumKeyOrder;
			const std::vector<FHostObjectHandle> StableHostKeyOrder =
				Source.HostKeyOrder;
			if (!CloneMapEntries(Source.IntEntries, Copy->IntEntries, OutError, Context)
				|| !CloneMapEntries(Source.UIntEntries, Copy->UIntEntries, OutError, Context)
				|| !CloneMapEntries(Source.Int64Entries, Copy->Int64Entries, OutError, Context)
				|| !CloneMapEntries(Source.UInt64Entries, Copy->UInt64Entries, OutError, Context)
				|| !CloneMapEntries(Source.StrEntries, Copy->StrEntries, OutError, Context)
				|| !CloneMapEntries(Source.WStrEntries, Copy->WStrEntries, OutError, Context)
				|| !CloneMapEntries(Source.EnumEntries, Copy->EnumEntries, OutError, Context)
				|| !CloneHostMapEntries(
					Source.HostEntries, Copy->HostEntries, OutError, Context))
			{
				return false;
			}
			for (const FHostObjectHandle& Key : StableHostKeyOrder)
			{
				const auto It = Copy->HostEntries.find(Key);
				if (It == Copy->HostEntries.end())
				{
					OutError = "failed to restore host ordered_map key order";
					return false;
				}
				Copy->HostKeyOrder.push_back(It->first);
			}
			Out = std::move(Copy);
			return true;
		}

		bool DeepCloneNativeBoundaryValue(
			const FValue& Value,
			FValue& Out,
			std::string& OutError,
			FNativeBoundaryCloneContext& Context)
		{
			if (Context.ActiveDepth >= FNativeBoundaryCloneContext::MaxDepth)
			{
				OutError = "native container graph nested too deeply";
				return false;
			}
			struct FDepthGuard
			{
				explicit FDepthGuard(std::size_t& InDepth) : Depth(InDepth)
				{
					++Depth;
				}
				~FDepthGuard()
				{
					--Depth;
				}
				std::size_t& Depth;
			} DepthGuard(Context.ActiveDepth);
			if (std::holds_alternative<FDynamicArrayHandle>(Value))
			{
				return CloneDynamicArrayForNativeBoundary(Value, Out, OutError, Context);
			}
			if (std::holds_alternative<FNativeArray>(Value))
			{
				return CloneNativeArrayForNativeBoundary(Value, Out, OutError, Context);
			}
			if (const auto* const Object = std::get_if<FObjectHandle>(&Value);
				Object && *Object && (*Object)->Kind == EScriptObjectKind::Struct)
			{
				return CloneScriptStructForNativeBoundary(Value, Out, OutError, Context);
			}
			if (std::holds_alternative<FMapHandle>(Value))
			{
				return CloneDynamicMapForNativeBoundary(Value, Out, OutError, Context);
			}
			if (std::holds_alternative<FSetHandle>(Value))
			{
				return CloneDynamicSetForNativeBoundary(Value, Out, OutError, Context);
			}
			if (std::holds_alternative<FQueueHandle>(Value))
			{
				return CloneSequenceForNativeBoundary<FQueueHandle, FQueue>(
					Value, Out, OutError, Context, Context.Queues, "queue");
			}
			if (std::holds_alternative<FStackHandle>(Value))
			{
				return CloneSequenceForNativeBoundary<FStackHandle, FStack>(
					Value, Out, OutError, Context, Context.Stacks, "stack");
			}
			if (std::holds_alternative<FOrderedMapHandle>(Value))
			{
				return CloneOrderedMapForNativeBoundary(Value, Out, OutError, Context);
			}
			if (std::holds_alternative<FDequeHandle>(Value))
			{
				return CloneSequenceForNativeBoundary<FDequeHandle, FDeque>(
					Value, Out, OutError, Context, Context.Deques, "deque");
			}
			if (!Runtime::TryCloneValue(Value, Out))
			{
				OutError = "failed to copy value";
				return false;
			}
			return true;
		}
	}

	bool NativeTypeMatchesValue(const ENativeType Type, const FValue& Value)
	{
		switch (Type)
		{
		case ENativeType::Variant:
			return std::holds_alternative<FScriptVariantHandle>(Value);
		case ENativeType::Void:
			return false;
		case ENativeType::Int:
			return std::holds_alternative<std::int32_t>(Value);
		case ENativeType::Bool:
			return std::holds_alternative<bool>(Value);
		case ENativeType::Double:
			return std::holds_alternative<double>(Value);
		case ENativeType::Float:
			return std::holds_alternative<float>(Value);
		case ENativeType::Int8:
			return std::holds_alternative<std::int8_t>(Value);
		case ENativeType::UInt8:
			return std::holds_alternative<std::uint8_t>(Value);
		case ENativeType::Int16:
			return std::holds_alternative<std::int16_t>(Value);
		case ENativeType::UInt16:
			return std::holds_alternative<std::uint16_t>(Value);
		case ENativeType::String:
			return std::holds_alternative<std::string>(Value);
		case ENativeType::Unsigned:
			return std::holds_alternative<std::uint32_t>(Value);
		case ENativeType::Int64:
			return std::holds_alternative<std::int64_t>(Value);
		case ENativeType::UInt64:
			return std::holds_alternative<std::uint64_t>(Value);
		case ENativeType::Enum:
			return std::holds_alternative<FEnumValue>(Value);
		case ENativeType::Function:
			return std::holds_alternative<FFunctionHandle>(Value)
				&& static_cast<bool>(std::get<FFunctionHandle>(Value));
		case ENativeType::HostObject:
			{
				if (std::holds_alternative<FHostObjectHandle>(Value))
				{
					return true;
				}
				if (const auto* Object = std::get_if<FObjectHandle>(&Value);
					Object && (!*Object || (*Object)->GetHostBase()))
				{
					return true;
				}
				return false;
			}
		case ENativeType::Array:
			return std::holds_alternative<FDynamicArrayHandle>(Value)
				&& static_cast<bool>(std::get<FDynamicArrayHandle>(Value));
		case ENativeType::Map:
		case ENativeType::MultiMap:
		case ENativeType::MultiSet:
			return std::holds_alternative<FMapHandle>(Value)
				&& static_cast<bool>(std::get<FMapHandle>(Value));
		case ENativeType::Set:
			return std::holds_alternative<FSetHandle>(Value)
				&& static_cast<bool>(std::get<FSetHandle>(Value));
		case ENativeType::Queue:
		case ENativeType::PriorityQueue:
			return std::holds_alternative<FQueueHandle>(Value)
				&& static_cast<bool>(std::get<FQueueHandle>(Value));
		case ENativeType::Stack:
			return std::holds_alternative<FStackHandle>(Value)
				&& static_cast<bool>(std::get<FStackHandle>(Value));
		case ENativeType::OrderedMap:
		case ENativeType::OrderedMultiMap:
		case ENativeType::OrderedMultiSet:
			return std::holds_alternative<FOrderedMapHandle>(Value)
				&& static_cast<bool>(std::get<FOrderedMapHandle>(Value));
		case ENativeType::Deque:
			return std::holds_alternative<FDequeHandle>(Value)
				&& static_cast<bool>(std::get<FDequeHandle>(Value));
		case ENativeType::Channel:
			return std::holds_alternative<FChannelHandle>(Value)
				&& static_cast<bool>(std::get<FChannelHandle>(Value));
		default:
			return false;
		}
	}

	bool NativeTypeMatchesTypeName(const ENativeType Type, const std::string_view TypeName)
	{
		const std::string CanonicalTypeName =
			CanonicalizeNativeElementTypeName(TypeName);
		const std::string_view Canonical = CanonicalTypeName;
		switch (Type)
		{
		case ENativeType::Variant:
			return Canonical == "variant";
		case ENativeType::Void:
			return Canonical == "void";
		case ENativeType::Int:
			return Canonical == "int";
		case ENativeType::Bool:
			return Canonical == "bool";
		case ENativeType::Double:
			return Canonical == "double";
		case ENativeType::Float:
			return Canonical == "float";
		case ENativeType::Int8:
			return Canonical == "char";
		case ENativeType::UInt8:
			return Canonical == "unsigned char";
		case ENativeType::Int16:
			return Canonical == "short";
		case ENativeType::UInt16:
			return Canonical == "unsigned short";
		case ENativeType::String:
			return Canonical == "string";
		case ENativeType::Unsigned:
			return Canonical == "unsigned int";
		case ENativeType::Int64:
			return Canonical == "int64" || Canonical == "long";
		case ENativeType::UInt64:
			return Canonical == "uint64" || Canonical == "unsigned long";
		case ENativeType::Enum:
			return Canonical != "void" && !Canonical.empty();
		case ENativeType::Function:
			return Runtime::IsFunctionTypeName(Canonical);
		case ENativeType::HostObject:
			return Canonical != "void" && !Canonical.empty()
				&& Canonical != "variant"
				&& !Runtime::IsFunctionTypeName(Canonical)
				&& !Runtime::IsDynamicArrayTypeName(Canonical)
				&& !Runtime::IsMapTypeName(Canonical)
				&& !Runtime::IsSetTypeName(Canonical)
				&& !Runtime::IsMultiSetTypeName(Canonical)
				&& !Runtime::IsOrderedMultiSetTypeName(Canonical)
				&& !Runtime::IsQueueTypeName(Canonical)
				&& !Runtime::IsStackTypeName(Canonical)
				&& !Runtime::IsOrderedMapTypeName(Canonical)
				&& !Runtime::IsDequeTypeName(Canonical)
				&& !Runtime::IsChannelTypeName(Canonical);
		case ENativeType::Array:
			return Runtime::IsDynamicArrayTypeName(Canonical);
		case ENativeType::Map:
			return Runtime::IsMapTypeName(Canonical)
				&& !Runtime::IsMultiMapTypeName(Canonical);
		case ENativeType::MultiMap:
			return Runtime::IsMultiMapTypeName(Canonical);
		case ENativeType::Set:
			return Runtime::IsSetTypeName(Canonical);
		case ENativeType::MultiSet:
			return Runtime::IsMultiSetTypeName(Canonical);
		case ENativeType::OrderedMultiSet:
			return Runtime::IsOrderedMultiSetTypeName(Canonical);
		case ENativeType::Queue:
			return Runtime::IsQueueTypeName(Canonical)
				&& !Runtime::IsPriorityQueueTypeName(Canonical);
		case ENativeType::PriorityQueue:
			return Runtime::IsPriorityQueueTypeName(Canonical);
		case ENativeType::Stack:
			return Runtime::IsStackTypeName(Canonical);
		case ENativeType::OrderedMap:
			return Runtime::IsOrderedMapTypeName(Canonical)
				&& !Runtime::IsOrderedMultiMapTypeName(Canonical);
		case ENativeType::OrderedMultiMap:
			return Runtime::IsOrderedMultiMapTypeName(Canonical);
		case ENativeType::Deque:
			return Runtime::IsDequeTypeName(Canonical);
		case ENativeType::Channel:
			return Runtime::IsChannelTypeName(Canonical);
		default:
			return false;
		}
	}

	bool CoerceValueToNativeType(
		const ENativeType Type,
		FValue& Value,
		std::string& OutError,
		const bool bDeepCopyContainers)
	{
		const char* PrimitiveTypeName = nullptr;
		switch (Type)
		{
		case ENativeType::Bool:
			{
				PrimitiveTypeName = "bool";
				break;
			}
		case ENativeType::Int8:
			{
				PrimitiveTypeName = "char";
				break;
			}
		case ENativeType::UInt8:
			{
				PrimitiveTypeName = "unsigned char";
				break;
			}
		case ENativeType::Int16:
			{
				PrimitiveTypeName = "short";
				break;
			}
		case ENativeType::UInt16:
			{
				PrimitiveTypeName = "unsigned short";
				break;
			}
		case ENativeType::Int:
			{
				PrimitiveTypeName = "int";
				break;
			}
		case ENativeType::Unsigned:
			{
				PrimitiveTypeName = "unsigned int";
				break;
			}
		case ENativeType::Int64:
			{
				PrimitiveTypeName = "int64";
				break;
			}
		case ENativeType::UInt64:
			{
				PrimitiveTypeName = "uint64";
				break;
			}
		case ENativeType::Float:
			{
				PrimitiveTypeName = "float";
				break;
			}
		case ENativeType::Double:
			{
				PrimitiveTypeName = "double";
				break;
			}
		case ENativeType::String:
			{
				PrimitiveTypeName = "string";
				break;
			}
		default:
			{
				break;
			}
		}
		if (PrimitiveTypeName != nullptr)
		{
			return Runtime::CoerceValueToPrimitiveTypeName(
				PrimitiveTypeName, Value, OutError);
		}
		switch (Type)
		{
		case ENativeType::Void:
			{
				OutError = "cannot coerce value to void";
				return false;
			}
		case ENativeType::Enum:
		case ENativeType::Function:
		case ENativeType::Variant:
			{
				if (!NativeTypeMatchesValue(Type, Value))
				{
					OutError = "native value type mismatch";
					return false;
				}
				return true;
			}
		case ENativeType::HostObject:
			{
				if (std::holds_alternative<FHostObjectHandle>(Value))
				{
					return true;
				}
				if (const auto* Object = std::get_if<FObjectHandle>(&Value);
					Object && !*Object)
				{
					// nullptr is a valid value for every host reference type. Normalize it
					// to the host-object ABI so ordinary natives and generic casts agree.
					Value = FHostObjectHandle{};
					return true;
				}
				if (const auto* Object = std::get_if<FObjectHandle>(&Value);
					Object && *Object && (*Object)->GetHostBase())
				{
					// Replacing Value may release the last script-object owner of HostBase.
					FHostObjectHandle HostBase = (*Object)->GetHostBase();
					Value = std::move(HostBase);
					return true;
				}
				OutError = "expected host object";
				return false;
			}
		case ENativeType::Array:
			{
				if (!bDeepCopyContainers)
				{
					if (!NativeTypeMatchesValue(Type, Value))
					{
						OutError = "expected array";
						return false;
					}
					// Mutable native references share the actual array handle. The public
					// native ABI is the boxed Elements representation, so expose it before
					// the callback. This is a one-way representation transition for that
					// shared handle: host code may retain it, and silently re-compacting it
					// after the call would invalidate the host-visible contract.
					return PrepareMutableNativeBoundaryContainers(Value, OutError);
				}
				{
					if (!ValidateNativeBoundaryStorageRepresentations(Value, OutError))
					{
						return false;
					}
					FNativeBoundaryCloneContext Context;
					const bool bSuccess =
					CloneDynamicArrayForNativeBoundary(Value, Value, OutError, Context);
					if (!bSuccess)
					{
						Context.DiscardClones();
					}
					return bSuccess;
				}
			}
		case ENativeType::Map:
		case ENativeType::MultiMap:
		case ENativeType::MultiSet:
			{
				if (!bDeepCopyContainers)
				{
					if (!NativeTypeMatchesValue(Type, Value))
					{
						OutError = "expected map";
						return false;
					}
					return PrepareMutableNativeBoundaryContainers(Value, OutError);
				}
				{
					if (!ValidateNativeBoundaryStorageRepresentations(Value, OutError))
					{
						return false;
					}
					FNativeBoundaryCloneContext Context;
					const bool bSuccess =
					CloneDynamicMapForNativeBoundary(Value, Value, OutError, Context);
					if (!bSuccess)
					{
						Context.DiscardClones();
					}
					return bSuccess;
				}
			}
		case ENativeType::Set:
			{
				if (!bDeepCopyContainers)
				{
					if (!NativeTypeMatchesValue(Type, Value))
					{
						OutError = "expected set";
						return false;
					}
					return PrepareMutableNativeBoundaryContainers(Value, OutError);
				}
				{
					if (!ValidateNativeBoundaryStorageRepresentations(Value, OutError))
					{
						return false;
					}
					FNativeBoundaryCloneContext Context;
					const bool bSuccess =
					CloneDynamicSetForNativeBoundary(Value, Value, OutError, Context);
					if (!bSuccess)
					{
						Context.DiscardClones();
					}
					return bSuccess;
				}
			}
		case ENativeType::Queue:
		case ENativeType::PriorityQueue:
		case ENativeType::Stack:
		case ENativeType::OrderedMap:
		case ENativeType::OrderedMultiMap:
		case ENativeType::OrderedMultiSet:
		case ENativeType::Deque:
			{
				if (!bDeepCopyContainers)
				{
					if (!NativeTypeMatchesValue(Type, Value))
					{
						OutError = std::string("expected ") + NativeTypeName(Type);
						return false;
					}
					return PrepareMutableNativeBoundaryContainers(Value, OutError);
				}
				{
					if (!ValidateNativeBoundaryStorageRepresentations(Value, OutError))
					{
						return false;
					}
					FNativeBoundaryCloneContext Context;
					bool bSuccess = false;
					switch (Type)
					{
					case ENativeType::Queue:
					case ENativeType::PriorityQueue:
						{
							bSuccess = CloneSequenceForNativeBoundary<FQueueHandle, FQueue>(
							Value, Value, OutError, Context, Context.Queues,
							NativeTypeName(Type));
							break;
						}
					case ENativeType::Stack:
						{
							bSuccess = CloneSequenceForNativeBoundary<FStackHandle, FStack>(
							Value, Value, OutError, Context, Context.Stacks, "stack");
							break;
						}
					case ENativeType::OrderedMap:
					case ENativeType::OrderedMultiMap:
					case ENativeType::OrderedMultiSet:
						{
							bSuccess =
							CloneOrderedMapForNativeBoundary(Value, Value, OutError, Context);
							break;
						}
					case ENativeType::Deque:
						{
							bSuccess = CloneSequenceForNativeBoundary<FDequeHandle, FDeque>(
							Value, Value, OutError, Context, Context.Deques, "deque");
							break;
						}
					default:
						{
							break;
						}
					}
					if (!bSuccess)
					{
						Context.DiscardClones();
					}
					return bSuccess;
				}
			}
		case ENativeType::Channel:
			// Channels are synchronization identities and cannot be meaningfully
			// deep-copied. By-value and const-ref both share the same channel.
			{
				if (!NativeTypeMatchesValue(Type, Value))
				{
					OutError = "expected channel";
					return false;
				}
				return true;
			}
		default:
			{
				OutError = "unknown native type";
				return false;
			}
		}
	}

	FHostObjectHandle MakeHostObject(
		const std::uint32_t TypeId,
		std::string TypeName,
		void* const Payload,
		void (*const Destroy)(void*),
		bool (*const IsType)(
			void* Payload,
			void* UserData,
			std::string_view ExpectedTypeName),
		void* const IsTypeUserData)
	{
		auto* const Object = new FHostObject{};
		Object->TypeId = TypeId;
		Object->TypeName = std::move(TypeName);
		Object->Payload = Payload;
		Object->IsTypeCallback = IsType;
		Object->IsTypeUserData = IsTypeUserData;
		return FHostObjectHandle(
			Object,
			[Destroy](FHostObject* Host)
			{
				if (!Host)
				{
					return;
				}
				if (Destroy && Host->Payload)
				{
					Destroy(Host->Payload);
				}
				delete Host;
			});
	}
}
