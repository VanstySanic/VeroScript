// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

import com.intellij.codeInsight.AutoPopupController
import com.intellij.codeInsight.completion.CompletionParameters
import com.intellij.codeInsight.completion.InsertionContext
import com.intellij.codeInsight.lookup.LookupElement
import com.intellij.codeInsight.lookup.LookupElementDecorator
import com.intellij.platform.lsp.api.customization.LspCompletionSupport
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import org.eclipse.lsp4j.CompletionItem
import org.eclipse.lsp4j.InsertTextFormat

class VeroScriptCompletionSupport : LspCompletionSupport() {
	override fun createLookupElement(parameters: CompletionParameters, item: CompletionItem): LookupElement? {
		val element = super.createLookupElement(parameters, item) ?: return null
		if (item.insertTextFormat == InsertTextFormat.Snippet || !element.lookupString.endsWith("::")) return element
		return object : LookupElementDecorator<LookupElement>(element) {
			override fun handleInsert(context: InsertionContext) {
				super.handleInsert(context)
				// The SDK has already applied the server's text edit. Do not insert another colon
				// when ':' was also used to accept this namespace completion.
				if (context.completionChar == ':') context.setAddCompletionChar(false)
				val editor = context.editor
				val offset = editor.caretModel.offset
				val previous = context.laterRunnable
				// Run after the current completion has finished, including SDK snippet/edit handling.
				context.laterRunnable = Runnable {
					previous?.run()
					if (!context.project.isDisposed && !editor.isDisposed && editor.caretModel.offset == offset) {
						AutoPopupController.getInstance(context.project).scheduleAutoPopup(editor) { file ->
							file.fileType == VeroScriptFileType && editor.caretModel.offset == offset &&
								offset >= 2 && offset <= editor.document.textLength &&
								editor.document.charsSequence.subSequence(offset - 2, offset).toString() == "::"
						}
					}
				}
			}
		}
	}
}
