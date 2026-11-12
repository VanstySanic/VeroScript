# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise the real CLI, curl redirect policy and portable local deployment."""
import argparse
import hashlib
import http.server
import io
import json
import os
import posixpath
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import urllib.parse
import zipfile


def requirements_tests(run, root, local, component_cli):
	area = root / '依赖列表 with spaces'
	lists = area / 'lists'
	lists.mkdir(parents=True)
	source = area / 'vendor @ source # 中文'
	shutil.copytree(local / 'library', source)
	archive = area / 'local archive.VsPkG'
	run('pack', source, f'--output={archive}')
	(lists / 'nested').mkdir()
	(lists / 'nested' / 'extra.txt').write_text(
		'example-math @ "../../local archive.VsPkG" # nested archive\n', encoding='utf-8')
	main = lists / '需求清单.txt'
	main.write_bytes(('\ufeff# UTF-8 with BOM and CRLF\r\n\r\n'
					  '-r "nested/extra.txt"\r\n').encode('utf-8'))
	included = lists / 'nested' / 'with # comment.txt'
	included.write_text('example-math @ "../../local archive.VsPkG"\n', encoding='utf-8')
	(lists / 'options.txt').write_text(
		'--requirement="nested/with # comment.txt" # include\n'
		"--requirement 'nested/with # comment.txt'\n"
		'-r"nested/with # comment.txt"\n', encoding='utf-8')
	project = area / 'selected project'
	project.mkdir()
	for index, options in enumerate((('-r', 'lists/需求清单.txt'),
									 ('-rlists/需求清单.txt',),
									 ('--requirement', main),
									 (f'--requirement={main}',))):
		run('install', *options, f'--path={project}', '--offline', cwd=area)
		manifest = json.loads((project / 'veroscript-project.json').read_text(encoding='utf-8'))
		dependency = manifest['dependencies']['example-math']
		assert dependency['archive'] == '../local archive.VsPkG'
		assert dependency['sha256'] == hashlib.sha256(archive.read_bytes()).hexdigest()
		if index == 0:
			saved_lock = (project / 'veroscript.lock').read_bytes()
		else:
			assert (project / 'veroscript.lock').read_bytes() == saved_lock
	run('install', '-r', main, '-r', main, f'--path={project}', '--locked', '--offline')
	run('install', '-r', lists / 'options.txt', f'--path={project}', '--locked', '--offline')
	assert (project / 'veroscript.lock').read_bytes() == saved_lock
	run('verify', f'--path={project}')
	restored = area / 'restored'
	restored.mkdir()
	for name in ('veroscript-project.json', 'veroscript.lock'):
		shutil.copy2(project / name, restored / name)
	run('install', '-r', main, f'--path={restored}', '--locked', '--offline')
	directory_list = lists / 'directory.txt'
	directory_list.write_text('"../vendor @ source # 中文" # literal symbols\n', encoding='utf-8')
	directory_project = area / 'directory-app'
	directory_project.mkdir()
	run('install', '--requirement', directory_list, f'--path={directory_project}', '--offline')
	directory_spec = json.loads((directory_project / 'veroscript-project.json').read_text(encoding='utf-8'))
	assert directory_spec['dependencies']['example-math']['directory'] == '../vendor @ source # 中文'
	directory_list.write_text(f'example-math @ "{source}"\n', encoding='utf-8')
	run('install', '-r', directory_list, f'--path={directory_project}', '--locked', '--offline')
	# A requirements file selects inputs; --root still selects only the destination.
	component_list = lists / 'component.txt'
	component_list.write_text('"../vendor @ source # 中文"\n', encoding='utf-8')
	run('install', '-r', component_list, '--root', '--offline', executable=component_cli)
	assert (component_cli.parent / 'Packages').is_dir()
	invalid = area / 'invalid'
	invalid.mkdir()
	for options in (('-r',), ('--requirement',), ('--requirement=',),
					('-r', '--root'), ('--requirement', '-r'), ('-r', 'missing.txt')):
		run('install', *options, f'--path={invalid}', '--offline', ok=False)
	for command in ('env', 'list', 'pack', 'publish', 'uninstall'):
		run(command, '-r', main, f'--path={invalid}', ok=False)
		run(command, '--requirement', main, f'--path={invalid}', ok=False)
	for index, contents in enumerate(('# comment\n--root\n', '-r loop.txt\n',
									  '-r "missing file.txt"\n', 'example-math==\n',
									  'example-math>=0.1.1,\n',
									  'example-math \\\n', '"missing quote\n')):
		bad = lists / 'loop.txt'
		bad.write_text(contents, encoding='utf-8')
		failed = run('install', '-r', bad, '--json', f'--path={invalid}', '--offline', ok=False)
		diagnostic = json.loads(failed.stderr)['error']
		assert diagnostic['source'] and 'loop.txt' in diagnostic['field'], diagnostic
	assert not (invalid / '.veroscript').exists()
	assert not (invalid / 'veroscript-project.json').exists()
	# The source manifest identity is checked for named direct references.
	bad.write_text('wrong-name @ "../local archive.VsPkG"\n', encoding='utf-8')
	failed = run('install', '-r', bad, '--json', f'--path={project}', '--offline', ok=False)
	assert json.loads(failed.stderr)['error']['code'] == 'package-identity'
	assert (project / 'veroscript.lock').read_bytes() == saved_lock
	empty = lists / 'empty.txt'
	empty.write_text('# no dependencies\n', encoding='utf-8')
	empty_project = area / 'empty project'
	empty_project.mkdir()
	run('install', '-r', empty, f'--path={empty_project}', '--offline')
	assert not json.loads((empty_project / 'veroscript.lock').read_text(encoding='utf-8'))['packages']


