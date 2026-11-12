import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const config=JSON.parse(fs.readFileSync('cross-config.json','utf8'));
const records=[];
function run(exe,args){const r=spawnSync(exe,args,{encoding:'utf8',timeout:180000,maxBuffer:16*1024*1024,windowsHide:true});if(r.error||r.status!==0)throw Error(`${exe} ${args.join(' ')}: ${r.status} ${r.error||''} ${r.stderr} ${r.stdout}`);return r;}
for(const [name,[origin,n,seed,reference,batch]]of Object.entries(config)){
 try{
 const cpp=Number(run('build/Release/compare.exe',[name,''+n,''+seed,'once']).stdout);
 const node=Number(run(process.execPath,['compare.mjs',name,''+n,''+seed,'once']).stdout);
 if(cpp!==node||(reference!==null&&cpp!==reference))throw Error(`checksum disagreement ${cpp} ${node} ${reference}`);
 const expected=(cpp*batch)%1000000007;
 for(const backend of ['mixed','jit',...(origin==='holdout'?['vm']:[])]){
  const r=run('build/Release/cross.exe',[`cross/${name}.vs`,backend,''+batch,''+expected,backend==='vm'?'1':'7']);
  records.push({name,backend,stdout:r.stdout,stderr:r.stderr});console.log(name,backend,'ok');
 }
 for(const backend of ['cpp','node']){
  const r=backend==='cpp'?run('build/Release/compare.exe',[name,''+n,''+seed]):run(process.execPath,['compare.mjs',name,''+n,''+seed]);
  records.push({name,backend,stdout:r.stdout,stderr:r.stderr});console.log(name,backend,'ok');
 }
 }catch(e){records.push({name,error:String(e)});console.error(String(e));}
 fs.writeFileSync('cross-results.json',JSON.stringify(records,null,2));
}
if(records.some(r=>r.error))process.exitCode=1;
