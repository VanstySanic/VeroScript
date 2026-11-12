// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJson.h"
#include "VeroScriptJsonSchema.h"
#include "VeroScriptJsonMapping.h"
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
	std::int64_t AllocationBudget = -1;
	void* Allocate(std::size_t Size)
	{
		if (AllocationBudget == 0)
		{
			throw std::bad_alloc();
		}
		if (AllocationBudget > 0)
		{
			--AllocationBudget;
		}
		if (void* Block = std::malloc(Size ? Size : 1))
		{
			return Block;
		}
		throw std::bad_alloc();
	}

	struct FFailureScope
	{
		explicit FFailureScope(std::int64_t Budget)
		{
			AllocationBudget = Budget;
		}
		~FFailureScope()
		{
			AllocationBudget = -1;
		}
	};

	void Check(bool Value)
	{
		if (!Value)
		{
			throw std::logic_error("Allocation failure contract violated");
		}
	}

	struct FReenterRegistry
	{
		FReenterRegistry(VeroScript::Json::FMappingRegistry& InRegistry, bool& InEnabled, bool& InRegistered)
			: Registry(InRegistry), Enabled(InEnabled), Registered(InRegistered)
		{
		}
		~FReenterRegistry()
		{
			if (!Enabled)
			{
				return;
			}
			// The original insertion fails, but the foreign destructor can use another allocator/budget.
			AllocationBudget = -1;
			VeroScript::Json::FTypeAdapter Other;
			Other.Name = "reentered";
			Other.Encode = [](const void*, auto&, auto&)
			{
				return true;
			};
			VeroScript::Json::FError Error;
			Registered = Registry.Register(typeid(int), std::move(Other), Error);
		}
		VeroScript::Json::FMappingRegistry& Registry;
		bool& Enabled;
		bool& Registered;
	};
	struct FPolyBase
	{
		virtual ~FPolyBase() = default;
	};
	struct FPolyChild : FPolyBase
	{
		int Count = 17;
	};
	void PolymorphicFailures()
	{
		using namespace VeroScript::Json;
		FMappingRegistry Registry;
		FError Error;
		TObjectMapping<FPolyChild> Child("allocation-child");
		Child.Field("count", &FPolyChild::Count);
		TPolymorphicMapping<FPolyBase> Table("allocation-base");
		Table.Add<FPolyChild>("child");
		Check(Child.Register(Registry, Error) && Table.Register(Registry, Error));
		std::shared_ptr<FPolyBase> Input = std::make_shared<FPolyChild>();
		FJsonValue Wire;
		Check(TryEncode(Input, Wire, Error, Registry));
		bool Registered = false, Encoded = false, Decoded = false, Schemed = false;
		for (std::int64_t Budget = 0; Budget < 512; ++Budget)
		{
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = Table.Register(Registry, Error, true);
			}
			Registered |= Success;
			Check(Success || Error.Code == EError::Allocation);
			FJsonValue Output(std::int64_t{7});
			{
				FFailureScope Scope(Budget);
				Success = TryEncode(Input, Output, Error, Registry);
			}
			Encoded |= Success;
			Check(Success || (Error.Code == EError::Allocation && Output.Kind() == EKind::Number));
			std::shared_ptr<FPolyBase> Copy = Input;
			{
				FFailureScope Scope(Budget);
				Success = TryDecode(Wire, Copy, Error, Registry);
			}
			Decoded |= Success;
			Check(Success ? Copy && Copy != Input : Error.Code == EError::Allocation && Copy == Input);
			Output = FJsonValue(std::int64_t{7});
			{
				FFailureScope Scope(Budget);
				Success = TryMappingSchema<FPolyBase>(EMappingDirection::Input, Output, Error, Registry);
			}
			Schemed |= Success;
			Check(Success || (Error.Code == EError::Allocation && Output.Kind() == EKind::Number));
		}
		Check(Registered && Encoded && Decoded && Schemed);
	}

	void RegistryInsertionFailure()
	{
		using namespace VeroScript::Json;
		bool Failed = false, Inserted = false;
		for (std::int64_t Budget = 0; Budget < 16; ++Budget)
		{
			bool Enabled = true, Reentered = false;
			FMappingRegistry Registry;
			FTypeAdapter Adapter;
			Adapter.Name = "owned";
			Adapter.Encode = [](const void*, auto&, auto&)
			{
				return true;
			};
			Adapter.Owner = std::make_shared<FReenterRegistry>(Registry, Enabled, Reentered);
			FError Error;
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = Registry.Register(typeid(FReenterRegistry), std::move(Adapter), Error);
			}
			Enabled = false;
			Inserted |= Success;
			Failed |= !Success;
			Check(Success || (Error.Code == EError::Allocation && Reentered));
		}
		Check(Failed && Inserted);
	}
} // namespace

