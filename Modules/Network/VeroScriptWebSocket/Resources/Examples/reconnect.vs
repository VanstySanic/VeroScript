// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network and websocket. Arguments: proxy URL (or empty), ws/wss echo URL, proxy Authorization, optional CA PEM text.
int main(string[] args)
{
	if (len(args) < 3 || len(args) > 4) return 1;
	Network::context context = Network::create_context(false);
	websocket::options options = websocket::make_options();
	options.set_proxy(args[0], args[2]);
	options.set_timeout(2000);
	if (len(args) == 4)
	{
		Network::tls_options tls = Network::make_tls_options();
		tls.set_trust_pem(args[3]);
		options.set_tls(tls);
	}
	websocket::reconnect_policy policy = websocket::make_reconnect_policy();
	assert(policy.max_attempts() == 1);
	websocket::reconnect_policy initial = policy;
	policy.set_limits(3, 5000);
	policy.set_backoff(10, 100, true);
	policy.set_retry_protocol_errors(true);
	assert(initial.max_attempts() == 1 && policy.max_attempts() == 3);
	websocket::reconnect_task task = websocket::connect_with_retry(context, args[1], options, policy);
	websocket::operation opening = task.operation();
	if (opening.wait(10000) != 0 || opening.error_code() != 0) return 2;
	websocket::reconnect_snapshot history = task.snapshot();
	assert(history.is_final() && history.error_code() == 0 && history.generation() > 0);
	assert(history.message_id() == "");
	assert(history.size() >= 1 && history.size() <= 3);
	websocket::reconnect_attempt last = history.at(history.size() - 1);
	assert(last.complete() && last.connected() && last.error_code() == 0);
	assert(last.message_id() == "");
	// Keep an attempt after releasing its original snapshot; it owns the immutable report.
	history = nullptr;
	assert(last.number() >= 1 && last.number() <= 3);
	websocket::connection connection = opening.connection();
	websocket::operation receiving = connection.receive(5000);
	websocket::operation sending = connection.send_text("one explicit message", 5000);
	if (sending.wait(10000) != 0 || sending.error_code() != 0) return 3;
	if (receiving.wait(10000) != 0 || receiving.error_code() != 0 || receiving.data() != "one explicit message") return 4;
	websocket::operation closing = connection.close(1000, "done", 5000);
	if (closing.wait(10000) != 0 || !closing.clean()) return 5;
	context.shutdown();
	assert(task.snapshot().is_final());
	return 0;
}
