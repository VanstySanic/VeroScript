# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT
from pathlib import Path
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from veroscript_module_symbols import (assemble_module_symbol_inputs,verify_module_symbols,symbol_archive_name)
from veroscript_release import archive_tree,archive_entries,digest
import veroscript_module_symbols as symbols

@unittest.skipUnless(sys.platform.startswith('linux') and all(shutil.which(t) for t in ('cc','ar','strip','objcopy','objdump')), 'real ELF toolchain required')
class ModuleSymbolTests(unittest.TestCase):
    def setUp(self):
        self.owner=tempfile.TemporaryDirectory();self.root=Path(self.owner.name)
        self.repo=self.root/'repo';(self.repo/'.tmp').mkdir(parents=True);(self.repo/'LICENSE').write_text('Test project license\n')
        self.out=self.root/'candidate';self.out.mkdir();self.versions={'Json':'0.1.1'}
    def tearDown(self):self.owner.cleanup()
    def compile(self,value=42,debug=True,static=False):
        source=self.root/('source'+str(value)+'.c');source.write_text('int answer(void){return '+str(value)+';}\n')
        raw=self.root/('lib'+str(value)+('.a' if static else '.so'))
        command=['cc','-g' if debug else '-g0','-fPIC']
        if static:
            obj=source.with_suffix('.o');subprocess.run(command+['-c',str(source),'-o',str(obj)],check=True)
            subprocess.run(['ar','rcs',str(raw),str(obj)],check=True)
        else:subprocess.run(command+['-shared',str(source),'-Wl,--build-id','-o',str(raw)],check=True)
        return raw
    def peer(self,raw):
        stage=self.root/'vs-json-0.1.1-linux-x64';(stage/'lib').mkdir(parents=True)
        library=stage/'lib'/raw.name;shutil.copy2(raw,library);subprocess.run(['strip','--strip-debug',str(library)],check=True)
        (stage/'LICENSE').write_text('Test project license\n');archive=self.out/'modules'/(stage.name+'.tar.gz');archive_tree(stage,archive)
        return archive,stage.name+'/lib/'+raw.name
    def export(self,raw,peer=None,entry=None,**kwargs):
        if peer is None:peer,entry=self.peer(raw)
        row={'business':'Json','slice':'linux-x64','source':str(raw),'functionalArchive':str(peer),'functionalEntry':entry,'settings':{}}
        row.update(kwargs)
        return assemble_module_symbol_inputs(self.repo,self.out,self.versions,'linux-x64',[row])
    def test_real_dynamic_elf_pair_and_original_preserved(self):
        raw=self.compile();before=digest(raw);result=self.export(raw);self.assertEqual(result['Json']['status'],'produced');self.assertEqual(digest(raw),before)
        artifact=Path(result['Json']['archive']);metadata=verify_module_symbols(artifact,'Json','0.1.1','linux-x64',peer_directory=self.out)
        self.assertEqual(metadata['libraries'][0]['identity']['format'],'elf');self.assertTrue(metadata['libraries'][0]['identity']['buildId'])
        self.assertTrue(any(name.endswith('.debug') for name in metadata['files']))
    def test_static_archive_keeps_complete_original_debug_objects(self):
        raw=self.compile(static=True);result=self.export(raw);metadata=verify_module_symbols(Path(result['Json']['archive']),'Json','0.1.1','linux-x64',peer_directory=self.out)
        record=metadata['libraries'][0];self.assertEqual(record['identity']['format'],'elf-static');self.assertIn(digest(raw),metadata['files'].values())
    @unittest.skipUnless(shutil.which('cmake'),'CMake RPATH transformation required')
    def test_cmake_install_rpath_change_does_not_mismatch_debug_code(self):
        raw=self.compile();source=self.root/'source42.c';old='$ORIGIN:/original/build/location'
        subprocess.run(['cc','-g','-fPIC','-shared',str(source),'-Wl,--build-id','-Wl,-rpath,'+old,'-o',str(raw)],check=True)
        peer,entry=self.peer(raw);stage=self.root/'vs-json-0.1.1-linux-x64';functional=stage/'lib'/raw.name
        script=self.root/'install-rpath.cmake';script.write_text('file(RPATH_CHANGE FILE [==['+str(functional)+']==] OLD_RPATH [==['+old+']==] NEW_RPATH [==[$ORIGIN]==])\n')
        subprocess.run(['cmake','-P',str(script)],check=True);archive_tree(stage,peer)
        result=self.export(raw,peer,entry);self.assertEqual(result['Json']['status'],'produced')
    def test_android_aar_inventory_and_jni_symbol_pair(self):
        raw=self.compile();functional=self.root/'jni.so';shutil.copy2(raw,functional)
        subprocess.run(['strip','--strip-unneeded',str(functional)],check=True)
        peer=self.out/'mobile/vs-asmjit-0.1.1-android.aar';peer.parent.mkdir();entry='jni/x86_64/'+raw.name
        with zipfile.ZipFile(peer,'w') as archive:archive.writestr(entry,functional.read_bytes())
        result=assemble_module_symbol_inputs(self.repo,self.out,{'Jit':'0.1.1'},'android',[
            {'business':'Jit','abi':'x86_64','originalLibrary':str(raw),'finalLibrary':str(peer)+'!/'+entry,
             'finalSha256':digest(functional),'stripOption':'--strip-unneeded','settings':{}}])
        self.assertEqual(result['Jit']['status'],'produced')
        verify_module_symbols(Path(result['Jit']['archive']),'Jit','0.1.1','android',peer_directory=self.out)
    def test_mismatched_code_is_rejected(self):
        raw=self.compile();wrong=self.compile(value=43);peer,entry=self.peer(wrong)
        with self.assertRaisesRegex(ValueError,'do not match functional code'):self.export(raw,peer,entry)
    def test_no_debug_does_not_create_empty_distribution(self):
        result=self.export(self.compile(debug=False));self.assertEqual(result['Json']['status'],'not-available');self.assertFalse(list((self.out/'symbols').glob('*')))
    def test_changed_recorded_source_is_rejected(self):
        raw=self.compile()
        with self.assertRaisesRegex(ValueError,'Changed original'):self.export(raw,originalSha256='0'*64)
    def test_wrong_target_architecture_is_rejected(self):
        raw=self.compile();peer,entry=self.peer(raw)
        with self.assertRaisesRegex(ValueError,'Wrong module debug target'):
            assemble_module_symbol_inputs(self.repo,self.out,self.versions,'linux-arm64',[{'business':'Json','slice':'linux-arm64','source':str(raw),'functionalArchive':str(peer),'functionalEntry':entry,'settings':{}}])
    def test_tampered_symbol_payload_is_rejected(self):
        result=self.export(self.compile());artifact=Path(result['Json']['archive']);entries=archive_entries(artifact);scratch=self.root/'tampered';scratch.mkdir()
        for name,(data,mode) in entries.items():
            path=scratch/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data+(b'changed' if name.endswith('.debug') else b''));path.chmod(mode)
        root=next(scratch.iterdir());artifact.unlink();archive_tree(root,artifact)
        with self.assertRaisesRegex(ValueError,'Stale/corrupt'):verify_module_symbols(artifact,'Json','0.1.1','linux-x64',peer_directory=self.out)
    def test_repeated_exact_inputs_reuse_identical_archive(self):
        raw=self.compile();peer,entry=self.peer(raw);first=self.export(raw,peer,entry);second=self.export(raw,peer,entry);self.assertEqual(first['Json']['sha256'],second['Json']['sha256'])

