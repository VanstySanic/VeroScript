# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Check capture scopes and persistent lambda state through actual LSP/DAP."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile

from VeroScriptLspResponsivenessTest import Client
from VeroScriptLspInteractionTests import physical_lsp_position, items, apply_edit
from VeroScriptProtocolReviewTests import Wire, apply_edits

COMPLETION_CAPABILITIES = {'textDocument': {'completion': {'completionItem': {'snippetSupport': True}}}}


def function_capture_cases(client, root, records):
	examples = [
		('qualified', 'namespace N{int make(){return 42;}}void main(){auto f=[callback=N::make]()->int{return callback();};}', 'const fn()->int', 0),
		('unqualified', 'int make(){return 42;}void main(){auto f=[callback=make]()->int{return callback();};}', 'const fn()->int', 0),
		('global_qualified', 'namespace N{namespace Inner{int make(){return 42;}}}void main(){auto f=[callback=::N::Inner::make]()->int{return callback();};}', 'const fn()->int', 0),
		('using_namespace', 'namespace N{namespace Inner{int make(){return 42;}}}using namespace N::Inner;void main(){auto f=[callback=make]()->int{return callback();};}', 'const fn()->int', 0),
		('generic_qualified', 'namespace N{template<typename T>T identity(T value){return value;}}void main(){auto f=[callback=N::identity<int>]()->int{return callback(42);};}', 'const fn(int)->int', 1),
		('generic_unqualified', 'template<typename T>T identity(T value){return value;}void main(){auto f=[callback=identity<int>]()->int{return callback(42);};}', 'const fn(int)->int', 1),
		('parenthesized', 'namespace N{int make(){return 42;}}void main(){auto f=[callback=(N::make)]()->int{return callback();};}', 'const fn()->int', 0),
		('direct_function_value', 'namespace N{int make(){return 42;}}void main(){auto callback=N::make;print(callback());}', 'fn()->int', 0),
		('through_local', 'namespace N{int make(){return 42;}}void main(){auto source=N::make;auto f=[callback=source]()->int{return callback();};}', 'const fn()->int', 0),
		('object_result', 'namespace N{struct Result{int value;};Result make(){return Result();}}void main(){auto f=[callback=N::make]()->N::Result{return callback();};}', 'const fn()->N.Result', 0),
		('object_parameter', 'namespace N{struct Value{int item;};int read(Value value){return value.item;}}void main(){auto f=[callback=N::read](N::Value value)->int{return callback(value);};}', 'const fn(N.Value)->int', 1),
		('reference_signature', 'namespace N{int& alias(int& value){return value;}}void main(){auto f=[callback=N::alias](int& value)->void{int& bound=callback(&value);bound+=1;};}', 'const fn(ref int)->ref int', 1),
		('nested_return', 'namespace N{struct Value{int n;};struct Result{int n;};Result transform(Value value){return Result();}fn(Value)->Result factory(){return transform;}}void main(){auto f=[callback=N::factory]()->void{auto g=callback();};}', 'const fn()->fn(N.Value)->N.Result', 0),
		('nested_parameter_and_return', 'namespace N{struct Value{int n;};struct Result{int n;};Result transform(Value value){return Result();}fn(Value)->Result pass(fn(Value)->Result value){return value;}}void main(){auto f=[callback=N::pass]()->void{auto g=callback(N::transform);};}', 'const fn(fn(N.Value)->N.Result)->fn(N.Value)->N.Result', 1),
		('generic_result', 'namespace N{template<typename T>struct Box{T value;};Box<int> make(){return Box<int>();}}void main(){auto f=[callback=N::make]()->void{auto g=callback();};}', 'const fn()->N.Box<int>', 0),
		('macro_initializer', '#define FACTORY N::make\nnamespace N{int make(){return 1;}}void main(){auto f=[callback=FACTORY]()->int{return callback();};}', 'const fn()->int', 0),
		('multiline_parenthesized', 'namespace N{int make(){return 1;}}\nvoid main(){auto f=[callback=( /* preserve */\n N::make\n)]()->int{return callback();};}', 'const fn()->int', 0),
		('value_shadows_namespace', 'namespace N{int make(){return 42;}}class Receiver{public:fn()->string make=[]()->string{return "field";};}void main(){Receiver N=Receiver();auto f=[callback=N.make]()->string{return callback();};}', 'const fn()->string', 0),
		('global_value_priority', 'namespace N{int make(){return 42;}fn()->string make=[]()->string{return "global";};}void main(){auto f=[callback=N::make]()->string{return callback();};}', 'const fn()->string', 0),
	]
	for suffix in ['prior_error', 'later_error', 'unfinished', 'utf16']:
		name, source, detail, arity = examples[0]
		source = ('int earlier=1\n'+source if suffix == 'prior_error' else
				  source+'\nvoid broken( {' if suffix == 'later_error' else
				  source[:-3] if suffix == 'unfinished' else
				  source.replace('void main(){', 'void main(){print("😀");'))
		examples.append((name+'_'+suffix, source, detail, arity))
	for name, source, detail, arity in examples:
		file_name = ('源码-😀 ' if name in {'object_result', 'object_parameter'} else '')+'function_'+name+'.vs'
		uri = (root/file_name).as_uri()
		client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
		def ask(method, offset):
			params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, offset)}
			response, _ = client.wait(client.request('textDocument/'+method, params))
			records.append(dict(name='function_'+name, document=source, method='textDocument/'+method, request=params, response=response))
			assert 'error' not in response, response
			return response
		declaration, use = source.index('callback'), source.rindex('callback')
		for offset in [declaration+2, use+2]:
			response = ask('hover', offset)
			# Prefix recovery at a declaration can end before its initializer.
			# An unknown declaration hover is safe; recovered use sites below
			# must retain their callable type even with a broken suffix.
			if offset == declaration+2 and name.endswith(('later_error', 'unfinished')) and response['result'] is None:
				continue
			assert f'```veroscript\n{detail}\n```' in response['result']['contents']['value'], response
		response = ask('completion', use+3)
		candidate = next(item for item in items(response) if item['label'] == 'callback')
		assert candidate['detail'] == detail and candidate['textEdit']['newText'] == 'callback', candidate
		assert apply_edit(source, candidate['textEdit']) == source, candidate
		response = ask('definition', use+2)
		assert len(response['result']) == 1 and response['result'][0]['uri'] == uri, response
		assert response['result'][0]['range']['start'] == physical_lsp_position(source, declaration), response
		response = ask('signatureHelp', use+len('callback('))
		result = response['result']
		assert result and len(result['signatures']) == 1, response
		assert len(result['signatures'][0].get('parameters', [])) == arity and result['activeParameter'] == 0, response
		assert result['signatures'][0]['label'].replace('::', '.') == detail.removeprefix('const ').replace('::', '.'), response
		if name in {'qualified', 'unqualified', 'generic_qualified', 'generic_unqualified', 'parenthesized'}:
			token = 'identity' if name.startswith('generic') else 'make'
			initializer = source.index(token, declaration)
			response = ask('completion', initializer+2)
			candidate = next(item for item in items(response) if item['label'] == token)
			assert candidate['insertText'] == candidate['textEdit']['newText'] == token, candidate
			assert apply_edit(source, candidate['textEdit']) == source, candidate
		client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

	source = 'namespace N{struct Result{int value;};Result make(){return Result();}}void main(){auto f=[callback=N::make]()->int{return callback().value;};}'
	uri = (root/'function_result_member.vs').as_uri()
	client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
	params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, source.rindex('value')+2)}
	response, _ = client.wait(client.request('textDocument/completion', params))
	records.append(dict(name='function_result_member', document=source, method='textDocument/completion', request=params, response=response))
	candidate = next(item for item in items(response) if item['label'] == 'value')
	assert apply_edit(source, candidate['textEdit']) == source, candidate
	client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

	for name, source in [
		('ambiguous', 'namespace N{int make(){return 1;}int make(int value){return value;}}void main(){auto f=[callback=N::make]()->int{return callback();};}'),
		('deleted', 'namespace N{int make()=delete;}void main(){auto f=[callback=N::make]()->int{return callback();};}'),
		('later_using', 'namespace N{int make(){return 1;}}void main(){auto f=[callback=make]()->int{return callback();};}using namespace N;'),
	]:
		uri = (root/('function_'+name+'.vs')).as_uri()
		client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
		params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, source.rindex('callback')+2)}
		response, _ = client.wait(client.request('textDocument/hover', params))
		records.append(dict(name='function_'+name, document=source, method='textDocument/hover', request=params, response=response))
		assert 'error' not in response and '```veroscript\nconst auto\n```' in response['result']['contents']['value'], response
		client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

	uri = (root/'function_changed.vs').as_uri()
	for version, type_name in enumerate(['float', 'int64', 'float'], 1):
		source = 'namespace N{'+type_name+' make(){return 1;}}void main(){auto f=[callback=N::make]()->'+type_name+'{return callback();};}'
		if version == 1:
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': version, 'text': source}})
		else:
			client.send('textDocument/didChange', {'textDocument': {'uri': uri, 'version': version}, 'contentChanges': [{'text': source}]})
		for method in ['semanticTokens/full', 'hover']:
			params = {'textDocument': {'uri': uri}}
			if method == 'hover':
				params['position'] = physical_lsp_position(source, source.rindex('callback')+2)
			response, _ = client.wait(client.request('textDocument/'+method, params))
			records.append(dict(name='function_changed_'+str(version), document=source, method='textDocument/'+method, request=params, response=response))
			assert 'error' not in response, response
			if method == 'hover':
				assert f'```veroscript\nconst fn()->{type_name}\n```' in response['result']['contents']['value'], response
			else:
				assert response['result']['data'], response
	client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

	source = 'namespace N{int make(){return 1;}}void main(){auto f=[callback=N::make]()->int{return '+'+'.join(['callback()']*48)+';};}'
	uri = (root/'dense_function_capture.vs').as_uri()
	client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
	params = {'textDocument': {'uri': uri}}
	response, elapsed = client.wait(client.request('textDocument/semanticTokens/full', params))
	records.append(dict(name='dense_function_capture', document=source, method='textDocument/semanticTokens/full', request=params, response=response, elapsedSeconds=elapsed))
	assert 'error' not in response and response['result']['data'] and elapsed < 2.0, (response, elapsed)
	client.send('textDocument/didClose', {'textDocument': {'uri': uri}})


