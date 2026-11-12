import fs from 'node:fs';import{spawnSync}from'node:child_process';
const out=[];function run(exe,args){let r=spawnSync(exe,args,{encoding:'utf8',timeout:180000,windowsHide:true});if(r.status!==0)throw Error(r.stderr+r.stdout);return r;}
for(const n of [100,1000,5000])for(const backend of ['vm','mixed','jit']){const r=run('build/Release/cross.exe',[`scale/compile-${n}.vs`,backend,'1','10','3']);out.push({kind:'compile',n,backend,...r});fs.writeFileSync('scale-results.json',JSON.stringify(out,null,2));console.log('compile',n,backend,'ok');}
for(const n of [1000,10000,40000]){const expected=Number(run(process.execPath,['compare.mjs','escaping',''+n,'17','once']).stdout)*5;for(const backend of ['vm','mixed','jit']){const r=run('build/Release/cross.exe',[`scale/escaping-${n}.vs`,backend,'5',''+expected,'3']);out.push({kind:'escaping',n,backend,...r});fs.writeFileSync('scale-results.json',JSON.stringify(out,null,2));console.log('escaping',n,backend,'ok');}}

