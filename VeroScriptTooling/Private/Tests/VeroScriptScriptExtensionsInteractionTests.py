# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check script extension editing, dependency invalidation and debugging on the wire."""
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

from VeroScriptLspResponsivenessTest import Client
from VeroScriptLspInteractionTests import frame, read_frame


EXTENSION = '''import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
auto dependency=ctx.read_dependency("member.txt");
string member=dependency.exists()?dependency.content():"int Read(){return 7;}";
out.add_declaration(ctx.top_scope(),"class Generated{public:"+member+"};");};}
export void register_extension(std::compiler::registration reg){
std::compiler::annotation_definition a=std::compiler::annotation_definition();
a.name="@Explain";a.documentation="Script registered documentation";reg.register_annotation(a);
std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
std::compiler::processor_definition d=std::compiler::processor_definition();d.id="tooling.script.wire";d.version="1";
d.phases=array<std::compiler::phase>{std::compiler::phase::types};
d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);}
'''


def position(source, needle, offset=0, last=False):
	before = source[:(source.rindex(needle) if last else source.index(needle)) + offset]
	return {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def token_at(tokens, legend, point):
	line = column = 0
	for i in range(0, len(tokens), 5):
		dl, dc, length, kind, modifiers = tokens[i:i + 5]
		line += dl
		column = column + dc if dl == 0 else dc
		if line == point["line"] and column <= point["character"] < column + length:
			return legend["tokenTypes"][kind], modifiers
	raise AssertionError((point, tokens))


def run(lsp, dap, fixture, destination):
	records, documents = [], {}
	binaries = {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
				for name, path in (("lsp", lsp), ("dap", dap), ("fixture", fixture))}
	with tempfile.TemporaryDirectory(prefix="vero-script-tooling-") as directory:
		root = Path(directory)
		extension, resource = root / "extension.vs", root / "member.txt"
		extension.write_text(EXTENSION, encoding="utf-8")
		catalog = root / "compiler.json"
		catalog.write_bytes(subprocess.check_output([str(fixture), "compiler-catalog"], cwd=root))

		def save():
			destination.parent.mkdir(parents=True, exist_ok=True)
			destination.write_text(json.dumps({"binaries": binaries, "cases": records}, ensure_ascii=False, indent=2), encoding="utf-8")

		client = Client(str(lsp), root, 30)
		try:
			initialized, _ = client.result(client.request("initialize", {"rootUri": root.as_uri(), "locale": "en-US",
				"capabilities": {"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}}))
			legend = initialized["capabilities"]["semanticTokensProvider"]["legend"]
			readonly = 1 << legend["tokenModifiers"].index("readonly")
			client.send("initialized")

			def open_doc(name, text):
				uri = (root / (name + ".vs")).as_uri()
				documents[uri] = text
				client.send("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "veroscript", "version": 1, "text": text}})
				return uri

			def ask(name, method, params):
				response, _ = client.wait(client.request(method, params))
				records.append({"name": name, "request": {"method": method, "params": params}, "response": response,
					"documents": dict(documents), "extension": extension.read_text(encoding="utf-8"),
					"resource": resource.read_text(encoding="utf-8") if resource.exists() else None})
				save()
				assert "error" not in response, (name, response)
				return response["result"]

			for name, prefix, suffix in (
				("closed", 'import compiler "', '";\nvoid main(){}'),
				("unfinished", 'import compiler "', ''),
				("bom", '\ufeffimport compiler "', '";\nvoid main(){}'),
				("comments", 'import /* first */ compiler /* second */ "', '";\nvoid main(){}'),
				("multiline-lf", 'import /* first\n second */ compiler\n "', '";\nvoid main( {'),
				("multiline-crlf", 'import // first\r\ncompiler /* second */\r\n "', '";\r\nvoid main( {'),
				("second", 'import compiler "./extension.vs"; import compiler "', '";\nvoid main(){}'),
				("raw-closed", 'import r"', '";\nvoid main( {'),
				("raw-unfinished", 'import r"', ''),
				("unfinished-line", 'import compiler "', '\nvoid main( {')):
				text = prefix + "ext" + suffix
				uri = open_doc("path-" + name, text)
				caret = position(text, "ext", 3, last=True)
				items = ask("compiler_import_path_" + name, "textDocument/completion", {
					"textDocument": {"uri": uri}, "position": caret, "context": {"triggerKind": 1}})["items"]
				item = next(i for i in items if i["label"] == "extension.vs")
				assert item["textEdit"]["newText"] == "extension.vs", item
				assert item["textEdit"]["range"] == {"start": position(text, "ext", last=True), "end": caret}, item

			for name, text in (("string", 'void main(){print("ext");}'),
				("comment", '/* import compiler "ext */'), ("export", 'export import compiler "ext";'),
				("after-code", 'class A{};\nimport compiler "ext";'),
				("compiler-raw", 'import compiler r"ext";'),
				("raw-string", 'void main(){print(r"ext");}')):
				uri = open_doc("no-path-" + name, text)
				result = ask("no_import_candidates_" + name, "textDocument/completion", {
					"textDocument": {"uri": uri}, "position": position(text, "ext", 3)})
				assert result["items"] == [], result

			for name, text in (("normal", 'import compiler "extension.vs";\nvoid main( {'),
				("commented", 'import /* "ignored" */ compiler "extension.vs";\nvoid main( {'),
				("bom", '\ufeffimport compiler "extension.vs";\nvoid main( {')):
				uri = open_doc("navigation-" + name, text)
				definitions = ask("compiler_import_definition_" + name, "textDocument/definition", {
					"textDocument": {"uri": uri}, "position": position(text, "extension.vs", 3)})
				assert len(definitions) == 1 and definitions[0]["uri"] == extension.as_uri(), definitions
				links = ask("compiler_import_links_" + name, "textDocument/documentLink", {"textDocument": {"uri": uri}})
				assert len(links) == 1 and links[0]["target"] == extension.as_uri(), links
				assert links[0]["range"] == {"start": position(text, "extension.vs"), "end": position(text, "extension.vs", 12)}, links

			for name, prefix in (("multiline-lf", 'import /* first\n second */ compiler\n "'),
				("multiline-crlf", 'import // first\r\ncompiler\r\n "'),
				("second", 'import compiler "./extension.vs"; import compiler "'),
				("raw", 'import r"'), ("raw-bom", '\ufeffimport r"')):
				text = prefix + 'extension.vs";\nvoid main( {'
				uri = open_doc("navigation-extra-" + name, text)
				point = position(text, "extension.vs", 3)
				# The first import of the same-line case uses a different spelling.
				if name == "second":
					point = {"line": 0, "character": len(prefix) + 3}
				definitions = ask("import_definition_" + name, "textDocument/definition", {
					"textDocument": {"uri": uri}, "position": point})
				assert len(definitions) == 1 and definitions[0]["uri"] == extension.as_uri(), definitions
				links = ask("import_links_" + name, "textDocument/documentLink", {"textDocument": {"uri": uri}})
				assert len(links) == (2 if name == "second" else 1) and all(l["target"] == extension.as_uri() for l in links), links
				start = {"line": prefix.count("\n"), "character": len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}
				assert links[-1]["range"] == {"start": start, "end": {**start, "character": start["character"] + 12}}, links

			invalid = 'import "extension.vs\\0invalid";\nvoid main( {'
			invalid_uri = open_doc("invalid-import-nul", invalid)
			assert ask("nul_import_links", "textDocument/documentLink", {"textDocument": {"uri": invalid_uri}}) == []
			assert ask("nul_import_definition", "textDocument/definition", {
				"textDocument": {"uri": invalid_uri}, "position": position(invalid, "extension.vs", 3)}) == []
			invalid_compiler = 'import compiler "extension.vs\\u0000invalid";\nvoid main( {'
			invalid_compiler_uri = open_doc("invalid-compiler-escape", invalid_compiler)
			assert ask("compiler_invalid_escape_links", "textDocument/documentLink", {
				"textDocument": {"uri": invalid_compiler_uri}}) == []
			assert ask("compiler_invalid_escape_definition", "textDocument/definition", {
				"textDocument": {"uri": invalid_compiler_uri}, "position": position(invalid_compiler, "extension.vs", 3)}) == []
			invalid_compiler = 'import compiler "extension.vs\rinvalid";\nvoid main( {'
			invalid_compiler_uri = open_doc("invalid-compiler-cr", invalid_compiler)
			assert ask("compiler_invalid_cr_links", "textDocument/documentLink", {
				"textDocument": {"uri": invalid_compiler_uri}}) == []
			assert ask("compiler_invalid_cr_definition", "textDocument/definition", {
				"textDocument": {"uri": invalid_compiler_uri}, "position": position(invalid_compiler, "extension.vs", 3)}) == []
			multiline_raw = 'import r"extension.vs\ncontinued";\nvoid main( {'
			raw_uri = open_doc("multiline-raw-import", multiline_raw)
			links = ask("multiline_raw_link_range", "textDocument/documentLink", {"textDocument": {"uri": raw_uri}})
			assert len(links) == 1 and links[0]["range"] == {
				"start": position(multiline_raw, "extension.vs"), "end": position(multiline_raw, "continued", 9)}, links

			stdlib = root / "custom-stdlib"
			stdlib.mkdir()
			(stdlib / "unique-extension.vs").write_text(EXTENSION, encoding="utf-8")
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"stdlibSearchPaths": [str(stdlib)]}}})
			text = 'import compiler "unique-ext";\nvoid main( {'
			uri = open_doc("custom-stdlib-import", text)
			items = ask("compiler_import_stdlib_only_completion", "textDocument/completion", {
				"textDocument": {"uri": uri}, "position": position(text, "unique-ext", 10)})["items"]
			item = next(i for i in items if i["label"] == "unique-extension.vs")
			assert item["textEdit"] == {"range": {"start": position(text, "unique-ext"), "end": position(text, "unique-ext", 10)},
				"newText": "unique-extension.vs"}, item
			text = 'import compiler "unique-extension.vs";\nvoid main( {'
			uri = open_doc("custom-stdlib-definition", text)
			definitions = ask("compiler_import_stdlib_only_definition", "textDocument/definition", {
				"textDocument": {"uri": uri}, "position": position(text, "unique-extension.vs", 3)})
			assert len(definitions) == 1 and definitions[0]["uri"] == (stdlib / "unique-extension.vs").as_uri(), definitions

			nested = root / "nested"
			nested.mkdir()
			(nested / "extension.vs").write_text(EXTENSION, encoding="utf-8")
			escaped = 'import "nested\\u002fext";\nvoid main( {'
			escaped_uri = open_doc("escaped-import-prefix", escaped)
			items = ask("escaped_import_completion", "textDocument/completion", {
				"textDocument": {"uri": escaped_uri}, "position": position(escaped, "ext", 3)})["items"]
			item = next(i for i in items if i["label"] == "nested/extension.vs")
			assert item["textEdit"]["newText"] == "nested/extension.vs", item
			assert item["filterText"] == "nested\\u002fextension.vs", item
			resolved = ask("escaped_import_resolve", "completionItem/resolve", item)
			for field in ("label", "labelDetails", "filterText", "insertText", "insertTextFormat", "textEdit", "sortText"):
				assert resolved.get(field) == item.get(field), (field, resolved)
			if os.name == "nt":
				escaped = 'import compiler "nested\\\\ext";\nvoid main( {'
				escaped_uri = open_doc("escaped-windows-import-prefix", escaped)
				items = ask("escaped_windows_import_completion", "textDocument/completion", {
					"textDocument": {"uri": escaped_uri}, "position": position(escaped, "ext", 3)})["items"]
				item = next(i for i in items if i["label"] == "nested\\extension.vs")
				assert item["textEdit"]["newText"] == "nested\\\\extension.vs", item
				assert item["filterText"] == "nested\\\\extension.vs", item
				resolved = ask("escaped_windows_import_resolve", "completionItem/resolve", item)
				for field in ("label", "labelDetails", "filterText", "insertText", "insertTextFormat", "textEdit", "sortText"):
					assert resolved.get(field) == item.get(field), (field, resolved)

			(nested / "print.vs").write_text("export class PathTarget{};", encoding="utf-8")
			escaped = 'import "nested\\u002fpn";\nvoid main( {'
			escaped_uri = open_doc("escaped-fuzzy-import-prefix", escaped)
			items = ask("escaped_fuzzy_import_completion", "textDocument/completion", {
				"textDocument": {"uri": escaped_uri}, "position": position(escaped, "pn", 2)})["items"]
			item = next(i for i in items if i["label"] == "nested/print.vs")
			assert item["filterText"] == "nested\\u002fpnt.vs" and item["textEdit"]["newText"] == "nested/print.vs", item
			escaped = 'import "\\u0065xt";\nvoid main( {'
			escaped_uri = open_doc("escaped-filename-prefix", escaped)
			items = ask("escaped_filename_completion", "textDocument/completion", {
				"textDocument": {"uri": escaped_uri}, "position": position(escaped, "xt", 2)})["items"]
			item = next(i for i in items if i["label"] == "extension.vs")
			assert item["filterText"] == "\\u0065xtension.vs" and item["textEdit"]["newText"] == "extension.vs", item

			dotted = root / "std.compiler.vs"
			dotted.write_text(EXTENSION, encoding="utf-8")
			for name, prefix in (("ordinary", 'import "'), ("raw", 'import r"'), ("compiler", 'import compiler "')):
				text = prefix + 'std.comp";\nvoid main( {'
				uri = open_doc("dotted-path-completion-" + name, text)
				items = ask("dotted_path_completion_" + name, "textDocument/completion", {
					"textDocument": {"uri": uri}, "position": position(text, "std.comp", 8)})["items"]
				item = next(i for i in items if i["label"] == "std.compiler.vs")
				assert item["filterText"] == "std.compiler.vs" and item["insertText"] == "std.compiler.vs", item
				assert item["textEdit"] == {"range": {"start": position(text, "std.comp"), "end": position(text, "std.comp", 8)},
					"newText": "std.compiler.vs"}, item
				assert not any(i["label"] == "std::compiler::vs" for i in items), items
				resolved = ask("dotted_path_resolve_" + name, "completionItem/resolve", item)
				for field in ("label", "labelDetails", "filterText", "insertText", "insertTextFormat", "textEdit", "sortText"):
					assert resolved.get(field) == item.get(field), (field, resolved)
				accepted = text[:len(prefix)] + item["textEdit"]["newText"] + text[len(prefix) + 8:]
				assert accepted == prefix + 'std.compiler.vs";\nvoid main( {'
				accepted_uri = open_doc("dotted-path-accepted-" + name, accepted)
				params = {"textDocument": {"uri": accepted_uri}, "position": position(accepted, "std.compiler.vs", 5)}
				definitions = ask("dotted_path_definition_" + name, "textDocument/definition", params)
				assert len(definitions) == 1 and definitions[0]["uri"] == dotted.as_uri(), definitions
				hover = ask("dotted_path_hover_" + name, "textDocument/hover", params)
				assert "std.compiler.vs" in str(hover) and "std::compiler::vs" not in str(hover), hover
				links = ask("dotted_path_link_" + name, "textDocument/documentLink", {"textDocument": {"uri": accepted_uri}})
				assert len(links) == 1 and links[0]["target"] == dotted.as_uri(), links

			source = 'import compiler "extension.vs";\n@Explain class Original{};\nvoid main(){Generated value=Generated();value.Read;}\n'
			uri = open_doc("business", source)
			params = {"textDocument": {"uri": uri}}
			point = position(source, "value.Read", 7)
			hover = ask("registered_annotation_hover", "textDocument/hover", {**params, "position": position(source, "Explain", 2)})
			assert "Script registered documentation" in str(hover), hover
			first = ask("semantic_missing_resource", "textDocument/semanticTokens/full", params)
			assert token_at(first["data"], legend, point)[0] == "method"
			repeated = ask("semantic_unchanged", "textDocument/semanticTokens/full", params)
			assert repeated["data"] == first["data"]
			resource.write_text("const int Read=7;", encoding="utf-8")
			delta = ask("semantic_created_resource_delta", "textDocument/semanticTokens/full/delta", {**params, "previousResultId": repeated["resultId"]})
			reconstructed = list(first["data"])
			for edit in reversed(delta["edits"]):
				reconstructed[edit["start"]:edit["start"] + edit["deleteCount"]] = edit.get("data", [])
			created = ask("semantic_created_resource", "textDocument/semanticTokens/full", params)
			assert reconstructed == created["data"] != first["data"]
			assert token_at(created["data"], legend, point) == ("property", readonly)
			resource.write_text("int Read=7;", encoding="utf-8")
			changed = ask("semantic_resource_const_removed", "textDocument/semanticTokens/full", params)
			assert token_at(changed["data"], legend, point) == ("property", 0)
			resource.unlink()
			deleted = ask("semantic_deleted_resource", "textDocument/semanticTokens/full", params)
			assert token_at(deleted["data"], legend, point)[0] == "method"
			stamp = extension.stat()
			extension.write_text(EXTENSION.replace("Script registered", "Script changed---"), encoding="utf-8")
			os.utime(extension, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
			hover = ask("extension_content_changed_same_stamp", "textDocument/hover", {**params, "position": position(source, "Explain", 2)})
			assert "Script changed--- documentation" in str(hover), hover
			extension.write_text(EXTENSION, encoding="utf-8")

			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"bindingCatalogPaths": [str(catalog)]}}})
			authoring = 'import "compiler.vs";\nvoid Describe(std::compiler::context ctx,std::compiler::output out){ctx.read_dependency("x");out.add_declaration(ctx.top_scope(),"class X{};");}\n'
			author_uri = open_doc("authoring", authoring)
			items = ask("compiler_service_context_members", "textDocument/completion", {
				"textDocument": {"uri": author_uri}, "position": position(authoring, "ctx.read_dependency", 4)})["items"]
			assert {"phase", "source", "read_dependency", "top_scope", "declarations"} <= {i["label"] for i in items}, items
			hover = ask("compiler_service_scope_hover", "textDocument/hover", {
				"textDocument": {"uri": author_uri}, "position": position(authoring, "ctx.top_scope", 6)})
			assert "std::compiler::scope" in str(hover) and "std.compiler." not in str(hover), hover
			signature = ask("compiler_service_output_signature", "textDocument/signatureHelp", {
				"textDocument": {"uri": author_uri}, "position": position(authoring, "out.add_declaration(", 20)})
			assert any("std::compiler::scope" in s["label"] and "string" in s["label"] for s in signature["signatures"]), signature
		finally:
			client.close()

		source = 'import compiler "std.compiler.vs";\nvoid main(){\nGenerated value=Generated();\nprint(value.Read());\n}\n'
		path = str(root / "debug.vs")
		incoming = queue.Queue()
		messages, requests, responses, events = [], [], {}, []
		process = subprocess.Popen([str(dap)], cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

		def read():
			try:
				while True:
					incoming.put(read_frame(process.stdout))
			except Exception as error:
				incoming.put(error)
		threading.Thread(target=read, daemon=True).start()

		def receive():
			message = incoming.get(timeout=30)
			if isinstance(message, Exception):
				raise message
			messages.append(message)
			if message["type"] == "response":
				responses[message["request_seq"]] = message
			else:
				events.append(message)

		def request(command, arguments=None):
			seq = len(requests) + 1
			message = {"seq": seq, "type": "request", "command": command, "arguments": arguments or {}}
			requests.append(message)
			process.stdin.write(frame(message))
			process.stdin.flush()
			while seq not in responses:
				receive()
			response = responses.pop(seq)
			assert response["success"], response
			return response.get("body", {})

		def event(name):
			while True:
				for index, value in enumerate(events):
					if value.get("event") == name:
						return events.pop(index)
				receive()
		try:
			request("initialize", {"adapterID": "veroscript"})
			request("launch", {"source": source, "fileLabel": path})
			bound = request("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": 4}]})
			assert bound["breakpoints"][0]["verified"], bound
			request("configurationDone")
			event("stopped")
			frames = request("stackTrace", {"threadId": 1})["stackFrames"]
			assert frames[0]["line"] == 4 and Path(frames[0]["source"]["path"]) == Path(path), frames
			request("setBreakpoints", {"source": {"path": path}, "breakpoints": []})
			request("continue", {"threadId": 1})
			event("terminated")
			assert event("exited")["body"]["exitCode"] == 0
			assert "7\n" in "".join(m.get("body", {}).get("output", "") for m in messages if m.get("event") == "output"), messages
			request("disconnect", {"terminateDebuggee": False})
			process.stdin.close()
			assert process.wait(timeout=10) == 0
		finally:
			if process.poll() is None:
				process.kill()
				process.wait(timeout=5)
			records.append({"name": "script_extension_dap", "document": source, "path": path,
				"extension": EXTENSION, "requests": requests, "messages": messages, "exitCode": process.returncode})
			save()


if __name__ == "__main__":
	run(*(Path(a).resolve() for a in sys.argv[1:5]))
