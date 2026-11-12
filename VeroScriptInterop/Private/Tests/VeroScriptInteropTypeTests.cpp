// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>

namespace
{
	vs_interop_string_view View(const char* Text)
	{
		return {Text, static_cast<std::uint64_t>(std::strlen(Text))};
	}

	struct FValue
	{
		FValue()
		{
			(void)vs_interop_value_create(&Handle);
		}

		~FValue()
		{
			vs_interop_value_destroy(Handle);
		}

		FValue(const FValue&) = delete;

		FValue& operator=(const FValue&) = delete;

		vs_interop_value* Handle = nullptr;
	};

	constexpr std::int64_t SignedValue = INT64_C(-5000000001);
	constexpr std::uint64_t UnsignedValue = UINT64_C(15000000000000000001);

	bool SetScalar(vs_interop_value* Value, const vs_interop_value_kind Kind)
	{
		vs_interop_status Status = VS_INTEROP_STATUS_INVALID_ARGUMENT;
		switch (Kind)
		{
		case VS_INTEROP_VALUE_BOOL:
			{
				Status = vs_interop_value_set_bool(Value, 1);
				break;
			}
		case VS_INTEROP_VALUE_INT8:
			{
				Status = vs_interop_value_set_int8(Value, -42);
				break;
			}
		case VS_INTEROP_VALUE_UINT8:
			{
				Status = vs_interop_value_set_uint8(Value, 242);
				break;
			}
		case VS_INTEROP_VALUE_INT16:
			{
				Status = vs_interop_value_set_int16(Value, -1234);
				break;
			}
		case VS_INTEROP_VALUE_UINT16:
			{
				Status = vs_interop_value_set_uint16(Value, 54321);
				break;
			}
		case VS_INTEROP_VALUE_INT32:
			{
				Status = vs_interop_value_set_int32(Value, -123456);
				break;
			}
		case VS_INTEROP_VALUE_UINT32:
			{
				Status = vs_interop_value_set_uint32(Value, 4000000001u);
				break;
			}
		case VS_INTEROP_VALUE_INT64:
			{
				Status = vs_interop_value_set_int64(Value, SignedValue);
				break;
			}
		case VS_INTEROP_VALUE_UINT64:
			{
				Status = vs_interop_value_set_uint64(Value, UnsignedValue);
				break;
			}
		case VS_INTEROP_VALUE_FLOAT:
			{
				Status = vs_interop_value_set_float(Value, 1.25f);
				break;
			}
		case VS_INTEROP_VALUE_DOUBLE:
			{
				Status = vs_interop_value_set_double(Value, 2.5);
				break;
			}
		case VS_INTEROP_VALUE_STRING:
			{
				Status = vs_interop_value_set_string(Value, View("value"));
				break;
			}
		case VS_INTEROP_VALUE_WSTRING:
			{
				const std::uint16_t Text[] = {0x4e2d, 0x6587};
				Status = vs_interop_value_set_wstring(Value, Text, 2);
				break;
			}
		default:
			{
				break;
			}
		}
		return Status == VS_INTEROP_STATUS_OK;
	}

	bool MatchesScalar(const vs_interop_value* Value, const vs_interop_value_kind Kind)
	{
		if (vs_interop_value_get_kind(Value) != Kind)
		{
			return false;
		}
		if (Kind == VS_INTEROP_VALUE_INT64)
		{
			std::int64_t Number = 0;
			return vs_interop_value_get_int64(Value, &Number) == VS_INTEROP_STATUS_OK && Number == SignedValue;
		}
		if (Kind == VS_INTEROP_VALUE_UINT64)
		{
			std::uint64_t Number = 0;
			return vs_interop_value_get_uint64(Value, &Number) == VS_INTEROP_STATUS_OK && Number == UnsignedValue;
		}
		return true;
	}

