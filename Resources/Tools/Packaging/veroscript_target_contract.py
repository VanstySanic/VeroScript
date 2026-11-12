# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Read compiler-evaluated target ABI data without executing a foreign program."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

MAGIC = b'VeroScript.PackContract.v1:'
BUFFER = 2048


def function_body(header, name):
	match = re.search(r'static inline std::string\s+' + re.escape(name) + r'\(\)\s*\{', header)
	if not match:
		raise ValueError('Missing ABI function: ' + name)
	start = match.end()
	depth = 1
	for index in range(start, len(header)):
		depth += (header[index] == '{') - (header[index] == '}')
		if not depth:
			return header[start:index]
	raise ValueError('Unterminated ABI function: ' + name)


def generate(repo, output):
	public = Path(repo) / 'VeroScript/Public'
	names = [('VeroScriptJitModule.h', 'BuildJitModuleAbi'),
		('VeroScriptVmModule.h', 'BuildVmModuleAbi'), ('VeroScriptNativeExtension.h', 'BuildNativeExtensionAbi')]
	bodies = [(name, function_body((public / header).read_text(encoding='utf-8-sig'), name)) for header, name in names]
	contract = hashlib.sha256(json.dumps(bodies, separators=(',', ':')).encode()).hexdigest()
	definitions = []
	for name, body in bodies:
		body = body.replace('std::string', 'Buffer').replace('std::to_string', 'Number')
		for _, function in names:
			body = body.replace(function + '()', function + 'Constant()')
		definitions.append('constexpr Buffer ' + name + 'Constant() {' + body + '}')
	source = r'''
#include "VeroScriptNativeExtension.h"
#include "VeroScriptVmModule.h"
#include <cstdint>
namespace VeroScript::PackagingContract {
struct Buffer {
 char Data[2048]{}; std::uint32_t Size = 0;
 constexpr Buffer() = default;
 constexpr Buffer(const char* Text) { while (*Text) Data[Size++] = *Text++; }
 constexpr Buffer& operator+=(const Buffer& Other) {
  for (std::uint32_t I=0; I<Other.Size; ++I) Data[Size++]=Other.Data[I];
  return *this;
 }
 constexpr Buffer& operator+=(char Character) { Data[Size++]=Character; return *this; }
};
constexpr Buffer operator+(Buffer Left, const Buffer& Right) { return Left += Right; }
constexpr Buffer operator+(Buffer Left, char Right) { return Left += Right; }
constexpr Buffer operator+(char Left, const Buffer& Right) { Buffer Result; Result+=Left; return Result+=Right; }
constexpr Buffer Number(std::uint64_t Value) {
 Buffer Result; char Digits[32]{}; unsigned Count=0;
 do { Digits[Count++]=char('0'+Value%10); Value/=10; } while(Value);
 while(Count) Result+=Digits[--Count]; return Result;
}
#if defined(_WIN32)
#define VS_PACKAGE_OS "win"
#elif defined(__APPLE__)
#define VS_PACKAGE_OS "macos"
#elif defined(__linux__)
#define VS_PACKAGE_OS "linux"
#else
#error Unsupported desktop packaging platform
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
#define VS_PACKAGE_ARCH "arm64"
#elif defined(_M_X64) || defined(__x86_64__)
#define VS_PACKAGE_ARCH "x64"
#elif defined(_M_IX86) || defined(__i386__)
#define VS_PACKAGE_ARCH "x86"
#else
#error Unsupported desktop packaging architecture
#endif
constexpr const char* TargetPlatform = VS_PACKAGE_OS "-" VS_PACKAGE_ARCH;
'''
	source += '\n'.join(definitions)
	source += '\nstruct Record { char Magic[32]; Buffer Jit, Vm, Native; char Core[65], Public[65], Platform[64], Source[65]; };\n'
	source += 'extern "C" {\n#if defined(__GNUC__)\n__attribute__((used,visibility("default")))\n#endif\n'
	source += 'extern const Record VeroScriptPackagingContract = { "' + MAGIC.decode() + '", BuildJitModuleAbiConstant(), BuildVmModuleAbiConstant(), BuildNativeExtensionAbiConstant(), VEROSCRIPT_CORE_IMPLEMENTATION_ID, VEROSCRIPT_JIT_PUBLIC_ABI, VS_PACKAGE_OS "-" VS_PACKAGE_ARCH, "' + contract + '" };\n}\n}\n'
	Path(output).write_text(source, encoding='utf-8')
	return contract


