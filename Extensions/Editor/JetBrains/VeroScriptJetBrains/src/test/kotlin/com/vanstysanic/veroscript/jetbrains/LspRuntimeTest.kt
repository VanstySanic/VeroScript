// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

package com.vanstysanic.veroscript.jetbrains

import org.eclipse.lsp4j.*
import org.eclipse.lsp4j.launch.LSPLauncher
import org.eclipse.lsp4j.services.LanguageClient
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CompletableFuture
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class LspRuntimeTest {
	@Test fun initializeDiagnosticsHoverCompletionSemanticFullAndDelta() {
		val home = System.getenv("VEROSCRIPT_TEST_RUNTIME")
		assumeTrue("Set VEROSCRIPT_TEST_RUNTIME to run native protocol integration", !home.isNullOrBlank())
		val runtime = Path.of(home)
		val directory = Files.createTempDirectory("vero lsp 空格 ")
		val file = directory.resolve("测试.vs")
		val closedFile = directory.resolve("关闭 😀.vs")
		val text = "void main()\n{\n int64 value = 42;\n print(value);\n}\n"
		Files.writeString(file, text)
		val diagnostics = CompletableFuture<PublishDiagnosticsParams>()
		val observedDiagnostics = LinkedBlockingQueue<PublishDiagnosticsParams>()
		val client = object : LanguageClient {
			override fun telemetryEvent(value: Any?) {}
			override fun publishDiagnostics(params: PublishDiagnosticsParams) {
				diagnostics.complete(params)
				observedDiagnostics.offer(params)
			}
			override fun showMessage(params: MessageParams) {}
			override fun logMessage(params: MessageParams) {}
			override fun showMessageRequest(params: ShowMessageRequestParams) = CompletableFuture.completedFuture<MessageActionItem>(null)
			override fun registerCapability(params: RegistrationParams) = CompletableFuture.completedFuture<Void>(null)
			override fun configuration(params: ConfigurationParams): CompletableFuture<List<Any>> =
				CompletableFuture.completedFuture(params.items.map { emptyMap<String, Any>() })
		}
		val suffix = if (System.getProperty("os.name").startsWith("Windows")) ".exe"
		else ""
		val process = ProcessBuilder(runtime.resolve("veroscript-lsp$suffix").toString()).directory(directory.toFile())
			.redirectError(ProcessBuilder.Redirect.INHERIT).start()
		val launcher = LSPLauncher.createClientLauncher(client, process.inputStream, process.outputStream)
		val listener = launcher.startListening()
		val server = launcher.remoteProxy
		try {
			val initialize = server.initialize(InitializeParams().apply {
				capabilities = ClientCapabilities()
				locale = "en-US"
				workspaceFolders = listOf(WorkspaceFolder(directory.toUri().toString(), "test"))
				initializationOptions = mapOf("veroscript" to mapOf("stdlibPath" to runtime.resolve("stdlib").toString()))
			}).get(10, TimeUnit.SECONDS)
			assertNotNull(initialize.capabilities.semanticTokensProvider)
			val legend = initialize.capabilities.semanticTokensProvider.legend
			assertTrue("The updated server must declare readonly fields", "readonly" in legend.tokenModifiers)
			server.initialized(InitializedParams())
			val identifier = TextDocumentIdentifier(file.toUri().toString())
			server.textDocumentService.didOpen(DidOpenTextDocumentParams(TextDocumentItem(identifier.uri, "veroscript", 1, text)))
			assertTrue(diagnostics.get(10, TimeUnit.SECONDS).diagnostics.none { it.severity == DiagnosticSeverity.Error })
			val hover = server.textDocumentService.hover(HoverParams(identifier, Position(3, 8))).get(10, TimeUnit.SECONDS)
			assertNotNull(hover)
			val printHover = server.textDocumentService.hover(HoverParams(identifier, Position(3, 3))).get(10, TimeUnit.SECONDS)
			assertEquals("markdown", printHover.contents.right.kind)
			assertTrue(printHover.contents.right.value.contains(
				"```veroscript\nvoid print(any value);\nvoid print(string fmt, ...);\n```"))
			val completion = server.textDocumentService.completion(CompletionParams(identifier, Position(3, 2))).get(10, TimeUnit.SECONDS)
			assertNotNull(completion)
			val tokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
			assertTrue(tokens.data.isNotEmpty())
			assertNotNull(tokens.resultId)
			val delta = server.textDocumentService.semanticTokensFullDelta(SemanticTokensDeltaParams(identifier, tokens.resultId))
				.get(10, TimeUnit.SECONDS)
			assertTrue(delta.isRight)
			assertTrue(delta.right.edits.isEmpty())
			fun completeNamespace(prefix: String, version: Int): List<CompletionItem> {
				val source = "void main()\n{\n $prefix\n}\n"
				server.textDocumentService.didChange(DidChangeTextDocumentParams(
					VersionedTextDocumentIdentifier(identifier.uri, version), listOf(TextDocumentContentChangeEvent(source))))
				val result = server.textDocumentService.completion(CompletionParams(identifier, Position(2, prefix.length + 1)))
					.get(10, TimeUnit.SECONDS)
				return if (result.isLeft) result.left
				else result.right.items
			}
			val standard = completeNamespace("st", 2).first { it.label == "std" }
			assertEquals("std::", standard.insertText)
			val nested = completeNamespace("std::", 3).first { it.label == "chrono" }
			assertEquals("chrono::", nested.insertText)
			assertTrue(completeNamespace("std::chrono::", 4).any { it.label == "now" })
			fun completeAtMarker(marked: String, version: Int): List<CompletionItem> {
				val caret = marked.indexOf('|')
				require(caret >= 0)
				val prefix = marked.substring(0, caret)
				server.textDocumentService.didChange(DidChangeTextDocumentParams(
					VersionedTextDocumentIdentifier(identifier.uri, version),
					listOf(TextDocumentContentChangeEvent(marked.removeRange(caret, caret + 1)))))
				val result = server.textDocumentService.completion(CompletionParams(identifier,
					Position(prefix.count { it == '\n' }, prefix.substringAfterLast('\n').length)))
					.get(10, TimeUnit.SECONDS)
				return if (result.isLeft) result.left
				else result.right.items
			}
			// Exercise the SDK's LSP4J path with the same unfinished for-header as the Rider report.
			val suppressed = listOf(
				"void main(){ for(int i=0|) }",
				"void main(){ int i=12|34; }",
				"void main(){ double p=1e+|2; }",
				"void main(){ int i=0 /* note */ |; }",
				"void main(){ print(\"std|\"); }",
				"void main(){ /* std| */ }"
			)
			var version = 5
			suppressed.forEach { source ->
				assertTrue("Literal/comment completion must be empty: $source",
					completeAtMarker(source, version++).isEmpty())
			}
			for (source in listOf(
				"void main(){ for(int i=0;i|) }",
				"void main(){ for(int i=0;|) }",
				"void main(){ int item0=1; int value=0+item|; }"
			)) {
				val expected = if (source.contains("item0")) "item0"
				else "i"
				assertTrue("Code completion must resume after delimiters: $source",
					completeAtMarker(source, version++).any { it.label == expected })
			}
			val inner = "class Inner{public:int value;};"
			for (chain in listOf(
				"void use(fn()->fn()->Inner factory){factory()().va|lue;}",
				"T make<T>(T value){return value;}void main(){using Local=Inner;make<handle<Local>>(handle<Local>()).value.va|lue;}",
				"struct Store<T>{struct Item{static Inner child;};};void use(){Store<int>::Item::child.va|lue;}",
				"class Store<T>{public:static T child=T();};void use(){Store<array<Inner>>::child[0].va|lue;}",
				"class Store<T>{public:static T child=T();};void use(){Store<map<int,Inner>>::child[0].va|lue;}",
				"class Outer{public:Inner child;Inner read(){return child;}int get(){return read().va|lue;}};"
			)) {
				val items = completeAtMarker(inner + chain, version++)
				assertTrue("Nested receiver completion lost its native type: $chain", items.any { it.label == "value" })
				assertTrue("Member completion must not fall back to unrelated keywords", items.none { it.label == "while" })
			}
			val shadowed = completeAtMarker(inner + "class Base{public:Inner child;};" +
				"class Outer:public Base{private:string child;};void use(Outer x){x.child.va|lue;}", version++)
			assertTrue("Private shadowing must not expose an inherited receiver", shadowed.none { it.label == "value" })
			val memoryItems = completeAtMarker("void main(){ std::memory::| }", version++)
			assertTrue(memoryItems.any { it.label == "buffer" })
			assertTrue(memoryItems.any { it.label == "read" })
			assertTrue(memoryItems.none { it.label.startsWith("__memory") })
			val members = completeAtMarker("void main(){ auto bytes=new std::memory::buffer(8); bytes.| }", version++)
			assertTrue(members.any { it.label == "size" })
			assertTrue(members.any { it.label == "readonly_slice" })
			val qualified = completeAtMarker("void main(){ std::memory::co| }", version++).first { it.label == "copy" }
			assertTrue("Clients without snippet support must receive a callable name", qualified.insertText.orEmpty().endsWith("copy"))
			assertFalse("Source insertion must use :: namespace spelling", qualified.insertText.orEmpty().contains("std.memory"))
			assertFalse("Internal intrinsics must not leak into source insertion", qualified.insertText.orEmpty().contains("__memory"))
			val readonlySource = "class Base { public: const int value = 1; };\n" +
				"class Derived : public Base { public:\n" +
				" int read() { return value + this.value; }\n" +
				" int shadow(int value) { return value + this.value; }\n" +
				"};\nvoid main() { Derived d = Derived(); print(d.value); }\n"
			fun changeSource(source: String) {
				server.textDocumentService.didChange(DidChangeTextDocumentParams(
					VersionedTextDocumentIdentifier(identifier.uri, version++),
					listOf(TextDocumentContentChangeEvent(source))))
			}
			fun checkFieldTokens(data: List<Int>, readonly: Boolean) {
				assertEquals("Semantic tokens have five integers per token", 0, data.size % 5)
				var line = 0
				var column = 0
				val fields = mutableMapOf<Pair<Int, Int>, Pair<String, Boolean>>()
				for (token in data.chunked(5)) {
					line += token[0]
					column = if (token[0] == 0) column + token[1]
					else token[1]
					val type = legend.tokenTypes[token[3]]
					val isReadonly = token[4] and (1 shl legend.tokenModifiers.indexOf("readonly")) != 0
					fields[line to column] = type to isReadonly
				}
				val lines = readonlySource.lines()
				for ((lineIndex, sourceLine) in lines.withIndex()) {
					for (match in Regex("\\bvalue\\b").findAll(sourceLine)) {
						val isParameter = lineIndex == 3 && match.range.first < sourceLine.indexOf("this.")
						val expected = if (isParameter) "variable" to false
						else "property" to readonly
						assertEquals("Semantic role at $lineIndex:${match.range.first}", expected,
							fields[lineIndex to match.range.first])
					}
				}
			}
			changeSource(readonlySource)
			val readonlyTokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS)
			checkFieldTokens(readonlyTokens.data, true)
			// Preserve positions while changing only the field's mutability.
			changeSource(readonlySource.replace("const", "     "))
			val mutableDelta = server.textDocumentService.semanticTokensFullDelta(
				SemanticTokensDeltaParams(identifier, readonlyTokens.resultId)).get(10, TimeUnit.SECONDS)
			val mutableData = if (mutableDelta.isLeft) mutableDelta.left.data
			else
			{
				val data = readonlyTokens.data.toMutableList()
				for (edit in mutableDelta.right.edits.sortedByDescending { it.start }) {
					data.subList(edit.start, edit.start + edit.deleteCount).clear()
					data.addAll(edit.start, edit.data.orEmpty())
				}
				data
			}
			checkFieldTokens(mutableData, false)
			val mutableFull = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS)
			assertEquals("Applying delta must reproduce the new full token stream", mutableFull.data, mutableData)

			fun expectDiagnostics(uri: String, documentVersion: Int, hasError: Boolean) {
				val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(10)
				while (true) {
					val remaining = deadline - System.nanoTime()
					val report = (if (remaining > 0) observedDiagnostics.poll(remaining, TimeUnit.NANOSECONDS)
					else null)
						?: throw AssertionError("Expected error=$hasError diagnostics for $uri version $documentVersion")
					// The server may percent-encode Unicode differently from java.net.URI.
					if (Path.of(java.net.URI(report.uri)) == Path.of(java.net.URI(uri)) && report.version == documentVersion &&
						report.diagnostics.any { it.severity == DiagnosticSeverity.Error } == hasError) return
				}
			}
			val builtinSource = "using Count = int64;\n" +
				"void sample(array<int8> values, map<string, int> table, priority_queue<int> queue, optional<int> selected, readonly<int> frozen) {}\n" +
				"void main() { Count value = 1; }\n"
			changeSource(builtinSource)
			fun builtinHover(offset: Int): String {
				val prefix = builtinSource.substring(0, offset)
				val value = server.textDocumentService.hover(HoverParams(identifier,
					Position(prefix.count { it == '\n' }, prefix.substringAfterLast('\n').length)))
					.get(10, TimeUnit.SECONDS)
				assertEquals("markdown", value.contents.right.kind)
				return com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDocumentation.hoverMarkdown(value.contents.right.value)
			}
			for (name in listOf("int64", "int8", "array", "map", "string", "priority_queue")) {
				assertEquals("**builtin type** ` $name `\n\nBuilt-in VeroScript type.",
					builtinHover(builtinSource.indexOf(name) + 1))
			}
			for (name in listOf("optional", "readonly")) {
				val value = builtinHover(builtinSource.indexOf(name) + 1)
				assertTrue(value, value.contains(name) && value.contains("Built-in symbol."))
				assertFalse(value, value.contains("**builtin type**"))
			}
			val aliasHover = builtinHover(builtinSource.lastIndexOf("Count") + 1)
			assertTrue(aliasHover, aliasHover.startsWith("**type alias** ` Count `"))
			assertTrue(aliasHover, aliasHover.contains("```veroscript") && aliasHover.contains("defined at"))
			val aliases = directory.resolve("aliases.vs")
			val facade = directory.resolve("facade.vs")
			try {
				Files.writeString(aliases, "namespace Measures {\n export using Count = int64;\n" +
					" export typedef Label = string;\n using Hidden = int;\n}\n")
				Files.writeString(facade, "export import \"aliases.vs\";\n")
				val imported = completeAtMarker("import \"facade.vs\";\nvoid main(){ Measures::| }", version++)
				assertTrue(imported.toString(), imported.any { it.label == "Count" })
				assertTrue(imported.toString(), imported.any { it.label == "Label" })
				assertFalse(imported.toString(), imported.any { it.label == "Hidden" })
				val consumer = "import \"facade.vs\";\nvoid main(){ Measures::Count count = 2147483648i64; " +
					"Measures::Label label = \"aliases\"; print(count); print(label); }\n"
				changeSource(consumer)
				server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
				expectDiagnostics(identifier.uri, version - 1, false)
				val position = Position(1, consumer.substringAfter('\n').indexOf("Count") + 1)
				val hover = server.textDocumentService.hover(HoverParams(identifier, position)).get(10, TimeUnit.SECONDS)
				val rendered = com.vanstysanic.veroscript.jetbrains.lsp.VeroScriptDocumentation.hoverMarkdown(hover.contents.right.value)
				assertTrue(rendered, rendered.startsWith("**type alias**"))
				assertTrue(rendered, rendered.contains("int64") && rendered.contains("aliases.vs"))
				val definition = server.textDocumentService.definition(DefinitionParams(identifier, position)).get(10, TimeUnit.SECONDS)
				val destinations = if (definition.isLeft) definition.left.map { it.uri }
				else definition.right.map { it.targetUri }
				assertEquals(listOf(aliases), destinations.map { Path.of(java.net.URI(it)) })
			}
			finally
			{
				Files.deleteIfExists(facade)
				Files.deleteIfExists(aliases)
			}
			val compilerExtension = directory.resolve("compiler_getter.vs")
			// A partially typed declaration can report an initializer error; completed functions must clear it.
			val declarationPrefix = "import \"core.vs\";\npublic static class A final {const int a=100;}\n"
			changeSource(declarationPrefix + "void t\nint main(){print(A::a);}\n")
			server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
			expectDiagnostics(identifier.uri, version - 1, true)
			for (body in listOf("void t(){}\n", "void t()\n{\n}\n")) {
				changeSource(declarationPrefix + body + "int main(){print(A::a);}\n")
				server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
				expectDiagnostics(identifier.uri, version - 1, false)
			}
			try {
				javaClass.getResourceAsStream("/compiler_getter.vs")!!.use { Files.copy(it, compilerExtension) }
				val prefix = "import compiler \"compiler_getter.vs\";\n@gEtTeR\nclass Box {public:int value=7;};\n"
				assertTrue(completeAtMarker(prefix + "void main(){ Box box=Box(); box.Re|; }", version++).any { it.label == "Read" })
				val generatedUse = "void main(){ Box box=Box(); print(box.Read()); }"
				changeSource(prefix + generatedUse + "\n")
				server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
				expectDiagnostics(identifier.uri, version - 1, false)
				val annotationHover = server.textDocumentService.hover(HoverParams(identifier, Position(1, 3)))
					.get(10, TimeUnit.SECONDS)
				assertTrue(annotationHover.contents.right.value, annotationHover.contents.right.value.contains("@Getter"))
				val position = Position(3, generatedUse.indexOf("Read") + 1)
				val generatedHover = server.textDocumentService.hover(HoverParams(identifier, position)).get(10, TimeUnit.SECONDS)
				assertTrue(generatedHover.contents.right.value, generatedHover.contents.right.value.contains("Read"))
				val definition = server.textDocumentService.definition(DefinitionParams(identifier, position)).get(10, TimeUnit.SECONDS)
				val targets = if (definition.isLeft) definition.left.map { it.uri to it.range.start.line }
					else definition.right.map { it.targetUri to it.targetSelectionRange.start.line }
				assertTrue("Generated navigation must reach its physical owner: $targets",
					targets.any { Path.of(java.net.URI(it.first)) == file && it.second == 2 })
				val publicMacros = directory.resolve("public_macros.vs")
				try {
					Files.writeString(publicMacros, "#define_export PUBLIC_VALUE 42\n")
					changeSource("import \"public_macros.vs\";\nvoid main(){int value=PUBLIC_VALUE;print(value);}\n")
					server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
					expectDiagnostics(identifier.uri, version - 1, false)
				}
				finally
				{
					Files.deleteIfExists(publicMacros)
				}
			}
			finally
			{
				Files.deleteIfExists(compilerExtension)
			}
			val catalog = directory.resolve("annotations.json")
			try {
				val annotationSource = "@hOSTwIDGET(Label=\"Card\",Enabled=true) class Card{};void main(){}"
				for (caseSensitive in listOf(false, true, false)) {
					Files.writeString(catalog, """{"version":1,"annotations":[{"name":"@HostWidget","caseSensitive":$caseSensitive,"documentation":"Host widget documentation.","arguments":[{"name":"Label","detail":"string"},{"name":"Enabled","detail":"bool"}]}]}""")
					server.workspaceService.didChangeConfiguration(DidChangeConfigurationParams(mapOf("veroscript" to mapOf(
						"stdlibPath" to runtime.resolve("stdlib").toString(), "bindingCatalogPaths" to listOf(catalog.toString())))))
					changeSource(annotationSource)
					val annotationHover = server.textDocumentService.hover(HoverParams(identifier, Position(0, 3)))
						.get(10, TimeUnit.SECONDS)
					assertEquals("A policy change must update annotation lookup without duplicate hover entries",
						if (caseSensitive) 0
						else 1, Regex("Host widget documentation\\.")
							.findAll(annotationHover?.contents?.right?.value.orEmpty()).count())
					val signature = server.textDocumentService.signatureHelp(SignatureHelpParams(identifier,
						Position(0, annotationSource.indexOf("Enabled") + 2))).get(10, TimeUnit.SECONDS)
					assertEquals(!caseSensitive, signature?.signatures.orEmpty().isNotEmpty())
					if (!caseSensitive) {
						assertEquals("@HostWidget(Label: string, Enabled: bool)", signature.signatures.single().label)
						assertEquals(listOf("Label", "Enabled"), signature.signatures.single().parameters.map { it.label.left })
						assertEquals(1, signature.activeParameter.toInt())
					}
				}
				Files.writeString(catalog, """{"version":1,"annotations":[{"name":"@Options","detail":"annotation (fn(int,string)->array<int> callback, bool enabled)"}]}""")
				server.workspaceService.didChangeConfiguration(DidChangeConfigurationParams(mapOf("veroscript" to mapOf(
					"stdlibPath" to runtime.resolve("stdlib").toString(), "bindingCatalogPaths" to listOf(catalog.toString())))))
				val legacySource = "@Options(callback=handler,enabled=true) class Card{};void main(){}"
				changeSource(legacySource)
				val signature = server.textDocumentService.signatureHelp(SignatureHelpParams(identifier,
					Position(0, legacySource.indexOf("enabled") + 2))).get(10, TimeUnit.SECONDS)
				assertEquals(listOf("fn(int,string)->array<int> callback", "bool enabled"),
					signature.signatures.single().parameters.map { it.label.left })
				assertEquals(1, signature.activeParameter.toInt())
			}
			finally
			{
				server.workspaceService.didChangeConfiguration(DidChangeConfigurationParams(mapOf("veroscript" to mapOf(
					"stdlibPath" to runtime.resolve("stdlib").toString()))))
			}
			for ((type, member) in listOf("annotation_info" to "matches_name", "annotation_definition_info" to "case_sensitive")) {
				assertTrue("New metadata helpers must remain visible through LSP4J: $member",
					completeAtMarker("void inspect($type info){info.|}void main(){}", version++).any { it.label == member })
			}
			changeSource("import \"memory.vs\";\nvoid main(){\n auto bytes = new std::memory::buffer(8);\n std::memory::write<uint32>(bytes, 0, 42u);\n print(std::memory::read<uint32>(bytes, 0));\n}\n")
			val memoryHover = server.textDocumentService.hover(HoverParams(identifier, Position(2, 8)))
				.get(10, TimeUnit.SECONDS)
			assertNotNull(memoryHover)
			assertTrue(memoryHover.contents.right.value.contains("std::memory::buffer"))
			val memoryTokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS)
			assertTrue(memoryTokens.data.isNotEmpty())
			expectDiagnostics(identifier.uri, version - 1, false)
			val memoryDelta = server.textDocumentService.semanticTokensFullDelta(
				SemanticTokensDeltaParams(identifier, memoryTokens.resultId)).get(10, TimeUnit.SECONDS)
			assertTrue(memoryDelta.isRight)
			assertTrue(memoryDelta.right.edits.isEmpty())
			// A rejected edit must not poison the next semantic snapshot on the SDK connection.
			changeSource("void main(){int x=0;" + "x=".repeat(1024) + "1;}")
			server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
			expectDiagnostics(identifier.uri, version - 1, true)
			changeSource(readonlySource.replace("const", "     "))
			assertEquals(mutableFull.data, server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS).data)
			expectDiagnostics(identifier.uri, version - 1, false)

			// Neither cyclic source is saved to disk: navigation and recovery must use editor overlays.
			val helper = TextDocumentIdentifier(directory.resolve("helper.vs").toUri().toString())
			val helperSource = "import \"测试.vs\";\nexport int answer() { return seed() + 2; }\n"
			server.textDocumentService.didOpen(DidOpenTextDocumentParams(TextDocumentItem(helper.uri, "veroscript", 1, helperSource)))
			changeSource("import \"helper.vs\";\nexport int seed() { return 40; }\nvoid main() { print(answer()); }\n")
			val cycleTokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS)
			assertTrue(cycleTokens.data.isNotEmpty())
			fun checkImportedDefinition(expectedLine: Int) {
				val definitions = server.textDocumentService.definition(DefinitionParams(identifier, Position(2, 22)))
					.get(10, TimeUnit.SECONDS)
				val locations = if (definitions.isLeft) definitions.left.map { it.uri to it.range }
					else definitions.right.map { it.targetUri to it.targetSelectionRange }
				assertTrue("Imported navigation must target the current unsaved helper definition",
					locations.any { (uri, range) -> Path.of(java.net.URI(uri)) == Path.of(java.net.URI(helper.uri)) &&
						range.start.line == expectedLine })
			}
			checkImportedDefinition(1)
			server.textDocumentService.didChange(DidChangeTextDocumentParams(VersionedTextDocumentIdentifier(helper.uri, 2),
				listOf(TextDocumentContentChangeEvent("export int answer( {"))))
			server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier)).get(10, TimeUnit.SECONDS)
			expectDiagnostics(helper.uri, 2, true)
			// Moving the definition also detects accidental reuse of the pre-error snapshot.
			server.textDocumentService.didChange(DidChangeTextDocumentParams(VersionedTextDocumentIdentifier(helper.uri, 3),
				listOf(TextDocumentContentChangeEvent("\n\n" + helperSource))))
			val recoveredTokens = server.textDocumentService.semanticTokensFull(SemanticTokensParams(identifier))
				.get(10, TimeUnit.SECONDS)
			assertEquals("Repairing a dependency must invalidate stale semantic state", cycleTokens.data, recoveredTokens.data)
			expectDiagnostics(helper.uri, 3, false)
			checkImportedDefinition(3)
			// Workspace symbols must include unopened sources and preserve Unicode locations.
			Files.writeString(closedFile, "void IndexedClosed() {}\n")
			val symbols = server.workspaceService.symbol(WorkspaceSymbolParams("IndexedClosed")).get(10, TimeUnit.SECONDS)
			val locations = if (symbols.isLeft) symbols.left.filter { it.name == "IndexedClosed" }.map { it.location }
				else symbols.right.filter { it.name == "IndexedClosed" }.map { it.location.left }
			assertEquals(1, locations.size)
			assertEquals(closedFile, Path.of(java.net.URI(locations.single().uri)))
			server.textDocumentService.didClose(DidCloseTextDocumentParams(helper))
			server.textDocumentService.didClose(DidCloseTextDocumentParams(identifier))
			server.shutdown().get(5, TimeUnit.SECONDS)
			server.exit()
			assertTrue(process.waitFor(5, TimeUnit.SECONDS))
			assertEquals(0, process.exitValue())
		}
		finally
		{
			listener.cancel(true)
			process.destroyForcibly()
			process.inputStream.close()
			process.outputStream.close()
			// Background analysis can still hold the previous catalog until shutdown.
			Files.deleteIfExists(directory.resolve("annotations.json"))
			Files.deleteIfExists(closedFile)
			Files.deleteIfExists(file)
			Files.deleteIfExists(directory)
		}
	}
}
