// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load the network and http modules before running this example. Uses a local ephemeral port.
int main()
{
	Network::context context = Network::create_context(false);
	http::server_options options = http::make_server_options();
	options.set_stream_requests(true);
	http::server server = http::listen_with_options(context, "127.0.0.1", 0, options);
	http::client client = http::create_client(context);

	Network::byte_pipe pipe = Network::create_byte_pipe(context, 64);
	Network::writable_stream writer = pipe.writer();
	http::request request = http::create_request("POST", "http://127.0.0.1:" + to_string(server.port()) + "/upload");
	request.begin_multipart();
	request.add_field("title", "example");
	request.add_file_stream("file", "hello.bin", "application/octet-stream", pipe.reader(), -1);

	http::operation accepting = server.accept(10000);
	http::operation sending = client.send(request);
	if (accepting.wait(10000) != 0 || accepting.error_code() != 0) return 1;
	http::exchange exchange = accepting.exchange();

	// The server already has the headers while the producer is still open.
	if (sending.done() || writer.write("hello\0world") != 11) return 2;
	writer.finish();

	Network::readable_stream body = exchange.body_stream();
	http::multipart_reader reader = http::make_multipart_reader(context, exchange.header("content-type", 0));
	string pending = "";
	string file_bytes = "";
	bool ended = false;
	int begins = 0;
	int ends = 0;
	int closed = 0;
	// A single-byte input makes the unconsumed suffix either the same byte or empty.
	// Larger blocks should preserve and resubmit their unconsumed suffix.
	for (int round = 0; round < 10000 && reader.state() != 2 && reader.state() != 3; ++round)
	{
		if (reader.state() != 1 && pending == "" && !ended)
		{
			pending = body.read(1);
			ended = body.state() == 2;
			if (body.state() == 3) return 3;
			if (pending == "" && !ended)
			{
				std::this_thread::sleep(1);
				continue;
			}
		}
		int64 consumed = reader.feed(pending, ended);
		if (consumed == len(pending))
		{
			pending = "";
		}
		else if (consumed != 0)
		{
			return 4;
		}
		if (reader.state() == 1)
		{
			http::multipart_event event = reader.event();
			if (event.kind() == 0)
			{
				++begins;
			}
			else if (event.kind() == 1 && event.part_index() == 1)
			{
				file_bytes += event.data();
			}
			else if (event.kind() == 2)
			{
				++ends;
			}
			else if (event.kind() == 3)
			{
				++closed;
			}
		}
	}
	if (reader.state() != 2 || begins != 2 || ends != 2 || closed != 1 || file_bytes != "hello\0world") return 5;
	http::operation received = exchange.body_operation();
	if (received.wait(10000) != 0 || received.error_code() != 0) return 6;
	if (exchange.reply(200, "uploaded", 10000).wait(10000) != 0) return 7;
	if (sending.wait(10000) != 0 || sending.error_code() != 0 || sending.response().body() != "uploaded") return 8;
	http::operation stopped = server.shutdown(1000, 10000);
	if (stopped.wait(10000) != 0 || stopped.error_code() != 0 || stopped.shutdown_forced()) return 9;
	context.shutdown();
	return 0;
}
