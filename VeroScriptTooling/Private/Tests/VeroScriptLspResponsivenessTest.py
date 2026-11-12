# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise actual LSP stdio scheduling, caching, cancellation and invalidation."""
from __future__ import annotations

import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


class Client:
	def __init__(self, executable: str | list[str], root: Path, response_timeout: float = 15):
		self.process = subprocess.Popen(
			[executable] if isinstance(executable, str) else executable, cwd=root, stdin=subprocess.PIPE,
			stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
		self.messages: queue.Queue = queue.Queue()
		self.pending: dict = {}
		self.logs: list[str] = []
		self.diagnostics: list = []
		self.order: list[int] = []
		self.next_id = 1
		self.response_timeout = response_timeout
		threading.Thread(target=self.read, daemon=True).start()

	def read(self):
		try:
			while True:
				headers = {}
				while True:
					line = self.process.stdout.readline()
					if not line:
						self.messages.put(RuntimeError("LSP closed stdout unexpectedly"))
						return
					if line == b"\r\n":
						break
					key, value = line.decode("ascii").split(":", 1)
					headers[key.lower()] = value.strip()
				length = int(headers["content-length"])
				body = bytearray()
				while len(body) < length:
					chunk = self.process.stdout.read(length - len(body))
					if not chunk:
						raise RuntimeError("Incomplete LSP body")
					body.extend(chunk)
				self.messages.put((time.monotonic(), json.loads(body)))
		except Exception as error:
			self.messages.put(error)

	def send(self, method: str, params=None, identifier=None):
		message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
		if identifier is not None:
			message["id"] = identifier
		body = json.dumps(message).encode("utf-8")
		self.process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
		self.process.stdin.flush()

	def request(self, method: str, params=None):
		identifier = self.next_id
		self.next_id += 1
		self.send(method, params, identifier)
		return identifier, time.monotonic()

	def wait(self, request, timeout=None):
		identifier, started = request
		if timeout is None:
			timeout = self.response_timeout
		limit = time.monotonic() + timeout
		while identifier not in self.pending:
			item = self.messages.get(timeout=max(0.001, limit - time.monotonic()))
			if isinstance(item, Exception):
				raise item
			received, message = item
			if "id" in message:
				assert message["id"] not in self.pending, "Duplicate response ID"
				self.order.append(message["id"])
				self.pending[message["id"]] = (received, message)
			elif message.get("method") == "textDocument/publishDiagnostics":
				self.diagnostics.append(message["params"])
			elif message.get("method") == "window/logMessage":
				self.logs.append(message["params"]["message"])
		received, message = self.pending.pop(identifier)
		return message, received - started

	def result(self, request):
		message, elapsed = self.wait(request)
		assert "error" not in message, message
		return message["result"], elapsed

	def close(self):
		if self.process.poll() is None:
			self.result(self.request("shutdown"))
			self.send("exit")
			self.process.wait(timeout=10)
		assert self.process.returncode == 0


def source_for(count: int):
	return "".join(
		f"int Compute{i}(int value) {{ int local = value + {i}; return local; }}\n"
		for i in range(count)) + "int main() { return Compute0(1); }\n"


def run_exit(executable: str):
	with tempfile.TemporaryDirectory(prefix="vero lsp exit ") as directory:
		root = Path(directory)
		client = Client(executable, root, 40)
		document = {"uri": (root / "large.vs").as_uri()}
		try:
			client.result(client.request("initialize", {
				"rootUri": root.as_uri(), "locale": "en-US", "capabilities": {}}))
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"lspRequestTimeoutMs": 30000, "lspAnalysisTimeoutMs": 30000}}})
			client.send("textDocument/didOpen", {"textDocument": {
				**document, "languageId": "veroscript", "version": 1, "text": source_for(2500)}})
			client.result(client.request("textDocument/hover", {
				"textDocument": document, "position": {"line": 0, "character": 6}}))
			# Leave cold analysis running and queued when the ordered shutdown
			# reaches exit; neither job should consume its remaining budget.
			running = client.request("textDocument/semanticTokens/full", {"textDocument": document})
			time.sleep(0.08)
			queued = client.request("textDocument/semanticTokens/full", {"textDocument": document})
			client.result(client.request("shutdown"))
			started = time.monotonic()
			client.send("exit")
			assert client.process.wait(timeout=1.5) == 0
			for request in (running, queued):
				response, _ = client.wait(request)
				# A faster analyzer may finish before exit reaches the server.
				if "error" in response:
					assert response["error"]["code"] == -32800, response
				else:
					assert "data" in response["result"], response
			print(f"LSP exits during pending analysis: {time.monotonic() - started:.3f}s")
		finally:
			if client.process.poll() is None:
				client.process.kill()
				client.process.wait(timeout=5)


