// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import * as vscode from 'vscode';
import type { VeroScriptToolsApi } from '../../VeroScriptVSCode/Public/api';

export async function baseExtension(): Promise<VeroScriptToolsApi> {
	const extension = vscode.extensions.getExtension<VeroScriptToolsApi>('VanstySanic.veroscript');
	const api = extension && (extension.isActive ? extension.exports : await extension.activate());
	if (!api || api.apiVersion !== 1 || typeof api.getToolchain !== 'function') {
		throw new Error(vscode.l10n.t('Install the matching VeroScript extension with tools API 1. Rebuild and reinstall both local extensions together.'));
	}
	return api;
}
