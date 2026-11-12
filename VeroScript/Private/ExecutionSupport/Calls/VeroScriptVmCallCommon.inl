// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		const std::string& MethodName = ActiveModule->StringPool[MethodNameIndex];
		std::optional<EMutationKind> ContainerMutationKind;
		if (MethodName == "clear")
		{
			ContainerMutationKind = EMutationKind::ContainerClear;
		}
		else if (MethodName == "add" || MethodName == "push" || MethodName == "push_front" || MethodName == "push_back" || MethodName == "enqueue" || MethodName == "insert")
		{
			ContainerMutationKind = EMutationKind::ContainerAdd;
		}
		else if (MethodName == "remove" || MethodName == "remove_at" || MethodName == "pop" || MethodName == "pop_front" || MethodName == "pop_back" || MethodName == "dequeue" || MethodName == "erase")
		{
			ContainerMutationKind = EMutationKind::ContainerRemove;
		}
		else if (MethodName == "set")
		{
			ContainerMutationKind = EMutationKind::ContainerSet;
		}
		if ((ContainerMutationKind || MethodName == "reserve") && !CheckContainerCopyMutation(ThisValue, SourceLocation))
		{
			return false;
		}
		// These methods keep native iterators while constructing independent
		// values. User copy constructors may read the source or change unrelated
		// containers, but may not invalidate this iteration by reentrant writes.
		VmDetail::FContainerCopyReadScope CopyReadScope(ActiveContainerCopyReads,
		    MethodName == "keys" || MethodName == "values" || MethodName == "entries"
		        ? ContainerStorageIdentity(ThisValue)
		        : nullptr);
		struct FContainerMutationGuard
		{
			~FContainerMutationGuard()
			{
				if (Vm && bError && !*bError && Container && Kind)
				{
					Vm->NotifyContainerMutation(*Container, *Kind);
				}
			}

			VeroScriptVm* Vm = nullptr;
			const bool* bError = nullptr;
			const FValue* Container = nullptr;
			std::optional<EMutationKind> Kind;

		} MutationGuard{
		    this,
		    &bHasError,
		    &ThisValue,
		    ContainerMutationKind};
		const auto ReadReserveCount = [&]() -> std::optional<std::size_t>
		{
			if (ArgumentCount != 1)
			{
				ReportError("reserve expects exactly 1 argument", SourceLocation);
				return std::nullopt;
			}
			std::int64_t SignedCount = 0;
			if (Runtime::TryGetInteger64(Arguments[0], SignedCount) && SignedCount < 0)
			{
				ReportError("reserve capacity cannot be negative", SourceLocation);
				return std::nullopt;
			}
			std::size_t Count = 0;
			if (!Runtime::TryGetSizeValue(Arguments[0], Count))
			{
				ReportError("reserve capacity must be a non-negative integer within this platform's range", SourceLocation);
				return std::nullopt;
			}
			return Count;
		};
