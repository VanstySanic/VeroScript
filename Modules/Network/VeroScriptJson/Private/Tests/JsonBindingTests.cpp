// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptJsonModule.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <chrono>
#include <vector>

using namespace VeroScript;
static EExecutionBackend TestBackend = EExecutionBackend::Vm;
static std::shared_ptr<const IJitBackendFactory> TestFactory;

static bool Run(FNativeExtensionQuery Query, const std::string& Dynamic, std::string_view Source = {},
	bool bExpectFailure = false, const std::string& SourceLabel = {}, bool Library = false, bool Plain = false,
	std::string_view ExpectedCompileError = {}, std::string_view ExpectedRuntimeId = {})
{
	std::string Error;
	FNativeExtensionLoadOptions LoadOptions;
	LoadOptions.ContentIdentity = Json::GetImplementationIdentity();
	auto Extension = Query ? QueryNativeExtension(Query, {}, Error, LoadOptions) : LoadNativeExtension(Dynamic, Error);
	FNativeExtensionRegistry Registry;
	FNativeExtensionRegistrationOptions Registration;
	Registration.Origin.ProviderId = "json";
	if (!Extension || !Registry.Add(Extension, Registration, Error))
	{
		std::cerr << Error << '\n';
		return false;
	}
	FModuleEnvironmentDesc Desc;
	Desc.EntryOwnerId = "project";
	Desc.Scopes = {{"project", {"json"}}, {"json", {}}};
	const auto Root = SourceLabel.empty() ? PathUtil::ToUtf8(std::filesystem::current_path())
										  : PathUtil::ToUtf8(PathUtil::FromUtf8(SourceLabel).parent_path());
	Desc.SourceRoots = {{Root, "project"}};
	const auto PackageRoot = PathUtil::ToUtf8(std::filesystem::current_path() / "json-library-mapping-fixture");
	if (Library)
	{
		std::filesystem::create_directories(PathUtil::FromUtf8(PackageRoot));
		Desc.SourceRoots.push_back({PackageRoot, "project"});
	}
	Desc.Bindings = Registry.GetBindings();
	Desc.CompileProcessing.Registry = Registry.GetProcessors();
	Desc.AvailableDependencies = Registry.GetDependencies();
	Desc.Owners = Registry.GetOwners();
	auto Environment = FModuleEnvironment::Create(std::move(Desc), Error);
	if (!Environment)
	{
		std::cerr << Error << '\n';
		return false;
	}
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.ModuleEnvironment = Environment;
	const auto Contract = R"(
bool continue_event(json::event event) { return true; }
bool pause_event(json::event event) { return false; }
int main()
{
	json::value a = json::parse("{\"answer\":42}");
	json::value b = a;
	b.set("answer", json::from_int(7));
	if (a.get("answer").as_int64() != 42) return 1;
	if (b.get("answer").as_int64() != 7) return 2;
	json::error e = json::error();
	if (json::try_parse("{", ref a, ref e)) return 3;
	if (e.code() == 0) return 4;
	if (a.get("answer").as_int64() != 42) return 5;
	if (!json::from_number("1.0").equals(json::from_int(1))) return 6;
	json::document d = json::document();
	d.set(a);
	json::document p = json::patch(d, json::parse("[{\"op\":\"remove\",\"path\":\"\"}]"));
	if (p.has_value()) return 7;
	if (json::resolve(d, "/answer").as_int64() != 42) return 8;
	json::value list = json::array();
	list.push(json::from_int(17));
	if (list.at(0).as_int64() != 17) return 9;
	list.insert(0, json::from_null());
	list.remove_at(1);
	if (list.size() != 1 || list.at(0).kind() != 0) return 10;
	json::builder builder = json::object_builder();
	json::builder alias = builder;
	builder.reserve(20);
	alias.set("x", json::from_number("18446744073709551615"));
	if (builder.size() != 1 || builder.capacity() < 20) return 11;
	json::value built = builder.finish();
	if (built.get("x").number_text() != "18446744073709551615") return 12;
	if (built.keys().at(0).as_string() != "x") return 13;
	alias.reset_array();
	builder.push(json::from_bool(true));
	if (!alias.finish().at(0).as_bool()) return 14;
	json::reader reader = json::make_reader();
	json::writer writer = json::make_writer();
	if (reader.feed("[1]", true) != 1 || reader.state() != 1) return 15;
	if (!writer.write(reader.event())) return 16;
	if (reader.feed("1]", true) != 1 || reader.event().text() != "1") return 17;
	if (!writer.write(reader.event())) return 18;
	if (reader.feed("]", true) != 1 || !writer.write(reader.event())) return 19;
	if (reader.feed("", true) != 0 || reader.state() != 2) return 20;
	writer.finish();
	if (writer.state() != 2 || writer.take() != "[1]" || writer.take() != "") return 21;
	reader.reset();
	reader.feed("!", true);
	if (reader.state() != 3 || reader.error().message_id() == "") return 22;
	json::pointer pointer = json::compile_pointer("#/answer", true);
	if (pointer.resolve(d).as_int64() != 42) return 23;
	reader.reset();
	if (reader.visit("[1]", true, pause_event) != 1 || reader.state() != 1) return 24;
	if (reader.visit("1]", true, continue_event) != 2 || reader.state() != 2) return 25;
	json::parse_options options = json::parse_options();
	options.set_duplicate_keys(2);
	json::parse_options other_options = options;
	other_options.set_duplicate_keys(0);
	if (options.duplicate_keys() != 2 || other_options.duplicate_keys() != 0) return 26;
	if (json::parse_with_options("{\"x\":1,\"x\":2}", options).get("x").as_int64() != 2) return 27;
	if (json::try_parse_with_options("{\"x\":1,\"x\":2}", ref a, ref e, other_options)) return 28;
	json::limits limits = options.limits();
	limits.set_input_bytes(2);
	options.set_limits(limits);
	if (json::try_parse_with_options("null", ref a, ref e, options) || e.code() == 0) return 29;
	if (options.limits().input_bytes() != 2 || other_options.limits().input_bytes() == 2) return 30;
	json::write_options output_options = json::write_options();
	output_options.set_sort_keys(true);
	string encoded = "unchanged";
	if (!json::try_write_with_options(json::parse("{\"b\":1,\"a\":2}"), ref encoded, ref e, output_options)) return 31;
	if (encoded != "{\"a\":2,\"b\":1}" || e.code() != 0) return 32;
	if (json::parse(encoded).write_with_options(output_options) != encoded) return 33;
	limits.set_output_bytes(1);
	output_options.set_limits(limits);
	if (json::try_write_with_options(a, ref encoded, ref e, output_options) || encoded != "{\"a\":2,\"b\":1}") return 34;
	json::value number = json::from_int(77);
	if (json::try_number("+1", ref number, ref e) || number.as_int64() != 77) return 35;
	if (!json::try_number("1.0", ref number, ref e) || e.code() != 0) return 36;
	int64 integer = 123;
	if (!json::try_as_int64(number, ref integer, ref e) || integer != 1) return 37;
	if (json::try_as_int64(json::from_number("1.5"), ref integer, ref e) || integer != 1) return 38;
	double floating = 99.0;
	if (json::try_as_double_exact(json::from_number("0.1"), ref floating, ref e) || floating != 99.0) return 39;
	if (!json::try_as_double(json::from_number("0.1"), ref floating, ref e)) return 40;
	bool boolean = false;
	if (!json::try_as_bool(json::from_bool(true), ref boolean, ref e) || !boolean) return 41;
	if (!json::try_as_string(json::from_string("ok"), ref encoded, ref e) || encoded != "ok") return 42;
	if (!json::try_resolve(d, "/answer", ref number, ref e) || number.as_int64() != 42) return 43;
	if (json::try_resolve(d, "/absent", ref number, ref e) || number.as_int64() != 42) return 44;
	if (!json::try_patch(d, json::parse("[]"), ref p, ref e) || !p.has_value()) return 45;
	if (json::try_patch(d, json::parse("[{\"op\":\"remove\",\"path\":\"/absent\"}]"), ref p, ref e)) return 46;
	if (p.get().get("answer").as_int64() != 42 || e.patch_operation() != 0) return 47;
	if (!json::try_merge_patch(a, json::parse("null"), ref number, ref e) || number.kind() != 0) return 48;
	options.set_duplicate_keys(3);
	limits.set_input_bytes(1024);
	options.set_limits(limits);
	reader = json::make_reader_with_options(options);
	if (reader.visit("{\"x\":1,\"x\":2}", true, continue_event) != 13 || reader.state() != 2) return 49;
	output_options = json::write_options();
	output_options.set_pretty(true);
	output_options.set_indent(1);
	writer = json::make_writer_with_options(output_options);
	writer.write(json::make_event(0, "", false));
	writer.write(json::make_event(5, "", false));
	writer.write(json::make_event(1, "", false));
	writer.finish();
	if (writer.take() != "[\n null\n]") return 50;
	json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"maximum\":9007199254740992}"));
	if (!schema.compiled() || !schema.is_valid(json::from_int(1)) || schema.is_valid(json::from_number("9007199254740993.0"))) return 51;
	json::schema_report report = schema.validate(json::from_string("bad"));
	if (report.valid() || report.truncated() || report.size() != 1 || report.issue(0).get("keyword").as_string() != "type") return 52;
	if (json::try_compile_schema(json::parse("{\"type\":12}"), ref schema, ref e) || e.message_id() != "json.schema" || !schema.compiled()) return 53;
	if (!json::try_validate_schema(schema, json::from_number("1.0"), ref report, ref e) || !report.valid() || e.code() != 0) return 54;
	json::schema_options schema_options = json::schema_options();
	schema_options.set_retrieval_uri("https://example.test/root.json");
	json::schema_options schema_options_copy = schema_options;
	schema_options_copy.set_retrieval_uri("https://example.test/copy.json");
	if (schema_options.retrieval_uri() != "https://example.test/root.json") return 55;
	json::value resources = json::object();
	resources.set("https://example.test/types.json", json::parse("{\"type\":\"boolean\"}"));
	schema = json::compile_schema_with_options(json::parse("{\"$ref\":\"types.json\"}"), schema_options, resources);
	if (!schema.is_valid(json::from_bool(true)) || schema.is_valid(json::from_int(1))) return 56;
	return 0;
}
)";

	std::string PackagedSource;
	if (Library)
	{
		std::ifstream Input(PathUtil::FromUtf8(Root) / "model.vs", std::ios::binary);

		std::string Model{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		Model += R"(
export struct PkgBox<T> {public: T payload;};
export template<typename T,int input,bool context> struct PkgValueBox {
public: T payload; int count=input; bool enabled=context;
};
export class PkgLexical {private:struct Hidden{int value=5;};public:struct Item{int value=17;};};
namespace PkgScope {export using Count=int64;export enum class Role{Read=1,Write=2};
export struct Record {public: Count count=23;Role role=PkgScope::Role::Read;};}
)";
		if (!Plain)
		{
			std::ifstream PolymorphicInput(
				PathUtil::FromUtf8(Root) / "Fixtures" / "polymorphic_model.vs", std::ios::binary);
			Model.append(std::istreambuf_iterator<char>(PolymorphicInput), std::istreambuf_iterator<char>());
			Model += R"(
@JsonFactory(type="PrivateFactory") export class PkgNeeds {public:int n;PkgNeeds(int value){n=value;}};
class PrivateFactory {public:static PkgNeeds create(json::value input,json::mapping_context context){return PkgNeeds(4);}};
)";
		}
		auto ProducerOptions = Options;
		if (Plain)
		{
			ProducerOptions.ModuleEnvironment.reset();
			ProducerOptions.Bindings = nullptr;
			auto EmptyProcessors = std::make_shared<FCompileProcessorRegistry>();
			EmptyProcessors->Freeze();
			ProducerOptions.CompileProcessing.Registry = std::move(EmptyProcessors);
		}
		auto Producer = VeroScriptCompile::CompileSource(Model, Root + "/model.vs", ProducerOptions);

		if (Producer.bSuccess)
		{
			bool Marked = false;
			for (const auto& D : Producer.PreparedProgram->GetDeclarations())
			{
				if (D.Name == "ImportedRecord")
				{
					for (const auto& A : D.Metadata.Annotations)
					{
						if (A.Name == "VeroJsonMappingIdentity" || A.Name == "@VeroJsonMappingIdentity")
						{
							Marked = true;
						}
					}
				}
			}
			if (Marked == Plain)
			{
				std::cerr << "JSON library producer processor profile mismatch\n";
				return false;
			}
		}
		FPackageBuildOptions Build;
		Build.Kind = EPackageKind::Library;
		Build.PackageName = "json-model";
		Build.bStripDebugInfo = true;
		std::vector<std::uint8_t> Bytes;
		if (!Producer.bSuccess || !BuildPackage(*Producer.Module, Build, Bytes, Error))
		{
			std::cerr << "JSON library producer: " << Producer.Error.Message << " " << Error << '\n';
			return false;
		}
		const auto Path = PathUtil::FromUtf8(PackageRoot) / "model.vslib";
		{
			std::ofstream Output(Path, std::ios::binary);
			Output.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			if (!Output)
			{
				return false;
			}
		}
		PackagedSource = Source;
		const auto ReturnAt = PackagedSource.find("return 0;");
		if (ReturnAt == PackagedSource.npos)
		{
			return false;
		}
		std::string Checks = R"(
 PkgBox<int> box=json::converter<PkgBox<int>>::read(json::parse("{\"payload\":31}"));assert(box.payload==31);
 PkgValueBox<string,7,true> value_box=json::converter<PkgValueBox<string,7,true>>::read(json::parse("{\"payload\":\"library\"}"));
 assert(value_box.payload=="library" && value_box.count==7 && value_box.enabled);
 json::value value_box_data=json::converter<PkgValueBox<string,7,true>>::write(ref value_box);
 json::schema value_box_schema=json::compile_schema(json::converter<PkgValueBox<string,7,true>>::schema());
 assert(value_box_schema.is_valid(value_box_data) && value_box_data.get("count").as_int64()==7);
 PkgLexical::Item nested=PkgLexical::Item();assert(nested.value==17);
 PkgScope::Record scoped=json::converter<PkgScope::Record>::read(json::parse("{\"count\":33,\"role\":\"Write\"}"));assert(scoped.count==33 && scoped.role==PkgScope::Role::Write);
 json::schema scoped_schema=json::compile_schema(json::converter<PkgScope::Record>::schema());assert(scoped_schema.is_valid(json::converter<PkgScope::Record>::write(ref scoped)));
)";
		if (!Plain)
		{
			Checks +=
				R"(PkgNeeds needs=json::converter<PkgNeeds>::read(json::parse("{\"n\":41}"));assert(needs.n==41);)";
			Checks += R"(
