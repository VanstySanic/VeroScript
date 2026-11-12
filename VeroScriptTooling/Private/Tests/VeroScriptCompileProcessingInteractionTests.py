# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Verify compile processing and source metadata through actual LSP/DAP frames."""
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

from VeroScriptLspResponsivenessTest import Client
from VeroScriptLspInteractionTests import frame, read_frame, utf16_offset


def position(source, needle, offset=0):
	before = source[:source.index(needle) + offset]
	return {"line": before.count("\n"),
			"character": len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def run(executable, destination):
	records = []
	destination.parent.mkdir(parents=True, exist_ok=True)

	def save():
		destination.write_text(json.dumps({"executable": str(executable),
			"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(), "cases": records},
			ensure_ascii=False, indent=2), encoding="utf-8")

	with tempfile.TemporaryDirectory(prefix="vero-processing-protocol-") as directory:
		root = Path(directory)
		dependency = root / "builder.txt"
		dependency.write_text("CardBuilder", encoding="utf-8")
		client = Client([str(executable), str(dependency), "lsp"], root, 20)
		documents, catalogs = {}, {}
		try:
			client.result(client.request("initialize", {"locale": "en-US", "capabilities": {
				"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}}))
			client.send("initialized")

			def open_doc(name, source):
				uri = (root / (name + ".vs")).as_uri()
				documents[uri] = source
				client.send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
				return uri

			def ask(name, method, params):
				response, _ = client.wait(client.request(method, params))
				records.append({"name": name, "method": method, "request": params, "response": response,
					"documents": [{"uri": uri, "text": text} for uri, text in documents.items()],
					"dependency": dependency.read_text(encoding="utf-8"), "bindingCatalogs": dict(catalogs)})
				save()
				assert "error" not in response, (name, response)
				return response["result"]

			source = "/// Card documentation.\n@Builder record Card {public:string text;};\n" \
					 'void main(){CardBuilder builder=Card::Create();Card card=builder.Text("Save").Build();}\n'
			uri = open_doc("builder", source)
			params = {"textDocument": {"uri": uri}, "position": position(source, ".Build", 3)}
			items = ask("generated_member_completion", "textDocument/completion", params)["items"]
			assert any(item["label"] == "Build" and item.get("textEdit", {}).get("newText") == "Build" for item in items), items
			hover = ask("generated_type_documentation", "textDocument/hover", {
				"textDocument": {"uri": uri}, "position": position(source, "CardBuilder builder", 3)})
			assert "Generated builder documentation" in str(hover), hover

			outline = ask("processed_document_symbol_ranges", "textDocument/documentSymbol", {
				"textDocument": {"uri": uri}})

			def check_outline(symbol, parent=None):
				extent = tuple(utf16_offset(source, symbol["range"][edge]) for edge in ("start", "end"))
				selected = tuple(utf16_offset(source, symbol["selectionRange"][edge]) for edge in ("start", "end"))
				assert extent[0] <= selected[0] < selected[1] <= extent[1], symbol
				if parent is not None:
					assert parent[0] <= extent[0] <= extent[1] <= parent[1], (symbol, parent)
				assert source[selected[0]:selected[1]] == symbol["name"], symbol
				for child in symbol.get("children", []):
					check_outline(child, extent)

			assert {symbol["name"] for symbol in outline} == {"Card", "main"}, outline
			for symbol in outline:
				check_outline(symbol)
			card = next(symbol for symbol in outline if symbol["name"] == "Card")
			assert {symbol["name"] for symbol in card["children"]} == {"text"}, card

			auto_source = source.replace("CardBuilder builder", "auto builder")
			auto_uri = open_doc("auto-builder", auto_source)
			items = ask("generated_auto_receiver", "textDocument/completion", {
				"textDocument": {"uri": auto_uri}, "position": position(auto_source, ".Build", 3)})["items"]
			assert any(item["label"] == "Build" for item in items), items

			shadow_source = 'class First{public:int first;};class Second{public:int second;};' \
				'void main(){First source=First();auto copy=source;{Second source=Second();copy.fi;}}'
			shadow_uri = open_doc("auto-shadow", shadow_source)
			items = ask("auto_initializer_scope", "textDocument/completion", {
				"textDocument": {"uri": shadow_uri}, "position": position(shadow_source, "copy.fi", 7)})["items"]
			assert any(item["label"] == "first" for item in items) and all(item["label"] != "second" for item in items), items

			snippet_source = source[:source.index("Card card=")] + "builder.Bu;}\n"
			snippet_uri = open_doc("builder-snippet", snippet_source)
			items = ask("generated_member_snippet", "textDocument/completion", {
				"textDocument": {"uri": snippet_uri}, "position": position(snippet_source, ".Bu;", 3)})["items"]
			build = next(item for item in items if item["label"] == "Build")
			assert build["insertTextFormat"] == 2 and build["insertText"] == "Build(${0})", build
			assert build["textEdit"]["newText"] == build["insertText"], build
			signature = ask("generated_member_signature", "textDocument/signatureHelp", {
				"textDocument": {"uri": uri}, "position": position(source, 'Text("Save")', 5)})
			assert signature["signatures"] and "string text" in signature["signatures"][0]["label"], signature

			annotation_source = '@Builder(label="Card") record Card {public:string text;};\n'
			annotation_uri = open_doc("annotation-description", annotation_source)
			hover = ask("processing_annotation_hover", "textDocument/hover", {
				"textDocument": {"uri": annotation_uri}, "position": position(annotation_source, "Builder", 2)})
			assert "Generates a builder type" in str(hover), hover
			hover = ask("processing_annotation_argument_hover", "textDocument/hover", {
				"textDocument": {"uri": annotation_uri}, "position": position(annotation_source, "label", 2)})
			assert "The builder label" in str(hover), hover
			items = ask("processing_annotation_argument_completion", "textDocument/completion", {
				"textDocument": {"uri": annotation_uri}, "position": position(annotation_source, "label", 2)})["items"]
			assert any(i["label"] == "label" for i in items), items

			for name, spelling, matches in (
				("relaxed_canonical", "@GuiWidget", True),
				("relaxed_variant", "@gUIwIDGET", True),
				("strict_canonical", "@StrictWidget", True),
				("strict_variant", "@sTRICTwIDGET", False),
			):
				text = spelling + '(Label="Card",Enabled=true) class Card{};void main(){}'
				policy_uri = open_doc(name, text)
				hover = ask(name + "_hover", "textDocument/hover", {
					"textDocument": {"uri": policy_uri}, "position": position(text, spelling, 3)})
				assert ("documentation." in str(hover)) == matches, (name, hover)
				if matches:
					doc = "Strict widget documentation." if spelling == "@StrictWidget" else "Widget documentation."
					assert str(hover).count(doc) == 1, (name, hover)
				signature = ask(name + "_signature", "textDocument/signatureHelp", {
					"textDocument": {"uri": policy_uri}, "position": position(text, "Enabled", 2)})
				signatures = (signature or {}).get("signatures", [])
				assert bool(signatures) == matches, (name, signature)
				if matches:
					assert len(signatures) == 1 and len(signatures[0]["parameters"]) == 2, signature
					assert signature["activeParameter"] == 1, signature
				hover = ask(name + "_argument_hover", "textDocument/hover", {
					"textDocument": {"uri": policy_uri}, "position": position(text, "Label", 2)})
				assert ("Label documentation." in str(hover)) == matches, (name, hover)
				items = ask(name + "_argument_completion", "textDocument/completion", {
					"textDocument": {"uri": policy_uri}, "position": position(text, "Label", 2)})["items"]
				assert any(i["label"] == "Label" for i in items) == matches, (name, items)
				if matches:
					item = next(i for i in items if i["label"] == "Label")
					assert item["textEdit"]["range"]["start"] == position(text, "Label"), item

			text = '@guiwidget(label="Card") class Card{};void main(){}'
			policy_uri = open_doc("argument-case", text)
			hover = ask("annotation_argument_case_sensitive", "textDocument/hover", {
				"textDocument": {"uri": policy_uri}, "position": position(text, "label", 2)})
			assert "Label documentation." not in str(hover), hover
			for name, text in (
				("unfinished", '@guiwidget(Label="Card",'),
				("preceding_error", 'void broken( {\n@guiwidget(Label="Card",Enabled=true) class Card{};'),
				("following_error", '@guiwidget(Label="Card",Enabled=true) class Card{};void broken( {'),
			):
				policy_uri = open_doc("annotation-" + name, text)
				point = position(text, 'Label="Card",', len('Label="Card",'))
				signature = ask("annotation_signature_" + name, "textDocument/signatureHelp", {
					"textDocument": {"uri": policy_uri}, "position": point})
				assert signature and signature["signatures"][0]["label"] == "@GuiWidget(Label: string, Enabled: bool)", signature
				assert signature["activeParameter"] == 1, signature

			text = '@LegacyOptions(values=1,label=2) class Card{};void main(){}'
			policy_uri = open_doc("legacy-annotation", text)
			signature = ask("legacy_annotation_parameters", "textDocument/signatureHelp", {
				"textDocument": {"uri": policy_uri}, "position": position(text, "label", 2)})
			assert signature and len(signature["signatures"]) == 1, signature
			assert len(signature["signatures"][0]["parameters"]) == 2 and signature["activeParameter"] == 1, signature

			text = 'void inspect(annotation_info a,annotation_definition_info d){a.matches_name("GuiWidget");d.case_sensitive();}void main(){}'
			policy_uri = open_doc("annotation-reflection", text)
			for member, prefix, arity in (("matches_name", "a.", 1), ("case_sensitive", "d.", 0)):
				items = ask("annotation_reflection_completion_" + member, "textDocument/completion", {
					"textDocument": {"uri": policy_uri}, "position": position(text, prefix + member, len(prefix) + 4)})["items"]
				item = next(i for i in items if i["label"] == member)
				assert item["kind"] == 2, item
				signature = ask("annotation_reflection_signature_" + member, "textDocument/signatureHelp", {
					"textDocument": {"uri": policy_uri}, "position": position(text, member + "(", len(member) + 1)})
				assert signature and len(signature["signatures"][0]["parameters"]) == arity, signature

			index_source = "@Builder record Card {public:string text;};\n"
			index_uri = open_doc("index", index_source)
			first = ask("processor_snapshot", "textDocument/semanticTokens/full", {"textDocument": {"uri": index_uri}})
			repeated = ask("processor_snapshot_repeated", "textDocument/semanticTokens/full", {"textDocument": {"uri": index_uri}})
			assert repeated["data"] == first["data"], (first, repeated)
			dependency.write_text("ChangedBuilder", encoding="utf-8")
			symbols = ask("processor_dependency_changed", "workspace/symbol", {"query": "ChangedBuilder"})
			assert any(item["name"] == "ChangedBuilder" for item in symbols), symbols
			dependency.write_text("CardBuilder", encoding="utf-8")

			for newline in ("\n", "\r\n"):
				mapped = newline.join(["#define NUMBER 42", "/*tooling-active-shift*/", "void main(){", "int local=NUMBER;", "print(local);", "}", ""])
				mapped_uri = open_doc("mapped-" + str(len(newline)), mapped)
				definition = ask("active_source_definition_" + str(len(newline)), "textDocument/definition", {
					"textDocument": {"uri": mapped_uri}, "position": position(mapped, "print(local)", 8)})
				assert len(definition) == 1 and definition[0]["uri"] == mapped_uri, definition
				assert definition[0]["range"]["start"] == position(mapped, "int local", 4), definition
				tokens = ask("active_source_semantics_" + str(len(newline)), "textDocument/semanticTokens/full", {
					"textDocument": {"uri": mapped_uri}})
				assert tokens["data"], tokens

				# RawSource edits also change the preprocessor's activity line table.
				raw = newline.join(["#if 1", "void main(){/*tooling-raw-shift*/", "int local=42;",
					"print(local);", "}", "#endif", ""])
				raw_uri = open_doc("raw-mapped-" + str(len(newline)), raw)
				references = ask("raw_source_references_" + str(len(newline)), "textDocument/references", {
					"textDocument": {"uri": raw_uri}, "position": position(raw, "print(local)", 8),
					"context": {"includeDeclaration": True}})
				assert len(references) == 2 and all(r["uri"] == raw_uri for r in references), references
				assert {r["range"]["start"]["line"] for r in references} == {2, 3}, references

			reflection = 'void main(){type<int> info=type<int>();source_node_info n=info.declaration();n.documentation();}\n'
			ref_uri = open_doc("metadata", reflection)
			items = ask("reflection_members", "textDocument/completion", {
				"textDocument": {"uri": ref_uri}, "position": position(reflection, "n.documentation", 2)})["items"]
			assert {"annotations", "source_metadata", "parameter", "documentation"} <= {i["label"] for i in items}, items
			hover = ask("reflection_return_type", "textDocument/hover", {
				"textDocument": {"uri": ref_uri}, "position": position(reflection, "info.declaration", 8)})
			assert "source_node_info" in str(hover), hover
			host_source = '@hOSTwIDGET(Label="Card",Enabled=true) class Card{};void main(){}'
			host_uri = open_doc("host-name-policy", host_source)
			for policy_version, case_sensitive in enumerate((False, True, False)):
				catalog = root / ("host-strict.json" if case_sensitive else "host-relaxed.json")
				catalog.write_text(json.dumps({"version": 1, "annotations": [{
					"name": "@HostWidget", "caseSensitive": case_sensitive,
					"documentation": "Host widget documentation.", "arguments": [
						{"name": "Label", "detail": "string", "documentation": "Host label documentation."},
						{"name": "Enabled", "detail": "bool"}]}]}), encoding="utf-8")
				catalogs = {str(catalog): json.loads(catalog.read_text(encoding="utf-8"))}
				client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
					"bindingCatalogPaths": [str(catalog)]}}})
				label = "host_policy_" + str(policy_version) + "_" + str(case_sensitive).lower()
				hover = ask(label + "_hover", "textDocument/hover", {
					"textDocument": {"uri": host_uri}, "position": position(host_source, "hOSTwIDGET", 3)})
				assert str(hover).count("Host widget documentation.") == (0 if case_sensitive else 1), hover
				signature = ask(label + "_signature", "textDocument/signatureHelp", {
					"textDocument": {"uri": host_uri}, "position": position(host_source, "Enabled", 2)})
				assert bool((signature or {}).get("signatures")) == (not case_sensitive), signature
				if not case_sensitive:
					assert signature["signatures"][0]["label"] == "@HostWidget(Label: string, Enabled: bool)", signature
					assert signature["activeParameter"] == 1, signature

			for name, parameters, expected_first in (
				("quoted-comma", 'string label="a,b"', 'string label="a,b"'),
				("multiline-lf", '\n string label', 'string label'),
				("multiline-crlf", '\r\n string label', 'string label'),
				("initializer", 'array<int> values={1,2}', 'array<int> values={1,2}'),
				("nested-generics", 'map<string,array<int>> values', 'map<string,array<int>> values'),
				("raw-string", 'string label=r"(a,b)"', 'string label=r"(a,b)"'),
				("character", "char separator=','", "char separator=','"),
				("comment", 'int value=1/* , ) ignored */', 'int value=1/* , ) ignored */'),
				("callable", 'fn(int,string)->array<int> callback', 'fn(int,string)->array<int> callback'),
			):
				catalog = root / ("legacy-" + name + ".json")
				detail = "annotation (" + parameters + ", bool enabled\r\n)"
				catalogs = {str(catalog): {"version": 1, "annotations": [{"name": "@Options", "detail": detail}]}}
				catalog.write_text(json.dumps(catalogs[str(catalog)]), encoding="utf-8")
				client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
					"bindingCatalogPaths": [str(catalog)]}}})
				text = '@Options(label="Card",enabled=true) class Card{};void main(){}'
				legacy_uri = open_doc("legacy-" + name, text)
				signature = ask("legacy_parameter_" + name, "textDocument/signatureHelp", {
					"textDocument": {"uri": legacy_uri}, "position": position(text, "enabled", 2)})
				assert signature and len(signature["signatures"]) == 1, signature
				assert [p["label"] for p in signature["signatures"][0]["parameters"]] == [expected_first, "bool enabled"], signature
				assert signature["activeParameter"] == 1, signature

		finally:
			client.close()

		for case_name, source, breakpoint_line in (
			("active_source_dap", "/*tooling-active-shift*/\nvoid main(){\nint local=42;\nprint(local);\n}\n", 4),
			("raw_source_dap", "#if 1\nvoid main(){/*tooling-raw-shift*/\nprint(42);\n}\n#endif\n", 3)):
			path = str(root / "mapped-dap.vs")
			incoming = queue.Queue()
			messages, requests, responses, events = [], [], {}, []
			process = subprocess.Popen([str(executable), str(dependency), "dap"], cwd=root,
				stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

			def read():
				try:
					while True:
						incoming.put(read_frame(process.stdout))
				except Exception as error:
					incoming.put(error)
			threading.Thread(target=read, daemon=True).start()

			def receive():
				message = incoming.get(timeout=15)
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
				request("initialize", {"adapterID": "veroscript", "linesStartAt1": True, "columnsStartAt1": True})
				request("launch", {"source": source, "fileLabel": path})
				breakpoints = request("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": breakpoint_line}]})
				assert breakpoints["breakpoints"][0]["verified"], breakpoints
				request("configurationDone")
				event("stopped")
				frames = request("stackTrace", {"threadId": 1})["stackFrames"]
				assert frames[0]["line"] == breakpoint_line and Path(frames[0]["source"]["path"]) == Path(path), frames
				request("setBreakpoints", {"source": {"path": path}, "breakpoints": []})
				request("continue", {"threadId": 1})
				event("terminated")
				assert event("exited")["body"]["exitCode"] == 0
				request("disconnect", {"terminateDebuggee": False})
				process.stdin.close()
				assert process.wait(timeout=10) == 0
			finally:
				if process.poll() is None:
					process.kill()
					process.wait(timeout=5)
				records.append({"name": case_name, "protocol": "dap", "document": source,
					"path": path, "requests": requests, "messages": messages, "exitCode": process.returncode})
				save()


if __name__ == "__main__":
	run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
