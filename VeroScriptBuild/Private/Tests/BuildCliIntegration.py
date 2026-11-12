# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise actual portable, deployed and single-file application artifacts."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def run(*args, cwd, expected=0):
	result = subprocess.run([str(value) for value in args], cwd=cwd, capture_output=True,
							text=True, encoding="utf-8", errors="backslashreplace", timeout=120)
	if result.returncode != expected:
		raise AssertionError(f"command returned {result.returncode}, expected {expected}: {args}\n{result.stdout}\n{result.stderr}")
	return result.stdout


def artifacts(text):
	return {target["id"]: {item["output"]: Path(item["path"]) for item in target["artifacts"]}
			for target in json.loads(text)["targets"]}


def configuration_names(args, root):
	project = root / "自定义 configuration project"
	project.mkdir()
	(project / "main.vs").write_text("int main(){return 17;}", encoding="utf-8")
	(project / "veroscript-project.json").write_text(json.dumps({"format": "veroscript-project", "formatVersion": "0.1.1",
		"name": "configuration-project", "version": "0.1.1"}), encoding="utf-8")
	config = {"format": "veroscript-build", "formatVersion": "0.1.1", "platform": "portable", "defaultTargets": ["application"],
			  "targets": {"application": {"kind": "application", "entry": "main.vs", "packageName": "configuration.application", "output": "app.vsapp"}}}
	external = root / "configs/release custom.json"
	external.parent.mkdir()
	external.write_text(json.dumps(config), encoding="utf-8")
	output = artifacts(run(args.cli, "build", "--path", project, "--build-file", external.relative_to(root), "--json", cwd=root))
	application = output["application"]["main"]
	with zipfile.ZipFile(application) as archive:
		assert json.loads(archive.read("veroscript-app.json"))["version"] == "0.1.1"
	run(args.cli, application, cwd=root, expected=17)
	local = project / "自定义 build.json"
	local.write_text(json.dumps(config), encoding="utf-8")
	for selected in (local.relative_to(root), local):
		plan = json.loads(run(args.cli, "build", "--build-file", selected, "--plan", "--json", cwd=root))
		assert Path(plan["projectRoot"]).resolve() == project.resolve()
	output = artifacts(run(args.cli, "build", "--build-file", local.relative_to(root),
						   "--output-directory", root / "custom-release", "--json", cwd=root))
	run(args.cli, output["application"]["main"], cwd=root, expected=17)
	old = root / "old-default-only"
	old.mkdir()
	(old / "veroscript.build.json").write_text(json.dumps({"format": "veroscript-build", "formatVersion": "0.1.1",
		"defaultTargets": ["done"], "targets": {"done": {"kind": "group"}}}), encoding="utf-8")
	run(args.cli, "build", "--path", old, "--plan", cwd=root, expected=2)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--cli", required=True, type=Path)
	parser.add_argument("--runtime", required=True, type=Path)
	parser.add_argument("--repo", required=True, type=Path)
	parser.add_argument("--work", required=True, type=Path)
	parser.add_argument("--sodium", type=Path)
	args = parser.parse_args()
	args.cli, args.runtime, args.repo, args.work = (p.resolve() for p in (args.cli, args.runtime, args.repo, args.work))
	if args.sodium:
		args.sodium = args.sodium.resolve()
	args.work.mkdir(parents=True, exist_ok=True)
	sys.path.insert(0, str(args.repo / "Resources/Tools/Packaging"))
	from veroscript_applications import stage_application_runtime
	# Retain the owned test directory on failure for diagnostics.
	root = Path(tempfile.mkdtemp(prefix="应用 build-", dir=args.work.resolve()))
	(root / "veroscript-project.json").write_text("unrelated startup-directory configuration", encoding="utf-8")
	configuration_names(args, root)
	project = root / "project with spaces"
	shutil.copytree(args.repo / "VeroScriptBuild/Resources/Examples/hello", project)
	# Keep construction side effects intact through library relocation and both
	# source/compiled application images, including raw fixed-array slot opcodes.
	(project / "src/library.vs").write_text('''
int constructed = 0;
int initial() { constructed += 1; return 21; }
export struct Item { public: int value = initial(); };
export struct Record { public: Item items[2]; };
export int answer() {
    constructed = 0;
    Record record = Record();
    assert(constructed == 2);
    Item values[2];
    assert(constructed == 4);
    return record.items[0].value + values[1].value;
}
''', encoding="utf-8")
	plan = json.loads(run(args.cli, "build", "--path", project, "--plan", "--json", cwd=root))
	assert plan["format"] == "veroscript-build-plan" and not (project / "dist").exists()
	output = artifacts(run(args.cli, "build", "--path", project, "--json", cwd=root))
	for target in ("application", "source", "directory", "zip"):
		assert "42" in run(args.cli, output[target]["main"], cwd=root)
	# Runtime defaults and explicit standard-library paths may identify the same
	# directory. External project environments must accept this for both image kinds.
	stdlib = args.cli.parent / "stdlib"
	if not stdlib.is_dir():
		stdlib = args.repo / "VeroScript/Resources/Scripts/stdlib"
	search = ("--backend=vm", "--stdlib-path", stdlib,
			  "--stdlib-path", stdlib / ".." / "stdlib", "--path", project)
	(project / "check-only.vs").write_text("int main() { return 23; }", encoding="utf-8")
	run(args.cli, *search, "--check", project / "check-only.vs", cwd=root)
	for target in ("application", "source", "directory", "zip"):
		run(args.cli, "--check", output[target]["main"], cwd=root)
		run(args.cli, *search, "--check", output[target]["main"], cwd=root)
		assert "42" in run(args.cli, *search, output[target]["main"], cwd=root)
	with zipfile.ZipFile(output["distribution"]["main"]) as archive:
		assert "veroscript-package.json" in archive.namelist()
	generated = root / "generated project"
	shutil.copytree(args.repo / "VeroScriptBuild/Resources/Examples/generated", generated)
	generated_output = artifacts(run(args.cli, "build", "--path", generated, "--allow-script", "--json", cwd=root))
	assert "generated application" in run(args.cli, generated_output["application"]["main"], cwd=root)
	component = root / "runtime component"
	shutil.copytree(args.repo / "VeroScript/Resources/Scripts/stdlib", component / "stdlib")
	if sys.platform == "win32":
		from veroscript_modules import build_cache
		build_root = next(path for path in args.cli.parents if (path / 'CMakeCache.txt').is_file())
		run(build_cache(build_root)['CMAKE_COMMAND'], '--install', build_root, '--config', 'Release',
			'--component', 'VeroScriptWindowsRuntime', '--prefix', component, cwd=root)
	assert stage_application_runtime(args.repo, args.runtime.parent, component)
	if sys.platform == "win32":
		from veroscript_release import verify_windows_runtime
		descriptor = json.loads((component / 'veroscript-runtime.json').read_text(encoding='utf-8'))
		verify_windows_runtime({item['path']: ((component / item['path']).read_bytes(), 0)
								for item in descriptor['files']}, ('windows', descriptor['target']['arch']))
	native = root / "native project"
	native.mkdir()
	(native / "main.vs").write_text('int main() { print("native application"); return 23; }', encoding="utf-8")
	reference = {"target": "application", "output": "main"}
	suffix = ".exe" if sys.platform == "win32" else ""
	config = {"format": "veroscript-build", "formatVersion": "0.1.1", "defaultTargets": ["directory", "single", "zip"],
			  "targets": {
				  "application": {"kind": "application", "entry": "main.vs", "packageName": "native.integration",
								  "options": {"version": "0.1.1"}, "output": "native.vsapp"},
				  "directory": {"kind": "bundle", "application": reference, "layout": "directory", "output": "App",
								"runtime": {"mode": "bundled", "component": str(component)}, "launcher": {"mode": "generated", "output": "app" + suffix}},
				  "single": {"kind": "bundle", "application": reference, "layout": "single-file", "output": "app" + suffix,
							 "runtime": {"mode": "bundled", "component": str(component)}, "launcher": {"mode": "generated"}},
				  "zip": {"kind": "bundle", "application": reference, "layout": "zip", "output": "App.zip",
						  "runtime": {"mode": "bundled", "component": str(component)},
						  "launcher": {"mode": "generated", "output": "app" + suffix}}}}
	(native / "veroscript-build.json").write_text(json.dumps(config), encoding="utf-8")
	built = artifacts(run(args.cli, "build", "--path", native, "--json", cwd=root))
	if sys.platform == "win32":
		directory = built['directory']['main']
		verify_windows_runtime({file.relative_to(directory).as_posix(): (file.read_bytes(), 0)
								for file in directory.rglob('*') if file.is_file()},
							   ('windows', descriptor['target']['arch']))
		for item in descriptor['files']:
			assert (directory / 'runtime' / item['path']).read_bytes() == (component / item['path']).read_bytes()
		with zipfile.ZipFile(built['zip']['main']) as archive:
			verify_windows_runtime({name: (archive.read(name), 0) for name in archive.namelist()},
								   ('windows', descriptor['target']['arch']))
	assert "native application" in run(built["directory"]["main"] / ("app" + suffix), "--application-cache", root / "native-cache", cwd=root, expected=23)
	assert "native application" in run(built["single"]["main"], "--application-cache", root / "native-cache", cwd=root, expected=23)
	assert "native application" not in run(built["single"]["main"], "--check", "--application-cache", root / "native-cache", cwd=root)
	if sys.platform == "win32":
		previous = (directory / 'msvcp140.dll').read_bytes()
		conflicting = native / 'conflicting.dll'
		conflicting.write_bytes(b'different runtime dependency')
		config['targets']['directory']['files'] = [{'from': str(conflicting), 'to': 'msvcp140.dll'}]
		(native / 'veroscript-build.json').write_text(json.dumps(config), encoding='utf-8')
		failed = subprocess.run([str(args.cli), 'build', '--path', str(native), '--target', 'directory'],
								cwd=root, capture_output=True, text=True, encoding='utf-8', timeout=120)
		assert failed.returncode == 2 and 'launcher-runtime-conflict' in failed.stderr, failed.stderr
		assert (directory / 'msvcp140.dll').read_bytes() == previous
	dependencies = root / "dependency project"
	shutil.copytree(args.repo / "VeroScriptPackageManager/Resources/Examples/Local", dependencies)
	application = dependencies / "app"
	run(args.cli, "install", "--path", application, cwd=root)
	config = {"format": "veroscript-build", "formatVersion": "0.1.1", "defaultTargets": ["release", "source-release"], "targets": {
		"application": {"kind": "application", "entry": "main.vs", "packageName": "dependency.application", "options": {"version": "0.1.1"}, "output": "application.vsapp"},
		"source": {"kind": "application", "mode": "source", "entry": "main.vs", "packageName": "dependency.source", "options": {"version": "0.1.1"}, "output": "source.vsapp"},
		"deployment": {"kind": "deployment", "usage": ["runtime", "compile"], "output": "environment"},
		"release": {"kind": "bundle", "application": {"target": "application", "output": "main"}, "environment": {"mode": "bundled", "artifact": {"target": "deployment", "output": "main"}}, "output": "release.vsapp"},
		"source-release": {"kind": "bundle", "application": {"target": "source", "output": "main"}, "environment": {"mode": "bundled", "artifact": {"target": "deployment", "output": "main"}}, "output": "source-release.vsapp"}}}
	(application / "veroscript-build.json").write_text(json.dumps(config), encoding="utf-8")
	deployed = artifacts(run(args.cli, "build", "--path", application, "--json", cwd=root))
	for name in ("application", "source"):
		run(args.cli, "--stdlib-path", stdlib, "--stdlib-path", stdlib / ".." / "stdlib",
			"--path", application, "--check", deployed[name]["main"], cwd=root)
		assert "42" in run(args.cli, "--stdlib-path", stdlib, "--path", application,
						   deployed[name]["main"], cwd=root)
	for name in ("release", "source-release"):
		assert "42" in run(args.runtime, deployed[name]["main"], "--application-cache", root / "dependency-cache", cwd=root)
	if args.sodium:
		seed = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
		public = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
		encoded = lambda value: base64.b64encode(value).decode("ascii")
		# Published RFC test keys, never production credentials.
		keys = {"format": "veroscript-keyring", "formatVersion": "0.1.1", "keys": [
			{"id": "content", "version": "1", "secret": encoded(bytes(range(32))), "purposes": ["encrypt", "decrypt"]},
			{"id": "release", "version": "1", "secret": encoded(seed + public), "public": encoded(public), "purposes": ["sign", "verify"], "trusted": True}]}
		keyring = root / "test-keyring.json"
		keyring.write_text(json.dumps(keys), encoding="utf-8")
		protected = json.loads((project / "veroscript-build.json").read_text())
		signature = {"algorithm": "ed25519", "keyRef": "release", "keyVersion": "1", "scope": "content"}
		protected["targets"]["application"]["protection"] = {"signature": signature,
			"encryption": {"algorithm": "xchacha20poly1305-ietf", "keyRef": "content", "keyVersion": "1"}}
		(project / "veroscript-build.json").write_text(json.dumps(protected), encoding="utf-8")
		output = artifacts(run(args.cli, "build", "--path", project, "--target", "application", "--crypto-library", args.sodium,
							   "--keyring", keyring, "--json", cwd=root))
		options = root / "host-options.json"
		options.write_text(json.dumps({"cryptoLibrary": str(args.sodium), "keyring": str(keyring), "requireSignature": True}), encoding="utf-8")
		assert "42" in run(args.cli, "--application-options", options, output["application"]["main"], cwd=root)
		assert "42" in run(args.runtime, "--crypto-library", args.sodium, "--keyring", keyring, "--require-signature", output["application"]["main"], cwd=root)
		run(args.cli, "build", "--path", project, "--target", "zip", "--crypto-library", args.sodium, "--keyring", keyring, "--json", cwd=root, expected=2)
		protected["targets"]["zip"]["protection"] = {"signature": signature}
		(project / "veroscript-build.json").write_text(json.dumps(protected), encoding="utf-8")
		output = artifacts(run(args.cli, "build", "--path", project, "--target", "zip", "--crypto-library", args.sodium, "--keyring", keyring, "--json", cwd=root))
		assert "42" in run(args.runtime, "--crypto-library", args.sodium, "--keyring", keyring, "--require-signature", output["zip"]["main"], cwd=root)
		resource = root / "signed resource.txt"
		resource.write_text("resource payload", encoding="utf-8")
		resource_signature = dict(signature, scope="artifact")
		mapping = {"from": str(resource), "to": "resources/value.txt", "protection": {"signature": resource_signature}}
		protected["targets"]["application"]["files"] = [mapping]

		def build_resource(expected=0):
			(project / "veroscript-build.json").write_text(json.dumps(protected), encoding="utf-8")
			result = run(args.cli, "build", "--path", project, "--target", "application", "--crypto-library", args.sodium,
						 "--keyring", keyring, "--json", cwd=root, expected=expected)
			return artifacts(result)["application"]["main"] if expected == 0 else None

		with zipfile.ZipFile(build_resource()) as archive:
			original_signature = archive.read("resources/value.txt.sig.json")
		resource.with_suffix(resource.suffix + ".sig.json").write_bytes(original_signature)
		del mapping["protection"]
		with zipfile.ZipFile(build_resource()) as archive:
			assert archive.read("resources/value.txt") == b"resource payload"
			assert archive.read("resources/value.txt.sig.json") == original_signature
		mapping["protection"] = {"signature": resource_signature}
		with zipfile.ZipFile(build_resource()) as archive:
			assert archive.read("resources/value.txt.sig.json") == original_signature
		mapping["protection"]["encryption"] = {"algorithm": "xchacha20poly1305-ietf", "keyRef": "content", "keyVersion": "1"}
		with zipfile.ZipFile(build_resource()) as archive:
			stored = archive.read("resources/value.txt")
			updated_signature = archive.read("resources/value.txt.sig.json")
		assert stored.startswith(b"VSAE") and updated_signature != original_signature
		subject = json.loads(updated_signature)[0]
		assert subject["kind"] == "resource" and subject["sha256"] == hashlib.sha256(stored).hexdigest()
		assert subject["bytes"] == str(len(stored))
		resource.write_bytes(stored)
		resource.with_suffix(resource.suffix + ".sig.json").write_bytes(updated_signature)
		repacked = build_resource()
		with zipfile.ZipFile(repacked) as archive:
			stored = archive.read("resources/value.txt")
			updated_signature = archive.read("resources/value.txt.sig.json")
		header_size = int.from_bytes(stored[4:8], "little")
		assert json.loads(stored[8:8 + header_size])["plaintextBytes"] == str(len(b"resource payload"))
		subject = json.loads(updated_signature)[0]
		assert subject["sha256"] == hashlib.sha256(stored).hexdigest()
		release_digest = hashlib.sha256(repacked.read_bytes()).hexdigest()
		del mapping["protection"]["signature"]
		build_resource(expected=2)
		assert hashlib.sha256(repacked.read_bytes()).hexdigest() == release_digest
		mapping["protection"]["signature"] = dict(resource_signature, scope="content")
		build_resource(expected=2)
		mapping["protection"]["signature"] = resource_signature
		resource.write_bytes(b"tampered resource")
		build_resource(expected=2)
		assert hashlib.sha256(repacked.read_bytes()).hexdigest() == release_digest
	owned = root.resolve()
	if owned.parent != args.work or not owned.name.startswith("应用 build-"):
		raise AssertionError("test cleanup escaped its exclusively created directory")
	shutil.rmtree(owned)
	print("Portable, source, generated, deployed, runtime bootstrap, single-file and optional protection integration passed.")


if __name__ == "__main__":
	main()