def lsp_cases(executable, root, records):
	client = Client(str(executable), root)
	try:
		initialized, _ = client.wait(client.request('initialize', {'locale': 'en-US', 'capabilities': COMPLETION_CAPABILITIES}))
		client.send('initialized')
		examples = [
			('literal', 'void main(){auto f=[count=0]()->int{return count;};}', 'return count', 'count', 'const int'),
			('mutable', 'void main(){auto f=[count=0]()mutable->int{return ++count;};}', '++count', 'count', 'int'),
			('reference_const', 'void main(){const int outer=7;auto f=[&alias=outer]()->int{return alias;};}', 'return alias', 'alias', 'const int&'),
			('reference', 'void main(){int outer=7;auto f=[&alias=outer]()->int{return alias;};}', 'return alias', 'alias', 'int&'),
			('copy_const', 'void main(){const int outer=7;auto f=[copy=outer]()mutable->int{return ++copy;};}', '++copy', 'copy', 'int'),
			('ordinary_const', 'void main(){const int outer=7;auto f=[outer]()mutable->int{return outer;};}', 'return outer', 'outer', 'const int'),
			('captureless_static', 'void main(){static int count=1;auto f=[]()->int{return count;};}', 'return count', 'count', 'int'),
			('initializer_outer_scope', 'void main(){string value="outer";auto f=[value=7,copy=value]()->string{return copy;};}', 'copy=value', 'value', 'string'),
			('explicit', 'void main(){int outer=7;auto f=[outer]()->int{return outer;};}', 'return outer', 'outer', 'const int'),
			('default_reference', 'void main(){int outer=7;auto f=[&]()->int{return outer;};}', 'return outer', 'outer', 'int'),
			('nested', 'void main(){int outer=7;auto f=[outer]()->int{auto g=[copy=outer]()mutable->int{return ++copy;};return g();};}', '++copy', 'copy', 'int'),
		]
		for name, source, needle, token, detail in examples[:3]:
			for variant in ['prior_error', 'later_error', 'unfinished', 'utf16_crlf']:
				changed = ('int earlier=1\n'+source if variant == 'prior_error' else
						   source+'\nvoid broken( {' if variant == 'later_error' else
						   source[:-3] if variant == 'unfinished' else
						   source.replace('void main(){', 'void main(){print("😀");').replace('\n', '\r\n'))
				examples.append((name+'_'+variant, changed, needle, token, detail))
		for name, source, needle, token, detail in examples:
			uri = (root/(name+'.vs')).as_uri()
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
			offset = source.index(needle)+needle.index(token)+len(token)-1
			position = physical_lsp_position(source, offset)
			for method in ['completion', 'hover', 'definition']:
				params = {'textDocument': {'uri': uri}, 'position': position}
				response, _ = client.wait(client.request('textDocument/'+method, params))
				records.append(dict(name=name, document=source, method='textDocument/'+method, request=params, response=response))
				assert 'error' not in response, response
				if method == 'completion':
					candidate = next(item for item in items(response) if item['label'] == token)
					assert candidate['detail'] == detail, candidate
					assert apply_edit(source, candidate['textEdit']) == source, candidate
				elif method == 'hover':
					assert f'```veroscript\n{detail}\n```' in response['result']['contents']['value'], response
				else:
					assert len(response['result']) == 1 and response['result'][0]['uri'] == uri, response
					assert response['result'][0]['range']['start'] == physical_lsp_position(source, source.index(token)), response
			client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		for name, source, needle, token, expected in [
			('declaration', 'void main(){auto f=[count=0]()->int{return count;};}', 'count=0', 'count', None),
			('shadow_declaration', 'void main(){int count=7;auto f=[count=0]()->int{return count;};}', 'count=0', 'count', None),
			('uncaptured', 'void main(){int hidden=7;auto f=[kept=1]()->int{return hidden;};}', 'return hidden', 'hidden', None),
			('captureless', 'void main(){int hidden=7;auto f=[]()->int{return hidden;};}', 'return hidden', 'hidden', None),
			('initializer_function', 'int make(){return 1;}void main(){auto f=[value=make()]()->int{return value;};}', 'value=make', 'make', 'make'),
			('initializer_function_reference', 'int make(){return 1;}void main(){auto f=[callback=make]()->int{return callback();};}', 'callback=make', 'make', 'make'),
			('qualified_function_reference', 'namespace N{int make(){return 1;}}void main(){auto f=[callback=N::make]()->int{return callback();};}', 'N::make', 'make', 'make'),
		]:
			uri = (root/(name+'.vs')).as_uri()
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
			offset = source.index(needle)+needle.index(token)+len(token)-1
			params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, offset)}
			response, _ = client.wait(client.request('textDocument/completion', params))
			records.append(dict(name=name, document=source, method='textDocument/completion', request=params, response=response))
			assert 'error' not in response, response
			if expected:
				candidate = next(item for item in items(response) if item['label'] == expected)
				assert apply_edit(source, candidate['textEdit']) == source, candidate
			else:
				assert not items(response), response
			client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		for name, source, needle, token in [
			('capture_prefix', 'void main(){int base=7;fn(int)->int f=[bas\n}', '[bas', 'bas'),
			('capture_empty', 'void main(){int base=7;auto f=[]()->int{return 0;};}', '[', ''),
			('capture_complete_prefix', 'void main(){int base=7;auto f=[bas]()->int{return 0;};}', '[bas', 'bas'),
		]:
			uri = (root/(name+'.vs')).as_uri()
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
			offset = source.index(needle)+len(needle)
			params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, offset)}
			response, _ = client.wait(client.request('textDocument/completion', params))
			records.append(dict(name=name, document=source, method='textDocument/completion', request=params, response=response))
			candidate = next(item for item in items(response) if item['label'] == 'base')
			if token or 'textEdit' in candidate:
				assert candidate['textEdit']['range'] == {'start': physical_lsp_position(source, offset-len(token)),
														 'end': physical_lsp_position(source, offset)}, candidate
				assert apply_edit(source, candidate['textEdit']) == source[:offset-len(token)]+'base'+source[offset:], candidate
			else:
				assert candidate['insertText'] == 'base' and candidate.get('insertTextFormat', 1) == 1, candidate
			assert not any(item['label'] == 'print' for item in items(response)), response
			client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		source = 'void main(){auto f=[count=0](int arg)->int{return count+arg;};}'
		uri = (root/'initialized_parameter.vs').as_uri()
		client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
		for token, detail in [('count', 'const int'), ('arg', 'int')]:
			params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, source.index(token)+1)}
			response, _ = client.wait(client.request('textDocument/hover', params))
			records.append(dict(name='initialized_parameter_'+token, document=source, method='textDocument/hover', request=params, response=response))
			assert f'```veroscript\n{detail}\n```' in response['result']['contents']['value'], response
		client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		for name, source, token in [
			('array_name', 'void main(){int values[2]={1,2};print(values[0]);}', 'values'),
			('group_name', 'void main(){int first=1,second=first+1;print(second);}', 'second'),
			('static_const_name', 'void main(){static const int count=1;print(count);}', 'count'),
		]:
			uri = (root/(name+'.vs')).as_uri()
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
			params = {'textDocument': {'uri': uri}, 'position': physical_lsp_position(source, source.rindex(token)+1)}
			response, _ = client.wait(client.request('textDocument/definition', params))
			records.append(dict(name=name, document=source, method='textDocument/definition', request=params, response=response))
			assert len(response.get('result', [])) == 1 and response['result'][0]['range']['start'] == physical_lsp_position(source, source.index(token)), response
			client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		uri = (root/'changed-capture.vs').as_uri()
		for version, source, detail in [
			(1, 'void main(){auto f=[value=1]()->int{return value;};}', 'const int'),
			(2, 'void main(){auto f=[value="text"]()->string{return value;};}', 'const string'),
			(3, 'void main(){auto f=[value=1]()mutable->int{return ++value;};}', 'int'),
		]:
			if version == 1:
				client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': version, 'text': source}})
			else:
				client.send('textDocument/didChange', {'textDocument': {'uri': uri, 'version': version}, 'contentChanges': [{'text': source}]})
			for method in ['semanticTokens/full', 'hover']:
				params = {'textDocument': {'uri': uri}}
				if method == 'hover':
					params['position'] = physical_lsp_position(source, source.rindex('value')+2)
				response, _ = client.wait(client.request('textDocument/'+method, params))
				records.append(dict(name='changed_capture_'+str(version), version=version, document=source,
									method='textDocument/'+method, request=params, response=response))
				assert 'error' not in response, response
				if method == 'hover':
					assert f'```veroscript\n{detail}\n```' in response['result']['contents']['value'], response
				else:
					assert response['result']['data'], response
		client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		function_capture_cases(client, root, records)

		source = 'void main(){auto f=['+','.join(f'c{i}={i}' for i in range(48))+']()->int{return '+'+'.join(f'c{i}' for i in range(48))+';};}'
		uri = (root/'dense-captures.vs').as_uri()
		client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
		params = {'textDocument': {'uri': uri}}
		response, elapsed = client.wait(client.request('textDocument/semanticTokens/full', params))
		records.append(dict(name='dense_captures', document=source, method='textDocument/semanticTokens/full',
							request=params, response=response, elapsedSeconds=elapsed))
		assert 'error' not in response and response['result']['data'] and elapsed < 2.0, (response, elapsed)
		client.send('textDocument/didClose', {'textDocument': {'uri': uri}})

		for eol in ['\n', '\r\n']:
			source = ('void main(){\n auto next=[count=0]() mutable ->int {\n'
					  '  static int calls=0;\n\n  return ++count+calls;\n };\n}\n').replace('\n', eol)
			uri = (root/('newline-'+str(len(eol))+'.vs')).as_uri()
			client.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'veroscript', 'version': 1, 'text': source}})
			params = {'textDocument': {'uri': uri}, 'position': {'line': 3, 'character': 0}, 'ch': '\n',
					  'options': {'tabSize': 2, 'insertSpaces': False}}
			for repeat in range(2):
				response, _ = client.wait(client.request('textDocument/onTypeFormatting', params))
				records.append(dict(name='lambda_newline_'+str(len(eol))+'_'+str(repeat), document=source,
									method='textDocument/onTypeFormatting', request=params, response=response))
				assert 'error' not in response, response
				edited = apply_edits(source, response['result'] or [])
				assert edited.count('\n') == source.count('\n') and ''.join(edited.split()) == ''.join(source.split())
				assert '\t' in edited
				if repeat:
					assert edited == source, response
				source = edited
				client.send('textDocument/didChange', {'textDocument': {'uri': uri, 'version': repeat+2}, 'contentChanges': [{'text': source}]})
			client.send('textDocument/didClose', {'textDocument': {'uri': uri}})
		return initialized.get('result', {}).get('serverInfo')
	finally:
		client.close()


