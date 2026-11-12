// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibrary.h"
#include "VeroScriptAst.h"
#include <algorithm>

#include <cstddef>
#include <string>

namespace VeroScript::StandardLibrary
{
	bool IsMethodBindingFactoryDeclaration(const FFunctionDecl& Function)
	{
		const bool Callable = Function.Name == "__std_bind_method";
		const bool Weak = Function.Name == "__std_make_weak_attribute_by_name";
		if ((!Callable && !Weak && Function.Name != "__std_make_attribute_by_name") || Function.Body ||
			Function.bDeleted || !Function.bExported ||
			Function.TypeParams != std::vector<std::string>{Callable ? "F" : "T", "U"} ||
			!Function.Bounds.empty() || Function.Parameters.size() != 2 ||
			Function.ReturnType != (Callable ? "F" : "attribute<T>") ||
			Function.Parameters[0].TypeName != (Weak ? "weak<U>" : "U") ||
			Function.Parameters[1].TypeName != "string")
		{
			return false;
		}

		for (const auto& P : Function.Parameters)
		{
			if (P.bIsRef || P.bIsVariadic || P.DefaultValue) return false;
		}

		return std::any_of(Function.DeclMeta.Annotations.begin(), Function.DeclMeta.Annotations.end(),
			[](const auto& A)
			{
				return A.Name == "@__std_method_binding_factory" && A.Arguments.empty();
			});
	}

	bool IsAttributeImplementationModule(const FProgram& Program, const std::string_view ModulePath)
	{
		for (const auto& D : Program.Structs)
		{
			if (D.Name == "attribute" && D.ModulePath == ModulePath && D.bExported &&
				D.TypeParams == std::vector<std::string>{"T"})
			{
				for (const auto& A : D.DeclMeta.Annotations)
				{
					if (A.Name == "@__std_attribute_library" && A.Arguments.empty()) return true;
				}
			}
		}

		return false;
	}

	namespace
	{
		constexpr FNameAlias SharedAlias = {"shared", "handle"};
		constexpr FNameAlias TypeAliases[] = {
			{"std.variant", "variant"},
			{"std.bigint", "std.bigint"},
			{"std.rational", "std.rational"},
			{"std.numeric.rounding_mode", "std.numeric.rounding_mode"},
			{"std.i18n.argument", "std.i18n.argument"},
			{"std.i18n.arguments", "std.i18n.arguments"},
			{"std.i18n.configuration", "std.i18n.configuration"},
			{"std.i18n.text", "std.i18n.text"},
			{"std.i18n.version", "std.i18n.version"},
			{"std.i18n.message_template", "std.i18n.message_template"},
			{"std.i18n.resolved_text", "std.i18n.resolved_text"},
			{"std.i18n.snapshot", "std.i18n.snapshot"},
			{"std.i18n.context", "std.i18n.context"},
			{"std.i18n.argument_kind", "std.i18n.argument_kind"},
			{"std.memory.buffer", "std.memory.buffer"},
			{"std.memory.view", "std.memory.view"},
			{"std.memory.readonly_view", "std.memory.readonly_view"},
			{"std.memory.byte_order", "std.memory.byte_order"},
			{"std.filesystem.path_style", "std.filesystem.path_style"},
			SharedAlias,
			{"std.string", "string"},
			{"std.wstring", "wstring"},
			{"std.array", "array"},
			{"std.map", "map"},
			{"std.multimap", "multimap"},
			{"std.ordered_map", "ordered_map"},
			{"std.ordered_multimap", "ordered_multimap"},
			{"std.set", "set"},
			{"std.multiset", "multiset"},
			{"std.ordered_multiset", "ordered_multiset"},
			{"std.queue", "queue"},
			{"std.priority_queue", "priority_queue"},
			{"std.stack", "stack"},
			{"std.deque", "deque"},
			{"std.channel", "channel"},
			{"std.pair", "pair"},
			{"std.optional", "optional"},
			{"std.readonly", "readonly"},
			{"std.weak", "weak"},
			{"std.handle", "handle"},
			{"std.shared", "handle"},
			{"std.delegate", "delegate"},
			{"std.attribute", "attribute"},
			{"std.multicast_delegate", "multicast_delegate"},
			{"std.task", "task"},
			{"std.concurrent.task_group", "task_group"},
			{"std.concurrent.mutex", "mutex"},
			{"std.concurrent.counting_semaphore", "counting_semaphore"},
			{"std.type", "type"},
			{"std.field_info", "field_info"},
			{"std.method_info", "method_info"},
			{"std.module_info", "module_info"},
			{"std.function_info", "function_info"},
			{"std.global_info", "global_info"},
			{"std.enum_member_info", "enum_member_info"},
			{"std.source_node_info", "source_node_info"},
			{"std.annotation_info", "annotation_info"},
			{"std.annotation_argument_info", "annotation_argument_info"},
			{"std.comment_info", "comment_info"},
			{"std.annotation_definition_info", "annotation_definition_info"},
			{"std.parameter_info", "source_node_info"},
			{"std.declaration_info", "source_node_info"},
			{"parameter_info", "source_node_info"},
			{"declaration_info", "source_node_info"},
		};

