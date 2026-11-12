# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT
"""Match owned Windows CodeView debug records to their actual native PDBs.

MSF7 / Info stream layouts follow LLVM's PDB/MsfFile and PDB/PdbStream
documentation. Never select a PDB by a directory glob or its name alone.
"""
from __future__ import annotations

import hashlib
import mmap
from pathlib import Path, PureWindowsPath
import re
import shutil
import struct
import uuid


def _checked(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError('Truncated Windows native debug data')
    return data[offset:offset + size]


def _u32(data, offset):
    return struct.unpack('<I', _checked(data, offset, 4))[0]


def _identity(guid, age):
    return {'guid': str(uuid.UUID(bytes_le=bytes(guid))), 'age': age}


def pdb_identity(path: Path) -> dict:
    """Read MSF7 stream 1 without loading a potentially large PDB into memory."""
    with Path(path).open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if _checked(data, 0, 32) != b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0':
            raise ValueError('Expected a native MSF7 PDB')
        block, free_map, count, directory_size, reserved, block_map = struct.unpack('<6I', _checked(data, 32, 24))
        if block not in {512, 1024, 2048, 4096, 8192, 16384, 32768, 65536} or not count or count * block != len(data):
            raise ValueError('Invalid MSF7 block layout')
        if free_map not in {1, 2} or reserved or directory_size < 12:
            raise ValueError('Invalid MSF7 superblock')
        def read_blocks(numbers, size):
            if any(number >= count for number in numbers):
                raise ValueError('MSF7 block reference exceeds the file')
            if len(set(numbers)) != len(numbers):
                raise ValueError('Duplicate MSF7 blocks')
            return b''.join(_checked(data, number * block, block) for number in numbers)[:size]
        number_directory_blocks = (directory_size + block - 1) // block
        if number_directory_blocks > block // 4 or block_map >= count:
            raise ValueError('Unsupported or malformed MSF7 directory block map')
        numbers = struct.unpack('<' + 'I' * number_directory_blocks,
                                _checked(data, block_map * block, number_directory_blocks * 4))
        directory = read_blocks(numbers, directory_size)
        streams = _u32(directory, 0)
        if streams < 2 or streams > (len(directory) - 4) // 4:
            raise ValueError('Invalid MSF7 stream directory')
        sizes = struct.unpack('<' + 'I' * streams, _checked(directory, 4, streams * 4))
        cursor = 4 + streams * 4
        info = None
        for index, size in enumerate(sizes):
            blocks = 0 if size == 0xffffffff else (size + block - 1) // block
            numbers = struct.unpack('<' + 'I' * blocks, _checked(directory, cursor, blocks * 4))
            cursor += blocks * 4
            if any(number >= count for number in numbers):
                raise ValueError('Invalid MSF7 stream block')
            if index == 1:
                if size == 0xffffffff or size < 28:
                    raise ValueError('Missing PDB Info stream')
                info = read_blocks(numbers, size)
        if cursor != len(directory):
            raise ValueError('Unexpected MSF7 directory bytes')
        version, signature, age = struct.unpack('<III', _checked(info, 0, 12))
        if version != 20000404:
            raise ValueError('Unsupported native PDB Info version')
        return _identity(_checked(info, 12, 16), age)


def _name(data):
    raw = bytes(data).split(b'\0', 1)
    if len(raw) != 2 or not raw[0]:
        raise ValueError('Missing CodeView PDB path')
    return raw[0].decode('utf-8')


def pe_codeview(data) -> list[dict]:
    if _checked(data, 0, 2) != b'MZ':
        raise ValueError('Expected a PE executable')
    pe = _u32(data, 0x3c)
    if _checked(data, pe, 4) != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    sections = struct.unpack('<H', _checked(data, pe + 6, 2))[0]
    optional_size = struct.unpack('<H', _checked(data, pe + 20, 2))[0]
    optional = _checked(data, pe + 24, optional_size)
    magic = struct.unpack('<H', _checked(optional, 0, 2))[0]
    directory_offset = {0x10b: 96, 0x20b: 112}.get(magic)
    if directory_offset is None:
        raise ValueError('Unsupported PE optional header')
    if _u32(optional, directory_offset - 4) <= 6:
        return []
    address, length = struct.unpack('<II', _checked(optional, directory_offset + 6 * 8, 8))
    if not address and not length:
        return []
    if not address or not length or length % 28:
        raise ValueError('Invalid PE debug directory')
    def mapped(rva, size):
        matches = []
        for index in range(sections):
            record = _checked(data, pe + 24 + optional_size + index * 40, 40)
            virtual_size, virtual_address, raw_size, raw = struct.unpack_from('<4I', record, 8)
            if virtual_address <= rva and rva + size <= virtual_address + raw_size:
                matches.append(raw + rva - virtual_address)
        if len(matches) != 1:
            raise ValueError('PE debug RVA is not uniquely mapped')
        return _checked(data, matches[0], size)
    entries = mapped(address, length)
    result = []
    for cursor in range(0, length, 28):
        kind, size, rva, raw = struct.unpack_from('<4I', entries, cursor + 12)
        if kind != 2:
            continue
        codeview = _checked(data, raw, size) if raw else mapped(rva, size)
        if _checked(codeview, 0, 4) != b'RSDS':
            raise ValueError('Unsupported PE CodeView record (expected RSDS)')
        result.append({**_identity(_checked(codeview, 4, 16), _u32(codeview, 20)),
                       'reference': _name(codeview[24:])})
    return result


def _archive_objects(data):
    if _checked(data, 0, 8) != b'!<arch>\n':
        raise ValueError('Expected a regular COFF archive')
    cursor = 8
    while cursor < len(data):
        header = _checked(data, cursor, 60)
        if header[58:60] != b'`\n' or not re.fullmatch(rb' *[0-9]+ *', header[48:58]):
            raise ValueError('Malformed COFF archive member')
        size = int(header[48:58]);name = bytes(header[:16]).strip()
        member = _checked(data, cursor + 60, size)
        if name.startswith(b'#1/'):
            name_size = int(name[3:]);member = _checked(member, name_size, size - name_size)
        if name not in {b'/', b'//', b'/SYM64/'} and not name.startswith(b'__.SYMDEF'):
            yield member
        cursor += 60 + size + (size & 1)
    if cursor != len(data):
        raise ValueError('Malformed COFF archive alignment')


def coff_debug(data) -> tuple[bool, list[dict]]:
    """Read C13 type-server records; keep embedded CodeView objects complete."""
    found = False;references = []
    for member in _archive_objects(data):
        if len(member) < 20:
            raise ValueError('Truncated COFF archive object')
        if member[:4] == b'\0\0\xff\xff':
            version = struct.unpack('<H', _checked(member, 4, 2))[0]
            if version == 0:
                # Short import objects have no embedded debug information.
                continue
            if version == 1 and _checked(member, 12, 16) == bytes.fromhex('38feb30ca5d9ab4dac9bd6b6222653c2'):
                if not _u32(member, 28) or _u32(member, 28) != len(member) - 32:
                    raise ValueError('Malformed MSVC LTCG anonymous object size')
                # MSVC /GL stores its IR in an anonymous object, rather than
                # ordinary COFF sections. Keep the entire original object and
                # extract only complete CodeView TypeServer2 records, including
                # their bounded name and exact CodeView alignment padding.
                position = 0;servers = []
                while (position := member.find(b'\x15\x15', position)) >= 0:
                    start = position - 2;position += 2
                    if start < 0:
                        continue
                    length = struct.unpack('<H', _checked(member, start, 2))[0]
                    if length < 24 or (length + 2) % 4 or start + 2 + length > len(member):
                        continue
                    record = member[start + 2:start + 2 + length]
                    name, separator, padding = bytes(record[22:]).partition(b'\0')
                    if not separator or not name.lower().endswith(b'.pdb') or len(padding) > 3:
                        continue
                    if padding != bytes(range(0xf0 + len(padding), 0xf0, -1)):
                        continue
                    try:
                        path = name.decode('utf-8')
                    except UnicodeError:
                        continue
                    servers.append({**_identity(record[2:18], _u32(record, 18)), 'reference': path})
                if not servers:
                    # /GL is also valid without /Zi. A recognized complete IR
                    # object without a complete type-server record supplies no
                    # independently verifiable PDB; never guess a nearby PDB.
                    continue
                found = True;references.extend(servers)
                continue
            if version != 2:
                raise ValueError('Unsupported anonymous COFF object version')
            if _checked(member, 12, 16) != bytes.fromhex('c7a1bad1eebaa94baf20faf66aa4dcb8'):
                raise ValueError('Unsupported anonymous COFF object')
            number = _u32(member, 44);section_offset = 56
        else:
            machine, number = struct.unpack('<HH', _checked(member, 0, 4))
            if machine not in {0x14c, 0x8664, 0xaa64, 0xa641, 0xa64e}:
                raise ValueError('Unsupported COFF object architecture')
            section_offset = 20 + struct.unpack('<H', _checked(member, 16, 2))[0]
        for index in range(number):
            section = _checked(member, section_offset + index * 40, 40)
            name = bytes(section[:8]).rstrip(b'\0')
            if name not in {b'.debug$S', b'.debug$T'}:
                continue
            size, offset = struct.unpack_from('<II', section, 16)
            if not size:
                continue
            found = True;types = _checked(member, offset, size)
            if name != b'.debug$T':
                continue
            if _u32(types, 0) != 4:
                raise ValueError('Unsupported COFF CodeView type signature')
            cursor = 4
            while cursor < len(types):
                size, kind = struct.unpack('<HH', _checked(types, cursor, 4))
                if size < 2:
                    raise ValueError('Invalid COFF CodeView type length')
                record = _checked(types, cursor + 2, size)
                if kind == 0x1515:
                    references.append({**_identity(_checked(record, 2, 16), _u32(record, 18)),
                                       'reference': _name(record[22:])})
                elif kind in {0x0016, 0x1501}:
                    raise ValueError('Unsupported legacy CodeView type-server record')
                cursor += size + 2
            if cursor != len(types):
                raise ValueError('Malformed COFF CodeView type records')
    unique = {(r['guid'], r['age'], r['reference']): r for r in references}
    return found, list(unique.values())


def _sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def debug_references(source: Path) -> list[dict]:
    """Expose paired CodeView identities for independent archive validation."""
    with Path(source).open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if data[:2] == b'MZ':
            references = pe_codeview(data)
        elif data[:8] == b'!<arch>\n':
            _, references = coff_debug(data)
        else:
            raise ValueError('Unsupported Windows native symbol input')
    return [{key: value for key, value in {**item, 'path': item['reference']}.items() if key != 'reference'}
            for item in references]


def export_windows_symbols(source: Path, functional: Path, folder: Path, row: dict):
    """Return only a library's paired PDBs and full debug-bearing static input."""
    source, functional, folder = Path(source), Path(functional), Path(folder)
    if _sha(source) != _sha(functional):
        raise ValueError('Windows symbol input differs from the functional library')
    with source.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if data[:2] == b'MZ':
            references = pe_codeview(data);kind = 'pe'
            if not references:
                return [], {}
        elif data[:8] == b'!<arch>\n':
            debug, references = coff_debug(data);kind = 'coff'
            if not debug:
                return [], {}
        else:
            raise ValueError('Unsupported Windows native symbol input')
    settings = row.get('settings', {})
    root_value = row.get('buildDirectory') or settings.get('CMAKE_CACHEFILE_DIR')
    if not root_value:
        raise ValueError('Windows symbols require the owning build directory')
    build = Path(root_value).resolve(strict=True)
    if not source.resolve(strict=True).is_relative_to(build):
        raise ValueError('Windows native source is outside its owning build directory')
    candidates = [Path(p) for p in row.get('pdbCandidates', [])]
    for group in row.get('compileGroups', []):
        for fragment in group.get('compileCommandFragments', []):
            command = fragment.get('fragment', '')
            for match in re.finditer(r'(?:^|\s)[-/]Fd\s*(?:"([^"]+)"|([^\s"]+))', command, re.I):
                candidates.append(Path((match.group(1) or match.group(2)).replace('\\', '/')))
    emitted = [];bindings = []
    folder.mkdir(parents=True, exist_ok=True)
    for reference in references:
        path = Path(reference['reference'].replace('\\', '/'))
        choices = [path, *candidates]
        matching = []
        for candidate in choices:
            candidate = candidate if candidate.is_absolute() else build / candidate
            if not candidate.is_file():
                continue
            candidate = candidate.resolve(strict=True)
            if not candidate.is_relative_to(build):
                continue
            if candidate.suffix.lower() != '.pdb':
                continue
            # An unrelated target PDB is not an input merely because it exists.
            if candidate.name.lower() != PureWindowsPath(reference['reference']).name.lower():
                continue
            identity = pdb_identity(candidate)
            # The final PE binding requires both GUID and age. Compiler PDB
            # age advances across /Zi compilations and is not a type-server
            # binding key (LLVM D62837, matching MSVC link.exe behavior).
            if identity['guid'] == reference['guid'] and (kind == 'coff' or identity['age'] == reference['age']):
                matching.append(candidate)
        matching = sorted(set(matching))
        if not matching:
            raise ValueError('Missing or mismatched owned PDB GUID/age: ' + reference['reference'])
        if len({_sha(path) for path in matching}) != 1:
            raise ValueError('Conflicting PDBs share a CodeView identity')
        paired = matching[0]
        # Distinct compiler databases often share the basename vc140.pdb.
        actual_identity = pdb_identity(paired)
        destination = folder / (actual_identity['guid'] + '-' + str(actual_identity['age'])) / paired.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(paired, destination)
        if _sha(destination) != _sha(paired) or pdb_identity(destination) != actual_identity:
            raise ValueError('PDB changed while exporting its symbols')
        if destination not in emitted:
            emitted.append(destination)
        if actual_identity not in bindings:
            bindings.append(actual_identity)
    if kind == 'coff':
        destination = folder / source.name
        shutil.copy2(source, destination);emitted.insert(0, destination)
    identity = {'format': kind, 'pdbBindings': bindings, 'embeddedCodeView': kind == 'coff'}
    if kind == 'coff':
        identity.update(typeServerMatch='guid', typeServerReferences=[{key: item[key] for key in ('guid', 'age')} for item in references])
    return emitted, identity
