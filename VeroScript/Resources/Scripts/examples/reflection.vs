// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// reflection.vs - immutable script declaration metadata.

@ setting(primary) export const int ReflectionVersion = 1;

@ utility(sample) export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@ display(primary) Ready,
	Archived
};

struct BaseRecord
{
	int read_id() const
	{
		return id;
	}

	int id;
};

@ entity(sample) struct Record : BaseRecord
{
	@ call(sample) virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}

	@ data(display) mutable string label;
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}", info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}", field.name(), field.declared_type(), field.runtime_type(), field.is_static(), field.is_mutable());
		if (field.has_annotation("data"))
		{
			print("  carries @data");
		}
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}", method.name(), method.return_type(), method.parameter_count(), method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}", module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info functionMeta = function_info(i);
		print("function {}: returns={}, exported={}", functionMeta.name(), functionMeta.return_type(), functionMeta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info globalMeta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}", globalMeta.name(), globalMeta.declared_type(), globalMeta.runtime_type(), globalMeta.is_const());
	}

	type<RecordState> stateType = type<RecordState>();
	enum_member_info<RecordState> state = enum_member_info<RecordState>(stateType.qualified_name, 0);
	print("enum member {}={} annotated={}", state.name(), state.value(), state.has_annotation("display"));

	return 0;
}