def dap_cases(executable, root, sessions):
	examples = [
		('persistent', '''void main(){
 auto counter=[value=3]()mutable->int{
  static int ticks=0;
  ++ticks;
  ++value;
  return value+ticks;
 };
 print(counter());
 print(counter());
}
''', 5, 2),
		('readonly', '''void main(){
 const int source=7;
 auto read=[&alias=source]()->int{
  return alias;
 };
 print(read());
}
''', 4, 1),
		('function_reference', '''namespace N{int make(){return 42;}}
void main(){
 auto run=[callback=N::make]()->int{
  return callback();
 };
 print(run());
}
''', 4, 1),
	]
	for name, source, line, stops in examples:
		debugger = Wire(executable, root, dap=True)
		record = dict(name=name, document=source, fileLabel=str(root/(name+'.vs')))
		sessions.append(record)
		try:
			assert debugger.request('initialize', {'adapterID': 'veroscript', 'locale': 'en-US'})['success']
			debugger.event('initialized')
			launch = debugger.request('launch', {'source': source, 'fileLabel': record['fileLabel'], 'stopOnEntry': False})
			assert launch['success'], launch
			assert debugger.request('setBreakpoints', {'source': {'path': record['fileLabel']}, 'breakpoints': [{'line': line}]})['success']
			assert debugger.request('configurationDone')['success']
			previous = None
			for iteration in range(stops):
				debugger.event('stopped')
				if previous is not None:
					assert not debugger.request('variables', {'variablesReference': previous})['success']
				stack = debugger.request('stackTrace', {'threadId': 1})['body']['stackFrames']
				assert stack[0]['name'] == '<lambda>' and stack[0]['line'] == line, stack
				frame = stack[0]['id']
				reference = debugger.request('scopes', {'frameId': frame})['body']['scopes'][0]['variablesReference']
				variables = debugger.request('variables', {'variablesReference': reference})['body']['variables']
				by_name = {item['name']: item for item in variables}
				if name == 'persistent':
					assert by_name['ticks']['value'] == str(iteration+1), variables
					assert 'readOnly' in by_name['ticks']['presentationHint']['attributes']
					assert by_name['value']['value'] == str(3 if not iteration else 11), variables
					assert 'readOnly' not in by_name['value'].get('presentationHint', {}).get('attributes', [])
					if not iteration:
						edit = debugger.request('setVariable', {'variablesReference': reference, 'name': 'value', 'value': '10'})
						assert edit['success'] and edit['body']['value'] == '10', edit
				elif name == 'readonly':
					assert by_name['alias']['value'] == '7' and 'readOnly' in by_name['alias']['presentationHint']['attributes'], variables
					assert not debugger.request('setVariable', {'variablesReference': reference, 'name': 'alias', 'value': '9'})['success']
				else:
					assert by_name['callback']['value'] == 'fn()->int', variables
					assert 'readOnly' in by_name['callback']['presentationHint']['attributes'], variables
					# This case inspects the captured callable once. The call
					# itself may have another executable location on this line.
					assert debugger.request('setBreakpoints', {'source': {'path': record['fileLabel']}, 'breakpoints': []})['success']
				previous = reference
				assert debugger.request('continue', {'threadId': 1})['success']
			debugger.event('terminated')
			assert debugger.event('exited')['body']['exitCode'] == 0
			assert not debugger.request('scopes', {'frameId': frame})['success']
			output = ''.join(item.get('body', {}).get('output', '') for item in debugger.messages if item.get('event') == 'output')
			assert output == {'persistent': '12\n14\n', 'readonly': '7\n', 'function_reference': '42\n'}[name], output
		finally:
			debugger.close()
			record.update(requests=debugger.requests, messages=debugger.messages)


def main():
	lsp, dap, destination = map(Path, sys.argv[1:4])
	report = dict(clientCapabilities=COMPLETION_CAPABILITIES, binaries={name: dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
							for name, path in [('lsp', lsp), ('dap', dap)]}, cases=[], dapSessions=[], verified=False)
	try:
		with tempfile.TemporaryDirectory(prefix='vero-lambda-protocol-') as temporary:
			root = Path(temporary)
			report['serverInfo'] = lsp_cases(lsp, root, report['cases'])
			dap_cases(dap, root, report['dapSessions'])
		report['verified'] = True
	finally:
		destination.parent.mkdir(parents=True, exist_ok=True)
		destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
	print('Lambda scopes, constness, newline formatting and persistent DAP state: PASS')


if __name__ == '__main__':
	main()