def manifest_name_tests(run, root):
	source = root / '项目 package with spaces'
	(source / 'scripts').mkdir(parents=True)
	(source / 'scripts/library.vs').write_text('export int value(){return 17;}', encoding='utf-8')
	distribution = {'entries': {'modules': {'generated/library.vs': 'scripts/library.vs'}},
					'files': ['veroscript-package.json', 'scripts/**']}
	project = {'format': 'veroscript-project', 'formatVersion': '0.1.1', 'name': 'generated-package',
			   'version': '0.1.1', 'packages': {'development-tool': {'enabled': False}},
			   'packageManager': {'distribution': distribution}}
	config = source / 'veroscript-project.json'
	config.write_text(json.dumps(project), encoding='utf-8')
	for index in range(2):
		if index:
			(source / 'distribution.json').write_text(json.dumps(distribution), encoding='utf-8')
			project['packageManager']['distribution'] = 'distribution.json'
			config.write_text(json.dumps(project), encoding='utf-8')
		output = root / f'generated-project-{index}.vspkg'
		run('pack', source, f'--output={output}')
		with zipfile.ZipFile(output) as archive:
			names = archive.namelist()
			manifest = json.loads(archive.read('veroscript-package.json'))
			assert 'veroscript-project.json' not in names and 'distribution.json' not in names
			assert manifest['format'] == 'veroscript-package' and manifest['name'] == 'generated-package'
			assert 'packages' not in manifest and 'packageManager' not in manifest
		consumer = root / f'generated-consumer-{index}'
		consumer.mkdir()
		run('install', output, f'--path={consumer}', '--offline')
		assert (consumer / 'veroscript-project.json').is_file()
		assert not (consumer / 'veroscript.json').exists()
		saved = json.loads((consumer / 'veroscript-project.json').read_text(encoding='utf-8'))
		assert 'generated-package' in saved['dependencies']
	# Project configuration is excluded even without a distribution file whitelist.
	project['packageManager']['distribution'] = {'entries': distribution['entries']}
	config.write_text(json.dumps(project), encoding='utf-8')
	for index, name in enumerate(('veroscript-project.json', 'VEROSCRIPT-PROJECT.JSON') if os.name == 'nt'
								 else ('veroscript-project.json',)):
		if config.name != name:
			config = config.rename(source / name)
		output = root / f'unfiltered-project-{index}.vspkg'
		run('pack', source, f'--output={output}')
		with zipfile.ZipFile(output) as archive:
			assert not any(path.casefold() == 'veroscript-project.json' for path in archive.namelist()), archive.namelist()
			assert json.loads(archive.read('veroscript-package.json'))['name'] == 'generated-package'
	if config.name != 'veroscript-project.json':
		config = config.rename(source / 'veroscript-project.json')
	package = {'format': 'veroscript-package', 'formatVersion': '0.1.1', 'name': 'explicit-package',
			   'version': '0.1.1', **distribution}
	(source / 'veroscript-package.json').write_text(json.dumps(package), encoding='utf-8')
	explicit = root / 'explicit-package.vspkg'
	run('pack', source, f'--output={explicit}')
	with zipfile.ZipFile(explicit) as archive:
		assert json.loads(archive.read('veroscript-package.json'))['name'] == 'explicit-package'
	legacy = root / 'old-manifest-only'
	legacy.mkdir()
	(legacy / 'veroscript.json').write_text(json.dumps(package), encoding='utf-8')
	(legacy / 'scripts').mkdir()
	(legacy / 'scripts/library.vs').write_text('export int value(){return 17;}', encoding='utf-8')
	run('pack', legacy, f'--output={root / "rejected-legacy.vspkg"}', ok=False)
	consumer = root / 'legacy-rejected'
	consumer.mkdir()
	run('install', legacy, f'--path={consumer}', '--offline', ok=False)
	old_archive = root / 'old-manifest.vspkg'
	with zipfile.ZipFile(old_archive, 'w') as archive:
		archive.writestr('veroscript.json', json.dumps(package))
		archive.writestr('scripts/library.vs', 'export int value(){return 17;}')
	run('install', old_archive, f'--path={consumer}', '--offline', ok=False)
	assert not (consumer / 'veroscript-project.json').exists()
	(legacy / 'veroscript.json').write_text('{invalid old project config', encoding='utf-8')
	assert '17' in run('-e', 'print(17);', cwd=legacy).stdout
	wrong_role = root / 'wrong-manifest-role'
	wrong_role.mkdir()
	(wrong_role / 'veroscript-package.json').write_text(json.dumps(project), encoding='utf-8')
	run('pack', wrong_role, f'--output={root / "rejected-role.vspkg"}', ok=False)


