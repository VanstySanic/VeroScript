# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Portable application archives with explicit target permissions."""
from pathlib import Path
import tarfile
import zipfile


def application_archive(source: Path, archive: Path, executable: str) -> None:
	files = sorted(path for path in source.rglob('*') if path.is_file())
	if archive.name.endswith('.tar.gz'):
		with tarfile.open(archive, 'w:gz') as output:
			for path in files:
				entry = output.gettarinfo(str(path), path.relative_to(source.parent).as_posix())
				entry.mode = 0o755 if path.relative_to(source).as_posix() == executable else 0o644
				with path.open('rb') as stream:
					output.addfile(entry, stream)
	else:
		with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
			for path in files:
				entry = zipfile.ZipInfo(path.relative_to(source.parent).as_posix())
				entry.create_system = 3
				entry.external_attr = (0o100755 if path.relative_to(source).as_posix() == executable else 0o100644) << 16
				output.writestr(entry, path.read_bytes())
