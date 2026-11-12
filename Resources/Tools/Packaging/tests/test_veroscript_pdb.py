# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from veroscript_pdb import coff_debug, debug_references, export_windows_symbols, pdb_identity, pe_codeview

GUID = uuid.UUID('12345678-1234-5678-9012-123456789abc').bytes_le


def pdb(guid=GUID, age=1):
    data = bytearray(512 * 6)
    data[:32] = b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'
    struct.pack_into('<6I', data, 32, 512, 1, 6, 16, 0, 3)
    struct.pack_into('<I', data, 3 * 512, 4)
    struct.pack_into('<4I', data, 4 * 512, 2, 0, 28, 5)
    struct.pack_into('<III16s', data, 5 * 512, 20000404, 0, age, guid)
    return bytes(data)


def pe(reference, guid=GUID, age=1):
    name = reference.encode() + b'\0'
    codeview = b'RSDS' + guid + struct.pack('<I', age) + name
    data = bytearray(1024 + len(codeview))
    data[:2] = b'MZ';struct.pack_into('<I', data, 0x3c, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<HH', data, 132, 0x8664, 1)
    struct.pack_into('<H', data, 148, 240)
    struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<I', data, 152 + 108, 16)
    struct.pack_into('<II', data, 152 + 112 + 6 * 8, 0x1000, 28)
    section = 152 + 240
    data[section:section + 8] = b'.rdata\0\0'
    struct.pack_into('<4I', data, section + 8, len(data) - 512, 0x1000, len(data) - 512, 512)
    struct.pack_into('<4I', data, 512 + 12, 2, len(codeview), 0x1200, 1024)
    data[1024:] = codeview
    return bytes(data)


def coff(reference=None, *, big=False, embedded=False, guid=GUID, age=1):
    types = b''
    if reference is not None:
        record = struct.pack('<H', 0x1515) + guid + struct.pack('<I', age) + reference.encode() + b'\0'
        types = struct.pack('<I', 4) + struct.pack('<H', len(record)) + record
    elif embedded:
        types = struct.pack('<IHHI', 4, 6, 0x1001, 0)
    header = 56 if big else 20
    data = bytearray(header + 40 + len(types))
    if big:
        struct.pack_into('<HHHH', data, 0, 0, 0xffff, 2, 0xaa64)
        data[12:28] = bytes.fromhex('c7a1bad1eebaa94baf20faf66aa4dcb8')
        struct.pack_into('<I', data, 44, 1)
    else:
        struct.pack_into('<HH', data, 0, 0x8664, 1)
    data[header:header + 8] = b'.debug$T' if types else b'.text\0\0\0'
    struct.pack_into('<II', data, header + 16, len(types), header + 40)
    data[header + 40:] = types
    return bytes(data)


def archive(*members):
    result = b'!<arch>\n'
    for index, member in enumerate(members):
        header = f'{index}.obj/'.ljust(16) + '0'.ljust(12) + '0'.ljust(6) + '0'.ljust(6) + '100644'.ljust(8) + str(len(member)).ljust(10) + '`\n'
        result += header.encode() + member + (b'\n' if len(member) % 2 else b'')
    return result


def ltcg(reference, age=1):
    header = bytearray(40)
    struct.pack_into('<HHHH', header, 0, 0, 0xffff, 1, 0xaa64)
    header[12:28] = bytes.fromhex('38feb30ca5d9ab4dac9bd6b6222653c2')
    record = struct.pack('<H', 0x1515) + GUID + struct.pack('<I', age) + reference.encode() + b'\0'
    padding = (-(len(record) + 2)) % 4
    record += bytes(range(0xf0 + padding, 0xf0, -1))
    result = header + b'IR:opaque\0' + struct.pack('<H', len(record)) + record
    struct.pack_into('<I', result, 28, len(result) - 32)
    return bytes(result)


class PdbTests(unittest.TestCase):
    def test_reads_actual_msf_stream_layout_and_rejects_invalid_block_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'library.pdb';path.write_bytes(pdb())
            self.assertEqual(pdb_identity(path), {'guid': str(uuid.UUID(bytes_le=GUID)), 'age': 1})
            for offset, value in ((32, 123), (36, 7), (44, 1), (3 * 512, 100), (4 * 512 + 12, 99)):
                malformed = bytearray(pdb());struct.pack_into('<I', malformed, offset, value);path.write_bytes(malformed)
                with self.subTest(offset=offset), self.assertRaises(ValueError):
                    pdb_identity(path)

    def test_pe_guid_and_age_pairing_rejects_missing_wrong_and_outside_pdb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);build = root / 'build';build.mkdir()
            symbols = build / 'library.pdb';source = build / 'library.dll';functional = root / 'functional.dll'
            source.write_bytes(pe(str(symbols)));functional.write_bytes(source.read_bytes())
            row = {'buildDirectory': str(build), 'pdbCandidates': [str(symbols)]}
            for content in (None, pdb(age=2), pdb(guid=b'x' * 16)):
                if content is not None:symbols.write_bytes(content)
                with self.subTest(content=content), self.assertRaisesRegex(ValueError, 'PDB GUID/age'):
                    export_windows_symbols(source, functional, root / 'output', row)
            symbols.write_bytes(pdb())
            files, identity = export_windows_symbols(source, functional, root / 'output', row)
            self.assertEqual(len(files), 1);self.assertEqual(files[0].read_bytes(), pdb())
            self.assertEqual(identity['format'], 'pe')
            self.assertEqual(identity['pdbBindings'], [{'guid': str(uuid.UUID(bytes_le=GUID)), 'age': 1}])
            outside = root / 'library.pdb';outside.write_bytes(pdb())
            source.write_bytes(pe(str(outside)));functional.write_bytes(source.read_bytes())
            with self.assertRaisesRegex(ValueError, 'PDB GUID/age'):
                export_windows_symbols(source, functional, root / 'outside', {'buildDirectory': str(build)})
            functional.write_bytes(b'unrelated functional binary')
            with self.assertRaisesRegex(ValueError, 'differs from'):
                export_windows_symbols(source, functional, root / 'mismatch', row)

    def test_static_type_server_retains_archive_and_pairs_full_compiler_pdb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);database = root / 'vc140.pdb';database.write_bytes(pdb())
            source = root / 'library.lib';source.write_bytes(archive(coff(str(database)), coff(str(database), big=True)))
            found, references = coff_debug(source.read_bytes())
            self.assertTrue(found);self.assertEqual(len(references), 1)
            files, identity = export_windows_symbols(source, source, root / 'symbols', {'buildDirectory': str(root)})
            self.assertEqual({p.name for p in files}, {'library.lib', 'vc140.pdb'})
            self.assertEqual(files[0].read_bytes(), source.read_bytes())
            self.assertEqual(identity['format'], 'coff')
            database.unlink()
            with self.assertRaisesRegex(ValueError, 'PDB GUID/age'):
                export_windows_symbols(source, source, root / 'missing', {'buildDirectory': str(root)})

    def test_embedded_debug_needs_no_pdb_and_no_debug_emits_nothing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);source = root / 'library.lib'
            source.write_bytes(archive(coff(embedded=True)))
            files, identity = export_windows_symbols(source, source, root / 'embedded', {'buildDirectory': str(root)})
            self.assertEqual(len(files), 1);self.assertEqual(identity['pdbBindings'], [])
            source.write_bytes(archive(coff()))
            self.assertEqual(export_windows_symbols(source, source, root / 'none', {}), ([], {}))
            self.assertFalse((root / 'none').exists())

    def test_msvc_ltcg_retains_whole_ir_and_compiler_age_can_advance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);database = root / 'compiler.pdb';database.write_bytes(pdb(age=8))
            source = root / 'library.lib';source.write_bytes(archive(ltcg(str(database), age=1), ltcg(str(database), age=7)))
            files, identity = export_windows_symbols(source, source, root / 'symbols', {'buildDirectory': str(root)})
            self.assertEqual(files[0].read_bytes(), source.read_bytes())
            self.assertEqual(len(files), 2)
            self.assertEqual(identity['pdbBindings'], [{'guid': str(uuid.UUID(bytes_le=GUID)), 'age': 8}])
            self.assertEqual({r['age'] for r in debug_references(source)}, {1, 7})
            self.assertEqual(identity['typeServerMatch'], 'guid')
            database.write_bytes(pdb(guid=b'x' * 16, age=8))
            with self.assertRaisesRegex(ValueError, 'PDB GUID/age'):
                export_windows_symbols(source, source, root / 'wrong', {'buildDirectory': str(root)})
            malformed = bytearray(ltcg(str(database)))
            struct.pack_into('<I', malformed, 28, len(malformed))
            with self.assertRaisesRegex(ValueError, 'anonymous object size'):
                coff_debug(archive(malformed))

    def test_valid_ltcg_without_debug_emits_no_guessed_pdb(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);source = root / 'release.lib'
            member = bytearray(ltcg('unused.pdb'))
            position = member.find(b'\x15\x15');member[position:position + 2] = b'\0\0'
            source.write_bytes(archive(member))
            (root / 'release.pdb').write_bytes(pdb())
            self.assertEqual(export_windows_symbols(source, source, root / 'symbols',
                {'buildDirectory': str(root), 'pdbCandidates': [str(root / 'release.pdb')]}), ([], {}))
            self.assertFalse((root / 'symbols').exists())
            for corrupt in (member[:30], bytearray(member)):
                if len(corrupt) >= 28:corrupt[12] ^= 1
                with self.assertRaises(ValueError):coff_debug(archive(corrupt))

    def test_compile_flags_are_exact_candidates_not_a_pdb_glob(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);database = root / 'types' / 'vc140.pdb';database.parent.mkdir();database.write_bytes(pdb())
            source = root / 'library.lib';source.write_bytes(archive(coff('vc140.pdb')))
            with self.assertRaisesRegex(ValueError, 'PDB GUID/age'):
                export_windows_symbols(source, source, root / 'missing', {'buildDirectory': str(root)})
            row = {'buildDirectory': str(root), 'compileGroups': [{'compileCommandFragments': [{'fragment': '/Zi /Fd"types/vc140.pdb" /O2'}]}]}
            files, identity = export_windows_symbols(source, source, root / 'found', row)
            self.assertEqual(len(files), 2)

    def test_truncated_and_legacy_debug_inputs_fail_closed(self):
        for binary in (b'MZ', pe('library.pdb')[:1020]):
            with self.subTest(binary=len(binary)), self.assertRaises(ValueError):pe_codeview(binary)
        malformed = bytearray(coff(embedded=True));struct.pack_into('<H', malformed, 20 + 40 + 6, 0x1501)
        with self.assertRaisesRegex(ValueError, 'legacy'):
            coff_debug(archive(malformed))


if __name__ == '__main__':
    unittest.main()
