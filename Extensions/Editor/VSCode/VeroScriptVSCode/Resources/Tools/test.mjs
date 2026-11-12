// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as fs from 'node:fs/promises';
import * as path from 'node:path';
import { randomUUID } from 'node:crypto';
import { downloadAndUnzipVSCode } from '@vscode/test-electron';
import { moduleRoot, runNode, temporaryRoot } from './paths.mjs';

const runtime = process.env.VEROSCRIPT_TEST_RUNTIME;
if (!runtime || !path.isAbsolute(runtime)) {
	throw new Error('Set VEROSCRIPT_TEST_RUNTIME to an absolute, extracted Runtime directory.');
}
await fs.access(path.join(runtime, process.platform === 'win32' ? 'veroscript.exe' : 'veroscript'));
if (!process.argv.includes('--skip-build')) {
	await runNode(path.join(moduleRoot, 'Resources/Tools/build.mjs'), ['--tests']);
}
const executable = process.env.VEROSCRIPT_TEST_VSCODE_EXECUTABLE || await downloadAndUnzipVSCode({
	version: process.env.VEROSCRIPT_TEST_VSCODE_VERSION || '1.106.0', cachePath: path.join(temporaryRoot, 'downloads')
});
const testDirectory = process.env.VEROSCRIPT_TEST_DIRECTORY || path.join(temporaryRoot, 'tests');
if (!path.isAbsolute(testDirectory)) {
	throw new Error('VEROSCRIPT_TEST_DIRECTORY must be absolute.');
}
const session = path.join(testDirectory, randomUUID());
const rootA = path.join(session, 'workspace A');
const rootB = path.join(session, 'workspace B');
const nested = path.join(rootA, 'nested');
const outside = path.join(session, 'outside');
const user = path.join(session, 'user');
const extensions = path.join(session, 'extensions');
await Promise.all([rootA, rootB, nested, outside, path.join(user, 'User'), extensions].map(directory => fs.mkdir(directory, { recursive: true })));
const files = {
	[path.join(rootA, 'hello world 中文.vs')]: 'int64 add(int64 left, int64 right)\n{\n    return left + right;\n}\n\nvoid main()\n{\n    int64 answer = add(20, 22);\n    print(answer);\n}\n',
	[path.join(rootB, 'other.vs')]: 'int64 onlyB() { return 7; }\nvoid main() { print(onlyB()); }\n',
	[path.join(nested, 'nested.vs')]: 'int64 nestedOnly() { return 9; }\nvoid main() { print(nestedOnly()); }\n',
	[path.join(outside, 'outside [x].vs')]: 'int64 outsideOnly() { return 11; }\nvoid main() { print(outsideOnly()); }\n',
	[path.join(rootA, 'args.vs')]: 'void main(string[] args)\n{\n    for (int64 i = 0; i < len(args); ++i)\n    {\n        print(args[i]);\n    }\n}\n'
};
await Promise.all(Object.entries(files).map(([file, content]) => fs.writeFile(file, content)));
const settings = { 'veroscript.runtimePath': runtime, 'task.saveBeforeRun': 'never', 'debug.openDebug': 'neverOpen', 'debug.internalConsoleOptions': 'neverOpen', 'files.hotExit': 'off', 'workbench.localHistory.enabled': false };
await fs.writeFile(path.join(user, 'User/settings.json'), JSON.stringify(settings));
const workspace = path.join(session, 'test.code-workspace');
await fs.writeFile(workspace, JSON.stringify({ folders: [{ path: rootA }, { path: rootB }, { path: nested }], settings }));
const cli = JSON.parse(await fs.readFile(path.join(moduleRoot, 'node_modules/@vscode/test-cli/package.json'), 'utf8'));
let harness;
let vsix;
const pkg = JSON.parse(await fs.readFile(path.join(moduleRoot, 'package.json'), 'utf8'));
if (process.argv.includes('--packaged')) {
	vsix = path.join(moduleRoot, 'out', `${pkg.name}-${pkg.version}.vsix`);
	await fs.access(vsix);
	harness = path.join(session, 'harness');
	await fs.mkdir(harness);
	await fs.writeFile(path.join(harness, 'package.json'), JSON.stringify({ name: 'veroscript-package-tests', publisher: 'local-tests', version: '0.0.0',
		engines: { vscode: '^1.106.0' }, main: 'extension.cjs', activationEvents: [] }));
	await fs.writeFile(path.join(harness, 'extension.cjs'), 'exports.activate = () => {};\n');
}
console.log(`VS Code: ${executable}\nRuntime: ${runtime}\nTest session: ${session}`);
await runNode(path.join(moduleRoot, 'node_modules/@vscode/test-cli', typeof cli.bin === 'string' ? cli.bin : Object.values(cli.bin)[0]), [], {
	env: { ...process.env, VEROSCRIPT_TEST_VSCODE_EXECUTABLE: executable,
		VEROSCRIPT_TEST_NODE: process.execPath,
		VEROSCRIPT_TEST_WORKSPACE: process.argv.includes('--folderless') ? path.join(outside, 'outside [x].vs') : workspace,
		VEROSCRIPT_TEST_USER_DATA: user, VEROSCRIPT_TEST_EXTENSIONS: extensions, VEROSCRIPT_TEST_SESSION: session,
		VEROSCRIPT_TEST_HARNESS: harness, VEROSCRIPT_TEST_PACKAGED: harness ? '1' : '',
		VEROSCRIPT_TEST_VSIX: vsix, VEROSCRIPT_TEST_EXTENSION_ID: `${pkg.publisher}.${pkg.name}`,
		VEROSCRIPT_TEST_FOLDERLESS: process.argv.includes('--folderless') ? '1' : '' }
});