		// Public compiler intrinsics and exported stdlib functions share the same
		// namespace surface. An alias to a source stdlib function remains subject to
		// its normal import rules because resolution only changes its spelling.
		constexpr FNameAlias FunctionAliases[] = {
			{"std.variant_has_value", "variant_has_value"},
			{"std.variant_type_name", "variant_type_name"},
			{"std.variant_is", "variant_is"},
			{"std.numeric.gcd", "std.numeric.gcd"},
			{"std.numeric.lcm", "std.numeric.lcm"},
			{"std.numeric.divmod", "numeric_divmod"},
			{"std.numeric.pow", "__std_numeric_pow"},
			{"std.strings.parse", "__std_parse"},
			{"std.strings.try_parse", "__std_try_parse"},
			{"std.strings.encode_utf8", "__std_encode_utf8"},
			{"std.strings.decode_utf8", "__std_decode_utf8"},
			{"std.strings.is_valid_utf8", "__std_is_valid_utf8"},
			{"std.i18n.normalize_language_tag", "std.i18n.normalize_language_tag"},
			{"std.bind_method", "__std_bind_method"},
			{"std.make_weak_attribute", "__std_make_weak_attribute"},
			{"std.make_attribute_by_name", "__std_make_attribute_by_name"},
			{"std.make_weak_attribute_by_name", "__std_make_weak_attribute_by_name"},
			{"std.compiler.configuration", "std.compiler.configuration"},
			{"std.compiler.read_dependency", "std.compiler.read_dependency"},
			{"std.compiler.check_cancelled", "std.compiler.check_cancelled"},
			{"std.compiler.escape_literal", "std.compiler.escape_literal"},
			{"std.compiler.source_name", "std.compiler.source_name"},
			{"std.memory.native_addresses_available", "__memory_available"},
			{"std.memory.address_of", "__memory_address"},
			{"std.memory.object_address", "__memory_object_address"},
			{"std.memory.object_id", "__memory_object_id"},
			{"std.memory.data_address", "__memory_data_address"},
			{"std.memory.copy", "__memory_copy"},
			{"std.memory.fill", "__memory_fill"},
			{"std.memory.equal", "__memory_equal"},
			{"std.memory.clone", "__memory_clone"},
			{"std.memory.from_array", "__memory_from_array"},
			{"std.memory.to_array", "__memory_to_array"},
			{"std.memory.read", "__memory_read"},
			{"std.memory.write", "__memory_write"},
			{"std.print", "print"},
			{"std.eprint", "eprint"},
			{"std.printf", "printf"},
			{"std.format", "format"},
			{"std.formatf", "formatf"},
			{"std.len", "len"},
			{"std.assert", "assert"},
			{"std.to_string", "to_string"},
			{"std.to_wstring", "to_wstring"},
			{"std.to_int", "to_int"},
			{"std.to_unsigned", "to_unsigned"},
			{"std.to_int64", "to_int64"},
			{"std.to_uint64", "to_uint64"},
			{"std.to_double", "to_double"},
			{"std.to_float", "to_float"},
			{"std.abs", "abs"},
			{"std.min", "min"},
			{"std.max", "max"},
			{"std.range", "range"},
			{"std.exit", "exit"},
			{"std.chrono.now", "now"},
			{"std.chrono.steady_now", "steady_now"},
			{"std.this_thread.sleep", "sleep"},
			{"std.environment.getenv", "getenv"},
			{"std.environment.setenv", "setenv"},
			{"std.environment.try_getenv", "try_getenv"},
			{"std.environment.unsetenv", "unsetenv"},
			{"std.process.exit", "exit"},
			{"std.console.write", "console_write"},
			{"std.console.error", "console_error"},
			{"std.console.flush", "console_flush"},
			{"std.console.clear", "console_clear"},
			{"std.console.is_interactive", "console_is_interactive"},
			{"std.console.supports_color", "console_supports_color"},
			{"std.console.supports_cursor", "console_supports_cursor"},
			{"std.console.supports_clear", "console_supports_clear"},
			{"std.console.is_output_terminal", "console_is_output_terminal"},
			{"std.console.set_color", "console_set_color"},
			{"std.console.reset_style", "console_reset_style"},
			{"std.console.clear_line", "console_clear_line"},
			{"std.console.move_cursor", "console_move_cursor"},
			{"std.console.set_cursor_visible", "console_set_cursor_visible"},
			{"std.console.width", "console_width"},
			{"std.console.height", "console_height"},
			{"std.io.read_token", "__std_read_token"},
			{"std.io.read_string", "__std_read_string"},
			{"std.io.read_int", "__std_read_int"},
			{"std.io.read_double", "__std_read_double"},
			{"std.io.read_bool", "__std_read_bool"},
			{"std.sqrt", "__std_sqrt"},
			{"std.sin", "__std_sin"},
			{"std.cos", "__std_cos"},
			{"std.tan", "__std_tan"},
			{"std.floor", "__std_floor"},
			{"std.ceil", "__std_ceil"},
			{"std.round", "__std_round"},
			{"std.trunc", "__std_trunc"},
			{"std.pow", "__std_pow"},
			{"std.exp", "__std_exp"},
			{"std.log", "__std_log"},
			{"std.log2", "__std_log2"},
			{"std.asin", "__std_asin"},
			{"std.acos", "__std_acos"},
			{"std.atan2", "__std_atan2"},
			{"std.isfinite", "__std_isfinite"},
			{"std.isnan", "__std_isnan"},
			{"std.pi", "__std_pi"},
			{"std.e", "__std_e"},
			{"std.clamp", "__std_clamp"},
			{"std.clamp_int", "__std_clamp_int"},
			{"std.io.read_file", "__std_read_file"},
			{"std.io.write_file", "__std_write_file"},
			{"std.io.append_file", "__std_append_file"},
			{"std.io.read_line", "__std_read_line"},
			{"std.io.try_read_line", "__std_try_read_line"},
			{"std.io.try_read_token", "__std_try_read_token"},
			{"std.io.read_bytes", "__std_read_bytes"},
			{"std.io.write_bytes", "__std_write_bytes"},
			{"std.io.append_bytes", "__std_append_bytes"},
			{"std.io.pause", "__std_pause"},
			{"std.filesystem.delete_file", "__std_delete_file"},
			{"std.filesystem.exists", "__std_exists"},
			{"std.filesystem.is_file", "__std_is_file"},
			{"std.filesystem.is_directory", "__std_is_directory"},
			{"std.filesystem.remove_dir", "__std_remove_dir"},
			{"std.filesystem.list_dir", "__std_list_dir"},
			{"std.filesystem.mkdir", "__std_mkdir"},
			{"std.filesystem.cwd", "__std_cwd"},
			{"std.filesystem.chdir", "__std_chdir"},
			{"std.filesystem.is_absolute", "__std_path_is_absolute"},
			{"std.filesystem.root_path", "__std_path_root"},
			{"std.filesystem.lexically_normal", "__std_path_normalize"},
			{"std.strings.substring", "__std_substring"},
			{"std.strings.find", "__std_find"},
			{"std.strings.char_code", "__std_char_code"},
			{"std.strings.contains", "__std_contains"},
			{"std.strings.starts_with", "__std_starts_with"},
			{"std.strings.ends_with", "__std_ends_with"},
			{"std.strings.trim_left", "__std_trim_left"},
			{"std.strings.trim_right", "__std_trim_right"},
			{"std.strings.trim", "__std_trim"},
			{"std.strings.split", "__std_split"},
			{"std.strings.join", "__std_join"},
			{"std.strings.to_lower", "__std_to_lower"},
			{"std.strings.to_upper", "__std_to_upper"},
			{"std.strings.replace", "__std_replace"},
			{"std.strings.last_find", "__std_last_find"},
			{"std.strings.repeat", "__std_repeat"},
			{"std.strings.remove_prefix", "__std_remove_prefix"},
			{"std.strings.remove_suffix", "__std_remove_suffix"},
			{"std.filesystem.basename", "__std_basename"},
			{"std.filesystem.dirname", "__std_dirname"},
			{"std.filesystem.join_path", "__std_join_path"},
			{"std.filesystem.extension", "__std_extension"},
			{"std.filesystem.change_extension", "__std_change_extension"},
			{"std.concurrent.channel_send", "__std_channel_send"},
			{"std.concurrent.channel_recv", "__std_channel_recv"},
			{"std.concurrent.channel_close", "__std_channel_close"},
			{"std.concurrent.spawn", "__std_spawn"},
			{"std.concurrent.spawn_task", "__std_spawn_task"},
			{"std.concurrent.join_task", "__std_join_task"},
			{"std.concurrent.cancel_task", "__std_cancel_task"},
			{"std.concurrent.cancel_requested", "__std_cancel_requested"},
			{"std.concurrent.send", "send"},
			{"std.concurrent.recv", "recv"},
			{"std.concurrent.close", "close"},
			{"std.concurrent.make_channel", "__std_make_channel"},
			{"std.concurrent.try_send", "try_send"},
			{"std.concurrent.send_for", "send_for"},
			{"std.concurrent.try_recv", "try_recv"},
			{"std.concurrent.recv_for", "recv_for"},
			{"std.concurrent.is_closed", "is_closed"},
			{"std.concurrent.pending", "pending"},
			{"std.concurrent.capacity", "capacity"},
			{"std.concurrent.when_all", "__std_when_all"},
			{"std.concurrent.cancel_all", "__std_cancel_all"},
			{"std.concurrent.make_mutex", "__std_make_mutex"},
			{"std.concurrent.mutex_lock", "mutex_lock"},
			{"std.concurrent.mutex_try_lock", "mutex_try_lock"},
			{"std.concurrent.mutex_lock_for", "__std_mutex_lock_for"},
			{"std.concurrent.mutex_unlock", "mutex_unlock"},
			{"std.concurrent.make_semaphore", "__std_make_semaphore"},
			{"std.concurrent.semaphore_acquire", "semaphore_acquire"},
			{"std.concurrent.semaphore_try_acquire", "semaphore_try_acquire"},
			{"std.concurrent.semaphore_acquire_for", "__std_semaphore_acquire_for"},
			{"std.concurrent.semaphore_release", "semaphore_release"},
			{"std.concurrent.make_synchronized", "__std_make_synchronized"},
			{"std.concurrent.synchronized_load", "synchronized_load"},
			{"std.concurrent.synchronized_store", "synchronized_store"},
			{"std.concurrent.synchronized_exchange", "synchronized_exchange"},
			{"std.concurrent.synchronized_fetch_add", "synchronized_fetch_add"},
			{"std.algorithm.sort", "algorithm_sort"},
			{"std.algorithm.reverse", "algorithm_reverse"},
			{"std.algorithm.find_index", "algorithm_find_index"},
			{"std.algorithm.count", "algorithm_count"},
			{"std.algorithm.lower_bound", "algorithm_lower_bound"},
			{"std.algorithm.binary_search", "algorithm_binary_search"},
			{"std.algorithm.fill", "algorithm_fill"},
			{"std.algorithm.copy", "algorithm_copy"},
			{"std.algorithm.contains", "algorithm_contains"},
			{"std.algorithm.equal", "algorithm_equal"},
			{"std.algorithm.is_sorted", "algorithm_is_sorted"},
			{"std.algorithm.min_index", "algorithm_min_index"},
			{"std.algorithm.max_index", "algorithm_max_index"},
			{"std.algorithm.rotate", "__std_algorithm_rotate"},
			{"std.algorithm.shuffle", "algorithm_shuffle"},
			{"std.numeric.accumulate", "numeric_accumulate"},
			{"std.numeric.sum", "numeric_sum"},
			{"std.numeric.average", "numeric_average"},
			{"std.numeric.accumulate_as", "numeric_accumulate_as"},
			{"std.numeric.sum_as", "numeric_sum_as"},
			{"std.random.make_engine", "random_make_engine"},
			{"std.random.seed", "random_seed"},
			{"std.random.next_uint", "random_next_uint"},
			{"std.random.next_bounded_uint", "random_next_bounded_uint"},
			{"std.random.next_int", "random_next_int"},
			{"std.random.next_double", "random_next_double"},
			{"std.random.next_double_range", "random_next_double_range"},
			{"std.random.next_bool", "random_next_bool"},
		};