ImportedBase polymorphic=ImportedChild();
json::value envelope=json::converter<ImportedBase>::write(ref polymorphic);
ImportedBase restored=json::converter<ImportedBase>::read(envelope);
assert(restored.marker==12 && envelope.get("kind").as_string()=="child");
json::schema table_schema=json::compile_schema(json::converter<ImportedBase>::schema());assert(table_schema.is_valid(envelope));
)";
		}
		PackagedSource.insert(ReturnAt, Checks);

		const auto At = PackagedSource.find("model.vs");
		if (At == PackagedSource.npos)
		{
			return false;
		}
		PackagedSource.replace(At, 8, PathUtil::ToUtf8(Path));
		Source = PackagedSource;
	}
	auto Code = VeroScriptCompile::CompileSource(Source.empty() ? std::string_view(Contract) : Source,
		SourceLabel.empty() ? Root + "/json-binding-test.vs" : SourceLabel,
		Options);
	if (!Code.bSuccess)
	{
		if (!ExpectedCompileError.empty() && Code.Error.Message.find(ExpectedCompileError) != std::string::npos)
		{
			return true;
		}
		std::cerr << Code.Error.Location.FileLabel << ':' << Code.Error.Location.Line << ':'
				  << Code.Error.Location.Column << ' ' << Code.Error.Message << '\n';
		return false;
	}
	if (!ExpectedCompileError.empty())
	{
		std::cerr << "Expected compile rejection: " << ExpectedCompileError << '\n';
		return false;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	Vm.SetExecutionBackend(TestBackend);
	if ((TestFactory && !Vm.SetJitBackendFactory(TestFactory, Error)) || !Vm.SetModuleEnvironment(Environment, Error))
	{
		std::cerr << Error << '\n';
		return false;
	}
	const bool Executed = Vm.Execute(*Code.Module, ERunMode::Script, {});
	if (bExpectFailure)
	{
		return !Executed && !Vm.GetError().Message.empty() &&
			   (ExpectedRuntimeId.empty() ||
				   (Vm.GetError().MessageId == ExpectedRuntimeId && Vm.GetError().MessageArgs.size() == 2));
	}
	if (!Executed || Vm.GetProcessExitCode() != 0)
	{
		std::cerr << Vm.GetError().Location.Line << ':' << Vm.GetError().Location.Column << ' ' << Vm.GetError().Message
				  << ' ' << Error << " exit=" << Vm.GetProcessExitCode() << '\n';
		return false;
	}
	return true;
}

