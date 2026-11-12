# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise host module environments and new standard metadata over actual wires."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from urllib.parse import quote

from VeroScriptProtocolReviewTests import Wire, point, apply_edits


def run(fixture, lsp, destination):
	cases, documents, disk_documents, clients = [], {}, {}, []
	binaries = {name: {"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
				for name, p in (("fixture", fixture), ("lsp", lsp))}
	library_uri = "veroscript-module:/dep/api.vs"
	library = "/* 中文😀 */ export int dep_value(){return 7;}\n"
	documents[library_uri] = library

	def save():
		destination.parent.mkdir(parents=True, exist_ok=True)
		destination.write_text(json.dumps({"binaries": binaries, "cases": cases,
			"requests": [c.requests for c in clients], "messages": [c.messages for c in clients]},
			ensure_ascii=False, indent=2), encoding="utf-8")

	def ask(client, name, method, params=None, success=True):
		reply = client.request(method, params)
		cases.append({"name": name, "request": client.requests[-1], "response": reply,
					  "documents": dict(documents), "diskDocuments": dict(disk_documents)})
		save()
		assert (reply.get("success") == success if client.dap else ("error" not in reply) == success), (name, reply)
		return reply.get("body", {}) if client.dap else reply["result" if success else "error"]

	def contents(result):
		if result is None:
			return ""
		value = result.get("contents", "")
		return value.get("value", "") if isinstance(value, dict) else str(value)

	def close(client):
		try:
			if sys.exc_info()[0] is None:
				client.close()
		finally:
			if client.process.poll() is None:
				client.process.kill()
				client.process.wait(timeout=10)

	with tempfile.TemporaryDirectory(prefix="vero module environment ") as directory:
		root = Path(directory).resolve()
		(root / "app").mkdir()
		main = root / "app/main.vs"
		uri = main.as_uri()
		version = 0

		def change(client, source):
			nonlocal version
			documents[uri] = source
			version += 1
			if version == 1:
				client.send("textDocument/didOpen", {"textDocument": {"uri": uri,
					"languageId": "veroscript", "version": version, "text": source}})
			else:
				client.send("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
					"contentChanges": [{"text": source}]})

		client = Wire(fixture, root, arguments=["--lsp"])
		clients.append(client)
		try:
			initialize = ask(client, "environment-initialize", "initialize", {"rootUri": root.as_uri(), "locale": "en-US",
				"capabilities": {"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}})
			metadata = initialize["capabilities"]["experimental"]["veroscriptModuleEnvironment"]
			assert metadata["compileFingerprint"] and metadata["executionFingerprint"], metadata
			memory = ask(client, "memory-module-source", metadata["moduleSourceMethod"], {"uri": library_uri})
			assert memory["uri"] == library_uri and memory["text"] == library and memory["readOnly"], memory
			assert memory["compileFingerprint"] == metadata["compileFingerprint"], memory
			assert ask(client, "hidden-module-source", metadata["moduleSourceMethod"],
					   {"uri": "veroscript-module:/hidden/api.vs"}) is None
			assert ask(client, "missing-module-source", metadata["moduleSourceMethod"],
					   {"uri": "veroscript-module:/missing/api.vs"}) is None
			client.send("initialized")
			source = 'import "dep"; void main(){print(visible_native());print(dep_value());}'
			change(client, source)
			hover = ask(client, "environment-native-hover", "textDocument/hover",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("visible_native"))})
			assert "visible_native" in contents(hover), hover
			definition = ask(client, "memory-module-definition", "textDocument/definition",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("dep_value"))})
			assert len(definition) == 1 and definition[0]["uri"] == library_uri, definition
			rename = ask(client, "readonly-memory-prepare-rename", "textDocument/prepareRename",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("dep_value"))}, success=False)
			assert rename["code"] == -32602 and "read-only" in rename["message"], rename
			offset = definition[0]["range"]["start"]["character"]
			assert offset in {len(library[:library.index(name)].encode("utf-16-le")) // 2
							  for name in ("int dep_value", "dep_value")}, definition
			links = ask(client, "memory-module-document-link", "textDocument/documentLink", {"textDocument": {"uri": uri}})
			assert any(link.get("target") == library_uri for link in links), links
			unusual_path = "veroscript-module:/dep/encoded % 模块.vs"
			unusual_uri = quote(unusual_path, safe="/:")
			unusual_source = 'import "strange"; void main(){print(named_value());}'
			change(client, unusual_source)
			unusual = ask(client, "escaped-memory-definition", "textDocument/definition",
				{"textDocument": {"uri": uri}, "position": point(unusual_source, unusual_source.index("named_value"))})
			assert unusual[0]["uri"] == unusual_uri, unusual
			fetched = ask(client, "escaped-memory-source", metadata["moduleSourceMethod"], {"uri": unusual_uri})
			assert fetched["uri"] == unusual_uri and "named_value" in fetched["text"], fetched
			documents[unusual_uri] = fetched["text"]
			leaf_uri = "veroscript-module:/leaf/api.vs"
			assert ask(client, "unimported-transitive-source", metadata["moduleSourceMethod"], {"uri": leaf_uri}) is None
			reexport = 'import "facade"; void main(){print(leaf_value());}'
			change(client, reexport)
			definition = ask(client, "reexported-memory-definition", "textDocument/definition",
				{"textDocument": {"uri": uri}, "position": point(reexport, reexport.index("leaf_value"))})
			assert definition[0]["uri"] == leaf_uri, definition
			leaf = ask(client, "reexported-memory-source", metadata["moduleSourceMethod"], {"uri": leaf_uri})
			assert leaf and "leaf_value" in leaf["text"], leaf
			documents[leaf_uri] = leaf["text"]
			change(client, source)
			tokens = ask(client, "environment-semantic-tokens", "textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
			assert tokens["data"], tokens
			for text, included, excluded in [('import "de', "dep", "forbidden"),
					('import "for', None, "forbidden"), ('import "str', None, "strings.vs"),
					('import "dep"; void main(){vis}', "visible_native", "hidden_native"),
					('void main(){vis}', None, "visible_native"),
					('int broken[; void main(){hidden}', None, "hidden_native"),
					('import "dep"; void main(){Private}', None, "PrivateGeneric"),
					('import "dep"; void main(){hidden}', None, "hidden_native")]:
				change(client, text)
				caret = len(text) if text.startswith('import "') and "void main" not in text else text.index("}")
				result = ask(client, "environment-completion-" + str(version), "textDocument/completion",
					{"textDocument": {"uri": uri}, "position": point(text, caret)})
				labels = {item["label"] for item in result["items"]}
				assert excluded not in labels, (text, labels)
				if included:
					assert included in labels, (text, labels)
					item = next(item for item in result["items"] if item["label"] == included)
					applied = apply_edits(text, [item["textEdit"]])
					assert included in applied, (item, applied)
			text = "void main(){hidden_native();}"
			change(client, text)
			hidden = ask(client, "environment-hidden-hover", "textDocument/hover",
				{"textDocument": {"uri": uri}, "position": point(text, text.index("hidden_native"))})
			assert not contents(hidden), hidden
			physical = root / "dep/physical.vs"
			physical_uri = physical.as_uri()
			caller = 'import "physical";void main(){print(physical_value());}'
			buffer = "\n\n/// Current buffer documentation\nexport int physical_value(){return 22;}\n"
			change(client, caller)
			documents[physical_uri] = buffer
			client.send("textDocument/didOpen", {"textDocument": {"uri": physical_uri,
				"languageId": "veroscript", "version": 1, "text": buffer}})
			params = {"textDocument": {"uri": uri}, "position": point(caller, caller.index("physical_value"))}
			refs = ask(client, "overridden-module-references", "textDocument/references",
				{**params, "context": {"includeDeclaration": True}})
			assert len(refs) == 2 and any(r["uri"] == physical_uri and r["range"]["start"]["line"] == 3 for r in refs), refs
			rename = ask(client, "overridden-module-rename", "textDocument/rename", {**params, "newName": "renamed_value"})
			assert set(rename["changes"]) == {uri, physical_uri}, rename
			renamed_caller = apply_edits(caller, rename["changes"][uri])
			renamed_buffer = apply_edits(buffer, rename["changes"][physical_uri])
			assert "physical_value" not in renamed_caller + renamed_buffer, rename
			change(client, renamed_caller)
			documents[physical_uri] = renamed_buffer
			client.send("textDocument/didChange", {"textDocument": {"uri": physical_uri, "version": 2},
				"contentChanges": [{"text": renamed_buffer}]})
			definition = ask(client, "renamed-override-definition", "textDocument/definition",
				{"textDocument": {"uri": uri}, "position": point(renamed_caller, renamed_caller.index("renamed_value"))})
			assert definition and definition[0]["uri"] == physical_uri and definition[0]["range"]["start"]["line"] == 3, definition
			client.send("textDocument/didClose", {"textDocument": {"uri": physical_uri}})
			del documents[physical_uri]
			for name in ("visible_native", "hidden_native"):
				text = 'import "dep"; void main(){' + name + "(}"
				change(client, text)
				result = ask(client, "environment-signature-" + name, "textDocument/signatureHelp",
					{"textDocument": {"uri": uri}, "position": point(text, text.index("(}" ) + 1)})
				signatures = (result or {}).get("signatures", [])
				assert bool(signatures) == (name == "visible_native"), result
		finally:
			close(client)
			save()

		debugger = Wire(fixture, root, dap=True, arguments=["--dap"])
		clients.append(debugger)
		try:
			ask(debugger, "environment-dap-initialize", "initialize")
			source = 'import "dep"; void main(){print(visible_native());print(dep_value());}'
			documents[uri] = source
			disk_documents[uri] = "void main(){print(-1);}\n"
			main.write_text(disk_documents[uri], encoding="utf-8")
			ask(debugger, "environment-dap-launch", "launch", {"source": source, "fileLabel": str(main), "stopOnEntry": True})
			ask(debugger, "environment-dap-configured", "configurationDone")
			debugger.event("stopped")
			frames = ask(debugger, "inline-main-stack", "stackTrace", {"threadId": 1})["stackFrames"]
			frame = next(f for f in frames if f.get("source", {}).get("path", "").replace("\\", "/") == str(main).replace("\\", "/"))
			main_reference = frame["source"]["sourceReference"]
			assert main_reference > 0, frame
			loaded = ask(debugger, "inline-main-source", "source", {"sourceReference": main_reference})
			assert loaded["content"] == source and loaded["content"] != disk_documents[uri], loaded
			ask(debugger, "inline-main-continue", "continue", {"threadId": 1})
			debugger.event("terminated")
			loaded = ask(debugger, "inline-main-source-after-run", "source", {"sourceReference": main_reference})
			assert loaded["content"] == source, loaded
			outputs = [m["body"].get("output") for m in debugger.messages if m.get("event") == "output"]
			assert "42\n" in outputs and "7\n" in outputs, debugger.messages
			assert any(m.get("event") == "exited" and m["body"]["exitCode"] == 0 for m in debugger.messages)
			override = "/* buffer 😀 */ export int dep_value(){return 8;}\n"
			main.unlink()
			del disk_documents[uri]
			documents[library_uri] = override
			ask(debugger, "memory-overlay-launch", "launch", {"source": source, "fileLabel": str(main),
				"sourceOverlays": [{"path": library_uri, "source": override}]})
			ask(debugger, "inline-main-stale-reference", "source", {"sourceReference": main_reference}, success=False)
			ask(debugger, "memory-overlay-breakpoint", "setBreakpoints", {"source": {"path": library_uri},
				"breakpoints": [{"line": 1}]})
			ask(debugger, "memory-overlay-configured", "configurationDone")
			debugger.event("stopped")
			frames = ask(debugger, "memory-overlay-stack", "stackTrace", {"threadId": 1})["stackFrames"]
			main_frame = next(f for f in frames if f.get("source", {}).get("path", "").replace("\\", "/") == str(main).replace("\\", "/"))
			loaded = ask(debugger, "inline-main-without-disk-source", "source", {"sourceReference": main_frame["source"]["sourceReference"]})
			assert loaded["content"] == source, loaded
			frame = next(f for f in frames if f.get("source", {}).get("path") == library_uri)
			assert frame["line"] == 1 and frame["column"] == len(override[:override.index("8")].encode("utf-16-le")) // 2 + 1, frame
			reference = frame["source"]["sourceReference"]
			assert reference > 0, frame
			loaded = ask(debugger, "memory-overlay-source", "source", {"sourceReference": reference})
			assert loaded["content"] == override, loaded
			ask(debugger, "memory-overlay-continue", "continue", {"threadId": 1})
			debugger.event("terminated")
			assert any(m.get("event") == "output" and m["body"].get("output") == "8\n" for m in debugger.messages)
			documents[library_uri] = library
			ask(debugger, "memory-overlay-replaced", "launch", {"source": source, "fileLabel": str(main)})
			ask(debugger, "memory-overlay-stale-reference", "source", {"sourceReference": reference}, success=False)
			physical = root / "dep/physical.vs"
			physical_source = "export int physical_value(){return 22;}\n"
			source = 'import "physical";void main(){print(physical_value());}'
			documents[uri] = source
			documents[physical.as_uri()] = physical_source
			ask(debugger, "physical-overlay-launch", "launch", {"source": source, "fileLabel": str(main),
				"sourceOverlays": [{"path": str(physical), "source": physical_source}]})
			ask(debugger, "physical-overlay-breakpoint", "setBreakpoints", {"source": {"path": str(physical)}, "breakpoints": [{"line": 1}]})
			ask(debugger, "physical-overlay-configured", "configurationDone")
			debugger.event("stopped")
			frames = ask(debugger, "physical-overlay-stack", "stackTrace", {"threadId": 1})["stackFrames"]
			frame = next(f for f in frames if f.get("source", {}).get("path", "").replace("\\", "/") == str(physical).replace("\\", "/"))
			loaded = ask(debugger, "physical-overlay-source", "source", {"sourceReference": frame["source"]["sourceReference"]})
			assert loaded["content"] == physical_source, loaded
			ask(debugger, "physical-overlay-continue", "continue", {"threadId": 1})
			debugger.event("terminated")
			assert any(m.get("event") == "output" and m["body"].get("output") == "22\n" for m in debugger.messages)
		finally:
			close(debugger)
			save()

		standalone = Wire(lsp, root)
		clients.append(standalone)
		version = 0
		try:
			ask(standalone, "standard-initialize", "initialize", {"locale": "en-US"})
			broken = "int broken[;void main(){std::i18n::"
			change(standalone, broken)
			recovered = ask(standalone, "broken-standard-namespace-types", "textDocument/completion",
				{"textDocument": {"uri": uri}, "position": point(broken, len(broken))})
			assert {"text", "normalize_language_tag"}.issubset({item["label"] for item in recovered["items"]}), recovered
			for source, token, required in [('void main(){std::}', "std::", "i18n"),
					('void main(){std::i18n::}', "std::i18n::", "text"),
					('void main(){auto text=std::i18n::text("Hello");text.}', "text.", "is_literal")]:
				change(standalone, source)
				result = ask(standalone, "standard-completion-" + str(version), "textDocument/completion",
					{"textDocument": {"uri": uri}, "position": point(source, source.index(token) + len(token))})
				assert required in {item["label"] for item in result["items"]}, result
				assert not any(item["label"].startswith("std.i18n") for item in result["items"]), result
			source = 'void main(){print(std::i18n::normalize_language_tag("en_us"));}'
			change(standalone, source)
			hover = ask(standalone, "standard-i18n-hover", "textDocument/hover",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("normalize_language_tag"))})
			assert "normalize_language_tag" in contents(hover) and "std.i18n" not in contents(hover), hover
			for tag, source, required, forbidden in [
					("namespaced-homonyms", 'namespace A{class Same{private:int secret;protected:int guarded;public:int visible;};}'
						'namespace B{class Same{public:void inspect(A::Same other){other.;}};}', {"visible"}, {"secret", "guarded"}),
					("global-homonym", 'class Same{private:int secret;protected:int guarded;public:int visible;};'
						'using GlobalSame=Same;namespace B{class Same{public:void inspect(GlobalSame other){other.;}};}', {"visible"}, {"secret", "guarded"}),
					("global-root", 'class Same{private:int secret;protected:int guarded;public:int visible;};'
						'namespace B{class Same{public:void inspect(::Same other){other.;}};}', {"visible"}, {"secret", "guarded", "inspect"}),
					("global-root-alias", 'class Target{public:int correct;};class Other{public:int wrong;};'
						'namespace A{using Pick=Other;}using Pick=Target;namespace B{void inspect(::Pick other){other.;}}', {"correct"}, {"wrong"}),
					("own-short-name", 'namespace A{class Same{private:int secret;protected:int guarded;public:int visible;'
						'void inspect(Same other){other.;}};}', {"visible", "secret", "guarded"}, set())]:
				change(standalone, source)
				result = ask(standalone, "member-access-" + tag, "textDocument/completion",
					{"textDocument": {"uri": uri}, "position": point(source, source.index("other.;") + len("other."))})
				labels = {i["label"] for i in result["items"]}
				assert required.issubset(labels) and not forbidden.intersection(labels), (tag, labels)
			for factory in ("make_attribute_by_name", "make_weak_attribute_by_name"):
				target = "weak<Model>(model)" if "weak" in factory else "model"
				source = ('import "attribute.vs";class Model{public:int read(){return 3;}};'
						  'void main(){Model model=Model();auto value=std::' + factory + '<int>(' + target + ',"read");value.;}')
				change(standalone, source)
				result = ask(standalone, "attribute-factory-" + factory, "textDocument/completion",
					{"textDocument": {"uri": uri}, "position": point(source, source.index("value.;") + len("value."))})
				assert {"get", "try_get", "bind", "reset"}.issubset({item["label"] for item in result["items"]}), result
			source = ('class Vec{public:bool operator==(Vec other){return true;}};'
					  'class Other{public:bool operator!=(Other other){return false;}};'
					  'void main(){Vec a=Vec();Vec b=Vec();bool different=a!=b;}')
			change(standalone, source)
			operator_params = {"textDocument": {"uri": uri}, "position": point(source, source.index("a!=b") + 1)}
			hover = ask(standalone, "receiver-scoped-operator-hover", "textDocument/hover", operator_params)
			assert "Vec.operator==" in contents(hover) and "Other.operator" not in contents(hover) and "std::i18n" not in contents(hover), hover
			definitions = ask(standalone, "receiver-scoped-operator-definition", "textDocument/definition", operator_params)
			assert len(definitions) == 1 and definitions[0]["uri"] == uri, definitions
			source = "void main(){int a=1;int b=2;bool different=a!=b;}"
			change(standalone, source)
			hover = ask(standalone, "primitive-operator-no-host-hover", "textDocument/hover",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("a!=b") + 1)})
			assert not contents(hover), hover
			source = 'void main(){auto a=std::i18n::text("one");auto b=std::i18n::text("two");bool different=a!=b;}'
			change(standalone, source)
			hover = ask(standalone, "native-operator-keeps-receiver", "textDocument/hover",
				{"textDocument": {"uri": uri}, "position": point(source, source.index("a!=b") + 1)})
			assert "text.operator!=" in contents(hover) and "argument.operator" not in contents(hover) and "version.operator" not in contents(hover), hover
		finally:
			close(standalone)
			save()
	print(f"Module environment interactions: {len(cases)} cases passed.")


if __name__ == "__main__":
	run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
