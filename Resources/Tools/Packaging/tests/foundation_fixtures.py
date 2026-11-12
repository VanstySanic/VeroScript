# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Synthetic independent SDK archives for release transaction tests."""
import hashlib
import json

from veroscript_foundation import payload_id, release_config
from veroscript_products import FOUNDATION_PACKAGE_NAMES


def foundation_files(business, versions, target, native, license_text):
	base = 'VeroScript' + business
	export = (f'lib/cmake/VeroScriptVisual/VeroScriptVisual{business}' if business in {'Platform', 'Graphics', 'Audio', 'Sdl3'}
			  else f'lib/cmake/{base}/{base}')
	files = {'LICENSE': license_text,
			 f'lib/cmake/{base}/{base}Config.cmake': b'config',
			 f'lib/cmake/{base}/{base}ConfigVersion.cmake': b'version',
			 export + 'Targets.cmake': b'targets'}
	headers = {'Platform': ['VeroScriptPlatformApi.h'], 'Graphics': ['VeroScriptGraphics.h', 'VeroScriptGraphics2D.h'],
			   'Audio': ['VeroScriptAudio.h'], 'Sdl3': ['VeroScriptSdl3.h', 'VeroScriptSdl3Module.h'],
			   'Image': ['VeroScriptImage.h'], 'Font': ['VeroScriptFont.h', 'VeroScriptFontLayout.h'],
			   'Stb': ['VeroScriptStb.h'], 'FontBackend': ['VeroScriptFontBackend.h'],
			   'Gui': ['VeroScriptGuiVersion.h', 'VeroScriptGuiWidget.h', 'VeroScriptGuiContext.h', 'VeroScriptGuiApplication.h'],
			   'RmlUi': ['VeroScriptRmlUi.h']}
	files.update({'include/VeroScriptVisual/' + name: b'header' for name in headers[business]})
	if business == 'Gui':
		files['include/VeroScriptVisual/VeroScriptGuiVersion.h'] = (
			f'namespace VeroScript::Gui {{ inline constexpr char Version[] = "{versions[business]}"; }}\n'.encode())
	meta = {'schemaVersion': 1, 'business': business, 'name': FOUNDATION_PACKAGE_NAMES[business],
			'version': versions[business], 'target': target, 'profile': 'native-data-sdk',
			'dependencies': {}, 'dependencyBuildIds': {}, 'systemDependencies': []}
	if business == 'Sdl3':
		files.update({'lib/libVeroScriptSdl3.so': native, 'lib/libSDL3.so': native,
					  'licenses/SDL3/LICENSE.txt': b'license', 'licenses/SDL3/SDL3.version.json': b'{}',
					  'licenses/SDL3/NOTICES.md': b'notices', 'include/SDL3/SDL.h': b'header',
					  'lib/cmake/SDL3/SDL3Config.cmake': b'config'})
		dependency = json.loads(foundation_files('Platform', versions, target, native, license_text)['veroscript-module.json'])
		meta['dependencies'] = {'Platform': dependency['version']}
		meta['dependencyBuildIds'] = {'Platform': dependency['payloadId']}
		meta['features'] = {key: False for key in ('audio', 'controllers', 'gpu', 'd3d11', 'opengl', 'webgpu', 'nativeExtension')}
	else:
		files[f'lib/lib{base}.a'] = b'ar'
	for dependency in {'Gui': ['Platform', 'Graphics', 'Image', 'Font'], 'RmlUi': ['Gui'],
					   'Stb': ['Image'], 'FontBackend': ['Font']}.get(business, []):
		dependency_meta = json.loads(foundation_files(dependency, versions, target, native, license_text)['veroscript-module.json'])
		meta['dependencies'][dependency] = dependency_meta['version']
		meta['dependencyBuildIds'][dependency] = dependency_meta['payloadId']
	if business == 'Stb':
		files.update({'licenses/stb/LICENSE': b'license', 'licenses/stb/stb.version.json': b'{}'})
	if business == 'Font':
		files.update({'licenses/Fonts/LICENSE.Lato.txt': b'font license'})
	if business == 'FontBackend':
		files.update({'licenses/FreeType/FTL.TXT': b'license', 'licenses/FreeType/LICENSE.TXT': b'license',
					  'licenses/FreeType/FreeType.version.json': b'{}', 'licenses/HarfBuzz/COPYING': b'license',
					  'licenses/HarfBuzz/HarfBuzz.version.json': b'{}', 'include/freetype2/ft2build.h': b'header',
					  f'lib/cmake/{base}/{base}DependenciesTargets.cmake': b'targets',
					  'lib/libfreetype.a': b'ar', 'lib/libVeroScriptFontBackendHarfBuzz.a': b'ar',
					  'lib/libVeroScriptFontBackendSheenBidi.a': b'ar',
					  'licenses/SheenBidi/LICENSE.Apache-2.0.txt': b'license',
					  'licenses/SheenBidi/LICENSE.Unicode-3.0.txt': b'license',
					  'licenses/SheenBidi/NOTICE.SheenBidi.txt': b'notices',
					  'licenses/SheenBidi/SheenBidi.version.json': b'{}'})
	if business == 'RmlUi':
		files.update({'licenses/RmlUi/LICENSE.txt': b'license', 'licenses/RmlUi/RmlUi.version.json': b'{}',
					  'licenses/RmlUi/NOTICES.md': b'notices', 'licenses/RmlUi/LICENSE.Containers.txt': b'license',
					  'licenses/RmlUi/LICENSE.Debugger.txt': b'license', 'lib/librmlui.a': b'ar',
					  'lib/cmake/RmlUi/RmlUiConfig.cmake': b'config', 'lib/cmake/RmlUi/RmlUiTargets.cmake': b'targets'})
	meta['payloadFiles'] = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
	meta['payloadId'] = payload_id(meta)
	files[f'lib/cmake/{base}/{base}Release.cmake'] = release_config(business, meta).encode()
	files['veroscript-module.json'] = json.dumps(meta).encode()
	return files
