# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from veroscript_target_contract import BUFFER, MAGIC, read_contract


class CompiledContractTests(unittest.TestCase):
	def record(self):
		result = MAGIC.ljust(32, b'\0')
		for value in (b'jit-abi', b'vm-abi', b'native-abi'):
			result += value.ljust(BUFFER, b'\0') + struct.pack('<I', len(value))
		for value, size in ((b'a'*64,65),(b'b'*64,65),(b'linux-arm64',64),(b'c'*64,65)):
			result += value.ljust(size, b'\0')
		return result

	def read(self, data, target=None):
		with tempfile.TemporaryDirectory() as directory:
			path = pathlib.Path(directory)/'contract.o';path.write_bytes(data)
			return read_contract(path, target)

	def object(self, payload, debug=b'', machine=183):
		header = bytearray(64);header[:7] = b'\x7fELF\x02\x01\x01'
		struct.pack_into('<H',header,18,machine)
		struct.pack_into('<Q',header,40,64)
		struct.pack_into('<HH',header,58,64,3)
		sections = bytearray(3*64)
		struct.pack_into('<IQQQQ',sections,64+4,1,2,0,256,len(payload))
		struct.pack_into('<IQQQQ',sections,128+4,1,0,0,256+len(payload),len(debug))
		return bytes(header)+bytes(sections)+payload+debug

	def test_debug_initializer_copy_does_not_duplicate_record(self):
		data = self.object(self.record(), MAGIC+b'\0debug initializer only')
		self.assertEqual(self.read(data,'linux-arm64')['abis']['vm'],'vm-abi')

	def test_two_complete_records_are_rejected(self):
		with self.assertRaisesRegex(ValueError,'ambiguous'):
			self.read(self.object(self.record()+self.record()))

	def test_object_cpu_and_requested_target_are_checked(self):
		with self.assertRaisesRegex(ValueError,'architecture'):
			self.read(self.object(self.record(),machine=62))
		with self.assertRaisesRegex(ValueError,'target differs'):
			self.read(self.object(self.record()),'linux-x64')

	def test_truncated_record_is_rejected(self):
		with self.assertRaisesRegex(ValueError,'Missing'):
			self.read(self.object(self.record()[:-1]))

	def test_complete_debug_constant_is_ignored(self):
		self.assertEqual(self.read(self.object(self.record(),self.record()))['target'],'linux-arm64')
		with self.assertRaisesRegex(ValueError,'Missing'):
			self.read(self.object(b'',self.record()))


if __name__ == '__main__':
	unittest.main()