static bool ModuleBudget()
{
	const auto Base = std::filesystem::current_path() / ".tmp";
	std::filesystem::create_directories(Base);
	const auto Root =
		Base / ("json-module-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	if (Root.parent_path() != Base || !std::filesystem::create_directory(Root))
	{
		return false;
	}
	struct FFixture
	{
		std::filesystem::path Root;
		~FFixture()
		{
			std::error_code Error;
			std::filesystem::remove_all(Root, Error);
		}
	} Fixture{Root};
	std::string Source;
	for (int I = 0; I < 80; ++I)
	{
		const auto Name = "part" + std::to_string(I) + ".vs";
		std::ofstream File(Root / Name, std::ios::binary);
		File << "export struct Budget" << I << "{public:int value=" << I << ";};\n";
		if (!File)
		{
			return false;
		}
		Source += "import \"" + Name + "\";\n";
	}
	Source += "int main(){Budget79 input=Budget79();json::value value=json::converter<Budget79>::write(ref input);"
			  "Budget79 output=json::converter<Budget79>::read(value);if(output.value!=79)return 1;return 0;}";
	if (!Run(Json::QueryModule, {}, Source, false, PathUtil::ToUtf8(Root / "main.vs")))
	{
		return false;
	}
	return true;
}

static int RunSuite(int Argc, char** Argv)
{
	if (Argc == 2 && std::string_view(Argv[1]) == "--module-budget")
	{
		return ModuleBudget() ? 0 : 1;
	}
	if (Argc > 1 && std::string_view(Argv[1]) == "--library")
	{
		if (Argc != 4)
		{
			return 1;
		}
		const auto Path = std::filesystem::absolute(PathUtil::FromUtf8(Argv[3])) / "import.vs";
		std::ifstream Input(Path, std::ios::binary);
		const std::string Source{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		if (Source.empty() || !Run(Json::QueryModule, {}, Source, false, PathUtil::ToUtf8(Path), true))
		{
			return 1;
		}
		if (std::string_view(Argv[2]) != "--static-only" && !Run(nullptr,
																PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[2]))),
																Source,
																false,
																PathUtil::ToUtf8(Path),
																true))
		{
			return 1;
		}
		if (!Run(Json::QueryModule, {}, Source, false, PathUtil::ToUtf8(Path), true, true))
		{
			return 1;
		}
		if (std::string_view(Argv[2]) != "--static-only" && !Run(nullptr,
																PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[2]))),
																Source,
																false,
																PathUtil::ToUtf8(Path),
																true,
																true))
		{
			return 1;
		}
		return 0;
	}

	const auto ClosureStatics = R"VS(
