# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Verify receiver scope and protocol recovery through the actual LSP process."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import time

from VeroScriptLspResponsivenessTest import Client
from VeroScriptLspInteractionTests import marked_source, items, item_with_label, physical_lsp_position, apply_edit


def check_namespace_lookup(client, folder, records):
	declarations = ("namespace Inner{int rootValue=1;int rootCall(){return 1;}}\n"
		"namespace Outer{namespace Inner{int localValue=2;int localCall(){return 2;}}\n")
	cases = (
		("nearest_namespace", declarations + "int run(){return Inner::localValue;}}", "Inner::",
		 {"localValue", "localCall"}, {"rootValue", "rootCall"}),
		("nearest_namespace_prefix", declarations + "int run(){return Inner::localValue;}}", "Inner::loc",
		 {"localValue", "localCall"}, {"rootValue", "rootCall"}),
		("namespace_root", declarations + "int run(){return ::Inner::rootValue;}}", "::Inner::",
		 {"rootValue", "rootCall"}, {"localValue", "localCall"}),
		("namespace_using_child", "namespace Parent{namespace Child{int member=2;}}\n"
		 "using namespace Parent;\nvoid use(){print(Child::member);}", "Child::", {"member"}, set()),
		("namespace_missing_child", "namespace Parent{namespace Child{int member=2;}}\n"
		 "namespace Outer{namespace Parent{}\nvoid use(){print(Parent::Child::member);}}",
		 "Parent::Child::", set(), {"member"}),
		("namespace_using_target", "namespace Parent{namespace Root{}}\n"
		 "namespace Outer{namespace Parent{namespace Local{}}\nusing namespace Parent::Local;}",
		 "Parent::", {"Local"}, {"Root"}),
		("namespace_root_using_target", "namespace Parent{namespace Root{}}\n"
		 "namespace Outer{namespace Parent{namespace Local{}}\nusing namespace ::Parent::Root;}",
		 "::Parent::", {"Root"}, {"Local"}),
		("namespace_root_standard", "namespace Outer{namespace std{int localValue=2;}\n"
		 "void use(){::std::print(1);}}", "::std::pr", {"print"}, {"localValue"}),
		("namespace_type_shadow", "namespace Outer{class std{public:static int localValue=2;};\n"
		 "void use(){print(std::localValue);}}", "std::", {"localValue"}, {"algorithm", "print"}),
		("namespace_type_shadow_root", "namespace Outer{class std{public:static int localValue=2;};\n"
		 "void use(){::std::print(1);}}", "::std::pr", {"print"}, {"localValue"}),
	)
	for name, original, receiver, required, forbidden in cases:
		for variant in ("complete", "prior_error", "later_error", "crlf"):
			source = ("int broken=1\n" if variant == "prior_error" else "") + original
			if variant == "later_error":
				source += "\nvoid broken( {"
			if variant == "crlf":
				source = source.replace("\n", "\r\n")
			offset = source.rindex(receiver) + len(receiver)
			position = physical_lsp_position(source, offset)
			uri = (folder / (name + "_" + variant + ".vs")).as_uri()
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
			try:
				params = {"textDocument": {"uri": uri}, "position": position}
				response, _ = client.wait(client.request("textDocument/completion", params))
				records.append({"name": name + "_" + variant, "document": source, "position": position,
					"method": "textDocument/completion", "request": params, "response": response})
				assert "error" not in response, response
				labels = {item["label"] for item in items(response)}
				assert not labels & forbidden, (name, variant, labels)
				assert required <= labels, (name, variant, labels)
				for label in required:
					edit = item_with_label(response, label)["textEdit"]
					assert edit["range"]["start"]["line"] == position["line"], edit
					assert edit["range"]["end"]["line"] == position["line"], edit
					assert edit["range"]["start"]["character"] <= position["character"] <= edit["range"]["end"]["character"], edit
					applied = apply_edit(source, edit)
					assert applied[:offset - len(receiver)] == source[:offset - len(receiver)], edit
			finally:
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

	# A body defined outside its namespace keeps its declaration's lookup scope.
	source = ("namespace Util{int helper(int value){return value+2;}int add(int value);}\n"
		"string helper(string value){return value;}\nint Util::add(int value){return helper(value);}")
	uri = (folder / "namespace_body.vs").as_uri()
	client.send("textDocument/didOpen", {"textDocument": {
		"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
	try:
		for method, offset in (("hover", source.rindex("helper") + 2),
			("definition", source.rindex("helper") + 2), ("signatureHelp", source.rindex("helper(") + len("helper("))):
			position = physical_lsp_position(source, offset)
			params = {"textDocument": {"uri": uri}, "position": position}
			response, _ = client.wait(client.request("textDocument/" + method, params))
			records.append({"name": "namespace_body_" + method, "document": source, "position": position,
				"method": "textDocument/" + method, "request": params, "response": response})
			assert "error" not in response, response
			if method == "hover":
				assert "int" in response["result"]["contents"]["value"] and "string" not in response["result"]["contents"]["value"], response
			elif method == "definition":
				assert len(response["result"]) == 1 and response["result"][0]["range"]["start"]["line"] == 0, response
			else:
				assert len(response["result"]["signatures"]) == 1, response
				assert response["result"]["signatures"][0]["parameters"] == [{"label": "int value"}], response
	finally:
		client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

	# A using-directive resolves namespaces only, and each component identifies
	# its own namespace even when a type shadows the same spelling in expressions.
	source = "namespace Outer{class std{};using namespace std::chrono;}"
	uri = (folder / "namespace_using_hover.vs").as_uri()
	client.send("textDocument/didOpen", {"textDocument": {
		"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
	try:
		for token in ("std::chrono", "chrono"):
			position = physical_lsp_position(source, source.rindex(token) + 1)
			params = {"textDocument": {"uri": uri}, "position": position}
			response, _ = client.wait(client.request("textDocument/hover", params))
			records.append({"name": "namespace_using_hover_" + token, "document": source, "position": position,
				"method": "textDocument/hover", "request": params, "response": response})
			assert "error" not in response and "**` namespace " in response["result"]["contents"]["value"], response
			header = response["result"]["contents"]["value"].splitlines()[0]
			assert ("std::chrono" in header) == (token == "chrono"), response
	finally:
		client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

	# Standard recovery must not bypass a nearer namespace even without an AST.
	for kind, declaration in (("namespace", "namespace std{int localValue=2;}"),
		("type", "class std{public:static int localValue=2;};")):
		source = "void bad(){print(\"oops\nnamespace Outer{" + declaration + "\nvoid use(){std::print"
		uri = (folder / ("namespace_" + kind + "_shadow_recovery.vs")).as_uri()
		client.send("textDocument/didOpen", {"textDocument": {
			"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
		try:
			position = physical_lsp_position(source, len(source))
			params = {"textDocument": {"uri": uri}, "position": position}
			response, _ = client.wait(client.request("textDocument/completion", params))
			records.append({"name": "namespace_" + kind + "_shadow_recovery", "document": source, "position": position,
				"method": "textDocument/completion", "request": params, "response": response})
			assert "error" not in response and not items(response), response
		finally:
			client.send("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_signature_shadowing(client, folder, records):
	cases = (
		("standard_namespace_shadow", "namespace Outer{namespace std{int print(int value){return value;}}"
		 "void run(){std::print(1);}}", "std::print(", "int value"),
		("standard_type_shadow", "namespace Outer{class std{public:static int print(int value){return value;}};"
		 "void run(){std::print(1);}}", "std::print(", "int value"),
		("standard_alias_shadow", "class Printer{public:static int print(int value){return value;}};"
		 "namespace Outer{using std=Printer;void run(){std::print(1);}}", "std::print(", "int value"),
		("standard_type_named_method", "namespace Outer{class std{public:static int handle(int value){return value;}};"
		 "void run(){std::handle(1);}}", "std::handle(", "int value"),
		("standard_type_named_alias", "class Item{public:Item(int code){}};namespace Outer{"
		 "class std{public:using handle=Item;};void run(){std::handle(1);}}", "std::handle(", "int code"),
		("local_callable_shadow", "int foo(int value){return value;}void run(){"
		 'fn(string)->string foo=[](string text)->string{return text;};foo("x");}', "foo(", "string"),
		("local_value_shadow", "int foo(int value){return value;}void run(){int foo=0;foo(1);}", "foo(", None),
	)
	for name, original, token, expected_parameter in cases:
		for variant in ("complete", "prior_error", "later_error", "crlf"):
			source = ("int broken=1\n" if variant == "prior_error" else "") + original
			if variant == "later_error":
				source += "\nvoid broken( {"
			if variant == "crlf":
				source = source.replace("\n", "\r\n")
			position = physical_lsp_position(source, source.rindex(token) + len(token))
			uri = (folder / (name + "_" + variant + ".vs")).as_uri()
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
			try:
				params = {"textDocument": {"uri": uri}, "position": position}
				response, _ = client.wait(client.request("textDocument/signatureHelp", params))
				records.append({"name": name + "_" + variant, "document": source, "position": position,
					"method": "textDocument/signatureHelp", "request": params, "response": response})
				assert "error" not in response, response
				# Recovery may fail closed when another declaration cannot be parsed.
				# Any recovered signature must still respect the original binding.
				if variant in ("prior_error", "later_error") and response["result"] is None:
					continue
				if expected_parameter is None:
					assert response["result"] is None, response
				else:
					signatures = response["result"]["signatures"]
					assert len(signatures) == 1 and signatures[0]["parameters"] == [{"label": expected_parameter}], response
					if name == "standard_namespace_shadow":
						assert signatures[0]["label"].startswith("Outer::std::print "), response
			finally:
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_root_namespace_queries(client, folder, records):
	for name, shadow, member in (
		("root_standard_type", "class std{};", "print"),
		("root_standard_value", "", "print"),
		("root_missing_member", "class std{public:static int absent(){return 1;}};", "absent"),
	):
		original = "namespace Outer{" + shadow + "void run(){int std=0;::std::" + member + "(1);}}"
		for newline in ("\n", "\r\n"):
			source = original.replace("void run", newline + "void run")
			uri = (folder / (name + str(len(newline)) + ".vs")).as_uri()
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
			try:
				for method in ("hover", "signatureHelp"):
					offset = source.rindex(member + "(") + (1 if method == "hover" else len(member) + 1)
					position = physical_lsp_position(source, offset)
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/" + method, params))
					records.append({"name": name + str(len(newline)) + "_" + method, "document": source,
						"position": position, "method": "textDocument/" + method, "request": params, "response": response})
					assert "error" not in response, response
					if member == "absent":
						assert response["result"] is None, response
					elif method == "signatureHelp":
						assert [signature["label"] for signature in response["result"]["signatures"]] == [
							"void std::print(any value)", "void std::print(string fmt, ...)"], response
					else:
						assert "void std::print(any value);\nvoid std::print(string fmt, ...);" in response["result"]["contents"]["value"], response
			finally:
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_host_namespace_shadow(client, folder, records):
	# Catalog identities use the runtime's dotted spelling; source uses `::`.
	catalog = {"version": 1, "types": [{"name": "Outer.std", "kind": "class", "members": [
		{"kind": "staticConstant", "name": "localValue", "returnType": "int"}]}]}
	path = folder / "namespace-host.json"
	path.write_text(json.dumps(catalog), encoding="utf-8")
	client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"bindingCatalogPaths": [str(path)]}}})
	source = "namespace Outer{void use(){std::localValue;}}"
	uri = (folder / "namespace-host.vs").as_uri()
	client.send("textDocument/didOpen", {"textDocument": {
		"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
	try:
		for method in ("completion", "hover"):
			position = physical_lsp_position(source, source.index("localValue") + (0 if method == "completion" else 1))
			params = {"textDocument": {"uri": uri}, "position": position}
			response, _ = client.wait(client.request("textDocument/" + method, params))
			records.append({"name": "host_namespace_shadow_" + method, "catalog": catalog, "document": source,
				"position": position, "method": "textDocument/" + method, "request": params, "response": response})
			assert "error" not in response, response
			if method == "completion":
				assert {item["label"] for item in items(response)} == {"localValue"}, response
			else:
				assert "int" in response["result"]["contents"]["value"], response
	finally:
		client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
		client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"bindingCatalogPaths": []}}})


def check_host_generic_chains(client, folder, records):
	catalog = {"version": 1, "genericTypeTemplates": [{"name": "HostBox", "arity": 1}], "types": []}
	for argument in ("int", "string"):
		owner = "HostBox<" + argument + ">"
		catalog["types"].append({"name": owner, "kind": "class", "inheritable": True, "genericTemplateName": "HostBox",
			"genericTypeArguments": [argument], "members": [
				{"kind": "staticMethod", "name": "make", "returnType": "object", "returnHostTypeName": owner,
				 "paramTypes": [argument], "paramNames": ["value"], "annotations": [{"name": "Factory"}]},
				{"kind": "staticConstant", "name": "singleton", "returnType": "object", "returnHostTypeName": owner},
				{"kind": "propertyGetter", "name": "child", "returnType": "object", "returnHostTypeName": owner,
				 "annotations": [{"name": "Child"}]},
				{"kind": "propertyGetter", "name": "value", "returnType": argument},
				{"kind": "method", "name": "read", "returnType": argument}]})
	# Core forbids generic host bases. Ordinary inheritable host metadata covers
	# the same lookup/shadowing boundary without claiming that language feature.
	catalog["types"].append({"name": "HostBase", "kind": "class", "inheritable": True, "hasSubclass": True,
		"members": [
			{"kind": "staticMethod", "name": "make", "returnType": "object", "returnHostTypeName": "HostBase",
			 "paramTypes": ["int"], "paramNames": ["value"], "annotations": [{"name": "Factory"}]},
			{"kind": "staticConstant", "name": "singleton", "returnType": "object", "returnHostTypeName": "HostBase"},
			{"kind": "propertyGetter", "name": "child", "returnType": "object", "returnHostTypeName": "HostBase",
			 "annotations": [{"name": "Child"}]},
			{"kind": "propertyGetter", "name": "value", "returnType": "int"},
			{"kind": "method", "name": "read", "returnType": "int"}]})
	path = folder / "host-generic-chains.json"
	path.write_text(json.dumps(catalog), encoding="utf-8")
	client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"bindingCatalogPaths": [str(path)]}}})
	try:
		for argument, owner in (("int", "HostBox<int>"), ("string", "HostBox<string>"), ("int", "HostBase")):
			case_tag = "plain" if owner == "HostBase" else argument
			literal = '"value"' if argument == "string" else "1"
			cases = [
				("method", owner + "::make(" + literal + ").", True),
				("constant", owner + "::singleton.", True),
				("instance_static_method", "box.make(" + literal + ").", True),
				("instance_property", "box.child.", True),
				("type_instance_property", owner + "::value.", False),
				("instance_constant", "box.singleton.", False),
				("wrong_arity", "HostBox<int,string>::singleton.", False)]
			if owner == "HostBase":
				cases += [("derived_property", "box.child.", True), ("derived_private_shadow", "box.child.", False)]
			else:
				cases.append(("spaced", "HostBox < " + argument + " >::singleton.", True))
			for name, receiver, valid in cases:
				parameter_type, declaration = owner, ""
				if name.startswith("derived_"):
					parameter_type = "Derived"
					declaration = "class Derived:public " + owner + "{" + ("private:int child;" if name == "derived_private_shadow" else "") + "};"
				source = declaration + "void use(" + parameter_type + " box){" + receiver + "value;}"
				uri = (folder / ("host_chain_" + case_tag + "_" + name + ".vs")).as_uri()
				client.send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
				try:
					for method in ("completion", "hover"):
						position = physical_lsp_position(source, source.rindex(receiver) + len(receiver) + 2)
						params = {"textDocument": {"uri": uri}, "position": position}
						response, _ = client.wait(client.request("textDocument/" + method, params))
						records.append({"name": "host_chain_" + case_tag + "_" + name + "_" + method,
							"catalog": catalog, "document": source, "position": position,
							"method": "textDocument/" + method, "request": params, "response": response})
						assert "error" not in response, response
						if method == "completion":
							if valid:
								item = item_with_label(response, "value")
								assert item["detail"] == argument and apply_edit(source, item["textEdit"]) == source, item
								assert item["textEdit"]["newText"] == "value" and item["textEdit"]["range"] == {
									"start": {"line": position["line"], "character": position["character"] - 2},
									"end": {"line": position["line"], "character": position["character"] + 3}}, item
							else:
								assert not items(response), response
						elif valid:
							assert "```veroscript\n" + argument + "\n```" in response["result"]["contents"]["value"], response
						else:
							assert response["result"] is None, response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
			for name, receiver in (("method", owner + "::make(" + literal + ")"),
								   ("constant", owner + "::singleton")):
				source = "void use(){" + receiver + ".read();}"
				uri = (folder / ("host_chain_signature_" + case_tag + "_" + name + ".vs")).as_uri()
				client.send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
				try:
					position = physical_lsp_position(source, source.rindex("read(") + len("read("))
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/signatureHelp", params))
					records.append({"name": "host_chain_signature_" + case_tag + "_" + name,
						"catalog": catalog, "document": source, "position": position,
						"method": "textDocument/signatureHelp", "request": params, "response": response})
					assert "error" not in response and len(response["result"]["signatures"]) == 1, response
					signature = response["result"]["signatures"][0]
					assert argument + " ()" in signature["label"] and signature["parameters"] == [], response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
	finally:
		client.send("workspace/didChangeConfiguration", {"settings": {"veroscript": {"bindingCatalogPaths": []}}})


def check_preprocessed_completion(client, folder, records):
	cases = (
		("inactive_standard", "#if 0\nvoid run(){std::print\n#endif", "std::pr", False),
		("inactive_identifier", "int visible;\n#if 0\nvoid run(){vis\n#endif", "vis", False),
		("inactive_using", "#if 0\nusing namespace std::chrono;\n#endif", "std::ch", False),
		("inactive_type_before_error", "#if 0\nnamespace Outer{class std{};}\n#endif\n"
		 "void broken( {\nnamespace Outer{void run(){std::print", "std::pr", True),
		("inactive_namespace_before_error", "#if 0\nnamespace Outer{namespace std{}}\n#endif\n"
		 "void broken( {\nnamespace Outer{void run(){std::print", "std::pr", True),
		("macro_namespace_shadow", "#define START namespace Outer{\nSTART\nnamespace std{int localValue;}\n"
		 "void broken( {\nvoid run(){std::print", "std::pr", False),
		("macro_type_shadow", "#define DECL class std{};\nnamespace Outer{DECL\n"
		 "void broken( {\nvoid run(){std::print", "std::pr", False),
		("macro_prefix_range", "#define START void run(){\nvoid broken( {\nSTART std::print", "std::pr", True),
		("macro_prefix_lex_error", '#define START void run(){\nvoid broken(){print("oops\nSTART std::print', "std::pr", True),
	)
	for name, original, token, expect_print in cases:
		for variant in ("lf", "crlf"):
			source = original if variant == "lf" else original.replace("\n", "\r\n")
			offset = source.rindex(token) + len(token)
			position = physical_lsp_position(source, offset)
			uri = (folder / (name + "_" + variant + ".vs")).as_uri()
			client.send("textDocument/didOpen", {"textDocument": {
				"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
			try:
				for trigger in (1, 2):
					request_position = position if trigger == 1 else physical_lsp_position(source, offset - 2)
					params = {"textDocument": {"uri": uri}, "position": request_position, "context": {"triggerKind": trigger}}
					if trigger == 2:
						params["context"]["triggerCharacter"] = ":"
					response, _ = client.wait(client.request("textDocument/completion", params))
					records.append({"name": name + "_" + variant + "_" + str(trigger), "document": source,
						"position": request_position, "method": "textDocument/completion", "request": params, "response": response})
					assert "error" not in response, response
					if not expect_print:
						assert not items(response), response
						continue
					edit = item_with_label(response, "print")["textEdit"]
					assert edit["range"]["start"] == physical_lsp_position(source, offset - 2), edit
					assert edit["range"]["end"] == physical_lsp_position(source, offset + 3), edit
					assert apply_edit(source, edit) == source[:offset - 2] + edit["newText"] + source[offset + 3:], edit
			finally:
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})


def check_trailing_namespace_recovery(client, folder, records):
	for prefix in ("int broken[;void main(){", "#define START void main(){\nint broken[;\nSTART "):
		for newline in ("\n", "\r\n"):
			for trailing_newline in (False, True):
				source = (prefix + "std::i18n::").replace("\n", newline)
				position = physical_lsp_position(source, len(source))
				if trailing_newline:
					source += newline
				name = "namespace_eof_" + str(len(records))
				uri = (folder / (name + ".vs")).as_uri()
				client.send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
				try:
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/completion", params))
					records.append({"name": name, "document": source, "position": position,
						"method": "textDocument/completion", "request": params, "response": response})
					assert "error" not in response, response
					labels = {item["label"] for item in items(response)}
					assert {"text", "normalize_language_tag"} <= labels and not labels & {"auto", "false", "print"}, response
					for item in items(response):
						if "textEdit" in item:
							assert item["textEdit"]["range"] == {"start": position, "end": position}, item
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})