class SymbolNamingTests(unittest.TestCase):
    def test_malformed_elf_search_path_bounds_are_rejected(self):
        import struct
        data=bytearray(512);data[:6]=b'\x7fELF\x02\x01';struct.pack_into('<Q',data,32,64);struct.pack_into('<HH',data,54,56,2)
        struct.pack_into('<I',data,64,1);struct.pack_into('<QQ',data,72,0,0x1000);struct.pack_into('<Q',data,96,512)
        struct.pack_into('<I',data,120,2);struct.pack_into('<QQ',data,128,176,0x10b0);struct.pack_into('<Q',data,152,64)
        for offset,pair in zip((176,192,208,224),((5,0x1100),(10,32),(29,0),(0,0))):struct.pack_into('<QQ',data,offset,*pair)
        data[256:261]=b'path\0';self.assertEqual(symbols._without_elf_search_paths(bytes(data))[256:261],b'\0'*5)
        invalid=[]
        outside=bytearray(data);struct.pack_into('<Q',outside,216,32);invalid.append(outside)
        unterminated=bytearray(data);unterminated[256:288]=b'x'*32;invalid.append(unterminated)
        truncated_segment=bytearray(data);struct.pack_into('<Q',truncated_segment,96,513);invalid.append(truncated_segment)
        incomplete_dynamic=bytearray(data);struct.pack_into('<Q',incomplete_dynamic,152,63);invalid.append(incomplete_dynamic)
        bad_width=bytearray(data);struct.pack_into('<H',bad_width,54,55);invalid.append(bad_width)
        for value in invalid:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):symbols._without_elf_search_paths(bytes(value))
    def test_existing_module_prefix_and_distribution_shape(self):
        self.assertEqual(symbol_archive_name('FontBackend','0.1.1','android'),'vs-font-backend-0.1.1-android-symbols.zip')
        self.assertEqual(symbol_archive_name('Jit','0.1.1','macos-arm64'),'vs-asmjit-0.1.1-macos-arm64-symbols.tar.gz')
        with self.assertRaises(ValueError):symbol_archive_name('CoreSDK','0.1.1','linux-x64')

class SymbolStatusOutputTests(unittest.TestCase):
    def run_cli(self,repo,status,*,must_not_export=False):
        result={'Json':{'status':'produced','archive':str(repo/'.tmp/candidate/symbols/vs-json-0.1.1-linux-x64-symbols.tar.gz'),'sha256':'a'*64}}
        command=['symbols','--repo-root',str(repo),'--build-directory',str(repo/'build'),'--candidate',str(repo/'.tmp/candidate'),'--target','linux-x64','--business','Json','--status-output',str(status)]
        with patch.object(sys,'argv',command),patch('veroscript_business_release.load_versions',return_value={'businesses':{'Json':'0.1.1'}}),patch.object(symbols,'assemble_module_symbols',return_value=result,side_effect=AssertionError('Export ran before output-path rejection') if must_not_export else None) as produce,patch('sys.stdout',new=io.StringIO()):
            symbols.main()
        return produce
    def test_work_json_contains_portable_status(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo=Path(temporary).resolve();status=repo/'.tmp/work/module-symbol-status.json'
            self.run_cli(repo,status)
            result=json.loads(status.read_text());self.assertEqual(result['Json']['archive'],'symbols/vs-json-0.1.1-linux-x64-symbols.tar.gz')
    def test_source_release_and_reserved_status_paths_are_rejected_before_export(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo=Path(temporary).resolve()
            for status in [repo/'Resources/status.json',repo/'Release/status.json',repo/'.tmp/work/release-manifest.json']:
                with self.subTest(path=status):
                    with self.assertRaises(ValueError):self.run_cli(repo,status,must_not_export=True)
                    self.assertFalse(status.exists())

if __name__=='__main__':unittest.main()
