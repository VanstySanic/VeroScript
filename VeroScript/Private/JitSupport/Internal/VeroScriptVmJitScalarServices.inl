// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		//~ Begin IJitScalarServices Interface
		bool AddLocalImm(std::uint16_t Slot, std::int32_t Imm) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitAddLocalImm(Slot, Imm);
			});
		}

		bool AddLocalLocal(std::uint16_t DestSlot, std::uint16_t SrcSlot) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitAddLocalLocal(DestSlot, SrcSlot);
			});
		}

		bool AddValueToLocal(std::uint16_t DestSlot, const FValue& Source) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitAddValueToLocal(DestSlot, Source);
			});
		}

		bool BindLocal(std::uint16_t Slot, FValue Value) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBindLocal(Slot, std::move(Value));
			});
		}

		FValue* BorrowDirectLocal(const std::uint16_t Slot) noexcept override
		{
			FValue* Storage = nullptr;
			(void)Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				Storage = Vm.JitBorrowDirectLinearLocal(Slot);
				return true;
			});
			return Storage;
		}

		bool BorrowLocalF32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			float*& OutStorage) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBorrowLocalF32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
			});
		}

		bool BorrowLocalF64Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			double*& OutStorage) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBorrowLocalF64Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
			});
		}

		bool BorrowLocalI32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBorrowLocalI32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
			});
		}

		bool BorrowLocalScalarField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			const FValue*& OutStorage) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBorrowLocalScalarField(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
			});
		}

		bool BorrowLocalU32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::uint32_t*& OutStorage) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitBorrowLocalU32Field(Slot, NameIndex, OutObject, OutFieldIndex, OutStorage);
			});
		}

		bool CanUseScalarNativeCalls(
			const std::uint32_t* EncodedGuards,
			std::size_t GuardCount
		) noexcept override
		{
			return Guard([&]()
						 {
							 return Vm.JitCanUseScalarNativeCalls(EncodedGuards, GuardCount);
						 });
		}

		void CommitBorrowedFieldMutation(
			FScriptObject& Object,
			const std::uint16_t FieldIndex) noexcept override
		{
			(void)Guard([&]()
						{
							Vm.JitCommitBorrowedFieldMutation(Object, FieldIndex);
							return !HasError();
						});
		}

		bool CompareLocals(
			EOpcode Op,
			std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitCompareLocals(Op, LeftSlot, RightSlot, bOutResult);
			});
		}

		bool EnableScalarArrayStorage(FValue& Container) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitEnableScalarArrayStorage(Container);
			});
		}

		bool EnableScalarMapStorage(FValue& Container) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitEnableScalarMapStorage(Container);
			});
		}

		bool EnableScalarSetStorage(FValue& Container) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitEnableScalarSetStorage(Container);
			});
		}

		bool EvaluateCondition(const FValue& Value, bool& bOutCondition) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitEvaluateCondition(Value, bOutCondition);
			});
		}

		bool HandleLinearThrow(
			FValue Value,
			std::size_t InstructionOffset,
			std::size_t& OutHandlerOffset) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitHandleLinearThrow(std::move(Value), InstructionOffset, OutHandlerOffset);
			});
		}

		bool LoadLocalField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitLoadLocalField(Slot, NameIndex, OutValue);
			});
		}

		bool LocalGeImm(std::uint16_t Slot, std::int32_t Immediate, bool& bOutResult) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitLocalGeImm(Slot, Immediate, bOutResult);
			});
		}

		bool LocalGeLocal(
			std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitLocalGeLocal(LeftSlot, RightSlot, bOutResult);
			});
		}

		bool TryAddLocalFromLocalField(
			std::uint16_t DestSlot,
			std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint32_t CallSiteOffset,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryAddLocalFromLocalField(DestSlot, ObjectSlot, NameIndex, CallSiteOffset, OutHandled);
			});
		}

		bool TryAddLocalFromScalarArray(
			std::uint16_t DestSlot,
			std::uint16_t ArraySlot,
			const FValue& Index,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryAddLocalFromScalarArray(DestSlot, ArraySlot, Index, OutHandled);
			});
		}

		bool TryAddLocalFromScalarMap(
			std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryAddLocalFromScalarMap(DestSlot, MapSlot, Key, OutHandled);
			});
		}

		bool TryAddLocalFromScalarMapModuloI32(
			std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			std::int32_t Modulus,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryAddLocalFromScalarMapModuloI32(DestSlot, MapSlot, Key, Modulus, OutHandled);
			});
		}

		bool TryCallScalarContainerMethodOnLocal(
			std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryCallScalarContainerMethodOnLocal(Slot, MethodNameIndex, Arguments, ArgumentCount, bDiscardReturn, OutValue, OutHandled);
			});
		}

		bool TryCallScalarContainerMethodOnLocals(
			std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint16_t ArgumentSlot,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryCallScalarContainerMethodOnLocals(Slot, MethodNameIndex, ArgumentSlot, bDiscardReturn, OutValue, OutHandled);
			});
		}

		bool TryCallScalarSetMethodOnLocal(
			std::uint16_t Slot,
			const FValue& Argument,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryCallScalarSetMethodOnLocal(Slot, Argument, Operation, bDiscardReturn, OutValue, OutHandled);
			});
		}

		bool TryCallScalarSetMethodOnLocals(
			std::uint16_t Slot,
			std::uint16_t ArgumentSlot,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryCallScalarSetMethodOnLocals(Slot, ArgumentSlot, Operation, bDiscardReturn, OutValue, OutHandled);
			});
		}

		bool TryCheckedScalarCast(
			const FValue& Value,
			std::uint16_t TargetTypeNameIndex,
			FValue& OutResult,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryCheckedScalarCast(Value, TargetTypeNameIndex, OutResult, OutHandled);
			});
		}

		bool TryCompareStringArrayElements(
			std::uint16_t LeftArraySlot,
			std::uint16_t LeftIndexSlot,
			std::uint16_t RightArraySlot,
			std::uint16_t RightIndexSlot,
			bool bEqual,
			bool& OutResult,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryCompareStringArrayElements(LeftArraySlot, LeftIndexSlot, RightArraySlot, RightIndexSlot, bEqual, OutResult, OutHandled);
			});
		}

		bool TryGetDynamicArrayScalar(
			const FValue& Container,
			std::int64_t Index,
			FValue& OutValue,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryGetDynamicArrayScalar(Container, Index, OutValue, bOutHandled);
			});
		}

		bool TryGetScalarIndexOnLocal(
			std::uint16_t Slot,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutMapHandled,
			bool& bOutArrayHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryGetScalarIndexOnLocal(Slot, Index, OutValue, bOutMapHandled, bOutArrayHandled);
			});
		}

		bool TryGetScalarMap(
			const FValue& Container,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryGetScalarMap(Container, Index, OutValue, bOutHandled);
			});
		}

		bool TryIncAndAccumulateLocalI32Field(
			std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint16_t AccumulatorSlot,
			bool& OutHandled,
			EJitFieldAccumulateFailure& OutFailure) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryIncAndAccumulateLocalI32Field(ObjectSlot, NameIndex, AccumulatorSlot, OutHandled, OutFailure);
			});
		}

		bool TryIncLocalField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryIncLocalField(Slot, NameIndex, OutHandled);
			});
		}

		bool TryInsertI32SetRangeOnLocal(
			std::uint16_t Slot,
			std::int32_t Begin,
			std::int32_t End,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryInsertI32SetRangeOnLocal(Slot, Begin, End, OutHandled);
			});
		}

		bool TryLoadScalarGlobal(
			std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryLoadScalarGlobal(Slot, OutValue, OutHandled);
			});
		}

		bool TryLoadScalarLocal(
			std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryLoadScalarLocal(Slot, OutValue, OutHandled);
			});
		}

		bool TryPushDynamicArrayScalar(
			const FValue& Container,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryPushDynamicArrayScalar(Container, Value, bOutHandled);
			});
		}

		bool TryPushDynamicArrayScalarOnLocal(
			std::uint16_t Slot,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryPushDynamicArrayScalarOnLocal(Slot, Value, bOutHandled);
			});
		}

		bool TryScalarBuiltinConversion(
			EBuiltinId Builtin,
			const FValue& Value,
			FValue& OutResult,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTryScalarBuiltinConversion(Builtin, Value, OutResult, OutHandled);
			});
		}

		bool TrySetDynamicArrayScalar(
			FValue& Container,
			std::int64_t Index,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTrySetDynamicArrayScalar(Container, Index, Value, bOutHandled);
			});
		}

		bool TrySetDynamicArrayScalarOnLocal(
			std::uint16_t ArraySlot,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTrySetDynamicArrayScalarOnLocal(ArraySlot, Index, Value, bOutHandled);
			});
		}

		bool TrySetI32MapAffineRangeOnLocal(
			std::uint16_t MapSlot,
			std::int32_t Begin,
			std::int32_t End,
			std::int32_t Immediate,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTrySetI32MapAffineRangeOnLocal(MapSlot, Begin, End, Immediate, OutHandled);
			});
		}

		bool TrySetScalarMap(
			FValue& Container,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitTrySetScalarMap(Container, Index, Value, bOutHandled);
			});
		}

		bool TrySetScalarMapDirectFromLocals(
			std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t SourceSlot,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTrySetScalarMapDirectFromLocals(MapSlot, KeySlot, SourceSlot, bOutHandled);
			});
		}

		bool TrySetScalarMapDirectOnLocal(
			std::uint16_t MapSlot,
			const FValue& Key,
			const FValue& Value,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTrySetScalarMapDirectOnLocal(MapSlot, Key, Value, bOutHandled);
			});
		}

		bool TrySetScalarMapI32AddOnLocals(
			std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t ValueSlot,
			std::int32_t Immediate,
			bool& bOutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTrySetScalarMapI32AddOnLocals(MapSlot, KeySlot, ValueSlot, Immediate, bOutHandled);
			});
		}

		bool TryUpdateLocalFieldI32Modulo(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			const FValue& Delta,
			std::int32_t Modulus,
			std::uint32_t CallSiteOffset,
			bool& OutHandled) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryUpdateLocalFieldI32Modulo(Slot, NameIndex, Delta, Modulus, CallSiteOffset, OutHandled);
			});
		}

		bool TryUpdateScalarMapEntryOnLocal(
			std::uint16_t MapSlot,
			const FValue& Key,
			EOpcode Operation,
			const FValue& Operand,
			bool& OutHandled,
			EJitScalarMapUpdateFailure& OutFailure) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(FrameToken)) return false;
				return Vm.JitTryUpdateScalarMapEntryOnLocal(MapSlot, Key, Operation, Operand, OutHandled, OutFailure);
			});
		}

		//~ End IJitScalarServices Interface
