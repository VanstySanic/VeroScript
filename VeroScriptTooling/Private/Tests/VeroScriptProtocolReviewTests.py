# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Preserve literal contents, refresh binding snapshots and honor DAP stack paging."""
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

from VeroScriptLspInteractionTests import frame, read_frame


class Wire:
	def __init__(self, executable, root, dap=False, arguments=()):
		self.process = subprocess.Popen([str(executable), *arguments], cwd=root, stdin=subprocess.PIPE,
			stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
		self.dap, self.sequence = dap, 0
		self.incoming, self.events, self.messages, self.requests = queue.Queue(), [], [], []
		threading.Thread(target=self.read, daemon=True).start()

	def read(self):
		try:
			while True:
				message = read_frame(self.process.stdout)
				if message is None:
					raise RuntimeError("Server closed stdout")
				self.incoming.put(message)
		except Exception as error:
			self.incoming.put(error)

	def write(self, message):
		self.process.stdin.write(frame(message))
		self.process.stdin.flush()

	def send(self, method, params=None):
		assert not self.dap
		message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
		self.requests.append(message)
		self.write(message)

	def receive(self):
		message = self.incoming.get(timeout=20)
		if isinstance(message, Exception):
			raise message
		self.messages.append(message)
		if not self.dap and message.get("method", "").startswith("client/") and "id" in message:
			self.write({"jsonrpc": "2.0", "id": message["id"], "result": None})
		else:
			self.events.append(message)
		return message

	def request(self, method, params=None):
		self.sequence += 1
		message = ({"seq": self.sequence, "type": "request", "command": method,
					"arguments": params or {}} if self.dap else
				   {"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params or {}})
		self.requests.append(message)
		self.write(message)
		while True:
			reply = self.receive()
			if (self.dap and reply.get("request_seq") == self.sequence
					or not self.dap and reply.get("id") == self.sequence):
				return reply

	def event(self, name):
		while True:
			for index, message in enumerate(self.events):
				if message.get("event") == name:
					return self.events.pop(index)
			self.receive()

	def close(self):
		if self.process.poll() is None:
			if self.dap:
				assert self.request("disconnect", {"terminateDebuggee": False})["success"]
				self.process.stdin.close()
			else:
				assert "error" not in self.request("shutdown")
				self.send("exit")
			self.process.wait(timeout=10)
		assert self.process.returncode == 0, self.process.returncode


def point(source, offset):
	prefix = source[:offset]
	return {"line": prefix.count("\n"),
			"character": len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def apply_edits(source, edits):
	lines = source.splitlines(keepends=True)

	def offset(position):
		line, column = position["line"], position["character"]
		assert 0 <= line <= len(lines), position
		if line == len(lines):
			assert column == 0 and source.endswith("\n"), position
			return len(source)
		current = lines[line]
		for index in range(len(current) + 1):
			if len(current[:index].encode("utf-16-le")) // 2 == column:
				return sum(map(len, lines[:line])) + index
		raise AssertionError(position)

	replacements = []
	for edit in edits:
		start, end = offset(edit["range"]["start"]), offset(edit["range"]["end"])
		assert start <= end
		replacements.append((start, end, edit["newText"]))
	for start, end, text in sorted(replacements, reverse=True):
		source = source[:start] + text + source[end:]
	return source


def run(lsp, dap, destination):
	records, documents, sessions = [], {}, []
	binaries = {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
				for name, path in (("lsp", lsp), ("dap", dap))}
	with tempfile.TemporaryDirectory(prefix="vero protocol review ") as directory:
		root = Path(directory).resolve()
		catalog = root / "bindings [host].json"

		def catalog_text(target):
			return json.dumps({"version": 1, "functions": [], "types": [],
				"typeAliases": [{"name": "HostAlias", "typeParams": [], "targetTypeName": target}]})

		catalog.write_text(catalog_text("int"), encoding="utf-8")
		(root / "shared.vs").write_text("", encoding="utf-8")
		(root / "a😀.vs").write_text("", encoding="utf-8")
		(root / "macros.vs").write_text("#define_export import print(1);\n", encoding="utf-8")
		client = Wire(lsp, root)
		sessions.append(client)

		def save():
			destination.parent.mkdir(parents=True, exist_ok=True)
			destination.write_text(json.dumps({"binaries": binaries, "cases": records,
				"messages": [c.messages for c in sessions], "requests": [c.requests for c in sessions]},
				ensure_ascii=False, indent=2), encoding="utf-8")

		def ask(name, method, params=None, server=None, success=True):
			server = server or client
			response = server.request(method, params)
			records.append({"name": name, "request": server.requests[-1], "response": response,
				"documents": dict(documents),
				"catalog": catalog.read_text(encoding="utf-8") if catalog.exists() else None,
				"catalogFiles": {str(p): p.read_text(encoding="utf-8") for p in root.glob("*.json")}})
			save()
			assert (response.get("success") == success if server.dap else "error" not in response), (name, response)
			return response.get("body", {}) if server.dap else response["result"]

		def open_doc(name, source):
			uri = (root / (name + ".vs")).as_uri()
			documents[uri] = source
			client.send("textDocument/didOpen", {"textDocument": {"uri": uri,
				"languageId": "veroscript", "version": 1, "text": source}})
			return uri

		def change_doc(uri, source, version):
			documents[uri] = source
			client.send("textDocument/didChange", {"textDocument": {"uri": uri, "version": version},
				"contentChanges": [{"text": source}]})

		def watched():
			client.send("workspace/didChangeWatchedFiles",
				{"changes": [{"uri": catalog.as_uri(), "type": 2}]})

		try:
			ask("initialize", "initialize", {"rootUri": root.as_uri(), "locale": "en-US",
				"capabilities": {"workspace": {"didChangeWatchedFiles": {
					"dynamicRegistration": True, "relativePatternSupport": True}}}})
			catalog_paths = [catalog.name, str(catalog), str(root) + "/./" + catalog.name]
			if sys.platform == "win32":
				catalog_paths.append(str(catalog).upper())
			client.send("workspace/didChangeConfiguration",
				{"settings": {"veroscript": {"bindingCatalogPaths": catalog_paths}}})
			client.send("initialized")
			# Organizing imports must preserve physical source, including code
			# which the current preprocessor branch does not happen to execute.
			imp = 'import "shared.vs";'
			organize_cases = [
				("simple", imp + "\n" + imp + "\nvoid main(){}\n", imp + "\nvoid main(){}\n"),
				("crlf-tabs", imp + "\r\n\t" + imp + "\r\n", imp + "\r\n"),
				("eof", imp + "\n" + imp, imp + "\n"),
				("optional-semicolon", 'import "shared.vs"\nimport "shared.vs"\n', 'import "shared.vs"\n'),
				("bom-unicode", '\ufeffimport "a😀.vs";\nimport "a😀.vs";\n', '\ufeffimport "a😀.vs";\n'),
				("reexport", imp + "\nexport " + imp + "\nexport " + imp + "\n", imp + "\nexport " + imp + "\n"),
				("branches", "#if 0\n" + imp + "\n#else\n" + imp + "\n#endif\n", None),
				("within-branches", "#if 0\n" + imp + "\n" + imp + "\n#else\n" + imp + "\n" + imp + "\n#endif\n",
					"#if 0\n" + imp + "\n" + imp + "\n#else\n" + imp + "\n#endif\n"),
				("macro-change", '#define PATH "a.vs"\nimport PATH;\n#undef PATH\n#define PATH "b.vs"\nimport PATH;\n', None),
				("keyword-macro", '#define import print(1);\n' + imp + "\n" + imp + "\n", None),
				("export-macro", '#define export print(1);\nexport ' + imp + "\nexport " + imp + "\n", None),
				("imported-keyword-macro", 'import "macros.vs";\n' * 3, None),
				("undefined-keyword-macro", '#define import print(1);\n' + imp + "\n" + imp + "\n#undef import\n" + imp + "\n" + imp + "\n",
					'#define import print(1);\n' + imp + "\n" + imp + "\n#undef import\n" + imp + "\n"),
				("inactive-keyword-macro", '#if 0\n#define import print(1);\n#endif\n' + imp + "\n" + imp + "\n",
					'#if 0\n#define import print(1);\n#endif\n' + imp + "\n"),
				("unresolved", 'import "missing.vs";\n' * 2, None),
				("other-statements", imp + " print(1);\n" + imp + " print(1);\n", None),
				("raw-string", 'void main(){string text=r"\nimport module;\nimport module;\n";print(text);}\n', None),
				("block-comment", "/*\n" + imp + "\n" + imp + "\n*/\n", None),
				("after-comment", "/* keep */\n" + imp + "\n" + imp + "\n", "/* keep */\n" + imp + "\n"),
				("trailing-comment", imp + " // keep\n" + imp + " // keep\n", None),
				("inline-comment", 'import /* keep */ "shared.vs";\nimport /* keep */ "shared.vs";\n', None),
				("scopes", "namespace A{\n" + imp + "\n}\nnamespace B{\n" + imp + "\n}\n", None),
				("function-body", "void main(){\n" + imp + "\n" + imp + "\n}\n", None),
				("continued-comment", "// continued \\\n" + imp + "\n" + imp + "\n", None),
				("unfinished", imp + "\n" + imp + '\nstring text="unfinished', None),
			]
			for tag, original, expected in organize_cases:
				uri = open_doc("organize-" + tag, original)
				params = {"textDocument": {"uri": uri}, "range": {"start": {"line": 0, "character": 0},
					"end": {"line": 0, "character": 0}}, "context": {"diagnostics": [], "only": ["source.organizeImports"]}}
				actions = ask("organize-" + tag, "textDocument/codeAction", params)
				if expected is None:
					assert actions == [], (tag, actions)
					continue
				assert len(actions) == 1 and actions[0]["kind"] == "source.organizeImports", actions
				edits = actions[0]["edit"]["changes"][uri]
				assert all(e["newText"] == "" and e["range"]["start"]["line"] > 0 for e in edits), edits
				applied = apply_edits(original, edits)
				assert applied == expected, (tag, applied, expected)
				change_doc(uri, applied, 2)
				assert ask("organize-idempotent-" + tag, "textDocument/codeAction", params) == [], tag
			for name, settings, changed in (
					("host-keyword", {"defines": ["import=print(1);"]}, False),
					("host-function-keyword", {"defines": [], "predefinedMacros": [
						{"name": "import", "parameters": [], "body": "print(1);"}]}, True)):
				client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": settings}})
				original = imp + "\n" + imp + "\n"
				uri = open_doc("organize-" + name, original)
				params["textDocument"]["uri"] = uri
				actions = ask("organize-" + name, "textDocument/codeAction", params)
				if changed:
					assert len(actions) == 1, actions
					assert apply_edits(original, actions[0]["edit"]["changes"][uri]) == imp + "\n"
				else:
					assert actions == [], actions
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"defines": [], "predefinedMacros": []}}})
			uri = open_doc("organize-overlay-macros", 'import "overlay-macros.vs";\n' * 3)
			(root / "overlay-macros.vs").write_text("", encoding="utf-8")
			open_doc("overlay-macros", "#define_export import print(1);\n")
			params["textDocument"]["uri"] = uri
			assert ask("organize-overlay-macros", "textDocument/codeAction", params) == []
			for newline, tag in (("\n", "lf"), ("\r\n", "crlf")):
				for incomplete in (False, True):
					name = tag + ("-unfinished" if incomplete else "-complete")
					source = 'void main(){string text=r"start' + newline + "  int   x=1;}" + newline
					if not incomplete:
						source += 'tail\\";' + newline + "int   y=2;" + newline + "}"
					uri = open_doc("raw-" + name, source)
					doc = {"textDocument": {"uri": uri}}
					selection = {"start": {"line": 1, "character": 0},
								 "end": {"line": 2, "character": 0}}
					edits = ask("raw-range-" + name, "textDocument/rangeFormatting",
						{**doc, "range": selection, "options": {"tabSize": 4, "insertSpaces": True}})
					assert edits == [], edits
					for trigger, position in (("\n", {"line": 2, "character": 0}),
							(";", {"line": 1, "character": 12}), ("}", {"line": 1, "character": 13})):
						edits = ask("raw-on-type-" + name + repr(trigger), "textDocument/onTypeFormatting",
							{**doc, "position": position, "ch": trigger,
							 "options": {"tabSize": 4, "insertSpaces": True}})
						assert edits == [] and apply_edits(source, edits) == source, edits
					if incomplete:
						continue
					edits = ask("raw-following-code-" + tag, "textDocument/rangeFormatting",
						{**doc, "range": {"start": {"line": 3, "character": 0},
							"end": {"line": 4, "character": 0}},
						 "options": {"tabSize": 4, "insertSpaces": True}})
					assert edits and "int y = 2;" in edits[0]["newText"], edits
					formatted = apply_edits(source, edits)
					assert 'r"start' + newline + "  int   x=1;}" + newline + 'tail\\";' in formatted, formatted
					change_doc(uri, formatted, 2)
					repeated = ask("raw-following-idempotent-" + tag, "textDocument/rangeFormatting",
						{**doc, "range": {"start": {"line": 3, "character": 0},
							"end": {"line": 4, "character": 0}},
						 "options": {"tabSize": 4, "insertSpaces": True}})
					assert apply_edits(formatted, repeated) == formatted, repeated

				# Enter has already added the newline before this request is sent.
				entered = "void main(){" + newline + "  int value=1;" + newline + newline + "}"
				uri = open_doc("enter-" + tag, entered)
				params = {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 0},
					"ch": "\n", "options": {"tabSize": 2, "insertSpaces": True}}
				edits = ask("enter-preserved-" + tag, "textDocument/onTypeFormatting", params)
				formatted = apply_edits(entered, edits)
				assert formatted.count(newline) == entered.count(newline) and "int value = 1;" in formatted, formatted
				change_doc(uri, formatted, 2)
				repeated = ask("enter-idempotent-" + tag, "textDocument/onTypeFormatting", params)
				assert apply_edits(formatted, repeated) == formatted, repeated

			source = "void main(){HostAlias value=1;}"
			uri = open_doc("catalog-consumer", source)
			params = {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 17}}

			def hover(name, expected):
				value = ask(name, "textDocument/hover", params)
				text = value["contents"]["value"]
				assert expected in text, (name, text)
				return text

			hover("catalog-initial", "int")
			assert not any(m.get("method") == "window/logMessage" and m["params"]["type"] == 2
						   for m in client.messages), client.messages
			registrations = [m for m in client.messages if m.get("method") == "client/registerCapability"]
			patterns = registrations[-1]["params"]["registrations"][0]["registerOptions"]["watchers"]
			catalog_patterns = [w["globPattern"] for w in patterns if isinstance(w["globPattern"], dict)]
			assert len(catalog_patterns) == 1 and catalog_patterns[0]["baseUri"] == root.as_uri(), patterns
			assert "[[]" in catalog_patterns[0]["pattern"] and "host" in catalog_patterns[0]["pattern"], patterns
			stamp = catalog.stat()
			catalog.write_text(catalog_text("any"), encoding="utf-8")
			os.utime(catalog, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
			assert catalog.stat().st_size == stamp.st_size
			watched()
			hover("catalog-same-stamp-notified", "any")
			catalog.write_text(catalog_text("bool"), encoding="utf-8")
			os.utime(catalog, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
			hover("catalog-size-change-no-event", "bool")
			catalog.write_text("{invalid catalog}", encoding="utf-8")
			watched()
			hover("catalog-invalid-retains-last", "bool")
			assert any(m.get("method") == "window/logMessage" and m["params"]["type"] == 2
					   for m in client.messages)
			catalog.write_text(catalog_text("int"), encoding="utf-8")
			watched()
			hover("catalog-recovered", "int")
			catalog.unlink()
			watched()
			hover("catalog-deleted-retains-last", "int")
			catalog.write_text(catalog_text("any"), encoding="utf-8")
			watched()
			hover("catalog-recreated", "any")
			replacement = root / "replacement.json"
			replacement.write_text(catalog_text("bool"), encoding="utf-8")
			client.send("workspace/didChangeConfiguration",
				{"settings": {"veroscript": {"bindingCatalogPaths": [str(replacement)]}}})
			hover("catalog-reconfigured", "bool")
			assert any(m.get("method") == "client/unregisterCapability" for m in client.messages)
			registrations = [m for m in client.messages if m.get("method") == "client/registerCapability"]
			patterns = registrations[-1]["params"]["registrations"][0]["registerOptions"]["watchers"]
			catalog_patterns = [w["globPattern"] for w in patterns if isinstance(w["globPattern"], dict)]
			assert len(catalog_patterns) == 1 and catalog_patterns[0]["pattern"] == "replacement.json", patterns
			ids = [m["id"] for m in client.messages if m.get("method", "").startswith("client/")]
			assert len(ids) == len(set(ids)), ids
		finally:
			client.close()
			save()

		# Older clients use string glob patterns, with the same event semantics.
		documents = {}
		client = Wire(lsp, root)
		sessions.append(client)
		try:
			ask("legacy-watch-initialize", "initialize", {"rootUri": root.as_uri(), "locale": "en-US",
				"capabilities": {"workspace": {"didChangeWatchedFiles": {"dynamicRegistration": True}}}})
			client.send("workspace/didChangeConfiguration",
				{"settings": {"veroscript": {"bindingCatalogPaths": [str(catalog)]}}})
			client.send("initialized")
			uri = open_doc("legacy-catalog-consumer", source)
			params = {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 17}}
			hover("legacy-watch-catalog", "any")
			registration = next(m for m in client.messages if m.get("method") == "client/registerCapability")
			patterns = registration["params"]["registrations"][0]["registerOptions"]["watchers"]
			assert all(isinstance(w["globPattern"], str) for w in patterns), patterns
			assert any("[[]" in w["globPattern"] and "host" in w["globPattern"] for w in patterns), patterns
			stamp = catalog.stat()
			catalog.write_text(catalog_text("int"), encoding="utf-8")
			os.utime(catalog, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
			watched()
			hover("legacy-watch-catalog-refreshed", "int")
		finally:
			client.close()
			save()

		# DAP uses the same path identities as LSP, and a rejected launch must
		# retain the registry referenced by the already compiled program.
		catalog.write_text(catalog_text("int"), encoding="utf-8")
		catalog_paths = [catalog.name, str(catalog), str(root) + "/./" + catalog.name]
		if sys.platform == "win32":
			catalog_paths.append(str(catalog).upper())
		source = 'void main(){HostAlias value=42;print(value);}'
		documents = {(root / "dap-alias.vs").as_uri(): source}
		for command in ("launch", "attach"):
			debugger = Wire(dap, root, dap=True)
			sessions.append(debugger)
			try:
				ask("dap-alias-initialize-" + command, "initialize", {}, debugger)
				ask("dap-alias-" + command, command, {"source": source, "fileLabel": "dap-alias.vs",
					"bindingCatalogPaths": catalog_paths}, debugger)
				ask("dap-alias-configured-" + command, "configurationDone", {}, debugger)
				debugger.event("terminated")
				assert any(m.get("event") == "output" and m["body"].get("output") == "42\n"
					for m in debugger.messages), debugger.messages
				assert any(m.get("event") == "exited" and m["body"]["exitCode"] == 0
					for m in debugger.messages), debugger.messages
			finally:
				debugger.close()
				save()

		other_catalog = root / "dap-other.json"
		other_catalog.write_text(catalog_text("float").replace("HostAlias", "HostOther"), encoding="utf-8")
		source = 'void main(){HostAlias value=42;HostOther other=0.5;print(value);print(other);}'
		documents = {(root / "dap-distinct.vs").as_uri(): source}
		debugger = Wire(dap, root, dap=True)
		sessions.append(debugger)
		try:
			ask("dap-distinct-initialize", "initialize", {}, debugger)
			ask("dap-distinct-launch", "launch", {"source": source, "fileLabel": "dap-distinct.vs",
				"bindingCatalogPaths": [str(catalog), str(other_catalog)]}, debugger)
			ask("dap-distinct-configured", "configurationDone", {}, debugger)
			debugger.event("terminated")
			outputs = [m["body"].get("output") for m in debugger.messages if m.get("event") == "output"]
			assert "42\n" in outputs and "0.5\n" in outputs, outputs
		finally:
			debugger.close()
			save()

		functions = root / "dap-functions.json"
		function_catalog = {"version": 1, "functions": [
			{"name": "hostProbe", "returnType": "int", "paramTypes": []}], "types": [],
			"typeAliases": [{"name": "HostAlias", "typeParams": [], "targetTypeName": "int"}]}
		functions.write_text(json.dumps(function_catalog), encoding="utf-8")
		partial = root / "dap-partial.json"
		partial.write_text(json.dumps({**function_catalog, "functions": [
			{"name": "otherProbe", "returnType": "int", "paramTypes": []}]}), encoding="utf-8")
		failures = {
			"partial-catalog": {"bindingCatalogPaths": [str(partial), str(root / "missing.json")]},
			"distinct-duplicate": {"bindingCatalogPaths": [str(functions), str(catalog)]},
			"environment": {"bindingCatalogPaths": [str(partial)], "env": {"bad=name": "value"}},
			"directory": {"bindingCatalogPaths": [str(partial)], "cwd": str(root / "missing-directory")}}
		source = 'void main(){print("before");print(hostProbe());print("after");}'
		documents = {(root / "dap-retained.vs").as_uri(): source}
		for name, arguments in failures.items():
			debugger = Wire(dap, root, dap=True)
			sessions.append(debugger)
			try:
				ask("dap-retained-initialize-" + name, "initialize", {"locale": "en-US"}, debugger)
				ask("dap-retained-launch-" + name, "launch", {"source": source,
					"fileLabel": "dap-retained.vs", "bindingCatalogPaths": [str(functions)]}, debugger)
				ask("dap-rejected-launch-" + name, "launch", {"source": "void main(){}", **arguments},
					debugger, False)
				threads = ask("dap-retained-threads-" + name, "threads", {}, debugger)
				assert [t["id"] for t in threads["threads"]] == [1], threads
				ask("dap-retained-configured-" + name, "configurationDone", {}, debugger)
				debugger.event("terminated")
				outputs = [m["body"] for m in debugger.messages if m.get("event") == "output"]
				assert any(o.get("category") == "stderr" and "not linked for execution" in o["output"]
					for o in outputs), outputs
				assert any(o.get("output") == "before\n" for o in outputs), outputs
				assert not any(o.get("output") == "after\n" for o in outputs), outputs
				assert any(m.get("event") == "exited" and m["body"]["exitCode"] == 1
					for m in debugger.messages), debugger.messages
			finally:
				debugger.close()
				save()

		source = 'int inner(){\nint value=1;\nreturn value;\n}\nint outer(){return inner();}\nint main(){print("中文😀");return outer();}\n'
		program = root / "stack.vs"
		program.write_text(source, encoding="utf-8")
		documents = {program.as_uri(): source}
		debugger = Wire(dap, root, dap=True)
		sessions.append(debugger)
		try:
			ask("dap-initialize", "initialize", {"adapterID": "veroscript"}, debugger)
			ask("dap-launch", "launch", {"program": str(program)}, debugger)
			ask("dap-breakpoints", "setBreakpoints", {"source": {"path": str(program)},
				"breakpoints": [{"line": 2}]}, debugger)
			ask("dap-configured", "configurationDone", {}, debugger)
			debugger.event("stopped")
			# Columns must describe the loaded version even after a disk edit.
			program.write_text(source.replace("中文😀", "changed"), encoding="utf-8")
			full = ask("dap-stack-full", "stackTrace", {"threadId": 1}, debugger)
			assert full["totalFrames"] == 3 and len(full["stackFrames"]) == 3, full
			main = next(f for f in full["stackFrames"] if f["name"] == "main")
			prefix = source.splitlines()[5].split("outer")[0]
			assert main["column"] == len(prefix.encode("utf-16-le")) // 2 + 1, main
			for start, levels in ((0, 1), (1, 1), (1, 0), (2, 9), (3, 1), (2147483647, 2147483647),
					(0, 4294967295), (4294967295, 4294967295)):
				page = ask(f"dap-stack-page-{start}-{levels}", "stackTrace",
					{"threadId": 1, "startFrame": start, "levels": levels}, debugger)
				expected = full["stackFrames"][start:] if levels == 0 else full["stackFrames"][start:start + levels]
				assert page["stackFrames"] == expected and page["totalFrames"] == 3, page
			for key in ("startFrame", "levels"):
				ask("dap-negative-" + key, "stackTrace", {"threadId": 1, key: -1}, debugger, False)
				for index, value in enumerate(("1", True, 0.5, 1e100, 4294967296)):
					ask(f"dap-malformed-{key}-{index}", "stackTrace",
						{"threadId": 1, key: value}, debugger, False)
			for command in ("stackTrace", "continue", "next", "stepIn", "stepOut", "pause"):
				ask("dap-wrong-thread-" + command, command, {"threadId": 42}, debugger, False)
			for index, value in enumerate(("1", True, 1.5, 1e100)):
				ask(f"dap-malformed-thread-{index}", "continue", {"threadId": value}, debugger, False)
			current = ask("dap-stack-after-rejections", "stackTrace", {"threadId": 1}, debugger)
			assert current == full, current
			scopes = ask("dap-scopes-after-rejections", "scopes",
				{"frameId": full["stackFrames"][0]["id"]}, debugger)
			assert scopes["scopes"], scopes
		finally:
			debugger.close()
			save()
		imported = root / "unicode-library.vs"
		imported_source = 'int inner(){\r\nint value=1;\r\nreturn value;\r\n}\r\nexport int outer(){print("中文😀");return inner();}\r\n'
		imported.write_text(imported_source, encoding="utf-8")
		source = 'import "unicode-library.vs";\r\nint main(){return outer();}\r\n'
		program.write_text(source, encoding="utf-8")
		documents = {program.as_uri(): source, imported.as_uri(): imported_source}
		debugger = Wire(dap, root, dap=True)
		sessions.append(debugger)
		try:
			ask("dap-zero-initialize", "initialize",
				{"adapterID": "veroscript", "linesStartAt1": False, "columnsStartAt1": False}, debugger)
			ask("dap-zero-launch", "launch", {"program": str(program)}, debugger)
			ask("dap-zero-breakpoints", "setBreakpoints", {"source": {"path": str(imported)},
				"breakpoints": [{"line": 1}]}, debugger)
			ask("dap-zero-configured", "configurationDone", {}, debugger)
			debugger.event("stopped")
			imported.write_text(imported_source.replace("中文😀", "changed"), encoding="utf-8")
			frames = ask("dap-zero-imported-columns", "stackTrace", {"threadId": 1}, debugger)["stackFrames"]
			outer = next(f for f in frames if f["name"] == "outer")
			prefix = imported_source.splitlines()[4].split("inner")[0]
			assert outer["column"] == len(prefix.encode("utf-16-le")) // 2 and outer["line"] == 4, outer
		finally:
			debugger.close()
			save()
		source = '\ufeffint main(){print("中文😀");return inner();}\nint inner(){\nint value=1;\nreturn value;\n}\n'
		program.write_text(source, encoding="utf-8")
		documents = {program.as_uri(): source}
		debugger = Wire(dap, root, dap=True)
		sessions.append(debugger)
		try:
			ask("dap-bom-initialize", "initialize", {"adapterID": "veroscript"}, debugger)
			ask("dap-bom-launch", "launch", {"program": str(program)}, debugger)
			ask("dap-bom-breakpoints", "setBreakpoints", {"source": {"path": str(program)},
				"breakpoints": [{"line": 3}]}, debugger)
			ask("dap-bom-configured", "configurationDone", {}, debugger)
			debugger.event("stopped")
			frames = ask("dap-bom-columns", "stackTrace", {"threadId": 1}, debugger)["stackFrames"]
			main = next(f for f in frames if f["name"] == "main")
			prefix = source.splitlines()[0].lstrip('\ufeff').split("inner")[0]
			assert main["column"] == len(prefix.encode("utf-16-le")) // 2 + 1, main
		finally:
			debugger.close()
			save()
	print(f"Protocol review: {len(records)} cases passed.")


if __name__ == "__main__":
	run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve())
