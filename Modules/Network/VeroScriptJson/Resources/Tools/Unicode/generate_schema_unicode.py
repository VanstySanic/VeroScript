# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Regenerate private Unicode 17 / IDNA 3.12 tables from verified offline inputs."""
import argparse
import hashlib
import io
import tarfile
import ast
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--idna-archive',type=Path,required=True)
parser.add_argument('--unicode-data',type=Path,required=True)
parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[3]/'ThirdParty/idna')
args=parser.parse_args()
archive=args.idna_archive.read_bytes()
if hashlib.sha256(archive).hexdigest()!='724e9952cc9e2bd7550ea784adb098d837ab5267ef67a1ab9cf7846bdbdd8254':
	parser.error('The python-idna input is not the pinned 3.12 source archive')
ucd=args.unicode_data.read_bytes()
if hashlib.sha256(ucd).hexdigest()!='2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c':
	parser.error('The character data is not the pinned Unicode 17.0.0 input')
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
	idna_text=tar.extractfile('idna-3.12/idna/idnadata.py').read().decode('utf-8')
	mapping_text=tar.extractfile('idna-3.12/idna/uts46data.py').read().decode('utf-8')
args.output.mkdir(parents=True,exist_ok=True)
tree=ast.parse(idna_text)
values={}
for node in tree.body:
	if isinstance(node,ast.Assign):
		for target in node.targets:
			if isinstance(target,ast.Name):
				values[target.id]=ast.literal_eval(node.value)
	elif isinstance(node,ast.FunctionDef) and node.name=='joining_types':
		values['joining_types']=ast.literal_eval(next(x for x in node.body if isinstance(x,ast.Return)).value)
assert values['__version__']=='17.0.0'

dirs={'L':1,'R':2,'AL':3,'EN':4,'AN':5,'ES':6,'CS':7,'ET':8,'ON':9,'BN':10,'NSM':11}
marks=[]
virama=[]
bidi=[]
pending=None
def append(out,start,end,value=None):
	if out and out[-1][1]==start and (value is None or out[-1][2]==value):
		old=out[-1]
		out[-1]=(old[0],end) if value is None else (old[0],end,value)
	else:
		out.append((start,end) if value is None else (start,end,value))
for line in ucd.decode('utf-8').splitlines():
	row=line.split(';')
	cp=int(row[0],16)
	if row[1].endswith(', First>'):
		pending=(cp,row)
		continue
	start=cp
	if row[1].endswith(', Last>'):
		assert pending is not None
		start,row=pending
		pending=None
	if row[2].startswith('M'):
		append(marks,start,cp+1)
	if row[3]=='9':
		append(virama,start,cp+1)
	if row[4] in dirs:
		append(bidi,start,cp+1,dirs[row[4]])

joined=[]
for cp,value in sorted(values['joining_types'].items()):
	append(joined,cp,cp+1,value)
out=['// Generated from python-idna 3.12 and Unicode 17.0.0; see accompanying source metadata and licenses.\n',
	 '#pragma once\n#include <cstdint>\nnamespace VeroScript::Json::Detail::Unicode17 {\n',
	 'enum class EDirection : std::uint8_t { Other=0,L=1,R=2,AL=3,EN=4,AN=5,ES=6,CS=7,ET=8,ON=9,BN=10,NSM=11 };\n',
	 'struct FTaggedRange { std::uint32_t Start, End; std::uint8_t Value; };\n']
for name,items in [(x,[(v>>32,v&0xffffffff) for v in table]) for x,table in values['codepoint_classes'].items()]+list((k,[(v>>32,v&0xffffffff) for v in v]) for k,v in values['scripts'].items())+[('Marks',marks),('Virama',virama)]:
	out.append('inline constexpr std::uint64_t '+name+'[] = {\n')
	out.extend('    0x%016xULL,\n'%((a<<32)|b) for a,b in items)
	out.append('};\n')
for name,items in [('Bidi',bidi),('Joining',joined)]:
	out.append('inline constexpr FTaggedRange '+name+'[] = {\n')
	out.extend('    {0x%x,0x%x,%d},\n'%x for x in items)
	out.append('};\n')
out.append('}\n')
path=args.output/'Unicode17Data.h'
path.write_text(''.join(out),encoding='utf-8',newline='\n')
print(path,len(''.join(out)),'bytes')

tree=ast.parse(mapping_text)
table=None
for node in tree.body:
	if isinstance(node,ast.AnnAssign) and isinstance(node.target,ast.Name) and node.target.id=='uts46data':
		table=ast.literal_eval(node.value)
	if isinstance(node,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='uts46data' for x in node.targets):
		table=ast.literal_eval(node.value)
assert table
pool=[]
known={}
entries=[]
for row in table:
	replacement=tuple(ord(c) for c in row[2]) if len(row)==3 else ()
	if replacement not in known:
		known[replacement]=len(pool)
		pool.extend(replacement)
	entries.append((row[0],known[replacement],len(replacement),row[1]))
out=['// Generated from python-idna 3.12 (Unicode 17); see source metadata and BSD-3-Clause license.\n',
	 '#pragma once\n#include <cstdint>\nnamespace VeroScript::Json::Detail::Unicode17 {\n',
	 'struct FMapping {std::uint32_t Start,Offset;std::uint16_t Length;char Status;};\n',
	 'inline constexpr std::uint32_t MappingText[] = {\n']
out.extend('    0x%x,\n'%x for x in pool)
out.append('};\ninline constexpr FMapping Mapping[] = {\n')
out.extend("    {0x%x,%d,%d,'%s'},\n"%row for row in entries)
out.append('};\n}\n')
path=args.output/'Unicode17Mapping.h'
path.write_text(''.join(out),encoding='utf-8',newline='\n')
print(path,len(''.join(out)),'bytes')
