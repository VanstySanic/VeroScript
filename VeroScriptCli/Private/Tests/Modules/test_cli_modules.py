# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise public-only native CLI modules through the actual executable."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--cli', type=Path, required=True)
	parser.add_argument('--module', type=Path, required=True)
	parser.add_argument('--conflict', type=Path, required=True)
	parser.add_argument('--throw-query', type=Path, required=True)
	parser.add_argument('--throw-create', type=Path, required=True)
	parser.add_argument('--example', type=Path, required=True)
	parser.add_argument('--packages', default='1')
	args = parser.parse_args()
	for field in ("cli", "module", "conflict", "example", "throw_query", "throw_create"):
		setattr(args, field, getattr(args, field).resolve())
	checks = 0
	with tempfile.TemporaryDirectory(prefix='vero-cli-modules-') as temporary:
		root = Path(temporary)

		def run(*tokens, code=0, contains='', cwd=root):
			nonlocal checks
			result = subprocess.run([str(args.cli), *map(str, tokens)], cwd=cwd,
									capture_output=True, encoding='utf-8', timeout=45)
			assert result.returncode == code, (tokens, result.returncode, result.stdout, result.stderr)
			assert contains in result.stdout + result.stderr, (tokens, result.stdout, result.stderr)
			checks += 1
			return result

		prefix = ('--cli-module', args.module)
		run(*prefix, 'cli-echo', 'hello world', '中文', '--module-message', 'script argument',
			contains='arg=hello world\narg=中文\narg=--module-message\narg=script argument\n')
		run(*prefix, '--module-message=你好', '--module-flag', '--module-item', 'one',
			'--module-item=two', 'cli-echo', contains='你好:on\nitem=one\nitem=two\n')
		run(*prefix, '--module-message', '--', 'cli-echo', code=2, contains='missing value')
		run(*prefix, '--module-message', code=2, contains='missing value')
		run(*prefix, '--module-message=a', '--module-message=b', 'cli-echo', code=2, contains='duplicate')
		run(*prefix, '--module-flag=yes', 'cli-echo', code=2, contains='does not accept a value')
		run(*prefix, '--module-message=invalid', 'cli-echo', code=2, contains='configuration rejected')
		run(*prefix, '--module-message=throw', 'cli-echo', code=2, contains='configure exception')
		run(*prefix, 'cli-echo', 'throw', code=1, contains='command exception')
		run(*prefix, 'cli-echo', 'exit', code=7)
		run(*prefix, '--module-message=invalid', 'cli-echo', '--help', contains='Usage: veroscript cli-echo')
		run(*prefix, '--help', contains='--module-message <text>')
		run(*prefix, '--version', contains='VeroScript 0.1.1')
		run(*prefix, '--cli-module', args.module, 'cli-echo', code=2, contains='duplicate')
		run('--cli-module', args.conflict, 'cli-echo', code=2, contains='reserved')
		for module in (args.throw_query, args.throw_create):
			run('--cli-module', module, 'cli-echo', code=2, contains='fixture native initialization exception')
		run('--cli-module', 'missing-library', 'cli-echo', code=2)
		run('--cli-module', code=2)
		unicode_directory = root / '模块 with spaces'
		unicode_directory.mkdir()
		unicode_module = unicode_directory / args.module.name
		shutil.copy2(args.module, unicode_module)
		run('--cli-module', unicode_module.relative_to(root), 'cli-echo', contains='default:off')
		run('--cli-module=' + str(unicode_module), 'cli-echo', contains='default:off')
		run('--cli-module', args.example, '--greeting=你好', 'greet', 'Vero', contains='你好, Vero!')
		run(*prefix, '--cli-module', args.example, '--greeting=Hello', 'greet', 'world', contains='Hello, world!')
		run(*prefix, '--module-message', '-h', code=2, contains='missing value')
		for option in ('-e', '-i', '-I', '-D', '-DNAME=1'):
			run(*prefix, '--module-message', option, code=2, contains='missing value')
		run(*prefix, '--module-message=-e', 'cli-echo', contains='-e:off')
		run(*prefix, '--module-message', '-42', 'cli-echo', contains='-42:off')
		run(*prefix, '--check', 'cli-echo', code=2, contains='cannot be combined')
		for option in ('--repl-strict', '--dap-port=12345', '--dap-bind=127.0.0.1'):
			run(*prefix, option, 'cli-echo', code=2, contains='cannot be combined')
		run('--module-message=test', '-e', 'print(1);', code=2, contains='unknown option')
		script = root / 'args.vs'
		script.write_text('int main(string[] args){print(args[0]);return 0;}', encoding='utf-8')
		run(*prefix, script, '--module-message=literal', contains='--module-message=literal')
		if args.packages != '0':
			fresh = root / 'fresh-project'
			fresh.mkdir()
			(fresh / 'veroscript-project.json').write_text(json.dumps({
				'format': 'veroscript-project', 'formatVersion': '0.1.1', 'name': 'fresh-project',
				'version': '0.1.1', 'dependencies': {'not-yet-installed': {'version': '0.1.1'}}}), encoding='utf-8')
			run('--help', cwd=fresh, contains='Usage:')
			run(*prefix, '--help', cwd=fresh, contains='cli-echo')
			run(*prefix, 'cli-echo', 'local', cwd=fresh, contains='arg=local')
			run(*prefix, '--module-message=local', 'cli-echo', cwd=fresh, contains='local:off')
			run(*prefix, '--module-message', '-e', 'print(1);', cwd=fresh, code=2, contains='missing value')
			run(*prefix, '--module-message=local', '-e', 'print(1);', cwd=fresh, code=2, contains='not installed')
			project = root / 'project'
			project.mkdir()
			target = json.loads(run('env', '--json', cwd=project).stdout)['target']
			target.pop('core', None)
			target['abis'] = {'native-extension': target['abis']['native-extension']}
			vendor = root / 'vendor'
			(vendor / 'native').mkdir(parents=True)
			shutil.copy2(args.module, vendor / 'native' / args.module.name)
			manifest = {'format': 'veroscript-package', 'formatVersion': '0.1.1',
						'name': 'cli-fixture-package', 'version': '0.1.1', 'target': target,
						'entries': {'native': [{'id': 'cli-fixture', 'path': 'native/' + args.module.name,
							'linkage': 'dynamic', 'componentVersion': '0.1.1', 'roles': ['bindings', 'cli']}]}}
			(vendor / 'veroscript-package.json').write_text(json.dumps(manifest), encoding='utf-8')
			run('install', vendor, '--offline', cwd=project)
			run('cli-echo', 'installed', cwd=project, contains='arg=installed')
			run('--help', cwd=project, contains='cli-echo')
			run('--module-message=package-value', '-e', 'print(native_cli_value());',
				cwd=project, contains='package-value')
			run('--path', project, '--module-message=selected', 'cli-echo', contains='selected:off')
			run('--module-flag', '--path', project, 'cli-echo', cwd=project, code=2, contains='must precede')
			run('disable', 'cli-fixture-package', cwd=project)
			run('--module-message=disabled', 'cli-echo', cwd=project, code=2, contains='unknown option')
			run('enable', 'cli-fixture-package', cwd=project)
			run('cli-echo', '--help', cwd=project, contains='Usage: veroscript cli-echo')
			# A CLI-only package must work without inventing runtime bindings,
			# and must not load its CLI factory during AOT preparation.
			pure_project = root / 'pure-cli-project'
			pure_project.mkdir()
			pure_vendor = root / 'pure-cli-vendor'
			(pure_vendor / 'native').mkdir(parents=True)
			shutil.copy2(args.example, pure_vendor / 'native' / args.example.name)
			manifest['name'] = 'cli-example-package'
			manifest['entries']['native'][0].update(
				id='cli-example', path='native/' + args.example.name, roles=['cli'])
			(pure_vendor / 'veroscript-package.json').write_text(json.dumps(manifest), encoding='utf-8')
			run('install', pure_vendor, '--offline', cwd=pure_project)
			run('--greeting=Package', 'greet', 'user', cwd=pure_project, contains='Package, user!')
			shadow = pure_project / 'greet'
			shadow.mkdir()
			run('greet', 'user', cwd=pure_project, contains='Hello, user!')
			shadow.rmdir()
			shadow.write_text('int main(){print("explicit script");return 0;}', encoding='utf-8')
			run('greet', 'user', cwd=pure_project, contains='Hello, user!')
			run('./greet', cwd=pure_project, contains='explicit script')
			run('-e', 'print(23);', cwd=pure_project, contains='23')
			(pure_project / 'main.vs').write_text('int main(){return 0;}', encoding='utf-8')
			run('aot', '--target=cpp', 'main.vs', '-o', 'main.cpp', cwd=pure_project)
			assert (pure_project / 'main.cpp').is_file()
		print(f'CLI module checks passed: {checks}')


if __name__ == '__main__':
	main()
