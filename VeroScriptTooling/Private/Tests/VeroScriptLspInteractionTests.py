#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise raw LSP formatting, namespace completion, and hover responses."""

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

wire_trace = []


def frame(message):
	body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
	return b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body


def read_frame(stream):
	headers = {}
	while line := stream.readline():
		if line == b"\r\n":
			break
		key, value = line.decode("ascii").strip().split(":", 1)
		headers[key.lower()] = value.strip()
	if "content-length" not in headers:
		raise RuntimeError("LSP server closed its output stream")
	return json.loads(stream.read(int(headers["content-length"])))


def utf16_offset(source, position):
	lines = source.splitlines(keepends=True)
	if not lines or source.endswith(("\n", "\r")):
		lines.append("")
	assert position["line"] < len(lines), position
	line = lines[position["line"]]
	content = line.rstrip("\r\n")
	units = 0
	for index, char in enumerate(content):
		if units == position["character"]:
			return sum(map(len, lines[:position["line"]])) + index
		units += 2 if ord(char) > 0xFFFF else 1
	assert units == position["character"], (position, content)
	return sum(map(len, lines[:position["line"]])) + len(content)


def apply_edit(source, edit):
	start = utf16_offset(source, edit["range"]["start"])
	end = utf16_offset(source, edit["range"]["end"])
	assert start <= end
	return source[:start] + edit["newText"] + source[end:]


