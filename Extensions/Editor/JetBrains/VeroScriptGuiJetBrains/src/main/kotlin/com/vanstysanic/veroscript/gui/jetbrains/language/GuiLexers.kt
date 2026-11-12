// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.lexer.LexerBase
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

object GuiTokens {
	val tag = IElementType("GUI_TAG", GuiMarkupLanguage)
	val attribute = IElementType("GUI_ATTRIBUTE", GuiMarkupLanguage)
	val delimiter = IElementType("GUI_DELIMITER", GuiMarkupLanguage)
	val string = IElementType("GUI_STRING", GuiMarkupLanguage)
	val literal = IElementType("GUI_LITERAL", GuiMarkupLanguage)
	val comment = IElementType("GUI_COMMENT", GuiMarkupLanguage)
	val text = IElementType("GUI_TEXT", GuiMarkupLanguage)
	val entity = IElementType("GUI_ENTITY", GuiMarkupLanguage)
	val script = IElementType("GUI_SCRIPT", GuiMarkupLanguage)
	val style = IElementType("GUI_STYLE", GuiMarkupLanguage)
	val styleString = IElementType("GUI_STYLE_STRING", GuiStyleLanguage)
	val styleComment = IElementType("GUI_STYLE_COMMENT", GuiStyleLanguage)
	val styleDelimiter = IElementType("GUI_STYLE_DELIMITER", GuiStyleLanguage)
	val selector = IElementType("GUI_SELECTOR", GuiStyleLanguage)
	val property = IElementType("GUI_PROPERTY", GuiStyleLanguage)
	val value = IElementType("GUI_VALUE", GuiStyleLanguage)
	val number = IElementType("GUI_NUMBER", GuiStyleLanguage)
	val leftBrace = IElementType("GUI_LEFT_BRACE", GuiStyleLanguage)
	val rightBrace = IElementType("GUI_RIGHT_BRACE", GuiStyleLanguage)
	val leftParen = IElementType("GUI_LEFT_PAREN", GuiStyleLanguage)
	val rightParen = IElementType("GUI_RIGHT_PAREN", GuiStyleLanguage)
}

abstract class GuiLexer : LexerBase() {
	protected var source: CharSequence = ""
	protected var limit = 0
	protected var begin = 0
	protected var finish = 0
	protected var mode = 0
	private var tokenState = 0
	protected var type: IElementType? = null

	final override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
		source = buffer
		limit = endOffset
		begin = startOffset
		finish = startOffset
		mode = initialState
		advance()
	}
	final override fun getState() = tokenState
	final override fun getTokenType() = type
	final override fun getTokenStart() = begin
	final override fun getTokenEnd() = finish
	final override fun getBufferSequence() = source
	final override fun getBufferEnd() = limit
	final override fun advance() {
		begin = finish
		tokenState = mode
		type = null
		if (begin >= limit) return
		read()
		check(finish > begin && finish <= limit)
	}
	protected abstract fun read()
	protected fun at(value: String, offset: Int = begin) =
		offset + value.length <= limit && source.regionMatches(offset, value, 0, value.length)
	protected fun find(value: String, start: Int): Int {
		// Layered highlighting shares the whole file buffer; search only this layer.
		var offset = start
		val last = limit - value.length
		while (offset <= last) {
			if (source[offset] == value[0] && source.regionMatches(offset, value, 0, value.length)) return offset
			offset++
		}
		return -1
	}
	protected fun through(value: String, start: Int) = find(value, start).let {
		if (it < 0) limit
		else it + value.length
	}
	protected fun quoted(): Int {
		val quote = source[begin]
		var offset = begin + 1
		while (offset < limit && source[offset] != quote) offset++
		return if (offset < limit) offset + 1
		else limit
	}
	protected fun whitespace(): Boolean {
		if (!source[begin].isWhitespace()) return false
		finish = begin + 1
		while (finish < limit && source[finish].isWhitespace()) finish++
		type = TokenType.WHITE_SPACE
		return true
	}
	companion object {
		fun namePart(char: Char) = char.isLetterOrDigit() || char in "_:-."
		fun styleNamePart(char: Char) = char.isLetterOrDigit() || char in "_-"
	}
}

