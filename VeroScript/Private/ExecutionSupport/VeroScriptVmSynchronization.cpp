// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptRuntime.h"

namespace VeroScript
{
	bool VeroScriptVm::CallSynchronizationBuiltin(EBuiltinId Builtin, FValue* A, std::uint8_t Count)
	{
		using namespace StandardLibrary;
		const FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
		if (!Context.Check()) return false;
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
				ReportErrorId("diag.stdlib_sync", {}, {std::move(Message)});
			}
			return false;
		};
		const auto Prepare = [&](const FValue& Source, FValue& Output) -> bool
		{
			if (const auto* Text = std::get_if<std::string>(&Source))
			{
				std::string Result;
				if (!ReportStandardLibraryResult(Repeat(*Text, 1, Result, Context))) return false;
				Output = std::move(Result);
			}
			else if (const auto* WideText = std::get_if<std::u16string>(&Source))
			{
				if (!ReportStandardLibraryResult(Context.CheckText(static_cast<std::uint64_t>(WideText->size()) * 2))) return false;
				std::u16string Result;
				Result.reserve(WideText->size());
				for (std::size_t I = 0; I < WideText->size();)
				{
					const auto N = std::min<std::size_t>(512, WideText->size() - I);
					if (!Context.Check(N * 2)) return false;
					Result.append(WideText->data() + I, N);
					I += N;
				}
				Output = std::move(Result);
			}
			else if (Concurrent::IsChannelPayload(Source))
			{
				Output = Source;
			}
			else
			{
				return Fail("unsupported synchronized payload");
			}
			return true;
		};
		std::string OperationError;
		if (Builtin == EBuiltinId::MakeMutex)
		{
			Push(FChannel::MakeMutex());
			return !bHasError;
		}
		if (Builtin == EBuiltinId::MakeSemaphore)
		{
			std::int64_t Initial = 0, Maximum = 0;
			if (!Runtime::TryGetInteger64(A[0], Initial) || !Runtime::TryGetInteger64(A[1], Maximum)) return Fail("semaphore counts must be integers");
			auto Result = FChannel::MakeSemaphore(Initial, Maximum, OperationError);
			if (!Result) return Fail(std::move(OperationError));
			Push(std::move(Result));
			return !bHasError;
		}
		if (Builtin == EBuiltinId::MakeSynchronized)
		{
			const auto* Type = std::get_if<std::string>(&A[1]);
			if (!Type) return Fail("invalid synchronized type");
			FValue Prepared;
			if (!Prepare(A[0], Prepared)) return false;
			auto Result = FChannel::MakeSynchronized("channel<" + *Type + ">", *Type, std::move(Prepared), OperationError);
			if (!Result) return Fail(std::move(OperationError));
			Push(std::move(Result));
			return !bHasError;
		}
		const auto* Channel = std::get_if<FChannelHandle>(&A[0]);
		if (!Channel || !*Channel) return Fail("invalid synchronization capability");
		if (Builtin >= EBuiltinId::SynchronizedLoad && Builtin <= EBuiltinId::SynchronizedFetchAdd)
		{
			std::shared_ptr<const FValue> Desired;
			if (Builtin == EBuiltinId::SynchronizedStore || Builtin == EBuiltinId::SynchronizedExchange)
			{
				FValue Prepared;
				if (!Prepare(A[1], Prepared)) return false;
				Desired = std::make_shared<const FValue>(std::move(Prepared));
			}
			for (;;)
			{
				if (!Context.Check(1)) return false;
				auto Previous = (*Channel)->SnapshotSynchronized(OperationError);
				if (!Previous) return Fail(std::move(OperationError));
				FValue Result;
				if (Builtin != EBuiltinId::SynchronizedStore && !Prepare(*Previous, Result)) return false;
				if (Builtin == EBuiltinId::SynchronizedLoad)
				{
					Push(std::move(Result));
					return !bHasError;
				}
				if (Builtin == EBuiltinId::SynchronizedFetchAdd)
				{
					FValue Next;
					if (!CheckedNumericAdd(*Previous, A[1], Next)) return Fail("synchronized fetch_add requires equal primitive numeric types without overflow");
					Desired = std::make_shared<const FValue>(std::move(Next));
				}
				if (!Context.Check()) return false;
				bool Published = false;
				if (!(*Channel)->PublishSynchronized(Previous, Desired, Builtin != EBuiltinId::SynchronizedStore, Published, OperationError)) return Fail(std::move(OperationError));
				if (!Published)
				{
					continue;
				}
				if (Builtin != EBuiltinId::SynchronizedStore)
				{
					Push(std::move(Result));
				}
				NotifyContainerMutation(A[0], EMutationKind::ContainerSet);
				return !bHasError;
			}
		}
		const bool Mutex = Builtin >= EBuiltinId::MutexLock && Builtin <= EBuiltinId::MutexUnlock;
		if (Builtin == EBuiltinId::MutexUnlock || Builtin == EBuiltinId::SemaphoreRelease)
		{
			if (!(*Channel)->ReleaseSynchronization(Mutex, Executor->Identity(), OperationError)) return Fail(std::move(OperationError));
			NotifyContainerMutation(A[0], EMutationKind::ContainerSet);
			return !bHasError;
		}
		std::int64_t Timeout = Builtin == EBuiltinId::MutexLock || Builtin == EBuiltinId::SemaphoreAcquire ? -1 : 0;
		if (Count == 2 && (!Runtime::TryGetInteger64(A[1], Timeout) || Timeout < 0)) return Fail("synchronization timeout must be a non-negative integer");
		if (Mutex)
		{
			Executor->PrepareMutex(*Channel);
		}
		bool Acquired = false;
		if (!(*Channel)->AcquireSynchronization(Mutex, Executor->Identity(), Timeout, Acquired, OperationError, Context.Work)) return Fail(std::move(OperationError));
		if (Builtin != EBuiltinId::MutexLock && Builtin != EBuiltinId::SemaphoreAcquire)
		{
			Push(Acquired);
		}
		if (Acquired)
		{
			NotifyContainerMutation(A[0], EMutationKind::ContainerSet);
		}
		return !bHasError;
	}
}