def allocated_sections(data):
	"""Read object data sections; debug information may repeat whole constants."""
	regions = []

	def region(offset, size):
		if offset < 0 or size < 0 or offset+size > len(data):
			raise ValueError('Object section exceeds its compiled archive member')
		regions.append(data[offset:offset+size])
	try:
		if data.startswith(b'\x7fELF'):
			if data[5] != 1:
				raise ValueError('Unsupported ELF byte order')
			wide = data[4] == 2
			start = struct.unpack_from('<Q' if wide else '<I', data, 40 if wide else 32)[0]
			stride, count = struct.unpack_from('<HH', data, 58 if wide else 46)
			if stride < (64 if wide else 40) or not count or start+stride*count > len(data):
				raise ValueError('Malformed ELF section table')
			for index in range(count):
				section = start+stride*index
				kind = struct.unpack_from('<I',data,section+4)[0]
				flags = struct.unpack_from('<Q' if wide else '<I',data,section+8)[0]
				if kind == 1 and flags & 2:
					offset,size = struct.unpack_from('<QQ' if wide else '<II',data,section+(24 if wide else 16))
					region(offset,size)
		elif data[:4] in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe'):
			wide = data[:4] == b'\xcf\xfa\xed\xfe'
			position = 32 if wide else 28
			for _ in range(struct.unpack_from('<I',data,16)[0]):
				command,size = struct.unpack_from('<II',data,position)
				if size < 8 or position+size > len(data):
					raise ValueError('Malformed Mach-O load command')
				if command == (0x19 if wide else 1):
					count = struct.unpack_from('<I',data,position+(64 if wide else 48))[0]
					start = position+(72 if wide else 56)
					stride = 80 if wide else 68
					if start+count*stride > position+size:
						raise ValueError('Malformed Mach-O section table')
					for index in range(count):
						section = start+index*stride
						name = data[section:section+16].rstrip(b'\0')
						segment = data[section+16:section+32].rstrip(b'\0')
						length = struct.unpack_from('<Q' if wide else '<I',data,section+(40 if wide else 36))[0]
						offset = struct.unpack_from('<I',data,section+(48 if wide else 40))[0]
						flags = struct.unpack_from('<I',data,section+(64 if wide else 56))[0]
						if segment not in (b'__DWARF',b'__LLVM') and not name.startswith(b'__debug') and flags & 0xff not in (1,12,18):
							region(offset,length)
				position += size
		else:
			big = data[:4] == b'\0\0\xff\xff'
			count = struct.unpack_from('<I' if big else '<H',data,44 if big else 2)[0]
			start = 56 if big else 20+struct.unpack_from('<H',data,16)[0]
			if not count or start+count*40 > len(data):
				raise ValueError('Malformed COFF section table')
			for index in range(count):
				section = start+index*40
				name = data[section:section+8].rstrip(b'\0')
				length,offset = struct.unpack_from('<II',data,section+16)
				flags = struct.unpack_from('<I',data,section+36)[0]
				if flags & 0x40 and not flags & (0x200|0x800) and not name.startswith(b'.debug'):
					region(offset,length)
	except (struct.error,IndexError) as error:
		raise ValueError('Truncated compiled object section table') from error
	return regions


def read_contract(path, expected_target=None):
	owner = Path(path).read_bytes()
	records = []
	if owner.startswith(b'!<arch>\n'):
		data = owner
		offset=8
		owners=[]
		while offset+60<=len(data):
			header=data[offset:offset+60]
			if header[58:60]!=b'`\n':
				raise ValueError('Malformed ABI static archive')
			length=int(header[48:58])
			member=data[offset+60:offset+60+length]
			if len(member)!=length:
				raise ValueError('Truncated ABI static archive member')
			if header[:16].startswith(b'#1/'):
				member=member[int(header[:16].decode().strip()[3:]):]
			if MAGIC+b'\0' in member:
				owners.append(member)
			offset+=60+length+(length%2)
		if len(owners)!=1:
			raise ValueError('ABI record has no unique archive object owner')
		owner=owners[0]
	from veroscript_release import TARGETS,native_identity
	try:
		identity=native_identity(owner[:4096])
	except ValueError:
		machine=struct.unpack_from('<H',owner,6 if owner[:4]==b'\0\0\xff\xff' else 0)[0]
		architecture={0x14c:'x86',0x8664:'x64',0xaa64:'arm64'}.get(machine)
		if not architecture:
			raise ValueError('Unknown ABI object architecture')
		identity=('windows',architecture)
	for data in allocated_sections(owner):
		records.extend(_section_records(data))
	if len(records) != 1:
		raise ValueError('Missing or ambiguous compiled packaging contract')
	abis, values = records[0]
	if expected_target and values['target'] != expected_target:
		raise ValueError('Compiled ABI record target differs from the product target')
	if values['target'] not in TARGETS or identity!=TARGETS[values['target']]:
		raise ValueError('Compiled ABI object architecture differs from its recorded target')
	return {'schemaVersion':1, **values, 'abis':abis}


def _section_records(data):
	records = []
	for match in re.finditer(re.escape(MAGIC + b'\0'), data):
		position = match.start() + 32
		abis = {}
		values = {}
		try:
			for name in ('jit', 'vm', 'native-extension'):
				text = data[position:position+BUFFER]
				size = struct.unpack_from('<I', data, position+BUFFER)[0]
				if not 0 < size < BUFFER or text[size] != 0:
					raise ValueError('Malformed compiled ABI data')
				abis[name] = text[:size].decode('ascii')
				position += BUFFER+4
			for name, length in [('coreBuild',65), ('publicAbi',65), ('target',64), ('sourceContract',65)]:
				field = data[position:position+length]
				if len(field) != length or b'\0' not in field:
					raise ValueError('Truncated compiled ABI identity')
				values[name] = field.split(b'\0')[0].decode('ascii')
				position += length
			if any(not re.fullmatch('[0-9a-f]{64}', values[key]) for key in ('coreBuild','publicAbi','sourceContract')):
				raise ValueError('Malformed compiled ABI identities')
			records.append((abis, values))
		except (ValueError, IndexError, struct.error):
			continue
	return records


if __name__ == '__main__':
	parser=argparse.ArgumentParser(description=__doc__)
	commands=parser.add_subparsers(dest='command',required=True)
	generate_parser=commands.add_parser('generate')
	generate_parser.add_argument('--repo-root',type=Path,required=True)
	generate_parser.add_argument('--output',type=Path,required=True)
	read_parser=commands.add_parser('read')
	read_parser.add_argument('file',type=Path)
	read_parser.add_argument('--target')
	args=parser.parse_args()
	if args.command=='generate':
		generate(args.repo_root,args.output)
	else:
		print(json.dumps(read_contract(args.file,args.target),indent=2))
