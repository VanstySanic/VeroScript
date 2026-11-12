// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.run

import com.intellij.execution.filters.Filter
import com.intellij.execution.filters.OpenFileHyperlinkInfo
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.LocalFileSystem
import java.nio.file.Path

/** Native ML columns count Unicode code points; line-only links avoid invalid UTF-16 offsets. */
class GuiDiagnosticFilter(private val project: Project, private val cwd: Path) : Filter {
	override fun applyFilter(line: String, entireLength: Int): Filter.Result? {
		val match = location.find(line.trimEnd()) ?: return null
		val label = match.groupValues[1]
		val lineNumber = match.groupValues[2].toIntOrNull()?.takeIf { it > 0 } ?: return null
		val path = runCatching { Path.of(label).let { if (it.isAbsolute) it
		else cwd.resolve(it) }.normalize() }.getOrNull() ?: return null
		val file = LocalFileSystem.getInstance().findFileByNioFile(path) ?: return null
		val start = entireLength - line.length + match.range.first
		return Filter.Result(start, start + label.length, OpenFileHyperlinkInfo(project, file, lineNumber - 1))
	}
	companion object {
		private val location = Regex("^(.+?):([0-9]+):([0-9]+):[ \\t]")
	}
}
