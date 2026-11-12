#!/usr/bin/env python3
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def frame(message):
	body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
	return b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body


def read_frame(stream):
	headers = {}
	while line := stream.readline():
		if line == b"\r\n":
			break
		key, value = line.decode().strip().split(":", 1)
		headers[key.lower()] = value.strip()
	return json.loads(stream.read(int(headers["content-length"])))


def main():
	exe, destination = sys.argv[1:]
	cases = [
		("scope_if", "void main(){ int local=1; if(loc|"),
		("scope_if_body", "void main(){ int local=1; if(loc|) }"),
		("scope_while", "void main(){ int local=1; while(loc|"),
		("scope_return", "int main(){ int local=1; return loc|"),
		("scope_expression", "void main(){ int local=1; loc|"),
		("scope_call", "void main(){ int local=1; print(loc|"),
		("scope_index", "void main(){ int local=1; array<int> values={}; values[loc|"),
		("scope_initializer", "void main(){ int local=1; int next=loc|"),
		("scope_for_initializer", "void main(){ int local=1; for(int i=loc|"),
		("scope_for_body", "void main(){ for(int i=0;i<2;++i){ i|"),
		("scope_for_complete", "void main(){ for(int i=0;i<2;++i){ i|; } }"),
		("scope_comparison", "void main(){ int local=1; if(local<loc|){} }"),
		("scope_comparison_global", "int local=1; void main(){ if(local<loc|"),
		("scope_comparison_namespace", "namespace N{ int value=1; } void main(){ int local=1; if(N::value<loc|"),
		("scope_comparison_field", "class C{ int value=1; void test(int local){ if(this.value<loc|"),
		("scope_comparison_chain", "void main(){ int local=1; if(local<2>loc|"),
		("scope_for_ternary", "void main(){ int local=1; for(true?0:loc|"),
		("scope_for_expression_lambda", "void main(){ for(int i=0;[](int param)->int=>par|"),
		("scope_for_expression_lambda_close", "void main(){ for(int i=0;[](int param)->int=>par|){} }"),
		("scope_for_expression_lambda_nested", "void main(){ for(int i=0;true?([](int param)->int=>par|):0){} }"),
		("scope_for_expression_lambda_step", "void main(){ for(int i=0;i<1;[](int param)->int=>par|"),
		("scope_for_expression_lambda_before_error", "void main(){ int before=0\nfor(int i=0;[](int param)->int=>par|"),
		("scope_for_expression_lambda_after_error", "void main(){ for(int i=0;[](int param)->int=>par|){} }\nvoid bad( {"),
		("scope_for_expression_lambda_crlf", "void main(){\r\nfor(int i=0;[](int param)->int=>par|"),
		("scope_for_expression_lambda_utf16", 'void main(){ string text="\U0001f600"; for(int i=0;[](int param)->int=>par|'),
		("scope_for_expression_lambda_outside", "void main(){ for(int i=0;[](int param)->int=>param;par|){} }"),
		("member_lambda_for", "void main(){ for(int i=0;[](array<int> items)->int=>items.pu|"),
		("scope_array_initializer", "void main(){ int local=1; array<int> arr={loc|"),
		("scope_array_next", "void main(){ int local=1; array<int> arr={0,loc|"),
		("scope_array_nested", "void main(){ int local=1; array<array<int>> arr={{loc|"),
		("scope_array_for", "void main(){ int local=1; for(array<int> arr={loc|"),
		("scope_array_before_error", "void main(){ int before=0\nint local=1; array<int> arr={loc|"),
		("scope_array_after_error", "void main(){ int local=1; array<int> arr={loc|}; }\nvoid bad( {"),
		("scope_array_crlf", "void main(){\r\nint local=1;\r\narray<int> arr={loc|"),
		("scope_array_utf16", 'void main(){ string text="\U0001f600"; int local=1; array<int> arr={loc|'),
		("scope_map_nested", 'void main(){ int local=1; map<string,array<int>> data={"x":{loc|'),
		("scope_map_key", "void main(){ int local=1; map<int,int> data={loc|:1}; }"),
		("scope_for_expression_lambda_call", "void main(){ for(int i=0;print([](int param)->int=>par|"),
		("scope_expression_lambda_outside", "void main(){ auto f=[](int param)->int=>param; par|"),
		("type_qualified_generic", "namespace N{ class Box<T>{} } void main(){ int local=1; N::Box<in|"),
		("type_unqualified_generic", "class Box<T>{} void main(){ int local=1; Box<in|"),
		("scope_comparison_suffix", "void main(){ int local=1; if(local<loc|al){} }"),
		("member_lambda_end", "void main(){ auto f=[](array<int> items)->int{return items.pu|;}; }"),
		("member_lambda_suffix", "void main(){ auto f=[](array<int> items)->int{return items.pu|sh();}; }"),
		("member_lambda_incomplete", "void main(){ auto f=[](array<int> items)->int{return items.pu|"),
		("scope_block_outside", "void main(){ {int local=1;} loc|"),
		("scope_later_declaration", "void main(){ loc|; int local=1; }"),
		("scope_nested", "void main(){ int local=1; if(true){ loc|"),
		("scope_do", "void main(){ int local=1; do{ loc|"),
		("scope_try", "void main(){ int local=1; try{ loc|"),
		("scope_lambda", "void main(){ auto f=[](int param)->int{return par|"),
		("scope_lambda_complete", "void main(){ auto f=[](int param)->int{return par|;}; }"),
		("scope_range", "void main(){ array<int> values={}; for(int value:val|"),
		("scope_range_shadow", "void main(){ array<int> value={}; for(int value:val|ue){} }"),
		("scope_lambda_outside", "void main(){ auto f=[](int param)->int{return param;}; par|"),
		("scope_loop_outside", "void main(){ for(int i=0;i<2;++i){} i|"),
		("scope_after_error", "void main(){ int local=1; loc|; }\nvoid bad( {"),
		("scope_before_error", "void main(){ int before=0\nint local=1; print(loc|"),
		("scope_crlf", "void main(){\r\nint local=1;\r\nprint(loc|"),
		("scope_utf16", 'void main(){ string text="\U0001f600"; int local=1; print(loc|'),
		("scope_token_suffix", "void main(){ int local=1; print(lo|cal"),
		("literal_string_end", 'void main(){ print("hello"|); }'),
		("literal_raw_end", 'void main(){ print(r"hello"|); }'),
		("literal_wide_end", 'void main(){ print(L"hello"|); }'),
		("literal_char_end", "void main(){ char c='a'|; }"),
		("literal_true_end", "void main(){ bool c=true|; }"),
		("literal_false_end", "void main(){ bool c=false|; }"),
		("literal_null_end", "void main(){ auto c=nullptr|; }"),
		("literal_number_space", "void main(){ int c=0 |; }"),
		("literal_comment_gap", "void main(){ int c=0 /* note */ |; }"),
		("literal_before_error", 'void bad( {\nvoid main(){ print("hello"|); }'),
		("literal_true_partial", "void main(){ bool c=tr|ue; }"),
		("literal_null_partial", "void main(){ auto c=nu|llptr; }"),
		("literal_operator", "void main(){ int local=1; int c=0+loc|"),
		("literal_statement", "void main(){ int local=1; bool c=true; loc|"),
		("number_manual", "void main(){ int i=0|; }"),
		("number_auto", "void main(){ int i=0|; }"),
		("number_inside", "void main(){ int i=12|34; }"),
		("number_exponent", "void main(){ double d=1e+|2; }"),
		("number_incomplete", "void main(){ int i=0x|"),
		("number_before_error", "void bad( {\nvoid main(){ int i=0|; }"),
		("number_after_error", "void main(){ int i=0|; }\nvoid bad( {"),
		("after_number_operator", "void main(){ int item=1; int i=0+|item; }"),
		("after_number_semicolon", "void main(){ for(int i=0;|i<2;++i) {} }"),
		("after_number_identifier", "void main(){ int item0=1; int i=0+item0|; }"),
		("for_condition", "void main(){ for(int i=0;i|) }"),
		("for_condition_auto", "void main(){ for(int i=0;i|) }"),
		("for_no_close", "void main(){ for(int i=0;i|"),
		("for_empty_condition", "void main(){ for(int i=0;|"),
		("for_step", "void main(){ for(int i=0;i<2;i|) }"),
		("for_call", "void main(){ for(int i=0;abs(i|)) }"),
		("for_before_error", "void main(){ int before=1\nfor(int i=0;i|) {} }"),
		("for_after_error", "void main(){ for(int i=0;i|) {} }\nvoid bad( {"),
		("for_crlf", "void main(){\r\nfor(int i=0;\r\ni|) {}\r\n}"),
		("for_utf16", 'void main(){ string text="\U0001f600"; for(int i=0;i|) {} }'),
		("for_identifier_suffix", "void main(){ for(int iteration=0;ite|ration) {} }"),
		("for_shadow", 'void main(){ string i="outer"; for(int i=0;i|) {} }'),
		("outside_for", "void main(){ for(int i=0;i<2;++i) {} i|; }"),
		("string", 'void main() { print("s|"); }'),
		("string_auto", 'void main() { print("s|"); }'),
		("unclosed_string", 'void main() { print("s|'),
		("raw_string", 'void main() { print(r"/* false| "); }'),
		("multiline_raw", 'void main() { print(r"first\nfalse|"); }'),
		("char", "void main() { char c = 's|'; }"),
		("line_comment", "void main() { // fal|\n}"),
		("block_comment", "void main() { /* fal| */ }"),
		("unclosed_comment", "void main() { /* fal|"),
		("fake_import_comment", '/*\nimport "stdlib/ch|"\n*/'),
		("class", "class A|"),
		("class_std_prefix", "class st|"),
		("struct", "struct A|"),
		("struct_std_prefix", "struct st|"),
		("namespace", "namespace A|"),
		("namespace_std_prefix", "namespace st|"),
		("interface", "interface A|"),
		("enum", "enum A|"),
		("class_multiline", "class\nA|"),
		("namespace_multiline", "namespace\nA|"),
		("type_multiline", "void main() { int\nA|; }"),
		("generic_type_multiline", "void main() { array<int>\nA|; }"),
		("named_type_multiline", "class Foo {}\nvoid main() { Foo\nA|; }"),
		("named_type_multiline_before_error", "void bad( {\nclass Foo {}\nvoid main() { Foo\nA|; }"),
		("alias_type_multiline", "using Alias = int;\nvoid main() { Alias\nA|; }"),
		("macro_after_identifier", "#define ANSWER 42\nvoid main() {\nMA\nANS|\n}"),
		("class_after_function", "void done() {}\nclass A|"),
		("interface_before_error", "void bad( {\ninterface A|"),
		("enum_after_error", "enum A|\nvoid bad( {"),
		("var", "void main() { int A|; }"),
		("var_std_prefix", "void main() { int st|; }"),
		("named_type_var", "class Foo {}\nvoid main() { Foo A|; }"),
		("unfinished_var", "void main() { int A|"),
		("qualified_typo", "std::chono|"),
		("qualified_comment_gap", "std:: /* gap */ chono|"),
		("qualified_comment_gap_before_error", "void bad( {\nstd:: /* gap */ chono|"),
		("qualified_newline", "std::\nchono|"),
		("qualified_newline_before_error", "void bad( {\nstd::\nchono|"),
		("qualified_empty_newline", "std::\n|"),
		("qualified_after_semicolon", "std::chono;|"),
		("qualified_after_multiline_semicolon", "std::\nchono;|"),
		("qualified_valid", 'void main() { std::chrono::| }'),
		("import", 'import "hel|";'),
		("include", '#include "hel|"'),
		("string_before_error", 'void bad( {\nvoid main() { print("s|"); }'),
		("string_after_error", 'void main() { print("s|"); }\nvoid bad( {'),
		("comment_before_error", 'void bad( {\nvoid main() { /* fal| */ }'),
		("unclosed_after_error", 'void main() { print("s|\nvoid bad( {'),
		("ambiguous_after_lex_error", 'void bad() { print("oops\npri|'),
		("class_before_error", 'void bad( {\nclass A|'),
		("class_after_error", 'class A|\nvoid bad( {'),
		("qualified_before_error", 'void bad( {\nstd::chono|'),
		("qualified_after_error", 'std::chono|\nvoid bad( {'),
		("qualified_valid_before_error", 'void bad( {\nvoid main() { std::chrono::| }'),
		("qualified_valid_after_error", 'void main() { std::chrono::| }\nvoid bad( {'),
		("qualified_valid_unclosed_before", 'void bad() { print("oops\nvoid main() { std::chrono::| }'),
		("unknown_member", 'void main() { unknown.me| }'),
		("unknown_member_before_error", 'void bad( {\nvoid main() { unknown.me| }'),
		("unknown_qualified", 'unknown::me|'),
		("unknown_qualified_comment_gap", 'unknown:: /* gap */ me|'),
		("unknown_qualified_newline", 'unknown::\nme|'),
		("unknown_qualified_before_error", 'void bad( {\nunknown::me|'),
		("unknown_arrow", 'void main() { unknown->me| }'),
		("shadowed_std_dot", 'void main() { array<int> std; std.| }'),
		("shadowed_std_dot_before_error", 'void bad( {\nvoid main() { array<int> std; std.| }'),
		("code_before_error", 'void bad( {\nvoid main() { ret|; }'),
		("code_after_error", 'void main() { ret|; }\nvoid bad( {'),
		("print_call", 'void main() { pri|; }'),
		("print_full_prefix", 'void main() { print|; }'),
		("print_empty_prefix", 'void main() { | }'),
		("print_reference", 'void main() { auto callback = pri|; }'),
		("print_existing_paren", 'void main() { pri|(); }'),
		("print_existing_paren_suffix", 'void main() { pri|nt(); }'),
		("print_existing_paren_comment", 'void main() { pri| /* note */ (); }'),
	]
	fixture_root = Path(tempfile.mkdtemp(prefix="veroscript-completion-evidence-"))
	(fixture_root / "helper.vs").write_text("void helper() {}\n", encoding="utf-8")
	process = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	def send(message):
		process.stdin.write(frame(message))
		process.stdin.flush()
	send({"jsonrpc":"2.0","id":1,"method":"initialize","params":{
		"capabilities":{"textDocument":{"completion":{"completionItem":{
			"snippetSupport":True}}}}}})
	initialize = read_frame(process.stdout)
	send({"jsonrpc":"2.0","method":"initialized","params":{}})
	records = []
	for index, (name, marked) in enumerate(cases, 2):
		offset = marked.index("|")
		document = marked[:offset] + marked[offset + 1:]
		before = marked[:offset]
		position = {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}
		uri = (fixture_root / f"{name}.vs").resolve().as_uri()
		send({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":uri,"languageId":"veroscript","version":1,"text":document}}})
		params = {"textDocument":{"uri":uri},"position":position}
		if name == "string_auto":
			params["context"] = {"triggerKind":2,"triggerCharacter":"s"}
		elif name == "number_auto":
			params["context"] = {"triggerKind":2,"triggerCharacter":"0"}
		elif name.endswith("_auto"):
			params["context"] = {"triggerKind":3}
		else:
			params["context"] = {"triggerKind":1}
		send({"jsonrpc":"2.0","id":index,"method":"textDocument/completion","params":params})
		notifications = []
		while True:
			response = read_frame(process.stdout)
			if response.get("id") == index:
				break
			notifications.append(response)
		records.append({"name":name,"document":document,"position":position,"request":params,"response":response,"notifications":notifications})
	send({"jsonrpc":"2.0","id":999,"method":"shutdown","params":{}})
	read_frame(process.stdout)
	send({"jsonrpc":"2.0","method":"exit","params":{}})
	process.stdin.close()
	evidence = {
		"executable": str(Path(exe).resolve()),
		"sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
		"serverInfo": initialize["result"]["serverInfo"],
		"cases": records,
	}
	Path(destination).write_text(
		json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8"
	)
	for record in records:
		response = record["response"]
		assert "error" not in response and isinstance(response.get("result"), dict), record
		items = response["result"]["items"]
		assert isinstance(items, list), record
		# Validate every returned edit, including candidates other than the one
		# required by each case. LSP positions count UTF-16 units and exclude CRLF.
		for item in items:
			if "textEdit" not in item:
				continue
			edit = item["textEdit"]
			start, end = edit["range"]["start"], edit["range"]["end"]
			position = record["position"]
			assert start["line"] == end["line"] == position["line"], record
			line = record["document"].split("\n")[position["line"]].removesuffix("\r").encode("utf-16-le")
			assert 0 <= start["character"] <= position["character"] <= end["character"] <= len(line) // 2, record
			line[:start["character"] * 2].decode("utf-16-le")
			line[:end["character"] * 2].decode("utf-16-le")
			assert edit["newText"] == item.get("insertText", item["label"]), record
		name = record["name"]
		if name.startswith("scope_") or name in {"literal_true_partial", "literal_null_partial", "literal_operator", "literal_statement"}:
			if name in {"scope_lambda_outside", "scope_loop_outside", "scope_block_outside", "scope_later_declaration", "scope_for_expression_lambda_outside", "scope_expression_lambda_outside"}:
				forbidden = {"scope_lambda_outside":"param", "scope_loop_outside":"i", "scope_for_expression_lambda_outside":"param", "scope_expression_lambda_outside":"param"}.get(name, "local")
				assert all(item["label"] != forbidden for item in items), record
				continue
			label = {
				"scope_for_body":"i", "scope_for_complete":"i",
				"scope_lambda":"param", "scope_lambda_complete":"param",
				"scope_range":"values", "scope_range_shadow":"value",
				"literal_true_partial":"true", "literal_null_partial":"nullptr",
			}.get(name, "local")
			if name.startswith("scope_for_expression_lambda"):
				label = "param"
			item = next(item for item in items if item["label"] == label)
			edit = item["textEdit"]
			start, end = edit["range"]["start"], edit["range"]["end"]
			assert start["line"] == end["line"] == record["position"]["line"], record
			line = record["document"].split("\n")[start["line"]].encode("utf-16-le")
			replaced = line[start["character"] * 2:end["character"] * 2].decode("utf-16-le")
			assert replaced and label.startswith(replaced), record
			assert edit["newText"] == item["insertText"] == label, record
			if name == "scope_range":
				assert all(item["label"] != "value" for item in items), record
			if name == "scope_range_shadow":
				assert item["detail"] == "array<int>", record
		elif name in {"type_qualified_generic", "type_unqualified_generic"}:
			assert any(item["label"] == "int" for item in items), record
			assert all(item["label"] != "local" for item in items), record
		elif name.startswith("member_lambda_"):
			item = next(item for item in items if item["label"] == "push")
			assert all(item["kind"] in {2, 5} for item in items), record
			edit = item["textEdit"]
			start, end = edit["range"]["start"], edit["range"]["end"]
			line = record["document"].split("\n")[start["line"]].encode("utf-16-le")
			replaced = line[start["character"] * 2:end["character"] * 2].decode("utf-16-le")
			assert replaced == ("push" if name.endswith("suffix") else "pu"), record
			assert edit["newText"] == item["insertText"], record
			assert item["insertText"] == ("push" if name.endswith("suffix") else "push(${1:value})"), record
			assert item.get("insertTextFormat", 1) == (1 if name.endswith("suffix") else 2), record
		elif name.startswith("number_") or name.startswith("literal_"):
			assert not items, record
		elif name.startswith("for_"):
			label = "iteration" if name == "for_identifier_suffix" else "i"
			item = next(item for item in items if item["label"] == label)
			assert item["insertText"] == label, record
			if name != "for_empty_condition":
				edit = item["textEdit"]
				start, end = edit["range"]["start"], edit["range"]["end"]
				line = record["document"].split("\n")[start["line"]]
				assert start["line"] == end["line"] == record["position"]["line"], record
				assert line.encode("utf-16-le")[start["character"] * 2:end["character"] * 2].decode("utf-16-le") == label, record
				assert edit["newText"] == label, record
			if name == "for_shadow":
				assert item["detail"] == "int", record
		elif name == "outside_for":
			assert all(item["label"] != "i" for item in items), record
		elif name.startswith("after_number_"):
			label = {"after_number_operator":"item", "after_number_semicolon":"i", "after_number_identifier":"item0"}[name]
			assert any(item["label"] == label for item in items), record
		print(record["name"], len(items), [item["label"] for item in items[:12]])
	print("serverInfo", initialize["result"]["serverInfo"])
	try:
		process.wait(timeout=2)
	except subprocess.TimeoutExpired:
		process.kill()
		process.wait()
	shutil.rmtree(fixture_root)


if __name__ == "__main__":
	main()