def physical_lsp_position(source, offset):
	prefix = source[:offset].replace("\r\n", "\n").replace("\r", "\n")
	return {"line": prefix.count("\n"),
			"character": len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def check_document_symbol_ranges(source, symbol, parent=None):
	extent = tuple(utf16_offset(source, symbol["range"][edge]) for edge in ("start", "end"))
	selected = tuple(utf16_offset(source, symbol["selectionRange"][edge]) for edge in ("start", "end"))
	assert extent[0] <= selected[0] < selected[1] <= extent[1], (source, symbol)
	if parent is not None:
		assert parent[0] <= extent[0] <= extent[1] <= parent[1], (source, symbol, parent)
	for child in symbol.get("children", []):
		check_document_symbol_ranges(source, child, extent)


def marked_source(marked):
	marker = marked.index("|")
	source = marked[:marker] + marked[marker + 1:]
	before = marked[:marker]
	position = {
		"line": before.count("\n"),
		"character": len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2,
	}
	return source, position


def items(response):
	result = response["result"]
	return result if isinstance(result, list) else result["items"]


def item_with_label(response, label):
	return next(item for item in items(response) if item["label"] == label)


def main():
	executable, destination = map(Path, sys.argv[1:3])
	records = []
	with tempfile.TemporaryDirectory(prefix="veroscript-lsp-interaction-") as directory:
		with subprocess.Popen(
			[str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
		) as process:
			next_id = 1
			diagnostics_by_uri = {}
			open_uris = set()

			def send(method, params, *, notification=False, expect_error=False):
				nonlocal next_id
				message = {"jsonrpc": "2.0", "method": method, "params": params}
				if not notification:
					next_id += 1
					message["id"] = next_id
				wire_trace.append({"direction": "request", "message": message})
				if method == "textDocument/didOpen":
					open_uris.add(params["textDocument"]["uri"])
				elif method == "textDocument/didClose":
					open_uris.discard(params["textDocument"]["uri"])
				process.stdin.write(frame(message))
				process.stdin.flush()
				if notification:
					return None
				while True:
					response = read_frame(process.stdout)
					if response.get("method") == "textDocument/publishDiagnostics":
						diagnostics_by_uri[response["params"]["uri"]] = response["params"]
					if response.get("id") == next_id:
						wire_trace.append({"direction": "response", "message": response})
						if expect_error:
							assert response.get("error", {}).get("code") == -32602, response
						else:
							assert "error" not in response, response
						return response

			initialize = send("initialize", {"locale": "en-US", "capabilities": {
				"textDocument": {"completion": {"completionItem": {
					"snippetSupport": True,
				}}},
			}})
			send("initialized", {}, notification=True)

			isolated_review_documents = []

			def open_document(name, source):
				# Each fixture in the initial review group is self-contained.
				# Release its overlay before opening the next one; later
				# workspace tests keep their explicitly shared documents.
				if isolated_review_documents:
					for previous_uri in isolated_review_documents:
						send("textDocument/didClose", {"textDocument": {"uri": previous_uri}}, notification=True)
					isolated_review_documents.clear()
				uri = (Path(directory) / (name + ".vs")).resolve().as_uri()
				send("textDocument/didOpen", {"textDocument": {
					"uri": uri, "languageId": "veroscript", "version": 1,
					"text": source,
				}}, notification=True)
				if isolated_review_documents is not None:
					isolated_review_documents.append(uri)
				return uri

			# Standard event helpers use the core's source spelling at every
			# arity; a callback type is one signature-help parameter even when
			# its own parameter list contains commas.
			for index, (type_name, arguments) in enumerate((
				("delegate<string>", []),
				("delegate<string,int>", ["int"]),
				("delegate<string,int,bool>", ["int", "bool"]),
				("delegate<string,int,bool,double>", ["int", "bool", "double"]),
				("std::delegate<string,int>", ["int"]),
				("multicast_delegate", []),
				("multicast_delegate<int>", ["int"]),
				("multicast_delegate<int,bool>", ["int", "bool"]),
				("std::multicast_delegate<int,bool,double>", ["int", "bool", "double"]),
			)):
				multicast = "multicast" in type_name
				label = "add" if multicast else "bind"
				expected = {"add", "remove", "broadcast", "count", "clear"} if multicast else {
					"bind", "unbind", "execute", "execute_if_bound", "is_bound"}
				callback = "fn(" + ", ".join(arguments) + ")->" + ("void" if multicast else "string") + " h"
				prefix = "void main(){" + type_name + " d=" + type_name + "();"
				for method, marked in (
					("textDocument/completion", "d.|;}"),
					("textDocument/hover", "d.co|unt();}" if multicast else "d.ex|ecute();}"),
					("textDocument/signatureHelp", "d." + label + "(|);}"),
				):
					source, position = marked_source(prefix + marked)
					name = "delegate_" + str(index) + "_" + method.rsplit("/", 1)[-1]
					uri = open_document(name, source)
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send(method, params)
					if method.endswith("completion"):
						candidates = items(response)
						assert {item["label"] for item in candidates} == expected, response
						item = item_with_label(response, label)
						assert item["detail"] == "void (" + callback + ")", item
						assert all(item["insertText"] == item["label"] for item in candidates), response
					elif method.endswith("hover"):
						contents = response["result"]["contents"]["value"]
						assert ("int64 ()" if multicast else "string (") in contents, response
						assert contents.count("soft builtin") + contents.count("*Built-in symbol.*") == 1, response
					else:
						help_result = response["result"]
						assert len(help_result["signatures"]) == 1, response
						assert help_result["activeParameter"] == 0 and help_result["activeSignature"] == 0, response
						assert help_result["signatures"][0]["parameters"] == [{"label": callback}], response
					records.append({"name": name, "document": source, "position": position,
									"method": method, "request": params, "response": response})

			# Actual declarations own their members. A soft event projection
			# must not leak into a user type, another namespace, or another
			# arity of an otherwise matching name.
			for name, declaration, type_name, expected in (
				("user_delegate", "class delegate<R>{public:R value;};", "delegate<int>", {"value"}),
				("user_multicast", "class multicast_delegate<A>{public:A value;};", "multicast_delegate<int>", {"value"}),
				("namespace_delegate", "namespace custom{class delegate<R>{public:R value;};}",
				 "custom::delegate<int>", {"value"}),
				("namespace_multicast", "namespace custom{class multicast_delegate<A>{public:A value;};}",
				 "custom::multicast_delegate<int>", {"value"}),
				("default_delegate", "class delegate<R,S=int>{public:R value;S extra;};",
				 "delegate<int>", {"value", "extra"}),
				("interface_delegate", "interface delegate<R>{R value;}", "delegate<int>", {"value"}),
				("other_delegate_arity", "class delegate{public:int value;};", "delegate<int>",
				 {"bind", "unbind", "execute", "execute_if_bound", "is_bound"}),
			):
				prefix = declaration + "void use(" + type_name + " d){"
				source, position = marked_source(prefix + "d.|;}")
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				labels = {item["label"] for item in items(response)}
				assert expected <= labels and labels - expected <= {"use"}, response
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params, "response": response})
				if name == "other_delegate_arity":
					continue
				multicast = "multicast" in type_name
				for method, marked in (
					("textDocument/hover", "d.ad|d();}" if multicast else "d.bi|nd();}"),
					("textDocument/signatureHelp", "d.add(|);}" if multicast else "d.bind(|);}"),
				):
					source, position = marked_source(prefix + marked)
					uri = open_document(name + "_" + method.rsplit("/", 1)[-1], source)
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send(method, params)
					assert response["result"] is None, response
					records.append({"name": name + "_" + method.rsplit("/", 1)[-1],
									"document": source, "position": position, "method": method,
									"request": params, "response": response})

			source, position = marked_source(
				"namespace custom{class delegate<R>{public:R value;};void use(delegate<int> d){d.|;}}")
			uri = open_document("namespace_delegate_unqualified", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/completion", params)
			assert {item["label"] for item in items(response)} == {"value", "use"}, response
			records.append({"name": "namespace_delegate_unqualified", "document": source,
							"position": position, "method": "textDocument/completion",
							"request": params, "response": response})

			# User methods with standard-looking names still use their actual
			# signatures and return types, including chained completion.
			prefix = ("class delegate<R>{public:R value;bool bind(string name){return true;}"
					  "array<string> execute(){return array<string>();}};void use(delegate<int> d){")
			for name, method, marked in (
				("declared_delegate_method_completion", "textDocument/completion", "d.bi|;}"),
				("declared_delegate_method_hover", "textDocument/hover", 'd.bi|nd("x");}'),
				("declared_delegate_method_signature", "textDocument/signatureHelp", "d.bind(|);}"),
				("declared_delegate_return_completion", "textDocument/completion", "d.execute().|;}"),
			):
				source, position = marked_source(prefix + marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send(method, params)
				if name.endswith("return_completion"):
					assert item_with_label(response, "push")["detail"] == "array<string> (string value)", response
				elif method.endswith("completion"):
					item = item_with_label(response, "bind")
					assert item["detail"] == "bool(string name)", item
					assert item["textEdit"]["range"] == {
						"start": {"line": 0, "character": position["character"] - 2}, "end": position}, item
				elif method.endswith("hover"):
					assert "bool(string name)" in response["result"]["contents"]["value"], response
				else:
					signatures = response["result"]["signatures"]
					assert len(signatures) == 1 and signatures[0]["parameters"] == [{"label": "string name"}], response
					assert "bool" in signatures[0]["label"], response
				records.append({"name": name, "document": source, "position": position,
								"method": method, "request": params, "response": response})

			for method in ("textDocument/hover", "textDocument/definition", "textDocument/references"):
				source, position = marked_source(
					"class Box{public:int value;};void bind(){}void use(Box box){box.bi|nd();}")
				name = "missing_script_member_" + method.rsplit("/", 1)[-1]
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				if method.endswith("references"):
					params["context"] = {"includeDeclaration": True}
				response = send(method, params)
				assert response["result"] == (None if method.endswith("hover") else []), response
				records.append({"name": name, "document": source, "position": position,
								"method": method, "request": params, "response": response})

			# Failed receiver lookups retain their scope for every query, even
			# when a bare local/global has the same leaf spelling.
			for name, marked in (
				("unresolved_receiver", "int bind(){return 1;}void use(){missing.bi|nd();}"),
				("unresolved_receiver_local", "int bind(){return 1;}void use(){int bind=0;missing.bi|nd();}"),
				("missing_namespace_member", "int bind(){return 1;}namespace ns{}void use(){ns::bi|nd();}"),
				("missing_namespace_member_local", "int bind(){return 1;}namespace ns{}void use(){int bind=0;ns::bi|nd();}"),
				("unresolved_call_receiver", "int bind(){return 1;}void use(){missing().bi|nd();}"),
				("unresolved_call_namespace", "void use(){missing().st|d;}"),
				("unresolved_receiver_error_before", "int broken=;int bind(){return 1;}void use(){missing.bi|nd();}"),
				("unresolved_receiver_error_after", "int bind(){return 1;}void use(){missing.bi|nd();}int broken=;"),
				("unresolved_receiver_incomplete", "int bind(){return 1;}void use(){missing.bi|nd("),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				for method in ("completion", "hover", "definition", "references"):
					params = {"textDocument": {"uri": uri}, "position": position}
					if method == "references":
						params["context"] = {"includeDeclaration": True}
					response = send("textDocument/" + method, params)
					if method == "completion":
						assert items(response) == [], response
					else:
						assert response["result"] == (None if method == "hover" else []), response
					records.append({"name": name + "_" + method, "document": source,
									"position": position, "method": "textDocument/" + method,
									"request": params, "response": response})
				send("textDocument/didClose", {"textDocument": {"uri": uri}}, notification=True)
				isolated_review_documents.remove(uri)

			# Namespace members still resolve by their complete identity. A
			# bare value at the call site cannot shadow a qualified function.
			for name, marked, expected_detail, target in (
				("qualified_function", "namespace ns{int bind(){return 1;}}void use(){ns::bi|nd();}",
				 "int()", "int bind()"),
				("qualified_function_with_local", "namespace ns{int bind(){return 1;}}void use(){int bind=0;ns::bi|nd();}",
				 "int()", "int bind()"),
				("qualified_function_with_global", "string bind(){return \"\";}namespace ns{int bind(){return 1;}}void use(){ns::bi|nd();}",
				 "int()", "int bind()"),
				("standard_qualified_function", "void use(){std::pr|int(1);}",
				 "void std::print(any value);", None),
				("standard_nested_function", "void use(){std::chrono::no|w();}",
				 "double std::chrono::now();", None),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				for method in ("completion", "hover", "definition"):
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send("textDocument/" + method, params)
					if method == "completion":
						label = "print" if "print" in source else "now" if "chrono" in source else "bind"
						assert expected_detail in item_with_label(response, label)["detail"], response
					elif method == "hover":
						assert expected_detail in response["result"]["contents"]["value"], response
						if target:
							assert "string" not in response["result"]["contents"]["value"], response
					elif target:
						locations = response["result"]
						assert len(locations) == 1 and locations[0]["uri"] == uri, response
						definition_position = locations[0]["range"]["start"]
						assert definition_position["line"] == 0 and definition_position["character"] == source.index(target), response
					else:
						assert response["result"] == [], response
					records.append({"name": name + "_" + method, "document": source,
									"position": position, "method": "textDocument/" + method,
									"request": params, "response": response})
				send("textDocument/didClose", {"textDocument": {"uri": uri}}, notification=True)
				isolated_review_documents.remove(uri)

			factory_alias = (
				"struct Factory<T>{static T create(){return T();}"
				"static U identity<U>(U input){return input;}T instance(){return T();}};"
				"using First=Factory<Inner>;using Maker=First;"
			)
			for name, marked in (
				("unknown_generic_extension", "void bind<T>(T value){}void use(){missing.bi|nd();}"),
				("void_result_extension", "void bind<T>(T value){}void use(){print(1).bi|nd();}"),
				("unknown_generic_extension_before", "int broken=;void bind<T>(T value){}void use(){missing.bi|nd();}"),
				("unknown_generic_extension_after", "void bind<T>(T value){}void use(){missing.bi|nd();}int broken=;"),
				("unknown_generic_extension_incomplete", "void bind<T>(T value){}void use(){missing.bi|nd("),
				("namespace_generic_extension", "void bind<T>(T value){}namespace ns{}void use(){ns::bi|nd();}"),
				("call_tail_namespace", "namespace ns{int bind(){return 1;}}void use(){missing().ns.bi|nd();}"),
				("index_tail_namespace", "namespace ns{int bind(){return 1;}}void use(){missing[0].ns.bi|nd();}"),
				("call_tail_standard_namespace", "void use(){missing().std::chrono::no|w();}"),
				("private_chain_field", "class Inner{public:int value;};class Outer{private:Inner child;};void use(Outer x){x.child.va|lue;}"),
				("shadowed_chain_field", "class Inner{public:int value;};class Base{public:Inner child;};class Outer:public Base{public:string child;};void use(Outer x){x.child.va|lue;}"),
				("unrelated_global_receiver", "class Inner{public:int value;};namespace N{array<Inner> shared={};}void use(){shared[0].va|lue;}"),
				("unresolved_qualified_global_receiver", "class Inner{public:int value;};array<Inner> items={};namespace M{array<Inner> items={};}void use(){N::items[0].va|lue;}"),
				("nearest_namespace_missing_value", "class Inner{public:const int value;};namespace N{array<Inner> shared={};}namespace A{namespace N{}void use(){N::shared[0].va|lue;}}"),
				("nearest_namespace_other_value", "class Inner{public:const int value;};class Other{};namespace N{array<Inner> shared={};}namespace A{namespace N{array<Other> shared={};}void use(){N::shared[0].va|lue;}}"),
				("nearest_namespace_missing_child", "class Inner{public:const int value;};namespace N{namespace M{array<Inner> shared={};}}namespace A{namespace N{}void use(){N::M::shared[0].va|lue;}}"),
				("unrecoverable_qualified_global_before", "int broken=;class Inner{public:int value;};namespace N{array<Inner> items={};}void use(){N::items[0].va|lue;}"),
				("ambiguous_global_receiver", "class Inner{public:int value;};namespace N{array<Inner> items={};}namespace M{array<Inner> items={};}using namespace N;using namespace M;void use(){items[0].va|lue;}"),
				("local_shadows_global_receiver", "class Inner{public:int value;};class Other{};array<Inner> shared={};void use(array<Other> shared){shared[0].va|lue;}"),
				("private_field_shadows_global_receiver", "class Inner{public:int value;};class Other{};array<Inner> shared={};class Base{private:array<Other> shared;};class Derived:public Base{void use(){shared[0].va|lue;}};"),
				("alias_static_reject_instance", "struct Inner{int value;};" + factory_alias + "void use(){Maker::instance().va|lue;}"),
				("alias_static_reject_private", "struct Inner{int value;};class Factory<T>{static T create(){return T();}};using Maker=Factory<Inner>;void use(){Maker::create().va|lue;}"),
				("alias_static_reject_arity", "struct Inner{int value;};" + factory_alias + "using Bad=Factory<Inner,int>;void use(){Bad::create().va|lue;}"),
				("alias_static_value_shadow", "struct Inner{int value;};" + factory_alias + "void use(int Maker){Maker.create().va|lue;}"),
				("nested_type_value_shadow", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};void use(){int Outer=0;Outer::Box<>().child.va|lue;}"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				for method in ("completion", "hover", "definition", "references"):
					params = {"textDocument": {"uri": uri}, "position": position}
					if method == "references":
						params["context"] = {"includeDeclaration": True}
					response = send("textDocument/" + method, params)
					assert (items(response) == [] if method == "completion" else
							response["result"] == (None if method == "hover" else [])), response
					records.append({"name": name + "_" + method, "document": source,
									"position": position, "method": "textDocument/" + method,
									"request": params, "response": response})

			inner = "class Inner{public:const int value;int get(){return value;}};"
			for name, marked in (
				("alias_static_auto_chain", inner + factory_alias + "void use(){auto result=Maker::create();result.va|lue;}"),
				("alias_static_template_chain", inner + factory_alias + "void use(){Maker::identity<Inner>(Inner()).va|lue;}"),
				("alias_static_template_inferred", inner + factory_alias + "void use(){Maker::identity(Inner()).va|lue;}"),
				("alias_static_template_auto", inner + factory_alias + "void use(){auto result=Maker::identity<Inner>(Inner());result.va|lue;}"),
				("local_alias_static_auto", inner + factory_alias + "void use(){using Local=Factory<Inner>;auto result=Local::create();result.va|lue;}"),
				("inherited_alias_static_template", inner + "struct Base<T>{static U identity<U>(U input){return input;}};struct Derived<T>:public Base<T>{};using Maker=Derived<Inner>;void use(){Maker::identity<Inner>(Inner()).va|lue;}"),
				("factory_chain", inner + "class Outer{public:Inner child;};Outer make(){return Outer();}void use(){make().child.va|lue;}"),
				("local_chain", inner + "class Outer{public:Inner child;};void use(Outer x){x.child.va|lue;}"),
				("index_chain", inner + "class Outer{public:Inner child;};void use(array<Outer> x){x[0].child.va|lue;}"),
				("soft_named_index_shared", inner + "void use(array<Inner> shared){shared[0].va|lue;}"),
				("soft_named_index_weak", inner + "void use(array<Inner> weak){weak[0].va|lue;}"),
				("soft_named_index_readonly", inner + "void use(array<Inner> readonly){readonly[0].va|lue;}"),
				("soft_named_global_shared", inner + "array<Inner> shared={};void use(){shared[0].va|lue;}"),
				("soft_named_global_weak", inner + "array<Inner> weak={};void use(){weak[0].va|lue;}"),
				("soft_named_global_readonly", inner + "array<Inner> readonly={};void use(){readonly[0].va|lue;}"),
				("soft_named_global_module_info", inner + "array<Inner> module_info={};void use(){module_info[0].va|lue;}"),
				("global_array_receiver", inner + "array<Inner> items={};void use(){items[0].va|lue;}"),
				("namespace_global_receiver", inner + "namespace N{array<Inner> shared={};void use(){shared[0].va|lue;}}"),
				("qualified_namespace_global_receiver", inner + "namespace N{array<Inner> shared={};}void use(){N::shared[0].va|lue;}"),
				("relative_namespace_receiver", inner + "namespace A{namespace N{array<Inner> items={};}void use(){N::items[0].va|lue;}}"),
				("qualified_namespace_global_object", inner + "namespace N{Inner item=Inner();}void use(){N::item.va|lue;}"),
				("qualified_namespace_global_anchor", inner + "namespace N{array<Inner> shared={};}void use(){::N::shared[0].va|lue;}"),
				("anchored_namespace_global", inner + "namespace N{array<Inner> shared={};}namespace A{namespace N{}void use(){::N::shared[0].va|lue;}}"),
				("anchored_namespace_global_homonym", inner + "class Other{};namespace N{array<Inner> shared={};}namespace A{namespace N{array<Other> shared={};}void use(){::N::shared[0].va|lue;}}"),
				("anchored_nested_namespace_global", inner + "namespace N{namespace M{array<Inner> shared={};}}namespace A{namespace N{}void use(){::N::M::shared[0].va|lue;}}"),
				("qualified_namespace_global_before_error", "void broken(){missing();}" + inner + "namespace N{array<Inner> shared={};}void use(){N::shared[0].va|lue;}"),
				("qualified_namespace_global_after_error", inner + "namespace N{array<Inner> shared={};}void use(){N::shared[0].va|lue;}void broken(){missing();}"),
				("qualified_namespace_global_after_syntax_error", inner + "namespace N{array<Inner> shared={};}void use(){N::shared[0].va|lue;}int broken=;"),
				("using_global_receiver", inner + "namespace N{array<Inner> shared={};}using namespace N;void use(){shared[0].va|lue;}"),
				("direct_global_receiver", inner + "Inner item=Inner();void use(){item.va|lue;}"),
				("nested_global_array_receiver", inner + "array<array<Inner>> items={};void use(){items[0][0].va|lue;}"),
				("soft_named_native_array_alias", inner + "void use(){using pair=Inner;auto values=pair[2]{Inner(),Inner()};values[0].va|lue;}"),
				("this_chain", inner + "class Outer{public:Inner child;int get(){return this.child.va|lue;}};"),
				("generic_chain", inner + "class Outer<T>{public:T child;};void use(Outer<Inner> x){x.child.va|lue;}"),
				("inherited_chain", inner + "class Base{public:Inner child;};class Outer:public Base{};void use(Outer x){x.child.va|lue;}"),
				("soft_generic_chain", inner + "void use(handle<Inner> x){x.value.va|lue;}"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				for method in ("completion", "hover", "definition", "references"):
					params = {"textDocument": {"uri": uri}, "position": position}
					if method == "references":
						params["context"] = {"includeDeclaration": True}
					response = send("textDocument/" + method, params)
					if method == "completion":
						item = item_with_label(response, "value")
						assert item["detail"] == "const int", item
						assert item["textEdit"]["range"] == {
							"start": {"line": 0, "character": position["character"] - 2},
							"end": {"line": 0, "character": position["character"] + 3}}, item
					elif method == "hover":
						assert "const int" in response["result"]["contents"]["value"], response
						assert "Inner.value" in response["result"]["contents"]["value"], response
					elif method == "definition":
						locations = response["result"]
						assert len(locations) == 1 and locations[0]["uri"] == uri, response
						assert locations[0]["range"]["start"] == {
							"line": 0, "character": source.index("const int value")}, response
					else:
						assert any(location["range"]["start"]["character"] == position["character"] - 2
								   for location in response["result"]), response
					records.append({"name": name + "_" + method, "document": source,
									"position": position, "method": "textDocument/" + method,
									"request": params, "response": response})

			for name, marked in (
				("empty_default_direct", "struct Box<T=int>{T value=7;};void use(Box<> b){b.va|lue;}"),
				("empty_default_constructor", "struct Box<T=int>{T value=7;};void use(){Box<>().va|lue;}"),
				("explicit_type_constructor", "struct Box<T>{T value=7;};void use(){Box<int>().va|lue;}"),
				("empty_default_auto", "struct Box<T=int>{T value=7;};void use(){auto b=Box<>();b.va|lue;}"),
				("empty_default_inherited", "struct Box<T=int>{T value=7;};struct Child:public Box<>{};void use(Child b){b.va|lue;}"),
				("empty_default_alias", "struct Box<T=int,U=T>{U value=7;};using Pick=Box<>;void use(Pick b){b.va|lue;}"),
				("empty_default_chain", "struct Inner{int value=7;};struct Box<T=Inner>{T child=T();};void use(Box<> b){b.child.va|lue;}"),
				("empty_default_temporary_chain", "struct Inner{int value=7;};struct Box<T=Inner>{T child=T();};void use(){Box<>().child.va|lue;}"),
				("nested_default_temporary", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};void use(){Outer<>::Box<>().child.va|lue;}"),
				("nested_default_local", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};void use(){Outer<>::Box<> box=Outer<>::Box<>();box.child.va|lue;}"),
				("nested_default_alias", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};void use(){using Pick=Outer<>::Box<>;auto box=Pick();box.child.va|lue;}"),
				("nested_default_global", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};void use(){::Outer<>::Box<>().child.va|lue;}"),
				("nested_default_nongeneric", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{struct Box{T child;};};void use(){Outer<>::Box().child.va|lue;}"),
				("nested_default_deep", "struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Middle{template<typename V=U> struct Box{V child;};};};void use(){Outer<>::Middle<>::Box<>().child.va|lue;}"),
				("nested_default_value", "struct Inner{int value=7;};template<typename T=Inner,int N=2> struct Outer{template<typename U=T,int M=(N+1)> struct Box{U children[M];};};void use(){Outer<>::Box<>().children[0].va|lue;}"),
				("native_array_field_chain", "struct Inner{int value=7;};struct Holder{Inner data[2];};void use(Holder holder){holder.data[0].va|lue;}"),
				("nested_default_namespace", "namespace N{struct Inner{int value=7;};template<typename T=Inner> struct Outer{template<typename U=T> struct Box{U child;};};}struct Inner{string wrong;};void use(){N::Outer<>::Box<>().child.va|lue;}"),
				("declaration_default_type_shadow", "export struct T{int value=7;};template<typename U=T> struct Box{U child;};template<typename T> int read(T unused){return Box<>().child.va|lue;}void main(){read(0);}"),
				("declaration_default_function_shadow", "export struct T{int value=7;};template<typename U=T> U make(){return U();}template<typename T> int read(T unused){return make<>().va|lue;}void main(){read(0);}"),
				("qualified_default_temporary_chain", "namespace N{struct Inner{int value=7;};struct Box<T=Inner>{T child=T();};}void use(){N::Box<>().child.va|lue;}"),
				("empty_default_child_auto", "struct Inner{int value=7;};struct Box<T=Inner>{T child=T();};void use(){auto child=Box<>().child;child.va|lue;}"),
				("empty_default_temporary_inherited", "struct Inner{int value=7;};class Base<T>{public:T child;};class Box<T=Inner>:public Base<T>{};void use(){Box<>().child.va|lue;}"),
				("exact_arity_before_default", "struct Inner{int value=7;};struct Box<T>{T child=T();};struct Box<T,U=int>{U child;};void use(){Box<Inner>().child.va|lue;}"),
				("exact_arity_after_default", "struct Inner{int value=7;};struct Box<T,U=int>{U child;};struct Box<T>{T child=T();};void use(){Box<Inner>().child.va|lue;}"),
				("empty_default_homonym", "struct Box{int value=7;};struct Box<T=int>{string other;};void use(Box<> b){b.va|lue;}"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				for method in ("completion", "hover", "definition"):
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send("textDocument/" + method, params)
					if method == "completion":
						item = item_with_label(response, "value")
						assert item["detail"] == "int", item
						assert apply_edit(source, item["textEdit"]) == source, item
					elif method == "hover":
						assert "```veroscript\nint\n```" in response["result"]["contents"]["value"], response
					else:
						assert len(response["result"]) == 1 and response["result"][0]["uri"] == uri, response
					records.append({"name": name + "_" + method, "document": source,
									"position": position, "method": "textDocument/" + method,
									"request": params, "response": response})

			for name, declaration, suffix in (
				("empty_default_override", "class Base<T=int>{public:virtual T read(T value){return value;}};", " override"),
				("empty_default_interface", "interface Base<T=int>{virtual T read(T value)=0;};", ""),
			):
				source, position = marked_source(declaration + "class Child:public Base<>{public:re|}")
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				item = item_with_label(response, "read")
				assert item["detail"] == "int read(int value)", item
				assert item["textEdit"]["newText"].startswith("int read(int value)" + suffix + " {"), item
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params, "response": response})

			source, position = marked_source("class Box<T=int>{public:T read(T value){return value;}};void use(Box<> b){b.read(|);}")
			uri = open_document("empty_default_signature", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/signatureHelp", params)
			assert len(response["result"]["signatures"]) == 1, response
			assert response["result"]["signatures"][0]["label"] == "Box.read int(int value)", response
			assert response["result"]["signatures"][0]["parameters"] == [{"label": "int value"}], response
			records.append({"name": "empty_default_signature", "document": source, "position": position,
							"method": "textDocument/signatureHelp", "request": params, "response": response})

			for name, marked, expected in (
				("unknown_generic_signature", "void bind<T>(T value){}void use(){missing.bind(|);}", None),
				("void_result_signature", "void bind<T>(T value){}void use(){print(1).bind(|);}", None),
				("tail_namespace_signature", "namespace ns{int bind(){return 1;}}void use(){missing().ns.bind(|);}", None),
				("valid_generic_extension_signature", "void bind<T>(T value){}void use(int x){x.bind(|);}", "bind"),
				("factory_chain_signature", inner + "class Outer{public:Inner child;};Outer make(){return Outer();}void use(){make().child.get(|);}", "get"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				if expected is None:
					assert response["result"] is None, response
				else:
					assert len(response["result"]["signatures"]) == 1, response
					assert expected in response["result"]["signatures"][0]["label"], response
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params, "response": response})

			signature_prefix = (
				"int Make<T,U>(){return 1;}"
				"void Three(int first,int second,int third){}"
				"void Use(string first,string second){}"
				"namespace N{void Use(int first,int second,int third){}}"
				"void Two(int first,int second){}"
			)
			for name, marked, expected_name, parameter_count in (
				("signature_generic_argument_comma",
				 signature_prefix + "void main(){Three(Make<int,string>(),42|,99);}", "Three", 3),
				("signature_namespace_isolated",
				 signature_prefix + "void main(){N::Use(Make<int,string>(),42|,99);}", "N::Use", 3),
				("signature_recovery_existing_semicolon",
				 signature_prefix + "void main(){Two(1, |/* keep */;}", "Two", 2),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				help_result = response["result"]
				assert help_result["activeParameter"] == 1 and help_result["activeSignature"] == 0, (name, response)
				assert len(help_result["signatures"]) == 1, (name, response)
				signature = help_result["signatures"][0]
				assert signature["label"].split("(", 1)[0].split()[0] == expected_name, (name, signature)
				assert len(signature["parameters"]) == parameter_count, (name, signature)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params, "response": response})

			source = ("void Two(int first,int second){}"
					  "class Indexed{public:int operator[](int index){return index;}};"
					  "void main(){Indexed values=Indexed();Two(1,2);values[1];}")
			uri = open_document("signature_closed_delimiters", source)
			for name, expression in (("call", "Two(1,2)"), ("index", "values[1]")):
				position = {"line": 0, "character": source.index(expression) + len(expression)}
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				assert response["result"] is None, (name, response)
				records.append({"name": "signature_closed_" + name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params, "response": response})

			for name, marked in (
				("standard_namespace_with_fuzzy_user_match", "namespace custom{}using namespace st|;"),
				("standard_namespace_with_empty_prefix", "namespace custom{}using namespace |;"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				item = item_with_label(response, "std")
				assert item["insertText"] == "std", item
				applied = apply_edit(source, item.get("textEdit", {
					"range": {"start": position, "end": position}, "newText": item["insertText"]}))
				assert "using namespace std;" in applied, (name, applied)
				if name.endswith("fuzzy_user_match"):
					labels = [candidate["label"] for candidate in items(response)]
					assert labels.index("std") < labels.index("custom"), response
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params,
								"response": response, "applied": applied})

			for spelling in ("Util.Pack", "Util::Pack"):
				prefix = ("namespace Util{class Pack{public:int x;Pack(int v){x=v;}int get(){return x;}}}"
						  "using Pack=" + spelling + ";void main(){Pack p=Pack(1);")
				for method, marked in (
					("textDocument/completion", "p.ge|t();}"),
					("textDocument/hover", "p.ge|t();}"),
					("textDocument/definition", "p.ge|t();}"),
					("textDocument/signatureHelp", "p.get(|);}"),
				):
					source, position = marked_source(prefix + marked)
					name = "alias_member_" + ("scope_" if "::" in spelling else "dot_") + method.rsplit("/", 1)[-1]
					uri = open_document(name, source)
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send(method, params)
					if method.endswith("completion"):
						assert item_with_label(response, "get")["detail"] == "int()", response
					elif method.endswith("hover"):
						assert "int()" in response["result"]["contents"]["value"], response
					elif method.endswith("definition"):
						locations = response["result"]
						assert len(locations) == 1 and locations[0]["uri"] == uri, response
						target = locations[0]["range"]["start"]
						assert target["line"] == 0 and source.index("int get()") <= target["character"] <= source.index("get()"), response
					else:
						signatures = response["result"]["signatures"]
						assert len(signatures) == 1 and signatures[0]["parameters"] == [], response
						assert "int" in signatures[0]["label"], response
					records.append({"name": name, "document": source, "position": position,
									"method": method, "request": params, "response": response})

			# These independent response checks have completed. Keep later
			# workspace/configuration tests limited to their own live fixtures.
			for review_uri in isolated_review_documents:
				send("textDocument/didClose", {"textDocument": {"uri": review_uri}}, notification=True)
			isolated_review_documents = None

			open_document("conversion_private_base",
						  "using Number=int;export class B{public:virtual Number operator Number(){return 1;}};")
			for name, base, implemented, count in (
				("conversion_override_alias",
				 "using Number=int;class B{public:virtual Number operator Number(){return 1;}};", "", 1),
				("conversion_override_generic",
				 "class B<T>{public:virtual T operator T(){return 1;}};", "", 1),
				("conversion_override_implemented_alias",
				 "using Number=int;class B{public:virtual Number operator Number(){return 1;}};",
				 "int operator int() override{return 2;}", 0),
				("conversion_override_const_overloads",
				 "class B{public:virtual int operator int(){return 1;}virtual int operator int() const{return 2;}};", "", 2),
				("conversion_override_final",
				 "class B{public:virtual int operator int() final{return 1;}};", "", 0),
				("conversion_override_private_aliases",
				 'import "conversion_private_base.vs";using Number=double;',
				 "Number operator Number(){return 2.5;}", 1),
				("conversion_override_fixed_width_alias",
				 "class B{public:virtual long operator long(){return 1;}};",
				 "int64 operator int64(){return 2;}", 0),
				("conversion_override_long",
				 "class B{public:virtual long operator long(){return 1;}};", "", 1),
			):
				parent = "B<int>" if name.endswith("generic") else "B"
				source = base + f"\nclass D:public {parent}{{public:{implemented}\n  \n}};void main(){{}}"
				uri = open_document(name, source)
				position = {"line": 2, "character": 2}
				params = {"textDocument": {"uri": uri}, "range": {"start": position, "end": position},
						  "context": {"diagnostics": [], "only": ["refactor.rewrite"]}}
				response = send("textDocument/codeAction", params)
				assert len(response["result"]) == count, response
				for action in response["result"]:
					assert action["kind"] == "refactor.rewrite" and set(action["edit"]["changes"]) == {uri}, action
					edits = action["edit"]["changes"][uri]
					assert len(edits) == 1 and edits[0]["range"] == params["range"], action
					text = edits[0]["newText"]
					assert " operator " in text and "operator conv " not in text and "operator T" not in text, action
					assert "override" in text and "${" not in text, action
				if count == 2:
					texts = [action["edit"]["changes"][uri][0]["newText"] for action in response["result"]]
					assert sum(") const override" in text for text in texts) == 1, texts
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/codeAction", "request": params, "response": response})
				if count == 1:
					incomplete = source.replace("\n  \n", "\n  ope\n")
					completion_uri = open_document(name + "_completion", incomplete)
					completion_position = {"line": 2, "character": 5}
					completion_params = {"textDocument": {"uri": completion_uri}, "position": completion_position}
					completion = send("textDocument/completion", completion_params)
					candidates = [item for item in items(completion) if item["label"].startswith("operator ")]
					assert len(candidates) == 1, completion
					item = candidates[0]
					assert item["insertTextFormat"] == 2 and item["textEdit"]["newText"] == item["insertText"], item
					assert item["textEdit"]["range"] == {"start": {"line": 2, "character": 2}, "end": completion_position}, item
					return_type = "long" if name.endswith("_long") else "int"
					assert item["label"] == "operator " + return_type, item
					assert item["insertText"].startswith(return_type + " " + item["label"] + "() override"), item
					assert "operator conv " not in item["detail"] and "operator T" not in item["insertText"], item
					records.append({"name": name + "_completion", "document": incomplete, "position": completion_position,
									"method": "textDocument/completion", "request": completion_params, "response": completion})

			override_libraries = {
				"override_generic_scope_base": "using Number=int;using D=int;export class B<T>{public:virtual T operator T(){return 1;}};",
				"override_ordinary_scope_base": "using Number=int;export class B{public:virtual Number Read(Number value){return value;}};",
			}
			for library_name, library_source in override_libraries.items():
				open_document(library_name, library_source)
			for name, base, parent, implemented, signature in (
				("override_mutable_reference", "class B{public:int value=1;virtual int& Read(){return value;}};",
				 "B", "", "int& Read()"),
				("override_const_reference", "class B{public:int value=1;virtual const int& Read() const{return value;}};",
				 "B", "", "const int& Read() const"),
				("override_root_alias", "using Number=int;class B{public:virtual ::Number operator ::Number(){return 1;}};",
				 "B", "", "int operator int()"),
				("override_root_alias_implemented", "using Number=int;class B{public:virtual ::Number operator ::Number(){return 1;}};",
				 "B", "int operator int(){return 2;}", None),
				("override_root_alias_shadowed", "using Number=int;namespace N{using Number=double;class B{public:virtual ::Number operator ::Number(){return 1;}};}",
				 "N::B", "", "int operator int()"),
				("override_generic_argument_scope", 'import "override_generic_scope_base.vs";using Number=double;',
				 "B<Number>", "", "double operator double()"),
				("override_generic_argument_implemented", 'import "override_generic_scope_base.vs";using Number=double;',
				 "B<Number>", "double operator double(){return 2.5;}", None),
				("override_generic_chain_scope", 'import "override_generic_scope_base.vs";using Number=double;class M<U>:public B<U>{};',
				 "M<Number>", "", "double operator double()"),
				("override_private_ordinary_alias", 'import "override_ordinary_scope_base.vs";using Number=double;',
				 "B", "", "int Read(int value)"),
				("override_ordinary_alias_implemented", "using Number=int;class B{public:virtual int Read(Number value){return value;}};",
				 "B", "int Read(int value){return value;}", None),
				("override_generic_reference", "class B<T>{public:T value;virtual const T& Read() const{return value;}};",
				 "B<int>", "", "const int& Read() const"),
				("override_generic_callable", "class B<T>{public:virtual T Apply(fn(T)->T cb,T value){return cb(value);}};",
				 "B<int>", "", "int Apply(fn(int)->int cb, int value)"),
				("override_callable_reference_return", "class B<T>{public:T value;virtual fn()->const T& Factory() const{throw 0;}};",
				 "B<int>", "", "fn()->const int& Factory() const"),
				("override_generic_fixed_array", "class B<T>{public:virtual int Size(ref T values[2][3]){return 1;}};",
				 "B<int>", "", "int Size(ref int[2][3] values)"),
				("override_equivalent_user_generic", "class Box<T>{public:T value;};class B{public:virtual Box<long> operator Box<long>(){return Box<long>();}};",
				 "B", "Box<int64> operator Box<int64>(){return Box<int64>();}", None),
				("override_distinct_map_key", "class B{public:virtual map<int,int> operator map<int,int>(){return map<int,int>();}};",
				 "B", "map<int64,int> operator map<int64,int>(){return map<int64,int>();}", "map<int,int> operator map<int,int>()"),
				("override_equivalent_value_container", "class B<T>{public:virtual T operator T(){return T();}};using Values=array<long>;",
				 "B<Values>", "array<int64> operator array<int64>(){return array<int64>();}", None),
			):
				source = base + f"\nclass D:public {parent}{{public:{implemented}\n  \n}};void main(){{}}"
				uri = open_document(name, source)
				position = {"line": 2, "character": 2}
				params = {"textDocument": {"uri": uri}, "range": {"start": position, "end": position},
						  "context": {"diagnostics": [], "only": ["refactor.rewrite"]}}
				response = send("textDocument/codeAction", params)
				assert len(response["result"]) == (signature is not None), (name, response)
				assert not diagnostics_by_uri[uri]["diagnostics"], (name, diagnostics_by_uri[uri])
				record = {"name": name, "document": source, "position": position,
						  "method": "textDocument/codeAction", "request": params, "response": response,
						  "supportingDocuments": override_libraries}
				if signature is not None:
					edits = response["result"][0]["edit"]["changes"][uri]
					assert len(edits) == 1 and edits[0]["range"] == params["range"], (name, edits)
					assert edits[0]["newText"].startswith(signature + " override {"), (name, edits)
					applied = apply_edit(source, edits[0])
					send("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
						 "contentChanges": [{"text": applied}]}, notification=True)
					send("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
					assert not diagnostics_by_uri[uri]["diagnostics"], (name, applied, diagnostics_by_uri[uri])
					repeated = send("textDocument/codeAction", params)
					assert repeated["result"] == [], (name, repeated)
					record.update({"applied": applied, "appliedDiagnostics": diagnostics_by_uri[uri],
								   "repeatedResponse": repeated})
					prefix = "ope" if "operator " in signature else next(
						method[:2] for method in ("Read", "Apply", "Factory", "Size") if method + "(" in signature)
					incomplete = source.replace("\n  \n", "\n  " + prefix + "\n")
					completion_uri = open_document(name + "_completion", incomplete)
					completion_position = {"line": 2, "character": 2 + len(prefix)}
					completion_params = {"textDocument": {"uri": completion_uri}, "position": completion_position}
					completion = send("textDocument/completion", completion_params)
					matching = [item for item in items(completion) if item["insertText"].startswith(signature + " override {")]
					assert len(matching) == 1, (name, completion)
					item = matching[0]
					assert item["insertTextFormat"] == 2 and item["textEdit"]["newText"] == item["insertText"], item
					assert item["textEdit"]["range"] == {"start": position, "end": completion_position}, item
					records.append({"name": name + "_completion", "document": incomplete, "position": completion_position,
									"method": "textDocument/completion", "request": completion_params, "response": completion,
									"supportingDocuments": override_libraries})
				records.append(record)

			for name, newline, out_of_class in (
				("document_symbol_ranges_lf", "\n", False),
				("document_symbol_ranges_crlf", "\r\n", False),
				("document_symbol_out_of_class_body", "\n", True),
			):
				source = newline.join((
					"class Box {", "public:", "  int Get();", "}",
					"int Box::Get() {", "  ExternalBody:", "  return 1;", "}", "",
				) if out_of_class else (
					"#define RET int", "class Box {", "  RET field;", "  RET Get() {",
					"    Done:", "    return field;", "  }", "}", "void main() {}", "",
				))
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}}
				response = send("textDocument/documentSymbol", params)

				symbols = response["result"]
				assert isinstance(symbols, list), (name, response)
				for symbol in symbols:
					check_document_symbol_ranges(source, symbol)
				box = next(symbol for symbol in symbols if symbol["name"] == "Box")
				method = next(symbol for symbol in box["children"] if symbol["name"] == "Get")
				assert source[utf16_offset(source, box["selectionRange"]["start"]):
							  utf16_offset(source, box["selectionRange"]["end"])] == "Box", box
				assert source[utf16_offset(source, method["selectionRange"]["start"]):
							  utf16_offset(source, method["selectionRange"]["end"])] == "Get", method
				if out_of_class:
					assert box["range"]["end"] == {"line": 3, "character": 1}, box
					assert not method.get("children"), method
					assert "ExternalBody" not in json.dumps(symbols), response
				else:
					assert box["range"]["end"] == {"line": 7, "character": 1}, box
					assert method["range"]["end"] == {"line": 6, "character": 3}, method
					assert {symbol["name"] for symbol in box["children"]} == {"field", "Get"}, box
					assert any(symbol["name"] == "Done" for symbol in method.get("children", [])), method
				records.append({"name": name, "document": source, "method": "textDocument/documentSymbol",
								"request": params, "response": response})

			for name, marked in (
				("document_symbol_shrinking_macro_eof", "#define LONGTYPE int\nclass Box { LONGTYPE field; }|"),
				("document_symbol_expanding_macro_eof", "#define T int\nclass Box { T field; }|"),
				("document_symbol_shrinking_macro_lf", "#define LONGTYPE int\nclass Box { LONGTYPE field; }|\n"),
				("document_symbol_expanding_macro_crlf", "#define T int\r\nclass Box { T field; }|\r\n"),
				("document_symbol_utf8_crlf", '#define LONGTYPE int\r\nclass Box { LONGTYPE field; string text = "😀"; }|\r\n'),
			):
				source, expected_end = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}}
				response = send("textDocument/documentSymbol", params)
				box = next(symbol for symbol in response["result"] if symbol["name"] == "Box")
				check_document_symbol_ranges(source, box)
				assert box["range"]["end"] == expected_end, (name, box)
				assert any(child["name"] == "field" for child in box["children"]), (name, box)
				records.append({"name": name, "document": source, "method": "textDocument/documentSymbol",
								"request": params, "response": response})

			for name, marked in (
				("document_symbol_macro_closing_brace", "#define END }\nclass Box { int field; |END"),
				("document_symbol_bom_extent_fallback", "\ufeffclass Box { int field; |}"),
			):
				source, last_token = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}}
				response = send("textDocument/documentSymbol", params)
				box = next(symbol for symbol in response["result"] if symbol["name"] == "Box")
				check_document_symbol_ranges(source, box)
				assert any(child["name"] == "field" for child in box["children"]), box
				# Collapsed macro origins / stripped first-line BOMs cannot
				# establish a precise physical extent. Keep reliable children.
				assert utf16_offset(source, box["range"]["end"]) <= utf16_offset(source, last_token), box
				records.append({"name": name, "document": source, "method": "textDocument/documentSymbol",
								"request": params, "response": response})

			source = "#define a(x) a\nenum E { a   (1), b }\n"
			uri = open_document("document_symbol_function_macro_member", source)
			params = {"textDocument": {"uri": uri}}
			response = send("textDocument/documentSymbol", params)
			enumeration = next(symbol for symbol in response["result"] if symbol["name"] == "E")
			check_document_symbol_ranges(source, enumeration)
			member = next(symbol for symbol in enumeration["children"] if symbol["name"] == "a")
			assert member["range"] == member["selectionRange"], member
			records.append({"name": "document_symbol_function_macro_member", "document": source,
							"method": "textDocument/documentSymbol", "request": params, "response": response})

			for name, marked, display_name, source_name in (
				("document_symbol_generic_selection", "class Boxcar{};\nclass |Box <T> {public:T value;}\n", "Box<T>", "Box"),
				("document_symbol_qualified_selection", "namespace N{\nclass Boxcar{};\nclass |Box{}\n}\n", "N.Box", "Box"),
				("document_symbol_comment_selection", "class /* Box */ |Box <T> {public:T value;}\n", "Box<T>", "Box"),
				("document_symbol_operator_prefix_selection", "class |operatorBox <T> {public:T value;}\n",
				 "operatorBox<T>", "operatorBox"),
			):
				source, expected = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}}
				response = send("textDocument/documentSymbol", params)
				symbol = next(item for item in response["result"] if item["name"] == display_name)
				assert symbol["selectionRange"]["start"] == expected, (name, symbol)
				begin = utf16_offset(source, symbol["selectionRange"]["start"])
				end = utf16_offset(source, symbol["selectionRange"]["end"])
				assert source[begin:end] == source_name, (name, symbol)
				assert utf16_offset(source, symbol["range"]["start"]) <= begin < end \
					<= utf16_offset(source, symbol["range"]["end"]), (name, symbol)
				records.append({"name": name, "document": source, "method": "textDocument/documentSymbol",
								"request": params, "response": response})

			library = Path(directory) / "outline-library.vs"
			library.write_text("export int LibraryValue=1;\n", encoding="utf-8")
			source = 'import "outline-library.vs";\nvoid main(){}\n'
			uri = open_document("document_symbol_import_selection", source)
			params = {"textDocument": {"uri": uri}}
			response = send("textDocument/documentSymbol", params)
			imported = next(item for item in response["result"] if item["name"] == "outline-library.vs")
			begin = utf16_offset(source, imported["selectionRange"]["start"])
			end = utf16_offset(source, imported["selectionRange"]["end"])
			assert source[begin:end] == "outline-library.vs", imported
			records.append({"name": "document_symbol_import_selection", "document": source,
							"method": "textDocument/documentSymbol", "request": params, "response": response,
							"supportingDocuments": {library.name: "export int LibraryValue=1;\n"}})

			for name, expression in (
				("format_double_minus", "- -x"), ("format_double_plus", "+ +x"),
				("format_minus_decrement", "- --x"), ("format_plus_increment", "+ ++x"),
			):
				source = f"void main(){{int x=3;int y={expression};print(x);print(y);}}"
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri},
						  "options": {"tabSize": 4, "insertSpaces": True}}
				response = send("textDocument/formatting", params)
				applied = source
				for edit in reversed(response["result"]):
					applied = apply_edit(applied, edit)
				assert f"int y = {expression};" in applied, (name, applied)
				send("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
					"contentChanges": [{"text": applied}]}, notification=True)
				repeated = send("textDocument/formatting", params)
				repeated_text = applied
				for edit in reversed(repeated["result"]):
					repeated_text = apply_edit(repeated_text, edit)
				assert repeated_text == applied, (name, repeated)
				records.append({"name": name, "document": source,
								"method": "textDocument/formatting", "request": params,
								"response": response, "applied": applied,
								"repeatedResponse": repeated})

			source = "void main(){int x=. 5;}"
			uri = open_document("format_separated_dot_number", source)
			params = {"textDocument": {"uri": uri},
					  "options": {"tabSize": 4, "insertSpaces": True}}
			response = send("textDocument/formatting", params)
			applied = source
			for edit in reversed(response["result"]):
				applied = apply_edit(applied, edit)
			assert ". 5" in applied, (applied, response)
			records.append({"name": "format_separated_dot_number", "document": source,
							"method": "textDocument/formatting", "request": params,
							"response": response, "applied": applied})

			base = "struct Point {{ int x; }}{eol}void main() {{{eol}    handle<Point> c=handle(local);{eol}{next_line}{eol}}}{eol}"

			library = ('export class Functor {public: int operator()(int v){return v;}'
					   'int operator[](int v){return v;}};'
					   'export int compute(int value){return value;}')
			uses = ('import "reference_library.vs";void main(){Functor f=Functor();'
					'print("😀");print(f(1));print(f(2));print(f[3]);print(f[4]);'
					'print(compute(1));print(compute(2));}')
			library_uri = open_document("reference_library", library)
			uses_uri = open_document("reference_uses", uses)
			unrelated = ('class Functor {public: int operator()(int v){return v;}'
						 'int operator[](int v){return v;}};'
						 'int compute(int value){return value;}'
						 'void main(){Functor f=Functor();print(f(1));print(f[2]);print(compute(3));}')
			unrelated_uri = open_document("reference_unrelated", unrelated)
			documents = [{"uri": library_uri, "text": library},
						 {"uri": uses_uri, "text": uses},
						 {"uri": unrelated_uri, "text": unrelated}]
			for token, name in (("operator()", "references_cross_file_call"),
								("operator[]", "references_cross_file_index"),
								("compute", "references_reverse_import")):
				position = {"line": 0, "character": library.index(token)}
				params = {"textDocument": {"uri": library_uri}, "position": position,
						  "context": {"includeDeclaration": True}}
				response = send("textDocument/references", params)
				locations = response["result"]
				assert len(locations) == 3, (name, response)
				assert sum(location["uri"] == uses_uri for location in locations) == 2, response
				assert all(location["uri"] != unrelated_uri for location in locations), response
				if token == "compute":
					expected = [len(uses[:offset].encode("utf-16-le")) // 2
								for offset in (uses.index("compute(1)"), uses.index("compute(2)"))]
					actual = sorted(location["range"]["start"]["character"]
									for location in locations if location["uri"] == uses_uri)
					assert actual == expected, (actual, expected, response)
				records.append({"name": name, "documents": documents, "position": position,
								"method": "textDocument/references", "request": params,
								"response": response})
			rename_params = {"textDocument": {"uri": library_uri},
							 "position": {"line": 0, "character": library.index("compute")},
							 "newName": "calculate"}
			renamed = send("textDocument/rename", rename_params)
			changes = renamed["result"]["changes"]
			assert set(changes) == {library_uri, uses_uri}, renamed
			assert len(changes[library_uri]) == 1 and len(changes[uses_uri]) == 2, renamed
			renamed_uses = uses
			for edit in changes[uses_uri]:
				renamed_uses = apply_edit(renamed_uses, edit)
			assert 'print("😀")' in renamed_uses and renamed_uses.count("calculate(") == 2, renamed_uses
			records.append({"name": "rename_reverse_import_utf16", "documents": documents,
							"method": "textDocument/rename", "request": rename_params,
							"response": renamed, "applied": renamed_uses})

			recovery_library = "export int recover_value(int value){return value;}"
			recovery_broken = ('import "reference_recovery_lib.vs";void main(){'
							   'print(recover_value(1));print(recover_value(2))}')
			recovery_valid = ('import "reference_recovery_lib.vs";'
							  'void extra(){print(recover_value(3));}')
			recovery_unrelated = ('int recover_value(int value){return value;}'
								  'void main(){print(recover_value(4))}')
			recovery_malformed = 'void main(){print("recover_value'
			recovery_lib_uri = open_document("reference_recovery_lib", recovery_library)
			recovery_broken_uri = open_document("reference_recovery_broken", recovery_broken)
			recovery_valid_uri = open_document("reference_recovery_valid", recovery_valid)
			recovery_unrelated_uri = open_document("reference_recovery_unrelated", recovery_unrelated)
			recovery_malformed_uri = open_document("reference_recovery_malformed", recovery_malformed)
			recovery_documents = [{"uri": recovery_lib_uri, "text": recovery_library},
								  {"uri": recovery_broken_uri, "text": recovery_broken},
								  {"uri": recovery_valid_uri, "text": recovery_valid},
								  {"uri": recovery_unrelated_uri, "text": recovery_unrelated},
								  {"uri": recovery_malformed_uri, "text": recovery_malformed}]
			hover_params = {"textDocument": {"uri": recovery_broken_uri},
							"position": {"line": 0, "character": recovery_broken.index("recover_value")}}
			recovered_hover = send("textDocument/hover", hover_params)
			assert recovered_hover["result"] and "recover_value" in str(recovered_hover["result"])
			records.append({"name": "hover_recovered_import", "documents": recovery_documents,
							"method": "textDocument/hover", "request": hover_params,
							"response": recovered_hover})
			for name, uri, source in (("references_recovered_importer", recovery_lib_uri, recovery_library),
									  ("references_from_recovered_importer", recovery_broken_uri, recovery_broken)):
				params = {"textDocument": {"uri": uri},
						  "position": {"line": 0, "character": source.index("recover_value")},
						  "context": {"includeDeclaration": True}}
				response = send("textDocument/references", params)
				locations = response["result"]
				assert len(locations) == 4, (name, response)
				assert sum(loc["uri"] == recovery_broken_uri for loc in locations) == 2, response
				assert sum(loc["uri"] == recovery_valid_uri for loc in locations) == 1, response
				assert all(loc["uri"] != recovery_unrelated_uri for loc in locations), response
				assert all(loc["uri"] != recovery_malformed_uri for loc in locations), response
				records.append({"name": name, "documents": recovery_documents,
								"method": "textDocument/references", "request": params, "response": response})
			params = {"textDocument": {"uri": recovery_lib_uri},
					  "position": {"line": 0, "character": recovery_library.index("recover_value")},
					  "newName": "fixed_value"}
			response = send("textDocument/rename", params)
			changes = response["result"]["changes"]
			assert set(changes) == {recovery_lib_uri, recovery_broken_uri, recovery_valid_uri}, response
			assert len(changes[recovery_broken_uri]) == 2, response
			renamed_broken = recovery_broken
			for edit in changes[recovery_broken_uri]:
				renamed_broken = apply_edit(renamed_broken, edit)
			assert renamed_broken == recovery_broken.replace("recover_value", "fixed_value"), renamed_broken
			records.append({"name": "rename_recovered_importer", "documents": recovery_documents,
							"method": "textDocument/rename", "request": params, "response": response,
							"applied": renamed_broken})

			for name in ("operatorCount", "operator_count", "operator2", "count"):
				source = (f"void {name}(){{}}class Other{{public:void {name}(){{}}}};"
						  f"void main(){{{name}();Other o=Other();o.{name}();}}")
				uri = open_document("reference_prefix_" + name, source)
				position = {"line": 0, "character": source.index(name)}
				params = {"textDocument": {"uri": uri}, "position": position,
						  "context": {"includeDeclaration": True}}
				response = send("textDocument/references", params)
				locations = response["result"]
				expected = [source.index(name), source.index(name, source.index("void main"))]
				assert len(locations) == 2 and all(loc["uri"] == uri for loc in locations), response
				assert sorted(loc["range"]["start"]["character"] for loc in locations) == expected, response
				records.append({"name": "references_prefix_" + name, "document": source,
								"method": "textDocument/references", "request": params, "response": response})
				params = {"textDocument": {"uri": uri}, "position": position, "newName": "renamed"}
				response = send("textDocument/rename", params)
				assert set(response["result"]["changes"]) == {uri}, response
				edits = response["result"]["changes"][uri]
				assert len(edits) == 2, response
				renamed_source = source
				for edit in edits:
					renamed_source = apply_edit(renamed_source, edit)
				expected_source = (f"void renamed(){{}}class Other{{public:void {name}(){{}}}};"
								   f"void main(){{renamed();Other o=Other();o.{name}();}}")
				assert renamed_source == expected_source, (renamed_source, response)
				records.append({"name": "rename_prefix_" + name, "document": source,
								"method": "textDocument/rename", "request": params,
								"response": response, "applied": renamed_source})

			source = ("namespace N{void count(){}class Other{public:void count(){}};"
					  "void main(){count();Other o=Other();o.count();}}")
			uri = open_document("references_same_line_namespace", source)
			position = {"line": 0, "character": source.index("count")}
			params = {"textDocument": {"uri": uri}, "position": position,
					  "context": {"includeDeclaration": True}}
			response = send("textDocument/references", params)
			locations = response["result"]
			expected = [source.index("count"), source.index("count", source.index("void main"))]
			assert len(locations) == 2 and all(loc["uri"] == uri for loc in locations), response
			assert sorted(loc["range"]["start"]["character"] for loc in locations) == expected, response
			records.append({"name": "references_same_line_namespace", "document": source,
							"method": "textDocument/references", "request": params, "response": response})
			params = {"textDocument": {"uri": uri}, "position": position, "newName": "renamed"}
			response = send("textDocument/rename", params)
			assert set(response["result"]["changes"]) == {uri}, response
			edits = response["result"]["changes"][uri]
			assert len(edits) == 2, response
			applied = source
			for edit in edits:
				applied = apply_edit(applied, edit)
			assert applied == ("namespace N{void renamed(){}class Other{public:void count(){}};"
							   "void main(){renamed();Other o=Other();o.count();}}"), (applied, response)
			records.append({"name": "rename_same_line_namespace", "document": source,
							"method": "textDocument/rename", "request": params,
							"response": response, "applied": applied})

			source = "void foo(){}void foo(int value){}void main(){foo();foo(1);}"
			uri = open_document("references_overload_group", source)
			position = {"line": 0, "character": source.index("foo")}
			params = {"textDocument": {"uri": uri}, "position": position,
					  "context": {"includeDeclaration": True}}
			response = send("textDocument/references", params)
			locations = response["result"]
			expected = [index for index in range(len(source)) if source.startswith("foo", index)]
			assert len(locations) == 4 and all(loc["uri"] == uri for loc in locations), response
			assert sorted(loc["range"]["start"]["character"] for loc in locations) == expected, response
			records.append({"name": "references_overload_group", "document": source,
							"method": "textDocument/references", "request": params, "response": response})
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/prepareRename", params, expect_error=True)
			assert "ambiguous" in response["error"]["message"], response
			records.append({"name": "rename_overload_group_refused", "document": source,
							"method": "textDocument/prepareRename", "request": params, "response": response})

			source = "class Foo{public:Foo(){}};void main(){Foo f=Foo();}"
			uri = open_document("rename_same_line_constructor", source)
			params = {"textDocument": {"uri": uri},
					  "position": {"line": 0, "character": source.index("Foo()")}}
			response = send("textDocument/prepareRename", params, expect_error=True)
			assert "type" in response["error"]["message"], response
			records.append({"name": "rename_constructor_refused", "document": source,
							"method": "textDocument/prepareRename", "request": params, "response": response})
			params = {"textDocument": {"uri": uri},
					  "position": {"line": 0, "character": source.index("Foo{")}, "newName": "Bar"}
			response = send("textDocument/rename", params)
			assert set(response["result"]["changes"]) == {uri}, response
			edits = response["result"]["changes"][uri]
			assert len(edits) == 4, response
			applied = source
			for edit in edits:
				applied = apply_edit(applied, edit)
			assert applied == source.replace("Foo", "Bar"), (applied, response)
			records.append({"name": "rename_same_line_type", "document": source,
							"method": "textDocument/rename", "request": params,
							"response": response, "applied": applied})

			for name, declaration in (
				("call", "int operator()(int value){return value;}"),
				("plus", "int operator+(int value){return value;}"),
				("conversion", "operator bool(){return true;}"),
			):
				source = "class OperatorOwner{public:" + declaration + "};void main(){}"
				uri = open_document("rename_operator_" + name, source)
				params = {"textDocument": {"uri": uri},
						  "position": {"line": 0, "character": source.index("operator")}}
				response = send("textDocument/prepareRename", params, expect_error=True)
				assert "result" not in response, response
				records.append({"name": "rename_operator_" + name + "_refused", "document": source,
								"method": "textDocument/prepareRename", "request": params, "response": response})

			for name, body, call in (
				("references_explicit_constructor",
				 "Functor f=Functor();print(f(1));", "f(1)"),
				("references_constructor_alias",
				 "Functor f=Maker();print(f(1));", "f(1)"),
				("references_type_name_shadow",
				 "Functor f=Functor();{Functor Functor=f;print(Functor(1));}", "Functor(1)"),
			):
				source = ("class Functor {public: Functor(){} "
						  "int operator()(int v){return v;}};using Maker=Functor;"
						  "void main(){" + body + "}")
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri},
						  "position": {"line": 0, "character": source.index("operator()")},
						  "context": {"includeDeclaration": True}}
				response = send("textDocument/references", params)
				locations = response["result"]
				assert len(locations) == 2 and all(loc["uri"] == uri for loc in locations), response
				assert sorted(loc["range"]["start"]["character"] for loc in locations) == [
					source.index("operator()"), source.index(call) + call.index("(")], response
				records.append({"name": name, "document": source, "method": "textDocument/references",
								"request": params, "response": response})

			for suffix, newline in (("lf", "\n"), ("crlf", "\r\n"), ("cr", "\r")):
				source = newline.join(('int first=1;', 'string tail="😀old"; int next=2;', 'int last=3;'))
				uri = open_document("incremental_physical_lines_" + suffix, source)
				expected = source
				changes = []
				for old, new in (("old", "fresh"), ("next", "replacement"), ("last", "finalValue")):
					begin = expected.index(old)
					edit = {"range": {"start": physical_lsp_position(expected, begin),
									  "end": physical_lsp_position(expected, begin + len(old))},
							"newText": new}
					changes.append({"range": edit["range"], "text": new})
					expected = apply_edit(expected, edit)
				change_params = {"textDocument": {"uri": uri, "version": 2}, "contentChanges": changes}
				send("textDocument/didChange", change_params, notification=True)
				params = {"textDocument": {"uri": uri}}
				response = send("textDocument/documentSymbol", params)
				assert {item["name"] for item in response["result"]} == {
					"first", "tail", "replacement", "finalValue"}, (suffix, response)
				formatting_params = {**params, "options": {"tabSize": 4, "insertSpaces": True}}
				formatted = send("textDocument/formatting", formatting_params)
				assert len(formatted["result"]) == 1, formatted
				applied = apply_edit(expected, formatted["result"][0])
				assert 'string tail = "😀fresh";' in applied and "int replacement = 2;" in applied \
					and "int finalValue = 3;" in applied, (suffix, applied)
				records.append({"name": "incremental_physical_lines_" + suffix, "document": source,
								"changes": change_params, "expectedDocument": expected,
								"method": "textDocument/documentSymbol", "request": params, "response": response,
								"formattingRequest": formatting_params, "formattingResponse": formatted,
								"applied": applied})

			source = "int\rvalue=1;"
			uri = open_document("partial_format_bare_cr", source)
			for method, arguments in (
				("textDocument/rangeFormatting", {"range": {"start": {"line": 1, "character": 0},
															"end": {"line": 1, "character": 8}}}),
				("textDocument/onTypeFormatting", {"position": {"line": 1, "character": 8}, "ch": ";"}),
				("textDocument/onTypeFormatting", {"position": {"line": 1, "character": 0}, "ch": "\n"}),
			):
				params = {"textDocument": {"uri": uri}, **arguments,
						  "options": {"tabSize": 4, "insertSpaces": True}}
				response = send(method, params)
				assert response["result"] == [], (method, response)
				records.append({"name": "partial_format_bare_cr", "document": source,
								"method": method, "request": params, "response": response})

			for suffix, source in (
				("lf", 'int first=1;\nstring tail="😀";'),
				("crlf", 'int first=1;\r\nstring tail="😀";'),
				("cr", 'int first=1;\rstring tail="😀";'),
				("mixed", 'int first=1;\r\nint second=2;\rstring tail="😀";'),
				("raw_cr", 'string tail=r"first\r😀";'),
				("terminal_cr", 'int tail=1;\r'),
			):
				physical = source.replace("\r\n", "\n").replace("\r", "\n").split("\n")
				expected_range = {"start": {"line": 0, "character": 0},
								  "end": {"line": len(physical) - 1,
										  "character": len(physical[-1].encode("utf-16-le")) // 2}}
				for method in ("textDocument/formatting", "textDocument/codeAction"):
					name = "full_document_range_" + suffix + ("_action" if method.endswith("codeAction") else "")
					uri = open_document(name, source)
					selection_params = {"textDocument": {"uri": uri},
										"positions": [{"line": 0, "character": 1}]}
					selection = send("textDocument/selectionRange", selection_params)
					document = selection["result"][0]
					while "parent" in document:
						document = document["parent"]
					assert document["range"] == expected_range, (name, selection)
					params = {"textDocument": {"uri": uri}}
					if method.endswith("codeAction"):
						params.update({"range": {"start": {"line": 0, "character": 0},
												 "end": {"line": 0, "character": 0}},
									   "context": {"diagnostics": [], "only": ["source.formatDocument"]}})
					else:
						params["options"] = {"tabSize": 4, "insertSpaces": True}
					response = send(method, params)
					edits = response["result"][0]["edit"]["changes"][uri] \
						if method.endswith("codeAction") else response["result"]
					assert len(edits) == 1 and edits[0]["range"] == expected_range, (name, response)
					applied = apply_edit(source, edits[0])
					assert applied == edits[0]["newText"] and applied.count("tail") == 1, (name, applied)
					send("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2},
						 "contentChanges": [{"text": applied}]}, notification=True)
					repeated = send(method, params)
					repeated_edits = repeated["result"][0]["edit"]["changes"][uri] \
						if method.endswith("codeAction") else repeated["result"]
					repeated_text = applied
					for edit in reversed(repeated_edits):
						repeated_text = apply_edit(repeated_text, edit)
					assert repeated_text == applied, (name, repeated)
					records.append({"name": name, "document": source, "method": method,
									"request": params, "response": response, "applied": applied,
									"selectionRequest": selection_params, "selectionResponse": selection,
									"repeatedResponse": repeated})

			for name, eol, next_line in (
				("enter_lf", "\n", ""),
				("enter_crlf", "\r\n", ""),
				("enter_auto_indent", "\n", "    "),
				("enter_comment", "\n", "    // keep  spaces"),
			):
				source = base.format(eol=eol, next_line=next_line)
				uri = open_document(name, source)
				position = {"line": 3, "character": len(next_line)}
				params = {"textDocument": {"uri": uri}, "position": position,
						  "ch": "\n", "options": {"tabSize": 4, "insertSpaces": True}}
				response = send("textDocument/onTypeFormatting", params)
				edits = response["result"] or []
				assert len(edits) <= 1, (name, response)
				for edit in edits:
					assert edit["range"]["start"]["line"] == 2, (name, edit)
					assert edit["range"]["end"]["line"] == 2, (name, edit)
					assert "\n" not in edit["newText"] and "\r" not in edit["newText"]
				applied = source
				for edit in reversed(edits):
					applied = apply_edit(applied, edit)
				assert eol + next_line + eol in applied, (name, applied)
				assert applied.count(eol) == source.count(eol), (name, applied)
				assert "handle<Point> c = handle(local);" in applied, (name, applied)
				if eol == "\r\n":
					assert applied.count("\r\n") == applied.count("\n")
				send("textDocument/didChange", {"textDocument": {"uri": uri,
					"version": 2}, "contentChanges": [{"text": applied}]},
					notification=True)
				repeated = send("textDocument/onTypeFormatting", params)
				assert repeated["result"] in ([], None), (name, repeated)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/onTypeFormatting", "request": params,
								"response": response, "applied": applied,
								"repeatedResponse": repeated})

			for name, marked in (
				("enter_unary_chain", "void main() {\n    int x=3;\n    int y=- -x;\n|\n}\n"),
				("enter_incomplete", "void main() {\n    handle<Point> c = handle(local\n|\n}\n"),
				("enter_string", 'void main() {\n    print("a  b");\n|\n}\n'),
				("enter_trailing_comment", 'void main() {\n    print( "a  b" ); // keep  spaces\n|\n}\n'),
				("enter_utf16", 'void main() {\n    print("😀");\n|\n}\n'),
				("enter_utf16_unformatted", 'void main() {\n    print("😀") ;\n|\n}\n'),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position,
						  "ch": "\n", "options": {"tabSize": 4, "insertSpaces": True}}
				response = send("textDocument/onTypeFormatting", params)
				edits = response["result"] or []
				assert len(edits) <= 1, (name, response)
				if name == "enter_utf16_unformatted":
					assert len(edits) == 1, (name, response)
				applied = source
				for edit in reversed(edits):
					assert edit["range"]["start"]["line"] == position["line"] - 1
					assert edit["range"]["end"]["line"] == position["line"] - 1
					if name == "enter_utf16_unformatted":
						assert edit["range"]["end"]["character"] == len(
							'    print("😀") ;'.encode("utf-16-le")) // 2
					applied = apply_edit(applied, edit)
				assert applied.count("\n") == source.count("\n")
				if name == "enter_string":
					assert '"a  b"' in applied
				if name == "enter_trailing_comment":
					assert '"a  b"' in applied and "// keep  spaces" in applied
					assert len(edits) == 1, (name, response)
				if name == "enter_unary_chain":
					assert "int y = - -x;" in applied, (name, applied)
					assert len(edits) == 1, (name, response)
				send("textDocument/didChange", {"textDocument": {"uri": uri,
					"version": 2}, "contentChanges": [{"text": applied}]},
					notification=True)
				repeated = send("textDocument/onTypeFormatting", params)
				assert repeated["result"] in ([], None), (name, repeated)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/onTypeFormatting", "request": params,
								"response": response, "applied": applied,
								"repeatedResponse": repeated})

			tab_source = "void main() {\nshared<Point> c=shared(local);\n\n}\n"
			tab_uri = open_document("enter_tabs", tab_source)
			tab_position = {"line": 2, "character": 0}
			tab_params = {"textDocument": {"uri": tab_uri},
						  "position": tab_position, "ch": "\n",
						  "options": {"tabSize": 4, "insertSpaces": False}}
			tab_response = send("textDocument/onTypeFormatting", tab_params)
			assert len(tab_response["result"] or []) == 1, tab_response
			tab_edit = tab_response["result"][0]
			assert tab_edit["newText"] == "\tshared<Point> c = shared(local);"
			tab_applied = apply_edit(tab_source, tab_edit)
			assert tab_applied == "void main() {\n\tshared<Point> c = shared(local);\n\n}\n"
			send("textDocument/didChange", {"textDocument": {
				"uri": tab_uri, "version": 2},
				"contentChanges": [{"text": tab_applied}]}, notification=True)
			tab_repeated = send("textDocument/onTypeFormatting", tab_params)
			assert tab_repeated["result"] in ([], None), tab_repeated
			records.append({"name": "enter_tabs", "document": tab_source,
							"position": tab_position,
							"method": "textDocument/onTypeFormatting",
							"request": tab_params, "response": tab_response,
							"applied": tab_applied,
							"repeatedResponse": tab_repeated})

			for name, marked in (
				("class_name_std_prefix", "class st|"),
				("struct_name_std_prefix", "struct st|"),
				("namespace_name_std_prefix", "namespace st|"),
				("variable_name_std_prefix", "void main() { int st|; }"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				assert items(response) == [], (name, response)
				records.append({"name": name, "document": source,
								"position": position, "method": "textDocument/completion",
								"request": params, "response": response})

			for name, marked, label, expected_insert in (
				("namespace_root", "void main() { std|; }", "std", "std::"),
				("namespace_root_existing_scope", "void main() { st|d::chrono::now(); }", "std", "std"),
				("namespace_child", "void main() { std::| }", "algorithm", "algorithm::"),
				("namespace_prefix", "void main() { std::ch| }", "chrono", "chrono::"),
				("namespace_existing_scope", "void main() { std::ch|rono::now(); }", "chrono", "chrono"),
				("namespace_member", "void main() { std::chrono::| }", "now", "now"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				assert all("std." not in item["label"] for item in items(response)), (name, response)
				item = item_with_label(response, label)
				assert item["insertText"] == expected_insert, (name, item)
				assert item["filterText"] == label, (name, item)
				assert item["sortText"], (name, item)
				if name == "namespace_member":
					assert not any(value in [candidate["label"] for candidate in items(response)]
								   for value in ("false", "new", "write_file"))
				if "textEdit" in item:
					assert item["textEdit"]["newText"] == expected_insert, (name, item)
				assert "std." not in json.dumps(item.get("labelDetails", {})), (name, item)
				applied = apply_edit(source, item.get("textEdit", {
					"range": {"start": position, "end": position},
					"newText": item["insertText"],
				}))
				assert "std::" in applied and "std." not in applied, (name, applied)
				if name == "namespace_root":
					assert "std::;" in applied
				elif name == "namespace_child":
					assert "std::algorithm::" in applied
				elif name == "namespace_member":
					assert "std::chrono::now" in applied
				else:
					assert "std::chrono::" in applied and "std::chrono::::" not in applied
				resolved = send("completionItem/resolve", item)
				resolved_item = resolved["result"]
				assert resolved_item["label"] == label
				assert resolved_item["insertText"] == expected_insert
				assert "std." not in json.dumps(resolved_item.get("labelDetails", {}))
				if "textEdit" in resolved_item:
					assert resolved_item["textEdit"]["newText"] == expected_insert
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params,
								"response": response, "chosenItem": item,
								"resolvedResponse": resolved, "applied": applied})

			for name, marked, label, expected_insert in (
				("user_namespace_child",
				 "namespace Tools { namespace Inner { void use() {} } }\n"
				 "void main() { Tools::| }", "Inner", "Inner::"),
				("using_namespace_root", "using namespace st|;", "std", "std"),
				("using_namespace_child", "using namespace std::ch|;", "chrono", "chrono"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				item = item_with_label(response, label)
				assert item["insertText"] == expected_insert, (name, item)
				applied = apply_edit(source, item.get("textEdit", {
					"range": {"start": position, "end": position},
					"newText": item["insertText"],
				}))
				if name == "user_namespace_child":
					assert "Tools::Inner::" in applied
				elif name == "using_namespace_root":
					assert "using namespace std;" in applied
				else:
					assert "using namespace std::chrono;" in applied
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params,
								"response": response, "chosenItem": item,
								"applied": applied})

			for name, marked, expected_name, target_line, target_character in (
				("using_namespace_same_line_definition",
				 "namespace A {}\nnamespace B {}\nusing namespace A;using namespace B|;void main(){}",
				 "B", 1, 0),
				("using_namespace_continued_macro_definition",
				 '#define PROLOGUE \\\nvoid unused() {}\nPROLOGUE\nnamespace A {}\nnamespace B {}\nusing namespace B|;void main(){}',
				 "B", 4, 0),
				("using_namespace_scoped_macro_definition",
				 '#define PROLOGUE \\\nvoid unused() {}\nPROLOGUE\nnamespace A {namespace Shared{}}\nnamespace B {namespace Shared{} using namespace Shared|;}void main(){}',
				 "B::Shared", 4, 13),
				("using_namespace_multiline_definition",
				 "namespace A {}\nnamespace B {}\nusing\nnamespace\nB|;void main(){}",
				 "B", 1, 0),
				("using_namespace_joined_line_definition",
				 'namespace A {}\nnamespace B {}\nusing \\\nnamespace B|;void main(){}',
				 "B", 1, 0),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				definition = send("textDocument/definition", params)
				target = {"line": target_line, "character": target_character}
				assert definition["result"] == [{"uri": uri,
						"range": {"start": target, "end": target}}], (name, definition)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/definition", "request": params, "response": definition})
				hover = send("textDocument/hover", params)
				assert f"namespace {expected_name} " in hover["result"]["contents"]["value"], (name, hover)
				hover_token = expected_name.rsplit("::", 1)[-1]
				assert hover["result"]["range"] == {"start": {
						"line": position["line"], "character": position["character"] - len(hover_token)},
						"end": position}, (name, hover)
				full = send("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
				assert full["result"]["data"], full
				cached = send("textDocument/hover", params)
				assert cached["result"] == hover["result"], (name, cached)
				records.append({"name": name.replace("definition", "hover"),
								"document": source, "position": position,
								"method": "textDocument/hover", "request": params, "response": hover,
								"semanticResponse": full, "cachedResponse": cached})

			hover_source = ("struct Point { int x; }\n"
							"void main() { handle<Point> h = handle<Point>(); "
							"print(h); std::chrono::now(); }\n")
			hover_uri = open_document("hover", hover_source)
			for name, token, advance in (("hover_type", "handle<Point>", 0),
										 ("hover_function", "print(h)", 0),
										 ("hover_root_namespace", "std::chrono", 0),
										 ("hover_namespace", "std::chrono", 5)):
				prefix = hover_source[:hover_source.index(token) + advance]
				position = {"line": prefix.count("\n"),
							"character": len(prefix.rsplit("\n", 1)[-1])}
				params = {"textDocument": {"uri": hover_uri}, "position": position}
				response = send("textDocument/hover", params)
				assert response["result"] is not None, (name, response)
				contents = json.dumps(response["result"]["contents"], ensure_ascii=False)
				assert "soft builtin" not in contents, (name, response)
				assert contents.count("Built-in symbol.") <= 1, (name, response)
				if name == "hover_type":
					assert contents.count("Built-in symbol.") == 1 and "handle<T>" in contents
				if name == "hover_function":
					assert "function print" in contents and "string fmt" in contents
					assert contents.count("Built-in symbol.") == 1, (name, response)
				if name == "hover_namespace":
					assert "std::chrono" in contents and "std.chrono" not in contents
				records.append({"name": name, "document": hover_source,
								"position": position, "method": "textDocument/hover",
								"request": params, "response": response})

			memory_setup = "void main(){auto b=new std::memory::buffer(8);"
			for name, marked, required, forbidden in (
				("memory_namespace", "void main(){std::memory::|}", {"buffer", "byte_order", "read", "write"}, set()),
				("memory_enum", "void main(){std::memory::byte_order::|}", {"little", "big"}, {"auto", "buffer"}),
				("memory_enum_prefix", "void main(){std::memory::byte_order::li|}", {"little"}, {"big"}),
				("memory_clone", memory_setup + "auto c=std::memory::clone(b);c.|}", {"size", "resize"}, set()),
				("memory_from_array", "void main(){array<uint8> bytes={};auto b=std::memory::from_array(bytes);b.|}", {"size", "resize"}, set()),
				("memory_slice", memory_setup + "auto v=b.slice(0,4);v.|}", {"size", "set", "readonly_slice"}, {"resize"}),
				("memory_readonly", memory_setup + "auto v=b.readonly_slice(0,4);v.|}", {"size", "get", "slice"}, {"set", "resize", "readonly_slice"}),
				("memory_readonly_nested", memory_setup + "auto v=b.readonly_slice(0,4).slice(0,2);v.|}", {"size", "get"}, {"set", "resize"}),
				("memory_alias", "using namespace std::memory;void main(){auto b=new buffer(8);auto c=std::memory::clone(b);c.|}", {"size", "resize"}, set()),
				("memory_alias_shadow", "using namespace std::memory;int clone(any value){return 0;}" + memory_setup + "auto c=clone(b);c.|}", set(), {"size", "resize"}),
				("memory_alias_repeated", "using namespace std::memory;using namespace std::memory;void main(){auto b=new buffer(8);b.|}", {"size", "resize"}, set()),
				("memory_alias_namespace", "namespace N{using namespace std::memory;void main(){auto b=new buffer(8);b.|}}", {"size", "resize"}, set()),
				("memory_alias_other_namespace", "namespace N{using namespace std::memory;}" + memory_setup + "auto c=clone(b);c.|}", set(), {"size", "resize"}),
				("memory_alias_future", memory_setup + "auto c=clone(b);c.|}using namespace std::memory;", set(), {"size", "resize"}),
				("memory_alias_ambiguous", "namespace N{int clone(any value){return 0;}}using namespace N;using namespace std::memory;" + memory_setup + "auto c=clone(b);c.|}", set(), {"size", "resize"}),
				("memory_alias_method", "using namespace std::memory;class C{int clone(any value){return 0;}" + memory_setup + "auto c=clone(b);c.|}}", set(), {"size", "resize"}),
				("memory_to_array", memory_setup + "auto bytes=std::memory::to_array(b);bytes.|}", {"push", "reserve"}, {"readonly_slice"}),
				("memory_copy_array", memory_setup + "auto bytes=std::memory::to_array(b);auto copy=bytes;copy.|}", {"push", "reserve"}, {"readonly_slice"}),
				("memory_direct_clone", memory_setup + "std::memory::clone(b).|}", {"size", "resize"}, {"push"}),
				("memory_direct_array", memory_setup + "std::memory::to_array(b).|}", {"push", "reserve"}, {"resize"}),
				("memory_chain_completion", memory_setup + "b.slice(0,4).readonly_slice(0,2).|}", {"size", "get", "slice"}, {"set", "resize"}),
				("memory_chain_shadow", "void main(){int b=0;b.slice(0,4).slice(0,2).|}", set(), {"size", "set", "get", "resize"}),
				("memory_inferred_alias", memory_setup + "auto other=b;other.|}", {"size", "resize"}, set()),
				("memory_shadow", memory_setup + "{int b=0;auto v=b.slice(0,4);v.|}}", set(), {"size", "set", "resize"}),
				("memory_using_completion", "using namespace std::memory;" + memory_setup + "auto c=cl|(b);}", {"clone"}, set()),
				("memory_using_snippet", "using namespace std::memory;" + memory_setup + "cl|;}", {"clone"}, set()),
				("memory_function_reference", "using namespace std::memory;" + memory_setup + "auto callback=cl|;}", {"clone"}, set()),
				("memory_read_snippet", memory_setup + "std::memory::re|;}", {"read"}, set()),
				("memory_read_existing_typeargs", memory_setup + "auto value=std::memory::re|<int>(b,0);}", {"read"}, set()),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				labels = {item["label"] for item in items(response)}
				assert required <= labels and not labels & forbidden, (name, response)
				if name == "memory_enum":
					assert labels == {"little", "big"}, response
					assert item_with_label(response, "little")["detail"] == "std::memory::byte_order = 0", response
					assert item_with_label(response, "big")["detail"] == "std::memory::byte_order = 1", response
				applied = None
				if name == "memory_using_completion":
					item = item_with_label(response, "clone")
					assert item["insertText"] == "std::memory::clone", item
					applied = apply_edit(source, item["textEdit"])
					assert "auto c=std::memory::clone(b);" in applied, applied
				if name == "memory_using_snippet":
					item = item_with_label(response, "clone")
					assert item["insertTextFormat"] == 2 and item["insertText"].startswith("std::memory::clone("), item
					assert item["textEdit"]["newText"] == item["insertText"], item
				if name == "memory_function_reference":
					item = item_with_label(response, "clone")
					assert item["insertText"] == "std::memory::clone" and item.get("insertTextFormat", 1) == 1, item
				if name == "memory_read_snippet":
					item = item_with_label(response, "read")
					assert item["insertTextFormat"] == 2 and item["insertText"] == "read<${1:ValueType}>(${0})", item
					assert item["textEdit"]["newText"] == item["insertText"], item
				if name == "memory_read_existing_typeargs":
					item = item_with_label(response, "read")
					assert item["insertText"] == "read", item
					applied = apply_edit(source, item["textEdit"])
					assert "std::memory::read<int>(b,0);" in applied, applied
				resolved = []
				for item in items(response):
					result = send("completionItem/resolve", item)
					resolved.append(result)
					for candidate in (item, result["result"]):
						for field in ("label", "detail", "insertText", "labelDetails", "textEdit"):
							assert "std.memory" not in json.dumps(candidate.get(field)), (name, field, candidate)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "request": params,
								"response": response, "resolved": resolved, "applied": applied})

			for name, marked, expected, active_parameter in (
				("memory_constructor_help", "void main(){auto b=new std::memory::buffer(|);}",
				 ["std::memory::buffer()", "std::memory::buffer(int64)"], 0),
				("memory_using_constructor_help", "using namespace std::memory;void main(){auto b=new buffer(|);}",
				 ["std::memory::buffer()", "std::memory::buffer(int64)"], 0),
				("memory_using_view_help", "using namespace std::memory;void main(){auto v=view(|);}",
				 ["std::memory::view()"], 0),
				("memory_method_help", memory_setup + "b.slice(|);}",
				 ["std::memory::view std::memory::buffer.slice(int64, int64)"], 0),
				("memory_chain_help", memory_setup + "b.slice(0,4).slice(|);}",
				 ["std::memory::view std::memory::view.slice(int64, int64)"], 0),
				("memory_clone_chain_help", memory_setup + "std::memory::clone(b).resize(|);}",
				 ["void std::memory::buffer.resize(int64)"], 0),
				("memory_read_help", memory_setup + "std::memory::read<int>(b,|0);}",
				 ["int std::memory::read<int>(region bytes, int64 offset, std::memory::byte_order order = std::memory::byte_order::little)"], 1),
				("memory_read_help_incomplete", memory_setup + "std::memory::read<int>(b,|);}",
				 ["int std::memory::read<int>(region bytes, int64 offset, std::memory::byte_order order = std::memory::byte_order::little)"], 1),
				("memory_write_help", memory_setup + "std::memory::write<int>(b,0,|1);}",
				 ["void std::memory::write<int>(region bytes, int64 offset, int value, std::memory::byte_order order = std::memory::byte_order::little)"], 2),
				("memory_write_help_incomplete", memory_setup + "std::memory::write<int>(b,0,|);}",
				 ["void std::memory::write<int>(region bytes, int64 offset, int value, std::memory::byte_order order = std::memory::byte_order::little)"], 2),
				("autoclosed_call_help_incomplete", "void f(int x,int y){} void main(){f(1,|);}",
				 None, 1),
				("memory_signature_defaults", 'void f(std::memory::buffer value,string text="std.memory.buffer",double factor=1.25){}' + memory_setup + 'f(|b);}',
				 None, 0),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				result = response["result"]
				assert result and result["activeParameter"] == active_parameter, (name, response)
				if expected is not None:
					assert [sig["label"] for sig in result["signatures"]] == expected, (name, response)
				if name == "memory_signature_defaults":
					label = result["signatures"][0]["label"]
					assert "std::memory::buffer value" in label, response
					assert "string text = …" in label and "double factor = …" in label, response
				else:
					assert "std.memory" not in json.dumps(result), (name, response)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params,
								"response": response})

			restricted = ("class Restricted{\nprivate:\nRestricted(int secret){}\n"
						  "protected:\nRestricted(string guarded){}\npublic:\nRestricted(bool visible){}\n")
			for name, marked, expected_parameters in (
				("constructor_access_outside", restricted + "};\nvoid outside(){Restricted item=Restricted(|true);}",
				 {"visible"}),
				("constructor_access_owner", restricted + "static Restricted make(){return Restricted(|1);}\n};",
				 {"secret", "guarded", "visible"}),
				("constructor_access_derived", restricted + "};\nclass Derived:public Restricted{public:Derived():Restricted(|\"base\"){} };",
				 {"guarded", "visible"}),
				("constructor_access_out_of_class", restricted + "};\nclass Derived:public Restricted{int marker;public:Derived(string value);};\n"
				 "Derived::Derived(string value):Restricted(|value),marker(0){}", {"guarded", "visible"}),
				("constructor_access_out_of_class_recovery", restricted + "};\nclass Derived:public Restricted{int marker;public:Derived(string value);};\n"
				 "Derived::Derived(string value):Restricted(|),marker(0){}", {"guarded", "visible"}),
				("constructor_access_alias", restricted + "};\nusing Alias=Restricted;\nvoid outside(){Alias item=Alias(|true);}",
				 {"visible"}),
				("constructor_access_global", "class Secret{public:Secret(bool visible){}private:Secret(int hidden){}};Secret item=Secret(|true);",
				 {"visible"}),
				("constructor_access_global_newline", "class Secret{public:Secret(bool visible){}private:Secret(int hidden){}};\nSecret item=Secret(|true);",
				 {"visible"}),
				("constructor_access_recovery", restricted + "};\nvoid outside(){Restricted item=Restricted(|);}",
				 {"visible"}),
				("constructor_access_field_initializer", "class Secret{friend class Owner;private:Secret(int hidden){}public:Secret(bool visible){}};\n"
				 "class Owner{public:Secret item=Secret(|1);};", {"hidden", "visible"}),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				signatures = (response["result"] or {}).get("signatures", [])
				parameters = {parameter["label"].rsplit(" ", 1)[-1]
							  for signature in signatures for parameter in signature["parameters"]}
				assert len(signatures) == len(expected_parameters) and parameters == expected_parameters, (name, response)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params,
								"response": response})

			for name, suffix in (
				("constructor_initializer_missing_argument", ",marker(value){}"),
				("constructor_initializer_missing_argument_block_comment", " /* keep */ ,marker(value){}"),
				("constructor_initializer_missing_argument_line_comment", " // keep\n,marker(value){}"),
				("constructor_initializer_missing_argument_body_comment", " /* keep */ {}"),
			):
				marked = ("class Base{private:Base(string hidden){}protected:Base(int guarded,int second){}"
						  "public:Base(bool visible){}};\n"
						  "class Derived:public Base{int marker;public:Derived(int value);};\n"
						  "Derived::Derived(int value):Base(value,|)" + suffix)
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				result = response["result"] or {}
				signatures = result.get("signatures", [])
				assert len(signatures) == 2 and result["activeParameter"] == 1, (name, response)
				assert "guarded" in signatures[result["activeSignature"]]["label"], (name, response)
				assert all("hidden" not in signature["label"] for signature in signatures), (name, response)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params,
								"response": response})

			marked = ("class Base{protected:Base(int value){}};\n"
					  "class Derived:public Base{int marker;public:Derived(int actual_value);};\n"
					  "Derived::Derived(int actual_value):Base(actual_va|lue),marker(actual_value){}\n")
			source, position = marked_source(marked)
			uri = open_document("constructor_initializer_parameter", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/completion", params)
			item = item_with_label(response, "actual_value")
			assert item["insertText"] == "actual_value", item
			assert apply_edit(source, item["textEdit"]) == source, item
			records.append({"name": "constructor_initializer_parameter_completion", "document": source,
							"position": position, "method": "textDocument/completion", "request": params,
							"response": response})
			response = send("textDocument/definition", params)
			_, expected = marked_source(source.replace("Derived::Derived(int actual_value)",
														"Derived::Derived(int |actual_value)", 1))
			locations = response["result"] or []
			assert len(locations) == 1 and locations[0]["uri"] == uri, response
			assert locations[0]["range"]["start"] == expected, response
			records.append({"name": "constructor_initializer_parameter_definition", "document": source,
							"position": position, "method": "textDocument/definition", "request": params,
							"response": response})

			for name, friend_type, function_type, allowed, function_namespace in (
				("mutable_reference_friend", "ref Secret", "ref Secret", True, ""),
				("const_reference_friend", "const ref Secret", "const ref Secret", True, ""),
				("value_friend_reference_overload", "Secret", "ref Secret", False, ""),
				("value_friend_const_reference_overload", "Secret", "const ref Secret", False, ""),
				("unrelated_namespace_friend", "ref Secret", "ref Secret", False, "Other"),
			):
				declaration = ("class Secret{\nfriend int Inspect(" + friend_type + " value);\nprivate:\n"
							   "int Hidden(int amount)const{return amount;}\n};\n")
				for method, body in (("textDocument/signatureHelp", "return value.Hidden(|1);"),
									 ("textDocument/completion", "value.Hi|;return 0;")):
					function = "int Inspect(" + function_type + " value){" + body + "}"
					if function_namespace:
						function = "namespace " + function_namespace + "{" + function + "}"
					source, position = marked_source(declaration + function)
					uri = open_document(name + ("_help" if method.endswith("Help") else "_completion"), source)
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send(method, params)
					if method == "textDocument/signatureHelp":
						signatures = (response["result"] or {}).get("signatures", [])
						assert bool(signatures) == allowed, (name, response)
						if allowed:
							assert len(signatures) == 1, response
							assert signatures[0]["label"] == "Secret.Hidden int(int amount) const", response
							assert signatures[0]["parameters"] == [{"label": "int amount"}], response
							assert response["result"]["activeSignature"] == 0 \
								and response["result"]["activeParameter"] == 0, response
					else:
						assert ("Hidden" in {item["label"] for item in items(response)}) == allowed, (name, response)
					records.append({"name": name, "document": source, "position": position,
									"method": method, "request": params, "response": response})

			for name, friend_type, function_type, allowed in (
				("friend_reference_identifier_collision", "ref int", "refint", False),
				("friend_identifier_reference_collision", "refint", "ref int", False),
				("friend_nested_callback_collision", "fn(ref int)->int", "fn(refint)->int", False),
				("friend_nested_callback_exact", "fn(ref int)->int", "fn(ref int)->int", True),
			):
				marked = ("struct refint{};class Secret{friend Secret Create(" + friend_type
						  + " value);private:Secret(int hidden){}public:Secret(bool visible){}};\n"
						  + "Secret Create(" + function_type + " value){return Secret(|true);}")
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/signatureHelp", params)
				signatures = (response["result"] or {}).get("signatures", [])
				assert len(signatures) == (2 if allowed else 1), (name, response)
				assert any("hidden" in signature["label"] for signature in signatures) == allowed, (name, response)
				assert any("visible" in signature["label"] for signature in signatures), (name, response)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/signatureHelp", "request": params, "response": response})

			for name, marked, expected in (
				("memory_type_hover", "void main(){std::memory::buf|fer b;}", "class std::memory::buffer"),
				("memory_enum_hover", "void main(){std::memory::byte_or|der order;}", "enum std::memory::byte_order"),
				("memory_enum_member_hover", "void main(){auto order=std::memory::byte_order::lit|tle;}", "std::memory::byte_order = 0"),
				("memory_local_hover", memory_setup + "print(b|);}", "std::memory::buffer"),
				("memory_method_hover", memory_setup + "b.sli|ce(0,4);}", "std::memory::view"),
				("memory_chain_hover", memory_setup + "b.slice(0,4).readonly_sli|ce(0,2);}", "std::memory::readonly_view"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/hover", params)
				assert response["result"], (name, response)
				contents = json.dumps(response["result"]["contents"], ensure_ascii=False)
				assert expected in contents and "std.memory" not in contents, (name, response)
				assert "standard type alias of" not in contents, (name, response)
				assert contents.count("Built-in symbol.") <= 1, (name, response)
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/hover", "request": params,
								"response": response})

			# Public macros use the core module timeline, including unsaved
			# imports. Definition sites are physical even for type macros.
			macro_library = ('#define PROLOGUE \\\nvoid unused() {}\nPROLOGUE\n'
							 '/// Published macro.\n#define_export PUBLIC_VALUE 42\n'
							 '#define PRIVATE_VALUE 7\n#define_export PUBLIC_TYPE int\n'
							 '#define_export PUBLIC_TWICE(x) ((x)*2)\n')
			macro_library_uri = open_document("public_macro_library", macro_library)
			facade = 'export import "public_macro_library.vs";\n'
			facade_uri = open_document("public_macro_facade", facade)
			private_facade = 'import "public_macro_library.vs";\n'
			private_facade_uri = open_document("private_macro_facade", private_facade)
			macro_documents = [{"uri": macro_library_uri, "text": macro_library},
							   {"uri": facade_uri, "text": facade},
							   {"uri": private_facade_uri, "text": private_facade}]
			public_names = {"PUBLIC_VALUE", "PUBLIC_TYPE", "PUBLIC_TWICE"}
			for name, marked, expected in (
				("macro_after_same_line_import", 'import "public_macro_library.vs";void main(){PUB|;}', public_names),
				("macro_before_import", 'void main(){PUB|;}import "public_macro_library.vs";', set()),
				("macro_private_hidden", 'import "public_macro_library.vs";void main(){PRIVATE_|;}', set()),
				("macro_reexport", 'import "public_macro_facade.vs";void main(){PUB|;}', public_names),
				("macro_private_import_not_reexported", 'import "private_macro_facade.vs";void main(){PUB|;}', set()),
				("macro_undef", 'import "public_macro_library.vs";\n#undef PUBLIC_VALUE\nvoid main(){PUBLIC_V|;}', set()),
				("macro_undef_reimport", 'import "public_macro_library.vs";\n#undef PUBLIC_VALUE\nimport "public_macro_library.vs";void main(){PUBLIC_V|;}', {"PUBLIC_VALUE"}),
				("macro_imported_preprocessor_completion", 'import "public_macro_library.vs";\n#ifdef PUBLIC_V|\n#endif\n', {"PUBLIC_VALUE"}),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				macros = [item for item in items(response)
						  if item["label"].startswith(("PUBLIC_", "PRIVATE_"))]
				assert {item["label"] for item in macros} == expected, (name, response)
				assert len(macros) == len(expected), (name, macros)
				for item in macros:
					assert item["detail"].startswith("#define_export " + item["label"]), item
					assert item["textEdit"]["newText"] == item["insertText"], item
					prefix_start = source.rfind("PUBLIC_", 0, utf16_offset(source, position))
					if prefix_start < 0:
						prefix_start = source.rfind("PUB", 0, utf16_offset(source, position))
					assert utf16_offset(source, item["textEdit"]["range"]["start"]) == prefix_start, item
					assert item["textEdit"]["range"]["end"] == position, item
				records.append({"name": name, "documents": macro_documents + [{"uri": uri, "text": source}],
								"document": source, "position": position,
								"method": "textDocument/completion", "request": params, "response": response})

			for name, marked, target_uri, target_line, target_character in (
				("macro_imported_type_definition", 'import "public_macro_library.vs";void main(){PUBLIC_T|YPE x=1;}', macro_library_uri, 6, 15),
				("macro_local_type_definition", '#define PROLOGUE \\\nvoid unused() {}\nPROLOGUE\n#define VALUE_TYPE int\nvoid main(){VALUE_T|YPE x=1;}', None, 3, 8),
				("generic_after_continued_macro_definition", '#define PROLOGUE \\\nvoid unused() {}\nPROLOGUE\ntemplate<T>\nclass Box {public: T| value;}\nvoid main(){Box<int> b=Box<int>();}', None, 3, 9),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/definition", params)
				target = {"line": target_line, "character": target_character}
				assert response["result"] == [{"uri": target_uri or uri,
						"range": {"start": target, "end": target}}], (name, response)
				records.append({"name": name, "documents": macro_documents + [{"uri": uri, "text": source}],
								"document": source, "position": position,
								"method": "textDocument/definition", "request": params, "response": response})

			source, position = marked_source('import "public_macro_library.vs";void main(){print(PUBLIC_V|ALUE);}')
			uri = open_document("macro_hover_cache", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			for version, value in ((1, 42), (2, 89)):
				current_library = macro_library.replace("PUBLIC_VALUE 42", f"PUBLIC_VALUE {value}")
				if version > 1:
					send("textDocument/didChange", {"textDocument": {"uri": macro_library_uri, "version": version},
						"contentChanges": [{"text": current_library}]}, notification=True)
				response = send("textDocument/hover", params)
				contents = response["result"]["contents"]["value"]
				assert f"#define_export PUBLIC_VALUE {value}" in contents, response
				assert "Published macro." in contents, response
				full = send("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
				assert full["result"]["data"], full
				cached = send("textDocument/hover", params)
				assert cached["result"] == response["result"], cached
				records.append({"name": f"macro_hover_cache_v{version}",
								"documents": [{"uri": uri, "text": source}, {"uri": macro_library_uri, "text": current_library}],
								"document": source, "position": position,
								"method": "textDocument/hover", "request": params, "response": response,
								"semanticResponse": full, "cachedResponse": cached})

			unrelated_macro = '#define_export PUBLIC_VALUE 7\nvoid main(){print(PUBLIC_VALUE);}'
			unrelated_macro_uri = open_document("macro_unrelated_identity", unrelated_macro)
			params = {"textDocument": {"uri": macro_library_uri},
					  "position": {"line": 4, "character": 15},
					  "context": {"includeDeclaration": True}}
			response = send("textDocument/references", params)
			expected_sites = {(macro_library_uri, 4, 15),
							  (uri, 0, source.index("PUBLIC_VALUE"))}
			actual_sites = {(location["uri"], location["range"]["start"]["line"],
							 location["range"]["start"]["character"])
							for location in response["result"]}
			assert actual_sites == expected_sites and len(response["result"]) == 2, response
			records.append({"name": "macro_imported_references_identity",
							"documents": [{"uri": uri, "text": source},
										  {"uri": macro_library_uri, "text": current_library},
										  {"uri": unrelated_macro_uri, "text": unrelated_macro}],
							"method": "textDocument/references", "request": params, "response": response})

			send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"implicitImportPaths": [str((Path(directory) / "public_macro_library.vs").resolve())],
			}}}, notification=True)
			source, position = marked_source('void main(){PUB|;}')
			uri = open_document("macro_implicit_import_visible", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/completion", params)
			assert {item["label"] for item in items(response)
					if item["label"].startswith("PUBLIC_")} == public_names, response
			records.append({"name": "macro_implicit_import_visible", "configuration": {
								"implicitImportPaths": [str((Path(directory) / "public_macro_library.vs").resolve())]},
							"documents": [{"uri": macro_library_uri, "text": current_library},
										  {"uri": uri, "text": source}],
							"document": source, "position": position,
							"method": "textDocument/completion", "request": params, "response": response})
			send("workspace/didChangeConfiguration", {"settings": {"veroscript": {
				"implicitImportPaths": [],
			}}}, notification=True)

			conditional_library = ('import "public_macro_library.vs";\n#if PUBLIC_VALUE == 89\n'
								   '#define INTRO \\\nint helper(){return 0;}\nINTRO\n'
								   '/// Conditional module function.\n'
								   'export PUBLIC_TYPE read_public(){return PUBLIC_VALUE;}\n#endif\n')
			conditional_uri = open_document("macro_conditional_library", conditional_library)
			source, position = marked_source('import "macro_conditional_library.vs";void main(){print(read_p|ublic());}')
			uri = open_document("macro_conditional_function_hover", source)
			params = {"textDocument": {"uri": uri}, "position": position}
			response = send("textDocument/hover", params)
			contents = response["result"]["contents"]["value"]
			assert "int()" in contents and "Conditional module function." in contents, response
			assert "macro_conditional_library.vs:7" in contents, response
			records.append({"name": "macro_conditional_function_hover",
							"documents": [{"uri": uri, "text": source},
										  {"uri": conditional_uri, "text": conditional_library},
										  {"uri": macro_library_uri, "text": current_library}],
							"document": source, "position": position,
							"method": "textDocument/hover", "request": params, "response": response})

			definition = send("textDocument/definition", params)
			assert len(definition["result"]) == 1, definition
			assert definition["result"][0]["uri"] == conditional_uri, definition
			assert definition["result"][0]["range"]["start"]["line"] == 6, definition
			records.append({"name": "macro_conditional_function_definition",
							"documents": [{"uri": uri, "text": source},
										  {"uri": conditional_uri, "text": conditional_library},
										  {"uri": macro_library_uri, "text": current_library}],
							"document": source, "position": position,
							"method": "textDocument/definition", "request": params, "response": definition})

			reference_params = {**params, "context": {"includeDeclaration": True}}
			references = send("textDocument/references", reference_params)
			sites = {(location["uri"], location["range"]["start"]["line"],
					  location["range"]["start"]["character"])
					 for location in references["result"]}
			assert sites == {(conditional_uri, 6, 19), (uri, 0, source.index("read_public"))}, references
			assert len(references["result"]) == 2, references
			records.append({"name": "macro_conditional_function_references",
							"documents": [{"uri": uri, "text": source},
										  {"uri": conditional_uri, "text": conditional_library},
										  {"uri": macro_library_uri, "text": current_library}],
							"document": source, "position": position,
							"method": "textDocument/references", "request": reference_params, "response": references})

			# The shared import/macro scenarios above are complete. Navigation
			# fixtures below are independent; retaining their unrelated overlays
			# would reanalyze every old document on each stdlib reconfiguration.
			for previous_uri in sorted(open_uris):
				send("textDocument/didClose", {"textDocument": {"uri": previous_uri}}, notification=True)
			isolated_review_documents = []

			stdlib_root = Path(directory) / "模块-😀" / "stdlib"
			stdlib_root.mkdir(parents=True)
			module_path = stdlib_root / "algorithm.vs"
			module_path.write_text("// standard namespace module\n", encoding="utf-8")
			for setting in ("stdlibSearchPaths", "stdlibPath"):
				for root in (stdlib_root.parent, stdlib_root):
					name = "definition_" + setting + ("_parent" if root != stdlib_root else "_direct")
					configuration = {"importPaths": [], setting: [str(root)]
									 if setting == "stdlibSearchPaths" else str(root)}
					send("workspace/didChangeConfiguration", {"settings": {
						"veroscript": configuration,
					}}, notification=True)
					source, position = marked_source("void main() { std::alg|orithm::sort(); }\n")
					uri = open_document(name, source)
					params = {"textDocument": {"uri": uri}, "position": position}
					response = send("textDocument/definition", params)
					assert response["result"] == [{"uri": module_path.as_uri(), "range": {
						"start": {"line": 0, "character": 0},
						"end": {"line": 0, "character": 0},
					}}], (name, response)
					records.append({"name": name, "configuration": configuration,
									"document": source, "position": position,
									"method": "textDocument/definition", "request": params,
									"response": response})

			override_root = Path(directory) / "优先-😀" / "stdlib"
			override_root.mkdir(parents=True)
			override_module = override_root / "algorithm.vs"
			override_module.write_text("// preferred standard namespace module\n", encoding="utf-8")
			for name, first in (("definition_repeated_root", str(stdlib_root)),
								("definition_normalized_repeated_root", str(stdlib_root) + "/.")):
				configuration = {"importPaths": [], "stdlibSearchPaths": [
					first, str(override_root), str(stdlib_root),
				]}
				send("workspace/didChangeConfiguration", {"settings": {
					"veroscript": configuration,
				}}, notification=True)
				source, position = marked_source("void main() { std::alg|orithm::sort(); }\n")
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/definition", params)
				assert response["result"] == [{"uri": override_module.as_uri(), "range": {
					"start": {"line": 0, "character": 0},
					"end": {"line": 0, "character": 0},
				}}], (name, response)
				records.append({"name": name, "configuration": configuration,
								"document": source, "position": position,
								"method": "textDocument/definition", "request": params,
								"response": response})

			send("shutdown", {})
			send("exit", {}, notification=True)
			process.stdin.close()
			process.wait(timeout=10)
			assert process.returncode == 0, process.stderr.read().decode(errors="replace")

		# A separate client without snippet support must keep required namespaces.
		with subprocess.Popen(
			[str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
		) as process:
			send("initialize", {"locale": "en-US", "capabilities": {}})
			send("initialized", {}, notification=True)
			for name, marked, label, insertion in (
				("memory_no_snippet", "using namespace std::memory;void main(){cl|;}", "clone", "std::memory::clone"),
				("memory_read_no_snippet", "using namespace std::memory;void main(){re|;}", "read", "std::memory::read"),
				("chrono_no_snippet", "using namespace std::chrono;void main(){no|;}", "now", "std::chrono::now"),
			):
				source, position = marked_source(marked)
				uri = open_document(name, source)
				params = {"textDocument": {"uri": uri}, "position": position}
				response = send("textDocument/completion", params)
				item = item_with_label(response, label)
				assert item["insertText"] == insertion and item.get("insertTextFormat", 1) == 1, item
				assert item["textEdit"]["newText"] == insertion, item
				resolved = send("completionItem/resolve", item)
				assert resolved["result"]["textEdit"] == item["textEdit"], resolved
				records.append({"name": name, "document": source, "position": position,
								"method": "textDocument/completion", "capabilities": {},
								"request": params, "response": response, "resolved": resolved})
			send("shutdown", {})
			send("exit", {}, notification=True)
			process.stdin.close()
			process.wait(timeout=10)
			assert process.returncode == 0, process.stderr.read().decode(errors="replace")

	evidence = {"executable": str(executable.resolve()),
				"sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
				"serverInfo": initialize["result"].get("serverInfo"),
				"cases": records}
	destination.parent.mkdir(parents=True, exist_ok=True)
	destination.write_text(json.dumps(evidence, ensure_ascii=False, indent=2),
						   encoding="utf-8")
	print(f"{len(records)} LSP interaction cases passed; evidence: {destination}")


if __name__ == "__main__":
	try:
		main()
	except Exception:
		# Preserve the complete document notification/request timeline even if
		# a regression assertion fails before its case record is appended.
		failure_path = Path(sys.argv[2]).with_suffix(".failure.json")
		failure_path.parent.mkdir(parents=True, exist_ok=True)
		failure_path.write_text(json.dumps({"executable": str(Path(sys.argv[1]).resolve()),
			"sha256": hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest(),
			"frames": wire_trace}, ensure_ascii=False, indent=2), encoding="utf-8")
		raise
