// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains.settings

import com.google.gson.GsonBuilder
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.intellij.openapi.components.PersistentStateComponent
import com.intellij.openapi.components.Service
import com.intellij.openapi.components.State
import com.intellij.openapi.components.Storage
import com.intellij.openapi.components.StoragePathMacros
import com.intellij.openapi.project.Project
import com.vanstysanic.veroscript.jetbrains.VeroScriptBundle.message
import java.nio.file.Path

class RuntimeState {
	var runtimeHome = ""
}

@Service(Service.Level.APP)
@State(name = "VeroScriptRuntime", storages = [Storage("veroscript.xml")])
class VeroScriptApplicationSettings : PersistentStateComponent<RuntimeState> {
	private var value = RuntimeState()
	override fun getState() = value
	override fun loadState(state: RuntimeState) { value = state }
}

@Service(Service.Level.PROJECT)
@State(name = "VeroScriptLocal", storages = [Storage(StoragePathMacros.WORKSPACE_FILE)])
class VeroScriptLocalSettings : PersistentStateComponent<RuntimeState> {
	private var value = RuntimeState()
	override fun getState() = value
	override fun loadState(state: RuntimeState) { value = state }
}

class ProjectState {
	var schemaVersion = 1
	var enabled = true
	var bracesOnNewLine = true
	var locale = ""
	var semantics = "{}"
}

@Service(Service.Level.PROJECT)
@State(name = "VeroScript", storages = [Storage("veroscript.xml")])
class VeroScriptProjectSettings : PersistentStateComponent<ProjectState> {
	private var value = ProjectState()
	override fun getState() = value
	override fun loadState(state: ProjectState) { value = state }

	fun configuration(): JsonObject {
		require(value.schemaVersion == 1) { message("settings.schema", value.schemaVersion) }
		return SemanticOptions.parse(value.semantics)
	}

	fun locale() = value.locale.ifBlank { com.intellij.DynamicBundle.getLocale().toLanguageTag() }

	companion object {
		fun get(project: Project) = project.getService(VeroScriptProjectSettings::class.java)
	}
}

/** Validates the actual protocol shape before settings can reach any tool. */
object SemanticOptions {
	val paths = setOf("importPaths", "stdlibSearchPaths", "implicitImportPaths", "bindingCatalogPaths")
	private val lists = paths + "defines"
	private val flags = setOf("allowCircularImports", "allowProjectWideAccess", "lspTraceAnalysis")
	private val limits = mapOf(
		"lspRequestTimeoutMs" to 50..30000, "lspAnalysisTimeoutMs" to 50..30000,
		"maxCompletionItems" to 32..5000, "maxWorkspaceSymbolResults" to 64..20000,
		"maxReferenceResults" to 64..50000, "maxWorkspaceFiles" to 1..100000,
		"maxWorkspaceFileBytes" to 1024..67108864
	)
	private val identifier = Regex("[A-Za-z_][A-Za-z0-9_]*")
	val gson = GsonBuilder().setPrettyPrinting().disableHtmlEscaping().create()

	fun parse(text: String): JsonObject {
		val parsed = JsonParser.parseString(text.ifBlank { "{}" })
		require(parsed.isJsonObject) { message("settings.object") }
		val result = parsed.asJsonObject
		for ((key, value) in result.entrySet()) {
			when (key) {
				in lists -> require(value.isJsonArray && value.asJsonArray.all {
					it.isJsonPrimitive && it.asJsonPrimitive.isString && !it.asString.contains('\u0000') &&
						(key == "defines" || it.asString.isNotBlank())
				}) { message("settings.stringArray", key) }
				"stdlibPath" -> require(value.isJsonPrimitive && value.asJsonPrimitive.isString && value.asString.isNotBlank() && !value.asString.contains('\u0000')) {
					message("settings.stdlibPath")
				}
				in flags -> require(value.isJsonPrimitive && value.asJsonPrimitive.isBoolean) {
					message("settings.boolean", key)
				}
				in limits -> require(value.isJsonPrimitive && value.asJsonPrimitive.isNumber &&
					value.asString.toIntOrNull() in limits.getValue(key)) { message("settings.range", key, limits.getValue(key)) }
				"predefinedMacros" -> {
					require(value.isJsonArray) { message("settings.macrosArray") }
					value.asJsonArray.forEach { macro ->
						require(macro.isJsonObject) { message("settings.macroObject") }
						validateMacro(macro.asJsonObject)
					}
				}
				else -> throw IllegalArgumentException(message("settings.unknown", key))
			}
		}
		require(!result.has("stdlibPath") || !result.has("stdlibSearchPaths")) {
			message("settings.stdlibChoice")
		}
		return result
	}

	private fun validateMacro(macro: JsonObject) {
		require(macro.keySet().all { it in setOf("name", "parameters", "variadic", "body", "sourceName") }) {
			message("settings.macroUnknown")
		}
		fun string(key: String, required: Boolean = false): String {
			val value = macro[key]
			require(value != null || !required) { message("settings.macroRequired", key) }
			require(value == null || value.isJsonPrimitive && value.asJsonPrimitive.isString) { message("settings.macroString", key) }
			return value?.asString.orEmpty()
		}
		require(identifier.matches(string("name", true))) { message("settings.macroName") }
		string("body", true)
		string("sourceName")
		macro["parameters"]?.let { parameters ->
			require(parameters.isJsonArray && parameters.asJsonArray.all {
				it.isJsonPrimitive && it.asJsonPrimitive.isString && identifier.matches(it.asString)
			}) { message("settings.macroParameters") }
			require(parameters.asJsonArray.map { it.asString }.distinct().size == parameters.asJsonArray.size()) {
				message("settings.macroDuplicate")
			}
		}
		macro["variadic"]?.let { require(it.isJsonPrimitive && it.asJsonPrimitive.isBoolean) { message("settings.boolean", "variadic") } }
	}

	fun resolve(options: JsonObject, base: Path, projectRoot: Path = base): JsonObject = options.deepCopy().apply {
		for (key in paths) getAsJsonArray(key)?.let { array ->
			add(key, gson.toJsonTree(array.map { absolute(it.asString, base, projectRoot).toString() }))
		}
		get("stdlibPath")?.let { addProperty("stdlibPath", absolute(it.asString, base, projectRoot).toString()) }
	}

	fun absolute(value: String, base: Path, projectRoot: Path = base): Path {
		val expanded = value.replace("\$PROJECT_DIR\$", projectRoot.toString())
		val path = Path.of(expanded)
		return (if (path.isAbsolute) path
		else base.resolve(path)).toAbsolutePath().normalize()
	}
}
