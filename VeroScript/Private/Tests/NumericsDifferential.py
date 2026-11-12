# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Independent exact-arithmetic oracle. Uses only the Python standard library."""
import fractions
import random
import subprocess
import sys
import struct
import math

rng = random.Random(0xB16A710)
inputs, expected = [], []


def text(value):
	if isinstance(value, fractions.Fraction):
		return str(value.numerator) if value.denominator == 1 else str(value.numerator) + "/" + str(value.denominator)
	return str(value)


def cases(kind, a, b):
	q = abs(a / b) if kind == "r" else abs(a) // abs(b)
	q = int(q) * (-1 if (a < 0) != (b < 0) else 1)
	results = [a + b, a - b, a * b, a / b if kind == "r" else q, a - q * b]
	if kind == "b":
		results += [a & b, a | b, a ^ b]
	for op, result in enumerate(results):
		inputs.append(f"{kind} {op} {text(a)} {text(b)}")
		expected.append(text(result))


for i in range(1300):
	bits = rng.choice([1, 31, 32, 33, 63, 64, 65, 96, 127, 128, 256, 512, 1024, 2048])
	a = rng.getrandbits(bits) * rng.choice([-1, 1])
	b = (rng.getrandbits(rng.randint(1, bits)) or 1) * rng.choice([-1, 1])
	cases("b", a, b)
for i in range(350):
	a = fractions.Fraction(rng.getrandbits(256) * rng.choice([-1, 1]), rng.getrandbits(256) or 1)
	b = fractions.Fraction((rng.getrandbits(256) or 1) * rng.choice([-1, 1]), rng.getrandbits(256) or 1)
	cases("r", a, b)

for i in range(1200):
	bits = rng.getrandbits(64)
	value = struct.unpack("<d", struct.pack("<Q", bits))[0]
	if not math.isfinite(value) or value == 0:
		continue
	exact = fractions.Fraction(value)
	for a in [exact, exact + exact / (1 << 54)]:
		try:
			wanted = struct.unpack("<Q", struct.pack("<d", float(a)))[0]
		except OverflowError:
			continue
		inputs.append(f"d 0 {text(a)} 0")
		expected.append(str(wanted))

# This value lies just above a binary32 midpoint, but rounds to that midpoint
# in binary64. A double intermediate would incorrectly produce 1.0f.
inputs.append(f"f 0 {text(fractions.Fraction(1) + fractions.Fraction(1, 1 << 24) + fractions.Fraction(1, 1 << 80))} 0")
expected.append(str(0x3F800001))


def rounding_boundaries(kind, value_format, bit_format, sign_bit, lower_values):
	# Exact midpoints between adjacent target floats supply an independent
	# oracle, including cases that a binary64 intermediate cannot distinguish.
	for lower_bits in lower_values:
		lower = fractions.Fraction(struct.unpack(value_format, struct.pack(bit_format, lower_bits))[0])
		upper = fractions.Fraction(struct.unpack(value_format, struct.pack(bit_format, lower_bits + 1))[0])
		midpoint = (lower + upper) / 2
		delta = (upper - lower) / (1 << 100)
		for source, rounded in [(midpoint - delta, lower_bits),
								(midpoint, lower_bits + (lower_bits & 1)),
								(midpoint + delta, lower_bits + 1)]:
			for negative in [False, True]:
				inputs.append(f"{kind} 0 {text(-source if negative else source)} 0")
				expected.append(str(rounded | (sign_bit if negative else 0)))


boundary_rng = random.Random(0xF10A7B0)
rounding_boundaries("f", "<f", "<I", 1 << 31,
					[0, 1, 0x007FFFFF, 0x00800000, 0x3F7FFFFF, 0x3F800000, 0x7F7FFFFE]
					+ [boundary_rng.randrange(0x7F7FFFFF) for _ in range(96)])
rounding_boundaries("d", "<d", "<Q", 1 << 63,
					[0, 1, 0x000FFFFFFFFFFFFF, 0x0010000000000000,
					 0x3FEFFFFFFFFFFFFF, 0x3FF0000000000000, 0x7FEFFFFFFFFFFFFE]
					+ [boundary_rng.randrange(0x7FEFFFFFFFFFFFFF) for _ in range(96)])

result = subprocess.run([*sys.argv[1:], "oracle"], input="\n".join(inputs) + "\n", text=True,
						capture_output=True, timeout=120, check=True)
actual = result.stdout.splitlines()
assert len(actual) == len(expected), (len(actual), len(expected), result.stderr)
for index, (value, wanted) in enumerate(zip(actual, expected)):
	assert value == wanted, (index, inputs[index], value, wanted)
print(f"{len(expected)} independent integer/Fraction differential cases passed")
