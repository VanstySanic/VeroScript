// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.codeInsight.completion.CompletionParameters
import com.jetbrains.rider.completion.RiderFrontendLanguagesPreselectionStrategy

/** Let Rider apply its normal first-item selection and the user's completion settings to VeroScript. */
class VeroScriptCompletionPreselection : RiderFrontendLanguagesPreselectionStrategy {
	override fun shouldPreselectFirstSuggestion(parameters: CompletionParameters) =
		parameters.originalFile.fileType == VeroScriptFileType
}
