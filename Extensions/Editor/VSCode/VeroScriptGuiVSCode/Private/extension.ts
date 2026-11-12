// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import { Catalogs } from './catalog';
import { GuiProviders } from './providers';
import { GuiTasks, report } from './tasks';

export function activate(context: vscode.ExtensionContext) {
	const output = vscode.window.createOutputChannel('VeroScript GUI', { log: true });
	const catalogs = new Catalogs(context, output);
	const providers = new GuiProviders(catalogs, output);
	const tasks = new GuiTasks(output);
	context.subscriptions.push(output, catalogs, providers, vscode.tasks.registerTaskProvider('veroscript-gui', tasks));
	const command = (name: string, execute: (argument?: unknown) => unknown) => {
		context.subscriptions.push(vscode.commands.registerCommand(`veroscriptGui.${name}`, async (argument?: unknown) => {
			try {
				return await execute(argument);
			}
			catch (error)
			{
				report(error, output);
				return undefined;
			}
		}));
	};
	command('check', argument => tasks.run('check', argument));
	command('build', argument => tasks.run('compile', argument));
	command('preview', argument => tasks.run('preview', argument));
	command('selectEntry', () => tasks.selectEntry());
	command('showOutput', () => output.show());
	return { apiVersion: 1 as const };
}
