import {performance} from 'node:perf_hooks';
const f={
modloop(n,seed){let total=seed;for(let i=0;i<n;i++)total=(total+i%97)%1000000007;return total;},
indexed(n,seed){const a=[.125,-.75,1.5,.0625,-1.25,.875,.375];let state=seed,v=.25;for(let i=0;i<n;i++){state=(state*25173+13849)%65521;v=v*.99991+a[state%7]*.00009;}return Math.trunc((v+4)*1000000)+state;},
indirect(n,seed){const a=[],indices=[];for(let i=0;i<257;i++){a.push((i*17+11)%101);indices.push((i*37+19)%257);}let state=seed,total=0;for(let i=0;i<n;i++){state=(state*25173+13849)%65521;const pos=(state+((i%257)*7))%257,idx=indices[pos],delta=(Math.trunc(state/7)%13)+1;a[idx]=(a[idx]+delta)%1000003;total=(total+a[idx]+(pos&3))%1000000007;}return(total+state+a[indices[state%257]])%1000000007;},
objects(n,seed){let state=seed,total=0;for(let i=0;i<n;i++){state=(state*25173+13849)%65521;const p=new Pair();p.first=state%127;p.second=Math.trunc(state/7)%127;total=(total+p.sum())%1000000007;}return total+state;},
virtuals(n,seed){const ops=[new Add(),new Xor()],order=[1,0,1,0];let state=seed,total=0;for(let i=0;i<n;i++){state=(state*75+74)%65521;const idx=order[(state+total)&3];total=ops[idx].step(total,state);}return total+state;},
maps(n,seed){const a=new Map();for(let i=0;i<1021;i++)a.set(i,(i%31)*.25+1);let state=seed,total=0;for(let i=0;i<n;i++){state=(state*75+74)%65521;const key=(state+i*13)%1021;let v=a.get(key)*1.000001+(state%7)*.25;if(v>10000)v*=.5;a.set(key,v);total+=v;}return Math.trunc(total)+state;},
strings(n,seed){const l=['alpha-000','beta-111','gamma-222','delta-333','epsilon-444','zeta-555','eta-666'],r=['alpha-000','beta-X11','gamma-222','delta-X33','epsilon-444','zeta-X55','eta-666'];let state=seed,total=0;for(let i=0;i<n;i++){state=(state*75+74)%65521;const a=state%7,b=(state+(i&3))%7;if(l[a]===r[b])total++;}return total+state;},
fib(n,seed){return fibimpl(n)+seed;},
sieve(n,seed){const a=new Array(n+1).fill(0);let count=seed;for(let p=2;p<=n;p++){if(a[p]===0){count++;if(p<=Math.trunc(n/p))for(let j=p*p;j<=n;j+=p)a[j]=1;}}return count;},
matrix(n,seed){const a=[],b=[],c=new Array(n*n).fill(0);for(let i=0;i<n*n;i++){a.push((i*17+seed)%97);b.push((i*31+7)%89);}for(let i=0;i<n;i++)for(let k=0;k<n;k++)for(let j=0;j<n;j++)c[i*n+j]=(c[i*n+j]+a[i*n+k]*b[k*n+j])%1000000007;let sum=0;for(const v of c)sum=(sum+v)%1000000007;return sum;},
escaping(n,seed){const a=[];for(let i=0;i<n;i++){const p=new Pair();p.first=(i+seed)&255;p.second=(i*7)&127;a.push(p);}let total=0;for(let i=n-1;i>=0;i--)total+=a[i].sum();return total;}
};
class Pair{first=0;second=0;sum(){return this.first+this.second;}}
class Add{step(v,salt){return(v+1+salt%3)%1000000007;}}
class Xor{step(v,salt){return((v^(salt&7))+7)%1000000007;}}
function fibimpl(n){return n<2?n:fibimpl(n-1)+fibimpl(n-2);}
const [name,ns,ss,mode]=process.argv.slice(2),n=Number(ns),seed=Number(ss),fn=f[name];
const expected=fn(n,seed);if(mode==='once'){console.log(expected);process.exit();}
let batch=1;const warmStart=performance.now();do{for(let i=0;i<batch;i++)globalThis.sink=fn(n,seed);batch=Math.min(batch*2,4096);}while(performance.now()-warmStart<250);
batch=1;for(;;){const t=performance.now();for(let i=0;i<batch;i++)globalThis.sink=fn(n,seed);if(performance.now()-t>=30||batch>=10000)break;batch*=2;}
for(let s=0;s<7;s++){const t=performance.now();for(let i=0;i<batch;i++){const v=fn(n,seed);if(v!==expected)throw Error('bad checksum');globalThis.sink=v;}console.log([name,'node',s,(performance.now()-t)/batch,batch,expected].join(','));}
