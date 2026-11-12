#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Assemble all desktop products from a real foreign-target build.

Build/source/ABI/inventory checks remain mandatory. Target execution is recorded
separately; this entry never silently substitutes host binaries or acceptance.
"""
import argparse
import base64
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

from veroscript_applications import stage_application_runtime
from veroscript_business_release import load_versions
from veroscript_cross_context import CrossPackagingContext
from veroscript_csharp_packages import stage_csharp_packages
from veroscript_icons import stage_runtime_icons,verify_runtime_bundle
from veroscript_installer import package_unix_installer,verify_apphost
from veroscript_jit import stage_backend,pack_backend as pack_jit,build_cache,cmake_enabled
from veroscript_modules import assemble_modules
from veroscript_msvc_dependencies import verify_msvc_dependencies
from veroscript_packages import stage_package_client,stage_package_manager_sdk,finalize_package_manager_sdk
from veroscript_products import DESKTOP_BUSINESSES,SDK_MODULE_BUSINESSES,clean_functional_payload,installer_filename
from veroscript_python_wheels import verify_matrix
from veroscript_release import TARGETS,archive_tree,binary_prefix,copy_tree,digest,native_identity,safe_output,stage_localization,verify_release,write_manifest
from veroscript_vm import stage_runtime,stage_sdk,stage_backend as stage_vm,pack_backend as pack_vm


def run(*args,cwd=None):
	print('+ '+' '.join(map(str,args)),flush=True)
	subprocess.run(list(map(str,args)),check=True,cwd=cwd)


class DesktopCrossRelease:
	def __init__(self,args):
		self.args=args
		self.repo=args.repo_root.resolve()
		self.build=args.build_directory.resolve()
		self.target=args.target
		self.platform,self.arch=TARGETS[self.target]
		self.windows=self.platform=='windows'
		self.pointer=4 if self.arch=='x86' else 8
		self.settings=build_cache(self.build)
		verify_msvc_dependencies(self.build,self.settings)
		self.cmake=self.settings.get('CMAKE_COMMAND','cmake')
		if Path(self.settings['CMAKE_HOME_DIRECTORY']).resolve()!=self.repo:
			raise ValueError('Cross build cache belongs to another source tree')
		if cmake_enabled(self.settings,'VEROSCRIPT_ENABLE_JIT_TEST_HOOKS'):
			raise ValueError('Production builds must disable JIT test hooks')
		version_config=load_versions(self.repo,None,None,None)
		self.versions=version_config['businesses']
		self.native_version=version_config['nativeVersion']
		self.root=safe_output(self.repo,args.output,component=True)
		self.work=safe_output(self.repo,args.work_directory,component=True)
		if self.root==self.work or self.root.is_relative_to(self.work) or self.work.is_relative_to(self.root):
			raise ValueError('Cross candidate and work directories must be separate siblings')
		for role,path in (('candidate output',self.root),('work directory',self.work)):
			if path.exists() and (not path.is_dir() or any(path.iterdir())):
				raise ValueError('Cross '+role+' must be empty; use a new owned staging directory')
		self.root.mkdir(parents=True,exist_ok=True)
		self.work.mkdir(parents=True,exist_ok=True)
		self.bin=next(p for p in (self.build/'bin/Release',self.build/'bin') if (p/self.exe('veroscript')).is_file())
		self.lib=next(p for p in (self.build/'lib/Release',self.build/'lib') if (p/self.static('VeroScript')).is_file())
		self.shared='VeroScriptInterop.dll' if self.windows else 'libVeroScriptInterop.'+('dylib' if self.platform=='macos' else 'so')
		self.interop=next(p for p in (self.bin/self.shared,self.lib/self.shared) if p.is_file())
		self.build_id=digest(self.lib/self.static('VeroScript'))
		self.native_symbols=None
		if self.platform=='linux':
			from veroscript_native_symbols import LinuxNativeSymbols
			self.native_symbols=LinuxNativeSymbols(self.work/'native-cache',self.settings.get('CMAKE_OBJCOPY','objcopy'),self.settings.get('CMAKE_OBJDUMP','objdump'))
			prepared=self.native_symbols.prepare(self.lib/self.static('VeroScript'))
			if prepared:
				self.build_id=digest(prepared[0])
		elif self.platform=='macos':
			from veroscript_native_symbols import MacNativeSymbols
			self.native_symbols=MacNativeSymbols(self.work/'native-cache',self.settings.get('CMAKE_STRIP','strip'))
			prepared=self.native_symbols.prepare(self.lib/self.static('VeroScript'))
			if prepared:
				self.build_id=digest(prepared[0])
		self.context=CrossPackagingContext(self.repo,args.target_contract,args.catalogs,args.host_cli,self.work/'pack-client',native_jit=cmake_enabled(self.settings,'VEROSCRIPT_ASMJIT_NATIVE_ENABLED'),target_tool=getattr(args,'target_tool',None))
		if self.context.target!=self.target:
			raise ValueError('Compiled metadata and requested target differ')
		self.jit={'backend':'none','target':'none','nativeCode':False}
		self.build_ids={}
		self.module_symbol_status={}
		self.components={}

	def exe(self,name):
		return name+('.exe' if self.windows else '')

	def static(self,name):
		return name+'.lib' if self.windows else 'lib'+name+'.a'

	def copy(self,source,dest):
		source,dest=Path(source),Path(dest)
		if not source.is_file():
			raise ValueError('Missing target build product: '+str(source))
		dest.parent.mkdir(parents=True,exist_ok=True)
		shutil.copy2(source,dest)

	def tree(self,source,dest):
		copy_tree(self.repo/source,dest)

	def install(self,component,dest):
		run(self.cmake,'--install',self.build,'--config','Release','--component',component,'--prefix',dest)

	def prepare(self,root,symbols):
		if self.native_symbols:
			self.native_symbols.prepare_tree(root,symbols)

	def symbols(self,destination,sources):
		destination.mkdir(parents=True,exist_ok=True)
		for source in sources:
			if not source.is_file():
				raise ValueError('Missing native symbol input: '+str(source))
			if self.native_symbols:
				self.native_symbols.export_symbols(source,destination)
			elif self.platform=='macos' and source.suffix!='.a':
				run('dsymutil',source,'-o',destination/(source.name+'.dSYM'))
			elif self.windows and source.suffix.lower() in {'.exe','.dll','.pyd'}:
				pdb=source.with_suffix('.pdb')
				if pdb.is_file():
					self.copy(pdb,destination/pdb.name)
			else:
				self.copy(source,destination/source.name)

	def crt(self,root):
		if not self.windows:
			return
		if not self.args.windows_crt:
			raise ValueError('Windows cross releases require the target CRT directory')
		from veroscript_windows_crt import target_crt_files
		libraries=target_crt_files(self.args.windows_crt,self.target)
		directories=set()
		for p in root.rglob('*'):
			if p.is_file() and p.suffix.lower() in {'.exe','.dll','.pyd'}:
				try:
					identity=native_identity(binary_prefix(p))
				except ValueError:
					continue
				if identity==TARGETS[self.target]:
					directories.add(p.parent)
		for folder in directories:
			for library in libraries:
				self.copy(library,folder/library.name)
			self.copy(self.repo/'Resources/Tools/CMake/WindowsRuntimeNotice.txt',folder/'licenses/MicrosoftVisualCpp/NOTICE.txt')

	def archive(self,category,prefix,source):
		from veroscript_business_release import PREFIXES
		version=self.versions[PREFIXES[prefix]]
		self.copy(self.repo/'LICENSE',source/'LICENSE')
		clean_functional_payload(source)
		self.crt(source)
		name=prefix+'-'+version+'-'+self.target
		# The archive root is stable while the component staging path stays reusable.
		portable=self.work/name
		copy_tree(source,portable)
		try:
			archive_tree(portable,self.root/category/(name+('.zip' if self.windows else '.tar.gz')))
		finally:
			shutil.rmtree(portable)

	def config(self,kind,destination):
		cpp=kind=='cpp'
		product='VeroScriptCpp' if cpp else 'VeroScript'
		business='Cpp' if cpp else 'CoreSDK'
		owner='Extensions/Language/Cpp/VeroScriptCpp' if cpp else 'VeroScript'
		values={'VERSION':self.versions[business],'CORE_VERSION':self.versions['CoreSDK'],'BUILD_ID':self.build_id,
			'STATIC_PREFIX':'' if self.windows else 'lib','STATIC_SUFFIX':'.lib' if self.windows else '.a',
			'EXE_SUFFIX':'.exe' if self.windows else '','HAS_ASMJIT':str(int(self.context.native_jit)),
			'JIT_TEST_HOOKS':'0','APPLICATION_ENABLED':'1','ASMJIT_X64':str(int(self.context.native_jit and self.arch=='x64')),
			'ASMJIT_A64':str(int(self.context.native_jit and self.arch=='arm64'))}
		text=(self.repo/owner/'Resources/Tools/CMake'/(product+'Config.cmake.in')).read_text(encoding='utf-8-sig')
		for key,value in values.items():
			text=text.replace('@'+key+'@',value)
		if re.search(r'@[A-Z_]+@',text):
			raise ValueError('Unexpanded SDK configuration token')
		directory=destination/'lib/cmake'/product
		directory.mkdir(parents=True,exist_ok=True)
		(directory/(product+'Config.cmake')).write_text(text,encoding='utf-8')
		run(self.cmake,'-DOUTPUT_FILE:FILEPATH='+str(directory/(product+'ConfigVersion.cmake')),
			'-DPACKAGE_SEMANTIC_VERSION:STRING='+self.versions[business],'-DPACKAGE_POINTER_SIZE:STRING='+str(self.pointer),
			'-P',self.repo/'Resources/Tools/CMake/write-cmake-package-version.cmake')

	def installer(self,business,profile,components):
		root=self.repo/'VeroScriptInstaller'
		tool=root/'Tool/VeroScriptInstaller.Tool.csproj'
		bundle=self.work/(business+'-bundle')
		command=['dotnet','run','--project',tool,'-c','Release','--','build-bundle','--output',bundle,'--version',self.versions[business],
			'--profile',profile,'--platform',self.platform,'--architecture',self.arch]
		for name,source in components.items():
			command+=['--source-'+name,source]
		run(*command,cwd=root)
		if business=='Runtime':
			verify_runtime_bundle(self.repo,bundle,self.target)
		run('dotnet','run','--project',tool,'-c','Release','--','verify','--bundle',bundle,cwd=root)
		embedded=self.work/(business+'-payload.zip')
		run('dotnet','run','--project',tool,'-c','Release','--','pack-bundle','--bundle',bundle,'--output',embedded,cwd=root)
		expected_payload=digest(embedded)
		shutil.rmtree(bundle)
		rid=('osx' if self.platform=='macos' else 'win' if self.windows else 'linux')+'-'+self.arch
		publish=self.work/(business+'-installer-publish')
		run('dotnet','publish',root/'App/VeroScriptInstaller.csproj','-c','Release','-r',rid,'--self-contained','true',
			'-p:PublishSingleFile=true','-p:IncludeNativeLibrariesForSelfExtract=true','-p:EnableCompressionInSingleFile=true','-p:PublishTrimmed=false','-p:DebugType=None','-p:DebugSymbols=false','-p:Version='+self.versions[business],
			'-p:InstallerBundleArchive='+str(embedded),'-o',publish,cwd=root)
		program=publish/self.exe('VeroScriptInstaller')
		program.chmod(program.stat().st_mode|0o111)
		output=self.root/'installer'
		if self.windows:
			download=output/installer_filename(business,self.versions[business],self.target)
			self.copy(program,download)
			verify_apphost(download,self.target,expected_payload)
		else:
			package_unix_installer(self.repo,program,output,business,self.versions[business],self.target,expected_payload)
		shutil.rmtree(publish)
		embedded.unlink()

	def language(self,language):
		prefix=language.capitalize() if language!='csharp' else 'CSharp'
		if language=='cpp':
			prefix='Cpp'
		parts={name:self.work/(language+'-'+name) for name in ('Runtime','Tools','Aot','Symbols')}
		for path in parts.values():
			path.mkdir(parents=True,exist_ok=True)
			self.copy(self.repo/'LICENSE',path/'LICENSE')
		if language=='cpp':
			self.tree('Extensions/Language/Cpp/VeroScriptCpp/Public',parts['Runtime']/'include/Cpp')
			self.copy(self.lib/self.static('VeroScriptCpp'),parts['Runtime']/'lib'/self.static('VeroScriptCpp'))
			self.config('cpp',parts['Runtime'])
			stage_localization(self.repo,parts['Runtime']/'localization','cpp')
			self.install('VeroScriptCppTools',parts['Tools'])
			self.tree('Extensions/Language/Cpp/VeroScriptCppAot/Public',parts['Aot']/'include/CppAot')
			self.copy(self.lib/self.static('VeroScriptCppAot'),parts['Aot']/'lib'/self.static('VeroScriptCppAot'))
		else:
			self.install('VeroScript'+prefix,parts['Runtime'])
			self.install('VeroScript'+prefix+'Tools',parts['Tools'])
			if language=='csharp':
				stage_csharp_packages(self.build,parts['Runtime'],parts['Tools'],self.root/'distributions/csharp',self.versions['CSharp'],self.args.csharp_packages)
				for framework in ('net8.0','net9.0'):
					self.copy(self.interop,parts['Runtime']/'lib/dotnet'/framework/self.shared)
					stage_localization(self.repo,parts['Runtime']/'lib/dotnet'/framework/'localization','interop')
				self.install('VeroScriptCSharpAot',parts['Aot'])
				for tool,project in [('bindgen','VeroScriptCSharpBindGen'),('transpiler','VeroScriptCSharpTranspiler')]:
					destination=parts['Tools']/'bin/dotnet-tools'/('veroscript-csharp-'+tool)
					if destination.exists():
						shutil.rmtree(destination)
					rid=('osx' if self.platform=='macos' else 'win' if self.windows else 'linux')+'-'+self.arch
					run('dotnet','publish',self.repo/'Extensions/Language/CSharp'/project/'Cli'/(project+'.Cli.csproj'),'-c','Release','-f','net9.0','-r',rid,'--self-contained','false','-o',destination)
					self.copy(self.interop,destination/self.shared)
			else:
				self.tree('Extensions/Language/Python/VeroScriptPythonAot/Public',parts['Aot']/'include/PythonAot')
				self.copy(self.lib/self.static('VeroScriptPythonAot'),parts['Aot']/'lib'/self.static('VeroScriptPythonAot'))
		sources=[self.lib/self.static('VeroScript'+prefix+'Aot')]
		if language=='cpp':
			sources.append(self.lib/self.static('VeroScriptCpp'))
		else:
			sources.append(self.interop)
		self.symbols(parts['Symbols'],sources)
		if self.windows:
			for path in (self.build/'Extensions/Language'/prefix).rglob('*.pdb'):
				self.copy(path,parts['Symbols']/'native'/path.relative_to(self.build/'Extensions/Language'/prefix))
			if language=='csharp':
				for path in (self.repo/'Extensions/Language/CSharp').rglob('*.pdb'):
					if 'bin' in path.parts and 'Release' in path.parts:
						self.copy(path,parts['Symbols']/'managed'/path.relative_to(self.repo/'Extensions/Language/CSharp'))
		for name in ('Runtime','Tools','Aot'):
			self.prepare(parts[name],parts['Symbols'])
			self.crt(parts[name])
		portable=self.work/(language+'-portable')
		for name,path in parts.items():
			copy_tree(path,portable/name)
		self.archive('languages','VeroScript'+prefix,portable)
		self.installer(prefix,language,{language+'-runtime':parts['Runtime'],language+'-tools':parts['Tools'],language+'-aot':parts['Aot'],'symbols':parts['Symbols']})
		return parts

	def assemble(self):
		for name in ('veroscript','veroscript-lsp','veroscript-dap','veroscript-runtime','veroscript-launcher'):
			if native_identity(binary_prefix(self.bin/self.exe(name)))!=TARGETS[self.target]:
				raise ValueError('Wrong target binary: '+name)
		runtime=self.work/'runtime'
		for name in ('veroscript','veroscript-lsp','veroscript-dap'):
			self.copy(self.bin/self.exe(name),runtime/self.exe(name))
		self.copy(self.interop,runtime/'lib'/self.shared)
		self.tree('VeroScriptInterop/Public',runtime/'include/VeroScript')
		self.tree('VeroScript/Resources/Scripts/stdlib',runtime/'stdlib')
		stage_localization(self.repo,runtime/'localization')
		stage_runtime_icons(self.repo,runtime,self.target)
		stage_runtime(self.build,runtime)
		self.copy(self.repo/'LICENSE',runtime/'LICENSE')
		self.crt(runtime)
		stage_application_runtime(self.repo,self.bin,runtime,target_context=self.context.context)
		self.prepare(runtime,self.work/'symbols')
		stage_application_runtime(self.repo,runtime,runtime,target_context=self.context.context)
		(runtime/'runtime-manifest.json').write_text(json.dumps({'schemaVersion':1,'product':'VeroScript Runtime','version':self.versions['Runtime'],
			'platform':self.platform,'architecture':self.arch,'includesExamples':False,'packageManager':True,'application':True,'jit':self.jit},indent=2)+'\n')
		stage_package_client(self.repo,runtime/self.exe('veroscript'),runtime,'runtime',target_context=self.context.context)
		self.archive('runtime','VeroScriptRuntime',runtime)
		sdk=self.work/'sdk'
		for module,folder in [('VeroScript','Script'),('VeroScriptTooling','Tooling')]:
			self.tree(module+'/Public',sdk/'include'/folder)
			self.copy(self.lib/self.static(module),sdk/'lib'/self.static(module))
		self.copy(self.build/'VeroScript/Generated/VeroScriptJitBuild.h',sdk/'include/Script/VeroScriptJitBuild.h')
		for name in ('veroscript','veroscript-lsp','veroscript-dap'):
			self.copy(self.bin/self.exe(name),sdk/'bin'/self.exe(name))
		self.tree('VeroScript/Resources/Scripts/stdlib',sdk/'assets/stdlib')
		stage_localization(self.repo,sdk/'assets/localization')
		stage_sdk(self.build,sdk)
		stage_package_manager_sdk(self.build,sdk)
		self.copy(self.repo/'LICENSE',sdk/'LICENSE')
		self.crt(sdk)
		stage_application_runtime(self.repo,self.bin,sdk,'bin',target_context=self.context.context)
		self.prepare(sdk,self.work/'symbols')
		stage_application_runtime(self.repo,sdk/'bin',sdk,'bin',target_context=self.context.context)
		finalize_package_manager_sdk(sdk,self.versions['CoreSDK'])
		self.config('core',sdk)
		(sdk/'sdk-manifest.json').write_text(json.dumps({'schemaVersion':1,'product':'VeroScript Core SDK','version':self.versions['CoreSDK'],'buildId':self.build_id,
			'platform':self.platform,'architecture':self.arch,'packageManager':True,'application':True,'packageRuntime':True,'buildSystem':True,'jit':self.jit},indent=2)+'\n')
		stage_package_client(self.repo,sdk/'bin'/self.exe('veroscript'),sdk,'core-sdk',target_context=self.context.context)
		self.archive('sdk','VeroScriptSDK',sdk)
		interop=self.work/'interop'
		self.install('VeroScriptInterop',interop)
		self.crt(interop)
		self.prepare(interop,self.work/'interop-symbols')
		run(self.cmake,'-DOUTPUT_FILE:FILEPATH='+str(interop/'lib/cmake/VeroScriptInterop/VeroScriptInteropConfigVersion.cmake'),'-DPACKAGE_SEMANTIC_VERSION:STRING='+self.versions['Interop'],'-DPACKAGE_POINTER_SIZE:STRING='+str(self.pointer),'-P',self.repo/'Resources/Tools/CMake/write-cmake-package-version.cmake')
		self.archive('interop','VeroScriptInteropSDK',interop)
		shared=self.args.shared_directory.resolve()
		for path in shared.rglob('*'):
			if path.is_file() and path.name not in {'release-manifest.json','checksums.txt','python-reference.whl','shared-metadata.json'}:
				self.copy(path,self.root/path.relative_to(shared))
		symbols=self.work/'symbols'
		symbols.mkdir(exist_ok=True)
		self.symbols(symbols,[self.lib/self.static(module) for module in ('VeroScript','VeroScriptTooling')]+[self.bin/self.exe(name) for name in ('veroscript','veroscript-lsp','veroscript-dap','veroscript-runtime','veroscript-launcher')])
		if self.windows:
			for path in self.build.rglob('*.pdb'):
				self.copy(path,symbols/'native'/path.relative_to(self.build))
		self.archive('symbols','VeroScriptSymbols',symbols)
		self.installer('Runtime','runtime',{'runtime':runtime})
		self.installer('CoreSDK','core-sdk',{'sdk':sdk,'symbols':symbols})
		self.installer('Interop','interop',{'interop-sdk':interop})
		for language in ('cpp','csharp','python'):
			self.components[language]=self.language(language)
		web=self.work/'web'
		web.mkdir(exist_ok=True)
		with zipfile.ZipFile(next((self.root/'webassembly').glob('*.zip'))) as archive:
			archive.extractall(web)
		web=next(p for p in web.iterdir() if p.is_dir())
		self.installer('WebAssembly','web',{'web-sdk':web})
		jit=self.work/'jit'
		stage_backend(self.repo,self.build,jit,self.target,self.versions['Jit'])
		self.crt(jit)
		self.archive('jit','vs-asmjit',jit)
		pack_jit(self.repo,jit,self.root/'jit'/('vs-asmjit-'+self.versions['Jit']+'-'+self.target+'.vspkg'),self.context,self.target,self.versions['Jit'])
		vm=self.work/'vm'
		stage_vm(self.build,vm,self.context,package_version=self.versions['Vm'])
		self.crt(vm)
		self.archive('vm','vs-bytecode-vm',vm)
		pack_vm(vm,self.root/'vm'/('vs-bytecode-vm-'+self.versions['Vm']+'-'+self.target+'.vspkg'),self.context)
		from veroscript_module_symbols import assemble_module_symbols
		self.module_symbol_status.update(assemble_module_symbols(self.repo,{b:self.build for b in ('Jit','Vm')},
			self.root,self.versions,self.target,['Jit','Vm']))
		self.build_ids=assemble_modules(self.repo,{b:self.build for b in SDK_MODULE_BUSINESSES},self.root,self.versions,self.target,SDK_MODULE_BUSINESSES,core_sdk=sdk,cpp_sdk=self.components['cpp']['Runtime'],package_tool=self.context,symbol_status=self.module_symbol_status)
		self.complete(interop,shared)

	def complete(self,interop,shared):
		self.wheels(interop,shared/'python-reference.whl')
		write_manifest(self.root,self.versions['CoreSDK'],self.target,self.build_id,business_versions=self.versions,native_version=self.native_version,
			build_environment={'configuration':'Release','crossCompiled':True,'targetContract':self.context.contract,'runtimeAcceptance':'not-run','catalogMethod':'source-identical independently matching native declarations; target compiler ABI'})
		file=self.root/'release-manifest.json'
		manifest=json.loads(file.read_text(encoding='utf-8'))
		from veroscript_module_symbols import portable_symbol_status
		manifest.update(selectedBusinesses=list(DESKTOP_BUSINESSES),businessBuildIds=self.build_ids,moduleSymbolStatus=portable_symbol_status(self.module_symbol_status),
			functionalPayloadBusinesses=list(DESKTOP_BUSINESSES),installableModuleBusinesses=['Jit','Vm',*SDK_MODULE_BUSINESSES],
			ideDependencies=json.loads((shared/'shared-metadata.json').read_text(encoding='utf-8'))['ideDependencies'])
		file.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
		verify_release(self.root,self.repo,businesses=set(DESKTOP_BUSINESSES))
		manifest['businessQualifications']={business:{'checks':{
			'build':{'status':'passed','reason':'Target build and candidate package integrity/source audits passed.'},
			'target-runtime':{'status':'not-run','reason':'This cross assembly did not execute the target runtime or installer.'}}}
			for business in DESKTOP_BUSINESSES}
		file.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

	def wheels(self,interop,reference):
		from veroscript_desktop_cross_wheels import build_matrix
		build_matrix(self.repo,self.build,interop,self.args.python_sdk,reference,self.root/'distributions/python',self.work/'python-wheels',self.target,self.versions['Python'],self.args.windows_crt)


def main():
	parser=argparse.ArgumentParser(description=__doc__)
	for name in ('repo-root','build-directory','target-contract','catalogs','host-cli','shared-directory','csharp-packages','output','work-directory'):
		parser.add_argument('--'+name,type=Path,required=True)
	parser.add_argument('--target',choices=tuple(TARGETS),required=True)
	parser.add_argument('--python-sdk',type=Path,action='append',required=True)
	parser.add_argument('--target-tool',action='append',help='Target metadata tool command; repeat for emulator arguments')
	parser.add_argument('--windows-crt',type=Path)
	args=parser.parse_args()
	DesktopCrossRelease(args).assemble()


if __name__=='__main__':
	main()
