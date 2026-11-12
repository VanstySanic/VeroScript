# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Build and audit native macOS application bundles and disk-image downloads."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

_VERIFIED_IMAGES={}


def application_name(business):
	return 'VeroScript' + business + 'Installer.app'


def bundle_info(business, package_version, executable):
	return {'CFBundleDevelopmentRegion':'en', 'CFBundleExecutable':executable,
		'CFBundleIdentifier':'com.vanstysanic.veroscript.installer.' + business.lower(),
		'CFBundleName':'VeroScript ' + business + ' Installer',
		'CFBundleDisplayName':'VeroScript ' + business + ' Installer',
		'CFBundlePackageType':'APPL', 'CFBundleShortVersionString':package_version,
		'CFBundleVersion':package_version, 'CFBundleIconFile':'VeroScript.icns',
		'LSMinimumSystemVersion':'15.0', 'NSHighResolutionCapable':True,
		'NSPrincipalClass':'NSApplication'}


def icon_source(repo):
	from veroscript_icons import source_icon_files
	# Use the official primary application icon, never a generated build copy.
	files=source_icon_files(Path(repo))
	icons=[path for name,path in files.items() if name=='icons/veroscript-icon.icns']
	if len(icons)!=1:
		raise ValueError('The official application ICNS icon is not unique')
	return icons[0]


def build_disk_image(repo, executable, output, business, package_version, target, *, expected_payload=None):
	from veroscript_products import INSTALLER_BUSINESSES
	from veroscript_release import TARGETS, native_identity, binary_prefix, digest
	if sys.platform!='darwin' or target not in {'macos-x64','macos-arm64'} or business not in INSTALLER_BUSINESSES:
		raise ValueError('Native macOS installer creation requires macOS and a registered installer business')
	executable,output=Path(executable).resolve(),Path(output).resolve()
	if native_identity(binary_prefix(executable))!=TARGETS[target]:
		raise ValueError('macOS installer executable target mismatch')
	output.parent.mkdir(parents=True,exist_ok=True)
	with tempfile.TemporaryDirectory(prefix='veroscript-dmg-',dir=output.parent) as temporary:
		work=Path(temporary)
		volume=work/'volume'
		app=volume/application_name(business)
		macos=app/'Contents/MacOS'
		resources=app/'Contents/Resources'
		macos.mkdir(parents=True)
		resources.mkdir()
		program=macos/'VeroScriptInstaller'
		shutil.copy2(executable,program)
		program.chmod(0o755)
		shutil.copy2(icon_source(repo),resources/'VeroScript.icns')
		(app/'Contents/Info.plist').write_bytes(plistlib.dumps(bundle_info(business,package_version,program.name)))
		# Ad-hoc signing makes the application structurally valid on Apple Silicon.
		# It is distinct from Developer ID signing and notarization.
		subprocess.run(['/usr/bin/codesign','--force','--sign','-','--timestamp=none',str(app)],check=True)
		(volume/'installer-distribution.json').write_text(json.dumps({'schemaVersion':1,'business':business,
			'version':package_version,'target':target,'executableSha256':digest(program),
			'iconSha256':digest(resources/'VeroScript.icns'),'signing':'ad-hoc','notarized':False},indent=2)+'\n',encoding='utf-8')
		subprocess.run(['/usr/bin/codesign','--verify','--deep','--strict',str(app)],check=True)
		subprocess.run(['/usr/bin/hdiutil','create','-ov','-fs','HFS+','-volname',('VeroScript '+business+' '+package_version)[:27],
			'-srcfolder',str(volume),'-format','UDZO',str(output)],check=True)
		subprocess.run(['/usr/bin/hdiutil','verify',str(output)],check=True)
	verify_disk_image(output,business,package_version,target,repo,expected_payload=expected_payload)
	return output


def _sevenzip():
	value=os.environ.get('VEROSCRIPT_7ZIP') or shutil.which('7zz') or shutil.which('7z')
	if not value or not Path(value).is_file():
		raise ValueError('Auditing a macOS DMG on this host requires 7-Zip (VEROSCRIPT_7ZIP or 7zz/7z on PATH)')
	return value


