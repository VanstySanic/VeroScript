// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// priority_queue.vs — maximum-priority-first queue
// Run from this directory: veroscript priority_queue.vs

void main()
{
	priority_queue<int> work = {3, 1, 5, 2};
	print("initial top: {}", work.top());
	print("pop: {}", work.pop());
	print("next top: {}", work.top());

	work.push(9).push(4);
	print("new maximum: {}", work.pop());
	print("remaining: {}", len(work));

	print("priority order:");
	for (int value : work)
	{
		print(value);
	}
}
