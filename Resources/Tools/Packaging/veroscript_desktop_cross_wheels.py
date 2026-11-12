# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Compile each desktop CPython ABI against one freshly built target Interop SDK."""
import base64
import csv
from email.parser import BytesParser
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

from veroscript_jit import build_cache
from veroscript_products import WINDOWS_WHEEL_TAGS
from veroscript_python_wheels import distribution_metadata,macos_deployment_version,supported_versions,verify_matrix,verify_python_requirement,verify_source_distribution
from veroscript_release import TARGETS,archive_entries,binary_prefix,digest,native_identity,stage_localization


def compiler_arguments(settings,target):
	generator=settings['CMAKE_GENERATOR']
	arguments=['-G',generator,'-DCMAKE_BUILD_TYPE=Release',
		'-DCMAKE_SYSTEM_NAME='+('Windows' if target.startswith('win-') else 'Darwin' if target.startswith('macos-') else 'Linux')]
	platform=settings.get('CMAKE_GENERATOR_PLATFORM')
	if generator.startswith('Visual Studio') and target.startswith('win-') and not platform:
		platform={'win-x64':'x64','win-x86':'Win32','win-arm64':'ARM64'}[target]
	if platform:
		arguments.extend(('-A',platform))
	if settings.get('CMAKE_GENERATOR_TOOLSET'):
		arguments.extend(('-T',settings['CMAKE_GENERATOR_TOOLSET']))
	if settings.get('CMAKE_CXX_COMPILER'):
		arguments.append('-DCMAKE_CXX_COMPILER='+settings['CMAKE_CXX_COMPILER'])
	elif not settings.get('CMAKE_TOOLCHAIN_FILE') and not generator.startswith('Visual Studio'):
		raise ValueError('Target compiler or an explicit cross toolchain is required')
	for key in ('CMAKE_TOOLCHAIN_FILE','CMAKE_MAKE_PROGRAM','CMAKE_GENERATOR_INSTANCE','CMAKE_LINKER',
			'CMAKE_CXX_COMPILER_TARGET','CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN','CMAKE_SYSROOT',
			'CMAKE_SYSROOT_COMPILE','CMAKE_SYSROOT_LINK','CMAKE_FIND_ROOT_PATH',
			'CMAKE_OSX_SYSROOT','CMAKE_OSX_ARCHITECTURES','CMAKE_OSX_DEPLOYMENT_TARGET',
			'CMAKE_CXX_FLAGS','CMAKE_CXX_FLAGS_RELEASE'):
		if settings.get(key):
			arguments.append('-D'+key+'='+settings[key])
	processor=settings.get('CMAKE_SYSTEM_PROCESSOR') or ('ARM64' if target=='win-arm64' else 'x86' if target=='win-x86' else 'arm64' if target.endswith('arm64') else 'x86_64')
	arguments.append('-DCMAKE_SYSTEM_PROCESSOR='+processor)
	return arguments


def verify_target_python_abi(settings,sdk,directory,target):
	"""Compile target header facts, without linking or executing target Python."""
	directory=Path(directory)
	source=directory/'abi-source'
	build=directory/'abi-build'
	source.mkdir(parents=True,exist_ok=True)
	major,minor,micro=map(int,sdk['pythonVersion'].split('.'))
	(source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\n'
		'project(VeroScriptTargetPythonAbi LANGUAGES CXX)\n'
		'add_library(VeroScriptPythonAbi OBJECT abi.cpp)\n'
		'target_include_directories(VeroScriptPythonAbi PRIVATE "${TARGET_PYTHON_INCLUDE}")\n'
		'target_compile_features(VeroScriptPythonAbi PRIVATE cxx_std_17)\n',encoding='utf-8')
	(source/'abi.cpp').write_text('#if defined(_WIN32) && defined(_DEBUG)\n#undef _DEBUG\n#endif\n'
		'#define Py_NO_LINK_LIB\n#include <Python.h>\n'
		'#if defined(Py_DEBUG) || defined(Py_GIL_DISABLED) || defined(Py_TRACE_REFS)\n'
		'#error VeroScript release wheels require the normal non-debug CPython ABI\n#endif\n'
		f'static_assert(PY_MAJOR_VERSION == {major} && PY_MINOR_VERSION == {minor} && PY_MICRO_VERSION == {micro}, "Target CPython header version differs from SDK");\n'
		f'static_assert(sizeof(void*) == {int(sdk["pointerSize"])}, "Target CPython pointer size differs from compiler");\n',encoding='utf-8')
	cmake=settings.get('CMAKE_COMMAND','cmake')
	subprocess.run([cmake,'-S',str(source),'-B',str(build),*compiler_arguments(settings,target),
		'-DTARGET_PYTHON_INCLUDE:PATH='+str(Path(sdk['includeDirectory']).resolve())],check=True)
	subprocess.run([cmake,'--build',str(build),'--config','Release','--target','VeroScriptPythonAbi'],check=True)


