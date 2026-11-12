// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.lang.Language
import com.intellij.openapi.fileTypes.LanguageFileType
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle
import com.vanstysanic.veroscript.jetbrains.VeroScriptIcons

object VeroScriptLanguage : Language("VeroScript")

object VeroScriptFileType : LanguageFileType(VeroScriptLanguage) {
	override fun getName() = "VeroScript"
	override fun getDescription() = VeroScriptBundle.message("file.description")
	override fun getDefaultExtension() = "vs"
	override fun getIcon() = VeroScriptIcons.file
}
