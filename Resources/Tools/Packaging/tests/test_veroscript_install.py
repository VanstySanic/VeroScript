# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
from veroscript_business_release import ALL_BUSINESSES
spec = importlib.util.spec_from_file_location("veroscript_local_install", SCRIPTS / "install-veroscript-runtime.py")
install = importlib.util.module_from_spec(spec)
spec.loader.exec_module(install)


class LocalInstallTests(unittest.TestCase):
	def setUp(self):
		self.temporary = tempfile.TemporaryDirectory()
		self.addCleanup(self.temporary.cleanup)
		self.repo = Path(self.temporary.name).resolve()
		self.source = self.repo / "build/native/bin"
		self.source.mkdir(parents=True)
		self.home = self.repo / "user-home"
		self.home.mkdir()

	def test_default_roots_match_product_layout_and_relative_xdg_is_ignored(self):
		data = self.repo / "data"
		self.assertEqual(install.default_install_root("linux", self.home, {"XDG_DATA_HOME": str(data)}), data / "VeroScript")
		self.assertEqual(install.default_install_root("linux", self.home, {"XDG_DATA_HOME": "relative"}), self.home / ".local/share/VeroScript")
		self.assertEqual(install.default_install_root("macos", self.home, {}), self.home / "Applications/VeroScript")
		self.assertEqual(install.default_install_root("windows", self.home, {"LOCALAPPDATA": str(data)}), data / "Programs/VeroScript")
		with self.assertRaisesRegex(ValueError, "absolute"):
			install.default_install_root("windows", self.home, {"LOCALAPPDATA": "relative"})

	def test_resources_use_module_sources_and_prefer_deployed_assets(self):
		expected = {"stdlib": self.repo / Path(__file__).resolve().parents[4].name / "VeroScript/Resources/Scripts/stdlib"}
		self.assertEqual(expected, install.runtime_resources(self.repo, self.source))
		(self.source / "stdlib").mkdir()
		expected["stdlib"] = self.source / "stdlib"
		self.assertEqual(expected, install.runtime_resources(self.repo, self.source))
		(self.source / "localization").mkdir()
		expected["localization"] = self.source / "localization"
		self.assertEqual(expected, install.runtime_resources(self.repo, self.source))

	def test_local_install_without_application_stages_required_vm(self):
		from test_veroscript_release import elf
		from test_veroscript_icons import populate_icon_sources
		populate_icon_sources(self.repo)

		for name in ('veroscript', 'veroscript-lsp', 'veroscript-dap'):
			tool = self.source / name
			tool.write_bytes(elf())
			tool.chmod(0o755)
		(self.source / 'Vm').mkdir()
		(self.source / 'Vm/libVeroScriptVmBytecode.so').write_bytes(elf() + b'vm')
		(self.source / 'stdlib').mkdir()
		(self.source / 'stdlib/core.vs').write_text('// fixture')
		(self.source / 'localization').mkdir()
		project = self.repo / 'VeroScriptInstaller/Tool/VeroScriptInstaller.Tool.csproj'
		project.parent.mkdir(parents=True)
		project.write_text('<Project/>')
		(self.repo / 'VeroScript/Public').mkdir(parents=True, exist_ok=True)
		(self.repo / 'VeroScript/Public/VeroScriptVersion.h').write_text('// module marker')
		staged = []

		def run(*arguments, env=None, capture=False):
			if capture:
				return 'VeroScript 0.1.1 [linux-x64; fixture]'
			if 'build-bundle' in arguments:
				source = Path(arguments[arguments.index('--source-runtime') + 1])
				staged.append((source / 'Vm/libVeroScriptVmBytecode.so').read_bytes())
				self.assertFalse((source / 'veroscript-runtime').exists())

		arguments = ['install-veroscript-runtime.py', '--repo-root', str(self.repo),
					 '--source-dir', str(self.source), '--install-dir', str(self.repo / '.tmp/install')]
		with patch.object(sys, 'argv', arguments), patch.object(install, 'host_target', return_value='linux-x64'), \
				patch.object(install.shutil, 'which', return_value='dotnet'), patch.object(install, 'run', side_effect=run), \
				patch.object(install, 'stage_package_client'):
			install.main()
		self.assertEqual(staged, [elf() + b'vm'])

	def test_roots_sources_and_complete_releases_are_preserved(self):
		release = self.repo / "dist/release"
		release.mkdir(parents=True)
		sentinel = release / "release-manifest.json"
		sentinel.write_text("preserve me")
		for root in (Path(self.repo.anchor), self.repo, self.home, self.source,
					 self.source.parent, self.source / "nested", self.repo / "Script/new", release / "nested"):
			with self.subTest(root=root), self.assertRaises(ValueError):
				install.validate_install_root(self.repo, self.source, root, self.home)
		self.assertEqual(sentinel.read_text(), "preserve me")

	def test_unmanaged_existing_installation_is_preserved(self):
		root = self.repo / ".tmp/legacy"
		root.mkdir(parents=True)
		sentinel = root / "user-file.vs"
		sentinel.write_text("user content")
		self.assertEqual(install.validate_install_root(self.repo, self.source, root, self.home), root)
		destination = root / "Runtime/0.1.1"
		destination.mkdir(parents=True)
		(destination / "foreign-file").write_text("preserve")
		with self.assertRaisesRegex(ValueError, "unmanaged"):
			install.validate_install_root(self.repo, self.source, root, self.home)
		self.assertEqual(sentinel.read_text(), "user content")
		empty = self.repo / ".tmp/empty"
		empty.mkdir()
		self.assertEqual(install.validate_install_root(self.repo, self.source, empty, self.home), empty)

	def test_linked_installation_is_rejected_without_modifying_target(self):
		target = self.repo / "other-data"
		target.mkdir()
		sentinel = target / "user-file"
		sentinel.write_text("user content")
		link = self.repo / ".tmp/linked"
		link.parent.mkdir()
		if os.name == "nt":
			subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True)
		else:
			link.symlink_to(target, target_is_directory=True)
		with self.assertRaisesRegex(ValueError, "Linked"):
			install.validate_install_root(self.repo, self.source, link / "install", self.home)
		self.assertEqual(sentinel.read_text(), "user content")

	@unittest.skipIf(os.name == "nt", "Unix installer entry requires Bash and a Unix host")
	def test_unix_installer_cli_identity_and_portable_temporary_paths(self):
		from test_veroscript_icons import populate_icon_sources
		from veroscript_icons import stage_runtime_icons
		# Packaging tools now belong to the standalone repository root. Keep
		# spaces in that root without inventing a second enclosing checkout.
		self.repo = self.repo / "Engine Sources"
		module = self.repo
		scripts = module / "Resources/Tools/Packaging"
		scripts.mkdir(parents=True)
		for name in ("package-veroscript-installer.sh", "veroscript_release.py", "veroscript_icons.py",
					 "veroscript_business_release.py", "veroscript_paths.py", "veroscript_products.py"):
			shutil.copy2(SCRIPTS / name, scripts / name)
		(module / 'VeroScriptInstaller').mkdir()
		populate_icon_sources(module)
		(scripts / 'ReleaseVersions.json').write_text(json.dumps({
			'schemaVersion': 1, 'nativeVersion': '1.0.0',
			'businesses': {name: '1.0.0' for name in ALL_BUSINESSES}}))
		target = install.host_target()
		rid = target.replace("macos-", "osx-")
		fakebin = self.repo / "fake-bin"
		fakebin.mkdir()
		dotnet = fakebin / "dotnet"
		dotnet.write_text("#!/bin/sh\necho EXPECTED_BUNDLE_BUILD_BOUNDARY >&2\nexit 91\n")
		dotnet.chmod(0o755)
		# Apple/BSD mktemp uses mkstemp/mkdtemp and requires trailing Xs.
		# Exercise that contract even when the test runs with GNU utilities.
		mktemp = fakebin / "mktemp"
		mktemp.write_text("#!/bin/sh\nfor template do :; done\n"
						  "case \"$template\" in *XXXXXX) ;; *) echo 'mktemp requires trailing Xs' >&2; exit 92 ;; esac\n"
						  "exec " + shlex.quote(shutil.which("mktemp")) + ' "$@"\n')
		mktemp.chmod(0o755)
		temporary = self.repo / "temporary files"
		temporary.mkdir()
		env = dict(os.environ, PATH=str(fakebin) + os.pathsep + os.environ["PATH"], TMPDIR=str(temporary))
		package = self.repo / "runtime-source"
		cli = package / "veroscript"
		cli.parent.mkdir(parents=True)
		stage_runtime_icons(module, package, target)
		for version, native, rejected in (
			('1.0.0', '1.0.0', False), ('2.0.0', '1.0.0', False),
			('1.0.0', '2.0.0', True), ('2.0.0', '2.0.0', True)):
			cli.write_text(f"#!/bin/sh\necho 'VeroScript {native} [{target}; fixture]'\n")
			cli.chmod(0o755)
			with self.subTest(version=version, native=native):
				output = self.repo / ".tmp" / ("runtime-" + version + '-native-' + native)
				command = ["bash", str(scripts / "package-veroscript-installer.sh"),
						   "--runtime", rid, "--version", version,
						   "--runtime-package-dir", str(package), "--out-root", str(output)]
				result = subprocess.run(command, env=env, text=True, capture_output=True)
				self.assertNotEqual(result.returncode, 0)
				if rejected:
					self.assertIn("CLI version/target mismatch", result.stderr)
					self.assertFalse(output.exists())
				else:
					self.assertIn("EXPECTED_BUNDLE_BUILD_BOUNDARY", result.stderr)
				self.assertEqual(list(temporary.iterdir()), [], "Installer leaked temporary files after failure")
		managed = module / 'Release' / 'Runtime' / target / '1.0.0'
		managed.mkdir(parents=True)
		marker = managed / 'release-manifest.json'
		marker.write_bytes(b'{}')
		output = managed / 'unexpected-installer'
		command = ['bash', str(scripts / 'package-veroscript-installer.sh'),
				   '--runtime', rid, '--out-root', str(output)]
		result = subprocess.run(command, env=env, text=True, capture_output=True)
		self.assertNotEqual(result.returncode, 0)
		self.assertIn('Refusing direct output into a complete release collection', result.stderr)
		self.assertNotIn('EXPECTED_BUNDLE_BUILD_BOUNDARY', result.stderr)
		self.assertFalse(output.exists())
		self.assertEqual([marker], list(managed.iterdir()))
		self.assertEqual(b'{}', marker.read_bytes())

	@unittest.skipIf(os.name == "nt", "Unix installer publication requires Bash and a Unix host")
	def test_unix_installer_requires_complete_successful_license_outputs(self):
		from test_veroscript_icons import populate_icon_sources
		from veroscript_icons import stage_runtime_icons, runtime_icon_files
		scripts = self.repo / 'Resources/Tools/Packaging'
		scripts.mkdir(parents=True)
		for name in ('package-veroscript-installer.sh', 'veroscript_release.py', 'veroscript_icons.py',
					 'veroscript_business_release.py', 'veroscript_paths.py', 'veroscript_products.py'):
			shutil.copy2(SCRIPTS / name, scripts / name)
		(scripts / 'veroscript_installer.py').write_text('''from veroscript_products import installer_filename
def package_unix_installer(repo, program, output, business, version, target, expected_payload):
    assert program.is_file() and len(expected_payload) == 64
    output.mkdir(parents=True)
    artifact = output / installer_filename(business, version, target)
    artifact.write_bytes(b'installer fixture')
    return artifact
''')
		populate_icon_sources(self.repo)
		(scripts / 'ReleaseVersions.json').write_text(json.dumps({
			'schemaVersion': 1, 'nativeVersion': '1.0.0',
			'businesses': {name: '1.0.0' for name in ALL_BUSINESSES}}))
		license_path = self.repo / 'LICENSE'
		shutil.copy2(SCRIPTS.parents[2] / 'LICENSE', license_path)
		notices = self.repo / 'VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt'
		notices.parent.mkdir(parents=True)
		shutil.copy2(SCRIPTS.parents[2] / notices.relative_to(self.repo), notices)
		fakebin = self.repo / 'fake-bin'
		fakebin.mkdir()
		fakeapp = self.repo / 'fixture-installer'
		fakeapp.write_text('''#!/bin/sh
if [ "$1" = --license ]; then
    if [ "$FIXTURE_MODE" = wrong ]; then echo wrong; else cat "$FIXTURE_LICENSE"; fi
    if [ "$FIXTURE_MODE" = fail ]; then exit 17; fi
elif [ "$1" = --third-party-licenses ]; then
    if [ "$FIXTURE_MODE" = third-party-wrong ]; then echo wrong; else cat "$FIXTURE_NOTICES"; fi
    if [ "$FIXTURE_MODE" = third-party-fail ]; then exit 18; fi
fi
exit 0
''')
		fakeapp.chmod(0o755)
		dotnet = fakebin / 'dotnet'
		dotnet.write_text('''#!/bin/sh
if [ "$1" = publish ]; then
    for output do :; done
    mkdir -p "$output"
    cp "$FIXTURE_APP" "$output/VeroScriptInstaller"
else
    previous=''
    for argument do
        if [ "$previous" = --output ]; then
				case " $* " in
				    *" pack-bundle "*) printf 'verified fixture' > "$argument" ;;
				    *) mkdir -p "$argument"; cp -R "$FIXTURE_BUNDLE/." "$argument/" ;;
				esac
            break
        fi
        previous="$argument"
    done
fi
exit 0
''')
		dotnet.chmod(0o755)
		target = install.host_target()
		package = self.repo / 'runtime-source'
		package.mkdir()
		stage_runtime_icons(self.repo, package, target)
		bundle = self.repo / 'fixture-bundle'
		stage_runtime_icons(self.repo, bundle / 'payload/runtime', target)
		(bundle / 'manifest.json').write_text(json.dumps({
			'platform': target.split('-')[0], 'architecture': target.split('-')[1], 'components': [
				{'id': 'runtime', 'payloadDirectory': 'payload/runtime', 'files': [
					{'path': name, 'size': source.stat().st_size, 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
					for name, source in runtime_icon_files(self.repo, target).items()]}]}))
		cli = package / 'veroscript'
		cli.write_text(f"#!/bin/sh\necho 'VeroScript 1.0.0 [{target}; fixture]'\n")
		cli.chmod(0o755)
		temporary = self.repo / 'temporary files'
		temporary.mkdir()
		for mode in ('ok', 'wrong', 'fail', 'third-party-wrong', 'third-party-fail'):
			with self.subTest(mode=mode):
				output = self.repo / '.tmp' / mode
				env = dict(os.environ, PATH=str(fakebin) + os.pathsep + os.environ['PATH'],
						   TMPDIR=str(temporary), FIXTURE_MODE=mode, FIXTURE_APP=str(fakeapp),
						   FIXTURE_BUNDLE=str(bundle),
						   FIXTURE_LICENSE=str(license_path), FIXTURE_NOTICES=str(notices))
				result = subprocess.run([
					'bash', str(scripts / 'package-veroscript-installer.sh'),
					'--runtime', target.replace('macos-', 'osx-'), '--version', '1.0.0',
					'--runtime-package-dir', str(package), '--out-root', str(output)],
					env=env, text=True, capture_output=True)
				artifacts = list(output.glob('VeroScriptRuntimeInstaller-*'))
				if mode == 'ok':
					self.assertEqual(result.returncode, 0, result.stderr)
					self.assertEqual([path.name for path in artifacts], [
						f'VeroScriptRuntimeInstaller-1.0.0-{target}' + ('.dmg' if target.startswith('macos-') else '.tar.gz')])
				else:
					self.assertNotEqual(result.returncode, 0)
					self.assertEqual(artifacts, [])
				self.assertEqual(list(temporary.iterdir()), [], 'Installer leaked temporary files')


if __name__ == "__main__":
	unittest.main()
