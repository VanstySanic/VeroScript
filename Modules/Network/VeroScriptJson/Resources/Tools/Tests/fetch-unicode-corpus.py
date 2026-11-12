#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Explicitly fetch the pinned Unicode 17 NFC qualification corpus."""
import argparse
import hashlib
from pathlib import Path
import urllib.request

URL = 'https://www.unicode.org/Public/17.0.0/ucd/NormalizationTest.txt'
SHA256 = '5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db'


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('directory', type=Path)
	args = parser.parse_args()
	output = args.directory / 'NormalizationTest.txt'
	data = output.read_bytes() if output.exists() else urllib.request.urlopen(URL, timeout=60).read()
	if hashlib.sha256(data).hexdigest() != SHA256:
		raise ValueError('Unicode normalization corpus checksum mismatch')
	args.directory.mkdir(parents=True, exist_ok=True)
	if not output.exists():
		output.write_bytes(data)
	print('Pinned Unicode 17 normalization corpus: ' + str(output))


if __name__ == '__main__':
	main()
