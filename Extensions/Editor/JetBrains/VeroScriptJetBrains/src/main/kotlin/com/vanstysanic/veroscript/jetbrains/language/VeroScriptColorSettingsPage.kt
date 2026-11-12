// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.codeInsight.editorActions.SimpleTokenSetQuoteHandler
import com.intellij.openapi.options.colors.AttributesDescriptor
import com.intellij.openapi.options.colors.ColorDescriptor
import com.intellij.openapi.options.colors.ColorSettingsPage
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import com.vanstysanic.veroscript.jetbrains.VeroScriptIcons

class VeroScriptQuoteHandler : SimpleTokenSetQuoteHandler(VeroScriptTokens.string)

class VeroScriptColorSettingsPage : ColorSettingsPage {
	override fun getDisplayName() = "VeroScript"
	override fun getIcon() = VeroScriptIcons.file
	override fun getHighlighter() = VeroScriptSyntaxHighlighter()
	override fun getDemoText() = """
        // VeroScript
        #define FEATURE 1
        int main() {
            string greeting = "Hello, world!";
            int64 count = 42;
            return 0;
        }
    """.trimIndent()
	override fun getAdditionalHighlightingTagToDescriptorMap() = null
	override fun getColorDescriptors(): Array<ColorDescriptor> = emptyArray()
	override fun getAttributeDescriptors() = arrayOf(
		AttributesDescriptor(message("color.keyword"), VeroScriptTextAttributes.keyword),
		AttributesDescriptor(message("color.string"), VeroScriptTextAttributes.string),
		AttributesDescriptor(message("color.number"), VeroScriptTextAttributes.number),
		AttributesDescriptor(message("color.comment"), VeroScriptTextAttributes.comment),
		AttributesDescriptor(message("color.preprocessor"), VeroScriptTextAttributes.metadata)
	)
}
