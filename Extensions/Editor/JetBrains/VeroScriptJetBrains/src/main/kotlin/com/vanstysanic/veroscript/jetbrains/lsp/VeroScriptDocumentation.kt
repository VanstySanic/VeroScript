// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.lsp

/** Presentation only: retain the server's signatures, descriptions and source spelling. */
internal object VeroScriptDocumentation {
	private val codeHeader = Regex("^\\*\\*(`+) ([^\\n]+) \\1\\*\\*$")
	private val title = Regex("^(enum member|enum class|type alias|annotation argument|import|namespace|function|method|constructor|destructor|struct|class|interface|enum|field|variable|constant|label|keyword|annotation|macro|symbol) (.+)$")
	private val paragraphBreak = Regex("\\n[ \\t]*\\n+")
	private val fenceLine = Regex("^ {0,3}(`{3,}|~{3,})(.*)$")
	private val builtinDetails = setOf("builtin type", "内置类型", "內建型別", "type intégré", "組み込み型", "기본 제공 형식")
	private val typeKinds = setOf("keyword", "type alias", "class", "struct", "interface", "enum", "enum class")

	fun code(value: String, minimum: Int = 1): String {
		val length = Regex("`+").findAll(value).maxOfOrNull { it.value.length + 1 } ?: minimum
		val fence = "`".repeat(maxOf(minimum, length))
		return if (minimum == 1) "$fence $value $fence"
		else "${fence}veroscript\n$value\n$fence"
	}

	fun hoverMarkdown(value: String): String {
		val text = value.replace("\r\n", "\n")
		val paragraphs = mutableListOf<String>()
		var start = 0
		for (separator in paragraphBreak.findAll(text)) {
			paragraphs += text.substring(start, separator.range.first)
			paragraphs += separator.value
			start = separator.range.last + 1
		}
		paragraphs += text.substring(start)
		fun header(part: String) = codeHeader.matchEntire(part)?.groupValues?.get(2)?.let(title::matchEntire)
		if (header(paragraphs.first()) == null) return value
		var fence: String? = null
		var consumedThrough = -1
		return paragraphs.mapIndexed { index, paragraph ->
			if (index <= consumedThrough) return@mapIndexed ""
			if (index % 2 != 0) return@mapIndexed paragraph
			if (fence == null && (index == 0 || paragraphs.getOrNull(index - 2) == "---")) {
				header(paragraph)?.let {
					val kind = it.groupValues[1]
					val name = code(it.groupValues[2])
					val detail = paragraphs.getOrNull(index + 2)
					// Native primitives and containers can have different semantic type kinds.
					if (kind in typeKinds && detail in builtinDetails) {
						consumedThrough = index + 2
						return@mapIndexed "**$detail** $name"
					}
					return@mapIndexed "**$kind** $name"
				}
			}
			for (line in paragraph.lineSequence()) {
				val marker = fenceLine.matchEntire(line) ?: continue
				val run = marker.groupValues[1]
				val current = fence
				if (current == null) fence = run
				else if (run.first() == current.first() && run.length >= current.length && marker.groupValues[2].isBlank()) fence = null
			}
			paragraph
		}.joinToString("")
	}
}
