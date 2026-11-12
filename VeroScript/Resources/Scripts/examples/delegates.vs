// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Typed delegates, rebinding, and multicast snapshots (zero to three parameters).

import "delegate.vs";

void main()
{
	std::multicast_delegate onClick = std::multicast_delegate();
	function() -> void firstListener = function() -> void => print(1);
	fn() -> void secondListener = fn() -> void
	{
		print(2);
	};
	onClick += firstListener;
	onClick += secondListener;
	onClick.broadcast();
	onClick -= firstListener;
	onClick.broadcast();
	assert(onClick.count() == 1);
	onClick.clear();
	assert(onClick.count() == 0);

	std::multicast_delegate<int> onScore = std::multicast_delegate<int>();
	fn(int) -> void onPoints = fn(int x) -> void => print(x);
	onScore.add(onPoints);
	onScore.broadcast(42);
	onScore.remove(onPoints);
	assert(onScore.count() == 0);

	std::delegate<int, int> mitigate = std::delegate<int, int>();
	assert(!mitigate.execute_if_bound(10));
	mitigate.bind(function(int dmg) -> int => dmg / 2);
	assert(mitigate.is_bound());
	assert(mitigate.execute_if_bound(10));
	assert(mitigate.execute(10) == 5);
	mitigate.unbind();
	assert(!mitigate.is_bound());
	assert(!mitigate.execute_if_bound(10));
	mitigate.bind(function(int dmg) -> int => dmg - 1);
	assert(mitigate.execute(10) == 9);

	// Both public spellings refer to the same type. Captures follow normal lambda rules.
	delegate<int> captured = std::delegate<int>();
	int offset = 5;
	captured.bind([offset]() -> int => offset);
	offset = 10;
	assert(captured.execute() == 5);

	std::delegate<int, int, int> sum = std::delegate<int, int, int>();
	sum.bind(fn(int a, int b) -> int => a + b);
	assert(sum.execute(2, 3) == 5);

	std::delegate<int, int, int, int> product = std::delegate<int, int, int, int>();
	product.bind(fn(int a, int b, int c) -> int => a * b * c);
	assert(product.execute(2, 3, 4) == 24);

	std::delegate<void> notification = std::delegate<void>();
	notification.bind(fn() -> void
	{
		assert(true);
	});
	notification.execute();
	notification.unbind();
	assert(!notification.execute_if_bound());

	std::multicast_delegate<int, string> onPair = std::multicast_delegate<int, string>();
	fn(int, string) -> void show = fn(int n, string s) -> void
	{
		print(n);
		print(s);
	};
	onPair.add(show);
	onPair.broadcast(7, "ok");

	std::multicast_delegate<int, int, int> onTriple = std::multicast_delegate<int, int, int>();
	function(int, int, int) -> void showProduct = function(int a, int b, int c) -> void => print(a * b * c);
	onTriple.add(showProduct);
	onTriple.broadcast(2, 3, 4);

	// Removing a listener during broadcast takes effect on the next broadcast.
	std::multicast_delegate snapshot = std::multicast_delegate();
	std::array<int> calls = std::array<int>();
	fn() -> void later = [&calls]() -> void
	{
		calls.push(2);
	};
	fn() -> void first = [&calls, &snapshot, later]() -> void
	{
		calls.push(1);
		snapshot.remove(later);
	};
	snapshot.add(first);
	snapshot.add(later);
	snapshot.broadcast();
	assert(calls[0] == 1 && calls[1] == 2 && snapshot.count() == 1);
	snapshot.broadcast();
	assert(calls[2] == 1 && std::len(calls) == 3);
	print("delegates: ok");
}
