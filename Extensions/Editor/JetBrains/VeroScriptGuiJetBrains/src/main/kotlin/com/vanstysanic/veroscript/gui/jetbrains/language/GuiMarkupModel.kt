// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiFile
import com.intellij.psi.util.CachedValueProvider
import com.intellij.psi.util.CachedValuesManager

data class GuiAttribute(val name: String, val nameRange: TextRange, val valueRange: TextRange?)
data class GuiTag(val name: String, val nameRange: TextRange, val range: TextRange,
	val attributes: List<GuiAttribute>, val closing: Boolean, val selfClosing: Boolean, val complete: Boolean)

class GuiMarkupModel(val tags: List<GuiTag>, private val opaque: List<TextRange>) {
	fun at(offset: Int) = tags.lastOrNull { offset >= it.range.startOffset && offset <= it.range.endOffset }
	fun isCodeOrComment(offset: Int) = opaque.any { it.containsOffset(offset) }
	fun openTags(offset: Int): List<GuiTag> = buildList {
		for (tag in tags) {
			if (tag.range.endOffset > offset) break
			if (tag.closing) {
				val index = indexOfLast { it.name == tag.name }
				if (index >= 0) while (size > index) removeAt(lastIndex)
			}
			else if (!tag.selfClosing && tag.name.isNotEmpty()) add(tag)
		}
	}

	companion object {
		fun get(file: PsiFile): GuiMarkupModel = CachedValuesManager.getCachedValue(file) {
			CachedValueProvider.Result.create(parse(file.text), file)
		}
		fun parse(text: CharSequence): GuiMarkupModel {
			val lexer = GuiMarkupLexer()
			lexer.start(text)
			val tags = mutableListOf<GuiTag>()
			val opaque = mutableListOf<TextRange>()
			var start = -1
			var name = ""
			var nameRange = TextRange.EMPTY_RANGE
			var closing = false
			var attributes = mutableListOf<GuiAttribute>()
			fun commit(end: Int, selfClosing: Boolean, complete: Boolean = false) {
				if (start >= 0) tags += GuiTag(name, nameRange, TextRange(start, end),
					attributes.toList(), closing, selfClosing, complete)
				start = -1
			}
			var count = 0
			while (lexer.tokenType != null) {
				if (++count % 256 == 0) ProgressManager.checkCanceled()
				val type = lexer.tokenType
				val begin = lexer.tokenStart
				val end = lexer.tokenEnd
				val value = if (type == GuiTokens.tag || type == GuiTokens.attribute || type == GuiTokens.string || type == GuiTokens.delimiter)
					text.subSequence(begin, end).toString()
				else ""
				when {
					type == GuiTokens.script || type == GuiTokens.style || type == GuiTokens.comment || type == GuiTokens.text ->
						opaque.add(TextRange(begin, end))
					type == GuiTokens.delimiter && (value == "<" || value == "</") -> {
						commit(begin, false)
						start = begin
						closing = value == "</"
						name = ""
						nameRange = TextRange(end, end)
						attributes = mutableListOf()
					}
					type == GuiTokens.tag -> {
						name = value
						nameRange = TextRange(begin, end)
					}
					type == GuiTokens.attribute -> attributes.add(GuiAttribute(value, TextRange(begin, end), null))
					type == GuiTokens.string && attributes.isNotEmpty() -> {
						val quoteClosed = value.length > 1 && value.last() == value.first()
						val range = TextRange(begin + 1, if (quoteClosed) end - 1
						else end)
						attributes[attributes.lastIndex] = attributes.last().copy(valueRange = range)
					}
					type == GuiTokens.delimiter && (value == ">" || value == "/>") -> commit(end, value == "/>", true)
				}
				lexer.advance()
			}
			commit(text.length, false)
			return GuiMarkupModel(tags, opaque)
		}
	}
}
