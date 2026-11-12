// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.application.options.CodeStyle
import com.intellij.codeInsight.editorActions.enter.EnterHandlerDelegate
import com.intellij.openapi.actionSystem.DataContext
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.actionSystem.EditorActionHandler
import com.intellij.openapi.util.Ref
import com.intellij.psi.PsiFile
import com.vanstysanic.veroscript.jetbrains.debug.VeroScriptDebugSourceFile
import com.vanstysanic.veroscript.jetbrains.settings.VeroScriptProjectSettings

/** Local empty-block editing; full-file formatting remains a language-server operation. */
class VeroScriptEnterHandler : EnterHandlerDelegate {
	override fun preprocessEnter(file: PsiFile, editor: Editor, caretOffset: Ref<Int>, caretAdvance: Ref<Int>,
		dataContext: DataContext, originalHandler: EditorActionHandler?): EnterHandlerDelegate.Result {
		if (file.fileType != VeroScriptFileType || file.virtualFile is VeroScriptDebugSourceFile ||
			editor.isViewer || !editor.document.isWritable || editor.caretModel.caretCount != 1 || editor.selectionModel.hasSelection() ||
			!VeroScriptProjectSettings.get(file.project).state.bracesOnNewLine) return EnterHandlerDelegate.Result.Continue
		val options = CodeStyle.getIndentOptions(file.project, editor.document)
		val unit = if (options.USE_TAB_CHARACTER) "\t"
		else " ".repeat(options.INDENT_SIZE.coerceIn(0, 32))
		val edit = VeroScriptBlockEnter.plan(editor.document.immutableCharSequence, caretOffset.get(), unit)
			?: return EnterHandlerDelegate.Result.Continue
		editor.document.replaceString(edit.start, edit.end, edit.text)
		editor.caretModel.moveToOffset(edit.caret)
		caretOffset.set(edit.caret)
		caretAdvance.set(0)
		return EnterHandlerDelegate.Result.Stop
	}
}

internal object VeroScriptBlockEnter {
	data class Edit(val start: Int, val end: Int, val text: String, val caret: Int)
	private val closing = Regex("^[ \\t]*(?:}[ \\t]*)?$")
	private val callable = Regex("\\)(?:[ \\t]+(?:const|override|final))*$")
	private val declaration = Regex("^(?:(?:public|private|protected|static|export)[ \\t]+)*(?:class|struct|interface|enum|namespace)\\b")

	fun plan(text: CharSequence, offset: Int, unit: String): Edit? {
		if (offset !in 1..minOf(text.length, 1024 * 1024)) return null
		val start = text.lastIndexOf('\n', offset - 1) + 1
		// Bound the suffix too: an early caret can sit on a very large unfinished line.
		val limit = minOf(text.length, 1024 * 1024)
		var newline = offset
		while (newline < limit && text[newline] != '\n') newline++
		if (newline == limit && limit < text.length) return null
		val end = if (newline > start && text[newline - 1] == '\r') newline - 1
		else newline
		if (offset > end) return null
		val before = text.subSequence(start, offset).toString()
		val after = text.subSequence(offset, end).toString()
		// Split horizontal whitespace directly; a lazy regex header can rescan long gaps quadratically.
		val beforeBrace = before.trimEnd(' ', '\t')
		if (!beforeBrace.endsWith('{')) return null
		val headerText = beforeBrace.dropLast(1).trimEnd(' ', '\t')
		val indent = headerText.takeWhile { it == ' ' || it == '\t' }
		val signature = headerText.drop(indent.length)
		if (signature.isEmpty() || signature.first() in "\r\u000B\u000C" ||
			signature.any { it in "\r\u0085\u2028\u2029" }) return null
		if (!closing.matches(after) || signature.any { it == '{' || it == '}' } ||
			';' in signature && !Regex("^for[ \\t]*\\(").containsMatchIn(signature)) return null
		if (!callable.containsMatchIn(signature) && !declaration.containsMatchIn(signature) &&
			signature !in setOf("else", "do", "try", "finally") &&
			!Regex("^void[ \\t]+[A-Za-z_][A-Za-z0-9_]*$").matches(signature)) return null
		// A continued header's indentation belongs to an earlier line.
		if (signature.startsWith(':')) return null
		var depth = 0
		for (character in signature) {
			if (character == '(') depth++
			else if (character == ')' && --depth < 0) return null
		}
		if (depth != 0) return null
		var directiveStart = start
		while (directiveStart > 0) {
			val previousEnd = directiveStart - 1
			val previousStart = text.lastIndexOf('\n', previousEnd - 1) + 1
			val contentEnd = if (previousEnd > previousStart && text[previousEnd - 1] == '\r') previousEnd - 1
			else previousEnd
			// Spaces after the backslash end a directive instead of continuing it.
			if (contentEnd <= previousStart || text[contentEnd - 1] != '\\') break
			directiveStart = previousStart
		}
		if (text.subSequence(directiveStart, offset).trimStart().startsWith("#")) return null
		val brace = start + before.lastIndexOf('{')
		val lexer = VeroScriptLexer()
		lexer.start(text, 0, brace + 1, 0)
		while (lexer.tokenType != null && lexer.tokenStart < brace) lexer.advance()
		if (lexer.tokenStart != brace || lexer.tokenType != VeroScriptTokens.leftBrace) return null
		val eol = if (newline < text.length && newline > 0 && text[newline - 1] == '\r') "\r\n"
		else "\n"
		val replacementStart = start + indent.length + signature.length
		val prefix = eol + indent + "{" + eol + indent + unit
		return Edit(replacementStart, end, prefix + if (after.isBlank()) ""
		else eol + indent + "}",
			replacementStart + prefix.length)
	}
}
