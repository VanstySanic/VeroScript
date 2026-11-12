// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		//~ Begin IJitValueServices Interface
		bool ResolveObjectLayout(const FScriptObject& Object, std::uint16_t& OutTypeIndex) noexcept override
		{
			OutTypeIndex = 0xFFFF;
			return Guard([&]()
			{
				if (!Vm.ActiveModule) return true;
				OutTypeIndex = Vm.ResolveObjectTypeIndex(Object);
				return true;
			});
		}

		bool BorrowObjectField(FScriptObject& Object, const std::uint16_t NameIndex,
			std::uint16_t& OutFieldIndex, FValue*& OutStorage) noexcept override
		{
			OutFieldIndex = 0xFFFF;
			OutStorage = nullptr;
			return Guard([&]()
			{
				if (!Vm.ActiveModule || NameIndex >= Vm.ActiveModule->StringPool.size()) return true;
				const auto TypeIndex = Vm.ResolveObjectTypeIndex(Object);
				if (TypeIndex == 0xFFFF) return true;
				if (FieldLookupModule != Vm.ActiveModule || FieldLookupGeneration != Vm.ProgramGeneration
					|| FieldLookupTypeIndex != TypeIndex || FieldLookupNameIndex != NameIndex)
				{
					FieldLookupIndex = Vm.FindJitFieldIndex(TypeIndex, NameIndex);
					FieldLookupModule = Vm.ActiveModule;
					FieldLookupGeneration = Vm.ProgramGeneration;
					FieldLookupTypeIndex = TypeIndex;
					FieldLookupNameIndex = NameIndex;
				}
				const auto FieldIndex = FieldLookupIndex;
				if (FieldIndex == 0xFFFF || FieldIndex >= Object.Fields.size()) return true;
				OutStorage = UnwrapClosureReferenceCell(&Object.Fields[FieldIndex]);
				if (OutStorage)
				{
					OutFieldIndex = FieldIndex;
				}
				return true;
			});
		}

		bool CreateObject(const std::uint16_t TypeIndex, FValue& OutObject,
			const bool bUninitialized) noexcept override
		{
			return ProduceValue(OutObject, [&](FValue &Value)
								{
									return Vm.JitNewObject(TypeIndex, Value, bUninitialized);
								});
		}

		bool CreateContainer(const EOpcode Operation, const std::uint16_t TypeNameIndex,
			FValue& OutContainer) noexcept override
		{
			return ProduceValue(OutContainer, [&](FValue& Value)
			{
				if (Operation == EOpcode::NewDynamicArray) return Vm.JitNewDynamicArray(TypeNameIndex, Value);
				if (Operation == EOpcode::NewMap) return Vm.JitNewMap(TypeNameIndex, Value);
				return Vm.JitNewContainer(Operation, TypeNameIndex, Value);
			});
		}

		bool CloneStruct(FValue& Value) noexcept override
		{
			return ProduceValue(Value, [&](FValue& Copy)
			{
				Copy = Value;
				return Vm.JitCloneStruct(Copy);
			});
		}

		bool LoadField(const FValue& Object, const std::uint16_t NameIndex, FValue& OutValue) noexcept override
		{
			return ProduceValue(OutValue, [&](FValue &Value)
								{
									return Vm.JitLoadField(Object, NameIndex, Value);
								});
		}

		bool StoreField(const FValue& Object, const std::uint16_t NameIndex, const FValue& Value) noexcept override
		{
			return Guard([&]()
						 {
							 return Vm.JitStoreField(Object, NameIndex, Value);
						 });
		}

		bool InitializeField(const FValue& Object, const std::uint16_t NameIndex, const FValue& Value) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitStoreField(Object, NameIndex, Value, false);
			});
		}

		bool IncrementField(const FValue& Object, const std::uint16_t NameIndex) noexcept override
		{
			return Guard([&]()
						 {
							 return Vm.JitIncField(Object, NameIndex);
						 });
		}

		bool MakeFieldReference(const FValue& Object, const std::uint16_t NameIndex, FValue& OutReference) noexcept override
		{
			return ProduceValue(OutReference, [&](FValue &Value)
								{
									return Vm.JitMakeFieldRef(Object, NameIndex, Value);
								});
		}

		bool GetIndex(const FValue& Container, const FValue& Index, FValue& OutValue) noexcept override
		{
			return ProduceValue(OutValue, [&](FValue &Value)
								{
									return Vm.JitGetIndex(Container, Index, Value);
								});
		}

		bool SetIndex(const FValue& Container, const FValue& Index, const FValue& Value,
			FValue& OutContainer) noexcept override
		{
			return ProduceValue(OutContainer, [&](FValue &Result)
								{
									return Vm.JitSetIndex(Container, Index, Value, Result);
								});
		}

		bool MakeIndexReference(const FJitFrameToken Frame, const std::uint16_t LocalSlot,
			const FValue& Index, FValue& OutReference, const bool bBorrow) noexcept override
		{
			return ProduceValue(OutReference, [&](FValue& Value)
			{
				return ValidateFrame(Frame) && Vm.JitGetIndexRef(LocalSlot, Index, Value, bBorrow);
			});
		}

		bool MakeMapEntryReference(const FJitFrameToken Frame, const std::uint16_t LocalSlot,
			const FValue& Key, FValue& OutReference) noexcept override
		{
			return ProduceValue(OutReference, [&](FValue& Value)
			{
				return ValidateFrame(Frame) && Vm.JitGetMapEntryRef(LocalSlot, Key, Value);
			});
		}
		//~ End IJitValueServices Interface