		// These runtime primitives have a clear standard-library owner and therefore
		// must not also pollute the global callable namespace. The qualified spelling
		// remains a zero-cost alias of the same builtin identity.
		constexpr FNameAlias NamespaceOnlyFunctions[] = {
			{"std::memory::native_addresses_available", "__memory_available"},
			{"std::memory::address_of", "__memory_address"},
			{"std::memory::object_address", "__memory_object_address"},
			{"std::memory::object_id", "__memory_object_id"},
			{"std::memory::data_address", "__memory_data_address"},
			{"std::memory::copy", "__memory_copy"},
			{"std::memory::fill", "__memory_fill"},
			{"std::memory::equal", "__memory_equal"},
			{"std::memory::clone", "__memory_clone"},
			{"std::memory::from_array", "__memory_from_array"},
			{"std::memory::to_array", "__memory_to_array"},
			{"std::memory::read", "__memory_read"},
			{"std::memory::write", "__memory_write"},
			{"std::chrono::now", "now"},
			{"std::chrono::steady_now", "steady_now"},
			{"std::this_thread::sleep", "sleep"},
			{"std::environment::getenv", "getenv"},
			{"std::environment::setenv", "setenv"},
			{"std::environment::try_getenv", "try_getenv"},
			{"std::environment::unsetenv", "unsetenv"},
			{"std::console::write", "console_write"},
			{"std::console::error", "console_error"},
			{"std::console::flush", "console_flush"},
			{"std::console::clear", "console_clear"},
			{"std::console::is_interactive", "console_is_interactive"},
			{"std::console::supports_color", "console_supports_color"},
			{"std::console::supports_cursor", "console_supports_cursor"},
			{"std::console::supports_clear", "console_supports_clear"},
			{"std::console::is_output_terminal", "console_is_output_terminal"},
			{"std::console::set_color", "console_set_color"},
			{"std::console::reset_style", "console_reset_style"},
			{"std::console::clear_line", "console_clear_line"},
			{"std::console::move_cursor", "console_move_cursor"},
			{"std::console::set_cursor_visible", "console_set_cursor_visible"},
			{"std::console::width", "console_width"},
			{"std::console::height", "console_height"},
			{"std::io::read_token", "read_token"},
			{"std::io::read_string", "read_string"},
			{"std::io::read_int", "read_int"},
			{"std::io::read_double", "read_double"},
			{"std::io::read_bool", "read_bool"},
			{"std::sqrt", "sqrt"},
			{"std::sin", "sin"},
			{"std::cos", "cos"},
			{"std::tan", "tan"},
			{"std::floor", "floor"},
			{"std::ceil", "ceil"},
			{"std::round", "round"},
			{"std::trunc", "trunc"},
			{"std::pow", "pow"},
			{"std::exp", "exp"},
			{"std::log", "log"},
			{"std::log2", "log2"},
			{"std::asin", "asin"},
			{"std::acos", "acos"},
			{"std::atan2", "atan2"},
			{"std::isfinite", "isfinite"},
			{"std::isnan", "isnan"},
			{"std::pi", "pi"},
			{"std::e", "e"},
			{"std::clamp", "clamp"},
			{"std::clamp_int", "clamp_int"},
			{"std::io::read_file", "read_file"},
			{"std::io::write_file", "write_file"},
			{"std::io::append_file", "append_file"},
			{"std::io::read_line", "read_line"},
			{"std::io::pause", "pause"},
			{"std::filesystem::delete_file", "delete_file"},
			{"std::filesystem::exists", "exists"},
			{"std::filesystem::is_file", "is_file"},
			{"std::filesystem::is_directory", "is_directory"},
			{"std::filesystem::remove_dir", "remove_dir"},
			{"std::filesystem::list_dir", "list_dir"},
			{"std::filesystem::mkdir", "mkdir"},
			{"std::filesystem::cwd", "cwd"},
			{"std::filesystem::chdir", "chdir"},
			{"std::strings::substring", "substring"},
			{"std::strings::find", "find"},
			{"std::strings::char_code", "char_code"},
			{"std::strings::contains", "contains"},
			{"std::strings::starts_with", "starts_with"},
			{"std::strings::ends_with", "ends_with"},
			{"std::strings::trim_left", "trim_left"},
			{"std::strings::trim_right", "trim_right"},
			{"std::strings::trim", "trim"},
			{"std::strings::split", "split"},
			{"std::strings::join", "join"},
			{"std::strings::to_lower", "to_lower"},
			{"std::strings::to_upper", "to_upper"},
			{"std::strings::replace", "replace"},
			{"std::strings::last_find", "last_find"},
			{"std::strings::repeat", "repeat"},
			{"std::strings::remove_prefix", "remove_prefix"},
			{"std::strings::remove_suffix", "remove_suffix"},
			{"std::filesystem::basename", "basename"},
			{"std::filesystem::dirname", "dirname"},
			{"std::filesystem::join_path", "join_path"},
			{"std::filesystem::extension", "extension"},
			{"std::filesystem::change_extension", "change_extension"},
			{"std::concurrent::channel_send", "channel_send"},
			{"std::concurrent::channel_recv", "channel_recv"},
			{"std::concurrent::channel_close", "channel_close"},
			{"std::concurrent::spawn", "spawn"},
			{"std::concurrent::spawn_task", "spawn_task"},
			{"std::concurrent::join_task", "join_task"},
			{"std::concurrent::cancel_task", "__std_cancel_task"},
			{"std::concurrent::cancel_requested", "__std_cancel_requested"},
			{"std::concurrent::make_channel", "__std_make_channel"},
			{"std::concurrent::when_all", "__std_when_all"},
			{"std::concurrent::cancel_all", "__std_cancel_all"},
			{"std::concurrent::make_mutex", "__std_make_mutex"},
			{"std::concurrent::mutex_lock", "mutex_lock"},
			{"std::concurrent::mutex_try_lock", "mutex_try_lock"},
			{"std::concurrent::mutex_lock_for", "__std_mutex_lock_for"},
			{"std::concurrent::mutex_unlock", "mutex_unlock"},
			{"std::concurrent::make_semaphore", "__std_make_semaphore"},
			{"std::concurrent::semaphore_acquire", "semaphore_acquire"},
			{"std::concurrent::semaphore_try_acquire", "semaphore_try_acquire"},
			{"std::concurrent::semaphore_acquire_for", "__std_semaphore_acquire_for"},
			{"std::concurrent::semaphore_release", "semaphore_release"},
			{"std::concurrent::make_synchronized", "__std_make_synchronized"},
			{"std::concurrent::synchronized_load", "synchronized_load"},
			{"std::concurrent::synchronized_store", "synchronized_store"},
			{"std::concurrent::synchronized_exchange", "synchronized_exchange"},
			{"std::concurrent::synchronized_fetch_add", "synchronized_fetch_add"},
			{"std::algorithm::sort", "algorithm_sort"},
			{"std::algorithm::reverse", "algorithm_reverse"},
			{"std::algorithm::find_index", "algorithm_find_index"},
			{"std::algorithm::count", "algorithm_count"},
			{"std::algorithm::lower_bound", "algorithm_lower_bound"},
			{"std::algorithm::binary_search", "algorithm_binary_search"},
			{"std::algorithm::fill", "algorithm_fill"},
			{"std::algorithm::copy", "algorithm_copy"},
			{"std::algorithm::contains", "algorithm_contains"},
			{"std::algorithm::equal", "algorithm_equal"},
			{"std::algorithm::is_sorted", "algorithm_is_sorted"},
			{"std::algorithm::min_index", "algorithm_min_index"},
			{"std::algorithm::max_index", "algorithm_max_index"},
			{"std::algorithm::rotate", "__std_algorithm_rotate"},
			{"std::algorithm::shuffle", "algorithm_shuffle"},
			{"std::numeric::accumulate", "numeric_accumulate"},
			{"std::numeric::sum", "numeric_sum"},
			{"std::numeric::average", "numeric_average"},
			{"std::random::make_engine", "random_make_engine"},
			{"std::random::seed", "random_seed"},
			{"std::random::next_uint", "random_next_uint"},
			{"std::random::next_bounded_uint", "random_next_bounded_uint"},
			{"std::random::next_int", "random_next_int"},
			{"std::random::next_double", "random_next_double"},
			{"std::random::next_double_range", "random_next_double_range"},
			{"std::random::next_bool", "random_next_bool"},
		};

