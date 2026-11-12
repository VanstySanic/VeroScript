// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.debug

import org.eclipse.lsp4j.debug.Capabilities

/** Native DAP extensions. Missing capabilities are deliberately unsupported. */
class VeroScriptDapCapabilities : Capabilities() {
	var veroscriptSupportsDomainAttach = false
}

class DomainDebugInfo {
	var veroscriptHostId: String? = null
	var veroscriptDomainId: String? = null
	var veroscriptTargetName: String? = null
	var veroscriptSourceRevision: String? = null
	var veroscriptBindingRevision: String? = null
	var veroscriptSources: List<DomainDebugSource> = emptyList()
	var veroscriptCanPause = false
	var veroscriptCanStep = false
	var veroscriptCanEvaluate = false
	var veroscriptCanUseConditionalBreakpoints = false
	var veroscriptCanSetVariable = false
	var veroscriptCanTerminate = false
}

class DomainDebugSource {
	var path: String? = null
	var version: String? = null
}
