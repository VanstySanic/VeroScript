// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.model.Pointer
import com.intellij.openapi.util.text.StringUtil
import com.intellij.platform.backend.documentation.DocumentationResult
import com.intellij.platform.backend.documentation.DocumentationTarget
import com.intellij.platform.backend.documentation.DocumentationTargetProvider
import com.intellij.platform.backend.presentation.TargetPresentation
import com.intellij.psi.PsiFile
import com.intellij.psi.SmartPointerManager
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import com.vanstysanic.veroscript.gui.jetbrains.GuiIcons

class GuiDocumentation : DocumentationTargetProvider {
	override fun documentationTargets(file: PsiFile, offset: Int): List<DocumentationTarget> {
		if (!file.isValid || file.project.isDisposed) return emptyList()
		if (file.fileType != GuiMarkupFileType && file.fileType != GuiStyleFileType) return emptyList()
		var name = ""
		var detail = ""
		if (file.fileType == GuiMarkupFileType) {
			val tag = GuiMarkupModel.get(file).at(offset) ?: return emptyList()
			val type = GuiCatalog.find(tag.name) ?: return emptyList()
			val attribute = tag.attributes.firstOrNull { it.nameRange.containsOffset(offset) }
			if (attribute == null && tag.nameRange.containsOffset(offset)) {
				name = type.name
				detail = message("doc.widget", type.qualifiedName, type.mark)
			}
			else if (attribute != null)
			{
				val propertyName = attribute.name.removePrefix("bind:")
				val property = GuiCatalog.properties(type).firstOrNull { propertyName == it.name || propertyName == it.mark }
				val event = GuiCatalog.events(type).firstOrNull { attribute.name == "on:${it.name}" || attribute.name == "on:${it.mark}" }
				if (property != null) {
					name = "${type.name}.${property.name}"
					detail = property.type + " · " + message(if (property.bindable) "doc.bindable"
					else "doc.literal") +
						" · " + message(if (property.style) "doc.style"
						else "doc.local")
				}
				else if (event != null)
				{
					name = "${type.name}.${event.name}"
					detail = message("doc.event", "on:${event.mark}")
				}
			}
		}
		else
		{
			val element = file.findElementAt(offset) ?: return emptyList()
			if (element.node?.elementType != GuiTokens.property) return emptyList()
			val word = element.text
			val property = GuiCatalog.styleProperties.firstOrNull { word == it.mark } ?: return emptyList()
			name = property.mark
			detail = property.type + " · " + message("doc.style")
		}
		if (name.isEmpty()) return emptyList()
		val pointer = SmartPointerManager.createPointer(file)
		val stamp = file.modificationStamp
		fun isCurrent() = pointer.dereference()?.let { it.isValid && !it.project.isDisposed && it.modificationStamp == stamp } == true
		val title = name
		val html = "<div class='definition'><pre>" + StringUtil.escapeXmlEntities(title) + "</pre></div><div class='content'>" +
			StringUtil.escapeXmlEntities(detail) + "<p>" + StringUtil.escapeXmlEntities(message("doc.catalog")) + "</p></div>"
		return listOf(object : DocumentationTarget {
			override fun createPointer(): Pointer<DocumentationTarget> = Pointer {
				if (isCurrent()) this
				else null
			}
			override fun computePresentation() = TargetPresentation.builder(title).icon(GuiIcons.file).presentation()
			override fun computeDocumentation(): DocumentationResult? = if (isCurrent()) DocumentationResult.documentation(html)
			else null
		})
	}
}
