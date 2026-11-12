// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network and http. Pass one existing absolute directory. Uses a local ephemeral port.
// Replaces only veroscript-http-partial.bin and veroscript-http-download.bin in that directory.
int main(string[] args)
{
	if (len(args) != 1) return 1;
	Network::context context = Network::create_context(false);
	Network::file_executor files = Network::create_file_executor(context);
	Network::byte_pipe pipe = Network::create_byte_pipe(context, 64);
	pipe.writer().write("file\0");
	pipe.writer().finish();
	Network::file_write_options partial = Network::make_file_write_options("veroscript-http-partial.bin", args[0]);
	partial.set_expected_length(5);
	partial.set_commit_options(true, false, true);
	Network::file_operation saving = files.write(partial, pipe.reader());
	if (saving.operation().wait(10000) != 0 || saving.operation().error_code() != 0) return 2;

	http::server server = http::listen(context, "127.0.0.1", 0);
	http::client client = http::create_client(context);
	string url = "http://127.0.0.1:" + to_string(server.port()) + "/file";
	Network::file_read_options input = Network::make_file_read_options("veroscript-http-partial.bin", args[0]);
	http::request upload = http::create_request("POST", url);
	upload.set_upload_file(files, input);
	http::operation accepted = server.accept(10000);
	http::operation sent = client.send(upload);
	if (accepted.wait(10000) != 0 || accepted.error_code() != 0) return 3;
	http::exchange exchange = accepted.exchange();
	if (exchange.body() != "file\0") return 4;
	http::operation replied = exchange.reply(200, "uploaded", 10000);
	if (replied.wait(10000) != 0 || replied.error_code() != 0 || sent.wait(10000) != 0 || sent.error_code() != 0) return 5;

	http::request get = http::create_request("GET", url);
	http::download_options options = http::make_download_options("veroscript-http-download.bin", args[0]);
	Network::file_write_options output = Network::make_file_write_options("veroscript-http-download.bin", args[0]);
	output.set_expected_length(12);
	output.set_commit_options(true, false, true);
	options.set_file_options(output);
	options.set_resume("veroscript-http-partial.bin", 5, "\"example-v1\"", false);
	accepted = server.accept(10000);
	http::download_operation download = client.download_file(files, get, options);
	if (accepted.wait(10000) != 0 || accepted.error_code() != 0) return 6;
	exchange = accepted.exchange();
	if (exchange.header("range", 0) != "bytes=5-" || exchange.header("if-range", 0) != "\"example-v1\"") return 7;
	http::response_builder response = http::create_response(206, "payload");
	response.add_header("ETag", "\"example-v1\"");
	response.add_header("Content-Range", "bytes 5-11/12");
	replied = exchange.reply_response(response, 10000);
	if (replied.wait(10000) != 0 || replied.error_code() != 0 || download.operation().wait(10000) != 0 || download.operation().error_code() != 0) return 8;
	http::download_progress progress = download.progress();
	if (!progress.resumed() || progress.already_complete() || progress.received_bytes() != 7 || progress.representation_bytes() != 12 || progress.file().bytes() != 12 || progress.file().commit_state() != 2) return 9;

	input = Network::make_file_read_options("veroscript-http-download.bin", args[0]);
	input.set_timeout(10000);
	Network::file_operation reading = files.read(input);
	Network::readable_stream bytes = reading.body();
	string actual = "";
	for (int round = 0; round < 10000 && bytes.state() != 2 && bytes.state() != 3; ++round)
	{
		actual += bytes.read(4);
		if (bytes.state() == 1)
		{
			std::this_thread::sleep(1);
		}
	}
	if (bytes.state() != 2 || actual != "file\0payload" || reading.operation().wait(10000) != 0 || reading.operation().error_code() != 0) return 10;
	server.close();
	files.close();
	context.shutdown();
	return 0;
}