template<class T> class StaticJsonValue
{
public:
	static json::value value = json::parse("7");
};
json::value remember(int value)
{
	static json::value stored = json::parse("{\"calls\":0}");
	stored.set("calls", json::from_int(stored.get("calls").as_int64() + 1));
	stored.set("value", json::from_int(value));
	return stored;
}
int main()
{
	int total = 0;
	const int tag = 7;
	auto callback = [&sum = total, builder = json::array_builder(), tag](json::event event) mutable -> bool
	{
		static int calls = 0;
		builder.push(json::from_int(++calls));
		++sum;
		assert(tag == 7 && builder.size() == calls);
		return true;
	};
	auto alias = callback;
	json::reader reader = json::make_reader();
	reader.visit("[1]", true, callback);
	reader.reset();
	reader.visit("[2]", true, alias);
	assert(total == 6);
	json::value parsed = json::from_null();
	json::error error = json::error();
	auto parse = [&output = parsed, &failure = error]() -> bool
	{
		return json::try_parse("42", ref output, ref failure);
	};
	assert(parse() && parsed.as_int64() == 42);
	json::value first = remember(7);
	json::value second = remember(9);
	assert(first.get("value").as_int64() == 7 && second.get("value").as_int64() == 9);
	assert(first.get("calls").as_int64() == 1 && second.get("calls").as_int64() == 2);
	assert(StaticJsonValue<int>::value.as_int64() == 7);
	auto cached = StaticJsonValue<int>::value.as_int64();
	assert(cached == 7);
	return 0;
}
)VS";
	if (!Run(Json::QueryModule, {}, ClosureStatics) ||
		(Argc > 1 && std::string_view(Argv[1]) != "--static-only" &&
			!Run(nullptr, PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))), ClosureStatics)))
	{
		return 1;
	}
	if (!Run(Json::QueryModule, {}))
	{
		return 1;
	}
	const auto CallbackFailure = R"(
