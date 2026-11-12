# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Python distribution metadata from a discovered VeroScript source module."""
from pathlib import Path
import re


def source_module(project: Path) -> Path:
	project = project.resolve()
	candidates = [path.parents[2] for path in project.glob('*/VeroScript/Public/VeroScriptVersion.h')
				  if path.is_file()]
	if (project / 'VeroScript/Public/VeroScriptVersion.h').is_file():
		candidates.append(project)
	# A checkout is packaged from Extensions/Language/Python; an SDist owns its native
	# sources below _native and is discovered by the scan above.
	if len(project.parents) >= 3:
		checkout = project.parents[2]
		if (checkout / 'Extensions/Language/Python' == project
				and (checkout / 'VeroScript/Public/VeroScriptVersion.h').is_file()):
			candidates.append(checkout)
	if len(candidates) != 1:
		raise ValueError('Expected exactly one VeroScript source module')
	return candidates[0]


def dynamic_metadata(settings, project):
	if settings:
		raise ValueError('VeroScript metadata provider takes no settings')
	root = source_module(Path.cwd())
	header = (root / 'VeroScript/Public/VeroScriptVersion.h').read_text(encoding='utf-8-sig')
	match = re.search(r'VEROSCRIPT_VERSION\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}', header)
	if match is None:
		raise ValueError('Missing native VeroScript version declaration')
	return {'version': '.'.join(match.groups())}
