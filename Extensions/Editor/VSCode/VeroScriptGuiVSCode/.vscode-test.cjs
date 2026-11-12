// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

const { defineConfig } = require('@vscode/test-cli');
module.exports = defineConfig({
	files: 'out/Integration/*.test.cjs', srcDir: 'Private',
	extensionDevelopmentPath: process.env.VEROSCRIPT_GUI_TEST_HARNESS || [__dirname, require('node:path').resolve(__dirname, '../VeroScriptVSCode')],
	skipExtensionDependencies: true,
	useInstallation: { fromPath: process.env.VEROSCRIPT_TEST_VSCODE_EXECUTABLE },
	workspaceFolder: process.env.VEROSCRIPT_GUI_TEST_WORKSPACE,
	launchArgs: ['--user-data-dir=' + process.env.VEROSCRIPT_GUI_TEST_USER,
		'--extensions-dir=' + process.env.VEROSCRIPT_GUI_TEST_EXTENSIONS, '--skip-welcome', '--skip-release-notes', '--disable-updates'],
	mocha: { timeout: 30000 }
});
