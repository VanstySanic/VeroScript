# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Regression checks for module-owned source assets and portable packages."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_release import copy_tree, portable_asset_bytes, sdk_asset_tree, release_asset_files


class ScriptAssetTests(unittest.TestCase):
	@unittest.skipUnless(sys.platform == "win32", "PowerShell publisher contract runs on Windows")
	def test_jit_build_identity_rejects_stale_cli_for_each_sdk_configuration(self):
		helper = str(Path(__file__).resolve().parents[1] / "veroscript-assets.ps1").replace("'", "''")
		script = f"$ErrorActionPreference = 'Stop'; . '{helper}'; " + r"""
            foreach ($built in @(0, 1)) {
                foreach ($native in @(0, 1)) {
                    $identity = Get-VeroScriptJitIdentity 'VeroScript 0.1.1 [win-x64; jit-backend=none; jit-target=none; native=off]'
                    Assert-VeroScriptJitBuildIdentity $identity $built $native
                }
            }
            foreach ($backend in @('asmjit', 'custom-backend')) {
                $identity = Get-VeroScriptJitIdentity "VeroScript 0.1.1 [win-x64; jit-backend=$backend; jit-target=portable-cpp; native=on]"
                $rejected = $false
                try { Assert-VeroScriptJitBuildIdentity $identity 1 1 } catch { $rejected = $true }
                if (-not $rejected) { throw 'Implicit runtime backend identity was accepted' }
            }
        """
		subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', script], check=True)

	def test_sdk_links_resolve_in_nested_examples_and_docs(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			documents = {"examples/scripts/README.md": b'[docs](../../Docs/en-US/index.md)\r\nveroscript --check examples/hello.vs\r\n',
						 "examples/scripts/package/README.md": b'[docs](../../../Docs/en-US/index.md)\r\n',
						 "Docs/en-US/index.md": b'[example](../../../VeroScript/Resources/Scripts/examples/hello.vs)\r\n',
						 "examples/scripts/hello.vs": b'void main() {}'}
			expected = {"examples/scripts/README.md": b'[docs](../../Docs/en-US/index.md)\r\nveroscript --check examples/scripts/hello.vs\r\n',
						"examples/scripts/package/README.md": b'[docs](../../../Docs/en-US/index.md)\r\n',
						"Docs/en-US/index.md": b'[example](../../examples/scripts/hello.vs)\r\n'}
			literal = (b'\n```md\n[code](../../Docs/en-US/index.md)\n```\n'
					   b'`[inline](../docs/example.md)`\n'
					   b'<!-- [comment](../examples/hello.vs) -->\n')
			for name in expected:
				documents[name] += literal
				expected[name] += literal
			for name, data in documents.items():
				path = root / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(data)
			for _ in range(2):
				sdk_asset_tree(root / "examples/scripts", examples=True)
				sdk_asset_tree(root / "Docs")
				for name, data in expected.items():
					self.assertEqual(data, (root / name).read_bytes())
			if sys.platform == "win32":
				for name, data in documents.items():
					(root / name).write_bytes(data)
				helper_arg = str(Path(__file__).resolve().parents[1] / 'veroscript-assets.ps1').replace("'", "''")
				root_arg = str(root).replace("'", "''")
				for _ in range(2):
					subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
									f"$ErrorActionPreference = 'Stop'; . '{helper_arg}'; "
									f"ConvertTo-VeroScriptSdkAssetTree '{root_arg}/examples/scripts' -Examples; "
									f"ConvertTo-VeroScriptSdkAssetTree '{root_arg}/Docs'"], check=True)
				for name, data in expected.items():
					self.assertEqual(data, (root / name).read_bytes())

	def test_packaged_links_and_bytes(self):
		with tempfile.TemporaryDirectory() as temporary:
			source = Path(temporary) / "source"
			source.mkdir()
			document = source / "README.md"
			document.write_bytes(b'\xef\xbb\xbf[example](../../../../VeroScript/Resources/Scripts/examples/hello.vs)\r\n'
								 b'[docs](../docs/en-US/index.md)\r\n'
								 b'[web](https://example.test/VeroScript/Resources/Scripts/examples/)\r\n')
			expected = (b'\xef\xbb\xbf[example](../../../examples/hello.vs)\r\n'
						b'[docs](../docs/en-US/index.md)\r\n'
						b'[web](https://example.test/VeroScript/Resources/Scripts/examples/)\r\n')
			destination = Path(temporary) / "package"
			copy_tree(source, destination)
			self.assertEqual(expected, (destination / "README.md").read_bytes())
			self.assertEqual(expected, portable_asset_bytes(destination / "README.md"))
			self.assertIn(b'VeroScript/Resources/Scripts', document.read_bytes())
			if sys.platform == "win32":
				helper = Path(__file__).resolve().parents[1] / "veroscript-assets.ps1"
				helper_arg = str(helper).replace("'", "''")
				source_arg = str(source).replace("'", "''")
				subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
								f"$ErrorActionPreference = 'Stop'; . '{helper_arg}'; ConvertTo-VeroScriptPortableAssetTree '{source_arg}'"], check=True)
				self.assertEqual(expected, document.read_bytes())

	def test_aggregate_inventory_excludes_generated_files(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			files = {"Script/Resources/Docs/User/en-US/index.md": b"docs",
					 "Script/Resources/Assets/icons/icon.svg": b"icon",
					 "Script/Resources/Assets/__pycache__/ignored.pyc": b"cache",
					 "Script/VeroScript/Resources/Scripts/stdlib/core.vs": b"stdlib",
					 "Script/VeroScript/Resources/Scripts/examples/hello.vs": b"example",
					 "Script/VeroScript/Resources/Scripts/examples/hello.vsc": b"cache",
					 "Script/Release/old/README.md": b"old"}
			for name, content in files.items():
				path = repo / name
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_bytes(content)
			self.assertEqual({"Resources/Assets/icons/icon.svg",
							  "Resources/Scripts/stdlib/core.vs", "Resources/Scripts/examples/hello.vs"},
							 {relative for relative, _ in release_asset_files(repo)})

	def test_authoritative_tutorials_and_generated_docs_are_not_loose_assets(self):
		with tempfile.TemporaryDirectory() as temporary:
			repo = Path(temporary)
			for route in ('Script/Resources/Docs/Tutorials/en-US/index.md',
						  'Script/Resources/Docs/Tutorials/en-US/script.vsbc',
						  '.tmp/docs-collection-example/Docs/User/en-US/index.md',
						  'Script/Resources/Docs/Developer/en-US/index.md'):
				path = repo / route
				path.parent.mkdir(parents=True, exist_ok=True)
				path.write_text('# Documentation source')
			self.assertEqual({}, dict(release_asset_files(repo)))
			self.assertFalse((repo / 'Script/VeroScriptUtils').exists())


if __name__ == "__main__":
	unittest.main()
