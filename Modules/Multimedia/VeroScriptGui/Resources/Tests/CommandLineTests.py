# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise the optional root CLI's GUI tasks and ordinary package dispatch."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


# GUI source commands may cold-compile the complete script framework.
COMPILE_TIMEOUT = 600


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--cli', type=Path, required=True)
	parser.add_argument('--stdlib', type=Path, required=True)
	parser.add_argument('--application', choices=('ON', 'OFF'), default='OFF')
	parser.add_argument('--runtime', type=Path)
	parser.add_argument('--scenario', choices=('all', 'source', 'artifact', 'dispatch', 'metadata'), default='all')
	args = parser.parse_args()
	cli = args.cli.resolve()
	stdlib = args.stdlib.resolve()

	def run(arguments, expected=0, cwd=None):
		print('GUI CLI command: ' + ' '.join(map(str, arguments)), flush=True)
		options = [] if arguments[:2] == ['package', 'inspect'] else ['--stdlib-path', str(stdlib)]
		result = subprocess.run([str(cli), *options, *map(str, arguments)],
								cwd=cwd,
								capture_output=True, text=True, encoding='utf-8', errors='replace',
								timeout=COMPILE_TIMEOUT)
		if result.returncode != expected:
			raise AssertionError(f'{arguments}: {result.returncode}\n{result.stdout}\n{result.stderr}')
		return result

	with tempfile.TemporaryDirectory(prefix='VeroScriptGuiCli-') as temporary:
		root = Path(temporary)
		sources = root / 'sources'
		sources.mkdir()
		page = sources / 'page.vswml'
		page.write_text('<Widget><script>public: double Count=3;</script>'
						'<style src="page.vswcss"/>'
						'<Widget id="part" bind:gap="{ context.Count }"/></Widget>', encoding='utf-8')
		(sources / 'page.vswcss').write_text('#part { padding: 7px; }', encoding='utf-8')
		entry = sources / 'main.vs'
		entry.write_text('import "page.vswml";void main(){'
						 'auto root=VS::Gui::LoadFactory<VS::Gui::Widget>("page.vswml").Create();'
						 'assert(root.GetChild(0).GetGap()==3);}', encoding='utf-8')
		plain = root / 'plain.vs'
		plain.write_text('class GuiCliState{public:VS::Gui::Observable<double> Value='
						 'VS::Gui::Observable<double>(0.25);};'
						 'void main(){VS::Gui::Widget w=VS::Gui::Widget::Create().Gap(6);'
						 'assert(w.GetGap()==6);GuiCliState state=GuiCliState();'
						 'auto shared=state.Value.AsBinding();'
						 'VS::Gui::ProgressBar first=VS::Gui::ProgressBar::Create().Value(shared);'
						 'assert(first.GetValue()==0.25);state.Value.Set(0.75);'
						 'VS::Gui::ProgressBar second=VS::Gui::ProgressBar::Create().Value(shared);'
						 'assert(second.GetValue()==0.75);}', encoding='utf-8')
		if args.scenario in ('all', 'metadata'):
			# The embedded framework uses the working directory in its source
			# identities. Repeated paths must not exhaust the metadata budget.
			working = root / ('metadata-' + 'nested-' * 14)
			working.mkdir()
			minimal = working / 'hello.vswml'
			minimal.write_text('<Stack><Text Text="Hello, GUI!" /></Stack>', encoding='utf-8')
			run(['gui-check', minimal], cwd=working)
			if args.application == 'ON':
				artifact = working / 'hello.vsapp'
				run(['gui-compile', minimal, '--output', artifact], cwd=working)
				with zipfile.ZipFile(artifact) as container:
					if not {'veroscript-app.json', 'veroscript-app.files.json', 'program/entry.bin'}.issubset(container.namelist()):
						raise AssertionError('Minimal GUI metadata regression did not produce an application container')
				minimal.unlink()
				run(['gui-check', artifact], cwd=working)
				run(['--check', artifact], cwd=working)

		if args.scenario in ('all', 'source'):
			print('GUI CLI source scenario', flush=True)
			if 'gui-compile' not in run(['--help']).stdout:
				raise AssertionError('GUI command help is missing')
			ordinary = root / 'ordinary.vs'
			ordinary.write_text('import "math.vs"; // VS::Gui @GuiWidget\n'
								'int main(){string label="Gui @GuiWidget";return 42;}', encoding='utf-8')
			run(['--check', ordinary])
			run(['gui-check', page])
			run(['gui-check', entry])
			run([entry])

		if args.scenario in ('all', 'artifact'):
			print('GUI CLI artifact scenario', flush=True)
			before = entry.read_bytes()
			run(['gui-compile', entry, '--output', entry], expected=2)
			if entry.read_bytes() != before:
				raise AssertionError('GUI compile overwrote its input')
			deployed = root / 'relocated 空间'
			deployed.mkdir()
			artifact = deployed / 'app.vsapp'
			if args.application == 'OFF':
				run(['gui-compile', entry, '--output', artifact], expected=2)
				if artifact.exists():
					raise AssertionError('GUI compilation wrote a raw .vsapp without Application support')
				return
			run(['gui-compile', entry, '--output', artifact])
			with zipfile.ZipFile(artifact) as container:
				if not {'veroscript-app.json', 'veroscript-app.files.json', 'program/entry.bin'}.issubset(container.namelist()):
					raise AssertionError('GUI compilation did not produce the standard application container')
			shutil.rmtree(sources)
			run(['gui-check', artifact])
			run(['gui-preview', artifact])
			run(['package', 'inspect', artifact])
			run(['--check', artifact])
			run([artifact])
			if args.runtime:
				runtime_cases = [['--check'], [], ['--check', '--vm-backend', 'auto'],
								 ['--check', '--jit-backend', 'none'],
								 ['--vm-backend', 'auto', '--jit-backend', 'none']]
				for library in (args.runtime.parent / 'VeroScriptJitAsmJit.dll',
								args.runtime.parent.parent / 'lib' / 'libVeroScriptJitAsmJit.so'):
					if library.is_file():
						runtime_cases.append(['--jit-backend', str(library.resolve())])
						break
				for options in runtime_cases:
					result = subprocess.run([str(args.runtime.resolve()), *options, str(artifact)],
											capture_output=True, text=True, encoding='utf-8', errors='replace',
											timeout=COMPILE_TIMEOUT)
					if result.returncode:
						raise AssertionError(result.stdout + result.stderr)

		if args.scenario in ('all', 'dispatch'):
			print('GUI CLI dispatch scenario', flush=True)
			run(['--gui-backend', 'rmlui', plain])
			if args.application == 'ON':
				container = root / 'container.vsapp'
				run(['--emit-package', container, plain])
				# A relative stdlib path must not duplicate the absolute root supplied
				# by the package/application environment.
				relative = stdlib.name
				print('GUI CLI dispatch: running package with relative stdlib', flush=True)
				result = subprocess.run([str(cli), '--stdlib-path', relative, str(container)],
										cwd=stdlib.parent, capture_output=True, text=True,
										encoding='utf-8', errors='replace', timeout=COMPILE_TIMEOUT)
				if result.returncode:
					raise AssertionError(result.stdout + result.stderr)
	if args.scenario == 'all':
		print('GUI CLI checks, source-free relocated artifacts and ordinary dispatch passed', flush=True)
	else:
		print(f'GUI CLI {args.scenario} scenario passed', flush=True)


if __name__ == '__main__':
	main()