def main():
	executable, destination = map(Path, sys.argv[1:3])
	records = []
	initialize = None
	inner = "class Inner{public:const int value;int get(){return value;}};"
	with tempfile.TemporaryDirectory(prefix="vero-lsp-context-") as directory:
		folder = Path(directory)
		client = Client(str(executable), folder)
		try:
			malformed = '{"jsonrpc":"2.0","id":1,'
			body = malformed.encode()
			client.process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
			client.process.stdin.flush()
			response, _ = client.wait((None, time.monotonic()))
			assert response.get("id") is None and response.get("error", {}).get("code") == -32700, response
			records.append({"name": "malformed_json_recovery", "rawRequest": malformed, "response": response})
			initialize, _ = client.result(client.request("initialize", {"locale": "en-US", "capabilities": {
				"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}}))
			client.send("initialized")
			check_namespace_lookup(client, folder, records)
			check_signature_shadowing(client, folder, records)
			check_root_namespace_queries(client, folder, records)
			check_host_namespace_shadow(client, folder, records)
			check_host_generic_chains(client, folder, records)
			check_preprocessed_completion(client, folder, records)
			check_trailing_namespace_recovery(client, folder, records)

			def open_document(name, source):
				uri = (folder / (name + ".vs")).as_uri()
				client.send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1, "text": source}})
				return uri

			# A closing generic argument is still part of the type receiver.
			# Keep the concrete arity/arguments through completion and queries,
			# including recovery and the request-owned lexical index.
			generic_store = ("namespace Indexed{"
							 "struct Store<T>{static T shared;T value;};"
							 "struct Store<T,U>{static U shared;T value;};"
							 "struct Derived<T>:public Store<T>{};}")
			for name, declarations, expression, detail, target in (
				("generic_static", "struct Store<T>{static T shared;T value;};",
				 "Store<int>::shared", "static int", "static T shared"),
				("generic_qualified_static", generic_store,
				 "Indexed::Store<int>::shared", "static int", "static T shared"),
				("generic_other_arity_static", generic_store,
				 "Indexed::Store<int,string>::shared", "static string", "static U shared"),
				("generic_inherited_static", generic_store,
				 "Indexed::Derived<int>::shared", "static int", "static T shared"),
				("generic_global_static", generic_store,
				 "::Indexed::Store<int>::shared", "static int", "static T shared"),
				("generic_nested_argument_static", generic_store,
				 "Indexed::Store<handle<int>>::shared", "static handle<int>", "static T shared"),
				("generic_default_static", "struct Store<T=int>{static T shared;};",
				 "Store<>::shared", "static int", "static T shared"),
				("generic_local_argument_alias_static", generic_store,
				 "Indexed::Store<Pick>::shared", "static string", "static T shared"),
				("generic_nested_type_static", "struct Outer<T>{struct Inner<U>{static U shared;};};",
				 "Outer<int>::Inner<string>::shared", "static string", "static U shared"),
				("generic_value_argument_static", "template<int N> struct Store{static int shared;};",
				 "Store<2>::shared", "static int", "static int shared"),
				("generic_shift_argument_static", "template<int N> struct Store{static int shared;};",
				 "Store<(4 >> 1)>::shared", "static int", "static int shared"),
				("generic_comparison_argument_static", "template<int N> struct Store{static int shared;};",
				 "Store<(4 > 1 ? 2 : 3)>::shared", "static int", "static int shared"),
			):
				for variant in ("complete", "unfinished", "prior_error", "prior_comment_error", "same_line_error", "later_error", "crlf"):
					before = ("int broken=1\n" if variant == "prior_error" else
							  "int broken=1 // keep comment\n" if variant == "prior_comment_error" else
							  "int broken=1 " if variant == "same_line_error" else "")
					after = "\nvoid broken( {" if variant == "later_error" else ""
					tail = "" if variant == "unfinished" else ");}"
					source = before + declarations + "\nvoid use(){using Pick=string;print(" + expression + tail + after
					if variant == "crlf":
						source = source.replace("\n", "\r\n")
					offset = source.index(expression) + len(expression) - len("shared") + 2
					position = physical_lsp_position(source, offset)
					uri = open_document(name + "_" + variant, source)
					try:
						for method in ("completion", "hover", "definition"):
							params = {"textDocument": {"uri": uri}, "position": position}
							response, _ = client.wait(client.request("textDocument/" + method, params))
							records.append({"name": name + "_" + variant + "_" + method,
											"document": source, "position": position,
											"method": "textDocument/" + method,
											"request": params, "response": response})
							assert "error" not in response, response
							if method == "completion":
								item = item_with_label(response, "shared")
								assert item["detail"] == detail, item
								assert item["textEdit"]["newText"] == "shared", item
								assert apply_edit(source, item["textEdit"]) == source, item
							elif method == "hover":
								assert "```veroscript\n" + detail + "\n```" in response["result"]["contents"]["value"], response
							else:
								assert len(response["result"]) == 1 and response["result"][0]["uri"] == uri, response
								expected = physical_lsp_position(source, source.index(target))
								assert response["result"][0]["range"]["start"] == expected, response
					finally:
						client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

			for name, source, receiver, allowed in (
				("empty_generic_type_scope", generic_store + "void use(){Indexed::Store<int>::;}",
				 "Indexed::Store<int>::", {"shared"}),
				("empty_inherited_generic_scope", generic_store + "void use(){Indexed::Derived<int>::;}",
				 "Indexed::Derived<int>::", {"shared"}),
				("invalid_generic_arity_scope", generic_store + "int shared;void use(){Indexed::Store<int,string,int>::;}",
				 "Indexed::Store<int,string,int>::", set()),
				("unknown_generic_scope", generic_store + "int shared;void use(){Missing::Store<int>::;}",
				 "Missing::Store<int>::", set()),
				("shadowed_generic_scope", "struct Store<T>{static T shared;};void use(){int Store=0;Store<int>::;}",
				 "Store<int>::", set()),
				("generic_static_method_scope", "struct Store<T>{static T shared;T value;static T build(){return T();}T read(){return value;}};void use(){Store<int>::;}",
				 "Store<int>::", {"shared", "build"}),
			):
				position = physical_lsp_position(source, source.index(receiver) + len(receiver))
				uri = open_document(name, source)
				try:
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/completion", params))
					records.append({"name": name, "document": source, "position": position,
									"method": "textDocument/completion", "request": params, "response": response})
					assert "error" not in response, response
					candidates = items(response)
					assert {item["label"] for item in candidates} == allowed, response
					for item in candidates:
						edit = item.get("textEdit", {"range": {"start": position, "end": position},
													 "newText": item.get("insertText", item["label"])})
						assert edit["range"] == {"start": position, "end": position}, item
						assert edit["newText"].startswith(item["label"]), item
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

			for name, source, detail in (
				("open_generic_method_static",
				 "struct Store<T>{static T shared;T read(){return Store<T>::shared;}};", "static T"),
				("open_generic_function_static",
				 "struct Store<T>{static T shared;};template<typename X> X read(){return Store<X>::shared;}", "static X"),
				("open_value_generic_static",
				 "template<int N> struct Store{static int shared;int read(){return Store<N>::shared;}};", "static int"),
				("generic_lexical_namespace_static",
				 "struct Store<T>{static string wrong;};namespace Indexed{struct Store<T>{static T shared;};void read(){print(Store<int>::shared);}}", "static int"),
			):
				position = physical_lsp_position(source, source.rindex("::shared") + len("::sh"))
				uri = open_document(name, source)
				try:
					for method in ("completion", "hover"):
						params = {"textDocument": {"uri": uri}, "position": position}
						response, _ = client.wait(client.request("textDocument/" + method, params))
						records.append({"name": name + "_" + method, "document": source,
										"position": position, "method": "textDocument/" + method,
										"request": params, "response": response})
						assert "error" not in response, response
						if method == "completion":
							item = item_with_label(response, "shared")
							assert item["detail"] == detail and apply_edit(source, item["textEdit"]) == source, item
						else:
							assert "```veroscript\n" + detail + "\n```" in response["result"]["contents"]["value"], response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

			def check_chain(name, marked, library=None, caller_type=True, negative=False):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				library_uri = None
				if library:
					library_uri = open_document("receiver-library", library)
				try:
					for method in ("completion", "hover", "definition", "references"):
						params = {"textDocument": {"uri": uri}, "position": position}
						if method == "references":
							params["context"] = {"includeDeclaration": True}
						response, _ = client.wait(client.request("textDocument/" + method, params))
						records.append({"name": name + "_" + method, "document": source,
										"dependencies": [{"uri": library_uri, "document": library}] if library else [],
										"position": position, "method": "textDocument/" + method,
										"request": params, "response": response})
						assert "error" not in response, response
						if negative:
							assert (items(response) == [] if method == "completion" else
									response["result"] == (None if method == "hover" else [])), response
						elif method == "completion":
							item = item_with_label(response, "value")
							assert item["detail"] == "const int", item
							assert item["textEdit"]["range"] == {
								"start": {"line": 0, "character": position["character"] - 2},
								"end": {"line": 0, "character": position["character"] + 3}}, item
						elif method == "hover":
							assert "const int" in response["result"]["contents"]["value"], response
							assert "Inner.value" in response["result"]["contents"]["value"], response
						elif method == "definition":
							target_uri, target_source = (uri, source) if caller_type else (library_uri, library)
							assert len(response["result"]) == 1 and response["result"][0]["uri"] == target_uri, response
							assert response["result"][0]["range"]["start"] == {
								"line": 0, "character": target_source.index("const int value")}, response
						else:
							assert any(location["uri"] == uri and
									   location["range"]["start"]["character"] == position["character"] - 2
									   for location in response["result"]), response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
					if library_uri:
						client.send("textDocument/didClose", {"textDocument": {"uri": library_uri}})

			for name, marked in (
				("local_constructor_alias_chain", inner + "void use(){using Local=Inner;Local().va|lue;}"),
				("function_value_chain", inner + "void use(fn()->Inner factory){factory().va|lue;}"),
				("const_reference_function_value_chain", inner + "void use(fn()->const Inner& factory){factory().va|lue;}"),
				("nested_const_reference_function_value_chain", inner + "void use(fn()->fn()->const Inner& factory){factory()().va|lue;}"),
				("local_generic_alias_chain", inner + "T make<T>(T value){return value;}void main(){using Local=Inner;make<Local>(Local()).va|lue;}"),
				("nested_local_generic_alias_chain", inner + "T make<T>(T value){return value;}void main(){using Local=Inner;make<handle<Local>>(handle<Local>()).value.va|lue;}"),
				("reference_return_chain", inner + "Inner& borrow(Inner& x){return x;}void use(Inner& x){borrow(ref x).va|lue;}"),
				("const_reference_return_chain", inner + "const Inner& borrow(const Inner& x){return x;}void use(const Inner& x){borrow(ref x).va|lue;}"),
				("nested_function_value_chain", inner + "void use(fn()->fn()->Inner factory){factory()().va|lue;}"),
				("field_function_value_chain", inner + "class Outer{public:fn()->Inner factory;};void use(Outer x){(x.factory)().va|lue;}"),
				("implicit_this_chain", inner + "class Outer{public:Inner child;int get(){return child.va|lue;}};"),
				("implicit_method_chain", inner + "class Outer{public:Inner child;Inner read(){return child;}int get(){return read().va|lue;}};"),
				("generic_static_field_value_chain", inner + "struct Store<T>{static Inner child;};void use(){Store<int>::child.va|lue;}"),
				("generic_static_field_parenthesized_chain", inner + "struct Store<T>{static Inner child;};void use(){(Store<int>::child).va|lue;}"),
				("generic_nested_static_field_chain", inner + "struct Store<T>{struct Item{static Inner child;};};void use(){Store<int>::Item::child.va|lue;}"),
				("generic_static_method_return_chain", inner + "struct Store<T>{static Inner make(){return Inner();}};void use(){Store<int>::make().va|lue;}"),
				("generic_static_value_method_chain", inner + "class Item{public:Inner read(){return Inner();}};class Store<T>{public:static Item child=Item();};void use(){Store<int>::child.read().va|lue;}"),
				("generic_static_value_alias_chain", inner + "namespace N{class Store<T>{public:static T child=T();};}using Item=Inner;using Cache=N::Store<Item>;void use(){Cache::child.va|lue;}"),
				("nested_generic_static_value_chain", inner + "class Store<T>{public:static T child=T();};void use(){Store<handle<Inner>>::child.value.va|lue;}"),
				("generic_static_container_value_chain", inner + "class Store<T>{public:static T child=T();};void use(){Store<array<Inner>>::child[0].va|lue;}"),
				("generic_static_map_value_chain", inner + "class Store<T>{public:static T child=T();};void use(){Store<map<int,Inner>>::child[0].va|lue;}"),
				("nested_generic_static_method_chain", inner + "class Store<T>{public:static T make(){return T();}};void use(){Store<handle<Inner>>::make().value.va|lue;}"),
			):
				check_chain(name, marked)
			check_chain("private_shadowed_chain_field", "class Inner{public:int value;};class Base{public:Inner child;};class Outer:public Base{private:string child;};void use(Outer x){x.child.va|lue;}", negative=True)
			check_chain("invalid_generic_static_value_arity", inner + "class Store<T>{public:static T child=T();};void use(){Store<Inner,int>::child.va|lue;}", negative=True)
			for name, argument in (("static_array_type_hover", "array<Inner>"),
								   ("static_map_type_hover", "map<int,Inner>")):
				source, position = marked_source(inner + "class Store<T>{public:static T child=T();};void use(){Store<" + argument + ">::ch|ild;}")
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response, _ = client.wait(client.request("textDocument/hover", params))
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/hover", "request": params, "response": response})
				assert "error" not in response and "static " + argument in response["result"]["contents"]["value"], response
				assert "Store<" + argument + ">.child" in response["result"]["contents"]["value"], response
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

			for name, library, marked, caller_type in (
				("module_function_alias_chain", "using Result=Inner;export Result make(){return Inner();}",
				 "class Wrong{public:string wrong;};void main(){using Result=Wrong;make().va|lue;}", False),
				("module_field_alias_chain", "using Result=Inner;export class Outer{public:Result child;};",
				 "class Wrong{public:string wrong;};void use(Outer x){using Result=Wrong;x.child.va|lue;}", False),
				("module_method_alias_chain", "using Result=Inner;export class Outer{public:Result get(){return Inner();}};",
				 "class Wrong{public:string wrong;};void use(Outer x){using Result=Wrong;x.get().va|lue;}", False),
				("module_generic_argument_chain", "using Inner=string;export T make<T>(T value){return value;}",
				 "class Inner{public:const int value;};void main(){make<Inner>(Inner()).va|lue;}", True),
				("module_private_type_alias_chain", "class Local{public:string wrong;};export void unused(){}",
				 "class Inner{public:const int value;};using Local=Inner;void main(){Local().va|lue;}", True),
			):
				check_chain(name, 'import "receiver-library.vs";' + marked,
							library if caller_type else "export " + inner + library, caller_type)

			for name, marked in (
				("local_alias_chain_signature", inner + "void use(){using Local=Inner;Local().get(|);}"),
				("nested_callable_chain_signature", inner + "void use(fn()->fn()->Inner factory){factory()().get(|);}"),
				("field_callable_chain_signature", inner + "class Outer{public:fn()->Inner factory;};void use(Outer x){(x.factory)().get(|);}"),
				("generic_static_value_chain_signature", inner + "struct Store<T>{static T child;};void use(){Store<Inner>::child.get(|);}"),
				("generic_static_value_method_chain_signature", inner + "class Item{public:Inner read(){return Inner();}};class Store<T>{public:static Item child=Item();};void use(){Store<int>::child.read().get(|);}"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response, _ = client.wait(client.request("textDocument/signatureHelp", params))
				assert "error" not in response and len(response["result"]["signatures"]) == 1, response
				assert "get" in response["result"]["signatures"][0]["label"], response
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params, "response": response})
				client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
			console_functions = (
				("write", "void", ["any value"], "1", "without adding a newline"),
				("error", "void", ["any value"], '"error"', "standard error"),
				("flush", "void", [], "", "Flush pending standard output"),
				("clear", "bool", [], "", "scrollback is retained"),
				("is_interactive", "bool", [], "", "both standard input and standard output"),
				("supports_color", "bool", [], "", "output supports color"),
				("supports_cursor", "bool", [], "", "output supports cursor control"),
				("supports_clear", "bool", [], "", "clearing the window or line"),
				("is_output_terminal", "bool", [], "", "standard input may be redirected"),
				("set_color", "bool", ["string foreground", "string background"], '"red", "default"', "bright_ variants"),
				("reset_style", "bool", [], "", "Reset terminal style"),
				("clear_line", "bool", ["string mode"], '"all"', "after, before, or all"),
				("move_cursor", "bool", ["int column", "int row"], "0, 0", "0..32766"),
				("set_cursor_visible", "bool", ["bool visible"], "false", "visibility is restored"),
				("width", "int", [], "", "width in columns; return 0"),
				("height", "int", [], "", "height in rows; return 0"),
			)

			def documentation_text(value):
				return value.get("value", "") if isinstance(value, dict) else value or ""

			def console_request(name, source, position, method, item=None):
				uri = open_document(name, source)
				params = item if item is not None else {"textDocument": {"uri": uri}, "position": position}
				try:
					response, _ = client.wait(client.request(method, params))
					records.append({"name": name, "document": source, "position": position,
									"method": method, "request": params, "response": response})
					assert "error" not in response, response
					return response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})

			for name, return_type, parameters, arguments, documentation in console_functions:
				source = "void main(){std::console::" + name + ";}"
				start = source.index(name)
				position = {"line": 0, "character": start + min(3, len(name))}
				expected_signature = return_type + " std::console::" + name + "(" + ", ".join(parameters) + ")"
				completion = console_request("console_" + name + "_completion", source, position, "textDocument/completion")
				item = item_with_label(completion, name)
				assert item["detail"].rstrip(";") == expected_signature, item
				expected_snippet = name + "(" + (", ".join("${" + str(index + 1) + ":" +
					parameter.rsplit(" ", 1)[-1] + "}" for index, parameter in enumerate(parameters))
					if parameters else "${0}") + ")"
				assert item["insertTextFormat"] == 2 and item["insertText"] == expected_snippet, item
				assert item["textEdit"] == {"range": {"start": {"line": 0, "character": start},
					"end": {"line": 0, "character": start + len(name)}}, "newText": expected_snippet}, item
				assert documentation in documentation_text(item.get("documentation")), item
				resolved = console_request("console_" + name + "_resolve", source, position, "completionItem/resolve", item)["result"]
				assert resolved["textEdit"] == item["textEdit"] and resolved["detail"] == item["detail"], resolved
				assert documentation in documentation_text(resolved.get("documentation")), resolved
				hover = console_request("console_" + name + "_hover", source, position, "textDocument/hover")["result"]
				assert "```veroscript\n" + expected_signature + ";\n```" in hover["contents"]["value"], hover
				assert documentation in hover["contents"]["value"], hover
				call_source = "void main(){std::console::" + name + "(" + arguments + ");}"
				call_position = {"line": 0, "character": call_source.index(name + "(") + len(name) + 1}
				signature = console_request("console_" + name + "_signature", call_source, call_position, "textDocument/signatureHelp")["result"]
				assert len(signature["signatures"]) == 1 and signature["signatures"][0]["label"] == expected_signature, signature
				assert [p["label"] for p in signature["signatures"][0].get("parameters", [])] == parameters, signature
				assert documentation in documentation_text(signature["signatures"][0].get("documentation")), signature

			source, position = marked_source("void main(){std::console::|}")
			namespace_items = items(console_request("console_namespace_completion", source, position, "textDocument/completion"))
			assert {item["label"] for item in namespace_items} == {spec[0] for spec in console_functions}, namespace_items
			assert all("std.console" not in json.dumps({key: item.get(key) for key in
				("label", "labelDetails", "detail", "insertText", "textEdit", "$valencePresentation")})
				for item in namespace_items), namespace_items
			source, position = marked_source("void main(){std::con|sole::write(1);}")
			namespace_hover = console_request("console_namespace_hover", source, position, "textDocument/hover")["result"]["contents"]["value"]
			assert all(word in namespace_hover for word in ("color", "cursor", "dimensions", "supports_color", "clear_line", "width")), namespace_hover
			source, position = marked_source('void main(){std::console::set_color("red", |"default");}')
			color_signature = console_request("console_background_parameter", source, position, "textDocument/signatureHelp")["result"]
			assert color_signature["activeSignature"] == 0 and color_signature["activeParameter"] == 1, color_signature
			source, position = marked_source("using namespace std::console;void main(){wri|;}")
			using_item = item_with_label(console_request("console_using_completion", source, position, "textDocument/completion"), "write")
			assert "without adding a newline" in documentation_text(using_item.get("documentation")), using_item
			assert using_item["insertText"] == "std::console::write(${1:value})", using_item
			using_resolved = console_request("console_using_resolve", source, position, "completionItem/resolve", using_item)["result"]
			assert using_resolved["documentation"] == using_item["documentation"], using_resolved
			for name, marked, insertion in (
				("console_root", "void main(){std::con|sole;}", "console::"),
				("console_root_existing_scope", "void main(){std::con|sole::write(1);}", "console"),
			):
				source, position = marked_source(marked)
				root_item = item_with_label(console_request(name + "_completion", source, position, "textDocument/completion"), "console")
				root_documentation = documentation_text(root_item.get("documentation"))
				assert all(word in root_documentation for word in
					("Console output", "window dimensions", "set_color", "clear_line")), root_item
				assert root_item["detail"] == "namespace std::console (builtin)", root_item
				start = source.index("console")
				assert root_item["textEdit"] == {"range": {"start": {"line": 0, "character": start},
					"end": {"line": 0, "character": start + len("console")}}, "newText": insertion}, root_item
				assert root_item["insertText"] == insertion, root_item
				root_resolved = console_request(name + "_resolve", source, position, "completionItem/resolve", root_item)["result"]
				assert root_resolved["documentation"] == root_item["documentation"], root_resolved
				assert root_resolved["textEdit"] == root_item["textEdit"], root_resolved
			source, position = marked_source("void main(){console_wi|dth();}")
			assert items(console_request("console_intrinsic_hidden_completion", source, position, "textDocument/completion")) == []
			assert console_request("console_intrinsic_hidden_hover", source, position, "textDocument/hover")["result"] is None
			# Pass the exact retained item in the protocol's `item` member.
			# Generic function types must survive substitution and base-list parsing.
			for name, argument in (("integer", "int"), ("callable", "fn(int,int)->int"),
								   ("nested_callable", "handle<fn(int,int)->int>"), ("default", "string")):
				base = ("class Base<T,U=int>{public:T payload;virtual int Apply(U value){return 1;}};\n"
						if name == "default" else "class Base<T>{public:virtual int Apply(T value){return 1;}};\n")
				source = (base +
						  "class Other{};\n"
						  f"class Child:public Base<{argument}>" + ("" if name == "nested_callable" else ",public Other")
						  + f"{{public:int Apply({'int' if name == 'default' else argument} value) override{{return 2;}}}};\n"
						  "void main(){}")
				uri = open_document("hierarchy_" + name, source)
				def hierarchy_request(suffix, method, params):
					response, _ = client.wait(client.request(method, params))
					assert "error" not in response, response
					records.append({"name": "hierarchy_" + name + "_" + suffix, "document": source,
									"method": method, "request": params, "response": response})
					return response["result"]
				try:
					implementation = hierarchy_request("implementation", "textDocument/implementation", {
						"textDocument": {"uri": uri}, "position": {"line": 0, "character": source.splitlines()[0].index("Apply") + 1}})
					assert len(implementation) == 1 and implementation[0]["uri"] == uri, implementation
					assert implementation[0]["range"]["start"]["line"] == 2, implementation
					child = hierarchy_request("prepare", "textDocument/prepareTypeHierarchy", {
						"textDocument": {"uri": uri}, "position": {"line": 2, "character": 7}})
					assert len(child) == 1 and child[0]["name"] == "Child", child
					parents = hierarchy_request("parents", "typeHierarchy/supertypes", {"item": child[0]})
					expected_base = "Base<T, U = int>" if name == "default" else "Base<T>"
					assert [p["name"] for p in parents] == ([expected_base] if name == "nested_callable" else [expected_base, "Other"]), parents
					children = hierarchy_request("children", "typeHierarchy/subtypes", {"item": parents[0]})
					assert len(children) == 1 and children[0]["data"] == child[0]["data"], children
					assert hierarchy_request("missing_item", "typeHierarchy/supertypes", {}) == []
					assert hierarchy_request("stale_item", "typeHierarchy/subtypes", {
						"item": {**child[0], "data": {"veroscriptTypeSymbolId": "missing"}}}) == []
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
			source = ("class Root{public:virtual int Apply(int x){return x;}};\n"
					  "class Loop<T>:public Loop<array<T>>{public:int Apply(int x){return x;}};\nvoid main(){}")
			uri = open_document("expanding_generic_cycle", source)
			params = {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 30}}
			response, _ = client.wait(client.request("textDocument/implementation", params))
			records.append({"name": "expanding_generic_cycle", "document": source,
							"method": "textDocument/implementation", "request": params, "response": response})
			assert response.get("result") == [], response
			client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
			# The following request also proves that the server survived the cycle.
			response, _ = client.wait(client.request("workspace/symbol", {"query": "std::console"}))
			assert "error" not in response, response
			records.append({"name": "expanding_generic_cycle_server_alive", "method": "workspace/symbol",
							"request": {"query": "std::console"}, "response": response})
			# Closed value applications have the same declarator-name role as
			# type applications. Keep the actual documents marker-free on wire.
			buffer = "template<int N> struct Buffer{};"
			declaration_cases = [
				("blank", buffer + "void use(){Buffer<2> |;}"),
				("name", buffer + "void use(){Buffer<2> na|me;}"),
				("parameter", buffer + "void use(Buffer<2> na|me){}"),
				("field", buffer + "struct Holder{Buffer<2> na|me;};"),
				("global", buffer + "Buffer<2> na|me;"),
				("for", buffer + "void use(){for(Buffer<2> na|me;;){}}"),
				("reference", buffer + "void use(Buffer<2>& na|me){}"),
				("spaced", buffer + "void use(){Buffer < 2 > na|me;}"),
				("nested", buffer + "struct Holder<T>{};void use(){Holder<Buffer<2>> na|me;}"),
				("comparison_argument", buffer + "void use(){Buffer<(1<2?3:4)> na|me;}"),
				("shift_argument", buffer + "void use(){Buffer<(8>>1)> na|me;}"),
				("mixed", "template<typename T,int N> struct Buffer{};void use(){Buffer<int,2> na|me;}"),
				("bool", "template<bool B> struct Flag{};void use(){Flag<true> na|me;}"),
				("char", "template<char C> struct Flag{};void use(){Flag<'a'> na|me;}"),
				("namespace", "namespace N{" + buffer + "}void use(){N::Buffer<2> na|me;}"),
				("multiline", buffer + "void use(){Buffer<2>\n    na|me;}"),
				("incomplete", buffer + "void use(){Buffer<2> na|"),
			]
			declaration_cases += [
				("error_before", "void broken(){int =;}" + buffer + "void use(){Buffer<2> na|me;}"),
				("error_after", buffer + "void use(){Buffer<2> na|me;}void broken(){int =;}"),
				("unknown", "void use(){Unknown<2> na|me;}"),
			]
			library = "export " + buffer
			library_path = folder / "value-types.vs"
			library_path.write_text(library, encoding="utf-8")
			declaration_cases.append(("imported", 'import "value-types.vs";void use(){Buffer<2> na|me;}'))
			for name, marked in declaration_cases:
				source, position = marked_source(marked)
				uri = open_document("value_declarator_" + name, source)
				try:
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/completion", params))
					records.append({"name": "value_declarator_" + name, "document": source,
						"position": position, "method": "textDocument/completion", "request": params,
						"response": response, "dependencies": [{"uri": library_path.as_uri(), "document": library}]
						if name == "imported" else []})
					assert "error" not in response and items(response) == [], response
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
			for name, marked in (
				("glued", "void use(){int value=0;int other=0;value<2>ot|her;}"),
				("spaced", "void use(){int value=0;int other=0;value < 2 > ot|her;}"),
				("parenthesized", "void use(){int value=0;int other=0;value<(1+2)>ot|her;}"),
				("condition", "void use(){int other=0;if(other<2>ot|her){}}"),
				("shadow", buffer + "void use(){int Buffer=0;int other=0;Buffer<2>ot|her;}"),
				("field", "struct S{int value;};void use(S s){int other=0;s.value<2>ot|her;}"),
			):
				source, position = marked_source(marked)
				uri = open_document("value_comparison_" + name, source)
				try:
					params = {"textDocument": {"uri": uri}, "position": position}
					response, _ = client.wait(client.request("textDocument/completion", params))
					records.append({"name": "value_comparison_" + name, "document": source,
						"position": position, "method": "textDocument/completion", "request": params,
						"response": response})
					assert "error" not in response, response
					item = item_with_label(response, "other")
					assert item["textEdit"]["range"] == {
						"start": {"line": 0, "character": position["character"] - 2},
						"end": {"line": 0, "character": position["character"] + 3}}, item
				finally:
					client.send("textDocument/didClose", {"textDocument": {"uri": uri}})
		finally:
			client.close()
			destination.parent.mkdir(parents=True, exist_ok=True)
			destination.write_text(json.dumps({"executable": str(executable.resolve()),
				"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
				"serverInfo": initialize.get("serverInfo") if initialize else None, "cases": records},
				ensure_ascii=False, indent=2), encoding="utf-8")
	print(f"{len(records)} LSP context cases passed; evidence: {destination}")


if __name__ == "__main__":
	main()
