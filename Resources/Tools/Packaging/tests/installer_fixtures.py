# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import hashlib
import struct
import zlib

from veroscript_installer import BUNDLE_SIGNATURE, PART_PREFIX


def installer_apphost(native, payload=b'fixture installer payload', *, parts=1, compress=False, checksum=None):
	def string(value):
		data = value.encode('ascii')
		return bytes([len(data)]) + data

	checksum = checksum or hashlib.sha256(payload).hexdigest()
	assembly = b''.join(b'\x01\x00' + string(key) + string(value) + b'\x00\x00' for key, value in (
		('VeroScriptInstaller.BundlePartCount', str(parts)), ('VeroScriptInstaller.BundleSha256', checksum)))
	files = [('VeroScriptInstaller.dll', assembly, 1)]
	for index in range(parts):
		files.append((f'{PART_PREFIX}{index:04d}', payload[len(payload) * index // parts:len(payload) * (index + 1) // parts], 0))
	offset = len(native) + 8 + len(BUNDLE_SIGNATURE)
	body, entries = [], []
	for name, data, kind in files:
		if compress:
			encoder = zlib.compressobj(wbits=-15)
			encoded = encoder.compress(data) + encoder.flush()
		else:
			encoded = data
		stored = encoded if len(encoded) < len(data) else data
		entries.append(struct.pack('<qqqB', offset, len(data), len(stored) if stored != data else 0, kind) + string(name))
		body.append(stored)
		offset += len(stored)
	manifest = struct.pack('<IIi', 6, 0, len(entries)) + string('fixture') + bytes(40) + b''.join(entries)
	return native + struct.pack('<q', offset) + BUNDLE_SIGNATURE + b''.join(body) + manifest
