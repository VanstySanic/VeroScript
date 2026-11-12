// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.gui.jetbrains.language

import com.google.gson.JsonParser

data class GuiProperty(val name: String, val mark: String, val type: String, val style: Boolean, val bindable: Boolean)
data class GuiEvent(val name: String, val mark: String)
data class GuiType(val name: String, val mark: String, val base: String, val properties: List<GuiProperty>,
	val events: List<GuiEvent>, val qualifiedName: String)

/** Suggestions for built-ins only. Unknown application schemas stay native-tool-owned. */
object GuiCatalog {
	private val data by lazy {
		requireNotNull(javaClass.getResourceAsStream("/Catalog/gui.json")).bufferedReader(Charsets.UTF_8).use {
			JsonParser.parseReader(it).asJsonObject.also { json -> check(json["schemaVersion"].asInt == 2) }
		}
	}
	val types: List<GuiType> by lazy {
		data.getAsJsonArray("types").map { value ->
			val item = value.asJsonObject
			GuiType(item["name"].asString, item["mark"].asString, item["base"].asString,
				item.getAsJsonArray("properties").map { entry ->
					val property = entry.asJsonObject
					GuiProperty(property["name"].asString, property["mark"].asString, property["type"].asString,
						property["style"].asBoolean, property["bindable"].asBoolean)
				}, item.getAsJsonArray("events").map { entry ->
					GuiEvent(entry.asJsonObject["name"].asString, entry.asJsonObject["mark"].asString)
				}, item["qualifiedName"].asString)
		}
	}
	val routedEvents: List<GuiEvent> by lazy {
		data.getAsJsonArray("routedEvents").map { GuiEvent(it.asJsonObject["name"].asString, it.asJsonObject["mark"].asString) }
	}
	fun find(name: String) = types.firstOrNull { name == it.name || name == it.mark || name == it.qualifiedName }
	private fun hierarchy(type: GuiType): List<GuiType> = buildList {
		var current: GuiType? = type
		val visited = mutableSetOf<String>()
		while (current != null && visited.add(current.name)) {
			add(current)
			current = find(current.base)
		}
	}
	fun properties(type: GuiType) = hierarchy(type).flatMap { it.properties }.distinctBy { it.name }
	fun events(type: GuiType) = (hierarchy(type).flatMap { it.events } + routedEvents).distinctBy { it.name }
	val styleProperties: List<GuiProperty> by lazy { types.flatMap { properties(it) }.filter { it.style }.distinctBy { it.mark } }
	val states = listOf("root", "hover", "active", "focus", "focus-within", "disabled", "checked", "read-only")
	val directives = mapOf("import" to listOf("src"), "style" to listOf("src"),
		"script" to listOf("src", "type"), "component" to listOf("type", "mark", "base"), "template" to emptyList())
}
