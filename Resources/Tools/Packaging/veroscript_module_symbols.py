# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT
"""Export each module's own debug inputs, paired with its actual functional archive."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import zipfile

from veroscript_products import (INSTALLABLE_PACKAGE_NAMES, FOUNDATION_BUSINESSES,
    FOUNDATION_MODULE_DIRECTORIES, FEATURE_MODULE_BUSINESSES)
from veroscript_release import archive_entries, archive_member_digest, archive_tree, digest, scratch_directory, safe_member

MODULES = tuple(INSTALLABLE_PACKAGE_NAMES)
MANIFEST = 'module-symbols.json'

def symbol_archive_name(business, version, target):
    from veroscript_products import RELEASE_TARGETS
    from veroscript_release import version as validate_version
    validate_version(version)
    if target not in RELEASE_TARGETS:raise ValueError('Unknown module symbols target')
    if business not in MODULES:
        raise ValueError('Unregistered module symbol owner: ' + business)
    suffix = '.zip' if target.startswith('win-') or target in {'android', 'ios'} else '.tar.gz'
    return INSTALLABLE_PACKAGE_NAMES[business] + '-' + version + '-' + target + '-symbols' + suffix

def _roots(repo):
    from veroscript_modules import MODULE_SOURCES
    return {'Jit': repo/'Modules/Execution/VeroScriptAsmJit',
            'Vm': repo/'Modules/Execution/VeroScriptBytecodeVm',
            **{k: repo/v for k,v in MODULE_SOURCES.items()},
            **{k: repo/v for k,v in FOUNDATION_MODULE_DIRECTORIES.items()}}

def _archive_inventory(path):
    from veroscript_foundation import archive_digests
    entries = archive_entries(path, max_entry_bytes=4096)
    if path.suffix in {'.zip','.aar'}:
        with zipfile.ZipFile(path) as reader:
            hashes={}
            for item in reader.infolist():
                if not item.is_dir():
                    with reader.open(item) as stream:hashes[item.filename]=hashlib.file_digest(stream,'sha256').hexdigest()
        return entries,hashes
    return entries, archive_digests(path)

def _peers(destination, business):
    from veroscript_business_release import artifact_business
    result = []
    for path in sorted(destination.rglob('*')):
        if not path.is_file() or 'symbols' in path.relative_to(destination).parts or path.suffix == '.vspkg':
            continue
        if not path.name.endswith(('.zip', '.tar.gz', '.aar')):
            continue
        try:
            owner = artifact_business(path.name)
        except ValueError:
            continue
        if owner == business or business == 'Vm' and owner == 'CoreSDK':
            result.append(path)
    return result

def _file_api(build, config):
    from veroscript_modules import build_cache
    settings = build_cache(build)
    reply = build/'.cmake/api/v1/reply'
    indexes = sorted(reply.glob('index-*.json'))
    if not indexes or 'codemodel-v2' not in json.loads(indexes[-1].read_text())['reply']:
        query = build/'.cmake/api/v1/query/codemodel-v2'
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()
        header = build/'VeroScript/Generated/VeroScriptJitBuild.h'
        before = header.read_bytes() if header.is_file() else None
        subprocess.run([settings.get('CMAKE_COMMAND','cmake'), '-S', settings['CMAKE_HOME_DIRECTORY'], '-B', str(build)], check=True)
        if before is not None and header.read_bytes() != before:
            raise ValueError('Module symbol metadata configure changed the compiled Core identity; rebuild first')
        indexes = sorted(reply.glob('index-*.json'))
    index = json.loads(indexes[-1].read_text())
    model = json.loads((reply/index['reply']['codemodel-v2']['jsonFile']).read_text())
    configurations = [c for c in model['configurations'] if c['name'] == config]
    if len(configurations) != 1:
        raise ValueError('No unique module symbol build configuration: ' + config)
    records = []
    for item in configurations[0]['targets']:
        value = json.loads((reply/item['jsonFile']).read_text())
        if value['type'] not in {'STATIC_LIBRARY','SHARED_LIBRARY','MODULE_LIBRARY'}:
            continue
        source = Path(value['paths']['source'])
        if not source.is_absolute():
            source = Path(model['paths']['source'])/source
        artifacts = [build/a['path'] for a in value.get('artifacts', [])]
        records.append({'name': value['name'], 'id':value['id'],'dependencies':[d['id'] for d in value.get('dependencies',[])], 'sourceDirectory': str(source.resolve()),
                        'artifacts': list(map(str, artifacts)), 'compileGroups': value.get('compileGroups', [])})
    report = {'schemaVersion':1, 'configuration':config, 'source':'CMake File-API codemodel-v2 target artifacts', 'targets':records}
    (build/('veroscript-module-symbol-inputs-'+config+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    return records, settings

def desktop_inputs(repo, builds, destination, target, selected, config='Release'):
    roots = _roots(repo)
    inventories, models, result = {}, {}, []
    for business in selected:
        if business not in MODULES:
            raise ValueError('Unknown module symbol business: ' + business)
        build = Path(builds[business]).resolve()
        if build not in models:
            models[build] = _file_api(build, config)
        model, settings = models[build]
        home=Path(settings['CMAKE_HOME_DIRECTORY']).resolve()
        if home != repo.resolve() and not home.is_relative_to((repo/'Modules').resolve()):
            raise ValueError('Module symbols build belongs to another source tree')
        start_count=len(result)
        owner = roots[business].resolve()
        expected = set()
        foundation = build/('veroscript-foundation-'+config+'.json')
        if business in FOUNDATION_BUSINESSES and foundation.is_file():
            from veroscript_foundation import foundation_script_targets
            info = json.loads(foundation.read_text())
            expected = {Path(info[k]).resolve() for k in [business,*foundation_script_targets(business)] if k in info and Path(info[k]).is_file()}
        found_expected = set()
        by_id={item['id']:item for item in model}
        eligible=set();pending=[item['id'] for item in model if Path(item['sourceDirectory']).is_relative_to(owner)
            or any(Path(p).resolve() in expected for p in item['artifacts'])]
        while pending:
            identifier=pending.pop()
            if identifier in eligible or identifier not in by_id:continue
            item=by_id[identifier];source=Path(item['sourceDirectory'])
            foreign=any(other!=business and source.is_relative_to(root.resolve()) for other,root in roots.items())
            own=source.is_relative_to(owner) or any(Path(p).resolve() in expected for p in item['artifacts'])
            # Follow actual linked vendor targets only. Stop at other products rather than absorbing Core dependencies.
            if foreign or not own and item['name'].startswith('VeroScript'):continue
            eligible.add(identifier);pending.extend(item['dependencies'])
        for item in model:
            paths = [Path(p) for p in item['artifacts'] if Path(p).is_file() and not p.endswith(('.pdb','.exp'))]
            if item['id'] not in eligible:
                continue
            for raw in paths:
                artifact_basename=raw.name
                raw = raw.resolve()
                if not raw.is_relative_to(build):
                    raise ValueError('Module debug input escaped its build: ' + str(raw))
                found_expected.add(raw)
                matches = []
                for peer in _peers(destination,business):
                    if peer not in inventories:
                        inventories[peer] = _archive_inventory(peer)
                    entries, hashes = inventories[peer]
                    for entry in entries:
                        # Match only actual distributed own outputs, never another module dependency.
                        if PurePosixPath(entry).name in {raw.name,artifact_basename}:
                            matches.append((peer,entry,hashes[entry]))
                if not matches:
                    continue
                from veroscript_business_release import artifact_business
                primary=[m for m in matches if artifact_business(m[0].name)==business]
                if primary:matches=primary
                unique = {(p.name,h) for p,e,h in matches}
                if len(unique) != 1:
                    raise ValueError('Ambiguous functional module library: '+str(raw))
                peer, entry, hashed = max(matches,key=lambda row: (PurePosixPath(row[1]).name == raw.name,len(row[1])))
                result.append({'business':business,'slice':target,'source':str(raw),'functionalArchive':str(peer),
                    'functionalEntry':entry,'functionalSha256':hashed,'targetName':item['name'],
                    'settings':settings,'buildDirectory':str(build),'compileGroups':item['compileGroups'],
                    'pdbCandidates':[p for p in item['artifacts'] if p.endswith('.pdb')]})
        if not expected.issubset(found_expected):
            raise ValueError('Foundation original build-info and target artifacts disagree: '+business)
        if len(result)==start_count:
            raise ValueError('No distributed owned original module output: '+business)
    return result

def _build_id(data):
    if not data.startswith(b'\x7fELF') or data[4] != 2:
        return None
    endian = '<' if data[5] == 1 else '>'
    offset = struct.unpack_from(endian+'Q',data,32)[0]
    size,count = struct.unpack_from(endian+'HH',data,54)
    for i in range(count):
        cursor = offset+i*size
        if cursor+56 > len(data) or struct.unpack_from(endian+'I',data,cursor)[0] != 4:
            continue
        start = struct.unpack_from(endian+'Q',data,cursor+8)[0]
        length = struct.unpack_from(endian+'Q',data,cursor+32)[0]
        end = min(start+length,len(data))
        while start+12 <= end:
            namesz,descsz,kind = struct.unpack_from(endian+'III',data,start)
            name = data[start+12:start+12+namesz].rstrip(b'\0')
            value = start+12+((namesz+3)//4)*4
            if kind == 3 and name == b'GNU':
                return data[value:value+descsz].hex()
            start = value+((descsz+3)//4)*4
    return None

def _macho_uuids(data):
    if data[:4] in {b'\xca\xfe\xba\xbe',b'\xca\xfe\xba\xbf'}:
        wide=data[:4]==b'\xca\xfe\xba\xbf';result=[]
        for i in range(struct.unpack_from('>I',data,4)[0]):
            offset,length=struct.unpack_from('>QQ' if wide else '>II',data,8+i*(32 if wide else 20)+8)
            result += _macho_uuids(data[offset:offset+length])
        return result
    if data[:4] not in {b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf'}:
        return []
    endian='<' if data[0]==0xcf else '>';cursor=32;result=[]
    for _ in range(struct.unpack_from(endian+'I',data,16)[0]):
        command,size=struct.unpack_from(endian+'II',data,cursor)
        if size<8 or cursor+size>len(data):raise ValueError('Invalid Mach-O symbol identity')
        if command==0x1b:result.append(data[cursor+8:cursor+24].hex())
        cursor+=size
    return sorted(result)

def _without_elf_search_paths(data):
    """Ignore only installed RPATH string rewrites, never code, symbols or relocation bytes."""
    if data[:4]!=b'\x7fELF':return data
    if len(data)<64:raise ValueError('Truncated module ELF header')
    if data[4]!=2:return data
    if data[5] not in {1,2}:raise ValueError('Invalid module ELF byte order')
    endian='<' if data[5]==1 else '>';programs=struct.unpack_from(endian+'Q',data,32)[0]
    width,count=struct.unpack_from(endian+'HH',data,54);loads=[];dynamic=None
    if count and width<56:raise ValueError('Invalid module ELF program header size')
    for index in range(count):
        cursor=programs+width*index
        if cursor+56>len(data):raise ValueError('Truncated module ELF program headers')
        kind=struct.unpack_from(endian+'I',data,cursor)[0]
        offset,address=struct.unpack_from(endian+'QQ',data,cursor+8)
        size=struct.unpack_from(endian+'Q',data,cursor+32)[0]
        if offset+size>len(data):raise ValueError('Invalid module ELF segment range')
        if kind==1:loads.append((address,size,offset))
        if kind==2:dynamic=(offset,size)
    if dynamic is None:return data
    if dynamic[1]%16:raise ValueError('Invalid module ELF dynamic table size')
    strings=None;size=None;paths=[]
    for cursor in range(dynamic[0],dynamic[0]+dynamic[1],16):
        tag,value=struct.unpack_from(endian+'QQ',data,cursor)
        if tag==0:break
        if tag==5:strings=value
        elif tag==10:size=value
        elif tag in {15,29}:paths.append(value)
    if not paths:return data
    if strings is None or size is None:raise ValueError('Missing ELF dynamic string table')
    locations=[offset+strings-address for address,length,offset in loads if address<=strings and strings+size<=address+length]
    if len(locations)!=1 or locations[0]+size>len(data):raise ValueError('Invalid ELF dynamic string table')
    result=bytearray(data)
    for relative in paths:
        if relative>=size:raise ValueError('Invalid ELF search path offset')
        start=locations[0]+relative;end=data.find(b'\0',start,locations[0]+size)
        if end<0:raise ValueError('Unterminated ELF search path')
        result[start:end]=b'\0'*(end-start)
    return bytes(result)

def _archive_code_digest(path):
    from veroscript_mobile_native import ar_objects
    data=path.read_bytes()
    if data[:8]==b'!<arch>\n':
        # Archive index offsets/timestamps are not code. Preserve actual object order and bytes.
        return hashlib.sha256(b''.join(hashlib.sha256(o).digest() for o in ar_objects(data))).hexdigest()
    return hashlib.sha256(_without_elf_search_paths(data)).hexdigest()

def _native_ids(data,coff=False):
    from veroscript_mobile_native import ar_objects
    from veroscript_release import native_identity
    if data[:8] == b'!<arch>\n':
        result=set()
        if coff:
            from veroscript_pdb import _archive_objects
            objects=_archive_objects(data)
        else:
            objects=ar_objects(data)
        for member in objects:result.update(_native_ids(member,coff))
        return result
    if data[:4] in {b'\xca\xfe\xba\xbe',b'\xca\xfe\xba\xbf'}:
        wide=data[:4]==b'\xca\xfe\xba\xbf';result=set()
        for i in range(struct.unpack_from('>I',data,4)[0]):
            offset,length=struct.unpack_from('>QQ' if wide else '>II',data,8+i*(32 if wide else 20)+8)
            result.update(_native_ids(data[offset:offset+length],coff))
        return result
    if data[:4] == b'\x7fELF' or data[:2] == b'MZ' or data[:4] in {b'\xcf\xfa\xed\xfe',b'\xfe\xed\xfa\xcf'}:
        return {native_identity(data[:4096])}
    if len(data)>=20:
        machine=struct.unpack_from('<H',data,6 if data[:4]==b'\0\0\xff\xff' else 0)[0]
        architecture={0x14c:'x86',0x8664:'x64',0xaa64:'arm64'}.get(machine)
        if architecture:return {('windows',architecture)}
    raise ValueError('Unknown module debug object architecture')

def _check_target(source,target,component):
    from veroscript_products import NATIVE_TARGETS
    identities=_native_ids(source.read_bytes(),target.startswith('win-'))
    if target in NATIVE_TARGETS:expected=NATIVE_TARGETS[target]
    elif target in {'android','ios'}:
        architecture='x64' if any(tag in component for tag in ('x86_64','x64')) else 'arm64'
        expected=('linux' if target=='android' else 'macos',architecture)
    else:raise ValueError('Unknown module symbols target')
    if identities!={expected}:raise ValueError('Wrong module debug target: '+str(identities)+' expected '+str(expected))
    return {'platform':target if target in {'android','ios'} else expected[0],'architecture':expected[1]}

def _extract_member(archive, entry, output):
    safe_member(entry)
    output.parent.mkdir(parents=True,exist_ok=True)
    if archive.name.endswith('.tar.gz'):
        with tarfile.open(archive) as reader:
            with reader.extractfile(entry) as source, output.open('wb') as target:
                shutil.copyfileobj(source,target)
    else:
        with zipfile.ZipFile(archive) as reader, reader.open(entry) as source, output.open('wb') as target:
            shutil.copyfileobj(source,target)

def _resolve_input(row, destination, inventory):
    source = Path(row.get('source',row.get('originalLibrary',''))).resolve(strict=True)
    archive = row.get('functionalArchive')
    entry = row.get('functionalEntry')
    final = row.get('finalLibrary','')
    if not archive and '!/' in final:
        archive,entry=final.split('!/',1)
    expected=row.get('functionalSha256',row.get('finalSha256'))
    matches=[]
    for peer in _peers(destination,row['business']):
        if archive and peer.name != Path(archive).name:continue
        if peer not in inventory:inventory[peer]=_archive_inventory(peer)
        entries,hashes=inventory[peer]
        for name in entries:
            if entry and name!=entry:continue
            if not entry and PurePosixPath(name).name!=Path(final or source.name).name:continue
            if expected and hashes[name]!=expected:continue
            matches.append((peer,name,hashes[name]))
    from veroscript_business_release import artifact_business
    primary=[m for m in matches if artifact_business(m[0].name)==row['business']]
    if primary:matches=primary
    if len(matches)!=1:
        raise ValueError('No unique final functional library for '+str(source)+': '+str(matches))
    return source,*matches[0]

def assemble_module_symbol_inputs(repo,destination,versions,target,inputs,*,tools=None):
    """Mobile producers may pass their exact slice/original/final mappings here too."""
    from veroscript_modules import build_cache
    from veroscript_native_symbols import LinuxNativeSymbols,MacNativeSymbols,macos_has_debug
    repo,destination=Path(repo),Path(destination)
    inventories={};groups={};results={}
    for row in inputs:
        if row['business'] not in MODULES:raise ValueError('Unknown module symbol owner')
        groups.setdefault(row['business'],[]).append(row)
    with tempfile.TemporaryDirectory(prefix='veroscript-module-symbols-',dir=scratch_directory(repo)) as temporary:
        work=Path(temporary)
        for business,rows in groups.items():
            name=symbol_archive_name(business,versions[business],target)
            stage=work/name.removesuffix('.tar.gz').removesuffix('.zip');stage.mkdir()
            shutil.copy2(repo/'LICENSE',stage/'LICENSE')
            records=[];unavailable=[]
            for index,row in enumerate(rows):
                source,peer,entry,functional_sha=_resolve_input(row,destination,inventories)
                if row.get('originalSha256') and digest(source)!=row['originalSha256']:
                    raise ValueError('Changed original module debug source: '+str(source))
                settings=row.get('settings')
                if settings is None:
                    settings=build_cache(Path(row['buildDirectory'])) if row.get('buildDirectory') else {}
                settings=dict(settings);settings.update(tools or {})
                component=row.get('slice',row.get('abi',target));safe_member(component)
                if not re.fullmatch('[A-Za-z0-9_.-]+',component):raise ValueError('Invalid module symbol slice')
                machine=_check_target(source,target,component)
                if row.get('originalHeader') and row.get('sourceCoreBuild'):
                    if row['sourceCoreBuild'] not in Path(row['originalHeader']).read_text():raise ValueError('Stale module source Core header')
                if row.get('sdkMetadata') and row.get('sdkMetadataSha256'):
                    if digest(Path(row['sdkMetadata']))!=row['sdkMetadataSha256']:raise ValueError('Changed module source SDK metadata')
                folder=stage/'symbols'/component/str(index);folder.mkdir(parents=True)
                item_work=work/(business+'-input-'+str(index));item_work.mkdir()
                functional=item_work/source.name
                _extract_member(peer,entry,functional)
                if digest(functional)!=functional_sha:raise ValueError('Changed functional module input')
                with source.open('rb') as stream:magic=stream.read(8)
                raw_sha=digest(source);identity={};symbol_files=[]
                absent={'slice':component,'sourceName':source.name,'sourceSha256':raw_sha,'functionalArchive':peer.name,'functionalEntry':entry,'functionalArchiveSha256':digest(peer),'functionalSha256':functional_sha,'status':'debug-not-generated'}
                if magic[:2]==b'MZ' or target.startswith('win-'):
                    from veroscript_pdb import export_windows_symbols
                    try:
                        symbol_files,identity=export_windows_symbols(source,functional,folder,row)
                    except ValueError as error:
                        raise ValueError('Module symbols '+business+': '+str(source)+': '+str(error)) from error
                elif target.startswith('macos-') or target=='ios':
                    native=MacNativeSymbols(item_work/'native',settings.get('strip',settings.get('CMAKE_STRIP','strip')))
                    prepared=native.prepare(source)
                    if prepared is None:
                        unavailable.append(absent)
                        shutil.rmtree(item_work);shutil.rmtree(folder)
                        continue
                    if magic==b'!<arch>\n':
                        # Apple strip is not idempotent: a second pass can reorder relocations.
                        # Compare the once-stripped original with the already-stripped actual SDK objects.
                        if macos_has_debug(functional.read_bytes()):raise ValueError('Functional Mach-O module retains debug data')
                        if _archive_code_digest(prepared[0])!=_archive_code_digest(functional):
                            raise ValueError('Static Mach-O module symbols do not match functional objects: '+str(source))
                        identity={'format':'macho-static','normalizedObjectsSha256':_archive_code_digest(functional)}
                    else:
                        raw_uuids=_macho_uuids(source.read_bytes());final_uuids=_macho_uuids(functional.read_bytes())
                        if not raw_uuids or raw_uuids!=final_uuids:raise ValueError('Module/dSYM UUID mismatch')
                        identity={'format':'macho','uuids':raw_uuids}
                    native.export_symbols(source,folder)
                    if prepared[1].is_dir():
                        dwarfs=list(prepared[1].glob('Contents/Resources/DWARF/*'))
                        if len(dwarfs)!=1 or _macho_uuids(dwarfs[0].read_bytes())!=identity.get('uuids'):
                            raise ValueError('Generated dSYM UUID mismatch')
                    symbol_files=[p for p in folder.rglob('*') if p.is_file()]
                else:
                    native=LinuxNativeSymbols(item_work/'native',settings.get('objcopy',settings.get('CMAKE_OBJCOPY','objcopy')),settings.get('objdump',settings.get('CMAKE_OBJDUMP','objdump')))
                    prepared=native.prepare(source)
                    if prepared is None:
                        unavailable.append(absent)
                        shutil.rmtree(item_work);shutil.rmtree(folder)
                        continue
                    normal=[]
                    for label,path in [('raw',source),('functional',functional)]:
                        copy=item_work/(label+'-normalized-'+source.name);shutil.copy2(path,copy)
                        subprocess.run([settings.get('strip',settings.get('CMAKE_STRIP','strip')),row.get('stripOption','--strip-debug'),str(copy)],check=True)
                        subprocess.run([native.objcopy,'--remove-section=.gnu_debuglink',str(copy)],check=True)
                        normal.append(_archive_code_digest(copy))
                    if normal[0]!=normal[1]:raise ValueError('ELF module symbols do not match functional code: '+str(source))
                    with source.open('rb') as stream:original_prefix=stream.read(1024*1024)
                    with functional.open('rb') as stream:functional_prefix=stream.read(1024*1024)
                    build_id=_build_id(original_prefix)
                    if build_id!=_build_id(functional_prefix):raise ValueError('ELF functional/debug build-id mismatch')
                    identity={'format':'elf-static' if magic==b'!<arch>\n' else 'elf','buildId':build_id,'normalizedCodeSha256':normal[0]}
                    native.export_symbols(source,folder);symbol_files=[p for p in folder.rglob('*') if p.is_file()]
                if not symbol_files:
                    unavailable.append(absent)
                    shutil.rmtree(item_work);shutil.rmtree(folder)
                    continue
                if digest(source)!=raw_sha:raise ValueError('Symbol export changed an original build file')
                records.append({'slice':component,'sourceName':source.name,'sourceSha256':raw_sha,
                    'functionalArchive':peer.name,'functionalArchiveSha256':digest(peer),'functionalEntry':entry,
                    'functionalSha256':functional_sha,'identity':identity,'machine':machine,'symbols':[p.relative_to(stage).as_posix() for p in symbol_files]})
                # Retain the source package's required legal notices, not documents.
                entries,_=inventories[peer]
                for member in entries:
                    parts=PurePosixPath(member).parts
                    legal=next((i for i,part in enumerate(parts[:-1]) if part.casefold() in {'licenses','notices'}),None)
                    if legal is not None:
                        relative=PurePosixPath(*parts[legal:]);target_file=stage/relative
                        if target_file.exists():
                            if digest(target_file)!=inventories[peer][1][member]:raise ValueError('Conflicting module legal notices')
                        else:_extract_member(peer,member,target_file)
                shutil.rmtree(item_work)
            if not records:
                results[business]={'status':'not-available','reason':'No debug-bearing original distributed own library; no empty archive emitted','unavailable':unavailable}
                shutil.rmtree(stage)
                continue
            files={p.relative_to(stage).as_posix():digest(p) for p in stage.rglob('*') if p.is_file()}
            metadata={'schemaVersion':1,'kind':'VeroScript module symbols','business':business,'version':versions[business],
                      'target':target,'libraries':records,'unavailable':unavailable,'files':files}
            (stage/MANIFEST).write_text(json.dumps(metadata,indent=2)+'\n')
            output=destination/'symbols'/name
            if output.exists():
                existing=verify_module_symbols(output,business,versions[business],target,peer_directory=destination)
                if existing!=metadata:raise ValueError('Refusing to replace an existing module symbols identity: '+str(output))
            else:
                output.parent.mkdir(parents=True,exist_ok=True)
                with tempfile.TemporaryDirectory(prefix='module-symbol-download-',dir=output.parent) as download:
                    pending=Path(download)/name
                    archive_tree(stage,pending)
                    verify_module_symbols(pending,business,versions[business],target,peer_directory=destination)
                    pending.replace(output)
            results[business]={'status':'produced','archive':str(output),'sha256':digest(output),'libraries':len(records),'unavailable':unavailable}
            shutil.rmtree(stage)
    return results

def assemble_module_symbols(repo,builds,destination,versions,target,selected,config='Release'):
    inputs=desktop_inputs(Path(repo),builds,Path(destination),target,selected,config)
    return assemble_module_symbol_inputs(repo,destination,versions,target,inputs)

def verify_module_symbols(path,business,version,target,*,peer_directory=None):
    path=Path(path)
    if path.name!=symbol_archive_name(business,version,target):raise ValueError('Module symbol filename/owner mismatch')
    entries=archive_entries(path,max_entry_bytes=1024*1024)
    prefix=path.name.removesuffix('.tar.gz').removesuffix('.zip')+'/'
    if any(not name.startswith(prefix) for name in entries):raise ValueError('Unexpected module symbol archive root')
    relative={name[len(prefix):]:value for name,value in entries.items()}
    try:metadata=json.loads(relative[MANIFEST][0])
    except (KeyError,ValueError) as error:raise ValueError('Missing module symbol manifest') from error
    if any(metadata.get(k)!=v for k,v in [('schemaVersion',1),('kind','VeroScript module symbols'),('business',business),('version',version),('target',target)]):
        raise ValueError('Module symbol manifest identity mismatch')
    files=metadata.get('files',{});records=metadata.get('libraries',[])
    from veroscript_foundation import archive_digests
    actual_hashes=archive_digests(path)
    peer_inventory={}
    if not records or not isinstance(files,dict) or set(relative)!=(set(files)|{MANIFEST}) or not relative.get('LICENSE'):
        raise ValueError('Empty or incomplete module symbol payload')
    symbols={name for r in records for name in r.get('symbols',[])}
    if not symbols or any(not name.startswith('symbols/') or name not in files for name in symbols):raise ValueError('Missing owned debug data')
    if {n for n in files if n.startswith('symbols/')}!=symbols:raise ValueError('Unowned extra debug payload')
    for name in symbols:
        if not name.endswith(('.a','.lib','.pdb','.debug')) and '.dSYM/' not in name:
            raise ValueError('Non-symbol payload in module debug archive: '+name)
    for name,expected in files.items():
        safe_member(name)
        if not re.fullmatch('[0-9a-f]{64}',expected) or actual_hashes.get(prefix+name)!=expected:
            raise ValueError('Stale/corrupt module debug inventory: '+name)
        if any(part.casefold() in {'docs','examples','tests'} or part.casefold().startswith('readme') for part in PurePosixPath(name).parts):
            raise ValueError('Documentation in module symbols')
    for record in records:
        if not record.get('symbols') or any(not re.fullmatch('[0-9a-f]{64}',record.get(k,'')) for k in ['sourceSha256','functionalSha256','functionalArchiveSha256']):
            raise ValueError('Unpaired module debug source')
        source_name=record.get('sourceName','');safe_member(source_name)
        if PurePosixPath(source_name).name!=source_name:raise ValueError('Unsafe module debug source name')
        kind=record.get('identity',{}).get('format')
        if kind not in {'elf','elf-static','macho','macho-static','pe','coff'}:
            raise ValueError('Missing native module symbol format')
        owner=record.get('functionalArchive','');safe_member(owner);safe_member(record['functionalEntry'])
        if PurePosixPath(owner).name!=owner:raise ValueError('Unsafe functional module archive name')
        from veroscript_business_release import artifact_business
        if artifact_business(owner)!=business and not (business=='Vm' and artifact_business(owner)=='CoreSDK'):
            raise ValueError('Module symbols refer to another business dependency')
        if kind in {'elf-static','macho-static','coff'}:
            originals=[n for n in record['symbols'] if PurePosixPath(n).name==record.get('sourceName')]
            if len(originals)!=1 or files[originals[0]]!=record['sourceSha256']:
                raise ValueError('Static symbols do not retain the complete original archive')
        if kind in {'elf','elf-static','macho-static'}:
            key='normalizedObjectsSha256' if kind=='macho-static' else 'normalizedCodeSha256'
            if not re.fullmatch('[0-9a-f]{64}',record['identity'].get(key,'')):raise ValueError('Missing paired native code fingerprint')
        if kind=='elf':
            identity=record['identity'];debug=[n for n in record['symbols'] if n.endswith('.debug')]
            if len(debug)!=1 or _build_id(relative[debug[0]][0])!=identity.get('buildId'):
                raise ValueError('ELF debug build-id mismatch')
        if kind=='macho':
            dwarf=[n for n in record['symbols'] if '/Contents/Resources/DWARF/' in n]
            if len(dwarf)!=1 or _macho_uuids(relative[dwarf[0]][0])!=record['identity'].get('uuids'):
                raise ValueError('Module dSYM UUID mismatch')
        if peer_directory is not None:
            peers=[p for p in Path(peer_directory).rglob(owner) if p.is_file()]
            if len(peers)!=1 or digest(peers[0])!=record['functionalArchiveSha256']:
                raise ValueError('Module debug symbols refer to a different functional archive')
            peer=peers[0]
            if peer not in peer_inventory:peer_inventory[peer]=_archive_inventory(peer)
            peer_entries,peer_hashes=peer_inventory[peer]
            if peer_hashes.get(record['functionalEntry'])!=record['functionalSha256']:
                raise ValueError('Module debug symbols refer to a different functional library')
            if kind=='elf' and _build_id(peer_entries[record['functionalEntry']][0])!=record['identity'].get('buildId'):
                raise ValueError('ELF functional/debug build-id mismatch')
            if kind in {'pe','coff'}:
                from veroscript_pdb import debug_references,pdb_identity
                if record['sourceSha256']!=record['functionalSha256']:
                    raise ValueError('Windows debug source does not match functional bytes')
                with tempfile.TemporaryDirectory(prefix='veroscript-pdb-verify-') as scratch:
                    scratch=Path(scratch);library=scratch/record['sourceName'];_extract_member(peer,record['functionalEntry'],library)
                    _check_target(library,target,record['slice']);references=debug_references(library);paired=[]
                    for index,name in enumerate(record['symbols']):
                        if name.endswith('.pdb'):
                            pdb=scratch/(str(index)+'.pdb');_extract_member(path,prefix+name,pdb);paired.append(pdb_identity(pdb))
                    if any(not any(p['guid']==r['guid'] and (kind=='coff' or p['age']==r['age']) for p in paired) for r in references):
                        raise ValueError('Functional Windows library/PDB GUID or age mismatch')
                    expected=record['identity'].get('pdbBindings',[])
                    if sorted((r['guid'],r['age']) for r in paired)!=sorted((r['guid'],r['age']) for r in expected):
                        raise ValueError('PDB identity differs from module symbol manifest')
                    if kind=='coff' and set((r['guid'],r['age']) for r in references)!=set((r['guid'],r['age']) for r in record['identity'].get('typeServerReferences',[])):
                        raise ValueError('Compiler PDB reference ages were not retained accurately')
    return metadata

def portable_symbol_status(status):
    return {business:{**value, **({'archive':'symbols/'+Path(value['archive']).name} if value.get('archive') else {})}
            for business,value in status.items()}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo-root',type=Path,required=True);p.add_argument('--build-directory',type=Path)
    p.add_argument('--candidate',type=Path,required=True);p.add_argument('--target',required=True)
    p.add_argument('--business',action='append',choices=MODULES);p.add_argument('--inputs',type=Path)
    p.add_argument('--objcopy');p.add_argument('--objdump');p.add_argument('--strip')
    p.add_argument('--status-output',type=Path,help='Owned .tmp/dist work JSON for portable module symbol availability')
    a=p.parse_args()
    status=None
    if a.status_output:
        from veroscript_release import safe_output,linked_path
        status=(a.repo_root/a.status_output).absolute()
        if status.suffix!='.json' or status.name.startswith(('release-manifest','release-index')):
            raise ValueError('Module symbol status requires a dedicated work JSON filename')
        status=safe_output(a.repo_root,status.parent,component=True)/status.name
        if linked_path(status):raise ValueError('Linked module symbol status output is forbidden')
    from veroscript_business_release import load_versions
    versions=load_versions(a.repo_root)['businesses']
    if a.inputs:
        value=json.loads(a.inputs.read_text());inputs=value.get('libraries',value) if isinstance(value,dict) else value
        if a.business:inputs=[r for r in inputs if r['business'] in a.business]
        result=assemble_module_symbol_inputs(a.repo_root,a.candidate,versions,a.target,inputs,tools={k:v for k,v in vars(a).items() if k in {'objcopy','objdump','strip'} and v})
    else:
        if not a.build_directory:p.error('--build-directory or --inputs is required')
        selected=a.business or list(MODULES)
        result=assemble_module_symbols(a.repo_root,{b:a.build_directory for b in selected},a.candidate,versions,a.target,selected)
    if status is not None:
        status.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=status.parent,prefix=status.name+'.',suffix='.tmp',delete=False) as stream:
            temporary=Path(stream.name);stream.write(json.dumps(portable_symbol_status(result),indent=2)+'\n')
        try:temporary.replace(status)
        finally:temporary.unlink(missing_ok=True)
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
