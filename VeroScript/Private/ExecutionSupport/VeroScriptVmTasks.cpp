// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptVm::CallTypedTaskBuiltin(EBuiltinId Builtin, FValue* A, std::uint8_t)
	{
		using E = Concurrent::FTypedTaskState::EStatus;
		if (!CheckExecutionGovernance()) return false;
		if (OperandStack.size() >= MaxOperandStackDepth)
		{
			ReportError("operand stack overflow");
			return false;
		}
		OperandStack.reserve(OperandStack.size() + 1);
		const auto Fail = [&](std::string Message)
		{
			if (!bHasError)
			{
				ReportErrorId("diag.stdlib_task", {}, {std::move(Message)});
			}
			return false;
		};
		const auto Cancel = [](const std::shared_ptr<Concurrent::FTypedTaskState>& Task, const FChannelHandle& Channel)
		{
			if (Task->Status != E::Active) return;
			Task->Status = E::Cancelled;
			Channel->Close();
			Task->Ticket.Cancel();
		};
		if (Builtin <= EBuiltinId::TypedTaskCancel)
		{
			const auto Task = ResolveTypedTask(A[0], A[1]);
			if (!Task) return false;
			const auto Channel = std::get<FChannelHandle>(A[1]);
			if (Builtin == EBuiltinId::TypedTaskBind) return true;
			if (Builtin == EBuiltinId::TypedTaskValid)
			{
				Push(Task->Status == E::Active);
				return true;
			}
			if (Builtin == EBuiltinId::TypedTaskDone)
			{
				Push(Task->Status != E::Active || Task->Ticket.Finished());
				return true;
			}
			if (Builtin == EBuiltinId::TypedTaskCancel)
			{
				Cancel(Task, Channel);
				return true;
			}
			if (Task->Status != E::Active || Task->Waiting) return Fail("typed task is cancelled, consumed or already being awaited");
			std::int64_t Timeout = -1;
			FObjectHandle Optional;
			std::uint16_t Present = 0, Value = 0;
			if (Builtin == EBuiltinId::TypedTaskAwaitFor)
			{
				if (!Runtime::TryGetInteger64(A[2], Timeout) || Timeout < 0) return Fail("task timeout must be a non-negative integer");
				const auto* Name = std::get_if<std::string>(&A[3]);
				if (!Name) return Fail("invalid optional task result");
				const auto TypeIndex = FindTypeIndex(*Name);
				if (!ActiveModule || TypeIndex >= ActiveModule->Types.size() || ModuleMeta::TypeSimpleName(*Name) != "optional" || !ModuleMeta::FindFieldIndex(*ActiveModule, *Name, "present", Present) || !ModuleMeta::FindFieldIndex(*ActiveModule, *Name, "value", Value) || ActiveModule->Types[TypeIndex].Fields[Present].TypeName != "bool" || !NativeElementTypeNamesEqual(ActiveModule->Types[TypeIndex].Fields[Value].TypeName, Task->ElementType))
				{
					return Fail("invalid optional task result");
				}
				Optional = CreateObject(TypeIndex, {});
				if (!Optional) return false;
			}
			FValue Result;
			std::string OperationError;
			const auto Status = AwaitTypedTask(*Task, *Channel, Timeout, Result, OperationError);
			if (Status == ETypedTaskWait::Interrupted) return false;
			if (Status == ETypedTaskWait::TerminalFailure)
			{
				Task->Status = E::Failed;
				return Fail(std::move(OperationError));
			}
			if (Optional)
			{
				if (Status == ETypedTaskWait::Ready)
				{
					Optional->Fields[Value] = std::move(Result);
					Optional->Fields[Present] = true;
				}
				Push(std::move(Optional));
			}
			else
			{
				Push(std::move(Result));
			}
			return !bHasError;
		}

		const bool Group = Builtin == EBuiltinId::TaskGroupAwaitAll || Builtin == EBuiltinId::TaskGroupCancelAll;
		const bool Cancelling = Builtin == EBuiltinId::TaskGroupCancelAll || Builtin == EBuiltinId::TaskArrayCancelAll;
		FObjectHandle GroupObject;
		FDynamicArrayHandle Array;
		std::int64_t Begin = 0;
		std::uint16_t CursorField = 0, BusyField = 0;
		if (Group)
		{
			const auto* Object = std::get_if<FObjectHandle>(&A[0]);
			if (!Object || !*Object || !ActiveModule || ModuleMeta::TypeSimpleName((*Object)->GetTypeName()) != "task_group") return Fail("invalid task group");
			GroupObject = *Object;
			std::uint16_t TasksField;
			if (!ModuleMeta::FindFieldIndex(*ActiveModule, GroupObject->GetTypeName(), "tasks", TasksField) || !ModuleMeta::FindFieldIndex(*ActiveModule, GroupObject->GetTypeName(), "cursor", CursorField) || !ModuleMeta::FindFieldIndex(*ActiveModule, GroupObject->GetTypeName(), "busy", BusyField) || TasksField >= GroupObject->Fields.size() || CursorField >= GroupObject->Fields.size() || BusyField >= GroupObject->Fields.size())
			{
				return Fail("invalid task group layout");
			}
			const auto* Tasks = std::get_if<FDynamicArrayHandle>(&GroupObject->Fields[TasksField]);
			const auto* Cursor = std::get_if<std::int64_t>(&GroupObject->Fields[CursorField]);
			const auto* Busy = std::get_if<bool>(&GroupObject->Fields[BusyField]);
			if (!Tasks || !*Tasks || !Cursor || !Busy || *Busy || *Cursor < 0 || static_cast<std::uint64_t>(*Cursor) > (*Tasks)->ElementCount())
			{
				return Fail("invalid or busy task group");
			}
			Array = *Tasks;
			Begin = *Cursor;
		}
		else if (const auto* Tasks = std::get_if<FDynamicArrayHandle>(&A[0]); Tasks && *Tasks)
		{
			Array = *Tasks;
		}
		if (!Array) return Fail("task collection requires an array of tasks");
		struct FBusyGuard
		{
			FObjectHandle Object;
			std::uint16_t Field;
			~FBusyGuard()
			{
				if (Object)
				{
					Object->Fields[Field] = false;
				}
			}
		} BusyGuard{GroupObject, BusyField};
		if (GroupObject)
		{
			GroupObject->Fields[BusyField] = true;
		}
		const auto InitialSize = Array->ElementCount();
		if (!Cancelling && (!std::holds_alternative<std::string>(A[1]) || !Runtime::IsAllowedChannelElementType(std::get<std::string>(A[1]))))
		{
			return Fail("invalid task collection result type");
		}
		Array->MaterializeBoxedElements();
		struct FPending
		{
			std::shared_ptr<Concurrent::FTypedTaskState> Task;
			FChannelHandle Channel;
		};
		std::vector<FPending> Pending;
		Pending.reserve(InitialSize - static_cast<std::size_t>(Begin));
		std::unordered_set<const void*> Seen;
		for (std::size_t I = static_cast<std::size_t>(Begin); I < InitialSize; ++I)
		{
			const auto Work = GetMemoryWork();
			if (!Work.Check(Work.UserData, 1)) return false;
			const auto* Object = std::get_if<FObjectHandle>(&Array->Elements[I]);
			StandardLibrary::FTaskLayout Layout;
			if (!Object || !*Object || !ActiveModule || !StandardLibrary::ResolveTaskLayout(*ActiveModule, (*Object)->GetTypeName(), Layout) || Layout.Id >= (*Object)->Fields.size() || Layout.Result >= (*Object)->Fields.size())
			{
				return Fail("task collection contains an invalid task");
			}
			const auto Task = ResolveTypedTask((*Object)->Fields[Layout.Id], (*Object)->Fields[Layout.Result]);
			if (!Task) return false;
			if (!Seen.insert(Task.get()).second) return Fail("task collection contains duplicate task capabilities");
			if (!Cancelling && (Task->Status != E::Active || Task->Waiting)) return Fail("task collection contains an inactive task");
			if (!Cancelling)
			{
				const auto* Element = std::get_if<std::string>(&A[1]);
				if (!Element || !NativeElementTypeNamesEqual(*Element, Task->ElementType)) return Fail("task collection result type mismatch");
			}
			Pending.push_back({Task, std::get<FChannelHandle>((*Object)->Fields[Layout.Result])});
		}
		if (Cancelling)
		{
			// Issue every request before cleanup or waiting; cancellation is cooperative.
			for (auto& Item : Pending)
			{
				Cancel(Item.Task, Item.Channel);
			}
			if (GroupObject)
			{
				GroupObject->Fields[CursorField] = static_cast<std::int64_t>(InitialSize);
				if (!ReleaseValueList(Array->Elements, {})) return false;
				GroupObject->Fields[CursorField] = std::int64_t{0};
			}
			return !bHasError;
		}
		const auto& Element = std::get<std::string>(A[1]);
		auto Results = CreateDynamicArray("array<" + Element + ">", {});
		if (!Results) return false;
		Results->MaterializeBoxedElements();
		Results->Elements.reserve(Pending.size());
		for (auto& Item : Pending)
		{
			FValue Result;
			std::string OperationError;
			const auto Status = AwaitTypedTask(*Item.Task, *Item.Channel, -1, Result, OperationError);
			if (Status == ETypedTaskWait::Interrupted) return false;
			if (Status == ETypedTaskWait::TerminalFailure)
			{
				Item.Task->Status = E::Failed;
				if (GroupObject)
				{
					GroupObject->Fields[CursorField] = ++Begin;
				}
				return Fail(std::move(OperationError));
			}
			Results->Elements.push_back(std::move(Result));
			if (GroupObject)
			{
				GroupObject->Fields[CursorField] = ++Begin;
			}
		}
		if (GroupObject)
		{
			if (!ReleaseValueList(Array->Elements, {})) return false;
			GroupObject->Fields[CursorField] = std::int64_t{0};
		}
		Push(std::move(Results));
		return !bHasError;
	}

	std::shared_ptr<Concurrent::FTypedTaskState> VeroScriptVm::ResolveTypedTask(const FValue& Id, const FValue& Value)
	{
		const auto* Number = std::get_if<std::int32_t>(&Id);
		const auto* Channel = std::get_if<FChannelHandle>(&Value);
		const auto Task = Channel && *Channel ? std::atomic_load(&(*Channel)->TypedTask) : nullptr;
		if (!Number || !Task || !Task->Ticket || Task->Id != *Number || Task->Executor != Executor->Identity() || Task->ProgramOwner != ProgramOwner || Task->Generation != ProgramGeneration)
		{
			ReportErrorId("diag.stdlib_task", {}, {"invalid, foreign or stale typed task capability"});
			return {};
		}
		return Task;
	}

	VeroScriptVm::ETypedTaskWait VeroScriptVm::AwaitTypedTask(Concurrent::FTypedTaskState& Task,
	    FChannel& Channel, std::int64_t Timeout, FValue& Output, std::string& Failure)
	{
		using E = Concurrent::FTypedTaskState::EStatus;
		if (Task.Status != E::Active || Task.Waiting)
		{
			Failure = "typed task is cancelled, consumed or already being awaited";
			return ETypedTaskWait::TerminalFailure;
		}
		Task.Waiting = true;
		struct FWaiting
		{
			bool& Value;
			~FWaiting()
			{
				Value = false;
			}
		} Waiting{Task.Waiting};
		const auto Begin = std::chrono::steady_clock::now();
		const auto Work = GetMemoryWork();
		for (;;)
		{
			if (!Work.Check(Work.UserData, 1)) return ETypedTaskWait::Interrupted;
			if (Task.Status != E::Active)
			{
				Failure = "typed task is no longer active";
				return ETypedTaskWait::TerminalFailure;
			}
			std::int64_t Slice = 10;
			if (Timeout >= 0)
			{
				const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Begin).count();
				Slice = std::min<std::int64_t>(10, std::max<std::int64_t>(0, Timeout - Elapsed));
			}
			bool TimedOut = false;
			if (Task.Ticket.Join(Failure, TimedOut, Slice))
			{
				break;
			}
			if (!TimedOut) return ETypedTaskWait::TerminalFailure;
			if (Timeout >= 0 && Slice == 0) return ETypedTaskWait::Timeout;
		}
		if (!CheckExecutionGovernance()) return ETypedTaskWait::Interrupted;
		// No result is awaited after worker completion. Moving the transferable
		// value, caller's prepared slot, task status and group cursor cannot allocate.
		bool LimitExceeded = false;
		if (!Channel.ConsumeTaskResult(Output, StandardLibraryLimits.MaxTextBytes, LimitExceeded))
		{
			if (LimitExceeded)
			{
				(void)ReportStandardLibraryResult({StandardLibrary::EServiceStatus::LimitExceeded, 0});
				return ETypedTaskWait::Interrupted;
			}
			Failure = "task finished without exactly one result";
			return ETypedTaskWait::TerminalFailure;
		}
		Task.Status = E::Consumed;
		return ETypedTaskWait::Ready;
	}
}
