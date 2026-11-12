// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network, http and websocket. HTTP and WebSocket share one local ephemeral port.
int main()
{
	Network::context context = Network::create_context(false);
	http::server_options server_options = http::make_server_options();
	server_options.set_stream_requests(true);
	http::server server = http::listen_with_options(context, "127.0.0.1", 0, server_options);
	websocket::options options = websocket::make_options();
	options.add_subprotocol("vero.example");
	options.set_require_subprotocol(true);
	options.set_timeout(10000);
	http::operation accepting = server.accept(10000);
	websocket::operation connecting = websocket::connect_with_options(context, "ws://127.0.0.1:" + to_string(server.port()) + "/socket", options);
	if (accepting.wait(10000) != 0 || accepting.error_code() != 0) return 1;
	http::exchange exchange = accepting.exchange();
	Network::upgrade_source source = exchange.prepare_upgrade();
	// Preparation keeps HTTP ownership; no 101 has been sent at this point.
	if (source.consumed()) return 2;
	websocket::operation accepting_websocket = websocket::accept_upgrade(source, options);
	if (accepting_websocket.wait(10000) != 0 || accepting_websocket.error_code() != 0 || connecting.wait(10000) != 0 || connecting.error_code() != 0 || !source.consumed()) return 3;
	websocket::connection peer = accepting_websocket.connection();
	websocket::connection client = connecting.connection();
	websocket::operation reading = peer.receive(10000);
	websocket::operation sending = client.send_binary("hello\0world", 10000);
	if (sending.wait(10000) != 0 || sending.error_code() != 0 || reading.wait(10000) != 0 || reading.error_code() != 0 || reading.data() != "hello\0world") return 4;
	// HTTP shutdown releases the listener; the adopted connection now has its own lifetime.
	http::operation stopped = server.shutdown(100, 10000);
	if (stopped.wait(10000) != 0 || stopped.error_code() != 0 || stopped.shutdown_forced()) return 5;
	websocket::operation closed = client.close(1000, "done", 10000);
	if (closed.wait(10000) != 0 || closed.error_code() != 0 || !closed.clean()) return 6;
	context.shutdown();
	return 0;
}