bool fail_event(json::event event) { assert(false); return true; }
int main() { json::reader r = json::make_reader(); r.visit("null", true, fail_event); return 0; }
)";
	const auto Reentrant = R"(
int main()
{
	json::reader r = json::make_reader();
	function(json::event)->bool callback = [&r](json::event e)->bool { r.reset(); return true; };
	r.visit("null", true, callback);
	return 0;
}

)";
	if (!Run(Json::QueryModule, {}, CallbackFailure, true) || !Run(Json::QueryModule, {}, Reentrant, true))
	{
		return 1;
	}
	const std::pair<std::string_view, std::string_view> Diagnostics[] = {
		{R"(int main(){json::parse("{");return 0;})", "json.syntax"},
		{R"(struct Item{public:int count=0;};int main(){json::converter<Item>::read(json::parse("{\"count\":\"bad\"}"));return 0;})",
			"json.type"},
		{R"(int main(){json::compile_schema(json::parse("{\"type\":3}"));return 0;})", "json.schema"},
		{R"(int main(){json::mapping_options options=json::mapping_options();options.set_limits(0,1);return 0;})",
			"json.binding"}};
	for (const auto& Test : Diagnostics)
	{
		if (!Run(Json::QueryModule, {}, Test.first, true, {}, false, false, {}, Test.second) ||
			(Argc > 1 && std::string_view(Argv[1]) != "--static-only" &&
				!Run(nullptr,
					PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))),
					Test.first,
					true,
					{},
					false,
					false,
					{},
					Test.second)))
		{
			std::cerr << "JSON diagnostic metadata failed: " << Test.second << '\n';
			return 1;
		}
	}
	if (Argc > 1 && std::string_view(Argv[1]) != "--static-only" &&
		!Run(nullptr, PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1])))))
	{
		return 1;
	}
	if (Argc > 2)
	{
		const auto Entry = std::filesystem::absolute(PathUtil::FromUtf8(Argv[2]));
		std::vector<std::filesystem::path> Examples;
		for (const auto& File : std::filesystem::directory_iterator(Entry.parent_path()))
		{
			if (File.is_regular_file() && File.path().extension() == ".vs")
			{
				Examples.push_back(File.path());
			}
		}
		std::sort(Examples.begin(), Examples.end());
		for (const auto& File : Examples)
		{
			std::ifstream Input(File, std::ios::binary);
			std::string Example{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
			if (Example.empty() || !Run(Json::QueryModule, {}, Example, false, PathUtil::ToUtf8(File)) ||
				(std::string_view(Argv[1]) != "--static-only" &&
					!Run(nullptr,
						PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))),
						Example,
						false,
						PathUtil::ToUtf8(File))))
			{
				return 1;
			}
		}
	}
	if (Argc > 3)
	{
		std::vector<std::filesystem::path> Fixtures;
		for (const auto& Entry : std::filesystem::directory_iterator(PathUtil::FromUtf8(Argv[3])))
		{
			if (Entry.is_regular_file() && Entry.path().extension() == ".vs")
			{
				Fixtures.push_back(Entry.path());
			}
		}
		std::sort(Fixtures.begin(), Fixtures.end());
		for (const auto& Path : Fixtures)
		{
			if (Path.filename() == "model.vs")
			{
				continue;
			}
			std::ifstream Input(Path, std::ios::binary);
			std::string Source{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
			const auto Label = PathUtil::ToUtf8(std::filesystem::absolute(Path));
			if (Source.empty() || !Run(Json::QueryModule, {}, Source, false, Label) ||
				(std::string_view(Argv[1]) != "--static-only" &&
					!Run(nullptr, PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))), Source, false, Label)))
			{
				std::cerr << "JSON mapping fixture failed: " << Label << '\n';
				return 1;
			}
		}
		const auto NegativeRoot = PathUtil::FromUtf8(Argv[3]) / "Fixtures" / "Negative";
		for (const auto& Entry : std::filesystem::directory_iterator(NegativeRoot))
		{
			if (Entry.path().extension() != ".vs")
			{
				continue;
			}
			std::ifstream Input(Entry.path(), std::ios::binary);
			std::string Expected;
			std::getline(Input, Expected);
			constexpr std::string_view Prefix = "// expect: ";
			if (Expected.rfind(Prefix, 0) != 0)
			{
				return 1;
			}
			Expected.erase(0, Prefix.size());
			if (!Expected.empty() && Expected.back() == '\r')
			{
				Expected.pop_back();
			}
			std::string Source{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
			const auto Label = PathUtil::ToUtf8(std::filesystem::absolute(Entry.path()));
			if (!Run(Json::QueryModule, {}, Source, false, Label, false, false, Expected) ||
				(std::string_view(Argv[1]) != "--static-only" &&
					!Run(nullptr,
						PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))),
						Source,
						false,
						Label,
						false,
						false,
						Expected)))
			{
				std::cerr << "JSON negative fixture failed: " << Label << '\n';
				return 1;
			}
		}
	}
	std::cout << "JSON native registration and selected backend tests passed\n";
	return 0;
}

static int RunTests(int Argc, char** Argv)
{
	if (Argc >= 3 && std::string_view(Argv[Argc - 2]) == "--jit-backend")
	{
		std::string Error;
		TestFactory = LoadJitBackend(Argv[Argc - 1], Error);
		if (!TestFactory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
		Argc -= 2;
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			TestBackend = Backend;
			if (RunSuite(Argc, Argv))
			{
				return 1;
			}
		}
		return 0;
	}
	return RunSuite(Argc, Argv);
}

#ifdef _WIN32
int wmain(int Argc, wchar_t** Argv)
{
	std::vector<std::string> Utf8;
	Utf8.reserve(Argc);
	for (int I = 0; I < Argc; ++I)
	{
		Utf8.push_back(PathUtil::ToUtf8(std::filesystem::path(Argv[I])));
	}
	std::vector<char*> Arguments;
	for (auto& Value : Utf8)
	{
		Arguments.push_back(Value.data());
	}
	return RunTests(Argc, Arguments.data());
}
#else
int main(int Argc, char** Argv)
{
	return RunTests(Argc, Argv);
}
#endif
