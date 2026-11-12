# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Install the actual VM distribution and execute its generated loader receipt."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('repo', 'build', 'tool', 'cli'):
	parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--config', default='Release')
args = parser.parse_args()
sys.path.insert(0, str(args.repo / 'Resources/Tools/Packaging'))
from veroscript_vm import stage_backend, pack_backend, verify_installable_backend
from veroscript_release import archive_entries, host_target

def run(*values):
	return subprocess.check_output([str(value) for value in values], text=True, encoding='utf-8', timeout=60)

with tempfile.TemporaryDirectory(prefix='vero-vm-package-') as temporary:
	root = Path(temporary)
	package, project = root / 'package', root / 'project'
	project.mkdir()
	stage_backend(args.build, package, args.tool, args.config)
	archive = root / 'vs-bytecode-vm.vspkg'
	pack_backend(package, archive, args.tool)
	package_version = json.loads((package / 'veroscript-package.json').read_text(encoding='utf-8'))['version']
	verify_installable_backend(archive_entries(archive), package_version, host_target())
	run(args.cli, 'install', archive, '--path=' + str(project), '--offline', '--cache-dir=' + str(root / 'cache'))
	state = json.loads((project / '.veroscript/state.json').read_text(encoding='utf-8'))
	installed = next(iter(state['profiles'].values()))['packages']['vs-bytecode-vm']
	receipt = installed['loaders']['backends:vm:vs-bytecode']
	library = project / '.veroscript' / installed['generated'] / receipt['path']
	script = root / 'check.vs'
	script.write_text('void main(){int result=0;for(int i=0;i<100;++i)result+=i;print(result);}', encoding='utf-8')
	assert run(args.cli, '--vm-backend=' + str(library), script).strip() == '4950'
	run(args.cli, 'verify', '--path=' + str(project), '--offline', '--cache-dir=' + str(root / 'cache'))
print('VM pack/install/generated-loader execution passed.')
