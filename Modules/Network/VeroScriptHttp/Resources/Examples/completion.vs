// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class CompletionCount
{
public:
	int value = 0;
};

int main()
{
	Network::context context = Network::create_context(false);
	http::server server = http::listen(context, "127.0.0.1", 0);
	http::client client = http::create_client(context);
	CompletionCount count = CompletionCount();
	http::operation incoming = server.accept(5000);
	incoming.on_complete([count](http::operation completed) mutable -> void
	{
		assert(completed.error_code() == 0); completed.exchange().reply(200, "completion", 5000); count.value += 1;
	});
	http::request request = http::create_request("GET", "http://127.0.0.1:" + to_string(server.port()) + "/");
	http::operation outgoing = client.send(request);
	outgoing.on_complete([count](http::operation completed) mutable -> void
	{
		assert(completed.error_code() == 0); assert(completed.response().status() == 200); assert(completed.response().body() == "completion"); count.value += 1;
	});
	for (int round = 0; round < 10000 && count.value != 2; ++round)
	{
		context.dispatch(64);
		if (count.value != 2)
		{
			std::this_thread::sleep(1);
		}
	}
	assert(count.value == 2);
	server.close();
	context.shutdown();
	return 0;
}
