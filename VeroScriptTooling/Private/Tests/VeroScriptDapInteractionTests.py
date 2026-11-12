# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise opaque stopped-state handles through the actual DAP executable."""
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


def run(executable, evidence_path=None):
	requests, messages, events, responses = [], [], [], {}
	incoming = queue.Queue()
	library_source = "#define_export SAY(x) \\\nprint(x)\n"
	source = ('import "macro_library.vs";void main(){\n array<int> values={1,2};\n'
			  " auto bytes=new std::memory::buffer(8);\n"
			  " auto view=bytes.readonly_slice(0,4);\n"
			  " SAY(values[0]);\n SAY(values[0]);\n}\n")
	readonly_source = ("struct Item{int value=7;};\nvoid main(){\n"
					   "variant box=Item();\nint ready=0;\nprint(ready);\n}\n")
	replace_child_source = ("class Box{public:array<int> values={1};};\nvoid main(){\n"
							"Box box=new Box();\narray<int> replacement={7,8};\narray<int> reset={4,5};\nprint(0);\n}\n")
	with tempfile.TemporaryDirectory(prefix="vero-dap-handles-") as directory:
		library_path = Path(directory) / "macro_library.vs"
		library_path.write_text(library_source, encoding="utf-8")
		process = subprocess.Popen([executable], cwd=directory, stdin=subprocess.PIPE,
								   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

		def read():
			try:
				while True:
					headers = {}
					while True:
						line = process.stdout.readline()
						if not line:
							raise RuntimeError("DAP closed stdout unexpectedly")
						if line == b"\r\n":
							break
						key, value = line.decode("ascii").split(":", 1)
						headers[key.lower()] = value.strip()
					length = int(headers["content-length"])
					body = bytearray()
					while len(body) < length:
						chunk = process.stdout.read(length - len(body))
						if not chunk:
							raise RuntimeError("Incomplete DAP message")
						body.extend(chunk)
					incoming.put(json.loads(body))
			except Exception as error:
				incoming.put(error)

		threading.Thread(target=read, daemon=True).start()

		def receive(deadline):
			message = incoming.get(timeout=max(0.001, deadline - time.monotonic()))
			if isinstance(message, Exception):
				raise message
			messages.append(message)
			if message["type"] == "response":
				responses[message["request_seq"]] = message
			else:
				events.append(message)

		def request(command, arguments=None, success=True):
			identifier = len(requests) + 1
			message = {"seq": identifier, "type": "request", "command": command,
					   "arguments": arguments or {}}
			requests.append(message)
			body = json.dumps(message).encode("utf-8")
			process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
			process.stdin.flush()
			deadline = time.monotonic() + 15
			while identifier not in responses:
				receive(deadline)
			response = responses.pop(identifier)
			assert response["success"] == success, response
			return response.get("body", {})

		def event(name):
			deadline = time.monotonic() + 15
			while True:
				for index, message in enumerate(events):
					if message.get("event") == name:
						return events.pop(index)
				receive(deadline)

		def variables(reference):
			return request("variables", {"variablesReference": reference})["variables"]

		def array_reference(reference):
			return next(variable["variablesReference"] for variable in variables(reference)
						if variable["name"] == "values")

		def memory_reference(reference, name, length):
			variable = next(v for v in variables(reference) if v["name"] == name)
			info = variable["veroMemory"]
			assert info["byteLength"] == str(length), variable
			if name == "bytes":
				assert isinstance(info["objectId"], str) and int(info["objectId"]) > 0, variable
			else:
				# Views are value types and have no separate object identity.
				assert "objectId" not in info and "objectAddress" not in info, variable
			for key, value in info.items():
				assert isinstance(value, str) and int(value) >= 0, (key, variable)
			if "memoryReference" in variable:
				assert isinstance(variable["memoryReference"], str), variable
				assert int(variable["memoryReference"]) > 0, variable
			children = variables(variable["variablesReference"])
			values = {child["name"]: child["value"] for child in children}
			assert values["[byte_length]"] == info["byteLength"], variable
			if "objectId" in info:
				assert values["[object_id]"] == info["objectId"], variable
			for child in children:
				assert child["variablesReference"] == 0, child
				assert "memoryReference" not in child and "veroMemory" not in child, child
				request("setVariable", {"variablesReference": variable["variablesReference"],
										"name": child["name"], "value": "0"}, success=False)
			return variable["variablesReference"], info.get("objectId")

		try:
			request("initialize", {"adapterID": "veroscript", "locale": "en-US"})
			event("initialized")
			label = str(Path(directory) / "handles.vs")
			request("launch", {"source": source, "fileLabel": label})
			request("setBreakpoints", {"source": {"path": label}, "breakpoints": [{"line": 5}, {"line": 6}]})
			request("configurationDone")
			event("stopped")
			first_stack = request("stackTrace", {"threadId": 1})["stackFrames"][0]
			assert first_stack["line"] == 5 and Path(first_stack["source"]["path"]).resolve() == Path(label).resolve(), first_stack
			first_frame = first_stack["id"]
			first_locals = request("scopes", {"frameId": first_frame})["scopes"][0]["variablesReference"]
			first_array = array_reference(first_locals)
			assert array_reference(first_locals) == first_array
			first_memory, first_identity = memory_reference(first_locals, "bytes", 8)
			first_view, view_identity = memory_reference(first_locals, "view", 4)
			assert memory_reference(first_locals, "bytes", 8) == (first_memory, first_identity)
			watch = request("evaluate", {"expression": "bytes", "frameId": first_frame})
			watch_children = {v["name"]: v["value"] for v in variables(watch["variablesReference"])}
			assert watch_children["[byte_length]"] == "8"
			assert watch_children["[object_id]"] == first_identity
			assert "[storage_address]" not in watch_children
			request("continue", {"threadId": 1})
			event("stopped")
			second_stack = request("stackTrace", {"threadId": 1})["stackFrames"][0]
			assert second_stack["line"] == 6 and Path(second_stack["source"]["path"]).resolve() == Path(label).resolve(), second_stack
			second_frame = second_stack["id"]
			second_locals = request("scopes", {"frameId": second_frame})["scopes"][0]["variablesReference"]
			second_array = array_reference(second_locals)
			second_memory, second_identity = memory_reference(second_locals, "bytes", 8)
			second_view, second_view_identity = memory_reference(second_locals, "view", 4)
			assert second_memory != first_memory and second_identity == first_identity
			assert second_view != first_view and second_view_identity == view_identity
			request("variables", {"variablesReference": first_memory}, success=False)
			request("variables", {"variablesReference": first_view}, success=False)
			request("variables", {"variablesReference": watch["variablesReference"]}, success=False)
			assert second_frame != first_frame and second_locals != first_locals and second_array != first_array
			request("scopes", {"frameId": first_frame}, success=False)
			request("evaluate", {"expression": "values[0]", "frameId": first_frame}, success=False)
			request("variables", {"variablesReference": first_locals}, success=False)
			request("variables", {"variablesReference": first_array}, success=False)
			request("setVariable", {"variablesReference": first_array, "name": "[0]", "value": "7"}, success=False)
			assert request("evaluate", {"expression": "values[0]"})["result"] == "1"
			request("setVariable", {"variablesReference": second_array, "name": "[0]", "value": "9"})
			refreshed_array = array_reference(second_locals)
			assert refreshed_array != second_array
			request("variables", {"variablesReference": second_array}, success=False)
			assert variables(refreshed_array)[0]["value"] == "9"
			assert request("evaluate", {"expression": "values[0]"})["result"] == "9"
			request("continue", {"threadId": 1})
			event("terminated")
			assert event("exited")["body"]["exitCode"] == 0
			# setBreakpoints replaces a logical source, including an empty set
			# addressed through ./, platform separators or Windows case aliases.
			label_path = Path(label)
			label_path.write_text(source, encoding="utf-8")
			aliases = [str(label_path.parent) + "/./" + label_path.name]
			if sys.platform == "win32":
				aliases.append(label.upper().replace("\\", "/"))
			for alias in aliases:
				request("launch", {"program": label, "stopOnEntry": False})
				request("setBreakpoints", {"source": {"path": label}, "breakpoints": [{"line": 5}]})
				request("setBreakpoints", {"source": {"path": alias}, "breakpoints": []})
				start = len(messages)
				request("configurationDone")
				event("terminated")
				assert event("exited")["body"]["exitCode"] == 0
				assert not any(m.get("event") == "stopped" for m in messages[start:]), messages[start:]

			# Core readonly inspection must survive both the adapter and DAP JSON,
			# including nested payloads and watch expressions. Mutable locals stay editable.
			readonly_label = str(Path(directory) / "readonly.vs")
			request("launch", {"source": readonly_source, "fileLabel": readonly_label, "stopOnEntry": False})
			request("setBreakpoints", {"source": {"path": readonly_label}, "breakpoints": [{"line": 5}]})
			request("configurationDone")
			event("stopped")
			frame = request("stackTrace", {"threadId": 1})["stackFrames"][0]["id"]
			locals_ref = request("scopes", {"frameId": frame})["scopes"][0]["variablesReference"]
			local_rows = {v["name"]: v for v in variables(locals_ref)}
			assert "readOnly" not in local_rows["ready"].get("presentationHint", {}).get("attributes", [])

			def check_readonly_box(reference):
				children = {v["name"]: v for v in variables(reference)}
				assert set(children) == {"$present", "$type", "$value"}, children
				for child in children.values():
					assert "readOnly" in child.get("presentationHint", {}).get("attributes", []), child
				fields_ref = children["$value"]["variablesReference"]
				fields = variables(fields_ref)
				assert len(fields) == 1 and fields[0]["name"] == "value" and fields[0]["value"] == "7", fields
				assert "readOnly" in fields[0].get("presentationHint", {}).get("attributes", []), fields
				request("setVariable", {"variablesReference": fields_ref, "name": "value", "value": "9"}, success=False)
				assert variables(fields_ref)[0]["value"] == "7"

			check_readonly_box(local_rows["box"]["variablesReference"])
			watch = request("evaluate", {"expression": "box", "frameId": frame})
			check_readonly_box(watch["variablesReference"])
			request("setVariable", {"variablesReference": locals_ref, "name": "ready", "value": "1"})
			request("continue", {"threadId": 1})
			event("terminated")
			assert event("exited")["body"]["exitCode"] == 0
			# A structured child assignment must return a fresh expandable value,
			# while old pages expire even when the parent locals remain valid.
			replace_label = str(Path(directory) / "replace-child.vs")
			request("launch", {"source": replace_child_source, "fileLabel": replace_label, "stopOnEntry": False})
			request("setBreakpoints", {"source": {"path": replace_label}, "breakpoints": [{"line": 6}]})
			request("configurationDone")
			event("stopped")
			frame = request("stackTrace", {"threadId": 1})["stackFrames"][0]["id"]
			locals_ref = request("scopes", {"frameId": frame})["scopes"][0]["variablesReference"]
			box_ref = next(v["variablesReference"] for v in variables(locals_ref) if v["name"] == "box")
			old_values_ref = next(v["variablesReference"] for v in variables(box_ref) if v["name"] == "values")
			changed = request("setVariable", {"variablesReference": box_ref, "name": "values", "value": "replacement"})
			changed_ref = changed["variablesReference"]
			assert changed_ref > 0 and changed_ref != old_values_ref, changed
			assert [v["value"] for v in variables(changed_ref)] == ["7", "8"]
			request("variables", {"variablesReference": box_ref}, success=False)
			request("variables", {"variablesReference": old_values_ref}, success=False)
			scalar = request("setVariable", {"variablesReference": changed_ref, "name": "[0]", "value": "9"})
			assert scalar["value"] == "9" and scalar["variablesReference"] == 0, scalar
			request("variables", {"variablesReference": changed_ref}, success=False)
			local = request("setVariable", {"variablesReference": locals_ref, "name": "replacement", "value": "reset"})
			assert local["variablesReference"] > 0, local
			assert [v["value"] for v in variables(local["variablesReference"])] == ["4", "5"]
			request("continue", {"threadId": 1})
			event("terminated")
			assert event("exited")["body"]["exitCode"] == 0
			request("variables", {"variablesReference": local["variablesReference"]}, success=False)
			request("disconnect", {"terminateDebuggee": False})
			process.stdin.close()
			assert process.wait(timeout=15) == 0
		finally:
			if process.poll() is None:
				process.kill()
				process.wait(timeout=5)
			if evidence_path:
				evidence = Path(evidence_path)
				evidence.parent.mkdir(parents=True, exist_ok=True)
				evidence.write_text(json.dumps({
					"executable": executable, "sha256": hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
					"document": source, "imports": [{"path": str(library_path), "text": library_source}],
					"readonlyDocument": readonly_source,
					"replaceChildDocument": replace_child_source,
					"requests": requests, "messages": messages,
					"exitCode": process.returncode}, ensure_ascii=False, indent=2), encoding="utf-8")
	print("DAP public-macro breakpoints, source aliases, stopped-state handles, child assignments, readonly and memory inspection: PASS")


if __name__ == "__main__":
	run(str(Path(sys.argv[1]).resolve()), sys.argv[2] if len(sys.argv) > 2 else None)
