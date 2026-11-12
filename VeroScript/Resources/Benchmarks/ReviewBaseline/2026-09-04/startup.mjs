import fs from 'node:fs';import {spawnSync}from'node:child_process';import{performance}from'node:perf_hooks';
const out=[];const cli='../../build/windows-msvc-vs2026/bin/Release/veroscript.exe';
const tests=[['cpp','build/Release/compare.exe',['modloop','0','1','once']],['node',process.execPath,['-e','console.log(1)']],...['vm','mixed','jit'].map(b=>['vero-'+b,cli,['--bc-cache=off','--backend='+b,'-e','print(1);']])];
for(let r=-1;r<11;r++){for(let j=0;j<tests.length;j++){const[name,exe,args]=tests[(j+r+tests.length)%tests.length];const t=performance.now();const p=spawnSync(exe,args,{encoding:'utf8',timeout:30000,windowsHide:true});const dt=performance.now()-t;if(p.status!==0||p.stdout.trim()!=='1')throw Error(name+': '+p.stderr+p.stdout);if(r>=0)out.push({name,sample:r,ms:dt});}}
fs.writeFileSync('startup-results.json',JSON.stringify(out,null,2));console.log('startup samples',out.length);

