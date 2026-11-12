import fs from'node:fs';import{spawnSync}from'node:child_process';const out=[];
for(let s=0;s<5;s++)for(const n of[100,1000,5000]){const r=spawnSync('build/Release/cross.exe',[`scale/compile-${n}.vs`,'mixed','1','10','1'],{encoding:'utf8',windowsHide:true,timeout:30000});if(r.status!==0)throw Error(r.stderr);out.push({n,s,phase:r.stderr});}
fs.writeFileSync('compile-repeat.json',JSON.stringify(out,null,2));console.log('compile repeat ok');

