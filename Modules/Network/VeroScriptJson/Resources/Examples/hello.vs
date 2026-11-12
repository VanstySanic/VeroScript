// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// The embedding host must load VeroScriptJson before compiling this example.
int main()
{
	json::value greeting = json::object();
	greeting.set("message", json::from_string("Hello, JSON!"));
	json::value decoded = json::parse(greeting.write());
	if (decoded.get("message").as_string() != "Hello, JSON!") return 1;
	return 0;
}