def run(executable: str):
	with tempfile.TemporaryDirectory(prefix="vero lsp response ") as directory:
		root = Path(directory)
		# The unoptimized Debug analyzer takes substantially longer on this
		# fixture; keep the release responsiveness budget unchanged.
		is_debug = Path(executable).parent.name.lower() == "debug"
		analysis_budget = 30000 if is_debug else 5000
		client = Client(executable, root, 40 if is_debug else 15)
		path = root / "main.vs"
		document = {"uri": path.as_uri()}
		version = 1
		source = source_for(400)

		def settings(analysis=None):
			if analysis is None:
				analysis = analysis_budget
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"lspRequestTimeoutMs": 2000, "lspAnalysisTimeoutMs": analysis,
				"lspTraceAnalysis": True}}})

		def change(text):
			nonlocal version
			version += 1
			client.send("textDocument/didChange", {
				"textDocument": {**document, "version": version},
				"contentChanges": [{"text": text}]})

		def full():
			return client.request("textDocument/semanticTokens/full", {"textDocument": document})

		def hover():
			return client.request("textDocument/hover", {
				"textDocument": document, "position": {"line": 0, "character": 6}})

		try:
			client.result(client.request("initialize", {
				"rootUri": root.as_uri(), "locale": "en-US", "capabilities": {}}))
			settings()
			client.send("textDocument/didOpen", {"textDocument": {
				**document, "languageId": "veroscript", "version": version, "text": source}})
			client.result(hover())  # Barrier: document diagnostics have completed.
			cold = full()
			interactive = hover()
			completion = client.request("textDocument/completion", {
				"textDocument": document, "position": {"line": 400, "character": 23}})
			hover_result, hover_time = client.result(interactive)
			completion_result, _ = client.result(completion)
			baseline, cold_time = client.result(cold)
			assert hover_result and completion_result["items"], (hover_result, completion_result)
			assert client.order.index(interactive[0]) < client.order.index(cold[0]), client.order
			assert client.order.index(completion[0]) < client.order.index(cold[0]), client.order
			assert hover_time < 2.0, hover_time
			assert baseline["data"] and len(baseline["data"]) % 5 == 0
			repeated, repeat_time = client.result(full())
			assert repeated["data"] == baseline["data"]
			assert any("cache=hit" in message for message in client.logs)
			delta, _ = client.result(client.request("textDocument/semanticTokens/full/delta", {
				"textDocument": document, "previousResultId": repeated["resultId"]}))
			assert delta["edits"] == []
			# Empty/unrelated settings and protocol budgets do not change the
			# analyzed program. They must retain completed semantic snapshots.
			for configuration in ({"veroscript": {}}, {"otherLanguage": {}},
								  {"veroscript": {"lspTraceAnalysis": True}},
								  {"veroscript": {"lspRequestTimeoutMs": 2000,
												  "lspAnalysisTimeoutMs": analysis_budget}}):
				hits = sum("cache=hit" in message for message in client.logs)
				client.send("workspace/didChangeConfiguration", {"settings": configuration})
				unchanged, _ = client.result(full())
				assert unchanged["data"] == baseline["data"]
				assert sum("cache=hit" in message for message in client.logs) > hits, client.logs[-2:]
			delta = unchanged  # The next delta must refer to the latest published result ID.

			# A cold rename must fit the normal interactive budget, including
			# exact definition checks for every occurrence and a same-name local.
			rename_document = {"uri": (root / "rename.vs").as_uri()}
			# Debug exercises the same checks with a smaller analyzer fixture;
			# the release performance gate remains 300 occurrences in 2 seconds.
			rename_occurrences = 30 if is_debug else 300
			rename_source = ("int RenameProbe(){int value=0;\n" + "value += 1;\n" * rename_occurrences
							 + "return value;}\nint ShadowProbe(){int value=9;return value;}\n")
			client.send("textDocument/didOpen", {"textDocument": {
				**rename_document, "languageId": "veroscript", "version": 1, "text": rename_source}})
			client.result(client.request("textDocument/documentSymbol", {"textDocument": rename_document}))
			renamed, rename_time = client.result(client.request("textDocument/rename", {
				"textDocument": rename_document, "position": {"line": 0, "character": 23}, "newName": "renamed"}))
			assert set(renamed["changes"]) == {rename_document["uri"]}, renamed
			edits = renamed["changes"][rename_document["uri"]]
			assert len(edits) == rename_occurrences + 2 and rename_time < 2.0, (len(edits), rename_time)
			assert len({(edit["range"]["start"]["line"], edit["range"]["start"]["character"])
						for edit in edits}) == rename_occurrences + 2, edits
			lines = rename_source.splitlines()
			for edit in edits:
				start, end = edit["range"]["start"], edit["range"]["end"]
				assert start["line"] == end["line"] <= rename_occurrences + 1, edit
				assert lines[start["line"]][start["character"]:end["character"]] == "value", edit
				assert edit["newText"] == "renamed", edit
			client.send("textDocument/didClose", {"textDocument": rename_document})
			# Completed AST snapshots must preserve foreground query results.
			cached_hover, _ = client.result(hover())
			cached_completion, _ = client.result(client.request("textDocument/completion", {
				"textDocument": document, "position": {"line": 400, "character": 23}}))
			assert cached_hover == hover_result
			assert cached_completion == completion_result
			change(source + "int Added() { return 9; }\n")
			edited_delta, _ = client.result(client.request("textDocument/semanticTokens/full/delta", {
				"textDocument": document, "previousResultId": delta["resultId"]}))
			reconstructed = list(baseline["data"])
			for edit in reversed(edited_delta["edits"]):
				start = edit["start"]
				reconstructed[start:start + edit["deleteCount"]] = edit.get("data", [])
			edited, _ = client.result(full())
			assert reconstructed == edited["data"] != baseline["data"]

			# A stale/duplicate version must not invalidate a good cache.
			client.send("textDocument/didChange", {
				"textDocument": {**document, "version": version},
				"contentChanges": [{"text": "void Wrong() {}"}]})
			same, _ = client.result(full())
			assert same["data"] == edited["data"]

			huge = source_for(1800)
			change(huge)
			client.result(hover())
			running = full()
			time.sleep(0.08)
			queued = full()
			client.result(hover())  # Barrier: the second analysis is already queued.
			time.sleep(0.02)
			client.send("$/cancelRequest", {"id": queued[0]})
			queued_result, queued_time = client.wait(queued)
			assert queued_result["error"]["code"] == -32800, queued_result
			assert queued_time < 1.5 and running[0] not in client.order
			cancelled_at = time.monotonic()
			client.send("$/cancelRequest", {"id": running[0]})
			cancelled, _ = client.wait(running)
			assert cancelled["error"]["code"] == -32800, cancelled
			cancel_time = time.monotonic() - cancelled_at
			assert cancel_time < 1.5, cancel_time
			client.result(hover())

			settings(50)
			timed_out, timeout_time = client.wait(full())
			assert timed_out["error"]["code"] == -32001, timed_out
			assert timeout_time < 1.5, timeout_time
			settings(analysis_budget)
			stale = full()
			change(source)
			stale_result, _ = client.wait(stale)
			assert stale_result["error"]["code"] == -32801, stale_result
			# Opening a document that is not imported cannot discard this job.
			independent = full()
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": (root / "unrelated.vs").as_uri(), "languageId": "veroscript",
				"version": 1, "text": "void Unrelated() {}\n"}})
			independent_result, _ = client.result(independent)
			assert independent_result["data"] == baseline["data"]
			cache_hits_before = sum("cache=hit" in message for message in client.logs)
			client.send("textDocument/didChange", {
				"textDocument": {"uri": (root / "unrelated.vs").as_uri(), "version": 2},
				"contentChanges": [{"text": "void UnrelatedChanged() {}\n"}]})
			still_cached, _ = client.result(full())
			assert still_cached["data"] == baseline["data"]
			assert sum("cache=hit" in message for message in client.logs) > cache_hits_before

			# Imported overlays invalidate cached classification and foreground ASTs.
			dependency = root / "dep.vs"
			dependency.write_text("export struct Thing { int member; }\n", encoding="utf-8")
			imported = 'import "dep.vs";\nThing Echo(Thing value) { return value; }\n'
			change(imported)
			imported_result, _ = client.result(full())
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": dependency.as_uri(), "languageId": "veroscript", "version": 1,
				"text": "export enum Thing { First, Second }\n"}})
			overlay_result, _ = client.result(full())
			assert overlay_result["data"] != imported_result["data"], (imported_result, overlay_result, client.logs[-3:], client.diagnostics[-3:])
			change(imported + source)
			dependent_job = full()
			client.send("textDocument/didChange", {
				"textDocument": {"uri": dependency.as_uri(), "version": 2},
				"contentChanges": [{"text": "export struct Thing { int replacement; }\n"}]})
			changed_dependency, _ = client.wait(dependent_job)
			assert changed_dependency["error"]["code"] == -32801, changed_dependency
			change(imported)
			client.send("textDocument/didClose", {"textDocument": {"uri": dependency.as_uri()}})
			disk_result, _ = client.result(full())
			assert disk_result["data"] == imported_result["data"]
			# Also detect disk dependency edits without a watched-files notification.
			dependency.write_text("export enum Thing { DiskFirst }\n", encoding="utf-8")
			changed_disk, _ = client.result(full())
			assert changed_disk["data"] != disk_result["data"]
			disk_hover, _ = client.result(client.request("textDocument/hover", {
				"textDocument": document, "position": {"line": 1, "character": 2}}))
			assert "enum Thing" in disk_hover["contents"]["value"], disk_hover

			# Snapshot reuse preserves macro coordinates, locals and member lookup.
			member_line = "int Read(int seed) { Value value(); value.amount = seed; return value.amount; }"
			member_source = ("#define BASE 2\n"
							 "struct Value { int amount; }\n" + member_line + "\n")
			change(member_source)
			member_position = {"line": 2, "character": member_line.rindex("amount") + 2}
			member_params = {"textDocument": document, "position": member_position}
			before = [client.result(client.request(method, member_params))[0] for method in (
				"textDocument/hover", "textDocument/definition", "textDocument/completion")]
			assert before[0] and before[1] and before[2]["items"], before
			client.result(full())
			after = [client.result(client.request(method, member_params))[0] for method in (
				"textDocument/hover", "textDocument/definition", "textDocument/completion")]
			assert before == after
			change(member_source.replace("return value.amount;", "return value.amount"))
			recovered, _ = client.result(full())
			cache_hits_before = sum("cache=hit" in message for message in client.logs)
			recovered_repeat, _ = client.result(full())
			assert recovered_repeat["data"] == recovered["data"]
			assert sum("cache=hit" in message for message in client.logs) > cache_hits_before

			# A configuration revision rejects analysis already using old options.
			change(huge)
			client.result(hover())
			old_configuration = full()
			client.send("workspace/didChangeConfiguration", {"settings": {
				"veroscript": {"defines": ["CONFIGURATION_CHANGED"]}}})
			invalidated, _ = client.wait(old_configuration)
			assert invalidated["error"]["code"] == -32801, invalidated

			# Closing/reopening a URI starts a new lifetime even when its LSP
			# version restarts at 1. Old jobs and delta IDs cannot cross it.
			previous_id = recovered_repeat["resultId"]
			old_lifetime = full()
			client.send("textDocument/didClose", {"textDocument": document})
			reopened_source = source_for(1)
			version = 1
			client.send("textDocument/didOpen", {"textDocument": {
				**document, "languageId": "veroscript", "version": version,
				"text": reopened_source}})
			reopened_delta = client.request("textDocument/semanticTokens/full/delta", {
				"textDocument": document, "previousResultId": previous_id})
			obsolete, _ = client.wait(old_lifetime)
			assert obsolete["error"]["code"] == -32801, obsolete
			reopened, _ = client.result(reopened_delta)
			reopened_full, _ = client.result(full())
			assert "data" in reopened and "edits" not in reopened, reopened
			assert reopened["data"] == reopened_full["data"] != baseline["data"]
			reopened_hover, _ = client.result(hover())
			assert reopened_hover

			# Core depth/chain guards must also protect the real LSP process.
			# A rejected document cannot leave loader errors or stale semantic
			# snapshots behind when the editor fixes it on the same connection.
			change("void main(){int x=" + "(" * 200 + "1" + ")" * 200 + ";}")
			accepted, _ = client.result(full())
			assert accepted and len(accepted["data"]) % 5 == 0, accepted
			assert any(diagnostic["uri"] == document["uri"]
					   and diagnostic.get("version") == version
					   and not diagnostic["diagnostics"] for diagnostic in client.diagnostics), client.diagnostics[-3:]
			excessive_sources = (
				("void main(){int x=0;" + "x=" * 1024 + "1;}",
				 "expression chain is too long"),
				("void main(){int x=" + "true?1:" * 1024 + "0;}",
				 "expression chain is too long"),
				("namespace n{" * 256 + "}" * 256, "namespace nested too deeply"),
				("using A=array<A>;void main(){A value=" + "{" * 256
				 + "}" * 256 + ";}", "initializer nested too deeply"),
				("void main(){int x=" + "(" * 512 + "1" + ")" * 512 + ";}",
				 "expression nested too deeply"),
			)
			for excessive, expected_diagnostic in excessive_sources:
				change(excessive)
				rejected, _ = client.result(full())
				# Failed AST analysis may still return conservative lexical
				# tokens. Both that fallback and a null response are valid.
				if rejected is not None:
					assert len(rejected["data"]) % 5 == 0, rejected
				assert any(diagnostic["uri"] == document["uri"]
						   and diagnostic.get("version") == version
						   and any(item.get("severity") == 1
								   and item["message"] == expected_diagnostic
								   for item in diagnostic["diagnostics"])
						   for diagnostic in client.diagnostics), client.diagnostics[-3:]
				change(reopened_source)
				restored, _ = client.result(full())
				assert restored["data"] == reopened_full["data"]
				restored_hover, _ = client.result(hover())
				assert restored_hover == reopened_hover, restored_hover
				assert any(diagnostic["uri"] == document["uri"]
						   and diagnostic.get("version") == version
						   and not diagnostic["diagnostics"]
						   for diagnostic in client.diagnostics), client.diagnostics[-3:]

			# Recovery must bound generated callable types as well as generic
			# bracket depth. Keep the statement unfinished to exercise the AST
			# recovery path rather than the compiler's invalid-base rejection.
			for argument in ("fn()->array<T>", "fn()->T"):
				growing = (f"class Growing<T> : Growing<{argument}> {{ public: int value; }}; "
						   "void main(){Growing<int> receiver; receiver.")
				change(growing)
				members, member_time = client.result(client.request("textDocument/completion", {
					"textDocument": document, "position": {"line": 0, "character": len(growing)}}))
				assert members["items"] == [] and member_time < 2.0, (members, member_time)
				change(reopened_source)
				assert client.result(full())[0]["data"] == reopened_full["data"]
				assert client.result(hover())[0] == reopened_hover

			# Query a closed file with more matches than the result cap. Verify
			# that top-k sorting and shared source snapshots retain stable order
			# and UTF-16 ranges, including non-BMP text before each declaration.
			prefix = "/*\U0001f600*/ int "
			closed = root / "rank.vs"
			closed.write_text("".join(prefix + f"Rank{i:05}(int x) {{ return x; }}\n"
									  for i in range(2105)), encoding="utf-8")
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"lspRequestTimeoutMs": 30000}}})
			# This section verifies ordering/ranges under the explicit 30s
			# server budget. The transport must wait long enough to receive
			# that result (or its timeout); latency gates above remain unchanged.
			client.response_timeout = max(client.response_timeout, 35)
			ranked, _ = client.result(client.request("workspace/symbol", {"query": "Rank"}))
			assert [item["name"] for item in ranked] == [f"Rank{i:05}" for i in range(2000)]
			character = len(prefix.encode("utf-16-le")) // 2
			for index, item in enumerate(ranked):
				assert item["location"]["uri"] == closed.as_uri(), item
				assert item["location"]["range"] == {
					"start": {"line": index, "character": character},
					"end": {"line": index, "character": character + 9}}, item
			repeated_ranked, _ = client.result(client.request("workspace/symbol", {"query": "Rank"}))
			assert repeated_ranked == ranked

			# Public paths use UTF-8 even on Windows. Cover both navigation of
			# soft std namespaces and workspace discovery below a Unicode path.
			unicode_root = root / "\u6a21\u5757-\U0001f600"
			standard_module = unicode_root / "stdlib" / "algorithm.vs"
			standard_module.parent.mkdir(parents=True)
			standard_module.write_text("// standard namespace module\n", encoding="utf-8")
			closed_unicode = unicode_root / "closed.vs"
			closed_unicode.write_text("void UnicodeClosed() {}\n", encoding="utf-8")
			namespace_source = "void main(){std::algorithm::sort();}\n"
			namespace_document = {"uri": (root / "namespace.vs").as_uri()}
			client.send("textDocument/didOpen", {"textDocument": {
				**namespace_document, "languageId": "veroscript", "version": 1,
				"text": namespace_source}})
			for import_root in (unicode_root, standard_module.parent):
				client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
					"lspRequestTimeoutMs": 30000, "importPaths": [str(import_root)]}}})
				definitions, _ = client.result(client.request("textDocument/definition", {
					"textDocument": namespace_document, "position": {
						"line": 0, "character": namespace_source.index("algorithm") + 2}}))
				assert len(definitions) == 1 and definitions[0]["uri"] == standard_module.as_uri(), definitions
			unicode_symbols, _ = client.result(client.request("workspace/symbol", {"query": "UnicodeClosed"}))
			assert len(unicode_symbols) == 1 and unicode_symbols[0]["location"]["uri"] == closed_unicode.as_uri(), unicode_symbols

			# Foreground requests share the same cooperative cancellation scope
			# as background analysis; a timed-out parse leaves the server usable.
			foreground = {"uri": (root / "foreground.vs").as_uri()}
			client.send("textDocument/didOpen", {"textDocument": {
				**foreground, "languageId": "veroscript", "version": 1,
				"text": source_for(6000)}})
			client.result(hover())
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"lspRequestTimeoutMs": 50}}})
			client.response_timeout = 40 if is_debug else 15
			client.result(hover())
			foreground_error, foreground_time = client.wait(client.request(
				"textDocument/documentSymbol", {"textDocument": foreground}))
			assert foreground_error["error"]["code"] == -32001, foreground_error
			assert foreground_time < 1.5, foreground_time
			assert client.result(hover())[0]

			# A cancelled diagnostic analysis must release its deadline scope
			# and allow later versions of the same document to publish and
			# clear diagnostics normally.
			catalog = root / "timeout-bindings.json"
			catalog_source = json.dumps({"version": 1, "functions": [], "types": []})
			catalog.write_text(catalog_source, encoding="utf-8")
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"bindingCatalogPaths": [str(catalog)]}}})
			client.result(hover())
			# The next didChange also triggers automatic catalog refresh. Its
			# whole-workspace diagnostics exceed the 50ms analysis budget, but
			# the edit must still be applied before handling the next request.
			catalog.write_text(catalog_source + " ", encoding="utf-8")
			client.send("textDocument/didChange", {
				"textDocument": {**foreground, "version": 2},
				"contentChanges": [{"text": "int main() { return UnknownAfterTimeout(); }\n"}]})
			symbols, _ = client.result(client.request(
				"textDocument/documentSymbol", {"textDocument": foreground}))
			assert [symbol["name"] for symbol in symbols] == ["main"], symbols
			# A 50ms diagnostic job may itself time out on a loaded host. Prove
			# recovery under the normal analysis budget, independently of the
			# forced timeout. Keep the 50ms foreground request budget in force.
			client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"lspAnalysisTimeoutMs": analysis_budget}}})
			for next_version, text, has_error in (
				(3, "int main() { return UnknownAfterTimeout(); }\n", True),
				(4, "int main() { return 7; }\n", False),
			):
				client.send("textDocument/didChange", {
					"textDocument": {**foreground, "version": next_version},
					"contentChanges": [{"text": text}]})
				symbols, _ = client.result(client.request(
					"textDocument/documentSymbol", {"textDocument": foreground}))
				assert [symbol["name"] for symbol in symbols] == ["main"], symbols
				matching = [diagnostic for diagnostic in client.diagnostics
							if diagnostic["uri"] == foreground["uri"]
							and diagnostic.get("version") == next_version]
				assert matching, client.diagnostics[-3:]
				assert any(item.get("severity") == 1
						   for item in matching[-1]["diagnostics"]) == has_error, matching[-1]

			print(f"LSP {len(source.encode())} bytes: cold={cold_time:.3f}s "
				  f"cached={repeat_time:.3f}s hover={hover_time:.3f}s "
				  f"cancel={cancel_time:.3f}s deadline={timeout_time:.3f}s rename={rename_time:.3f}s")
			for message in client.logs:
				if "cache=miss" in message:
					print(message)
					break
			client.close()
		finally:
			if client.process.poll() is None:
				client.process.kill()
				client.process.wait(timeout=5)


if __name__ == "__main__":
	run(str(Path(sys.argv[1]).resolve()))
	run_exit(str(Path(sys.argv[1]).resolve()))
