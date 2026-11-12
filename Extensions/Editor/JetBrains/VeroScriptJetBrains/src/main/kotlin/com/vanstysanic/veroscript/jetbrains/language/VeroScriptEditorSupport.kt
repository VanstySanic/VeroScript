// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.language

import com.intellij.extapi.psi.PsiFileBase
import com.intellij.lang.ASTNode
import com.intellij.lang.BracePair
import com.intellij.lang.Commenter
import com.intellij.lang.PairedBraceMatcher
import com.intellij.lang.ParserDefinition
import com.intellij.lang.PsiBuilder
import com.intellij.lang.PsiParser
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as Colors
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.extapi.psi.ASTWrapperPsiElement
import com.intellij.psi.tree.IElementType
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet

object VeroScriptTextAttributes {
	val keyword = TextAttributesKey.createTextAttributesKey("VEROSCRIPT_KEYWORD", Colors.KEYWORD)
	val number = TextAttributesKey.createTextAttributesKey("VEROSCRIPT_NUMBER", Colors.NUMBER)
	val string = TextAttributesKey.createTextAttributesKey("VEROSCRIPT_STRING", Colors.STRING)
	val comment = TextAttributesKey.createTextAttributesKey("VEROSCRIPT_COMMENT", Colors.LINE_COMMENT)
	val metadata = TextAttributesKey.createTextAttributesKey("VEROSCRIPT_METADATA", Colors.METADATA)
}

class VeroScriptParserDefinition : ParserDefinition {
	override fun createLexer(project: Project?) = VeroScriptLexer()
	override fun createParser(project: Project?) = PsiParser { root, builder -> parse(root, builder) }
	override fun getFileNodeType() = fileNode
	override fun getCommentTokens() = TokenSet.create(VeroScriptTokens.lineComment, VeroScriptTokens.blockComment)
	override fun getStringLiteralElements() = TokenSet.create(VeroScriptTokens.string)
	override fun createElement(node: ASTNode): PsiElement = ASTWrapperPsiElement(node)
	override fun createFile(viewProvider: FileViewProvider): PsiFile = object : PsiFileBase(viewProvider, VeroScriptLanguage) {
		override fun getFileType() = VeroScriptFileType
	}

	private fun parse(root: IElementType, builder: PsiBuilder): ASTNode {
		val marker = builder.mark()
		while (!builder.eof()) builder.advanceLexer()
		marker.done(root)
		return builder.treeBuilt
	}

	companion object {
		private val fileNode = IFileElementType(VeroScriptLanguage)
	}
}

class VeroScriptSyntaxHighlighter(private val documentation: Boolean = false) : SyntaxHighlighterBase() {
	override fun getHighlightingLexer() = if (documentation) VeroScriptDocumentationLexer()
	else VeroScriptLexer()
	override fun getTokenHighlights(type: IElementType): Array<TextAttributesKey> = pack(when (type) {
		VeroScriptDocumentationTokens.qualifier, VeroScriptDocumentationTokens.type -> Colors.CLASS_NAME
		VeroScriptDocumentationTokens.function -> Colors.FUNCTION_DECLARATION
		VeroScriptTokens.keyword -> VeroScriptTextAttributes.keyword
		VeroScriptTokens.number -> VeroScriptTextAttributes.number
		VeroScriptTokens.string -> VeroScriptTextAttributes.string
		VeroScriptTokens.lineComment, VeroScriptTokens.blockComment -> VeroScriptTextAttributes.comment
		VeroScriptTokens.preprocessor -> VeroScriptTextAttributes.metadata
		VeroScriptTokens.leftParen, VeroScriptTokens.rightParen -> Colors.PARENTHESES
		VeroScriptTokens.leftBrace, VeroScriptTokens.rightBrace -> Colors.BRACES
		VeroScriptTokens.leftBracket, VeroScriptTokens.rightBracket -> Colors.BRACKETS
		VeroScriptTokens.operator -> Colors.OPERATION_SIGN
		else -> null
	})
}

class VeroScriptSyntaxHighlighterFactory : SyntaxHighlighterFactory() {
	// The SDK renders documentation in temporary PSI files, without LSP semantic tokens.
	override fun getSyntaxHighlighter(project: Project?, virtualFile: VirtualFile?) =
		VeroScriptSyntaxHighlighter(documentation = virtualFile == null || virtualFile is com.intellij.testFramework.LightVirtualFile)
}

class VeroScriptCommenter : Commenter {
	override fun getLineCommentPrefix() = "//"
	override fun getBlockCommentPrefix() = "/*"
	override fun getBlockCommentSuffix() = "*/"
	override fun getCommentedBlockCommentPrefix(): String? = null
	override fun getCommentedBlockCommentSuffix(): String? = null
}

class VeroScriptBraceMatcher : PairedBraceMatcher {
	override fun getPairs() = arrayOf(
		BracePair(VeroScriptTokens.leftParen, VeroScriptTokens.rightParen, false),
		BracePair(VeroScriptTokens.leftBracket, VeroScriptTokens.rightBracket, false),
		BracePair(VeroScriptTokens.leftBrace, VeroScriptTokens.rightBrace, true)
	)
	override fun isPairedBracesAllowedBeforeType(left: IElementType, context: IElementType?) =
		context == null || context == com.intellij.psi.TokenType.WHITE_SPACE ||
			context == VeroScriptTokens.lineComment || context == VeroScriptTokens.blockComment ||
			context == VeroScriptTokens.rightParen || context == VeroScriptTokens.rightBrace ||
			context == VeroScriptTokens.rightBracket || context == VeroScriptTokens.operator
	override fun getCodeConstructStart(file: PsiFile, openingBraceOffset: Int) = openingBraceOffset
}
