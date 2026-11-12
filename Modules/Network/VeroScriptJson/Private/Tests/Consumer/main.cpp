// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <VeroScriptJson.h>
#include <VeroScriptJsonSchema.h>
#include <VeroScriptJsonMapping.h>

struct FAnimal
{
	virtual ~FAnimal() = default;
	virtual int Sound() const = 0;
};
struct FCat : FAnimal
{
	int Lives = 9;
	int Sound() const override
	{
		return 1;
	}
};

int main()
{
	using namespace VeroScript::Json;
	FJsonValue Value;
	FError Error;
	FMappingRegistry Mapping;
	FJsonValue Mapped;
	std::shared_ptr<std::vector<std::int64_t>> Decoded;
	if (!TryEncode(std::vector<std::int64_t>{INT64_MIN, INT64_MAX}, Mapped, Error, Mapping) ||
		!TryDecode(Mapped, Decoded, Error, Mapping) || Decoded->size() != 2 || Decoded->at(1) != INT64_MAX)
	{
		return 8;
	}
	if (!TryParse("{\"answer\":42}", Value, Error))
	{
		return 1;
	}
	TObjectMapping<FCat> Cat("cat");
	Cat.Field("lives", &FCat::Lives);
	TPolymorphicMapping<FAnimal> Animals("animal");
	Animals.Add<FCat>("cat");
	std::shared_ptr<FAnimal> Animal = std::make_shared<FCat>(), Copy;
	if (!Cat.Register(Mapping, Error) || !Animals.Register(Mapping, Error) ||
		!TryEncode(Animal, Mapped, Error, Mapping) || !TryDecode(Mapped, Copy, Error, Mapping) || !Copy ||
		Copy->Sound() != 1)
	{
		return 12;
	}
	FPointer Pointer;
	if (!FPointer::TryCompile("/answer", Pointer, Error))
	{
		return 2;
	}
	FJsonValue Child;
	if (!Pointer.TryResolve({true, Value}, Child, Error))
	{
		return 3;
	}
	std::int64_t Number = 0;
	if (!Child.TryInt64(Number, Error) || Number != 42)
	{
		return 4;
	}
	FBuilder Builder;
	if (!Builder.TryPush(Child, Error) || !Builder.TryFinish(Value, Error))
	{
		return 5;
	}
	std::string Text;
	if (!TryWrite(Value, Text, Error) || Text != "[42]")
	{
		return 6;
	}
	FSchema Schema;
	bool Valid = false;
	if (!TryParse(R"({"type":"string","pattern":"^\\p{Letter}+$"})", Value, Error) ||
		!FSchema::TryCompile(Value, Schema, Error) || !FJsonValue::TryString("中文abc", Value, Error) ||
		!Schema.TryIsValid(Value, Valid, Error) || !Valid)
	{
		return 7;
	}
	FSchemaReport Report;
	FSchemaReportOptions Options;
	if (!TryParse(R"({"title":"SDK","type":"integer"})", Value, Error) || !FSchema::TryCompile(Value, Schema, Error) ||
		!Schema.TryValidateWithOptions(FJsonValue(std::int64_t{1}), Report, Error, Options) || !Report.bValid ||
		Report.Annotations.size() != 1 || Report.Annotations[0].Keyword != "title")
	{
		return 9;
	}
	Schema = {};
	if (!Report.Annotations[0].Value.TryString(Text, Error) || Text != "SDK")
	{
		return 10;
	}
	FSchemaOptions FormatOptions;
	FormatOptions.bAssertFormats = true;
	if (!TryParse(R"({"format":"uuid"})", Value, Error) || !FSchema::TryCompile(Value, Schema, Error, FormatOptions) ||
		!FJsonValue::TryString("bad", Value, Error) || !Schema.TryIsValid(Value, Valid, Error) || Valid)
	{
		return 11;
	}
	return 0;
}
