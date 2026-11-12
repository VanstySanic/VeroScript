# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Configuration rejection must stop CLI compilation, export and execution."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
	executable, destination = (Path(argument).resolve() for argument in sys.argv[1:3])
	records = []
	environment = dict(os.environ)
	# Shared-library builds can still locate their Core dependencies. The product
	# VM is deliberately absent from the copied executable's own Vm directory.
	for variable in ("PATH", "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"):
		environment[variable] = str(executable.parent) + os.pathsep + environment.get(variable, "")
	with tempfile.TemporaryDirectory(prefix="vero cli configuration ") as directory:
		root = Path(directory)
		isolated = root / executable.name
		shutil.copy2(executable, isolated)
		source = root / "main.vs"
		text = 'void main(){print("executed");}\n'
		source.write_text(text, encoding="utf-8")
		seed = root / "seed.vsapp"

		def run(name, binary, arguments):
			command = [str(binary), *map(str, arguments)]
			result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
				encoding="utf-8", errors="replace", timeout=20)
			records.append({"name": name, "command": command, "document": text, "returncode": result.returncode,
				"stdout": result.stdout, "stderr": result.stderr})
			return result

		try:
			checked = run("healthy_check", executable, ["--check", source])
			assert checked.returncode == 0, records[-1]
			exported = run("healthy_export", executable, ["--emit-package", seed, source])
			assert exported.returncode == 0 and seed.is_file(), records[-1]
			records[-1]["artifactSha256"] = hashlib.sha256(seed.read_bytes()).hexdigest()
			for name, arguments in (("source", [source]), ("package", [seed])):
				executed = run("healthy_run_" + name, executable, arguments)
				assert executed.returncode == 0 and executed.stdout == "executed\n", records[-1]
			for name, arguments in (
				("check", ["--check", source]),
				("export", ["--emit-package", root / "rejected.vsapp", source]),
				("run_source", [source]),
				("run_package", [seed]),
			):
				result = run("rejected_" + name, isolated, arguments)
				assert result.returncode == 2 and result.stderr and not result.stdout, records[-1]
				assert not (root / "rejected.vsapp").exists(), records[-1]
			assert not (root / "Vm").exists()
		finally:
			destination.parent.mkdir(parents=True, exist_ok=True)
			destination.write_text(json.dumps({"binary": {"path": str(executable),
				"sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}, "cases": records},
				ensure_ascii=False, indent=2), encoding="utf-8", newline="\n")
	print(f"{len(records)} CLI configuration cases passed; evidence: {destination}")


if __name__ == "__main__":
	main()
