// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.codeInsight.editorActions.SimpleTokenSetQuoteHandler
import com.intellij.codeInsight.editorActions.TypedHandlerDelegate
import com.intellij.codeInsight.AutoPopupController
import com.intellij.lang.BracePair
import com.intellij.lang.Commenter
import com.intellij.lang.PairedBraceMatcher
import com.intellij.lexer.LayeredLexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as Colors
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IElementType
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptLexer
import com.vanstysanic.veroscript.jetbrains.language.VeroScriptSyntaxHighlighter

class GuiSyntaxHighlighter(private val markup: Boolean) : SyntaxHighlighterBase() {
	private val script = VeroScriptSyntaxHighlighter()
	override fun getHighlightingLexer() = if (!markup) GuiStyleLexer()
	else LayeredLexer(GuiMarkupLexer()).apply {
		registerSelfStoppingLayer(VeroScriptLexer(), arrayOf(GuiTokens.script), IElementType.EMPTY_ARRAY)
		registerSelfStoppingLayer(GuiStyleLexer(), arrayOf(GuiTokens.style), IElementType.EMPTY_ARRAY)
	}
	override fun getTokenHighlights(type: IElementType): Array<TextAttributesKey> {
		val color = when (type) {
			GuiTokens.tag, GuiTokens.selector -> Colors.CLASS_NAME
			GuiTokens.attribute, GuiTokens.property -> Colors.INSTANCE_FIELD
			GuiTokens.string, GuiTokens.styleString -> Colors.STRING
			GuiTokens.comment, GuiTokens.styleComment -> Colors.BLOCK_COMMENT
			GuiTokens.number -> Colors.NUMBER
			GuiTokens.value -> Colors.IDENTIFIER
			GuiTokens.entity -> Colors.VALID_STRING_ESCAPE
			GuiTokens.delimiter, GuiTokens.styleDelimiter -> Colors.OPERATION_SIGN
			GuiTokens.leftBrace, GuiTokens.rightBrace -> Colors.BRACES
			GuiTokens.leftParen, GuiTokens.rightParen -> Colors.PARENTHESES
			else -> null
		}
		return if (color != null) pack(color)
		else script.getTokenHighlights(type)
	}
}

class GuiMarkupHighlighterFactory : SyntaxHighlighterFactory() {
	override fun getSyntaxHighlighter(project: Project?, virtualFile: VirtualFile?) = GuiSyntaxHighlighter(true)
}
class GuiStyleHighlighterFactory : SyntaxHighlighterFactory() {
	override fun getSyntaxHighlighter(project: Project?, virtualFile: VirtualFile?) = GuiSyntaxHighlighter(false)
}
class GuiMarkupCommenter : Commenter {
	override fun getLineCommentPrefix(): String? = null
	override fun getBlockCommentPrefix() = "<!--"
	override fun getBlockCommentSuffix() = "-->"
	override fun getCommentedBlockCommentPrefix(): String? = null
	override fun getCommentedBlockCommentSuffix(): String? = null
}
class GuiStyleCommenter : Commenter {
	override fun getLineCommentPrefix(): String? = null
	override fun getBlockCommentPrefix() = "/*"
	override fun getBlockCommentSuffix() = "*/"
	override fun getCommentedBlockCommentPrefix(): String? = null
	override fun getCommentedBlockCommentSuffix(): String? = null
}
class GuiQuoteHandler : SimpleTokenSetQuoteHandler(GuiTokens.string, GuiTokens.styleString)
class GuiBraceMatcher : PairedBraceMatcher {
	override fun getPairs() = arrayOf(BracePair(GuiTokens.leftBrace, GuiTokens.rightBrace, true),
		BracePair(GuiTokens.leftParen, GuiTokens.rightParen, false))
	override fun isPairedBracesAllowedBeforeType(left: IElementType, context: IElementType?) = true
	override fun getCodeConstructStart(file: PsiFile, openingBraceOffset: Int) = openingBraceOffset
}

class GuiTypedHandler : TypedHandlerDelegate() {
	override fun checkAutoPopup(char: Char, project: Project, editor: Editor, file: PsiFile): Result {
		if (file.fileType == GuiMarkupFileType && char in "< :" && editor.caretModel.caretCount == 1 &&
			editor.document.textLength <= 1024 * 1024) {
			val offset = editor.caretModel.offset
			val model = GuiMarkupModel.parse(editor.document.immutableCharSequence)
			val tag = model.at(offset)
			if (tag != null && !model.isCodeOrComment((offset - 1).coerceAtLeast(0)) &&
				tag.attributes.none { it.valueRange?.let { range -> offset in range.startOffset..range.endOffset } == true }) {
				AutoPopupController.getInstance(project).autoPopupMemberLookup(editor, null)
				return Result.STOP
			}
		}
		return Result.CONTINUE
	}

	override fun charTyped(char: Char, project: Project, editor: Editor, file: PsiFile): Result {
		if (file.fileType != GuiMarkupFileType || char != '>' || editor.caretModel.caretCount != 1 ||
			editor.document.textLength > 1024 * 1024 ||
			!com.intellij.codeInsight.CodeInsightSettings.getInstance().AUTOINSERT_PAIR_BRACKET) return Result.CONTINUE
		val offset = editor.caretModel.offset
		val text = editor.document.immutableCharSequence
		val closing = GuiTagClosing.suffix(text, offset) ?: return Result.CONTINUE
		editor.document.insertString(offset, closing)
		editor.caretModel.moveToOffset(offset)
		return Result.CONTINUE
	}
}

object GuiTagClosing {
	fun suffix(text: CharSequence, offset: Int): String? {
		val model = GuiMarkupModel.parse(text)
		val tag = model.tags.lastOrNull { it.range.endOffset == offset } ?: return null
		if (!tag.complete || tag.closing || tag.selfClosing || tag.name.isBlank()) return null
		// Count same-name nesting so a child's closing tag cannot stand in for ours.
		var depth = 1
		for (next in model.tags) {
			if (next.range.startOffset < offset || next.name != tag.name) continue
			if (next.closing) {
				depth--
				if (depth == 0) return null
			}
			else if (!next.selfClosing)
			{
				depth++
			}
		}
		return "</${tag.name}>"
	}
}

class GuiCompletionPreselection : com.jetbrains.rider.completion.RiderFrontendLanguagesPreselectionStrategy {
	override fun shouldPreselectFirstSuggestion(parameters: com.intellij.codeInsight.completion.CompletionParameters) =
		parameters.originalFile.fileType == GuiMarkupFileType || parameters.originalFile.fileType == GuiStyleFileType
}
