// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network and websocket. Arguments: proxy URL, ws/wss URL, proxy Authorization, optional CA PEM text.
int main(string[] args)
{
	if (len(args) < 3 || len(args) > 4) return 1;
	Network::context context = Network::create_context(false);
	websocket::options options = websocket::make_options();
	options.set_proxy(args[0], args[2]);
	websocket::options copy = options;
	options.clear_proxy();
	assert(options.proxy_url() == "" && copy.proxy_url() == args[0]);
	copy.set_timeout(5000);
	if (len(args) == 4)
	{
		Network::tls_options tls = Network::make_tls_options();
		tls.set_trust_pem(args[3]);
		copy.set_tls(tls);
	}
	websocket::operation connecting = websocket::connect_with_options(context, args[1], copy);
	if (connecting.wait(10000) != 0 || connecting.error_code() != 0) return 2;
	websocket::connection connection = connecting.connection();
	websocket::operation receiving = connection.receive(5000);
	websocket::operation sending = connection.send_text("hello through CONNECT", 5000);
	if (sending.wait(10000) != 0 || sending.error_code() != 0) return 3;
	if (receiving.wait(10000) != 0 || receiving.error_code() != 0 || receiving.data() != "hello through CONNECT") return 4;
	websocket::operation closing = connection.close(1000, "done", 5000);
	if (closing.wait(10000) != 0 || closing.error_code() != 0 || !closing.clean()) return 5;
	context.shutdown();
	return 0;
}