class GuiMarkupLexer : GuiLexer() {
	override fun read() {
		val state = mode and 15
		if (state in 3..6) {
			val script = state == 3 || state == 5
			val cdata = state >= 5
			val close = if (script) "</script"
			else "</style"
			if (cdata && at("]]>"))
			{
				finish = begin + 3
				type = GuiTokens.delimiter
				mode = if (script) 3
				else 4
				return
			}
			if (!cdata && at("<![CDATA[")) {
				finish = begin + 9
				type = GuiTokens.delimiter
				mode = if (script) 5
				else 6
				return
			}
			if (!cdata && at(close)) mode = 0
			else
			{
				// Stop at the first boundary instead of rescanning the remaining file
				// for each raw/CDATA segment in a document with many inline blocks.
				finish = begin + 1
				while (finish < limit) {
					val boundary = if (cdata) at("]]>", finish)
					else at(close, finish) || at("<![CDATA[", finish)
					if (boundary) break
					finish++
				}
				type = if (script) GuiTokens.script
				else GuiTokens.style
				return
			}
		}
		if (whitespace()) return
		if (at("<?")) {
			finish = through("?>", begin + 2)
			type = GuiTokens.comment
			return
		}
		if (at("<!--")) {
			finish = through("-->", begin + 4)
			type = GuiTokens.comment
			return
		}
		if (at("<![CDATA[")) {
			finish = through("]]>", begin + 9)
			type = GuiTokens.text
			return
		}
		finish = begin + 1
		if (mode and 15 == 0) {
			if (source[begin] == '<') {
				val closing = at("</")
				if (closing) finish++
				mode = 1 or (if (closing) 256
				else 0)
				type = GuiTokens.delimiter
			}
			else if (source[begin] == '&')
			{
				while (finish < limit && source[finish] !in ";<\r\n \t") finish++
				if (finish < limit && source[finish] == ';') finish++
				type = GuiTokens.entity
			}
			else
			{
				while (finish < limit && source[finish] != '<' && source[finish] != '&') finish++
				type = GuiTokens.text
			}
			return
		}
		when {
			at("/>") -> {
				finish++
				mode = 0
				type = GuiTokens.delimiter
			}
			source[begin] == '>' -> {
				mode = if (mode and 256 != 0) 0
				else (mode shr 4) and 15
				type = GuiTokens.delimiter
			}
			source[begin] == '"' || source[begin] == '\'' -> {
				finish = quoted()
				type = GuiTokens.string
			}
			namePart(source[begin]) -> {
				while (finish < limit && namePart(source[finish])) finish++
				type = if (mode and 15 == 1) GuiTokens.tag
				else GuiTokens.attribute
				if (mode and 15 == 1) {
					val raw = when (source.subSequence(begin, finish).toString()) {
						"script" -> 3
						"style" -> 4
						else -> 0
					}
					mode = 2 or (raw shl 4) or (mode and 256)
				}
			}
			else -> type = GuiTokens.delimiter
		}
	}
}

class GuiStyleLexer : GuiLexer() {
	override fun read() {
		if (whitespace()) return
		finish = begin + 1
		if (at("/*")) {
			finish = through("*/", begin + 2)
			type = GuiTokens.styleComment
			return
		}
		val state = mode and 3
		when (source[begin]) {
			'{' -> {
				mode = 1
				type = GuiTokens.leftBrace
			}
			'}' -> {
				mode = 0
				type = GuiTokens.rightBrace
			}
			'(' -> {
				mode += 4
				type = GuiTokens.leftParen
			}
			')' -> {
				mode = maxOf(0, mode - 4)
				type = GuiTokens.rightParen
			}
			':' -> {
				if (state == 1) mode = (mode and 3.inv()) or 2
				type = GuiTokens.styleDelimiter
			}
			';' -> {
				if (mode shr 2 == 0 && state != 0) mode = 1
				type = GuiTokens.styleDelimiter
			}
			'"', '\'' -> {
				// Unlike ML literals, style strings accept escaped quotes.
				while (finish < limit) {
					val char = source[finish++]
					if (char == '\\' && finish < limit) finish++
					else if (char == source[begin]) break
				}
				type = GuiTokens.styleString
			}
			else -> {
				val char = source[begin]
				if (char.isLetterOrDigit() || char in "_-#.%") {
					while (finish < limit && (namePart(source[finish]) && source[finish] != ':' || source[finish] in "#%")) finish++
					type = when {
						state == 0 -> GuiTokens.selector
						state == 1 -> GuiTokens.property
						char.isDigit() || char == '#' -> GuiTokens.number
						else -> GuiTokens.value
					}
				}
				else type = GuiTokens.styleDelimiter
			}
		}
	}
}
