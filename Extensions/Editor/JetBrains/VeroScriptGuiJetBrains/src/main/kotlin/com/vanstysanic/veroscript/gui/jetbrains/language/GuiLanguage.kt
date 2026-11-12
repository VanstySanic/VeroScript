// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.extapi.psi.ASTWrapperPsiElement
import com.intellij.extapi.psi.PsiFileBase
import com.intellij.lang.*
import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.openapi.project.Project
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import com.vanstysanic.veroscript.gui.jetbrains.GuiIcons

object GuiMarkupLanguage : Language("VeroScriptGuiMarkup")
object GuiStyleLanguage : Language("VeroScriptGuiStyle")

object GuiMarkupFileType : LanguageFileType(GuiMarkupLanguage) {
	override fun getName() = "VeroScript GUI Markup"
	override fun getDescription() = message("file.markup")
	override fun getDefaultExtension() = "vswml"
	override fun getIcon() = GuiIcons.file
}

object GuiStyleFileType : LanguageFileType(GuiStyleLanguage) {
	override fun getName() = "VeroScript GUI Style"
	override fun getDescription() = message("file.style")
	override fun getDefaultExtension() = "vswcss"
	override fun getIcon() = GuiIcons.file
}

/** Native GUI tools own syntax validation, including custom widget schemas. */
abstract class GuiParserDefinition(private val language: Language) : ParserDefinition {
	private val fileNode = IFileElementType(language)
	override fun createParser(project: Project?) = PsiParser { root, builder ->
		val marker = builder.mark()
		while (!builder.eof()) {
			if (language == GuiMarkupLanguage && builder.tokenType == GuiTokens.string) {
				val literal = builder.mark()
				builder.advanceLexer()
				literal.done(GuiTokens.literal)
			}
			else builder.advanceLexer()
		}
		marker.done(root)
		builder.treeBuilt
	}
	override fun getFileNodeType() = fileNode
	override fun getCommentTokens() = TokenSet.create(if (language == GuiMarkupLanguage) GuiTokens.comment
	else GuiTokens.styleComment)
	override fun getStringLiteralElements() = TokenSet.create(if (language == GuiMarkupLanguage) GuiTokens.string
	else GuiTokens.styleString)
	override fun createElement(node: ASTNode): PsiElement = if (node.elementType == GuiTokens.literal) GuiStringLiteral(node)
	else ASTWrapperPsiElement(node)
	override fun createFile(viewProvider: FileViewProvider) = object : PsiFileBase(viewProvider, language) {
		override fun getFileType() = if (language == GuiMarkupLanguage) GuiMarkupFileType
		else GuiStyleFileType
	}
}

class GuiMarkupParserDefinition : GuiParserDefinition(GuiMarkupLanguage) {
	override fun createLexer(project: Project?) = GuiMarkupLexer()
}

class GuiStyleParserDefinition : GuiParserDefinition(GuiStyleLanguage) {
	override fun createLexer(project: Project?) = GuiStyleLexer()
}
