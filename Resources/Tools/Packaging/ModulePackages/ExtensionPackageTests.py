# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Audit actual backend archives and consume installed optional extensions."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	for name in ('repo', 'build', 'tool', 'cli', 'work'):
		parser.add_argument('--' + name, type=Path, required=True)
	parser.add_argument('--config', default='Release')
	parser.add_argument('--kind', choices=('jit', 'foundation'), required=True)
	args = parser.parse_args()
	repo, build, tool, cli = (getattr(args, name).resolve() for name in ('repo', 'build', 'tool', 'cli'))
	args.work.mkdir(parents=True, exist_ok=True)
	work = Path(tempfile.mkdtemp(prefix=args.kind + '-', dir=args.work))
	sys.path.insert(0, str(repo / 'Resources/Tools/Packaging'))
	from veroscript_release import host_target, archive_tree, archive_entries, digest, write_manifest, verify_release
	from veroscript_jit import build_cache
	from veroscript_business_release import load_versions
	target = host_target()
	settings = build_cache(build)
	cmake = settings.get('CMAKE_COMMAND', 'cmake')
	versions = load_versions(repo)['businesses']

	def run(*values, cwd=work, expected=0):
		result = subprocess.run([str(value) for value in values], cwd=cwd, capture_output=True,
								text=True, encoding='utf-8', errors='replace', timeout=600)
		print(result.stdout, end='', flush=True)
		if result.returncode != expected:
			raise AssertionError(f'{values}: {result.returncode}\n{result.stderr}')
		return result.stdout

	if args.kind == 'jit':
		from veroscript_jit import stage_backend, pack_backend, verify_installable_backend
		source = work / ('vs-asmjit-0.1.1-' + target)
		stage_backend(repo, build, source, target, '0.1.1', args.config)
		portable = work / (source.name + ('.zip' if target.startswith('win-') else '.tar.gz'))
		archive_tree(source, portable)
		assert all(name.startswith(source.name + '/') for name in archive_entries(portable))
		archive = work / (source.name + '.vspkg')
		pack_backend(repo, source, archive, tool, target, '0.1.1')
		verify_installable_backend(archive_entries(archive), '0.1.1', target)
		assert not (source / 'veroscript-package.json').exists()
		invalid = work / 'invalid-project'
		invalid.mkdir()
		wrapped_zip = work / 'portable-with-wrapper.zip'
		archive_tree(source, wrapped_zip)
		run(cli, 'install', wrapped_zip, '--path=' + str(invalid), '--offline', expected=1)
		renamed = work / 'installable.zip'
		shutil.copy2(archive, renamed)
		(work / 'zip-project').mkdir()
		run(cli, 'install', renamed, '--path=' + str(work / 'zip-project'), '--offline', '--cache-dir=' + str(work / 'cache'))
		project = work / '项目 with spaces'
		project.mkdir()
		cache = work / 'cache'
		# No environment option: startup directory selects the project.
		run(cli, 'install', archive, '--offline', '--cache-dir=' + str(cache), cwd=project)
		source_file = project / 'main.vs'
		source_file.write_text('int next(){static int value=0;return ++value;}'
							   'int sum(int count){int result=0;for(int i=0;i<count;++i)result+=i;return result;}'
							   'void main(){assert(next()==1&&next()==2);'
							   'auto count=[value=0]()mutable->int{return ++value;};auto alias=count;'
							   'assert(count()==1&&alias()==2);'
							   'int result=0;for(int n=0;n<100;++n)result=sum(100);'
							   'assert(result==4950);print(result);}', encoding='utf-8')
		run(cli, '--path=' + project.name, '--jit-backend=asmjit', '--backend=jit', source_file)
		# Installed service candidates are automatic; explicit disable never falls through.
		run(cli, '--path=' + project.name, '--backend=jit', source_file)
		run(cli, '--path=' + project.name, '--jit-backend=auto', '--vm-backend=auto', '--backend=jit', source_file)
		run(cli, '--path=' + project.name, '--jit-backend=none', '--backend=mixed', source_file)
		disabled = subprocess.run([str(cli), '--path=' + project.name, '--jit-backend=none',
								   '--backend=jit', str(source_file)], cwd=work, capture_output=True, timeout=120)
		assert disabled.returncode != 0
		output = run(tool, 'run', project, source_file, '--jit')
		assert 'installed jit compiled=' in output and '4950' in output
		(project / '.veroscript').rename(project / 'saved-environment')
		run(cli, 'install', '--path=' + str(project), '--locked', '--offline', '--cache-dir=' + str(cache))
		run(tool, 'run', project, source_file, '--jit')
		run(cli, 'disable', 'vs-asmjit', '--path=' + str(project), '--offline', '--cache-dir=' + str(cache))
		run(cli, '--path=' + str(project), '--backend=mixed', source_file)
		unavailable = subprocess.run([str(cli), '--path=' + str(project), '--backend=jit', str(source_file)],
									 capture_output=True, timeout=120)
		assert unavailable.returncode != 0
		failed = subprocess.run([str(cli), '--path=' + str(project), '--jit-backend=asmjit', str(source_file)], capture_output=True, timeout=120)
		assert failed.returncode != 0
		run(cli, 'enable', 'vs-asmjit', '--path=' + str(project), '--offline', '--cache-dir=' + str(cache))
		run(cli, 'uninstall', 'vs-asmjit', '--path=' + str(project), '--offline', '--cache-dir=' + str(cache))
		failed = subprocess.run([str(cli), '--path=' + str(project), '--jit-backend=asmjit', str(source_file)], capture_output=True, timeout=120)
		assert failed.returncode != 0
		target_context = json.loads(run(cli, 'env', '--json'))['target']
		for role in ('runtime', 'core-sdk'):
			component = work / (role + '-component')
			component.mkdir()
			(component / 'veroscript-component.json').write_text(json.dumps({
				'format': 'veroscript-component', 'formatVersion': '0.1.1', 'role': role,
				'root': '.', 'target': target_context}), encoding='utf-8')
			run(cli, 'install', archive, '--root', '--path=' + str(component), '--offline', '--cache-dir=' + str(cache))
			run(cli, '--root', '--path=' + str(component), '--jit-backend=asmjit', '--backend=jit', source_file)
	else:
		from veroscript_products import FOUNDATION_BUSINESSES
		from veroscript_foundation import assemble_foundation
		from veroscript_foundation_packages import verify_installable_foundation_consumers
		info = json.loads((build / 'veroscript-foundation-Release.json').read_text(encoding='utf-8'))
		selected = [name for name in FOUNDATION_BUSINESSES if name in info]
		sdk = work / 'sdk'
		sdk.mkdir()
		for module, folder, library in (('VeroScript', 'Script', info['Core']),
										 ('VeroScriptTooling', 'Tooling', None),
										 ('Extensions/Language/Cpp/VeroScriptCpp', 'Cpp', info['Cpp'])):
			shutil.copytree(repo / module / 'Public', sdk / 'include' / folder)
			if library:
				(sdk / 'lib').mkdir(exist_ok=True)
				shutil.copy2(library, sdk / 'lib' / Path(library).name)
		shutil.copy2(info['CoreBuildHeader'], sdk / 'include/Script/VeroScriptJitBuild.h')
		core_id = digest(Path(info['Core']))
		for product, owner in (('VeroScript', 'VeroScript'), ('VeroScriptCpp', 'Extensions/Language/Cpp/VeroScriptCpp')):
			text = (repo / owner / 'Resources/Tools/CMake' / (product + 'Config.cmake.in')).read_text(encoding='utf-8')
			for key, value in {'VERSION': '0.1.1', 'CORE_VERSION': '0.1.1', 'BUILD_ID': core_id,
							   'STATIC_PREFIX': '' if target.startswith('win-') else 'lib',
							   'STATIC_SUFFIX': '.lib' if target.startswith('win-') else '.a',
							   'APPLICATION_ENABLED': '1', 'EXE_SUFFIX': '.exe' if target.startswith('win-') else ''}.items():
				text = text.replace('@' + key + '@', value)
			directory = sdk / 'lib/cmake' / product
			directory.mkdir(parents=True)
			(directory / (product + 'Config.cmake')).write_text(text, encoding='utf-8')
		candidate = work / 'candidate'
		identities = assemble_foundation(repo, dict.fromkeys(selected, build), candidate, versions,
										 target, selected, core_sdk=sdk, cpp_sdk=sdk, package_tool=tool)
		write_manifest(candidate, '0.1.1', target, core_id, business_versions=versions, native_version='0.1.1')
		path = candidate / 'release-manifest.json'
		manifest = json.loads(path.read_text(encoding='utf-8'))
		manifest.update(selectedBusinesses=selected, businessBuildIds=identities,
						installableModuleBusinesses=selected)
		path.write_text(json.dumps(manifest), encoding='utf-8')
		verify_release(candidate, repo, businesses=set(selected))
		verify_installable_foundation_consumers(repo, candidate, dict.fromkeys(selected, build),
												selected, target, tool)
	print('Extension package integration passed; artifacts: ' + str(work))


if __name__ == '__main__':
	main()
