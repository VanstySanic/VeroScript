// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import com.intellij.codeInsight.AutoPopupController
import com.intellij.codeInsight.completion.*
import com.intellij.codeInsight.lookup.LookupElement
import com.intellij.codeInsight.lookup.LookupElementDecorator
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.EditorFactory
import com.intellij.openapi.fileTypes.PlainTextFileType
import com.intellij.openapi.project.ProjectManager
import com.intellij.openapi.util.Condition
import com.intellij.openapi.util.Disposer
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.PsiFileFactory
import com.intellij.testFramework.replaceService
import com.intellij.testFramework.TestApplicationManager
import com.jetbrains.rider.completion.RiderFrontendLanguagesPreselectionStrategy
import com.jetbrains.rider.completion.RiderTypingSessionPreselectionStrategy
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptFileType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptCompletionPreselection
import com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptCompletionSupport
import org.eclipse.lsp4j.*
import org.eclipse.lsp4j.jsonrpc.messages.Either
import org.junit.Assert.*
import org.junit.BeforeClass
import org.junit.Test
import java.util.concurrent.TimeUnit

class CompletionSupportTest {
	@Test fun riderPreselectsVeroScriptWithoutClaimingOtherLanguages() = onEditor("std::") { file, editor ->
		val parameters = parameters(file, editor)
		assertTrue(RiderFrontendLanguagesPreselectionStrategy.EP_NAME.extensionList.any {
			it is VeroScriptCompletionPreselection
		})
		assertTrue(RiderTypingSessionPreselectionStrategy().shouldPreselectFirstSuggestion(parameters))
		val other = PsiFileFactory.getInstance(file.project).createFileFromText("other.txt", PlainTextFileType.INSTANCE, "text")
		assertFalse(VeroScriptCompletionPreselection().shouldPreselectFirstSuggestion(parameters(other, editor)))
	}

	@Test fun namespaceAcceptanceSchedulesNextPopupAfterInsertion() {
		for (namespace in listOf("std::", "std::chrono::", "Tools::")) {
			for (accept in listOf('\n', '\t', ':')) onEditor(namespace) { file, editor ->
				val popup = RecordingPopup()
				val lifetime = Disposer.newDisposable()
				file.project.replaceService(AutoPopupController::class.java, popup, lifetime)
				val offsets = OffsetMap(editor.document).apply { addOffset(InsertionContext.TAIL_OFFSET, namespace.length) }
				try {
					val item = CompletionItem(namespace.removeSuffix("::")).apply {
						// Exercise the textEdit path used by the native server, with a distinct filterText.
						filterText = label
						textEdit = Either.forLeft(TextEdit(Range(Position(0, 0), Position(0, 1)), namespace))
					}
					val element = requireNotNull(VeroScriptCompletionSupport().createLookupElement(parameters(file, editor), item))
					assertTrue(element.allLookupStrings.contains(item.label))
					assertSame(item, element.`object`)
					val context = InsertionContext(offsets, accept, arrayOf(element), file, editor, true)
					var previousCalled = false
					context.laterRunnable = Runnable { previousCalled = true }
					element.handleInsert(context)
					assertNull(popup.editor)
					requireNotNull(context.laterRunnable).run()
					assertTrue(previousCalled)
					assertSame(editor, popup.editor)
					assertTrue(requireNotNull(popup.condition).value(file))
					assertEquals(namespace, editor.document.text)
					if (accept == ':') assertFalse(context.shouldAddCompletionChar())
					editor.caretModel.moveToOffset(0)
					assertFalse(requireNotNull(popup.condition).value(file))
				}
				finally
				{
					Disposer.dispose(offsets)
					Disposer.dispose(lifetime)
				}
			}
		}
	}

	@Test fun functionsAndSnippetsKeepDefaultInsertHandling() = onEditor("print") { file, editor ->
		for (item in listOf(CompletionItem("print"), CompletionItem("snippet").apply {
			insertText = "${'$'}{1:name}::"
			insertTextFormat = InsertTextFormat.Snippet
		})) {
			val element = requireNotNull(VeroScriptCompletionSupport().createLookupElement(parameters(file, editor), item))
			assertFalse(element is LookupElementDecorator<*>)
		}
	}

	private fun parameters(file: PsiFile, editor: Editor) = CompletionParameters(
		file, file, CompletionType.BASIC, minOf(editor.caretModel.offset, file.textLength), 0, editor,
		CompletionProcess { true })

	private fun onEditor(text: String, action: (PsiFile, Editor) -> Unit) {
		ApplicationManager.getApplication().invokeAndWait {
			ApplicationManager.getApplication().runWriteAction {
				val project = ProjectManager.getInstance().defaultProject
				val file = PsiFileFactory.getInstance(project).createFileFromText("completion.vs", VeroScriptFileType, text)
				val factory = EditorFactory.getInstance()
				val editor = factory.createEditor(factory.createDocument(text), project)
				try {
					editor.caretModel.moveToOffset(text.length)
					action(file, editor)
				}
				finally
				{
					factory.releaseEditor(editor)
				}
			}
		}
	}

	@Suppress("OVERRIDE_DEPRECATION")
	private class RecordingPopup : AutoPopupController() {
		var editor: Editor? = null
		var condition: Condition<in PsiFile>? = null
		override fun scheduleAutoPopup(editor: Editor, type: CompletionType, condition: Condition<in PsiFile>?) {
			this.editor = editor
			this.condition = condition
		}
		override fun autoPopupParameterInfo(editor: Editor, highlightedMethod: PsiElement?) {}
		override fun cancelAllRequests() {}
		override fun waitForDelayedActions(timeout: Long, unit: TimeUnit) {}
		override fun autoPopupMemberLookup(editor: Editor, condition: Condition<in PsiFile>?) {}
		override fun autoPopupMemberLookup(editor: Editor, type: CompletionType, condition: Condition<in PsiFile>?) {}
	}

	companion object {
		@JvmStatic @BeforeClass fun initializeApplication() { TestApplicationManager.getInstance() }
	}
}