def archive_tests(run,root,local):
	area=root/'归档 with spaces'
	area.mkdir()
	selection=f'--path={local / "app"}'
	packed=json.loads(run('pack',local/'library',selection).stdout)
	default=Path(packed['path'])
	assert default.name=='example-math-0.1.1.vspkg' and zipfile.is_zipfile(default)
	original=default.read_bytes()
	digest=hashlib.sha256(original).hexdigest()
	assert packed['sha256']==digest
	for index,suffix in enumerate(('.zip','.ZIP','.vspkg','.VsPkG')):
		output=area/f'packed-{index}{suffix}'
		result=json.loads(run('pack',local/'library',selection,f'--output={output}').stdout)
		assert zipfile.is_zipfile(output) and output.read_bytes()==original and result['sha256']==digest
	archive=area/'界面 package.VsPkG'
	archive.write_bytes(original)
	legacy=area/'gui.ZIP'
	legacy.write_bytes(original)
	cases=[(archive.name,area),('./'+archive.name,area),(archive,root),
		   (legacy.name,area),('./gui.ZIP',area),(legacy,root)]
	for index,(argument,cwd) in enumerate(cases):
		project=area/f'project-{index}'
		project.mkdir()
		run('install',argument,f'--path={project}','--offline',cwd=cwd)
		view=json.loads(run('list',f'--path={project}','--json').stdout)
		package=view['packages'][0]
		assert package['name']=='example-math' and package['sha256']==digest
		assert (Path(package['path'])/'scripts/math.vs').read_bytes()==(local/'library/scripts/math.vs').read_bytes()
		requirement=json.loads((project/'veroscript-project.json').read_text(encoding='utf-8'))['dependencies']['example-math']
		assert requirement['archive'].endswith(archive.name if index<3 else legacy.name)
		run('install',f'--path={project}','--locked','--offline')
	for suffix in ('.zip','.vspkg'):
		dependency=area/f'dependency{suffix}'
		dependency.write_bytes(original)
		project=area/f'dependency-{suffix[1:]}'
		project.mkdir()
		settings={'format':'veroscript-project','formatVersion':'0.1.1','name':'archive-consumer',
				  'version':'0.1.1','dependencies':{'example-math':{'version':'0.1.1','archive':'../'+dependency.name}}}
		(project/'veroscript-project.json').write_text(json.dumps(settings),encoding='utf-8')
		run('install',f'--path={project}','--offline')
		lock=json.loads((project/'veroscript.lock').read_text(encoding='utf-8'))
		assert lock['packages'][0]['source']['location'].endswith(dependency.name)
		assert lock['packages'][0]['artifact']['sha256']==digest
		restored=area/f'restored-{suffix[1:]}'
		restored.mkdir()
		for name in ('veroscript-project.json','veroscript.lock'):
			shutil.copy2(project/name,restored/name)
		run('install',f'--path={restored}','--locked','--offline')
	broken=area/'broken.vspkg'
	broken.write_bytes(b'not a ZIP archive')
	missing=area/'missing.ZIP'
	invalid=area/'invalid.VSPKG'
	for file,manifest in [(missing,None),(invalid,{'format':'wrong','formatVersion':'0.1.1'})]:
		with zipfile.ZipFile(file,'w') as z:
			if manifest is not None:
				z.writestr('veroscript-package.json',json.dumps(manifest))
			z.writestr('docs/readme.txt','not an installable package')
	for index,file in enumerate((broken,missing,invalid)):
		project=area/f'rejected-{index}'
		project.mkdir()
		result=run('install',file.name,f'--path={project}','--offline','--json',cwd=area,ok=False)
		assert json.loads(result.stderr)['error']['code']!='package-name'
		assert not (project/'veroscript-project.json').exists() and not (project/'.veroscript/state.json').exists()
		run('publish',file,'--registry-api=https://publish.invalid/api',cwd=area,ok=False)
	repetitive=area/'repetitive-source'
	shutil.copytree(local/'library',repetitive)
	(repetitive/'repeated.txt').write_text('x'*500000,encoding='utf-8')
	for suffix in ('.vspkg','.zip'):
		packed=area/('repetitive'+suffix)
		run('pack',repetitive,selection,f'--output={packed}')
		project=area/('repetitive-'+suffix[1:])
		project.mkdir()
		run('install',packed,f'--path={project}','--offline')
		package=json.loads(run('list',f'--path={project}','--json').stdout)['packages'][0]
		assert (Path(package['path'])/'repeated.txt').read_bytes()==b'x'*500000


