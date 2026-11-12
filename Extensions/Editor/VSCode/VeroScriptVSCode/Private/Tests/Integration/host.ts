// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { containsPath } from '../../configuration';

export async function extensionUnderTest<T>(): Promise<vscode.Extension<T>> {
	const id = process.env.VEROSCRIPT_TEST_EXTENSION_ID!;
	const vsix = process.env.VEROSCRIPT_TEST_VSIX;
	if (vsix) {
		// Use the documented editor install command, independent of CLI layout.
		await vscode.commands.executeCommand('workbench.extensions.installExtension', vscode.Uri.file(vsix));
	}
	const deadline = Date.now() + 15000;
	while (Date.now() < deadline) {
		const extension = vscode.extensions.getExtension<T>(id);
		if (extension) {
			if (vsix && !containsPath(process.env.VEROSCRIPT_TEST_EXTENSIONS!, extension.extensionPath)) {
				throw new Error('The packaged test loaded source code instead of the installed VSIX.');
			}
			return extension;
		}
		await new Promise(resolve => setTimeout(resolve, 100));
	}
	throw new Error(`Extension manifest did not load: ${id}`);
}
