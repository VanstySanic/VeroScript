// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "../NetworkTestProviders.h"

#include "VeroScriptNetworkModule.h"
#include "VeroScriptHttpModule.h"
#include "VeroScriptWebSocketModule.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPath.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <regex>
#include <vector>

using namespace VeroScript;

static EExecutionBackend TestBackend = EExecutionBackend::Vm;
static std::shared_ptr<const IJitBackendFactory> TestFactory;
static bool ConfigureVm(VeroScriptVm& Vm)
{
	Vm.SetExecutionBackend(TestBackend);
	std::string Error;
	if (TestFactory && !Vm.SetJitBackendFactory(TestFactory, Error))
	{
		std::cerr << Error << '\n';
		return false;
	}
	return true;
}

static bool Run(const std::vector<std::string>& Paths, bool bExplicitClose = true,
    std::vector<std::weak_ptr<int>>* OwnerProbes = nullptr)
{
	const FNativeExtensionQuery Queries[] = {Network::QueryModule, Http::QueryModule, WebSocket::QueryModule};
	const char* Identities[] = {
	    Network::GetImplementationIdentity(), Http::GetImplementationIdentity(), WebSocket::GetImplementationIdentity()};
	const char* Providers[] = {"network", "http", "websocket"};
	auto Registry = std::make_unique<FNativeExtensionRegistry>();
	std::string Error;
	auto BackendCatalog = Network::Test::AddProviders(*Registry, Error, !Paths.empty());
	if (!BackendCatalog)
	{
		std::cerr << Error << "\n";
		return false;
	}
	for (unsigned I = 0; I < 3; ++I)
	{
		FNativeExtensionLoadOptions Options;
		Options.ContentIdentity = Identities[I];
		auto Owner = std::make_shared<int>(0);
		if (OwnerProbes)
		{
			OwnerProbes->push_back(Owner);
		}
		auto Module = Paths.empty() ? QueryNativeExtension(Queries[I], Owner, Error, Options)
		                            : LoadNativeExtension(Paths[I], Error);
		FNativeExtensionRegistrationOptions Registration;
		Registration.Services = {BackendCatalog->Binding()};
		Registration.Origin.ProviderId = Providers[I];
		if (!Module || !Registry->Add(Module, Registration, Error))
		{
			std::cerr << Error << '\n';
			return false;
		}
	}
	FModuleEnvironmentDesc Desc;
	Desc.EntryOwnerId = "project";
	Desc.Scopes = {{"project", {"network", "http", "websocket"}}, {"network", {}}, {"http", {"network"}}, {"websocket", {"network"}}};
	const auto Root = PathUtil::ToUtf8(std::filesystem::current_path());
	const auto FileRoot =
	    std::filesystem::current_path() / "file-binding-tests" /
	    PathUtil::FromUtf8(
	        "script-文件 " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(FileRoot);
	Desc.SourceRoots = {{Root, "project"}};
	Desc.Bindings = Registry->GetBindings();
	Desc.Owners = Registry->GetOwners();
	Desc.AvailableDependencies = Registry->GetDependencies();
	auto Environment = FModuleEnvironment::Create(std::move(Desc), Error);
	if (!Environment)
	{
		std::cerr << Error << '\n';
		return false;
	}
	Registry.reset();
	FCompileOptions Options;
	VeroScript::Test::Configure(Options);
	Options.ModuleEnvironment = Environment;
	const auto Source = R"(
int main()
{
	Network::url address = Network::parse_url("HTTPS://[::1]:8443/a?x#f");
	if (address.scheme() != "https" || address.host() != "::1" || address.port() != 8443 || !address.ipv6()) return 26;
	if (address.authority() != "[::1]:8443" || address.target() != "/a?x" || !address.explicit_port()) return 27;
	if (address.without_fragment() != "https://[::1]:8443/a?x" || address.fragment() != "f") return 28;
	Network::url address_copy = address;
	address = Network::resolve_url("https://a/b/c?x#f", "../d?");
	if (address.to_string() != "https://a/d?" || !address.has_query() || address.query() != "" || address.has_fragment()) return 29;
	if (address_copy.to_string() != "https://[::1]:8443/a?x#f") return 30;
	if (Network::percent_decode("a+b%00", true) != "a b\0" || Network::percent_encode("a +", true) != "a+%2B") return 31;
	Network::context context = Network::create_context(false);
	Network::file_executor files = Network::create_file_executor(context);
	string file_data = "file\0payload";
	Network::byte_pipe file_pipe = Network::create_byte_pipe(context, 64);
	if (file_pipe.writer().write(file_data) != len(file_data)) return 170;
	file_pipe.writer().finish();
	Network::file_write_options file_write_options = Network::make_file_write_options("sample.bin", __TEST_FILE_ROOT__);
	file_write_options.set_expected_length(len(file_data));
	file_write_options.set_commit_options(true, false, true);
	Network::file_operation file_write = files.write(file_write_options, file_pipe.reader());
	Network::operation file_written = file_write.operation();
	if (file_written.wait(10000) != 0 || file_written.error_code() != 0) return 171;
	if (file_written.message_id() != "") return 192;
	Network::file_progress file_progress = file_write.progress();
	if (file_progress.bytes() != len(file_data) || file_progress.total_bytes() != len(file_data) ||
		!file_progress.physical_complete() || file_progress.commit_state() != 2 || file_progress.partial_path() != "") return 172;
	Network::file_read_options file_read_options = Network::make_file_read_options("sample.bin", __TEST_FILE_ROOT__);
	file_read_options.set_range(4, 1);
	Network::file_operation file_read = files.read(file_read_options);
	Network::readable_stream file_reader = file_read.body();
	string selected = "";
	while (file_reader.state() != 2 && file_reader.state() != 3)
	{
		selected += file_reader.read(8);
		if (file_reader.state() == 1) std::this_thread::sleep(1);
	}
	if (selected != "\0" || file_reader.state() != 2 || file_read.operation().wait(10000) != 0 || file_read.progress().bytes() != 1) return 173;
	file_read_options.set_range(0, -1);
	Network::byte_pipe pipe = Network::create_byte_pipe(context, 4);
	Network::readable_stream pipe_reader = pipe.reader();
	Network::writable_stream pipe_writer = pipe.writer();
	if (pipe_reader.message_id() != "" || pipe_writer.message_id() != "") return 194;
	if (pipe_reader.read(1) != "" || pipe_reader.state() != 1) return 32;
	if (pipe_writer.write("ab\0c!") != 4 || pipe_writer.write("x") != 0 || pipe_writer.state() != 1) return 33;
	if (pipe_reader.read(2) != "ab" || pipe_writer.write("d") != 1) return 34;
	pipe_writer.finish();
	if (pipe_reader.read(10) != "\0cd" || pipe_reader.read(1) != "" || pipe_reader.state() != 2 || pipe_reader.error_code() != 0) return 35;
	Network::tcp_listener listener = Network::listen(context, "127.0.0.1", 0);
	Network::operation cancelled = listener.accept(10000);
	cancelled.cancel();
	if (cancelled.wait(10000) != 0 || cancelled.message_id() != "network.cancelled") return 193;
	Network::operation accept = listener.accept(10000);
	Network::operation connect = Network::connect(context, "127.0.0.1", listener.port(), 10000);
	if (connect.wait(10000) != 0 || accept.wait(10000) != 0) return 1;
	Network::tcp_socket sender = connect.socket();
	Network::tcp_socket peer = accept.socket();
	Network::operation sent = sender.write("hello", 10000);
	if (sent.wait(10000) != 0 || sent.count() != 5) return 2;
	Network::operation read = peer.read(5, 10000);
	if (read.wait(10000) != 0 || read.data() != "hello" || read.eof()) return 3;
	listener.close();
	sender.close();
	peer.close();
	http::server server = http::listen(context, "127.0.0.1", 0);
	http::client client = http::create_client(context);
	http::request request = http::create_request("POST", "http://127.0.0.1:" + to_string(server.port()) + "/test");
	request.set_body("first");
	request.set_header("Content-Type", "text/plain");
	http::request copy = request;
	copy.set_body("second");
	http::operation incoming = server.accept(10000);
	http::operation outgoing = client.send(request);
	if (incoming.wait(10000) != 0 || incoming.error_code() != 0)
	{
		print("HTTP accept: " + to_string(incoming.state()) + " " + incoming.error());
		print("HTTP send: " + to_string(outgoing.state()) + " " + outgoing.error());
		return 4;
	}
	http::exchange exchange = incoming.exchange();
	if (exchange.target() != "/test" || exchange.method() != "POST" || exchange.body() != "first") return 5;
	http::operation reply = exchange.reply(201, "response", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0) return 6;
	http::response response = outgoing.response();
	if (response.status() != 201 || response.body() != "response") return 7;
	http::transfer_details details = response.details();
	http::transfer_statistics measurements = details.statistics();
	if (details.response_count() != 1 || details.response(0).status() != 201 ||
		details.response(0).method() != "POST" || details.response(0).redirect_followed() ||
		details.response(0).location_count() != 0 || !measurements.is_final() ||
		measurements.total_time_us() < 0 || measurements.received_header_bytes() <= 0 ||
		measurements.accepted_body_bytes() != len("response") || measurements.last_transfer_downloaded_bytes() != len("response") ||
		measurements.followed_redirects() != 0 || response.attempt(0).details().response_count() != 1) return 184;
	http::request file_request = http::create_request("POST", "http://127.0.0.1:" + to_string(server.port()) + "/file");
	file_request.set_upload_file(files, file_read_options);
	incoming = server.accept(10000);
	outgoing = client.send(file_request);
	if (incoming.wait(10000) != 0 || incoming.error_code() != 0)
	{
		print("file-upload incoming: " + to_string(incoming.error_code()) + " " + incoming.message_id());
		print("file-upload outgoing: " + to_string(outgoing.error_code()) + " " + outgoing.message_id());
		return 174;
	}
	exchange = incoming.exchange();
	if (exchange.body() != file_data) return 175;
	reply = exchange.reply(200, "file-upload", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0 || outgoing.error_code() != 0) return 176;
	file_request.begin_multipart();
	file_request.add_file_path("file", "sample.bin", "application/octet-stream", files, file_read_options);
	incoming = server.accept(10000);
	outgoing = client.send(file_request);
	if (incoming.wait(10000) != 0 || incoming.error_code() != 0)
	{
		print("file-multipart incoming: " + to_string(incoming.error_code()) + " " + incoming.message_id());
		print("file-multipart outgoing: " + to_string(outgoing.error_code()) + " " + outgoing.message_id());
		return 177;
	}
	exchange = incoming.exchange();
	string file_multipart_body = exchange.body();
	Network::byte_pipe file_mime_pipe = Network::create_byte_pipe(context, len(file_multipart_body));
	if (file_mime_pipe.writer().write(file_multipart_body) != len(file_multipart_body)) return 178;
	file_mime_pipe.writer().finish();
	Network::readable_stream file_mime_bytes = file_mime_pipe.reader();
	http::multipart_reader file_mime = http::make_multipart_reader(context, exchange.header("content-type", 0));
	if (file_mime.message_id() != "") return 195;
	string file_mime_pending = "";
	string file_mime_payload = "";
	bool file_mime_end = false;
	for (int round = 0; round < 10000 && file_mime.state() != 2 && file_mime.state() != 3; ++round)
	{
		if (file_mime.state() != 1 && file_mime_pending == "" && !file_mime_end)
		{
			file_mime_pending = file_mime_bytes.read(1);
			file_mime_end = file_mime_bytes.state() == 2;
		}
		int64 consumed = file_mime.feed(file_mime_pending, file_mime_end);
		if (consumed == len(file_mime_pending)) file_mime_pending = "";
		else if (consumed != 0) return 178;
		if (file_mime.state() == 1 && file_mime.event().kind() == 1) file_mime_payload += file_mime.event().data();
	}
	if (file_mime.state() != 2 || file_mime_payload != file_data) return 178;
	reply = exchange.reply(200, "file-multipart", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0 || outgoing.error_code() != 0) return 179;
	file_request = http::create_request("GET", "http://127.0.0.1:" + to_string(server.port()) + "/file");
	http::download_options download_options = http::make_download_options("download.bin", __TEST_FILE_ROOT__);
	Network::file_write_options download_file_options = Network::make_file_write_options("download.bin", __TEST_FILE_ROOT__);
	download_file_options.set_expected_length(len(file_data));
	download_file_options.set_commit_options(true, false, true);
	download_options.set_file_options(download_file_options);
	incoming = server.accept(10000);
	http::download_operation downloading = client.download_file(files, file_request, download_options);
	if (incoming.wait(10000) != 0 || incoming.error_code() != 0) return 180;
	exchange = incoming.exchange();
	reply = exchange.reply(200, file_data, 10000);
	http::operation downloaded = downloading.operation();
	if (reply.wait(10000) != 0 || downloaded.wait(10000) != 0 || downloaded.error_code() != 0)
	{
		print("file-download reply: " + to_string(reply.error_code()) + " " + reply.message_id());
		print("file-download result: " + to_string(downloaded.error_code()) + " " + downloaded.message_id());
		return 181;
	}
	http::download_progress download_progress = downloading.progress();
	if (!downloading.head_ready() || downloading.head().status() != 200 || downloading.response().body_bytes() != len(file_data) ||
		download_progress.received_bytes() != len(file_data) || download_progress.representation_bytes() != len(file_data) ||
		download_progress.resumed() || download_progress.already_complete()) return 182;
	file_progress = download_progress.file();
	if (file_progress.bytes() != len(file_data) || !file_progress.physical_complete() || file_progress.commit_state() != 2) return 183;
	http::form form = http::create_form();
	form.add("name", "first second");
	form.add("name", "+*~");
	http::form form_copy = form;
	form_copy.add("copy", "x");
	if (form.encode() != "name=first+second&name=%2B*%7E") return 19;
	request.set_form(form);
	incoming = server.accept(10000);
	outgoing = client.send(request);
	if (incoming.wait(10000) != 0) return 20;
	exchange = incoming.exchange();
	if (exchange.body() != form.encode()) return 21;
	reply = exchange.reply(200, "form-ok", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0 || outgoing.response().body() != "form-ok") return 22;
	request.begin_multipart();
	request.add_field("field", "text");
	request.add_file("file", "hello.bin", "application/octet-stream", "data");
	copy = request;
	copy.set_body("other");
	incoming = server.accept(10000);
	outgoing = client.send(request);
	if (incoming.wait(10000) != 0) return 23;
	exchange = incoming.exchange();
	if (len(exchange.body()) < 30) return 24;
	reply = exchange.reply(200, "multipart-ok", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0 || outgoing.response().body() != "multipart-ok") return 25;
	request.set_body("");
	incoming = server.accept(10000);
	http::stream_operation streamed = client.send_stream(request, 16384);
	if (incoming.wait(10000) != 0) return 36;
	exchange = incoming.exchange();
	reply = exchange.reply(200, "stream\0body", 10000);
	http::operation transfer = streamed.operation();
	if (reply.wait(10000) != 0 || transfer.wait(10000) != 0 || !streamed.head_ready() || streamed.head().status() != 200) return 37;
	Network::readable_stream body_stream = streamed.body();
	string stream_body = body_stream.read(100);
	string stream_end = body_stream.read(1);
	if (stream_body != "stream\0body" || stream_end != "" || body_stream.state() != 2)
	{
		print("stream body bytes: " + to_string(len(stream_body)) + ", end bytes: " + to_string(len(stream_end)));
		print("stream state: " + to_string(body_stream.state()) + ", error: " + to_string(body_stream.error_code()) + " " + body_stream.message_id());
		return 38;
	}
	if (transfer.response().body() != "" || transfer.response().body_bytes() != 11) return 39;
	Network::byte_pipe upload_pipe = Network::create_byte_pipe(context, 16);
	Network::writable_stream upload_writer = upload_pipe.writer();
	if (upload_writer.write("upload\0bytes") != 12) return 40;
	upload_writer.finish();
	request.set_upload(upload_pipe.reader(), 12);
	incoming = server.accept(10000);
	outgoing = client.send(request);
	if (incoming.wait(10000) != 0 || incoming.error_code() != 0) return 41;
	exchange = incoming.exchange();
	if (exchange.body() != "upload\0bytes") return 42;
	reply = exchange.reply(200, "uploaded", 10000);
	if (reply.wait(10000) != 0 || outgoing.wait(10000) != 0 || outgoing.response().body() != "uploaded") return 43;

    {
        http::request stream_request=http::create_request("GET","http://127.0.0.1:"+to_string(server.port())+"/server-stream");
        incoming=server.accept(10000);
        http::stream_operation received_stream=client.send_stream(stream_request,16384);
        if(incoming.wait(10000)!=0)return 75;
        Network::byte_pipe response_pipe=Network::create_byte_pipe(context,64);
        Network::writable_stream response_writer=response_pipe.writer();
        http::response_builder stream_response=http::create_response(200,"");
        stream_response.add_trailer("X-Result","complete");
        http::response_stream_operation responding=incoming.exchange().reply_stream(stream_response,response_pipe.reader(),-1,10000);
        for(int attempt=0;attempt<1000 && !received_stream.head_ready() && !received_stream.operation().done();++attempt)std::this_thread::sleep(1);
        if(!received_stream.head_ready() || responding.operation().done() || responding.state()!=1)return 76;
        Network::readable_stream response_body=received_stream.body();
        if(response_writer.write("live")!=4)return 77;
        for(int attempt=0;attempt<1000 && response_body.buffered()!=4;++attempt)std::this_thread::sleep(1);
        if(response_body.read(4)!="live" || responding.operation().done())return 78;
        response_writer.finish();
        if(responding.operation().wait(10000)!=0 || received_stream.operation().wait(10000)!=0 ||
            responding.operation().error_code()!=0 || responding.state()!=3 || responding.body_bytes()!=4 || received_stream.operation().response().trailer_count("X-Result")!=1 || received_stream.operation().response().trailer("x-result",0)!="complete")return 79;
    }

    {
        http::server_options server_options=http::make_server_options();
        server_options.set_stream_requests(true);server_options.set_request_stream_buffer_bytes(16);
        http::server_options copied_server_options=server_options;copied_server_options.set_request_stream_buffer_bytes(8);
        if(server_options.request_stream_buffer_bytes()!=16 || copied_server_options.request_stream_buffer_bytes()!=8 || !server_options.stream_requests())return 80;
        http::server stream_server=http::listen_with_options(context,"127.0.0.1",0,server_options);
        http::request upload_request=http::create_request("POST","http://127.0.0.1:"+to_string(stream_server.port())+"/body");
        Network::byte_pipe input=Network::create_byte_pipe(context,16);Network::writable_stream input_writer=input.writer();
        upload_request.set_upload(input.reader(),-1);upload_request.set_header("Expect","100-continue");
        http::operation input_accept=stream_server.accept(10000);http::operation input_sent=client.send(upload_request);
        if(input_accept.wait(10000)!=0 || input_accept.error_code()!=0)return 81;
        http::exchange input_exchange=input_accept.exchange();
        if(!input_exchange.has_body_stream() || input_exchange.body()!="" || input_exchange.header_count("expect")!=1 || input_exchange.header("Expect",0)!="100-continue")return 82;
        Network::readable_stream input_body=input_exchange.body_stream();http::operation input_operation=input_exchange.body_operation();
        if(input_writer.write("live")!=4)return 83;
        for(int attempt=0;attempt<1000 && input_body.buffered()!=4;++attempt)std::this_thread::sleep(1);
        if(input_operation.done() || input_body.read(4)!="live")return 84;
        input_writer.finish();
        if(input_operation.wait(10000)!=0 || input_operation.error_code()!=0 || input_operation.request_body_result().body_bytes()!=4 || input_operation.request_body_result().trailer_count()!=0)return 85;
        if(input_exchange.reply(200,"received",10000).wait(10000)!=0 || input_sent.wait(10000)!=0 || input_sent.response().body()!="received")return 86;

        http::request mime_request=http::create_request("POST","http://127.0.0.1:"+to_string(stream_server.port())+"/multipart");
        Network::byte_pipe mime_pipe=Network::create_byte_pipe(context,64);Network::writable_stream mime_writer=mime_pipe.writer();
        mime_request.begin_multipart();mime_request.add_field("field","text");mime_request.add_file_stream("file","hello.bin","application/octet-stream",mime_pipe.reader(),-1);
        http::operation mime_accept=stream_server.accept(10000);http::operation mime_sent=client.send(mime_request);
        if(mime_accept.wait(10000)!=0 || mime_accept.error_code()!=0)return 90;
        http::exchange mime_exchange=mime_accept.exchange();Network::readable_stream mime_body=mime_exchange.body_stream();
        if(mime_sent.done() || mime_writer.write("data\0bytes")!=10)return 99;mime_writer.finish();
        http::multipart_options mime_options=http::make_multipart_options();mime_options.set_max_parts(4);
        http::multipart_options mime_copy=mime_options;mime_copy.set_max_parts(3);
        if(mime_options.max_parts()!=4 || mime_copy.max_parts()!=3)return 91;
        http::multipart_reader mime=http::make_multipart_reader_with_options(context,mime_exchange.header("Content-Type",0),mime_options);
        string carry="";bool mime_eof=false;int mime_begins=0;int mime_ends=0;int mime_closed=0;string field_bytes="";string file_bytes="";
        for(int round=0;round<100000 && mime.state()!=2 && mime.state()!=3;++round)
        {
            if(mime.state()!=1 && carry=="" && !mime_eof)
            {
                carry=mime_body.read(1);mime_eof=mime_body.state()==2;
                if(mime_body.state()==3)return 92;
                if(carry=="" && !mime_eof){std::this_thread::sleep(1);continue;}
            }
            int64 accepted=mime.feed(carry,mime_eof);
            if(accepted<0 || accepted>len(carry))return 93;
            if(accepted==len(carry))carry="";
            if(mime.state()==1)
            {
                http::multipart_event event=mime.event();
                if(event.kind()==0)
                {
                    if(event.part_index()!=mime_begins)return 94;
                    if(mime_begins==0 && (event.name()!="field" || event.has_filename()))return 95;
                    if(mime_begins==1 && (event.name()!="file" || !event.has_filename() || event.filename()!="hello.bin" || event.content_type()!="application/octet-stream" || event.header_count("content-type")!=1))return 96;
                    ++mime_begins;
                }
                else if(event.kind()==1){if(event.part_index()==0)field_bytes+=event.data();else file_bytes+=event.data();}
                else if(event.kind()==2)++mime_ends;else ++mime_closed;
            }
        }
        if(mime.state()!=2 || mime.error_code()!=0 || mime_begins!=2 || mime_ends!=2 || mime_closed!=1 || field_bytes!="text" || file_bytes!="data\0bytes")return 97;
        if(mime_exchange.body_operation().wait(10000)!=0 || mime_exchange.reply(200,"multipart-stream",10000).wait(10000)!=0 || mime_sent.wait(10000)!=0 || mime_sent.response().body()!="multipart-stream")return 98;
        http::operation stream_stopped=stream_server.shutdown(1000,10000);
        if(stream_stopped.wait(10000)!=0 || stream_stopped.error_code()!=0 || stream_stopped.shutdown_forced() || stream_stopped.shutdown_connections()!=1)return 88;
    }


 http::router router=http::create_router();router.add("GET","/user/:id",7);router.add("GET","/user/me",8);
 http::route_snapshot routes=router.snapshot();http::route_match route=routes.match("GET","/user/a%2Fb?flag&flag=");
 if(!route.found() || route.id()!=7 || route.parameter_count()!=1 || route.parameter_value(0)!="a/b" || route.query_count()!=2 || route.query_has_equals(0) || !route.query_has_equals(1))return 73;
 if(!router.remove(8) || routes.match("GET","/user/me").id()!=8 || router.snapshot().match("GET","/user/me").id()!=7)return 74;
 http::client_options client_options=http::make_client_options();client_options.set_cookies(true);client_options.set_max_cookie_bytes(8192);
 http::client_options copied_options=client_options;copied_options.set_max_pending_requests(1);
 if(client_options.max_pending_requests()!=128 || copied_options.max_pending_requests()!=1)return 61;
 http::client cookie_client=http::create_client_with_options(context,client_options);
 http::request cookie_request=http::create_request("GET","http://127.0.0.1:"+to_string(server.port())+"/cookies");
 incoming=server.accept(10000);http::operation cookie_sent=cookie_client.send(cookie_request);
 if(incoming.wait(10000)!=0)return 62;
 http::response_builder cookie_response=http::create_response(200,"cookies");cookie_response.add_header("Set-Cookie","a=1; Path=/");cookie_response.add_header("Set-Cookie","b=2; Path=/");
 if(incoming.exchange().reply_response(cookie_response,10000).wait(10000)!=0 || cookie_sent.wait(10000)!=0 || cookie_sent.error_code()!=0)return 63;
 http::operation cookie_export=cookie_client.cookies(10000);if(cookie_export.wait(10000)!=0 || cookie_export.error_code()!=0 || cookie_export.cookies().size()!=2)return 64;
 http::cookies saved_cookies=cookie_export.cookies();http::operation cleared=cookie_client.clear_cookies(10000);
 if(cleared.wait(10000)!=0 || cleared.count()!=0)return 65;
 http::operation imported=cookie_client.import_cookies(saved_cookies,10000);if(imported.wait(10000)!=0 || imported.count()!=2)return 66;
 http::retry_options retry_options=http::make_retry_options();retry_options.set_max_attempts(2);retry_options.set_allow_replay(true);retry_options.set_delay_ms(1);retry_options.clear_statuses();retry_options.add_status(503);
 http::attempt_history attempt_history=http::make_attempt_history();cookie_request.set_attempt_history(attempt_history);
 cookie_request.set_retry_options(retry_options);incoming=server.accept(10000);cookie_sent=cookie_client.send(cookie_request);
 if(incoming.wait(10000)!=0 || incoming.exchange().reply(503,"retry",10000).wait(10000)!=0)return 67;
 incoming=server.accept(10000);if(incoming.wait(10000)!=0 || incoming.exchange().reply(200,"retried",10000).wait(10000)!=0 || cookie_sent.wait(10000)!=0)return 68;
 if(cookie_sent.error_code()!=0 || cookie_sent.response().attempt_count()!=2 || cookie_sent.response().attempt(0).status()!=503 || cookie_sent.response().attempt(1).number()!=2)return 69;

 while(!attempt_history.snapshot().is_final())std::this_thread::sleep(1);
 http::attempt_snapshot attempt_snapshot=attempt_history.snapshot();
 if(attempt_snapshot.attempt_count()!=2 || attempt_snapshot.error_code()!=0 || attempt_snapshot.is_truncated() || attempt_snapshot.attempt(0).status()!=503 || attempt_snapshot.attempt(1).error_code()!=0)return 70;
 if(attempt_snapshot.message_id()!="" || attempt_snapshot.attempt(0).message_id()!="")return 196;
 http::request invalid_history_request=http::create_request("GET","invalid");invalid_history_request.set_attempt_history(attempt_history);
 http::operation history_failed=cookie_client.send(invalid_history_request);if(history_failed.wait(10000)!=0 || history_failed.error_code()==0)return 71;
 if(attempt_history.snapshot().error_code()==0 || attempt_snapshot.attempt_count()!=2 || attempt_snapshot.error_code()!=0)return 72;
 if(attempt_history.snapshot().message_id()!="network.invalid_argument" || history_failed.message_id()!="network.invalid_argument")return 197;
 cookie_client.close();
	server.close();
	client.close();
	{
		http::server_options upgrade_server_options = http::make_server_options();
		upgrade_server_options.set_stream_requests(true);
		http::server upgrade_server = http::listen_with_options(context, "127.0.0.1", 0, upgrade_server_options);
		websocket::options upgrade_options = websocket::make_options();
		upgrade_options.add_subprotocol("vero.upgrade");
		upgrade_options.set_require_subprotocol(true);
		http::operation upgrade_incoming = upgrade_server.accept(10000);
		websocket::operation upgrade_connecting = websocket::connect_with_options(context,
			"ws://127.0.0.1:" + to_string(upgrade_server.port()) + "/upgrade", upgrade_options);
		if (upgrade_incoming.wait(10000) != 0 || upgrade_incoming.error_code() != 0) return 184;
		http::exchange upgrade_exchange = upgrade_incoming.exchange();
		Network::upgrade_source upgrade_source = upgrade_exchange.prepare_upgrade();
		if (upgrade_source.consumed() || upgrade_source.protocol() != "websocket" || len(upgrade_source.request_head()) == 0) return 185;
		websocket::operation upgraded = websocket::accept_upgrade(upgrade_source, upgrade_options);
		if (upgraded.wait(10000) != 0 || upgraded.error_code() != 0 || upgrade_connecting.wait(10000) != 0 ||
			upgrade_connecting.error_code() != 0 || !upgrade_source.consumed()) return 186;
		websocket::connection upgrade_peer = upgraded.connection();
		websocket::connection upgrade_client = upgrade_connecting.connection();
		websocket::operation upgrade_reading = upgrade_peer.receive(10000);
		websocket::operation upgrade_sending = upgrade_client.send_binary("upgrade\0bytes", 10000);
		if (upgrade_sending.wait(10000) != 0 || upgrade_sending.error_code() != 0 || upgrade_reading.wait(10000) != 0 ||
			upgrade_reading.data() != "upgrade\0bytes") return 187;
		http::operation upgrade_stopped = upgrade_server.shutdown(10, 10000);
		if (upgrade_stopped.wait(10000) != 0 || upgrade_stopped.error_code() != 0 || upgrade_stopped.shutdown_forced()) return 188;
		websocket::operation upgrade_closed = upgrade_client.close(1000, "done", 10000);
		if (upgrade_closed.wait(10000) != 0 || !upgrade_closed.clean()) return 189;
	}
	if (Network::tls_available())
	{
		Network::tls_options tls = Network::make_tls_options();
		tls.set_certificate_pem(__TEST_CERT_PEM__);
		tls.set_private_key_pem(__TEST_KEY_PEM__);
		tls.set_trust_pem(__TEST_CERT_PEM__);
		tls.use_system_trust(false);
		websocket::server secure_server = websocket::listen_tls(context, "127.0.0.1", 0, tls);
		websocket::operation secure_accept = secure_server.accept(10000);
		websocket::operation secure_connect = websocket::connect_tls(context, "wss://127.0.0.1:" + to_string(secure_server.port()), tls, 10000);
		if (secure_accept.wait(10000) != 0 || secure_connect.wait(10000) != 0 || secure_connect.error_code() != 0) return 44;
		websocket::connection secure_client = secure_connect.connection();
		websocket::connection secure_peer = secure_accept.connection();
		websocket::operation secure_read = secure_peer.receive(10000);
		websocket::operation secure_sent = secure_client.send_text("secure", 10000);
		if (secure_read.wait(10000) != 0 || secure_sent.wait(10000) != 0 || secure_read.data() != "secure") return 45;
		websocket::operation secure_close = secure_client.close(1000, "done", 10000);
		if (secure_close.wait(10000) != 0 || !secure_close.clean()) return 46;
		secure_server.close();
		http::server_options https_options=http::make_server_options();
        https_options.set_stream_requests(true);https_options.set_request_stream_buffer_bytes(16);https_options.set_tls(tls);
        http::server https_server=http::listen_with_options(context,"127.0.0.1",0,https_options);
		http::client https_client = http::create_client(context);
		http::request https_request = http::create_request("POST", "https://127.0.0.1:" + to_string(https_server.port()));
		https_request.set_ca_file(__TEST_CERT_PATH__);https_request.set_body("secure-input");
		http::operation https_accept = https_server.accept(10000);
		http::operation https_sent = https_client.send(https_request);
		if (https_accept.wait(10000) != 0 || https_accept.error_code() != 0) return 47;
		http::exchange https_exchange = https_accept.exchange();
		if(!https_exchange.has_body_stream() || https_exchange.body_operation().wait(10000)!=0 || https_exchange.body_operation().request_body_result().body_bytes()!=12 || https_exchange.body_stream().read(16)!="secure-input")return 87;
        Network::byte_pipe https_output=Network::create_byte_pipe(context,32);Network::writable_stream https_writer=https_output.writer();
        https_writer.write("secure-body");https_writer.finish();
        http::response_builder https_response=http::create_response(200,"");https_response.add_trailer("X-Result","complete");
        http::response_stream_operation https_reply=https_exchange.reply_stream(https_response,https_output.reader(),-1,10000);
        if(https_reply.operation().wait(10000)!=0 || https_reply.operation().error_code()!=0 || https_sent.wait(10000)!=0 || https_sent.response().body()!="secure-body" || https_sent.response().trailer("X-Result",0)!="complete")return 48;
		https_server.close(); https_client.close();
	}
	websocket::options ws_options = websocket::make_options();
	http::sse_options sse_options = http::make_sse_options();
	sse_options.set_reconnect(0, 0, 0);
	sse_options.set_limits(1024, 4096, 16384, 2);
	http::server sse_server = http::listen(context, "127.0.0.1", 0);
	http::operation sse_accept = sse_server.accept(10000);
	http::sse_client sse_client = http::connect_sse_with_options(context, "http://127.0.0.1:" + to_string(sse_server.port()), sse_options);
	if (sse_accept.wait(10000) != 0) return 57;
	http::response_builder sse_response = http::create_response(200, "");
	sse_response.set_header("Content-Type", "text/event-stream");
	Network::byte_pipe sse_output=Network::create_byte_pipe(context,64);Network::writable_stream sse_writer=sse_output.writer();
    http::response_stream_operation sse_sending=sse_accept.exchange().reply_stream(sse_response,sse_output.reader(),-1,10000);
    if(sse_writer.write("id: probe\ndata: hello\n\n")!=23)return 58;
	int sse_state = 0;
	for (int attempt = 0; attempt < 1000 && sse_state == 0; attempt++) { sse_state = sse_client.poll(); std::this_thread::sleep(1); }
	if (sse_sending.operation().done() || sse_state != 1 || sse_client.event_data() != "hello" || sse_client.event_id() != "probe" || sse_client.last_event_id() != "probe") return 59;
	if (sse_client.message_id() != "") return 198;
    sse_writer.finish();if(sse_sending.operation().wait(10000)!=0 || sse_sending.operation().error_code()!=0)return 89;
	sse_client.close();
	sse_server.close();
	ws_options.set_compression(websocket::compression_supported());
	ws_options.set_receive_fragments(true);
	ws_options.set_limits(1048576, 2097152, 128);
	ws_options.set_idle_timeout(30000, true);
	ws_options.add_subprotocol("vero.test");
	ws_options.set_require_subprotocol(true);
	websocket::server ws_server = websocket::listen_with_options(context, "127.0.0.1", 0, ws_options);
	websocket::operation ws_accept = ws_server.accept(10000);
	ws_options.set_receive_fragments(false);
	websocket::operation ws_connect = websocket::connect_with_options(context, "ws://127.0.0.1:" + to_string(ws_server.port()), ws_options);
	if (ws_accept.wait(10000) != 0 || ws_connect.wait(10000) != 0) return 8;
	websocket::connection ws_client = ws_connect.connection();
	websocket::connection ws_peer = ws_accept.connection();
	if (ws_client.subprotocol() != "vero.test" || ws_peer.subprotocol() != "vero.test") return 56;
	if (ws_client.closed()) return 9;
	websocket::operation message = ws_peer.receive(10000);
	websocket::operation ws_sent = ws_client.send_text("hello", 10000);
	if (message.wait(10000) != 0 || ws_sent.wait(10000) != 0 || !message.is_text() || message.data() != "hello") return 10;
	websocket::operation fragment = ws_client.send_text_fragment("left", false, 10000);
	if (fragment.wait(10000) != 0 || fragment.error_code() != 0) return 49;
	websocket::operation invalid_message = ws_client.send_text("other", 10000);
	if (invalid_message.wait(10000) != 0 || invalid_message.error_code() == 0) return 50;
	fragment = ws_client.send_text_fragment("-right", true, 10000);
	if (fragment.wait(10000) != 0 || fragment.error_code() != 0) return 51;
	string combined = "";
	bool final_piece = false;
	bool first_piece = true;
	while (!final_piece)
	{
		message = ws_peer.receive(10000);
		if (message.wait(10000) != 0 || message.error_code() != 0 || !message.is_text()) return 52;
		if (message.is_first() != first_piece) return 60;
		first_piece = false;
		combined += message.data();
		final_piece = message.is_final();
	}
	if (combined != "left-right") return 53;
	websocket::operation ping = ws_client.ping("probe", 10000);
	if (ping.wait(10000) != 0 || ping.error_code() != 0) return 54;
	websocket::control_event control = ws_client.try_control();
	if (control.has_value() && (control.is_ping() || control.payload() != "probe")) return 55;
	if (context.memory_limit() != 268435456 || context.peak_retained_bytes() < context.retained_bytes()) return 56;
	websocket::operation close = ws_client.close(1000, "done", 10000);
	if (close.wait(10000) != 0 || !close.clean()) return 11;
	ws_server.close();
	context.shutdown();
	if (!context.closed()) return 12;
	http::sse_reader sse = http::make_sse_reader();
	if (sse.message_id() != "") return 199;
	string event_bytes = http::encode_sse("first\nsecond", "update", "one");
	if (sse.feed(event_bytes, true) != len(event_bytes) || sse.state() != 1) return 13;
	if (sse.event_data() != "first\nsecond" || sse.event_type() != "update" || sse.event_id() != "one") return 14;
	if (sse.feed("", true) != 0 || sse.state() != 2 || sse.last_event_id() != "one") return 15;
	sse.reset();
	if (sse.feed("retry: 12\n\n", false) != 11 || sse.retry_ms() != 12 || sse.last_event_id() != "") return 16;
	if (http::encode_sse_comment("keep\nalive") != ": keep\n: alive\n") return 17;
	sse = http::make_sse_reader_with_limits(8, 3);
	sse.feed("data: large\n\n", true);
	if (sse.state() != 3 || sse.error_code() == 0 || sse.error() == "") return 18;
	if (sse.message_id() != "network.limit") return 200;
	return 0;
}
)";
	auto Quote = [](std::string_view Text)
	{
		std::string Out(1, '"');
		for (const char Ch : Text)
		{
			if (Ch == '\n')
			{
				Out.push_back('\\');
				Out.push_back('n');
			}
			else if (Ch == '\r')
			{
				Out.push_back('\\');
				Out.push_back('r');
			}
			else if (Ch == '\\' || Ch == '"')
			{
				Out.push_back('\\');
				Out.push_back(Ch);
			}
			else
			{
				Out.push_back(Ch);
			}
		}
		Out.push_back('"');
		return Out;
	};
	auto Read = [](const std::filesystem::path& Path)
	{
		std::ifstream Input(Path, std::ios::binary);
		if (!Input)
		{
			throw std::runtime_error("Cannot read network test input: " + PathUtil::ToUtf8(Path));
		}
		return std::string(std::istreambuf_iterator<char>(Input), {});
	};
	const auto Fixtures = PathUtil::FromUtf8(__FILE__).parent_path().parent_path() / "Fixtures";
	std::string EffectiveSource = Source;
	for (const auto& Entry : std::vector<std::pair<std::string, std::string>>{
	         {"__TEST_CERT_PEM__", Quote(Read(Fixtures / "loopback-cert.pem"))},
	         {"__TEST_KEY_PEM__", Quote(Read(Fixtures / "loopback-key.pem"))},
	         {"__TEST_CERT_PATH__", Quote(PathUtil::ToUtf8(Fixtures / "loopback-cert.pem"))},
	         {"__TEST_FILE_ROOT__", Quote(PathUtil::ToUtf8(FileRoot))}})
	{
		std::size_t Position = 0;
		while ((Position = EffectiveSource.find(Entry.first, Position)) != std::string::npos)
		{
			EffectiveSource.replace(Position, Entry.first.size(), Entry.second);
			Position += Entry.second.size();
		}
	}

	if (!bExplicitClose)
	{
		EffectiveSource = std::regex_replace(EffectiveSource,
		    std::regex(R"(\w+\.close\(\);|context\.shutdown\(\);|if \(!context.closed\(\)\) return 12;)"),
		    "");
	}
	auto Code = VeroScriptCompile::CompileSource(EffectiveSource, Root + "/network-bindings.vs", Options);
	if (!Code.bSuccess)
	{
		std::cerr << Code.Error.Location.Line << ':' << Code.Error.Message << '\n';
		return false;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
	VeroScript::Test::Configure(Vm);
	if (!ConfigureVm(Vm) || !Vm.SetModuleEnvironment(Environment, Error) || !Vm.Execute(*Code.Module, ERunMode::Script, {}) ||
	    Vm.GetProcessExitCode() != 0)
	{
		std::cerr << Vm.GetError().Location.Line << ':' << Vm.GetError().Message << ' ' << Error
		          << " exit=" << Vm.GetProcessExitCode() << '\n'
		          << Output.str();
		return false;
	}
	if (bExplicitClose)
	{
		for (const auto* Negative :
		    {"int main(){Network::context missing=nullptr;Network::resolve(missing,\"localhost\",80,1000);return 0;}",
		        R"VS(int main(){
Network::context c=Network::create_context(false);
Network::operation op=Network::resolve(c,"localhost",80,5000);
op.wait(5000);
op.on_complete([c](Network::operation completed)mutable->void{c.dispatch(1);});
c.dispatch(1);return 0;})VS",
		        R"VS(int main(){
Network::context c=Network::create_context(false);
Network::operation op=Network::resolve(c,"localhost",80,5000);
op.wait(5000);
op.on_complete([](Network::operation completed)->void{assert(false);});
c.dispatch(1);return 0;})VS",
		        R"VS(int main(){
Network::context c=Network::create_context(false);
Network::operation op=Network::resolve(c,"localhost",80,5000);
op.on_complete([](Network::operation completed)->void{});
op.on_complete([](Network::operation completed)->void{});return 0;})VS",
		        "int main(){Network::upgrade_source "
		        "missing=nullptr;websocket::accept_upgrade(missing,websocket::make_options());return 0;}"})
		{
			auto NegativeCode = VeroScriptCompile::CompileSource(Negative, Root + "/network-null-argument.vs", Options);
			if (!NegativeCode.bSuccess)
			{
				std::cerr << NegativeCode.Error.Message << '\n';
				return false;
			}
			std::ostringstream NegativeOutput;
			VeroScriptVm NegativeVm(NegativeOutput);
			VeroScript::Test::Configure(NegativeVm);
			if (!ConfigureVm(NegativeVm) || !NegativeVm.SetModuleEnvironment(Environment, Error) ||
			    NegativeVm.Execute(*NegativeCode.Module, ERunMode::Script, {}) || NegativeVm.GetError().Message.empty())
			{
				std::cerr << "Null network objects must fail as script errors\n";
				return false;
			}
		}
		const auto Network = PathUtil::FromUtf8(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
		const auto ExampleBase = PathUtil::ToUtf8(FileRoot);
		for (const auto& Relative : {"VeroScriptHttp/Resources/Examples/multipart_stream.vs",
		         "VeroScriptNetwork/Resources/Examples/completion.vs",
		         "VeroScriptHttp/Resources/Examples/completion.vs",
		         "VeroScriptWebSocket/Resources/Examples/completion.vs",
		         "VeroScriptHttp/Resources/Examples/websocket_upgrade.vs",
		         "VeroScriptHttp/Resources/Examples/redirect_trace.vs",
		         "VeroScriptWebSocket/Resources/Examples/compression.vs",
		         "VeroScriptNetwork/Resources/Examples/file_stream.vs",
		         "VeroScriptHttp/Resources/Examples/file_transfer.vs"})
		{
			const auto Example = Network / Relative;
			auto ExampleCode =
			    VeroScriptCompile::CompileSource(Read(Example), Root + "/multipart-stream-example.vs", Options);
			if (!ExampleCode.bSuccess)
			{
				std::cerr << ExampleCode.Error.Location.Line << ':' << ExampleCode.Error.Message << '\n';
				return false;
			}
			std::ostringstream ExampleOutput;
			VeroScriptVm ExampleVm(ExampleOutput);
			VeroScript::Test::Configure(ExampleVm);
			if (!ConfigureVm(ExampleVm) || !ExampleVm.SetModuleEnvironment(Environment, Error) ||
			    !ExampleVm.Execute(*ExampleCode.Module,
			        ERunMode::Script,
			        (Example.filename() == "multipart_stream.vs" || Example.filename() == "websocket_upgrade.vs")
			            ? std::vector<std::string_view>{}
			            : std::vector<std::string_view>{ExampleBase}) ||
			    ExampleVm.GetProcessExitCode() != 0)
			{
				std::cerr << ExampleVm.GetError().Location.Line << ':' << ExampleVm.GetError().Message
				          << " example exit=" << ExampleVm.GetProcessExitCode() << '\n';
				return false;
			}
		}
	}
	std::filesystem::remove_all(FileRoot);
	return true;
}

static int RunTests(int Argc, char** Argv)
{
	if (Argc == 2 || Argc == 5)
	{
		std::string Error;
		TestFactory = LoadJitBackend(Argv[Argc - 1], Error);
		if (!TestFactory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
	{
		if (Backend != EExecutionBackend::Vm && !TestFactory)
		{
			continue;
		}
		TestBackend = Backend;
		std::vector<std::weak_ptr<int>> OwnerProbes;
		if (!Run({}, true, &OwnerProbes) || !Run({}, false, &OwnerProbes)) return 1;
		for (const auto& Owner : OwnerProbes)
		{
			if (!Owner.expired())
			{
				std::cerr << "Native registration or context code owner leaked\n";
				return 1;
			}
		}
		if (Argc == 4 || Argc == 5)
		{
			const std::vector<std::string> Paths = {
			    PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[1]))),
			    PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[2]))),
			    PathUtil::ToUtf8(std::filesystem::absolute(PathUtil::FromUtf8(Argv[3])))};
			if (!Run(Paths) || !Run(Paths, false)) return 1;
		}
	}
	std::cout << "Network static/dynamic registration and selected execution backends passed\n";
	return 0;
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