def _extract_with_7zip(image, output):
	tool=_sevenzip()
	listing=subprocess.check_output([tool,'l','-slt','-sccUTF-8','--',str(image)],text=True,encoding='utf-8',errors='strict')
	entries=listing.split('----------',1)[-1]
	for name in re.findall(r'^Path = (.+)$',entries,re.MULTILINE):
		parts=name.replace('\\','/').split('/')
		if name.startswith(('/','\\')) or '..' in parts or re.match(r'^[A-Za-z]:',name):
			raise ValueError('Unsafe DMG archive path')
	if sum(map(int,re.findall(r'^Size = (\d+)$',entries,re.MULTILINE)))>8*1024**3:
		raise ValueError('DMG expansion exceeds the installer audit limit')
	subprocess.run([tool,'x','-y','-o'+str(output),'--',str(image)],check=True,stdout=subprocess.DEVNULL)


@contextmanager
def mounted_contents(path):
	with tempfile.TemporaryDirectory(prefix='veroscript-dmg-audit-') as temporary:
		work=Path(temporary)
		if sys.platform=='darwin':
			mount=work/'mount'
			mount.mkdir()
			subprocess.run(['/usr/bin/hdiutil','attach','-readonly','-nobrowse','-mountpoint',str(mount),str(path)],check=True,stdout=subprocess.DEVNULL)
			try:
				yield mount
			finally:
				subprocess.run(['/usr/bin/hdiutil','detach',str(mount)],check=True,stdout=subprocess.DEVNULL)
		else:
			outer=work/'outer'
			outer.mkdir()
			_extract_with_7zip(path,outer)
			apps=list(outer.rglob('*.app'))
			if not apps:
				files=[p for p in outer.rglob('*') if p.is_file() and p.suffix.casefold() in {'.hfs','.hfsx','.img'}]
				if len(files)!=1:
					raise ValueError('DMG has no unique HFS installer filesystem')
				inner=work/'inner'
				inner.mkdir()
				_extract_with_7zip(files[0],inner)
				yield inner
			else:
				yield outer


def verify_disk_image(path,business,package_version,target,repo=None,*,expected_payload=None):
	from veroscript_release import TARGETS,binary_prefix,digest,native_identity
	from veroscript_products import INSTALLER_BUSINESSES
	from veroscript_installer import verify_apphost
	if business not in INSTALLER_BUSINESSES or target not in {'macos-x64','macos-arm64'}:
		raise ValueError('Invalid macOS installer product/target')
	key=(digest(Path(path)),business,package_version,target,digest(icon_source(repo)) if repo else None,expected_payload)
	if key in _VERIFIED_IMAGES:
		return dict(_VERIFIED_IMAGES[key])
	with Path(path).open('rb') as stream:
		stream.seek(-512,2)
		if stream.read(4)!=b'koly':
			raise ValueError('Installer is not a UDIF disk image')
	with mounted_contents(Path(path).resolve()) as contents:
		apps=list(contents.rglob('*.app'))
		if len(apps)!=1 or apps[0].name!=application_name(business):
			raise ValueError('DMG does not contain exactly the expected installer application')
		app=apps[0]
		program=app/'Contents/MacOS/VeroScriptInstaller'
		resources=app/'Contents/Resources'
		info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
		if info!=bundle_info(business,package_version,program.name):
			raise ValueError('Installer application Info.plist differs from its product contract')
		if native_identity(binary_prefix(program))!=TARGETS[target]:
			raise ValueError('DMG installer architecture mismatch')
		if sys.platform=='darwin' and not program.stat().st_mode&0o111:
			raise ValueError('Application executable is not executable')
		record=json.loads((app.parent/'installer-distribution.json').read_text(encoding='utf-8'))
		if record!={'schemaVersion':1,'business':business,'version':package_version,'target':target,
			'executableSha256':digest(program),'iconSha256':digest(resources/'VeroScript.icns'),'signing':'ad-hoc','notarized':False}:
			raise ValueError('Installer application inventory/hash mismatch')
		if repo and digest(resources/'VeroScript.icns')!=digest(icon_source(repo)):
			raise ValueError('Installer application icon differs from the official source icon')
		verify_apphost(program,target,expected_payload)
		if sys.platform=='darwin':
			from veroscript_release import host_target
			if target==host_target():
				with tempfile.TemporaryDirectory(prefix='veroscript-dmg-runtime-') as extraction:
					environment=os.environ|{'DOTNET_BUNDLE_EXTRACT_BASE_DIR':extraction}
					for argument in ('--verify-embedded-bundle','--verify-localization'):
						subprocess.run([str(program),argument],env=environment,check=True,stdout=subprocess.DEVNULL)
	_VERIFIED_IMAGES[key]=dict(record)
	return record