def reference_metadata(reference, sdist, version, versions):
	from veroscript_business_release import distribution_identity
	info=f'veroscript-{version}.dist-info/'
	entries=archive_entries(reference,max_entry_bytes=1024*1024)
	metadata={name:data for name,(data,_) in entries.items()
		if name.startswith(info) and name[len(info):] not in ('WHEEL','RECORD','RECORD.jws','RECORD.p7s')}
	message=BytesParser().parsebytes(metadata.get(info+'METADATA',b''))
	if message['Name']!='veroscript' or message['Version']!=version:
		raise ValueError('Reference wheel metadata version mismatch')
	verify_python_requirement(metadata[info+'METADATA'],versions,reference)
	entries=archive_entries(sdist,max_entry_bytes=4096)
	if distribution_identity(sdist,entries=entries)!=('Python',version):
		raise ValueError('Reference Python source distribution version mismatch')
	verify_python_requirement(distribution_metadata(sdist,entries),versions,sdist)
	verify_source_distribution(sdist)
	return metadata


def stage_extension(source, destination, settings, target, scratch):
	"""Keep target debug data in the work tree, outside the functional wheel."""
	from veroscript_native_symbols import LinuxNativeSymbols, MacNativeSymbols
	if target.startswith('linux-'):
		symbols = LinuxNativeSymbols(scratch, settings.get('CMAKE_OBJCOPY', 'objcopy'),
			settings.get('CMAKE_OBJDUMP', 'objdump'))
	elif target.startswith('macos-'):
		symbols = MacNativeSymbols(scratch, settings.get('CMAKE_STRIP', 'strip'))
	else:
		symbols = None
	prepared = symbols.prepare(source) if symbols else None
	shutil.copy2(prepared[0] if prepared else source, destination)
	if native_identity(binary_prefix(destination)) != TARGETS[target]:
		raise ValueError('Staged CPython extension has the wrong target architecture')
	return destination


