# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

from codecs import BOM_UTF8
from contextlib import chdir
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

LINK = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'Consumer/Foundation/LinkMsvc.py'))['link']


class MsvcLinkTests(unittest.TestCase):
	def test_only_unmarked_utf8_needs_conversion(self):
		text = '"独立 SDK\\widget.lib"\n'
		utf8 = text.encode('utf-8')
		originals = {
			'utf8 args.rsp': utf8,
			'ascii.rsp': b'widget.lib\n',
			'bom.rsp': BOM_UTF8 + utf8,
			'utf16.rsp': text.encode('utf-16'),
			'ansi.rsp': text.encode('cp936'),
		}
		with tempfile.TemporaryDirectory() as work, chdir(work):
			for name, data in originals.items():
				Path(name).write_bytes(data)

			def invoke(command):
				self.assertEqual(command[:2], ['link.exe', '/NOLOGO'])
				response = Path(command[2][1:])
				self.assertFalse(response.is_absolute())
				self.assertEqual(response.read_bytes(), BOM_UTF8 + utf8)
				self.assertEqual(command[3:], ['@' + name for name in list(originals)[1:]])
				return 1181

			with patch('subprocess.call', side_effect=invoke):
				self.assertEqual(LINK(['link.exe', '/NOLOGO', *('@' + name for name in originals)]), 1181)
			self.assertEqual({p.name: p.read_bytes() for p in Path.cwd().iterdir()}, originals)
