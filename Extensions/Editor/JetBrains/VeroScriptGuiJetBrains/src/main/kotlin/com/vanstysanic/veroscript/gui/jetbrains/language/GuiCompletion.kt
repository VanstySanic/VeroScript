// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.codeInsight.completion.*
import com.intellij.codeInsight.lookup.LookupElementBuilder
import com.intellij.openapi.project.DumbAware
import com.intellij.patterns.PlatformPatterns
import com.intellij.util.ProcessingContext
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import com.vanstysanic.veroscript.gui.jetbrains.GuiIcons

class GuiCompletion : CompletionContributor(), DumbAware {
	override fun beforeCompletion(context: CompletionInitializationContext) {
		val offset = context.offsetMap.getOffset(CompletionInitializationContext.SELECTION_END_OFFSET)
		val text = context.editor.document.immutableCharSequence
		if (offset >= text.length) return
		val token = context.file.findElementAt(offset)?.node?.elementType
		val markup = context.file.fileType == GuiMarkupFileType
		val name = if (markup) token == GuiTokens.tag || token == GuiTokens.attribute
		else context.file.fileType == GuiStyleFileType && token in setOf(GuiTokens.property, GuiTokens.selector, GuiTokens.value)
		if (!name) return
		var end = offset
		while (end < text.length && (if (markup) GuiLexer.namePart(text[end])
		else GuiLexer.styleNamePart(text[end]))) end++
		context.offsetMap.addOffset(CompletionInitializationContext.IDENTIFIER_END_OFFSET, end)
	}

	init {
		extend(CompletionType.BASIC, PlatformPatterns.psiElement().withLanguage(GuiMarkupLanguage), object : CompletionProvider<CompletionParameters>() {
			override fun addCompletions(parameters: CompletionParameters, context: ProcessingContext, result: CompletionResultSet) {
				markup(parameters, result)
			}
		})
		extend(CompletionType.BASIC, PlatformPatterns.psiElement().withLanguage(GuiStyleLanguage), object : CompletionProvider<CompletionParameters>() {
			override fun addCompletions(parameters: CompletionParameters, context: ProcessingContext, result: CompletionResultSet) {
				style(parameters, result)
			}
		})
	}

	private fun markup(parameters: CompletionParameters, result: CompletionResultSet) {
		val file = parameters.position.containingFile
		if (file.fileType != GuiMarkupFileType) return
		val offset = parameters.offset
		val model = GuiMarkupModel.get(file)
		if (model.isCodeOrComment((offset - 1).coerceAtLeast(0))) return
		val tag = model.at(offset) ?: return
		val text = file.text
		if (offset == tag.range.endOffset && offset > 0 && text[offset - 1] == '>') return
		var begin = offset
		while (begin > tag.range.startOffset && GuiLexer.namePart(text[begin - 1])) begin--
		val target = result.withPrefixMatcher(text.substring(begin, offset))
		if (tag.name.isEmpty() || begin <= tag.nameRange.endOffset) {
			val names = if (tag.closing) model.openTags(tag.range.startOffset).asReversed().map { it.name }.distinct()
			else GuiCatalog.types.flatMap { listOf(it.name, it.mark) }.distinct() + GuiCatalog.directives.keys
			names.forEach { target.addElement(item(it, message("completion.tag"))) }
			return
		}
		if (tag.closing) return
		val attribute = tag.attributes.firstOrNull {
			it.valueRange?.let { range -> offset in range.startOffset..range.endOffset } == true
		}
		val type = GuiCatalog.find(tag.name)
		if (attribute != null) {
			val property = type?.let { GuiCatalog.properties(it) }?.firstOrNull { attribute.name == it.name || attribute.name == it.mark }
			if (property?.type == "bool") listOf("true", "false").forEach { target.addElement(item(it, "bool")) }
			return
		}
		val editing = tag.attributes.firstOrNull { begin in it.nameRange.startOffset..it.nameRange.endOffset }
		val used = tag.attributes.filter { it != editing }.map { it.name }.toSet()
		fun add(name: String, description: String) {
			if (name in used) return
			target.addElement(item(name, description).withInsertHandler { context, _ ->
				if (context.completionChar == '=') context.setAddCompletionChar(false)
				var end = context.tailOffset
				val document = context.document
				while (end < document.textLength && document.charsSequence[end].isWhitespace()) end++
				if (end >= document.textLength || document.charsSequence[end] != '=') {
					val tail = context.tailOffset
					document.insertString(tail, "=\"\"")
					context.editor.caretModel.moveToOffset(tail + 2)
				}
				else if (context.completionChar == '=')
				{
					var value = end + 1
					while (value < document.textLength && document.charsSequence[value].isWhitespace()) value++
					if (value < document.textLength && document.charsSequence[value] in "\"'") value++
					context.editor.caretModel.moveToOffset(value)
				}
			})
		}
		GuiCatalog.directives[tag.name]?.forEach { add(it, message("completion.directive")) }
		if (tag.name !in GuiCatalog.directives) listOf("id", "class").forEach { add(it, "string") }
		if (type == null) return
		GuiCatalog.properties(type).forEach { property ->
			if (used.none { it.removePrefix("bind:") == property.name || it.removePrefix("bind:") == property.mark }) {
				listOf(property.name, property.mark).distinct().forEach {
					add(it, property.type)
					if (property.bindable) add("bind:$it", message("completion.binding", property.type))
				}
			}
		}
		GuiCatalog.events(type).forEach { event ->
			if ("on:${event.name}" !in used && "on:${event.mark}" !in used) {
				listOf(event.name, event.mark).distinct().forEach { add("on:$it", message("completion.event")) }
			}
		}
	}

