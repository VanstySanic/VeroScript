# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

class Counter:
	value: int = 0

	def __init__(self, initial: int) -> None:
		self.value = initial

	def increment(self, amount: int) -> int:
		self.value += amount
		return self.value


def add(left: int, right: int) -> int:
	return left + right


def main() -> int:
	counter: Counter = Counter(2)
	return 0 if add(counter.increment(3), 4) == 9 else 1
