// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBuiltins.h"
#include "VeroScriptStandardLibrary.h"

#include <array>
#include <cstddef>

namespace VeroScript::Builtins
{
	namespace
	{
		constexpr FDescriptor Descriptors[] = {
			{"__memory_available", EBuiltinId::MemoryAvailable, 0, false},
			{"__memory_address", EBuiltinId::MemoryAddress, 1, false},
			{"__memory_object_address", EBuiltinId::MemoryObjectAddress, 2, false},
			{"__memory_object_id", EBuiltinId::MemoryObjectId, 2, false},
			{"__memory_data_address", EBuiltinId::MemoryDataAddress, 1, false},
			{"__memory_copy", EBuiltinId::MemoryCopy, 5, true},
			{"__memory_fill", EBuiltinId::MemoryFill, 4, true},
			{"__memory_equal", EBuiltinId::MemoryEqual, 5, false},
			{"__memory_clone", EBuiltinId::MemoryClone, 1, false},
			{"__memory_from_array", EBuiltinId::MemoryFromArray, 1, false},
			{"__memory_to_array", EBuiltinId::MemoryToArray, 1, false},
			{"__memory_read", EBuiltinId::MemoryRead, 4, false},
			{"__memory_write", EBuiltinId::MemoryWrite, 5, true},
			{"print", EBuiltinId::Print, 1, true},
			{"to_string", EBuiltinId::ToString, 1, false},
			{"len", EBuiltinId::Len, 1, false},
			{"abs", EBuiltinId::Abs, 1, false},
			{"min", EBuiltinId::Min, 2, false},
			{"max", EBuiltinId::Max, 2, false},
			{"__sqrt", EBuiltinId::Sqrt, 1, false},
			{"__sin", EBuiltinId::Sin, 1, false},
			{"__cos", EBuiltinId::Cos, 1, false},
			{"__tan", EBuiltinId::Tan, 1, false},
			{"__floor", EBuiltinId::Floor, 1, false},
			{"__ceil", EBuiltinId::Ceil, 1, false},
			{"__read_file", EBuiltinId::ReadFile, 1, false},
			{"__write_file", EBuiltinId::WriteFile, 2, true},
			{"__delete_file", EBuiltinId::DeleteFile, 1, false},
			{"__exists", EBuiltinId::Exists, 1, false},
			{"__substr", EBuiltinId::Substr, 3, false},
			{"__find", EBuiltinId::Find, 2, false},
			{"__char_code", EBuiltinId::CharCode, 2, false},
			{"__list_dir", EBuiltinId::ListDir, 1, false},
			{"to_int", EBuiltinId::ToInt, 1, false},
			{"to_double", EBuiltinId::ToDouble, 1, false},
			{"to_float", EBuiltinId::ToFloat, 1, false},
			{"__to_int8", EBuiltinId::ToInt8, 1, false},
			{"__to_uint8", EBuiltinId::ToUInt8, 1, false},
			{"__to_int16", EBuiltinId::ToInt16, 1, false},
			{"__to_uint16", EBuiltinId::ToUInt16, 1, false},
			{"to_unsigned", EBuiltinId::ToUnsigned, 1, false},
			{"to_int64", EBuiltinId::ToInt64, 1, false},
			{"to_uint64", EBuiltinId::ToUInt64, 1, false},
			{"to_wstring", EBuiltinId::ToWString, 1, false},
			{"__split", EBuiltinId::Split, 2, false},
			{"__join", EBuiltinId::Join, 2, false},
			{"__to_lower", EBuiltinId::ToLower, 1, false},
			{"__to_upper", EBuiltinId::ToUpper, 1, false},
			{"__read_line", EBuiltinId::ReadLine, 0, false},
			{"exit", EBuiltinId::Exit, 1, true},
			{"getenv", EBuiltinId::GetEnv, 1, false},
			{"__mkdir", EBuiltinId::MkDir, 1, true},
			{"assert", EBuiltinId::Assert, 1, 2, true},
			{"eprint", EBuiltinId::Eprint, 1, true},
			{"__append_file", EBuiltinId::AppendFile, 2, true},
			{"sleep", EBuiltinId::Sleep, 1, true},
			{"__format", EBuiltinId::Format, 2, false},
			{"__format_printf", EBuiltinId::FormatPrintf, 2, false},
			{"setenv", EBuiltinId::SetEnv, 2, true},
			{"range", EBuiltinId::Range, 1, false},
			{"__cwd", EBuiltinId::Cwd, 0, false},
			{"__chdir", EBuiltinId::ChDir, 1, true},
			{"now", EBuiltinId::Now, 0, false},
			{"__channel_send", EBuiltinId::ChannelSend, 2, true},
			{"__channel_recv", EBuiltinId::ChannelRecv, 1, false},
			{"__channel_close", EBuiltinId::ChannelClose, 1, true},
			{"__spawn", EBuiltinId::Spawn, 2, false},
			{"__join_task", EBuiltinId::JoinTask, 1, true},
			{"__cancel_task", EBuiltinId::CancelTask, 1, true},
			{"__cancel_requested", EBuiltinId::CancelRequested, 0, false},
			{"__weak_create", EBuiltinId::WeakCreate, 1, false},
			{"__weak_lock", EBuiltinId::WeakLock, 1, false},
			{"__weak_expired", EBuiltinId::WeakExpired, 1, false},
			{"__weak_reset", EBuiltinId::WeakReset, 1, true},
			{"__spawn_result", EBuiltinId::SpawnResult, 2, false},
			{"__read_token", EBuiltinId::ReadToken, 0, false},
			{"__read_int", EBuiltinId::ReadInt, 0, false},
			{"__read_double", EBuiltinId::ReadDouble, 0, false},
			{"__read_string", EBuiltinId::ReadString, 0, false},
			{"__read_bool", EBuiltinId::ReadBool, 0, false},
			{"console_write", EBuiltinId::ConsoleWrite, 1, true},
			{"console_error", EBuiltinId::ConsoleError, 1, true},
			{"console_flush", EBuiltinId::ConsoleFlush, 0, true},
			{"console_clear", EBuiltinId::ConsoleClear, 0, false},
			{"console_is_interactive", EBuiltinId::ConsoleIsInteractive, 0, false},
			{"console_supports_color", EBuiltinId::ConsoleSupportsColor, 0, false},
			{"console_supports_cursor", EBuiltinId::ConsoleSupportsCursor, 0, false},
			{"console_supports_clear", EBuiltinId::ConsoleSupportsClear, 0, false},
			{"console_is_output_terminal", EBuiltinId::ConsoleIsOutputTerminal, 0, false},
			{"console_set_color", EBuiltinId::ConsoleSetColor, 2, false},
			{"console_reset_style", EBuiltinId::ConsoleResetStyle, 0, false},
			{"console_clear_line", EBuiltinId::ConsoleClearLine, 1, false},
			{"console_move_cursor", EBuiltinId::ConsoleMoveCursor, 2, false},
			{"console_set_cursor_visible", EBuiltinId::ConsoleSetCursorVisible, 1, false},
			{"console_width", EBuiltinId::ConsoleWidth, 0, false},
			{"console_height", EBuiltinId::ConsoleHeight, 0, false},
			{"steady_now", EBuiltinId::SteadyNow, 0, false},
			{"__round", EBuiltinId::Round, 1, false},
			{"__trunc", EBuiltinId::Trunc, 1, false},
			{"__pow", EBuiltinId::Pow, 2, false},
			{"__exp", EBuiltinId::Exp, 1, false},
			{"__log", EBuiltinId::Log, 1, false},
			{"__log2", EBuiltinId::Log2, 1, false},
			{"__asin", EBuiltinId::Asin, 1, false},
			{"__acos", EBuiltinId::Acos, 1, false},
			{"__atan2", EBuiltinId::Atan2, 2, false},
			{"__isfinite", EBuiltinId::IsFinite, 1, false},
			{"__isnan", EBuiltinId::IsNan, 1, false},
			{"__reflection_string", EBuiltinId::ReflectionString, 5, false},
			{"__reflection_int", EBuiltinId::ReflectionInt, 5, false},
			{"__reflection_bool", EBuiltinId::ReflectionBool, 5, false},
			{"__reflection_int64", EBuiltinId::ReflectionInt64, 5, false},
			{"__reflection_bind", EBuiltinId::ReflectionBind, 1, 2, true},
			{"__method_binding_resolve", EBuiltinId::MethodBindingResolve, 4, false},
			{"__method_binding_validate", EBuiltinId::MethodBindingValidate, 1, true},
			{"__method_binding_invoke", EBuiltinId::MethodBindingInvoke, 2, 255, false},
			{"__reflection_get", EBuiltinId::ReflectionGet, 3, false},
			{"__reflection_set", EBuiltinId::ReflectionSet, 5, true},
			{"__reflection_invoke", EBuiltinId::ReflectionInvoke, 4, 255, false},
			{"__reflection_invoke_static", EBuiltinId::ReflectionInvokeStatic, 2, 255, false},
			{"__source_metadata_string", EBuiltinId::SourceMetadataString, 4, false},
			{"__source_metadata_int", EBuiltinId::SourceMetadataInt, 4, false},
			{"__source_metadata_bool", EBuiltinId::SourceMetadataBool, 4, false},
			{"__source_metadata_int64", EBuiltinId::SourceMetadataInt64, 4, false},
			{"__source_metadata_uint64", EBuiltinId::SourceMetadataUInt64, 4, false},
			{"__source_metadata_float", EBuiltinId::SourceMetadataFloat, 4, false},
			{"__source_metadata_node", EBuiltinId::SourceMetadataNode, 4, false},
			{"__source_metadata_annotation", EBuiltinId::SourceMetadataAnnotation, 4, false},
			{"__source_metadata_argument", EBuiltinId::SourceMetadataArgument, 4, false},
			{"__source_metadata_comment", EBuiltinId::SourceMetadataComment, 4, false},
			{"__source_metadata_definition", EBuiltinId::SourceMetadataDefinition, 4, false},
			{"__channel_try_send", EBuiltinId::ChannelTrySend, 2, false},
			{"__channel_send_for", EBuiltinId::ChannelSendFor, 3, false},
			{"__channel_try_recv", EBuiltinId::ChannelTryRecv, 1, false},
			{"__channel_recv_for", EBuiltinId::ChannelRecvFor, 2, false},
			{"__channel_is_closed", EBuiltinId::ChannelIsClosed, 1, false},
			{"__channel_size", EBuiltinId::ChannelSize, 1, false},
			{"__channel_capacity", EBuiltinId::ChannelCapacity, 1, false},
			{"__channel_set_capacity", EBuiltinId::ChannelSetCapacity, 2, true},
			{"__join_task_for", EBuiltinId::JoinTaskFor, 2, false},
			{"__task_done", EBuiltinId::TaskDone, 1, false},
			{"__is_file", EBuiltinId::IsFile, 1, false},
			{"__is_directory", EBuiltinId::IsDirectory, 1, false},
			{"__remove_dir", EBuiltinId::RemoveDir, 1, false},
			{"__spawn_callable", EBuiltinId::SpawnCallable, 1, 255, false},
			{"__spawn_callable_result", EBuiltinId::SpawnCallableResult, 2, 255, false},
			{"__scalar_parse", EBuiltinId::ScalarParse, 3, false},
			{"__scalar_try_parse", EBuiltinId::ScalarTryParse, 4, false},
			{"__string_starts_with", EBuiltinId::StringStartsWith, 2, false},
			{"__string_ends_with", EBuiltinId::StringEndsWith, 2, false},
			{"__string_last_find", EBuiltinId::StringLastFind, 2, false},
			{"__string_repeat", EBuiltinId::StringRepeat, 2, false},
			{"__string_replace", EBuiltinId::StringReplace, 3, false},
			{"__string_trim", EBuiltinId::StringTrim, 3, false},
			{"__string_encode_utf8", EBuiltinId::StringEncodeUtf8, 1, false},
			{"__string_decode_utf8", EBuiltinId::StringDecodeUtf8, 1, false},
			{"__string_is_valid_utf8", EBuiltinId::StringIsValidUtf8, 1, false},
			{"__try_read_line", EBuiltinId::TryReadLine, 1, false},
			{"__try_read_token", EBuiltinId::TryReadToken, 1, false},
			{"__read_bytes", EBuiltinId::ReadBytes, 1, 2, false},
			{"__write_bytes", EBuiltinId::WriteBytes, 2, true},
			{"__append_bytes", EBuiltinId::AppendBytes, 2, true},
			{"__path_is_absolute", EBuiltinId::PathIsAbsolute, 2, false},
			{"__path_root", EBuiltinId::PathRoot, 2, false},
			{"__path_normalize", EBuiltinId::PathNormalize, 2, false},
			{"try_getenv", EBuiltinId::TryGetEnv, 2, false},
			{"unsetenv", EBuiltinId::UnsetEnv, 1, true},
			{"__numeric_type", EBuiltinId::NumericTypeValidate, 1, true},
			{"__sync_make_mutex", EBuiltinId::MakeMutex, 0, false},
			{"__sync_mutex_lock", EBuiltinId::MutexLock, 1, true},
			{"__sync_mutex_try_lock", EBuiltinId::MutexTryLock, 1, false},
			{"__sync_mutex_lock_for", EBuiltinId::MutexLockFor, 2, false},
			{"__sync_mutex_unlock", EBuiltinId::MutexUnlock, 1, true},
			{"__sync_make_semaphore", EBuiltinId::MakeSemaphore, 2, false},
			{"__sync_semaphore_acquire", EBuiltinId::SemaphoreAcquire, 1, true},
			{"__sync_semaphore_try_acquire", EBuiltinId::SemaphoreTryAcquire, 1, false},
			{"__sync_semaphore_acquire_for", EBuiltinId::SemaphoreAcquireFor, 2, false},
			{"__sync_semaphore_release", EBuiltinId::SemaphoreRelease, 1, true},
			{"__sync_make", EBuiltinId::MakeSynchronized, 2, false},
			{"__sync_load", EBuiltinId::SynchronizedLoad, 1, false},
			{"__sync_store", EBuiltinId::SynchronizedStore, 2, true},
			{"__sync_exchange", EBuiltinId::SynchronizedExchange, 2, false},
			{"__sync_fetch_add", EBuiltinId::SynchronizedFetchAdd, 2, false},
			{"__typed_task_bind", EBuiltinId::TypedTaskBind, 2, true},
			{"__typed_task_valid", EBuiltinId::TypedTaskValid, 2, false},
			{"__typed_task_done", EBuiltinId::TypedTaskDone, 2, false},
			{"__typed_task_await", EBuiltinId::TypedTaskAwait, 2, false},
			{"__typed_task_await_for", EBuiltinId::TypedTaskAwaitFor, 4, false},
			{"__typed_task_cancel", EBuiltinId::TypedTaskCancel, 2, true},
			{"__task_group_await_all", EBuiltinId::TaskGroupAwaitAll, 2, false},
			{"__task_group_cancel_all", EBuiltinId::TaskGroupCancelAll, 1, true},
			{"__task_array_await_all", EBuiltinId::TaskArrayAwaitAll, 2, false},
			{"__task_array_cancel_all", EBuiltinId::TaskArrayCancelAll, 1, true},
			{"__array_prepare_sort", EBuiltinId::ArrayPrepareSort, 1, false},
			{"__array_reverse", EBuiltinId::ArrayReverse, 1, true},
			{"__array_rotate", EBuiltinId::ArrayRotate, 2, true},
			{"__array_shuffle", EBuiltinId::ArrayShuffle, 2, true},
			{"__random_next_uint", EBuiltinId::RandomNextUInt, 1, false},
			{"__random_index", EBuiltinId::RandomIndex, 2, false},
			{"__variant_empty", EBuiltinId::VariantEmpty, 0, false},
			{"variant_has_value", EBuiltinId::VariantHasValue, 1, false},
			{"variant_type_name", EBuiltinId::VariantTypeName, 1, false},
		};

