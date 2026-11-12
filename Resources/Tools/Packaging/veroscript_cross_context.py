# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Explicit, source-bound metadata for packaging code that cannot run on the host.

Catalogs describe public script declarations. They can be reused only when two
native builds agree and every participating source file still matches. ABI
strings are evaluated by the target compiler, never copied from that reference.
This adapter performs no target module load or runtime acceptance.
"""
import copy
import hashlib
import json
from pathlib import Path
import subprocess

from veroscript_products import INSTALLABLE_PACKAGE_NAMES
from veroscript_release import TARGETS,binary_prefix,core_public_abi,digest,native_identity
from veroscript_target_contract import read_contract


def catalog_source(path):
	parts=path.parts
	return (parts[0] in {'VeroScript','Shared','Modules'} and not {'Tests','ThirdParty','build','bin','obj','.cxx','.gradle','CMakeFiles','Generated'}.intersection(parts)
		and path.suffix.lower() in {'.h','.hpp','.cpp','.c','.inl','.inc'})


def normalized_digest(data):
	return hashlib.sha256(data.replace(b'\r\n',b'\n')).hexdigest()


class CrossPackagingContext:
	def __init__(self,repo,compiled_contract,catalogs,host_cli,work,*,native_jit,target_tool=None):
		self.repo=Path(repo)
		self.contract=read_contract(compiled_contract)
		self.target=self.contract['target']
		self.bundle=json.loads(Path(catalogs).read_text(encoding='utf-8'))
		self.host_cli=Path(host_cli).resolve()
		self.work=Path(work)
		self.work.mkdir(parents=True,exist_ok=True)
		self.native_jit=bool(native_jit)
		if (self.bundle.get('schemaVersion')!=1 or self.bundle.get('publicAbi')!=self.contract['publicAbi']
			or self.bundle.get('coreBuild')!=self.contract['coreBuild']
			or core_public_abi(self.repo/'VeroScript/Public')!=self.contract['publicAbi']):
			raise ValueError('Cross catalogs and compiled Core source contracts differ')
		actual={p.relative_to(self.repo).as_posix():normalized_digest(p.read_bytes())
			for root in ('VeroScript','Shared','Modules') for p in (self.repo/root).rglob('*')
			if p.is_file() and catalog_source(p.relative_to(self.repo))}
		if actual!=self.bundle.get('sourceFiles'):
			raise ValueError('Cross catalog declaration sources differ from the native reference build')
		os_name,arch=TARGETS[self.target]
		self.context={'core':self.bundle['coreVersion'],'coreBuild':self.contract['coreBuild'],'os':os_name,
			'arch':arch,'environment':'desktop','abis':self.contract['abis'],'minimumVersions':{},'capabilities':['native-jit','dynamic-jit']}
		self.target_file=self.work/'target.json'
		self.target_file.write_text(json.dumps(self.context),encoding='utf-8')
		self.target_tool=list(map(str,target_tool)) if target_tool else None
		if self.target_tool:
			actual=json.loads(subprocess.check_output([*self.target_tool,'target'],text=True,encoding='utf-8'))
			if any(actual.get(key)!=self.context[key] for key in ('core','coreBuild','os','arch','environment','abis')):
				raise ValueError('Executed target metadata differs from the compiler-evaluated ABI record')
		elif arch=='x86':
			raise ValueError('A 32-bit target must export its own native catalogs; do not reuse 64-bit declarations')

	def native_abi(self,library,target,public_abi,implementation):
		if (target!=self.target or public_abi!=self.contract['publicAbi'] or implementation!=self.contract['coreBuild']
			or native_identity(binary_prefix(library))!=TARGETS[target]):
			raise ValueError('Cross module differs from its target compiler contract')
		return self.contract['abis']['native-extension']

	def _check_library(self,library):
		if native_identity(binary_prefix(Path(library)))!=TARGETS[self.target]:
			raise ValueError('Cross package library has the wrong target architecture')

	def packaging_output(self,*arguments):
		operation=arguments[0]
		if operation=='target':
			return json.dumps(self.context)
		if self.target_tool:
			value=json.loads(subprocess.check_output([*self.target_tool,*arguments],text=True,encoding='utf-8'))
			if any(value.get('target',{}).get(key)!=self.context[key] for key in ('core','coreBuild','os','arch','environment','abis')):
				raise ValueError('Loaded module target differs from its compiler-evaluated ABI record')
			value['target']=self.context
			return json.dumps(value)
		if operation in {'describe-jit','describe-vm'}:
			self._check_library(arguments[1])
			return json.dumps({'target':self.context,'id':'asmjit' if operation=='describe-jit' else 'vs-bytecode',
				'version':self.bundle['coreVersion'],'nativeCode':self.native_jit})
		if operation!='describe' or len(arguments) not in {5,6}:
			raise ValueError('Unsupported cross metadata request')
		library,package,sha,catalog=arguments[1:5]
		self._check_library(library)
		if digest(Path(library))!=sha:
			raise ValueError('Cross catalog library hash mismatch')
		business=next((b for b,name in INSTALLABLE_PACKAGE_NAMES.items() if name==package),None)
		reference=self.bundle['catalogs'].get(business)
		if not reference or len(set(reference.get('referenceTargets',[])))<2:
			raise ValueError('No independently matching native declaration catalogs: '+str(business))
		value=copy.deepcopy(reference['catalog'])
		provider='native.'+hashlib.sha256(json.dumps({'package':package,'module':reference['id'],'sha256':sha},
			sort_keys=True,separators=(',',':')).encode()).hexdigest()

		def replace_origin(item):
			if isinstance(item,dict):
				if 'origin' in item:
					origin=item['origin']
					if origin.get('providerId')!=reference['providerId']:
						raise ValueError('Cross catalog contains an unexpected provider origin')
					origin['providerId']=provider
				for child in item.values():
					replace_origin(child)
			elif isinstance(item,list):
				for child in item:
					replace_origin(child)
		replace_origin(value)
		Path(catalog).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
		return json.dumps({'target':self.context,'id':reference['id'],'componentVersion':reference['version'],
			'roles':reference['roles'],'backends':reference['backends'],'abi':self.contract['abis']['native-extension'],
			'coreBuild':self.contract['coreBuild']})

	def packaging_run(self,*arguments):
		if arguments[:2]!=('command','pack'):
			raise ValueError('Cross packaging adapter only supports the public pack operation')
		return subprocess.run([str(self.host_cli),*arguments[1:],'--target='+str(self.target_file),'--path='+str(self.work)],check=True)
