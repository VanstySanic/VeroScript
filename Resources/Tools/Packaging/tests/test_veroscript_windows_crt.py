# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from veroscript_windows_crt import target_crt_files
from veroscript_release import verify_windows_runtime


def pe(machine):
	data=bytearray(128)
	data[:2]=b'MZ'
	struct.pack_into('<I',data,60,64)
	data[64:68]=b'PE\0\0'
	struct.pack_into('<H',data,68,machine)
	return bytes(data)


class WindowsCrtTests(unittest.TestCase):
	def test_native_arm64_excludes_only_the_x64_emulation_satellite(self):
		with tempfile.TemporaryDirectory() as temporary:
			root=Path(temporary)
			for name in ('msvcp140.dll','vcruntime140.dll'):
				(root/name).write_bytes(pe(0xaa64))
			(root/'vcruntime140_1.dll').write_bytes(pe(0x8664))
			self.assertEqual({p.name for p in target_crt_files(root,'win-arm64')},{'msvcp140.dll','vcruntime140.dll'})
			(root/'msvcp140.dll').write_bytes(pe(0x8664))
			with self.assertRaises(ValueError):
				target_crt_files(root,'win-arm64')

	def test_native_arm64_inventory_does_not_require_x64_exception_dll(self):
		entries={name:(pe(0xaa64),0o644) for name in ('veroscript.exe','msvcp140.dll','vcruntime140.dll')}
		entries['licenses/MicrosoftVisualCpp/NOTICE.txt']=(b'Official CRT notice',0o644)
		verify_windows_runtime(entries,('windows','arm64'))
		entries['vcruntime140_1.dll']=(pe(0x8664),0o644)
		with self.assertRaises(ValueError):
			verify_windows_runtime(entries,('windows','arm64'))


if __name__=='__main__':
	unittest.main()
