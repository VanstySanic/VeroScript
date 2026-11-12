// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.intellij.patterns.PlatformPatterns
import com.intellij.extapi.psi.ASTWrapperPsiElement
import com.intellij.lang.ASTNode
import com.intellij.model.psi.PsiExternalReferenceHost
import com.intellij.openapi.util.TextRange
import com.intellij.openapi.util.SystemInfo
import com.intellij.openapi.vfs.LocalFileSystem
import com.intellij.psi.*
import com.intellij.psi.impl.source.resolve.reference.ReferenceProvidersRegistry
import com.intellij.psi.impl.source.resolve.reference.impl.providers.FileReference
import com.intellij.psi.impl.source.resolve.reference.impl.providers.FileReferenceSet
import com.intellij.util.ProcessingContext
import com.intellij.util.IncorrectOperationException
import com.intellij.psi.util.PsiTreeUtil
import com.vanstysanic.veroscript.gui.jetbrains.GuiBundle.message
import java.nio.file.Path
import java.text.Normalizer

/** Composite literals expose provider references instead of relying on raw token leaves. */
class GuiStringLiteral(node: ASTNode) : ASTWrapperPsiElement(node), PsiExternalReferenceHost {
	override fun getReferences(): Array<PsiReference> = ReferenceProvidersRegistry.getReferencesFromProviders(this)
	override fun getReference(): PsiReference? = getReferences().firstOrNull()
}

/** File references delegate rename/move edits to the literal's registered manipulator. */
class GuiStringLiteralManipulator : AbstractElementManipulator<GuiStringLiteral>() {
	override fun getRangeInElement(element: GuiStringLiteral): TextRange {
		val text = element.text
		val closed = text.length > 1 && text.last() == text.first()
		return TextRange(1, if (closed) text.length - 1
		else text.length)
	}

	override fun handleContentChange(element: GuiStringLiteral, range: TextRange, newContent: String): GuiStringLiteral {
		if (!getRangeInElement(element).contains(range)) throw IncorrectOperationException(message("error.pathEdit"))
		val encoded = newContent.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
			.replace("\"", "&quot;").replace("'", "&apos;")
		val text = element.text.replaceRange(range.startOffset, range.endOffset, encoded)
		// No suffix is appended: an unfinished quote must not absorb synthetic markup.
		val file = PsiFileFactory.getInstance(element.project).createFileFromText("path.vswml", GuiMarkupFileType, "<import src=$text")
		val literal = PsiTreeUtil.findChildOfType(file, GuiStringLiteral::class.java)
			?: throw IncorrectOperationException(message("error.pathEdit"))
		return element.replace(literal) as GuiStringLiteral
	}
}

class GuiReferences : PsiReferenceContributor() {
	override fun registerReferenceProviders(registrar: PsiReferenceRegistrar) {
		registrar.registerReferenceProvider(PlatformPatterns.psiElement(GuiStringLiteral::class.java).withLanguage(GuiMarkupLanguage), object : PsiReferenceProvider() {
			override fun getReferencesByElement(element: PsiElement, context: ProcessingContext): Array<PsiReference> {
				val file = element.containingFile
				val offset = element.textRange.startOffset
				val tag = GuiMarkupModel.get(file).at(offset) ?: return PsiReference.EMPTY_ARRAY
				val attribute = tag.attributes.firstOrNull { it.valueRange?.startOffset == offset + 1 }
				if (attribute?.name != "src" || tag.name !in setOf("import", "style", "script")) return PsiReference.EMPTY_ARRAY
				val raw = element.text
				val value = raw.substring(1, if (raw.length > 1 && raw.last() == raw.first()) raw.length - 1
				else raw.length)
				// A host reader may use opaque resource identities; filesystem navigation is soft.
				// Match both SDK constructor trim() and reparse Character.isWhitespace(), not Kotlin's broader space set.
				if (value.isBlank() || value != value.trim { it <= ' ' || Character.isWhitespace(it) } || '&' in value || Regex("^[A-Za-z][A-Za-z0-9+.-]*:").containsMatchIn(value) &&
					!Regex("^[A-Za-z]:[/\\\\]").containsMatchIn(value)) return PsiReference.EMPTY_ARRAY
				// Preserve literal backslashes on POSIX; only Windows treats them as separators.
				val normalized = value.replace(java.io.File.separatorChar, '/')
				val root = runCatching { Path.of(value).takeIf { it.isAbsolute }?.root }.getOrNull()
				val caseSensitive = if (root == null) file.virtualFile?.fileSystem?.isCaseSensitive ?: true
				else LocalFileSystem.getInstance().isCaseSensitive
				val prefix = root?.toString()?.replace(java.io.File.separatorChar, '/')?.trimEnd('/').orEmpty()
				if (!normalized.startsWith(prefix, ignoreCase = !caseSensitive)) return PsiReference.EMPTY_ARRAY
				// SDK absolute references default to project roots and do not recognize drive names.
				// Keep the literal offsets while resolving native absolute paths from the OS root.
				return object : FileReferenceSet(normalized.substring(prefix.length), element, 1 + prefix.length, this, caseSensitive) {
					override fun isSoft() = true
					override fun computeDefaultContexts(): Collection<PsiFileSystemItem> {
						if (root == null) return super.computeDefaultContexts()
						val directory = LocalFileSystem.getInstance().findFileByNioFile(root) ?: return emptyList()
						val context = PsiManager.getInstance(element.project).findDirectory(directory) ?: return emptyList()
						return listOf(context)
					}
					// SDK absolute/relative conversion uses project roots, not native filesystem roots.
					override fun couldBeConvertedTo(absolute: Boolean) = false
					override fun createFileReference(range: TextRange, index: Int, text: String): FileReference {
						return object : FileReference(this, range, index, text) {
							// Keep native filename characters intact, including '?', and retain the SDK's Mac normalization.
							override fun decode(text: String): String = if (SystemInfo.isMac) Normalizer.normalize(text, Normalizer.Form.NFC)
							else text
							override fun bindToElement(target: PsiElement, absolute: Boolean): PsiElement {
								if (root == null) return super.bindToElement(target, absolute)
								val destination = (target as? PsiFileSystemItem)?.virtualFile
									?.takeIf { it.fileSystem.protocol == "file" }
									?: throw IncorrectOperationException(message("error.pathEdit"))
								// Replace the native root too; the SDK otherwise rebases against module roots.
								return ElementManipulators.handleContentChange(this.element, TextRange(1, this.rangeInElement.endOffset), destination.path)
							}
						}
					}
				}.allReferences.map { it as PsiReference }.toTypedArray()
			}
		})
	}
}
