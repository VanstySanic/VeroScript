# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise native annotation preparation through real LSP processes."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import queue
import subprocess
import threading
import time

from VeroScriptLspResponsivenessTest import Client
from VeroScriptLspInteractionTests import marked_source
from VeroScriptLspInteractionTests import apply_edit, frame, read_frame


def run_dap(executable, root, dependency, records, destination):
	requests, messages, events, responses = [], [], [], {}
	incoming = queue.Queue()
	source = "struct A {@Getter int value=42;};\nvoid main(){\nA a=A();\nprint(a.Read());\n}\n"
	label = str(root / "generated-dap.vs")
	process = subprocess.Popen([str(executable), str(dependency), "cacheable", "dap"], cwd=root,
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
	def request(command, arguments=None, success=True):
		identifier = len(requests) + 1
		message = {"seq": identifier, "type": "request", "command": command, "arguments": arguments or {}}
		requests.append(message)
		process.stdin.write(frame(message))
		process.stdin.flush()
		while identifier not in responses:
			receive()
		response = responses.pop(identifier)
		assert response["success"] == success, response
		return response.get("body", {})
	def event(name):
		while True:
			for index, value in enumerate(events):
				if value.get("event") == name:
					return events.pop(index)
			receive()
	try:
		request("initialize", {"adapterID": "veroscript", "linesStartAt1": True, "columnsStartAt1": True})
		request("launch", {"source": source, "fileLabel": label, "stopOnEntry": False})
		request("setBreakpoints", {"source": {"path": label}, "breakpoints": [{"line": 4}]})
		request("configurationDone")
		event("stopped")
		# The debugger can stop on the receiver/argument instructions before
		# entering the call. Follow those valid stops to the generated frame.
		for _ in range(16):
			request("stepIn", {"threadId": 1})
			event("stopped")
			frames = request("stackTrace", {"threadId": 1})["stackFrames"]
			generated = frames[0]["source"]
			if generated["path"].startswith("veroscript-generated:"):
				break
		assert generated["path"].startswith("veroscript-generated:") and generated["sourceReference"] > 0, frames
		reference = generated["sourceReference"]
		text = request("source", {"sourceReference": reference, "source": generated})
		assert "Read" in text["content"] and "return value" in text["content"], text
		assert request("stackTrace", {"threadId": 1})["stackFrames"][0]["source"]["sourceReference"] == reference
		request("source", {"sourceReference": reference, "source": {"path": label}}, success=False)
		request("source", {"sourceReference": reference + 0.5}, success=False)
		request("source", {"sourceReference": -1}, success=False)
		request("setBreakpoints", {"source": {"path": label}, "breakpoints": []})
		request("continue", {"threadId": 1})
		event("terminated")
		assert event("exited")["body"]["exitCode"] == 0
		assert request("source", {"sourceReference": reference}) == text
		request("disconnect", {"terminateDebuggee": False})
		process.stdin.close()
		assert process.wait(timeout=10) == 0
	finally:
		if process.poll() is None:
			process.kill()
			process.wait(timeout=5)
		records.append({"name": "generated_dap_source", "protocol": "dap", "document": source, "path": label,
						"requests": requests, "messages": messages, "exitCode": process.returncode})
		destination.write_text(json.dumps({"executable": str(executable),
			"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(), "cases": records},
			ensure_ascii=False, indent=2), encoding="utf-8")


def run(executable, destination):
	records = []
	destination.parent.mkdir(parents=True, exist_ok=True)
	with tempfile.TemporaryDirectory(prefix="vero-annotation-lsp-") as directory:
		root = Path(directory)
		dependency = root / "metadata.txt"
		dependency.write_text("Read", encoding="utf-8")
		for mode in ("cacheable", "volatile"):
			client = Client([str(executable), str(dependency), mode, "lsp"], root, 20)
			try:
				initialize, _ = client.result(client.request("initialize", {"locale": "en-US", "capabilities": {
					"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}}))
				client.send("initialized")
				documents = {}

				def open_doc(name, source):
					uri = (root / (mode + "-" + name + ".vs")).as_uri()
					documents[uri] = source
					client.send("textDocument/didOpen", {"textDocument": {
						"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
					return uri

				def ask(name, method, params, expected_error=None):
					response, _ = client.wait(client.request(method, params))
					records.append({"name": name, "method": method, "request": params, "response": response,
									"documents": [{"uri": uri, "text": source} for uri, source in documents.items()],
									"dependency": {"path": str(dependency), "exists": dependency.exists(),
												   "content": dependency.read_text() if dependency.exists() else ""}})
					destination.write_text(json.dumps({"executable": str(executable),
						"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(), "cases": records},
						ensure_ascii=False, indent=2), encoding="utf-8")
					if expected_error is not None:
						assert response.get("error", {}).get("code") == expected_error, (name, response)
						return response["error"]
					assert "error" not in response, (name, response)
					return response["result"]

				source = "struct A { @Getter int value=4; };\nvoid main(){A a=A();a.Read();a.Changed();}\n"
				uri = open_doc("cache", source)
				params = {"textDocument": {"uri": uri}}
				first = ask(mode + "_semantic_initial", "textDocument/semanticTokens/full", params)
				assert first["data"], first
				if mode == "volatile":
					second = ask("volatile_semantic_repeated", "textDocument/semanticTokens/full", params)
					assert second["data"] != first["data"], (first, second)
					continue
				repeated = ask("cacheable_semantic_repeated", "textDocument/semanticTokens/full", params)
				assert repeated["data"] == first["data"], (first, repeated)
				dependency.write_text("Changed", encoding="utf-8")
				changed = ask("dependency_changed", "textDocument/semanticTokens/full", params)
				assert changed["data"] != first["data"], (first, changed)
				dependency.unlink()
				removed = ask("dependency_removed", "textDocument/semanticTokens/full", params)
				assert removed["data"] == first["data"], (first, removed)
				dependency.write_text("Changed", encoding="utf-8")
				created = ask("dependency_created", "textDocument/semanticTokens/full", params)
				assert created["data"] == changed["data"], (changed, created)
				dependency.write_text("Read", encoding="utf-8")

				navigation, position = marked_source(
					'#define INTRO \\\nvoid unused(){}\nINTRO\n'
					'struct A { /*😀*/ @Getter int value=4; };\n'
					'void main(){A a=A();int broken=missing;print(a.Re|ad());}\n')
				nav_uri = open_doc("navigation", navigation)
				params = {"textDocument": {"uri": nav_uri}, "position": position}
				# General processing anchors default declaration generation to
				# the annotated declaration's range, rather than its annotation.
				origin_before = navigation[:navigation.index("int value")]
				origin = {"line": origin_before.count("\n"),
						  "character": len(origin_before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}
				for method in ("definition", "implementation"):
					result = ask("generated_" + method + "_with_error", "textDocument/" + method, params)
					assert result == [{"uri": nav_uri, "range": {
						"start": origin, "end": {**origin, "character": origin["character"] + 1}}}], result
				outline = ask("generated_outline", "textDocument/documentSymbol", {"textDocument": {"uri": nav_uri}})
				def names(symbols):
					return [name for symbol in symbols for name in [symbol["name"], *names(symbol.get("children", []))]]
				assert "A" in names(outline) and "value" in names(outline) and "Read" not in names(outline), outline
				renamed = ask("generated_prepare_rename", "textDocument/prepareRename", params, -32602)
				assert "read-only" in renamed["message"], renamed

				for name, marked, target_line in (
					("prototype_parameter", "int Calc(int declared);\nint Calc(int actual){\nreturn act|ual;\n}\nvoid main(){print(Calc(1));}", 1),
					("method_definition_parameter", "struct C {int Read(int declared);};\nint C::Read(int actual){\nreturn act|ual;\n}\nvoid main(){C c=C();print(c.Read(1));}", 1),
				):
					text, position = marked_source(marked)
					doc = open_doc(name, text)
					params = {"textDocument": {"uri": doc}, "position": position}
					result = ask(name, "textDocument/definition", params)
					character = text.splitlines()[target_line].index("actual")
					target = {"line": target_line, "character": character}
					assert result == [{"uri": doc, "range": {"start": target, "end": target}}], result
					references = ask(name + "_references", "textDocument/references",
									 {**params, "context": {"includeDeclaration": True}})
					sites = []
					offset = 0
					while (offset := text.find("actual", offset)) >= 0:
						before = text[:offset]
						sites.append((doc, before.count("\n"), len(before.rsplit("\n", 1)[-1])))
						offset += len("actual")
					assert {(hit["uri"], hit["range"]["start"]["line"], hit["range"]["start"]["character"])
							for hit in references} == set(sites), references
					renamed = ask(name + "_rename", "textDocument/rename", {**params, "newName": "renamed"})
					edits = renamed.get("changes", {}).get(doc, [])
					assert len(edits) == 2, renamed
					applied = text
					for edit in sorted(edits, key=lambda item: (item["range"]["start"]["line"],
															   item["range"]["start"]["character"]), reverse=True):
						applied = apply_edit(applied, edit)
					assert "actual" not in applied and "declared" in applied and applied.count("renamed") == 2, applied

				library = '#define INTRO \\\nvoid library_helper(){}\nINTRO\nexport struct Box<T>{@Getter T value;Box(T initial){value=initial;}};'
				library_uri = open_doc("library", library)
				imported, position = marked_source('import "cacheable-library.vs";void main(){Box<int> b(4);int broken=missing;print(b.Re|ad());}')
				imported_uri = open_doc("imported", imported)
				params = {"textDocument": {"uri": imported_uri}, "position": position}
				hover = ask("generated_generic_hover", "textDocument/hover", params)
				assert "int()" in hover["contents"]["value"], hover
				definition = ask("generated_imported_definition", "textDocument/definition", params)
				assert len(definition) == 1 and definition[0]["uri"] == library_uri, definition
				assert definition[0]["range"]["start"] == {"line": 3, "character": library.splitlines()[3].index("T value")}, definition
				completion_source, completion_position = marked_source('import "cacheable-library.vs";void main(){Box<int> b(4);b.Re|;}')
				completion_uri = open_doc("generic-completion", completion_source)
				completion = ask("generated_generic_completion", "textDocument/completion", {
					"textDocument": {"uri": completion_uri}, "position": completion_position})
				chosen = next(item for item in completion["items"] if item["label"] == "Read")
				assert "int()" in chosen["detail"] and chosen["textEdit"]["newText"] == chosen["insertText"], chosen
				signature_source, signature_position = marked_source('import "cacheable-library.vs";void main(){Box<int> b(4);b.Read(|);}')
				signature_uri = open_doc("generic-signature", signature_source)
				signature = ask("generated_generic_signature", "textDocument/signatureHelp", {
					"textDocument": {"uri": signature_uri}, "position": signature_position})
				assert any("int()" in item["label"] for item in signature["signatures"]), signature
			finally:
				client.close()
		run_dap(executable, root, dependency, records, destination)
	print(f"{len(records)} native annotation LSP/DAP cases passed")
	return records


if __name__ == "__main__":
	executable = Path(sys.argv[1]).resolve()
	destination = Path(sys.argv[2])
	try:
		cases = run(executable, destination)
		destination.parent.mkdir(parents=True, exist_ok=True)
		destination.write_text(json.dumps({"executable": str(executable),
			"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(), "cases": cases},
			ensure_ascii=False, indent=2), encoding="utf-8")
	except Exception:
		raise