		constexpr std::size_t DescriptorIdCount =
			static_cast<std::size_t>(EBuiltinId::Count);

		constexpr std::array<const FDescriptor*, DescriptorIdCount>
		MakeDescriptorsById()
		{
			std::array<const FDescriptor*, DescriptorIdCount> Result{};
			for (const FDescriptor& Descriptor : Descriptors)
			{
				const std::size_t Index = static_cast<std::size_t>(Descriptor.Id);
				// Keep the first descriptor as the canonical bytecode diagnostic name.
				if (Result[Index] == nullptr)
				{
					Result[Index] = &Descriptor;
				}
			}
			return Result;
		}

		constexpr auto DescriptorsById = MakeDescriptorsById();
	}

	const FDescriptor* Lookup(const std::string_view Name)
	{
		const std::string_view StandardName = StandardLibrary::ResolveFunctionName(Name);
		const std::string_view LookupName = StandardName.empty() ? Name : StandardName;
		for (const FDescriptor& Descriptor : Descriptors)
		{
			if (Descriptor.Name == LookupName)
			{
				return &Descriptor;
			}
		}
		return nullptr;
	}

	const FDescriptor* LookupById(const EBuiltinId Id)
	{
		const std::size_t Index = static_cast<std::size_t>(Id);
		if (Index >= DescriptorsById.size())
		{
			return nullptr;
		}
		return DescriptorsById[Index];
	}
}