	private fun style(parameters: CompletionParameters, result: CompletionResultSet) {
		val file = parameters.position.containingFile
		if (file.fileType != GuiStyleFileType) return
		val text = file.text
		val offset = parameters.offset
		val lexer = GuiStyleLexer()
		lexer.start(text)
		var tokens = 0
		while (lexer.tokenType != null && lexer.tokenEnd <= offset) {
			if (++tokens % 256 == 0) com.intellij.openapi.progress.ProgressManager.checkCanceled()
			lexer.advance()
		}
		if (lexer.tokenType == GuiTokens.styleComment || lexer.tokenType == GuiTokens.styleString) return
		var begin = offset
		while (begin > 0 && GuiLexer.styleNamePart(text[begin - 1])) begin--
		val target = result.withPrefixMatcher(text.substring(begin, offset))
		when (lexer.state and 3) {
			0 -> {
				if (begin > 0 && text[begin - 1] == ':') GuiCatalog.states.forEach { target.addElement(item(it, message("completion.state"))) }
				else GuiCatalog.types.flatMap { listOf(it.name, it.mark) }.distinct().forEach { target.addElement(item(it, message("completion.tag"))) }
			}
			1 -> GuiCatalog.styleProperties.forEach { property ->
				target.addElement(item(property.mark, property.type).withInsertHandler { context, _ ->
					if (context.completionChar == ':') context.setAddCompletionChar(false)
					var end = context.tailOffset
					while (end < context.document.textLength && context.document.charsSequence[end].isWhitespace()) end++
					if (end >= context.document.textLength || context.document.charsSequence[end] != ':') {
						val tail = context.tailOffset
						context.document.insertString(tail, ": ")
						context.editor.caretModel.moveToOffset(tail + 2)
					}
					else if (context.completionChar == ':')
					{
						var value = end + 1
						while (value < context.document.textLength && context.document.charsSequence[value].isWhitespace()) value++
						context.editor.caretModel.moveToOffset(value)
					}
				})
			}
			2 -> {
				listOf("var", "url", "rgb", "rgba").forEach { target.addElement(item(it, message("completion.value"))) }
				Regex("--[A-Za-z_][A-Za-z0-9_-]*(?=\\s*:)").findAll(text).map { it.value }.distinct().forEach {
					target.addElement(item(it, message("completion.variable")))
				}
			}
		}
	}

	private fun item(name: String, description: String) = LookupElementBuilder.create(name).withCaseSensitivity(true)
		.withIcon(GuiIcons.file).withTypeText(description, true)
}