void* operator new(std::size_t Size)
{
	return Allocate(Size);
}
void* operator new[](std::size_t Size)
{
	return Allocate(Size);
}
void operator delete(void* Block) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block) noexcept
{
	std::free(Block);
}
void operator delete(void* Block, std::size_t) noexcept
{
	std::free(Block);
}
void operator delete[](void* Block, std::size_t) noexcept
{
	std::free(Block);
}

int main()
{
	using namespace VeroScript::Json;
	#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
	// The MSVC iterator-debug STL allocates even from noexcept std::string
	// construction. Global-new failure injection terminates inside that STL.
	std::cout << "Global allocation injection requires the MSVC release STL; skipped\n";
	return 77;
	#endif
	try
	{
		RegistryInsertionFailure();
		const std::string Text =
			R"({"long member name for allocation failure":"a string longer than small string storage","values":[1,2,3,{"answer":42}]})";
		FError Error;
		FJsonValue Original;
		Check(TryParse(Text, Original, Error));
		bool Parsed = false, Written = false, Built = false;
		for (std::int64_t Budget = 0; Budget < 160; ++Budget)
		{
			FJsonValue Value(std::int64_t{17});
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = TryParse(Text, Value, Error);
			}
			Parsed |= Success;
			Check(Success || (Error.Code == EError::Allocation && *Error.Id()));
			if (!Success)
			{
				std::int64_t Number = 0;
				Check(Value.TryInt64(Number, Error) && Number == 17);
			}
			std::string Output = "preserved";
			{
				FFailureScope Scope(Budget);
				Success = TryWrite(Original, Output, Error);
			}
			Written |= Success;
			Check(Success || (Error.Code == EError::Allocation && Output == "preserved"));
			FBuilder Builder(EKind::Object);
			Check(Builder.TrySet("long member name for allocation failure", Original, Error));
			Value = FJsonValue(std::int64_t{17});
			{
				FFailureScope Scope(Budget);
				Success = Builder.TryFinish(Value, Error);
			}
			Built |= Success;
			Check(Success || Error.Code == EError::Allocation);
			if (!Success)
			{
				std::int64_t Count = 0;
				Check(Builder.TrySize(Count, Error) && Count == 1);
				Check(Builder.TryFinish(Value, Error));
			}
			FReader Reader;
			FEvent Event;
			FFeedResult Read;
			{
				FFailureScope Scope(Budget);
				Read = Reader.Feed("!", true, Event, Error);
				// Sticky failure retrieval must remain safe without error-message allocation.
				Reader.Feed("", true, Event, Error);
			}
			Check(Read.State == EReadState::Failed && Error && *Error.Id());
		}
		Check(Parsed && Written && Built);
		FMappingRegistry Mapping;
		const std::vector<std::int64_t> MappingInput{INT64_MIN, INT64_MAX, 17};
		FJsonValue MappingJson;
		Check(TryEncode(MappingInput, MappingJson, Error, Mapping));
		bool Encoded = false, Decoded = false, Generated = false;
		for (std::int64_t Budget = 0; Budget < 256; ++Budget)
		{
			FJsonValue Value(std::int64_t{19});
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = TryEncode(MappingInput, Value, Error, Mapping);
			}
			Encoded |= Success;
			Check(Success || Error.Code == EError::Allocation);
			if (!Success)
			{
				std::int64_t Number;
				Check(Value.TryInt64(Number, Error) && Number == 19);
			}
			auto OriginalValue = std::make_shared<std::vector<std::int64_t>>(1, 23);
			auto Output = OriginalValue;
			{
				FFailureScope Scope(Budget);
				Success = TryDecode(MappingJson, Output, Error, Mapping);
			}
			Decoded |= Success;
			Check(Success || (Error.Code == EError::Allocation && Output == OriginalValue));
			{
				FFailureScope Scope(Budget);
				Success = TryMappingSchema<std::vector<std::int64_t>>(EMappingDirection::Input, Value, Error, Mapping);
			}
			Generated |= Success;
			Check(Success || Error.Code == EError::Allocation);
		}
		Check(Encoded && Decoded && Generated);
		FJsonValue SchemaSource;
		Check(TryParse(R"({"type":"string","pattern":"^\\p{Letter}+$"})", SchemaSource, Error));
		FSchema OriginalSchema;
		Check(FSchema::TryCompile(SchemaSource, OriginalSchema, Error));
		bool Compiled = false;
		for (std::int64_t Budget = 0; Budget <= 32768; Budget = Budget < 160 ? Budget + 1 : Budget * 2)
		{
			auto Schema = OriginalSchema;
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = FSchema::TryCompile(SchemaSource, Schema, Error);
			}
			if (!Success && Error.Code != EError::Allocation)
			{
				throw std::runtime_error(
					"Schema allocation budget " + std::to_string(Budget) + ": " + Error.Id() + " " + Error.Message);
			}
			Check(Schema.IsCompiled());
			Compiled |= Success;
		}
		Check(Compiled);
		FSchema ReportSchema;
		FJsonValue ReportSource, ReportInstance;
		Check(TryParse(
			R"({"title":"owned annotation long enough to allocate","properties":{"x":{"description":"field description"}},"default":[1,2,{"a":"kept"}]})",
			ReportSource,
			Error));
		Check(TryParse(R"({"x":1})", ReportInstance, Error));
		Check(FSchema::TryCompile(ReportSource, ReportSchema, Error));
		bool Reported = false;
		for (std::int64_t Budget = 0; Budget < 256; ++Budget)
		{
			FSchemaReport Report;
			Report.bValid = false;
			Report.Issues.push_back({});
			bool Success;
			{
				FFailureScope Scope(Budget);
				Success = ReportSchema.TryValidate(ReportInstance, Report, Error);
			}
			Reported |= Success;
			Check(Success ? Report.bValid && !Report.Annotations.empty()
						  : Error.Code == EError::Allocation && !Report.bValid && Report.Issues.size() == 1 &&
								Report.Annotations.empty());
		}
		Check(Reported);
		FSchema Idna;
		FSchemaOptions IdnaOptions;
		IdnaOptions.bAssertFormats = true;
		FJsonValue IdnaSource, IdnaInput;
		Check(TryParse(R"({"format":"idn-hostname"})", IdnaSource, Error));
		Check(FSchema::TryCompile(IdnaSource, Idna, Error, IdnaOptions));
		Check(FJsonValue::TryString("例え.テスト", IdnaInput, Error));
		bool IdnaCompleted = false;
		for (std::int64_t Budget = 0; Budget < 128; ++Budget)
		{
			bool Valid = false, Success;
			{
				FFailureScope Scope(Budget);
				Success = Idna.TryIsValid(IdnaInput, Valid, Error);
			}
			IdnaCompleted |= Success;
			Check(Success ? Valid : Error.Code == EError::Allocation && !Valid);
		}
		Check(IdnaCompleted);
		const auto DeepText = std::string("{\"const\":") + std::string(250, '[') + "0" + std::string(250, ']') + "}";
		FJsonValue DeepSource;
		FSchema DeepSchema;
		Check(TryParse(DeepText, DeepSource, Error));
		Check(FSchema::TryCompile(DeepSource, DeepSchema, Error));
		{
			FFailureScope Scope(0);
			DeepSchema = {};
		}
		PolymorphicFailures();
		std::cout << "JSON allocation failure tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		AllocationBudget = -1;
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
