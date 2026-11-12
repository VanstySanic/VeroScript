# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Fetch the pinned official JSON Schema corpus for an explicit test run."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import urllib.request

COMMIT = "55719286c33ae0d1823bfb65fea10338a19bc37c"
SHA256 = "c65cf5b4152b9edfc71644252249e9402d3563ddaeb6f0fa80aeef2a85cefcf7"
URL = f"https://codeload.github.com/json-schema-org/JSON-Schema-Test-Suite/tar.gz/{COMMIT}"


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("destination", type=Path)
	args = parser.parse_args()
	destination = args.destination.resolve()
	if destination.exists() and any(destination.iterdir()):
		raise RuntimeError("The destination must be empty")
	data = urllib.request.urlopen(URL, timeout=60).read()
	if hashlib.sha256(data).hexdigest() != SHA256:
		raise RuntimeError("The pinned corpus archive checksum differs")
	destination.mkdir(parents=True, exist_ok=True)
	with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
		for member in archive:
			relative = Path(*Path(member.name).parts[1:])
			if not member.isfile() or not relative.parts:
				continue
			if relative.parts[0] not in {"tests", "remotes", "LICENSE"}:
				continue
			target = (destination / relative).resolve()
			if not target.is_relative_to(destination):
				raise ValueError("Invalid fixture path")
			target.parent.mkdir(parents=True, exist_ok=True)
			target.write_bytes(archive.extractfile(member).read())
	(destination / "source.json").write_text(
		json.dumps({"commit": COMMIT, "url": URL, "sha256": SHA256}, indent=2) + "\n", encoding="utf-8"
	)


if __name__ == "__main__":
	main()