		bool QualifiedNameEquals(
			const std::string_view Written,
			const std::string_view Normalized)
		{
			std::size_t WrittenIndex = 0;
			for (const char Expected : Normalized)
			{
				if (Expected == '.')
				{
					if (WrittenIndex >= Written.size())
					{
						return false;
					}
					if (Written[WrittenIndex] == '.')
					{
						++WrittenIndex;
						continue;
					}
					if (Written[WrittenIndex] == ':'
						&& WrittenIndex + 1 < Written.size()
						&& Written[WrittenIndex + 1] == ':')
					{
						WrittenIndex += 2;
						continue;
					}
					return false;
				}
				if (WrittenIndex >= Written.size() || Written[WrittenIndex] != Expected)
				{
					return false;
				}
				++WrittenIndex;
			}
			return WrittenIndex == Written.size();
		}

		template <std::size_t Count>
		std::string_view Resolve(
			const std::string_view Name,
			const FNameAlias (&Aliases)[Count])
		{
			if (Name.size() < 5 || Name[0] != 's' || Name[1] != 't' || Name[2] != 'd'
				|| (Name[3] != '.'
					&& !(Name[3] == ':' && Name[4] == ':')))
			{
				return {};
			}
			for (const FNameAlias& Alias : Aliases)
			{
				if (QualifiedNameEquals(Name, Alias.QualifiedName))
				{
					return Alias.CanonicalName;
				}
			}
			return {};
		}