	bool TestScalarContainers(const char* TypeName, const vs_interop_value_kind Kind)
	{
		FValue Scalar, Wrong, Null, Container, Out, Key;
		const auto WrongKind = Kind == VS_INTEROP_VALUE_UINT64 ? VS_INTEROP_VALUE_UINT32
		    : Kind == VS_INTEROP_VALUE_UINT32                  ? VS_INTEROP_VALUE_UINT64
		    : Kind == VS_INTEROP_VALUE_INT32                   ? VS_INTEROP_VALUE_INT64
		                                                       : VS_INTEROP_VALUE_INT32;
		if (!SetScalar(Scalar.Handle, Kind) || !SetScalar(Wrong.Handle, WrongKind) || vs_interop_value_set_null(Null.Handle) != VS_INTEROP_STATUS_OK || vs_interop_value_set_string(Key.Handle, View("key")) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		const vs_interop_value* Elements[] = {Scalar.Handle};
		for (const vs_interop_value_kind SequenceKind :
		    {VS_INTEROP_VALUE_ARRAY, VS_INTEROP_VALUE_QUEUE, VS_INTEROP_VALUE_STACK, VS_INTEROP_VALUE_DEQUE})
		{
			if (vs_interop_value_create_sequence(Container.Handle, SequenceKind, View(TypeName), Elements, 1) != VS_INTEROP_STATUS_OK)
			{
				return false;
			}
			for (const auto* Invalid : {Null.Handle, Wrong.Handle})
			{
				// A valid prefix must not be committed when a later element fails.
				const vs_interop_value* InvalidElements[] = {Scalar.Handle, Invalid};
				if (vs_interop_value_create_sequence(Container.Handle, SequenceKind,
				        View(TypeName), InvalidElements, 2) != VS_INTEROP_STATUS_TYPE_MISMATCH)
				{
					return false;
				}
				std::uint64_t Count = 0;
				if (vs_interop_value_sequence_count(Container.Handle, &Count) != VS_INTEROP_STATUS_OK || Count != 1 || vs_interop_value_sequence_get(Container.Handle, 0, Out.Handle) != VS_INTEROP_STATUS_OK || !MatchesScalar(Out.Handle, Kind))
				{
					return false;
				}
			}
		}
		const vs_interop_value* Keys[] = {Key.Handle, Key.Handle};
		for (const std::uint8_t Ordered : {std::uint8_t{0}, std::uint8_t{1}})
		{
			if (vs_interop_value_create_map(Container.Handle, View("string"), View(TypeName),
			        Ordered, Keys, Elements, 1) != VS_INTEROP_STATUS_OK)
			{
				return false;
			}
			for (const auto* Invalid : {Null.Handle, Wrong.Handle})
			{
				const vs_interop_value* InvalidValues[] = {Scalar.Handle, Invalid};
				if (vs_interop_value_create_map(Container.Handle, View("string"), View(TypeName),
				        Ordered, Keys, InvalidValues, 2) != VS_INTEROP_STATUS_TYPE_MISMATCH)
				{
					return false;
				}
				std::uint64_t Count = 0;
				std::uint8_t Found = 0;
				if (vs_interop_value_map_count(Container.Handle, &Count) != VS_INTEROP_STATUS_OK || Count != 1 || vs_interop_value_map_get(Container.Handle, Key.Handle, Out.Handle, &Found) != VS_INTEROP_STATUS_OK || !Found || !MatchesScalar(Out.Handle, Kind))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool TestNullableObjects()
	{
		FValue Null, Container, Out;
		if (vs_interop_value_set_null(Null.Handle) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		const vs_interop_value* Elements[] = {Null.Handle};
		for (const auto Kind :
		    {VS_INTEROP_VALUE_ARRAY, VS_INTEROP_VALUE_QUEUE, VS_INTEROP_VALUE_STACK, VS_INTEROP_VALUE_DEQUE})
		{
			if (vs_interop_value_create_sequence(Container.Handle, Kind, View("Widget"), Elements, 1) != VS_INTEROP_STATUS_OK || vs_interop_value_sequence_get(Container.Handle, 0, Out.Handle) != VS_INTEROP_STATUS_OK || vs_interop_value_get_kind(Out.Handle) != VS_INTEROP_VALUE_NULL)
			{
				return false;
			}
		}
		return true;
	}

	bool TestScriptObjectContainerSubtypes()
	{
		vs_interop_session* Raw = nullptr;
		if (vs_interop_session_create(nullptr, &Raw) != VS_INTEROP_STATUS_OK) return false;
		const std::unique_ptr<vs_interop_session, decltype(&vs_interop_session_destroy)>
		    Session(Raw, vs_interop_session_destroy);
		FValue Child, Other, Container, Element, Result, Key;
		vs_interop_run_result Run{sizeof(Run)};
		const auto ReadArray = [](vs_interop_call_context* Context, vs_interop_value* Output,
		                           void* UserData) -> vs_interop_callback_result
		{
			// Reuse the borrowed return slot for intermediate script values. Its
			// NativeContext identifies the executing generation and VM metadata.
			if (vs_interop_call_context_invoke_function(Context, View("make_child"), nullptr, 0, Output) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			const auto* Type = static_cast<const char*>(UserData);
			const vs_interop_value* Values[] = {Output};
			if (vs_interop_value_create_array(Output, View(Type), Values, 1) != VS_INTEROP_STATUS_OK)
			{
				return VS_INTEROP_CALLBACK_ERROR;
			}
			vs_interop_value* Arguments[] = {Output};
			return vs_interop_call_context_invoke_function(Context,
			           View(std::strcmp(Type, "Base") == 0 ? "read_base" : "read_interface"),
			           Arguments, 1, Output) == VS_INTEROP_STATUS_OK
			    ? VS_INTEROP_CALLBACK_OK
			    : VS_INTEROP_CALLBACK_ERROR;
		};
		vs_interop_native_type_desc Return{sizeof(Return)};
		Return.type = VS_INTEROP_NATIVE_INT32;
		if (vs_interop_session_register_native(Raw, View("native_base_read"), &Return, nullptr, 0,
		        ReadArray, const_cast<char*>("Base")) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		if (vs_interop_session_register_native(Raw, View("native_interface_read"), &Return, nullptr, 0,
		        ReadArray, const_cast<char*>("IRead")) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		if (vs_interop_session_load_source(Raw, View("export interface IRead { virtual int read()=0; }; "
		                                             "export interface IChildRead:IRead {}; "
		                                             "export class Base:public IChildRead { public:virtual int read(){return 7;} }; "
		                                             "export class Child:public Base {}; export class Other {}; "
		                                             "Child make_child(){return Child();} "
		                                             "int read_base(array<Base> values){return values[0].read();} "
		                                             "int read_interface(array<IRead> values){return values[0].read();} "
		                                             "int read_child_interface(array<IChildRead> values){return values[0].read();} "
		                                             "int read_native_subtypes(){return native_base_read()+native_interface_read();}"),
		        View("interop-container-subtypes.vs"), &Run) != VS_INTEROP_STATUS_OK ||
		    !Run.success || vs_interop_session_construct_object(Raw, View("Child"), nullptr, 0, Child.Handle, &Run) != VS_INTEROP_STATUS_OK || !Run.success || vs_interop_session_construct_object(Raw, View("Other"), nullptr, 0, Other.Handle, &Run) != VS_INTEROP_STATUS_OK || !Run.success || vs_interop_value_set_int32(Key.Handle, 1) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		std::int32_t NativeNumber = 0;
		if (vs_interop_session_call_function(Raw, View("read_native_subtypes"), nullptr, 0,
		        Result.Handle, &Run) != VS_INTEROP_STATUS_OK ||
		    !Run.success || vs_interop_value_get_int32(Result.Handle, &NativeNumber) != VS_INTEROP_STATUS_OK || NativeNumber != 14)
		{
			return false;
		}
		const vs_interop_value* Values[] = {Child.Handle};
		const vs_interop_value* WrongValues[] = {Other.Handle};
		const vs_interop_value* Keys[] = {Key.Handle};
		for (const char* Type : {"Base", "IRead", "IChildRead"})
		{
			for (const auto Kind : {VS_INTEROP_VALUE_ARRAY, VS_INTEROP_VALUE_QUEUE,
			         VS_INTEROP_VALUE_STACK, VS_INTEROP_VALUE_DEQUE})
			{
				std::uint64_t Count = 0;
				if (vs_interop_value_create_sequence(Container.Handle, Kind, View(Type), Values, 1) != VS_INTEROP_STATUS_OK || vs_interop_value_sequence_get(Container.Handle, 0, Element.Handle) != VS_INTEROP_STATUS_OK || vs_interop_value_create_sequence(Container.Handle, Kind, View(Type), WrongValues, 1) != VS_INTEROP_STATUS_TYPE_MISMATCH || vs_interop_value_sequence_count(Container.Handle, &Count) != VS_INTEROP_STATUS_OK || Count != 1)
				{
					return false;
				}
				vs_interop_string_view ElementType{};
				if (vs_interop_value_sequence_get(Container.Handle, 0, Element.Handle) != VS_INTEROP_STATUS_OK || vs_interop_value_get_type_name(Element.Handle, &ElementType) != VS_INTEROP_STATUS_OK || ElementType.size != 5 || std::memcmp(ElementType.data, "Child", 5) != 0)
				{
					return false;
				}
				if (Kind == VS_INTEROP_VALUE_ARRAY)
				{
					vs_interop_value* Arguments[] = {Container.Handle};
					std::int32_t Number = 0;
					if (vs_interop_session_call_function(Raw,
					        View(std::strcmp(Type, "Base") == 0       ? "read_base"
					                : std::strcmp(Type, "IRead") == 0 ? "read_interface"
					                                                  : "read_child_interface"),
					        Arguments, 1, Result.Handle, &Run) != VS_INTEROP_STATUS_OK ||
					    !Run.success || vs_interop_value_get_int32(Result.Handle, &Number) != VS_INTEROP_STATUS_OK || Number != 7)
					{
						return false;
					}
				}
			}
			for (const auto Kind : {VS_INTEROP_VALUE_MAP, VS_INTEROP_VALUE_ORDERED_MAP,
			         VS_INTEROP_VALUE_MULTI_MAP, VS_INTEROP_VALUE_ORDERED_MULTI_MAP})
			{
				std::uint64_t Count = 0;
				if (vs_interop_value_create_map_kind(Container.Handle, Kind, View("int"), View(Type),
				        Keys, Values, 1) != VS_INTEROP_STATUS_OK ||
				    vs_interop_value_create_map_kind(Container.Handle, Kind, View("int"), View(Type),
				        Keys, WrongValues, 1) != VS_INTEROP_STATUS_TYPE_MISMATCH ||
				    vs_interop_value_map_count(Container.Handle, &Count) != VS_INTEROP_STATUS_OK || Count != 1)
				{
					return false;
				}
				std::uint8_t Found = 0;
				if (vs_interop_value_map_get(Container.Handle, Key.Handle, Element.Handle, &Found) != VS_INTEROP_STATUS_OK || !Found)
				{
					return false;
				}
				if ((Kind == VS_INTEROP_VALUE_MULTI_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MULTI_MAP) && vs_interop_value_sequence_get(Element.Handle, 0, Element.Handle) != VS_INTEROP_STATUS_OK)
				{
					return false;
				}
				vs_interop_string_view ElementType{};
				if (vs_interop_value_get_type_name(Element.Handle, &ElementType) != VS_INTEROP_STATUS_OK || ElementType.size != 5 || std::memcmp(ElementType.data, "Child", 5) != 0)
				{
					return false;
				}
			}
		}
		// Generic containers remain invariant: accepting Child in a Base slot does
		// not permit an array<Child> where an array<Base> value was declared.
		if (vs_interop_value_create_array(Element.Handle, View("Child"), Values, 1) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		const vs_interop_value* Nested[] = {Element.Handle};
		return vs_interop_value_create_array(Container.Handle, View("array<Base>"), Nested, 1) == VS_INTEROP_STATUS_TYPE_MISMATCH;
	}

	struct FCharacterEnumeration
	{
		bool bSawKey = false;
		bool bSawElement = false;
	};

	vs_interop_callback_result VS_INTEROP_CALL VisitCharacterMapEntry(
	    const vs_interop_value* Key,
	    const vs_interop_value* Value,
	    void* UserData)
	{
		auto& State = *static_cast<FCharacterEnumeration*>(UserData);
		std::int8_t Character = 0;
		std::int32_t Mapped = 0;
		if (vs_interop_value_get_kind(Key) != VS_INTEROP_VALUE_INT8 || vs_interop_value_get_int8(Key, &Character) != VS_INTEROP_STATUS_OK || vs_interop_value_get_int32(Value, &Mapped) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		State.bSawKey = Character == INT8_C(-1) && Mapped == 7;
		return VS_INTEROP_CALLBACK_OK;
	}

	vs_interop_callback_result VS_INTEROP_CALL VisitCharacterSetElement(
	    const vs_interop_value* Element,
	    void* UserData)
	{
		auto& State = *static_cast<FCharacterEnumeration*>(UserData);
		std::int8_t Character = 0;
		if (vs_interop_value_get_kind(Element) != VS_INTEROP_VALUE_INT8 || vs_interop_value_get_int8(Element, &Character) != VS_INTEROP_STATUS_OK)
		{
			return VS_INTEROP_CALLBACK_ERROR;
		}
		State.bSawElement = Character == INT8_C(-1);
		return VS_INTEROP_CALLBACK_OK;
	}

	bool TestCharacterKeys()
	{
		FValue Key, Value, Map, Out, Set;
		if (vs_interop_value_set_int8(Key.Handle, INT8_C(-1)) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(Value.Handle, 7) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		const vs_interop_value* Keys[] = {Key.Handle};
		const vs_interop_value* Values[] = {Value.Handle};
		for (const std::uint8_t Ordered : {std::uint8_t{0}, std::uint8_t{1}})
		{
			std::uint8_t Found = 0;
			std::int32_t Mapped = 0;
			FCharacterEnumeration Enumeration;
			if (vs_interop_value_create_map(
			        Map.Handle, View("char"), View("int"), Ordered, Keys, Values, 1) != VS_INTEROP_STATUS_OK ||
			    vs_interop_value_map_get(
			        Map.Handle, Key.Handle, Out.Handle, &Found) != VS_INTEROP_STATUS_OK ||
			    !Found || vs_interop_value_get_int32(Out.Handle, &Mapped) != VS_INTEROP_STATUS_OK || Mapped != 7 || vs_interop_value_map_enumerate(Map.Handle, VisitCharacterMapEntry, &Enumeration) != VS_INTEROP_STATUS_OK || !Enumeration.bSawKey)
			{
				return false;
			}
		}

		std::uint8_t Contains = 0;
		FCharacterEnumeration Enumeration;
		return vs_interop_value_create_set(Set.Handle, View("char"), Keys, 1) == VS_INTEROP_STATUS_OK && vs_interop_value_set_contains(Set.Handle, Key.Handle, &Contains) == VS_INTEROP_STATUS_OK && Contains && vs_interop_value_set_enumerate(Set.Handle, VisitCharacterSetElement, &Enumeration) == VS_INTEROP_STATUS_OK && Enumeration.bSawElement;
	}

	bool TestMapAndSetKeyTypes()
	{
		FValue Container, Element, Mapped;
		if (!Container.Handle || !Element.Handle || !Mapped.Handle || vs_interop_value_set_int32(Element.Handle, 1) != VS_INTEROP_STATUS_OK || vs_interop_value_set_int32(Mapped.Handle, 7) != VS_INTEROP_STATUS_OK)
		{
			return false;
		}
		const vs_interop_value* Keys[] = {Element.Handle};
		const vs_interop_value* Values[] = {Mapped.Handle};
		const auto Create = [&](const vs_interop_value_kind Kind, const char* Type, std::uint64_t Count)
		{
			if (Kind == VS_INTEROP_VALUE_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MAP || Kind == VS_INTEROP_VALUE_MULTI_MAP || Kind == VS_INTEROP_VALUE_ORDERED_MULTI_MAP)
			{
				return vs_interop_value_create_map_kind(Container.Handle, Kind, View(Type), View("int"),
				    Count ? Keys : nullptr, Count ? Values : nullptr, Count);
			}
			return vs_interop_value_create_set_kind(Container.Handle, Kind, View(Type),
			    Count ? Keys : nullptr, Count);
		};
		for (const auto Kind : {VS_INTEROP_VALUE_MAP, VS_INTEROP_VALUE_ORDERED_MAP,
		         VS_INTEROP_VALUE_MULTI_MAP, VS_INTEROP_VALUE_ORDERED_MULTI_MAP,
		         VS_INTEROP_VALUE_SET, VS_INTEROP_VALUE_MULTI_SET, VS_INTEROP_VALUE_ORDERED_MULTI_SET})
		{
			for (const char* Type : {"bool", "unsigned char", "uint8", "short", "int16",
			         "unsigned short", "uint16", "float", "double", "void", "array<int>", "int[2]",
			         "map<int,int>", "ordered_map<int,int>", "multimap<int,int>", "ordered_multimap<int,int>",
			         "set<int>", "multiset<int>", "ordered_multiset<int>", "queue<int>", "priority_queue<int>",
			         "stack<int>", "deque<int>", "channel<int>", "fn()->int", "ref int", "const ref int"})
			{
				// Invalid declarations must be rejected even when no elements can
				// trigger payload validation. Neither path may replace the old value.
				for (const std::uint64_t Count : {UINT64_C(0), UINT64_C(1)})
				{
					std::int32_t Preserved = 0;
					if (vs_interop_value_set_int32(Container.Handle, 42) != VS_INTEROP_STATUS_OK || Create(Kind, Type, Count) != VS_INTEROP_STATUS_INVALID_ARGUMENT || vs_interop_value_get_int32(Container.Handle, &Preserved) != VS_INTEROP_STATUS_OK || Preserved != 42)
					{
						std::cerr << "FAIL: rejected key declaration " << Type << ", kind " << Kind
						          << ", count " << Count << '\n';
						return false;
					}
				}
			}
			// Named enum/host identities (including instantiated host types) are
			// resolved by the consuming session; construction must retain them.
			for (const char* Type : {"char", "int8", "int", "int32", "unsigned", "uint32",
			         "long", "int64", "unsigned long", "uint64", "string", "wstring", "Color", "demo::Value<int>"})
			{
				if (Create(Kind, Type, 0) != VS_INTEROP_STATUS_OK || vs_interop_value_get_kind(Container.Handle) != Kind)
				{
					std::cerr << "FAIL: accepted key declaration " << Type << ", kind " << Kind << '\n';
					return false;
				}
			}
		}
		return true;
	}
}

int main()
{
	const struct
	{
		const char* Name;
		vs_interop_value_kind Kind;
	} Cases[] = {
	    {"bool", VS_INTEROP_VALUE_BOOL},
	    {"char", VS_INTEROP_VALUE_INT8}, {"int8", VS_INTEROP_VALUE_INT8},
	    {"unsigned char", VS_INTEROP_VALUE_UINT8}, {"uint8", VS_INTEROP_VALUE_UINT8},
	    {"short", VS_INTEROP_VALUE_INT16}, {"int16", VS_INTEROP_VALUE_INT16},
	    {"unsigned short", VS_INTEROP_VALUE_UINT16}, {"uint16", VS_INTEROP_VALUE_UINT16},
	    {"int", VS_INTEROP_VALUE_INT32}, {"int32", VS_INTEROP_VALUE_INT32},
	    {"unsigned", VS_INTEROP_VALUE_UINT32}, {"unsigned int", VS_INTEROP_VALUE_UINT32},
	    {"uint32", VS_INTEROP_VALUE_UINT32},
	    {"long", VS_INTEROP_VALUE_INT64}, {"int64", VS_INTEROP_VALUE_INT64},
	    {"unsigned long", VS_INTEROP_VALUE_UINT64}, {"uint64", VS_INTEROP_VALUE_UINT64},
	    {"float", VS_INTEROP_VALUE_FLOAT}, {"double", VS_INTEROP_VALUE_DOUBLE},
	    {"string", VS_INTEROP_VALUE_STRING}, {"wstring", VS_INTEROP_VALUE_WSTRING}};
	bool Success = true;
	for (const auto& Case : Cases)
	{
		if (!TestScalarContainers(Case.Name, Case.Kind))
		{
			std::cerr << "FAIL: scalar container contract for " << Case.Name << '\n';
			Success = false;
		}
	}
	if (!TestNullableObjects())
	{
		std::cerr << "FAIL: nullable object elements remain supported\n";
		Success = false;
	}
	if (!TestScriptObjectContainerSubtypes())
	{
		std::cerr << "FAIL: script object container subtype contract\n";
		Success = false;
	}
	if (!TestCharacterKeys())
	{
		std::cerr << "FAIL: char keys use stable INT8 semantics\n";
		Success = false;
	}
	if (!TestMapAndSetKeyTypes())
	{
		std::cerr << "FAIL: map/set key declarations and rejected-update preservation\n";
		Success = false;
	}
	if (!Success)
	{
		return 1;
	}
	std::cout << "VeroScriptInteropTypeTests passed\n";
	return 0;
}
