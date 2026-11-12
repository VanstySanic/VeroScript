# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Content contract for installable execution backends; portable archives stay separate."""
import json
import re

from veroscript_products import verify_module_payload
from veroscript_release import TARGETS, native_identity, verify_windows_runtime


def verify_backend_entries(entries, name, service, backend, version, target):
	verify_module_payload(entries)
	system, architecture = TARGETS[target]
	library = ('VeroScriptJitAsmJit' if service == 'jit' else 'VeroScriptVmBytecode')
	library = library + '.dll' if system == 'windows' else 'lib' + library + ('.so' if system == 'linux' else '.dylib')
	try:
		manifest = json.loads(entries['veroscript-package.json'][0])
		context = manifest['target']
		libraries = manifest['entries']['libraries']
		expected = {'service': service, 'id': backend, 'contract': service, 'path': library,
					'libraries': [item['id'] for item in libraries]}
		if (manifest.get('format') != 'veroscript-package' or manifest.get('formatVersion') != '0.1.1'
				or manifest.get('name') != name or manifest.get('version') != version
				or (context.get('os'), context.get('arch')) != (system, architecture)
				or not context.get('environment') or not re.fullmatch('[0-9a-f]{64}', context.get('coreBuild', ''))
				or not context.get('abis', {}).get(service) or not manifest.get('requires', {}).get('core')
				or manifest.get('entries') != {'backends': [expected], 'libraries': libraries}
				or len({item['id'] for item in libraries}) != len(libraries)
				or any(item.get('linkage') != 'dynamic' or not entries[item['path']][0] for item in libraries)
				or manifest.get('licenseFile') != 'LICENSE' or not entries['LICENSE'][0]
				or native_identity(entries[library][0][:4096]) != (system, architecture)):
			raise ValueError('Installable backend identity/entry mismatch')
		for path, (data, _) in entries.items():
			if data.startswith((b'MZ', b'\x7fELF', b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')):
				if native_identity(data[:4096]) != (system, architecture):
					raise ValueError('Installable backend native target mismatch: ' + path)
		if system == 'windows':
			verify_windows_runtime(entries, (system, architecture))
	except (KeyError, TypeError, IndexError) as error:
		raise ValueError('Missing/invalid installable backend metadata') from error