		std::string NormalizeNamespaceSpelling(const std::string_view Name)
		{
			std::string Normalized;
			Normalized.reserve(Name.size());
			for (std::size_t Index = 0; Index < Name.size(); ++Index)
			{
				if (Name[Index] == ':' && Index + 1 < Name.size()
					&& Name[Index + 1] == ':')
				{
					Normalized.push_back('.');
					++Index;
				}
				else
				{
					Normalized.push_back(Name[Index]);
				}
			}
			return Normalized;
		}

		bool NamespacePrefixMatches(
			const std::string_view NormalizedName,
			const std::string_view QualifiedSymbol)
		{
			if (NormalizedName.empty()
				|| NormalizedName.size() >= QualifiedSymbol.size())
			{
				return false;
			}
			if (QualifiedSymbol.compare(0, NormalizedName.size(), NormalizedName) != 0)
			{
				return false;
			}
			return QualifiedSymbol[NormalizedName.size()] == '.';
		}
	}

	std::string_view ResolveTypeName(const std::string_view QualifiedName)
	{
		if (QualifiedName == SharedAlias.QualifiedName) return SharedAlias.CanonicalName;
		return Resolve(QualifiedName, TypeAliases);
	}

	std::string_view ResolveFunctionName(const std::string_view QualifiedName)
	{
		return Resolve(QualifiedName, FunctionAliases);
	}

