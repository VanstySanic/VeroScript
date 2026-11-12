// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

const { defineConfig } = require('@vscode/test-cli');

module.exports = defineConfig({
	files: [process.env.VEROSCRIPT_TEST_FOLDERLESS ? 'out/Integration/folderless.test.cjs' : 'out/Integration/extension.test.cjs', 'out/Integration/domain.test.cjs', 'out/Integration/moduleSources.test.cjs', 'out/Integration/editing.test.cjs'],
	srcDir: 'Private',
	extensionDevelopmentPath: process.env.VEROSCRIPT_TEST_HARNESS || __dirname,
	useInstallation: { fromPath: process.env.VEROSCRIPT_TEST_VSCODE_EXECUTABLE },
	workspaceFolder: process.env.VEROSCRIPT_TEST_WORKSPACE,
	launchArgs: ['--user-data-dir=' + process.env.VEROSCRIPT_TEST_USER_DATA,
		'--extensions-dir=' + process.env.VEROSCRIPT_TEST_EXTENSIONS, '--skip-welcome', '--skip-release-notes', '--disable-updates'],
	mocha: { timeout: 30000 }
});
