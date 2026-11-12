// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network and websocket. Compression is negotiated independently in each direction.
int main()
{
	if (!websocket::compression_supported()) return 0;
	websocket::compression_options compression = websocket::make_compression_options();
	compression.set_client_window_bits(12);
	compression.set_server_window_bits(12);
	compression.set_client_no_context_takeover(true);
	compression.set_server_no_context_takeover(true);
	compression.set_level(6);
	compression.set_memory_level(4);
	websocket::options options = websocket::make_options();
	options.set_compression(true);
	options.set_compression_options(compression);
	// Options are copied; a later change does not alter the submitted options.
	compression.set_client_window_bits(15);
	if (options.compression_options().client_window_bits() != 12) return 1;
	Network::context context = Network::create_context(false);
	websocket::server server = websocket::listen_with_options(context, "127.0.0.1", 0, options);
	websocket::operation accepting = server.accept(10000);
	websocket::operation connecting = websocket::connect_with_options(context, "ws://127.0.0.1:" + to_string(server.port()) + "/", options);
	if (connecting.wait(10000) != 0 || connecting.error_code() != 0 || accepting.wait(10000) != 0 || accepting.error_code() != 0) return 2;
	websocket::connection client = connecting.connection();
	websocket::connection peer = accepting.connection();
	websocket::compression_info actual = client.compression();
	if (!actual.enabled() || actual.client_window_bits() != 12 || actual.server_window_bits() != 12 || !actual.client_no_context_takeover() || !actual.server_no_context_takeover() || !peer.compression().enabled()) return 3;
	websocket::operation receive = peer.receive(10000);
	websocket::operation send = client.send_text("hello compressed world", 10000);
	if (send.wait(10000) != 0 || send.error_code() != 0 || receive.wait(10000) != 0 || receive.error_code() != 0 || receive.data() != "hello compressed world") return 4;
	websocket::operation closed = client.close(1000, "done", 10000);
	if (closed.wait(10000) != 0 || closed.error_code() != 0 || !closed.clean()) return 5;
	server.close();
	context.shutdown();
	if (!actual.enabled() || client.compression().client_window_bits() != 12) return 6;
	return 0;
}