def publication_tests(run,root):
	area=root/'publication'
	area.mkdir()
	state={'version':None,'uploads':{},'commits':{},'lose_response':True,'error_response':False,'reads':[]}
	class Server(http.server.BaseHTTPRequestHandler):
		def log_message(self,*unused):
			pass
		def reply(self,status,data):
			body=json.dumps(data).encode()
			self.send_response(status)
			self.send_header('Content-Type','application/json')
			self.send_header('Content-Length',str(len(body)))
			self.end_headers()
			self.wfile.write(body)
		def read(self):
			return self.rfile.read(int(self.headers['Content-Length']))
		def do_GET(self):
			state['reads'].append((self.path,self.headers.get('Cache-Control')))
			if self.path!='/api/packages/cli-platform/versions/0.1.1' or state['version'] is None:
				self.reply(404,{'code':'not-found','message':'version absent'})
				return
			self.reply(200,state['version'])
		def do_PUT(self):
			upload=state['uploads'][self.path.rsplit('/',1)[-1]]
			data=self.read()
			assert len(data)==upload['size'] and hashlib.sha256(data).hexdigest()==upload['sha256']
			with zipfile.ZipFile(io.BytesIO(data)) as archive:
				raw=archive.read('veroscript-package.json')
				assert hashlib.sha256(raw).hexdigest()==upload['manifestSha256']
				manifest=json.loads(raw)
				assert manifest.get('target',{})==upload['target']
			upload['stored']=True
			self.reply(200,{})
		def do_POST(self):
			body=json.loads(self.read())
			if self.path=='/api/uploads':
				identifier='upload-'+str(len(state['uploads'])+1)
				state['uploads'][identifier]=body
				self.reply(200,{'format':'veroscript-upload','formatVersion':'0.1.1',
					'sourceId':'veroscript.official','uploadId':identifier,'method':'PUT',
					'url':f'http://127.0.0.1:{self.server.server_port}/objects/{identifier}'})
				return
			assert self.path=='/api/packages/cli-platform/versions' and body['mode']=='append'
			key=body['idempotencyKey']
			if key in state['commits']:
				assert state['commits'][key]==body
			else:
				fields=('sourceId','name','version','dependencies','conflicts')
				version=state['version']
				if version is None:
					version={'format':'veroscript-registry-version','formatVersion':'0.1.1',
							 **{field:body[field] for field in fields},'artifacts':[]}
				assert all(version[field]==body[field] for field in fields)
				additions=[]
				for ref in body['artifacts']:
					assert state['uploads'][ref['uploadId']]['stored']
					artifact={field:ref[field] for field in ('sha256','size','manifestSha256','target','requires')}
					for prior in version['artifacts']+additions:
						assert prior['target']['os']!=artifact['target']['os']
					artifact['id']='artifact-'+ref['sha256'][:24]
					additions.append(artifact)
				state['version']={**version,'artifacts':version['artifacts']+additions}
				state['commits'][key]=body
			if state['lose_response']:
				state['lose_response']=False
				self.close_connection=True
				return
			if state['error_response']:
				state['error_response']=False
				self.reply(502,{'code':502,'message':False,'requestId':{}})
				return
			self.reply(200,state['version'])
	server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
	thread=threading.Thread(target=server.serve_forever,daemon=True)
	thread.start()
	try:
		archives=[]
		for label,os_name in (('windows','windows'),('linux','linux'),('replacement','windows')):
			source=area/label
			source.mkdir()
			(source/'veroscript-package.json').write_text(json.dumps({'format':'veroscript-package',
				'formatVersion':'0.1.1','name':'cli-platform','version':'0.1.1','target':{'os':os_name}}),encoding='utf-8')
			(source/'payload.txt').write_text(label,encoding='utf-8')
			archive=area/(label+'.vspkg')
			run('pack',source,f'--output={archive}',cwd=area)
			archives.append(archive)
		options=[f'--registry-api=http://127.0.0.1:{server.server_port}/api',
				 f'--path={area}',f'--cache-dir={area / "cache"}','--allow-http']
		run('publish',archives[0],*options)
		original=dict(state['version']['artifacts'][0])
		state['error_response']=True
		run('publish',archives[1],*options)
		assert len(state['version']['artifacts'])==2 and state['version']['artifacts'][0]==original
		run('publish',*archives[:2],*options)
		result=run('publish',archives[2],*options,'--json',ok=False)
		assert json.loads(result.stderr)['error']['code']=='publish-conflict'
		assert len(state['uploads'])==2 and len(state['commits'])==2
		assert state['reads'] and all(scope=='no-cache' for path,scope in state['reads'])
	finally:
		server.shutdown()
		server.server_close()
		thread.join(timeout=5)


