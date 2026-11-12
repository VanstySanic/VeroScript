// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <VeroScriptCompile.h>
#include <VeroScriptNativeExtensionLoader.h>
#include <VeroScriptVm.h>
#include <VeroScriptVmBackendLoader.h>
#include <VeroScriptPath.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>
#if TEST_JSON
#include <VeroScriptJsonModule.h>
#endif
#if TEST_NETWORK
#include <VeroScriptNetworkModule.h>
#include <VeroScriptNetworkProviderHost.h>
#endif
#if TEST_HTTP
#include <VeroScriptHttpModule.h>
#endif
#if TEST_WEBSOCKET
#include <VeroScriptWebSocketModule.h>
#endif

#if TEST_BOOST
#include <VeroScriptBoostModule.h>
#endif
#if TEST_CURL
#include <VeroScriptCurlModule.h>
#endif
#define RUN_NETWORK (TEST_NETWORK && TEST_BOOST)
#define RUN_HTTP (TEST_HTTP && TEST_CURL && TEST_BOOST)
#define RUN_WEBSOCKET (TEST_WEBSOCKET && TEST_BOOST)

static int RunConsumer(int Argc, char** Argv)
{
	using namespace VeroScript;
	struct FModule
	{
		const char* Provider;
		FNativeExtensionQuery Query;
		const char* Identity;
	};
	const std::vector<FModule> Modules = {
	#if TEST_JSON
	    {"json", Json::QueryModule, Json::GetImplementationIdentity()},
	#endif
	#if TEST_NETWORK
	    {"network", Network::QueryModule, Network::GetImplementationIdentity()},
	#endif
	#if TEST_HTTP
	    {"http", Http::QueryModule, Http::GetImplementationIdentity()},
	#endif
	#if TEST_WEBSOCKET
	    {"websocket", WebSocket::QueryModule, WebSocket::GetImplementationIdentity()},
	#endif
	#if TEST_BOOST
	    {"boost", Boost::QueryModule, Boost::GetImplementationIdentity()},
	#endif
	#if TEST_CURL
	    {"curl", Curl::QueryModule, Curl::GetImplementationIdentity()},
	#endif
	};
	if (Argc != 2 && Argc != static_cast<int>(Modules.size()) + 2)
	{
		return 1;
	}
	std::string Error;
	auto VmFactory = LoadVmBackend(Argv[1], Error);
	if (!VmFactory)
	{
		std::cerr << Error << '\n';
		return 6;
	}
	FNativeExtensionRegistry Registry;
	#if TEST_NETWORK
	auto BackendCatalog = std::make_shared<Network::FBackendCatalog>();
	#endif
	FModuleEnvironmentDesc Desc;
	Desc.EntryOwnerId = "project";
	Desc.Scopes.push_back({"project", {}});
	for (std::size_t I = 0; I < Modules.size(); ++I)
	{
		const auto& Module = Modules[I];
		FNativeExtensionLoadOptions Load;
		Load.ContentIdentity = Module.Identity;
		auto Extension = Argc == 2
		    ? QueryNativeExtension(Module.Query, {}, Error, Load)
		    : LoadNativeExtension(
		          PathUtil::ToUtf8(std::filesystem::absolute(std::filesystem::u8path(Argv[I + 2]))), Error);
		FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = Module.Provider;
		#if TEST_NETWORK
		if (std::string_view(Module.Provider) != "boost" && std::string_view(Module.Provider) != "curl")
		{
			Registration.Services.push_back(BackendCatalog->Binding());
		}
		#endif
		if (!Extension || !Registry.Add(Extension, Registration, Error))
		{
			std::cerr << Error << '\n';
			return 2;
		}
		Desc.Scopes[0].DirectDependencies.push_back(Module.Provider);
		Desc.Scopes.push_back({Module.Provider,
		    (std::string(Module.Provider) == "http" || std::string(Module.Provider) == "websocket")
		        ? std::vector<std::string>{"network"}
		        : std::vector<std::string>{}});
	}
	#if TEST_NETWORK
	BackendCatalog->Update(Registry.GetProviders());
	#endif
	const auto Root = PathUtil::ToUtf8(std::filesystem::current_path());
	Desc.SourceRoots = {{Root, "project"}};
	Desc.Bindings = Registry.GetBindings();
	Desc.CompileProcessing.Registry = Registry.GetProcessors();
	Desc.Owners = Registry.GetOwners();
	Desc.AvailableDependencies = Registry.GetDependencies();
	auto Environment = FModuleEnvironment::Create(std::move(Desc), Error);
	if (!Environment)
	{
		std::cerr << Error << '\n';
		return 3;
	}
	std::string Source;
	#if TEST_JSON
	Source += "struct JsonConsumerRecord { public: int count=17; };\n";
	Source += R"(
@JsonConverter(type="AnimalTable") @JsonFactory(type="AnimalTable")
class Animal {public:virtual int sound()=0;};
class Cat : public Animal {public:int lives=9;int sound() override{return 1;}};
@JsonPolymorphic(base="Animal",variants=(cat="Cat")) class AnimalTable {};
)";
	#endif
	#if RUN_NETWORK
	Source += "class ModuleCompletionCount{public:int value=0;};Network::operation saved_completion=nullptr;\n";
	#endif
	Source += "int main() {\n";
	#if TEST_JSON
	Source += R"(
	json::value value = json::parse("{\"count\":9223372036854775807}");
	if (value.get("count").as_int64() != 9223372036854775807i64) return 1;
 JsonConsumerRecord record=JsonConsumerRecord();json::value mapped=json::converter<JsonConsumerRecord>::write(ref record);
 if(json::converter<JsonConsumerRecord>::read(mapped).count!=17 || !json::compile_schema(json::converter<JsonConsumerRecord>::schema()).is_valid(mapped))return 11;
 json::schema annotated=json::compile_schema(json::parse("{\"title\":\"Installed SDK\"}"));
 json::schema_report_options report_options=json::schema_report_options();
 json::schema_report annotations=annotated.validate_with_options(json::from_int(1),report_options);
 if(!annotations.valid() || annotations.annotation_count()!=1 || annotations.annotation(0).get("value").as_string()!="Installed SDK")return 18;
 json::schema_options format_options=json::schema_options();format_options.set_assert_formats(true);
 json::schema checked_format=json::compile_schema_with_options(json::parse("{\"format\":\"email\"}"),format_options,json::object());
 if(checked_format.is_valid(json::from_string("bad")) || !checked_format.is_valid(json::from_string("x@example.test")))return 19;
 Animal animal=Cat();json::value envelope=json::converter<Animal>::write(ref animal);
 Animal restored=json::converter<Animal>::read(envelope);
 if(restored.sound()!=1 || envelope.get("value").get("lives").as_int64()!=9 ||
    !json::compile_schema(json::converter<Animal>::schema()).is_valid(envelope))return 25;
)";
	#endif
	#if RUN_NETWORK
	Source += R"(
	Network::context context = Network::create_context(false);
	ModuleCompletionCount completion_count=ModuleCompletionCount();
	Network::operation callback_net=Network::resolve(context,"localhost",80,5000);
	callback_net.cancel();
	callback_net.on_complete([completion_count](Network::operation completed) mutable -> void{
		assert(completed.done() && completed.message_id()=="network.cancelled");
		saved_completion=completed;completion_count.value+=1;
	});
	Network::byte_pipe pipe = Network::create_byte_pipe(context, 8);
	if (pipe.writer().write("abc") != 3 || pipe.reader().read(3) != "abc") return 2;
	Network::file_executor files=Network::create_file_executor(context);
	Network::byte_pipe file_pipe=Network::create_byte_pipe(context,64);
	file_pipe.writer().write("file\0bytes");file_pipe.writer().finish();
	Network::file_write_options file_output=Network::make_file_write_options("input.bin",__MODULE_FILE_ROOT__);
	file_output.set_expected_length(10);
	Network::file_operation file_write=files.write(file_output,file_pipe.reader());
	if(file_write.operation().wait(10000)!=0 || file_write.operation().error_code()!=0 ||
		file_write.progress().commit_state()!=2)return 20;
	Network::file_read_options file_input=Network::make_file_read_options("input.bin",__MODULE_FILE_ROOT__);
	file_input.set_timeout(10000);
	Network::file_operation file_read=files.read(file_input);
	Network::readable_stream file_bytes=file_read.body();string file_value="";
	for(int round=0;round<10000 && file_bytes.state()!=2 && file_bytes.state()!=3;++round)
	{file_value+=file_bytes.read(4);if(file_bytes.state()==1)std::this_thread::sleep(1);}
	if(file_bytes.state()!=2 || file_value!="file\0bytes" || file_read.operation().wait(10000)!=0 ||
		file_read.operation().error_code()!=0)return 21;
)";
	#endif
	#if RUN_HTTP
	Source += R"(
	http::server_options server_options=http::make_server_options();server_options.set_stream_requests(true);
	http::server server = http::listen_with_options(context, "127.0.0.1", 0, server_options);
	http::client client = http::create_client(context);
    Network::byte_pipe multipart_pipe=Network::create_byte_pipe(context,8);
    Network::writable_stream multipart_writer=multipart_pipe.writer();
    http::request multipart_request=http::create_request("POST","http://127.0.0.1:"+to_string(server.port())+"/");
    multipart_request.begin_multipart();multipart_request.add_field_stream("value",multipart_pipe.reader(),-1);
    http::operation accept=server.accept(10000);http::operation sent=client.send(multipart_request);
    if(accept.wait(10000)!=0 || accept.error_code()!=0)return 3;
    http::exchange exchange=accept.exchange();
    if(!exchange.has_body_stream() || multipart_writer.write("abc")!=3)return 12;multipart_writer.finish();
    http::multipart_reader multipart=http::make_multipart_reader(context,exchange.header("content-type",0));
    Network::readable_stream request_body=exchange.body_stream();string pending="";bool ended=false;string field="";
    for(int round=0;round<10000 && multipart.state()!=2 && multipart.state()!=3;++round)
    {
        if(multipart.state()!=1 && pending=="" && !ended)
        {
            pending=request_body.read(1);ended=request_body.state()==2;
            if(pending=="" && !ended){std::this_thread::sleep(1);continue;}
        }
        int64 accepted=multipart.feed(pending,ended);
        if(accepted==len(pending))pending="";else if(accepted!=0)return 15;
        if(multipart.state()==1 && multipart.event().kind()==1)field+=multipart.event().data();
    }
    if(multipart.state()!=2 || field!="abc" || exchange.body_operation().wait(10000)!=0)return 14;
    Network::byte_pipe response_pipe=Network::create_byte_pipe(context,8);
    Network::writable_stream response_writer=response_pipe.writer();
    response_writer.write("ok");response_writer.finish();
    http::response_builder response=http::create_response(200,"");response.add_trailer("X-Result","complete");
    http::response_stream_operation responding=exchange.reply_stream(response,response_pipe.reader(),-1,10000);
    if(responding.operation().wait(10000)!=0 || responding.operation().error_code()!=0 || responding.body_bytes()!=2 || sent.wait(10000)!=0 || sent.response().body()!="ok")return 4;
    http::transfer_details received_details=sent.response().details();
    int64 received_head_count=received_details.response_count();
    if(received_head_count<1 || received_details.response(received_head_count-1).status()!=200 ||
        received_details.response(received_head_count-1).method()!="POST" || !received_details.statistics().is_final() ||
        received_details.statistics().accepted_body_bytes()!=2 || received_details.statistics().total_time_us()<0 ||
        sent.response().attempt(0).details().response_count()!=received_head_count)return 33;
	http::request get=http::create_request("GET","http://127.0.0.1:"+to_string(server.port())+"/file");
	http::download_options download_options=http::make_download_options("download.bin",__MODULE_FILE_ROOT__);
	accept=server.accept(10000);
	http::download_operation download=client.download_file(files,get,download_options);
	if(accept.wait(10000)!=0 || accept.error_code()!=0)return 22;
	exchange=accept.exchange();http::operation file_reply=exchange.reply(200,"file\0bytes",10000);
	if(file_reply.wait(10000)!=0 || file_reply.error_code()!=0 || download.operation().wait(10000)!=0 ||
		download.operation().error_code()!=0 || download.response().body_bytes()!=10 ||
		download.progress().file().bytes()!=10 || download.progress().file().commit_state()!=2)return 23;
	http::operation stopped=server.shutdown(1000,10000);
	if(stopped.wait(10000)!=0 || stopped.error_code()!=0 || stopped.shutdown_forced())return 13;
)";
	#endif
	#if RUN_WEBSOCKET
	Source += R"(
	websocket::options proxy_options=websocket::make_options();
	proxy_options.set_proxy("http://127.0.0.1:8080","");
	websocket::options proxy_copy=proxy_options;proxy_options.clear_proxy();
	if(proxy_options.proxy_url()!="" || proxy_copy.proxy_url()!="http://127.0.0.1:8080")return 28;
	websocket::reconnect_policy reconnect_policy=websocket::make_reconnect_policy();
	if(reconnect_policy.max_attempts()!=1)return 29;
	websocket::reconnect_task reconnect_task=websocket::connect_with_retry(context,"not-a-websocket-url",websocket::make_options(),reconnect_policy);
	if(reconnect_task.operation().wait(10000)!=0 || reconnect_task.operation().error_code()==0)return 30;
	websocket::reconnect_snapshot reconnect_snapshot=reconnect_task.snapshot();
	if(!reconnect_snapshot.is_final() || reconnect_snapshot.size()!=1 || !reconnect_snapshot.at(0).complete() ||
		reconnect_snapshot.at(0).connected() || reconnect_snapshot.at(0).error_code()==0)return 31;
	websocket::server ws_server = websocket::listen(context, "127.0.0.1", 0);
	websocket::operation callback_ws=ws_server.accept(5000);
	callback_ws.on_complete([completion_count](websocket::operation completed) mutable -> void{
		assert(completed.done());completion_count.value+=1;
	});
	ws_server.close();
)";
	#if RUN_HTTP
	Source += R"(
	http::server upgrade_server=http::listen(context,"127.0.0.1",0);
	websocket::compression_options compression_options=websocket::make_compression_options();
	compression_options.set_client_window_bits(12);compression_options.set_server_window_bits(12);
	compression_options.set_server_no_context_takeover(true);
	websocket::options compressed_options=websocket::make_options();
	compressed_options.set_compression(websocket::compression_supported());
	compressed_options.set_compression_options(compression_options);
	http::operation upgrade_accept=upgrade_server.accept(10000);
	websocket::operation upgrade_connect=websocket::connect_with_options(context,"ws://127.0.0.1:"+to_string(upgrade_server.port()),compressed_options);
	if(upgrade_accept.wait(10000)!=0 || upgrade_accept.error_code()!=0)return 24;
	Network::upgrade_source source=upgrade_accept.exchange().prepare_upgrade();
	websocket::operation upgrading=websocket::accept_upgrade(source,compressed_options);
	if(upgrading.wait(10000)!=0 || upgrading.error_code()!=0 || upgrade_connect.wait(10000)!=0 ||
		upgrade_connect.error_code()!=0 || !source.consumed())return 25;
	websocket::connection upgrade_peer=upgrading.connection();websocket::connection upgrade_client=upgrade_connect.connection();
	websocket::compression_info compression=upgrade_client.compression();
	if(compression.enabled()!=websocket::compression_supported() || upgrade_peer.compression().enabled()!=websocket::compression_supported() ||
		(websocket::compression_supported() && (compression.client_window_bits()!=12 || compression.server_window_bits()!=12 ||
		!compression.server_no_context_takeover())))return 34;
	websocket::operation upgrade_read=upgrade_peer.receive(10000);
	websocket::operation upgrade_send=upgrade_client.send_text("upgraded",10000);
	if(upgrade_send.wait(10000)!=0 || upgrade_send.error_code()!=0 || upgrade_read.wait(10000)!=0 ||
		upgrade_read.error_code()!=0 || upgrade_read.data()!="upgraded")return 26;
	websocket::operation upgrade_close=upgrade_client.close(1000,"done",10000);
	if(upgrade_close.wait(10000)!=0 || !upgrade_close.clean())return 27;
	upgrade_server.close();
)";
	#endif
	#endif
	#if RUN_NETWORK
	Source += "int expected_completions=1;\n";
	#if RUN_HTTP
	Source += R"(
	sent.on_complete([completion_count](http::operation completed) mutable -> void{
		assert(completed.done() && completed.error_code()==0 && completed.response().status()==200);
		completion_count.value+=1;
	});
	expected_completions+=1;
)";
	#endif
	#if RUN_WEBSOCKET
	Source += "expected_completions+=1;\n";
	#endif
	Source += R"(
	for(int round=0;round<10000 && completion_count.value!=expected_completions;++round)
	{context.dispatch(64);if(completion_count.value!=expected_completions)std::this_thread::sleep(1);}
	if(completion_count.value!=expected_completions || !saved_completion.done())return 35;
)";
	Source += "context.shutdown();\n";
	#endif
	#if RUN_WEBSOCKET
	Source += "if(!reconnect_task.snapshot().is_final())return 32;\n";
	#endif
	Source += "return 0; }";
	#if RUN_NETWORK
	const auto FileRoot =
	    std::filesystem::current_path() /
	    std::filesystem::u8path(
	        "script-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	if (!std::filesystem::create_directory(FileRoot))
	{
		return 6;
	}
	std::string Literal = "\"";
	for (const auto Ch : PathUtil::ToUtf8(FileRoot))
	{
		if (Ch == '\\' || Ch == '"')
		{
			Literal.push_back('\\');
		}
		if (Ch == '\n')
		{
			Literal += "\\n";
		}
		else if (Ch == '\r')
		{
			Literal += "\\r";
		}
		else
		{
			Literal.push_back(Ch);
		}
	}
	Literal.push_back('"');
	for (std::size_t Pos = 0; (Pos = Source.find("__MODULE_FILE_ROOT__", Pos)) != std::string::npos;
	    Pos += Literal.size())
	{
		Source.replace(Pos, sizeof("__MODULE_FILE_ROOT__") - 1, Literal);
	}
	#endif
	FCompileOptions Options;
	Options.ModuleEnvironment = Environment;
	Options.ScriptCompileExtensions.VmBackendFactory = VmFactory;
	auto Code = VeroScriptCompile::CompileSource(Source, Root + "/installed-module.vs", Options);
	if (!Code.bSuccess)
	{
		std::cerr << Code.Error.Message << '\n';
		return 4;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	Vm.SetExecutionBackend(EExecutionBackend::Vm);
	if (!Vm.SetVmBackendFactory(VmFactory, Error) || !Vm.SetModuleEnvironment(Environment, Error) ||
	    !Vm.Execute(*Code.Module, ERunMode::Script, {}) ||
	    Vm.GetProcessExitCode())
	{
		std::cerr << Error << ' ' << Vm.GetError().Message << " exit=" << Vm.GetProcessExitCode() << ' ' << Output.str()
		          << '\n';
		return 5;
	}
	#if RUN_NETWORK
	std::filesystem::remove_all(FileRoot);
	#endif
	return 0;
}

#ifdef _WIN32
int wmain(int Argc, wchar_t** Argv)
{
	std::vector<std::string> Utf8;
	Utf8.reserve(Argc);
	for (int I = 0; I < Argc; ++I)
	{
		Utf8.push_back(VeroScript::PathUtil::ToUtf8(std::filesystem::path(Argv[I])));
	}
	std::vector<char*> Arguments;
	for (auto& Value : Utf8)
	{
		Arguments.push_back(Value.data());
	}
	return RunConsumer(Argc, Arguments.data());
}
#else
int main(int Argc, char** Argv)
{
	return RunConsumer(Argc, Argv);
}
#endif
