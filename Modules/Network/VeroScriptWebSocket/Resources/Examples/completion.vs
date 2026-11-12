// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class CompletionState
{
public:
	int count = 0;
	websocket::connection client = nullptr;
	websocket::connection peer = nullptr;
};

int main()
{
	Network::context context = Network::create_context(false);
	websocket::server server = websocket::listen(context, "127.0.0.1", 0);
	CompletionState state = CompletionState();
	websocket::operation accepting = server.accept(5000);
	accepting.on_complete([state](websocket::operation completed) mutable -> void
	{
		assert(completed.error_code() == 0); state.peer = completed.connection(); websocket::operation reading = state.peer.receive(5000); reading.on_complete([state](websocket::operation message) mutable -> void
		{
			assert(message.error_code() == 0 && message.data() == "completion"); state.count += 1;
		}); state.count += 1;
	});
	websocket::operation connecting = websocket::connect(context, "ws://127.0.0.1:" + to_string(server.port()) + "/", 5000);
	connecting.on_complete([state](websocket::operation completed) mutable -> void
	{
		assert(completed.error_code() == 0); state.client = completed.connection(); state.client.send_text("completion", 5000); state.count += 1;
	});
	for (int round = 0; round < 10000 && state.count != 3; ++round)
	{
		context.dispatch(64);
		if (state.count != 3)
		{
			std::this_thread::sleep(1);
		}
	}
	assert(state.count == 3);
	state.client.close(1000, "done", 5000);
	server.close();
	context.shutdown();
	return 0;
}