def main():
	parser=argparse.ArgumentParser()
	parser.add_argument('--cli',type=Path,required=True)
	parser.add_argument('--examples',type=Path,required=True)
	args=parser.parse_args()
	cli=args.cli.resolve()
	with tempfile.TemporaryDirectory(prefix='vero-package-cli-') as temp:
		root=Path(temp)
		env=os.environ.copy()
		env['NO_PROXY']='127.0.0.1,localhost'
		env['VERO_PM_TEST_TOKEN']='Bearer package-fixture-secret'
		def run(*argv,ok=True,cwd=None,executable=None,timeout=40):
			p=subprocess.run([str(executable or cli),*map(str,argv)],cwd=cwd or root,env=env,capture_output=True,text=True,encoding='utf-8',timeout=timeout)
			if ok and p.returncode:
				raise AssertionError(f'{argv}: exit {p.returncode}: {p.stdout}\n{p.stderr}')
			if not ok and not p.returncode:
				raise AssertionError(f'{argv}: expected failure')
			return p
		local=root/'Local'
		shutil.copytree(args.examples/'Local',local)
		project=f'--path={local / "app"}'
		invalid=root/'invalid-selectors'
		invalid.mkdir()
		for opts in [('--project',),('--user',),('--project='+str(invalid),),('--path',),('--path=',),
					 ('--path','--root'),('--root=bad',),('--root','--root'),('--path',str(invalid),'--path='+str(invalid))]:
			run('install',local/'library',*opts,cwd=invalid,ok=False)
		assert not (invalid/'veroscript-project.json').exists() and not (invalid/'.veroscript').exists()
		run('env','--root',ok=False)
		run('env','--root','--path',str(invalid),ok=False)
		run('install',project,'--offline')
		run('install','-r',local/'requirements.txt',project,'--locked','--offline')
		p=run(project,local/'app/main.vs')
		assert '42' in p.stdout
		run('--check',project,local/'app/main.vs')
		catalog=root/'bindings.json'
		catalog.write_text(json.dumps({'version':1,'functions':[]}),encoding='utf-8')
		run('--check','--binding-catalog',catalog,'main.vs',cwd=local/'app')
		assert '42' in run('--binding-catalog',catalog,'main.vs',cwd=local/'app').stdout
		run('aot','--target=csharp','--binding-catalog',catalog,'main.vs','-o',root/'catalog.cs',cwd=local/'app')
		assert '42' in run('-e','import "example/math.vs";print("{}",answer());',project).stdout
		for target, extension in [('cpp','cpp'),('csharp','cs'),('python','py')]:
			run('aot',project,f'--target={target}',local/'app/main.vs','-o',root/f'package-aot.{extension}')
		run('--debug',project,ok=False)
		repl=subprocess.run([str(cli),'-i',project],input='import "example/math.vs";\nprint("{}", answer());\n:q\n',cwd=root,env=env,capture_output=True,text=True,encoding='utf-8',timeout=30)
		assert repl.returncode==0 and '42' in repl.stdout, (repl.stdout,repl.stderr)

		compiler=root/'Compiler'
		shutil.copytree(args.examples/'Compiler',compiler)
		compiler_app=compiler/'app'
		stdlib=args.examples.parents[2]/'VeroScript/Resources/Scripts/stdlib'
		run('install','--offline',cwd=compiler_app)
		# This is a cold end-to-end extension test, also run against GUI-enabled
		# hosts that retain the full implicit GUI library for user generators.
		# Its ordinary compilation is outside the extension VM deadline.
		# Deadline behavior has dedicated Core tests; ordinary commands keep 40s.
		compiler_options = ('--stdlib-path', stdlib, '--compiler-timeout', '15000')
		run(*compiler_options,'--check','main.vs',cwd=compiler_app,timeout=120)
		assert '42' in run(*compiler_options,'main.vs',cwd=compiler_app,timeout=120).stdout
		for target,extension in [('cpp','cpp'),('csharp','cs'),('python','py')]:
			run('aot',*compiler_options,f'--target={target}','main.vs','-o',root/f'generated.{extension}',cwd=compiler_app,timeout=120)
			original=(compiler_app/'main.vs').read_bytes()
			run('aot',*compiler_options,f'--target={target}','main.vs','-o','main.vs',cwd=compiler_app,ok=False,timeout=120)
			assert (compiler_app/'main.vs').read_bytes()==original
		artifact=root/'generated.vsapp'
		run(*compiler_options,'--emit-package',artifact,'main.vs',cwd=compiler_app,timeout=120)
		assert '42' in run(artifact).stdout
		compiler_deployment=root/'compiler-deployment'
		run('deploy',f'--output={compiler_deployment}',cwd=compiler_app)
		assert not json.loads((compiler_deployment/'veroscript-deployment.json').read_text(encoding='utf-8'))['packages']

		run('pack',local/'library',project,f'--output={root / "library.zip"}')
		manifest_name_tests(run, root)
		archive_tests(run,root,local)
		run('deploy',project,f'--output={root / "deployment"}')
		assert (root/'deployment/veroscript-deployment.json').is_file()
		lock=(local/'app/veroscript.lock').read_bytes()
		run('disable','example-math',project)
		assert (local/'app/veroscript.lock').read_bytes()==lock
		run('enable','example-math',project)
		run(project,local/'app/main.vs')
		run('uninstall','example-math',project,'--skip-hooks')
		run('clean',project)
		hooks=root/'hook-project'
		hooks.mkdir()
		run('install',args.examples/'Hooks',f'--path={hooks}','--allow-hooks','--input=title="Example"')
		generated=hooks/'.veroscript/generated'
		assert any(p.read_text(encoding='utf-8').strip()=='Example' for p in generated.rglob('title.txt'))
		assert any(p.read_bytes()==(args.examples/'Hooks/data/source.txt').read_bytes() for p in generated.rglob('copied.txt'))
		# The CLI build and system temp directory may be on different Windows
		# drives. This also exercises external stores/lockfiles on other hosts.
		with tempfile.TemporaryDirectory(prefix='vero-pm-store-',dir=cli.parent) as remote:
			external=Path(remote)
			separate=root/'external-project'
			separate.mkdir()
			selected=[f'--path={separate}',f'--packages-dir={external / "packages"}',f'--lockfile={external / "target.lock"}']
			run('install',local/'library',*selected,'--offline')
			actual=json.loads(run('env',f'--path={separate}','--json').stdout)
			assert Path(actual['packagesDirectory']).samefile(external/'packages')
			run('verify',*selected)
			run('uninstall','example-math',*selected,'--skip-hooks')
			run('clean',*selected)
		# A shipped CLI defaults to the startup project, and selects its
		# component only with --root. Different script results prove isolation.
		component=root/'runtime-component'
		component.mkdir()
		component_cli=component/cli.name
		shutil.copy2(cli,component_cli)
		shutil.copytree(cli.parent/'Vm', component/'Vm')
		target=json.loads(run('env','--json').stdout)['target']
		descriptor={'format':'veroscript-component','formatVersion':'0.1.1','role':'runtime','root':'.','target':target}
		(component/'veroscript-component.json').write_text(json.dumps(descriptor),encoding='utf-8')
		default_project=root/'default-project'
		default_project.mkdir()
		run('install',local/'library','--offline',cwd=default_project,executable=component_cli)
		assert (default_project/'.veroscript/packages').is_dir() and not (component/'.veroscript').exists()
		entry=default_project/'main.vs'
		entry.write_text('import "example/math.vs";void main(){print("{}",answer());}',encoding='utf-8')
		assert '42' in run(entry,cwd=default_project,executable=component_cli).stdout
		requirements_tests(run, root, local, component_cli)
		assert '42' in run('-e','import "example/math.vs";print("{}",answer());',cwd=default_project,executable=component_cli).stdout
		shared_library=root/'shared-library'
		shutil.copytree(local/'library',shared_library)
		(shared_library/'scripts/math.vs').write_text('export int answer(){return 84;}',encoding='utf-8')
		run('install',shared_library,'--root','--offline',cwd=default_project,executable=component_cli)
		assert (component/'Packages').is_dir()
		assert '84' in run('--root',entry,cwd=default_project,executable=component_cli).stdout
		assert '42' in run(entry,cwd=default_project,executable=component_cli).stdout
		# Interactive discovery starts at cwd, regardless of similarly named
		# files or links. The old synthetic entry could select this component.
		linked_directory=component/'nested'
		linked_directory.mkdir()
		synthetic_entry=default_project/'.veroscript-entry.vs'
		if os.name=='nt':
			quoted=lambda p: "'"+str(p).replace("'", "''")+"'"
			subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-Command',
				f'New-Item -ItemType Junction -Path {quoted(synthetic_entry)} -Target {quoted(linked_directory)} | Out-Null'],
				check=True,capture_output=True,text=True)
		else:
			synthetic_entry.symlink_to(linked_directory,target_is_directory=True)
		try:
			assert '42' in run('-e','import "example/math.vs";print("{}",answer());',cwd=default_project,executable=component_cli).stdout
			linked_repl=subprocess.run([str(component_cli),'-i'],input='import "example/math.vs";\nprint("{}",answer());\n:q\n',
				cwd=default_project,env=env,capture_output=True,text=True,encoding='utf-8',timeout=30)
			assert linked_repl.returncode==0 and '42' in linked_repl.stdout,(linked_repl.stdout,linked_repl.stderr)
		finally:
			if os.name=='nt':
				synthetic_entry.rmdir()
			else:
				synthetic_entry.unlink()
		assert '84' in run('--root','--path','../runtime-component',entry,cwd=default_project).stdout
		for target_name, extension in [('cpp','cpp'),('csharp','cs'),('python','py')]:
			run('aot','--root','--path',str(component),f'--target={target_name}',entry,'-o',root/f'root-aot.{extension}')
		selected=json.loads(run('env','--path','Local/app','--json').stdout)
		assert selected['scope']=='project' and Path(selected['root']).samefile(local/'app')
		explicit_repl=subprocess.run([str(cli),'-i','--path',str(default_project)],input='import "example/math.vs";\nprint("{}",answer());\n:q\n',
			cwd=root,env=env,capture_output=True,text=True,encoding='utf-8',timeout=30)
		assert explicit_repl.returncode==0 and '42' in explicit_repl.stdout,(explicit_repl.stdout,explicit_repl.stderr)
		(component/'veroscript-component.json').write_text('{invalid',encoding='utf-8')
		assert json.loads(run('env','--json',executable=component_cli).stdout)['scope']=='project'
		run('env','--root',executable=component_cli,ok=False)
		(component/'veroscript-component.json').write_text(json.dumps(descriptor),encoding='utf-8')
		# Unselected legacy user state is not part of project or component input.
		env['XDG_DATA_HOME']=str(root/'old-user-data')
		target_key=hashlib.sha256(json.dumps(target,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()).hexdigest()
		old=root/'old-user-data/VeroScript/.veroscript/environments'/target_key
		old.mkdir(parents=True)
		(old/'state.json').write_text('{invalid',encoding='utf-8')
		isolated=root/'isolated'
		isolated.mkdir()
		assert '84' in run('-e','import "example/math.vs";print("{}",answer());',cwd=isolated,executable=component_cli).stdout
		manifest={'format':'veroscript-package','formatVersion':'0.1.1','name':'network-example','version':'0.1.1','entries':{'modules':{'network/example.vs':'api.vs'}}}
		raw=json.dumps(manifest,sort_keys=True,indent=2).encode()
		stream=io.BytesIO()
		with zipfile.ZipFile(stream,'w',compression=zipfile.ZIP_STORED) as z:
			z.writestr('veroscript-package.json',raw)
			z.writestr('api.vs','export int network_answer(){return 54;}')
		archive=stream.getvalue()
		digest=hashlib.sha256(archive).hexdigest()
		requests=[]
		conditional_headers=[]
		class Server(http.server.BaseHTTPRequestHandler):
			def log_message(self,*unused):
				pass
			def do_GET(self):
				requests.append((self.path,self.headers.get('Authorization')))
				route=self.path
				if route=='/api/search?q=header-ows':
					conditional_headers.append(self.headers.get('If-None-Match'))
					if conditional_headers[-1]=='"ows-fixture"':
						self.send_response(304)
						self.end_headers()
						return
				if posixpath.normpath(urllib.parse.unquote(route.partition('?')[0]).replace('\\','/')) in ('/direct/gui.VSPKG','/direct/界面.vspkg'):
					route='/direct/gui.VSPKG'
				if route=='/scoped/packages/network-example':
					self.send_response(302)
					self.send_header('Location','/scoped/packages/outside')
					self.end_headers()
					return
				if route=='/scoped/packages/outside':
					route='/api/packages/network-example'
				elif route.startswith('/scoped/'):
					route='/api/'+route[len('/scoped/'):]
				if route=='/api/packages/network-example':
					data={'format':'veroscript-registry-package','formatVersion':'0.1.1','sourceId':'veroscript.official','name':'network-example','versions':[{'version':'0.1.1'}]}
				elif route.startswith('/api/search?'):
					data={'sourceId':'veroscript.official','packages':[]}
				elif route=='/api/packages/network-example/versions/0.1.1':
					data={'format':'veroscript-registry-version','formatVersion':'0.1.1','sourceId':'veroscript.official','name':'network-example','version':'0.1.1','artifacts':[{'id':'network-0.1.1','size':len(archive),'sha256':digest,'manifestSha256':hashlib.sha256(raw).hexdigest(),'target':{},'requires':{}}]}
				elif route=='/api/artifacts/network-0.1.1/download':
					self.send_response(302)
					self.send_header('Location',f'http://localhost:{self.server.server_port}/cdn/archive')
					self.end_headers()
					return
				elif route in ('/cdn/archive','/direct/gui.VSPKG','/direct/artifact?download=1'):
					self.send_response(200)
					self.send_header('Content-Length',str(len(archive)))
					self.end_headers()
					self.wfile.write(archive)
					return
				else:
					self.send_response(404)
					self.end_headers()
					return
				tag=' '*60000+'\t"ows-fixture" \t' if route=='/api/search?q=header-ows' else '"fixture"'
				body=json.dumps(data).encode()
				self.send_response(200)
				self.send_header('Content-Length',str(len(body)))
				self.send_header('ETag',tag)
				self.end_headers()
				self.wfile.write(body)
		server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
		thread=threading.Thread(target=server.serve_forever,daemon=True)
		thread.start()
		try:
			network=root/'network'
			network.mkdir()
			api=f'http://127.0.0.1:{server.server_port}/api'
			opts=[f'--path={network}',f'--cache-dir={root / "network-cache"}',f'--registry-api={api}']
			run('install','network-example',*opts,ok=False)
			run('install','network-example',*opts,'--allow-http','--credential-env=VERO_PM_TEST_TOKEN',f'--credential-scope={api}')
			assert requests and all(auth==env['VERO_PM_TEST_TOKEN'] for path,auth in requests if path.startswith('/api/'))
			assert any(path=='/cdn/archive' and auth is None for path,auth in requests)
			ows_project=root/'header-ows'
			ows_project.mkdir()
			ows_cache=root/'header-ows-cache'
			ows_options=[f'--path={ows_project}',f'--cache-dir={ows_cache}',f'--registry-api={api}','--allow-http']
			run('search','header-ows',*ows_options)
			saved_headers=list((ows_cache/'metadata').rglob('*.json'))
			assert len(saved_headers)==1 and json.loads(saved_headers[0].read_text(encoding='utf-8'))['etag']=='"ows-fixture"'
			run('search','header-ows',*ows_options)
			assert conditional_headers==[None,'"ows-fixture"'], 'HTTP OWS must be trimmed before caching or sending validators'
			official_api='https://veroscript.org/api'
			assert official_api in run('install','--help').stdout
			assert json.loads((network/'veroscript.lock').read_text(encoding='utf-8'))['sources']['official']['api']==api
			default_project=root/'default-official'
			default_project.mkdir()
			default_cache=root/'default-official-cache'
			before=len(requests)
			run('install','network-example',f'--path={default_project}',f'--cache-dir={default_cache}',
				f'--mirror={api}','--allow-http','--credential-env=VERO_PM_TEST_TOKEN')
			seen=requests[before:]
			assert any(path=='/api/packages/network-example' for path,auth in seen)
			assert all(auth is None for path,auth in seen), 'default official credentials must not be sent to a mirror'
			default_lock=json.loads((default_project/'veroscript.lock').read_text(encoding='utf-8'))
			assert default_lock['sources']['official']['id']=='veroscript.official'
			assert default_lock['sources']['official']['api']==official_api
			restored_default=root/'default-official-restored'
			restored_default.mkdir()
			for name in ('veroscript-project.json', 'veroscript.lock'):
				shutil.copy2(default_project / name, restored_default / name)
			before=len(requests)
			run('install',f'--path={restored_default}',f'--cache-dir={default_cache}','--locked','--offline')
			assert len(requests)==before
			run('search','network-example',f'--path={default_project}',f'--mirror={api}','--allow-http')
			assert any(path.startswith('/api/search?') for path,auth in requests[before:])
			configured=root/'configured-official'
			configured.mkdir()
			(configured/'veroscript-project.json').write_text(json.dumps({'format':'veroscript-project','formatVersion':'0.1.1',
				'name':'configured-app','version':'0.1.1',
				'packageManager':{'registries':{'official':{'id':'veroscript.official','api':api}}}}),encoding='utf-8')
			run('install','network-example',f'--path={configured}',f'--cache-dir={root / "configured-cache"}',
				'--allow-http','--credential-env=VERO_PM_TEST_TOKEN',f'--credential-scope={api}')
			assert json.loads((configured/'veroscript.lock').read_text(encoding='utf-8'))['sources']['official']['api']==api
			scoped_api=f'http://127.0.0.1:{server.server_port}/scoped'
			for index,(suffix,expected) in enumerate((('/packages/network-example',None),('/packages',env['VERO_PM_TEST_TOKEN']))):
				scoped=root/f'scoped-{index}'
				scoped.mkdir()
				before=len(requests)
				run('install','network-example',f'--path={scoped}',f'--cache-dir={root / f"scoped-cache-{index}"}',
					f'--registry-api={scoped_api}','--allow-http','--credential-env=VERO_PM_TEST_TOKEN',
					f'--credential-scope={scoped_api}{suffix}')
				seen=requests[before:]
				assert any(path=='/scoped/packages/network-example' and auth==env['VERO_PM_TEST_TOKEN'] for path,auth in seen)
				assert any(path=='/scoped/packages/outside' and auth==expected for path,auth in seen), \
					'redirect must preserve only credentials authorized for its exact target'
			normalized=root/'normalized-credential'
			normalized.mkdir()
			before=len(requests)
			origin=f'http://127.0.0.1:{server.server_port}'
			run('install',origin+'/direct/scoped/../gui.VSPKG',f'--path={normalized}',
				f'--cache-dir={root / "normalized-cache"}','--allow-http',
				'--credential-env=VERO_PM_TEST_TOKEN',f'--credential-scope={origin}/direct/scoped')
			assert any(path=='/direct/gui.VSPKG' and auth is None for path,auth in requests[before:]), \
				'credential scope must be checked against the normalized request path'
			for index,parent in enumerate(('%2e%2e','%2e%2e%2f','.%2e%2F','%2e.%5c','%2e%2e%5C')):
				encoded=root/f'encoded-credential-{index}'
				encoded.mkdir()
				before=len(requests)
				run('install',origin+f'/direct/scoped/{parent}/gui.VSPKG',f'--path={encoded}',
					f'--cache-dir={root / f"encoded-cache-{index}"}','--allow-http',
					'--credential-env=VERO_PM_TEST_TOKEN',f'--credential-scope={origin}/direct/scoped')
				seen=requests[before:]
				assert seen and all(auth is None for path,auth in seen), \
					f'encoded parent segments must not escape the credential path boundary: {[(path,auth is not None) for path,auth in seen]}'
			for index,path in enumerate(('/direct/gui%2eVSPKG','/direct/'+urllib.parse.quote('界面.vspkg'))):
				allowed=root/f'encoded-allowed-{index}'
				allowed.mkdir()
				before=len(requests)
				run('install',origin+path,f'--path={allowed}',f'--cache-dir={root / f"encoded-allowed-cache-{index}"}',
					'--allow-http','--credential-env=VERO_PM_TEST_TOKEN',f'--credential-scope={origin}/direct')
				seen=requests[before:]
				assert seen and all(auth==env['VERO_PM_TEST_TOKEN'] for path,auth in seen), \
					'encoded filename dots and UTF-8 paths remain authorized within scope'
			before=len(requests)
			run('search','network-example',*opts,'--allow-http','--credential-env=VERO_PM_TEST_TOKEN',
				f'--credential-scope={api}/search')
			assert any(path.startswith('/api/search?') and auth==env['VERO_PM_TEST_TOKEN'] for path,auth in requests[before:]), \
				'queries on the exact credential path remain in scope'
			(network/'main.vs').write_text('import "network/example.vs";int main(){return network_answer()==54?0:1;}',encoding='utf-8')
			run(f'--path={network}',network/'main.vs')
			before=len(requests)
			run('install',*opts,'--locked','--offline')
			assert len(requests)==before
			for index,route in enumerate(('/direct/gui.VSPKG','/direct/artifact?download=1')):
				direct=root/f'direct-{index}'
				direct.mkdir()
				cache=root/f'direct-cache-{index}'
				url=f'http://127.0.0.1:{server.server_port}{route}'
				run('install',url,f'--path={direct}',f'--cache-dir={cache}','--allow-http')
				spec=json.loads((direct/'veroscript-project.json').read_text(encoding='utf-8'))['dependencies']['network-example']
				assert spec['url']==url and spec['sha256']==digest
				restored=root/f'direct-restored-{index}'
				restored.mkdir()
				for name in ('veroscript-project.json','veroscript.lock'):
					shutil.copy2(direct/name,restored/name)
				before=len(requests)
				run('install',url,f'--path={direct}',f'--cache-dir={cache}','--locked','--offline','--allow-http')
				run('install',f'--path={restored}',f'--cache-dir={cache}','--locked','--offline','--allow-http')
				assert len(requests)==before
			for file in (network/'veroscript-project.json',network/'veroscript.lock'):
				assert 'package-fixture-secret' not in file.read_text(encoding='utf-8')
			requirements = root / 'network-requirements.txt'
			requirements.write_text('network-example >= 0.1.1, \\\n < 0.2.0 # range\n'
									'network-example == 0.1.1\n', encoding='utf-8')
			network_requirements = root / 'network-requirements'
			network_requirements.mkdir()
			selection = [f'--path={network_requirements}', f'--cache-dir={root / "requirements-cache"}',
						 f'--registry-api={api}', '--allow-http']
			run('install', 'network-example@*', '-r', requirements, *selection)
			saved = (network_requirements / 'veroscript.lock').read_bytes()
			before = len(requests)
			run('install', '-r', requirements, *selection, '--locked', '--offline')
			assert len(requests) == before
			assert (network_requirements / 'veroscript.lock').read_bytes() == saved
			requirements.write_text(f'network-example @ http://127.0.0.1:{server.server_port}/direct/artifact?download=1 # URL\n', encoding='utf-8')
			url_requirements = root / 'url-requirements'
			url_requirements.mkdir()
			url_cache = root / 'url-requirements-cache'
			url_selection = [f'--path={url_requirements}', f'--cache-dir={url_cache}', '--allow-http']
			run('install', '-r', requirements, *url_selection)
			actual = json.loads((url_requirements / 'veroscript-project.json').read_text(encoding='utf-8'))
			assert actual['dependencies']['network-example']['sha256'] == digest
			url_lock = (url_requirements / 'veroscript.lock').read_bytes()
			url = f'http://127.0.0.1:{server.server_port}/direct/artifact?download=1'
			for entry in (url, f'network-example @ {url}'):
				requirements.write_text(entry + '\n', encoding='utf-8')
				before = len(requests)
				run('install', '-r', requirements, *url_selection, '--locked', '--offline')
				assert len(requests) == before
				assert (url_requirements / 'veroscript.lock').read_bytes() == url_lock
			restored_urls = root / 'url-requirements-restored'
			restored_urls.mkdir()
			for name in ('veroscript-project.json', 'veroscript.lock'):
				shutil.copy2(url_requirements / name, restored_urls / name)
			before = len(requests)
			run('install', '-r', requirements, f'--path={restored_urls}', f'--cache-dir={url_cache}',
				'--locked', '--offline', '--allow-http')
			assert len(requests) == before
			assert (restored_urls / 'veroscript.lock').read_bytes() == url_lock
		finally:
			server.shutdown()
			server.server_close()
			thread.join(timeout=5)
		publication_tests(run,root)
		print('Package manager CLI, HTTP policy, authentication scopes, append publication and offline restore passed.')
if __name__=='__main__':
	main()