	std::string_view RequiredNamespaceForFunction(const std::string_view CanonicalName)
	{
		for (const FNameAlias& Alias : NamespaceOnlyFunctions)
		{
			if (Alias.CanonicalName == CanonicalName)
			{
				return Alias.QualifiedName;
			}
		}
		return {};
	}

	bool IsNamespace(const std::string_view Name)
	{
		const std::string Normalized = NormalizeNamespaceSpelling(Name);
		if (Normalized == "std")
		{
			return true;
		}
		for (const FNameAlias& Alias : TypeAliases)
		{
			if (NamespacePrefixMatches(Normalized, Alias.QualifiedName))
			{
				return true;
			}
		}
		for (const FNameAlias& Alias : FunctionAliases)
		{
			if (NamespacePrefixMatches(Normalized, Alias.QualifiedName))
			{
				return true;
			}
		}
		return false;
	}

	bool IsReservedDeclarationNamespace(const std::string_view QualifiedName)
	{
		const std::string Normalized = NormalizeNamespaceSpelling(QualifiedName);
		return Normalized == "std" || Normalized.rfind("std.", 0) == 0;
	}

	const FNameAlias* GetTypeAliases(std::size_t& OutCount)
	{
		OutCount = sizeof(TypeAliases) / sizeof(TypeAliases[0]);
		return TypeAliases;
	}

	const FNameAlias* GetFunctionAliases(std::size_t& OutCount)
	{
		OutCount = sizeof(FunctionAliases) / sizeof(FunctionAliases[0]);
		return FunctionAliases;
	}
}