def build_matrix(repo,build,interop,sdks,reference,output,work,target,version,windows_crt=None):
	settings=build_cache(build)
	cmake=settings.get('CMAKE_COMMAND','cmake')
	versions=supported_versions(repo)
	values={}
	for path in sdks:
		value=json.loads(Path(path).read_text(encoding='utf-8'))
		minor='.'.join(value['pythonVersion'].split('.')[:2])
		if minor in values:
			raise ValueError('Duplicate target CPython SDK')
		values[minor]=(Path(path).resolve(),value)
	if set(values)!=set(versions):
		raise ValueError('Cross wheel SDKs do not cover the declared CPython minor versions')
	metadata=reference_metadata(reference,output/f'veroscript-{version}.tar.gz',version,versions)
	work.mkdir(parents=True,exist_ok=True)
	records=[]
	for minor,(sdk,value) in sorted(values.items()):
		directory=work/('cp'+minor.replace('.',''))
		verify_target_python_abi(settings,value,directory,target)
		command=[cmake,'-S',str(repo/'Extensions/Language/Python/Resources/Tools/Desktop'),'-B',str(directory),*compiler_arguments(settings,target),
			'-DVEROSCRIPT_MODULE_ROOT='+str(repo),'-DVEROSCRIPT_INTEROP_PREFIX='+str(interop),'-DVEROSCRIPT_PYTHON_DESKTOP_SDK='+str(sdk)]
		subprocess.run(command,check=True)
		subprocess.run([cmake,'--build',str(directory),'--config','Release','--parallel','2'],check=True)
		extensions=list((directory/'extension').rglob('_veroscript'+value['extensionSuffix']))
		if len(extensions)!=1 or native_identity(binary_prefix(extensions[0]))!=TARGETS[target]:
			raise ValueError('Cross CPython extension is missing or has the wrong ABI target')
		stage=directory/'wheel'
		package=stage/'veroscript'
		package.mkdir(parents=True)
		ignore_cache=shutil.ignore_patterns('__pycache__','*.pyc','*.pyo')
		shutil.copytree(repo/'Extensions/Language/Python/VeroScriptPython/python/veroscript',package,dirs_exist_ok=True,ignore=ignore_cache)
		shutil.copytree(repo/'VeroScript/Resources/Scripts/stdlib',package/'stdlib')
		stage_localization(repo,package/'localization','python')
		for name,module in [('frontend','VeroScriptPythonFrontend'),('bindgen','VeroScriptPythonBindGen'),('transpiler','VeroScriptPythonTranspiler'),('aot','VeroScriptPythonAot')]:
			shutil.copytree(repo/'Extensions/Language/Python'/module/'python'/('veroscript_python_'+name),stage/('veroscript_python_'+name),ignore=ignore_cache)
		stage_extension(extensions[0], package/extensions[0].name, settings, target, directory/'native-symbols')
		library='VeroScriptInterop.dll' if target.startswith('win-') else 'libVeroScriptInterop.'+('dylib' if target.startswith('macos-') else 'so')
		interop_library=next(p for p in (interop/'bin'/library,interop/'lib'/library) if p.is_file())
		shutil.copy2(interop_library,package/library)
		if target.startswith('win-'):
			if windows_crt is None:
				raise ValueError('Target Windows CRT is required for cross wheels')
			from veroscript_windows_crt import target_crt_files
			for dll in target_crt_files(windows_crt,target):
				shutil.copy2(dll,package/dll.name)
			notice=package/'licenses/MicrosoftVisualCpp/NOTICE.txt'
			notice.parent.mkdir(parents=True)
			shutil.copy2(repo/'Resources/Tools/CMake/WindowsRuntimeNotice.txt',notice)
		for vendor,file in [('miniz','LICENSE'),('json','LICENSE.MIT'),('sha256','LICENSE')]:
			folder=package/'licenses/application'/vendor
			folder.mkdir(parents=True)
			shutil.copy2(repo/'VeroScriptApplication/ThirdParty'/vendor/file,folder/file)
		for name,data in metadata.items():
			file=stage/name
			file.parent.mkdir(parents=True,exist_ok=True)
			file.write_bytes(data)
		tag='cp'+minor.replace('.','')
		if target.startswith('macos-'):
			minimum=(15,0,0)
			for library_path in (extensions[0],interop_library):
				with library_path.open('rb') as source:
					minimum=max(minimum,macos_deployment_version(source.read(1024*1024)))
			platform_major,platform_minor,platform_patch=minimum
			architecture='arm64' if target=='macos-arm64' else 'x86_64'
			platform=f'macosx_{platform_major}_{platform_minor + bool(platform_patch)}_{architecture}'
		else:
			platform=({**WINDOWS_WHEEL_TAGS,'linux-arm64':'linux_aarch64','linux-x64':'linux_x86_64'})[target]
		wheel_tag=tag+'-'+tag+'-'+platform
		info=stage/f'veroscript-{version}.dist-info'
		(info/'WHEEL').write_text('Wheel-Version: 1.0\nGenerator: VeroScript target SDK builder\nRoot-Is-Purelib: false\nTag: '+wheel_tag+'\n',encoding='utf-8')
		rows=[]
		for file in sorted(stage.rglob('*')):
			if not file.is_file():
				continue
			data=file.read_bytes()
			rows.append([file.relative_to(stage).as_posix(),'sha256='+base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode(),str(len(data))])
		record=info/'RECORD'
		rows.append([record.relative_to(stage).as_posix(),'',''])
		buffer=io.StringIO()
		csv.writer(buffer,lineterminator='\n').writerows(rows)
		record.write_text(buffer.getvalue(),encoding='utf-8')
		wheel=output/f'veroscript-{version}-{wheel_tag}.whl'
		with zipfile.ZipFile(wheel,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
			for file in sorted(stage.rglob('*')):
				if file.is_file():
					entry=zipfile.ZipInfo(file.relative_to(stage).as_posix(),(1980,1,1,0,0,0))
					entry.external_attr=(0o100644<<16)
					entry.compress_type=zipfile.ZIP_DEFLATED
					archive.writestr(entry,file.read_bytes())
		records.append({'python':minor,'wheel':wheel.name,'sha256':digest(wheel),'build':'passed','targetRuntime':'not-run'})
	verify_matrix(list(output.iterdir()),repo,target,version)
	(work/'python-wheel-matrix.json').write_text(json.dumps({'target':target,'wheels':records,'runtimeAcceptance':'not-run'},indent=2)+'\n')
	return records
