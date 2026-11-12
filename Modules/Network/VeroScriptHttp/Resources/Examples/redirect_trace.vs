// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network and http. The local server redirects a GET to a second resource.
int main()
{
	Network::context context = Network::create_context(false);
	http::server server = http::listen(context, "127.0.0.1", 0);
	http::client client = http::create_client(context);
	string base = "http://127.0.0.1:" + to_string(server.port());
	http::request request = http::create_request("GET", base + "/start");
	request.follow_redirects(true);
	request.set_redirect_limit(4);
	http::operation accepting = server.accept(10000);
	http::operation sending = client.send(request);
	if (accepting.wait(10000) != 0 || accepting.error_code() != 0) return 1;
	http::exchange first = accepting.exchange();
	http::response_builder first_response = http::create_response(302, "");
	first_response.add_header("Location", "/finish");
	accepting = server.accept(10000);
	http::operation replied = first.reply_response(first_response, 10000);
	if (replied.wait(10000) != 0 || replied.error_code() != 0 || accepting.wait(10000) != 0 || accepting.error_code() != 0) return 2;
	http::exchange second = accepting.exchange();
	if (second.target() != "/finish" || second.method() != "GET") return 3;
	replied = second.reply(200, "hello", 10000);
	if (replied.wait(10000) != 0 || sending.wait(10000) != 0 || sending.error_code() != 0) return 4;
	http::response response = sending.response();
	http::transfer_details details = response.details();
	http::response_head redirect = details.response(0);
	http::transfer_statistics statistics = details.statistics();
	if (response.body() != "hello" || details.response_count() != 2 || redirect.status() != 302 || redirect.url() != base + "/start" || redirect.location(0) != "/finish" || !redirect.redirect_followed() || redirect.redirect_url() != base + "/finish" || redirect.redirect_method() != "GET") return 5;
	if (!statistics.is_final() || statistics.followed_redirects() != 1 || statistics.accepted_body_bytes() != 5 || statistics.last_transfer_downloaded_bytes() != 5 || statistics.total_time_us() < 0) return 6;
	client.close();
	server.close();
	context.shutdown();
	// The immutable result remains valid after context shutdown.
	if (details.response(1).status() != 200 || response.attempt(0).details().response_count() != 2) return 7;
	return 0;
}
