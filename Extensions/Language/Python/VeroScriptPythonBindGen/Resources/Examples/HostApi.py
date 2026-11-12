# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from enum import IntEnum


class Mode(IntEnum):
	OFF = 0
	READ = 1 << 0
	WRITE = 1 << 1
	ALL = READ | WRITE


@dataclass
class Counter:
	value: int

	def __init__(self, initial: int) -> None:
		self.value = initial

	def add(self, amount: int) -> int:
		return self.value + amount


def combine(left: int, right: int) -> int:
	return left + right
